#include <tasfw/scripts/BitFSPyramidOscillation.hpp>

#include <tasfw/Script.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>
#include <sm64/Pyramid.hpp>
#include <sm64/Surface.hpp>
#include <sm64/Trig.hpp>

bool BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::validation()
{
	// Check if Mario is on the pyramid platform
	auto marioObj = (PyramidUpdateMem::Sm64Object*)(resource->addr("gMarioObject"));
	return marioObj->platformIsPyramid;
}

bool BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::execution()
{
	AdvanceFrameRead();

	auto marioState = (PyramidUpdateMem::Sm64MarioState*)(resource->addr("gMarioStates"));
	auto pyramid = (PyramidUpdateMem::Sm64Object*)(resource->addr("Pyramid"));
	if (marioState->floorId == -1 || marioState->isFloorStatic)
	{
		CustomStatus.floorAngle = 0;
		CustomStatus.isSlope = false;
		return false;
	}

	auto floor = &pyramid->surfaces[1][marioState->floorId];
	short floorAngle = atan2s(floor->normal.z, floor->normal.x);
	CustomStatus.isSlope = PyramidUpdateMem::FloorIsSlope(floor, marioState->action);

	// m->floorAngle - m->faceAngle[1] >= -0x3FFF && m->floorAngle -
	// m->faceAngle[1] <= 0x3FFF
	int32_t lowerAngle = floorAngle + 0x3FFF;
	int32_t upperAngle = floorAngle - 0x3FFF;

	int32_t lowerAngleDiff = abs(int16_t(lowerAngle - _targetAngle));
	int32_t upperAngleDiff = abs(int16_t(upperAngle - _targetAngle));

	if (lowerAngleDiff <= upperAngleDiff)
	{
		CustomStatus.angleFacing = lowerAngle;
		CustomStatus.angleNotFacing = upperAngle;
		CustomStatus.downhillRotation = lowerAngleDiff < upperAngleDiff ? Rotation::CLOCKWISE : Rotation::NONE;
	}
	else
	{
		CustomStatus.angleFacing = upperAngle;
		CustomStatus.angleNotFacing = lowerAngle;
		CustomStatus.downhillRotation = Rotation::COUNTERCLOCKWISE;
	}

	// Get optimal angle for switching from turnaround to finish turnaround
	int16_t facingDYaw = _faceAngle - CustomStatus.angleFacing;
	if (abs(facingDYaw) <= 0x471C)
		CustomStatus.angleFacingAnalogBack = _faceAngle + 0x471D * sign(facingDYaw);
	else
		CustomStatus.angleFacingAnalogBack = CustomStatus.angleFacing;

	int16_t notFacingDYaw = _faceAngle - CustomStatus.angleNotFacing;
	if (abs(notFacingDYaw) <= 0x471C)
		CustomStatus.angleNotFacingAnalogBack = _faceAngle + 0x471D * sign(notFacingDYaw);
	else
		CustomStatus.angleNotFacingAnalogBack = CustomStatus.angleNotFacing;

	CustomStatus.floorAngle = floorAngle;

	return true;
}

bool BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::assertion()
{
	return CustomStatus.isSlope;
}
