#pragma once
#ifndef SCRIPT_H
#error "TopLevelScript.hpp is included by Script.hpp, which it completes"
#else
#ifndef TOPLEVELSCRIPT_H
#define TOPLEVELSCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class DefaultStateTracker : public Script<TResource>
{
public:
	DefaultStateTracker() = default;

	bool validation() { return true; }
	bool execution() { return true; }
	bool assertion() { return true; }
};

template <derived_from_specialization_of<Resource> TResource,
	std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>>
class TopLevelScript : public Script<TResource>
{
public:
	TopLevelScript()
	{
		this->_stateTrackerTag = &StateTrackerTag<TStateTracker>::value;
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource();
		resource.SaveStart(0);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainImport(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TResource* resource, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		// Initialize start save if resource is new. If not, load start save to reset resource.
		if (resource->InitialFrame() == -1)
			resource->SaveStart(0);
		else
			resource->LoadStart();

		return InitializeAndRun(m64, script, resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename TResourceConfig, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource, TResourceConfig> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainConfig(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource(config);
		resource.SaveStart(0);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, class TState,
		typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource>
			&& std::derived_from<TResource, Resource<TState>>
			&& std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainFromSave(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, ImportedSave<TState>& save, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource();
		resource.load(save.state);
		resource.SaveStart(save.initialFrame);

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, class TState,
		typename TResourceConfig, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource, TResourceConfig>
			&& std::derived_from<TResource, Resource<TState>>
			&& std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainFromSaveConfig(
		M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, ImportedSave<TState>& save, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource(config);
		resource.load(save.state);
		resource.SaveStart(save.initialFrame);

		return InitializeAndRun(m64, script, &resource);
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

private:
	friend class Script<TResource>;
	M64* _m64 = nullptr;
	M64Metadata GetM64Metadata() const override;
	// The root's walk over its own levels, then the movie. The same walk as Script's, and it
	// stays a copy on purpose: one walk for both, ending in a private virtual the root
	// overrides for the movie (the GetM64Metadata shape), measured 7 to 18 ns more per
	// uncached lookup on MSVC 19.51 in three forms, with the root's own walk flat
	// (docs/performance-changelog.md, 2026-09-15).
	void GetInputsMetadata(int64_t frame, InputsMetadata<TResource>& metadata) override;
	// (No self-friend declaration: a class is always its own friend, and GCC warns about it.)

	// Data: trackedStates[script][adhocLevel][frame] = state;
	std::shared_ptr<StateTrackerFactoryBase<TStateTracker>> stateTrackerFactory = nullptr;
	std::unordered_map<Script<TResource>*, LevelStack<std::map<int64_t, typename TStateTracker::CustomScriptStatus>>> trackedStates;

	void TrackState(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	bool TrackedStateExistsInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	void PopTrackedStatesContainer(Script<TResource>* currentScript, int64_t adhocLevel) override;
	void MoveSyncedTrackedStates(Script<TResource>* sourceScript, int64_t sourceAdhocLevel, Script<TResource>* destScript, int64_t destAdhocLevel) override;
	void EraseTrackedStates(Script<TResource>* currentScript, int64_t adhocLevel, int64_t firstFrame) override;
	const typename TStateTracker::CustomScriptStatus& GetTrackedStateInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata);

	template <std::derived_from<TopLevelScript<TResource, TStateTracker>> TTopLevelScript>
	static ScriptStatus<TTopLevelScript> InitializeAndRun(M64& m64, TTopLevelScript& script, TResource* resource)
	{
		// Script's names through the base, where nothing a TTopLevelScript declares hides them
		// (ScattershotThread has an Initialize() of its own).
		Script<TResource>& base = script;
		script._m64 = &m64;
		base.resource = resource;
		base.Initialize(nullptr);

		base.TrackState(&base, base.GetInputsMetadata(base.GetCurrentFrame()));

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
};

//Include template method implementations
#include "tasfw/TopLevelScript.t.hpp"

#endif
#endif
