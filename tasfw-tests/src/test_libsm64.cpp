#include <doctest/doctest.h>
#include <BitFsObjects.hpp>
#include <LibSm64.hpp>
#include <VerifyLayout.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <tasfw/testing/Env.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Smoke test against the real game DLL. Runs only when the environment names a DLL and a
// movie (scripts/test.ps1 sets these from res/ when present):
//   TASFW_LIBSM64 = path to sm64_jp_N.dll     TASFW_M64 = path to the source movie
//   TASFW_FRAME   = frame inside a level (default 3330)
// What it proves: the DLL loads, the layout self-check passes there, and playing the movie
// twice from power-on gives bit-identical Mario state (savestate/advance/input plumbing is
// deterministic end to end). The golden values pin the pinned DLL + movie combination;
// update them deliberately, never to make the test pass.

namespace
{
	using tasfw::testing::Env;

	bool HaveDll()
	{
		return !Env("TASFW_LIBSM64").empty() && !Env("TASFW_M64").empty();
	}

	struct MarioSnapshot
	{
		float pos[3];
		float vel[3];
		float forwardVel;
		uint32_t action;
		int16_t faceYaw;
		float pyramidNormal[3];
		uint32_t frame;

		bool operator==(const MarioSnapshot&) const = default;
	};

	struct SmokeResults
	{
		MarioSnapshot snapshot {};
	};

	class PlayAndSnapshot : public TopLevelScript<LibSm64>
	{
	public:
		PlayAndSnapshot(int64_t frame, SmokeResults& results) : _frame(frame), _results(results) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_frame);

			MarioState* m = *(MarioState**)(resource->addr("gMarioState"));
			Object* pyramid = &((Object*)(resource->addr("gObjectPool")))[84];
			MarioSnapshot& s = _results.snapshot;
			for (int i = 0; i < 3; i++)
			{
				s.pos[i] = m->pos[i];
				s.vel[i] = m->vel[i];
			}
			s.forwardVel = m->forwardVel;
			s.action = m->action;
			s.faceYaw = m->faceAngle[1];
			s.pyramidNormal[0] = pyramid->oTiltingPyramidNormalX;
			s.pyramidNormal[1] = pyramid->oTiltingPyramidNormalY;
			s.pyramidNormal[2] = pyramid->oTiltingPyramidNormalZ;
			s.frame = uint32_t(GetCurrentFrame());
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _frame;
		SmokeResults& _results;
	};

	SmokeResults Play(LibSm64& resource, M64& m64, int64_t frame)
	{
		SmokeResults results;
		TopLevelScriptBuilder<PlayAndSnapshot>::Build(m64).ImportResource(&resource).Run(frame, results);
		return results;
	}
}

