#include "Script.hpp"

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::verification()
{
	//Verify Mario is running on the platform
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* behavior = floorObject->behavior;
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game.addr("bhvBitfsTiltingInvertedPyramid"));
	if (behavior != pyramidBehavior)
		return false;

	//Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_FINISH_TURNING_AROUND)
		return false;

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game.addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	Camera* camera = *(Camera**)(game.addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	//Keep running until target angle is reached
	int16_t actualIntendedYaw = 0;
	int16_t roughTargetAngle = marioState->faceAngle[1] + 0x8000;
	Slot lowSpeedSave = game.alloc_slot();
	do
	{
		game.save_state(&lowSpeedSave);
		auto inputs = Inputs::GetClosestInputByYawHau(_angle, 32, camera->yaw);
		actualIntendedYaw = Inputs::GetIntendedYawMagFromInput(inputs.first, inputs.second, camera->yaw).first;
		advance_frame(Inputs(0, inputs.first, inputs.second));

		if ((marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
			|| marioState->floor->object == NULL
			|| marioState->floor->object->behavior != pyramidBehavior)
			return false;

		//TODO: This is temporary, I want to make a script for getting the valid turning range
		if (marioState->forwardVel < 16.0f)
		{
			load_state(&lowSpeedSave);
			break;
		}

	} while (marioState->faceAngle[1] != actualIntendedYaw);

	if (marioState->action != ACT_WALKING)
		return false;

	if (marioState->forwardVel < 16.0f)
	{
		CustomStatus.tooSlowForTurnAround = true;
		return false;
	}

	//Turn around
	Slot turnAroundSave = game.alloc_slot();
	do
	{
		auto inputs = Inputs::GetClosestInputByYawHau(marioState->faceAngle[1] + 0x8000, 32, camera->yaw);
		game.save_state(&turnAroundSave);
		advance_frame(Inputs(0, inputs.first, inputs.second));

		if (marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_FINISH_TURNING_AROUND)
			return false;

		if (marioState->floor->object == NULL || marioState->floor->object->behavior != pyramidBehavior)
			return false;
	} while (marioState->action == ACT_TURNING_AROUND);

	//Wind back 1f and run downhill optimally
	load_state(&turnAroundSave);
	auto runDownhillStatus = Modify<BitFsPyramidOscillation_RunDownhill>(roughTargetAngle);

	if (!runDownhillStatus.validated)
		return false;

	CustomStatus.framePassedEquilibriumPoint = runDownhillStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runDownhillStatus.maxSpeed;
	CustomStatus.finalXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	if (CustomStatus.finalXzSum < CustomStatus.initialXzSum - 0.00001)
		return false;

	if (CustomStatus.framePassedEquilibriumPoint == -1)
		return false;

	return true;
}