#include <doctest/doctest.h>

#include <Scattershot_BitfsDr.hpp>
#include <ScriptMath.hpp>

// The oscillation stage's root rule (ROADMAP 4.6): a solution of a pass with a pass after it
// is a root the next pass can continue from only if a turnaround begun from its frame would
// end above the lava. The states are the maintainer's run of 2026-10-06, replayed through the
// game, each at its hand-over frame: the pyramid's normal, Mario's offset from the pyramid's
// home, his facing and speed. The first three are the roots its third pass started from,
// which all ended on the lava at the swing's -x end (the game had them 2 to 3 units above it
// on arrival, under it a frame later); the next three are slower hand-over candidates from
// the same pass's rows, which stood 40 to 63 above the lava.
TEST_CASE("dr-oscillations: the turnaround clearance refuses the roots whose end sinks")
{
	const float px = -1945.0f, py = -3225.0f, pz = -715.0f;
	struct Handover
	{
		float nx, ny, nz, dx, dz;
		int16_t yaw;
		float speed;
	};
	const Handover sunk[] = {
		{ -0.389f, 0.839f, 0.323f, -282.0f, 166.0f, -25530, 26.3f },
		{ -0.419f, 0.839f, 0.293f, -294.0f, 150.0f, -24576, 25.1f },
		{ -0.399f, 0.845f, 0.293f, -278.0f, 155.0f, -26188, 24.9f },
	};
	for (const Handover& h : sunk)
		CHECK(BitfsDrMetrics::TurnaroundClearance(h.nx, h.ny, h.nz, px + h.dx, pz + h.dz, h.yaw, h.speed, px, py, pz) < 10.0f);

	const Handover clear[] = {
		{ -0.329f, 0.84f, 0.303f, -222.0f, 148.0f, -24576, 22.9f },
		{ -0.289f, 0.84f, 0.343f, -189.0f, 171.0f, -24576, 22.8f },
		{ -0.329f, 0.84f, 0.343f, -225.0f, 181.0f, -24576, 21.5f },
	};
	for (const Handover& h : clear)
		CHECK(BitfsDrMetrics::TurnaroundClearance(h.nx, h.ny, h.nz, px + h.dx, pz + h.dz, h.yaw, h.speed, px, py, pz) > 25.0f);

	// At rest the clearance is the floor under Mario: the first root's frame had him 10.3 above
	// the lava (the inputs here are rounded, so within a few units).
	CHECK(BitfsDrMetrics::TurnaroundClearance(-0.389f, 0.839f, 0.323f, px - 282.0f, pz + 166.0f, -25530, 0.0f, px, py, pz) == doctest::Approx(10.3f).epsilon(0.3));
}

