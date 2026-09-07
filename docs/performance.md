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

Places where the code pays (or paid) for an abstraction it should not, gated by the suite:

- ~~`Script::SetInputs` resolved `gControllerPads` three times per frame, `advance()`
  resolved `sm64_update`, `getCurrentFrame()` resolved `gGlobalTimer`, all via
  `GetProcAddress`.~~ Fixed: `Resource::setInputs()` and cached pointers (change log below).
- ~~`Script` bookkeeping was `unordered_map<int64_t, std::map<...>>` per ad-hoc level with
  `operator[]` default-inserts on the hot path.~~ Fixed: `LevelStack` (change log below).
- Scripts re-resolve `gMarioState`, `gCamera`, behaviors and the object pool by string at
  the top of every `validation()` / `execution()` (62 ns each on this DLL).
- `Script::GetTrackedState` performs a `dynamic_cast` on the root script per call, and
  tracking goes through virtual hooks on `_rootScript` on every frame advance.
- `Resource::save` / `load` / `advance` / `setInputs` are virtual and called per frame.
- Block segments are `std::shared_ptr<Segment>` chains, touched on every decode.
- Tracker scripts are constructed per tracked frame: six `std::map` head allocations on
  MSVC's STL, three lifecycle sandboxes, and a `CustomStatus` copy (with `std::vector`s in
  the real trackers).

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
   tracked-state maps are `std::map` operations per frame, per hierarchy level (the per-level
   containers themselves are a `LevelStack` and cost nothing to enter).
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

## Running the suite

