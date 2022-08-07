#include <tasfw/scripts/General.hpp>

#include <cmath>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

bool BrakeToIdle::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));

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
	[[maybe_unused]] const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));
	Camera* camera				 = *(Camera**) (resource->addr("gCamera"));
	[[maybe_unused]] Object* pyramid				 = marioState->floor->object;

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
	if (!status.asserted)
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

bool BrakeToIdle::assertion()
{
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));
	return marioState->action == ACT_IDLE;
}