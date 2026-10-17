#pragma once

#include <tasfw/Script.hpp>
#include <Sm64Resource.hpp>

// The downhill angle one frame on: where the pyramid's tilt toward Mario leaves the floor
// he stands on, and the facing angles that run down it or just short of uphill. One frame of
// its own, reverted with it (ROADMAP 4.13: what the PyramidUpdate stand-in used to answer,
// asked of the resource itself).
class GetMinimumDownhillWalkingAngle : public Script<Sm64Resource>
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
		float steepness = 0;
		bool isSlope = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	GetMinimumDownhillWalkingAngle(int16_t targetAngle) : _faceAngle(targetAngle), _targetAngle(targetAngle) { }
	GetMinimumDownhillWalkingAngle(int16_t targetAngle, int16_t faceAngle) :  _faceAngle(faceAngle), _targetAngle(targetAngle) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	static bool FloorIsSlope(float normalY, uint32_t action);

	// How far inside the downhill half the facing it reports lies, in angle units: the
	// top face's two triangles and the stick's rounding move the boundary by up to about
	// 20 (the comment in execution).
	static constexpr int32_t DownhillMargin = 64;

	int16_t _faceAngle;
	int16_t _targetAngle;
};

class TryHackedWalkOutOfBounds : public Script<Sm64Resource>
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

	TryHackedWalkOutOfBounds(float speed) : _speed(speed) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	float _speed;
};

class BrakeToIdle : public Script<Sm64Resource>
{
public:
	class CustomScriptStatus
	{
	public:
		int32_t decelerationFrames = -1;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BrakeToIdle() { }

	bool validation();
	bool execution();
	bool assertion();
};