// The speed rule (the maintainer's form of 2026-10-10, held to the regime on 2026-10-18, the
// scope a setting since 2026-10-19; ROADMAP 4.6 and 4.12): a crossing must beat the one two
// before it, in speed and in its probed downhill peak, whatever the tilt did between, where
// the setting compares the two: both in the regime ("regime"), or both below it or both in
// it ("everywhere"); never across the regime's edge, never when "off". The third crossing
// here is slower than the first on both counts.
TEST_CASE("dr-oscillations: the speed rule compares crossings where its setting says, never across the regime's edge")
{
	auto status = [](float firstSum, bool firstInRegime, float thirdSum, bool thirdInRegime)
	{
		BitfsDrMetrics::CustomScriptStatus state;
		state.reachedNormRegime = thirdInRegime;
		state.crossingData.push_back({ 3300, 0.f, firstSum, 0.3f, 0.3f, 30.f, 24.f, firstInRegime });
		state.crossingData.push_back({ 3340, 0.f, firstSum, 0.3f, 0.3f, 28.f, 22.f, firstInRegime });
		state.crossingData.push_back({ 3380, 0.f, thirdSum, 0.3f, 0.3f, 23.f, 20.f, thirdInRegime });
		return state;
	};
	const float threshold = 0.1f;
	const SpeedRule regime = SpeedRule::REGIME;
	const SpeedRule everywhere = SpeedRule::EVERYWHERE;
	const SpeedRule off = SpeedRule::OFF;

	// Both below the regime, at the same tilt or a grown one: compared everywhere, not in the
	// regime-only setting.
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.632f, false), threshold, 0.0f, regime));
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.652f, false), threshold, 0.0f, regime));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.632f, false), threshold, 0.0f, everywhere));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.652f, false), threshold, 0.0f, everywhere));

	// Both in the regime: compared under both settings, whatever the tilt did.
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.712f, true), threshold, 0.0f, regime));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.672f, true), threshold, 0.0f, regime));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.712f, true), threshold, 0.0f, everywhere));

	// The earlier crossing below the regime, this one in it: never compared.
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.652f, false, 0.672f, true), threshold, 0.0f, regime));
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.652f, false, 0.672f, true), threshold, 0.0f, everywhere));

	// Off: nothing is compared.
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.712f, true), threshold, 0.0f, off));
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.632f, false), threshold, 0.0f, off));

	// The same answers from the question the metric script asks before it probes.
	CHECK(BitfsDrMetrics::SpeedRuleCompares(regime, true, true));
	CHECK(!BitfsDrMetrics::SpeedRuleCompares(regime, false, false));
	CHECK(!BitfsDrMetrics::SpeedRuleCompares(regime, true, false));
	CHECK(BitfsDrMetrics::SpeedRuleCompares(everywhere, false, false));
	CHECK(BitfsDrMetrics::SpeedRuleCompares(everywhere, true, true));
	CHECK(!BitfsDrMetrics::SpeedRuleCompares(everywhere, true, false));
	CHECK(!BitfsDrMetrics::SpeedRuleCompares(off, true, true));

	// A faster crossing passes in the regime.
	BitfsDrMetrics::CustomScriptStatus faster = status(0.672f, true, 0.712f, true);
	faster.crossingData.back().speed = 31.f;
	faster.crossingData.back().maxDownhillSpeed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(faster, threshold, 0.0f, regime));

	// Which comparisons apply (speedGates, the maintainer, 2026-10-19). The earlier crossing
	// here has a speed of 0 and a swing's peak of 30; a crossing no faster at the crossing
	// but with a faster probed run (31) passes the run-only setting and no other, and one
	// faster at the crossing (31) with a slower run (20) passes the crossing-only setting
	// and no other.
	BitfsDrMetrics::CustomScriptStatus runOnly = status(0.672f, true, 0.712f, true);
	runOnly.crossingData.back().maxDownhillSpeed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(runOnly, threshold, 0.0f, regime, SpeedGates::RUN));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(runOnly, threshold, 0.0f, regime, SpeedGates::BOTH));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(runOnly, threshold, 0.0f, regime, SpeedGates::CROSSING));
	BitfsDrMetrics::CustomScriptStatus crossingOnly = status(0.672f, true, 0.712f, true);
	crossingOnly.crossingData.back().speed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(crossingOnly, threshold, 0.0f, regime, SpeedGates::CROSSING));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(crossingOnly, threshold, 0.0f, regime, SpeedGates::BOTH));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(crossingOnly, threshold, 0.0f, regime, SpeedGates::RUN));
	// Where the rule does not compare, the gates do not matter.
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.712f, true), threshold, 0.0f, off, SpeedGates::RUN));

	// The crossing's axes at least minCrossingAxisDiff apart (the maintainer, 2026-10-10): the
	// crossings here are (0.3, 0.3), apart by nothing, so 0.1 refuses the third and 0 lets it
	// be; at (0.5, 0.2) it passes the example's 0.26. (In the regime and faster, so the speed
	// rule lets it be.)
	BitfsDrMetrics::CustomScriptStatus apart = status(0.672f, true, 0.712f, true);
	apart.crossingData.back().speed = 31.f;
	apart.crossingData.back().maxDownhillSpeed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.0f, regime));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.1f, regime));
	apart.crossingData.back().nX = 0.5f;
	apart.crossingData.back().nZ = 0.2f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.26f, regime));
}

