#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"
#include "ObjectFields.hpp"

#include <array>
#include <cmath>

bool BitFsPyramidOscillation::verification()
{
	//Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

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
	MarioState* marioState = *(MarioState**) (game->addr("gMarioState"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//TODO: Optimize init angle
	auto initAngleStatus = Test<GetMinimumDownhillWalkingAngle>(0);
	auto stick = Inputs::GetClosestInputByYawExact(0x2000, 32, camera->yaw, initAngleStatus.downhillRotation);
	AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

	uint64_t preTurnFrame = GetCurrentFrame();
	OptionalSave();

	//Initial run downhill
	auto initRunStatus = Modify<BitFsPyramidOscillation_RunDownhill>(0);
	if (!initRunStatus.validated)
		return false;

	//Record initial XZ sum, don't want to decrease this (TODO: optimize angle of first frame and record this before run downhill)
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	if (initRunStatus.maxSpeed > CustomStatus.maxSpeed[0])
		CustomStatus.maxSpeed[0] = initRunStatus.maxSpeed;

	//We want to turn uphill as late as possible, and also turn around as late as possible, without sacrificing XZ sum
	uint64_t minFrame = initRunStatus.framePassedEquilibriumPoint == -1 ? initRunStatus.m64Diff.frames.begin()->first : initRunStatus.framePassedEquilibriumPoint;
	uint64_t maxFrame = initRunStatus.m64Diff.frames.rbegin()->first;
	vector<std::pair<int64_t, int64_t>> oscillationMinMaxFrames;
	for (int i = 0; i < 20; i++)
	{
		oscillationMinMaxFrames.push_back({ minFrame, maxFrame });

		//Start at the latest ppossible frame and work backwards. Stop when the max speed at the equilibrium point stops increasing.
		auto turnRunStatus = Execute<BitFsPyramidOscillation_Iteration>(minFrame, maxFrame, CustomStatus.maxSpeed[i & 1], CustomStatus.initialXzSum, false);

		//If path was affected by ACT_FINISH_TURNING_AROUND taking too long to expire, retry the PREVIOUS oscillation with braking + quickturn
		//Then run another oscillation, compare the speeds, and continue with the diff that has the higher speed
		if (i > 0 && (turnRunStatus.finishTurnaroundFailedToExpire || !turnRunStatus.validated))
		{
			M64Diff nonBrakeDiff = BaseStatus.m64Diff;
			int64_t minFrameBrake = oscillationMinMaxFrames[i - 1].first;
			int64_t maxFrameBrake = oscillationMinMaxFrames[i - 1].second;
			auto turnRunStatusBrake = Modify<BitFsPyramidOscillation_Iteration>(minFrameBrake, maxFrameBrake, CustomStatus.maxSpeed[(i & 1) ^ 1], CustomStatus.initialXzSum, true);
			if (turnRunStatusBrake.validated)
			{
				int64_t minFrame2 = turnRunStatusBrake.framePassedEquilibriumPoint;
				int64_t maxFrame2 = turnRunStatusBrake.m64Diff.frames.rbegin()->first;
				auto turnRunStatus2 = Execute<BitFsPyramidOscillation_Iteration>(minFrame2, maxFrame2, CustomStatus.maxSpeed[i & 1], CustomStatus.initialXzSum, false);
				if (turnRunStatus2.passedEquilibriumSpeed > turnRunStatus.passedEquilibriumSpeed)
				{
					oscillationMinMaxFrames[i - 1] = { minFrameBrake, maxFrameBrake };
					oscillationMinMaxFrames[i] = { minFrame2, maxFrame2 };
					CustomStatus.maxSpeed[(i & 1)] = turnRunStatusBrake.speedBeforeTurning;
					turnRunStatus = turnRunStatus2;
				}
				else
					Apply(nonBrakeDiff);
			}
		}

		if (turnRunStatus.passedEquilibriumSpeed > CustomStatus.maxPassedEquilibriumSpeed[i & 1])
		{
			CustomStatus.finalXzSum = turnRunStatus.finalXzSum;
			CustomStatus.maxSpeed[(i & 1) ^ 1] = turnRunStatus.speedBeforeTurning;
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