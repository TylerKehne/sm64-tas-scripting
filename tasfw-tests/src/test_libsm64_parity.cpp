#include <doctest/doctest.h>
#include <BitfsCorner.hpp>
#include <LibSm64.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/M64.hpp>

#include "libsm64_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

// ROADMAP 4.13: the parity harness. Two resources from the same state take the same inputs
// every frame, and after each, everything the search reads of the game is compared: Mario
// (every value field of gMarioStates, his object's fields, his platform, and the floor,
// ceiling and wall under, over and beside him), the objects of the pyramid corner (slots
// 84, 83, 85 and 27: their fields and their transform), the camera, the global timer and
// the RNG seed. Identical after every frame of a fuzz that walks, dives and falls from the
// anchor frame, or the second resource is not the game for the search's purposes: a
// mismatch is a bug, never a rate (AGENTS.md, hard rule 1). Pointers are compared as what
// they point at (a pool slot, a surface's fields), since two instances of the game live at
// different addresses; an object's rawData holds no pointer on this build (ptrData is
// separate), so it is compared whole.
//
// The pairs: the game against a second copy of itself first (the comparator must find
// nothing between two copies of the DLL, or it compares something that is not state), then
// the game against the game with every object but the corner's unloaded, the cut that
// `dllcheck --keep-objects` measures at 2.4x on a frame; then the game against the corner
// resource, BitfsCorner, under neutral inputs (the stationary frame) and under the fuzz. For
// that pair what the corner does not model is left out: the camera's position and focus
// (its yaw is what Mario's inputs read, and the corner holds it), and the particles (Mario's
// oActiveParticleFlags follows objects the corner does not spawn). A frame the corner
// refuses (a reach into what it has not got) ends the run and is reported with where.
//
// The fuzz: segments of up to 150 frames from a save at the anchor, each ending early on
// the frame Mario touches the lava (the search's boundary, so the fuzz's: past it the boosts
// carry him across the lava sea, out of the corner), both resources loaded back between them
// so the cut stays cut, the stick a random walk with B presses and the odd Z, never A (the
// setup is an ABC run) and never a C button (the camera mode turns on them, and the corner
// refuses them); 6,000 frames by default, TASFW_PARITY_FRAMES the total for a long run. The
// game pairs need two DLL copies (res/sm64_jp_0.dll and sm64_jp_1.dll) and skip without the
// second. The test drives the resources directly, as the other tests whose subject is the
// resource do (hard rule 9).
//
// The second input set: the solution movies the pipeline exports (the fixer's dive and
// rollout, the oscillations' turnarounds, the ending's leaves with their C-up), inputs a
// random walk seldom makes. Every movie under TASFW_PARITY_MOVIES (analysis/m64, which
// scripts/test.ps1 names when it is there) is played on the corner and on the game cut to
// the corner's objects, from the earliest frame any of them leaves the source movie, and
// compared after every frame; TASFW_PARITY_MOVIE_LIMIT says how many (40 unless set, 0 for
// all). The cut, not the whole game, because the way onto the pyramid passes a yellow coin
// (slot 23) the corner has not got: on the whole game it changes Mario's coin count and
// heals him, nothing the search reads. That the coin is all the whole game adds along the
// movies is its own case: the whole game with its yellow coins unloaded against the cut
// game, everything compared, on the same movies.

using namespace tasfw::tests;

namespace
{
	// A value field of a game struct: where it is, how to read it, how many elements.
	struct FieldSpec
	{
		enum Kind { Float, Signed, Unsigned };
		const char* name;
		size_t offset;
		size_t elemSize;
		size_t count;
		Kind kind;
	};

	// MarioState without its pointers (wall, ceil, floor, the objects, the area, the
	// controller, the animation: compared through what they point at, or not state).
	const FieldSpec MarioFields[] = {
		{"unk00", offsetof(MarioState, unk00), 2, 1, FieldSpec::Unsigned},
		{"input", offsetof(MarioState, input), 2, 1, FieldSpec::Unsigned},
		{"flags", offsetof(MarioState, flags), 4, 1, FieldSpec::Unsigned},
		{"particleFlags", offsetof(MarioState, particleFlags), 4, 1, FieldSpec::Unsigned},
		{"action", offsetof(MarioState, action), 4, 1, FieldSpec::Unsigned},
		{"prevAction", offsetof(MarioState, prevAction), 4, 1, FieldSpec::Unsigned},
		{"terrainSoundAddend", offsetof(MarioState, terrainSoundAddend), 4, 1, FieldSpec::Unsigned},
		{"actionState", offsetof(MarioState, actionState), 2, 1, FieldSpec::Unsigned},
		{"actionTimer", offsetof(MarioState, actionTimer), 2, 1, FieldSpec::Unsigned},
		{"actionArg", offsetof(MarioState, actionArg), 4, 1, FieldSpec::Unsigned},
		{"intendedMag", offsetof(MarioState, intendedMag), 4, 1, FieldSpec::Float},
		{"intendedYaw", offsetof(MarioState, intendedYaw), 2, 1, FieldSpec::Signed},
		{"invincTimer", offsetof(MarioState, invincTimer), 2, 1, FieldSpec::Signed},
		{"framesSinceA", offsetof(MarioState, framesSinceA), 1, 1, FieldSpec::Unsigned},
		{"framesSinceB", offsetof(MarioState, framesSinceB), 1, 1, FieldSpec::Unsigned},
		{"wallKickTimer", offsetof(MarioState, wallKickTimer), 1, 1, FieldSpec::Unsigned},
		{"doubleJumpTimer", offsetof(MarioState, doubleJumpTimer), 1, 1, FieldSpec::Unsigned},
		{"faceAngle", offsetof(MarioState, faceAngle), 2, 3, FieldSpec::Signed},
		{"angleVel", offsetof(MarioState, angleVel), 2, 3, FieldSpec::Signed},
		{"slideYaw", offsetof(MarioState, slideYaw), 2, 1, FieldSpec::Signed},
		{"twirlYaw", offsetof(MarioState, twirlYaw), 2, 1, FieldSpec::Signed},
		{"pos", offsetof(MarioState, pos), 4, 3, FieldSpec::Float},
		{"vel", offsetof(MarioState, vel), 4, 3, FieldSpec::Float},
		{"forwardVel", offsetof(MarioState, forwardVel), 4, 1, FieldSpec::Float},
		{"slideVelX", offsetof(MarioState, slideVelX), 4, 1, FieldSpec::Float},
		{"slideVelZ", offsetof(MarioState, slideVelZ), 4, 1, FieldSpec::Float},
		{"ceilHeight", offsetof(MarioState, ceilHeight), 4, 1, FieldSpec::Float},
		{"floorHeight", offsetof(MarioState, floorHeight), 4, 1, FieldSpec::Float},
		{"floorAngle", offsetof(MarioState, floorAngle), 2, 1, FieldSpec::Signed},
		{"waterLevel", offsetof(MarioState, waterLevel), 2, 1, FieldSpec::Signed},
		{"collidedObjInteractTypes", offsetof(MarioState, collidedObjInteractTypes), 4, 1, FieldSpec::Unsigned},
		{"numCoins", offsetof(MarioState, numCoins), 2, 1, FieldSpec::Signed},
		{"numStars", offsetof(MarioState, numStars), 2, 1, FieldSpec::Signed},
		{"numKeys", offsetof(MarioState, numKeys), 1, 1, FieldSpec::Signed},
		{"numLives", offsetof(MarioState, numLives), 1, 1, FieldSpec::Signed},
		{"health", offsetof(MarioState, health), 2, 1, FieldSpec::Signed},
		{"unkB0", offsetof(MarioState, unkB0), 2, 1, FieldSpec::Signed},
		{"hurtCounter", offsetof(MarioState, hurtCounter), 1, 1, FieldSpec::Unsigned},
		{"healCounter", offsetof(MarioState, healCounter), 1, 1, FieldSpec::Unsigned},
		{"squishTimer", offsetof(MarioState, squishTimer), 1, 1, FieldSpec::Unsigned},
		{"fadeWarpOpacity", offsetof(MarioState, fadeWarpOpacity), 1, 1, FieldSpec::Unsigned},
		{"capTimer", offsetof(MarioState, capTimer), 2, 1, FieldSpec::Unsigned},
		{"prevNumStarsForDialog", offsetof(MarioState, prevNumStarsForDialog), 2, 1, FieldSpec::Signed},
		{"peakHeight", offsetof(MarioState, peakHeight), 4, 1, FieldSpec::Float},
		{"quicksandDepth", offsetof(MarioState, quicksandDepth), 4, 1, FieldSpec::Float},
		{"unkC4", offsetof(MarioState, unkC4), 4, 1, FieldSpec::Float},
	};

