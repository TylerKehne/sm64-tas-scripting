#include <BitFSPyramidOscillation.hpp>
#include <General.hpp>

#include <cmath>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::CompareSpeed(
	const ScriptStatus<BitFsPyramidOscillation_TurnAroundAndRunDownhill>& status1,
	const ScriptStatus<BitFsPyramidOscillation_TurnAroundAndRunDownhill>& status2)
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

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::validation()
{
	CustomStatus.angle = _angle;

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

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));
	Camera* camera = *(Camera**)(resource->addr("gCamera"));

	CustomStatus.initialXzSum = _oscillationParams.initialXzSum;
	_oscillationParams.roughTargetAngle = marioState->faceAngle[1] + 0x8000;

	// Keep running until target angle is reached
	int16_t actualIntendedYaw = 0;
	do
	{
		// Don't want to turn around early, so cap intended yaw diff at 2048
		int16_t intendedYaw = _angle;
		if (abs(int16_t(_angle - marioState->faceAngle[1])) >= 16384)
			intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(_angle - marioState->faceAngle[1]));

		auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
		actualIntendedYaw = Inputs::GetIntendedYawMagFromInput(inputs.first, inputs.second, camera->yaw).first;
		AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

		if ((marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
			|| marioState->floor->object == nullptr
			|| marioState->floor->object->behavior != pyramidBehavior)
			return false;

		// At least 16 speed is needed for turnaround
		if (marioState->forwardVel < 16.0f)
		{
			CustomStatus.tooSlowForTurnAround = true;
			return false;
		}

		// Check if we need extra frames to wait for ACT_FINISH_TURNING_AROUND
		// to expire
		if (marioState->faceAngle[1] == actualIntendedYaw
			&& marioState->action == ACT_FINISH_TURNING_AROUND)
			CustomStatus.finishTurnaroundFailedToExpire = true;
	} while (marioState->faceAngle[1] != actualIntendedYaw || marioState->action == ACT_FINISH_TURNING_AROUND);

	if (marioState->action != ACT_WALKING)
	{
		CustomStatus.tooDownhill = (marioState->action == ACT_LAVA_BOOST);
		return false;
	}

	uint64_t initFrame = GetCurrentFrame();
	bool terminate = false;
	auto status = DynamicModifyCompareAdhoc<StatusField<BitFsPyramidOscillation_TurnAroundAndRunDownhill>, std::tuple<BitFsPyramidOscillation_ParamsDto>>(
		[&](auto iteration, auto& params) //paramsGenerator
		{
			params = _oscillationParams;
			return !terminate && iteration < 100;
		},
		[&](auto customStatus, BitFsPyramidOscillation_ParamsDto params) //script
		{
			terminate = false;
			customStatus->status = Modify<BitFsPyramidOscillation_TurnAroundAndRunDownhill>(params);

			if (!customStatus->status.asserted || customStatus->status.tooUphill)
			{
				terminate = true;
				return false;
			}

			if (customStatus->status.maxSpeed < _oscillationParams.prevMaxSpeed || customStatus->status.passedEquilibriumSpeed <= 0)
			{
				if (customStatus->status.maxSpeed <= 0)
					terminate = true;

				return false;
			}

			return true;
		},
		[&]() //mutator
		{
			// Run forward for another frame and try again
			auto inputs = Inputs::GetClosestInputByYawHau(_angle, 32, camera->yaw);
			AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
			return true;
		},
		[&](auto status1, auto status2) //comparator
		{
			//Route is only valid if it got more speed than the last time
			//We aren't trying to maxmimize this, but this ensures route won't be too close to the lava
			//Also make sure equilibrium point has been passed
			if (status2->status.maxSpeed >= _oscillationParams.prevMaxSpeed
				&& CompareSpeed(status1->status, status2->status))
				return status2;
			// This also indicates running frames aren't helping and we can
			// terminate, except when past the threshold. In that case, we don't mind
			// if this goes down.
			else if (status2->status.maxSpeed < status1->status.maxSpeed)
				terminate = true;

			return status1;
		}
	);

	auto& runDownhillStatus = status.substatus.status;

	if (!status.executed)
	{
		// We want to record if these flags are set without any additional
		// running frames This tells the caller that the angle paramter is
		// definitely not in the valid range After the first frame it's more
		// ambiguous, so don't record
		if (GetCurrentFrame() != initFrame)
		{
			CustomStatus.tooDownhill = runDownhillStatus.tooDownhill;
			CustomStatus.tooUphill	 = runDownhillStatus.tooUphill;
		}

		return false;
	}

	CustomStatus.framePassedEquilibriumPoint = runDownhillStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runDownhillStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = runDownhillStatus.passedEquilibriumSpeed;
	CustomStatus.passedEquilibriumXzDist = runDownhillStatus.passedEquilibriumXzDist;
	CustomStatus.finalXzSum = runDownhillStatus.finalXzSum;
	CustomStatus.finishTurnaroundFailedToExpire |= runDownhillStatus.finishTurnaroundFailedToExpire;

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::assertion()
{
	if (IsDiffEmpty())
		return false;

	if (CustomStatus.tooUphill || CustomStatus.tooDownhill)
		return false;

	if (CustomStatus.framePassedEquilibriumPoint == -1)
		return false;

	return true;
}