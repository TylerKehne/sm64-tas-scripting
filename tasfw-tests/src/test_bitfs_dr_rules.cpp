#include <doctest/doctest.h>

#include <Scattershot_BitfsDr.hpp>

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

// The speed rule (the maintainer's form of 2026-10-10, ROADMAP 4.6 and 4.12): a crossing must
// beat the one two before it, in speed and in its downhill peak, whatever the tilt did
// between, below the regime and in it alike; never when the earlier one was below the regime
// and this one in it. The third crossing here is slower than the first on both counts.
TEST_CASE("dr-oscillations: the speed rule compares crossings below the regime and in it, never across")
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

	// Both below the regime: compared, at the same tilt or a grown one.
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.632f, false), threshold, 0.0f));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.632f, false, 0.652f, false), threshold, 0.0f));

	// Both in the regime: compared, whatever the tilt did.
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.712f, true), threshold, 0.0f));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(status(0.672f, true, 0.672f, true), threshold, 0.0f));

	// The earlier crossing below the regime, this one in it: never compared.
	CHECK(BitfsDrMetrics::ValidateCrossingData(status(0.652f, false, 0.672f, true), threshold, 0.0f));

	// A faster crossing passes in the regime.
	BitfsDrMetrics::CustomScriptStatus faster = status(0.672f, true, 0.712f, true);
	faster.crossingData.back().speed = 31.f;
	faster.crossingData.back().maxDownhillSpeed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(faster, threshold, 0.0f));

	// The crossing's axes at least minCrossingAxisDiff apart (the maintainer, 2026-10-10): the
	// crossings here are (0.3, 0.3), apart by nothing, so 0.1 refuses the third and 0 lets it
	// be; at (0.5, 0.2) it passes the example's 0.26. (A faster third crossing, so the speed
	// rule lets it be.)
	BitfsDrMetrics::CustomScriptStatus apart = status(0.632f, false, 0.652f, false);
	apart.crossingData.back().speed = 31.f;
	apart.crossingData.back().maxDownhillSpeed = 31.f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.0f));
	CHECK(!BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.1f));
	apart.crossingData.back().nX = 0.5f;
	apart.crossingData.back().nZ = 0.2f;
	CHECK(BitfsDrMetrics::ValidateCrossingData(apart, threshold, 0.26f));
}
