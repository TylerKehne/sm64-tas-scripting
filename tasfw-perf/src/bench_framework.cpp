#include <benchmark/benchmark.h>
#include "measure.hpp"

#include <BitFSPyramidOscillation.hpp>
#include <LibSm64.hpp>
#include <PyramidUpdate.hpp>
#include <Scattershot_BitfsDr.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <tasfw/testing/Env.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// Tier C: fixed framework workloads on the real game (docs/performance.md). Gated on the
// same environment as Tier B (TASFW_LIBSM64, TASFW_M64, TASFW_FRAME) and skipped without it.
//
// Every workload runs with the resource's cost model off, so no automatic savestate is ever
// created and the counts reported here (frameAdvances, saves, loads) are exact functions of
// the code, not of timing; scripts/perf_compare.py gates them on equality. The family
// (^BM_Framework) runs in its own process: one resource is played to the anchor frame by
// hand, that state becomes the resource's start save, and every imported run begins by
// reloading it (TopLevelScript::MainImport), so iterations are independent.

namespace
{
	using tasfw::testing::Env;

	struct Game
	{
		std::unique_ptr<LibSm64> resource;
		std::unique_ptr<M64> m64;
		int64_t frame = 3330;
	};

	Game* LoadGame(benchmark::State& state)
	{
		static std::unique_ptr<Game> game;
		if (Env("TASFW_LIBSM64").empty() || Env("TASFW_M64").empty())
		{
			state.SkipWithMessage("TASFW_LIBSM64 / TASFW_M64 not set");
			return nullptr;
		}
		if (!game)
		{
			auto loaded = std::make_unique<Game>();
			loaded->m64 = std::make_unique<M64>(Env("TASFW_M64"));
			if (!loaded->m64->load())
			{
				state.SkipWithMessage("cannot load TASFW_M64");
				return nullptr;
			}

			LibSm64Config config;
			config.dllPath = Env("TASFW_LIBSM64");
			config.countryCode = CountryCode::SUPER_MARIO_64_J;
			config.saveMode = LibSm64SaveMode::Dirty;
			loaded->frame = Env("TASFW_FRAME").empty() ? 3330 : std::stoll(Env("TASFW_FRAME"));
			loaded->resource = std::make_unique<LibSm64>(config);
			loaded->resource->useCostModel = false;

			LibSm64& resource = *loaded->resource;
			for (int64_t f = 0; f < loaded->frame; f++)
			{
				auto inputs = loaded->m64->frames.find(uint64_t(f));
				resource.setInputs(inputs != loaded->m64->frames.end() ? inputs->second : Inputs());
				resource.FrameAdvance();
			}
			// The anchor is the start save from here on (what MainImport does on first use).
			resource.save(resource.startSave);
			resource.initialFrame = 0;
			game = std::move(loaded);
		}
		return game.get();
	}

	struct Work
	{
		uint64_t frameAdvances = 0;
		uint64_t saves = 0;
		uint64_t loads = 0;
		uint64_t advanceCycles = 0;
		uint64_t saveCycles = 0;
		uint64_t loadCycles = 0;
	};

	Work Snapshot(LibSm64& resource)
	{
		return { resource.nFrameAdvances, resource.nSaveStates, resource.nLoadStates,
			resource.GetTotalFrameAdvanceTime(), resource.GetTotalSaveStateTime(), resource.GetTotalLoadStateTime() };
	}

	// Exact counts per iteration, the replay ratio (frames advanced per frame of output)
	// and the framework overhead: the share of wall time spent outside the resource's own
	// advance, save and load (all measured in rdtsc cycles, so the units agree).
	void ReportWork(benchmark::State& state, const Work& before, const Work& after, uint64_t wallCycles, uint64_t outputFrames)
	{
		double iterations = state.iterations() > 0 ? double(state.iterations()) : 1.0;
		uint64_t advances = after.frameAdvances - before.frameAdvances;
		state.counters["frameAdvances"] = benchmark::Counter(double(advances) / iterations);
		state.counters["saves"] = benchmark::Counter(double(after.saves - before.saves) / iterations);
		state.counters["loads"] = benchmark::Counter(double(after.loads - before.loads) / iterations);
		state.counters["replayRatio"] = benchmark::Counter(outputFrames ? double(advances) / double(outputFrames) : 0.0);
		uint64_t inside = (after.advanceCycles - before.advanceCycles) + (after.saveCycles - before.saveCycles)
			+ (after.loadCycles - before.loadCycles);
		state.counters["overheadPct"] = benchmark::Counter(wallCycles ? 100.0 * (1.0 - double(inside) / double(wallCycles)) : 0.0);
	}

