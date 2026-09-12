#include <VerifyLayout.hpp>

#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>

#include <cstddef>
#include <cstdio>
#include <exception>

namespace
{
	std::string Hex(const void* p)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%p", p);
		return buf;
	}

	std::string Describe(const ExpectedObject& e)
	{
		char buf[160];
		if (e.checkHome)
			std::snprintf(buf, sizeof(buf), "gObjectPool[%d] is %s at home (%g, %g, %g)", e.slot, e.behavior, e.homeX, e.homeY, e.homeZ);
		else
			std::snprintf(buf, sizeof(buf), "gObjectPool[%d] is %s", e.slot, e.behavior);
		return buf;
	}
}

bool VerifyLayout::execution()
{
	LongLoad(_frame);

	std::vector<std::string>& lines = CustomStatus.lines;
	auto ok = [&](const std::string& what) { lines.push_back("ok: " + what); };
	auto fail = [&](const std::string& what)
	{
		lines.push_back("FAIL: " + what);
		CustomStatus.failures++;
	};
	auto note = [&](const std::string& what) { lines.push_back("note: " + what); };

	// A wrong layout must end in a FAIL line, not a crash: every pointer this script follows
	// is first checked against something computed from symbols alone (the gMarioStates array,
	// a whole slot of gObjectPool), and the script stops at the first of those that fails.

	// --- Checks valid at any frame ------------------------------------------------------
	MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
	MarioState* marioStates = (MarioState*)(resource->addr("gMarioStates"));
	if (marioState != marioStates)
	{
		fail("gMarioState (" + Hex(marioState) + ") != &gMarioStates[0] (" + Hex(marioStates) + "); pointer width or symbol resolution is wrong");
		return true;
	}
	ok("gMarioState points at gMarioStates[0]");

	Object* objectPool = (Object*)(resource->addr("gObjectPool"));
	auto poolSlot = [&](const void* p) -> int // the slot an object pointer names, or -1
	{
		std::ptrdiff_t offset = reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(objectPool);
		if (offset < 0 || offset % std::ptrdiff_t(sizeof(Object)) != 0 || offset / std::ptrdiff_t(sizeof(Object)) >= LibSm64ObjectPoolCapacity)
			return -1;
		return int(offset / std::ptrdiff_t(sizeof(Object)));
	};

	uint32_t timer = *(uint32_t*)(resource->addr("gGlobalTimer"));
	ok("gGlobalTimer readable (" + std::to_string(timer) + ")");

	// --- Checks that need Mario to exist (inside a level) --------------------------------
	Object* marioObj = *(Object**)(resource->addr("gMarioObject"));
	if (marioObj == nullptr)
	{
		note("gMarioObject is null (not in a level); in-level checks skipped");
		return true;
	}
	int marioSlot = poolSlot(marioObj);
	if (marioSlot < 0)
	{
		fail("gMarioObject (" + Hex(marioObj) + ") is not a whole slot of gObjectPool (" + Hex(objectPool) + ", " + std::to_string(LibSm64ObjectPoolCapacity)
			+ " x " + std::to_string(sizeof(Object)) + " bytes); struct Object layout is wrong");
		return true;
	}
	ok("gMarioObject is gObjectPool[" + std::to_string(marioSlot) + "] (sizeof(Object) = " + std::to_string(sizeof(Object)) + " matches the pool stride)");

	const void* bhvMario = resource->addr("bhvMario");
	if (marioObj->behavior == bhvMario)
		ok("gMarioObject->behavior == bhvMario (Object::behavior offset)");
	else
		fail("gMarioObject->behavior (" + Hex(marioObj->behavior) + ") != bhvMario (" + Hex(bhvMario) + "); Object::behavior offset is wrong");

	if (marioState->marioObj != marioObj)
	{
		fail("gMarioState->marioObj (" + Hex(marioState->marioObj) + ") != gMarioObject (" + Hex(marioObj) + "); MarioState layout is wrong");
		return true;
	}
	ok("gMarioState->marioObj == gMarioObject (MarioState::marioObj offset)");

	// mario.c (update_mario_inputs / copy_mario_state_to_object) mirrors MarioState::pos into
	// both oPosX/Y/Z and header.gfx.pos every frame Mario is updated.
	if (marioObj->oPosX == marioState->pos[0] && marioObj->oPosY == marioState->pos[1] && marioObj->oPosZ == marioState->pos[2])
		ok("gMarioObject->oPos[XYZ] == gMarioState->pos (Object::rawData indexing and MarioState::pos offset)");
	else
		fail("gMarioObject->oPos (" + std::to_string(marioObj->oPosX) + ", " + std::to_string(marioObj->oPosY) + ", " + std::to_string(marioObj->oPosZ)
			+ ") != gMarioState->pos (" + std::to_string(marioState->pos[0]) + ", " + std::to_string(marioState->pos[1]) + ", " + std::to_string(marioState->pos[2])
			+ "); Object field indexing or MarioState::pos offset is wrong");

	const f32* gfxPos = marioObj->header.gfx.pos;
	if (gfxPos[0] == marioState->pos[0] && gfxPos[1] == marioState->pos[1] && gfxPos[2] == marioState->pos[2])
		ok("gMarioObject->header.gfx.pos == gMarioState->pos (GraphNodeObject layout)");
	else
		fail("gMarioObject->header.gfx.pos (" + std::to_string(gfxPos[0]) + ", " + std::to_string(gfxPos[1]) + ", " + std::to_string(gfxPos[2])
			+ ") != gMarioState->pos; GraphNode/GraphNodeObject layout is wrong");

	Surface* floor = marioState->floor;
	if (floor == nullptr)
		note("gMarioState->floor is null (Mario airborne or out of bounds); Surface checks skipped");
	else
	{
		float n = floor->normal.x * floor->normal.x + floor->normal.y * floor->normal.y + floor->normal.z * floor->normal.z;
		if (n > 0.999f && n < 1.001f)
			ok("gMarioState->floor->normal is unit length (Surface::normal offset)");
		else
			fail("gMarioState->floor->normal has squared length " + std::to_string(n) + "; Surface layout is wrong");

		// A surface belongs to a level (no object) or to an object in the pool.
		if (floor->object == nullptr)
			ok("gMarioState->floor->object is null, a level surface (Surface::object offset)");
		else if (poolSlot(floor->object) >= 0)
			ok("gMarioState->floor->object is gObjectPool[" + std::to_string(poolSlot(floor->object)) + "] (Surface::object offset)");
		else
			fail("gMarioState->floor->object (" + Hex(floor->object) + ") is neither null nor a slot of gObjectPool; Surface::object offset is wrong");
	}

	Camera* camera = *(Camera**)(resource->addr("gCamera"));
	if (camera != nullptr)
		ok("gCamera is set");
	else
		fail("gCamera is null inside a level");

	// --- Level objects the scripts address by slot -----------------------------------------
	auto behaviorOf = [&](const ExpectedObject& e) -> const void*
	{
		try
		{
			return resource->addr(e.behavior);
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	};
	for (const ExpectedObject& e : _expected)
	{
		if (e.slot < 0 || e.slot >= LibSm64ObjectPoolCapacity)
		{
			fail("expected " + Describe(e) + ", but the slot is outside the pool");
			continue;
		}
		const void* behavior = behaviorOf(e);
		if (behavior == nullptr)
		{
			fail("expected " + Describe(e) + ", but this DLL does not export " + e.behavior + " (nor an alias of it)");
			continue;
		}
		const Object& o = objectPool[e.slot];
		if (o.activeFlags == 0)
		{
			fail("expected " + Describe(e) + ", but the slot is inactive (not in that level, or the level reloaded)");
			continue;
		}
		if (o.behavior != behavior)
		{
			// Name the intruder when we can: a slot shift shows up as a neighbouring level object.
			std::string other;
			for (const ExpectedObject& f : _expected)
				if (o.behavior == behaviorOf(f))
					other = std::string(" (it runs ") + f.behavior + ")";
			fail("expected " + Describe(e) + ", but the slot runs a different behavior" + other
				+ "; the object spawn order changed, so every hardcoded slot index is suspect");
			continue;
		}
		if (e.checkHome && (o.oHomeX != e.homeX || o.oHomeY != e.homeY || o.oHomeZ != e.homeZ))
		{
			char buf[96];
			std::snprintf(buf, sizeof(buf), " (%g, %g, %g)", o.oHomeX, o.oHomeY, o.oHomeZ);
			fail("expected " + Describe(e) + ", but that object's home is" + buf + "; same behavior, different object (the level has more than one)");
			continue;
		}
		ok(Describe(e));
	}
	return true;
}
