// dllcheck: is this libsm64 DLL the one our headers describe, and what does a frame cost?
//
//   dllcheck <libsm64.dll> <movie.m64> <frame> [--save-mode full|fixed|dirty] [--leak-scan [frames]]
//
// Loads the DLL, runs the VerifyLayout script to <frame> (which should be inside a level)
// and prints one line per check (ROADMAP 1.1); in the fixed save mode it also reports
// whether the fixed slices cover each symbol the framework depends on. Then it replays the
// movie on the resource itself and prints the measured cost of a frame advance and of a
// savestate save/load, the first Tier B numbers in docs/performance.md. Exit code 0 when
// every check passes, 1 on a failed check, 2 on usage or load errors.
//
// --leak-scan: at <frame>, save a state, snapshot the DLL's .data and .bss, play `frames`
// (default 120) of a fixed input pattern, load the state back and snapshot again. Every byte
// range that differs is state the load did not restore, i.e. state that survives a load and
// can make a replay depend on history (ROADMAP 4.5). A second pass with a different input
// pattern shows which of those ranges depend on what was played. Offsets are section-relative;
// scripts/dll_symbols.py maps them to exported symbols.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <LibSm64.hpp>
#include <VerifyLayout.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Inputs.hpp>

namespace
{
	double MicrosecondsSince(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
	}

	struct LeakRange
	{
		int segment;
		size_t offset;
		size_t length;
	};

	struct Results
	{
		int64_t frameReached = -1;
		uint64_t framesAdvanced = 0;
		double playMicros = 0;
		double saveMicros = 0;
		double loadMicros = 0;
		double addrNanos = 0;
		LibSm64SaveMode saveMode = LibSm64SaveMode::Dirty;
		std::vector<std::string> coverage; // fixed saves: does each symbol the framework depends on lie inside the slices?
		bool listObjects = false;
		std::vector<std::string> objects; // one line per active object in gObjectPool
		int leakScanFrames = 0;
		std::vector<LeakRange> leaksPass1; // differs after play + load
		std::vector<LeakRange> leaksPass2; // differs after a different play + load
		size_t leakBytesPass1 = 0;
		size_t leakBytesPass2 = 0;
		int dirtyScanFrames = 0;   // --dirty-scan: pages written per frame under pattern inputs from <frame>
		bool dirtyReplay = false;  // --dirty-replay: pages written per frame while replaying the movie 0..<frame>
		std::vector<std::string> dirtyReport;
		// Dirty save mode: the set as it stood when save/load were timed (after the warm-up
		// frames), not after the top-level script's rewind.
		bool haveDirtyPages = false;
		size_t dirtyPagesWritten = 0;
		size_t dirtyPageCount = 0;
		int dirtyBaseline = 0;
		uint64_t dirtyFaults = 0;
		int warmupFrames = 60;
	};

	// Which 4 KB pages of .data/.bss changed between two snapshots, and how many bytes in them.
	struct DirtyStats
	{
		std::vector<uint8_t> unionPages[2]; // one flag per page of each segment
		std::vector<size_t> pagesPerFrame;
		std::vector<size_t> bytesPerFrame;
		size_t unionBytes = 0;              // bytes seen to change at least once, exact
		std::vector<std::pair<int, size_t>> checkpoints; // (frames, union pages) at 1, 10, 30, 60, 120, ...
	};

	void AccumulateDirty(DirtyStats& stats, int segment, const std::vector<uint8_t>& before, const std::vector<uint8_t>& after,
		std::vector<uint8_t>& everChanged, size_t& framePages, size_t& frameBytes)
	{
		size_t n = std::min(before.size(), after.size());
		size_t pages = (n + pagesize - 1) / pagesize;
		if (stats.unionPages[segment].size() < pages)
			stats.unionPages[segment].resize(pages, 0);
		if (everChanged.size() < n)
			everChanged.resize(n, 0);
		for (size_t p = 0; p < pages; p++)
		{
			size_t begin = p * pagesize;
			size_t end = std::min(n, begin + pagesize);
			if (std::memcmp(before.data() + begin, after.data() + begin, end - begin) == 0)
				continue;
			framePages++;
			stats.unionPages[segment][p] = 1;
			for (size_t i = begin; i < end; i++)
			{
				if (before[i] != after[i])
				{
					frameBytes++;
					if (!everChanged[i])
					{
						everChanged[i] = 1;
						stats.unionBytes++;
					}
				}
			}
		}
	}

