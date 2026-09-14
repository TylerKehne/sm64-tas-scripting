#include <doctest/doctest.h>
#include <tasfw/Inputs.hpp>

#include "script_fixtures.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace tasfw::tests;

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

TEST_CASE("M64 identity: a fresh movie is the JP game, and the game a movie is saved as loads back")
{
	auto path = TempMovie("tasfw-tests-identity.m64");
	std::filesystem::remove(path);

	M64 fresh(path);
	CHECK(fresh.metadata.rom == Rom::SUPER_MARIO_64_J);
	CHECK(fresh.metadata.countryCode == CountryCode::SUPER_MARIO_64_J);
	fresh.frames[0] = Inputs(1, 2, 3);
	REQUIRE(fresh.save() == 1);
	M64 loadedJp(path);
	REQUIRE(loadedJp.load() == 1);
	CHECK(loadedJp.metadata.rom == Rom::SUPER_MARIO_64_J);
	CHECK(loadedJp.metadata.countryCode == CountryCode::SUPER_MARIO_64_J);
	std::filesystem::remove(path);

	M64 us(path);
	us.metadata.rom = Rom::SUPER_MARIO_64_U;
	us.metadata.countryCode = CountryCode::SUPER_MARIO_64_U;
	us.frames[0] = Inputs(1, 2, 3);
	REQUIRE(us.save() == 1);
	{
		// Mupen layout: the ROM's CRC bytes at 0xE4, its country and version bytes at 0xE8.
		std::ifstream f(path, std::ios::binary);
		f.seekg(0xE4);
		unsigned char header[6];
		f.read(reinterpret_cast<char*>(header), 6);
		CHECK(header[0] == 0x63);
		CHECK(header[1] == 0x5A);
		CHECK(header[2] == 0x2B);
		CHECK(header[3] == 0xFF);
		CHECK(header[4] == 'E');
		CHECK(header[5] == 0);
	}
	M64 loadedUs(path);
	REQUIRE(loadedUs.load() == 1);
	CHECK(loadedUs.metadata.rom == Rom::SUPER_MARIO_64_U);
	CHECK(loadedUs.metadata.countryCode == CountryCode::SUPER_MARIO_64_U);

	// Saving into the existing file keeps writing the movie's own game.
	loadedUs.frames[1] = Inputs(4, 5, 6);
	REQUIRE(loadedUs.save() == 1);
	M64 again(path);
	REQUIRE(again.load() == 1);
	CHECK(again.metadata.countryCode == CountryCode::SUPER_MARIO_64_U);
	CHECK(again.frames.size() == 2);
	std::filesystem::remove(path);
}

TEST_CASE("ExportM64 marks the export with the game the source movie is for")
{
	auto path = TempMovie("tasfw-tests-export-identity.m64");
	std::filesystem::remove(path);

	MockResource resource;
	M64 source;
	source.metadata.rom = Rom::SUPER_MARIO_64_U;
	source.metadata.countryCode = CountryCode::SUPER_MARIO_64_U;
	for (int i = 0; i < 5; i++)
		source.frames[i] = In(i);
	RunRoot(resource, source, [&](auto& s)
		{
			s.AdvanceFrameWrite(In(10));
			s.AdvanceFrameWrite(In(11));
			CHECK(s.ExportM64(path));
		});

	M64 exported(path);
	REQUIRE(exported.load() == 1);
	CHECK(exported.metadata.rom == Rom::SUPER_MARIO_64_U);
	CHECK(exported.metadata.countryCode == CountryCode::SUPER_MARIO_64_U);
	REQUIRE(exported.frames.size() == 2);
	CHECK(exported.frames.at(0) == In(10));
	CHECK(exported.frames.at(1) == In(11));
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
