#include <doctest/doctest.h>
#include <BitfsCorner.hpp>
#include <General.hpp>
#include <LibSm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include "libsm64_env.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>

// GetMinimumDownhillWalkingAngle (ROADMAP 4.13): the downhill angle one tilt on, asked of the
// resource itself as a child script, where a PyramidUpdate stand-in used to answer it. The
// child's frame is reverted with it; the angle it reports is the floor under Mario one frame
// on, read then (a Surface pointer is good for one frame: docs/tasing.md, "Pitfalls"), which
// is the pyramid's normal after that frame's tilt up to the rounding of the surface's s16
// vertices; and the game and the corner answer alike. The child's frame takes a neutral
// input of its own, never the movie's (the maintainer, 2026-10-19): the committed movies
// carry junk inputs after their content, which the corner refuses for the C buttons, and a
// script a search runs must work on whatever a movie holds.

using namespace tasfw::tests;

namespace
{
	struct DownhillResult
	{
		bool validated = false;
		bool executed = false;
		bool asserted = false;
		int64_t frameBefore = 0;
		int64_t frameAfter = 0; // the cursor after the child: unchanged
		int32_t floorAngle = 0;
		int32_t angleFacing = 0;
		int32_t angleNotFacing = 0;
		int32_t angleFacingAnalogBack = 0;
		int32_t angleNotFacingAnalogBack = 0;
		int rotation = 0;
		float steepness = 0;
		bool isSlope = false;
		int16_t normalAngleBefore = 0; // the pyramid's normal at the child's frame
		int16_t normalAngleOneFrameOn = 0; // the pyramid's normal after the root's own neutral frame
		int16_t floorAngleOneFrameOn = 0; // Mario's floor after that frame, read then
	};

	class DownhillProbe : public TopLevelScript<Sm64Resource>
	{
	public:
		DownhillProbe(int16_t target, int16_t face, DownhillResult& result) : _target(target), _face(face), _result(result) {}

		bool validation() override { return true; }
		bool execution() override
		{
			MarioState* mario = (MarioState*)ReadState("gMarioStates");
			Object* pyramid = mario->floor->object;
			// The anchor is a rest, where the normal is at its goal and would not move in the
			// child's frame: run Mario a few frames first, until the normal is moving.
			int16_t previous = atan2s(pyramid->oTiltingPyramidNormalZ, pyramid->oTiltingPyramidNormalX);
			for (int i = 0; i < 12; i++)
			{
				AdvanceFrameWrite(Inputs(0, 0, 127));
				int16_t now = atan2s(pyramid->oTiltingPyramidNormalZ, pyramid->oTiltingPyramidNormalX);
				if (now != previous)
					break;
				previous = now;
			}
			_result.normalAngleBefore = atan2s(pyramid->oTiltingPyramidNormalZ, pyramid->oTiltingPyramidNormalX);
			_result.frameBefore = GetCurrentFrame();
			auto status = Test<GetMinimumDownhillWalkingAngle>(_target, _face);
			_result.frameAfter = GetCurrentFrame();
			_result.validated = status.validated;
			_result.executed = status.executed;
			_result.asserted = status.asserted;
			_result.floorAngle = status.floorAngle;
			_result.angleFacing = status.angleFacing;
			_result.angleNotFacing = status.angleNotFacing;
			_result.angleFacingAnalogBack = status.angleFacingAnalogBack;
			_result.angleNotFacingAnalogBack = status.angleNotFacingAnalogBack;
			_result.rotation = int(status.downhillRotation);
			_result.steepness = status.steepness;
			_result.isSlope = status.isSlope;

			AdvanceFrameWrite(Inputs());
			_result.normalAngleOneFrameOn = atan2s(pyramid->oTiltingPyramidNormalZ, pyramid->oTiltingPyramidNormalX);
			Surface* floor = mario->floor;
			_result.floorAngleOneFrameOn = atan2s(floor->normal.z, floor->normal.x);
			return true;
		}
		bool assertion() override { return true; }

	private:
		int16_t _target;
		int16_t _face;
		DownhillResult& _result;
	};

