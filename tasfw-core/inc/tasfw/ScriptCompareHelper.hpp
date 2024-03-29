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
	static_assert(std::same_as<std::invoke_result_t<F, decltype(params)...>, bool>,
		"Ad-hoc script not constructible from supplied parameters");
};

template <typename F, class TCompareStatus, class TTuple>
concept AdhocCompareScript = requires (TCompareStatus* status, TTuple& params)
{
	std::apply(AdhocCompareScript_impl<F>, std::tuple_cat(std::tuple(status), params));
};

template <typename F, typename TTuple>
concept ScriptParamsGenerator = requires
{
	std::same_as<std::invoke_result_t<F, int64_t, TTuple&>, bool>;
};

template <typename F, typename TScript>
concept ScriptComparator = requires 
{
	derived_from_specialization_of<TScript, Script>;
	std::same_as<std::invoke_result_t<F, const ScriptStatus<TScript>*, const ScriptStatus<TScript>*>, const ScriptStatus<TScript>*>;
};

template <typename F, typename TScript>
concept ScriptTerminator = requires
{
	derived_from_specialization_of<TScript, Script>;
	std::same_as<std::invoke_result_t<F, const ScriptStatus<TScript>*>, bool>;
};

template <typename F, typename TCompareStatus>
concept AdhocScriptComparator = requires
{
	std::same_as<std::invoke_result_t<F, const AdhocScriptStatus<TCompareStatus>*, const AdhocScriptStatus<TCompareStatus>*>, const AdhocScriptStatus<TCompareStatus>*>;
};

template <typename F, typename TCompareStatus>
concept AdhocScriptTerminator = requires
{
	std::same_as<std::invoke_result_t<F, const AdhocScriptStatus<TCompareStatus>*>, bool>;
};

template <derived_from_specialization_of<Script> TScript>
class Substatus
{
public:
	int64_t nMutations = 0;
	ScriptStatus<TScript> substatus;

	Substatus(int64_t nMutations, ScriptStatus<TScript> substatus) : nMutations(nMutations), substatus(substatus) { }
};

template <class TCompareStatus>
class AdhocSubstatus
{
public:
	int64_t nMutations = 0;
	AdhocScriptStatus<TCompareStatus> substatus;

	AdhocSubstatus(int64_t nMutations, AdhocScriptStatus<TCompareStatus> substatus) : nMutations(nMutations), substatus(substatus) { }
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
		if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
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
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
				return status2;

