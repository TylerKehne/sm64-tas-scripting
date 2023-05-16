#include <BitFSPyramidOscillation.hpp>
#include <General.hpp>

#include <cmath>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

bool BitFsPyramidOscillation_TurnThenRunDownhill::CompareSpeed(
	const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>& status1,
	const ScriptStatus<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>& status2)
{
	if (_oscillationParams.optimizeMaxSpeed)
	{
		if (abs(status2.maxSpeed - status1.maxSpeed) < 0.2f)
			return status2.passedEquilibriumXzDist > status1.passedEquilibriumXzDist;

		return status2.maxSpeed > status1.maxSpeed;
	}

	if (abs(status2.passedEquilibriumSpeed - status1.passedEquilibriumSpeed) < 0.2f)
		return status2.passedEquilibriumXzDist > status1.passedEquilibriumXzDist;

	return status2.passedEquilibriumSpeed > status1.passedEquilibriumSpeed;
}

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
		(const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
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
	MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

	//Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = _oscillationParams.initialXzSum;

	//Get range of target turning around angles to test
	auto hillStatus = Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
	Rotation downhillRotation = hillStatus.downhillRotation == Rotation::CLOCKWISE ? Rotation::CLOCKWISE : Rotation::COUNTERCLOCKWISE;
	int32_t extremeDownhillHau = hillStatus.angleFacing - (hillStatus.angleFacing & 15U);
	int32_t extremeUphillHau = extremeDownhillHau - 0x4000 * (int)downhillRotation;
	int32_t midHau = (extremeDownhillHau + extremeUphillHau) / 2;

	int progression = 0;
	auto runStatus = ModifyCompareAdhoc<StatusField<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>, std::tuple<int32_t>>(
		[&]([[maybe_unused]] auto iteration, auto& params) //paramsGenerator
		{
			auto& angle = std::get<0>(params);

			if (progression < 2)
			{
				if (progression == 0) //init downhill
				{
					angle = midHau;
					progression = 1;
				}
				else if (progression == 1) //increment downhill
					angle += 512 * downhillRotation;

				if (angle * -downhillRotation < extremeDownhillHau * -downhillRotation)
					progression = 2;
				else
					return true;
			}

			if (progression == 2) //init uphill
			{
				angle = midHau - 512 * downhillRotation;
				progression = 3;
			}
			else if (progression == 3) //iterate uphill
				angle -= 512 * downhillRotation;

			return progression != 4 && (angle * -downhillRotation <= extremeUphillHau * -downhillRotation);
		},
		[&](auto customStatus, auto angle) //script
		{
			customStatus->status = Modify<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(_oscillationParams, angle);

			if (!customStatus->status.asserted)
			{
				if (customStatus->status.tooDownhill)
					progression = 2; //do uphill half
				else if (customStatus->status.tooUphill)
					progression = 4; //terminate

				return false;
			}

			return customStatus->status.passedEquilibriumSpeed > 0;
		},
		[&](auto status1, auto status2) //comparator
		{
			if (CompareSpeed(status1->status, status2->status))
				return status2;

			return status1;
		}).status;

	if (runStatus.asserted)
	{
		progression = 0;
		int16_t midHau2 = runStatus.angle;
		auto runStatus2 = ModifyCompareAdhoc<StatusField<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>, std::tuple<int32_t>>(
			[&]([[maybe_unused]] auto iteration, auto& params) //paramsGenerator
			{
				auto& angle = std::get<0>(params);

				if (progression < 2)
				{
					if (progression == 0) //init downhill
					{
						angle = midHau2;
						progression = 1;
					}
					else if (progression == 1) //increment downhill
						angle += 16 * downhillRotation;

					if (angle * -downhillRotation < (midHau2 + 496 * downhillRotation) * -downhillRotation)
						progression = 2;
					else
						return true;
				}

				if (progression == 2) //init uphill
				{
					angle = midHau - 16 * downhillRotation;
					progression = 3;
				}
				else if (progression == 3) //iterate uphill
					angle -= 16 * downhillRotation;

				return progression != 4 && (angle * -downhillRotation <= (midHau2 - 496 * downhillRotation) * -downhillRotation);
			},
			[&](auto customStatus, auto angle) //script
			{
				if (angle == midHau2)
					customStatus->status = runStatus;
				else
					customStatus->status = Modify<BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle>(_oscillationParams, angle);

				if (!customStatus->status.asserted)
				{
					if (customStatus->status.tooDownhill)
						progression = 2; //do uphill half
					else if (customStatus->status.tooUphill)
						progression = 4; //terminate

					return false;
				}

				return customStatus->status.passedEquilibriumSpeed > 0;
			},
				[&](auto status1, auto status2) //comparator
			{
				if (CompareSpeed(status1->status, status2->status))
					return status2;

				return status1;
			}).status;

		runStatus = runStatus2;
	}

	if (!runStatus.asserted)
		return false;

	CustomStatus.finalXzSum = runStatus.finalXzSum;
	CustomStatus.framePassedEquilibriumPoint = runStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = runStatus.passedEquilibriumSpeed;
	CustomStatus.passedEquilibriumXzDist = runStatus.passedEquilibriumXzDist;
	CustomStatus.finishTurnaroundFailedToExpire = runStatus.finishTurnaroundFailedToExpire;

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill::assertion()
{
	if (IsDiffEmpty())
		return false;

	return true;
}
