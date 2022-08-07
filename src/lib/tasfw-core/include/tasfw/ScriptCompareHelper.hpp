#pragma once
#include <tasfw/ScriptStatus.hpp>
#include <tasfw/SharedLib.hpp>

#ifndef SCRIPT_COMPARE_HELPER_H
#define SCRIPT_COMPARE_HELPER_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <typename F>
auto AdhocCompareScript_impl = [](auto... params) constexpr -> void
{
	static_assert(std::same_as < std::invoke_result_t<F, decltype(params)...>, bool>,
		"Ad-hoc script not constructible from supplied parameters");
};

template <typename F, class TCompareStatus, class TTuple>
concept AdhocCompareScript = requires (TCompareStatus* status, TTuple& params)
{
	std::apply(AdhocCompareScript_impl<F>, std::tuple_cat(std::tuple(status), params));
};

template <typename F, typename TScript, typename TTuple>
concept ScriptParamsGenerator = requires
{
	derived_from_specialization_of<TScript, Script>;
	constructible_from_tuple<TScript, TTuple>;
	std::same_as<std::invoke_result_t<F, int64_t, TTuple&>, bool>;
};

template <typename F, typename TScript>
concept ScriptComparator = requires 
{
	derived_from_specialization_of<TScript, Script>;
	std::same_as<std::invoke_result_t<F, int64_t, const ScriptStatus<TScript>*, const ScriptStatus<TScript>*>, const ScriptStatus<TScript>*>;
};

template <typename F, typename TScript>
concept ScriptTerminator = requires
{
	derived_from_specialization_of<TScript, Script>;
	std::same_as<std::invoke_result_t<F, int64_t, const ScriptStatus<TScript>*>, bool>;
};

template <typename F, typename TCompareStatus>
concept AdhocScriptComparator = requires
{
	std::same_as<std::invoke_result_t<F, int64_t, const AdhocScriptStatus<TCompareStatus>*, const AdhocScriptStatus<TCompareStatus>*>, const AdhocScriptStatus<TCompareStatus>*>;
};

template <typename F, typename TCompareStatus>
concept AdhocScriptTerminator = requires
{
	std::same_as<std::invoke_result_t<F, int64_t, const AdhocScriptStatus<TCompareStatus>*>, bool>;
};

template <derived_from_specialization_of<Script> TScript>
class Substatus
{
public:
	int64_t nMutations = 0;
	ScriptStatus<TScript> substatus;

	Substatus(int64_t nMutations, ScriptStatus<TScript> substatus) : nMutations(nMutations), substatus(substatus) { }
};

template <derived_from_specialization_of<Resource> TResource>
class ScriptCompareHelper
{
public:
    Script<TResource>* script;

    ScriptCompareHelper(Script<TResource>* script) : script(script) { }

	template <class TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F&& comparator, G terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		status1 = ExecuteFromTuple<TScript>(*(paramsList.begin()));
		if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
			return status1;

		bool first = true;
		for (const auto& params : paramsList)
		{
			// We already ran the first set of parameters
			if (first)
			{
				first = false;
				continue;
			}

			iteration++;
			ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				return status2;

			SelectStatus(std::forward<F>(comparator), iteration, status1, status2);
		}

