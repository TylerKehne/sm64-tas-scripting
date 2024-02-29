#pragma once
#include <unordered_map>
#include <tasfw/Resource.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>
#include <tasfw/ScriptStatus.hpp>
#include <set>
#include <tasfw/SharedLib.hpp>
#include <tasfw/ScriptCompareHelper.hpp>

#ifndef SCRIPT_H
#define SCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <derived_from_specialization_of<Resource> TResource>
class DefaultStateTracker;

template <derived_from_specialization_of<Resource> TResource,
	std::derived_from<Script<TResource>> TStateTracker>
class TopLevelScript;

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState,
	class TResourceConfig,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderConfigured;

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderImported;

template <derived_from_specialization_of<Script> TStateTracker>
class StateTrackerFactoryBase;

template <derived_from_specialization_of<Resource> TResource>
class ScriptFriend;

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
		: inputs(inputs), frame(frame), stateOwner(stateOwner), stateOwnerAdhocLevel(stateOwnerAdhocLevel), source(source) {}
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
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.isStateTracker = isStateTracker;
		script.Initialize(this);

		uint64_t loadStateTimeStart = resource->GetTotalLoadStateTime();
		uint64_t saveStateTimeStart = resource->GetTotalSaveStateTime();
		uint64_t advanceFrameTimeStart = resource->GetTotalFrameAdvanceTime();

		uint64_t start = get_time();
		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->GetTotalLoadStateTime() - loadStateTimeStart;
		BaseStatus[_adhocLevel].saveDuration = resource->GetTotalSaveStateTime() - saveStateTimeStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->GetTotalFrameAdvanceTime() - advanceFrameTimeStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff, script.saveBank[0], &script);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.isStateTracker = isStateTracker;
		script.Initialize(this);

		uint64_t loadStateTimeStart = resource->GetTotalLoadStateTime();
		uint64_t saveStateTimeStart = resource->GetTotalSaveStateTime();
		uint64_t advanceFrameTimeStart = resource->GetTotalFrameAdvanceTime();

		uint64_t start = get_time();
		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->GetTotalLoadStateTime() - loadStateTimeStart;
		BaseStatus[_adhocLevel].saveDuration = resource->GetTotalSaveStateTime() - saveStateTimeStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->GetTotalFrameAdvanceTime() - advanceFrameTimeStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		ApplyChildDiff(script.BaseStatus[0], script.saveBank[0], initialFrame, &script);

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

	#pragma region Compare Methods

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F&& comparator, G&& terminator)
	{
		return compareHelper.template Compare<TScript>(paramsList, std::forward<F>(comparator), std::forward<G>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F>
	requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F&& comparator)
	{
		return compareHelper.template Compare<TScript>(paramsList, std::forward<F>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator, H&& terminator)
	{
		return compareHelper.template Compare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator)
	{
		return compareHelper.template Compare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F&& comparator, G&& terminator)
	{
		return compareHelper.template ModifyCompare<TScript>(paramsList, std::forward<F>(comparator), std::forward<G>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F&& comparator)
	{
		return compareHelper.template ModifyCompare<TScript>(paramsList, std::forward<F>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator, H&& terminator)
	{
		return compareHelper.template ModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G>
	requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator)
	{
		return compareHelper.template ModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H&& terminator)
	{
		return compareHelper.template DynamicCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator)
	{
		return compareHelper.template DynamicCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H&& terminator)
	{
		return compareHelper.template DynamicModifyCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator)
	{
		return compareHelper.template DynamicModifyCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H&& terminator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus>(paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus>(paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I&& terminator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H&& terminator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I&& terminator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J&& terminator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), std::forward<J>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J&& terminator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), std::forward<J>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	#pragma endregion

	template <std::derived_from<Script<TResource>> TStateTracker>
		requires std::constructible_from<TStateTracker>
	typename TStateTracker::CustomScriptStatus GetTrackedState(int64_t frame)
	{
		TopLevelScript<TResource, TStateTracker>* root = dynamic_cast<TopLevelScript<TResource, TStateTracker>*>(_rootScript);
		if (!root) {
			std::cout << "Type mismatch! Expected TopLevelScript<" << typeid(TResource).name() << ", " << typeid(TStateTracker).name() << ">.\n";
			throw std::runtime_error("Type mismatch in GetTrackedState<TStateTracker>()");
		}

		return root->GetTrackedStateInternal(this, GetInputsMetadataAndCache(frame));
	}

	template <std::derived_from<Script<TResource>> TStateTracker>
		requires std::constructible_from<TStateTracker>
	bool TrackedStateExists(int64_t frame)
	{
		TopLevelScript<TResource, TStateTracker>* root = dynamic_cast<TopLevelScript<TResource, TStateTracker>*>(_rootScript);
		if (!root) {
			std::cout << "Type mismatch! Expected TopLevelScript<" << typeid(TResource).name() << ", " << typeid(TStateTracker).name() << ">.\n";
			throw std::runtime_error("Type mismatch in GetTrackedState<TStateTracker>()");
		}

		return root->TrackedStateExistsInternal(this, GetInputsMetadataAndCache(frame));
	}

	// TODO: move this method to some utility class
	template <typename T>
	int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	uint64_t GetCurrentFrame();
	bool IsDiffEmpty();
	M64Diff GetDiff();
	M64Diff GetTotalDiff();
	M64Diff GetBaseDiff();
	void Apply(const M64Diff& m64Diff);
	void AdvanceFrameRead();
	void AdvanceFrameWrite(Inputs inputs);
	void OptionalSave();
	void Save();
	void Load(uint64_t frame);
	void LongLoad(int64_t frame);
	void Rollback(uint64_t frame);
	void RollForward(int64_t frame);
	void Restore(int64_t frame);
	Inputs GetInputs(int64_t frame);
	M64Diff GetInputs(int64_t firstFrame, int64_t lastFrame);
	bool ExportM64(std::filesystem::path fileName);
	bool ExportM64(std::filesystem::path fileName, int64_t maxFrame);

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	friend class ScriptFriend<TResource>;
	friend class SaveMetadata<TResource>;
	friend class InputsMetadata<TResource>;
	friend class ScriptCompareHelper<TResource>;

	int64_t _adhocLevel = 0;
	int32_t _initialFrame = 0;
	std::unordered_map<int64_t, BaseScriptStatus> BaseStatus;
	std::unordered_map<int64_t, std::map<int64_t, SlotHandle<TResource>>> saveBank;// contains handles to savestates
	std::unordered_map<int64_t, std::map<int64_t, uint64_t>> frameCounter;// tracks opportunity cost of having to frame advance from an earlier save
	std::unordered_map<int64_t, std::map<int64_t, SaveMetadata<TResource>>> saveCache;// stores metadata of ancestor saves to save recursion time
	std::unordered_map<int64_t, std::map<int64_t, InputsMetadata<TResource>>> inputsCache;// caches ancestor inputs to save recursion time
	std::unordered_map<int64_t, std::set<int64_t>> loadTracker;// track past loads to know whether a cached save is optimal
	Script* _parentScript;
	Script* _rootScript;
	bool isStateTracker = false;
	ScriptCompareHelper<TResource> compareHelper = ScriptCompareHelper<TResource>(this);

	bool Run();

	void Initialize(Script<TResource>* parentScript);
	SaveMetadata<TResource> GetLatestSave(int64_t frame);
	SaveMetadata<TResource> GetLatestSaveAndCache(int64_t frame);
	virtual InputsMetadata<TResource> GetInputsMetadata(int64_t frame);
	InputsMetadata<TResource> GetInputsMetadataAndCache(int64_t frame);
	void DeleteSave(int64_t frame, int64_t adhocLevel);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64, std::map<int64_t, SlotHandle<TResource>>& childSaveBank, Script<TResource>* childScript);
	void AdvanceFrameRead(uint64_t& counter);
	uint64_t GetFrameCounter(InputsMetadata<TResource> cachedInputs);
	uint64_t IncrementFrameCounter(InputsMetadata<TResource> cachedInputs);
	void ApplyChildDiff(const BaseScriptStatus& status, std::map<int64_t, SlotHandle<TResource>>& childSaveBank, int64_t initialFrame, Script<TResource>* childScript);
	SaveMetadata<TResource> Save(int64_t adhocLevel);
	void LoadBase(uint64_t frame, bool desync);

	template <typename F>
	BaseScriptStatus ExecuteAdhocBase(F adhocScript);

	template <derived_from_specialization_of<Script> TStateTracker>
	ScriptStatus<TStateTracker> ExecuteStateTracker(int64_t frame, std::shared_ptr<StateTrackerFactoryBase<TStateTracker>> stateTrackerFactory)
	{
		uint64_t initialFrame = GetCurrentFrame();

		TStateTracker script = stateTrackerFactory->Generate();
		script.Initialize(this);

		uint64_t loadStateTimeStart = resource->GetTotalLoadStateTime();
		uint64_t saveStateTimeStart = resource->GetTotalSaveStateTime();
		uint64_t advanceFrameTimeStart = resource->GetTotalFrameAdvanceTime();

		uint64_t start = get_time();
		// The information is stored per frame, so we need to make sure we are there before running the state tracking script.
		script.Load(frame);

		// Set this after the load so states can be tracked efficiently if the state is in the future.
		script.isStateTracker = true;

		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->GetTotalLoadStateTime() - loadStateTimeStart;
		BaseStatus[_adhocLevel].saveDuration = resource->GetTotalSaveStateTime() - saveStateTimeStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->GetTotalFrameAdvanceTime() - advanceFrameTimeStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff, script.saveBank[0], &script);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TStateTracker>(script.BaseStatus[0], script.CustomStatus);
	}

	// Needed for state tracking. These do nothing, but TopLevelScript overrides them. Can't access explicitly because of lack of template information.
	virtual void TrackState(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) { return; }
	virtual bool TrackedStateExistsInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) { return false; }
	virtual void PushTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel) { return; }
	virtual void PopTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel) { return; }
	virtual void MoveSyncedTrackedStates(Script<TResource>* sourceScript, int sourceAdhocLevel, Script<TResource>* destScript, int destAdhocLevel) { return; }
	virtual void EraseTrackedStates(Script<TResource>* currentScript, int adhocLevel, int64_t firstFrame) { return; }
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