	size_t UnionPages(const DirtyStats& stats)
	{
		size_t total = 0;
		for (const auto& seg : stats.unionPages)
			for (uint8_t flag : seg)
				total += flag;
		return total;
	}

	// Human-readable summary of a dirty-page measurement plus how the union sits against the
	// fixed slices: pages the slices do not fully contain are state a Fixed-mode load
	// would not restore.
	std::vector<std::string> DescribeDirty(const char* title, const DirtyStats& stats, const LibSm64& resource)
	{
		std::vector<std::string> lines;
		char buf[256];
		lines.push_back(title);
		if (!stats.pagesPerFrame.empty())
		{
			std::vector<size_t> sorted = stats.pagesPerFrame;
			std::sort(sorted.begin(), sorted.end());
			std::vector<size_t> bytesSorted = stats.bytesPerFrame;
			std::sort(bytesSorted.begin(), bytesSorted.end());
			std::snprintf(buf, sizeof(buf), "  per frame: %zu / %zu / %zu pages (min / median / max), %zu / %zu / %zu bytes changed",
				sorted.front(), sorted[sorted.size() / 2], sorted.back(),
				bytesSorted.front(), bytesSorted[bytesSorted.size() / 2], bytesSorted.back());
			lines.push_back(buf);
		}
		std::string growth = "  union after frames:";
		for (const auto& [frames, pages] : stats.checkpoints)
		{
			std::snprintf(buf, sizeof(buf), " %d -> %zu pages", frames, pages);
			growth += buf;
		}
		lines.push_back(growth);
		size_t unionPages = UnionPages(stats);
		std::snprintf(buf, sizeof(buf), "  union: %zu pages = %zu KB (%zu bytes actually changed); full .data+.bss = %zu KB; fixed slices = %zu KB",
			unionPages, unionPages * pagesize / 1024, stats.unionBytes,
			(resource.segment[0].length + resource.segment[1].length) / 1024,
			(LibSm64FixedBuf1Size + LibSm64FixedBuf2Size) / 1024);
		lines.push_back(buf);

		// Coverage by the current slices.
		size_t covered = 0;
		std::string outside;
		int outsideCount = 0;
		for (int seg = 0; seg < 2; seg++)
		{
			const char* name = seg == 0 ? ".data" : ".bss";
			for (size_t p = 0; p < stats.unionPages[seg].size(); p++)
			{
				if (!stats.unionPages[seg][p])
					continue;
				size_t begin = p * pagesize;
				bool inside = false;
				for (const LibSm64FixedSlice& slice : LibSm64FixedSlices)
					if (slice.segment == seg && begin >= slice.offset && begin + pagesize <= slice.offset + slice.length)
						inside = true;
				if (inside)
					covered++;
				else
				{
					if (outsideCount++ < 40)
					{
						std::snprintf(buf, sizeof(buf), " %s+%zu", name, begin);
						outside += buf;
					}
				}
			}
		}
		std::snprintf(buf, sizeof(buf), "  fixed slices contain %zu of those pages; %d outside:%s%s", covered, outsideCount,
			outside.c_str(), outsideCount > 40 ? " ..." : "");
		lines.push_back(buf);
		return lines;
	}

	Inputs PatternInputs(int i, int variant)
	{
		double angle = (i * (variant == 0 ? 0.37 : 0.61));
		int8_t x = int8_t(60.0 * std::cos(angle));
		int8_t y = int8_t(60.0 * std::sin(angle));
		uint16_t buttons = 0;
		if (i % (variant == 0 ? 7 : 5) == 0)
			buttons |= 0x8000; // A
		if (i % (variant == 0 ? 13 : 11) == 0)
			buttons |= 0x4000; // B
		return Inputs(buttons, x, y);
	}

	std::vector<uint8_t> Snapshot(const LibSm64& resource, int segment)
	{
		const uint8_t* begin = static_cast<const uint8_t*>(resource.segment[size_t(segment)].address);
		return std::vector<uint8_t>(begin, begin + resource.segment[size_t(segment)].length);
	}

