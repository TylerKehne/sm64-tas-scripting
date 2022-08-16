#include <tasfw/scripts/BitFSPyramidOscillation.hpp>

#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <tasfw/Script.hpp>

bool BitFsPyramidOscillation_Iteration::validation()
{
	// Verify Mario is running on the platform
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	// Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_FINISH_TURNING_AROUND)
		return false;

	return true;
}

bool BitFsPyramidOscillation_Iteration::execution()
{
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));

	bool terminate = false;
	bool foundResult = false;
	auto turnRunStatus = ModifyCompareAdhoc<StatusField<BitFsPyramidOscillation_TurnThenRunDownhill>, std::tuple<int64_t>>(
		[&](auto iteration, auto& params) //paramsGenerator
		{
			auto& frame = std::get<0>(params);
			if (iteration == 0)
				frame = _maxFrame;
			else
				frame--;

			return !terminate && frame >= _minFrame;
		},
		[&](auto customStatus, auto frame) //script
		{
			Load(frame);
			CustomStatus.speedBeforeTurning = marioState->forwardVel;
			CustomStatus.initialXzSum = _oscillationParams.initialXzSum;
			customStatus->status = Modify<BitFsPyramidOscillation_TurnThenRunDownhill>(_oscillationParams);

			// Keep iterating until we get a valid result, then keep iterating until
			// we stop getting better results Once we get a valid result, we expect
			// successive iterations to be worse (less space to accelerate), but
			// that isn't always true
			if (!customStatus->status.asserted)
			{
				terminate = foundResult;
				return false;
			}

			if (customStatus->status.passedEquilibriumSpeed > 0)
				foundResult = true;
			else if (customStatus->status.maxSpeed == 0.0f)
			{
				terminate = true;
				return false;
			}

			return true;
		},
		[&](auto status1, auto status2) //comparator
		{
			if (status2->status.passedEquilibriumSpeed > status1->status.passedEquilibriumSpeed)
				return status2;
			else if (status2->status.maxSpeed == 0.0f)
				terminate = true;

			return status1;
		}).status;

	/*
	ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill> turnRunStatus;
	for (int64_t frame = _maxFrame; frame >= _minFrame; frame--)
	{
		Load(frame);
		CustomStatus.speedBeforeTurning = marioState->forwardVel;
		CustomStatus.initialXzSum		= _oscillationParams.initialXzSum;
		auto status = Execute<BitFsPyramidOscillation_TurnThenRunDownhill>(_oscillationParams);

		// Keep iterating until we get a valid result, then keep iterating until
		// we stop getting better results Once we get a valid result, we expect
		// successive iterations to be worse (less space to accelerate), but
		// that isn't always true
		if (!status.asserted)
		{
			if (turnRunStatus.passedEquilibriumSpeed == 0.0f)
				continue;
			else
				break;
		}

		if (status.passedEquilibriumSpeed >
			turnRunStatus.passedEquilibriumSpeed)
			turnRunStatus = status;
		else if (status.maxSpeed == 0.0f)
			break;
	}
	*/

	if (!turnRunStatus.asserted)
		return false;

	CustomStatus.finalXzSum = turnRunStatus.finalXzSum;
	CustomStatus.maxSpeed = turnRunStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = turnRunStatus.passedEquilibriumSpeed;
	CustomStatus.framePassedEquilibriumPoint = turnRunStatus.framePassedEquilibriumPoint;
	CustomStatus.finishTurnaroundFailedToExpire = turnRunStatus.finishTurnaroundFailedToExpire;

	//Apply(turnRunStatus.m64Diff);

	return true;
}

bool BitFsPyramidOscillation_Iteration::assertion()
{
	return !IsDiffEmpty();
}