#pragma once
#include <memory>
#include <tuple>
#include <utility>
#include <tasfw/Concepts.hpp>
#include <tasfw/Resource.hpp>

#ifndef STATETRACKER_H
#define STATETRACKER_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

// Identity of a state-tracker type without RTTI: `&StateTrackerTag<T>::value` is one
// address per T for the whole program. TopLevelScript stores its tracker's tag in the root
// and GetTrackedState compares against it instead of a dynamic_cast on every lookup.
template <class T>
struct StateTrackerTag
{
	static constexpr char value = 0;
};

// The tracker a script reads when it names none (Script::GetTrackedState(frame)): its
// StateTracker alias when it declares one (TopLevelScript declares its template parameter,
// so every root and every stage script inherits the answer), else the script itself, a
// tracker reading its own earlier state.
template <class TScript>
struct TrackerOf
{
	using type = TScript;
};

template <class TScript>
	requires requires { typename TScript::StateTracker; }
struct TrackerOf<TScript>
{
	using type = typename TScript::StateTracker;
};

template <derived_from_specialization_of<Script> TStateTracker>
class StateTrackerFactoryBase
{
public:
	virtual ~StateTrackerFactoryBase() = default;
	virtual TStateTracker Generate() = 0;
};

template <derived_from_specialization_of<Script> TStateTracker, typename... TStateTrackerParams>
	requires (std::constructible_from<TStateTracker, TStateTrackerParams...>)
class StateTrackerFactory : public StateTrackerFactoryBase<TStateTracker>
{
public:
	StateTrackerFactory(std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams) : _stateTrackerParams(stateTrackerParams) {}

	TStateTracker Generate()
	{
		return std::apply(
			[]<typename... Ts>(Ts&&... params) -> TStateTracker { return TStateTracker(std::forward<Ts>(params)...); },
			*_stateTrackerParams);
	}

private:
	std::shared_ptr<std::tuple<TStateTrackerParams...>> _stateTrackerParams;
};

#endif
