#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"

#include <cmath>

bool TryHackedWalkOutOfBounds::verification()
{
	return true;
}

bool TryHackedWalkOutOfBounds::execution()
{
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));

	CustomStatus.startSpeed = _speed;
	Script::CopyVec3f(CustomStatus.startPos, marioState->pos);

	//Attempt to walk OOB to prevent QStep position updates
	marioState->forwardVel = _speed;
	marioState->action = ACT_WALKING;
	auto inputs = Inputs::GetClosestInputByYawHau(marioState->faceAngle[1], 32, camera->yaw);
	AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

	CustomStatus.endSpeed = marioState->forwardVel;
	Script::CopyVec3f(CustomStatus.endPos, marioState->pos);
	CustomStatus.endAction = marioState->action;
	CustomStatus.floorAngle = marioState->floorAngle;

	return true;
}

bool TryHackedWalkOutOfBounds::validation()
{
	float hDistMoved = sqrtf(pow(CustomStatus.endPos[0] - CustomStatus.startPos[0], 2) + pow(CustomStatus.endPos[2] - CustomStatus.startPos[2], 2));
	if (hDistMoved >= abs(_speed * 0.01f))
		return false;

	if (CustomStatus.endAction != ACT_WALKING)
		return false;

	return true;
}
