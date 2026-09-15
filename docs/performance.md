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
  `GetProcAddress`.~~ Fixed: `Resource::setInputs()` and cached pointers (performance-changelog.md).
- ~~`Script` bookkeeping was `unordered_map<int64_t, std::map<...>>` per ad-hoc level with
  `operator[]` default-inserts on the hot path.~~ Fixed: `LevelStack` (performance-changelog.md).
- ~~Scripts re-resolve `gMarioState`, `gCamera`, behaviors and the object pool by string at
  the top of every `validation()` / `execution()` (62 ns each on this DLL; 1.5% of the
  throughput Tier D run's CPU on 16 threads, with the loader lock contended).~~ Fixed:
  `LibSm64::addr` resolves a name once and answers from its own table after, 17 ns alone
  and 45 ns at 16 threads against 61 ns and 5.6 us through the loader (the `Addr` rows of
  Tier B; performance-changelog.md 2026-09-14). The scripts still ask per execution, which
  is now what the contract allows (`Resource::addr`).
- ~~`Script::GetTrackedState` performs a `dynamic_cast` on the root script per call.~~ Fixed:
  a per-type tag compare (ROADMAP 3.7). Tracking still goes through virtual hooks on
  `_rootScript` on every frame advance.
- `Resource::save` / `load` / `advance` / `setInputs` are virtual and called per frame.
- ~~On MSVC 19.51 `LevelStack::operator[]` is not inlined into `Script::GetInputsMetadata`
  (a call per container per level, twelve per uncached lookup at depth 1, 22% of that
  loop's samples; 19.44 inlined it), and the walk returns its 40-byte result by copying a
  local it assembled after the parent's answer (34% of its samples on the instruction after
  the copy).~~ Found 2026-09-15 with PMC counters and fixed the same day: `Grow()` is
  `TAS_FW_NOINLINE` (docs/compilers.md), so the accessor inlines everywhere again, and the
  walk writes into the caller's object (ROADMAP 3.19; performance-changelog.md).
- Block segments are `std::shared_ptr<Segment>` chains, touched on every decode.
- Tracker scripts are constructed per tracked frame: three lifecycle sandboxes and a
  `CustomStatus` move. The per-level containers are created on first use and, with
  `M64Diff::frames`, are `FrameMap`s (ROADMAP 3.7, 2026-09-14): a sandbox allocates
  nothing and a tracked frame 3 times instead of 14. The BitFS trackers' status objects
  (`TiltTargetShotMetrics`, `BitfsOscFinalMetrics`, `StateTracker_BitfsDr`) hold their
  per-axis values in `std::array`s since the same day; as `std::vector`s they were
  thirteen allocations per construction and per copy in the tilt-target tracker, 7% of
  the throughput Tier D run (ROADMAP 3.8, performance-changelog.md). What remains per
  tracked frame is the node per tracked state.

## What costs what

Ordered by how much they dominate a typical scattershot run:

1. **Frame advance** (`sm64_update`). Fixed cost per frame, on the order of tens of
   microseconds. Everything else exists to advance fewer frames per useful frame of output.
2. **Savestate save/load.** Depends on the save mode (docs/libsm64.md, "Savestates"): about
   122 pages (488 KB, 7 us) in `dirty` mode during BitFS play, 1.5 MB (41 to 49 us) for the
   `fixed` slices, 7.3 MB (190 us) for a `full` copy. Memory-bandwidth bound, so it scales
   worse than frame advance as thread count rises; `dirty` also grows with what the game
   writes since the run's first slot (docs/libsm64.md, "Savestates"), and its scattered
   pages leave the cache sooner than `fixed`'s contiguous ranges.
3. **Block decoding.** Every scattershot shot replays the base block's segment chain from the
   root by re-running scripts. Cost grows with block depth over the run and shows up as
   "Overhead" in the end-of-run summary.
4. **State trackers.** They run at every frame advance and load. A tracker that itself advances
   frames (for example `StateTracker_BitfsDr::CalculateOscillations` runs up to 50 frames per
   crossing) multiplies the cost of every frame it is evaluated on.
5. **Script bookkeeping.** `GetInputsMetadata`, `GetLatestSave`, the per-level caches and
   tracked-state maps are `std::map` operations per frame, per hierarchy level (the per-level
   containers themselves are a `LevelStack` and cost nothing to enter).
6. **Synchronization.** Named `omp critical` sections in scattershot; `Deterministic` mode
   serves every script's upsert, and each shot's block selection and stop check, in thread
   order through a ticket queue (`QueueThreadById`), so a thread waits for its own turn
   rather than for every thread at every round, spinning for a bounded budget and then
   blocking on the turn counter (the barriers it replaced were 58% of the deterministic
   Tier D run's CPU, the spinning queue about 50%; with the wait blocking, the run's CPU
   outside the resource is about 24%, ROADMAP 3.15).
7. **`PyramidUpdate` construction.** `ImportSave<PyramidUpdateMem>` reads and transforms every
   pyramid surface out of the DLL each time it is called, which is once per frame in
   `RunDownhill` and once per crossing in the trackers.

Measured on the tilt-target workloads (2026-09-14, "Known hotspots, measured" below): the
order holds, with the game at 62% of the production run's CPU, loads at 13%, and the
replays of item 1 being the search's evaluation after each script rather than block
decoding, which is 2 to 3%; the tracker's cost is mostly its status object's allocations.

### What the game writes per frame (2026-09-08)

Measured on the pinned DLL with `dllcheck --dirty-scan 300 --dirty-replay` at frame 3330
of `movies/bitfs-pyramid-jp.m64` (docs/libsm64.md). Consecutive frames compared page by
page; `.data` + `.bss` is 7,108 KB, the `fixed` slices copy 1,464 KB in 41 us.

| | Pattern inputs from 3330, 300 frames | Movie replay, frames 1..3330 |
|---|---|---|
| pages written per frame (min / median / max) | 6 / 57 / 83 | 4 / 49 / 147 |
| bytes changed per frame (min / median / max) | 50 / 5,636 / 110,086 | 7 / 3,075 / 205,872 |
| distinct pages touched after 1 / 30 / 60 / 120 / 300 frames | 58 / 64 / 64 / 74 / 122 | 94 / 105 / 105 / 263 / 286 (303 at 3330) |
| total touched | 122 pages, 488 KB (140 KB of bytes ever changed) | 303 pages, 1,212 KB (482 KB ever changed) |
| touched pages outside the `fixed` slices | 7, all audio scratch (`gAudioHeap`, `gSoundDataADSR`) and the lava texture scroll | 55, adding level-load state (`gDemoInputs`, palettes, gfx buffers) |

Three conclusions. The in-level write set is a third of what the slices copy, so a save
that tracked writes could run about 3x faster. The set keeps growing slowly as Mario does
new things (74 to 122 pages between frames 120 and 300), so any fixed set taken from a
sample can miss a page a later pellet writes; only tracking writes as they happen is
complete by construction. And a frame writes about 57 pages while changing about 5 KB of
bytes, so per-save re-protection (a fault per page per save) would cost more than the
copy it saves; tracking has to accumulate and reset at coarse points (a stage's start save).

## Existing instrumentation

Use it and extend it rather than adding ad-hoc timers:

- `Resource::work` (`ResourceWork`, `tasfw/Resource.hpp`): frame advances, saves and loads
  with the rdtsc cycles each cost, and the slot manager's high-water marks (slots and bytes
  live at once), pool reuses and evictions. Tier C snapshots it per workload, `bitfs-turn`
  per stage; the difference of two snapshots is the work between them (the maxima stay
  the later snapshot's).
- `bitfs-turn`'s stage summary: the counts above, the process cycles, the slot line, and
  the `CPU time` line: the process CPU time over the stage (user and kernel, every thread,
  spin-waits included; `ProcessCpuSeconds` in `ProcessCycles.cpp`) with the resource's
  advance, save and load as shares of it, each with its mean cost, and the share outside
  the resource. The cycles are converted with the TSC rate the stage's own wall clock
  measured, so the line needs no calibration. Tier C's `overheadPct` is the same quantity
  for a single thread (ROADMAP 3.8).
- `BaseScriptStatus` per script and per ad-hoc level: validation/execution/assertion
  durations, save/load/advance durations, and counts.
- Scattershot end-of-run summary: Load / Save / Frame Advance / Overhead / Other percentages,
  plus Futility / Redundancy / Discovery ratios and CSV row counts.
- Every duration is rdtsc cycles, read through `get_time()` in `Resource.t.hpp`; divide by
  the machine's TSC rate for seconds. (Until ROADMAP 3.6, `ExecuteAdhocBase` alone recorded
  its `executionDuration` in milliseconds through `std::chrono`.)

## The performance test suite

Location: a `tasfw-perf` target tree, built and run only in `Release` or `RelWithDebInfo`.
Debug builds use `/Od /RTC1` and are meaningless for performance. Every benchmark emits JSON
(git sha, config, CPU model, thread count, metrics); baselines are checked in under
`perf/baselines/` per machine class, and a compare script prints a delta table.

Two kinds of metric are recorded for every workload:

- **Counts** (frame advances, saves, loads, blocks, hash probes, allocations). These are
  machine-independent and, in deterministic mode, exactly reproducible. They are gated on
  exact equality.
- **Times** (wall, per-operation mean, p50, p99). These are gated with a tolerance against
  the baseline commit's own binaries run in the same session (the reference, "Running the
  suite") and tracked over time.
- **Cycles** (CPU cycles per iteration on the benchmark thread; for Tier D the process
  total over every thread). Reported next to the times as the second opinion on a shift:
  cycles ignore time spent descheduled and mostly ignore the clock, so a wall-time change
  the cycles do not follow is the machine, not the code. Not gated.

Counts catch the regressions that matter most, which are "the framework now does more work",
independent of machine noise.

### Tier A: microbenchmarks (no DLL, run in CI on every PR)

Google Benchmark via FetchContent. Cases:

- `Scattershot::GetHash` over `BinaryStateBin<16>`, including the rehash-on-collision path.
- `BinaryStateBin::AddRegionBitsByNRegions` / `AddRegionBitsByRegionSize`.
- `Inputs::GetClosestInputByYawHau` and `GetClosestInputByYawExact` across a yaw x magnitude
  grid; `GetIntendedYawMagFromInput`.
- `M64::load` / `M64::save` on a 10,000-frame movie; `M64Diff` merge as done by `ApplyChildDiff`.
- `SlotManager` with the mock `Resource`: `CreateSlot`, `LoadSlot`, `EraseOldestSlot` at 100,
  1,000 and 10,000 live slots.
- `Script` bookkeeping with the mock `Resource`: `GetInputsMetadata` and `GetLatestSave` at
  hierarchy depth 1, 4 and 16 and ad-hoc level 0 and 4; `AdvanceFrameWrite` erase cost with
  10,000 cached frames.

Gate: time within 10% of the reference (the baseline commit's binary, run interleaved in the
same session; see "Running the suite"); heap allocations per iteration within 0.1 of the
baseline (counted on every benchmark).

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
  after (100 slots, and 1,000 in the `fixed` and `dirty` modes; 1,000 full saves would pass
  4 GB and skip). `stateBytes` is one state as the resource accounts it, exact and gated:
  7,279,456 bytes full, 1,500,000 fixed, and in `dirty` mode the pages written since the
  baseline (about 500,000 at the anchor) on the pinned DLL. `rssPerSlot` is what the
  process grew by per slot, slot-map nodes included: within 0.2% of `stateBytes` at both
  counts, so a live slot costs its state and nothing else.
- `Addr` (`fixed` family only): one `addr()` lookup, cycling through the four names the
  BitFS scripts ask for at the top of every `validation()` and `execution()`. Single
  thread on purpose: a 16 ns lookup's per-thread time on 16 unpinned threads is the
  scheduler's, not the code's (it moved 20% between runs of one binary), and the loader
  lock it once contended on is gone (performance-changelog.md, 2026-09-14).

`^BM_LibSm64Scaling` runs `FrameAdvance` and `SaveErase` on 1, 2, 4, 8 and 16 threads,
each thread on its own DLL copy with `dirty` saves, as the search runs (thread i loads
the copy whose trailing index is i + 1, `res\sm64_jp_1.dll` onward; the family skips
without those copies). `perf.ps1` does not pin this family: 16 threads on the 8
performance cores' SMT siblings is not the pipeline's shape (16 threads on 24 cores), and
pinning it that way once exposed a savestate race in `LibSm64` that hung 1 launch in 10
(ROADMAP 3.12: the runtime's bytes at the sections' edges, fixed 2026-09-13 and gated
with 100 pinned launches by `scripts\perf_scaling_hang.ps1`). Google Benchmark
reports these rows per thread, so the aggregate rate is n times the row's;
`perf_compare.py` computes efficiency, the per-thread rate at n threads over the rate at
one thread, from each run's own rows. First numbers (2026-09-08, 16 cores, 32 logical
CPUs; MSVC and clang-cl within 3 points): frame advance 100 / 99 / 96 / 80% at 2 / 4 / 8 /
16 threads, fixed-slice save and erase 98 / 99 / 93 / 65% (measured before the `dirty`
mode existed; the family now runs `dirty`). Frames scale until the 16 threads start
sharing physical cores; a bandwidth-bound save drops sooner.

