#pragma once
#include <tasfw/Resource.hpp>
#include <sm64/Area.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Controller.hpp>
#include <sm64/SurfaceLoad.hpp>
#include <sm64/Types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifndef BITFSCORNERMEM_H
#define BITFSCORNERMEM_H

class Sm64Mem; // the SM64 resources' shared state, of which BitfsCornerMem is one alternative (Sm64Mem.hpp)

// The corner's level and savestate (BitfsCorner.hpp has the resource and the overview).

// Which of the corner's objects a run has (the maintainer, 2026-10-11): each of the four is
// optional and off unless asked for, so a state imported with the default has Mario alone,
// and a stage names what it needs. An object left out is not in the corner at all: its slot
// is empty, its surfaces are gone after the first frame, and a pointer the game had to it
// is null.
struct BitfsCornerObjects
{
	bool pyramid = false;	   // slot 84, the one the setup happens on
	bool otherPyramid = false; // slot 83, at x = -2866
	bool trackPlatform = false; // slot 85
	bool bully = false;		   // slot 27, the ending's

	bool Includes(int slot) const
	{
		return (slot == 84 && pyramid) || (slot == 83 && otherPyramid) || (slot == 85 && trackPlatform) || (slot == 27 && bully);
	}
	static constexpr BitfsCornerObjects All() { return {true, true, true, true}; }
};

// The level as data, read once from the running game at a frame inside it (the static
// surfaces with their partition in the game's own cell order, the corner's behavior scripts,
// the collision data of its surface objects, Mario's animations, the track platform's path)
// and shared, immutable, by every corner in the process; so a run loads the game once, not
// once per thread.
class BitfsCornerLevel
{
public:
	// The corner's objects beside Mario, by gObjectPool slot (BitFsObjects.hpp): the pyramid
	// the setup happens on, the other pyramid, the track platform and the ending's bully. The
	// level reads all four; a state takes the ones asked for (BitfsCornerObjects).
	static constexpr std::array<int, 4> ObjectSlots = {84, 83, 85, 27};
	// The behaviors they and Mario run, by export name.
	static constexpr std::array<const char*, 4> BehaviorNames = {
		"bhvMario", "bhvBitfsTiltingInvertedPyramid", "bhvPlatformOnTrack", "bhvSmallBully"};

	// A behavior script copied out of the game up to its loop's end: its words are the
	// game's, pointers included. The natives it calls are the game's addresses, in the
	// script's order; the frame knows each script's natives by name in that order (the
	// game does not export every one), so a CALL_NATIVE resolves by its position.
	struct Behavior
	{
		std::string name;
		const BehaviorScript* gameAddress = nullptr;
		std::vector<BehaviorScript> script;
		std::vector<const void*> natives;
	};

	// A surface object's collision data, the words load_object_collision_model reads.
	struct Collision
	{
		int slot = -1;
		std::vector<int16_t> data;
	};

	// Read from the game at a frame inside the level, through the resource's addr() alone.
	explicit BitfsCornerLevel(const Resource<Sm64Mem>& game);

	// The copy of a behavior, the identity an object's behavior pointer carries; null for
	// a name that is not one of the corner's.
	const BehaviorScript* Script(const char* name) const;
	const Collision* CollisionOf(int slot) const;
	// Mario's animation by id, as the game has it once loaded: its header with the value
	// and index pointers resolved into its own bytes (the game does that on each load into
	// its one buffer; the level holds every animation resolved, so a load is a pointer).
	const Animation* MarioAnimation(size_t id) const;

	int marioSlot = -1;
	int16_t levelNum = 0;
	int16_t courseNum = 0;
	int16_t areaIndex = 0;
	std::vector<Surface> staticSurfaces;
	std::vector<SurfaceNode> staticNodes;
	SpatialPartitionCell staticPartition[NUM_CELLS][NUM_CELLS] = {};
	std::vector<Behavior> behaviors;
	std::vector<Collision> collisions;
	std::vector<std::vector<uint8_t>> marioAnimations;
	std::vector<Waypoint> trackPlatformPath; // the track platform's, to its end marker

private:
	void ReadSurfaces(const Resource<Sm64Mem>& game);
	void ReadBehaviors(const Resource<Sm64Mem>& game);
	void ReadCollisions(const Resource<Sm64Mem>& game);
	void ReadAnimations(const Resource<Sm64Mem>& game);
	void ReadPath(const Resource<Sm64Mem>& game);
};

