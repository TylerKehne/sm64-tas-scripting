#include "ScriptDefs.hpp"

#include <cmath>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>

bool BrakeToIdle::verification()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	// Verify mario has enough speed to brake
	if (marioState->forwardVel < 16.0f)
		return false;

	// Check that Mario can enter walking action
	if (marioState->action != ACT_WALKING)
		return false;

	return true;
}

bool BrakeToIdle::execution()
{
	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	Camera* camera				 = *(Camera**) (game->addr("gCamera"));
	Object* pyramid				 = marioState->floor->object;

	// Brake to a stop
	do
	{
		AdvanceFrameWrite(Inputs(0, 0, 0));

		if (
			marioState->action != ACT_BRAKING &&
			marioState->action != ACT_BRAKING_STOP)
			return false;
	} while (marioState->action == ACT_BRAKING);

	// Quickturn uphill
	auto status = Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
	if (!status.validated)
		return false;

	// Get closest cardinal controller input to uphill angle
	int16_t uphillCardinalYaw =
		(((uint16_t) (status.floorAngle + 0x8000 - camera->yaw + 0x2000) >> 14)
		 << 14) +
		camera->yaw;
	auto inputs = Inputs::GetClosestInputByYawHau(
		uphillCardinalYaw, nextafter(0.0f, 1.0f), camera->yaw);
	AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

	if (marioState->action != ACT_WALKING)
		return false;

	do
	{
		AdvanceFrameWrite(Inputs(0, 0, 0));
		CustomStatus.decelerationFrames++;
	} while (marioState->action == ACT_DECELERATING);

	return true;
}

bool BrakeToIdle::validation()
{
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	if (marioState->action != ACT_IDLE)
		return false;

	return true;
}