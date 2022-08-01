#pragma once
#include <tasfw/ScriptStatus.hpp>
#include <tasfw/SharedLib.hpp>

#ifndef SCRIPT_COMPARE_HELPER_H
#define SCRIPT_COMPARE_HELPER_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

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
	std::same_as<std::invoke_result_t<F, int64_t, const ScriptStatus<TScript>&, const ScriptStatus<TScript>&>, const ScriptStatus<TScript>&>;
};

template <typename F, typename TScript>
concept ScriptTerminator = requires
{
	derived_from_specialization_of<TScript, Script>;
	std::same_as<std::invoke_result_t<F, int64_t, const ScriptStatus<TScript>&>, bool>;
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
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F comparator, G terminator)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Execute<TScript>(std::forward<Ts>(p)...);
		};

		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;

		// return if container is empty
		if (paramsList.begin() == paramsList.end())
			return status1;

		status1 = std::apply(executeFromTuple, *(paramsList.begin()));
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
			ScriptStatus<TScript> status2 = std::apply(executeFromTuple, params);
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				return status2;

			// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
			script->ExecuteAdhoc([&]()
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

					status1 = comparator(iteration, status1, status2);
					return true;
				});
		}

		return status1;
	}

	template <class TScript,
		typename TTuple,
		ScriptParamsGenerator<TScript, TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F paramsGenerator, G comparator, H terminator)
	{
		//Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Execute<TScript>(std::forward<Ts>(p)...);
		};

		// generate parameters safely
		auto generateParams = [&](int64_t iteration, TTuple& params) -> bool
		{
			return script->ExecuteAdhoc([&]()
				{
					return paramsGenerator(iteration, params);
				}).executed;
		};

		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!generateParams(iteration, params))
			return status1;

		status1 = std::apply(executeFromTuple, params);
		if (status1.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status1); }).executed)
			return status1;

		while (generateParams(++iteration, params))
		{
			ScriptStatus<TScript> status2 = std::apply(executeFromTuple, params);
			if (status2.asserted && script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
				return status2;

			// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
			script->ExecuteAdhoc([&]()
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

					status1 = comparator(iteration, status1, status2);
					return true;
				});
		}

		return status1;
	}

	template <class TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (derived_from_specialization_of<TScript, Script> && constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F comparator, G terminator)
	{
		// Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Modify<TScript>(std::forward<Ts>(p)...);
		};

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
				status1 = std::apply(executeFromTuple, *(paramsList.begin()));

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
			terminate = script->ExecuteAdhocBase([&]()
				{
					status2 = std::apply(executeFromTuple, params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
						return true;

					// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
					script->ExecuteAdhoc([&]()
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

							status1 = comparator(iteration, status1, status2);
							return true;
						});

					return false;
				}).executed;

			// Don't revert if best script is the last one to be run
			if (terminate || ((&params == &*std::prev(paramsList.end()) && (&status1 == &status2))))
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
	ScriptStatus<TScript> ModifyCompare(F paramsGenerator, G comparator, H terminator)
	{
		// Have to wrap in lambda to satisfy type constraints
		auto executeFromTuple = [&]<typename... Ts>(Ts&&... p) -> ScriptStatus<TScript>
		{
			return script->template Modify<TScript>(std::forward<Ts>(p)...);
		};

		// generate parameters safely
		auto generateParams = [&](int64_t iteration, TTuple& params) -> bool
		{
			return script->ExecuteAdhoc([&]()
				{
					return paramsGenerator(iteration, params);
				}).executed;
		};

		ScriptStatus<TScript> status1 = ScriptStatus<TScript>();
		ScriptStatus<TScript> status2 = ScriptStatus<TScript>();
		int64_t initialFrame = script->GetCurrentFrame();
		int64_t iteration = 0;
		TTuple params;

		// return if no params
		if (!generateParams(iteration, params))
			return status1;

		// We want to avoid reversion of successful script
		bool terminate = script->ExecuteAdhocBase([&]()
			{
				status1 = std::apply(executeFromTuple, params);

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

		while (generateParams(++iteration, params))
		{
			// We want to avoid reversion of successful script
			terminate = script->ExecuteAdhocBase([&]()
				{
					status2 = std::apply(executeFromTuple, params);

					if (!status2.asserted)
						return false;

					if (script->ExecuteAdhoc([&]() { return terminator(iteration, status2); }).executed)
						return true;

					// Select better status according to comparator. Wrap in ad-hoc block in case it alters state
					script->ExecuteAdhoc([&]()
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

							status1 = comparator(iteration, status1, status2);
							return true;
						});

					return false;
				}).executed;

			// Don't revert if best script is the last one to be run
			if (terminate || ((!generateParams(iteration + 1, params) && (&status1 == &status2))))
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
};

#endif