		return status1;
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TScript, TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams<TScript>(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		status1 = ExecuteFromTuple<TScript>(params);
		if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
			return status1;

		while (GenerateParams<TScript>(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				return status2;

			SelectStatus(std::forward<G>(comparator), iteration, status1, status2);
		}

		return status1;
	}

	template <class TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F&& comparator, G terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t initialFrame = script->GetCurrentFrame();
		int64_t iteration = 0;
		bool terminate = false;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		// We want to avoid reversion of successful script
		terminate = script->ExecuteAdhocBase([&]()
			{
				status1 = ModifyFromTuple<TScript>(*(paramsList.begin()));

				if (!status1.asserted)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed;
			}).executed;

		// Handle reversion/application of last script run
		if (terminate)
		{
			script->ApplyChildDiff(status1, script->saveBank[script->_adhocLevel + 1], initialFrame);
			return status1;
		}
		else
			script->Revert(initialFrame, status1.m64Diff, script->saveBank[script->_adhocLevel + 1]);

		bool first = true;
		for (const auto& params : paramsList)
		{
			// We already ran the first set of parameters
			if (first)
			{
				first = false;
				continue;
			}

			// We want to avoid reversion of successful script
			iteration++;
			bool newIncumbent = false;
			terminate = script->ExecuteAdhocBase([&]()
				{
					status2 = ModifyFromTuple<TScript>(params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
						return true;

					newIncumbent = SelectStatus(std::forward<F>(comparator), iteration, status1, status2);
					return false;
				}).executed;

			// Don't revert if best script is the last one to be run
			if (terminate || (status1.asserted && (&params == &*std::prev(paramsList.end())) && newIncumbent))
			{
				script->ApplyChildDiff(status2, script->saveBank[script->_adhocLevel + 1], initialFrame);
				return status2;
			}
			else
				script->Revert(initialFrame, status2.m64Diff, script->saveBank[script->_adhocLevel + 1]);
		}

		// If best script was from earlier, apply it
		if (status1.asserted)
			script->Apply(status1.m64Diff);

		return status1;
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TScript, TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t initialFrame = script->GetCurrentFrame();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams<TScript>(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ExecuteAdhocBase([&]()
			{
				status1 = ModifyFromTuple<TScript>(params);

				if (!status1.asserted)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed;
			}).executed;

		// Handle reversion/application of last script run
		if (terminate)
		{
			script->ApplyChildDiff(status1, script->saveBank[script->_adhocLevel + 1], initialFrame);
			return status1;
		}
		else
			script->Revert(initialFrame, status1.m64Diff, script->saveBank[script->_adhocLevel + 1]);

		bool nextParamsGenerated = false;
		bool lastParams = false;
		while (nextParamsGenerated || GenerateParams<TScript>(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			// We want to avoid reversion of successful script
			bool newIncumbent = false;
			nextParamsGenerated = false;
			terminate = script->ExecuteAdhocBase([&]()
				{
					status2 = ModifyFromTuple<TScript>(params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
						return true;

					newIncumbent = SelectStatus(std::forward<G>(comparator), iteration, status1, status2);

					//avoid calling params generator twice per iteration
					lastParams = !GenerateParams<TScript>(std::forward<F>(paramsGenerator), ++iteration, params);
					nextParamsGenerated = true;

					return false;
				}).executed;

			// Don't revert if best script is the last one to be run
			if (terminate || (status1.asserted && lastParams && newIncumbent))
			{
				script->ApplyChildDiff(status2, script->saveBank[script->_adhocLevel + 1], initialFrame);
				return status2;
			}
			else
				script->Revert(initialFrame, status2.m64Diff, script->saveBank[script->_adhocLevel + 1]);
		}

		// If best script was from earlier, apply it
		if (status1.asserted)
			script->Apply(status1.m64Diff);

		return status1;
	}

	template <class TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		M64Diff incumbentDiff;

		auto baseStatus = script->ExecuteAdhoc([&]()
		{
			// return if container is empty
			if (paramsList.begin() == paramsList.end())
				return false;

			status1 = ExecuteFromTuple<TScript>(*(paramsList.begin()));
			if (status1.asserted)
				incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

			if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
				return true;

			bool first = true;
			for (const auto& params : paramsList)
			{
				// We already ran the first set of parameters
				if (first)
				{
					first = false;
					continue;
				}

				// Mutate state before next iteration. If mutation fails, stop execution
				if (!script->ModifyAdhoc(std::forward<F>(mutator)).executed)
					return status1.asserted;
				nMutations++;

				iteration++;
				ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
				if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				{
					status1 = status2;
					return true;
				}

				// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
				if (SelectStatus(std::forward<G>(comparator), iteration, status1, status2))
					incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel].m64Diff, status1.m64Diff);
			}

			return status1.asserted;
		});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(nMutations, status1));
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TScript, TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (derived_from_specialization_of<TScript, Script>&& constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		M64Diff incumbentDiff;
		TTuple params;

		auto baseStatus = script->ExecuteAdhoc([&]()
			{
				// return if no params
				if (!GenerateParams<TScript>(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				status1 = ExecuteFromTuple<TScript>(params);
				if (status1.asserted)
					incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

				if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
					return true;

				while (GenerateParams<TScript>(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.asserted;
					nMutations++;

					iteration++;
					ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
					if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
					{
						status1 = status2;
						return true;
					}

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (SelectStatus(std::forward<H>(comparator), iteration, status1, status2))
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel].m64Diff, status1.m64Diff);
				}

				return status1.asserted;
			});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(nMutations, status1));
	}

	template <class TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script>&& constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t initialFrame = script->GetCurrentFrame();
		int64_t iteration = 0;
		bool terminate = false;
		int64_t nMutations = 0;
		M64Diff incumbentDiff;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (paramsList.begin() == paramsList.end())
					return false;

				// We want to avoid reversion of successful script
				terminate = script->ExecuteAdhocBase([&]()
					{
						status1 = ModifyFromTuple<TScript>(*(paramsList.begin()));
						if (status1.asserted)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.asserted)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
				{
					script->ApplyChildDiff(status1, script->saveBank[script->_adhocLevel + 1], initialFrame);
					return true;
				}
				else
					script->Revert(initialFrame, status1.m64Diff, script->saveBank[script->_adhocLevel + 1]);

				bool first = true;
				for (const auto& params : paramsList)
				{
					// We already ran the first set of parameters
					if (first)
					{
						first = false;
						continue;
					}

					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<F>(mutator)).executed)
						return status1.asserted;
					nMutations++;

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					terminate = script->ExecuteAdhocBase([&]()
						{
							status2 = ModifyFromTuple<TScript>(params);

							if (!status2.asserted)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							newIncumbent = SelectStatus(std::forward<G>(comparator), iteration, status1, status2);

							return false;
						}).executed;

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (newIncumbent)
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel].m64Diff, status1.m64Diff);

					// Don't revert if best script is the last one to be run
					if (terminate || (status1.asserted && (&params == &*std::prev(paramsList.end())) && newIncumbent))
					{
						script->ApplyChildDiff(status2, script->saveBank[script->_adhocLevel + 1], initialFrame);
						return true;
					}
					else
						script->Revert(initialFrame, status2.m64Diff, script->saveBank[script->_adhocLevel + 1]);
				}

				// If best script was from earlier, apply it
				if (status1.asserted)
					script->Apply(incumbentDiff);

				return status1.asserted;
			});

		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(nMutations, status1));
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TScript, TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (derived_from_specialization_of<TScript, Script>&& constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t initialFrame = script->GetCurrentFrame();
		int64_t iteration = 0;
		bool terminate = false;
		int64_t nMutations = 0;
		M64Diff incumbentDiff;
		TTuple params;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (!GenerateParams<TScript>(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				// We want to avoid reversion of successful script
				terminate = script->ExecuteAdhocBase([&]()
					{
						status1 = ModifyFromTuple<TScript>(params);
						if (status1.asserted)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.asserted)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
				{
					script->ApplyChildDiff(status1, script->saveBank[script->_adhocLevel + 1], initialFrame);
					return true;
				}
				else
					script->Revert(initialFrame, status1.m64Diff, script->saveBank[script->_adhocLevel + 1]);

				bool nextParamsGenerated = false;
				bool lastParams = false;
				while (nextParamsGenerated || GenerateParams<TScript>(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.asserted;
					nMutations++;

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					nextParamsGenerated = false;
					terminate = script->ExecuteAdhocBase([&]()
						{
							status2 = ModifyFromTuple<TScript>(params);

							if (!status2.asserted)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							newIncumbent = SelectStatus(std::forward<G>(comparator), iteration, status1, status2);

							//avoid calling params generator twice per iteration
							lastParams = !GenerateParams<TScript>(std::forward<F>(paramsGenerator), iteration + 1, params);
							nextParamsGenerated = true;

							return false;
						}).executed;

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (newIncumbent)
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel].m64Diff, status1.m64Diff);

					// Don't revert if best script is the last one to be run
					if (terminate || (status1.asserted && lastParams && newIncumbent))
					{
						script->ApplyChildDiff(status2, script->saveBank[script->_adhocLevel + 1], initialFrame);
						return true;
					}
					else
						script->Revert(initialFrame, status2.m64Diff, script->saveBank[script->_adhocLevel + 1]);
				}

				// If best script was from earlier, apply it
				if (status1.asserted)
					script->Apply(incumbentDiff);

				return status1.asserted;
			});

		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(nMutations, status1));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		status1 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), *(paramsList.begin()));
		if (status1.executed && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
			return status1;

		bool first = true;
		for (const auto& params : paramsList)
		{
			// We already ran the first set of parameters
			if (first)
			{
				first = false;
				continue;
			}

			iteration++;
			AdhocScriptStatus<TCompareStatus> status2 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), params);
			if (status2.executed && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				return status2;

			SelectStatusAdhoc(std::forward<G>(comparator), iteration, status1, status2);
		}

		return status1;
	}

