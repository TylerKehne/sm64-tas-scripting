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
	Full,  // the game's bytes of both sections: 7.3 MB, about 184 us per save or load. The
	       // reference, and the mode to use under a debugger (no page faults).
	Fixed, // five hand-tuned byte ranges: 1.5 MB, about 41 us, constant whatever the game
	       // does. Tuned to the pinned build (wafel v0.8.1's libsm64, docs/libsm64.md):
	       // construction refuses it on a build whose
	       // sections are smaller, and `dllcheck --save-mode fixed` reports which of the
	       // symbols the framework depends on the slices cover.
	Dirty, // the pages the game has written since the current baseline: about 122 pages
	       // (488 KB) and 7 us in BitFS play. Exact by construction on any build and on Linux;
	       // the cost grows with what the game writes (level loads, deaths) until the next
	       // baseline, which the resource takes on its own at the first save of a run.
};

class LibSm64Config
{
public:
	std::filesystem::path dllPath;
	CountryCode countryCode;
	LibSm64SaveMode saveMode = LibSm64SaveMode::Dirty;
};

constexpr int pagesize = 4096;

// Exported names the decomp has changed since the pinned build (docs/libsm64.md).
// LibSm64::addr tries the name it was given first, so the pinned DLL never pays for this
// table; only when that lookup fails does it try the other spelling. A newer build therefore
// costs one extra failed lookup per addr() call, which callers must not make per frame
// anyway (Resource::addr).
struct LibSm64SymbolAlias
{
	const char* pinned;  // exported by the pinned build (wafel v0.8.1's libsm64)
	const char* current; // exported by every later build (wafel v0.8.5 and its 2022-08-07 update, bitfs-sbb's .so)
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
inline constexpr size_t LibSm64FixedSliceCount = sizeof(LibSm64FixedSlices) / sizeof(LibSm64FixedSlices[0]);

// The game's bytes of the DLL's .data and .bss. Each section holds, besides the game's
// state, the state of the C runtime the DLL was built with (mingw-w64) at both ends: at the
// head `crtdll.c`'s atexit table and attach count, at the tail the startup lock and state,
// the TLS index, the pseudo-relocation table, the thread-key list with its critical section
// (`__mingwthr_cs`), gdtoa's memory and its critical section, and the math-error and
// exception handlers. The loader runs the runtime's code on every thread of the process,
// not only on the one that owns the instance: at every thread's exit it calls the DLL's TLS
// callback, which enters and leaves `__mingwthr_cs`, and at every thread's attach and
// detach it calls `DllMainCRTStartup`. A load that restored those bytes while another thread
// was inside that critical section reset it under that thread, which then died leaving a
// lock it no longer owned (STATUS_RESOURCE_NOT_OWNED) and left the process hanging on its
// join (ROADMAP 3.12, the sixteen-thread hang). So a savestate is the game's bytes only: no
// mode copies or restores anything outside [begin, end) of either section.
//
// Where the runtime's objects end and begin is a property of the build, read from the DLL's
// COFF symbol table by scripts/dll_game_bytes.py, which prints the entry for this table. A
// build is recognised by its sections' sizes; construction then checks that `__mingwthr_cs`
// reads as an initialised, unlocked critical section at the offset the entry names and
// refuses the DLL otherwise (a wrong entry would leave the race in place without a word). A
// build no entry knows (the Linux .so, whose loader runs nothing in the library at a thread's
// exit) is saved and restored whole, as before; `dllcheck` prints which applies.
struct LibSm64GameBytes
{
	size_t dataSize;      // the build's .data size in bytes: its key, with bssSize
	size_t bssSize;       //     and its .bss size
	size_t begin[2];      // the game's bytes of .data (0) and .bss (1): [begin, end), section-relative
	size_t end[2];
	size_t threadKeyLock; // .bss offset of the runtime's __mingwthr_cs, checked at construction
};

inline constexpr LibSm64GameBytes LibSm64KnownGameBytes[] = {
	{0x248B50, 0x4A8810, {0x20, 0x20}, {0x248A80, 0x4A7C60}, 0x4A7CE0}, // wafel v0.8.1, JP: the pinned build (res/sm64_jp_N.dll)
	{0x247310, 0x4A8810, {0x20, 0x20}, {0x247240, 0x4A7C60}, 0x4A7CE0}, // wafel v0.8.5, JP (bitfs-sbb's sm64_jp.dll)
	{0x247290, 0x4A87D0, {0x20, 0x20}, {0x2471C0, 0x4A7C20}, 0x4A7CA0}, // wafel's 2022-08-07 update, JP
	{0x2B04B0, 0x4A8710, {0x20, 0x20}, {0x2B03E0, 0x4A7B60}, 0x4A7BE0}, // wafel v0.8.5, US (bitfs-sbb's sm64_us.dll, res/sm64_us_0.dll)
};

// A savestate. Full and Fixed fill buf1/buf2 (the game's bytes of the sections, or the fixed
// slices packed). Dirty records which baseline the state is relative to, which pages of the page
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

