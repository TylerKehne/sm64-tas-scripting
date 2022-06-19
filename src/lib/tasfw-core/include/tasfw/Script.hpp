#pragma once
#include <unordered_map>
#include <tasfw/Resource.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>
#include <tasfw/ScriptStatus.hpp>
#include <set>
#include <tasfw/SharedLib.hpp>

#ifndef SCRIPT_H
#define SCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <derived_from_specialization_of<Resource> TResource>
class TopLevelScript;

template <typename F, typename R = std::invoke_result_t<F>>
concept AdhocScript = std::same_as<R, bool>;

template <typename F, typename T>
concept AdhocCustomStatusScript = std::same_as<std::invoke_result_t<F, T&>, bool>;

template <derived_from_specialization_of<Resource> TResource>
class SlotHandle
{
public:
	TResource* resource = NULL;
	int64_t slotId = -1;

	SlotHandle(TResource* resource, int64_t slotId) : resource(resource), slotId(slotId) { }

	SlotHandle(SlotHandle<TResource>&&) = default;
	SlotHandle<TResource>& operator = (SlotHandle<TResource>&&) = default;

	SlotHandle(const SlotHandle<TResource>&) = delete;
	SlotHandle<TResource>& operator= (const SlotHandle<TResource>&) = delete;

	~SlotHandle();

	bool isValid();
};

template <derived_from_specialization_of<Resource> TResource>
class InputsMetadata
{
public:
	enum class InputsSource : int8_t
	{
		DIFF = 0,
		ORIGINAL = 1,
		DEFAULT = 2
	};

	Inputs inputs;
	int64_t frame = -1;
	Script<TResource>* stateOwner = nullptr;
	int64_t stateOwnerAdhocLevel = -1;
	InputsSource source = InputsSource::DIFF;

	InputsMetadata() = default;

	InputsMetadata(Inputs inputs, int64_t frame, Script<TResource>* stateOwner, int64_t stateOwnerAdhocLevel, InputsSource source = InputsSource::DIFF)
		: inputs(inputs), stateOwner(stateOwner), frame(frame), stateOwnerAdhocLevel(stateOwnerAdhocLevel), source(source) {}
};

template <derived_from_specialization_of<Resource> TResource>
class SaveMetadata
{
public:
	Script<TResource>* script = nullptr; //ancestor script that won't go out of scope
	int64_t frame = -1;
	int64_t adhocLevel = -1;
	bool isStartSave = false;

	SaveMetadata() = default;

	SaveMetadata(Script<TResource>* script, int64_t frame, int64_t adhocLevel, bool isStartSave = false)
		: script(script), frame(frame), adhocLevel(adhocLevel), isStartSave(isStartSave) { }

	SlotHandle<TResource>* GetSlotHandle();
	bool IsValid();
};

/// <summary>
/// Execute a state-changing operation on the resource. Parameters should correspond
/// to the script's class constructor.
/// </summary>
template <derived_from_specialization_of<Resource> TResource>
class Script
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus = {};

	// TODO: make private
	TResource* resource = nullptr;
	SlotHandle<TResource> startSaveHandle = SlotHandle<TResource>(nullptr, -1);

	Script() = default;

	Script(const Script<TResource>&) = delete;
	Script& operator= (const Script<TResource>&) = delete;

	// TODO: move this method to some utility class
	static void CopyVec3f(Vec3f dest, Vec3f source);

