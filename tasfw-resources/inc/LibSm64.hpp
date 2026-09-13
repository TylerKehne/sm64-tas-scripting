#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "tasfw/Resource.hpp"
#include <tasfw/Inputs.hpp>

#ifndef LIBSM64_H
#define LIBSM64_H

// OBJECT_POOL_CAPACITY in the decomp: gObjectPool is a static array of this many Objects.
inline constexpr int LibSm64ObjectPoolCapacity = 240;

// How a savestate represents the DLL's .data and .bss (docs/libsm64.md, "Savestates"). The
// choice is the resource's alone: scripts never see it, and savestate management stays
// automatic whichever mode is set. Costs are for the pinned DLL on the reference machine.
enum class LibSm64SaveMode
{
	Full,  // both sections whole: 7.3 MB, about 184 us per save or load. The reference, and
	       // the mode to use under a debugger (no page faults).
	Fixed, // five hand-tuned byte ranges: 1.5 MB, about 41 us, constant whatever the game
	       // does. Tuned to the pinned 2022 build: construction refuses it on a build whose
	       // sections are smaller, and `dllcheck --save-mode fixed` reports which of the
	       // symbols the framework depends on the slices cover.
	Dirty, // the pages the game has written since the current baseline: about 122 pages
	       // (488 KB) and 7 us in BitFS play. Exact by construction on any build and on Linux;
	       // the cost grows with what the game writes (level loads, deaths) until the next
	       // baseline, which the resource takes on its own at the first save of a run.
};

const char* LibSm64SaveModeName(LibSm64SaveMode mode);                    // "full", "fixed", "dirty"
bool ParseLibSm64SaveMode(const std::string& name, LibSm64SaveMode& mode); // the same words; false if unknown

class LibSm64Config
{
public:
	std::filesystem::path dllPath;
	CountryCode countryCode;
	LibSm64SaveMode saveMode = LibSm64SaveMode::Dirty;
};

constexpr int pagesize = 4096;

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

// Fixed mode copies only these byte ranges of the DLL's .data (segment 0) and .bss
// (segment 1) instead of the whole sections. They were chosen empirically for the pinned
// 2022 build (docs/libsm64.md); `dllcheck --save-mode fixed` reports whether the game state
// the framework depends on lies inside them for whatever DLL is loaded.
struct LibSm64FixedSlice
{
	int segment;       // 0 = .data, 1 = .bss
	size_t offset;     // byte offset into the section
	size_t length;     // bytes copied
	size_t bufOffset;  // byte offset into buf1 (.data) or buf2 (.bss)
};

inline constexpr LibSm64FixedSlice LibSm64FixedSlices[] = {
	{0, 0,       100000,  0},
	{0, 2000000, 100000,  100000},
	{1, 0,       600000,  0},
	{1, 1700000, 600000,  600000},
	{1, 4700000, 100000,  1200000},
};
inline constexpr size_t LibSm64FixedBuf1Size = 200000;
inline constexpr size_t LibSm64FixedBuf2Size = 1300000;

// A savestate. Full and Fixed fill buf1/buf2 (the whole sections, or the fixed slices
// packed). Dirty records which baseline the state is relative to, which pages of the page
// index space (.data's pages then .bss's) the game had written since that baseline began,
// and those pages' contents in index order. SlotManager recycles these objects, so the
// vectors keep their capacity and a save into a warm slot allocates nothing.
class LibSm64Mem
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
	int baseline = 0;
	std::vector<uint64_t> written;
	std::vector<uint8_t> pages;
};

