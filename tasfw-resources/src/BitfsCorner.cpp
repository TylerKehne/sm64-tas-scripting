#include "BitfsCorner.hpp"

#include <sm64/ObjectFields.hpp>
#include <sm64/SurfaceTerrains.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "bitfs-corner/BitfsCornerGame.hpp" // last: it carries the decomp's macros

namespace
{
	// A behavior script's END_LOOP and BREAK, BC_B(0x09) and BC_B(0x0A) of the decomp's
	// behavior_data.h: where a copied script ends.
	constexpr BehaviorScript BehaviorEndLoop = BehaviorScript(0x09) << 24;
	constexpr BehaviorScript BehaviorBreak = BehaviorScript(0x0A) << 24;
	constexpr int MaxBehaviorWords = 128;

	[[noreturn]] void Refuse(const std::string& what)
	{
		throw std::runtime_error("BitfsCorner: " + what);
	}

	template <class T>
	T Read(const Resource<Sm64Mem>& game, const char* symbol)
	{
		return *static_cast<const T*>(game.addr(symbol));
	}

	template <class T>
	T* Pointer(const Resource<Sm64Mem>& game, const char* symbol)
	{
		return static_cast<T*>(game.addr(symbol));
	}

	bool Exports(const Resource<Sm64Mem>& game, const char* symbol)
	{
		try
		{
			game.addr(symbol);
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	// A symbol a build may lack: its value, or the default.
	template <class T>
	T ReadOr(const Resource<Sm64Mem>& game, const char* symbol, T fallback)
	{
		try
		{
			return Read<T>(game, symbol);
		}
		catch (const std::exception&)
		{
			return fallback;
		}
	}

	// surface_has_force of the decomp's surface_load.c: the surface types whose triangles
	// carry a fourth word.
	bool SurfaceHasForce(int16_t surfaceType)
	{
		switch (surfaceType)
		{
		case SURFACE_0004:
		case SURFACE_FLOWING_WATER:
		case SURFACE_DEEP_MOVING_QUICKSAND:
		case SURFACE_SHALLOW_MOVING_QUICKSAND:
		case SURFACE_MOVING_QUICKSAND:
		case SURFACE_HORIZONTAL_WIND:
		case SURFACE_INSTANT_MOVING_QUICKSAND:
			return true;
		default:
			return false;
		}
	}

	uintptr_t Address(const void* p) { return reinterpret_cast<uintptr_t>(p); }

	// The sentinel address of a pool slot's object in a state imported from the game
	// (BitfsCornerMem::ImportPoolBase), which every load relocates onto its own pool.
	Object* ImportSlot(int slot)
	{
		return reinterpret_cast<Object*>(BitfsCornerMem::ImportPoolBase + uintptr_t(slot) * sizeof(Object));
	}
}

// --- BitfsCornerLevel -------------------------------------------------------------------------

BitfsCornerLevel::BitfsCornerLevel(const Resource<Sm64Mem>& game)
{
	const Object* pool = Pointer<Object>(game, "gObjectPool");
	const Object* mario = Read<Object*>(game, "gMarioObject");
	if (mario == nullptr || mario < pool || mario >= pool + LibSm64ObjectPoolCapacity)
		Refuse("gMarioObject is not a slot of gObjectPool; the game is not inside a level");
	marioSlot = int(mario - pool);
	levelNum = Read<int16_t>(game, "gCurrLevelNum");
	courseNum = Read<int16_t>(game, "gCurrCourseNum");
	areaIndex = Read<int16_t>(game, "gCurrAreaIndex");
	ReadSurfaces(game);
	ReadBehaviors(game);
	ReadCollisions(game);
	ReadAnimations(game);
	ReadPath(game);
}

// The track platform's path, from the waypoint its object starts at to the end marker: what
// bhv_platform_on_track_update follows when Mario rides it.
void BitfsCornerLevel::ReadPath(const Resource<Sm64Mem>& game)
{
	const Object* pool = Pointer<Object>(game, "gObjectPool");
	const Waypoint* start = pool[85].oPlatformOnTrackStartWaypoint;
	if (start == nullptr)
		Refuse("the track platform has no path");
	for (size_t i = 0;; i++)
	{
		if (i >= 256)
			Refuse("the track platform's path has no end within 256 waypoints");
		trackPlatformPath.push_back(start[i]);
		if (start[i].flags == -1) // WAYPOINT_FLAGS_END
			break;
	}
}

const BehaviorScript* BitfsCornerLevel::Script(const char* name) const
{
	for (const Behavior& behavior : behaviors)
		if (behavior.name == name)
			return behavior.script.data();
	return nullptr;
}

const BitfsCornerLevel::Collision* BitfsCornerLevel::CollisionOf(int slot) const
{
	for (const Collision& collision : collisions)
		if (collision.slot == slot)
			return &collision;
	return nullptr;
}

const Animation* BitfsCornerLevel::MarioAnimation(size_t id) const
{
	if (id >= marioAnimations.size())
		Refuse("Mario's animation " + std::to_string(id) + " is beyond the " + std::to_string(marioAnimations.size()) + " the game has");
	return reinterpret_cast<const Animation*>(marioAnimations[id].data());
}

// The static surfaces and their partition, node for node in the game's order: a tie between
// floors goes to the first in a cell's list, so the order is part of the physics.
void BitfsCornerLevel::ReadSurfaces(const Resource<Sm64Mem>& game)
{
	const Surface* surfacePool = Read<Surface*>(game, "sSurfacePool");
	const SurfaceNode* nodePool = Read<SurfaceNode*>(game, "sSurfaceNodePool");
	int32_t numSurfaces = Read<int32_t>(game, "gNumStaticSurfaces");
	int32_t numNodes = Read<int32_t>(game, "gNumStaticSurfaceNodes");
	if (surfacePool == nullptr || nodePool == nullptr || numSurfaces <= 0 || numNodes <= 0)
		Refuse("the game has no static surfaces loaded");

	staticSurfaces.assign(surfacePool, surfacePool + numSurfaces);
	for (Surface& surface : staticSurfaces)
		if (surface.object != nullptr)
			Refuse("a static surface belongs to an object");

	auto node = [&](const SurfaceNode* p) -> SurfaceNode*
	{
		if (p == nullptr)
			return nullptr;
		std::ptrdiff_t i = p - nodePool;
		if (i < 0 || i >= numNodes)
			Refuse("a static partition list reaches a node outside the static nodes");
		return &staticNodes[size_t(i)];
	};
	auto surface = [&](const Surface* p) -> Surface*
	{
		std::ptrdiff_t i = p - surfacePool;
		if (p == nullptr || i < 0 || i >= numSurfaces)
			Refuse("a static surface node points outside the static surfaces");
		return &staticSurfaces[size_t(i)];
	};
	staticNodes.resize(size_t(numNodes));
	for (int32_t i = 0; i < numNodes; i++)
	{
		staticNodes[size_t(i)].next = node(nodePool[i].next);
		staticNodes[size_t(i)].surface = surface(nodePool[i].surface);
	}
	const SpatialPartitionCell* cells = Pointer<SpatialPartitionCell>(game, "gStaticSurfacePartition");
	for (int z = 0; z < NUM_CELLS; z++)
		for (int x = 0; x < NUM_CELLS; x++)
			for (int list = 0; list < 3; list++)
			{
				staticPartition[z][x][list].next = node(cells[z * NUM_CELLS + x][list].next);
				staticPartition[z][x][list].surface = nullptr;
			}
}

namespace
{
	// How many words each behavior command takes, 0x00 to 0x37, from what the decomp's
	// behavior_script.c advances gCurBhvCommand by in each bhv_cmd_* (a jump's own length
	// where it jumps); what a script's walk steps by.
	constexpr uint8_t BehaviorCommandWords[0x38] = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, // 0x00-0x0F: BEGIN .. ADD_INT; CALL_NATIVE is 0x0C
		1, 1, 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 3, 1, 1, 1, // 0x10-0x1F
		1, 1, 1, 2, 1, 1, 1, 2, 1, 3, 2, 3, 3, 1, 2, 2, // 0x20-0x2F
		5, 2, 1, 2, 1, 1, 2, 2,							// 0x30-0x37
	};
	constexpr uint8_t BehaviorCallNative = 0x0C;
}

// Each behavior's words from its export to its loop's END_LOOP (or BREAK), the game's own
// bytes, walked command by command for the natives it calls, in order.
void BitfsCornerLevel::ReadBehaviors(const Resource<Sm64Mem>& game)
{
	for (const char* name : BehaviorNames)
	{
		Behavior behavior;
		behavior.name = name;
		behavior.gameAddress = static_cast<const BehaviorScript*>(game.addr(name));
		for (int i = 0;;)
		{
			if (i >= MaxBehaviorWords)
				Refuse(std::string(name) + " has no END_LOOP within " + std::to_string(MaxBehaviorWords) + " words");
			BehaviorScript word = behavior.gameAddress[i];
			uint8_t command = uint8_t(word >> 24);
			if (word >> 32 != 0 || command >= std::size(BehaviorCommandWords))
				Refuse(std::string(name) + " has a word that is not a behavior command at " + std::to_string(i));
			int words = BehaviorCommandWords[command];
			for (int k = 0; k < words; k++)
				behavior.script.push_back(behavior.gameAddress[i + k]);
			if (command == BehaviorCallNative)
				behavior.natives.push_back(reinterpret_cast<const void*>(behavior.gameAddress[i + 1]));
			i += words;
			if (word == BehaviorEndLoop || word == BehaviorBreak)
				break;
		}
		behaviors.push_back(std::move(behavior));
	}
}

// Each surface object's collision data, parsed the way load_object_collision_model reads it
// (the vertices, then the triangles by surface type, to TERRAIN_LOAD_CONTINUE inclusive).
void BitfsCornerLevel::ReadCollisions(const Resource<Sm64Mem>& game)
{
	const Object* pool = Pointer<Object>(game, "gObjectPool");
	for (int slot : ObjectSlots)
	{
		const int16_t* data = static_cast<const int16_t*>(pool[slot].collisionData);
		if (data == nullptr)
			continue;
		if (data[0] != TERRAIN_LOAD_VERTICES)
			Refuse("slot " + std::to_string(slot) + "'s collision data does not begin with its vertices");
		size_t i = 1;
		int16_t vertices = data[i++];
		i += size_t(3) * size_t(vertices);
		for (;;)
		{
			int16_t type = data[i];
			if (type == TERRAIN_LOAD_CONTINUE)
			{
				i++;
				break;
			}
			int16_t count = data[i + 1];
			i += 2 + size_t(count) * (SurfaceHasForce(type) ? 4 : 3);
			if (i > 4096)
				Refuse("slot " + std::to_string(slot) + "'s collision data does not end");
		}
		Collision collision;
		collision.slot = slot;
		collision.data.assign(data, data + i);
		collisions.push_back(std::move(collision));
	}
}

// Mario's animations, every one, from the table the game loads them through (gMarioAnimsBuf's
// DMA table: a count, the blob they are cut from, and each one's offset and size in it), each
// resolved the way set_mario_animation resolves the one it loads.
void BitfsCornerLevel::ReadAnimations(const Resource<Sm64Mem>& game)
{
	const MarioState* mario = Read<MarioState*>(game, "gMarioState");
	if (mario == nullptr || mario->animation == nullptr || mario->animation->animDmaTable == nullptr)
		Refuse("the game has no animation table for Mario");
	const MarioAnimDmaRelatedThing* table = mario->animation->animDmaTable;
	if (table->count == 0 || table->count > 1024 || table->srcAddr == nullptr)
		Refuse("Mario's animation table is empty or absurd");
	marioAnimations.resize(table->count);
	for (uint32_t i = 0; i < table->count; i++)
	{
		const uint8_t* bytes = table->srcAddr + table->anim[i].offset;
		uint32_t size = table->anim[i].size;
		if (size < sizeof(Animation))
			Refuse("Mario's animation " + std::to_string(i) + " is shorter than its header");
		marioAnimations[i].assign(bytes, bytes + size);
		Animation* animation = reinterpret_cast<Animation*>(marioAnimations[i].data());
		animation->values = reinterpret_cast<const s16*>(reinterpret_cast<uint8_t*>(animation) + reinterpret_cast<uintptr_t>(animation->values));
		animation->index = reinterpret_cast<const u16*>(reinterpret_cast<uint8_t*>(animation) + reinterpret_cast<uintptr_t>(animation->index));
	}
}

// --- BitfsCornerMem ---------------------------------------------------------------------------

BitfsCornerMem::BitfsCornerMem(const Resource<Sm64Mem>& game, const BitfsCornerLevel& level, BitfsCornerObjects wanted)
{
	included = wanted;
	const Object* gamePool = Pointer<Object>(game, "gObjectPool");
	const MarioState* gameMario = Read<MarioState*>(game, "gMarioState");
	const Camera* gameCamera = Read<Camera*>(game, "gCamera");
	const Surface* surfacePool = Read<Surface*>(game, "sSurfacePool");
	const SurfaceNode* nodePool = Read<SurfaceNode*>(game, "sSurfaceNodePool");
	int32_t numStatic = Read<int32_t>(game, "gNumStaticSurfaces");
	int32_t numStaticNodes = Read<int32_t>(game, "gNumStaticSurfaceNodes");
	int32_t allocated = Read<int32_t>(game, "gSurfacesAllocated");
	int32_t allocatedNodes = Read<int32_t>(game, "gSurfaceNodesAllocated");
	if (gameMario == nullptr || gameCamera == nullptr)
		Refuse("the game has no Mario or camera; it is not inside a level");
	if (int32_t(level.staticSurfaces.size()) != numStatic)
		Refuse("the level was read from a game with a different number of static surfaces");
	if (gameMario->area == nullptr || gameMario->marioBodyState == nullptr || gameMario->statusForCamera == nullptr || gameMario->animation == nullptr)
		Refuse("Mario's area, body state, camera state or animation is not set");

	liveBase = Address(&globals);
	poolBase = ImportPoolBase;
	slots[0] = level.marioSlot;
	for (size_t k = 0; k < BitfsCornerLevel::ObjectSlots.size(); k++)
		slots[1 + k] = BitfsCornerLevel::ObjectSlots[k];

	// A pointer into the game's pool as the sentinel pointer to the same slot, null for an
	// object the corner has not got or this state leaves out.
	auto object = [&](const Object* p) -> Object*
	{
		if (p == nullptr)
			return nullptr;
		std::ptrdiff_t slot = p - gamePool;
		if (slot == level.marioSlot || (slot >= 0 && slot < LibSm64ObjectPoolCapacity && included.Includes(int(slot))))
			return ImportSlot(int(slot));
		return nullptr;
	};
	auto surface = [&](const Surface* p) -> Surface*
	{
		if (p == nullptr)
			return nullptr;
		std::ptrdiff_t i = p - surfacePool;
		if (i < 0 || i >= allocated)
			Refuse("a surface pointer lies outside the game's surface pool");
		if (i < numStatic)
			return const_cast<Surface*>(&level.staticSurfaces[size_t(i)]);
		i -= numStatic;
		if (i >= MaxDynamicSurfaces)
			Refuse("the game has more dynamic surfaces than a state holds");
		return &globals.sDynamicSurfacePool[i];
	};
	auto node = [&](const SurfaceNode* p) -> SurfaceNode*
	{
		if (p == nullptr)
			return nullptr;
		std::ptrdiff_t i = p - nodePool;
		if (i < numStaticNodes || i >= allocatedNodes)
			Refuse("a dynamic partition list reaches a node outside the dynamic nodes");
		i -= numStaticNodes;
		if (i >= MaxDynamicNodes)
			Refuse("the game has more dynamic surface nodes than a state holds");
		return &globals.sDynamicSurfaceNodePool[i];
	};
	// Mario's animation as the level holds it, by the id his object carries.
	auto marioAnimation = [&](const Animation* p) -> Animation*
	{
		if (p == nullptr)
			return nullptr;
		int id = gameMario->marioObj->header.gfx.animInfo.animID;
		if (id < 0)
			Refuse("Mario has an animation but no animation id");
		return const_cast<Animation*>(level.MarioAnimation(size_t(id)));
	};

	// Mario: the game's struct with its surfaces and objects re-pointed, his body state,
	// camera state, area and animation as the state's own copies.
	MarioState& m = globals.gMarioStates[0];
	m = *gameMario;
	m.wall = surface(m.wall);
	m.ceil = surface(m.ceil);
	m.floor = surface(m.floor);
	m.interactObj = object(m.interactObj);
	m.heldObj = object(m.heldObj);
	m.usedObj = object(m.usedObj);
	m.riddenObj = object(m.riddenObj);
	m.marioObj = object(m.marioObj);
	m.spawnInfo = nullptr;
	m.area = &globals.gAreas[0];
	m.statusForCamera = &globals.gPlayerCameraState[0];
	m.marioBodyState = &globals.gBodyStates[0];
	m.controller = &globals.gControllers[0];
	m.animation = &globals.gMarioAnimsBuf;
	globals.gMarioState = &globals.gMarioStates[0];
	globals.gMarioObject = m.marioObj;
	globals.gBodyStates[0] = *gameMario->marioBodyState;
	globals.gPlayerCameraState[0] = *gameMario->statusForCamera;
	globals.gPlayerCameraState[0].usedObj = object(globals.gPlayerCameraState[0].usedObj);
	globals.gAreas[0] = *gameMario->area;
	globals.gAreas[0].unk04 = nullptr;
	globals.gAreas[0].terrainData = nullptr;
	globals.gAreas[0].surfaceRooms = nullptr;
	globals.gAreas[0].macroObjects = nullptr;
	globals.gAreas[0].warpNodes = nullptr;
	globals.gAreas[0].paintingWarpNodes = nullptr;
	globals.gAreas[0].instantWarps = nullptr;
	globals.gAreas[0].objectSpawnInfos = nullptr;
	globals.gAreas[0].camera = &globals.gCameras[0];
	globals.gAreas[0].unused28 = nullptr;
	globals.gAreas[0].whirlpools[0] = nullptr;
	globals.gAreas[0].whirlpools[1] = nullptr;
	globals.gCurrentArea = &globals.gAreas[0];
	globals.gCameras[0] = *gameCamera;
	globals.gCamera = &globals.gCameras[0];
	globals.gMarioAnimsBuf = *gameMario->animation;
	globals.gMarioAnimsBuf.animDmaTable = nullptr;
	globals.gMarioAnimsBuf.targetAnim = marioAnimation(gameMario->animation->targetAnim);
	globals.gMarioAnimsBuf.currentAnimAddr = reinterpret_cast<u8*>(globals.gMarioAnimsBuf.targetAnim);

	// The objects: their behavior and collision data onto the level's copies, their object
	// pointers onto the sentinels, their graph node's links dropped (the corner has no
	// scene graph; the node's flags stay). Their ptrData is copied as it is: an entry that
	// is a pool pointer is re-pointed, the rest (an animation list, say) is the game's
	// address, which the frame's port replaces as it comes to need each one.
	for (int j = 0; j < ObjectCount; j++)
	{
		const Object& from = gamePool[slots[j]];
		Object& o = objects[j];
		if (j > 0 && !included.Includes(slots[j]))
		{
			o = Object{}; // left out: an empty slot
			continue;
		}
		o = from;
		o.header.gfx.node.prev = nullptr;
		o.header.gfx.node.next = nullptr;
		o.header.gfx.node.parent = nullptr;
		o.header.gfx.node.children = nullptr;
		o.header.gfx.sharedChild = nullptr;
		o.header.gfx.unk4C = nullptr;
		o.header.gfx.throwMatrix = from.header.gfx.throwMatrix == &from.transform
			? reinterpret_cast<Mat4*>(Address(ImportSlot(slots[j])) + offsetof(Object, transform))
			: nullptr;
		if (j == 0)
			o.header.gfx.animInfo.curAnim = marioAnimation(o.header.gfx.animInfo.curAnim);
		o.header.next = nullptr;
		o.header.prev = nullptr;
		o.parentObj = object(o.parentObj);
		o.prevObj = object(o.prevObj);
		for (Object*& collided : o.collidedObjs)
			collided = object(collided);
		for (void*& p : o.ptrData.asVoidPtr)
		{
			const Object* asObject = static_cast<const Object*>(p);
			if (asObject >= gamePool && asObject < gamePool + LibSm64ObjectPoolCapacity)
				p = object(asObject);
		}
		o.platform = object(o.platform);
		o.respawnInfo = nullptr;
		if (const BitfsCornerLevel::Collision* collision = level.CollisionOf(slots[j]))
			o.collisionData = const_cast<int16_t*>(collision->data.data());
		else if (o.collisionData != nullptr)
			Refuse("slot " + std::to_string(slots[j]) + " has collision data the level did not read");
		const BitfsCornerLevel::Behavior* behavior = nullptr;
		for (const BitfsCornerLevel::Behavior& candidate : level.behaviors)
			if (candidate.gameAddress == from.behavior)
				behavior = &candidate;
		if (behavior == nullptr)
			Refuse("slot " + std::to_string(slots[j]) + " runs a behavior the corner has not got");
		o.behavior = behavior->script.data();
		std::ptrdiff_t at = from.curBhvCommand - from.behavior;
		if (at < 0 || at >= std::ptrdiff_t(behavior->script.size()))
			Refuse("slot " + std::to_string(slots[j]) + " is outside the copied part of " + behavior->name);
		o.curBhvCommand = behavior->script.data() + at;
		// The behavior stack holds the addresses the loop returns to (END_LOOP pops one).
		for (u32 k = 0; k < o.bhvStackIndex && k < std::size(o.bhvStack); k++)
		{
			std::ptrdiff_t entry = reinterpret_cast<const BehaviorScript*>(o.bhvStack[k]) - from.behavior;
			if (entry < 0 || entry >= std::ptrdiff_t(behavior->script.size()))
				Refuse("slot " + std::to_string(slots[j]) + "'s behavior stack points outside the copied part of " + behavior->name);
			o.bhvStack[k] = reinterpret_cast<uintptr_t>(behavior->script.data() + entry);
		}
		// The track platform's waypoints point into the game's path data: onto the level's copy.
		if (slots[j] == 85)
		{
			const Waypoint* gameStart = from.oPlatformOnTrackStartWaypoint;
			const Waypoint* gamePrev = from.oPlatformOnTrackPrevWaypoint;
			if (gameStart == nullptr || level.trackPlatformPath.empty())
				Refuse("the track platform has no path to follow");
			Waypoint* start = const_cast<Waypoint*>(level.trackPlatformPath.data());
			o.oPlatformOnTrackStartWaypoint = start;
			if (gamePrev != nullptr)
			{
				std::ptrdiff_t at = gamePrev - gameStart;
				if (at < 0 || at >= std::ptrdiff_t(level.trackPlatformPath.size()))
					Refuse("the track platform's previous waypoint is outside its path");
				o.oPlatformOnTrackPrevWaypoint = start + at;
			}
		}
	}

	// The object lists: the corner's objects in the order the game's lists have them, linked
	// through the sentinels (an object's header is its first member, so a slot's sentinel is
	// its node's) and the heads in this state.
	const ObjectNode* gameLists = Read<ObjectNode*>(game, "gObjectLists");
	for (int list = 0; list < ObjectLists; list++)
	{
		ObjectNode& head = globals.gObjectListArray[list];
		int order[ObjectCount];
		int count = 0;
		// The game initializes NUM_OBJ_LISTS (13) of its 16 heads; the rest are null.
		for (const ObjectNode* n = gameLists[list].next; n != nullptr && n != &gameLists[list]; n = n->next)
		{
			std::ptrdiff_t slot = reinterpret_cast<const Object*>(n) - gamePool;
			for (int j = 0; j < ObjectCount; j++)
				if (slot == slots[j] && (j == 0 || included.Includes(slots[j])))
					order[count++] = j;
		}
		auto sentinel = [&](int k) { return reinterpret_cast<ObjectNode*>(ImportSlot(slots[order[k]])); };
		head.next = count > 0 ? sentinel(0) : &head;
		head.prev = count > 0 ? sentinel(count - 1) : &head;
		for (int k = 0; k < count; k++)
		{
			objects[order[k]].header.prev = k == 0 ? &head : sentinel(k - 1);
			objects[order[k]].header.next = k + 1 == count ? &head : sentinel(k + 1);
		}
	}

	// The dynamic surfaces and their partition as the game has them this frame, under the
	// game's own counts (the static part included).
	int32_t dynamic = allocated - numStatic;
	int32_t dynamicNodes = allocatedNodes - numStaticNodes;
	if (dynamic < 0 || dynamic > MaxDynamicSurfaces || dynamicNodes < 0 || dynamicNodes > MaxDynamicNodes)
		Refuse("the game has more dynamic surfaces or nodes than a state holds");
	globals.gSurfacesAllocated = allocated;
	globals.gSurfaceNodesAllocated = allocatedNodes;
	for (int32_t i = 0; i < dynamic; i++)
	{
		globals.sDynamicSurfacePool[i] = surfacePool[numStatic + i];
		globals.sDynamicSurfacePool[i].object = object(globals.sDynamicSurfacePool[i].object);
	}
	for (int32_t i = 0; i < dynamicNodes; i++)
	{
		globals.sDynamicSurfaceNodePool[i].next = node(nodePool[numStaticNodes + i].next);
		globals.sDynamicSurfaceNodePool[i].surface = surface(nodePool[numStaticNodes + i].surface);
	}
	const SpatialPartitionCell* cells = Pointer<SpatialPartitionCell>(game, "gDynamicSurfacePartition");
	for (int z = 0; z < NUM_CELLS; z++)
		for (int x = 0; x < NUM_CELLS; x++)
			for (int list = 0; list < 3; list++)
			{
				globals.gDynamicSurfacePartition[z][x][list].next = node(cells[z * NUM_CELLS + x][list].next);
				globals.gDynamicSurfacePartition[z][x][list].surface = nullptr;
			}

	// The controller, the timer, the seed, the time stop, the counters and the frame.
	globals.gControllers[0] = Pointer<Controller>(game, "gControllers")[0];
	globals.gControllers[0].statusData = nullptr;
	globals.gControllers[0].controllerData = nullptr;
	globals.gPlayer1Controller = &globals.gControllers[0];
	globals.gMarioPlatform = object(ReadOr<Object*>(game, "gMarioPlatform", nullptr));
	globals.gGlobalTimer = Read<uint32_t>(game, "gGlobalTimer");
	globals.gRandomSeed16 = ReadOr<uint16_t>(game, "gRandomSeed16", 0);
	globals.gTimeStopState = Read<uint32_t>(game, "gTimeStopState");
	globals.gAreaUpdateCounter = Read<int16_t>(game, "gAreaUpdateCounter");
	globals.gCameraMovementFlags = ReadOr<int16_t>(game, "gCameraMovementFlags", 0);
	// camera.c's statics the C-up mode is made of. Outside that mode the mode's entry sets
	// them, so a build that does not export them is fine until an import inside it.
	globals.sModeInfo = ReadOr<ModeTransitionInfo>(game, "sModeInfo", {});
	globals.sCameraStoreCUp = ReadOr<CameraStoredInfo>(game, "sCameraStoreCUp", {});
	globals.sCUpCameraPitch = ReadOr<int16_t>(game, "sCUpCameraPitch", 0);
	globals.sModeOffsetYaw = ReadOr<int16_t>(game, "sModeOffsetYaw", 0);
	bool inCUp = globals.gCameras[0].mode == CAMERA_MODE_C_UP || (globals.gCameraMovementFlags & CAM_MOVE_C_UP_MODE) != 0;
	if (inCUp && !Exports(game, "sModeInfo"))
		Refuse("an import inside the camera's C-up mode from a build that does not export camera.c's statics");
	globals.frame = game.getCurrentFrame();
	globals.inputs = Inputs();
}

// --- BitfsCornerGame --------------------------------------------------------------------------

BitfsCornerGame::BitfsCornerGame(const BitfsCornerLevel& level) : level(level)
{
	gMarioState = &gMarioStates[0];
	gMarioObject = &gObjectPool[level.marioSlot];
	gCamera = &gCameras[0];
	gCurrentArea = &gAreas[0];
	gPlayer1Controller = &gControllers[0];
	gMarioStates[0].controller = gPlayer1Controller;
	sMarioCamState = &gPlayerCameraState[0];
	gCurrLevelArea = level.levelNum * 16 + level.areaIndex;
	for (ObjectNode& head : gObjectListArray)
	{
		head.next = &head;
		head.prev = &head;
	}
	gObjectLists = gObjectListArray;
	gNumStaticSurfaces = s32(level.staticSurfaces.size());
	gNumStaticSurfaceNodes = s32(level.staticNodes.size());
	gStaticSurfacePartition = const_cast<SpatialPartitionCell(*)[NUM_CELLS]>(level.staticPartition);
	bhvSmallBully = level.Script("bhvSmallBully");
	gCurrLevelNum = level.levelNum;
	gCurrCourseNum = level.courseNum;
	gCurrAreaIndex = level.areaIndex;
}

// One frame of the game for the corner, in the game's order (game_init.c's loop,
// level_update.c's play_mode_normal, rendering_graph_node.c's pass, display_and_vsync):
// the controller read, the level's objects updated, the camera's mode step (the
// 8-direction mode held at its constant yaw, with its C-up input; the C-up mode whole;
// Lakitu's flight not modelled), the geo pass's one effect on state (an animation's frame
// advances and the throw matrix is dropped, for every object it draws), and the global
// timer.
void BitfsCornerGame::Frame()
{
	// read_controller_inputs for player 1: the raw stick, the buttons and the press edge.
	Controller* controller = gPlayer1Controller;
	controller->rawStickX = inputs.stick_x;
	controller->rawStickY = inputs.stick_y;
	controller->buttonPressed = inputs.buttons & (inputs.buttons ^ controller->buttonDown);
	controller->buttonDown = inputs.buttons;
	adjust_analog_stick(controller);

	area_update_objects();

	// update_camera's mode step, for the two modes the corner has. In the 8-direction mode
	// only the C-button input of mode_8_directions_camera runs (radial_camera_input: C-up
	// enters the C-up mode), after C-left, C-right and C-down, which would turn or zoom
	// the camera, are refused; the C-up mode runs whole (mode_c_up_camera: the zoom in,
	// Mario's head on the stick, the exit on A, B or another C button). Any other mode
	// is held as it is, with C-up refused in it.
	Camera* camera = gCamera;
	if (camera->mode == CAMERA_MODE_C_UP)
		mode_c_up_camera(camera);
	else if (controller->buttonPressed & (D_CBUTTONS | L_CBUTTONS | R_CBUTTONS))
		Refuse("a C button other than C-up (the camera would turn or zoom)");
	else if (camera->mode == CAMERA_MODE_8_DIRECTIONS)
		radial_camera_input(camera, 0.0f);
	else if (controller->buttonPressed & U_CBUTTONS)
		Refuse(("C-up in camera mode " + std::to_string(camera->mode) + " (the corner has the 8-direction mode's)").c_str());

	for (Object& object : gObjectPool)
	{
		if (object.activeFlags == 0 || !(object.header.gfx.node.flags & GRAPH_RENDER_ACTIVE))
			continue;
		if (object.header.gfx.animInfo.curAnim != nullptr)
			geo_set_animation_globals(&object.header.gfx.animInfo, (object.header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION) != 0);
		object.header.gfx.throwMatrix = nullptr;
	}

	gGlobalTimer++;
	frame++;
}

// --- BitfsCorner ------------------------------------------------------------------------------

BitfsCorner::BitfsCorner(std::shared_ptr<const BitfsCornerLevel> level)
	: Sm64Resource(SavestateBudgetBytes), _level(std::move(level))
{
	if (_level == nullptr)
		Refuse("a resource needs the level");
	_game = std::make_unique<BitfsCornerGame>(*_level);
	_gCurrLevelNum = _level->levelNum;
	_gCurrCourseNum = _level->courseNum;
	_gCurrAreaIndex = _level->areaIndex;
}

BitfsCorner::~BitfsCorner() = default;
BitfsCorner::BitfsCorner(BitfsCorner&&) noexcept = default;

void BitfsCorner::save(Sm64Mem& state) const
{
	SaveCorner(state.Corner());
}

void BitfsCorner::load(const Sm64Mem& state)
{
	LoadCorner(state.Corner());
}

void BitfsCorner::SaveCorner(BitfsCornerMem& state) const
{
	state.globals = static_cast<const BitfsCornerMem::Globals&>(*_game);
	state.slots[0] = _level->marioSlot;
	for (size_t k = 0; k < BitfsCornerLevel::ObjectSlots.size(); k++)
		state.slots[1 + k] = BitfsCornerLevel::ObjectSlots[k];
	for (int j = 0; j < BitfsCornerMem::ObjectCount; j++)
		state.objects[j] = _game->gObjectPool[state.slots[j]];
	state.liveBase = Address(static_cast<const BitfsCornerMem::Globals*>(_game.get()));
	state.poolBase = Address(_game->gObjectPool);
}

void BitfsCorner::LoadCorner(const BitfsCornerMem& state)
{
	static_cast<BitfsCornerMem::Globals&>(*_game) = state.globals;
	for (int j = 0; j < BitfsCornerMem::ObjectCount; j++)
		_game->gObjectPool[state.slots[j]] = state.objects[j];
	if (state.liveBase == Address(static_cast<const BitfsCornerMem::Globals*>(_game.get())) && state.poolBase == Address(_game->gObjectPool))
		return;
	// A state saved elsewhere (another resource, or the game through the import): its
	// pointers move from the ranges they were valid in to this resource's.
	RelocateGlobals(state);
	for (int j = 0; j < BitfsCornerMem::ObjectCount; j++)
		RelocateObject(state, _game->gObjectPool[state.slots[j]]);
}

void BitfsCorner::Relocate(const BitfsCornerMem& from, void** pointer) const
{
	uintptr_t v = Address(*pointer);
	if (v == 0)
		return;
	if (v >= from.liveBase && v < from.liveBase + sizeof(BitfsCornerMem::Globals))
		*pointer = reinterpret_cast<void*>(v - from.liveBase + Address(static_cast<const BitfsCornerMem::Globals*>(_game.get())));
	else if (v >= from.poolBase && v < from.poolBase + PoolBytes)
		*pointer = reinterpret_cast<void*>(v - from.poolBase + Address(_game->gObjectPool));
}

void BitfsCorner::RelocateGlobals(const BitfsCornerMem& from)
{
	BitfsCornerGame& g = *_game;
	MarioState& m = g.gMarioStates[0];
	Relocate(from, m.wall);
	Relocate(from, m.ceil);
	Relocate(from, m.floor);
	Relocate(from, m.interactObj);
	Relocate(from, m.heldObj);
	Relocate(from, m.usedObj);
	Relocate(from, m.riddenObj);
	Relocate(from, m.marioObj);
	Relocate(from, m.spawnInfo);
	Relocate(from, m.area);
	Relocate(from, m.statusForCamera);
	Relocate(from, m.marioBodyState);
	Relocate(from, m.controller);
	Relocate(from, m.animation);
	Relocate(from, g.gMarioState);
	Relocate(from, g.gMarioObject);
	Relocate(from, g.gPlayerCameraState[0].usedObj);
	Relocate(from, g.gAreas[0].camera);
	Relocate(from, g.gCurrentArea);
	Relocate(from, g.gCamera);
	Relocate(from, g.gPlayer1Controller);
	Relocate(from, g.gMarioPlatform);
	for (ObjectNode& head : g.gObjectListArray)
	{
		Relocate(from, head.next);
		Relocate(from, head.prev);
	}
	for (int32_t i = 0; i < g.gSurfacesAllocated - g.gNumStaticSurfaces; i++)
		Relocate(from, g.sDynamicSurfacePool[i].object);
	for (int32_t i = 0; i < g.gSurfaceNodesAllocated - g.gNumStaticSurfaceNodes; i++)
	{
		Relocate(from, g.sDynamicSurfaceNodePool[i].next);
		Relocate(from, g.sDynamicSurfaceNodePool[i].surface);
	}
	for (auto& row : g.gDynamicSurfacePartition)
		for (auto& cell : row)
			for (SurfaceNode& head : cell)
				Relocate(from, head.next);
}

void BitfsCorner::RelocateObject(const BitfsCornerMem& from, Object& o) const
{
	Relocate(from, o.header.gfx.node.prev);
	Relocate(from, o.header.gfx.node.next);
	Relocate(from, o.header.gfx.node.parent);
	Relocate(from, o.header.gfx.node.children);
	Relocate(from, o.header.gfx.sharedChild);
	Relocate(from, o.header.gfx.unk4C);
	Relocate(from, o.header.gfx.throwMatrix);
	Relocate(from, o.header.next);
	Relocate(from, o.header.prev);
	Relocate(from, o.parentObj);
	Relocate(from, o.prevObj);
	for (Object*& collided : o.collidedObjs)
		Relocate(from, collided);
	for (void*& p : o.ptrData.asVoidPtr)
		Relocate(from, &p);
	Relocate(from, o.platform);
}

void BitfsCorner::advance()
{
	_game->Frame();
}

void BitfsCorner::setInputs(const Inputs& inputs)
{
	_game->inputs = inputs;
}

void* BitfsCorner::addr(const char* symbol) const
{
	// The symbols the scripts read, served from the live game; the pointer-to-pointer
	// symbols (gMarioState, gMarioObject, gCamera) as the game has them.
	BitfsCornerGame& g = *_game;
	BitfsCorner* self = const_cast<BitfsCorner*>(this);
	std::string_view name(symbol); // never a std::string: the scripts ask at the top of every execution(), and a behavior's name outgrows the small buffer
	if (name == "gMarioStates")
		return g.gMarioStates;
	if (name == "gMarioState")
		return &g.gMarioState;
	if (name == "gMarioObject")
		return &g.gMarioObject;
	if (name == "gObjectPool")
		return g.gObjectPool;
	if (name == "gCamera")
		return &g.gCamera;
	if (name == "gControllers")
		return g.gControllers;
	if (name == "gGlobalTimer")
		return &g.gGlobalTimer;
	if (name == "gRandomSeed16")
		return &g.gRandomSeed16;
	if (name == "gCurrLevelNum")
		return &self->_gCurrLevelNum;
	if (name == "gCurrCourseNum")
		return &self->_gCurrCourseNum;
	if (name == "gCurrAreaIndex")
		return &self->_gCurrAreaIndex;
	if (const BehaviorScript* script = _level->Script(symbol))
		return const_cast<BehaviorScript*>(script);
	if (_game->reportRefusals)
	{
		std::fprintf(stderr, "BitfsCorner: refused the symbol %s\n", symbol);
		std::fflush(stderr);
	}
	Refuse("symbol " + std::string(name) + " is not one the corner serves");
}

void BitfsCorner::ReportRefusals(bool on)
{
	_game->reportRefusals = on;
}

std::size_t BitfsCorner::getStateSize(const Sm64Mem&) const
{
	return sizeof(BitfsCornerMem);
}

uint32_t BitfsCorner::getCurrentFrame() const
{
	return uint32_t(_game->frame);
}
