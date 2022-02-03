#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::verification()
{
	//Check if Mario is on the pyramid platform
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* behavior = floorObject->behavior;
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (behavior != pyramidBehavior)
		return false;

	//Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING)
		return false;

	return true;
}

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	do
	{
		auto inputs = Inputs::GetClosestInputByYawHau(marioState->faceAngle[1] + 0x8000, 32, camera->yaw);
		AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

		if (marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_FINISH_TURNING_AROUND)
		{
			CustomStatus.tooDownhill = (marioState->action == ACT_LAVA_BOOST);
			return false;
		}

		if (marioState->floor->object == NULL || marioState->floor->object->behavior != pyramidBehavior)
		{
			CustomStatus.tooDownhill = (marioState->floor->object == NULL);
			return false;
		}
	} while (marioState->action == ACT_TURNING_AROUND);

	//Wind back 1f and run downhill optimally
	Rollback(game->getCurrentFrame() - 1);

	auto status = Modify<BitFsPyramidOscillation_RunDownhill>(_roughTargetAngle);

	CustomStatus.framePassedEquilibriumPoint = status.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = status.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = status.passedEquilibriumSpeed;
	CustomStatus.finalXzSum = status.finalXzSum;

	if (status.validated && status.finalXzSum < _minXzSum - 0.00001)
		CustomStatus.tooUphill = true;

	return true;
}

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	return true;
}