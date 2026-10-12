#include <General.hpp>

#include <tasfw/Script.hpp>
#include <ScriptMath.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>

#include <cmath>
#include <cstdlib>

bool GetMinimumDownhillWalkingAngle::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (ReadState("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (ReadState("bhvBitfsTiltingInvertedPyramid"));
	return floorObject->behavior == pyramidBehavior;
}

bool GetMinimumDownhillWalkingAngle::execution()
{
	MarioState* marioState = (MarioState*) (ReadState("gMarioStates"));

	// The pyramid tilts toward where Mario stands before he steps (the surface objects
	// update before the player in a frame), so one frame on, this same surface of the
	// pyramid (reloaded in place: the surface pool is rebuilt in the same order every
	// frame) carries the normal his next step will walk on, whatever that step's inputs.
	// The frame is this script's own and is reverted with it.
	Surface* floor = marioState->floor;
	Object* pyramid = floor->object;
	AdvanceFrameRead();
	if (floor->object != pyramid)
		return false; // the surface slot is not the pyramid's any more: nothing to read

	short floorAngle = atan2s(floor->normal.z, floor->normal.x);
	CustomStatus.isSlope = FloorIsSlope(floor->normal.y, marioState->action);
	// std::sqrt(float) overload, not std::sqrtf: libstdc++ 13 does not declare the f-suffixed
	// names in namespace std (docs/compilers.md). Same instruction, same result.
	CustomStatus.steepness = std::sqrt(floor->normal.x * floor->normal.x + floor->normal.z * floor->normal.z);

	// m->floorAngle - m->faceAngle[1] >= -0x3FFF && m->floorAngle -
	// m->faceAngle[1] <= 0x3FFF
	int32_t lowerAngle = floorAngle + 0x3FFF;
	int32_t upperAngle = floorAngle - 0x3FFF;

	int32_t lowerAngleDiff = abs(int16_t(lowerAngle - _targetAngle));
	int32_t upperAngleDiff = abs(int16_t(upperAngle - _targetAngle));

	if (lowerAngleDiff <= upperAngleDiff)
	{
		CustomStatus.angleFacing = lowerAngle;
		CustomStatus.angleNotFacing = upperAngle;
		CustomStatus.downhillRotation = lowerAngleDiff < upperAngleDiff ? Rotation::CLOCKWISE : Rotation::NONE;
	}
	else
	{
		CustomStatus.angleFacing = upperAngle;
		CustomStatus.angleNotFacing = lowerAngle;
		CustomStatus.downhillRotation = Rotation::COUNTERCLOCKWISE;
	}

	// Get optimal angle for switching from turnaround to finish turnaround
	int16_t facingDYaw = _faceAngle - CustomStatus.angleFacing;
	if (abs(int32_t(facingDYaw)) <= 0x471C)
		CustomStatus.angleFacingAnalogBack = _faceAngle + 0x471D * CustomStatus.downhillRotation;
	else
		CustomStatus.angleFacingAnalogBack = CustomStatus.angleFacing;

	int16_t notFacingDYaw = _faceAngle - CustomStatus.angleNotFacing;
	if (abs(int32_t(notFacingDYaw)) <= 0x471C)
		CustomStatus.angleNotFacingAnalogBack = _faceAngle + 0x471D * CustomStatus.downhillRotation.Negate();
	else
		CustomStatus.angleNotFacingAnalogBack = CustomStatus.angleNotFacing;

	CustomStatus.floorAngle = floorAngle;

	return true;
}

bool GetMinimumDownhillWalkingAngle::assertion()
{
	return true;
}

// mario_floor_is_slope on the pyramid: its surfaces are of the default class (and BitFS is
// not a slide terrain), so the floor is a slope below cos 15 degrees, or below cos 20 when
// Mario crawls on a floor steeper than 60 degrees (mario_get_floor_class's one exception).
bool GetMinimumDownhillWalkingAngle::FloorIsSlope(float normalY, uint32_t action)
{
	const bool crawlingHolds = action == ACT_CRAWLING && normalY > 0.5f;
	return normalY <= (crawlingHolds ? 0.9396926f : 0.9659258f);
}