TEST_CASE("libsm64: loads, passes the layout check, and plays the movie deterministically"
	* doctest::skip(!HaveDll()))
{
	LibSm64Config config;
	config.dllPath = Env("TASFW_LIBSM64");
	config.countryCode = CountryCode::SUPER_MARIO_64_J;
	config.saveMode = LibSm64SaveMode::Fixed; // the golden values and the slice coverage checks live here
	int64_t frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));

	// A build whose sections are smaller than the pinned DLL's (the Linux .so) refuses the
	// fixed slices at construction; the smoke test then runs in the dirty mode instead.
	std::unique_ptr<LibSm64> owned;
	try
	{
		owned = std::make_unique<LibSm64>(config);
	}
	catch (const std::runtime_error& e)
	{
		MESSAGE("fixed save mode unavailable on this build, running the smoke test in dirty mode: " << std::string(e.what()));
		config.saveMode = LibSm64SaveMode::Dirty;
		owned = std::make_unique<LibSm64>(config);
	}
	LibSm64& resource = *owned;
	M64 m64(Env("TASFW_M64"));
	REQUIRE(m64.load() == 1);

	SmokeResults first = Play(resource, m64, frame);
	CHECK(first.snapshot.frame == uint32_t(frame));

	// Second play: ImportResource resets to the start save and replays from power-on.
	SmokeResults second = Play(resource, m64, frame);
	CHECK(first.snapshot == second.snapshot);

	// The layout and the slots the scripts hardcode (ROADMAP 1.1, 2.4), through the script the
	// pipeline runs before its first stage: BitFsObjects.hpp must hold, and the script must fail
	// for a wrong home (the far pyramid's on slot 84), a wrong behavior (the track platform's on
	// slot 83) and an empty slot.
	auto layout = TopLevelScriptBuilder<VerifyLayout>::Build(m64).ImportResource(&resource).Run(frame, BitFsExpectedObjects);
	CHECK(layout.asserted);
	CHECK(layout.failures == 0);
	for (const std::string& line : layout.lines)
	{
		CAPTURE(line);
		CHECK(line.rfind("FAIL: ", 0) != 0);
	}
	auto has = [&](const std::vector<std::string>& lines, const char* prefix)
	{
		return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; });
	};
	CHECK(has(layout.lines, "ok: gMarioObject is gObjectPool["));
	CHECK(has(layout.lines, "ok: gObjectPool[84] is bhvBitfsTiltingInvertedPyramid at home (-1945, -3225, -715)"));
	CHECK(has(layout.lines, "ok: gObjectPool[83] is bhvBitfsTiltingInvertedPyramid at home (-2866, -3225, -715)"));
	CHECK(has(layout.lines, "ok: gObjectPool[85] is bhvPlatformOnTrack at home (-5744, -3072, 0)"));

	auto wrong = TopLevelScriptBuilder<VerifyLayout>::Build(m64).ImportResource(&resource).Run(frame, std::vector<ExpectedObject> {
		{84, "bhvBitfsTiltingInvertedPyramid", -2866.0f, -3225.0f, -715.0f},
		{83, "bhvPlatformOnTrack", -5744.0f, -3072.0f, 0.0f},
		{239, "bhvBitfsTiltingInvertedPyramid", 0.0f, 0.0f, 0.0f, false},
	});
	CHECK_FALSE(wrong.asserted);
	CHECK(wrong.failures == 3);
	std::vector<std::string> fails;
	for (const std::string& line : wrong.lines)
		if (line.rfind("FAIL: ", 0) == 0)
			fails.push_back(line);
	REQUIRE(fails.size() == 3);
	CHECK(fails[0].find("FAIL: expected gObjectPool[84]") == 0);
	CHECK(fails[0].find("home is (-1945, -3225, -715)") != std::string::npos);
	CHECK(fails[1].find("FAIL: expected gObjectPool[83]") == 0);
	CHECK(fails[1].find("different behavior") != std::string::npos);
	CHECK(fails[2].find("FAIL: expected gObjectPool[239]") == 0);
	CHECK(fails[2].find("inactive") != std::string::npos);

	char line[512];
	std::snprintf(line, sizeof(line),
		"frame %u pos=(%.9g, %.9g, %.9g) vel=(%.9g, %.9g, %.9g) fwd=%.9g action=0x%08X yaw=%d pyramid=(%.9g, %.9g, %.9g)",
		first.snapshot.frame, first.snapshot.pos[0], first.snapshot.pos[1], first.snapshot.pos[2],
		first.snapshot.vel[0], first.snapshot.vel[1], first.snapshot.vel[2], first.snapshot.forwardVel,
		first.snapshot.action, int(first.snapshot.faceYaw),
		first.snapshot.pyramidNormal[0], first.snapshot.pyramidNormal[1], first.snapshot.pyramidNormal[2]);
	MESSAGE(line);

	// Golden values for res/sm64_jp_0.dll + res/comissonPyra2-Fanart_x-Z.m64 at frame 3330,
	// captured 2026-09-07 (%.9g round-trips a float exactly). Any change to the movie, the
	// DLL, or the engine's input/savestate plumbing before that frame shows up here.
	if (frame == 3330 && Env("TASFW_M64").find("comissonPyra2-Fanart_x-Z") != std::string::npos)
	{
		// Exact: the movie is deterministic and FP contraction is off on every compiler.
		CHECK(first.snapshot.action == 0x0C400201u); // ACT_IDLE
		CHECK(first.snapshot.faceYaw == -13031);
		CHECK(first.snapshot.forwardVel == 0.0f);
		CHECK(first.snapshot.pos[0] == -2076.76392f);
		CHECK(first.snapshot.pos[1] == -2976.61865f);
		CHECK(first.snapshot.pos[2] == -558.218628f);
		CHECK(first.snapshot.pyramidNormal[0] == -0.243864223f);
		CHECK(first.snapshot.pyramidNormal[1] == 0.92538321f);
		CHECK(first.snapshot.pyramidNormal[2] == 0.290165693f);
	}
}

