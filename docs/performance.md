# Performance

Performance is a first-class requirement, equal to correctness. The framework exists to run
millions of game frames per search, and it is written in C++ for that reason. A change that
is cleaner but slower is a regression. It is rejected unless the slowdown is measured,
explained, and explicitly accepted in the PR.

This document is the specification for how performance is measured and gated, and the
record of what the suite (`tasfw-perf`, `scripts/perf.ps1`, ROADMAP 1.3) measures.

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
- ~~`Script::GetTrackedState` performs a `dynamic_cast` on the root script per call.~~ Fixed:
  a per-type tag compare (ROADMAP 3.7). Tracking still goes through virtual hooks on
  `_rootScript` on every frame advance.
- `Resource::save` / `load` / `advance` / `setInputs` are virtual and called per frame.
- Block segments are `std::shared_ptr<Segment>` chains, touched on every decode.
- Tracker scripts are constructed per tracked frame: three lifecycle sandboxes and a
  `CustomStatus` move (with `std::vector`s in the real trackers). The per-level containers
  are now created on first use (ROADMAP 3.7), but Tier C still counts 29 heap allocations
  per frame in the `StateTracker_BitfsDr` sweep and about 9 per frame advanced in the
  nested-script pyramid oscillation (2026-09-08), which is what ROADMAP 3.7's remainder and
  3.8 are for.

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

Gate: time within 10% of baseline; heap allocations per iteration within 0.1 of baseline
(counted on every benchmark; see "Running the suite").

### Tier B: resource benchmarks (DLL required)