	// An object's value fields beside its rawData: its graph node's position, angle, scale
	// and animation (the frame and timer an action's end is read from), the flags, the
	// behavior stack's depth and delay, the hitboxes and the transform (the pyramid's is its
	// surfaces' frame).
	const FieldSpec ObjectFields[] = {
		{"gfx.angle", offsetof(Object, header.gfx.angle), 2, 3, FieldSpec::Signed},
		{"gfx.pos", offsetof(Object, header.gfx.pos), 4, 3, FieldSpec::Float},
		{"gfx.scale", offsetof(Object, header.gfx.scale), 4, 3, FieldSpec::Float},
		{"gfx.animInfo.animID", offsetof(Object, header.gfx.animInfo.animID), 2, 1, FieldSpec::Signed},
		{"gfx.animInfo.animYTrans", offsetof(Object, header.gfx.animInfo.animYTrans), 2, 1, FieldSpec::Signed},
		{"gfx.animInfo.animFrame", offsetof(Object, header.gfx.animInfo.animFrame), 2, 1, FieldSpec::Signed},
		{"gfx.animInfo.animTimer", offsetof(Object, header.gfx.animInfo.animTimer), 2, 1, FieldSpec::Unsigned},
		{"gfx.animInfo.animFrameAccelAssist", offsetof(Object, header.gfx.animInfo.animFrameAccelAssist), 4, 1, FieldSpec::Signed},
		{"gfx.animInfo.animAccel", offsetof(Object, header.gfx.animInfo.animAccel), 4, 1, FieldSpec::Signed},
		{"activeFlags", offsetof(Object, activeFlags), 2, 1, FieldSpec::Signed},
		{"numCollidedObjs", offsetof(Object, numCollidedObjs), 2, 1, FieldSpec::Signed},
		{"collidedObjInteractTypes", offsetof(Object, collidedObjInteractTypes), 4, 1, FieldSpec::Unsigned},
		{"bhvStackIndex", offsetof(Object, bhvStackIndex), 4, 1, FieldSpec::Unsigned},
		{"bhvDelayTimer", offsetof(Object, bhvDelayTimer), 2, 1, FieldSpec::Signed},
		{"respawnInfoType", offsetof(Object, respawnInfoType), 2, 1, FieldSpec::Signed},
		{"hitboxRadius", offsetof(Object, hitboxRadius), 4, 1, FieldSpec::Float},
		{"hitboxHeight", offsetof(Object, hitboxHeight), 4, 1, FieldSpec::Float},
		{"hurtboxRadius", offsetof(Object, hurtboxRadius), 4, 1, FieldSpec::Float},
		{"hurtboxHeight", offsetof(Object, hurtboxHeight), 4, 1, FieldSpec::Float},
		{"hitboxDownOffset", offsetof(Object, hitboxDownOffset), 4, 1, FieldSpec::Float},
		{"transform", offsetof(Object, transform), 4, 16, FieldSpec::Float},
	};

	// A surface without its object pointer (compared as a slot beside it).
	const FieldSpec SurfaceFields[] = {
		{"type", offsetof(Surface, type), 2, 1, FieldSpec::Signed},
		{"force", offsetof(Surface, force), 2, 1, FieldSpec::Signed},
		{"flags", offsetof(Surface, flags), 1, 1, FieldSpec::Signed},
		{"room", offsetof(Surface, room), 1, 1, FieldSpec::Signed},
		{"lowerY", offsetof(Surface, lowerY), 2, 1, FieldSpec::Signed},
		{"upperY", offsetof(Surface, upperY), 2, 1, FieldSpec::Signed},
		{"vertex1", offsetof(Surface, vertex1), 2, 3, FieldSpec::Signed},
		{"vertex2", offsetof(Surface, vertex2), 2, 3, FieldSpec::Signed},
		{"vertex3", offsetof(Surface, vertex3), 2, 3, FieldSpec::Signed},
		{"normal", offsetof(Surface, normal), 4, 3, FieldSpec::Float},
		{"originOffset", offsetof(Surface, originOffset), 4, 1, FieldSpec::Float},
	};