			SelectStatus(std::forward<F>(comparator), status1, status2);
		}

		return status1;
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		status1 = ExecuteFromTuple<TScript>(params);
		if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
			return status1;

		while (GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
				return status2;

			SelectStatus(std::forward<G>(comparator), status1, status2);
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
		int64_t iteration = 0;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ModifyAdhoc([&]()
			{
				status1 = ModifyFromTuple<TScript>(*(paramsList.begin()));

				if (!status1.asserted)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
			}).executed;

		// Handle reversion/application of last script run
		if (terminate)
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

			// We want to avoid reversion of successful script
			iteration++;
			bool newIncumbent = false;
			terminate = script->ModifyAdhoc([&]()
				{
					status2 = ModifyFromTuple<TScript>(params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
						return true;

					newIncumbent = SelectStatus(std::forward<F>(comparator), status1, status2);
					return (&params == &*std::prev(paramsList.end())) && newIncumbent;
				}).executed;

			// Don't revert if best script is the last one to be run
			if (terminate)
				return status2;
		}

		// If best script was from earlier, apply it
		if (status1.asserted)
			script->Apply(status1.m64Diff);

		return status1;
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator, H terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ModifyAdhoc([&]()
			{
				status1 = ModifyFromTuple<TScript>(params);

				if (!status1.asserted)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
			}).executed;

		// Handle reversion/application of last script run
		if (terminate)
			return status1;

		bool nextParamsGenerated = false;
		bool lastParams = false;
		while ((nextParamsGenerated && !lastParams) || GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			// We want to avoid reversion of successful script
			bool newIncumbent = false;
			nextParamsGenerated = false;
			terminate = script->ModifyAdhoc([&]()
				{
					status2 = ModifyFromTuple<TScript>(params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
						return true;

					newIncumbent = SelectStatus(std::forward<G>(comparator), status1, status2);

					//avoid calling params generator twice per iteration
					lastParams = !GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params);
					nextParamsGenerated = true;

					return lastParams && newIncumbent;
				}).executed;

			if (terminate)
				return status2;
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
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;

		auto baseStatus = script->ExecuteAdhoc([&]()
		{
			// return if container is empty
			if (paramsList.begin() == paramsList.end())
				return false;

			status1 = ExecuteFromTuple<TScript>(*(paramsList.begin()));
			if (status1.asserted)
				incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

			if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
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
				if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
				{
					status1 = status2;
					return true;
				}

				// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
				if (SelectStatus(std::forward<G>(comparator), status1, status2))
				{
					incumbentMutations = nMutations;
					incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
				}
			}

			return status1.asserted;
		});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(incumbentMutations, status1));
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (derived_from_specialization_of<TScript, Script>&& constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;
		TTuple params;

		auto baseStatus = script->ExecuteAdhoc([&]()
			{
				// return if no params
				if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				status1 = ExecuteFromTuple<TScript>(params);
				if (status1.asserted)
					incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

				if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
					return true;

				while (GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.asserted;
					nMutations++;

					iteration++;
					ScriptStatus<TScript> status2 = ExecuteFromTuple<TScript>(params);
					if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
					{
						status1 = status2;
						return true;
					}

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (SelectStatus(std::forward<H>(comparator), status1, status2))
					{
						incumbentMutations = nMutations;
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
					}
				}

				return status1.asserted;
			});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(incumbentMutations, status1));
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
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;
		bool applyIncumbentDiff = false;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (paramsList.begin() == paramsList.end())
					return false;

				// We want to avoid reversion of successful script
				bool terminate = script->ModifyAdhoc([&]()
					{
						status1 = ModifyFromTuple<TScript>(*(paramsList.begin()));
						if (status1.asserted)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.asserted)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
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

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					terminate = script->ModifyAdhoc([&]()
						{
							status2 = ModifyFromTuple<TScript>(params);

							if (!status2.asserted)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
							newIncumbent = SelectStatus(std::forward<G>(comparator), status1, status2);
							if (newIncumbent)
							{
								incumbentMutations = nMutations;
								incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
							}

							return (&params == &*std::prev(paramsList.end())) && newIncumbent;
						}).executed;

					if (terminate)
						return true;
				}
				
				if (status1.asserted)
					applyIncumbentDiff = true;

				return status1.asserted;
			});

		// If best script was from earlier, apply it
		if (applyIncumbentDiff)
			script->Apply(incumbentDiff);

		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(incumbentMutations, status1));
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (derived_from_specialization_of<TScript, Script>&& constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I terminator)
	{
		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;
		TTuple params;
		bool applyIncumbentDiff = false;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				// We want to avoid reversion of successful script
				bool terminate = script->ModifyAdhoc([&]()
					{
						status1 = ModifyFromTuple<TScript>(params);
						if (status1.asserted)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.asserted)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
					return true;

				bool nextParamsGenerated = false;
				bool lastParams = false;
				while ((nextParamsGenerated && !lastParams) || GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.asserted;
					nMutations++;

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					nextParamsGenerated = false;
					terminate = script->ModifyAdhoc([&]()
						{
							status2 = ModifyFromTuple<TScript>(params);

							if (!status2.asserted)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
							newIncumbent = SelectStatus(std::forward<G>(comparator), status1, status2);
							if (newIncumbent)
							{
								incumbentMutations = nMutations;
								incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
							}	

							//avoid calling params generator twice per iteration
							lastParams = !GenerateParams(std::forward<F>(paramsGenerator), iteration + 1, params);
							nextParamsGenerated = true;
							return lastParams && newIncumbent;
						}).executed;

					if (terminate)
						return true;
				}
				
				if (status1.asserted)
					applyIncumbentDiff = true;

				return status1.asserted;
			});

		// If best script was from earlier, apply it
		if (applyIncumbentDiff)
			script->Apply(incumbentDiff);

		return AdhocScriptStatus<Substatus<TScript>>(baseStatus, Substatus<TScript>(incumbentMutations, status1));
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
		if (status1.executed && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
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
			if (status2.executed && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
				return status2;

			SelectStatusAdhoc(std::forward<G>(comparator), status1, status2);
		}

		return status1;
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		status1 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);
		if (status1.executed && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
			return status1;

		while (GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			AdhocScriptStatus<TCompareStatus> status2 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);
			if (status2.executed && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
				return status2;

			SelectStatusAdhoc(std::forward<H>(comparator), status1, status2);
		}

		return status1;
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		AdhocScriptStatus<TCompareStatus> status2 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ModifyAdhoc([&]()
			{
				status1 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), *(paramsList.begin()));

				if (!status1.executed)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
			}).executed;

		if (terminate)
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

			// We want to avoid reversion of successful script
			iteration++;
			bool newIncumbent = false;
			terminate = script->ModifyAdhoc([&]()
				{
					status2 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), params);

					if (!status2.executed)
						return false;

					terminate = script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed;
					if (terminate)
						return true;

					newIncumbent = SelectStatusAdhoc(std::forward<G>(comparator), status1, status2);
					return &params == &*std::prev(paramsList.end()) && newIncumbent;
				}).executed;

			if (terminate)
				return status2;
		}

		// If best script was from earlier, apply it
		if (status1.executed)
			script->Apply(status1.m64Diff);

		return status1;
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		AdhocScriptStatus<TCompareStatus> status2 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ModifyAdhoc([&]()
			{
				status1 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);

				if (!status1.executed)
					return false;

				return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
			}).executed;

		if (terminate)
			return status1;

		bool nextParamsGenerated = false;
		bool lastParams = false;
		while ((nextParamsGenerated && !lastParams) || GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
		{
			// We want to avoid reversion of successful script
			bool newIncumbent = false;
			nextParamsGenerated = false;
			terminate = script->ModifyAdhoc([&]()
				{
					status2 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);

					if (!status2.executed)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
						return true;

					newIncumbent = SelectStatusAdhoc(std::forward<H>(comparator), status1, status2);

					//avoid calling params generator twice per iteration
					lastParams = !GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params);
					nextParamsGenerated = true;
					return lastParams && newIncumbent;
				}).executed;

			if (terminate)
				return status2;
		}

		// If best script was from earlier, apply it
		if (status1.executed)
			script->Apply(status1.m64Diff);

		return status1;
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;

		auto baseStatus = script->ExecuteAdhoc([&]()
			{
				// return if container is empty
				if (paramsList.begin() == paramsList.end())
					return false;

				status1 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), *(paramsList.begin()));
				if (status1.executed)
					incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

				if (status1.executed && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
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
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.executed;
					nMutations++;

					iteration++;
					AdhocScriptStatus<TCompareStatus> status2 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), params);
					if (status2.executed && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
					{
						status1 = status2;
						return true;
					}

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (SelectStatusAdhoc(std::forward<H>(comparator), status1, status2))
					{
						incumbentMutations = nMutations;
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
					}
				}

				return status1.executed;
			});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<AdhocSubstatus<TCompareStatus>>(baseStatus, AdhocSubstatus<TCompareStatus>(incumbentMutations, status1));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;
		TTuple params;

		auto baseStatus = script->ExecuteAdhoc([&]()
			{
				// return if no params
				if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				status1 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);
				if (status1.executed)
					incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

				if (status1.executed && script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed)
					return true;

				while (GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<H>(mutator)).executed)
						return status1.executed;
					nMutations++;

					iteration++;
					AdhocScriptStatus<TCompareStatus> status2 = ExecuteFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);
					if (status2.executed && script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
					{
						status1 = status2;
						return true;
					}

					// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
					if (SelectStatusAdhoc(std::forward<I>(comparator), status1, status2))
					{
						incumbentMutations = nMutations;
						incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
					}
						
				}

				return status1.executed;
			});

		baseStatus.m64Diff = incumbentDiff;
		return AdhocScriptStatus<AdhocSubstatus<TCompareStatus>>(baseStatus, AdhocSubstatus<TCompareStatus>(incumbentMutations, status1));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		AdhocScriptStatus<TCompareStatus> status2 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		int64_t incumbentMutations = 0;
		M64Diff incumbentDiff;
		bool applyIncumbentDiff = false;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (paramsList.begin() == paramsList.end())
					return false;

				// We want to avoid reversion of successful script
				bool terminate = script->ModifyAdhoc([&]()
					{
						status1 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), *(paramsList.begin()));
						if (status1.executed)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.executed)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
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
					if (!script->ModifyAdhoc(std::forward<G>(mutator)).executed)
						return status1.executed;
					nMutations++;

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					terminate = script->ModifyAdhoc([&]()
						{
							status2 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<F>(adhocScript), params);

							if (!status2.executed)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
							newIncumbent = SelectStatusAdhoc(std::forward<H>(comparator), status1, status2);
							if (newIncumbent)
							{
								incumbentMutations = nMutations;
								incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
							}	

							return (&params == &*std::prev(paramsList.end())) && newIncumbent;
						}).executed;

					if (terminate)
						return true;
				}

				if (status1.executed)
					applyIncumbentDiff = true;

				return status1.executed;
			});

		// If best script was from earlier, apply it
		if (applyIncumbentDiff)
			script->Apply(incumbentDiff);

		return AdhocScriptStatus<AdhocSubstatus<TCompareStatus>>(baseStatus, AdhocSubstatus<TCompareStatus>(incumbentMutations, status1));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J terminator)
	{
		AdhocScriptStatus<TCompareStatus> status1 = AdhocScriptStatus<TCompareStatus>();
		AdhocScriptStatus<TCompareStatus> status2 = AdhocScriptStatus<TCompareStatus>();
		int64_t iteration = 0;
		int64_t nMutations = 0;
		M64Diff incumbentDiff;
		int64_t incumbentMutations = 0;
		TTuple params;
		bool applyIncumbentDiff = false;

		auto baseStatus = script->ModifyAdhoc([&]()
			{
				// return if container is empty
				if (!GenerateParams(std::forward<F>(paramsGenerator), iteration, params))
					return false;

				// We want to avoid reversion of successful script
				bool terminate = script->ModifyAdhoc([&]()
					{
						status1 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);
						if (status1.executed)
							incumbentDiff.frames.insert(status1.m64Diff.frames.begin(), status1.m64Diff.frames.end());

						if (!status1.executed)
							return false;

						return script->ExecuteAdhoc([&]() { return terminator(&status1); }).executed;
					}).executed;

				// Handle reversion/application of last script run
				if (terminate)
					return true;

				bool nextParamsGenerated = false;
				bool lastParams = false;
				while ((nextParamsGenerated && !lastParams) || GenerateParams(std::forward<F>(paramsGenerator), ++iteration, params))
				{
					// Mutate state before next iteration. If mutation fails, stop execution
					if (!script->ModifyAdhoc(std::forward<H>(mutator)).executed)
						return status1.executed;
					nMutations++;

					// We want to avoid reversion of successful script
					iteration++;
					bool newIncumbent = false;
					nextParamsGenerated = false;
					terminate = script->ModifyAdhoc([&]()
						{
							status2 = ModifyFromTupleAdhoc<TCompareStatus>(std::forward<G>(adhocScript), params);

							if (!status2.executed)
								return false;

							if (script->ExecuteAdhoc([&]() { return terminator(&status2); }).executed)
							{
								status1 = status2;
								return true;
							}

							// If new status is best, construct diff from accumulated mutations and apply the new diff on top of that. Don't execute anything
							newIncumbent = SelectStatusAdhoc(std::forward<I>(comparator), status1, status2);
							if (newIncumbent)
							{
								incumbentMutations = nMutations;
								incumbentDiff = MergeDiffs(script->BaseStatus[script->_adhocLevel - 1].m64Diff, status1.m64Diff);
							}	

							//avoid calling params generator twice per iteration
							lastParams = !GenerateParams(std::forward<F>(paramsGenerator), iteration + 1, params);
							nextParamsGenerated = true;
							return lastParams && newIncumbent;
						}).executed;

					if (terminate)
						return true;
				}

				if (status1.executed)
					applyIncumbentDiff = true;

				return status1.executed;
			});

		// If best script was from earlier, apply it
		if (applyIncumbentDiff)
			script->Apply(incumbentDiff);

		return AdhocScriptStatus<AdhocSubstatus<TCompareStatus>>(baseStatus, AdhocSubstatus<TCompareStatus>(incumbentMutations, status1));
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
			return script->ExecuteAdhoc([&]() { return adhocScript(std::forward<Ts>(p)...); });
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

	template <typename TCompareStatus, typename TTuple, AdhocCompareScript<TCompareStatus, TTuple> F>
	AdhocScriptStatus<TCompareStatus> ModifyFromTupleAdhoc(F adhocScript, TTuple& params)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTupleAdhoc = [&]<typename... Ts>(Ts&&... p) -> AdhocBaseScriptStatus
		{
			return script->ModifyAdhoc([&]() { return adhocScript(std::forward<Ts>(p)...); });
		};

		TCompareStatus compareStatus = TCompareStatus();
		auto baseStatus = std::apply(executeFromTupleAdhoc, std::tuple_cat(std::tuple(&compareStatus), params));

		return AdhocScriptStatus<TCompareStatus>(baseStatus, compareStatus);
	}

	template <typename TTuple, ScriptParamsGenerator<TTuple> F>
	bool GenerateParams(F paramsGenerator, int64_t iteration, TTuple& params)
	{
		return script->ExecuteAdhoc([&]()
			{
				return paramsGenerator(iteration, params);
			}).executed;
	}

	template <class TScript, ScriptComparator<TScript> F>
	bool SelectStatus(F comparator, ScriptStatus<TScript>& status1, ScriptStatus<TScript>& status2)
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

				bool newIncumbent = comparator(&status1, &status2) == &status2;
				if (newIncumbent)
					status1 = status2;

				return newIncumbent;
			}).executed;
	}

	template <class TCompareStatus, AdhocScriptComparator<TCompareStatus> F>
	bool SelectStatusAdhoc(F comparator, AdhocScriptStatus<TCompareStatus>& status1, AdhocScriptStatus<TCompareStatus>& status2)
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

				bool newIncumbent = comparator(&status1, &status2) == &status2;
				if (newIncumbent)
					status1 = status2;

				return newIncumbent;
			}).executed;
	}
};

#endif