// A pass's crossings (the maintainer's root mixture, 2026-10-20): a count of the target's
// parity at or under the target. The target itself, from a root at the oscillation before;
// two or four under it, from a root filled in from an earlier aligned pool; never a count
// over the target or of the other parity (the roots' own crossings), and nothing without a
// target.
TEST_CASE("dr-oscillations: a pass's crossings are those of its target's parity at or under it")
{
	CHECK(Scattershot_BitfsDr::PassCrossing(9, 9));
	CHECK(Scattershot_BitfsDr::PassCrossing(7, 9));
	CHECK(Scattershot_BitfsDr::PassCrossing(5, 9));
	CHECK(!Scattershot_BitfsDr::PassCrossing(8, 9));
	CHECK(!Scattershot_BitfsDr::PassCrossing(10, 9));
	CHECK(!Scattershot_BitfsDr::PassCrossing(11, 9));
	CHECK(Scattershot_BitfsDr::PassCrossing(0, 0));
	CHECK(Scattershot_BitfsDr::PassCrossing(1, 1));
	CHECK(!Scattershot_BitfsDr::PassCrossing(0, 1));
	CHECK(!Scattershot_BitfsDr::PassCrossing(0, -1));
}

// One direction per oscillation (the maintainer, 2026-10-17): the first crossing's
// roughTargetAngle is the chord end the leg runs to, the far side of the corner on the rest's
// steeper axis, and the crossings alternate from it. The committed rest (-0.179, 0.393) in
// corner 4 is steeper on z, so the leg runs to the -x -z end (-24576, the corner's A) and the
// first crossing has Mario running toward +x +z, the squish edge's corner, the second back.
TEST_CASE("dr-oscillations: the first crossing's direction follows from the rest's normal and the crossings alternate")
{
	const int16_t a = BitfsDrMetrics::RoughTargetAngleA(4);
	const int16_t b = BitfsDrMetrics::RoughTargetAngleB(4);
	CHECK(Scattershot_BitfsDr::FirstCrossingAngle(-0.1792f, 0.393f, 4) == a);
	CHECK(Scattershot_BitfsDr::FirstCrossingAngle(-0.393f, 0.1792f, 4) == b); // x the steeper axis: the leg runs the other way
	CHECK(Scattershot_BitfsDr::CrossingAngle(a, 1, 4) == a);
	CHECK(Scattershot_BitfsDr::CrossingAngle(a, 2, 4) == b);
	CHECK(Scattershot_BitfsDr::CrossingAngle(a, 3, 4) == a);
	CHECK(Scattershot_BitfsDr::CrossingAngle(b, 1, 4) == b);
	CHECK(Scattershot_BitfsDr::CrossingAngle(b, 4, 4) == a);

	// In every corner, with z the steeper axis: the leg's end lies on the corner's side of x
	// and the other side of z, and is one of the corner's two chord ends.
	for (int quadrant = 1; quadrant <= 4; quadrant++)
	{
		CAPTURE(quadrant);
		const float nx = 0.2f * float(BitfsDrMetrics::CornerSignX(quadrant));
		const float nz = 0.4f * float(BitfsDrMetrics::CornerSignZ(quadrant));
		const int16_t first = Scattershot_BitfsDr::FirstCrossingAngle(nx, nz, quadrant);
		CHECK((first == BitfsDrMetrics::RoughTargetAngleA(quadrant) || first == BitfsDrMetrics::RoughTargetAngleB(quadrant)));
		CHECK(ScriptMath::Sign(gSineTable[uint16_t(first) >> 4]) == BitfsDrMetrics::CornerSignX(quadrant));
		CHECK(ScriptMath::Sign(gCosineTable[uint16_t(first) >> 4]) == -BitfsDrMetrics::CornerSignZ(quadrant));
		CHECK(Scattershot_BitfsDr::CrossingAngle(first, 2, quadrant) != first);
		CHECK(Scattershot_BitfsDr::CrossingAngle(first, 3, quadrant) == first);
	}
}