Implemented in `tasfw-perf/src/bench_libsm64.cpp` as three families that `perf.ps1` runs
in their own processes (a DLL path loads once per process): `^BM_LibSm64Full` and
`^BM_LibSm64Light`, one per save mode, and `^BM_LibSm64Scaling`. They run when
`TASFW_LIBSM64`/`TASFW_M64` name a DLL and a movie (`perf.ps1` finds them in `res\` like
`test.ps1` does) and are skipped otherwise, so the suite still runs in CI. The save-mode
families play the movie to `TASFW_FRAME` (default 3330) once, then measure:

- `FrameAdvance`: one game frame with neutral inputs (fixed 3,000 iterations, then the
  anchor frame is reloaded).
- `SaveErase`: save into a recycled slot and release it, the steady-state cost of `Save()`.
- `SaveFresh`: save into fresh storage, what a save costs before anything was released
  (fixed few iterations; each keeps its buffers until the end).
- `Load`: load the anchor slot.
- `ResidentPerSlot/N`: N fresh saves with the process working set sampled before and
  after (100 slots, and 1,000 in lightweight mode; 1,000 full saves would pass 4 GB and
  skip). `stateBytes` is one state as the resource accounts it, exact and gated:
  1,500,000 bytes lightweight, 7,279,456 full on the pinned DLL. `rssPerSlot` is what the
  process grew by per slot, slot-map nodes included: within 0.2% of `stateBytes` at both
  counts, so a live slot costs its state and nothing else.

`^BM_LibSm64Scaling` runs `FrameAdvance` and `SaveErase` on 1, 2, 4, 8 and 16 threads,
each thread on its own DLL copy with lightweight saves, as the search runs (thread i loads
the copy whose trailing index is i + 1, `res\sm64_jp_1.dll` onward; the family skips
without those copies). `perf.ps1` does not pin this family to one CPU. Google Benchmark
reports these rows per thread, so the aggregate rate is n times the row's;
`perf_compare.py` computes efficiency, the per-thread rate at n threads over the rate at
one thread, from each run's own rows. First numbers (2026-09-08, 16 cores, 32 logical
CPUs; MSVC and clang-cl within 3 points): frame advance 100 / 99 / 96 / 80% at 2 / 4 / 8 /
16 threads, lightweight save and erase 98 / 99 / 93 / 65%. Frames scale until the 16
threads start sharing physical cores; the 1.5 MB save is bandwidth-bound and drops sooner.

Gate: counts exact (`stateBytes` included); times within 10%; efficiency at 2, 4 and 8
threads must not fall below the baseline by more than 5 points (`--efficiency-tolerance`).
The 16-thread rows are reported, not gated: they share physical cores with each other and
with whatever else runs, and moved 2 to 3 points between runs of the same binary.

### Tier C: framework benchmarks (DLL required, deterministic)

Implemented in `tasfw-perf/src/bench_framework.cpp` as the `^BM_Framework` family, gated on
the same environment as Tier B and run in its own process. One resource is played to
`TASFW_FRAME` by hand, that state becomes the resource's start save, and every iteration
starts by reloading it. The resource's cost model is off (`useCostModel = false`), so no
automatic savestate is ever created and every count is an exact function of the code.
Fixed workloads on the source movie:

- `PyramidOscillation`: `BitFsPyramidOscillation` after the pyramid-osc-approach preamble
  (one stick input, wait until idle), 2 iterations.
- `DownhillAngle_PyramidUpdate`: 1,000 calls of
  `BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle` on a `PyramidUpdate` imported from
  the game, exactly as `BitFsPyramidOscillation::execution` does, 3 iterations.
- `TrackerSweep`: `StateTracker_BitfsDr` (the committed dr-oscillations parameters) over 500
  consecutive `AdvanceFrameRead`s, 3 iterations.

Metrics per row: wall time (fastest of nine, as in Tier A); `allocs`; the counters
`frameAdvances`, `saves`, `loads` (per iteration, exact); **replay ratio** = frames advanced
per frame of output diff (per frame swept for the tracker); **overhead %** = 1 minus
(advance + save + load time) / wall, all in rdtsc cycles.

Gate: counts exactly equal to baseline (an increase fails; a decrease is printed for review
and re-baselined); wall within 10%; allocations within 0.1 per iteration; overhead % no more
than 2 points above baseline.

### Tier D: scattershot end to end (DLL required)

Run by `scripts/perf.ps1` through `bitfs-turn` on two configs under `perf/`, each one
`tilt-target` stage from frame 3330 with 100 pellets per shot (output under `perf/results/`):

- `tierd-deterministic.json`: `Deterministic = true`, seed 3, 600 shots, 8 threads, cost
  model off. The row `TierD_Deterministic` carries exact counts summed over threads:
  `shots`, `scripts`, `blocks`, `solutions`, `validationFailures` (must stay 0, ROADMAP 4.5),
  `frameAdvances`, `saves`, `loads`; wall within 10%.
- `tierd-throughput.json`: `Deterministic = false`, 1,200 shots, 16 threads, cost model on.
  The row `TierD_Throughput` carries `shotsPerSecond`, `scriptsPerSecond`,
  `frameAdvancesPerSecond`, `peakResidentMB` (reported) and `validationFailures` (exact, 0);
  wall within 10%.

The deterministic run has the cost model off because automatic savestates depend on measured
timings: with it on the search outcome is still identical (ROADMAP 4.5), but `frameAdvances`
and `saves` are not. Both runs are skipped with `-Filter`, with `-NoTierD`, or when
`res\sm64_jp_0.dll` .. `sm64_jp_15.dll` are missing; together they take about five minutes.
Neither uses the full pipeline in `config.json`.

When a Tier D wall row trips the gate while every exact count is identical, do not conclude
from one run or one A/B: build the pre-change tree (`git stash`), and run pre, post, pre,
post on the deterministic workload back to back. On 2026-09-08 this machine drifted from
140 s to 160 s on that workload within one afternoon with no VM and nothing else running,
and a single A/B in the middle pointed at a change that four interleaved runs then cleared
(change log). Do not measure with Docker Desktop's VM up either; it alone adds about 11%.

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

Tiers A, B and C are implemented in `tasfw-perf/` (Google Benchmark, fetched by CMake); Tier D
runs `bitfs-turn` on the configs under `perf/`. One command runs everything the machine can
run: Tier A always, B and C when the DLL and movie are found, D when the DLL copies are.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1                 # build Release, run, compare
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SaveBaseline   # store this run as the baseline
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -Filter Script -NoBuild   # one family, no Tier D
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -NoTierD        # skip the five-minute Tier D
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -TierDOnly -NoBuild   # only Tier D (other rows read MISSING)
```

Results go to `perf\results\<timestamp>-<sha>.json` (gitignored). The baseline for a machine
is `perf\baselines\<computername>.json` (committed). `scripts\perf_compare.py` prints the delta
table and exits non-zero on any regression over the threshold (default 10%). Paste that table
into the PR.

Every benchmark also reports `allocs`, heap allocations per iteration. `tasfw-perf` replaces
the global `operator new` (`tasfw-perf/src/alloc_counter.cpp`) and each benchmark reads the
count around its timed loop. The count is deterministic, so the compare gates it separately
from time: an increase of more than 0.1 allocations per iteration (`--alloc-tolerance`; the
slack only absorbs one-time set-up amortised over the fixed iteration counts) fails the run
even when the clock does not notice. The counter is a relaxed load and store, not
`fetch_add`: a locked read-modify-write right after every `malloc` cost 10 to 20 ns per
allocation and read as a 75% regression on the allocation-heavy benchmarks.

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
- Tight loops are sensitive to code layout. Adding code elsewhere in the binary has moved
  clang-cl's uncached `GetInputs` rows by about 15% in both directions, stably across
  processes, while the same rows on MSVC and every benchmark that touched the changed code
  stayed put. A shift on one compiler only, on a benchmark whose code did not change, is
  layout: confirm with a re-run, say so in the change log, and re-save the baseline.
- The perf binary's own code moves its tight loops, and by more than 15%. Adding the Tier B
  scaling and memory benchmarks moved `Resource_SaveLoadState` from 149 to 340 ns on MSVC
  and `Scattershot_UpsertBlock_Improve` from 91 to 128 ns on clang-cl with the framework
  unchanged; rebuilt without the new code, both read their baselines. To attribute a large
  shift on a row whose code did not change, rebuild the perf binary without the addition
  and measure the row; then re-save with the numbers in the change log.
- The lightweight `SaveErase` and `Load` rows (a 1.5 MB copy that fits the 2 MB L2) have two
  states, about 37 and about 42 us, on both compilers, hours apart, on unchanged code, while
  the full-save rows and the frame advance stay put. A run in the other state on unchanged
  code is re-saved with a note in the change log, not investigated again.

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

A save cost 5 to 7 times its load in both modes at that point: every `SaveState` allocated and
zero-filled fresh vectors because slots were never reused. That is fixed (ROADMAP 3.9; the
change log below has the after numbers): a save now costs about what a load does, so a
lightweight save is worth about 5 frame advances and a lightweight load about 5, which is what
the `shouldSave`/`shouldLoad` cost model is trading against.

What the Tier A and B numbers say together:

- The bare per-frame framework cost (216 ns on MSVC then, 170 ns now) is about 2% of a 10 us
  game frame. The hierarchy itself is close to zero-cost.
- A **state tracker cost about 2.6 us per frame** at that point, a quarter of a game frame,
  on every frame of every thread: every tracked frame instantiates a script, runs all three
  lifecycle phases inside ad-hoc sandboxes and reverts. The 3.7 work in the change log
  brought it to about 0.8 us.
- **Instantiating a child script cost about 2.2 us** even when it did nothing (0.7 us since
  3.7). Scripts that are run per frame (the downhill angle probes) pay this every time.
- `LongLoad` at depth 16 is 20x depth 1; the ancestor walk is linear and not free.
- Slot bookkeeping grows with live slots (three `std::map`s per slot).
- **The two compilers disagree by up to 60% on individual paths, in both directions**, with
  the same MSVC STL headers underneath. MSVC is ahead on the map-heavy slot and per-frame
  bookkeeping; clang is ahead on hashing, m64 writing, and ad-hoc sandbox setup. Any
  "optimization" measured on one compiler alone is suspect.

## Change log (measured)

Every hot-path change records its delta table here, newest first.

### 2026-09-08: renamed-symbol fallback in `LibSm64::addr`; the Linux `.so` measured (ROADMAP 2.1, 3.4)

`LibSm64::addr` now resolves a symbol with the new non-throwing `SharedLib::tryGet` and,
only when that fails, looks the name up in `LibSm64SymbolAliases` for the decomp's renamed
spelling (docs/libsm64.md, "Renamed symbols"); the lightweight slice-coverage checks in
`layoutCheckReport` are skipped where lightweight saves do not exist (Linux). On the pinned
DLL every symbol the search asks for resolves on the first lookup, so the per-call work is
the same single `GetProcAddress` as before plus a null test; nothing per frame changed.
MSVC Release against the committed baselines, Docker Desktop's VM stopped:

| Tier | Result |
|---|---|
| A and B (time rows) | 0 regressions over 10%; 3 rows faster (`Script_GetInputs_Uncached_Depth/1` -10.9%, `/4` -12.2%, `Resource_SaveLoadState` -56.3%, the layout-sensitive rows noise control already lists). Everything else within +1% to +6%, the DLL's own `FrameAdvance` rows included (13.7 -> 14.3 us), so the day's drift, not the change. |
| allocations, exact counts, scaling efficiency | 0 / 0 / 0 regressions |
| C (oscillation, downhill, sweep) | +2.2%, -7.2%, -1.0% |
| D exact counts | identical: 55 solutions, 109,958 blocks, 520,052 scripts, 0 validation failures |
| D wall | see below |

The Tier D wall rows tripped the gate and took an hour to run down. The suite read
155.5 s / 85.7 s (deterministic / throughput; +11% / +14%), two `-TierDOnly` reruns 153.0 /
86.2 and 154.0 / 86.9, and a stash A/B of the pre-change tree in between read 141.9 / 71.1,
which looked like a real regression with identical counts. Bisecting said otherwise: with
only the `addr` body reverted the run read 155.1 / 84.2, and with `LibSm64.hpp`/`.cpp` at
HEAD and only the unused `tryGet` added it read 165.8 / 90.5, a binary that cannot be
slower for any reason. Four interleaved runs of the deterministic workload then read
pre 159.0, post 162.6, pre 160.7, post 158.1 s: pre and post agree within 0.3% and the
machine, with no VM and no other process above 1% CPU, had simply drifted from 140 s to
160 s on the same workload over the afternoon (commit charge was 33.6 of 36.3 GB). The
change costs nothing measurable. Two things learned for the procedure: a single A/B on a
drifting machine can point the wrong way, so a Tier D wall regression with identical counts
needs interleaved pre/post runs before it counts (Tier D section); and a suite run right
after long builds and container work is a poor sample.

Linux, first numbers (`dllcheck` on bitfs-sbb's JP `.so` at frame 3330, Ubuntu 26.04
container on this machine, so not comparable with the Windows rows): frame advance 21.0 us
(GCC 15) / 22.4 us (Clang 21), dirty-page save 44.9 / 46.4 us, load 44.1 / 41.6 us, `dlsym`
33 / 37 ns. Windows `dllcheck` on bitfs-sbb's 2026 JP DLL: 8.5 us per frame, 41.0 us
lightweight save, 41.8 us load, 583 ns `GetProcAddress`, all layout checks passing.

### 2026-09-08: warning levels raised on every compiler (ROADMAP 1.7)

About 300 edits across the tree so that MSVC at `/W3`, clang-cl at `/W4` and GCC and Clang
at `-Wall -Wextra` build clean with warnings as errors: explicit casts of conversions that
were already happening (`float(a * b - c * d)` keeps the integer arithmetic and the
result), deleted dead locals and fields (several were `resource->addr()` lookups per call,
so a little less work), unnamed unused parameters, `int64_t` for the tracked-state hooks'
ad-hoc levels and `BitFsPyramidOscillation_Iteration`'s frames, a virtual destructor on
`Resource`, `snprintf` for `sprintf`. Exact counts identical everywhere: the deterministic
Tier D run gives 55 solutions, 109,958 blocks, 520,052 scripts and 0 validation failures on
both compilers, and the drift test still matches the DLL bit for bit.

Against the committed baselines (Tier A and B fastest of nine; Tier C exact; Tier D on the
`perf/tierd-*.json` workloads):

| Compiler | Tier A/B time rows over 10% | Allocation / count / efficiency regressions | Tier C (oscillation, downhill, sweep) | Tier D deterministic / throughput |
|---|---|---|---|---|
| MSVC | 0 (`M64_Load_10k` -11%, its layout flip back to 1.1 ms; re-saved) | 0 / 0 / 0 | +5.4%, -1.3%, +3.6% | 139.6 -> 140.8 s, 74.9 -> 73.3 s |
| clang-cl | 3, `Script_GetInputs_Uncached_Depth/1`, `/4`, `/16` (+17%, +12%, +14%) | 0 / 0 / 0 | +4.2%, +4.3%, +2.5% | 145.0 -> 145.9 s, 73.2 -> 75.5 s |

Two things were run down. The first full MSVC run read the deterministic Tier D at 156 s
(+11.7%); Docker Desktop's VM, started for the Linux checks, was running at the time, and
with it stopped the run reads 140.8 s. Do not measure with the VM up. Second, an A/B against
the pre-change tree (stash, rebuild the perf binary, measure, restore) on the rows that
moved: the MSVC oscillation row reads 772.8 ms before and 777.4 ms after (+0.6%; the rest of
its +5% against the baseline is the day's drift, present before the change), and clang-cl's
uncached `GetInputs` rows read 178 / 212 / 304 ns before and 201 / 226 / 334 ns after. That
path changed only in that `GetAdhocLevel` returns `int64_t` (one sign extension fewer) and
`Script::_initialFrame` widened into what was padding, and on MSVC the same rows moved the
other way (-10%); it is the code-layout sensitivity of these rows that noise control
already describes, so they were re-saved. The search itself did not move on either compiler.

### 2026-09-08: Tier B thread scaling and memory per slot (ROADMAP 1.3); json 3.12

No framework code changed. The new Tier B rows on both compilers (per-thread times;
efficiency relative to one thread, from each run's own rows):

| Row | 1 thread | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| `Scaling_FrameAdvance`, MSVC | 14.2 us | 99.7% | 98.6% | 96.1% | 80.3% |
| `Scaling_FrameAdvance`, clang-cl | 14.4 us | 99.9% | 99.9% | 98.6% | 82.8% |
| `Scaling_SaveErase` (lightweight), MSVC | 40.4 us | 97.9% | 98.6% | 92.9% | 65.0% |
| `Scaling_SaveErase` (lightweight), clang-cl | 40.8 us | 102% | 102% | 94.5% | 66.7% |

Efficiency moved up to 4 points between the family run and the full run that followed it
on the same binary (8-thread saves 92.9 to 89.0 on MSVC), which is why the gate is 5 points
and stops at 8 threads.

| Row | Time | `stateBytes` | `rssPerSlot` |
|---|---|---|---|
| `Full_ResidentPerSlot/100` | 121 ms | 7,279,456 | 7,282,688 |
| `Light_ResidentPerSlot/100` | 27.3 ms | 1,500,000 | 1,498,317 |
| `Light_ResidentPerSlot/1000` | 279 ms | 1,500,000 | 1,502,552 |

The full runs against the committed baselines: Tier C within 5% and Tier D within 2% on
both compilers with identical counts (55 solutions, 109,958 blocks, 520,052 scripts, 0
validation failures); no allocation, count or efficiency regression. Six Tier A and B time
rows were over the gate on unchanged code and were re-saved, each after a re-run and, where
the number was large, a perf binary built without the new benchmark code to attribute it:

| Row | Baseline | Now | Cause |
|---|---|---|---|
| `LibSm64Light_SaveErase`, `_Load` (both compilers) | 37 us | 41 to 42 us | the 1.5 MB copy's second state, every run today but one (noise control) |
| `Resource_SaveLoadState`, MSVC | 149 ns | 340 ns | the perf binary's own layout: 150 ns when built without the new benchmarks |
| `Scattershot_UpsertBlock_Improve`, clang-cl | 91.5 ns | 128 ns | same: 93.7 ns without them |
| `Script_GetInputs_Uncached_Depth/1`, `/4`, MSVC | 125 / 158 ns | 140 / 176 ns | layout from earlier today (138 / 172 ns without the new benchmarks too); the rows noise control already names |
| `SlotManager_LoadSlot/100`, clang-cl | 110 ns | 89 ns | layout the other way (90.5 ns without them) |

`M64_Load_10k` on MSVC, re-saved at 1.2 ms earlier in the day, read 1.1 ms again (-5.7%):
the same layout sensitivity, inside the gate this time.

### 2026-09-08: `tasfw-scripts-scattershot-bitfs-dr` gets the shared optimization flags (ROADMAP 1.5)

The library holding `Scattershot_BitfsDr.cpp` and `StateTracker_BitfsDr.cpp` was the one
first-party target never passed to `add_optimization_flags`, so it built without LTO (and,
on GCC, without the `-Wno-missing-requires` every sibling has, which is how the omission
surfaced: the only warnings left in the GCC job). No source under measurement changed; the
same change fixed the last MSVC warnings (an `int` literal for a float in
`StateTracker_BitfsDr.cpp`) and moved the test helpers' `getenv` into `tasfw-testing`.

Against the committed baselines (Tier A and B fastest of nine, Tier C exact counts, Tier D
on the two `perf/tierd-*.json` workloads):

| Compiler | Time rows over 10% | Allocation / count regressions | Tier C (oscillation, downhill, tracker sweep) | Tier D |
|---|---|---|---|---|
| MSVC | 3 on the first run, see below | 0 / 0 | +2.4%, -0.6%, +1.0% | deterministic 139.6 -> 140.7 s, 141.0 s on re-run, identical counts (55 solutions, 109,958 blocks, 520,052 scripts, 0 validation failures); throughput 74.9 -> 77.3 s, 73.5 s on re-run |
| clang-cl | 1 (`LibSm64Light_SaveErase` +13%) | 0 / 0 | +0.7%, -1.1%, -0.5% | deterministic 145.0 -> 145.9 s, identical counts; throughput 73.2 -> 70.7 s |

The MSVC rows over the gate: `Resource_SaveLoadState` read 217.9 ns, its known bimodal
value, and 148.4 ns on a re-run of its family; `LibSm64Light_SaveErase` +15.6%, then +0.3%
on re-run; both are the placement-sensitive rows described under noise control.
`M64_Load_10k` stayed at 1.2 ms against 1.1 ms across three runs (+13.0%, +11.7%, +13.8%)
and returned to 1.1 ms (+2.1%) when the `add_optimization_flags` line alone was removed and
the perf binary relinked; clang-cl's row moved +3.6%. m64 loading calls nothing in that
library, so this is link-time code layout on MSVC, and the `BM_M64` family rows in
`perf/baselines/tyler-desktop.json` were re-saved from the run with the change; every other
family keeps its baseline rows.

### 2026-09-08: Tier C and Tier D land (ROADMAP 1.3); first numbers

No code under measurement changed; these are the first baselines for the new rows, on the
pinned DLL and movie at frame 3330, MSVC and clang-cl agreeing on every count.

Tier C (`^BM_Framework`, cost model off, per iteration):

| Workload | Wall | Frame advances | Saves | Loads | Allocs | Overhead |
|---|---|---|---|---|---|---|
| `PyramidOscillation` (quadrant 4, does not assert from here; 20 output frames) | 750 ms | 42,923 | 0 | 1,389 | 404.6 k | 5% |
| `DownhillAngle_PyramidUpdate` (1,000 calls) | 3.0 ms MSVC, 2.4 ms clang-cl | 1,000 | 0 | 1,000 | 56.0 k | n/a |
| `TrackerSweep` (500 frames) | 7.7 ms | 500 | 0 | 1 | 14.5 k | 14.5% |

Read the allocation column: about 9 heap allocations per frame advanced in the nested-script
oscillation, 56 per downhill-angle call (the `PyramidUpdateMem` import), 29 per tracked
frame. That is the remainder of ROADMAP 3.7 and the input to 3.8.

Tier D (`bitfs-turn`, `tilt-target` from frame 3330, 100 pellets per shot):

| Run | Wall | Counts |
|---|---|---|
| Deterministic, 600 shots, 8 threads, cost model off | 140 s | 520,052 scripts, 109,958 blocks, 55 solutions, 18,014,927 frame advances, 608 saves, 1,038,084 loads, 0 validation failures; 121 MB peak resident |
| Throughput, 1,200 shots, 16 threads, cost model on | 74 s | 16.1 shots/s, 13.7 k scripts/s, 475 k frame advances/s; 686 MB peak resident |

An 800-shot deterministic run gave the same counts on MSVC and clang-cl to the last frame
advance (24,229,711), which is the reproducibility ROADMAP 4.5 restored.

The gates were checked by mutation before the baselines were committed: one extra
save/advance/load per `LoadBase` call fails Tier C on counts (`frameAdvances 42923 -> 44331`
in the oscillation, `500 -> 501` in the sweep) as well as time and allocations; a 3x
`GetHash` loop fails Tier A at +189% and +273%. One row of the first MSVC baseline run,
`Resource_SaveLoadState`, read 217 ns against 155 ns before on unchanged code and 149 ns on
a re-run of its family: the per-process bimodality described under noise control. The
family's rows in the baseline were taken from the re-run.

### 2026-09-08: `Revert` drops a reverted child's desynced saves (ROADMAP 4.5)

Correctness fix on a per-script path. `Revert` used to move every save of a reverted child
into the parent's bank when none of them was synced; it now moves only saves at or before the
child's first written frame (in practice none, so the bank is simply dropped and its slots
recycled). Less work, fewer live slots, and the parent's bank no longer accumulates a save
per reverted pellet.

Tier A and Tier B, fastest of nine, against the committed baselines:

| Compiler | Rows | Regressions > 10% | Allocation regressions | Notes |
|---|---|---|---|---|
| MSVC | 52 | 0 | 0 | every row within -9.5% .. +3.3% |
| clang-cl | 52 | 0 | 0 | `GetInputs_Uncached_Depth/1` -11% and `LibSm64Light_SaveErase` -13%, the two rows already known to flip with code layout and process placement |

The end-to-end workload (400-shot deterministic `tilt-target` at frame 3330, seed 3, 100
pellets per shot; old `Revert` -> new, back-to-back on the same machine, counts summed over
threads as `bitfs-turn` prints them):

| Run | Validation failures | Wall | Frame advances | Saves | Loads |
|---|---|---|---|---|---|
| 4 threads, lightweight | 10 -> 0 | 147 -> 157 s | 10.88 M -> 11.97 M (+10%) | 26.3 k -> 28.9 k (+10%) | 676 k -> 710 k (+5%) |
| 4 threads, full saves | 0 -> 0 | 214 -> 215 s | 11.70 M -> 12.04 M (+3%) | 2.7 k -> 2.9 k (+9%) | 716 k -> 710 k (-1%) |
| 1 thread, lightweight | 5 to 8 -> 0 | 283 to 291 s -> 329 s | (binary without counters) | | |

Read the counts, not the wall column: the same binary ran the full-save workload in 249 s
and 215 s an hour apart, so wall time swings by up to 15% here. The extra frame advances are
replays that used to stop at a save made with reverted inputs, and the old runs also skipped
the pellets of every failed shot (10 of 400 in the lightweight run). Saves rise because a
pellet's automatic saves are now dropped with the pellet and the next one earns its own.

What the fix buys besides correctness: the lightweight and full-save runs now perform the
identical search (26 solutions and 709,843 loads in both), so the outcome of a deterministic
run no longer depends on which savestates the cost model happened to create. Before, the two
modes found 27 and 38 solutions from the same seed.

### 2026-09-08: recycled savestate buffers (ROADMAP 3.9) and the first Tier B benchmarks

`SlotManager` used to construct a fresh `TState` for every save (`save()` then grew empty
vectors, zero-filling them) and destroy it on erase or eviction. Erased and evicted states
now go to a bounded pool (32 states, pooled memory counted against the slot budget) and the
next `CreateSlot` reuses one, so the save is a single copy into already-sized buffers.

`dllcheck`, MSVC, single thread (before -> after):

| Primitive | Save | Load |
|---|---|---|
| Full (7.3 MB) | 1561 -> 191 us | 197 -> 222 us |
| Lightweight (1.5 MB) | 285 -> 50 us | 42 -> 53 us |

The Tier B family that now gates this (`bench_libsm64.cpp`, fastest of nine, MSVC / clang-cl):

| Benchmark | MSVC | clang-cl | allocs/iter |
|---|---|---|---|
| `LibSm64Full_SaveErase` (recycled) | 189 us | 192 us | 3 |
| `LibSm64Full_SaveFresh` | 1.26 ms | 1.26 ms | 5.25 |
| `LibSm64Full_Load` | 189 us | 189 us | 1 |
| `LibSm64Light_SaveErase` (recycled) | 42 us | 44 us | 3 |
| `LibSm64Light_SaveFresh` | 278 us | 284 us | 5.07 |
| `LibSm64Light_Load` | 44 us | 41 us | 1 |
| `LibSm64*_FrameAdvance` (neutral inputs at frame 3330) | 14.3 us | 14.2 us | 0 |

A recycled save now costs the same as a load in both modes (the 3.9 bar was 2x). The three
allocations per recycled save are the slot's map node and the two access-order map nodes;
the fresh save adds the two buffers (and their growth). Tier A on MSVC: 0 regressions, 10
improvements, among them `SlotManager` create + erase -22 to -56% and create at the cap -15
to -26% (the fake state is 256 bytes, so that is the map churn the pool removes, not the
copy). clang-cl: the same `SlotManager` gains, and four rows the pool does not touch read +12 to
+19% against the previous baseline. Re-run: `LoadSlot` at 100 slots flips between 93 and
104 ns from process to process (the bimodality described under noise control), while
`GetInputs` uncached at depths 1, 4 and 16 is stable at 200 / 226 / 333 ns, against
169 / 202 / 296 in the previous baseline and 204 / 246 / 406 before the 3.7 work. That
loop calls nothing the change touched and MSVC's rows did not move, so this is the
code-layout sensitivity noted under noise control; the baseline was re-saved with the new
values.

### 2026-09-07: allocation-free ad-hoc levels, cheaper child scripts and trackers (ROADMAP 3.7)

Measured first with the new allocation counter: a frame advance with a trivial tracker cost
64 heap allocations on MSVC, an empty child script 53, an empty `ExecuteAdhoc` 9. MSVC's
`std::map` allocates a head node in its constructor, a `Script` holds six such containers
per ad-hoc level, and every pop replaced a level by assigning a fresh `T()`. Changes:
`LevelStack` constructs every level (including 0) on first use and resets popped levels in
place (`clear()`, `BaseScriptStatus::Reset()`); `ExecuteAdhocBase` and `Initialize` no
longer pre-create containers; `Revert`/`ApplyChildDiff` take the child's save bank as a
pointer that is null when the child never saved; tracked-state entries are created on first
insert and looked up with `find()`; the `dynamic_cast` in `GetTrackedState` became a type-tag
compare; statuses are moved out of finished scripts and `GetTrackedState` returns a
reference. While at it, a `SlotHandle` move that copied the slot id (so every save a child
handed to its parent on `Modify` was erased by the child's bank and later replayed) was
fixed and pinned by a test, and `LevelStack::operator[]` was split so MSVC keeps inlining it
(docs/compilers.md).

Before and after are both measured with the counting binary, fastest of nine:

| Benchmark | allocs/iter | MSVC | clang-cl |
|---|---|---|---|
| Frame advance with a trivial tracker | 64 → 14 | 2.18 → 0.83 us (-62%) | 2.41 → 0.96 us (-60%) |
| Frame advance with a recursive tracker | 65 → 19 | 2.36 → 1.08 us (-54%) | 2.72 → 1.26 us (-54%) |
| `Execute<EmptyScript>` | 53 → 11 | 1.48 → 0.67 us (-55%) | 2.37 → 0.43 us (-82%) |
| `Execute` / `Modify<OneFrameScript>` | 62 → 31 | 1.97 / 2.06 → 1.13 / 1.17 us (-43%) | 2.05 / 2.15 → 1.02 / 1.14 us (-50% / -47%) |
| `ExecuteAdhoc`, one frame + revert | 15 → 5 | 571 → 296 ns (-48%) | 566 → 271 ns (-52%) |
| `ModifyAdhoc`, one frame | 15 → 5 | 608 → 328 ns (-46%) | 657 → 358 ns (-46%) |
| `ExecuteAdhoc`, empty lambda | 9 → 2 | 262 → 283 ns (+8%) | 248 → 81 ns (-68%) |
| `AdvanceFrameWrite` / `AdvanceFrameRead` | 2 / 1 | 179 / 259 → 170 / 229 ns | 252 / 354 → 224 / 366 ns |
| Write one frame + `Load` back | 3 | 202 → 176 ns (-13%) | 215 → 171 ns (-21%) |
| `LongLoad` to root and back, depth 1 / 16 | 1 | 243 / 4130 → 209 / 3880 ns | 252 / 4390 → 194 / 2630 ns |
| `GetInputs` uncached, depth 1 / 16 | 1 | 126 / 322 → 138 / 315 ns | 204 / 406 → 169 / 296 ns |

Clang-cl: 16 improvements, 0 regressions. MSVC: 10 improvements; the two `GetInputs` depth
1 and 4 rows read +10% against an unusually fast before-run but are 8% and 4% faster than
the committed baseline, and the empty `ExecuteAdhoc` is the one MSVC case that got slower:
its two remaining allocations are MSVC's `std::map` move constructor (it gives the moved-from
map a new head) and the returned status's own diff, which cost more than the seven it no
longer does. All 616 test assertions unchanged on both compilers. Other families: all
within noise, allocation counts identical.

Two effects of the counting binary itself, visible against the previous baselines and now
baked into the re-saved ones: `SlotManager_CreateErase/100` measures 350 ns in the suite and
215 ns standalone (the per-process heap-layout effect described above), and clang-cl's
`Execute<EmptyScript>` rose from 1.5 to 2.4 us before any core change, consistent with
clang eliding new/delete pairs when `operator new` is the library's and no longer being able
to once a replacement is visible under LTO. Both compilers now measure the un-elided cost,
which is the honest one for code that runs on MSVC as well.

What remains in a tracked frame (0.8 us, 14 allocations): the head nodes MSVC allocates for
the containers the tracker actually touches, one `inputsCache` node per frame, the
`unordered_map` entry for the tracker's own tracked states, and the moved status. A flat or
pooled per-level container would remove most of it (ROADMAP 3.7, remaining).

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
