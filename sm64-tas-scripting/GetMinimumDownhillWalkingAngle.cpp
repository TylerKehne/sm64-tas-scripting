#include "Script.hpp"
#include "Sm64.hpp"
#include "Types.hpp"
#include "pyramid.hpp"
#include "surface.hpp"

bool GetMinimumDownhillWalkingAngle::verification()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	return true;
}

bool GetMinimumDownhillWalkingAngle::execution()
{
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	/*
	s32(*mario_floor_is_slope)(struct MarioState*) = (s32(*)(struct
	MarioState*))(game->addr("mario_floor_is_slope"));

	bool hackedWalkasserted = false;

	//Walk OOB to get Mario's floor after surface updates + platform
	displacement with no QStep interference int16_t floorAngle = 0; for (float
	walkSpeed = -131072.0f; walkSpeed > -(1 << 31); walkSpeed *= 1.5f)
	{
			auto status = Test<TryHackedWalkOutOfBounds>(walkSpeed);

			if (status.asserted)
			{
					hackedWalkasserted = true;
					floorAngle = status.floorAngle;
					CustomStatus.isSlope = mario_floor_is_slope(marioState);
					break;
			}

			if (!status.executed)
					return false;

			//Nothing in QSteps should trigger this, so if this is true walking
	is impossible if (status.endAction | ACT_FLAG_INVULNERABLE) return false;
	}

	if (!hackedWalkasserted)
			return false;
	*/

	Object* marioObj				= marioState->marioObj;
	Object* pyramidPlatform = marioState->floor->object;

	short floorAngle = 0;

	if (!simulate_platform_tilt(
				marioObj, pyramidPlatform, &floorAngle, &CustomStatus.isSlope))
		return false;

	// m->floorAngle - m->faceAngle[1] >= -0x3FFF && m->floorAngle -
	// m->faceAngle[1] <= 0x3FFF
	int32_t lowerAngle = floorAngle + 0x3FFF;
	int32_t upperAngle = floorAngle - 0x3FFF;

	int32_t lowerAngleDiff = abs(int16_t(lowerAngle - _targetAngle));
	int32_t upperAngleDiff = abs(int16_t(upperAngle - _targetAngle));

	if (lowerAngleDiff <= upperAngleDiff)
	{
		CustomStatus.angleFacing		= lowerAngle;
		CustomStatus.angleNotFacing = upperAngle;
		CustomStatus.downhillRotation =
			lowerAngleDiff < upperAngleDiff ? Rotation::CLOCKWISE : Rotation::NONE;
	}
	else
	{
		CustomStatus.angleFacing			= upperAngle;
		CustomStatus.angleNotFacing		= lowerAngle;
		CustomStatus.downhillRotation = Rotation::COUNTERCLOCKWISE;
	}

	// Get optimal angle for switching from turnaround to finish turnaround
	int16_t facingDYaw = _faceAngle - CustomStatus.angleFacing;
	if (abs(facingDYaw) <= 0x471C)
		CustomStatus.angleFacingAnalogBack = _faceAngle + 0x471D * sign(facingDYaw);
	else
		CustomStatus.angleFacingAnalogBack = CustomStatus.angleFacing;

	int16_t notFacingDYaw = _faceAngle - CustomStatus.angleNotFacing;
	if (abs(notFacingDYaw) <= 0x471C)
		CustomStatus.angleNotFacingAnalogBack =
			_faceAngle + 0x471D * sign(notFacingDYaw);
	else
		CustomStatus.angleNotFacingAnalogBack = CustomStatus.angleNotFacing;

	CustomStatus.floorAngle = floorAngle;

	return true;
}

bool GetMinimumDownhillWalkingAngle::assertion()
{
	return CustomStatus.isSlope;
}
