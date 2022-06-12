#pragma once

#include "tasfw/resources/LibSm64.hpp"
#include <tasfw/Script.hpp>
#include <tasfw/resources/PyramidUpdate.hpp>

#ifndef SCRIPT_BITFS_PYRAMID_OSCILLATION_H
	#define SCRIPT_BITFS_PYRAMID_OSCILLATION_H

class BitFsPyramidOscillation_ParamsDto
{
public:
	int quadrant			  = 1;
	float targetXzSum		  = 0;
	float prevMaxSpeed		  = 0;
	bool brake				  = false;
	int16_t roughTargetNormal = 0;
	float initialXzSum		  = 0;
	int16_t roughTargetAngle  = 0;
};

class BitFsPyramidOscillation : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum				   = 0;
		float finalXzSum[2]				   = {0, 0};
		float maxSpeed[2]				   = {0, 0};
		float maxPassedEquilibriumSpeed[2] = {0, 0};
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation(float targetXzSum, int quadrant) :
		_targetXzSum(targetXzSum), _quadrant(quadrant)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	float _targetXzSum = 0;
	int _quadrant	   = 1;
};

// Find the optimal result of BitFsPyramidOscillation_TurnThenRunDownhill over a
// range of frames
class BitFsPyramidOscillation_Iteration : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum					= 0;
		float finalXzSum					= 0;
		float maxSpeed						= 0;
		float passedEquilibriumSpeed		= 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
		float speedBeforeTurning			= 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_Iteration(
		BitFsPyramidOscillation_ParamsDto oscillationParams, int32_t minFrame,
		int32_t maxFrame) :
		_oscillationParams(oscillationParams),
		_minFrame(minFrame),
		_maxFrame(maxFrame)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	int32_t _minFrame = -1;
	int32_t _maxFrame = -1;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnThenRunDownhill : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		float initialXzSum					= 0;
		float finalXzSum					= 0;
		float maxSpeed						= 0;
		float passedEquilibriumSpeed		= 0;
		int64_t framePassedEquilibriumPoint = -1;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill(
		BitFsPyramidOscillation_ParamsDto oscillationParams) :
		_oscillationParams(oscillationParams)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle :
	public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float initialXzSum					= 0;
		float finalXzSum					= 0;
		float maxSpeed						= 0;
		float passedEquilibriumSpeed		= 0;
		bool tooSlowForTurnAround			= false;
		bool tooUphill						= false;
		bool tooDownhill					= false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle(
		BitFsPyramidOscillation_ParamsDto oscillationParams, int16_t angle) :
		_oscillationParams(oscillationParams), _angle(angle)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	int16_t _angle;
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_TurnAroundAndRunDownhill : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed						= 0;
		float passedEquilibriumSpeed		= 0;
		float finalXzSum					= 0;
		bool tooDownhill					= false;
		bool tooUphill						= false;
		bool finishTurnaroundFailedToExpire = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_TurnAroundAndRunDownhill(
		BitFsPyramidOscillation_ParamsDto oscillationParams) :
		_oscillationParams(oscillationParams)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
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
			bool isAngleOptimal	 = false;
		};

		std::map<uint64_t, FrameInputStatus> frameStatuses;
		int64_t framePassedEquilibriumPoint = -1;
		float maxSpeed						= 0;
		float passedEquilibriumSpeed		= 0;
		float finalXzSum					= 0;
		bool tooUphill						= false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_RunDownhill(
		BitFsPyramidOscillation_ParamsDto oscillationParams) :
		_oscillationParams(oscillationParams)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	BitFsPyramidOscillation_ParamsDto _oscillationParams;
};

class BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle :
	public TopLevelScript<PyramidUpdate>
{
public:
	class CustomScriptStatus
	{
	public:
		Rotation downhillRotation		 = Rotation::NONE;
		int32_t angleFacing				 = 0;
		int32_t angleNotFacing			 = 0;
		int32_t angleFacingAnalogBack	 = 0;
		int32_t angleNotFacingAnalogBack = 0;
		int32_t floorAngle				 = 0;
		bool isSlope					 = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle(
		int16_t targetAngle) :
		_targetAngle(targetAngle), _faceAngle(targetAngle)
	{
	}
	BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle(
		int16_t targetAngle, int16_t faceAngle) :
		_targetAngle(targetAngle), _faceAngle(faceAngle)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	int16_t _faceAngle;
	int16_t _targetAngle;
};

#endif