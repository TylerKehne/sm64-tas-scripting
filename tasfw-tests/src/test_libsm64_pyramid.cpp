#include <doctest/doctest.h>
#include <PyramidUpdate.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include "libsm64_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// ROADMAP 3.3: PyramidUpdate (the C++ reimplementation of the tilting pyramid used to probe
// angles without paying for a game frame) must produce the DLL's normal bit-for-bit. Runs
// against the real game DLL (libsm64_env.hpp says when and what it plays).
//
// Order within a game frame matters: terrain objects (the pyramid) update before the player,
// so the pyramid loop reads gMarioObject->oPos and ->platform as Mario left them at the end
// of the previous frame, steps the normal 0.01 toward its goal, and displaces
// gMarioState->pos before Mario's own update runs. So importing the whole state before the
// frame and advancing both sides is the exact model (first attempt fed the port Mario's
// post-frame position and mismatched on frame one; that is how this was learned).

using namespace tasfw::tests;

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
	LibSm64Config config = DllConfig(LibSm64SaveMode::Dirty);
	int64_t frame = TestFrame();

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