	// Byte ranges where two snapshots differ, merging gaps of up to 16 bytes.
	void Diff(int segment, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, std::vector<LeakRange>& out, size_t& bytes)
	{
		size_t n = std::min(a.size(), b.size());
		size_t i = 0;
		while (i < n)
		{
			if (a[i] == b[i])
			{
				i++;
				continue;
			}
			size_t start = i;
			size_t last = i;
			while (i < n && i - last <= 16)
			{
				if (a[i] != b[i])
				{
					last = i;
					bytes++;
				}
				i++;
			}
			out.push_back(LeakRange { segment, start, last - start + 1 });
		}
	}

	// Advance `frames` frames from wherever the resource is, feeding the movie's inputs (neutral
	// where it has none). The tool's own loop: its subject is the resource, so it drives it
	// directly instead of through a script (AGENTS.md, hard rule 9).
	void PlayFrames(LibSm64& resource, const M64& m64, int64_t frames)
	{
		for (int64_t f = 0; f < frames; f++)
		{
			auto inputs = m64.frames.find(uint64_t(resource.getCurrentFrame()));
			resource.setInputs(inputs != m64.frames.end() ? inputs->second : Inputs());
			resource.FrameAdvance();
		}
	}

	// The measurements drive the resource directly, outside any script: their subject is the
	// resource (AGENTS.md, hard rule 9). --dirty-replay watches every frame of the play to
	// <frame>, so it is that play, instrumented; otherwise PlayFrames gets there and its time
	// is the frame-advance cost.
	class PlayToFrame
	{
	public:
		PlayToFrame(LibSm64& resource, const M64& m64, int64_t frame, Results& results)
			: resource(&resource), _m64(&m64), _frame(frame), _results(results) {}

		void Run()
		{
			// Only the frames of this play count: VerifyLayout advanced its own on the way to <frame>.
			uint64_t framesBefore = resource->nFrameAdvances;
			auto start = std::chrono::steady_clock::now();
			if (_results.dirtyReplay)
				DirtyReplay();
			else
				PlayFrames(*resource, *_m64, _frame);
			_results.playMicros = MicrosecondsSince(start);
			_results.framesAdvanced = resource->nFrameAdvances - framesBefore;
			_results.frameReached = resource->getCurrentFrame();

			if (_results.saveMode == LibSm64SaveMode::Fixed)
				FixedCoverage();

			// Cost of the savestate primitives, measured directly on the resource, from a state a
			// run is actually in: some frames after the first slot (LongLoad's, which in Dirty
			// mode set the baseline), so that a dirty-page save has the pages a pellet dirties to
			// copy instead of none.
			int64_t anchor = resource->SaveState();
			for (int i = 0; i < _results.warmupFrames; i++)
			{
				resource->setInputs(PatternInputs(i, 0));
				resource->FrameAdvance();
			}
			if (const LibSm64DirtyPages* d = resource->dirtyPages())
			{
				_results.haveDirtyPages = true;
				_results.dirtyPageCount = d->pageCount;
				_results.dirtyBaseline = d->baseline;
				_results.dirtyFaults = d->faults;
				_results.dirtyPagesWritten = d->WrittenCount();
			}

			constexpr int reps = 100;
			int64_t slot = resource->SaveState();
			start = std::chrono::steady_clock::now();
			for (int i = 0; i < reps; i++)
				resource->LoadState(slot);
			_results.loadMicros = MicrosecondsSince(start) / reps;
			resource->slotManager.EraseSlot(slot);

			start = std::chrono::steady_clock::now();
			for (int i = 0; i < reps; i++)
			{
				int64_t id = resource->SaveState();
				resource->slotManager.EraseSlot(id);
			}
			_results.saveMicros = MicrosecondsSince(start) / reps;

			resource->LoadState(anchor); // back to <frame> for everything below
			resource->slotManager.EraseSlot(anchor);

			// Cost of a symbol lookup (GetProcAddress through the loader). Scripts that call
			// addr() per frame pay this each time; see docs/performance.md.
			constexpr int addrReps = 2000;
			start = std::chrono::steady_clock::now();
			const void* sink = nullptr;
			for (int i = 0; i < addrReps; i++)
				sink = resource->addr((i & 1) ? "gMarioState" : "gCamera");
			_results.addrNanos = MicrosecondsSince(start) * 1000.0 / addrReps;
			(void)sink;

			if (_results.listObjects)
				ListObjects();
			if (_results.leakScanFrames > 0)
				LeakScan();
			if (_results.dirtyScanFrames > 0)
				DirtyScan();
		}

