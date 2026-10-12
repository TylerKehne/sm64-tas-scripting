#include <doctest/doctest.h>
#include <BitfsCorner.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/SurfaceLoad.hpp>
#include <sm64/SurfaceTerrains.hpp>
#include <sm64/Types.hpp>
#include <tasfw/M64.hpp>

#include "libsm64_env.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// ROADMAP 4.13: the BitFS corner resource, before its frame is ported. What a resource of the
// corner reads of the game at its construction and at a state's import must be the game's:
// the static surfaces and their partition node for node, the behaviors and the collision
// data; Mario, the corner's objects, their surfaces and lists with every pointer landed on
// the level's copy or the resource's own memory; and a state must survive a save, a load and
// a move to another resource with its pointers following. Runs against the real game DLL
// (libsm64_env.hpp says when and what it plays).

using namespace tasfw::tests;

namespace
{
	template <class T>
	T Read(LibSm64& game, const char* symbol)
	{
		return *static_cast<T*>(game.addr(symbol));
	}

	// The surfaces a partition list holds, as indices into the pool its nodes point into.
	std::vector<std::ptrdiff_t> ListSurfaces(const SurfaceNode& head, const Surface* pool)
	{
		std::vector<std::ptrdiff_t> out;
		for (const SurfaceNode* n = head.next; n != nullptr; n = n->next)
			out.push_back(n->surface - pool);
		return out;
	}

	struct GameAtFrame
	{
		LibSm64 game;
		GameAtFrame() : game(DllConfig(LibSm64SaveMode::Dirty))
		{
			M64 m64(Env("TASFW_M64"));
			REQUIRE(m64.load() == 1);
			PlayFrames(game, m64, TestFrame());
		}
	};

	int CellIndex(float coordinate)
	{
		int index = int((coordinate + LEVEL_BOUNDARY_MAX) / CELL_SIZE);
		return index < 0 ? 0 : index > NUM_CELLS_INDEX ? NUM_CELLS_INDEX : index;
	}
}

TEST_CASE("BitfsCorner: the level read from the game is the game's"
	* doctest::skip(!HaveDll()))
{
	GameAtFrame at;
	LibSm64& game = at.game;
	BitfsCornerLevel level(game);

	CHECK(level.marioSlot == 89);
	CHECK(level.staticSurfaces.size() == size_t(Read<int32_t>(game, "gNumStaticSurfaces")));
	CHECK(level.staticNodes.size() == size_t(Read<int32_t>(game, "gNumStaticSurfaceNodes")));
	const Surface* gamePool = Read<Surface*>(game, "sSurfacePool");
	CHECK(std::memcmp(level.staticSurfaces.data(), gamePool, level.staticSurfaces.size() * sizeof(Surface)) == 0);

	// Every cell's three lists walk the same surfaces in the same order.
	const SpatialPartitionCell* gameCells = static_cast<SpatialPartitionCell*>(game.addr("gStaticSurfacePartition"));
	int lists = 0;
	int mismatched = 0;
	for (int z = 0; z < NUM_CELLS; z++)
		for (int x = 0; x < NUM_CELLS; x++)
			for (int list = 0; list < 3; list++)
			{
				auto theirs = ListSurfaces(gameCells[z * NUM_CELLS + x][list], gamePool);
				auto ours = ListSurfaces(level.staticPartition[z][x][list], level.staticSurfaces.data());
				lists += !theirs.empty();
				mismatched += theirs != ours;
			}
	CHECK(lists > 0);
	CHECK(mismatched == 0);

	REQUIRE(level.behaviors.size() == 4);
	for (const BitfsCornerLevel::Behavior& behavior : level.behaviors)
	{
		CHECK(behavior.script.size() > 2);
		CHECK(behavior.script.back() == BehaviorScript(0x09) << 24); // END_LOOP
		MESSAGE(behavior.name << ": " << behavior.script.size() << " words");
	}
	CHECK(level.Script("bhvBitfsTiltingInvertedPyramid") != nullptr);
	CHECK(level.Script("bhvGoomba") == nullptr);

	// The two pyramids and the track platform have collision data; the bully has none.
	REQUIRE(level.collisions.size() == 3);
	for (const BitfsCornerLevel::Collision& collision : level.collisions)
	{
		CHECK(collision.data.front() == TERRAIN_LOAD_VERTICES);
		CHECK(collision.data.back() == TERRAIN_LOAD_CONTINUE);
		MESSAGE("slot " << collision.slot << ": " << collision.data.size() << " words of collision data, " << collision.data[1] << " vertices");
	}
	CHECK(level.CollisionOf(84) != nullptr);
	CHECK(level.CollisionOf(85) != nullptr);
	CHECK(level.CollisionOf(27) == nullptr);
	// The track platform's path, to its end marker.
	REQUIRE(level.trackPlatformPath.size() > 1);
	CHECK(level.trackPlatformPath.back().flags == -1);
	CHECK(level.trackPlatformPath.front().flags != -1);
	MESSAGE("track platform path: " << level.trackPlatformPath.size() - 1 << " waypoints");
	MESSAGE("static surfaces: " << level.staticSurfaces.size() << ", nodes " << level.staticNodes.size() << ", lists " << lists
		<< "; level " << level.levelNum << " course " << level.courseNum << " area " << level.areaIndex);
}

