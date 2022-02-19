#include "Script.hpp"
#include <cmath>

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::verification()
{
	//Verify Mario is running on the platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

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

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	Camera* camera = *(Camera**)(game->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	//Keep running until target angle is reached
	int16_t actualIntendedYaw = 0;
	int16_t roughTargetAngle = marioState->faceAngle[1] + 0x8000;
	do
	{
		//Don't want to turn around early, so cap intended yaw diff at 2048
		int16_t intendedYaw = _angle;
		if (abs(_angle - marioState->faceAngle[1]) > 2048)
			intendedYaw = marioState->faceAngle[1] + 2048 * sign(_angle - marioState->faceAngle[1]);

		auto inputs = Inputs::GetClosestInputByYawHau(_angle, 32, camera->yaw);
		actualIntendedYaw = Inputs::GetIntendedYawMagFromInput(inputs.first, inputs.second, camera->yaw).first;
		AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

		if ((marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
			|| marioState->floor->object == NULL
			|| marioState->floor->object->behavior != pyramidBehavior)
			return false;

		//At least 16 speed is needed for turnaround
		if (marioState->forwardVel < 16.0f)
		{
			CustomStatus.tooSlowForTurnAround = true;
			return false;
		}

		//Check if we need extra frames to wait for ACT_FINISH_TURNING_AROUND to expire
		if (marioState->faceAngle[1] == actualIntendedYaw && marioState->action == ACT_FINISH_TURNING_AROUND)
			CustomStatus.finishTurnaroundFailedToExpire = true;
	} while (marioState->faceAngle[1] != actualIntendedYaw || marioState->action == ACT_FINISH_TURNING_AROUND);

	if (marioState->action != ACT_WALKING)
	{
		CustomStatus.tooDownhill = (marioState->action == ACT_LAVA_BOOST);
		return false;
	}

	ScriptStatus<BitFsPyramidOscillation_TurnAroundAndRunDownhill> runDownhillStatus;
	uint64_t initFrame = GetCurrentFrame();
	//This should never reach 50 frames, but just in case
	for (int i = 0; i < 50; i++)
	{
		//Immediately turn around and run downhill optimally
		auto status = Execute<BitFsPyramidOscillation_TurnAroundAndRunDownhill>(_brake, roughTargetAngle);

		//Something weird happened, terminate
		if (!status.validated)
			break;

		//We've gone too far uphill, terminate
		if (status.tooUphill)
			break;

		//Extra running frames are no longer helping, terminate
		//Note that this requires a previous valid status to trigger
		//This is the value we want to maximize
		if (status.passedEquilibriumSpeed < runDownhillStatus.passedEquilibriumSpeed)
			break;

		//Route is only valid if it got more speed than the last time
		//We aren't trying to maxmimize this, but this ensures route won't be too close to the lava
		//Also make sure equillibrium point has been passed
		if (status.maxSpeed >= _prevMaxSpeed && status.framePassedEquilibriumPoint != -1)
			runDownhillStatus = status;
		//This also indicates running frames aren't helping and we can terminate, except when past the threshold
		//In that case, we don't mind if this goes down.
		else if (status.maxSpeed <= runDownhillStatus.maxSpeed)
			break;

		//Run forward for another frame and try again
		auto inputs = Inputs::GetClosestInputByYawHau(_angle, 32, camera->yaw);
		AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
	} 
	
	if (!runDownhillStatus.validated)
	{
		//We want to record if these flags are set without any additional running frames
		//This tells the caller that the angle paramter is definitely not in the valid range
		//After the first frame it's more ambiguous, so don't record
		if (GetCurrentFrame() != initFrame)
		{
			CustomStatus.tooDownhill = runDownhillStatus.tooDownhill;
			CustomStatus.tooUphill = runDownhillStatus.tooUphill;
		}

		return false;
	}
		
	CustomStatus.framePassedEquilibriumPoint = runDownhillStatus.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed = runDownhillStatus.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = runDownhillStatus.passedEquilibriumSpeed;
	CustomStatus.finalXzSum = runDownhillStatus.finalXzSum;
	CustomStatus.finishTurnaroundFailedToExpire |= runDownhillStatus.finishTurnaroundFailedToExpire;

	Apply(runDownhillStatus.m64Diff);

	return true;
}

bool BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle::validation()
{
	if (BaseStatus.m64Diff.frames.empty())
		return false;

	if (CustomStatus.finalXzSum < _minXzSum - 0.00001)
		return false;

	if (CustomStatus.framePassedEquilibriumPoint == -1)
		return false;

	return true;
}