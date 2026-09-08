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

// OBJECT_POOL_CAPACITY in the decomp: gObjectPool is a static array of this many Objects.
inline constexpr int LibSm64ObjectPoolCapacity = 240;

// What a script expects to find in a gObjectPool slot. Scripts address level objects by slot
// (`objectPool[84]` is the BitFS pyramid), which is the maintainer's decision (ROADMAP 2.4):
// several objects share a behavior, so the slot is the identifier. The slot is a side effect
// of spawn order, though, so each expectation is verified once per thread by the layout check
// (LibSm64::objectCheckReport): the slot must be active and run the named behavior, and, when
// checkHome is set, sit at the home position the level script spawned it at. Home is what
// tells the two BitFS pyramids apart (their behavior runs SET_HOME), but it is not a rule
// that holds for every object: many never set it and moving objects may update it, so an
// expectation can leave it unchecked. A mismatch fails start-up with a readable message
// instead of letting scripts read another object. The BitFS list is
// tasfw-scripts/inc/BitFsObjects.hpp.
struct LibSm64ExpectedObject
{
	int slot;
	const char* behavior; // exported behavior symbol, pinned-DLL spelling (LibSm64SymbolAliases apply)
	float homeX;
	float homeY;
	float homeZ;
	bool checkHome = true;
};

class LibSm64Config
{
public:
	std::filesystem::path dllPath;
	CountryCode countryCode;
	bool lightweight; // true = faster, but accuracy not guaranteed in all situations.
	                  // Windows only; see LibSm64LightweightSupported.
	std::vector<LibSm64ExpectedObject> expectedObjects; // verified by layoutCheckReport once in a level
};

constexpr int pagesize = 4096;

// Lightweight saves exist only on Windows. The Linux LibSm64 write-protects .data/.bss and
// saves the pages the game dirtied instead (LibSm64.cpp), so LibSm64Config::lightweight is
// ignored there and the slice coverage checks in layoutCheckReport do not apply.
#if defined(_WIN32)
inline constexpr bool LibSm64LightweightSupported = true;
#else
inline constexpr bool LibSm64LightweightSupported = false;
#endif

// Exported names the decomp has changed since the pinned 2022 build (docs/libsm64.md).
// LibSm64::addr tries the name it was given first, so the pinned DLL never pays for this
// table; only when that lookup fails does it try the other spelling. A newer build therefore
// costs one extra failed lookup per addr() call, which callers must not make per frame
// anyway (Resource::addr).
struct LibSm64SymbolAlias
{
	const char* pinned;  // exported by the pinned 2022 build
	const char* current; // exported by builds from the current decomp (wafel 2023, bitfs-sbb 2026)
};

inline constexpr LibSm64SymbolAlias LibSm64SymbolAliases[] = {
	{"bhvBitfsTiltingInvertedPyramid", "bhvBitFSTiltingInvertedPyramid"},
	{"bhvLllTiltingInvertedPyramid", "bhvLLLTiltingInvertedPyramid"},
};

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
	void setInputs(const Inputs& inputs) override;
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

	// One "ok: "/"FAIL: " line per expectation: does gObjectPool[slot] hold an active object
	// running `behavior` at home (homeX, homeY, homeZ)? Only meaningful inside a level.
	// layoutCheckReport calls it with config.expectedObjects; exposed so a test can hand it a
	// wrong expectation and see the FAIL.
	std::vector<std::string> objectCheckReport(const std::vector<LibSm64ExpectedObject>& expected) const;

private:
	// Resolved once at construction. DLL symbol addresses never move. (GetProcAddress
	// measures ~60 ns on this DLL, so the four per-frame lookups this replaces were about
	// 2% of a frame; caching is hygiene rather than a headline win. See dllcheck.)
	using UpdateFn = void(TAS_FW_STDCALL*)();
	UpdateFn _sm64Update = nullptr;
	uint8_t* _controllerPads = nullptr; // gControllerPads: u16 button, s8 stick_x, s8 stick_y
	const uint32_t* _globalTimer = nullptr;
};

#endif