#include <tasfw/scripts/BitFSPyramidOscillation.hpp>
#include <tasfw/scripts/General.hpp>

#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <tasfw/Script.hpp>

bool BitFsPyramidOscillation_TurnThenRunDownhill::validation()
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
		(const BehaviorScript*) (resource->addr(
			"bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	// Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_FINISH_TURNING_AROUND)
		return false;

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::execution()
{
	MarioState* marioState = *(MarioState**) (resource->addr("gMarioState"));
	Camera* camera		   = *(Camera**) (resource->addr("gCamera"));
	Object* pyramid		   = marioState->floor->object;

	// Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = _oscillationParams.initialXzSum;

	// Get range of target turning around angles to test
	auto hillStatus =
		Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
	Rotation downhillRotation =
		hillStatus.downhillRotation == Rotation::CLOCKWISE ?
		Rotation::CLOCKWISE :
		Rotation::COUNTERCLOCKWISE;
	int32_t extremeDownhillHau =
		hillStatus.angleFacing - (hillStatus.angleFacing & 15U);
	int32_t extremeUphillHau =
		extremeDownhillHau - 0x4000 * (int) downhillRotation;
	int32_t midHau = (extremeDownhillHau + extremeUphillHau) / 2;

	ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle> runStatus;
	for (int32_t angle = midHau;
		 angle * -downhillRotation >= extremeDownhillHau * -downhillRotation;
		 angle += 512 * downhillRotation)
	{
		auto status =
			Execute<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(
				_oscillationParams, angle);

		if (!status.asserted)
		{
			if (status.tooDownhill)
				break;
			else
				continue;
		}

		if (status.passedEquilibriumSpeed > runStatus.passedEquilibriumSpeed)
			runStatus = status;
	}

	for (int32_t angle = midHau - 512 * downhillRotation;
		 angle * -downhillRotation <= extremeUphillHau * -downhillRotation;
		 angle -= 512 * downhillRotation)
	{
		auto status =
			Execute<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(
				_oscillationParams, angle);

		if (!status.asserted)
		{
			if (status.tooUphill)
				break;
			else
				continue;
		}

		if (status.passedEquilibriumSpeed > runStatus.passedEquilibriumSpeed)
			runStatus = status;
	}

	if (!runStatus.asserted)
		return false;

	CustomStatus.finalXzSum = runStatus.finalXzSum;
	CustomStatus.framePassedEquilibriumPoint =
		runStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed				= runStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = runStatus.passedEquilibriumSpeed;
	CustomStatus.finishTurnaroundFailedToExpire =
		runStatus.finishTurnaroundFailedToExpire;

	Apply(runStatus.m64Diff);

	return runStatus.asserted;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::assertion()
{
	if (IsDiffEmpty())
		return false;

	return true;
}