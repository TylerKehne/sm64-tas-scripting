#include "Script.hpp"

bool BitFsPyramidOscillation_TurnThenRunDownhill::verification()
{
	//Verify Mario is running on the platform
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

	//Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_FINISH_TURNING_AROUND)
		return false;

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::execution()
{
	MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = _oscillationParams.initialXzSum;

	//Get range of target turning around angles to test
	auto hillStatus = Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
	Rotation downhillRotation = hillStatus.downhillRotation == Rotation::CLOCKWISE ? Rotation::CLOCKWISE : Rotation::COUNTERCLOCKWISE;
	int32_t extremeDownhillHau = hillStatus.angleFacing - (hillStatus.angleFacing & 15);
	int32_t extremeUphillHau = extremeDownhillHau - 0x4000 * (int)downhillRotation;
	int32_t midHau = (extremeDownhillHau + extremeUphillHau) / 2;

	ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle> runStatus;
	for (int32_t angle = midHau; angle * -downhillRotation >= extremeDownhillHau * -downhillRotation; angle += 512 * downhillRotation)
	{
		auto status = Execute<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(_oscillationParams, angle);

		if (!status.validated)
		{
			if (status.tooDownhill)
				break;
			else
				continue;
		}

		if (status.passedEquilibriumSpeed > runStatus.passedEquilibriumSpeed)
			runStatus = status;
	}

	for (int32_t angle = midHau - 512 * downhillRotation; angle * -downhillRotation <= extremeUphillHau * -downhillRotation; angle -= 512 * downhillRotation)
	{
		auto status = Execute<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(_oscillationParams, angle);

		if (!status.validated)
		{
			if (status.tooUphill)
				break;
			else
				continue;
		}

		if (status.passedEquilibriumSpeed > runStatus.passedEquilibriumSpeed)
			runStatus = status;
	}

	if (!runStatus.validated)
		return false;

	CustomStatus.finalXzSum = runStatus.finalXzSum;
	CustomStatus.framePassedEquilibriumPoint = runStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = runStatus.passedEquilibriumSpeed;
	CustomStatus.finishTurnaroundFailedToExpire = runStatus.finishTurnaroundFailedToExpire;

	Apply(runStatus.m64Diff);

	return runStatus.validated;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::validation()
{
	if (BaseStatus.m64Diff.frames.empty())
		return false;

	return true;
}