	// The game's bytes of each page (LibSm64GameBytes): the whole page except where a
	// section's edge falls inside it. A save copies this much of a page and a load restores
	// this much; the handler's baseline copies take the whole page, which is only a read.
	struct Span
	{
		uint32_t offset = 0;
		uint32_t length = pagesize;
	};
	std::vector<Span> span; // one per page of the index space

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

	// Is the movie for the game this DLL was declared to be (config.countryCode)? Empty when
	// it is; otherwise the mismatch in words, for the caller's error. A movie for the other
	// version desyncs without a word (the intro and the text run different lengths), so
	// every place a movie meets a resource asks first: the pipeline before its first stage,
	// the tests, dllcheck. Resource itself knows nothing of games; this is libsm64's.
	std::string CheckMovie(const M64& movie) const;

	// The words the save modes and the game versions go by in config.json, on the command
	// line and in file names (`sm64_jp_0.dll`, `sm64_us_0.dll`; `{version}` in the pipeline's
	// dllPattern). A DLL carries no mark of its game inside, so what a DLL is comes from its
	// name (or a flag) and is declared in LibSm64Config::countryCode; a movie carries its game
	// in its header (M64Metadata).
	static const char* SaveModeName(LibSm64SaveMode mode);                             // "full", "fixed", "dirty"
	static bool ParseSaveMode(const std::string& name, LibSm64SaveMode& mode);         // the same words; false if unknown
	static const char* VersionName(CountryCode code);                                  // "jp", "us"; "?" for anything else
	static bool VersionFromPath(const std::filesystem::path& path, CountryCode& code); // from "sm64_jp" or "sm64_us" in the file name; false if neither
	static Rom RomFor(CountryCode code);                                               // the vanilla ROM's CRC that goes with a code in a movie header

	// The Dirty-mode bookkeeping, or nullptr in the other modes. Read-only, for dllcheck's
	// report and the tests.
	const LibSm64DirtyPages* dirtyPages() const { return _dirtyPages.get(); }

	// The entry of LibSm64KnownGameBytes this DLL matched, or nullptr when no entry knows the
	// build and both sections are saved and restored whole. Read-only, for dllcheck's report
	// and the tests.
	const LibSm64GameBytes* gameBytes() const { return _gameBytes; }

private:
	// Resolved once at construction. DLL symbol addresses never move. (GetProcAddress
	// measures ~60 ns on this DLL, so the four per-frame lookups this replaces were about
	// 2% of a frame; caching is hygiene rather than a headline win. See dllcheck.)
	using UpdateFn = void(TAS_FW_STDCALL*)();
	UpdateFn _sm64Update = nullptr;
	uint8_t* _controllerPads = nullptr; // gControllerPads: u16 button, s8 stick_x, s8 stick_y
	const uint32_t* _globalTimer = nullptr;

	// The game's bytes of .data (0) and .bss (1) as the save modes copy them (LibSm64GameBytes;
	// the whole section when the build is unknown), and the fixed slices cut to them.
	struct Bytes
	{
		uint8_t* begin = nullptr;
		size_t length = 0;
	};
	const LibSm64GameBytes* _gameBytes = nullptr;
	Bytes _game[2];
	LibSm64FixedSlice _fixedSlices[LibSm64FixedSliceCount] {};

	std::unique_ptr<LibSm64DirtyPages> _dirtyPages;
	void TakeBaseline(const LibSm64Mem& saving) const; // `saving`: the state about to be written
};

#endif
