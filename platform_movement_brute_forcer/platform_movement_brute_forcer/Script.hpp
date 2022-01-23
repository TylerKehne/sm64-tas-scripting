#pragma once
#include "Inputs.hpp"
#include "Game.hpp"
#include "Types.hpp"
#include "ObjectFields.hpp"
#include <unordered_map>

#ifndef SCRIPT_H
#define SCRIPT_H

class Script;

class BaseScriptStatus
{
public:
	bool verified = false;
	bool executed = false;
	bool validated = false;
	bool verificationThrew = false;
	bool executionThrew = false;
	bool validationThrew = false;
	uint64_t verificationDuration = 0;
	uint64_t executionDuration = 0;
	uint64_t validationDuration = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() {}

	BaseScriptStatus(uint64_t initFrame) : m64Diff(M64Diff(initFrame)) {}
};

template<std::derived_from<Script> TScript> class ScriptStatus
	: public BaseScriptStatus, public TScript::CustomScriptStatus
{
public:
	ScriptStatus<TScript>(BaseScriptStatus baseStatus, TScript::CustomScriptStatus customStatus)
		: BaseScriptStatus(baseStatus), TScript::CustomScriptStatus(customStatus) {}
};

/// <summary>
/// Execute a state-changing operation on the game. Parameters should correspond to the script's class constructor.
/// </summary>
class Script
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus = {};
	BaseScriptStatus BaseStatus;
	Slot _initialSave = game.alloc_slot();
	Script* _parentScript;

	Script(Script* parentScript)
	{
		_parentScript = parentScript;
		_initialFrame = game.getCurrentFrame();
		BaseStatus = BaseScriptStatus(_initialFrame);
		game.save_state(&_initialSave);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Us...>)
	static ScriptStatus<TScript> Main(Us&&... params)
	{
		TScript script = TScript(std::forward<Us>(params)...);

		if (script.verify() && script.execute())
			script.validate();

		game.load_state(&script._initialSave);

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.verify() && script.execute())
			script.validate();
		
		game.load_state(&script._initialSave);

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		ScriptStatus<TScript> status = Execute<TScript>(std::forward<Us>(params)...);
		if (status.validated)
			Apply(&status.m64Diff);

		return status;
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Test(Us&&... params)
	{
		ScriptStatus<TScript> status = Execute<TScript>(std::forward<Us>(params)...);

		status.m64Diff = M64Diff();

		return status;
	}

	static void CopyVec3f(Vec3f dest, Vec3f source);

	void advance_frame();
	void advance_frame(Inputs inputs);
	void load_state(Slot* saveState);
	void GoToFrame(uint64_t frame);
	void StartToFrame(uint64_t frame);
	virtual Inputs GetInputs(uint64_t frame);

protected:
	uint32_t _initialFrame = -1;

	bool verify();
	bool execute();
	bool validate();

	template <typename T> int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	void Apply(M64Diff* m64Diff);

	virtual bool verification() = 0;
	virtual bool execution() = 0;
	virtual bool validation() = 0;	
};

class MainScript : public Script
{
public:
	MainScript(M64* m64) : Script(NULL), _m64(m64) {}

	bool verification();
	bool execution();
	bool validation();

	Inputs GetInputs(uint64_t frame);
private:
	M64* _m64;
};

class BitFsPyramidOscillation_RunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		class FrameInputStatus
		{
		public:
			bool isAngleDownhill = false;
			bool isAngleOptimal = false;
		};

		std::map<uint64_t, FrameInputStatus> frameStatuses;
		float maxSpeed = 0;
		int64_t framePassedEquilibriumPoint = -1;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(Script* parentScript) : Script(parentScript) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int _initXDirection = 0;
	int _initZDirection = 0;
};

class GetMinimumDownhillWalkingAngle: public Script
{
public:
	class CustomScriptStatus
	{
	public:
		bool isSlope = false;
		int32_t angleFacing = 0;
		int32_t angleNotFacing = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	GetMinimumDownhillWalkingAngle(Script* parentScript) : Script(parentScript) {}

	bool verification();
	bool execution();
	bool validation();
};

class TryHackedWalkOutOfBounds : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		Vec3f startPos;
		Vec3f endPos;
		float startSpeed;
		float endSpeed;
		uint32_t endAction;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	TryHackedWalkOutOfBounds(Script* parentScript, float speed) : Script(parentScript), _speed(speed) {}

	bool verification();
	bool execution();
	bool validation();

private:
	float _speed;
};

class BitFsPyramidOscillation : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(Script* parentScript) : Script(parentScript) {}

	bool verification();
	bool execution();
	bool validation();
};

#endif

