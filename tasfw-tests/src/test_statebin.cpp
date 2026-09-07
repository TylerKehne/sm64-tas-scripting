#include <doctest/doctest.h>
#include <BinaryStateBin.hpp>

#include <stdexcept>

TEST_CASE("BinaryStateBin packs values at the bit cursor")
{
	BinaryStateBin<4> bin;
	uint8_t cursor = 0;

	bin.AddValueBits(cursor, 8, 0xAB);
	CHECK(cursor == 8);
	CHECK(bin.bytes[0] == 0xAB);

	bin.AddValueBits(cursor, 4, 0xF);
	CHECK(cursor == 12);
	CHECK(bin.bytes[1] == 0x0F);

	bin.AddValueBits(cursor, 12, 0x123);
	CHECK(cursor == 24);
	// 0x123 starts at bit 12: low nibble 0x3 lands in the high nibble of byte 1, 0x12 in byte 2.
	CHECK(bin.bytes[1] == 0x3F);
	CHECK(bin.bytes[2] == 0x12);
	CHECK(bin.bytes[3] == 0x00);
}

TEST_CASE("BinaryStateBin rejects values that do not fit")
{
	BinaryStateBin<2> bin;
	uint8_t cursor = 0;
	CHECK_THROWS_AS(bin.AddValueBits(cursor, 4, 16), std::runtime_error);
	CHECK(cursor == 0);
}

TEST_CASE("AddRegionBitsByNRegions maps the range onto regions and clamps the top edge")
{
	auto region = [](float value)
	{
		BinaryStateBin<2> bin;
		uint8_t cursor = 0;
		bin.AddRegionBitsByNRegions<float>(cursor, 4, value, 0.0f, 1.0f, 16);
		return int(bin.bytes[0]);
	};
	CHECK(region(0.0f) == 0);
	CHECK(region(0.5f) == 8);
	CHECK(region(0.99f) == 15);
	CHECK(region(1.0f) == 15); // exactly max would be region 16; clamped

	BinaryStateBin<2> bin;
	uint8_t cursor = 0;
	CHECK_THROWS_AS(bin.AddRegionBitsByNRegions<float>(cursor, 4, 1.5f, 0.0f, 1.0f, 16), std::runtime_error);
	CHECK_THROWS_AS(bin.AddRegionBitsByNRegions<float>(cursor, 4, 0.5f, 0.0f, 1.0f, 0), std::runtime_error);
}

TEST_CASE("AddRegionBitsByRegionSize quantizes by step")
{
	auto region = [](int value)
	{
		BinaryStateBin<2> bin;
		uint8_t cursor = 0;
		bin.AddRegionBitsByRegionSize<int>(cursor, 8, value, -32768, 32767, 1024);
		return int(bin.bytes[0]);
	};
	CHECK(region(-32768) == 0);
	CHECK(region(-32768 + 1024) == 1);
	CHECK(region(0) == 32);
	CHECK(region(32767) == 63);
}

TEST_CASE("Bins with different contents compare unequal; identical packing compares equal")
{
	BinaryStateBin<16> a;
	BinaryStateBin<16> b;
	uint8_t ca = 0;
	uint8_t cb = 0;
	a.AddRegionBitsByNRegions<float>(ca, 12, 0.25f, -1.0f, 1.0f, 4000);
	b.AddRegionBitsByNRegions<float>(cb, 12, 0.25f, -1.0f, 1.0f, 4000);
	CHECK(a == b);
	b.AddValueBits(cb, 1, 1);
	CHECK_FALSE(a == b);
}
