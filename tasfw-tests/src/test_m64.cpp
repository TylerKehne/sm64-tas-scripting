#include <doctest/doctest.h>
#include <tasfw/Inputs.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	std::filesystem::path TempMovie(const char* name)
	{
		return std::filesystem::temp_directory_path() / name;
	}
}

TEST_CASE("M64 save/load round trip preserves every frame")
{
	auto path = TempMovie("tasfw-tests-roundtrip.m64");
	std::filesystem::remove(path);

	M64 writer(path);
	for (int i = 0; i < 100; i++)
		writer.frames[i] = Inputs(uint16_t((i * 0x1357) & 0xFFFF), int8_t(i - 50), int8_t(3 * i));
	REQUIRE(writer.save() == 1);

	M64 reader(path);
	REQUIRE(reader.load() == 1);
	REQUIRE(reader.frames.size() == 100);
	for (int i = 0; i < 100; i++)
	{
		CAPTURE(i);
		CHECK(reader.frames.at(i) == writer.frames.at(i));
	}

	// Mupen layout: 0x400-byte header, 4 bytes per frame.
	CHECK(std::filesystem::file_size(path) == 0x400 + 4 * 100);

	{
		std::ifstream f(path, std::ios::binary);
		char sig[4];
		f.read(sig, 4);
		CHECK(sig[0] == 'M');
		CHECK(sig[1] == '6');
		CHECK(sig[2] == '4');
		CHECK(sig[3] == 0x1A);
	} // closed before remove: Windows refuses to delete an open file

	std::filesystem::remove(path);
}

TEST_CASE("M64 save fills gaps with neutral inputs")
{
	auto path = TempMovie("tasfw-tests-gaps.m64");
	std::filesystem::remove(path);

	M64 writer(path);
	writer.frames[0] = Inputs(1, 2, 3);
	writer.frames[4] = Inputs(4, 5, 6);
	REQUIRE(writer.save() == 1);

	M64 reader(path);
	REQUIRE(reader.load() == 1);
	REQUIRE(reader.frames.size() == 5);
	CHECK(reader.frames.at(0) == Inputs(1, 2, 3));
	CHECK(reader.frames.at(2) == Inputs(0, 0, 0));
	CHECK(reader.frames.at(4) == Inputs(4, 5, 6));

	std::filesystem::remove(path);
}

TEST_CASE("M64::save into a missing directory reports failure instead of throwing")
{
	std::filesystem::path missing = std::filesystem::temp_directory_path() / "tasfw-no-such-dir-4c1e" / "movie.m64";
	std::filesystem::remove_all(missing.parent_path());
	M64 m64(missing);
	m64.frames[0] = Inputs(1, 2, 3);
	CHECK_NOTHROW(CHECK(m64.save() == 0)); // used to throw ios_base::failure, which aborts inside OpenMP
	CHECK_FALSE(std::filesystem::exists(missing));
}