// DO NOT EVER USE THESE METHODS OUTSIDE OF THE TOPLEVELSCRIPT BASE CLASS
// MSVC's casual relationship with the C++ standard necessitates this class to access certain Script private members.
// Preferably we could just declare a friend class template for TopLevelScript like we're supposed to be able to do.
template <derived_from_specialization_of<Resource> TResource>
class ScriptFriend
{
public:
	static int GetAdhocLevel(Script<TResource>* script)
	{
		return script->_adhocLevel;
	}

	static std::unordered_map<int64_t, BaseScriptStatus>& GetBaseStatus(Script<TResource>* script)
	{
		return script->BaseStatus;
	}

	static std::unordered_map<int64_t, std::map<int64_t, InputsMetadata<TResource>>>& GetInputsCache(Script<TResource>* script)
	{
		return script->inputsCache;
	}

	static void DisposeSlotHandles(Script<TResource>* script)
	{
		script->saveBank[0].erase(script->saveBank[0].begin(), script->saveBank[0].end());
	}

	static void Initialize(Script<TResource>* script, Script<TResource>* parentScript)
	{
		script->Initialize(parentScript);
	}

	static bool Run(Script<TResource>* script)
	{
		return script->Run();
	}

	static Script<TResource>* GetParentScript(Script<TResource>* script)
	{
		return script->_parentScript;
	}

