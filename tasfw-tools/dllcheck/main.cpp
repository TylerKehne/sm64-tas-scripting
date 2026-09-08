// dllcheck: is this libsm64 DLL the one our headers describe, and what does a frame cost?
//
//   dllcheck <libsm64.dll> <movie.m64> <frame> [--lightweight] [--leak-scan [frames]]
//
// Loads the DLL, plays the movie to <frame> (which should be inside a level), runs the
// LibSm64 layout self-check (ROADMAP 1.1) and prints one line per check. Also prints the
// measured cost of a frame advance and of a savestate save/load, which are the first Tier B
// numbers in docs/performance.md. Exit code 0 when every check passes, 1 on a failed check,
// 2 on usage or load errors.
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
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>

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
		std::vector<std::string> report;
		int leakScanFrames = 0;
		std::vector<LeakRange> leaksPass1; // differs after play + load
		std::vector<LeakRange> leaksPass2; // differs after a different play + load
		size_t leakBytesPass1 = 0;
		size_t leakBytesPass2 = 0;
	};

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

	// Everything that must happen "inside the level" happens inside execution(): a
	// TopLevelScript whose execution writes no inputs is rewound to its initial frame when it
	// returns (see ARCHITECTURE.md, "Modify moves the cursor"), so checking after Run() would
	// look at frame 0.
	class PlayToFrame : public TopLevelScript<LibSm64>
	{
	public:
		PlayToFrame(int64_t frame, Results& results) : _frame(frame), _results(results) {}

		bool validation() override { return true; }
		bool execution() override
		{
			auto start = std::chrono::steady_clock::now();
			LongLoad(_frame);
			_results.playMicros = MicrosecondsSince(start);
			_results.framesAdvanced = resource->nFrameAdvances;
			_results.frameReached = GetCurrentFrame();

			_results.report = resource->layoutCheckReport();

			// Cost of the savestate primitives, measured directly on the resource.
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

			// Cost of a symbol lookup (GetProcAddress through the loader). Scripts that call
			// addr() per frame pay this each time; see docs/performance.md.
			constexpr int addrReps = 2000;
			start = std::chrono::steady_clock::now();
			const void* sink = nullptr;
			for (int i = 0; i < addrReps; i++)
				sink = resource->addr((i & 1) ? "gMarioState" : "gCamera");
			_results.addrNanos = MicrosecondsSince(start) * 1000.0 / addrReps;
			(void)sink;

			if (_results.leakScanFrames > 0)
				LeakScan();
			return true;
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
		bool assertion() override { return true; }

	private:
		int64_t _frame;
		Results& _results;
	};
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: dllcheck <libsm64.dll> <movie.m64> <frame> [--lightweight]\n");
		return 2;
	}

	std::filesystem::path dllPath = argv[1];
	std::filesystem::path m64Path = argv[2];
	int64_t frame = std::stoll(argv[3]);
	bool lightweight = false;
	int leakScanFrames = 0;
	for (int i = 4; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--lightweight")
			lightweight = true;
		else if (arg == "--leak-scan")
		{
			leakScanFrames = 120;
			if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0]))
				leakScanFrames = std::atoi(argv[++i]);
		}
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
		config.lightweight = lightweight;

		std::cout << "DLL:   " << dllPath.string() << "\n";
		std::cout << "movie: " << m64Path.string() << "\n";
		std::cout << "frame: " << frame << (lightweight ? " (lightweight saves)" : " (full saves)") << "\n\n";

		LibSm64 resource(config);

		std::cout << "Layout checks at power-on:\n";
		for (const std::string& line : resource.layoutCheckReport())
			std::cout << "  " << line << "\n";

		M64 m64(m64Path);
		if (!m64.load())
		{
			std::fprintf(stderr, "error: could not load movie %s\n", m64Path.string().c_str());
			return 2;
		}

		Results results;
		results.leakScanFrames = leakScanFrames;
		TopLevelScriptBuilder<PlayToFrame>::Build(m64).ImportResource(&resource).Run(frame, results);

		std::cout << "\nLayout checks at frame " << results.frameReached << ":\n";
		int failures = 0;
		for (const std::string& line : results.report)
		{
			std::cout << "  " << line << "\n";
			if (line.rfind("FAIL: ", 0) == 0)
				failures++;
		}

		std::cout << "\nCost:\n";
		std::printf("  frame advance: %.1f us (%llu frames in %.0f ms, includes input write)\n",
			results.framesAdvanced ? results.playMicros / double(results.framesAdvanced) : 0.0,
			(unsigned long long)results.framesAdvanced, results.playMicros / 1000.0);
		std::printf("  save state:    %.1f us (%s)\n", results.saveMicros,
			!LibSm64LightweightSupported ? "dirty pages; lightweight is Windows-only" : lightweight ? "lightweight" : "full .data+.bss");
		std::printf("  load state:    %.1f us\n", results.loadMicros);
		std::printf("  addr() lookup: %.0f ns (GetProcAddress; never call per frame)\n", results.addrNanos);

		if (leakScanFrames > 0)
		{
			const char* names[2] = { ".data", ".bss" };
			auto print = [&](const char* title, const std::vector<LeakRange>& ranges, size_t bytes)
			{
				std::printf("\n%s: %llu range(s), %llu byte(s) not restored by the load (%s saves)\n", title,
					(unsigned long long)ranges.size(), (unsigned long long)bytes, lightweight ? "lightweight" : "full");
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

		if (failures > 0)
		{
			std::cout << "\n" << failures << " layout check(s) FAILED: the headers in tasfw-core/inc/sm64 do not describe this DLL build.\n";
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