TEST_CASE("BitfsCorner: a state imported from the game is the game's, survives a save and a load, and moves between resources"
	* doctest::skip(!HaveDll()))
{
	GameAtFrame at;
	LibSm64& game = at.game;
	auto level = std::make_shared<const BitfsCornerLevel>(game);
	BitfsCorner corner(level);
	corner.load(BitfsCornerMem(game, *level, BitfsCornerObjects::All()));

	const MarioState* gm = Read<MarioState*>(game, "gMarioState");
	const Object* gamePool = static_cast<Object*>(game.addr("gObjectPool"));
	MarioState* m = *static_cast<MarioState**>(corner.addr("gMarioState"));
	Object* pool = static_cast<Object*>(corner.addr("gObjectPool"));

	// The symbols, as the game serves them.
	CHECK(m == corner.addr("gMarioStates"));
	CHECK(*static_cast<Object**>(corner.addr("gMarioObject")) == &pool[89]);
	CHECK(*static_cast<Camera**>(corner.addr("gCamera")) != nullptr);
	CHECK(*static_cast<int16_t*>(corner.addr("gCurrLevelNum")) == Read<int16_t>(game, "gCurrLevelNum"));
	CHECK(*static_cast<uint32_t*>(corner.addr("gGlobalTimer")) == Read<uint32_t>(game, "gGlobalTimer"));
	CHECK(corner.getCurrentFrame() == game.getCurrentFrame());
	CHECK_THROWS_AS(corner.addr("bhvGoomba"), std::runtime_error);

	// Mario, re-pointed: his object, his controller, his floor on the pyramid.
	CHECK(m->marioObj == &pool[89]);
	CHECK(m->controller == corner.addr("gControllers"));
	CHECK(m->action == gm->action);
	CHECK(m->pos[0] == gm->pos[0]);
	CHECK(m->pos[1] == gm->pos[1]);
	CHECK(m->pos[2] == gm->pos[2]);
	CHECK(m->floorHeight == gm->floorHeight);
	REQUIRE(m->floor != nullptr);
	CHECK(m->floor->object == &pool[84]);
	CHECK(m->floor->normal.x == gm->floor->normal.x);
	CHECK(m->floor->normal.y == gm->floor->normal.y);
	CHECK(m->floor->type == gm->floor->type);

	// The objects: the behaviors and collision data on the level's copies, the pointers in
	// the pool, the values the game's.
	CHECK(pool[89].behavior == corner.addr("bhvMario"));
	CHECK(pool[84].behavior == corner.addr("bhvBitfsTiltingInvertedPyramid"));
	CHECK(pool[83].behavior == corner.addr("bhvBitfsTiltingInvertedPyramid"));
	CHECK(pool[27].behavior == corner.addr("bhvSmallBully"));
	CHECK(pool[85].behavior == corner.addr("bhvPlatformOnTrack"));
	CHECK(pool[85].oPlatformOnTrackStartWaypoint == level->trackPlatformPath.data());
	CHECK(pool[85].oPlatformOnTrackPrevWaypoint - pool[85].oPlatformOnTrackStartWaypoint
		== gamePool[85].oPlatformOnTrackPrevWaypoint - gamePool[85].oPlatformOnTrackStartWaypoint);
	CHECK(pool[84].curBhvCommand - pool[84].behavior == gamePool[84].curBhvCommand - gamePool[84].behavior);
	CHECK(pool[84].collisionData == level->CollisionOf(84)->data.data());
	CHECK(pool[84].oTiltingPyramidNormalX == gamePool[84].oTiltingPyramidNormalX);
	CHECK(pool[84].oTiltingPyramidNormalZ == gamePool[84].oTiltingPyramidNormalZ);
	CHECK(pool[84].oPosX == gamePool[84].oPosX);
	CHECK(pool[89].platform == &pool[84]);
	// The geo pass nulls an object's throwMatrix after drawing it (geo_process_object), and
	// the pyramid's behavior sets it again each frame: at a frame boundary it is null in
	// the game, and the import keeps whatever the game has, re-pointed when set.
	CHECK((pool[84].header.gfx.throwMatrix == nullptr) == (gamePool[84].header.gfx.throwMatrix == nullptr));
	CHECK((pool[84].header.gfx.throwMatrix == nullptr || pool[84].header.gfx.throwMatrix == &pool[84].transform));
	CHECK(pool[89].parentObj == &pool[89]);
	CHECK(pool[83].activeFlags == gamePool[83].activeFlags);
	CHECK(pool[27].activeFlags == gamePool[27].activeFlags);
	CHECK(pool[0].activeFlags == 0); // a slot the corner has not got stays empty

	// A save and a load restore the values with the pointers intact.
	int64_t slot = corner.SaveState();
	float x = m->pos[0];
	float nx = pool[84].oTiltingPyramidNormalX;
	m->pos[0] += 10.0f;
	pool[84].oTiltingPyramidNormalX += 0.5f;
	corner.LoadState(slot);
	CHECK(m->pos[0] == x);
	CHECK(pool[84].oTiltingPyramidNormalX == nx);
	CHECK(m->floor->object == &pool[84]);
	CHECK(m->marioObj == &pool[89]);
	corner.DisposeState(slot);

	// Another resource loads the state: every pointer lands in its own memory.
	BitfsCorner other(level);
	Sm64Mem saved;
	corner.save(saved);
	other.load(saved);
	MarioState* om = *static_cast<MarioState**>(other.addr("gMarioState"));
	Object* otherPool = static_cast<Object*>(other.addr("gObjectPool"));
	CHECK(om != m);
	CHECK(om->marioObj == &otherPool[89]);
	CHECK(om->controller == other.addr("gControllers"));
	REQUIRE(om->floor != nullptr);
	CHECK(om->floor != m->floor);
	CHECK(om->floor->object == &otherPool[84]);
	CHECK(om->floor->normal.x == m->floor->normal.x);
	CHECK(otherPool[89].platform == &otherPool[84]);
	CHECK((otherPool[84].header.gfx.throwMatrix == nullptr || otherPool[84].header.gfx.throwMatrix == &otherPool[84].transform));
	CHECK(otherPool[89].header.next != pool[89].header.next);
	// The dynamic partition's lists point into the other's own nodes: the floor under Mario
	// is in his cell's floor list there.
	Sm64Mem againState;
	other.save(againState);
	const BitfsCornerMem& again = againState.Corner();
	const SurfaceNode& floors = again.globals.gDynamicSurfacePartition[CellIndex(om->pos[2])][CellIndex(om->pos[0])][SPATIAL_PARTITION_FLOORS];
	bool found = false;
	for (const SurfaceNode* n = floors.next; n != nullptr; n = n->next)
		found = found || n->surface == om->floor;
	CHECK(found);
	CHECK(om->area != m->area);
	CHECK(om->area->camera == om->area->camera); // relocated with it: not the other's camera
	CHECK(om->area->camera != m->area->camera);
	CHECK(om->animation != nullptr);
	CHECK(om->marioObj->header.gfx.animInfo.curAnim == level->MarioAnimation(size_t(om->marioObj->header.gfx.animInfo.animID)));
	MESSAGE("state: " << sizeof(BitfsCornerMem) << " bytes; dynamic surfaces " << again.globals.gSurfacesAllocated - int32_t(level->staticSurfaces.size())
		<< ", nodes " << again.globals.gSurfaceNodesAllocated - int32_t(level->staticNodes.size()) << "; Mario's animations " << level->marioAnimations.size());

	// Mario's animation as each side resolved it, at the anchor and after one neutral frame:
	// the same id, the same header, the same frame.
	auto animation = [](const MarioState* mario)
	{
		const AnimInfo& info = mario->marioObj->header.gfx.animInfo;
		const Animation* anim = info.curAnim;
		char buf[160];
		std::snprintf(buf, sizeof(buf), "id %d frame %d timer %u accel %d; header flags 0x%X start %d loop %d-%d divisor %d", int(info.animID), int(info.animFrame),
			unsigned(info.animTimer), int(info.animAccel), anim ? unsigned(anim->flags) : 0u, anim ? int(anim->startFrame) : -1, anim ? int(anim->loopStart) : -1,
			anim ? int(anim->loopEnd) : -1, anim ? int(anim->animYTransDivisor) : -1);
		return std::string(buf);
	};
	MESSAGE("anchor: game " << animation(gm) << " | corner " << animation(m));
	game.setInputs(Inputs());
	game.FrameAdvance();
	corner.setInputs(Inputs());
	corner.FrameAdvance();
	MESSAGE("+1 frame: game " << animation(gm) << " | corner " << animation(m) << "; area counters " << *static_cast<int16_t*>(game.addr("gAreaUpdateCounter"))
		<< " vs " << again.globals.gAreaUpdateCounter + 1);
	CHECK(m->marioObj->header.gfx.animInfo.animID == gm->marioObj->header.gfx.animInfo.animID);
	CHECK(m->marioObj->header.gfx.animInfo.animFrame == gm->marioObj->header.gfx.animInfo.animFrame);

	// A first look at the cost, next to the game's (docs/performance.md): neutral frames
	// from here, then saves and loads. The Tier B rows are the measurement; this is the
	// sign that they are worth making.
	auto micros = [](std::chrono::steady_clock::time_point start) { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count(); };
	int64_t anchor = corner.SaveState();
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 20000; i++)
	{
		corner.setInputs(Inputs());
		corner.FrameAdvance();
	}
	double framePer = micros(start) / 20000;
	corner.LoadState(anchor);
	start = std::chrono::steady_clock::now();
	for (int i = 0; i < 2000; i++)
	{
		int64_t s = corner.SaveState();
		corner.DisposeState(s);
	}
	double savePer = micros(start) / 2000;
	start = std::chrono::steady_clock::now();
	for (int i = 0; i < 2000; i++)
		corner.LoadState(anchor);
	double loadPer = micros(start) / 2000;
	corner.DisposeState(anchor);
	MESSAGE("cost: frame " << framePer << " us (neutral), save " << savePer << " us, load " << loadPer << " us");
}