Gate: counts exact (`stateBytes` included); times within 10% of the reference; efficiency at
2, 4 and 8 threads must not fall below the reference's by more than 5 points
(`--efficiency-tolerance`; the baseline's where there is no reference).
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

A third config, `tierd-ci.json` (and `tierd-ci-linux.json` with the `.so` pattern and
`dirty` saves), is the deterministic workload cut to 100 shots on 4 threads for CI, where
only its exact counts gate (`perf/baselines/tierd-ci.json`; docs/libsm64.md, "Continuous
integration"). `perf.ps1` does not run it. Its counts are the same in `fixed` and `dirty`
mode, on every compiler and on both game builds: 2,948,886 frame advances, 104 saves,
174,384 loads, 34,241 blocks, 88,778 scripts and 12 solutions from 100 shots since the
deterministic queue became a ticket on 2026-09-14 (`perf/baselines/tierd-ci.json`, regenerated
from that run through `perf_compare.py tierd`; before it 2,981,801 frame advances, 104
saves, 184,344 loads, 36,347 blocks, 93,774 scripts and 10 solutions, the counts the
2026-09-13 Linux runs matched once scattershot's hash stopped going through `std::hash`,
docs/compilers.md). The stage log of any Tier D run becomes a
benchmark row through `perf_compare.py tierd`, which both `perf.ps1` and CI use.

Both run at High priority. The deterministic run is pinned to one logical CPU per
performance core (`0x5555` on the desktop's 8P+16E i9-13900K: no SMT sibling sharing, no
efficiency core); unpinned, Windows' hybrid scheduler handed each run a different mix of
cores and the wall time moved with it. A machine with fewer performance cores than the
configured threads runs it unpinned, and the runner says so. The throughput run stays
unpinned: its 16 threads would have to share the 8 performance cores' SMT siblings, which
is not the pipeline's shape (16 threads on 24 cores); it is also the configuration that
exposed the savestate race of ROADMAP 3.12 in the scaling family (Tier B), fixed since. `perf.ps1` also runs the
reference `bitfs-turn.exe` on each workload before the current one (`-Alternations`
pairs, default 1, fastest of each); the time gate is current against reference, while the
exact counts still gate against the committed baseline. Both rows carry `cycles`, the
process's CPU cycles over every thread, next to the wall time (spin-waits in the
deterministic run's queue count), and `overheadPct`, the share of the process CPU time
outside the resource from the stage summary's `CPU time` line, gated like Tier C's at
`--overhead-tolerance` points (default 2) against the anchor: the framework's share of a
run, which a change to it moves and the machine's day does not (two runs of one binary
read 24.0 and 24.3%, 19.1 and 19.2%, 68.9 and 68.8% on 2026-09-14). For the
deterministic run the share includes the queue's wait (about 13 points of it since the
wait blocks past a bounded spin; 50 while it spun, 58 with the barriers it replaced), so a
change that alters the spread of a script's cost moves it too.

