#include <benchmark/benchmark.h>
#include "measure.hpp"
#include "resident.hpp"

#include <LibSm64.hpp>
#include <BitfsCorner.hpp>
#include <tasfw/testing/PerfAccess.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/M64.hpp>
#include <tasfw/testing/Env.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Tier B: the game DLL itself (docs/performance.md). Runs when the environment names a DLL
// and a movie, as for the libsm64 test:
//   TASFW_LIBSM64 = path to sm64_jp_N.dll     TASFW_M64 = path to the source movie
//   TASFW_FRAME   = frame inside a level (default 3330)
// scripts/perf.ps1 sets these from res/ when present; without them every benchmark here is
// skipped, so the suite still runs in CI.
//
// A DLL path loads once per process, so the three save modes are three families
// (^BM_LibSm64Full, ^BM_LibSm64Fixed, ^BM_LibSm64Dirty) that perf.ps1 runs in separate processes; one
// resource per process is played to the frame once, saved (the first slot of the run), played
// 60 more frames and saved again (the anchor every row measures from) and shared by the
// family. In every family the frame-advance row is registered last, so the rows that save or
// load measure the warm-up set: 3,000 idle frames from a moving Mario can end in a death,
// which reloads the level and dirties four times as many pages. Each family
// ends with a memory row (resident set per live slot), and a third family
// (^BM_LibSm64Scaling) measures thread scaling on one DLL copy per thread.
//
// The corner resource (BitfsCorner, ROADMAP 4.13) has the same rows in two more families:
// ^BM_BitfsCorner_ on one corner imported from the game at the anchor with every object, so
// that each row measures the corner at the frame, the Mario and the warm-up the game's rows
// measure; and ^BM_BitfsCornerScaling on one corner per thread, each loaded from that state.
// The game's one copy serves them all, the corner needing it only to import.

namespace
{
	using tasfw::testing::Env;

	struct Game
	{
		std::unique_ptr<LibSm64> resource;
		LibSm64SaveMode mode = LibSm64SaveMode::Full;
		int64_t frame = 0;
		int64_t first = -1;  // the run's first slot, at `frame`: where the dirty mode takes its baseline
		int64_t anchor = -1; // slot WarmupFrames later, to measure from and return to after advancing
	};

	// The same input pattern dllcheck plays for its warm-up. Saves and loads are measured
	// WarmupFrames after the run's first slot, so that a dirty save has the pages a pellet
	// dirties to copy and a load has them to restore; at the first slot itself the dirty set is
	// empty and both would measure nothing (the 0.1 us rows of 2026-09-12).
	constexpr int WarmupFrames = 60;
	Inputs PatternInputs(int i)
	{
		double angle = i * 0.37;
		uint16_t buttons = 0;
		if (i % 7 == 0)
			buttons |= 0x8000; // A
		if (i % 13 == 0)
			buttons |= 0x4000; // B
		return Inputs(buttons, int8_t(60.0 * std::cos(angle)), int8_t(60.0 * std::sin(angle)));
	}

	template <class TResource>
	int64_t AnchorAfterWarmup(TResource& resource, int64_t& first)
	{
		first = resource.SaveState();
		for (int i = 0; i < WarmupFrames; i++)
		{
			resource.setInputs(PatternInputs(i));
			resource.FrameAdvance();
		}
		return resource.SaveState();
	}

	std::unique_ptr<Game> processGame; // the process's one copy of the DLL

	Game* LoadGame(LibSm64SaveMode mode, benchmark::State& state)
	{
		if (Env("TASFW_LIBSM64").empty() || Env("TASFW_M64").empty())
		{
			state.SkipWithMessage("TASFW_LIBSM64 / TASFW_M64 not set");
			return nullptr;
		}
		if (processGame && processGame->mode != mode)
		{
			state.SkipWithMessage("the DLL is already loaded in the other save mode; run this family in its own process");
			return nullptr;
		}
		if (!processGame)
		{
			LibSm64Config config;
			config.dllPath = Env("TASFW_LIBSM64");
			config.countryCode = CountryCode::SUPER_MARIO_64_J;
			config.saveMode = mode;

			auto loaded = std::make_unique<Game>();
			loaded->mode = mode;
			loaded->frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));
			loaded->resource = std::make_unique<LibSm64>(config);