	const FieldSpec CameraFields[] = {
		{"mode", offsetof(Camera, mode), 1, 1, FieldSpec::Unsigned},
		{"defMode", offsetof(Camera, defMode), 1, 1, FieldSpec::Unsigned},
		{"yaw", offsetof(Camera, yaw), 2, 1, FieldSpec::Signed},
		{"focus", offsetof(Camera, focus), 4, 3, FieldSpec::Float},
		{"pos", offsetof(Camera, pos), 4, 3, FieldSpec::Float},
		{"areaCenX", offsetof(Camera, areaCenX), 4, 1, FieldSpec::Float},
		{"areaCenZ", offsetof(Camera, areaCenZ), 4, 1, FieldSpec::Float},
		{"cutscene", offsetof(Camera, cutscene), 1, 1, FieldSpec::Unsigned},
		{"nextYaw", offsetof(Camera, nextYaw), 2, 1, FieldSpec::Signed},
		{"doorStatus", offsetof(Camera, doorStatus), 1, 1, FieldSpec::Unsigned},
		{"areaCenY", offsetof(Camera, areaCenY), 4, 1, FieldSpec::Float},
	};
	const FieldSpec CameraYawField[] = {
		{"yaw", offsetof(Camera, yaw), 2, 1, FieldSpec::Signed},
	};

	// The rawData index of oActiveParticleFlags, found through the accessor itself.
	size_t ActiveParticleFlagsIndex()
	{
		Object probe{};
		probe.oActiveParticleFlags = 0x5EEDF1A6;
		for (size_t i = 0; i < std::size(probe.rawData.asU32); i++)
			if (probe.rawData.asU32[i] == 0x5EEDF1A6)
				return i;
		return size_t(-1);
	}

	// What a comparison leaves out, for a pair whose second resource does not model it.
	struct Options
	{
		bool cameraYawOnly = false;	 // the corner holds the camera; its yaw is what Mario reads
		bool particles = true;		 // false: Mario's oActiveParticleFlags, which follows particle objects
		bool unmodelledAbsent = false; // a pointer to an object outside the compared set (and Mario) counts as null
		// A refusal whose message contains this ends the segment rather than the run: what
		// the corner declines by design and the search never reaches (the bully's death,
		// which spawns smoke and a coin), so the fuzz goes on from the anchor.
		const char* refusalEndsSegment = nullptr;
	};

	// What the search reads of the game, through the resource's addr() as a script would
	// (the pointers cached once, as ReadState's contract asks).
	struct GameView
	{
		Object* pool = nullptr;
		MarioState* mario = nullptr;
		Camera* camera = nullptr;
		const uint32_t* globalTimer = nullptr;
		const uint16_t* randomSeed = nullptr; // null on a build that does not export it
		const int16_t* cameraMovementFlags = nullptr; // gCameraMovementFlags, the C-up mode's flag among them; null likewise

		template <class TResource>
		static GameView Read(TResource& resource)
		{
			GameView v;
			v.pool = static_cast<Object*>(resource.addr("gObjectPool"));
			v.mario = *static_cast<MarioState**>(resource.addr("gMarioState"));
			v.camera = *static_cast<Camera**>(resource.addr("gCamera"));
			v.globalTimer = static_cast<const uint32_t*>(resource.addr("gGlobalTimer"));
			try
			{
				v.randomSeed = static_cast<const uint16_t*>(resource.addr("gRandomSeed16"));
			}
			catch (const std::exception&)
			{
				v.randomSeed = nullptr;
			}
			try
			{
				v.cameraMovementFlags = static_cast<const int16_t*>(resource.addr("gCameraMovementFlags"));
			}
			catch (const std::exception&)
			{
				v.cameraMovementFlags = nullptr;
			}
			return v;
		}

		// A pool pointer as its slot: -1 for null, -2 for a pointer outside the pool.
		int Slot(const Object* o) const
		{
			if (o == nullptr)
				return -1;
			std::ptrdiff_t i = o - pool;
			return (i >= 0 && i < LibSm64ObjectPoolCapacity && pool + i == o) ? int(i) : -2;
		}
	};

	// The first difference between two views as one line, or empty.
	class Parity
	{
	public:
		// What a comparison is of: the objects compared, Mario's slot, and the options.
		struct Context
		{
			std::initializer_list<int> objects;
			int marioSlot;
			Options options;
			bool Modelled(int slot) const
			{
				return slot == marioSlot || std::find(objects.begin(), objects.end(), slot) != objects.end();
			}
		};

		static std::string Diff(const GameView& a, const GameView& b, std::initializer_list<int> objects, const Options& options)
		{
			std::string out;
			Context c{objects, a.Slot(a.mario->marioObj), options};
			if (Fields("mario", MarioFields, std::size(MarioFields), a.mario, b.mario, out))
				return out;
			if (Slot("mario.marioObj", a, b, a.mario->marioObj, b.mario->marioObj, c, out)
				|| Slot("mario.interactObj", a, b, a.mario->interactObj, b.mario->interactObj, c, out)
				|| Slot("mario.usedObj", a, b, a.mario->usedObj, b.mario->usedObj, c, out)
				|| Slot("mario.riddenObj", a, b, a.mario->riddenObj, b.mario->riddenObj, c, out)
				|| Slot("mario.heldObj", a, b, a.mario->heldObj, b.mario->heldObj, c, out))
				return out;
			if (SurfaceAt("mario.floor", a, b, a.mario->floor, b.mario->floor, c, out)
				|| SurfaceAt("mario.ceil", a, b, a.mario->ceil, b.mario->ceil, c, out)
				|| SurfaceAt("mario.wall", a, b, a.mario->wall, b.mario->wall, c, out))
				return out;
			if (a.mario->marioObj != nullptr && b.mario->marioObj != nullptr
				&& ObjectAt("mario.marioObj", a, b, *a.mario->marioObj, *b.mario->marioObj, c, out))
				return out;
			for (int slot : objects)
			{
				char name[32];
				std::snprintf(name, sizeof(name), "obj%d", slot);
				if (ObjectAt(name, a, b, a.pool[slot], b.pool[slot], c, out))
					return out;
			}
			if (options.cameraYawOnly ? Fields("camera", CameraYawField, std::size(CameraYawField), a.camera, b.camera, out)
									  : Fields("camera", CameraFields, std::size(CameraFields), a.camera, b.camera, out))
				return out;
			if (Value("gGlobalTimer", (long long)*a.globalTimer, (long long)*b.globalTimer, out))
				return out;
			if (a.randomSeed != nullptr && b.randomSeed != nullptr
				&& Value("gRandomSeed16", (long long)*a.randomSeed, (long long)*b.randomSeed, out))
				return out;
			if (a.cameraMovementFlags != nullptr && b.cameraMovementFlags != nullptr
				&& Value("gCameraMovementFlags", (long long)*a.cameraMovementFlags, (long long)*b.cameraMovementFlags, out))
				return out;
			return out;
		}

