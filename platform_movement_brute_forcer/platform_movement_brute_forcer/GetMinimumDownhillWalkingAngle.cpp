#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"

bool GetMinimumDownhillWalkingAngle::verification()
{
	return true;
}

bool GetMinimumDownhillWalkingAngle::execution()
{
	bool hackedWalkValidated = false;

	//Walk OOB to get Mario's floor after surface updates + platform displacement with no QStep interference
	for (float walkSpeed = -131072.0f; walkSpeed > -(1 << 31); walkSpeed *= 1.5f)
	{
		auto status = Script::Execute<TryHackedWalkOutOfBounds>(_m64, walkSpeed);

		if (status.validated)
		{
			hackedWalkValidated = true;
			break;
		}

		if (!status.executed)
			return false;

		//Nothing in QSteps should trigger this, so if this is true walking is impossible
		if (status.endAction | ACT_FLAG_INVULNERABLE)
			return false;
	}

	if (!hackedWalkValidated)
		return false;

	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	s32 (*mario_floor_is_slope)(struct MarioState*) = (s32(*)(struct MarioState*))(game.addr("mario_floor_is_slope"));
	CustomStatus.isSlope = mario_floor_is_slope(marioState);

	//m->floorAngle - m->faceAngle[1] >= -0x3FFF && m->floorAngle - m->faceAngle[1] <= 0x3FFF
	int32_t lowerAngle = marioState->floorAngle + 0x3FFF;
	int32_t upperAngle = marioState->floorAngle - 0x3FFF;

	game.load_state(&_initialSave);

	int32_t lowerAngleDiff = abs(lowerAngle - marioState->faceAngle[1]);
	int32_t upperAngleDiff = abs(upperAngle - marioState->faceAngle[1]);

	if (lowerAngleDiff <= upperAngleDiff)
	{
		CustomStatus.angleFacing = lowerAngle;
		CustomStatus.angleNotFacing = upperAngle;
	}
	else
	{
		CustomStatus.angleFacing = upperAngle;
		CustomStatus.angleNotFacing = lowerAngle;
	}

	return true;
}

bool GetMinimumDownhillWalkingAngle::validation()
{
	return true;
}