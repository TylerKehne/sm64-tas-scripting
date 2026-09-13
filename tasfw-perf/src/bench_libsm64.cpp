#include <benchmark/benchmark.h>
#include "measure.hpp"
#include "resident.hpp"

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/testing/Env.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

	Game* LoadGame(LibSm64SaveMode mode, benchmark::State& state)
	{
		static std::unique_ptr<Game> game;
		if (Env("TASFW_LIBSM64").empty() || Env("TASFW_M64").empty())
		{
			state.SkipWithMessage("TASFW_LIBSM64 / TASFW_M64 not set");
			return nullptr;
		}
		if (game && game->mode != mode)
		{
			state.SkipWithMessage("the DLL is already loaded in the other save mode; run this family in its own process");
			return nullptr;
		}
		if (!game)
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
			game = std::move(loaded);
		}
		return game.get();
	}

	// Save into a recycled slot and release it: the steady-state cost of a save once the
	// pool is warm, which is what a script pays per Save() during a search.
	void SaveErase(benchmark::State& state, LibSm64SaveMode mode)
	{
		Game* game = LoadGame(mode, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
		{
			int64_t id = resource.SaveState();
			resource.slotManager.EraseSlot(id);
		}
		tasfw_perf::EndMeasure(state, m0);
	}

	// Save into fresh storage: what a save costs when nothing has been released yet. Kept to
	// a fixed few iterations because every one of them keeps its buffers until the end.
	void SaveFresh(benchmark::State& state, LibSm64SaveMode mode)
	{
		Game* game = LoadGame(mode, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;
		resource.slotManager._pool.clear();
		resource.slotManager._pooledMem = 0;

		std::vector<int64_t> ids;
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			ids.push_back(resource.SaveState());
		tasfw_perf::EndMeasure(state, m0);

		for (int64_t id : ids)
			resource.slotManager.EraseSlot(id);
	}

	void Load(benchmark::State& state, LibSm64SaveMode mode)
	{
		Game* game = LoadGame(mode, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			resource.LoadState(game->anchor);
		tasfw_perf::EndMeasure(state, m0);
	}

	// One game frame with neutral inputs, from the anchor frame; the anchor is reloaded at
	// the end so the family's other benchmarks start from the same state.
	void FrameAdvance(benchmark::State& state, LibSm64SaveMode mode)
	{
		Game* game = LoadGame(mode, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		resource.setInputs(Inputs());
		tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
		for (auto _ : state)
			resource.FrameAdvance();
		tasfw_perf::EndMeasure(state, m0);
		resource.LoadState(game->anchor);
	}

	// Resident set per live slot: N fresh saves from the anchor state with the working set
	// sampled before and after, the pool cleared first so every slot is real storage.
	// stateBytes is the resource's own account of one state, exact and gated (it changes
	// only when the save layout does); rssPerSlot is what the process grew by per slot,
	// slot-map nodes included, and is informational. Skipped when N states would pass 4 GB
	// (full saves at 1,000 slots).
	void ResidentPerSlot(benchmark::State& state, LibSm64SaveMode mode)
	{
		Game* game = LoadGame(mode, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;
		const int64_t slots = state.range(0);
		const double stateBytes = double(resource.getStateSize(resource.slotManager.slotsById.at(game->anchor)));
		if (stateBytes * double(slots) > 4.0 * 1024 * 1024 * 1024)
		{
			state.SkipWithMessage("more than 4 GB of states");
			return;
		}
		resource.slotManager._pool.clear();
		resource.slotManager._pooledMem = 0;

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
			resource.slotManager.EraseSlot(id);
		resource.slotManager._pool.clear();
		resource.slotManager._pooledMem = 0;
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
			resource.slotManager.EraseSlot(id);
		}
		state.SetItemsProcessed(state.iterations());
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
