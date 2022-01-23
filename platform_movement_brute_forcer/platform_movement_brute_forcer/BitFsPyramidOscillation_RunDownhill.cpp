#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"

bool BitFsPyramidOscillation_RunDownhill::verification()
{
	//Check if Mario is on the pyramid platform
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* behavior = floorObject->behavior;
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game.addr("bhvBitfsTiltingInvertedPyramid"));
	if (behavior != pyramidBehavior)
		return false;

	//Check that Mario can enter walking action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_IDLE && action != ACT_PUNCHING)
		return false;

	return true;
}

bool BitFsPyramidOscillation_RunDownhill::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game.addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	Camera* camera = *(Camera**)(game.addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//This shouldn't go on forever, but set a max frame number just in case
	for (int n = 0; n < 1000; n++)
	{
		float prevNormalX = pyramid->oTiltingPyramidNormalX;
		float prevNormalZ = pyramid->oTiltingPyramidNormalZ;

		//Terminate if Mario is not on the platform
		if (marioState->marioObj->platform == NULL || marioState->marioObj->platform->behavior != pyramidBehavior)
			return true;

		auto status = Test<GetMinimumDownhillWalkingAngle>();

		//Terminate if unable to locate a downhill angle
		if (!status.executed)
			return true;

		Slot lastFrameSave = game.alloc_slot();
		
		//Calculate input bias direction to navigate around deadzones
		int32_t direction = 0;
		if (status.angleFacing > status.angleNotFacing)
			direction = -1;
		else if (status.angleFacing < status.angleNotFacing)
			direction = 1;

		//Attempt to run downhill with minimum angle
		auto stick = Inputs::GetClosestInputByYawHau(status.angleFacing, 32, camera->yaw, direction);
		advance_frame(Inputs(0, stick.first, stick.second));

		//Terminate and roll back frame if Mario is no longer running
		if (marioState->action != ACT_WALKING)
		{
			load_state(&lastFrameSave);
			return true;
		}

		//Record whether downhill angle attempt was successful + optimal
		CustomScriptStatus::FrameInputStatus frameStatus;

		int32_t faceYawHau = marioState->faceAngle[1] - (marioState->faceAngle[1] & 15);
		int32_t angleFacingHau = status.angleFacing - (status.angleFacing & 15);
		int32_t angleNotFacingHau = status.angleNotFacing - (status.angleNotFacing & 15);

		if (faceYawHau == angleFacingHau)
		{
			frameStatus.isAngleDownhill = true;
			frameStatus.isAngleOptimal = true;
		}
		else if (faceYawHau < angleFacingHau)
			frameStatus.isAngleDownhill = faceYawHau >= angleNotFacingHau;
		else
			frameStatus.isAngleDownhill = faceYawHau <= angleNotFacingHau;

		CustomStatus.frameStatuses[game.getCurrentFrame() - 1] = frameStatus;

		//Update max speed
		if (marioState->forwardVel > CustomStatus.maxSpeed)
			CustomStatus.maxSpeed = marioState->forwardVel;

		//Record initial tilt change direction and check if equilibrium point has been passed
		int xTiltDirection = Script::sign(pyramid->oTiltingPyramidNormalX - prevNormalX);
		int zTiltDirection = Script::sign(pyramid->oTiltingPyramidNormalZ - prevNormalZ);

		if (BaseStatus.m64Diff.frames.size() == 1)
		{
			_initXDirection = xTiltDirection;
			_initZDirection = zTiltDirection;
		}
		else if (
			CustomStatus.framePassedEquilibriumPoint == -1
			&& xTiltDirection != _initXDirection
			&& zTiltDirection != _initZDirection
			&& fabs(pyramid->oTiltingPyramidNormalX - prevNormalX) + fabs(pyramid->oTiltingPyramidNormalZ - prevNormalZ) >= 1.9999f)
		{
			CustomStatus.framePassedEquilibriumPoint = game.getCurrentFrame() - 1;
		}
	}

	return true;
}

bool BitFsPyramidOscillation_RunDownhill::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	return true;
}