			M64 m64(Env("TASFW_M64"));
			if (!m64.load())
			{
				state.SkipWithMessage("cannot load TASFW_M64");
				return nullptr;
			}
			LibSm64& resource = *loaded->resource;
			for (int64_t f = 0; f < loaded->frame; f++)
			{
				auto inputs = m64.frames.find(uint64_t(f));
				resource.setInputs(inputs != m64.frames.end() ? inputs->second : Inputs());
				resource.FrameAdvance();
			}
			loaded->anchor = AnchorAfterWarmup(resource, loaded->first);
			processGame = std::move(loaded);
		}
		return processGame.get();
	}

	// The rows, on either resource: the game's by save mode, the corner's at the end.
	//
	// Save into a recycled slot and release it: the steady-state cost of a save once the
	// pool is warm, which is what a script pays per Save() during a search.
	template <class TResource>
	void SaveEraseLoop(benchmark::State& state, TResource& resource)
	{
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
		{
			int64_t id = resource.SaveState();
			resource.DisposeState(id);
		}
		tasfw_perf::EndMeasure(state, m0);
	}

	void SaveErase(benchmark::State& state, LibSm64SaveMode mode)
	{
		if (Game* game = LoadGame(mode, state))
			SaveEraseLoop(state, *game->resource);
	}

	// Save into fresh storage: what a save costs when nothing has been released yet. Kept to
	// a fixed few iterations because every one of them keeps its buffers until the end.
	template <class TResource>
	void SaveFreshLoop(benchmark::State& state, TResource& resource)
	{
		PerfAccess::DropPool(resource);

		std::vector<int64_t> ids;
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			ids.push_back(resource.SaveState());
		tasfw_perf::EndMeasure(state, m0);

		for (int64_t id : ids)
			resource.DisposeState(id);
	}

	void SaveFresh(benchmark::State& state, LibSm64SaveMode mode)
	{
		if (Game* game = LoadGame(mode, state))
			SaveFreshLoop(state, *game->resource);
	}

	template <class TResource>
	void LoadLoop(benchmark::State& state, TResource& resource, int64_t anchor)
	{
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			resource.LoadState(anchor);
		tasfw_perf::EndMeasure(state, m0);
	}

	void Load(benchmark::State& state, LibSm64SaveMode mode)
	{
		if (Game* game = LoadGame(mode, state))
			LoadLoop(state, *game->resource, game->anchor);
	}

	// One game frame with neutral inputs, from the anchor frame; the anchor is reloaded at
	// the end so the family's other benchmarks start from the same state.
	template <class TResource>
	void FrameAdvanceLoop(benchmark::State& state, TResource& resource, int64_t anchor)
	{
		resource.setInputs(Inputs());
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			resource.FrameAdvance();
		tasfw_perf::EndMeasure(state, m0);
		resource.LoadState(anchor);
	}

	void FrameAdvance(benchmark::State& state, LibSm64SaveMode mode)
	{
		if (Game* game = LoadGame(mode, state))
			FrameAdvanceLoop(state, *game->resource, game->anchor);
	}

	// Resident set per live slot: N fresh saves from the anchor state with the working set
	// sampled before and after, the pool cleared first so every slot is real storage.
	// stateBytes is the resource's own account of one state, exact and gated (it changes
	// only when the save layout does); rssPerSlot is what the process grew by per slot,
	// slot-map nodes included, and is informational. Skipped when N states would pass 4 GB
	// (full saves at 1,000 slots).
	template <class TResource>
	void ResidentPerSlotLoop(benchmark::State& state, TResource& resource, int64_t anchor)
	{
		const int64_t slots = state.range(0);
		const double stateBytes = double(resource.getStateSize(PerfAccess::SlotState(resource, anchor)));
		if (stateBytes * double(slots) > 4.0 * 1024 * 1024 * 1024)
		{
			state.SkipWithMessage("more than 4 GB of states");
			return;
		}
		PerfAccess::DropPool(resource);

		std::vector<int64_t> ids;
		ids.reserve(size_t(slots));
		double rssPerSlot = 0;
		for (auto _ : state)
		{
			const int64_t before = int64_t(tasfw_perf::ResidentBytes());
			for (int64_t i = 0; i < slots; i++)
				ids.push_back(resource.SaveState());
			const int64_t after = int64_t(tasfw_perf::ResidentBytes());
			rssPerSlot = double(after - before) / double(slots);
		}
		state.counters["stateBytes"] = benchmark::Counter(stateBytes);
		state.counters["rssPerSlot"] = benchmark::Counter(rssPerSlot);
		for (int64_t id : ids)
			resource.DisposeState(id);
		PerfAccess::DropPool(resource);
	}

	void ResidentPerSlot(benchmark::State& state, LibSm64SaveMode mode)
	{
		if (Game* game = LoadGame(mode, state))
			ResidentPerSlotLoop(state, *game->resource, game->anchor);
	}

	// Thread scaling: n threads, each on its own DLL copy with dirty-page saves, as the
	// search runs. Thread i loads the copy whose trailing index is i + 1, derived from
	// TASFW_LIBSM64 (res/sm64_jp_0.dll -> sm64_jp_1.dll, ...), so the family can share a
	// process with the single-resource families, which hold copy 0. A copy is loaded and
	// played to the frame once and kept for every thread count. Every thread checks that
	// all copies for the run exist, so a run either measures or skips as a whole. Google
	// Benchmark reports time and items_per_second per thread for these rows (the aggregate
	// rate is n times the row's); scripts/perf_compare.py turns the per-thread rate at n
	// threads over the rate at one thread into efficiency and gates it (docs/performance.md).
	struct ThreadGame
	{
		std::unique_ptr<LibSm64> resource;
		int64_t first = -1;
		int64_t anchor = -1;
	};

	std::filesystem::path DllCopyPath(int index)
	{
		const std::filesystem::path base = Env("TASFW_LIBSM64");
		std::string stem = base.stem().string();
		while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back())))
			stem.pop_back();
		return base.parent_path() / (stem + std::to_string(index) + base.extension().string());
	}

	ThreadGame* LoadThreadGame(benchmark::State& state)
	{
		static std::mutex mutex;
		static std::map<int, std::unique_ptr<ThreadGame>> games;
		if (Env("TASFW_LIBSM64").empty() || Env("TASFW_M64").empty())
		{
			state.SkipWithMessage("TASFW_LIBSM64 / TASFW_M64 not set");
			return nullptr;
		}
		for (int i = 1; i <= state.threads(); i++)
		{
			if (!std::filesystem::exists(DllCopyPath(i)))
			{
				state.SkipWithMessage("no DLL copy for thread " + std::to_string(i) + ": " + DllCopyPath(i).string());
				return nullptr;
			}
		}

		const int index = state.thread_index() + 1;
		std::lock_guard<std::mutex> lock(mutex);
		auto found = games.find(index);
		if (found != games.end())
			return found->second.get();

		LibSm64Config config;
		config.dllPath = DllCopyPath(index);
		config.countryCode = CountryCode::SUPER_MARIO_64_J;
		config.saveMode = LibSm64SaveMode::Dirty;
		auto loaded = std::make_unique<ThreadGame>();
		loaded->resource = std::make_unique<LibSm64>(config);

		M64 m64(Env("TASFW_M64"));
		if (!m64.load())
		{
			state.SkipWithMessage("cannot load TASFW_M64");
			return nullptr;
		}
		const int64_t frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));
		LibSm64& resource = *loaded->resource;
		for (int64_t f = 0; f < frame; f++)
		{
			auto inputs = m64.frames.find(uint64_t(f));
			resource.setInputs(inputs != m64.frames.end() ? inputs->second : Inputs());
			resource.FrameAdvance();
		}
		loaded->anchor = AnchorAfterWarmup(resource, loaded->first);
		return (games[index] = std::move(loaded)).get();
	}

	void ScalingFrameAdvance(benchmark::State& state)
	{
		ThreadGame* game = LoadThreadGame(state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;
		resource.setInputs(Inputs());
		for (auto _ : state)
			resource.FrameAdvance();
		state.SetItemsProcessed(state.iterations());
		resource.LoadState(game->anchor);
	}

	void ScalingSaveErase(benchmark::State& state)
	{
		ThreadGame* game = LoadThreadGame(state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;
		for (auto _ : state)
		{
			int64_t id = resource.SaveState();
			resource.DisposeState(id);
		}
		state.SetItemsProcessed(state.iterations());
	}

	// Symbol lookup, the names the BitFS scripts ask for at the top of every validation()
	// and execution() (ROADMAP 3.7). Through the OS loader this was GetProcAddress under the
	// loader lock, which the threads of a search contended on: 5.6 us per call at 16 threads
	// against 61 ns alone; LibSm64::addr's table ended that (docs/performance-changelog.md,
	// 2026-09-14). One pinned thread here: a 16 ns lookup's per-thread time on 16 unpinned
	// threads is the hybrid scheduler's (it moved 20% between runs of one binary) and the
	// contention it would show is gone by construction, there being no shared lock.
	constexpr const char* ScriptSymbols[] = { "gMarioState", "gCamera", "gObjectPool", "bhvBitfsTiltingInvertedPyramid" };

	template <class TGame>
	void AddrLoop(benchmark::State& state, TGame* game)
	{
		if (!game)
			return;
		auto& resource = *game->resource;
		size_t i = 0;
		for (auto _ : state)
		{
			benchmark::DoNotOptimize(resource.addr(ScriptSymbols[i]));
			i = (i + 1) % std::size(ScriptSymbols);
		}
		state.SetItemsProcessed(state.iterations());
	}

	void Addr(benchmark::State& state, LibSm64SaveMode mode) { AddrLoop(state, LoadGame(mode, state)); }

	// The corner (BitfsCorner, ROADMAP 4.13): its level read once from the game, one
	// resource imported from the game at the anchor with every object (the search's shape),
	// its anchor that imported state. The game in any save mode serves, the one this process
	// has loaded or else the search's.
	Game* LoadGameAnyMode(benchmark::State& state)
	{
		return LoadGame(processGame ? processGame->mode : LibSm64SaveMode::Fixed, state);
	}

	struct Corner
	{
		std::shared_ptr<const BitfsCornerLevel> level;
		std::unique_ptr<BitfsCorner> resource;
		int64_t anchor = -1;
		int64_t window = 0; // neutral frames from the anchor before the corner refuses one
	};

	// The frame-advance rows play neutral frames from the anchor, and the game's play through
	// Mario's death there (the level reloads; the row's comment above). The corner refuses a
	// death, a warp it has not got, so its rows play the window before it, the anchor state
	// copied back whenever the window runs out, with the timing paused; the window is found
	// once, by playing to the refusal, and reported on the row (windowFrames).
	constexpr int64_t FrameAdvanceIterations = 3000;

	int64_t NeutralFramesBeforeRefusal(BitfsCorner& resource, int64_t anchor)
	{
		resource.setInputs(Inputs());
		int64_t frames = 0;
		try
		{
			for (; frames < FrameAdvanceIterations; frames++)
				resource.FrameAdvance();
		}
		catch (const std::exception&)
		{
		}
		resource.LoadState(anchor);
		return frames;
	}

	Corner* LoadCorner(benchmark::State& state)
	{
		static std::unique_ptr<Corner> corner;
		if (!corner)
		{
			Game* game = LoadGameAnyMode(state);
			if (!game)
				return nullptr;
			LibSm64& resource = *game->resource;
			resource.LoadState(game->anchor);
			auto loaded = std::make_unique<Corner>();
			loaded->level = std::make_shared<const BitfsCornerLevel>(resource);
			loaded->resource = std::make_unique<BitfsCorner>(loaded->level);
			loaded->resource->load(BitfsCornerMem(resource, *loaded->level, BitfsCornerObjects::All()));
			loaded->anchor = loaded->resource->SaveState();
			loaded->window = NeutralFramesBeforeRefusal(*loaded->resource, loaded->anchor);
			corner = std::move(loaded);
		}
		return corner.get();
	}

	// One corner frame with neutral inputs inside the window; the anchor is reloaded at the
	// end as the game's row does.
	void CornerFrameAdvanceLoop(benchmark::State& state, BitfsCorner& resource, int64_t anchor, int64_t window)
	{
		const Sm64Mem& anchorState = PerfAccess::SlotState(resource, anchor);
		resource.setInputs(Inputs());
		int64_t played = 0;
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
		{
			if (played == window)
			{
				state.PauseTiming();
				resource.load(anchorState); // the plain copy: no slot, no allocation to count
				played = 0;
				state.ResumeTiming();
			}
			resource.FrameAdvance();
			played++;
		}
		tasfw_perf::EndMeasure(state, m0);
		state.counters["windowFrames"] = benchmark::Counter(double(window));
		resource.LoadState(anchor);
	}

	// A refusal (the corner reaching what it has not got; BitfsCorner.hpp) ends the row with
	// its message, not the process.
	template <class TRow>
	void CornerRow(benchmark::State& state, TRow&& row)
	{
		try
		{
			if (Corner* corner = LoadCorner(state))
				row(*corner);
		}
		catch (const std::exception& e)
		{
			state.SkipWithError(e.what());
		}
	}

	void CornerSaveErase(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { SaveEraseLoop(state, *corner.resource); });
	}

	void CornerSaveFresh(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { SaveFreshLoop(state, *corner.resource); });
	}

	void CornerLoad(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { LoadLoop(state, *corner.resource, corner.anchor); });
	}

	void CornerFrameAdvance(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { CornerFrameAdvanceLoop(state, *corner.resource, corner.anchor, corner.window); });
	}

	void CornerResidentPerSlot(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { ResidentPerSlotLoop(state, *corner.resource, corner.anchor); });
	}

	void CornerAddr(benchmark::State& state)
	{
		CornerRow(state, [&](Corner& corner) { AddrLoop(state, &corner); });
	}

	// Thread scaling for the corner: n threads, one corner each on the shared level, loaded
	// from the single corner's anchor state (a load between resources relocates it), the
	// shape a search on the corner has. Nothing is copied but the state; the level is read
	// once and the game not loaded again.
	struct ThreadCorner
	{
		std::unique_ptr<BitfsCorner> resource;
		int64_t anchor = -1;
		int64_t window = 0;
	};

	ThreadCorner* LoadThreadCorner(benchmark::State& state)
	{
		static std::mutex mutex;
		static std::map<int, std::unique_ptr<ThreadCorner>> corners;
		std::lock_guard<std::mutex> lock(mutex);
		Corner* corner = LoadCorner(state);
		if (!corner)
			return nullptr;
		auto found = corners.find(state.thread_index());
		if (found != corners.end())
			return found->second.get();
		auto loaded = std::make_unique<ThreadCorner>();
		loaded->resource = std::make_unique<BitfsCorner>(corner->level);
		loaded->resource->load(PerfAccess::SlotState(*corner->resource, corner->anchor));
		loaded->anchor = loaded->resource->SaveState();
		loaded->window = corner->window;
		return (corners[state.thread_index()] = std::move(loaded)).get();
	}

	void CornerScalingFrameAdvance(benchmark::State& state)
	{
		try
		{
			ThreadCorner* corner = LoadThreadCorner(state);
			if (!corner)
				return;
			BitfsCorner& resource = *corner->resource;
			const Sm64Mem& anchorState = PerfAccess::SlotState(resource, corner->anchor);
			resource.setInputs(Inputs());
			int64_t played = 0;
			for (auto _ : state)
			{
				if (played == corner->window)
				{
					state.PauseTiming();
					resource.load(anchorState);
					played = 0;
					state.ResumeTiming();
				}
				resource.FrameAdvance();
				played++;
			}
			state.SetItemsProcessed(state.iterations());
			resource.LoadState(corner->anchor);
		}
		catch (const std::exception& e)
		{
			state.SkipWithError(e.what());
		}
	}

	void CornerScalingSaveErase(benchmark::State& state)
	{
		try
		{
			ThreadCorner* corner = LoadThreadCorner(state);
			if (!corner)
				return;
			BitfsCorner& resource = *corner->resource;
			for (auto _ : state)
			{
				int64_t id = resource.SaveState();
				resource.DisposeState(id);
			}
			state.SetItemsProcessed(state.iterations());
		}
		catch (const std::exception& e)
		{
			state.SkipWithError(e.what());
		}
	}
}

