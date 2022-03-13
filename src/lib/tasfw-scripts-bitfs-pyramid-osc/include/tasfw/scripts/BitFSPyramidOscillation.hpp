#pragma once

#include <tasfw/Script.hpp>

#ifndef SCRIPT_BITFS_PYRAMID_OSCILLATION_H
	#define SCRIPT_BITFS_PYRAMID_OSCILLATION_H

class BitFsPyramidOscillation_ParamsDto
{
public:
	int quadrant = 1;
	float targetXzSum = 0;
	float prevMaxSpeed = 0;
	bool brake = false;
	int16_t roughTargetNormal = 0;
	float initialXzSum = 0;
	int16_t roughTargetAngle = 0;

};

class BitFsPyramidOscillation : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum[2] = { 0, 0 };
		float maxSpeed[2] = { 0, 0 };
		float maxPassedEquilibriumSpeed[2] = { 0, 0 };
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(Script* parentScript, float targetXzSum, int quadrant)
		: Script(parentScript), _targetXzSum(targetXzSum), _quadrant(quadrant) {}

	bool verification();
	bool execution();
	bool validation();

private:
	float _targetXzSum = 0;
	int _quadrant = 1;
};

// Find the optimal result of BitFsPyramidOscillation_TurnThenRunDownhill over a
// range of frames
class BitFsPyramidOscillation_Iteration : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum									= 0;
		float finalXzSum										= 0;
		float maxSpeed											= 0;
		float passedEquilibriumSpeed				= 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
		float speedBeforeTurning						= 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_Iteration(Script* parentScript, BitFsPyramidOscillation_ParamsDto oscillationParams, int32_t minFrame, int32_t maxFrame)
		: Script(parentScript), _oscillationParams(oscillationParams), _minFrame(minFrame), _maxFrame(maxFrame) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int32_t _minFrame = -1;
	int32_t _maxFrame = -1;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnThenRunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum									= 0;
		float finalXzSum										= 0;
		float maxSpeed											= 0;
		float passedEquilibriumSpeed				= 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill(Script* parentScript, BitFsPyramidOscillation_ParamsDto oscillationParams)
		: Script(parentScript), _oscillationParams(oscillationParams) {}

	bool verification();
	bool execution();
	bool validation();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float initialXzSum									= 0;
		float finalXzSum										= 0;
		float maxSpeed											= 0;
		float passedEquilibriumSpeed				= 0;
		bool tooSlowForTurnAround						= false;
		bool tooUphill											= false;
		bool tooDownhill										= false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle(
		Script* parentScript, BitFsPyramidOscillation_ParamsDto oscillationParams, int16_t angle)
		: Script(parentScript), _oscillationParams(oscillationParams), _angle(angle){}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _angle;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed											= 0;
		float passedEquilibriumSpeed				= 0;
		float finalXzSum										= 0;
		bool tooDownhill										= false;
		bool tooUphill											= false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnAroundAndRunDownhill(
		Script* parentScript, BitFsPyramidOscillation_ParamsDto oscillationParams)
		: Script(parentScript), _oscillationParams(oscillationParams) {}

	bool verification();
	bool execution();
	bool validation();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
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
			bool isAngleOptimal	 = false;
		};

		std::map<uint64_t, FrameInputStatus> frameStatuses;
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float finalXzSum = 0;
		bool tooUphill = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(Script* parentScript, BitFsPyramidOscillation_ParamsDto oscillationParams)
		: Script(parentScript), _oscillationParams(oscillationParams) {}

	bool verification();
	bool execution();
	bool validation();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

#endif