	private:
		static bool Value(const char* name, long long x, long long y, std::string& out)
		{
			if (x == y)
				return false;
			char buf[192];
			std::snprintf(buf, sizeof(buf), "%s: %lld vs %lld", name, x, y);
			out = buf;
			return true;
		}

		// One element of a field read as the kind says, for the message.
		static std::string Element(const FieldSpec& f, const void* base, size_t i)
		{
			const unsigned char* p = static_cast<const unsigned char*>(base) + f.offset + i * f.elemSize;
			char buf[64];
			if (f.kind == FieldSpec::Float)
			{
				float v;
				std::memcpy(&v, p, 4);
				std::snprintf(buf, sizeof(buf), "%.9g", double(v));
			}
			else
			{
				unsigned long long u = 0;
				std::memcpy(&u, p, f.elemSize); // little-endian, as the game's x64 build is
				if (f.kind == FieldSpec::Signed)
				{
					long long s = (long long)(u << (64 - 8 * f.elemSize)) >> (64 - 8 * f.elemSize);
					std::snprintf(buf, sizeof(buf), "%lld", s);
				}
				else
					std::snprintf(buf, sizeof(buf), "0x%llX", u);
			}
			return buf;
		}

		// Every element of every field, bit for bit; the first that differs names the line.
		static bool Fields(const char* prefix, const FieldSpec* specs, size_t n, const void* a, const void* b, std::string& out)
		{
			for (size_t k = 0; k < n; k++)
			{
				const FieldSpec& f = specs[k];
				for (size_t i = 0; i < f.count; i++)
				{
					const unsigned char* x = static_cast<const unsigned char*>(a) + f.offset + i * f.elemSize;
					const unsigned char* y = static_cast<const unsigned char*>(b) + f.offset + i * f.elemSize;
					if (std::memcmp(x, y, f.elemSize) == 0)
						continue;
					char buf[256];
					if (f.count > 1)
						std::snprintf(buf, sizeof(buf), "%s.%s[%zu]: %s vs %s", prefix, f.name, i, Element(f, a, i).c_str(), Element(f, b, i).c_str());
					else
						std::snprintf(buf, sizeof(buf), "%s.%s: %s vs %s", prefix, f.name, Element(f, a, i).c_str(), Element(f, b, i).c_str());
					out = buf;
					return true;
				}
			}
			return false;
		}

		// Two object pointers as slots; a slot outside the compared set counts as none when the
		// second resource does not model it (the game keeps, say, the last object Mario touched).
		static bool Slot(const char* name, const GameView& a, const GameView& b, const Object* x, const Object* y, const Context& c, std::string& out)
		{
			int sa = a.Slot(x);
			int sb = b.Slot(y);
			if (c.options.unmodelledAbsent)
			{
				if (sa >= 0 && !c.Modelled(sa))
					sa = -1;
				if (sb >= 0 && !c.Modelled(sb))
					sb = -1;
			}
			return Value(name, sa, sb, out);
		}

		static bool ObjectAt(const char* name, const GameView& a, const GameView& b, const Object& x, const Object& y, const Context& c, std::string& out)
		{
			static const size_t particleFlags = ActiveParticleFlagsIndex();
			for (size_t i = 0; i < std::size(x.rawData.asU32); i++)
			{
				if (x.rawData.asU32[i] == y.rawData.asU32[i])
					continue;
				if (!c.options.particles && i == particleFlags)
					continue;
				char buf[192];
				std::snprintf(buf, sizeof(buf), "%s.rawData[0x%02zX]: 0x%08X (%.9g) vs 0x%08X (%.9g)", name, i,
					unsigned(x.rawData.asU32[i]), double(x.rawData.asF32[i]), unsigned(y.rawData.asU32[i]), double(y.rawData.asF32[i]));
				out = buf;
				return true;
			}
			if (Fields(name, ObjectFields, std::size(ObjectFields), &x, &y, out))
				return true;
			std::string platform = std::string(name) + ".platform";
			std::string parent = std::string(name) + ".parentObj";
			return Slot(platform.c_str(), a, b, x.platform, y.platform, c, out)
				|| Slot(parent.c_str(), a, b, x.parentObj, y.parentObj, c, out);
		}

		static bool SurfaceAt(const char* name, const GameView& a, const GameView& b, const Surface* x, const Surface* y, const Context& c, std::string& out)
		{
			if (Value((std::string(name) + " is set").c_str(), x != nullptr, y != nullptr, out))
				return true;
			if (x == nullptr)
				return false;
			if (Fields(name, SurfaceFields, std::size(SurfaceFields), x, y, out))
				return true;
			return Slot((std::string(name) + ".object").c_str(), a, b, x->object, y->object, c, out);
		}
	};

	// The fuzz's inputs: the stick a random walk (a new direction one frame in seven, a
	// third of them neutral, the magnitude with it), B one frame in twenty, Z one in fifty,
	// C-up one in fifty (the camera's C-up mode: Mario brakes into it from a walk, stands
	// looking in it, and leaves it on B), never A and never another C button (they turn or
	// zoom the camera, which the corner refuses). The generator's raw words rather than a
	// distribution, so the sequence is the same on every standard library.
	Inputs FuzzInputs(std::mt19937& rng, const Inputs& previous)
	{
		auto pct = [&]() { return int(rng() % 100); };
		int8_t x = previous.stick_x;
		int8_t y = previous.stick_y;
		if (pct() < 15)
		{
			if (pct() < 33)
				x = y = 0;
			else
			{
				double angle = double(rng() % 3600) * (3.14159265358979323846 / 1800.0);
				double magnitude = double(8 + rng() % 113);
				x = int8_t(std::lround(magnitude * std::cos(angle)));
				y = int8_t(std::lround(magnitude * std::sin(angle)));
			}
		}
		uint16_t buttons = 0;
		if (pct() < 5)
			buttons |= Buttons::B;
		if (pct() < 2)
			buttons |= Buttons::Z;
		if (pct() < 2)
			buttons |= Buttons::C_UP;
		return Inputs(buttons, x, y);
	}