// The bookkeeping behind Dirty mode (ROADMAP 2.3). Whole pages covering .data and .bss (edge
// pages included) form one index space and are all made read-only; the first write to a page
// faults, a process-wide handler (vectored exception handler on Windows, SIGSEGV on Linux)
// finds the set that owns the address, records the page and makes it writable again. Faults
// happen once per page per baseline, about 120 in BitFS play and about 500 per scattershot
// shot, never per frame. A save copies the pages written since the current baseline began; a
// load writes them back and restores every other page written since the state's baseline
// began from that baseline's copy of it, so a load is exact by construction.
//
// A baseline holds, for every page written since it began, the page's content as it was
// when it began: the fault handler copies the page into every live baseline that lacks it
// before the first write (copy-on-write, at most a few 4 KB copies per fault). Taking a
// baseline therefore copies nothing; it costs re-protecting the pages and the first-write
// faults that follow. Storage is one lazily touched buffer per live baseline, resident only
// where pages were written.
//
// A baseline is taken by LibSm64::save itself, from what the resource observes and nothing
// else: when it is asked to save while the slot manager holds no live slots, which is the
// start save every top-level run begins with and the first slot of a run, the save LongLoad
// makes at the frame exploration starts from. Taking one opens a new, empty baseline and
// re-protects every page, so later saves copy only what the run itself writes; loads of
// older states stay exact because the baseline a live slot's state names keeps its pages
// until no live slot names it (released at the next baseline; the start save's is always
// kept). Re-baselining again during a run, once its loads had paid for one, was measured
// and dropped (ROADMAP 2.3): the set regrows within a scattershot shot whatever the
// baseline. Nothing in the framework or in any script takes part, and no result depends on
// when a baseline is taken. Heap-allocated and owned through a unique_ptr so that moving a
// LibSm64 does not move what the handler points at.
struct LibSm64DirtyPages
{
	struct Range
	{
		uint8_t* begin = nullptr; // page-aligned
		size_t pages = 0;
	};
	Range range[2];
	size_t pageCount = 0;
	int baseline = 0; // index of the current baseline

	// One entry per baseline taken, by index. A live one holds every page written since it
	// began (`present`, one bit per page) with the content the page had when it began; a
	// released one (no live state names it) has no storage.
	struct Baseline
	{
		std::vector<uint64_t> present;
		std::unique_ptr<uint8_t[]> pages; // pageCount * pagesize bytes, touched only where present
	};
	std::vector<Baseline> baselines;
	std::vector<int> live;              // indices of the baselines with storage, for the handler
	uint64_t faults = 0;                // first writes recorded, all baselines

	const std::vector<uint64_t>& Written() const { return baselines[size_t(baseline)].present; } // since the current baseline began
	uint8_t* PageAddress(size_t index) const;
	bool Contains(const void* p, size_t& index) const;
	void OnWrite(size_t index); // handler path: copy into the baselines that lack the page, record, make writable
	void ProtectAll();
	void UnprotectAll();
	size_t WrittenCount() const;
	void AddBaseline();         // a new, empty current baseline
};

class LibSm64 : public Resource<LibSm64Mem>
{
public:
	SharedLib dll;
	std::vector<SegVal> segment;
	const LibSm64Config config;

	LibSm64(const LibSm64Config& config);
	~LibSm64() override;
	LibSm64(LibSm64&&) = default;

	void save(LibSm64Mem& state) const override;
	void load(const LibSm64Mem& state) override;
	void advance() override;
	void setInputs(const Inputs& inputs) override;
	void* addr(const char* symbol) const override;
	std::size_t getStateSize(const LibSm64Mem& state) const override;
	uint32_t getCurrentFrame() const override;

	// The Dirty-mode bookkeeping, or nullptr in the other modes. Read-only, for dllcheck's
	// report and the tests.
	const LibSm64DirtyPages* dirtyPages() const { return _dirtyPages.get(); }

private:
	// Resolved once at construction. DLL symbol addresses never move. (GetProcAddress
	// measures ~60 ns on this DLL, so the four per-frame lookups this replaces were about
	// 2% of a frame; caching is hygiene rather than a headline win. See dllcheck.)
	using UpdateFn = void(TAS_FW_STDCALL*)();
	UpdateFn _sm64Update = nullptr;
	uint8_t* _controllerPads = nullptr; // gControllerPads: u16 button, s8 stick_x, s8 stick_y
	const uint32_t* _globalTimer = nullptr;

	std::unique_ptr<LibSm64DirtyPages> _dirtyPages;
	void TakeBaseline(const LibSm64Mem& saving) const; // `saving`: the state about to be written
};

#endif