	static bool IsStateTracker(Script<TResource>* script)
	{
		return script->isStateTracker;
	}

	template <derived_from_specialization_of<Script> TStateTracker>
	static ScriptStatus<TStateTracker> ExecuteStateTracker(
		int64_t frame, Script<TResource>* script, std::shared_ptr<StateTrackerFactoryBase<TStateTracker>> stateTrackerFactory)
	{
		return script->ExecuteStateTracker<TStateTracker>(frame, stateTrackerFactory);
	}

	static uint64_t GetCurrentFrame(Script<TResource>* script)
	{
		return script->GetCurrentFrame();
	}
};

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
	TopLevelScript() = default;

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource();
		resource.save(resource.startSave);

		resource.initialFrame = 0;

		return InitializeAndRun(m64, script, &resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainImport(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TResource* resource, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		// Initialize start save if resource is new. If not, load start save to reset resource.
		if (resource->initialFrame == -1)
		{
			resource->save(resource->startSave);
			resource->initialFrame = 0;
		}
		else
			resource->load(resource->startSave);

		return InitializeAndRun(m64, script, resource);
	}

	template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename TResourceConfig, typename... TStateTrackerParams, typename... Ts>
		requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource, TResourceConfig> && std::constructible_from<TStateTracker, TStateTrackerParams...>)
	static ScriptStatus<TTopLevelScript> MainConfig(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TResourceConfig config, Ts&&... params)
	{
		TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
		script.stateTrackerFactory = std::make_shared<StateTrackerFactory<TStateTracker, TStateTrackerParams...>>(stateTrackerParams);

		TResource resource = TResource(config);
		resource.save(resource.startSave);

		resource.initialFrame = 0;

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
		resource.save(resource.startSave);

		resource.initialFrame = save.initialFrame;

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
		resource.save(resource.startSave);

		resource.initialFrame = save.initialFrame;

		return InitializeAndRun(m64, script, &resource);
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

protected:
	M64* _m64 = nullptr;

private:
	friend class Script<TResource>;
	friend class TopLevelScript<TResource, TStateTracker>;

	// Data: trackedStates[script][adhocLevel][frame] = state;
	std::shared_ptr<StateTrackerFactoryBase<TStateTracker>> stateTrackerFactory = nullptr;
	std::unordered_map<Script<TResource>*, std::unordered_map<int64_t, std::map<int64_t, typename TStateTracker::CustomScriptStatus>>> trackedStates;

	void TrackState(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	bool TrackedStateExistsInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata) override;
	void PushTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel) override;
	void PopTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel) override;
	void MoveSyncedTrackedStates(Script<TResource>* sourceScript, int sourceAdhocLevel, Script<TResource>* destScript, int destAdhocLevel) override;
	void EraseTrackedStates(Script<TResource>* currentScript, int adhocLevel, int64_t firstFrame) override;
	typename TStateTracker::CustomScriptStatus GetTrackedStateInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata);

	InputsMetadata<TResource> GetInputsMetadata(int64_t frame) override;

	template <std::derived_from<TopLevelScript<TResource, TStateTracker>> TTopLevelScript>
	static ScriptStatus<TTopLevelScript> InitializeAndRun(M64& m64, TTopLevelScript& script, TResource* resource)
	{
		script._m64 = &m64;
		script.resource = resource;
		ScriptFriend<TResource>::Initialize(&script, nullptr);

		script.TrackState(&script, script.GetInputsMetadata(ScriptFriend<TResource>::GetCurrentFrame(&script)));

		uint64_t loadStateTimeStart = resource->GetTotalLoadStateTime();
		uint64_t saveStateTimeStart = resource->GetTotalSaveStateTime();
		uint64_t advanceFrameTimeStart = resource->GetTotalFrameAdvanceTime();

		uint64_t start = get_time();
		ScriptFriend<TResource>::Run(&script);
		uint64_t finish = get_time();

		auto& baseStatus = ScriptFriend<TResource>::GetBaseStatus(&script)[0];
		baseStatus.loadDuration = resource->GetTotalLoadStateTime() - loadStateTimeStart;
		baseStatus.saveDuration = resource->GetTotalSaveStateTime() - saveStateTimeStart;
		baseStatus.advanceFrameDuration = resource->GetTotalFrameAdvanceTime() - advanceFrameTimeStart;
		baseStatus.totalDuration = finish - start;

		//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
		ScriptFriend<TResource>::DisposeSlotHandles(&script);

		return ScriptStatus<TTopLevelScript>(baseStatus, script.CustomStatus);
	}
};

