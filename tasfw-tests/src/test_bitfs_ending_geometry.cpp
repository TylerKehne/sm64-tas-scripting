#include <doctest/doctest.h>

#include <Scattershot_BitfsScEnding.hpp>

#include <array>
#include <cmath>

// The pyramid's top face as the ending's metric script computes it (BitfsScEndingMetrics::Face),
// against the game itself: the maintainer's movies replayed through the DLL with its own
// find_floor and find_ceil called along Mario's fall (ROADMAP 4.11, 2026-10-10). The
// pyramid's home is (-1945, -3225, -715), the lava at -3071; the positions are relative to
// the home and the heights above the lava. The game's edges are where find_floor, scanned a
// unit at a time, stops finding the top, so they are good to a unit; its normals and its
// sides' heights come from the face's vertices truncated to shorts, toward zero, which puts
// an edge on a negative side up to a unit nearer the apex than this geometry's and a
// side's height within 2.5 of it.
namespace
{
	using Face = BitfsScEndingMetrics::Face;
	using Placement = BitfsScEndingMetrics::Placement;

	constexpr float HomeX = -1945.0f;
	constexpr float HomeY = -3225.0f;
	constexpr float HomeZ = -715.0f;
	constexpr float Lava = BitfsScEndingMetrics::LavaHeight;

	Face FaceOf(float nx, float ny, float nz) { return Face::Of(nx, ny, nz, HomeX, HomeY, HomeZ); }

	// A frame of a movie: the stored normal, Mario's position and the game's +x or -z edge
	// at his position, with the top's height at that edge.
	struct Frame
	{
		float nx, ny, nz, marioX, marioZ, edge, edgeHeight;
	};
}

TEST_CASE("sc-ending: the face's edges and heights are the game's")
{
	// The uscz movie (2026-10-10, frames 3474 to 3482): the +x edge at Mario's z, and the +z edge
	// at his x (418, 416, 413, 411).
	const Frame uscz[] = {
		{ -0.233f, 0.789f, 0.454f, 244.1f, 309.2f, 244.0f, 106.1f },
		{ -0.203f, 0.799f, 0.444f, 253.7f, 318.2f, 254.0f, 93.7f },
		{ -0.163f, 0.819f, 0.424f, 264.0f, 291.1f, 263.0f, 99.3f },
		{ -0.153f, 0.829f, 0.414f, 273.3f, 287.5f, 266.0f, 99.5f },
	};
	const float usczPlusZ[] = { 418.0f, 416.0f, 413.0f, 411.0f };
	for (size_t i = 0; i < 4; i++)
	{
		const Frame& f = uscz[i];
		const Face face = FaceOf(f.nx, f.ny, f.nz);
		const float x = HomeX + f.marioX;
		const float z = HomeZ + f.marioZ;
		CHECK(std::fabs(face.EdgeOut(1, 0, x, z) - f.edge) <= 1.5f);
		CHECK(std::fabs(face.TopHeight(HomeX + face.EdgeOut(1, 0, x, z), z) - Lava - f.edgeHeight) <= 1.5f);
		CHECK(std::fabs(face.EdgeOut(0, 1, x, z) - usczPlusZ[i]) <= 1.5f);
	}

	// The uscx movie (pyramid_-1945_northeast, frames 4442 to 4449): the -z edge, at a constant z.
	const Frame uscx[] = {
		{ 0.428f, 0.815f, 0.277f, 294.8f, -205.1f, 204.0f, 123.4f },
		{ 0.398f, 0.845f, 0.247f, 278.0f, -221.6f, 217.0f, 130.2f },
		{ 0.368f, 0.875f, 0.217f, 243.8f, -233.7f, 229.0f, 142.7f },
		{ 0.358f, 0.885f, 0.207f, 241.5f, -243.4f, 233.0f, 142.4f },
	};
	for (const Frame& f : uscx)
	{
		const Face face = FaceOf(f.nx, f.ny, f.nz);
		const float x = HomeX + f.marioX;
		const float z = HomeZ + f.marioZ;
		CHECK(std::fabs(face.EdgeOut(0, -1, x, z) - f.edge) <= 1.5f);
		CHECK(std::fabs(face.TopHeight(x, HomeZ - face.EdgeOut(0, -1, x, z)) - Lava - f.edgeHeight) <= 1.5f);
	}

	// The sides: the ceiling the game finds under the face, and its normal, on the squish
	// frames. uscz 3482 at (264, 291): 94.3 above the lava, normal (0.810, -0.496, -0.312);
	// uscx 4449 at (230, -230): 142.2, normal (-0.201, -0.499, -0.843). And the top at the uscx
	// cancel's pushed spot, (322, -233): 110.2.
	{
		const Face face = FaceOf(-0.153f, 0.829f, 0.414f);
		CHECK(std::fabs(face.SideHeight(1, 0, HomeX + 264.0f, HomeZ + 291.0f) - Lava - 94.3f) <= 1.5f);
		CHECK(std::fabs(face.SideNormalY(1, 0) - (-0.4959f)) <= 0.004f);
		float dirX, dirZ;
		face.SideNormalDirection(1, 0, dirX, dirZ);
		CHECK(std::fabs(dirX - 0.934f) <= 0.01f);
		CHECK(std::fabs(dirZ - (-0.359f)) <= 0.01f);
	}
	{
		const Face face = FaceOf(0.358f, 0.885f, 0.207f);
		CHECK(std::fabs(face.SideHeight(0, -1, HomeX + 230.0f, HomeZ - 230.0f) - Lava - 142.2f) <= 3.0f); // the game's vertices truncate toward zero: its -z edge sits at -233, this geometry's at -234
		CHECK(std::fabs(face.SideNormalY(0, -1) - (-0.4988f)) <= 0.004f);
		CHECK(std::fabs(face.TopHeight(HomeX + 322.0f, HomeZ - 233.0f) - Lava - 110.2f) <= 1.5f);
		float dirX, dirZ;
		face.SideNormalDirection(0, -1, dirX, dirZ);
		CHECK(std::fabs(dirX - (-0.235f)) <= 0.01f);
		CHECK(std::fabs(dirZ - (-0.972f)) <= 0.01f);
	}

	// The ends of the +x edge: the +z corner where the +z edge is (411 at 3482), the -z end a
	// unit nearer the apex.
	{
		const Face face = FaceOf(-0.153f, 0.829f, 0.414f);
		CHECK(std::fabs(face.CornerAlong(1, 0, 0.0f, 1.0f) - 411.0f) <= 1.5f);
		CHECK(std::fabs(face.CornerAlong(1, 0, 0.0f, -1.0f) - 139.4f) <= 1.5f); // the -z end, as a coordinate along -z
		CHECK(face.Rise(0.0f, 1.0f) < 0.0f); // downhill toward +z: the +z side is the low one
	}
}