	// The stationary frame: nothing pressed, so Mario stands where he is.
	Inputs NeutralInputs(std::mt19937&, const Inputs&)
	{
		return Inputs();
	}

	struct ParityRun
	{
		int segments = 0;
		int frames = 0;
		int framesOnPyramid = 0;   // Mario's platform is slot 84
		int framesAirborne = 0;
		int segmentsReloaded = 0;  // slot 84 stopped being the pyramid: Mario died and the level reloaded
		int segmentsEndedInLava = 0;
		int segmentsEndedByRefusal = 0; // the refusal the options let end a segment
		int framesCameraTurned = 0; // the camera's yaw is not the anchor's (the resource of 4.13 holds it constant)
		std::string mismatch;      // the first difference and where, or empty
		std::string refusal;       // what the second resource refused to run, and where, or empty
	};

	void NoSetup(std::mt19937&, GameView&, GameView&);

	// The fuzz from the state both resources are in: segments of `segmentFrames` from a
	// save of it, every frame compared, the save loaded back between segments; `setup`
	// writes a segment's start on both sides after the load (the bully's placement).
	template <class TA, class TB, class TInputs, class TSetup = void (*)(std::mt19937&, GameView&, GameView&)>
	ParityRun Fuzz(TA& a, TB& b, int totalFrames, int segmentFrames, std::initializer_list<int> objects, const Options& options, TInputs inputsFor,
		TSetup setup = NoSetup)
	{
		GameView va = GameView::Read(a);
		GameView vb = GameView::Read(b);
		const void* pyramidBehavior = va.pool[84].behavior;
		const int16_t anchorYaw = va.camera->yaw;
		int64_t slotA = a.SaveState();
		int64_t slotB = b.SaveState();
		std::mt19937 rng(20261011u);
		ParityRun run;
		while (run.frames < totalFrames && run.mismatch.empty() && run.refusal.empty())
		{
			setup(rng, va, vb);
			Inputs inputs;
			bool reloaded = false;
			for (int f = 0; f < segmentFrames && run.frames < totalFrames; f++, run.frames++)
			{
				inputs = inputsFor(rng, inputs);
				a.setInputs(inputs);
				a.FrameAdvance();
				b.setInputs(inputs);
				try
				{
					b.FrameAdvance();
				}
				catch (const std::exception& e)
				{
					if (options.refusalEndsSegment != nullptr && std::string(e.what()).find(options.refusalEndsSegment) != std::string::npos)
					{
						run.segmentsEndedByRefusal++;
						run.frames++;
						break;
					}
					char buf[400];
					std::snprintf(buf, sizeof(buf), "segment %d, frame %lld (%d into the segment), inputs 0x%04X (%d, %d), Mario at (%.9g, %.9g, %.9g) in 0x%08X: %s",
						run.segments, (long long)a.getCurrentFrame(), f + 1, unsigned(inputs.buttons), int(inputs.stick_x), int(inputs.stick_y),
						double(va.mario->pos[0]), double(va.mario->pos[1]), double(va.mario->pos[2]), unsigned(va.mario->action), e.what());
					run.refusal = buf;
					break;
				}
				if (va.mario->marioObj != nullptr && va.Slot(va.mario->marioObj->platform) == 84)
					run.framesOnPyramid++;
				if (va.mario->action & ACT_FLAG_AIR)
					run.framesAirborne++;
				if (va.camera->yaw != anchorYaw)
					run.framesCameraTurned++;
				reloaded = reloaded || va.pool[84].behavior != pyramidBehavior;
				std::string diff = Parity::Diff(va, vb, objects, options);
				if (!diff.empty())
				{
					char buf[400];
					std::snprintf(buf, sizeof(buf), "segment %d, frame %lld (%d into the segment), inputs 0x%04X (%d, %d), Mario at (%.9g, %.9g, %.9g) in 0x%08X: %s",
						run.segments, (long long)a.getCurrentFrame(), f + 1, unsigned(inputs.buttons), int(inputs.stick_x), int(inputs.stick_y),
						double(va.mario->pos[0]), double(va.mario->pos[1]), double(va.mario->pos[2]), unsigned(va.mario->action), diff.c_str());
					run.mismatch = buf;
					break;
				}
				// The lava is the search's boundary (a shot that touches it is dead), so it is
				// the fuzz's: compared on the frame it is touched, then the segment ends. Past
				// it the boosts carry Mario across the lava sea, where the whole game has
				// floors the cut one has not (the sinking platforms, 3,000 units from the
				// corner), which the first million-frame run found.
				if (va.mario->action == ACT_LAVA_BOOST)
				{
					run.segmentsEndedInLava++;
					run.frames++;
					break;
				}
			}
			run.segments++;
			run.segmentsReloaded += reloaded;
			a.LoadState(slotA);
			b.LoadState(slotB);
		}
		a.DisposeState(slotA);
		b.DisposeState(slotB);
		return run;
	}

	// Every active object but the listed slots and Mario deactivated the way
	// obj_mark_for_deletion does it; the game unloads them at the end of the next update.
	int UnloadAllBut(LibSm64& resource, std::initializer_list<int> keep)
	{
		GameView v = GameView::Read(resource);
		int unloaded = 0;
		for (int i = 0; i < LibSm64ObjectPoolCapacity; i++)
		{
			Object& o = v.pool[i];
			if (o.activeFlags == 0 || &o == v.mario->marioObj || std::find(keep.begin(), keep.end(), i) != keep.end())
				continue;
			o.activeFlags = 0; // ACTIVE_FLAG_DEACTIVATED
			unloaded++;
		}
		return unloaded;
	}

	int ActiveObjects(LibSm64& resource)
	{
		GameView v = GameView::Read(resource);
		int active = 0;
		for (int i = 0; i < LibSm64ObjectPoolCapacity; i++)
			active += v.pool[i].activeFlags != 0;
		return active;
	}