class DefaultState {};

class DefaultResourceConfig {};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams>
class TopLevelScriptBuilder
{
public:
	TopLevelScriptBuilder(M64& m64) : _m64(m64) { _stateTrackerParams = std::make_shared<std::tuple<>>(); }
	TopLevelScriptBuilder(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: _m64(m64), _stateTrackerParams(stateTrackerParams) {}

	static TopLevelScriptBuilder<TTopLevelScript> Build(M64& m64)
	{
		return TopLevelScriptBuilder<TTopLevelScript>(m64);
	}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilder<TTopLevelScript> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilder<TTopLevelScript, UStateTrackerParams...>(_m64, tuplePtr);
	}

	template <class TState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...> ImportSave(
		uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<TState> importedSave = ImportedSave(TState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...>(
			_m64, std::move(importedSave), DefaultResourceConfig(), _stateTrackerParams);
	}

	template <typename TResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TStateTrackerParams...> ConfigureResource(TResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TStateTrackerParams...>(
			_m64, ImportedSave<DefaultState>(DefaultState(), -1), resourceConfig, _stateTrackerParams);
	}

	template <class TResource>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, TStateTrackerParams...> ImportResource(TResource* resource)
	{
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, TStateTrackerParams...>(_m64, resource, _stateTrackerParams);
	}

protected:
	M64& _m64;
	std::shared_ptr<std::tuple<TStateTrackerParams...>> _stateTrackerParams;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState = DefaultState,
	class TResourceConfig = DefaultResourceConfig,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderConfigured : public TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_stateTrackerParams;

	TopLevelScriptBuilderConfigured(M64& m64, ImportedSave<TState> importedSave,
		TResourceConfig resourceConfig, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>(m64, stateTrackerParams), _importedSave(importedSave), _resourceConfig(resourceConfig) {}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UStateTrackerParams...> ConfigureStateTracker(
		TStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UStateTrackerParams...>(
			_m64, std::move(_importedSave), std::move(_resourceConfig), tuplePtr);
	}

	template <class UState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...> ImportSave(uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<UState> importedSave = ImportedSave(UState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...>(
			_m64, std::move(importedSave), std::move(_resourceConfig), _stateTrackerParams);
	}

	template <typename UResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TStateTrackerParams...> ConfigureResource(UResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TStateTrackerParams...>(
			_m64, std::move(_importedSave), resourceConfig, _stateTrackerParams);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		if constexpr (std::is_same<TState, DefaultState>::value)
		{
			if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
				return TTopLevelScript::template Main<TTopLevelScript>(
					_m64, _stateTrackerParams, std::forward<TScriptParams>(scriptParams)...);
			else
				return TTopLevelScript::template MainConfig<TTopLevelScript, TResourceConfig>(
					_m64, _stateTrackerParams, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
		}
		else if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
			return TTopLevelScript::template MainFromSave<TTopLevelScript, TState>(
				_m64, _stateTrackerParams, _importedSave, std::forward<TScriptParams>(scriptParams)...);
		else
			return TTopLevelScript::template MainFromSaveConfig<TTopLevelScript, TState, TResourceConfig>(
				_m64, _stateTrackerParams, _importedSave, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
	}

private:
	ImportedSave<TState> _importedSave { TState(), -1 };
	TResourceConfig _resourceConfig;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderImported : public TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_stateTrackerParams;

	TopLevelScriptBuilderImported(M64& m64, TResource* resource, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>(m64, stateTrackerParams), _resource(resource) {}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, UStateTrackerParams...> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, UStateTrackerParams...>(
			_m64, _resource, tuplePtr);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		return TTopLevelScript::template MainImport<TTopLevelScript>(
			_m64, _stateTrackerParams, _resource, std::forward<TScriptParams>(scriptParams)...);
	}

private:
	TResource* _resource;
};

//Include template method implementations
#include "tasfw/Script.t.hpp"

#endif
