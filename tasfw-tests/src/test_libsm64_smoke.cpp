#include <doctest/doctest.h>
#include <BitFsObjects.hpp>
#include <VerifyLayout.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include "libsm64_env.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Smoke test against the real game DLL (libsm64_env.hpp says when it runs and what it plays).
// What it proves: the DLL loads, the layout self-check passes there, and playing the movie
// twice from power-on gives bit-identical Mario state (savestate/advance/input plumbing is
// deterministic end to end). The golden values pin the pinned DLL + movie combination;
// update them deliberately, never to make the test pass.

using namespace tasfw::tests;

namespace
{
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

			MarioState* m = *(MarioState**)(ReadState("gMarioState"));
			Object* pyramid = &((Object*)(ReadState("gObjectPool")))[84];
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
	LibSm64Config config = DllConfig(LibSm64SaveMode::Fixed); // the golden values and the slice coverage checks live here
	int64_t frame = TestFrame();

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

	// The movie and the DLL must be the same game (the DLL's from its name, the movie's from
	// its header); the other pairing desyncs without a word.
	CHECK(resource.CheckMovie(m64).empty());
	M64 other = m64;
	other.metadata.countryCode = config.countryCode == CountryCode::SUPER_MARIO_64_J ? CountryCode::SUPER_MARIO_64_U : CountryCode::SUPER_MARIO_64_J;
	std::string mismatch = resource.CheckMovie(other);
	CHECK(mismatch.find(LibSm64::VersionName(other.metadata.countryCode)) != std::string::npos);
	CHECK(mismatch.find(LibSm64::VersionName(config.countryCode)) != std::string::npos);

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

	// Golden values for the pinned DLL + movies/bitfs-pyramid-jp.m64 at frame 3330, captured
	// 2026-09-07 (%.9g round-trips a float exactly); the bitfs-sbb builds CI unlocks reach
	// the same state (docs/libsm64.md, "Known builds"). Any change to the movie, the DLL, or
	// the engine's input/savestate plumbing before that frame shows up here. The US DLL with
	// movies/bitfs-pyramid-us.m64 reaches the very same state at frame 3397: that movie is the
	// JP one from its BitFS entry spliced onto a US movie that enters the level 67 frames
	// later, and the level plays the same on both versions (docs/libsm64.md, "A movie for
	// the US game").
	std::string movie = Env("TASFW_M64");
	bool goldenJp = frame == 3330 && movie.find("bitfs-pyramid-jp") != std::string::npos;
	bool goldenUs = frame == 3397 && movie.find("bitfs-pyramid-us") != std::string::npos;
	if (goldenJp || goldenUs)
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
