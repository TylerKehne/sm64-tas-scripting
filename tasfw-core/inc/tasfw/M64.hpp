#pragma once
#include <cstdint>
#include <filesystem>
#include <utility>
#include <tasfw/FrameMap.hpp>
#include <tasfw/Inputs.hpp>

#ifndef M64_H
#define M64_H

// The vanilla ROMs, as a Mupen movie's header names them: the CRC at 0xE4 (the ROM header's
// bytes at 0x10) and the country code at 0xE8 (the ROM header's bytes at 0x3E: the region
// letter and the version byte). The values were read from the ROMs (docs/libsm64.md).
enum class Rom : uint32_t
{
	SUPER_MARIO_64_J = 0x4EAA3D0E,
	SUPER_MARIO_64_U = 0x635A2BFF
};

enum class CountryCode : uint16_t
{
	SUPER_MARIO_64_J = 0x4A00, // 'J'
	SUPER_MARIO_64_U = 0x4500  // 'E'
};

class M64Base
{
public:
	FrameMap<uint64_t, Inputs> frames;

	M64Base() = default;
};

// What a Mupen movie's header says about the movie beyond its inputs: the game it is for.
struct M64Metadata
{
	Rom rom = Rom::SUPER_MARIO_64_J;
	CountryCode countryCode = CountryCode::SUPER_MARIO_64_J;

	bool operator==(const M64Metadata&) const = default;
};

class M64 : public M64Base
{
public:
	std::filesystem::path fileName;
	// Read from the header by load (a header naming something else keeps its values, cast)
	// and written back by save. A fresh M64 is the JP game; Script::ExportM64 marks an export
	// with its source movie's (GetM64Metadata). Whether a movie and a resource are the same
	// game is the resource's question (LibSm64::CheckMovie).
	M64Metadata metadata;

	M64() = default;

	M64(std::filesystem::path fileName) : fileName(std::move(fileName)) {}

	int load();
	int save(long initFrame = 0);
};

class M64Diff : public M64Base
{
public:
	M64Diff() = default;
	M64Diff(M64Diff&&) noexcept = default;
	M64Diff(const M64Diff&) = default;
	M64Diff& operator=(const M64Diff&) = default;
	M64Diff& operator=(M64Diff&& other) noexcept = default;
};

#endif
