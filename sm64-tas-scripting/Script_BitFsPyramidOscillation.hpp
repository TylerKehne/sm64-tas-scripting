#pragma once

#include "Script.hpp"

#ifndef SCRIPT_BITFS_PYRAMID_OSCILLATION_H
#define SCRIPT_BITFS_PYRAMID_OSCILLATION_H

class BitFsPyramidOscillation : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum = 0;
		float finalXzSum = 0;
		float maxSpeed[2] = { 0, 0 };
		float maxPassedEquilibriumSpeed[2] = { 0, 0 };
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(Script* parentScript) : Script(parentScript) {}

	bool verification();
	bool execution();
	bool validation();
};

//Find the optimal result of BitFsPyramidOscillation_TurnThenRunDownhill over a range of frames
class BitFsPyramidOscillation_Iteration : public Script
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
		bool finishTurnaroundFailedToExpire = false;
		float speedBeforeTurning = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_Iteration(Script* parentScript, int32_t minFrame, int32_t maxFrame, float prevMaxSpeed, float minXzSum, bool brake)
		: Script(parentScript), _prevMaxSpeed(prevMaxSpeed), _minXzSum(minXzSum), _brake(brake), _minFrame(minFrame), _maxFrame(maxFrame) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int32_t _minFrame = -1;
	int32_t _maxFrame = -1;
	float _prevMaxSpeed = 0;
	float _minXzSum = 0;
	bool _brake = false;
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
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill(Script* parentScript, float prevMaxSpeed, float minXzSum, bool brake)
		: Script(parentScript), _prevMaxSpeed(prevMaxSpeed), _minXzSum(minXzSum), _brake(brake) {}

	bool verification();
	bool execution();
	bool validation();

private:
	float _prevMaxSpeed = 0;
	float _minXzSum = 0;
	bool _brake = false;
};

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle: public Script
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
		bool tooSlowForTurnAround = false;
		bool tooUphill = false;
		bool tooDownhill = false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle(Script* parentScript, int16_t angle, float prevMaxSpeed, float minXzSum, bool brake)
		: Script(parentScript), _angle(angle), _prevMaxSpeed(prevMaxSpeed), _minXzSum(minXzSum), _brake(brake) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _angle;
	float _prevMaxSpeed = 0;
	float _minXzSum = 0;
	bool _brake = false;
};

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float finalXzSum = 0;
		bool tooDownhill = false;
		bool tooUphill = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnAroundAndRunDownhill(Script* parentScript, bool brake, int16_t roughTargetAngle = 0, float minXzSum = 0)
		: Script(parentScript), _roughTargetAngle(roughTargetAngle), _minXzSum(minXzSum), _brake(brake) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int16_t _roughTargetAngle = 0;
	float _minXzSum = 0;
	bool _brake = false;
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
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed = 0;
		float passedEquilibriumSpeed = 0;
		float finalXzSum = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(Script* parentScript, int16_t roughTargetAngle)
		: Script(parentScript), _roughTargetAngle(roughTargetAngle) {}

	bool verification();
	bool execution();
	bool validation();

private:
	int _targetXDirection = 0;
	int _targetZDirection = 0;
	int16_t _roughTargetAngle = 0;
};

#endif