#pragma once

#include <tasfw/Script.hpp>
#include "tasfw/resources/LibSm64.hpp"
#include <tasfw/resources/PyramidUpdate.hpp>

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

class BitFsPyramidOscillation : public Script<LibSm64>
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

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script<LibSm64>
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

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle : public Script<LibSm64>
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

class BitFsPyramidOscillation_TurnThenRunDownhill : public Script<LibSm64>
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
class BitFsPyramidOscillation_Iteration : public Script<LibSm64>
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

	BitFsPyramidOscillation_Iteration(BitFsPyramidOscillation_ParamsDto oscillationParams, int32_t minFrame, int32_t maxFrame)
		: _minFrame(minFrame), _maxFrame(maxFrame), _oscillationParams(oscillationParams) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	int32_t _minFrame = -1;
	int32_t _maxFrame = -1;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;

	bool CompareSpeed(
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill>& status1,
		const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill>& status2);
};

class BitFsPyramidOscillation_RunDownhill : public Script<LibSm64>
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

class BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle : public TopLevelScript<PyramidUpdate>
{
public:
	class CustomScriptStatus
	{
	public:
		Rotation downhillRotation = Rotation::NONE;
		int32_t angleFacing = 0;
		int32_t angleNotFacing = 0;
		int32_t angleFacingAnalogBack = 0;
		int32_t angleNotFacingAnalogBack = 0;
		int32_t floorAngle = 0;
		bool isSlope = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle(int16_t targetAngle) : _faceAngle(targetAngle), _targetAngle(targetAngle) { }
	BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle(int16_t targetAngle, int16_t faceAngle) : _faceAngle(faceAngle), _targetAngle(targetAngle) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	int16_t _faceAngle;
	int16_t _targetAngle;
};

#endif