// A savestate of the corner, one alternative of the SM64 resources' shared state (Sm64Mem): a
// plain copy of the corner's state. Mario, the corner's objects (each optional, by the slots
// they have in the game), the camera, the controller, the timer and the RNG seed, the dynamic
// surfaces with their partition, the object lists and the frame. The game's structs hold
// pointers; a state records the addresses its pointers were valid at, and a load into another
// resource relocates them (the shared level needs none).
class BitfsCornerMem
{
public:
	static constexpr int ObjectCount = 1 + int(BitfsCornerLevel::ObjectSlots.size()); // Mario's slot first
	static constexpr int MaxDynamicSurfaces = 160;
	static constexpr int MaxDynamicNodes = 640;
	static constexpr int ObjectLists = 16;

	// The game's globals the corner keeps as state, under the decomp's names: the copied
	// game code reads them as members, through the class that derives from this
	// (BitfsCornerGame, private to the resource). What the resource keeps live, and what
	// the state's pointers point into, beside the pool.
	struct Globals
	{
		MarioState gMarioStates[1] = {};
		MarioState* gMarioState = nullptr; // &gMarioStates[0]
		Object* gMarioObject = nullptr;	   // the pool's Mario slot
		MarioBodyState gBodyStates[1] = {};
		PlayerCameraState gPlayerCameraState[1] = {};
		MarioAnimation gMarioAnimsBuf = {}; // its pointers into the level's animations
		Area gAreas[1] = {};
		Area* gCurrentArea = nullptr;
		Camera gCameras[1] = {};
		Camera* gCamera = nullptr;
		Controller gControllers[1] = {};
		Controller* gPlayer1Controller = nullptr;
		ObjectNode gObjectListArray[ObjectLists] = {};
		Object* gMarioPlatform = nullptr;
		uint32_t gGlobalTimer = 0;
		uint16_t gRandomSeed16 = 0;
		uint32_t gTimeStopState = 0;
		int16_t gAreaUpdateCounter = 0;
		int16_t gCameraMovementFlags = 0;
		// camera.c's statics the C-up mode is made of: the mode change in progress, the
		// camera stored at its start, and the pitch and yaw of Mario's look.
		ModeTransitionInfo sModeInfo = {};
		CameraStoredInfo sCameraStoreCUp = {};
		int16_t sCUpCameraPitch = 0;
		int16_t sModeOffsetYaw = 0;
		uint16_t D_8032FEC0 = 0;	   // platform_displacement.c's
		uint8_t sDelayInvincTimer = 0; // interaction.c's
		int16_t sInvulnerable = 0;
		// The game's surface pool past its static part, and the partition over it.
		Surface sDynamicSurfacePool[MaxDynamicSurfaces] = {};
		SurfaceNode sDynamicSurfaceNodePool[MaxDynamicNodes] = {};
		SpatialPartitionCell gDynamicSurfacePartition[NUM_CELLS][NUM_CELLS] = {};
		int32_t gSurfacesAllocated = 0; // the game's counts, the static ones included
		int32_t gSurfaceNodesAllocated = 0;
		int64_t frame = 0;
		Inputs inputs; // the next frame's
	};

	Globals globals;
	Object objects[ObjectCount] = {};
	int slots[ObjectCount] = {}; // the gObjectPool slot of each
	BitfsCornerObjects included;   // which of them the state has (an excluded one's entry is empty)

	// Where the pointers in this state are valid: the globals and the pool they were saved
	// from. A load elsewhere relocates them (BitfsCorner::load); a state imported from the
	// game has the ImportPoolBase sentinel for its pool, so every load relocates it.
	uintptr_t liveBase = 0;
	uintptr_t poolBase = 0;
	static constexpr uintptr_t ImportPoolBase = 0x10000;

	BitfsCornerMem() = default;
	// What a script on the game exports for a run on the corner: Mario, the objects asked
	// for, the camera, the controller, the timer, the seed and the dynamic surfaces as the
	// game has them, their pointers rewritten onto the level's copies and this state
	// (ExportSave<Sm64Mem>(level, objects), through Sm64Mem's converting constructor).
	BitfsCornerMem(const Resource<Sm64Mem>& game, const BitfsCornerLevel& level, BitfsCornerObjects objects = {});
};

#endif