static void BM_LibSm64Full_SaveErase(benchmark::State& state) { SaveErase(state, LibSm64SaveMode::Full); }
static void BM_LibSm64Full_SaveFresh(benchmark::State& state) { SaveFresh(state, LibSm64SaveMode::Full); }
static void BM_LibSm64Full_Load(benchmark::State& state) { Load(state, LibSm64SaveMode::Full); }
static void BM_LibSm64Full_FrameAdvance(benchmark::State& state) { FrameAdvance(state, LibSm64SaveMode::Full); }
BENCHMARK(BM_LibSm64Full_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Full_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(40);
BENCHMARK(BM_LibSm64Full_Load)->Unit(benchmark::kMicrosecond);
static void BM_LibSm64Full_ResidentPerSlot(benchmark::State& state) { ResidentPerSlot(state, LibSm64SaveMode::Full); }
BENCHMARK(BM_LibSm64Full_ResidentPerSlot)->Unit(benchmark::kMillisecond)->Arg(100)->Iterations(1);
BENCHMARK(BM_LibSm64Full_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000);

static void BM_LibSm64Fixed_SaveErase(benchmark::State& state) { SaveErase(state, LibSm64SaveMode::Fixed); }
static void BM_LibSm64Fixed_SaveFresh(benchmark::State& state) { SaveFresh(state, LibSm64SaveMode::Fixed); }
static void BM_LibSm64Fixed_Load(benchmark::State& state) { Load(state, LibSm64SaveMode::Fixed); }
static void BM_LibSm64Fixed_FrameAdvance(benchmark::State& state) { FrameAdvance(state, LibSm64SaveMode::Fixed); }
BENCHMARK(BM_LibSm64Fixed_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Fixed_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(200);
BENCHMARK(BM_LibSm64Fixed_Load)->Unit(benchmark::kMicrosecond);
static void BM_LibSm64Fixed_ResidentPerSlot(benchmark::State& state) { ResidentPerSlot(state, LibSm64SaveMode::Fixed); }
BENCHMARK(BM_LibSm64Fixed_ResidentPerSlot)->Unit(benchmark::kMillisecond)->Arg(100)->Arg(1000)->Iterations(1);
static void BM_LibSm64Fixed_Addr(benchmark::State& state) { Addr(state, LibSm64SaveMode::Fixed); }
BENCHMARK(BM_LibSm64Fixed_Addr)->Unit(benchmark::kNanosecond)->Iterations(200000);
BENCHMARK(BM_LibSm64Fixed_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000);

static void BM_LibSm64Dirty_SaveErase(benchmark::State& state) { SaveErase(state, LibSm64SaveMode::Dirty); }
static void BM_LibSm64Dirty_SaveFresh(benchmark::State& state) { SaveFresh(state, LibSm64SaveMode::Dirty); }
static void BM_LibSm64Dirty_Load(benchmark::State& state) { Load(state, LibSm64SaveMode::Dirty); }
static void BM_LibSm64Dirty_FrameAdvance(benchmark::State& state) { FrameAdvance(state, LibSm64SaveMode::Dirty); }
BENCHMARK(BM_LibSm64Dirty_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Dirty_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(200);
BENCHMARK(BM_LibSm64Dirty_Load)->Unit(benchmark::kMicrosecond);
static void BM_LibSm64Dirty_ResidentPerSlot(benchmark::State& state) { ResidentPerSlot(state, LibSm64SaveMode::Dirty); }
BENCHMARK(BM_LibSm64Dirty_ResidentPerSlot)->Unit(benchmark::kMillisecond)->Arg(100)->Arg(1000)->Iterations(1);
BENCHMARK(BM_LibSm64Dirty_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000);

static void BM_LibSm64Scaling_FrameAdvance(benchmark::State& state) { ScalingFrameAdvance(state); }
static void BM_LibSm64Scaling_SaveErase(benchmark::State& state) { ScalingSaveErase(state); }
BENCHMARK(BM_LibSm64Scaling_SaveErase)->Unit(benchmark::kMicrosecond)->Iterations(2000)->UseRealTime()->ThreadRange(1, 16);
BENCHMARK(BM_LibSm64Scaling_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000)->UseRealTime()->ThreadRange(1, 16);

static void BM_BitfsCorner_SaveErase(benchmark::State& state) { CornerSaveErase(state); }
static void BM_BitfsCorner_SaveFresh(benchmark::State& state) { CornerSaveFresh(state); }
static void BM_BitfsCorner_Load(benchmark::State& state) { CornerLoad(state); }
static void BM_BitfsCorner_FrameAdvance(benchmark::State& state) { CornerFrameAdvance(state); }
BENCHMARK(BM_BitfsCorner_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_BitfsCorner_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(200);
BENCHMARK(BM_BitfsCorner_Load)->Unit(benchmark::kMicrosecond);
static void BM_BitfsCorner_ResidentPerSlot(benchmark::State& state) { CornerResidentPerSlot(state); }
BENCHMARK(BM_BitfsCorner_ResidentPerSlot)->Unit(benchmark::kMillisecond)->Arg(100)->Arg(1000)->Iterations(1);
static void BM_BitfsCorner_Addr(benchmark::State& state) { CornerAddr(state); }
BENCHMARK(BM_BitfsCorner_Addr)->Unit(benchmark::kNanosecond)->Iterations(200000);
BENCHMARK(BM_BitfsCorner_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(FrameAdvanceIterations);

static void BM_BitfsCornerScaling_FrameAdvance(benchmark::State& state) { CornerScalingFrameAdvance(state); }
static void BM_BitfsCornerScaling_SaveErase(benchmark::State& state) { CornerScalingSaveErase(state); }
BENCHMARK(BM_BitfsCornerScaling_SaveErase)->Unit(benchmark::kMicrosecond)->Iterations(2000)->UseRealTime()->ThreadRange(1, 16);
BENCHMARK(BM_BitfsCornerScaling_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(FrameAdvanceIterations)->UseRealTime()->ThreadRange(1, 16);
