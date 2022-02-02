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

	const BehaviorScript* behavior = floorObject->behavior;
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (behavior != pyramidBehavior)
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
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	//Get range of target turning around angles to test
	auto hillStatus = Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
	Rotation downhillRotation = hillStatus.downhillRotation == Rotation::CLOCKWISE ? Rotation::CLOCKWISE : Rotation::COUNTERCLOCKWISE;
	int32_t extremeDownhillHau = hillStatus.angleFacing - (hillStatus.angleFacing & 15);
	int32_t extremeUphillHau = extremeDownhillHau - 0x3F00 * (int)downhillRotation;

	//Verify we aren't too close to the lava
	if (Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(extremeUphillHau).framePassedEquilibriumPoint == -1)
		return false;

	//Binary search to get most downhill turning angle that passes equilibrium point
	if (Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(extremeDownhillHau).framePassedEquilibriumPoint == -1)
	{
		int32_t downhillLimitHau = extremeDownhillHau;
		int32_t uphillLimitHau = extremeUphillHau;
		while (true)
		{
			int32_t midAngle = (downhillLimitHau + uphillLimitHau) / 2;
			int32_t midHau = midAngle - (midAngle & 15);

			//Boundary is two HAUs wide and we checked the edges already so this should never happen
			if (midHau == downhillLimitHau || midHau == uphillLimitHau)
				throw std::exception("Binary search failed to find downhill turning limit.");

			bool passedEquilibriumPoint = Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau).framePassedEquilibriumPoint != -1;
			if (passedEquilibriumPoint)
			{
				if (Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau + 16 * (int)downhillRotation).framePassedEquilibriumPoint == -1)
				{
					extremeDownhillHau = midHau;
					break;
				}

				uphillLimitHau = midHau;
			}
			else
			{
				if (Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau - 16 * (int)downhillRotation).framePassedEquilibriumPoint != -1)
				{
					extremeDownhillHau = midHau - 16 * (int)downhillRotation;
					break;
				}

				downhillLimitHau = midHau;
			}
		}
	}

	//Verify window for preserving XZ sum exists
	if (!Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(extremeDownhillHau).validated)
		return false;

	//Binary search to get most uphill turning angle that preserves XZ sum
	int32_t optimalHau = extremeUphillHau;
	if (!Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(extremeUphillHau).validated)
	{
		int32_t downhillLimitHau = extremeDownhillHau;
		int32_t uphillLimitHau = extremeUphillHau;
		while (true)
		{
			int32_t midAngle = (downhillLimitHau + uphillLimitHau) / 2;
			int32_t midHau = midAngle - (midAngle & 15);

			//Boundary is two HAUs wide and we checked the edges already so this should never happen
			if (midHau == downhillLimitHau || midHau == uphillLimitHau)
				throw std::exception("Binary search failed to find optimal HAU.");

			if (!Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau).validated)
			{
				if (Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau + 16 * (int)downhillRotation).validated)
				{
					optimalHau = midHau + 16 * (int)downhillRotation;
					break;
				}

				uphillLimitHau = midHau;
			}
			else
			{
				if (!Test<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(midHau - 16 * (int)downhillRotation).validated)
				{
					optimalHau = midHau;
					break;
				}

				downhillLimitHau = midHau;
			}
		}
	}
	
	auto runStatus = Modify<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(optimalHau);

	CustomStatus.finalXzSum = runStatus.finalXzSum;
	CustomStatus.framePassedEquilibriumPoint = runStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runStatus.maxSpeed;

	return runStatus.validated;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	if (CustomStatus.finalXzSum < CustomStatus.initialXzSum - 0.00001)
		return false;

	if (CustomStatus.framePassedEquilibriumPoint == -1)
		return false;

	return true;
}