	// The bully placed on Mario the way the ending's squish probe places it
	// (BitfsScEndingMetrics::ProbeOnce): `behind` units from him, against the direction
	// `yaw` (his facing turned by an offset), moving that way at `speed` in the action
	// given (its knockback, which pushes, or its chase). The same writes on both views.
	constexpr int32_t BullyActChaseMario = 1; // BULLY_ACT_CHASE_MARIO, object_constants.h
	constexpr int32_t BullyActKnockback = 2;  // BULLY_ACT_KNOCKBACK
	void PlaceBully(GameView& v, float behind, int16_t yawOffset, float speed, int32_t action)
	{
		Object& bully = v.pool[27];
		int16_t yaw = int16_t(v.mario->faceAngle[1] + yawOffset);
		float dirX = sins(yaw);
		float dirZ = coss(yaw);
		bully.oPosX = v.mario->pos[0] - dirX * behind;
		bully.oPosY = v.mario->pos[1];
		bully.oPosZ = v.mario->pos[2] - dirZ * behind;
		bully.oBullyPrevX = bully.oPosX;
		bully.oBullyPrevY = bully.oPosY;
		bully.oBullyPrevZ = bully.oPosZ;
		bully.oVelX = 0.0f;
		bully.oVelY = 0.0f;
		bully.oVelZ = 0.0f;
		bully.oForwardVel = speed;
		bully.oMoveAngleYaw = yaw;
		bully.oFaceAngleYaw = yaw;
		bully.oAction = action;
		bully.oBullyKBTimerAndMinionKOCounter = 0;
		bully.oInteractStatus = 0;
		bully.oIntangibleTimer = 0;
	}

	// A segment's placement of the bully, drawn from the fuzz's generator: 40 to 200 units
	// away, up to a quarter turn off Mario's facing either way, at up to the probe's speed,
	// pushing or chasing.
	void PlaceBullyAtRandom(std::mt19937& rng, GameView& a, GameView& b)
	{
		float behind = 40.0f + float(rng() % 161);
		int16_t yawOffset = int16_t(int(rng() % 0x8000) - 0x4000);
		float speed = float(rng() % 101);
		int32_t action = rng() % 2 ? BullyActKnockback : BullyActChaseMario;
		PlaceBully(a, behind, yawOffset, speed, action);
		PlaceBully(b, behind, yawOffset, speed, action);
	}

	void NoSetup(std::mt19937&, GameView&, GameView&) {}

	// The frames the fuzz plays: TASFW_PARITY_FRAMES, or 6,000 (under a second).
	int ParityFrames()
	{
		std::string frames = Env("TASFW_PARITY_FRAMES");
		return frames.empty() ? 6000 : std::stoi(frames);
	}

	constexpr int SegmentFrames = 150;
	constexpr std::initializer_list<int> CornerObjects = {84, 83, 85, 27}; // what the cut keeps and the corner can run
	constexpr std::initializer_list<int> PyramidAlone = {84};			   // a corner asked for the pyramid only

	// The game at the anchor frame.
	struct GameAtAnchor
	{
		LibSm64 game;
		explicit GameAtAnchor(LibSm64Config config) : game(config)
		{
			M64 m64(Env("TASFW_M64"));
			REQUIRE(m64.load() == 1);
			PlayFrames(game, m64, TestFrame());
		}
	};

	// Two copies of the game at the anchor frame.
	struct GamePair
	{
		GameAtAnchor a;
		GameAtAnchor b;
		GamePair() : a(DllConfig(LibSm64SaveMode::Dirty)), b(CopyConfig()) {}

		static LibSm64Config CopyConfig()
		{
			LibSm64Config config = DllConfig(LibSm64SaveMode::Dirty);
			config.dllPath = DllCopyPath(1);
			return config;
		}
	};

	// The game at the anchor, and the corner resource with the state imported from it, with
	// the objects asked for.
	struct GameAndCorner
	{
		GameAtAnchor at;
		std::shared_ptr<const BitfsCornerLevel> level;
		BitfsCorner corner;
		explicit GameAndCorner(BitfsCornerObjects objects)
			: at(DllConfig(LibSm64SaveMode::Dirty)), level(std::make_shared<const BitfsCornerLevel>(at.game)), corner(level)
		{
			corner.load(BitfsCornerMem(at.game, *level, objects));
		}
	};

	void Report(const ParityRun& run)
	{
		MESSAGE("frames: " << run.frames << " in " << run.segments << " segments, on the pyramid " << run.framesOnPyramid
			<< ", airborne " << run.framesAirborne << ", camera turned " << run.framesCameraTurned
			<< ", segments ended in the lava " << run.segmentsEndedInLava << ", by the accepted refusal " << run.segmentsEndedByRefusal
			<< ", with a level reload " << run.segmentsReloaded);
		if (!run.mismatch.empty())
			MESSAGE(run.mismatch);
		if (!run.refusal.empty())
			MESSAGE("refused: " << run.refusal);
		CHECK(run.mismatch.empty());
		CHECK(run.refusal.empty());
	}

	// The exported solution movies as the harness's second input set (ROADMAP 4.13). A
	// movie under TASFW_PARITY_MOVIES is the source movie up to the frame its stage started
	// at and the solution's inputs from there, so the game plays the source once to the
	// earliest such frame among the movies chosen, is cut to the corner's objects there,
	// the corner is imported from it, and each movie's frames from there are played on
	// both from a load of that state and compared after every frame.
	struct MovieRun
	{
		int movies = 0; // played to their end with no difference
		int frames = 0;
		int framesOnPyramid = 0;
		int64_t base = 0; // the frame every movie starts from on both resources
		std::string mismatch;
		std::string refusal;
	};

	// The movies under a directory in sorted order: all of them, or `limit` taken evenly
	// over that order, so that every stage's directory is among them.
	std::vector<std::filesystem::path> SolutionMovies(const std::filesystem::path& directory, size_t limit)
	{
		std::vector<std::filesystem::path> all;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
			if (entry.is_regular_file() && entry.path().extension() == ".m64")
				all.push_back(entry.path());
		std::sort(all.begin(), all.end());
		if (limit == 0 || all.size() <= limit)
			return all;
		std::vector<std::filesystem::path> chosen;
		for (size_t i = 0; i < limit; i++)
			chosen.push_back(all[i * all.size() / limit]);
		return chosen;
	}

	// The first frame at which a movie's inputs differ from the source's, or -1 when none does.
	int64_t FirstDifference(const M64& movie, const M64& source)
	{
		for (const auto& [frame, inputs] : movie.frames)
		{
			auto s = source.frames.find(frame);
			if (!(inputs == (s != source.frames.end() ? s->second : Inputs())))
				return int64_t(frame);
		}
		return -1;
	}

	size_t MovieLimit()
	{
		std::string limit = Env("TASFW_PARITY_MOVIE_LIMIT");
		return limit.empty() ? 40 : size_t(std::stoull(limit));
	}

