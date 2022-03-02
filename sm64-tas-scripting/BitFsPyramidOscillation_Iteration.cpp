#include "Script.hpp"

bool BitFsPyramidOscillation_Iteration::verification()
{
	// Verify Mario is running on the platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
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
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	Camera* camera				 = *(Camera**) (game->addr("gCamera"));
	Object* pyramid				 = marioState->floor->object;

	ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill> turnRunStatus;
	for (uint64_t frame = _maxFrame; frame >= _minFrame; frame--)
	{
		Load(frame);
		CustomStatus.speedBeforeTurning = marioState->forwardVel;
		CustomStatus.initialXzSum = _oscillationParams.initialXzSum;
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

		if (status.passedEquilibriumSpeed > turnRunStatus.passedEquilibriumSpeed)
			turnRunStatus = status;
		else if (status.maxSpeed == 0.0f)
			break;
	}

	if (!turnRunStatus.asserted)
		return false;

	CustomStatus.finalXzSum = turnRunStatus.finalXzSum;
	CustomStatus.maxSpeed = turnRunStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = turnRunStatus.passedEquilibriumSpeed;
	CustomStatus.framePassedEquilibriumPoint =
		turnRunStatus.framePassedEquilibriumPoint;
	CustomStatus.finishTurnaroundFailedToExpire =
		turnRunStatus.finishTurnaroundFailedToExpire;

	Apply(turnRunStatus.m64Diff);

	return true;
}

bool BitFsPyramidOscillation_Iteration::assertion()
{
	if (BaseStatus.m64Diff.frames.empty())
		return false;

	return true;
}