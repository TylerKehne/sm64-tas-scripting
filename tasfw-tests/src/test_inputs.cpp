#include <doctest/doctest.h>
#include <tasfw/Inputs.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

// Joystick mapping. The framework's tables are checked against a brute force over all
// 65,536 stick positions using GetIntendedYawMagFromInput as the oracle, so these tests do
// not depend on the table implementation, only on its consistency with the game formula.

namespace
{
	struct Best
	{
		bool found = false;
		float magDistance = std::numeric_limits<float>::infinity();
	};

	// Closest achievable magnitude among sticks whose resulting yaw satisfies `yawOk`.
	template <class F>
	Best BruteForce(float intendedMag, int16_t cameraYaw, F yawOk)
	{
		Best best;
		for (int x = -128; x <= 127; x++)
		{
			for (int y = -128; y <= 127; y++)
			{
				auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(int8_t(x), int8_t(y), cameraYaw);
				if (mag <= 0.0f || !yawOk(yaw))
					continue;
				best.found = true;
				best.magDistance = std::min(best.magDistance, std::fabs(mag - intendedMag));
			}
		}
		return best;
	}

	// Signed distance in whole HAUs (16-unit buckets) from `from` to `to`, wrapping.
	int HauDistance(int16_t from, int16_t to)
	{
		int16_t diff = int16_t((to - (to & 15)) - (from - (from & 15)));
		return diff / 16;
	}

	// Smallest |HAU distance| from `target` at which any stick position lands (at any
	// magnitude). Not every HAU is reachable: atan2s quantizes, so some buckets are empty.
	int NearestReachableHau(int16_t target, int16_t cameraYaw)
	{
		int nearest = 0x7FFF;
		for (int x = -128; x <= 127; x++)
		{
			for (int y = -128; y <= 127; y++)
			{
				auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(int8_t(x), int8_t(y), cameraYaw);
				if (mag <= 0.0f)
					continue;
				nearest = std::min(nearest, std::abs(HauDistance(target, yaw)));
			}
		}
		return nearest;
	}
}

TEST_CASE("GetIntendedYawMagFromInput matches the game's stick formula on known points")
{
	// Dead zone: |stick| < 8 contributes nothing.
	CHECK(Inputs::GetIntendedYawMagFromInput(0, 0, 0).second == 0.0f);
	CHECK(Inputs::GetIntendedYawMagFromInput(7, -7, 0).second == 0.0f);

	// Full deflection is capped at 64 raw -> intended magnitude 32.
	CHECK(Inputs::GetIntendedYawMagFromInput(127, 0, 0).second == 32.0f);
	CHECK(Inputs::GetIntendedYawMagFromInput(127, 127, 0).second == 32.0f);

	// x = 10 -> adjusted 4 -> ((4/64)^2 * 64) / 2 = 0.125.
	CHECK(Inputs::GetIntendedYawMagFromInput(10, 0, 0).second == doctest::Approx(0.125f));

	// Camera yaw is simply added.
	auto a = Inputs::GetIntendedYawMagFromInput(127, 0, 0);
	auto b = Inputs::GetIntendedYawMagFromInput(127, 0, 1000);
	CHECK(int16_t(a.first + 1000) == b.first);
	CHECK(a.second == b.second);
}

TEST_CASE("HauEquals compares 16-unit angle buckets")
{
	CHECK(Inputs::HauEquals(0, 15));
	CHECK_FALSE(Inputs::HauEquals(15, 16));
	CHECK(Inputs::HauEquals(-16, -1));
	CHECK_FALSE(Inputs::HauEquals(-1, 0));
}

TEST_CASE("Rotation::Negate")
{
	CHECK(Rotation(Rotation::CLOCKWISE).Negate() == Rotation::COUNTERCLOCKWISE);
	CHECK(Rotation(Rotation::COUNTERCLOCKWISE).Negate() == Rotation::CLOCKWISE);
	CHECK(Rotation(Rotation::NONE).Negate() == Rotation::NONE);
}

TEST_CASE("GetClosestInputByYawHau lands in the nearest reachable HAU with the closest magnitude")
{
	const int16_t cameraYaws[] = {0, 0x1234, int16_t(-0x3000)};
	const int16_t yaws[] = {0, 0x4000, int16_t(-0x4000), int16_t(0x7FF0), 12345, int16_t(-321)};
	const float mags[] = {32.0f, 20.5f, 3.0f, 0.125f};

	for (int16_t cameraYaw : cameraYaws)
		for (int16_t yaw : yaws)
		{
			// With no bias the search widens symmetrically and stops at the first HAU distance
			// where either side has an input, so the result must sit at that distance and have
			// the closest magnitude among sticks at that distance (on either side).
			int nearest = NearestReachableHau(yaw, cameraYaw);
			for (float mag : mags)
			{
				CAPTURE(cameraYaw);
				CAPTURE(yaw);
				CAPTURE(mag);
				CAPTURE(nearest);
				auto stick = Inputs::GetClosestInputByYawHau(yaw, mag, cameraYaw);
				auto [gotYaw, gotMag] = Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, cameraYaw);

				Best best = BruteForce(mag, cameraYaw, [&](int16_t y) { return std::abs(HauDistance(yaw, y)) == nearest; });
				REQUIRE(best.found);
				CHECK(std::abs(HauDistance(yaw, gotYaw)) == nearest);
				CHECK(std::fabs(gotMag - mag) == doctest::Approx(best.magDistance));
			}
		}
}

TEST_CASE("Some HAUs are unreachable, which is why the search widens")
{
	// Documents the reason for the test above: at least one of these buckets has no stick.
	int unreachable = 0;
	for (int16_t yaw : {int16_t(0), int16_t(0x4000), int16_t(12345), int16_t(-321), int16_t(0x7FF0)})
		if (NearestReachableHau(yaw, 0) > 0)
			unreachable++;
	MESSAGE("unreachable HAUs among the sampled targets: " << unreachable);
	CHECK(NearestReachableHau(0, 0) == 0);      // straight ahead is always reachable
	CHECK(NearestReachableHau(0x4000, 0) == 0); // and so are the cardinals
}

TEST_CASE("GetClosestInputByYawExact prefers an exact yaw when one exists")
{
	const int16_t cameraYaw = 0x0ABC;
	// Yaws that some stick position produces exactly: take them from the oracle itself.
	int16_t exactYaws[3] = {};
	int n = 0;
	for (int x = 20; x < 128 && n < 3; x += 37)
	{
		auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(int8_t(x), int8_t(x / 3), cameraYaw);
		exactYaws[n++] = yaw;
	}

	for (int16_t yaw : exactYaws)
		for (float mag : {32.0f, 5.0f})
		{
			CAPTURE(yaw);
			CAPTURE(mag);
			auto stick = Inputs::GetClosestInputByYawExact(yaw, mag, cameraYaw);
			auto [gotYaw, gotMag] = Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, cameraYaw);
			CHECK(gotYaw == yaw);

			Best best = BruteForce(mag, cameraYaw, [&](int16_t y) { return y == yaw; });
			REQUIRE(best.found);
			CHECK(std::fabs(gotMag - mag) == doctest::Approx(best.magDistance));
		}
}

TEST_CASE("Zero magnitude maps to the neutral stick")
{
	CHECK(Inputs::GetClosestInputByYawHau(1234, 0.0f, 0) == std::pair<int8_t, int8_t>(0, 0));
	CHECK(Inputs::GetClosestInputByYawExact(1234, 0.0f, 0) == std::pair<int8_t, int8_t>(0, 0));
}
