#pragma once

#include <tasfw/Script.hpp>
#include "Sm64Resource.hpp"
#include <General.hpp>

#ifndef SCRIPT_BITFS_PYRAMID_OSCILLATION_H
#define SCRIPT_BITFS_PYRAMID_OSCILLATION_H

class BitFsPyramidOscillation_ParamsDto
{
public:
	int quadrant = 1;
	float targetXzSum = 0;
	float prevMaxSpeed = 0;
	bool brake = false;
	float initialXzSum = 0;
	int16_t roughTargetAngle = 0;
	bool optimizeMaxSpeed = false;
	bool ignoreXzSum = false;
};

class BitFsPyramidOscillation : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum[2] = { 0, 0 };
		float maxSpeed[2] = { 0, 0 };
		float maxPassedEquilibriumSpeed[2] = { 0, 0 };
		float maxPassedEquilibriumXzDist[2] = { 0, 0 };
		std::vector<std::pair<int64_t, int64_t>> oscillationMinMaxFrames;
		float actualMaxSpeed[2] = { 0, 0 };
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(float targetXzSum, int quadrant, bool alwaysBrake = false) : _targetXzSum(targetXzSum), _quadrant(quadrant), _alwaysBrake(alwaysBrake) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	float _targetXzSum = 0;
	int lowerXzSumParity = 1;
	int _quadrant = 1;
	bool _alwaysBrake = false;
};

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float passedEquilibriumXzDist = 0;
		float finalXzSum = 0;
		bool tooDownhill = false;
		bool tooUphill = false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnAroundAndRunDownhill(BitFsPyramidOscillation_ParamsDto oscillationParams)
		: _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float passedEquilibriumXzDist = 0;
		bool tooSlowForTurnAround = false;
		bool tooUphill = false;
		bool tooDownhill = false;
		bool finishTurnaroundFailedToExpire = false;
		int16_t angle;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle(BitFsPyramidOscillation_ParamsDto oscillationParams, int16_t angle)
		: _angle(angle), _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	int16_t _angle;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;

	bool CompareSpeed(
		const ScriptStatus<BitFsPyramidOscillation_TurnAroundAndRunDownhill>& status1,
		const ScriptStatus<BitFsPyramidOscillation_TurnAroundAndRunDownhill>& status2);
};

class BitFsPyramidOscillation_TurnThenRunDownhill : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float passedEquilibriumXzDist = 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill(BitFsPyramidOscillation_ParamsDto oscillationParams)
		: _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;

	bool CompareSpeed(
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>& status1,
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>& status2);
};

// Find the optimal result of BitFsPyramidOscillation_TurnThenRunDownhill over a
// range of frames
class BitFsPyramidOscillation_Iteration : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float passedEquilibriumXzDist = 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
		float speedBeforeTurning = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_Iteration(BitFsPyramidOscillation_ParamsDto oscillationParams, int64_t minFrame, int64_t maxFrame)
		: _minFrame(minFrame), _maxFrame(maxFrame), _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	int64_t _minFrame = -1;
	int64_t _maxFrame = -1;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;

	bool CompareSpeed(
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill>& status1,
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill>& status2);
};

class BitFsPyramidOscillation_RunDownhill : public Script<Sm64Resource>
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
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float passedEquilibriumXzDist = 0;
		float finalXzSum = 0;
		bool tooUphill = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(BitFsPyramidOscillation_ParamsDto oscillationParams)
		: _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};


#endif