// ---------------------------------------------------------------------------------------
// ROADMAP 3.3: PyramidUpdate (the C++ reimplementation of the tilting pyramid used to probe
// angles without paying for a game frame) must produce the DLL's normal bit-for-bit.
//
// Order within a game frame matters: terrain objects (the pyramid) update before the player,
// so the pyramid loop reads gMarioObject->oPos and ->platform as Mario left them at the end
// of the previous frame, steps the normal 0.01 toward its goal, and displaces
// gMarioState->pos before Mario's own update runs. So importing the whole state before the
// frame and advancing both sides is the exact model (first attempt fed the port Mario's
// post-frame position and mismatched on frame one; that is how this was learned).

#include <PyramidUpdate.hpp>

namespace
{
	struct DriftResults
	{
		int framesCompared = 0;
		int framesWithMotion = 0;
		int firstMismatchFrame = -1;
		float maxAbsDiff = 0.0f;
		std::string firstMismatch;
		std::string aborted;
	};

	class PyramidDrift : public TopLevelScript<LibSm64>
	{
	public:
		PyramidDrift(int64_t startFrame, int frames, DriftResults& results)
			: _startFrame(startFrame), _frames(frames), _results(results) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_startFrame);
			Object* pyramid = &((Object*)(resource->addr("gObjectPool")))[84];
			// BitFS uses bhvBitfsTiltingInvertedPyramid; bhvLllTiltingInvertedPyramid is the LLL one.
			// Newer builds export it as bhvBitFSTiltingInvertedPyramid; LibSm64::addr maps between them.
			const void* pyramidBehavior = resource->addr("bhvBitfsTiltingInvertedPyramid");
			MarioState* mario = *(MarioState**)(resource->addr("gMarioState"));
			Camera* camera = *(Camera**)(resource->addr("gCamera"));
			PyramidUpdate pu;
			int framesOffInARow = 0;

			for (int i = 0; i < _frames; i++)
			{
				float nx0 = pyramid->oTiltingPyramidNormalX;
				float ny0 = pyramid->oTiltingPyramidNormalY;
				float nz0 = pyramid->oTiltingPyramidNormalZ;

				// Everything the pyramid loop will read this frame, as it stands before the frame.
				PyramidUpdateMem mem(*resource, pyramid);

				// Walk toward the pyramid's centre for a while (the tilt changes every frame and
				// the platform levels out under Mario, so he cannot slide off), then hold still.
				Inputs inputs(0, 0, 0);
				if (i < 40)
				{
					int16_t yawToCentre = atan2s(pyramid->oPosZ - mario->pos[2], pyramid->oPosX - mario->pos[0]);
					auto stick = Inputs::GetClosestInputByYawHau(yawToCentre, 8.0f, camera->yaw);
					inputs = Inputs(0, stick.first, stick.second);
				}
				AdvanceFrameWrite(inputs);

				// If Mario ever dies the level reloads and slot 84 stops being the pyramid; the
				// surface loader would then read garbage. Abort with a message instead.
				if (pyramid->behavior != pyramidBehavior || mario->marioObj == nullptr)
				{
					_results.aborted = "object pool slot 84 is no longer the pyramid at frame " + std::to_string(GetCurrentFrame());
					break;
				}
				if (mario->marioObj->platform == pyramid)
					framesOffInARow = 0;
				else if (++framesOffInARow > 10)
				{
					_results.aborted = "Mario left the platform around frame " + std::to_string(GetCurrentFrame());
					break;
				}

				pu.load(mem);
				pu.advance();
				auto* sim = (PyramidUpdateMem::Sm64Object*)pu.addr("Pyramid");

				float gx = pyramid->oTiltingPyramidNormalX;
				float gy = pyramid->oTiltingPyramidNormalY;
				float gz = pyramid->oTiltingPyramidNormalZ;
				_results.framesCompared++;
				if (gx != nx0 || gy != ny0 || gz != nz0)
					_results.framesWithMotion++;

				float diff = std::max({std::fabs(gx - sim->tiltingPyramidNormalX), std::fabs(gy - sim->tiltingPyramidNormalY), std::fabs(gz - sim->tiltingPyramidNormalZ)});
				_results.maxAbsDiff = std::max(_results.maxAbsDiff, diff);
				if (diff != 0.0f && _results.firstMismatchFrame < 0)
				{
					char buf[256];
					std::snprintf(buf, sizeof(buf), "frame %lld: game (%.9g, %.9g, %.9g) sim (%.9g, %.9g, %.9g)",
						(long long)GetCurrentFrame(), gx, gy, gz, sim->tiltingPyramidNormalX, sim->tiltingPyramidNormalY, sim->tiltingPyramidNormalZ);
					_results.firstMismatchFrame = int(GetCurrentFrame());
					_results.firstMismatch = buf;
				}
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _startFrame;
		int _frames;
		DriftResults& _results;
	};
}