Tier A is implemented in `tasfw-perf/` (Google Benchmark, fetched by CMake). Tiers B to D
are not yet written; see ROADMAP 1.3.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1                 # build Release, run, compare
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SaveBaseline   # store this run as the baseline
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -Filter Script -NoBuild
```

Results go to `perf\results\<timestamp>-<sha>.json` (gitignored). The baseline for a machine
is `perf\baselines\<computername>.json` (committed). `scripts\perf_compare.py` prints the delta
table and exits non-zero on any regression over the threshold (default 10%). Paste that table
into the PR.

Noise control, learned the hard way while setting this up:

- Each benchmark runs three repetitions in each of three fresh processes, and the comparison
  uses the **fastest** of the nine. External noise only ever adds time,
  so the minimum is the best estimate of intrinsic cost. Medians of three drifted 15 to 35%
  between runs of the same binary on a busy desktop.
- The benchmark process runs at High priority pinned to one logical CPU (`-Affinity`, default
  `0x10`), so other processes and the scheduler contribute less.
- Each benchmark family runs in its own process, several times. One slot benchmark measured
  168 ns in isolation and 335 ns when run after the allocation-heavy scattershot and m64
  families in the same process; heap state carries over between benchmarks.
- The allocating benchmarks (`Script`, `SlotManager`) use fixed iteration counts, so the
  heap state entering each benchmark does not depend on how many iterations the previous
  ones happened to run.
- The runner does one throwaway launch before measuring. On Windows, the **first launch of
  a freshly written executable** measures differently from every later launch of the same
  file: `Execute_ChildEmpty` took 2.85 us on the first launch and 2.1 us on every launch
  after, reproduced on demand by copying the exe to a new name, and the allocation-heavy
  benchmarks (`ExecuteAdhoc_Empty`, 430 vs 630 ns) flipped mode with it. Cause not
  identified (image placement or prefetch are the suspects); the throwaway launch makes it
  irrelevant. The allocation churn that makes these benchmarks layout-sensitive is itself a
  ROADMAP 3.7 target.
- Deltas under 1 ns in absolute terms never count, so sub-nanosecond benchmarks cannot trip
  the gate on jitter.

If a result still looks like noise, rerun with `-Repetitions 9` and close other programs
before believing it. Never run two benchmark processes at once, and never benchmark while a
build is running.

Baselines are per machine **and per compiler**: `scripts\perf.ps1 -Compiler clang` builds
with clang-cl and compares against `<computername>-clang.json`. Comparing the two baselines
against each other is the cheapest way to see which compiler the hot paths prefer.

The benchmarks in `tasfw-perf/src/bench_script.cpp` run on `FakeResource`, an in-memory
resource whose frame advance is a few nanoseconds, so they isolate what the framework adds
per operation. Everything else there is a direct measurement of the named component.

## First measurements (Tier A, 2026-09-07)

32-thread desktop at 3.0 GHz. MSVC 19.44 with `/arch:AVX2 /fp:precise /GL`; clang-cl 19.1 with
`-march=native -ffp-contract=off -flto=thin`. Fastest of nine pinned repetitions. Rounded.
These are the numbers to beat, and the ones that make "zero-cost" concrete.

| Operation | MSVC | clang-cl |
|---|---|---|
| `AdvanceFrameWrite`, root script, no tracker | 216 ns | 297 ns |
| `AdvanceFrameRead` (includes uncached input lookup) | 296 ns | 382 ns |
| `AdvanceFrameWrite` + `Save` | 0.94 us | 1.32 us |
| Write one frame, `Load` back to the previous save | 270 ns | 285 ns |
| `ExecuteAdhoc` with an empty lambda | 619 ns | 433 ns |
| `ExecuteAdhoc` writing one frame, then revert | 843 ns | 846 ns |
| `Execute<EmptyScript>` | 2.29 us | 2.11 us |
| `Execute<OneFrameScript>` | 2.69 us | 2.73 us |
| `AdvanceFrameWrite` with a trivial state tracker | 2.83 us | 3.08 us |
| `AdvanceFrameWrite` with a recursive state tracker | 3.08 us | 3.39 us |
| `GetInputs`, uncached, hierarchy depth 1 / 4 / 16 | 139 / 202 / 482 ns | 226 / 289 / 545 ns |
| `LongLoad` to root save and back, depth 1 / 4 / 16 | 0.36 / 0.88 / 7.5 us | 0.35 / 0.83 / 6.6 us |
| `SlotManager` create + erase at 100 / 10,000 live slots | 196 / 375 ns | 258 / 607 ns |
| `SlotManager` load at 1,000 / 10,000 live slots | 208 / 337 ns | 146 / 246 ns |
| `Scattershot::GetHash` on a 16-byte bin | 21 ns | 13 ns |
| `UpsertBlock` novel / redundant / improved | 104 / 67 / 95 ns | 100 / 64 / 93 ns |
| `Inputs::GetClosestInputByYawHau` (full magnitude) | 58 ns | 57 ns |
| `M64::save` / `M64::load`, 10,000 frames | 2.1 / 1.1 ms | 1.4 / 1.3 ms |

### First Tier B numbers (from `dllcheck`, 2026-09-07)

`dllcheck.exe res\sm64_jp_0.dll res\comissonPyra2-Fanart_x-Z.m64 3330 [--lightweight]`,
MSVC build, single thread, idle machine:

| Primitive | Cost |
|---|---|
| Frame advance (`sm64_update` plus `SetInputs`) | 9.5 to 10 us |
| Save, lightweight (1.5 MB) | 285 us |
| Load, lightweight | 42 us |
| Save, full (7.3 MB) | 1.4 ms |
| Load, full | 190 us |

A save costs 5 to 7 times its load in both modes: every `SaveState` allocates and zero-fills
fresh vectors because slots are never reused (ROADMAP 3.9). A lightweight save is worth about
30 frame advances, a lightweight load about 4, which is what the `shouldSave`/`shouldLoad`
cost model is trading against.

What the Tier A and B numbers say together:

- The bare per-frame framework cost (216 ns on MSVC) is about 2% of a 10 us game frame. The
  hierarchy itself is close to zero-cost.
- A **state tracker costs about 2.6 us per frame**, which is a quarter of a game frame, on
  every frame of every thread. Every tracked frame instantiates a script, runs all three
  lifecycle phases inside ad-hoc sandboxes and reverts. That is the first target for
  ROADMAP 3.7.
- **Instantiating a child script costs about 2.2 us** even when it does nothing. Scripts
  that are run per frame (the downhill angle probes) pay this every time.
- `LongLoad` at depth 16 is 20x depth 1; the ancestor walk is linear and not free.
- Slot bookkeeping grows with live slots (three `std::map`s per slot).
- **The two compilers disagree by up to 60% on individual paths, in both directions**, with
  the same MSVC STL headers underneath. MSVC is ahead on the map-heavy slot and per-frame
  bookkeeping; clang is ahead on hashing, m64 writing, and ad-hoc sandbox setup. Any
  "optimization" measured on one compiler alone is suspect.

## Change log (measured)

Every hot-path change records its delta table here, newest first.

### 2026-09-07: per-level bookkeeping as a `LevelStack` (ROADMAP 3.7)

`Script` kept six `std::unordered_map<int64_t, ...>` keyed by ad-hoc level, hashed on every
access and allocated on every level push. Levels are a stack, so they are now a
`LevelStack<T>` (`tasfw/LevelStack.hpp`): level 0 inline, higher levels heap-allocated once
and reused after pop. Tier A, MSVC, fastest of nine:

| Benchmark | before | after | delta |
|---|---|---|---|
| `ExecuteAdhoc`, empty lambda | 619 ns | 271 ns | -56% |
| `ExecuteAdhoc`, one frame + revert | 843 ns | 575 ns | -32% |
| `Execute<EmptyScript>` | 2.29 us | 1.56 us | -32% |
| `Execute<OneFrameScript>` | 2.69 us | 2.05 us | -24% |
| Frame advance with a trivial state tracker | 2.83 us | 2.22 us | -22% |
| `AdvanceFrameWrite` / `AdvanceFrameRead` | 216 / 297 ns | 192 / 265 ns | -11% |
| Write one frame + `Load` back | 270 ns | 201 ns | -25% |
| `LongLoad` to root and back, depth 16 | 7.53 us | 4.06 us | -46% |
| `GetInputs` uncached, depth 16 | 482 ns | 328 ns | -32% |

15 improvements, 0 regressions; all 536 test assertions unchanged. What remains in a
tracked frame (about 2.2 us) is constructing the tracker script object (six `std::map`s,
each of which allocates a head node on MSVC's STL), its three lifecycle sandboxes, and
copying its `CustomStatus` (which for the real trackers holds `std::vector`s). Those are the
next targets.

### 2026-09-07: cached symbol pointers in `LibSm64` (ROADMAP 3.7)

`Script::SetInputs` resolved `gControllerPads` three times per frame, `advance()` resolved
`sm64_update` and `getCurrentFrame()` resolved `gGlobalTimer` on every call, all through
`GetProcAddress`. `Resource` gained `setInputs()`; `LibSm64` caches the three pointers at
construction. Measured `GetProcAddress` cost on this DLL is 62 ns (`dllcheck` prints it), so
the frame-advance change is within noise (9.9 vs 9.7 us); hygiene, not a headline. Scripts
that call `addr()` per frame pay that 62 ns per call.
