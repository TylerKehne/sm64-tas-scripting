#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "tasfw/Resource.hpp"
#include <tasfw/Inputs.hpp>

#ifndef LIBSM64_H
#define LIBSM64_H

class LibSm64Config
{
public:
	std::filesystem::path dllPath;
	CountryCode countryCode;
	bool lightweight; // true = faster, but accuracy not guaranteed in all situations
};

constexpr int pagesize = 4096;

// Lightweight save mode copies only these byte ranges of the DLL's .data (segment 0) and
// .bss (segment 1) instead of the whole sections. They were chosen empirically for the
// pinned 2022 build (docs/libsm64.md); LibSm64::layoutCheckReport verifies that the game
// state the framework depends on actually lies inside them for whatever DLL is loaded.
struct LibSm64LightweightSlice
{
	int segment;       // 0 = .data, 1 = .bss
	size_t offset;     // byte offset into the section
	size_t length;     // bytes copied
	size_t bufOffset;  // byte offset into buf1 (.data) or buf2 (.bss)
};

inline constexpr LibSm64LightweightSlice LibSm64LightweightSlices[] = {
	{0, 0,       100000,  0},
	{0, 2000000, 100000,  100000},
	{1, 0,       600000,  0},
	{1, 1700000, 600000,  600000},
	{1, 4700000, 100000,  1200000},
};
inline constexpr size_t LibSm64LightweightBuf1Size = 200000;
inline constexpr size_t LibSm64LightweightBuf2Size = 1300000;

class LibSm64Mem
{
public:
#if defined(_WIN32)
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
#else
	std::unordered_map<void*, std::array<uint8_t, pagesize>> changed_regions;
	uint64_t region_count_at_save_time=0;
#endif
};

class LibSm64 : public Resource<LibSm64Mem>
{
public:
	SharedLib dll;
	std::vector<SegVal> segment;
	const LibSm64Config config;

#if !defined(_WIN32)
	std::vector<uint8_t> original_buf1;
	std::vector<uint8_t> original_buf2;
#endif

	LibSm64(const LibSm64Config& config);
	void save(LibSm64Mem& state) const override;
	void load(const LibSm64Mem& state) override;
	void advance() override;
	void* addr(const char* symbol) const override;
	std::size_t getStateSize(const LibSm64Mem& state) const override;
	uint32_t getCurrentFrame() const override;

	// Layout self-check (ROADMAP 1.1). Reads game state through the copied decomp structs
	// and cross-checks it against relationships the game guarantees (Mario's object lives in
	// the object pool at a multiple of sizeof(Object), its behavior is bhvMario, its
	// position fields mirror MarioState, the floor normal is unit length, ...). Any mismatch
	// means the headers in tasfw-core/inc/sm64 do not describe this DLL build.
	// Returns one line per check, prefixed "ok: " or "FAIL: ". In-level checks are only
	// possible once Mario exists (i.e. after loading to a frame inside a level).
	std::vector<std::string> layoutCheckReport() const;
	void verifyLayout() override;

	// True if the pointer lies inside the DLL's .data or .bss section, i.e. it is plausibly
	// a pointer into game memory rather than garbage read through a wrong layout.
	bool pointsIntoGameData(const void* p) const;
};

#endif