	template <class TA, class TB>
	MovieRun PlayMovies(TA& a, int64_t slotA, TB& b, int64_t slotB, const std::vector<std::pair<std::filesystem::path, M64>>& movies, int64_t base,
		std::initializer_list<int> objects, const Options& options)
	{
		GameView va = GameView::Read(a);
		GameView vb = GameView::Read(b);
		MovieRun run;
		run.base = base;
		for (const auto& [path, movie] : movies)
		{
			a.LoadState(slotA);
			b.LoadState(slotB);
			const int64_t last = int64_t(movie.frames.rbegin()->first);
			for (int64_t f = base; f <= last; f++)
			{
				auto it = movie.frames.find(uint64_t(f));
				const Inputs inputs = it != movie.frames.end() ? it->second : Inputs();
				a.setInputs(inputs);
				a.FrameAdvance();
				b.setInputs(inputs);
				bool refused = false;
				std::string what;
				try
				{
					b.FrameAdvance();
				}
				catch (const std::exception& e)
				{
					refused = true;
					what = e.what();
				}
				if (!refused)
				{
					run.frames++;
					if (va.mario->marioObj != nullptr && va.Slot(va.mario->marioObj->platform) == 84)
						run.framesOnPyramid++;
					what = Parity::Diff(va, vb, objects, options);
					if (what.empty())
						continue;
				}
				char buf[400];
				std::snprintf(buf, sizeof(buf), "%s, frame %lld (%lld into the movie), inputs 0x%04X (%d, %d), Mario at (%.9g, %.9g, %.9g) in 0x%08X: %s",
					path.filename().string().c_str(), (long long)f, (long long)(f - base), unsigned(inputs.buttons), int(inputs.stick_x), int(inputs.stick_y),
					double(va.mario->pos[0]), double(va.mario->pos[1]), double(va.mario->pos[2]), unsigned(va.mario->action), what.c_str());
				(refused ? run.refusal : run.mismatch) = buf;
				return run;
			}
			run.movies++;
		}
		return run;
	}
	// The movies chosen, loaded, with the earliest frame any of them leaves the source.
	struct MovieSet
	{
		M64 source;
		std::vector<std::pair<std::filesystem::path, M64>> movies;
		int64_t base = -1;
		int skipped = 0; // the source itself, or not a movie
	};

	MovieSet LoadMovieSet()
	{
		MovieSet set;
		set.source = M64(Env("TASFW_M64"));
		REQUIRE(set.source.load() == 1);
		for (const auto& path : SolutionMovies(Env("TASFW_PARITY_MOVIES"), MovieLimit()))
		{
			M64 movie(path);
			int64_t first = movie.load() == 1 && !movie.frames.empty() ? FirstDifference(movie, set.source) : -1;
			if (first < 0)
			{
				set.skipped++;
				continue;
			}
			set.base = set.base < 0 ? first : std::min(set.base, first);
			set.movies.emplace_back(path, std::move(movie));
		}
		MESSAGE("movies: " << set.movies.size() << " under " << Env("TASFW_PARITY_MOVIES") << " (" << set.skipped
			<< " skipped: no difference from the source), every one the source's to frame " << set.base);
		REQUIRE(!set.movies.empty());
		REQUIRE(set.base >= 1);
		return set;
	}

	// Every active object with the behavior named deactivated the way obj_mark_for_deletion
	// does it (the game unloads it at the end of the next update); the slots they had.
	std::vector<int> UnloadBehavior(LibSm64& resource, const char* behavior)
	{
		GameView v = GameView::Read(resource);
		const void* script = resource.addr(behavior);
		std::vector<int> slots;
		for (int i = 0; i < LibSm64ObjectPoolCapacity; i++)
		{
			Object& o = v.pool[i];
			if (o.activeFlags == 0 || static_cast<const void*>(o.behavior) != script)
				continue;
			o.activeFlags = 0; // ACTIVE_FLAG_DEACTIVATED
			slots.push_back(i);
		}
		return slots;
	}

	void ReportMovies(const MovieRun& run, size_t movies)
	{
		MESSAGE("played " << run.movies << " of " << movies << " movies to their end, " << run.frames << " frames from " << run.base
			<< ", on the pyramid " << run.framesOnPyramid);
		if (!run.mismatch.empty())
			MESSAGE(run.mismatch);
		if (!run.refusal.empty())
			MESSAGE("refused: " << run.refusal);
		CHECK(run.mismatch.empty());
		CHECK(run.refusal.empty());
		CHECK(run.movies == int(movies));
	}
}

TEST_CASE("BitfsCorner parity: the corner agrees with the game on the exported solution movies"
	* doctest::skip(!HaveDll() || Env("TASFW_PARITY_MOVIES").empty()))
{
	MovieSet set = LoadMovieSet();

	// The game to the frame before the base, cut to the corner's objects over that frame
	// (the game removes a deactivated object at the end of the next update; every movie
	// still follows the source through it), the corner imported at the base, a save of
	// each. Cut, because the way onto the pyramid passes a yellow coin (slot 23, on the
	// pyramid's top; the fixer's movies collect it at frame 3277), which the corner has not
	// got: on the whole game Mario's coin count and the heal a coin gives would differ from
	// the corner's from there on. The next case shows that coin is all the whole game adds
	// along the movies; the fuzz pairs start on the pyramid, after the source movie has
	// collected it, and compare the whole game.
	LibSm64 game(DllConfig(LibSm64SaveMode::Dirty));
	PlayFrames(game, set.source, set.base - 1);
	int unloaded = UnloadAllBut(game, CornerObjects);
	PlayFrames(game, set.source, 1);
	MESSAGE("the game cut to the corner's objects at frame " << set.base << ": " << ActiveObjects(game) << " objects (" << unloaded << " unloaded)");
	auto level = std::make_shared<const BitfsCornerLevel>(game);
	BitfsCorner corner(level);
	corner.load(BitfsCornerMem(game, *level, BitfsCornerObjects::All()));
	int64_t slotA = game.SaveState();
	int64_t slotB = corner.SaveState();
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	MovieRun run = PlayMovies(game, slotA, corner, slotB, set.movies, set.base, CornerObjects, options);
	game.DisposeState(slotA);
	corner.DisposeState(slotB);
	ReportMovies(run, set.movies.size());
}

