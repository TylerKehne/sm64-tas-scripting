#pragma once
#include <unordered_map>
#include <tasfw/Game.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>
#include <tasfw/ScriptStatus.hpp>

#ifndef SCRIPT_H
#define SCRIPT_H

class Script;
class SlotHandle;
class Game;
class TopLevelScript;

template <typename F, typename R = std::invoke_result_t<F>>
concept AdhocScript = std::same_as<R, bool>;

template <typename F, typename T>
concept AdhocCustomStatusScript = std::same_as<std::invoke_result_t<F, T&>, bool>;

/// <summary>
/// Execute a state-changing operation on the game. Parameters should correspond
/// to the script's class constructor.
/// </summary>
class Script
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus = {};

	// TODO: make private
	Game* game = nullptr;

	Script(Script* parentScript) : _parentScript(parentScript)
	{
		if (_parentScript)
		{
			game = _parentScript->game;
			_initialFrame = GetCurrentFrame();
		}
	}

	// TODO: move this method to some utility class
	static void CopyVec3f(Vec3f dest, Vec3f source);

protected:
	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params);

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params);

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Test(Us&&... params);

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
	virtual Inputs GetInputs(int64_t frame);

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	friend class SlotManager;
	friend class TopLevelScript;

	int64_t _adhocLevel = 0;
	int32_t _initialFrame = 0;
	std::vector<BaseScriptStatus> BaseStatus = { BaseScriptStatus() };
	std::vector<std::map<int64_t, SlotHandle>> saveBank = { std::map<int64_t, SlotHandle>() };
	std::vector<std::map<int64_t, uint64_t>> frameCounter = { std::map<int64_t, uint64_t>() };
	Script* _parentScript;

	bool checkPreconditions();
	bool execute();
	bool checkPostconditions();

	std::pair<int64_t, SlotHandle*> GetLatestSave(int64_t frame);
	void DeleteSave(int64_t frame, int64_t adhocLevel);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64);
	virtual Inputs GetInputsTracked(uint64_t frame, uint64_t& counter);
	void AdvanceFrameRead(uint64_t& counter);
	virtual uint64_t GetFrameCounter(int64_t frame);
	void ApplyChildDiff(const BaseScriptStatus& status, int64_t initialFrame);

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

class TopLevelScript : public Script
{
public:
	TopLevelScript(M64& m64, Game* game) : Script(nullptr), _m64(m64)
	{
		this->game = game;
	}

	template <std::derived_from<TopLevelScript> TTopLevelScript, std::derived_from<Game> TGame, typename... Ts>
		requires(std::constructible_from<TGame, Ts...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, Ts&&... params);

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

	Inputs GetInputs(int64_t frame) override;

protected:
	M64& _m64;

private:
	Inputs GetInputsTracked(uint64_t frame, uint64_t& counter) override;
	uint64_t GetFrameCounter(int64_t frame) override;
};

//Include template method implementations
#include "tasfw/Script.t.hpp"

#endif
