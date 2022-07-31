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
};

#endif