	// The single-threaded pyramid oscillation from the anchor frame, with the preamble the
	// pyramid-osc-approach stage uses (Stages.cpp): one stick input, then wait until idle.
	// Quadrant 4: from this movie's frame 3330 it is the quadrant that gives a real run of
	// child scripts in under a second (quadrant 3 takes 12 s, quadrants 1 and 2 fail at once).
	// Whether the oscillation asserts is reported as the exact count `solutions`, not
	// required; the workload is the same either way.
	class OscillationWorkload : public TopLevelScript<LibSm64>
	{
	public:
		struct Result
		{
			bool asserted = false;
			uint64_t diffFrames = 0;
		};

		explicit OscillationWorkload(Result& result) : _result(result) {}

		bool validation() override { return true; }
		bool execution() override
		{
			Camera* camera = *(Camera**)(resource->addr("gCamera"));
			MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
			auto stick = Inputs::GetClosestInputByYawExact(-16384, 32, camera->yaw);
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
			for (int waited = 0; marioState->action != ACT_IDLE; waited++)
			{
				if (waited > 1000)
					return false;
				AdvanceFrameWrite(Inputs(0, 0, 0));
			}

			auto oscillation = Modify<BitFsPyramidOscillation>(0.69f, 4, false);
			_result.asserted = oscillation.asserted;
			// The child's own output, kept in the status whether or not it was merged.
			_result.diffFrames = oscillation.m64Diff.frames.size();
			return true;
		}
		bool assertion() override { return true; }

	private:
		Result& _result;
	};

	// GetMinimumDownhillWalkingAngle the way BitFsPyramidOscillation::execution calls it: the
	// pyramid is imported from the game into a PyramidUpdate resource and the script runs on
	// that. The counts come from the script statuses (the PyramidUpdate resource is created
	// per call and not reachable from here); the game resource itself does nothing.
	class DownhillAngleWorkload : public TopLevelScript<LibSm64>
	{
	public:
		struct Result
		{
			bool ok = true;
			uint64_t frameAdvances = 0;
			uint64_t saves = 0;
			uint64_t loads = 0;
		};

		DownhillAngleWorkload(int calls, Result& result) : _calls(calls), _result(result) {}

