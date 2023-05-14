#include <tasfw/scripts/BitFsScApproach.hpp>
#include <cmath>

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
		//_targetXzSum = _oscStatus.finalXzSum[((max - 1) & 1U) ^ 1U];
		_prevMaxSpeed = _oscStatus.maxSpeed[((max - 1) & 1U)];
		_prevActualMaxSpeed = _oscStatus.actualMaxSpeed[((max - 1) & 1U)];
		_lastOscAngle = angle1 + 0x8000;
	}
	else
	{
		_minFramePrev = _oscStatus.oscillationMinMaxFrames[max - 2].first;
		_maxFramePrev = _oscStatus.oscillationMinMaxFrames[max - 2].second;
		_minFrame = _oscStatus.oscillationMinMaxFrames[max - 1].first;
		_maxFrame = _oscStatus.oscillationMinMaxFrames[max - 1].second;
		//_targetXzSum = _oscStatus.finalXzSum[(max & 1U) ^ 1U];
		_prevMaxSpeed = _oscStatus.maxSpeed[(max & 1U)];
		_prevActualMaxSpeed = _oscStatus.actualMaxSpeed[(max & 1U)];
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

	//First attempt with optimized equilibrium speed and increased xz sum
	float prevMaxSpeed = _prevMaxSpeed;
	bool terminate = false;
	auto drStatus = ModifyCompareAdhoc<BitFsScApproach_AttemptDr_BF::CustomScriptStatus, std::tuple<>>(
		[&](auto iteration, auto& params) //paramsGenerator
		{
			return !terminate;
		},
		[&](auto customStatus) //script
		{
			auto oscParams = BitFsPyramidOscillation_ParamsDto{};
			oscParams.quadrant = _quadrant;
			oscParams.targetXzSum = _targetXzSum + 0.05f;
			oscParams.initialXzSum = _targetXzSum - 0.01f;
			oscParams.brake = false;
			oscParams.prevMaxSpeed = prevMaxSpeed;
			oscParams.roughTargetAngle = _lastOscAngle;
			oscParams.optimizeMaxSpeed = false;
			auto turnRunStatus = Modify<BitFsPyramidOscillation_Iteration>(oscParams, _minFramePrev, _maxFramePrev);
			if (!turnRunStatus.asserted)
			{
				//try to get actual max speed
				terminate = true;
				oscParams.prevMaxSpeed = _prevMaxSpeed;
				oscParams.optimizeMaxSpeed = true;
				turnRunStatus = Modify<BitFsPyramidOscillation_Iteration>(oscParams, _minFramePrev, _maxFramePrev);
				if (!turnRunStatus.asserted)
					return false;
			}

			prevMaxSpeed = nextafterf(turnRunStatus.maxSpeed, INFINITY);

			*customStatus = Modify<BitFsScApproach_AttemptDr_BF>(_roughTargetAngle, turnRunStatus.framePassedEquilibriumPoint, turnRunStatus.m64Diff.frames.rbegin()->first);
			return customStatus->drLanded;
		},
		[&](auto incumbent, auto challenger) //comparator
		{
			if (challenger->drRelHeight > 0 && challenger->drRelHeight < incumbent->drRelHeight)
				return challenger;

			return incumbent;
		},
		[&](auto challenger) //terminator
		{
			return challenger->executed;
		});

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