		// Fixed mode only saves fixed slices of .data/.bss. Every piece of state the search
		// depends on must lie inside a slice, or savestates silently stop restoring it. This is
		// the report a different DLL build is expected to fail; the leak scan is the measurement
		// behind it. At <frame>, where the camera and Mario's floor exist.
		void FixedCoverage()
		{
			std::vector<std::string>& lines = _results.coverage;
			auto covered = [&](const void* p, size_t size) -> bool
			{
				const char* ptr = static_cast<const char*>(p);
				for (const LibSm64FixedSlice& slice : LibSm64FixedSlices)
				{
					const char* begin = static_cast<const char*>(resource->segment[size_t(slice.segment)].address) + slice.offset;
					if (ptr >= begin && ptr + size <= begin + slice.length)
						return true;
				}
				return false;
			};
			auto sectionOffset = [&](const void* p) -> std::string
			{
				for (const SegVal& seg : resource->segment)
				{
					const char* begin = static_cast<const char*>(seg.address);
					const char* ptr = static_cast<const char*>(p);
					if (ptr >= begin && ptr < begin + seg.length)
						return seg.name + "+" + std::to_string(ptr - begin);
				}
				return "outside sections";
			};
			auto checkCoverage = [&](const char* symbol, const void* p, size_t size)
			{
				if (covered(p, size))
					lines.push_back(std::string("ok: fixed slices cover ") + symbol + " (" + sectionOffset(p) + ", " + std::to_string(size) + " bytes)");
				else
					lines.push_back(std::string("FAIL: fixed slices do NOT cover ") + symbol + " (" + sectionOffset(p) + ", " + std::to_string(size)
						+ " bytes); fixed-mode savestates would not restore it. Use the dirty or full save mode, or re-derive LibSm64FixedSlices for this DLL");
			};
			auto optional = [&](const char* symbol) -> void*
			{
				try
				{
					return resource->addr(symbol);
				}
				catch (const std::exception&)
				{
					return nullptr;
				}
			};

			MarioState* marioStates = (MarioState*)(resource->addr("gMarioStates"));
			Object* objectPool = (Object*)(resource->addr("gObjectPool"));
			Camera* camera = *(Camera**)(resource->addr("gCamera"));
			Surface* floor = (*(MarioState**)(resource->addr("gMarioState")))->floor;
			checkCoverage("gMarioStates", marioStates, 2 * sizeof(MarioState));
			checkCoverage("gObjectPool", objectPool, LibSm64ObjectPoolCapacity * sizeof(Object));
			checkCoverage("gGlobalTimer", resource->addr("gGlobalTimer"), sizeof(uint32_t));
			checkCoverage("gControllerPads", resource->addr("gControllerPads"), 4 * 6);
			checkCoverage("gMarioObject", resource->addr("gMarioObject"), sizeof(void*));
			checkCoverage("gCamera", resource->addr("gCamera"), sizeof(void*));
			if (camera != nullptr)
				checkCoverage("*gCamera", camera, sizeof(Camera));
			if (floor != nullptr)
				checkCoverage("gMarioState->floor (surface pool)", floor, sizeof(Surface));

			// Symbols that may not exist in every build: check when present.
			for (const char* symbol : {"gRandomSeed16", "gCurrentArea", "sSurfacePool", "gAreas"})
				if (void* p = optional(symbol))
					checkCoverage(symbol, p, sizeof(void*));

			// Camera state. The game turns a raw stick into Mario's intended yaw through the camera,
			// so any of this that a load does not restore makes a replay diverge from the run that
			// recorded it. These were not part of the original slice selection; they are reported
			// as warnings rather than failures until ROADMAP 4.5 settles what the search needs.
			// Sizes are the decomp's, generous where the x64 layout is unknown.
			auto warnCoverage = [&](const char* symbol, size_t size)
			{
				void* p = optional(symbol);
				if (p == nullptr)
					return;
				if (covered(p, size))
					lines.push_back(std::string("ok: fixed slices cover ") + symbol + " (" + sectionOffset(p) + ")");
				else
					lines.push_back(std::string("WARN: fixed slices do NOT cover ") + symbol + " (" + sectionOffset(p) + ", "
						+ std::to_string(size) + " bytes); a fixed-mode load does not restore it");
			};
			warnCoverage("gLakituState", 136);
			warnCoverage("gPlayerCameraState", 2 * 72);
			warnCoverage("gCameraMovementFlags", 2);
			warnCoverage("sModeTransition", 64);
			warnCoverage("sMarioCamState", sizeof(void*));
			warnCoverage("sModeOffsetYaw", 2);
			warnCoverage("sYawSpeed", 2);
			warnCoverage("sCUpCameraPitch", 2);
			warnCoverage("sFOVState", 16);
			warnCoverage("sCameraStoreCUp", 32);
			warnCoverage("sPanDistance", 4);
			warnCoverage("sZeroZoomDist", 4);
			warnCoverage("sSelectionFlags", 2);
			warnCoverage("sCButtonsPressed", 2);

			// Controller state: buttonPressed is an edge against the previous frame's buttonDown,
			// so a load that leaves the old buttonDown behind changes whether the next frame's
			// A is a press or a hold.
			warnCoverage("gControllers", 3 * 40);
			warnCoverage("gControllerBits", 2);
			warnCoverage("gPlayer1Controller", sizeof(void*));
		}

