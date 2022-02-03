#pragma once

#include "Script.hpp"

#ifndef SCRIPT_GENERAL_H
#define SCRIPT_GENERAL_H

class GetMinimumDownhillWalkingAngle : public Script
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

#endif
