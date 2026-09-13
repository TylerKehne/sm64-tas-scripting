#pragma once
#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/testing/Env.hpp>

#include <cstdint>
#include <string>

// Shared by the libsm64 tests (test_libsm64_*.cpp). They run against the real game DLL, and
// only when the environment names a DLL and a movie (scripts/test.ps1 sets these from res/
// when present):
//   TASFW_LIBSM64 = path to sm64_jp_N.dll     TASFW_M64 = path to the source movie
//   TASFW_FRAME   = frame inside a level (default 3330)
namespace tasfw::tests
{
	using tasfw::testing::Env;

	inline bool HaveDll()
	{
		return !Env("TASFW_LIBSM64").empty() && !Env("TASFW_M64").empty();
	}

	// The DLL the environment names, as the JP game in the given save mode.
	inline LibSm64Config DllConfig(LibSm64SaveMode mode)
	{
		LibSm64Config config;
		config.dllPath = Env("TASFW_LIBSM64");
		config.countryCode = CountryCode::SUPER_MARIO_64_J;
		config.saveMode = mode;
		return config;
	}

	// The frame the tests play the movie to: TASFW_FRAME, or 3330, which is inside the level in
	// the committed movie.
	inline int64_t TestFrame()
	{
		std::string frame = Env("TASFW_FRAME");
		return frame.empty() ? 3330 : std::stoll(frame);
	}

	// A test's own loop to a frame, for the tests whose subject is the resource's savestates:
	// they drive the resource directly instead of through a script (AGENTS.md, hard rule 9).
	inline void PlayFrames(LibSm64& resource, const M64& m64, int64_t frames)
	{
		for (int64_t f = 0; f < frames; f++)
		{
			auto inputs = m64.frames.find(uint64_t(resource.getCurrentFrame()));
			resource.setInputs(inputs != m64.frames.end() ? inputs->second : Inputs());
			resource.FrameAdvance();
		}
	}
}
