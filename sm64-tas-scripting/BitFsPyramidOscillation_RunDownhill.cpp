#include "Camera.hpp"
#include "Script.hpp"
#include "Sm64.hpp"
#include "Types.hpp"

#include <cmath>

bool BitFsPyramidOscillation_RunDownhill::verification()
{
	// Check if Mario is on the pyramid platform
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
	if (
		action != ACT_WALKING && action != ACT_IDLE && action != ACT_PUNCHING &&
		action != ACT_TURNING_AROUND)
		return false;

	return true;
}

bool BitFsPyramidOscillation_RunDownhill::execution()
{
	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));
	Camera* camera				 = *(Camera**) (game->addr("gCamera"));
	Object* pyramid				 = marioState->floor->object;

	int targetXDirection = sign(gSineTable[(uint16_t)(_oscillationParams.roughTargetAngle) >> 4]);
	int targetZDirection = sign(gCosineTable[(uint16_t)(_oscillationParams.roughTargetAngle) >> 4]);

	// This shouldn't go on forever, but set a max frame number just in case
	for (int n = 0; n < 1000; n++)
	{
		float prevNormalX = pyramid->oTiltingPyramidNormalX;
		float prevNormalZ = pyramid->oTiltingPyramidNormalZ;

		//Turnaround face angle is not guaranteed to be closer to one orientation or the other, so rely on caller to specify a close-enough target orientation
		auto status = Test<GetMinimumDownhillWalkingAngle>(_oscillationParams.roughTargetAngle, marioState->faceAngle[1]);

		// Terminate if unable to locate a downhill angle
		if (!status.executed)
			return true;

		// Attempt to run downhill with minimum angle
		int16_t intendedYaw = marioState->action == ACT_TURNING_AROUND ?
			status.angleFacingAnalogBack :
			status.angleFacing;
		auto stick					= Inputs::GetClosestInputByYawExact(
							 intendedYaw, 32, camera->yaw, status.downhillRotation);
		AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

		// Terminate and roll back frame if Mario is no longer running on the
		// platform
		if (
			(marioState->action != ACT_WALKING &&
			 marioState->action != ACT_FINISH_TURNING_AROUND) ||
			marioState->marioObj->platform == NULL ||
			marioState->marioObj->platform->behavior != pyramidBehavior)
		{
			Rollback(GetCurrentFrame() - 1);
			return true;
		}

		// Record whether downhill angle attempt was successful + optimal
		CustomScriptStatus::FrameInputStatus frameStatus;

		if (!status.asserted)
			frameStatus.isAngleDownhill = false;
		else if (marioState->faceAngle[1] == status.angleFacing)
			frameStatus.isAngleDownhill = true;
		else if (marioState->faceAngle[1] < status.angleFacing)
			frameStatus.isAngleDownhill =
				marioState->faceAngle[1] >= status.angleNotFacing;
		else
			frameStatus.isAngleDownhill =
				marioState->faceAngle[1] <= status.angleNotFacing;

		int32_t faceYawHau =
			marioState->faceAngle[1] - (marioState->faceAngle[1] & 15);
		int32_t angleFacingHau = status.angleFacing - (status.angleFacing & 15);
		int32_t angleNotFacingHau =
			status.angleNotFacing - (status.angleNotFacing & 15);
		frameStatus.isAngleOptimal = frameStatus.isAngleDownhill &&
			(faceYawHau == angleFacingHau || faceYawHau == angleNotFacingHau);

		CustomStatus.frameStatuses[GetCurrentFrame() - 1] = frameStatus;

		// Update max speed
		if (marioState->forwardVel > CustomStatus.maxSpeed)
			CustomStatus.maxSpeed = marioState->forwardVel;

		//Check if equilibrium point has been passed
		//The axis we want to check is determined by whichever normal component is lesser
		int tiltDirection, targetTiltDirection;
		float normalDiff;
		if (fabs(pyramid->oTiltingPyramidNormalX) < fabs(pyramid->oTiltingPyramidNormalZ))
		{
			tiltDirection = Script::sign(pyramid->oTiltingPyramidNormalX - prevNormalX);
			targetTiltDirection = targetXDirection;
			normalDiff = fabs(pyramid->oTiltingPyramidNormalX - prevNormalX);
		}
		else
		{
			tiltDirection = Script::sign(pyramid->oTiltingPyramidNormalZ - prevNormalZ);
			targetTiltDirection = targetZDirection;
			normalDiff = fabs(pyramid->oTiltingPyramidNormalZ - prevNormalZ);
		}

		if (
			CustomStatus.framePassedEquilibriumPoint == -1
			&& tiltDirection == targetTiltDirection
			&& normalDiff >= 0.0099999f)
		{
			CustomStatus.passedEquilibriumSpeed			 = marioState->forwardVel;
			CustomStatus.framePassedEquilibriumPoint = GetCurrentFrame() - 1;
			CustomStatus.finalXzSum = 
				pyramid->oTiltingPyramidNormalX * (_oscillationParams.quadrant < 3 ? 1 : -1)
				+ pyramid->oTiltingPyramidNormalZ * (_oscillationParams.quadrant == 1 || _oscillationParams.quadrant == 4 ? 1 : -1);

			if (CustomStatus.finalXzSum < _oscillationParams.targetXzSum - 0.00001 && CustomStatus.finalXzSum < _oscillationParams.initialXzSum + 0.01f)
			{
				CustomStatus.tooUphill = true;
				return true;
			}
		}
	}

	return true;
}

bool BitFsPyramidOscillation_RunDownhill::assertion()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	return true;
}