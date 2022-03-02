#pragma once
#include <unordered_map>
#include <tasfw/Game.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>


class Script;

class BaseScriptStatus
{
public:
	bool verified									= false;
	bool executed									= false;
	bool validated								= false;
	bool verificationThrew				= false;
	bool executionThrew						= false;
	bool validationThrew					= false;
	uint64_t verificationDuration = 0;
	uint64_t executionDuration		= 0;
	uint64_t validationDuration		= 0;
	uint64_t nLoads								= 0;
	uint64_t nSaves								= 0;
	uint64_t nFrameAdvances				= 0;
	M64Diff m64Diff								= M64Diff();

	BaseScriptStatus() {}
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
	std::map<uint64_t, Slot> saveBank;
	Game* game = NULL;

	Script(Script* parentScript)
	{
		_parentScript = parentScript;
		if (_parentScript)
		{
			game					= _parentScript->game;
			_initialFrame = GetCurrentFrame();
		}
	}

	// TODO: move this method to some utility class
	static void CopyVec3f(Vec3f dest, Vec3f source);

	virtual Inputs GetInputs(uint64_t frame);

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

		if (script.verify() && script.execute())
			script.validate();

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
		ScriptStatus<TScript> status =
			Execute<TScript>(std::forward<Us>(params)...);
		if (status.validated)
			Apply(status.m64Diff);

		return status;
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

	bool verify();
	bool execute();
	bool validate();

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

	virtual bool verification() = 0;
	virtual bool execution()		= 0;
	virtual bool validation()		= 0;

private:
	std::pair<uint64_t, Slot*> GetLatestSave(uint64_t frame);
	bool RemoveEarliestSave(uint64_t earliestFrame = 0);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64);
};

class TopLevelScript : public Script
{
public:
	TopLevelScript(M64& m64, Game* game) : Script(NULL), _m64(m64)
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

		if (script.verify() && script.execute())
			script.validate();

		return ScriptStatus<TTopLevelScript>(
			script.BaseStatus, script.CustomStatus);
	}

	virtual bool verification() override = 0;
	virtual bool execution() override		 = 0;
	virtual bool validation() override	 = 0;

	Inputs GetInputs(uint64_t frame) override;

protected:
	M64& _m64;
};