		// How much of .data/.bss the game writes per frame, and how fast the set of touched
		// pages grows: the numbers behind any replacement of the hand-tuned fixed slices
		// (ROADMAP 2.3). --dirty-scan plays pattern inputs from <frame>; --dirty-replay walks the
		// movie from power-on to <frame>. Both compare consecutive frames page by page.
		void DirtyScan()
		{
			int frames = _results.dirtyScanFrames;
			int64_t slot = resource->SaveState();
			DirtyStats stats;
			std::vector<uint8_t> ever[2];
			std::vector<uint8_t> prev[2] = { Snapshot(*resource, 0), Snapshot(*resource, 1) };
			for (int i = 0; i < frames; i++)
			{
				resource->setInputs(PatternInputs(i, 0));
				resource->FrameAdvance();
				Step(stats, ever, prev, i + 1, frames);
			}
			resource->LoadState(slot);
			resource->slotManager.EraseSlot(slot);
			char title[128];
			std::snprintf(title, sizeof(title), "\nPages written, pattern inputs from frame %lld for %d frames:", (long long)_frame, frames);
			for (const std::string& line : DescribeDirty(title, stats, *resource))
				_results.dirtyReport.push_back(line);
		}

		void DirtyReplay()
		{
			DirtyStats stats;
			std::vector<uint8_t> ever[2];
			std::vector<uint8_t> prev[2] = { Snapshot(*resource, 0), Snapshot(*resource, 1) };
			for (int64_t f = 1; f <= _frame; f++)
			{
				PlayFrames(*resource, *_m64, 1);
				Step(stats, ever, prev, int(f), int(_frame));
			}
			char title[128];
			std::snprintf(title, sizeof(title), "\nPages written while replaying the movie, frames 1..%lld:", (long long)_frame);
			for (const std::string& line : DescribeDirty(title, stats, *resource))
				_results.dirtyReport.push_back(line);
		}

		void Step(DirtyStats& stats, std::vector<uint8_t> (&ever)[2], std::vector<uint8_t> (&prev)[2], int frame, int total)
		{
			size_t framePages = 0;
			size_t frameBytes = 0;
			for (int seg = 0; seg < 2; seg++)
			{
				std::vector<uint8_t> cur = Snapshot(*resource, seg);
				AccumulateDirty(stats, seg, prev[seg], cur, ever[seg], framePages, frameBytes);
				prev[seg] = std::move(cur);
			}
			stats.pagesPerFrame.push_back(framePages);
			stats.bytesPerFrame.push_back(frameBytes);
			for (int mark : {1, 10, 30, 60, 120, 300, 1000, 2000})
				if (frame == mark)
					stats.checkpoints.emplace_back(frame, UnionPages(stats));
			if (frame == total)
				stats.checkpoints.emplace_back(frame, UnionPages(stats));
		}

