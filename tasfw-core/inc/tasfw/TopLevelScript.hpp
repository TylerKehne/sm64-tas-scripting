#pragma once
#ifndef SCRIPT_H
#error "TopLevelScript.hpp is included by Script.hpp, which it completes"
#else
#ifndef TOPLEVELSCRIPT_H
#define TOPLEVELSCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class DefaultMetricScript : public Script<TResource>
{
public:
	DefaultMetricScript() = default;

	bool validation() { return true; }
	bool execution() { return true; }
	bool assertion() { return true; }
};

template <derived_from_specialization_of<Resource> TResource,
	std::derived_from<Script<TResource>> TMetricScript = DefaultMetricScript<TResource>>
class TopLevelScript : public Script<TResource>
{
public:
	// What GetMetrics(frame) reads in a root and in everything derived from one.
	using MetricScript = TMetricScript;

	TopLevelScript()
	{
		this->_metricScriptTag = &MetricScriptTag<TMetricScript>::value;
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

	// How a run starts, one entry point per way of getting a resource: fresh, imported, built from
	// a configuration, or loaded from an exported save (the builders call these).
	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TMetricScriptParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource> && std::constructible_from<TMetricScript, TMetricScriptParams...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.metricScriptFactory = std::make_shared<MetricScriptFactory<TMetricScript, TMetricScriptParams...>>(metricScriptParams);

		TResource resource = TResource();
		resource.SaveStart(0);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TMetricScriptParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TMetricScript, TMetricScriptParams...>)
	static ScriptStatus<TTopLevelScript> MainImport(M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, TResource* resource, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.metricScriptFactory = std::make_shared<MetricScriptFactory<TMetricScript, TMetricScriptParams...>>(metricScriptParams);

		// Initialize start save if resource is new. If not, load start save to reset resource.
		if (resource->InitialFrame() == -1)
			resource->SaveStart(0);
		else
			resource->LoadStart();

		return InitializeAndRun(m64, script, resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename TResourceConfig, typename... TMetricScriptParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource, TResourceConfig> && std::constructible_from<TMetricScript, TMetricScriptParams...>)
	static ScriptStatus<TTopLevelScript> MainConfig(M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.metricScriptFactory = std::make_shared<MetricScriptFactory<TMetricScript, TMetricScriptParams...>>(metricScriptParams);

		TResource resource = TResource(config);
		resource.SaveStart(0);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, class TState,
		typename... TMetricScriptParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource>
			&& std::derived_from<TResource, Resource<TState>>
			&& std::constructible_from<TMetricScript, TMetricScriptParams...>)
	static ScriptStatus<TTopLevelScript> MainFromSave(M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, ImportedSave<TState>& save, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.metricScriptFactory = std::make_shared<MetricScriptFactory<TMetricScript, TMetricScriptParams...>>(metricScriptParams);

		TResource resource = TResource();
		resource.load(save.state);
		resource.SaveStart(save.initialFrame);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, class TState,
		typename TResourceConfig, typename... TMetricScriptParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource, TResourceConfig>
			&& std::derived_from<TResource, Resource<TState>>
			&& std::constructible_from<TMetricScript, TMetricScriptParams...>)
	static ScriptStatus<TTopLevelScript> MainFromSaveConfig(
		M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, ImportedSave<TState>& save, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.metricScriptFactory = std::make_shared<MetricScriptFactory<TMetricScript, TMetricScriptParams...>>(metricScriptParams);

		TResource resource = TResource(config);
		resource.load(save.state);
		resource.SaveStart(save.initialFrame);

		return InitializeAndRun(m64, script, &resource);
	}

private:
	friend class Script<TResource>;
	// (No self-friend declaration: a class is always its own friend, and GCC warns about it.)

	M64* _m64 = nullptr;
	// Data: metrics[script][adhocLevel][frame] = state;
	std::shared_ptr<MetricScriptFactoryBase<TMetricScript>> metricScriptFactory = nullptr;
	std::unordered_map<Script<TResource>*, LevelStack<std::map<int64_t, typename TMetricScript::CustomScriptStatus>>> metrics;

	template <std::derived_from<TopLevelScript<TResource, TMetricScript>> TTopLevelScript>
	static ScriptStatus<TTopLevelScript> InitializeAndRun(M64& m64, TTopLevelScript& script, TResource* resource)
	{
		// Script's names through the base, where nothing a TTopLevelScript declares hides them
		// (ScattershotThread has an Initialize() of its own).
		Script<TResource>& base = script;
		script._m64 = &m64;
		base.resource = resource;
		base.Initialize(nullptr);

		base.RecordMetrics(&base, base.GetInputsMetadata(base.GetCurrentFrame()));

		uint64_t loadCyclesStart = resource->work.loadCycles;
		uint64_t saveCyclesStart = resource->work.saveCycles;
		uint64_t advanceCyclesStart = resource->work.advanceCycles;

		uint64_t start = get_time();
		base.Run();
		uint64_t finish = get_time();

		auto& baseStatus = base.BaseStatus[0];
		baseStatus.loadDuration = resource->work.loadCycles - loadCyclesStart;
		baseStatus.saveDuration = resource->work.saveCycles - saveCyclesStart;
		baseStatus.advanceFrameDuration = resource->work.advanceCycles - advanceCyclesStart;
		baseStatus.totalDuration = finish - start;

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		base.saveBank[0].erase(base.saveBank[0].begin(), base.saveBank[0].end());

		return ScriptStatus<TTopLevelScript>(std::move(baseStatus), std::move(script.CustomStatus));
	}

	M64Metadata GetM64Metadata() const override;
	// The root's walk over its own levels, then the movie. The same walk as Script's, and it
	// stays a copy on purpose: one walk for both, ending in a private virtual the root
	// overrides for the movie (the GetM64Metadata shape), measured 7 to 18 ns more per
	// uncached lookup on MSVC 19.51 in three forms, with the root's own walk flat
	// (docs/performance-changelog.md, 2026-09-15).
	void GetInputsMetadata(int64_t frame, InputsMetadata<TResource>& metadata) override;

	void RecordMetrics(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	bool MetricsExistInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	void PopMetricsContainer(Script<TResource>* currentScript, int64_t adhocLevel) override;
	void MoveSyncedMetrics(Script<TResource>* sourceScript, int64_t sourceAdhocLevel, Script<TResource>* destScript, int64_t destAdhocLevel) override;
	void EraseMetrics(Script<TResource>* currentScript, int64_t adhocLevel, int64_t firstFrame) override;
	const typename TMetricScript::CustomScriptStatus& GetMetricsInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata);
};

//Include template method implementations
#include "tasfw/TopLevelScript.t.hpp"

#endif
#endif
