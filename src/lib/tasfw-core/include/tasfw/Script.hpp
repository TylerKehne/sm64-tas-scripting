#pragma once
#include <unordered_map>
#include <tasfw/Game.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>


class Script;
class SlotHandle;
class Game;
class TopLevelScript;

class BaseScriptStatus
{
public:
	bool validated = false;
	bool executed = false;
	bool asserted = false;
	bool validationThrew = false;
	bool executionThrew = false;
	bool assertionThrew = false;
	uint64_t validationDuration = 0;
	uint64_t executionDuration = 0;
	uint64_t assertionDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() = default;
};

template <std::derived_from<Script> TScript>
class ScriptStatus : public BaseScriptStatus, public TScript::CustomScriptStatus
{
public:
	ScriptStatus() : BaseScriptStatus(), TScript::CustomScriptStatus() {}

	ScriptStatus(
		BaseScriptStatus baseStatus,
		typename TScript::CustomScriptStatus customStatus) :
		BaseScriptStatus(baseStatus), TScript::CustomScriptStatus(customStatus)
	{
	}
};

/// <summary>
/// Execute a state-changing operation on the game. Parameters should correspond
/// to the script's class constructor.
/// </summary>
class Script
{
public:
	class CustomScriptStatus
	{
	};
	CustomScriptStatus CustomStatus = {};
	BaseScriptStatus BaseStatus;
	Script* _parentScript;
	std::map<uint64_t, SlotHandle> saveBank;
	std::map<int64_t, uint64_t> frameCounter;
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
	uint32_t _initialFrame = 0;

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();
		OptionalSave();

		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		// Load if necessary
		Revert(initialFrame, script.BaseStatus.m64Diff);

		BaseStatus.nLoads += script.BaseStatus.nLoads;
		BaseStatus.nSaves += script.BaseStatus.nSaves;
		BaseStatus.nFrameAdvances += script.BaseStatus.nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();
		OptionalSave();

		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		// Revert state if assertion fails, otherwise apply diff
		if (!script.BaseStatus.asserted || script.BaseStatus.m64Diff.frames.empty())
			Revert(initialFrame, script.BaseStatus.m64Diff);
		else
		{
			uint64_t firstFrame = script.BaseStatus.m64Diff.frames.begin()->first;
			uint64_t lastFrame = script.BaseStatus.m64Diff.frames.rbegin()->first;

			// Erase all saves after starting frame of diff
			auto firstInvalidFrame = saveBank.upper_bound(firstFrame);
			saveBank.erase(firstInvalidFrame, saveBank.end());

			//Apply diff. State is already synced from child script, so no need to update it
			int64_t frame = firstFrame;
			while (frame <= lastFrame)
			{
				if (script.BaseStatus.m64Diff.frames.count(frame))
					BaseStatus.m64Diff.frames[frame] = script.BaseStatus.m64Diff.frames.at(frame);
				frame++;
			}

			//Forward state to end of diff
			Load(lastFrame + 1);
		}

		BaseStatus.nLoads += script.BaseStatus.nLoads;
		BaseStatus.nSaves += script.BaseStatus.nSaves;
		BaseStatus.nFrameAdvances += script.BaseStatus.nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Test(Us&&... params)
	{
		ScriptStatus<TScript> status =
			Execute<TScript>(std::forward<Us>(params)...);

		status.m64Diff = M64Diff();

		return status;
	}

	bool checkPreconditions();
	bool execute();
	bool checkPostconditions();

	// TODO: move this method to some utility class
	template <typename T>
	int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	uint64_t GetCurrentFrame();
	void Apply(const M64Diff& m64Diff);
	void AdvanceFrameRead();
	void AdvanceFrameWrite(Inputs inputs);
	void OptionalSave();
	void Save();
	void Save(uint64_t frame);
	void Load(uint64_t frame);
	void Rollback(uint64_t frame);
	void RollForward(int64_t frame);
	void Restore(int64_t frame);

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	friend class SlotManager;
	friend class TopLevelScript;

	std::pair<uint64_t, SlotHandle*> GetLatestSave(uint64_t frame);
	void DeleteSave(int64_t frame);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64);
	Inputs GetInputsTracked(uint64_t frame);
	virtual Inputs GetInputsTracked(uint64_t frame, uint64_t& counter);
	void AdvanceFrameRead(uint64_t& counter);
	virtual uint64_t GetFrameCounter(int64_t frame);
};

class TopLevelScript : public Script
{
public:
	TopLevelScript(M64& m64, Game* game) : Script(nullptr), _m64(m64)
	{
		this->game = game;
	}

	template <
		std::derived_from<TopLevelScript> TTopLevelScript,
		std::derived_from<Game> TGame, typename... Ts>
		requires(std::constructible_from<TGame, Ts...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, Ts&&... params)
	{
		TGame game = TGame(std::forward<Ts>(params)...);

		TTopLevelScript script = TTopLevelScript(m64, &game);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		return ScriptStatus<TTopLevelScript>(
			script.BaseStatus, script.CustomStatus);
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

protected:
	M64& _m64;

private:
	Inputs GetInputsTracked(uint64_t frame, uint64_t& counter) override;
	uint64_t GetFrameCounter(int64_t frame) override;
};
