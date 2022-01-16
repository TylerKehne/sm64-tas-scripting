#pragma once
#include "Inputs.hpp"
#include "Game.hpp"
#include "Types.hpp"
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
	BaseScriptStatus BaseStatus = BaseScriptStatus();

	Script(M64* m64) : _m64(m64)
	{
		_initialFrame = game.getCurrentFrame();

		game.save_state(&_initialSave);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Us...>)
	static ScriptStatus<TScript> Execute(Us&&... params)
	{
		TScript script = TScript(std::forward<Us>(params)...);

		if (script.verify() && script.execute())
			script.validate();
		
		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	static void CopyVec3f(Vec3f dest, Vec3f source);

protected:
	M64* _m64;
	uint32_t _initialFrame = -1;
	Slot _initialSave = game.alloc_slot();

	bool verify();
	bool execute();
	bool validate();

	virtual bool verification() = 0;
	virtual bool execution() = 0;
	virtual bool validation() = 0;	
};

class RunDownhillOnInvertedPyramid: public Script
{
public:
	RunDownhillOnInvertedPyramid(M64* m64) : Script(m64) {}

	bool verification();
	bool execution();
	bool validation();
};

class AdvanceM64FromStartToFrame: public Script
{
public:
	AdvanceM64FromStartToFrame(M64* m64, uint64_t frame) : Script(m64), _frame(frame) {}

	bool verification();
	bool execution();
	bool validation();

private:
	uint64_t _frame;
};

class GetMinimumDownhillWalkingAngle: public Script
{
public:
	class CustomScriptStatus
	{
	public:
		bool isSlope = false;
		int16_t angleFacing = 0;
		int16_t angleNotFacing = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	GetMinimumDownhillWalkingAngle(M64* m64) : Script(m64) {}

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

	TryHackedWalkOutOfBounds(M64* m64, float speed) : Script(m64), _speed(speed) {}

	bool verification();
	bool execution();
	bool validation();

private:
	float _speed;
};

#endif