	DownhillResult Probe(Sm64Resource& resource, int16_t target, int16_t face, M64& movie)
	{
		DownhillResult result;
		TopLevelScriptBuilder<DownhillProbe>::Build(movie).ImportResource(&resource).Run(target, face, result);
		return result;
	}

	DownhillResult Probe(Sm64Resource& resource, int16_t target, int16_t face)
	{
		M64 empty;
		return Probe(resource, target, face, empty);
	}

	int AngleGap(int16_t a, int16_t b) { return std::abs(int(int16_t(a - b))); }
}

TEST_CASE("libsm64: the downhill angle one tilt on is Mario's floor one frame on, the pyramid's normal then, the same on the game and on the corner"
	* doctest::skip(!HaveDll()))
{
	LibSm64 game(DllConfig(LibSm64SaveMode::Dirty));
	M64 m64(Env("TASFW_M64"));
	REQUIRE(m64.load() == 1);
	PlayFrames(game, m64, TestFrame());
	auto level = std::make_shared<const BitfsCornerLevel>(game);
	BitfsCorner corner(level);
	corner.load(BitfsCornerMem(game, *level, BitfsCornerObjects::All()));

	const int16_t target = 16384;
	const int16_t face = -13031; // Mario's facing at the anchor
	DownhillResult onGame = Probe(game, target, face);
	DownhillResult onCorner = Probe(corner, target, face);

	for (const DownhillResult* r : {&onGame, &onCorner})
	{
		CHECK(r->validated);
		CHECK(r->executed);
		CHECK(r->asserted);
		CHECK(r->frameAfter == r->frameBefore);
		CHECK(int16_t(r->floorAngle) == r->floorAngleOneFrameOn);
		// One tilt on, not the frame's own: the normal moves at the anchor, and the answer is
		// the normal after the move, up to the surface's rounding (tens of angle units at most).
		CHECK(r->normalAngleOneFrameOn != r->normalAngleBefore);
		CHECK(AngleGap(int16_t(r->floorAngle), r->normalAngleOneFrameOn) < AngleGap(int16_t(r->floorAngle), r->normalAngleBefore));
		CHECK(AngleGap(int16_t(r->floorAngle), r->normalAngleOneFrameOn) < 64);
		CHECK(r->steepness > 0.0f);
	}
	CHECK(onGame.floorAngle == onCorner.floorAngle);
	CHECK(onGame.angleFacing == onCorner.angleFacing);
	CHECK(onGame.angleNotFacing == onCorner.angleNotFacing);
	CHECK(onGame.angleFacingAnalogBack == onCorner.angleFacingAnalogBack);
	CHECK(onGame.angleNotFacingAnalogBack == onCorner.angleNotFacingAnalogBack);
	CHECK(onGame.rotation == onCorner.rotation);
	CHECK(onGame.steepness == onCorner.steepness);
	CHECK(onGame.isSlope == onCorner.isSlope);
	MESSAGE("floor angle one tilt on " << onGame.floorAngle << " (the normal " << onGame.normalAngleBefore << " then "
		<< onGame.normalAngleOneFrameOn << "), facing " << onGame.angleFacing << " / " << onGame.angleNotFacing
		<< ", steepness " << onGame.steepness << ", slope " << onGame.isSlope);

	// The movie's own frames are not the child's: on a movie whose every frame presses the
	// buttons the committed movies' junk frames do (A, B, START and the C buttons at once),
	// the corner answers as on an empty one, where reading the movie's frame would have it
	// refuse the C buttons.
	M64 junk;
	for (uint64_t frame = 0; frame < 64; frame++)
		junk.frames[frame] = Inputs(uint16_t(0xDC2C), 0, 0);
	DownhillResult onJunk = Probe(corner, target, face, junk);
	CHECK(onJunk.validated);
	CHECK(onJunk.executed);
	CHECK(onJunk.asserted);
	CHECK(onJunk.floorAngle == onCorner.floorAngle);
	CHECK(onJunk.angleFacing == onCorner.angleFacing);
	CHECK(onJunk.angleNotFacing == onCorner.angleNotFacing);
	CHECK(onJunk.rotation == onCorner.rotation);
	CHECK(onJunk.steepness == onCorner.steepness);
}
