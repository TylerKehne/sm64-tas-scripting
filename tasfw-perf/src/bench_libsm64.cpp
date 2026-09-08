#include <benchmark/benchmark.h>
#include "alloc_counter.hpp"

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/testing/Env.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

// Tier B: the game DLL itself (docs/performance.md). Runs when the environment names a DLL
// and a movie, as for the libsm64 test:
//   TASFW_LIBSM64 = path to sm64_jp_N.dll     TASFW_M64 = path to the source movie
//   TASFW_FRAME   = frame inside a level (default 3330)
// scripts/perf.ps1 sets these from res/ when present; without them every benchmark here is
// skipped, so the suite still runs in CI.
//
// A DLL path loads once per process, so full and lightweight saves are two families
// (^BM_LibSm64Full, ^BM_LibSm64Light) that perf.ps1 runs in separate processes; one
// resource per process is played to the frame once and shared by the family.

namespace
{
	using tasfw::testing::Env;

	struct Game
	{
		std::unique_ptr<LibSm64> resource;
		bool lightweight = false;
		int64_t frame = 0;
		int64_t anchor = -1; // slot at `frame`, to return to after advancing
	};

	Game* LoadGame(bool lightweight, benchmark::State& state)
	{
		static std::unique_ptr<Game> game;
		if (Env("TASFW_LIBSM64").empty() || Env("TASFW_M64").empty())
		{
			state.SkipWithMessage("TASFW_LIBSM64 / TASFW_M64 not set");
			return nullptr;
		}
		if (game && game->lightweight != lightweight)
		{
			state.SkipWithMessage("the DLL is already loaded in the other save mode; run this family in its own process");
			return nullptr;
		}
		if (!game)
		{
			LibSm64Config config;
			config.dllPath = Env("TASFW_LIBSM64");
			config.countryCode = CountryCode::SUPER_MARIO_64_J;
			config.lightweight = lightweight;

			auto loaded = std::make_unique<Game>();
			loaded->lightweight = lightweight;
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
			loaded->anchor = resource.SaveState();
			game = std::move(loaded);
		}
		return game.get();
	}

	// Save into a recycled slot and release it: the steady-state cost of a save once the
	// pool is warm, which is what a script pays per Save() during a search.
	void SaveErase(benchmark::State& state, bool lightweight)
	{
		Game* game = LoadGame(lightweight, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		uint64_t allocs0 = tasfw_perf::AllocCount();
		for (auto _ : state)
		{
			int64_t id = resource.SaveState();
			resource.slotManager.EraseSlot(id);
		}
		tasfw_perf::ReportAllocs(state, allocs0);
	}

	// Save into fresh storage: what a save costs when nothing has been released yet. Kept to
	// a fixed few iterations because every one of them keeps its buffers until the end.
	void SaveFresh(benchmark::State& state, bool lightweight)
	{
		Game* game = LoadGame(lightweight, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;
		resource.slotManager._pool.clear();
		resource.slotManager._pooledMem = 0;

		std::vector<int64_t> ids;
		uint64_t allocs0 = tasfw_perf::AllocCount();
		for (auto _ : state)
			ids.push_back(resource.SaveState());
		tasfw_perf::ReportAllocs(state, allocs0);

		for (int64_t id : ids)
			resource.slotManager.EraseSlot(id);
	}

	void Load(benchmark::State& state, bool lightweight)
	{
		Game* game = LoadGame(lightweight, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		uint64_t allocs0 = tasfw_perf::AllocCount();
		for (auto _ : state)
			resource.LoadState(game->anchor);
		tasfw_perf::ReportAllocs(state, allocs0);
	}

	// One game frame with neutral inputs, from the anchor frame; the anchor is reloaded at
	// the end so the family's other benchmarks start from the same state.
	void FrameAdvance(benchmark::State& state, bool lightweight)
	{
		Game* game = LoadGame(lightweight, state);
		if (!game)
			return;
		LibSm64& resource = *game->resource;

		resource.setInputs(Inputs());
		uint64_t allocs0 = tasfw_perf::AllocCount();
		for (auto _ : state)
			resource.FrameAdvance();
		tasfw_perf::ReportAllocs(state, allocs0);
		resource.LoadState(game->anchor);
	}
}

static void BM_LibSm64Full_SaveErase(benchmark::State& state) { SaveErase(state, false); }
static void BM_LibSm64Full_SaveFresh(benchmark::State& state) { SaveFresh(state, false); }
static void BM_LibSm64Full_Load(benchmark::State& state) { Load(state, false); }
static void BM_LibSm64Full_FrameAdvance(benchmark::State& state) { FrameAdvance(state, false); }
BENCHMARK(BM_LibSm64Full_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Full_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(40);
BENCHMARK(BM_LibSm64Full_Load)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Full_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000);

static void BM_LibSm64Light_SaveErase(benchmark::State& state) { SaveErase(state, true); }
static void BM_LibSm64Light_SaveFresh(benchmark::State& state) { SaveFresh(state, true); }
static void BM_LibSm64Light_Load(benchmark::State& state) { Load(state, true); }
static void BM_LibSm64Light_FrameAdvance(benchmark::State& state) { FrameAdvance(state, true); }
BENCHMARK(BM_LibSm64Light_SaveErase)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Light_SaveFresh)->Unit(benchmark::kMicrosecond)->Iterations(200);
BENCHMARK(BM_LibSm64Light_Load)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LibSm64Light_FrameAdvance)->Unit(benchmark::kMicrosecond)->Iterations(3000);