		bool validation() override { return true; }
		bool execution() override
		{
			MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
			Object* pyramid = marioState->floor ? marioState->floor->object : nullptr;
			if (!pyramid)
			{
				_result.ok = false;
				return false;
			}

			M64 empty;
			for (int i = 0; i < _calls; i++)
			{
				int16_t targetAngle = int16_t(i * 4099); // walks the whole circle
				auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(empty)
					.ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
					.Run(targetAngle);
				_result.ok = _result.ok && status.validated && status.executed;
				_result.frameAdvances += status.nFrameAdvances;
				_result.saves += status.nSaves;
				_result.loads += status.nLoads;
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int _calls;
		Result& _result;
	};

	// StateTracker_BitfsDr over consecutive frames: the tracker runs once per advanced frame
	// and looks the previous frame up recursively, so the sweep costs one extra load and one
	// extra advance in total (the frame before the first advance) and nothing per frame.
	class TrackerSweepWorkload : public TopLevelScript<LibSm64, StateTracker_BitfsDr>
	{
	public:
		struct Result
		{
			bool initialized = false;
			int64_t frame = -1;
		};

		TrackerSweepWorkload(int frames, Result& result) : _frames(frames), _result(result) {}

		bool validation() override { return true; }
		bool execution() override
		{
			for (int i = 0; i < _frames; i++)
				AdvanceFrameRead();
			const auto& last = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
			_result.initialized = last.initialized;
			_result.frame = last.frame;
			return true;
		}
		bool assertion() override { return true; }

	private:
		int _frames;
		Result& _result;
	};

	NormalSpecsDto DrNormalSpecs()
	{
		// The committed dr-oscillations stage (config.json).
		NormalSpecsDto specs;
		specs.onlyMinMajor = true;
		specs.minXzSum = 0.69f;
		specs.minMajor = 0.3f;
		specs.maxMajor = 0.601f;
		specs.regionsMajor = 10000;
		specs.minMinor = 0.19f;
		specs.maxMinor = 0.2f;
		specs.regionsMinor = 10000;
		return specs;
	}
}

static void BM_Framework_PyramidOscillation(benchmark::State& state)
{
	Game* game = LoadGame(state);
	if (!game)
		return;
	LibSm64& resource = *game->resource;

	Work before = Snapshot(resource);
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	uint64_t wall = 0;
	uint64_t outputFrames = 0;
	uint64_t asserted = 0;
	for (auto _ : state)
	{
		OscillationWorkload::Result result;
		uint64_t t0 = __rdtsc();
		TopLevelScriptBuilder<OscillationWorkload>::Build(*game->m64).ImportResource(&resource).Run(result);
		wall += __rdtsc() - t0;
		outputFrames += result.diffFrames;
		asserted += result.asserted ? 1 : 0;
	}
	tasfw_perf::EndMeasure(state, m0);
	ReportWork(state, before, Snapshot(resource), wall, outputFrames);
	double iterations = state.iterations() > 0 ? double(state.iterations()) : 1.0;
	state.counters["solutions"] = benchmark::Counter(double(asserted) / iterations);
	state.counters["outputFrames"] = benchmark::Counter(double(outputFrames) / iterations);
}
BENCHMARK(BM_Framework_PyramidOscillation)->Unit(benchmark::kMillisecond)->Iterations(2);

static void BM_Framework_DownhillAngle_PyramidUpdate(benchmark::State& state)
{
	Game* game = LoadGame(state);
	if (!game)
		return;
	LibSm64& resource = *game->resource;
	const int calls = 1000;

	Work total;
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	bool failed = false;
	for (auto _ : state)
	{
		DownhillAngleWorkload::Result result;
		TopLevelScriptBuilder<DownhillAngleWorkload>::Build(*game->m64).ImportResource(&resource).Run(calls, result);
		if (!result.ok)
		{
			state.SkipWithError("GetMinimumDownhillWalkingAngle did not validate on the imported pyramid");
			failed = true;
			break;
		}
		total.frameAdvances += result.frameAdvances;
		total.saves += result.saves;
		total.loads += result.loads;
	}
	if (failed)
		return;
	tasfw_perf::EndMeasure(state, m0);
	double iterations = state.iterations() > 0 ? double(state.iterations()) : 1.0;
	state.counters["calls"] = benchmark::Counter(double(calls));
	state.counters["frameAdvances"] = benchmark::Counter(double(total.frameAdvances) / iterations);
	state.counters["saves"] = benchmark::Counter(double(total.saves) / iterations);
	state.counters["loads"] = benchmark::Counter(double(total.loads) / iterations);
}
BENCHMARK(BM_Framework_DownhillAngle_PyramidUpdate)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_Framework_TrackerSweep(benchmark::State& state)
{
	Game* game = LoadGame(state);
	if (!game)
		return;
	LibSm64& resource = *game->resource;
	const int frames = 500;
	NormalSpecsDto specs = DrNormalSpecs();

	Work before = Snapshot(resource);
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	uint64_t wall = 0;
	bool failed = false;
	for (auto _ : state)
	{
		TrackerSweepWorkload::Result result;
		uint64_t t0 = __rdtsc();
		TopLevelScriptBuilder<TrackerSweepWorkload>::Build(*game->m64)
			.ImportResource(&resource)
			.ConfigureStateTracker(int64_t(game->frame), 4, specs, 15, -0.17944f, 0.3936f)
			.Run(frames, result);
		wall += __rdtsc() - t0;
		if (!result.initialized || result.frame != game->frame + frames)
		{
			state.SkipWithError("the tracker did not produce a state for the last frame of the sweep");
			failed = true;
			break;
		}
	}
	if (failed)
		return;
	tasfw_perf::EndMeasure(state, m0);
	ReportWork(state, before, Snapshot(resource), wall, uint64_t(frames) * uint64_t(state.iterations()));
}
BENCHMARK(BM_Framework_TrackerSweep)->Unit(benchmark::kMillisecond)->Iterations(3);
