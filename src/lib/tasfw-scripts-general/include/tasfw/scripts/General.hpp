#pragma once

#include <tasfw/Script.hpp>

class GetMinimumDownhillWalkingAngle : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		Rotation downhillRotation				 = Rotation::NONE;
		int32_t angleFacing							 = 0;
		int32_t angleNotFacing					 = 0;
		int32_t angleFacingAnalogBack		 = 0;
		int32_t angleNotFacingAnalogBack = 0;
		int32_t floorAngle							 = 0;
		bool isSlope										 = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	GetMinimumDownhillWalkingAngle(Script* parentScript, int16_t targetAngle) :
		Script(parentScript), _targetAngle(targetAngle), _faceAngle(targetAngle)
	{
	}
	GetMinimumDownhillWalkingAngle(
		Script* parentScript, int16_t targetAngle, int16_t faceAngle) :
		Script(parentScript), _targetAngle(targetAngle), _faceAngle(faceAngle)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

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

	TryHackedWalkOutOfBounds(Script* parentScript, float speed) :
		Script(parentScript), _speed(speed)
	{
	}

	bool validation();
	bool execution();
	bool assertion();

private:
	float _speed;
};

class BrakeToIdle : public Script
{
public:
	class CustomScriptStatus
	{
	public:
		int32_t decelerationFrames = -1;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BrakeToIdle(Script* parentScript) : Script(parentScript) {}

	bool validation();
	bool execution();
	bool assertion();
};
