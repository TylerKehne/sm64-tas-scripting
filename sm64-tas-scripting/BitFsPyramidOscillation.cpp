#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"
#include "ObjectFields.hpp"

#include <array>

bool BitFsPyramidOscillation::verification()
{
	//Check if Mario is on the pyramid platform
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	//Check that Mario is idle
	if (marioState->action != ACT_IDLE)
		return false;

	//Check that pyramid is at equilibrium
	array<float, 3> normal = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };
	AdvanceFrameRead();
	array<float, 3> normal2 = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };

	return normal == normal2;
}

bool BitFsPyramidOscillation::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//TODO: Optimize init angle
	auto initAngleStatus = Test<GetMinimumDownhillWalkingAngle>(0);
	auto stick = Inputs::GetClosestInputByYawExact(0x2000, 32, camera->yaw, initAngleStatus.downhillRotation);
	AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

	uint64_t preTurnFrame = GetCurrentFrame();
	OptionalSave();

	auto initRunStatus = Modify<BitFsPyramidOscillation_RunDownhill>();
	if (!initRunStatus.validated)
		return false;

	//Record initial XZ sum, don't want to decrease this (TODO: optimize angle of first frame and record this before run downhill)
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	if (initRunStatus.maxSpeed > CustomStatus.maxSpeed[0])
		CustomStatus.maxSpeed[0] = initRunStatus.maxSpeed;

	//We want to turn uphill as late as possible, and also turn around as late as possible, without sacrificing XZ sum
	uint64_t minFrame = initRunStatus.m64Diff.frames.begin()->first;
	uint64_t maxFrame = initRunStatus.m64Diff.frames.rbegin()->first;
	for (int i = 0; i < 12; i++)
	{
		ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill> turnRunStatus;
		float speedBeforeTurning = 0;
		for (uint64_t frame = maxFrame; frame >= minFrame; frame--)
		{
			Load(frame);
			speedBeforeTurning = marioState->forwardVel;
			auto status = Execute<BitFsPyramidOscillation_TurnThenRunDownhill>(CustomStatus.maxSpeed[i & 1], CustomStatus.initialXzSum);

			//Keep iterating until we get a valid result, then keep iterating until we stop getting better results
			//Once we get a valid result, we expect successive iterations to be worse (less space to accelerate), but that isn't always true
			if (!status.validated)
			{
				if (turnRunStatus.passedEquilibriumSpeed == 0.0)
					continue;
				else
					break;
			}

			if (status.passedEquilibriumSpeed > turnRunStatus.passedEquilibriumSpeed && status.maxSpeed > CustomStatus.maxSpeed[i & 1])
				turnRunStatus = status;
			else if (status.maxSpeed == 0.0)
				break;
		}

		if (turnRunStatus.passedEquilibriumSpeed > CustomStatus.maxPassedEquilibriumSpeed[i & 1])
		{
			CustomStatus.finalXzSum = turnRunStatus.finalXzSum;
			CustomStatus.maxSpeed[(i & 1) ^ 1] = speedBeforeTurning;
			CustomStatus.maxPassedEquilibriumSpeed[i & 1] = turnRunStatus.passedEquilibriumSpeed;
			Apply(turnRunStatus.m64Diff);
			minFrame = turnRunStatus.framePassedEquilibriumPoint;
			maxFrame = turnRunStatus.m64Diff.frames.rbegin()->first;
			Save(minFrame);
		}
		else
			break;
	}

	return true;
}

bool BitFsPyramidOscillation::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	return true;
}