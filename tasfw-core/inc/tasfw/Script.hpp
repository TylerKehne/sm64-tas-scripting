#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <tasfw/FrameMap.hpp>
#include <tasfw/LevelStack.hpp>
#include <tasfw/Resource.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/M64.hpp>
#include <sm64/Types.hpp>
#include <tasfw/ScriptStatus.hpp>
#include <tasfw/Concepts.hpp>
#include <tasfw/ScriptCompareHelper.hpp>
#include <tasfw/SlotHandle.hpp>
#include <tasfw/ScriptMetadata.hpp>
#include <tasfw/StateTracker.hpp>

#ifndef SCRIPT_H
#define SCRIPT_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <derived_from_specialization_of<Resource> TResource,
	std::derived_from<Script<TResource>> TStateTracker>
class TopLevelScript;

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

	Script() = default;

	Script(const Script<TResource>&) = delete;
	Script& operator= (const Script<TResource>&) = delete;

protected:
	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.isStateTracker = isStateTracker;
		script.Initialize(this);

		uint64_t loadCyclesStart = resource->work.loadCycles;
		uint64_t saveCyclesStart = resource->work.saveCycles;
		uint64_t advanceCyclesStart = resource->work.advanceCycles;

		uint64_t start = get_time();
		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->work.loadCycles - loadCyclesStart;
		BaseStatus[_adhocLevel].saveDuration = resource->work.saveCycles - saveCyclesStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->work.advanceCycles - advanceCyclesStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff, SaveBankIfCreated(script, 0), &script);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(std::move(script.BaseStatus[0]), std::move(script.CustomStatus));
	}

	template <derived_from_specialization_of<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		uint64_t initialFrame = GetCurrentFrame();

		TScript script = TScript(std::forward<Us>(params)...);
		script.isStateTracker = isStateTracker;
		script.Initialize(this);

		uint64_t loadCyclesStart = resource->work.loadCycles;
		uint64_t saveCyclesStart = resource->work.saveCycles;
		uint64_t advanceCyclesStart = resource->work.advanceCycles;

		uint64_t start = get_time();
		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->work.loadCycles - loadCyclesStart;
		BaseStatus[_adhocLevel].saveDuration = resource->work.saveCycles - saveCyclesStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->work.advanceCycles - advanceCyclesStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		ApplyChildDiff(script.BaseStatus[0], SaveBankIfCreated(script, 0), initialFrame, &script);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(std::move(script.BaseStatus[0]), std::move(script.CustomStatus));
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

	// The tracked state at `frame`, computed on first request. The reference points into the
	// root's table and stays valid until a write at or before `frame` invalidates it; copy
	// it (`auto state = ...`) if it has to outlive the next AdvanceFrameWrite/Load.
	template <std::derived_from<Script<TResource>> TStateTracker>
		requires std::constructible_from<TStateTracker>
	const typename TStateTracker::CustomScriptStatus& GetTrackedState(int64_t frame)
	{
		return TrackerRoot<TStateTracker>()->GetTrackedStateInternal(this, GetInputsMetadataAndCache(frame));
	}

	template <std::derived_from<Script<TResource>> TStateTracker>
		requires std::constructible_from<TStateTracker>
	bool TrackedStateExists(int64_t frame)
	{
		return TrackerRoot<TStateTracker>()->TrackedStateExistsInternal(this, GetInputsMetadataAndCache(frame));
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

	// The resource's memory, read only: the address of a symbol (LibSm64: a DLL export,
	// PyramidUpdate: a field of its own state; an unknown name throws). The one way a script
	// sees game memory (AGENTS.md, hard rule 9). A write is a hack, a kind of input the
	// framework will apply for the script (ROADMAP Phase 5), never a store through this.
	void* ReadState(const char* symbol) const;

	// The script's state, exported as another resource's state type for a top-level run on
	// that resource to import (TopLevelScriptBuilder::ImportSave): UState converts from the
	// resource it is exported from (PyramidUpdateMem from a Resource<LibSm64Mem>) plus any
	// further parameters, through Resource::State. The first form exports the current
	// frame; the second loads `frame` first and returns to the current frame after, both
	// through the script's own loads, counted like any. The constraint is Resource::State's
	// own, repeated so a frame is never taken for a state parameter when the two overload.
	template <class UState, typename... Us>
		requires(requires(TResource& r, Us&&... params) { r.template State<UState>(std::forward<Us>(params)...); })
	ImportedSave<UState> ExportSave(Us&&... params)
	{
		return ImportedSave<UState>(resource->template State<UState>(std::forward<Us>(params)...), GetCurrentFrame());
	}

	template <class UState, typename... Us>
		requires(requires(TResource& r, Us&&... params) { r.template State<UState>(std::forward<Us>(params)...); })
	ImportedSave<UState> ExportSave(int64_t frame, Us&&... params)
	{
		uint64_t currentFrame = GetCurrentFrame();
		Load(frame);
		ImportedSave<UState> save = ExportSave<UState>(std::forward<Us>(params)...);
		Load(currentFrame);
		return save;
	}

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	// TopLevelScript is the root of every hierarchy: it starts the lifecycle from outside it
	// (InitializeAndRun), stores its tracker's tag, and runs the state tracker as a child of
	// whichever script asked for a state. The template-head repeats the primary's,
	// constraints included; MSVC and Clang reject an unconstrained one (docs/compilers.md).
	template <derived_from_specialization_of<Resource> R, std::derived_from<Script<R>> T>
	friend class TopLevelScript;
	
	friend class SaveMetadata<TResource>;
	friend class InputsMetadata<TResource>;
	friend class ScriptCompareHelper<TResource>;

	SlotHandle<TResource> startSaveHandle = SlotHandle<TResource>(nullptr, -1);

	// The resource the hierarchy runs on, set by TopLevelScript when it starts. Every
	// interaction goes through the operations above; scripts never hold it (hard rule 9).
	TResource* resource = nullptr;
	
	int64_t _adhocLevel = 0;
	int64_t _initialFrame = 0;
	// One entry per ad-hoc level (see LevelStack.hpp); level 0 is the script itself.
	LevelStack<BaseScriptStatus> BaseStatus;
	LevelStack<FrameMap<int64_t, SlotHandle<TResource>>> saveBank;// contains handles to savestates
	LevelStack<FrameMap<int64_t, uint64_t>> frameCounter;// tracks opportunity cost of having to frame advance from an earlier save
	LevelStack<FrameMap<int64_t, SaveMetadata<TResource>>> saveCache;// stores metadata of ancestor saves to save recursion time
	LevelStack<FrameMap<int64_t, InputsMetadata<TResource>>> inputsCache;// caches ancestor inputs to save recursion time
	LevelStack<FrameSet<int64_t>> loadTracker;// track past loads to know whether a cached save is optimal
	Script* _parentScript;
	Script* _rootScript;
	// Set by TopLevelScript on the root; see StateTrackerTag.
	const void* _stateTrackerTag = nullptr;
	bool isStateTracker = false;
	ScriptCompareHelper<TResource> compareHelper = ScriptCompareHelper<TResource>(this);

	bool Run();

	void Initialize(Script<TResource>* parentScript);
	SaveMetadata<TResource> GetLatestSave(int64_t frame);
	SaveMetadata<TResource> GetLatestSaveAndCache(int64_t frame);
	// The inputs of a frame and who owns the state there. The walk is the second form: it
	// writes into the caller's object and asks the parent to write into the same one, so
	// nothing is copied per level; the first form is that object, built in the caller's
	// return slot (a named return value optimization every compiler applies to one named
	// object returned once). Returning temporaries on some paths and a named local on
	// another made MSVC copy the 40 bytes at the end of every level, a third of the walk's
	// time (docs/performance-changelog.md, 2026-09-15).
	InputsMetadata<TResource> GetInputsMetadata(int64_t frame);
	virtual void GetInputsMetadata(int64_t frame, InputsMetadata<TResource>& metadata);
	InputsMetadata<TResource> GetInputsMetadataAndCache(int64_t frame);
	void DeleteSave(int64_t frame, int64_t adhocLevel);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64, FrameMap<int64_t, SlotHandle<TResource>>* childSaveBank, Script<TResource>* childScript);
	void AdvanceFrameRead(uint64_t& counter);
	uint64_t GetFrameCounter(InputsMetadata<TResource> cachedInputs);
	uint64_t IncrementFrameCounter(InputsMetadata<TResource> cachedInputs);
	void ApplyChildDiff(const BaseScriptStatus& status, FrameMap<int64_t, SlotHandle<TResource>>* childSaveBank, int64_t initialFrame, Script<TResource>* childScript);
	SaveMetadata<TResource> Save(int64_t adhocLevel);
	void LoadBase(uint64_t frame, bool desync);

	// A child's save bank at `adhocLevel`, or nullptr if the child never saved (the level was
	// never created). Reverting through a pointer avoids constructing an empty map just to
	// find out it is empty.
	static FrameMap<int64_t, SlotHandle<TResource>>* SaveBankIfCreated(Script<TResource>& script, int64_t adhocLevel)
	{
		return script.saveBank.contains(adhocLevel) ? &script.saveBank[adhocLevel] : nullptr;
	}

	// The root as its TopLevelScript type. Checked by comparing type tags rather than with
	// dynamic_cast because this runs on every tracked-state lookup (ROADMAP 3.7).
	template <class TStateTracker>
	TopLevelScript<TResource, TStateTracker>* TrackerRoot()
	{
		if (_rootScript->_stateTrackerTag != &StateTrackerTag<TStateTracker>::value) [[unlikely]]
		{
			throw std::runtime_error(std::string("GetTrackedState<") + typeid(TStateTracker).name()
				+ ">: the root script's state tracker is a different type");
		}
		return static_cast<TopLevelScript<TResource, TStateTracker>*>(_rootScript);
	}

	template <typename F>
	BaseScriptStatus ExecuteAdhocBase(F adhocScript);

	template <derived_from_specialization_of<Script> TStateTracker>
	ScriptStatus<TStateTracker> ExecuteStateTracker(int64_t frame, std::shared_ptr<StateTrackerFactoryBase<TStateTracker>> stateTrackerFactory)
	{
		uint64_t initialFrame = GetCurrentFrame();

		TStateTracker script = stateTrackerFactory->Generate();
		script.Initialize(this);

		uint64_t loadCyclesStart = resource->work.loadCycles;
		uint64_t saveCyclesStart = resource->work.saveCycles;
		uint64_t advanceCyclesStart = resource->work.advanceCycles;

		uint64_t start = get_time();
		// The information is stored per frame, so we need to make sure we are there before running the state tracking script.
		script.Load(frame);

		// Set this after the load so states can be tracked efficiently if the state is in the future.
		script.isStateTracker = true;

		script.Run();
		uint64_t finish = get_time();

		BaseStatus[_adhocLevel].loadDuration = resource->work.loadCycles - loadCyclesStart;
		BaseStatus[_adhocLevel].saveDuration = resource->work.saveCycles - saveCyclesStart;
		BaseStatus[_adhocLevel].advanceFrameDuration = resource->work.advanceCycles - advanceCyclesStart;
		BaseStatus[_adhocLevel].totalDuration = finish - start;

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff, SaveBankIfCreated(script, 0), &script);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TStateTracker>(std::move(script.BaseStatus[0]), std::move(script.CustomStatus));
	}

	// Needed for state tracking. These do nothing, but TopLevelScript overrides them. Can't access explicitly because of lack of template information.
	// Tracked-state containers are created on first use, so there is no "push"; "pop" drops them.
	virtual void TrackState(Script<TResource>* /*currentScript*/, const InputsMetadata<TResource>& /*inputsMetadata*/) { return; }
	virtual bool TrackedStateExistsInternal(Script<TResource>* /*currentScript*/, const InputsMetadata<TResource>& /*inputsMetadata*/) { return false; }
	virtual void PopTrackedStatesContainer(Script<TResource>* /*currentScript*/, int64_t /*adhocLevel*/) { return; }
	virtual void MoveSyncedTrackedStates(Script<TResource>* /*sourceScript*/, int64_t /*sourceAdhocLevel*/, Script<TResource>* /*destScript*/, int64_t /*destAdhocLevel*/) { return; }
	virtual void EraseTrackedStates(Script<TResource>* /*currentScript*/, int64_t /*adhocLevel*/, int64_t /*firstFrame*/) { return; }
	
	// What the source movie's header says (its game), for ExportM64: a script asks the root,
	// and the root, a TopLevelScript, answers from its movie. Once per export, never per frame.
	virtual M64Metadata GetM64Metadata() const;
};

//Include template method implementations
#include "tasfw/Script.t.hpp"

// The root and the builders complete what Script declares (GetTrackedState reaches into
// TopLevelScript through the friend), so a script's translation unit has all three; the two
// headers are not included on their own.
#include <tasfw/TopLevelScript.hpp>
#include <tasfw/TopLevelScriptBuilder.hpp>

#endif