namespace
{
	// Every save mode must bring the whole .data and .bss back byte for byte from a load, for
	// states saved before and after other saves and for pages first written after the save. In
	// Dirty mode this crosses the baseline the resource took at the first slot of the run and
	// restores pages from the baseline snapshot.
	struct SaveModeResults
	{
		size_t s0Diff = 0, s1Diff = 0, s2Diff = 0, replayDiff = 0; // bytes of .data+.bss that differ after the load
		int baselineAtS1 = -1;
		size_t pagesInS0 = 0, pagesInS1 = 0;
		uint64_t faults = 0;
	};

	// The test's own loop to a frame: its subject is the resource's save modes, so it drives the
	// resource directly instead of through a script (AGENTS.md, hard rule 9).
	void PlayFrames(LibSm64& resource, const M64& m64, int64_t frames)
	{
		for (int64_t f = 0; f < frames; f++)
		{
			auto inputs = m64.frames.find(uint64_t(resource.getCurrentFrame()));
			resource.setInputs(inputs != m64.frames.end() ? inputs->second : Inputs());
			resource.FrameAdvance();
		}
	}

	SaveModeResults SaveModeRoundTrip(LibSm64& resource, const M64& m64, int64_t frame)
	{
		SaveModeResults results;
		PlayFrames(resource, m64, frame);

		auto sections = [&]()
		{
			std::vector<uint8_t> all;
			for (const SegVal& seg : resource.segment)
			{
				const uint8_t* begin = static_cast<const uint8_t*>(seg.address);
				all.insert(all.end(), begin, begin + seg.length);
			}
			return all;
		};
		auto play = [&](int frames, int variant)
		{
			for (int i = 0; i < frames; i++)
			{
				double angle = i * (variant == 0 ? 0.37 : 0.61);
				resource.setInputs(Inputs(uint16_t(i % 7 == 0 ? 0x8000 : 0), int8_t(60.0 * std::cos(angle)), int8_t(60.0 * std::sin(angle))));
				resource.FrameAdvance();
			}
		};
		auto differing = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
		{
			size_t n = 0;
			for (size_t i = 0; i < a.size() && i < b.size(); i++)
				n += a[i] != b[i];
			return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
		};
		auto pagesOf = [&](int64_t slot)
		{
			return resource.slotManager.slotsById.at(slot).pages.size() / size_t(pagesize);
		};

		// s0 is the first slot of this run, so in Dirty mode it is where the resource takes its
		// baseline (as it would at LongLoad's slot in a script).
		std::vector<uint8_t> snap0 = sections();
		int64_t s0 = resource.SaveState();
		play(60, 0);
		std::vector<uint8_t> snap1 = sections();
		int64_t s1 = resource.SaveState();
		play(60, 1);
		std::vector<uint8_t> snap2 = sections();
		int64_t s2 = resource.SaveState();

		if (const LibSm64DirtyPages* d = resource.dirtyPages())
		{
			results.baselineAtS1 = d->baseline;
			results.faults = d->faults;
			results.pagesInS0 = pagesOf(s0);
			results.pagesInS1 = pagesOf(s1);
		}

		resource.LoadState(s0);
		results.s0Diff = differing(sections(), snap0);
		resource.LoadState(s2);
		results.s2Diff = differing(sections(), snap2);
		resource.LoadState(s1);
		results.s1Diff = differing(sections(), snap1);
		play(60, 1); // the same inputs from the same state reach the same memory
		results.replayDiff = differing(sections(), snap2);

		resource.slotManager.EraseSlot(s0);
		resource.slotManager.EraseSlot(s1);
		resource.slotManager.EraseSlot(s2);
		return results;
	}
}

