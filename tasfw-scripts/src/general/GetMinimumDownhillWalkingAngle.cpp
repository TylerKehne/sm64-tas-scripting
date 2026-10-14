#include <General.hpp>

#include <tasfw/Script.hpp>
#include <ScriptMath.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>

#include <cmath>
#include <cstdlib>

bool GetMinimumDownhillWalkingAngle::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (ReadState("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (ReadState("bhvBitfsTiltingInvertedPyramid"));
	return floorObject->behavior == pyramidBehavior;
}

bool GetMinimumDownhillWalkingAngle::execution()
{
	MarioState* marioState = (MarioState*) (ReadState("gMarioStates"));

	// The pyramid tilts toward where Mario stands before he steps (the surface objects
	// update before the player in a frame), so one frame on, the floor under him carries
	// the normal his next step will walk on, whatever that step's inputs. The frame is
	// this script's own and is reverted with it.
	//
	// The floor is read again after the frame, never kept across it. The game rebuilds its
	// surface pool every frame, and an object's surfaces take other slots when another
	// surface object's collision loads or unloads (the other pyramid's does, 1000 units
	// from Mario, right where the oscillations turn around), so a Surface pointer kept
	// across a frame can point at a slot nothing reloaded, still holding the old normal
	// (docs/tasing.md, "Pitfalls").
	Object* pyramid = marioState->floor->object;
	AdvanceFrameRead();
	Surface* floor = marioState->floor;
	if (!floor || floor->object != pyramid)
		return false; // the frame took Mario off the pyramid: nothing to read

	short floorAngle = atan2s(floor->normal.z, floor->normal.x);
	CustomStatus.isSlope = FloorIsSlope(floor->normal.y, marioState->action);
	// std::sqrt(float) overload, not std::sqrtf: libstdc++ 13 does not declare the f-suffixed
	// names in namespace std (docs/compilers.md). Same instruction, same result.
	CustomStatus.steepness = std::sqrt(floor->normal.x * floor->normal.x + floor->normal.z * floor->normal.z);

	// The game runs downhill where m->floorAngle - m->faceAngle[1] is within (-0x4000,
	// 0x4000): the facing nearest the target that is lies 0x3FFF from the floor's angle,
	// less DownhillMargin. The margin is for what this frame cannot see: the top face is
	// two triangles, and the game's rounding of their vertices sets their normals up to
	// about 15 angle units apart, so the triangle under Mario's step can put a facing 1
	// unit inside the half on this one 10 units outside it on the other; and the stick
	// the frame writes gives a facing a few units from the one asked for. At 0x3FFF
	// exactly, half of the ending's run frames at this angle were uphill, each the
	// slope's gain lost twice over (1.8 of speed), and the runs reached 33 where the
	// maintainer's example movie reaches 35 (2026-10-13). The cost of the margin is a
	// heading 64 units further downhill, a third of a degree.
	int32_t lowerAngle = floorAngle + 0x3FFF - DownhillMargin;
	int32_t upperAngle = floorAngle - 0x3FFF + DownhillMargin;

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
	if (abs(int32_t(facingDYaw)) <= 0x471C)
		CustomStatus.angleFacingAnalogBack = _faceAngle + 0x471D * CustomStatus.downhillRotation;
	else
		CustomStatus.angleFacingAnalogBack = CustomStatus.angleFacing;

	int16_t notFacingDYaw = _faceAngle - CustomStatus.angleNotFacing;
	if (abs(int32_t(notFacingDYaw)) <= 0x471C)
		CustomStatus.angleNotFacingAnalogBack = _faceAngle + 0x471D * CustomStatus.downhillRotation.Negate();
	else
		CustomStatus.angleNotFacingAnalogBack = CustomStatus.angleNotFacing;

	CustomStatus.floorAngle = floorAngle;

	return true;
}

bool GetMinimumDownhillWalkingAngle::assertion()
{
	return true;
}

// mario_floor_is_slope on the pyramid: its surfaces are of the default class (and BitFS is
// not a slide terrain), so the floor is a slope below cos 15 degrees, or below cos 20 when
// Mario crawls on a floor steeper than 60 degrees (mario_get_floor_class's one exception).
bool GetMinimumDownhillWalkingAngle::FloorIsSlope(float normalY, uint32_t action)
{
	const bool crawlingHolds = action == ACT_CRAWLING && normalY > 0.5f;
	return normalY <= (crawlingHolds ? 0.9396926f : 0.9659258f);
}
