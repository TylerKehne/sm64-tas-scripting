#pragma once
#include "Inputs.hpp"
#include "Game.hpp"
#include "Types.hpp"
#include "ObjectFields.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"
#include "Trig.hpp"
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
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() {}
};

template<std::derived_from<Script> TScript> class ScriptStatus
	: public BaseScriptStatus, public TScript::CustomScriptStatus
{
public:
	ScriptStatus<TScript>() : BaseScriptStatus(), TScript::CustomScriptStatus() {}

	ScriptStatus<TScript>(BaseScriptStatus baseStatus, TScript::CustomScriptStatus customStatus)
		: BaseScriptStatus(baseStatus), TScript::CustomScriptStatus(customStatus) {}
};

/// <summary>
/// Execute a state-changing operation on the game-> Parameters should correspond to the script's class constructor.
/// </summary>
class Script
{
public:
	class CustomScriptStatus {};
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
			game = _parentScript->game;
			_initialFrame = game->getCurrentFrame();
		}
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Us...>)
	static ScriptStatus<TScript> Main(Us&&... params)
	{
		TScript script = TScript(std::forward<Us>(params)...);

		if (script.verify() && script.execute())
			script.validate();

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		//Save state if performant
		uint64_t initialFrame = game->getCurrentFrame();
		OptionalSave();

		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.verify() && script.execute())
			script.validate();
		
		//Load if necessary
		Revert(initialFrame, script.BaseStatus.m64Diff);

		BaseStatus.nLoads += script.BaseStatus.nLoads;
		BaseStatus.nSaves += script.BaseStatus.nSaves;
		BaseStatus.nFrameAdvances += script.BaseStatus.nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus, script.CustomStatus);
	}

	template<std::derived_from<Script> TScript, typename... Us>
	requires (std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		ScriptStatus<TScript> status = Execute<TScript>(std::forward<Us>(params)...);
		if (status.validated)
			Apply(status.m64Diff);

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

	virtual Inputs GetInputs(uint64_t frame);

protected:
	uint32_t _initialFrame = 0;

	bool verify();
	bool execute();
	bool validate();

	template <typename T> int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	void Apply(const M64Diff& m64Diff);
	void AdvanceFrameRead();
	void AdvanceFrameWrite(Inputs inputs);
	void OptionalSave();
	void Save();
	void Save(uint64_t frame);
	void Load(uint64_t frame);
	void Rollback(uint64_t frame);

	virtual bool verification() = 0;
	virtual bool execution() = 0;
	virtual bool validation() = 0;	

private:
	std::pair<uint64_t, Slot*> GetLatestSave(Script* script, uint64_t frame);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64);
};

class MainScript : public Script
{
public:
	MainScript(M64* m64, Game* game) : Script(NULL), _m64(m64) { this->game = game; }

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
		float passedEquilibriumSpeed = 0;
		int64_t framePassedEquilibriumPoint = -1;
		float finalXzSum = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(Script* parentScript, int16_t roughTargetAngle = 0)
		: Script(parentScript), _roughTargetAngle(roughTargetAngle) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int _targetXDirection = 0;
	int _targetZDirection = 0;
	int16_t _roughTargetAngle = 0;
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
		int32_t angleFacingAnalogBack = 0;
		int32_t angleNotFacingAnalogBack = 0;
		Rotation downhillRotation = Rotation::NONE;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	GetMinimumDownhillWalkingAngle(Script* parentScript, int16_t targetAngle)
		: Script(parentScript), _targetAngle(targetAngle), _faceAngle(targetAngle) {}
	GetMinimumDownhillWalkingAngle(Script* parentScript, int16_t targetAngle, int16_t faceAngle)
		: Script(parentScript), _targetAngle(targetAngle), _faceAngle(faceAngle) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _faceAngle;
	int16_t _targetAngle;
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
		uint16_t floorAngle;
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
		float maxSpeed[2] = { 0, 0 };
		float maxPassedEquilibriumSpeed[2] = {0, 0};
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(Script* parentScript) : Script(parentScript) {}

	bool verification();
	bool execution();
	bool validation();
};

class BitFsPyramidOscillation_TurnThenRunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		int64_t framePassedEquilibriumPoint = -1;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill(Script* parentScript, float prevMaxSpeed, float minXzSum)
		: Script(parentScript), _prevMaxSpeed(prevMaxSpeed), _minXzSum(minXzSum) {}

	bool verification();
	bool execution();
	bool validation();

private:
	float _prevMaxSpeed = 0;
	float _minXzSum = 0;
};

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool tooSlowForTurnAround = false;
		bool tooUphill = false;
		bool tooDownhill = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle(Script* parentScript, int16_t angle, float prevMaxSpeed, float minXzSum)
		: Script(parentScript), _angle(angle), _prevMaxSpeed(prevMaxSpeed), _minXzSum(minXzSum) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _angle;
	float _prevMaxSpeed = 0;
	float _minXzSum = 0;
};

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		int64_t framePassedEquilibriumPoint = -1;
		float finalXzSum = 0;
		bool tooDownhill = false;
		bool tooUphill = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnAroundAndRunDownhill(Script* parentScript, int16_t roughTargetAngle = 0, float minXzSum = 0)
		: Script(parentScript), _roughTargetAngle(roughTargetAngle), _minXzSum(minXzSum) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _roughTargetAngle = 0;
	float _minXzSum = 0;
};

#endif

