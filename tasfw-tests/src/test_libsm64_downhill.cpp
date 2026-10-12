#include <doctest/doctest.h>
#include <BitfsCorner.hpp>
#include <General.hpp>
#include <LibSm64.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include "libsm64_env.hpp"

#include <cstdint>
#include <memory>

// GetMinimumDownhillWalkingAngle (ROADMAP 4.13): the downhill angle one tilt on, asked of the
// resource itself as a child script, where a PyramidUpdate stand-in used to answer it. The
// child's frame is reverted with it; the angle it reports is the pyramid's normal one frame
// on, read at the surface Mario stood on; and the game and the corner answer alike.

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
		int16_t normalAngleOneFrameOn = 0; // the root's own neutral frame, then the surface's normal
	};

	class DownhillProbe : public TopLevelScript<Sm64Resource>
	{
	public:
		DownhillProbe(int16_t target, int16_t face, DownhillResult& result) : _target(target), _face(face), _result(result) {}

		bool validation() override { return true; }
		bool execution() override
		{
			MarioState* mario = (MarioState*)ReadState("gMarioStates");
			Surface* floor = mario->floor;
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
			_result.normalAngleOneFrameOn = atan2s(floor->normal.z, floor->normal.x);
			return true;
		}
		bool assertion() override { return true; }

	private:
		int16_t _target;
		int16_t _face;
		DownhillResult& _result;
	};

	DownhillResult Probe(Sm64Resource& resource, int16_t target, int16_t face)
	{
		M64 empty;
		DownhillResult result;
		TopLevelScriptBuilder<DownhillProbe>::Build(empty).ImportResource(&resource).Run(target, face, result);
		return result;
	}
}

TEST_CASE("libsm64: the downhill angle one tilt on is the pyramid's normal one frame on, the same on the game and on the corner"
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
		CHECK(int16_t(r->floorAngle) == r->normalAngleOneFrameOn);
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
	MESSAGE("floor angle one tilt on " << onGame.floorAngle << ", facing " << onGame.angleFacing << " / " << onGame.angleNotFacing
		<< ", steepness " << onGame.steepness << ", slope " << onGame.isSlope);
}
