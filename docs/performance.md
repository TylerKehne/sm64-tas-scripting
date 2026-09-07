# Performance

Performance is a first-class requirement, equal to correctness. The framework exists to run
millions of game frames per search, and it is written in C++ for that reason. A change that
is cleaner but slower is a regression. It is rejected unless the slowdown is measured,
explained, and explicitly accepted in the PR.

This document is the specification for how performance is measured and gated. The
implementation is ROADMAP item 1.3; until it lands, the "manual" section at the end applies.

## Design principle: zero-cost abstractions

The target is abstractions that disappear at compile time. The framework already leans that
way: `Script<TResource>`, `TopLevelScript<TResource, TStateTracker>` and the scattershot
classes are templates constrained by concepts, so the resource type, the tracker type and
the state bin type are all static; `if constexpr` compiles state tracking out entirely when
the tracker is `DefaultStateTracker`; the whole build uses LTO. Keep going in that direction.

Concretely, in code that runs per frame or per script:

- Static dispatch over virtual calls. If a virtual call is unavoidable, mark the concrete
  class `final` so LTO can devirtualize it, and measure.
- No `dynamic_cast`, no `std::function`, no `shared_ptr` copies, no string-keyed lookups.
- No heap allocation. Reserve up front or reuse buffers.
- Resolve anything that can be resolved once (symbol addresses, table sizes, config) at
  construction, not at use.
- Generic code is fine; generic code that pays for genericity at runtime is not.

Places where the code currently pays for an abstraction it should not (all are cheap to fix
and should be gated by the suite once it exists):

- `Script::SetInputs` calls `resource->addr("gControllerPads")` three times per frame, and
  `LibSm64::advance` calls `dll.get("sm64_update")` per frame. Each is a `GetProcAddress`
  string lookup through the OS loader. Cache the pointers in `LibSm64` at construction.
- Scripts re-resolve `gMarioState`, `gCamera`, behaviors and the object pool by string at
  the top of every `validation()` / `execution()`.
- `Script::GetTrackedState` performs a `dynamic_cast` on the root script per call, and
  tracking goes through virtual hooks on `_rootScript` on every frame advance.
- `Resource::save` / `load` / `advance` / `addr` are virtual and called per frame.
- Block segments are `std::shared_ptr<Segment>` chains, touched on every decode.
- `Script` bookkeeping is `unordered_map<int64_t, std::map<...>>` per ad-hoc level with
  `operator[]` default-inserts on the hot path.

## What costs what

Ordered by how much they dominate a typical scattershot run:

1. **Frame advance** (`sm64_update`). Fixed cost per frame, on the order of tens of
   microseconds. Everything else exists to advance fewer frames per useful frame of output.
2. **Savestate save/load.** `memcpy` of about 1.5 MB in lightweight mode or about 7.3 MB for a
   full `.data` + `.bss` copy. Memory-bandwidth bound, so it scales worse than frame advance
   as thread count rises.
3. **Block decoding.** Every scattershot shot replays the base block's segment chain from the
   root by re-running scripts. Cost grows with block depth over the run and shows up as
   "Overhead" in the end-of-run summary.
4. **State trackers.** They run at every frame advance and load. A tracker that itself advances
   frames (for example `StateTracker_BitfsDr::CalculateOscillations` runs up to 50 frames per
   crossing) multiplies the cost of every frame it is evaluated on.
5. **Script bookkeeping.** `GetInputsMetadata`, `GetLatestSave`, the per-level caches and
   tracked-state maps are `std::map` / `unordered_map` operations per frame, per hierarchy level.
6. **Synchronization.** Named `omp critical` sections in scattershot; `Deterministic` mode adds
   a barrier per script through `QueueThreadById`.
7. **`PyramidUpdate` construction.** `ImportSave<PyramidUpdateMem>` reads and transforms every
   pyramid surface out of the DLL each time it is called, which is once per frame in
   `RunDownhill` and once per crossing in the trackers.

## Existing instrumentation

Use it and extend it rather than adding ad-hoc timers:

- `Resource`: `nFrameAdvances`, `nLoads`, `nSaves` and rdtsc totals for each.
- `BaseScriptStatus` per script and per ad-hoc level: validation/execution/assertion
  durations, save/load/advance durations, and counts.
- Scattershot end-of-run summary: Load / Save / Frame Advance / Overhead / Other percentages,
  plus Futility / Redundancy / Discovery ratios and CSV row counts.
- Known inconsistency: `ExecuteAdhocBase` records `executionDuration` in milliseconds via
  `std::chrono` while everything else is rdtsc cycles. Unifying this is ROADMAP 3.6.

## The performance test suite

Location: a `tasfw-perf` target tree, built and run only in `Release` or `RelWithDebInfo`.
Debug builds use `/Od /RTC1` and are meaningless for performance. Every benchmark emits JSON
(git sha, config, CPU model, thread count, metrics); baselines are checked in under
`perf/baselines/` per machine class, and a compare script prints a delta table.

Two kinds of metric are recorded for every workload:

- **Counts** (frame advances, saves, loads, blocks, hash probes, allocations). These are
  machine-independent and, in deterministic mode, exactly reproducible. They are gated on
  exact equality.
- **Times** (wall, per-operation mean, p50, p99). These are gated with a tolerance and tracked
  over time.

Counts catch the regressions that matter most, which are "the framework now does more work",
independent of machine noise.