		// Every active object in the pool: index, behavior as <section>+<offset> (so that
		// `scripts/dll_symbols.py <dll> -` names it from the export table), behavior params,
		// position and home. What a level-specific index like gObjectPool[84] actually refers
		// to, and what distinguishes objects that share a behavior (ROADMAP 2.4).
		void ListObjects()
		{
			constexpr int poolCapacity = LibSm64ObjectPoolCapacity;
			Object* pool = (Object*)(resource->addr("gObjectPool"));
			auto sections = resource->dll.readSections();
			auto where = [&](const void* p) -> std::string
			{
				const char* ptr = static_cast<const char*>(p);
				for (const auto& [name, info] : sections)
				{
					const char* begin = static_cast<const char*>(info.address);
					if (ptr >= begin && ptr < begin + info.length)
						return name + "+" + std::to_string(ptr - begin);
				}
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%p", p);
				return buf;
			};
			for (int i = 0; i < poolCapacity; i++)
			{
				Object& o = pool[i];
				if (o.activeFlags == 0)
					continue;
				char buf[256];
				std::snprintf(buf, sizeof(buf), "obj %3d: behavior %s, behParams 0x%08X, pos (%.1f, %.1f, %.1f), home (%.1f, %.1f, %.1f)",
					i, where(o.behavior).c_str(), unsigned(o.oBehParams), o.oPosX, o.oPosY, o.oPosZ, o.oHomeX, o.oHomeY, o.oHomeZ);
				_results.objects.push_back(buf);
			}
		}