protected:
	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.Initialize(this);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff, script.saveBank[0]);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.Initialize(this);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		ApplyChildDiff(script.BaseStatus[0], script.saveBank[0], initialFrame);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Test(Us&&... params)
	{
		ScriptStatus<TScript> status = Execute<TScript>(std::forward<Us>(params)...);
		status.m64Diff = M64Diff();
		return status;
	}

	AdhocBaseScriptStatus ExecuteAdhoc(AdhocScript auto adhocScript);

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ExecuteAdhoc(F adhocScript);

	AdhocBaseScriptStatus ModifyAdhoc(AdhocScript auto adhocScript);

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ModifyAdhoc(F adhocScript);

	AdhocBaseScriptStatus TestAdhoc(AdhocScript auto&& adhocScript);

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> TestAdhoc(F&& adhocScript);

	//Leaf method for comparing ad-hoc scripts. This is the one the user will call.
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts);

	//Leaf method for comparing ad-hoc scripts and applying the result. This is the one the user will call.
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts);

	//Same as Compare(), but with no diff returned
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> TestCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts);

	// TODO: move this method to some utility class
	template <typename T>
	int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	uint64_t GetCurrentFrame();
	bool IsDiffEmpty();
	M64Diff GetDiff();
	void Apply(const M64Diff& m64Diff);
	void AdvanceFrameRead();
	void AdvanceFrameWrite(Inputs inputs);
	void OptionalSave();
	void Save();
	void Load(uint64_t frame);
	void Rollback(uint64_t frame);
	void RollForward(int64_t frame);
	void Restore(int64_t frame);
	Inputs GetInputs(int64_t frame);

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	friend class TopLevelScript<TResource>;
	friend class SaveMetadata<TResource>;
	friend class InputsMetadata<TResource>;

	int64_t _adhocLevel = 0;
	int32_t _initialFrame = 0;
	std::unordered_map<int64_t, BaseScriptStatus> BaseStatus;
	std::unordered_map<int64_t, std::map<int64_t, SlotHandle<TResource>>> saveBank;// contains handles to savestates
	std::unordered_map<int64_t, std::map<int64_t, uint64_t>> frameCounter;// tracks opportunity cost of having to frame advance from an earlier save
	std::unordered_map<int64_t, std::map<int64_t, SaveMetadata<TResource>>> saveCache;// stores metadata of ancestor saves to save recursion time
	std::unordered_map<int64_t, std::map<int64_t, InputsMetadata<TResource>>> inputsCache;// caches ancestor inputs to save recursion time
	std::unordered_map<int64_t, std::set<int64_t>> loadTracker;// track past loads to know whether a cached save is optimal
	Script* _parentScript;

	bool checkPreconditions();
	bool execute();
	bool checkPostconditions();

	void Initialize(Script<TResource>* parentScript);
	SaveMetadata<TResource> GetLatestSave(int64_t frame);
	SaveMetadata<TResource> GetLatestSaveAndCache(int64_t frame);
	virtual InputsMetadata<TResource> GetInputsMetadata(int64_t frame);
	InputsMetadata<TResource> GetInputsMetadataAndCache(int64_t frame);
	void DeleteSave(int64_t frame, int64_t adhocLevel);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64, std::map<int64_t, SlotHandle<TResource>>& childSaveBank);
	void AdvanceFrameRead(uint64_t& counter);
	uint64_t GetFrameCounter(InputsMetadata<TResource> cachedInputs);
	uint64_t IncrementFrameCounter(InputsMetadata<TResource> cachedInputs);
	void ApplyChildDiff(const BaseScriptStatus& status, std::map<int64_t, SlotHandle<TResource>>& childSaveBank, int64_t initialFrame);
	SaveMetadata<TResource> Save(int64_t adhocLevel);
	void LoadBase(uint64_t frame, bool desync);

	template <typename F>
	BaseScriptStatus ExecuteAdhocBase(F adhocScript);

	//Root method for Compare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
	template <class TCompareStatus>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(const AdhocScriptStatus<TCompareStatus>& status1);

	//Main recursive method for comparing ad hoc scripts. Only the root and leaf use different methods
	template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts);

	//Only to be used by ModifyCompare(). Will desync if used alone, as it assumes the caller will call Revert().
	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ExecuteAdhocNoRevert(F adhocScript);

	//Root method for ModifyCompare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
	template <class TCompareStatus>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1);

	//Main recursive method for ModifyCompare(). Only the root and leaf use different methods
	template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts);
};

template <derived_from_specialization_of<Resource> TResource>
class TopLevelScript : public Script<TResource>
{
public:
	TopLevelScript() = default;

	template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, Ts&&... params) 
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		TResource resource = TResource();
		resource.save(resource.startSave);
		resource.initialFrame = 0;

		script._m64 = &m64;
		script.resource = &resource;
		script.Initialize(nullptr);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

		return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);		
	}

	template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, typename TResourceConfig, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource, TResourceConfig>)
	static ScriptStatus<TTopLevelScript> MainConfig(M64& m64, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		TResource resource = TResource(config);
		resource.save(resource.startSave);
		resource.initialFrame = 0;

		script._m64 = &m64;
		script.resource = &resource;
		script.Initialize(nullptr);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

		return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, class TState, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource>
			&& std::derived_from<TResource, Resource<TState>>)
	static ScriptStatus<TTopLevelScript> MainFromSave(M64& m64, ImportedSave<TState>& save, Ts&&... params) 
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		TResource resource = TResource();
		resource.load(save.state);
		resource.save(resource.startSave);
		resource.initialFrame = save.initialFrame;

		script._m64 = &m64;
		script.resource = &resource;
		script.Initialize(nullptr);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

		return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, class TState, typename TResourceConfig, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...>
			&& std::constructible_from<TResource, TResourceConfig>
			&& std::derived_from<TResource, Resource<TState>>)
	static ScriptStatus<TTopLevelScript> MainFromSaveConfig(M64& m64, ImportedSave<TState>& save, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		TResource resource = TResource(config);
		resource.load(save.state);
		resource.save(resource.startSave);
		resource.initialFrame = save.initialFrame;

		script._m64 = &m64;
		script.resource = &resource;
		script.Initialize(nullptr);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

		return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

protected:
	M64* _m64 = nullptr;

private:
	InputsMetadata<TResource> GetInputsMetadata(int64_t frame) override;
};

//Include template method implementations
#include "tasfw/Script.t.hpp"

#endif