TEST_CASE("BitfsCorner: the objects are optional and off unless asked for"
	* doctest::skip(!HaveDll()))
{
	GameAtFrame at;
	LibSm64& game = at.game;
	auto level = std::make_shared<const BitfsCornerLevel>(game);

	// The default: Mario alone. The four slots are empty, the lists hold only him, and the
	// game's pointers to the left-out objects are null (his platform was the pyramid).
	BitfsCorner alone(level);
	alone.load(BitfsCornerMem(game, *level));
	Object* pool = static_cast<Object*>(alone.addr("gObjectPool"));
	MarioState* m = *static_cast<MarioState**>(alone.addr("gMarioState"));
	for (int slot : BitfsCornerLevel::ObjectSlots)
		CHECK(pool[slot].activeFlags == 0);
	CHECK(pool[89].activeFlags != 0);
	CHECK(pool[89].platform == nullptr);
	REQUIRE(m->floor != nullptr);
	CHECK(m->floor->object == nullptr); // the pyramid's surface, orphaned until the first frame
	// A save's pointers are the live game's, so the lists are read through the objects' own
	// links: a linked object has a next, an empty slot has none.
	Sm64Mem saved;
	alone.save(saved);
	int listed = 0;
	for (const Object& o : saved.Corner().objects)
		listed += o.header.next != nullptr;
	CHECK(listed == 1);
	// A frame runs: Mario, with the pyramid gone from under him, has the lava for a floor.
	alone.setInputs(Inputs());
	alone.FrameAdvance();
	CHECK(m->floor->object == nullptr);
	CHECK(m->floorHeight < -3000.0f);

	// One object asked for: the pyramid alone, and Mario stands on it.
	BitfsCornerObjects pyramidOnly;
	pyramidOnly.pyramid = true;
	BitfsCorner pyramid(level);
	pyramid.load(BitfsCornerMem(game, *level, pyramidOnly));
	Object* pyramidPool = static_cast<Object*>(pyramid.addr("gObjectPool"));
	MarioState* pm = *static_cast<MarioState**>(pyramid.addr("gMarioState"));
	CHECK(pyramidPool[84].activeFlags != 0);
	CHECK(pyramidPool[83].activeFlags == 0);
	CHECK(pyramidPool[85].activeFlags == 0);
	CHECK(pyramidPool[27].activeFlags == 0);
	CHECK(pyramidPool[89].platform == &pyramidPool[84]);
	pyramid.setInputs(Inputs());
	pyramid.FrameAdvance();
	CHECK(pm->floor->object == &pyramidPool[84]);
}
