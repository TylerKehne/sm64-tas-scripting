#include <doctest/doctest.h>
#include <LibSm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>

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
	std::string Env(const char* name)
	{
		const char* v = std::getenv(name);
		return v ? std::string(v) : std::string();
	}

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