private:
	M64Diff MergeDiffs(const M64Diff& diff1, const M64Diff& diff2)
	{
		M64Diff newDiff;
		newDiff.frames.insert(diff1.frames.begin(), diff1.frames.end());
		newDiff.frames.insert(diff2.frames.begin(), diff2.frames.end());

		return newDiff;
	}

	template <class TScript, typename TTuple>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ExecuteFromTuple(TTuple& params)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Execute<TScript>(std::forward<Ts>(p)...);
		};

		return std::apply(executeFromTuple, params);
	}

	template <typename TCompareStatus, typename TTuple, AdhocCompareScript<TCompareStatus, TTuple> F>
	AdhocScriptStatus<TCompareStatus> ExecuteFromTupleAdhoc(F adhocScript, TTuple& params)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTupleAdhoc = [&]<typename... Ts>(Ts&&... p) -> AdhocBaseScriptStatus
		{
			return script->template ExecuteAdhoc([&]() { return adhocScript(std::forward<Ts>(p)...); });
		};

		TCompareStatus compareStatus = TCompareStatus();
		auto baseStatus = std::apply(executeFromTupleAdhoc, std::tuple_cat(std::tuple(&compareStatus), params));

		return AdhocScriptStatus<TCompareStatus>(baseStatus, compareStatus);
	}

	template <class TScript, typename TTuple>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyFromTuple(TTuple& params)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Modify<TScript>(std::forward<Ts>(p)...);
		};

		return std::apply(executeFromTuple, params);
	}

	template <class TScript, typename TTuple, ScriptParamsGenerator<TScript, TTuple> F>
	bool GenerateParams(F paramsGenerator, int64_t iteration, TTuple& params)
	{
		return script->ExecuteAdhoc([&]()
			{
				return paramsGenerator(iteration, params);
			}).executed;
	}

	template <class TScript, ScriptComparator<TScript> F>
	bool SelectStatus(F comparator, int64_t iteration, ScriptStatus<TScript>& status1, ScriptStatus<TScript>& status2)
	{
		// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
		return script->ExecuteAdhoc([&]()
			{
				//Default to status1 if status2 was not successful
				if (!status2.asserted)
					return true;

				//Default to status2 if it was successful and status1 was not
				if (!status1.asserted && status2.asserted)
				{
					status1 = status2;
					return true;
				}

				bool newIncumbent = comparator(iteration, &status1, &status2) == &status2;
				if (newIncumbent)
					status1 = status2;

				return newIncumbent;
			}).executed;
	}

	template <class TCompareStatus, AdhocScriptComparator<TCompareStatus> F>
	bool SelectStatusAdhoc(F comparator, int64_t iteration, AdhocScriptStatus<TCompareStatus>& status1, AdhocScriptStatus<TCompareStatus>& status2)
	{
		// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
		return script->ExecuteAdhoc([&]()
			{
				//Default to status1 if status2 was not successful
				if (!status2.executed)
					return true;

				//Default to status2 if it was successful and status1 was not
				if (!status1.executed && status2.executed)
				{
					status1 = status2;
					return true;
				}

				bool newIncumbent = comparator(iteration, &status1, &status2) == &status2;
				if (newIncumbent)
					status1 = status2;

				return newIncumbent;
			}).executed;
	}
};

#endif
