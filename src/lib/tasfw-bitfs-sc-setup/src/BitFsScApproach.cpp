#include <tasfw/scripts/BitFsScApproach.hpp>

bool BitFsScApproach::validation()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));

	// Calculate starting params
	if (!_oscStatus.asserted || _oscStatus.oscillationMinMaxFrames.size() < 3)
		return false;

	int64_t max = _oscStatus.oscillationMinMaxFrames.size() - 1;
	Load(_oscStatus.oscillationMinMaxFrames[max - 1].first);
	int16_t angle1 = marioState->faceAngle[1];
	int16_t angle1Diff = abs(int16_t(angle1 - _roughTargetAngle));

	Load(_oscStatus.oscillationMinMaxFrames[max].first);
	int16_t angle2 = marioState->faceAngle[1];
	int16_t angle2Diff = abs(int16_t(angle2 - _roughTargetAngle));

	if (angle1Diff > angle2Diff)
	{
		_minFramePrev = _oscStatus.oscillationMinMaxFrames[max - 1].first;
		_maxFramePrev = _oscStatus.oscillationMinMaxFrames[max - 1].second;
		_minFrame = _oscStatus.oscillationMinMaxFrames[max].first;
		_maxFrame = _oscStatus.oscillationMinMaxFrames[max].second;
		_targetXzSum = _oscStatus.finalXzSum[((max - 1) & 1U) ^ 1U];
		_prevMaxSpeed = _oscStatus.maxSpeed[((max - 1) & 1U)];
		_lastOscAngle = angle1 + 0x8000;
	}
	else
	{
		_minFramePrev = _oscStatus.oscillationMinMaxFrames[max - 2].first;
		_maxFramePrev = _oscStatus.oscillationMinMaxFrames[max - 2].second;
		_minFrame = _oscStatus.oscillationMinMaxFrames[max - 1].first;
		_maxFrame = _oscStatus.oscillationMinMaxFrames[max - 1].second;
		_targetXzSum = _oscStatus.finalXzSum[(max & 1U) ^ 1U];
		_prevMaxSpeed = _oscStatus.maxSpeed[(max & 1U)];
		_lastOscAngle = angle2 + 0x8000;
	}
		
	// Check if Mario is on the pyramid platform
	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	return true;
}

bool BitFsScApproach::execution()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));

	//First attempt with optimized equilibrium speed
	auto drStatus = Modify<BitFsScApproach_AttemptDr_BF>(_roughTargetAngle, _minFrame, _maxFrame);

	//If unsuccessful, try again with optimized max speed
	if (!drStatus.asserted)
	{
		// Initialize base oscillation params dto
		auto oscParams = BitFsPyramidOscillation_ParamsDto{};
		oscParams.quadrant = _quadrant;
		oscParams.targetXzSum = _targetXzSum;
		oscParams.initialXzSum = _targetXzSum;
		oscParams.brake = false;
		oscParams.prevMaxSpeed = _prevMaxSpeed;
		oscParams.roughTargetAngle = _lastOscAngle;
		oscParams.optimizeMaxSpeed = true;
		auto turnRunStatus = Modify<BitFsPyramidOscillation_Iteration>(oscParams, _minFramePrev, _maxFramePrev);
		if (turnRunStatus.asserted)
			drStatus = Modify<BitFsScApproach_AttemptDr_BF>(_roughTargetAngle, turnRunStatus.framePassedEquilibriumPoint, turnRunStatus.m64Diff.frames.rbegin()->first);
	}

	CustomStatus.diveLanded = drStatus.diveLanded;
	CustomStatus.diveRelHeight = drStatus.diveRelHeight;
	CustomStatus.drLanded = drStatus.drLanded;
	CustomStatus.drRelHeight = drStatus.drRelHeight;

	return true;
}

bool BitFsScApproach::assertion()
{
	return CustomStatus.drLanded;
}
