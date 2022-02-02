#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"
#include "ObjectFields.hpp"

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

	const BehaviorScript* behavior = floorObject->behavior;
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (behavior != pyramidBehavior)
		return false;

	//Check that Mario is idle
	uint32_t action = marioState->action;
	if (action != ACT_IDLE)
		return false;

	//Check that pyramid is at equilibrium
	vector<float> normal = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };
	AdvanceFrameRead();
	vector<float> normal2 = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };

	if (normal != normal2)
		return false;

	return true;
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

	uint64_t preTurnFrame = game->getCurrentFrame();
	OptionalSave();

	auto initRunStatus = Modify<BitFsPyramidOscillation_RunDownhill>();
	if (!initRunStatus.validated)
		return false;

	//Record initial XZ sum, don't want to decrease this (TODO: optimize angle of first frame and record this before run downhill)
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	if (initRunStatus.maxSpeed > CustomStatus.maxSpeed)
		CustomStatus.maxSpeed = initRunStatus.maxSpeed;

	//We want to turn uphill as late as possible, and also turn around as late as possible, without sacrificing XZ sum
	uint64_t minFrame = initRunStatus.m64Diff.frames.begin()->first;
	uint64_t maxFrame = initRunStatus.m64Diff.frames.rbegin()->first;
	while (true)
	{
		float maxSpeed = 0;
		float finalXzSum = 0;
		uint64_t passedEquilibriumFrame = -1;
		M64Diff turnRunDiff = M64Diff();
		for (uint64_t frame = maxFrame; frame >= minFrame; frame--)
		{
			Load(frame);

			auto turnRunStatus = Execute<BitFsPyramidOscillation_TurnThenRunDownhill>();
			if (!turnRunStatus.validated)
				continue;

			if (turnRunStatus.maxSpeed > maxSpeed)
			{
				maxSpeed = turnRunStatus.maxSpeed;
				finalXzSum = turnRunStatus.finalXzSum;
				turnRunDiff = turnRunStatus.m64Diff;
				passedEquilibriumFrame = turnRunStatus.framePassedEquilibriumPoint;
			}
		}

		if (maxSpeed > CustomStatus.maxSpeed)
		{
			CustomStatus.finalXzSum = finalXzSum;
			CustomStatus.maxSpeed = maxSpeed;
			Apply(turnRunDiff);
			minFrame = passedEquilibriumFrame;
			maxFrame = turnRunDiff.frames.rbegin()->first;
			Save();
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