		void LeakScan()
		{
			int frames = _results.leakScanFrames;
			int64_t slot = resource->SaveState();
			std::vector<uint8_t> data0 = Snapshot(*resource, 0);
			std::vector<uint8_t> bss0 = Snapshot(*resource, 1);

			auto play = [&](int variant)
			{
				for (int i = 0; i < frames; i++)
				{
					resource->setInputs(PatternInputs(i, variant));
					resource->FrameAdvance();
				}
				resource->LoadState(slot);
			};

			play(0);
			Diff(0, data0, Snapshot(*resource, 0), _results.leaksPass1, _results.leakBytesPass1);
			Diff(1, bss0, Snapshot(*resource, 1), _results.leaksPass1, _results.leakBytesPass1);

			play(1);
			Diff(0, data0, Snapshot(*resource, 0), _results.leaksPass2, _results.leakBytesPass2);
			Diff(1, bss0, Snapshot(*resource, 1), _results.leaksPass2, _results.leakBytesPass2);

			resource->slotManager.EraseSlot(slot);
		}
	private:
		LibSm64* resource;
		const M64* _m64;
		int64_t _frame;
		Results& _results;
	};
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: dllcheck <libsm64.dll> <movie.m64> <frame> [--save-mode full|fixed|dirty] [--leak-scan [frames]] [--objects] [--dirty-scan [frames]] [--dirty-replay]\n");
		return 2;
	}

	std::filesystem::path dllPath = argv[1];
	std::filesystem::path m64Path = argv[2];
	int64_t frame = std::stoll(argv[3]);
	LibSm64SaveMode saveMode = LibSm64SaveMode::Dirty;
	bool listObjects = false;
	int leakScanFrames = 0;
	int dirtyScanFrames = 0;
	bool dirtyReplay = false;
	for (int i = 4; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--save-mode")
		{
			if (i + 1 >= argc || !ParseLibSm64SaveMode(argv[i + 1], saveMode))
			{
				std::fprintf(stderr, "--save-mode needs one of full, fixed, dirty\n");
				return 2;
			}
			i++;
		}
		else if (arg == "--objects")
			listObjects = true;
		else if (arg == "--leak-scan")
		{
			leakScanFrames = 120;
			if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0]))
				leakScanFrames = std::atoi(argv[++i]);
		}
		else if (arg == "--dirty-scan")
		{
			dirtyScanFrames = 120;
			if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0]))
				dirtyScanFrames = std::atoi(argv[++i]);
		}
		else if (arg == "--dirty-replay")
			dirtyReplay = true;
		else
		{
			std::fprintf(stderr, "unknown option %s\n", argv[i]);
			return 2;
		}
	}

	try
	{
		LibSm64Config config;
		config.dllPath = dllPath;
		config.countryCode = CountryCode::SUPER_MARIO_64_J;
		config.saveMode = saveMode;

		std::cout << "DLL:   " << dllPath.string() << "\n";
		std::cout << "movie: " << m64Path.string() << "\n";
		std::cout << "frame: " << frame << " (" << LibSm64SaveModeName(saveMode) << " saves)\n\n";

		LibSm64 resource(config);

		M64 m64(m64Path);
		if (!m64.load())
		{
			std::fprintf(stderr, "error: could not load movie %s\n", m64Path.string().c_str());
			return 2;
		}

		// The layout report comes from the same script the pipeline runs before its first stage.
		std::cout << "Layout checks at frame " << frame << " (VerifyLayout):\n";
		auto layout = TopLevelScriptBuilder<VerifyLayout>::Build(m64).ImportResource(&resource).Run(frame, std::vector<ExpectedObject>());
		int failures = layout.failures;
		for (const std::string& line : layout.lines)
			std::cout << "  " << line << "\n";
		resource.LoadState(-1); // back to power-on: the measurements replay the movie on the resource itself

		Results results;
		results.saveMode = saveMode;
		results.leakScanFrames = leakScanFrames;
		results.listObjects = listObjects;
		results.dirtyScanFrames = dirtyScanFrames;
		results.dirtyReplay = dirtyReplay;
		PlayToFrame(resource, m64, frame, results).Run();

		if (!results.coverage.empty())
		{
			std::cout << "\nFixed slices at frame " << results.frameReached << ":\n";
			for (const std::string& line : results.coverage)
			{
				std::cout << "  " << line << "\n";
				if (line.rfind("FAIL: ", 0) == 0)
					failures++;
			}
		}

		if (listObjects)
		{
			std::cout << "\nActive objects at frame " << results.frameReached << " (" << results.objects.size() << "):\n";
			for (const std::string& line : results.objects)
				std::cout << "  " << line << "\n";
		}

		std::cout << "\nCost:\n";
		std::printf("  frame advance: %.1f us (%llu frames in %.0f ms, includes input write%s)\n",
			results.framesAdvanced ? results.playMicros / double(results.framesAdvanced) : 0.0,
			(unsigned long long)results.framesAdvanced, results.playMicros / 1000.0,
			results.dirtyReplay ? " and the per-frame snapshots of --dirty-replay" : "");
		std::printf("  save state:    %.1f us (%s saves, %d frames into the run)\n", results.saveMicros,
			LibSm64SaveModeName(saveMode), results.warmupFrames);
		std::printf("  load state:    %.1f us\n", results.loadMicros);
		if (results.haveDirtyPages)
			std::printf("  dirty pages:   %zu of %zu pages written since baseline %d (%zu KB per save), %llu first writes in all baselines\n",
				results.dirtyPagesWritten, results.dirtyPageCount, results.dirtyBaseline, results.dirtyPagesWritten * pagesize / 1024,
				(unsigned long long)results.dirtyFaults);
		std::printf("  addr() lookup: %.0f ns (GetProcAddress; never call per frame)\n", results.addrNanos);

		if (leakScanFrames > 0)
		{
			const char* names[2] = { ".data", ".bss" };
			auto print = [&](const char* title, const std::vector<LeakRange>& ranges, size_t bytes)
			{
				std::printf("\n%s: %llu range(s), %llu byte(s) not restored by the load (%s saves)\n", title,
					(unsigned long long)ranges.size(), (unsigned long long)bytes, LibSm64SaveModeName(saveMode));
				size_t shown = 0;
				for (const LeakRange& r : ranges)
				{
					if (shown++ == 60)
					{
						std::printf("  ... %llu more\n", (unsigned long long)(ranges.size() - 60));
						break;
					}
					std::printf("  %s+%llu %llu bytes\n", names[r.segment], (unsigned long long)r.offset, (unsigned long long)r.length);
				}
			};
			print("Leak scan, pass 1 (play, load)", results.leaksPass1, results.leakBytesPass1);
			print("Leak scan, pass 2 (different play, load)", results.leaksPass2, results.leakBytesPass2);
		}

		for (const std::string& line : results.dirtyReport)
			std::cout << line << "\n";

		if (failures > 0)
		{
			std::cout << "\n" << failures << " check(s) FAILED: the headers in tasfw-core/inc/sm64 do not describe this DLL build, or the fixed slices do not cover it.\n";
			return 1;
		}
		std::cout << "\nAll layout checks passed.\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "error: %s\n", e.what());
		return 2;
	}
}