// The catch and its cancel, on the frames the game was asked about (ROADMAP 4.11, 2026-10-10;
// the push-out distance 115: the bully's 73, Mario's 37, the padding 5). The next frame's face
// is the one the squish is judged against, the normal stepped 0.01 toward flat from the frame
// before.
TEST_CASE("sc-ending: the catch's pushes are the ones the game cancels with")
{
	std::array<Placement, 3> placements;
	const float pushOut = 115.0f;

	// The uscz movie's own squish: caught at 3482 from (264.0, 291.1) 13.7 above the lava, the
	// +x edge standing 99 there: every push in the game was a pancake.
	{
		const Face next = FaceOf(-0.153f, 0.829f, 0.414f);
		CHECK(BitfsScEndingMetrics::CatchPlacements(next, 1, 0, HomeX + 264.0f, Lava + 13.7f, HomeZ + 291.1f, pushOut, placements) == 0);
	}

	// The same movie with Mario set at (254, 202) at the start of 3481, 37.7 above the lava,
	// where the edge stands 146: the push straight down the edge at the fractions 0.3, 0.4
	// and 0.5 gave idle at -413, 105 to 116 above the lava.
	{
		const Face next = FaceOf(-0.163f, 0.819f, 0.424f);
		const int count = BitfsScEndingMetrics::CatchPlacements(next, 1, 0, HomeX + 254.0f, Lava + 37.7f, HomeZ + 202.0f, pushOut, placements);
		CHECK(count == 3);
		for (int i = 0; i < count; i++)
		{
			CHECK(placements[i].dirZ > 0.95f); // down the edge, +z
			CHECK(placements[i].spotHeight > 100.0f);
			CHECK(placements[i].spotHeight <= 37.7f + 78.0f + BitfsScEndingMetrics::CatchSlack);
		}
		CHECK(placements[0].fraction == 0.3f);
		CHECK(placements[2].fraction == 0.5f);
	}

	// The uscx movie's cancel: caught at 4449 from (243.8, -233.7) 62.5 above the lava, the
	// push along the -z edge toward +x, 5 degrees in, at 0.3 and 0.4: idle at -413, 110 above.
	{
		const Face next = FaceOf(0.358f, 0.885f, 0.207f);
		const int count = BitfsScEndingMetrics::CatchPlacements(next, 0, -1, HomeX + 243.8f, Lava + 62.5f, HomeZ - 233.7f, pushOut, placements);
		CHECK(count >= 2);
		for (int i = 0; i < count; i++)
		{
			CHECK(placements[i].dirX > 0.95f); // along the edge toward +x, the low end
			CHECK(placements[i].dirZ > 0.0f);  // and in over the top
			CHECK(placements[i].spotHeight > 100.0f);
			CHECK(placements[i].spotHeight <= 62.5f + 78.0f + BitfsScEndingMetrics::CatchSlack);
		}
	}

	// No catch: Mario far outside the edge (the uscz leave, 3474, against the face of 3475,
	// Mario 4 outside it), and Mario on the top's own level (its floor, not the lava).
	{
		const Face next = FaceOf(-0.223f, 0.799f, 0.444f);
		CHECK(BitfsScEndingMetrics::CatchPlacements(next, 1, 0, HomeX + 244.1f + 10.0f, Lava + 105.8f, HomeZ + 309.2f, pushOut, placements) == -1);
		CHECK(BitfsScEndingMetrics::CatchPlacements(next, 1, 0, HomeX + 240.0f, Lava + 100.0f, HomeZ + 309.2f, pushOut, placements) == -1);
	}
}
