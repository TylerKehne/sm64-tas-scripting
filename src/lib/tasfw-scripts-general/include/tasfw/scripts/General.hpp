#pragma once

#include "tasfw/resources/LibSm64.hpp"
#include <tasfw/Script.hpp>

class GetMinimumDownhillWalkingAngle : public Script<LibSm64>
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

	GetMinimumDownhillWalkingAngle(int16_t targetAngle) :
		_targetAngle(targetAngle), _faceAngle(targetAngle)
	{
	}
	GetMinimumDownhillWalkingAngle(int16_t targetAngle, int16_t faceAngle) :
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

class TryHackedWalkOutOfBounds : public Script<LibSm64>
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

	TryHackedWalkOutOfBounds(float speed) : _speed(speed) {}

	bool validation();
	bool execution();
	bool assertion();

private:
	float _speed;
};

class BrakeToIdle : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		int32_t decelerationFrames = -1;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BrakeToIdle() {}

	bool validation();
	bool execution();
	bool assertion();
};