TEST_CASE("libsm64 parity: the whole game less its yellow coins agrees with the game cut to the corner's objects on the exported solution movies"
	* doctest::skip(!HaveDllCopy(1) || Env("TASFW_PARITY_MOVIES").empty()))
{
	// What the whole game has beyond the corner's objects along the solutions' paths: with
	// its yellow coins gone (slot 23 among them, the one the corner case found Mario
	// collecting), it and the cut game must agree frame for frame, or another object
	// touches Mario on the way and the difference names it. Everything is compared here,
	// the camera and the particles included: both are the game.
	MovieSet set = LoadMovieSet();
	LibSm64 whole(DllConfig(LibSm64SaveMode::Dirty));
	LibSm64 cut(GamePair::CopyConfig());
	PlayFrames(whole, set.source, set.base - 1);
	PlayFrames(cut, set.source, set.base - 1);
	std::vector<int> coins = UnloadBehavior(whole, "bhvYellowCoin");
	int unloaded = UnloadAllBut(cut, CornerObjects);
	PlayFrames(whole, set.source, 1);
	PlayFrames(cut, set.source, 1);
	std::string slots;
	for (int slot : coins)
		slots += (slots.empty() ? "" : ", ") + std::to_string(slot);
	MESSAGE("at frame " << set.base << ": the whole game " << ActiveObjects(whole) << " objects, its " << coins.size()
		<< " yellow coin(s) unloaded (slot " << slots << "); the cut game " << ActiveObjects(cut) << " (" << unloaded << " unloaded)");
	CHECK(!coins.empty());
	int64_t slotA = whole.SaveState();
	int64_t slotB = cut.SaveState();
	MovieRun run = PlayMovies(whole, slotA, cut, slotB, set.movies, set.base, CornerObjects, Options{});
	whole.DisposeState(slotA);
	cut.DisposeState(slotB);
	ReportMovies(run, set.movies.size());
}

TEST_CASE("libsm64 parity: two copies of the game agree under the fuzz"
	* doctest::skip(!HaveDllCopy(1)))
{
	GamePair games;
	ParityRun run = Fuzz(games.a.game, games.b.game, ParityFrames(), SegmentFrames, CornerObjects, Options{}, FuzzInputs);
	Report(run);
	CHECK(run.framesOnPyramid > 0);
	CHECK(run.framesAirborne > 0);
}

TEST_CASE("libsm64 parity: the game with only the pyramid corner's objects agrees with the whole game"
	* doctest::skip(!HaveDllCopy(1)))
{
	GamePair games;
	int unloaded = UnloadAllBut(games.b.game, CornerObjects);
	CHECK(unloaded > 0);
	// The unload frame, on both: the game removes the deactivated objects at its end.
	games.a.game.setInputs(Inputs());
	games.a.game.FrameAdvance();
	games.b.game.setInputs(Inputs());
	games.b.game.FrameAdvance();
	MESSAGE("objects: " << ActiveObjects(games.a.game) << " in the whole game, " << ActiveObjects(games.b.game) << " in the cut one (" << unloaded << " unloaded)");
	CHECK(ActiveObjects(games.b.game) == int(CornerObjects.size()) + 1);
	ParityRun run = Fuzz(games.a.game, games.b.game, ParityFrames(), SegmentFrames, CornerObjects, Options{}, FuzzInputs);
	Report(run);
	CHECK(run.framesOnPyramid > 0);
	CHECK(run.framesAirborne > 0);
}

TEST_CASE("BitfsCorner parity: the corner agrees with the game under neutral inputs"
	* doctest::skip(!HaveDll()))
{
	GameAndCorner pair(BitfsCornerObjects::All());
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	Report(Fuzz(pair.at.game, pair.corner, ParityFrames(), SegmentFrames, CornerObjects, options, NeutralInputs));
}

TEST_CASE("BitfsCorner parity: the corner agrees with the game under the fuzz"
	* doctest::skip(!HaveDll()))
{
	GameAndCorner pair(BitfsCornerObjects::All());
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	ParityRun run = Fuzz(pair.at.game, pair.corner, ParityFrames(), SegmentFrames, CornerObjects, options, FuzzInputs);
	Report(run);
	CHECK(run.framesOnPyramid > 0);
}

TEST_CASE("BitfsCorner parity: the corner with the pyramid alone agrees with the game cut to the pyramid"
	* doctest::skip(!HaveDll()))
{
	// The other three objects left out of the corner and unloaded from the game: the two
	// must agree under the fuzz. (Against the whole game they would not, and rightly: the
	// boosts carry Mario to the other pyramid, which one side has and the other has not.)
	BitfsCornerObjects objects;
	objects.pyramid = true;
	GameAndCorner pair(objects);
	int unloaded = UnloadAllBut(pair.at.game, PyramidAlone);
	CHECK(unloaded > 0);
	// The unload frame on both: the game removes its objects at the end of it.
	pair.at.game.setInputs(Inputs());
	pair.at.game.FrameAdvance();
	pair.corner.setInputs(Inputs());
	pair.corner.FrameAdvance();
	CHECK(ActiveObjects(pair.at.game) == 2);
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	ParityRun run = Fuzz(pair.at.game, pair.corner, ParityFrames(), SegmentFrames, PyramidAlone, options, FuzzInputs);
	Report(run);
	CHECK(run.framesOnPyramid > 0);
}

TEST_CASE("BitfsCorner parity: the corner agrees with the game with the bully on Mario, under neutral inputs"
	* doctest::skip(!HaveDll()))
{
	// The bully placed as the ending's probe places it, every segment somewhere else: its
	// push and what follows, or its chase, Mario doing nothing himself. The bully's death
	// (into the lava) is what the corner refuses, by design, and ends a segment.
	GameAndCorner pair(BitfsCornerObjects::All());
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	options.refusalEndsSegment = "spawning";
	ParityRun run = Fuzz(pair.at.game, pair.corner, ParityFrames(), SegmentFrames, CornerObjects, options, NeutralInputs, PlaceBullyAtRandom);
	Report(run);
}

TEST_CASE("BitfsCorner parity: the corner agrees with the game with the bully on Mario, under the fuzz"
	* doctest::skip(!HaveDll()))
{
	GameAndCorner pair(BitfsCornerObjects::All());
	Options options;
	options.cameraYawOnly = true;
	options.particles = false;
	options.unmodelledAbsent = true;
	options.refusalEndsSegment = "spawning";
	ParityRun run = Fuzz(pair.at.game, pair.corner, ParityFrames(), SegmentFrames, CornerObjects, options, FuzzInputs, PlaceBullyAtRandom);
	Report(run);
}
