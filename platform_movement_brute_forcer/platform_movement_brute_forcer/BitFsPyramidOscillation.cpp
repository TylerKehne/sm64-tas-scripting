#include "Script.hpp"
#include "Types.hpp"
#include "Sm64.hpp"
#include "Camera.hpp"
#include "ObjectFields.hpp"

bool BitFsPyramidOscillation::verification()
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

	//Check that Mario is idle
	uint32_t action = marioState->action;
	if (action != ACT_IDLE)
		return false;

	//Check that pyramid is at equilibrium
	vector<float> normal = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };
	advance_frame();
	vector<float> normal2 = { floorObject->oTiltingPyramidNormalX, floorObject->oTiltingPyramidNormalY, floorObject->oTiltingPyramidNormalZ };

	if (normal != normal2)
		return false;

	return true;
}

bool BitFsPyramidOscillation::execution()
{
	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(game.addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	Camera* camera = *(Camera**)(game.addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//Record initial XZ sum, don't want to decrease this
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

	auto status = Execute<BitFsPyramidOscillation_RunDownhill>();
	if (!status.validated)
		return false;

	if (status.maxSpeed > CustomStatus.maxSpeed)
		CustomStatus.maxSpeed = status.maxSpeed;

	//We want to turn uphill as late as possible, and also turn around as late as possible, without sacrificing XZ sum


	return true;
}

bool BitFsPyramidOscillation::validation()
{
	if (!BaseStatus.m64Diff.frames.size())
		return false;

	return true;
}