TEST_CASE("libsm64: every save mode restores .data and .bss exactly, including pages written after the save"
	* doctest::skip(!HaveDll()))
{
	// Dirty first, then the others in the same test case: on Linux that is where doctest's own
	// SIGSEGV handler (installed per test case) and the resource's have to coexist.
	for (LibSm64SaveMode mode : {LibSm64SaveMode::Dirty, LibSm64SaveMode::Fixed, LibSm64SaveMode::Full})
	{
		std::string modeName = LibSm64SaveModeName(mode);
		CAPTURE(modeName);
		LibSm64Config config;
		config.dllPath = Env("TASFW_LIBSM64");
		config.countryCode = CountryCode::SUPER_MARIO_64_J;
		config.saveMode = mode;
		int64_t frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));

		std::unique_ptr<LibSm64> owned;
		try
		{
			owned = std::make_unique<LibSm64>(config);
		}
		catch (const std::runtime_error& e)
		{
			// The fixed slices fit the pinned build only; a build with smaller sections refuses
			// them at construction, which is the intended answer, not a failure of this test.
			REQUIRE(mode == LibSm64SaveMode::Fixed);
			MESSAGE("fixed save mode unavailable on this build: " << std::string(e.what()));
			continue;
		}
		LibSm64& resource = *owned;
		M64 m64(Env("TASFW_M64"));
		REQUIRE(m64.load() == 1);

		SaveModeResults results = SaveModeRoundTrip(resource, m64, frame);
		// Full and Dirty restore everything. Fixed misses a handful of bytes by design: the
		// lava texture-scroll offsets outside its slices, which nothing in the physics reads
		// (docs/libsm64.md, --leak-scan): 4 to 6 bytes after a load, about 20 after a replay
		// through them, since they accumulate. That is the price of its constant cost.
		size_t tolerance = mode == LibSm64SaveMode::Fixed ? 32 : 0;
		CHECK(results.s0Diff <= tolerance);
		CHECK(results.s1Diff <= tolerance);
		CHECK(results.s2Diff <= tolerance);
		CHECK(results.replayDiff <= tolerance);
		if (mode == LibSm64SaveMode::Fixed)
			MESSAGE("fixed slices: bytes not restored after loading s0 / s1 / s2 / replay: " << results.s0Diff << " / " << results.s1Diff
				<< " / " << results.s2Diff << " / " << results.replayDiff);
		if (mode == LibSm64SaveMode::Dirty)
		{
			// Baseline 0 is the start save's; LongLoad's slot at the frame moved it to 1, so the
			// saves of this run copy only what the run wrote (docs/performance.md, "What the game
			// writes"): well under the 1,464 KB the fixed slices copy.
			CHECK(results.baselineAtS1 == 1);
			CHECK(results.faults > 0);
			CHECK(results.pagesInS1 > results.pagesInS0);
			CHECK(results.pagesInS1 > 0);
			CHECK(results.pagesInS1 < 200);
			MESSAGE("dirty pages: " << results.pagesInS0 << " at the first save of the run, " << results.pagesInS1
				<< " after 60 frames; " << results.faults << " first writes recorded in all baselines");
		}
	}
}

TEST_CASE("libsm64: PyramidUpdate reproduces the DLL's pyramid normal bit-for-bit"
	* doctest::skip(!HaveDll()))
{
	LibSm64Config config;
	config.dllPath = Env("TASFW_LIBSM64");
	config.countryCode = CountryCode::SUPER_MARIO_64_J;
	config.saveMode = LibSm64SaveMode::Dirty;
	int64_t frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));

	LibSm64 resource(config);
	M64 m64(Env("TASFW_M64"));
	REQUIRE(m64.load() == 1);

	DriftResults results;
	TopLevelScriptBuilder<PyramidDrift>::Build(m64).ImportResource(&resource).Run(frame, 240, results);

	MESSAGE("frames compared: " << results.framesCompared << ", frames where the normal moved: " << results.framesWithMotion
		<< ", max |diff|: " << results.maxAbsDiff);
	CHECK(results.aborted.empty());
	if (!results.aborted.empty())
		MESSAGE(results.aborted);
	CHECK(results.framesCompared == 240);
	CHECK(results.framesWithMotion > 30); // the walk must have tilted the platform
	if (results.firstMismatchFrame >= 0)
		MESSAGE("first mismatch: " << results.firstMismatch);
	CHECK(results.firstMismatchFrame == -1);
}