The deterministic run has the cost model off because automatic savestates depend on measured
timings: with it on the search outcome is still identical (ROADMAP 4.5), but `frameAdvances`
and `saves` are not. Both runs are skipped with `-Filter`, with `-NoTierD`, or when
`res\sm64_jp_0.dll` .. `sm64_jp_15.dll` are missing; together they take about five minutes.
Neither uses the full pipeline in `config.json`.

The interleaved reference exists because of what this machine did before it had one. On
2026-09-08 it drifted from 140 s to 160 s on the deterministic workload within one
afternoon with no VM and nothing else running, and a single A/B in the middle pointed at a
change that four interleaved runs then cleared; Docker Desktop's VM alone added about 11%;
and three times on 2026-09-12 the first run after a large build read 40 to 60% slow with
the phase split unchanged, back at the baseline minutes later (performance-changelog.md). The pre-flight
checks in "Running the suite" refuse the first two states and the calibration catches the
third; the reference cancels whatever is left. If a Tier D wall row still trips the gate
with identical counts and the cycles do not move with it, rerun with `-Alternations 2`
before believing it.

### Reporting and gating

- Tier A runs in CI on every PR. With the maintainer's `LIBSM64_KEY` secret the game jobs
  also run Tier C and the CI-sized Tier D workload there against the committed baselines
  with `perf_compare.py --counts-only`: exact counts and allocations gate, timings are only
  printed (docs/libsm64.md, "Continuous integration").
