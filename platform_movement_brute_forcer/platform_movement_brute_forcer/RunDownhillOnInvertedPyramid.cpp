#include "Script.hpp"
#include "Types.hpp"

bool RunDownhillOnInvertedPyramid::verification()
{
	//Check if Mario is on the pyramid platform
	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	if (!marioState)
		return false;

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

	return true;
}

bool RunDownhillOnInvertedPyramid::execution()
{
	return true;
}

bool RunDownhillOnInvertedPyramid::validation()
{
	return true;
}