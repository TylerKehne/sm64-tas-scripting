#include <BitFSPyramidOscillation.hpp>
#include <General.hpp>

#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include <array>
#include <cmath>

bool BitFsPyramidOscillation::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	// Check that Mario is idle
	if (marioState->action != ACT_IDLE)
		return false;

	return true;
}

bool BitFsPyramidOscillation::execution()
{
	MarioState* marioState = *(MarioState**) (resource->addr("gMarioState"));
	Camera* camera		   = *(Camera**) (resource->addr("gCamera"));
	Object* pyramid		   = marioState->floor->object;

	int16_t initAngle	 = -32768;
	auto m64 = M64();
	auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
	auto initAngleStatus = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, initAngle);
	if (!initAngleStatus.validated)
		return false;
	//auto initAngleStatus = Test<GetMinimumDownhillWalkingAngle>(initAngle);
	auto stick = Inputs::GetClosestInputByYawExact(
		initAngleStatus.angleFacing, 32, camera->yaw,
		initAngleStatus.downhillRotation);
	AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

	// Initialize base oscillation params dto
	auto baseOscParams		  = BitFsPyramidOscillation_ParamsDto {};
	baseOscParams.quadrant	  = _quadrant;
	baseOscParams.targetXzSum = _targetXzSum;

	// Initial run downhill
	auto oscillationParams = baseOscParams;
	oscillationParams.roughTargetAngle = initAngleStatus.angleFacing;

	auto initRunStatus =
		Modify<BitFsPyramidOscillation_RunDownhill>(oscillationParams);
	if (!initRunStatus.asserted)
		return false;

	// Record initial XZ sum, don't want to decrease this (TODO: optimize angle
	// of first frame and record this before run downhill)
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) +
		fabs(pyramid->oTiltingPyramidNormalZ);

	// We want to turn uphill as late as possible, and also turn around as late
	// as possible, without sacrificing XZ sum
	uint64_t minFrame = initRunStatus.framePassedEquilibriumPoint == -1 ?
		initRunStatus.m64Diff.frames.begin()->first :
		initRunStatus.framePassedEquilibriumPoint;
	uint64_t maxFrame = initRunStatus.m64Diff.frames.rbegin()->first;
	CustomStatus.finalXzSum[1] = initRunStatus.finalXzSum;
	for (int i = 0; i < 15; i++)
	{
		CustomStatus.oscillationMinMaxFrames.emplace_back(minFrame, maxFrame);
		float targetXzSum = _targetXzSum - 0.02 * (i & 1U);
		float targetXzSumPrev = _targetXzSum - 0.02 * ((i & 1U) ^ 1U);

		// Start at the latest ppossible frame and work backwards. Stop when the
		// max speed at the equilibrium point stops increasing.
		oscillationParams = baseOscParams;
		oscillationParams.targetXzSum = targetXzSum;
		oscillationParams.prevMaxSpeed = CustomStatus.maxSpeed[i & 1U];
		oscillationParams.brake		   = false;
		oscillationParams.initialXzSum = CustomStatus.finalXzSum[i & 1U] != 0 ? CustomStatus.finalXzSum[i & 1U] : CustomStatus.finalXzSum[(i & 1U) ^ 1U];
		auto turnRunStatus = Execute<BitFsPyramidOscillation_Iteration>(oscillationParams, minFrame, maxFrame);

		// If path was affected by ACT_FINISH_TURNING_AROUND taking too long to
		// expire, retry the PREVIOUS oscillation with braking + quickturn Then
		// run another oscillation, compare the speeds, and continue with the
		// diff that has the higher speed
		if (i > 0 && (_alwaysBrake || turnRunStatus.finishTurnaroundFailedToExpire || !turnRunStatus.asserted))
		{
			M64Diff nonBrakeDiff	   = GetDiff();
			int64_t minFrameBrake	   = CustomStatus.oscillationMinMaxFrames[i - 1].first;
			int64_t maxFrameBrake	   = CustomStatus.oscillationMinMaxFrames[i - 1].second;
			auto oscillationParamsPrev = baseOscParams;
			oscillationParamsPrev.prevMaxSpeed = CustomStatus.maxSpeed[(i & 1U) ^ 1U];
			oscillationParamsPrev.brake		   = true;
			oscillationParamsPrev.initialXzSum = CustomStatus.finalXzSum[(i & 1U) ^ 1U] != 0 ? CustomStatus.finalXzSum[(i & 1U) ^ 1U] : CustomStatus.finalXzSum[i & 1U];
			oscillationParamsPrev.targetXzSum = targetXzSumPrev;
			auto turnRunStatusBrake = Modify<BitFsPyramidOscillation_Iteration>(oscillationParamsPrev, minFrameBrake, maxFrameBrake);
			if (turnRunStatusBrake.asserted)
			{
				int64_t minFrame2 = turnRunStatusBrake.framePassedEquilibriumPoint;
				int64_t maxFrame2 = turnRunStatusBrake.m64Diff.frames.rbegin()->first;
				auto turnRunStatus2 = Execute<BitFsPyramidOscillation_Iteration>(oscillationParams, minFrame2, maxFrame2);

				bool isFaster2 = turnRunStatus2.passedEquilibriumSpeed > turnRunStatus.passedEquilibriumSpeed;
				if (abs(turnRunStatus2.passedEquilibriumSpeed - turnRunStatus.passedEquilibriumSpeed) < 0.2f)
					isFaster2 = turnRunStatus2.passedEquilibriumXzDist > turnRunStatus.passedEquilibriumXzDist;
				if (_alwaysBrake || (turnRunStatus2.asserted && isFaster2))
				{
					CustomStatus.oscillationMinMaxFrames[i] = {minFrame2, maxFrame2};
					CustomStatus.maxSpeed[(i & 1U)] = turnRunStatusBrake.speedBeforeTurning;
					CustomStatus.finalXzSum[(i & 1U) ^ 1U] = turnRunStatusBrake.finalXzSum;
					CustomStatus.actualMaxSpeed[(i & 1U) ^ 1U] = turnRunStatusBrake.maxSpeed;
					CustomStatus.maxPassedEquilibriumSpeed[(i & 1U) ^ 1U] = turnRunStatusBrake.passedEquilibriumSpeed;
					CustomStatus.maxPassedEquilibriumXzDist[(i & 1U) ^ 1U] = turnRunStatusBrake.passedEquilibriumXzDist;
					turnRunStatus = turnRunStatus2;
				}
				else
					Apply(nonBrakeDiff);
			}
		}

		bool isFaster = turnRunStatus.passedEquilibriumSpeed > CustomStatus.maxPassedEquilibriumSpeed[i & 1];
		if (abs(turnRunStatus.passedEquilibriumSpeed - CustomStatus.maxPassedEquilibriumSpeed[i & 1]) < 0.2f)
			isFaster = turnRunStatus.passedEquilibriumXzDist > CustomStatus.maxPassedEquilibriumXzDist[i & 1];

		// Terminate when path fails to increase speed and XZ sum target has
		// been reached in both directions
		if (turnRunStatus.asserted &&
			(turnRunStatus.passedEquilibriumSpeed > CustomStatus.maxPassedEquilibriumSpeed[i & 1]
				|| CustomStatus.finalXzSum[(i & 1) ^ 1] < targetXzSumPrev
				|| CustomStatus.finalXzSum[(i & 1)] < targetXzSum))
		{
			CustomStatus.finalXzSum[i & 1] = turnRunStatus.finalXzSum;
			CustomStatus.maxSpeed[(i & 1) ^ 1] = turnRunStatus.speedBeforeTurning;
			CustomStatus.actualMaxSpeed[(i & 1)] = turnRunStatus.maxSpeed;
			CustomStatus.maxPassedEquilibriumSpeed[i & 1] = turnRunStatus.passedEquilibriumSpeed;
			CustomStatus.maxPassedEquilibriumXzDist[i & 1] = turnRunStatus.passedEquilibriumXzDist;
			Apply(turnRunStatus.m64Diff);
			minFrame = turnRunStatus.framePassedEquilibriumPoint;
			maxFrame = turnRunStatus.m64Diff.frames.rbegin()->first;
		}
		else
			break;
	}

	return true;
}

bool BitFsPyramidOscillation::assertion()
{
	if (IsDiffEmpty())
		return false;

	float targetXzSum1 = _targetXzSum - 0.02 * (lowerXzSumParity & 1U);
	float targetXzSum0 = _targetXzSum - 0.02 * ((lowerXzSumParity & 1U) ^ 1U);

	if (CustomStatus.finalXzSum[0] < targetXzSum0 ||
		CustomStatus.finalXzSum[1] < targetXzSum1)
		return false;

	return true;
}