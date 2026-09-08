#include <doctest/doctest.h>
#include <LibSm64.hpp>
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
		std::vector<std::string> report;
	};

	class PlayAndSnapshot : public TopLevelScript<LibSm64>
	{
	public:
		PlayAndSnapshot(int64_t frame, SmokeResults& results) : _frame(frame), _results(results) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_frame);
			_results.report = resource->layoutCheckReport();

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
			s.frame = resource->getCurrentFrame();
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
	config.lightweight = true;
	int64_t frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));

	LibSm64 resource(config);
	M64 m64(Env("TASFW_M64"));
	REQUIRE(m64.load() == 1);

	SmokeResults first = Play(resource, m64, frame);
	for (const std::string& line : first.report)
	{
		CAPTURE(line);
		CHECK(line.rfind("FAIL: ", 0) != 0);
	}
	CHECK(first.snapshot.frame == uint32_t(frame));

	// Second play: ImportResource resets to the start save and replays from power-on.
	SmokeResults second = Play(resource, m64, frame);
	CHECK(first.snapshot == second.snapshot);

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

TEST_CASE("libsm64: PyramidUpdate reproduces the DLL's pyramid normal bit-for-bit"
	* doctest::skip(!HaveDll()))
{
	LibSm64Config config;
	config.dllPath = Env("TASFW_LIBSM64");
	config.countryCode = CountryCode::SUPER_MARIO_64_J;
	config.lightweight = true;
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
