#pragma once
#include <memory>
#include <tuple>
#include <utility>
#include <tasfw/Concepts.hpp>
#include <tasfw/Resource.hpp>

#ifndef METRICSCRIPT_H
#define METRICSCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

// Identity of a metric-script type without RTTI: `&MetricScriptTag<T>::value` is one
// address per T for the whole program. TopLevelScript stores its metric script's tag in the root
// and GetMetrics compares against it instead of a dynamic_cast on every lookup.
template <class T>
struct MetricScriptTag
{
	static constexpr char value = 0;
};

// The metric script a script reads when it names none (Script::GetMetrics(frame)): its
// MetricScript alias when it declares one (TopLevelScript declares its template parameter,
// so every root and every stage script inherits the answer), else the script itself, a
// metric script reading its own earlier state.
template <class TScript>
struct MetricScriptOf
{
	using type = TScript;
};

template <class TScript>
	requires requires { typename TScript::MetricScript; }
struct MetricScriptOf<TScript>
{
	using type = typename TScript::MetricScript;
};

template <derived_from_specialization_of<Script> TMetricScript>
class MetricScriptFactoryBase
{
public:
	virtual ~MetricScriptFactoryBase() = default;
	virtual TMetricScript Generate() = 0;
};

template <derived_from_specialization_of<Script> TMetricScript, typename... TMetricScriptParams>
	requires (std::constructible_from<TMetricScript, TMetricScriptParams...>)
class MetricScriptFactory : public MetricScriptFactoryBase<TMetricScript>
{
public:
	MetricScriptFactory(std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams) : _metricScriptParams(metricScriptParams) {}

	TMetricScript Generate()
	{
		return std::apply(
			[]<typename... Ts>(Ts&&... params) -> TMetricScript { return TMetricScript(std::forward<Ts>(params)...); },
			*_metricScriptParams);
	}

private:
	std::shared_ptr<std::tuple<TMetricScriptParams...>> _metricScriptParams;
};

#endif