- Tiers B, C and D run locally before merging anything under `tasfw-core`,
  `tasfw-scattershot` or `tasfw-resources`. Paste the delta table into the PR description.
- Policy: any increase in a gated count, or more than 5% wall-time regression on B, C or D
  against the reference binaries ("Running the suite"), blocks the merge unless the PR
  explains why and the maintainer accepts it.
- New hot-path features must add a benchmark in the tier that covers them.

## Profiling

- Build with `scripts\build.ps1 -Config RelWithDebInfo`. LTO is enabled for every config by
  `add_optimization_flags` (`-DTASFW_LTO=OFF` is only for a toolchain whose LTO is broken,
  docs/compilers.md), and the `/arch` flag is detected at configure time.
- MSVC's OpenMP is the 2.0 runtime (`-openmp`). `-openmp:llvm` is available if newer
  directives are needed; measure before switching.
- Tools that work with this code: Visual Studio Performance Profiler (CPU sampling handles
  OpenMP threads), Superluminal, Windows Performance Analyzer. In-process, the rdtsc counters
  above are the first thing to read.
- Never draw conclusions from a Debug build.

## Known hotspots, measured

The list began as suspects. On 2026-09-14 (ROADMAP 3.8) the stage summary's `CPU time` line
and a sampled profile of both Tier D workloads and the Tier C family put a number on each
(performance-changelog.md, "where the Tier D CPU time goes"). Shares are of the threads' CPU
time on the throughput run (16 threads, cost model on, the pipeline's shape) unless said
otherwise; the resource's own advance, save and load take 76% of it and the rest is 24%.

1. Block decoding replaying from the root on every shot (ROADMAP 4.3): 2.3% (3.0% on the
   deterministic run). Chains are short at 600 to 1,200 shots; the replays that matter are
   item 9. On the `dr-oscillations` stage it is 8.4% at 281,650 blocks after 30,000 shots,
   and grows with the run: that stage is where 4.3 pays.
2. `PyramidUpdateMem` construction copying and transforming all surfaces per call: not on
   the tilt-target workload. The Tier C row says 3.2 us and 42 allocations per
   `GetMinimumDownhillWalkingAngle` call (56 before the FrameMap), which the downhill
   scripts make once per frame: about 30% the construction itself, 5% the stand-in's
   physics, the rest the `TopLevelScript`, resource and slot manager built and torn down
   around one frame. On the `dr-oscillations` stage, where the DR tracker imports it per
   frame of its crossing lookahead, it is below one sample in 25,000 (performance-changelog.md,
   "where the `dr-oscillations` stage's CPU time goes"): the stage crosses rarely for what
   it advances. Not a hotspot on any workload measured.
3. `CalculateOscillations` advancing up to 50 frames inside a tracker evaluation: not on the
   tilt-target workload, the Tier C sweep never crosses (500 frames, 500 advances), and on
   the `dr-oscillations` stage it is 0.01% of the CPU, the whole DR tracker 2.1%. Not a
   hotspot. What that stage pays for instead is per script, since its scripts are one frame
   each: `SelectMovementOptions` 4.1% (the `std::map<MovementOption, double>` of weights
   `AddRandomMovementOption` took by value, built from a braced list per call, and the
   `movementOptions` set reassigned per script; both fixed the same day, the weights an
   `initializer_list` walked in key order and the options a bit mask, the stage's first
   pass identical and its wall time -14%, performance-changelog.md), loads 16.5% (one per
   ten scripts, the `REWIND` option and the decode replays), and block decoding 8.4%
   (item 1).
4. `UpsertBlock` hashing and probing while holding the `blocks` critical section: 0.04%.
5. `std::map` bookkeeping in `Script`: 5.0% in map code, 3.3% inclusive in
   `GetInputsMetadata` (the root's lookup in the movie's map per replayed and tracked
   frame is 2.3% on its own), and about 3.5% of heap time for the framework's own
   allocations: `BaseScriptStatus` per sandbox, `Script::Run`, the inputs-cache, save-cache,
   load-tracker and tracked-state nodes, `LevelStack::Grow`. Together the remainder of
   ROADMAP 3.7, about 9%. Fixed the same day by `FrameMap` (ARCHITECTURE.md, "Script
   hierarchy"): the throughput run's CPU outside the resource went from 22.7% to 19.2%
   and its wall time -2.9%, the deterministic run's wall time -4.1%, with every count
   identical (performance-changelog.md).
6. Console output under the `print` critical section every shot: 0.
7. Barriers per script in `Deterministic` mode: 58% of the deterministic run's CPU time was
   the spin-wait in `_vcomp::PartialBarrierN::Block` (vcomp spins through `SwitchToThread`
   and `NtDelayExecution`, so it counts as CPU and as `process cycles`). `QueueThreadById`
   was one barrier and then one per thread around every script's `UpsertBlock`, and every
   thread waited for the slowest each time. Replaced the same day by a ticket queue in
   thread order, where a thread waits only for its own turn and computes its next script
   meanwhile: the deterministic Tier D run 112 -> 92 s, its CPU outside the resource 63
   -> 56%, the search reproducible on a different path (performance-changelog.md). The
   gate run's cycles still measure waiting, now the queue's.
8. The slot budget (`resources.savestateBudgetMB`, the process budget the threads' resources
   subtract their limits from) and the eviction pattern under memory pressure. Measured 2026-09-13 through the stage summary's slot
   line: the tilt-target stage never holds more than 27 savestates per thread (38 MB) and
   never evicts under the default, so there is no pressure to measure until a search keeps
   far more saves alive; the slot line shows it when one does.

Found by the profile rather than suspected:

9. **Replays are the game time.** 95% of the frame advances replay known inputs
   (`AdvanceFrameRead` 68% of the CPU against 3.7% for `AdvanceFrameWrite`): the
   `TiltTargetShot` evaluation runs the game to the pyramid's equilibrium after every script
   (`GetEquilibriumTrackedState`, 67% inclusive, about 33 frames), plus the rewinds
   `ApplyMovement` makes. The later calls of the same lookahead find their tracked states
   cached, so the cost is one evaluation per script; only fewer or cheaper evaluation frames
   change it (ROADMAP 4.3, `PyramidUpdate` as the stand-in).
10. **The tracker's status object is the heap**: `TiltTargetShotMetrics::CustomScriptStatus`
    held thirteen `std::vector`s for three-element arrays, constructed per tracked frame
    and copied whole wherever a `GetTrackedState` result was taken by value: 7.0% of the CPU
    in the allocator alone (11.1% is heap in total), and most of the 8.9% in `std::vector`
    code. Fixed the same day in the stage scripts: the per-axis values are `std::array`s
    in the tilt-target, osc-final and DR trackers and their solutions, and the tilt-target
    tracker reads its previous states by reference (performance-changelog.md).
11. **`resource->addr()` per call**: 1.5%, and `LdrGetProcedureAddressForCaller` takes the
    loader lock, so 16 threads contend on it (`RtlEnterCriticalSection`, `RtlBackoff`):
    5.6 us per call at 16 threads against 61 ns alone. Fixed the same day: `LibSm64::addr`
    keeps a table of the names it resolved, 17 ns alone and 45 ns at 16 threads (ROADMAP
    3.7, performance-changelog.md).
12. **Loads are memory bandwidth**: the `fixed` load is one 1.5 MB `memcpy`, 41 us alone,
    47 us with 8 threads on the performance cores, 74 us with 16 threads on every core; two
    per script, 13.3% of the CPU.

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
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -SetupDefender  # once per machine: exclude build\, res\ and perf\ from real-time scanning
powershell -ExecutionPolicy Bypass -File scripts\perf.ps1 -NoReference    # absolute against the committed baseline (noisier; see below)
```

Results go to `perf\results\<timestamp>-<sha>.json` (gitignored; `-dirty` is appended to the
commit when the tree has uncommitted changes, which is the usual state when a baseline is
saved). The baseline for a machine is the directory `perf\baselines\<computername>\`
(committed): one file per benchmark family (`Script.json`, `LibSm64Fixed.json`, `TierD.json`,
...) holding that family's repetition rows one per line, and `context.json` with the run's
context and the family order. `-SaveBaseline` writes it from the merged result
(`perf_compare.py baseline`) without Google Benchmark's aggregate rows, which the compare
never reads (it takes the fastest repetition; `--stat median` takes the median of them), so
a family can be read or diffed on its own. `scripts\perf_compare.py` prints the delta table
and exits non-zero on any regression over the threshold (default 10%). Paste that table into
the PR.

**The gate is relative.** `-SaveBaseline` also copies the `tasfw-perf.exe` and
`bitfs-turn.exe` it measured into `perf\reference\<computername>[-clang]\` (gitignored, with
a `reference.json` naming the commit), and every later run launches those next to the
current build: reference, current, reference, current, family by family, and the two Tier D
workloads the same way. `perf_compare.py --reference` gates time and thread-scaling
efficiency against the reference's rows, which saw the same machine state minutes earlier,
and prints the committed baseline's numbers next to them with a **machine factor** (median
of reference over baseline across the rows) that says how the machine reads today. Exact
counts and allocations gate against the committed baseline as before, and the compare warns
when the reference's counts differ from the baseline's (it is then not the baseline commit's
build). Without a reference (a fresh clone, a baseline saved elsewhere) the run compares
absolute against the committed baseline and says so; build the baseline's commit and copy
the two executables into that directory to get the relative gate back.

**The machine is checked first.** `perf.ps1` refuses to run (`-SkipPreflight` overrides)
next to a virtual machine (`vmmem`, Docker Desktop, VMware, VirtualBox) or a CPU more than
8% busy over three seconds, naming the processes responsible (the desktop idles at 3 to
4%). It reports when Defender's real-time scanning still covers `build\`, `res\` and the
perf directories; `-SetupDefender` adds the exclusions once, asking for elevation, and is
the right shape for it: scanning is per file written, the build writes thousands, and an
exclusion toggled per run would need elevation every time and stay behind after a crash.
It switches the power plan to High performance for the run (`-PowerPlan`, alias or GUID;
`powercfg` accepts both without elevation; restored afterwards, also on an error or
Ctrl+C), so the Balanced plan's core parking and frequency ramps stay out of the numbers.
It reads the core topology (`GetLogicalProcessorInformationEx`) so that the efficiency
cores of a hybrid CPU never measure anything: the single-thread families stay on one
performance-core CPU (`-Affinity`, default `0x10`, moved if that CPU is not a performance
core), the deterministic Tier D run takes one CPU per performance core, and the scaling
family and the throughput run stay unpinned (their sections say why). Then it calibrates: the reference binary's `LibSm64Fixed_FrameAdvance` row
(`BinaryStateBin_Pack` without the DLL), pinned as usual, against the baseline's reading of
it. Outside 0.80 to 1.25 the machine is throttled or still busy; the runner waits 30 s and
retries twice, then refuses. The factor, the power plan, the commit and the Tier D
affinities are stored in the result's `context`.

Every benchmark also reports `allocs`, heap allocations per iteration. `tasfw-perf` replaces
the global `operator new` (`tasfw-perf/src/alloc_counter.cpp`) and each benchmark reads the
count around its timed loop. The count is deterministic, so the compare gates it separately
from time: an increase of more than 0.1 allocations per iteration (`--alloc-tolerance`; the
slack only absorbs one-time set-up amortised over the fixed iteration counts) fails the run
even when the clock does not notice. The counter is a relaxed load and store, not
`fetch_add`: a locked read-modify-write right after every `malloc` cost 10 to 20 ns per
allocation and read as a 75% regression on the allocation-heavy benchmarks.

Noise control, learned the hard way while setting this up:

- The gate is relative (above). Comparing against numbers saved on another day read the
  day's drift as a regression whenever the machine was slower, and hid real ones when it
  was faster; the same binary run minutes apart is the only fair anchor for a wall time.
- Nothing measures on an efficiency core by choice. The desktop's i9-13900K has 8
  performance and 16 efficiency cores; a thread on an efficiency core runs the same code in
  more time, and unpinned multithreaded runs got a different mix each time, so the
  single-thread rows stay on one performance-core CPU and the deterministic Tier D run gets
  one CPU per performance core. The 16-thread rows (scaling family, Tier D throughput) stay
  unpinned: packing them onto the 8 performance cores' SMT siblings hangs (ROADMAP 3.12).
- Each benchmark runs three repetitions in each of three fresh processes, and the comparison
  uses the **fastest** of the nine. External noise only ever adds time,
  so the minimum is the best estimate of intrinsic cost. Medians of three drifted 15 to 35%
  between runs of the same binary on a busy desktop. Every repetition measures the same
  state: the Framework benchmarks run their workload once before the measurement starts,
  since the first run on a fresh resource fills the symbol table `LibSm64::addr` keeps, and
  the allocation gate is exact (CI's Tier C gate tripped on a cold first repetition,
  2026-09-15; it runs three repetitions like the suite). The rule (the maintainer,
  2026-09-15): a benchmark that needs a warm state warms it itself, before its
  measurement; the compare never allows for a cold repetition, so the gates stay
  consistent between the suite and CI and between repetitions.
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
  processes, while the same rows on MSVC 19.44 and every benchmark that touched the changed
  code stayed put. On MSVC 19.51 the same rows moved by more than the gate while the walk
  was front-end bound (master with one unrelated benchmark appended read them 15 to 28%
  over master's own binary); with ROADMAP 3.19 the same probe moves them 1 to 6%
  (performance-changelog.md, 2026-09-15). A shift on a benchmark whose code did not change
  is layout: confirm with a re-run, say so in the change log, and re-save the baseline.
  Across a toolset change, measure against master built with the new toolset and passed
  with `-Reference` until the baselines are re-saved on it (ROADMAP 3.18).
- The perf binary's own code moves its tight loops, and by more than 15%. Adding the Tier B
  scaling and memory benchmarks moved `Resource_SaveLoadState` from 149 to 340 ns on MSVC
  and `Scattershot_UpsertBlock_Improve` from 91 to 128 ns on clang-cl with the framework
  unchanged; rebuilt without the new code, both read their baselines. To attribute a large
  shift on a row whose code did not change, rebuild the perf binary without the addition
  and measure the row; then re-save with the numbers in the change log.
- The `Fixed` `SaveErase` and `Load` rows (a 1.5 MB copy that fits the 2 MB L2) have two
  states, about 37 and about 42 us, on both compilers, hours apart, on unchanged code, while
  the full-save rows and the frame advance stay put. A run in the other state on unchanged
  code is re-saved with a note in the change log, not investigated again.

If a result still looks like noise, rerun with `-Repetitions 9` and close other programs
before believing it. Never run two benchmark processes at once, and never benchmark while a
build is running; the pre-flight check refuses to start in either state.

Baselines are per machine **and per compiler**: `scripts\perf.ps1 -Compiler clang` builds
with clang-cl and compares against `<computername>-clang\`. Comparing the two baselines
against each other is the cheapest way to see which compiler the hot paths prefer.

The benchmarks in `tasfw-perf/src/bench_script.cpp` run on `MockResource`, an in-memory
resource whose frame advance is a few nanoseconds, so they isolate what the framework adds
per operation. Everything else there is a direct measurement of the named component.

## Change log

Every hot-path change records its delta table in
[performance-changelog.md](performance-changelog.md), newest first, with the first
measurements (2026-09-07) at the bottom.