### Tier A: microbenchmarks (no DLL, run in CI on every PR)

Google Benchmark via FetchContent. Cases:

- `Scattershot::GetHash` over `BinaryStateBin<16>`, including the rehash-on-collision path.
- `BinaryStateBin::AddRegionBitsByNRegions` / `AddRegionBitsByRegionSize`.
- `Inputs::GetClosestInputByYawHau` and `GetClosestInputByYawExact` across a yaw x magnitude
  grid; `GetIntendedYawMagFromInput`.
- `M64::load` / `M64::save` on a 10,000-frame movie; `M64Diff` merge as done by `ApplyChildDiff`.
- `SlotManager` with a fake `Resource`: `CreateSlot`, `LoadSlot`, `EraseOldestSlot` at 100,
  1,000 and 10,000 live slots.
- `Script` bookkeeping with a fake `Resource`: `GetInputsMetadata` and `GetLatestSave` at
  hierarchy depth 1, 4 and 16 and ad-hoc level 0 and 4; `AdvanceFrameWrite` erase cost with
  10,000 cached frames.

Gate: time within 10% of baseline; allocation counts exact where measured.

### Tier B: resource benchmarks (DLL required)

- Frame advance: mean, p50, p99 microseconds over 10,000 frames starting at the BitFS start frame.
- `save` / `load`: full versus lightweight, microseconds and GB/s.
- Thread scaling: aggregate frames/s and saves/s at 1, 2, 4, 8 and 16 threads, each with its
  own DLL copy, reported as efficiency relative to one thread.
- Memory: resident set per resource with 100 and 1,000 live slots.

Gate: counts exact; times within 10%; scaling efficiency at 8 threads must not fall below the
baseline by more than 5 points.

### Tier C: framework benchmarks (DLL required, deterministic)

Fixed workloads on the source movie:

- `BitFsPyramidOscillation` from a fixed frame.
- `BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle` through `PyramidUpdate`, 1,000 calls.
- `StateTracker_BitfsDr` swept over 500 consecutive frames.

Metrics: wall time; `nFrameAdvances`, `nLoads`, `nSaves`; **replay ratio** = frames advanced
per frame of output diff; **overhead %** = 1 minus (advance + save + load time) / wall.

Gate: counts exactly equal to baseline; wall within 10%; overhead % no more than 2 points
above baseline.

### Tier D: scattershot end to end (DLL required)

- Deterministic run: `Deterministic = true`, fixed `Seed`, `MaxShots = 2000`, 8 threads.
  Exact equality on `TotalShots`, `ScriptCount`, block count, solution count and the sum of
  `nFrameAdvances` across threads; wall within 10%.
- Throughput run: `Deterministic = false`, fixed `MaxShots`, 16 threads. Shots/s, scripts/s,
  novel blocks/s, decode overhead %, and peak resident set. Wall within 10%.

Both runs use a dedicated small configuration, never the pipeline in `main.cpp`.

### Reporting and gating

- Tier A runs in CI on every PR.
- Tiers B, C and D run locally before merging anything under `tasfw-core`,
  `tasfw-scattershot` or `tasfw-resources`. Paste the delta table into the PR description.
- Policy: any increase in a gated count, or more than 5% wall-time regression on B, C or D,
  blocks the merge unless the PR explains why and the maintainer accepts it.
- New hot-path features must add a benchmark in the tier that covers them.

## Profiling

- Build with `scripts\build.ps1 -Config RelWithDebInfo`. LTO is enabled for every config by
  `add_optimization_flags`, and the `/arch` flag is detected at configure time.
- MSVC's OpenMP is the 2.0 runtime (`-openmp`). `-openmp:llvm` is available if newer
  directives are needed; measure before switching.
- Tools that work with this code: Visual Studio Performance Profiler (CPU sampling handles
  OpenMP threads), Superluminal, Windows Performance Analyzer. In-process, the rdtsc counters
  above are the first thing to read.
- Never draw conclusions from a Debug build.

## Known hotspots to measure first

These are suspects, not verdicts. Measure before changing any of them.

1. Block decoding replaying from the root on every shot (ROADMAP 4.3).
2. `PyramidUpdateMem` construction copying and transforming all surfaces per call.
3. `CalculateOscillations` advancing up to 50 frames inside a tracker evaluation.
4. `UpsertBlock` hashing and probing while holding the `blocks` critical section.
5. `std::map` bookkeeping in `Script`, including `operator[]` default inserts on
   `unordered_map<int64_t, std::map<...>>` per ad-hoc level.
6. Console output under the `print` critical section every shot.
7. Barriers per script in `Deterministic` mode.
8. Per-thread 8 GB slot budget and the resulting eviction pattern under memory pressure.

## Rules of thumb for hot paths

- No heap allocation per frame in `Script` or `Resource` code paths; reserve or reuse.
- No `std::map` lookup per frame unless it replaces a frame advance.
- No I/O while holding a critical section.
- Anything that adds a frame advance needs a measured justification.
- Prefer counts over timings when writing a test; counts are deterministic.

## Until the suite exists

For any change under `tasfw-core`, `tasfw-scattershot` or `tasfw-resources`, run a fixed
workload before and after in `Release`, and report at minimum: wall time, `nFrameAdvances`,
`nSaves`, `nLoads`, and the scattershot summary percentages if applicable. State the workload
precisely so the numbers can be reproduced.
