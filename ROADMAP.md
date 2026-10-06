# Roadmap

Status as of 2026-09-15: Phases 1 and 2 are done; Phase 3 is done but for 3.21, a latent
defect in the bare scattershot builder found while writing 3.17 (the guidelines for TASing
with the framework, docs/tasing.md, the phase's last item; the rest landed in #94, #95, #97
and #98 and 3.2's access contract last); Phase 4 has 4.1 and 4.5
done; Phase 5 has its first item, per-scenario movement options, done. Items are ordered; each phase makes the next one safe to do with an AI
agent. Check boxes as work lands and keep "Done when" honest.

**Cross-cutting rules:**

- Performance is as important as correctness and the target is zero-cost abstractions (see
  [docs/performance.md](docs/performance.md)). Every item below must leave the gated
  performance counts unchanged or better, and any item that touches a hot path reports its
  delta table. Nothing in this roadmap is "done" if it made the search slower.
- Every item must build clean with MSVC, clang-cl, GCC and Clang, the six toolchains CI runs
  (see [docs/compilers.md](docs/compilers.md)). Compiler-specific workarounds are documented
  there, never hidden in `#if` forks.
- The documentation describes the current state of the repository. Every change is
  reconciled with AGENTS.md, ARCHITECTURE.md, this file, README.md and docs/ before the
  turn ends: extensions of what is documented are updated in place, deviations from a
  documented rule, goal or claim are reported and wait for approval. The Stop hook in
  `.claude/settings.json` enforces this (see AGENTS.md, "Documentation must match the
  repository").

## Phase 0: where things stood when this roadmap was written

The starting point, kept as written so the items below read against it.

- Builds clean from a fresh configure with VS 2022 (0 errors, 29 warnings).
- No tests of any kind, correctness or performance. The only executable runs the whole BitFS
  pipeline. Timing instrumentation exists (rdtsc counters, scattershot summary) but mixes units
  and has no baseline to compare against.
- Runtime inputs (DLLs, .m64) are gitignored and undocumented except in [docs/libsm64.md](docs/libsm64.md).
- The game DLL is a pinned March 2022 wafel libsm64 build; several hacks depend on that exact binary.
- `main.cpp` is a 537-line experiment log with hardcoded `C:\repos` paths.
- Last substantive work: Dec 2025 ("script dump"). Last framework refactor: 2023.

## Phase 1: make the repo verifiable

Goal: an agent can build, run something small, and know whether it broke anything, in
correctness and in speed.

- [x] **1.0 Onboarding docs.** AGENTS.md, ARCHITECTURE.md, ROADMAP.md, docs/libsm64.md,
      docs/performance.md, `scripts/build.ps1`.
- [x] **1.1 DLL layout self-check.** The `VerifyLayout` script (`tasfw-scripts/inc/VerifyLayout.hpp`)
      cross-checks the copied structs against relationships the game guarantees (Mario's object
      is a whole slot of `gObjectPool`, its behavior is `bhvMario`, `oPos` and `gfx.pos` mirror
      `MarioState::pos`, the floor normal is unit length), reading through `ReadState()`
      only. The pipeline runs it once on one resource before its first stage and stops with the
      report on a failure. Until 2026-09-12 it was a `LibSm64` method called per scattershot
      thread through a `Resource` virtual; the maintainer's review moved it out, since the
      framework has no business knowing about layouts (AGENTS.md, hard rules 9 and 10).
      `dllcheck.exe <dll> <m64> <frame> [--save-mode full|fixed|dirty]` prints the same report,
      adds whether the `fixed` slices cover every hot symbol, and prints frame-advance and
      save/load cost. Result
      (2026-09-07): the pinned 2022 DLL and wafel's 2023 DLL both pass every check, so the newer
      DLL is a drop-in replacement as far as layout goes.
- [x] **1.2 Correctness test tier.** `tasfw-tests` (doctest), run by `scripts\test.ps1` or
      `ctest`, 49 cases / 700 assertions on 2026-09-07:
      - DLL-free: joystick mapping checked against a brute force over all 65,536 stick
        positions, `M64` round trip and gap filling, `BinaryStateBin` packing and clamping,
        `SlotManager` LRU eviction, `LevelStack` (on-demand levels, in-place reset and
        storage reuse, reference stability, slot release on erase), and the script engine's
        invariants on `MockResource` (diff recording, movie fallback, Execute/Modify/Test/ad-hoc
        semantics, exact restore on `Load`, bit-identical replays, hierarchy input resolution,
        `Rollback`, cache invalidation after rewriting a frame, a child's saves surviving
        `Modify`, and metric scripts: recursive metrics, queries ahead of the cursor computed
        in a sandbox, states following the diff on Execute/Modify, unasserted states stored
        as defaults, the metric script type guard); and the bitfs-turn pipeline library (config
        schema and path resolution, scattershot override layering, rejected keys and
        references, solution-file round trip, selection, `input:<metric>` arguments).
      - libsm64 smoke test (skips unless `TASFW_LIBSM64`/`TASFW_M64` are set): loads the DLL,
        passes `VerifyLayout` at frame 3330, plays the movie twice with identical Mario and
        pyramid state, and pins that state to exact golden values. Any one-frame change to the
        movie or the engine before frame 3330 fails it.
      Runs in CI on all four compilers (DLL-free part), and since 2026-09-13 the libsm64
      group as well on the Windows jobs and an Ubuntu 26.04 job, on the bitfs-sbb build
      unlocked from the `LIBSM64_KEY` secret (2.1). `PyramidUpdate` against the DLL is
      covered by 3.3; the scattershot loop end to end by 3.13 on the mock resource (Tier D
      covers it on the game).
- [x] **1.3 Performance test suite.** Done 2026-09-08 (see the sub-items; what is left is
      listed under them and is not part of the done condition). Implements
      [docs/performance.md](docs/performance.md) as a `tasfw-perf` target tree plus the
      `bitfs-turn` runs, Release/RelWithDebInfo only:
      - [x] Tier A microbenchmarks (Google Benchmark, DLL-free): hashing, state bins, input
        mapping, m64 I/O, `SlotManager`, `Script` per-operation overhead, hierarchy depth,
        metric scripts. `scripts\perf.ps1` runs and compares; first baseline committed
        (2026-09-07). Heap allocations per iteration are counted on every benchmark and
        gated at 0.1 (2026-09-07). Runs in CI since 2026-09-08: every Tier A family once
        per job with a short minimum time, to prove the binary executes; nothing is gated
        there.
      - [x] Tier B resource benchmarks. 2026-09-07: frame advance, save (recycled and
        fresh) and load, one family per save mode (`full`, `fixed`, `dirty`), in `bench_libsm64.cpp`; `perf.ps1` finds the
        DLL like `test.ps1` and the families are skipped without it. 2026-09-08: thread
        scaling 1 to 16 (`^BM_LibSm64Scaling`, one DLL copy per thread, unpinned;
        efficiency computed and gated by `perf_compare.py`) and resident set per live slot
        at 100 and 1,000 slots (`ResidentPerSlot`, `stateBytes` exact). Numbers in
        docs/performance.md.
      - [x] Tier C framework workloads with exact-count gates. Done 2026-09-08:
        `bench_framework.cpp` (`^BM_Framework`): the pyramid oscillation, 1,000 downhill-angle
        calls through `PyramidUpdate`, and a 500-frame `BitfsDrMetrics` sweep, with the
        cost model off so frame advances, saves and loads are exact; replay ratio and
        overhead % reported, overhead gated at 2 points. Since 2026-09-21 the workloads
        run on frozen copies of their scripts under `tasfw-perf/workloads/`
        (docs/performance.md), so the rows measure the framework alone while the live
        scripts change with the brute forcer (4.6 to 4.9).
      - [x] Tier D scattershot end to end. Done 2026-09-08: `perf.ps1` runs `bitfs-turn` on
        `perf/tierd-deterministic.json` (8 threads, cost model off, exact counts including
        zero validation failures, see 4.5) and `perf/tierd-throughput.json` (16 threads,
        rates and peak resident set), and folds both into the same delta table.
      - [x] JSON output, checked-in baselines under `perf/baselines/`, a compare script that
        prints the delta table for PRs (`perf_compare.py`: time, allocation, exact-count and
        overhead gates), the regression policy from the spec, and a PR template
        (`.github/PULL_REQUEST_TEMPLATE.md`) that asks for the table.
      - [x] Relative gate and machine checks. Done 2026-09-12: `perf.ps1` runs the baseline
        commit's binaries (`perf\reference\`, filled by `-SaveBaseline`) interleaved with the
        current ones and `perf_compare.py --reference` gates time and efficiency against
        them, the committed baseline anchoring the counts and showing the day's drift as a
        machine factor; Tier D and the scaling family are pinned to the performance cores of
        the hybrid CPU; the runner refuses a VM or a busy CPU, calibrates against the
        baseline, switches the power plan for the run and reports missing Defender
        exclusions (`-SetupDefender`); every row carries CPU cycles next to wall time
        (reported, not gated). docs/performance.md, "Running the suite".
      - [x] Tier D exact counts in CI. Done 2026-09-13 on a CI-sized workload
        (`perf/tierd-ci.json`: 100 shots, 4 threads, about 40 s on the desktop) with its own
        committed counts, alongside `dllcheck`'s layout checks and leak scan and the
        pipeline's dry run (docs/libsm64.md, "Continuous integration"). The full
        deterministic workload stays local (eight copies, minutes on a hosted runner).
      *Done when:* a deliberate extra frame advance in `LoadBase` fails Tier C, a deliberate
      10% slowdown in `GetHash` fails Tier A, and a PR template asks for the delta table.
      Verified 2026-09-08 against the first baselines: one extra save/advance/load per
      `LoadBase` call read as `frameAdvances 42923 -> 44331` and `500 -> 501` (two count
      regressions, plus time and allocation ones); a 3x `GetHash` loop read as +189% and
      +273%. Both mutations were reverted.
- [x] **1.4 Split `main.cpp`.** Done 2026-09-07: `bitfs-turn` runs named stages from a JSON
      pipeline config (`PipelineConfig`: DLL directory and pattern, threads, movie, output
      directory, scattershot defaults, per-stage overrides, typed `args`, `select`, `export`),
      all of them or `--stage <name>`; stage results persist as `solutions/<stage>.json` and
      feed the next stage in memory or from that file; `--list` and `--dry-run` need no
      search. The `error.m64` dump goes through `Configuration::CsvOutputDirectory`. No
      absolute path remains in source; range-v3 is gone. `tasfw-tests` covers the config
      schema, solution files and argument helpers (`tasfw::bitfs_pipeline`). The
      single-threaded pyramid-oscillation experiment in the old `main.cpp` was not ported: it
      cannot link (4.1).
- [x] **1.5 Warnings and the bugs behind them.** Done 2026-09-08. Fixed 2026-09-07:
      C4715/-Wreturn-type in the `TurnAround` lambda, the `printf("%d", uint64_t)` calls, the
      root `Segment` argument order (RngHash was being truncated into `nScripts`),
      `Rotation::Negate`, the two empty-body `if (...);` statements in the Approach/Recover
      stages (now `return false`), the always-false `&&` in `TurnUphill_1f`, eight dropped
      `.executed` results, missing `override`s, unhandled `switch` cases, `main` returning
      `false`, a bool/s32 compare in `PyramidUpdate`, int16-to-int8 narrowing in `Inputs.cpp`.
      Fixed 2026-09-08: the three MSVC C4244s (an `int` literal for the float
      `CrossingDto::speed` in `BitfsDrMetrics.cpp`; the `TiltTargetShot.hpp` initializer
      lists first blamed were never the source), clang-cl's `getenv` deprecation (one
      `tasfw::testing::Env` helper, `_CRT_SECURE_NO_WARNINGS` on its consumers) and Google
      Benchmark's `/MP` under clang-cl (silenced on the two benchmark targets).
      `TASFW_WARNINGS_AS_ERRORS` now covers every first-party target
      (`cmake/Warnings.cmake`) and every CI job passes it; on the way,
      `tasfw-scripts-scattershot-bitfs-dr` got the `add_optimization_flags` call every
      sibling had (LTO, and the GCC `-Wno-missing-requires`; docs/performance-changelog.md). This left all
      four compilers warning-free at their default levels only (MSVC `/W1`, since CMake
      stopped adding `/W3` in 3.15; GCC and Clang without `-Wall`); 1.7 raised them.
- [x] **1.6 Build hygiene and compiler matrix.** Done 2026-09-08: the matrix is green on
      `master` (the merge of PR #79) for windows-msvc, windows-clang-cl, ubuntu-gcc (GCC 13)
      and ubuntu-clang (Clang 17), each configured from its `CMakePresets.json` release
      preset with `TASFW_WARNINGS_AS_ERRORS=ON`, building everything and running the
      DLL-free tests and every Tier A family. `CMakePresets.json` has one
      `<compiler>-<config>` configure preset per compiler and config (`msvc-release`,
      `clang-cl-debug`, `gcc-release`, `clang-relwithdebinfo`, ...) with the build
      directories `build.ps1` always used, plus build and test presets of the same names,
      and `build.ps1` configures and builds through them. The Visual Studio and Ninja
      Multi-Config leftovers in `build/` are gone. Getting the first matrix green
      (2026-09-07) took three runs: CMake 4 rejecting nlohmann/json's minimum version, three
      missing `template` keywords MSVC had accepted, f-suffixed `std::` math functions, a
      missing `<cmath>` (docs/compilers.md). Also fixed there: the CMake compiler-ID bug that
      left MSVC builds without any `/arch` flag, and FP contraction is now off on every
      compiler (docs/compilers.md).
- [x] **1.7 Raise the warning levels.** Done 2026-09-08: `cmake/Warnings.cmake` puts `/W3`
      on MSVC, `/W4` on clang-cl (its spelling of `-Wall -Wextra`; a GNU-style `-Wall` there
      is MSVC's `/Wall`, which is `-Weverything`) and `-Wall -Wextra` on GCC and Clang, on
      every first-party target, and the 1.5 option passes on all four. The inventory before
      the fixes: 106 unique MSVC sites (80 C4244 narrowing conversions, 12 C4018
      signed/unsigned compares, 6 C4267, 5 C4996 `sprintf`, 2 C4101, 1 C4065); 126 clang-cl
      sites at `-Wall` (91 unused variables, 10 set-but-unused, 8 `&&` inside `||` without
      parentheses, 7 unused private fields, 5 unused functions, 3 constructor-order, 2
      deletes through a non-virtual destructor); 155 GCC and 166 Clang sites at `-Wall
      -Wextra` (the same plus 22 sign compares, 18 unused parameters, 3 `-Wtype-limits`, one
      `-Wdangling-reference`, one `-Wmaybe-uninitialized`). Nearly every fix is an explicit
      cast of the conversion that was already happening, a deleted dead local or field, or
      an unnamed parameter; the rest: `Resource` has a virtual destructor, `sprintf` is
      `snprintf`, the metrics hooks and `BitFsPyramidOscillation_Iteration` take
      `int64_t` levels and frames, `RequireObject` takes a `std::string_view`, and
      `BitFsScApproach_AttemptDr_BF` lost an unused constructor argument. Done in one pass
      rather than tree by tree because the casts are codegen-neutral; the perf delta table
      is in docs/performance.md. GCC and Clang were checked in a Docker container before CI
      saw the change (docs/compilers.md, "GCC and Clang locally").
- [x] **1.8 Bump nlohmann/json to 3.12.** Done 2026-09-08: 3.12.0 writes `operator ""_json`,
      which newer clang wanted. The `CMAKE_POLICY_VERSION_MINIMUM` workaround stays, for
      doctest 2.4.11's `cmake_minimum_required(VERSION 3.0)`, not for json. On the way the
      dependency handling became version-safe: every tarball is hash-pinned and cached by
      CMake in `build/downloads` (`TASFW_DOWNLOAD_DIR`), replacing `build.ps1`'s reuse of any
      `<name>-src` directory under `build/`, which would have kept 3.11.2 on this machine
      after the bump.

## Phase 2: loosen the grip of the pinned DLL

Goal: the DLL becomes a reproducible, swappable artifact instead of a mystery binary.

- [x] **2.1 Document and script DLL production.** Done 2026-09-13 (closed by the maintainer;
      see the end of this item). Record which wafel release the 2022 DLL came from,
      how to unlock a `.dll.locked` against a JP ROM with `libsm64_lock`, and add a script that
      makes the N per-thread copies. *Done when:* a fresh machine can populate `res/` from a wafel
      release plus a ROM by following the doc. Progress 2026-09-08: docs/libsm64.md now
      documents a second, scripted source, bitfs-sbb's `fernet-lock.py` (wafel's key
      derivation in Python), which unlocks both the Windows `.dll` and the Linux `.so` for JP
      and US from a ROM; its 2026-06-30 JP builds pass every check against the pinned DLL's
      golden state on both platforms. Policy set by the maintainer the same day: ROMs and
      unlocked binaries never enter the public repo; a CI job that needs them takes the key
      from a maintainer-only secret. Progress 2026-09-13: `scripts/unlock_libsm64.py` is the
      copies script (fetches the pinned bitfs-sbb build, derives the key from a ROM or takes
      it from the environment, verifies both sides by sha256, writes N copies), the source
      movie is committed as `movies/bitfs-pyramid-jp.m64`, and `build.yml` runs the libsm64
      test group and the Tier C count gates on the bitfs-sbb build with the `LIBSM64_KEY`
      secret on the Windows jobs and an Ubuntu 26.04 container job, skipping on forks
      (docs/libsm64.md, "Continuous integration"); the pinned DLL's provenance turned out
      not to be needed for that. Provenance recorded 2026-09-13 by unlocking every locked
      JP DLL in wafel's history with wafel's own locker: the pinned DLL is wafel v0.8.1's
      libsm64 (built 2021-06-17, committed `c155b258`), bitfs-sbb's Windows DLLs are wafel
      v0.8.5's re-locked, and the "wafel 2023" DLL is wafel's 2022-08-07 post-release
      update (docs/libsm64.md, "Known builds"). *Done when* holds: a fresh machine can
      populate `res/` from a ROM by following the doc, which the script makes one command,
      and the provenance of the pinned build is recorded. Not recorded by anyone, and closed
      without it by the maintainer's decision the same day: the decomp fork, commit and
      build command behind the wafel builds. A build from source with a recorded recipe is
      possible (jgcodes2020 built the Linux `.so` from the current decomp) and would be its
      own item if it is ever wanted; nothing depends on it today. Tier D counts in CI moved
      to 1.3.
- [x] **2.2 The sm64 headers against the DLL and the decomp.** Reframed and done 2026-09-13,
      the reframing approved by the maintainer the same day (the original item read
      "Generate the sm64 headers:
      replace the hand-copied `tasfw-core/inc/sm64/*.hpp` with headers generated from the
      decomp source, or from wafel's `sm64_layout` DWARF dump, for the exact DLL build. Done
      when regenerating for a new DLL is one command and 1.1 passes"). What replaced it: the
      copied headers stay and are verified from both sides, without the game.
      `tasfw-tests/src/sm64_layout.inc` is the table of every field offset and struct size of
      the seven structs the code reads through, written by `scripts/dll_layout.py` from the
      pinned DLL's DWARF through wafel's `sm64_layout`, and `test_sm64_layout.cpp` compiles
      it against the headers with `offsetof` and `sizeof` on every run of the tests
      (docs/libsm64.md, "Struct layouts"). `scripts/decomp_pin.json` pins each copied file
      to the n64decomp/sm64 commit and path it came from with the hash of its residual
      edits, and `scripts/decomp_diff.py` recomputes them, in CI too (docs/decomp.md).
      Result: 818 fields and 7 sizes, 0 mismatches against the pinned build; the v0.8.5
      build lays them out identically (its table differs in two `Camera` filler names); 42
      names the newer decomp added are absent from the headers and harmless; the copies are
      Refresh 13 of the decomp except `SurfaceTerrains.hpp` (Refresh 15). *Done when*
      (reframed): checking a new DLL against the headers is one command (`dll_layout.py` on
      it, diff the table) and every copy's origin is recorded and verified. Why not
      generated: the Windows DLLs carry full DWARF, but the constants and the `o*`
      object-field names are not in it (wafel injects them from its own hand-kept table),
      the Linux `.so` has no DWARF at all, the build's own source is a private fork ahead
      of upstream master (docs/libsm64.md, "Reproducing the DLL from source"), and the
      comparison found the copies already exact; a generator would have replaced curated,
      proven headers with a dependency and still needed a hand-kept table for half their
      content. Generating from the DLL stays the option if a build with another layout
      ever appears. The copying itself is the wrong pattern by the maintainer's account and
      is a Phase 5 question ("One declared source for a game's vocabulary").
- [x] **2.3 Lightweight saves that do not depend on the pinned DLL's offsets.** Derive the hot
      regions of `.data`/`.bss` from symbol addresses (Mario state, object pool, surfaces,
      camera, RNG, timers) or adopt the dirty-page tracking that the Linux branch already
      sketched. *Done when:* a lightweight save mode works unchanged on a different DLL
      build, and the search on the pinned build is no slower than before. Both hold
      (amended and closed by the maintainer 2026-09-12: the original wording asked for the
      new mode itself to be no slower than the old offsets, which it is not on this search,
      see below; the offsets stay available as `fixed` by decision). Decided by the
      maintainer 2026-09-12 after a first attempt was erased for putting the policy in the
      wrong place (scripts called a `BeginEpoch` hook on the resource; "stage" and "epoch"
      were pipeline words). The design that replaced it stays inside `LibSm64` and adds
      nothing for a script author: `LibSm64SaveMode`, one of `full` (both sections), `fixed`
      (the old hand-tuned slices, kept for their constant cost) and `dirty` (the default;
      docs/libsm64.md, "Savestates"). `dirty` write-protects the sections, records the first
      write to each page, copies the written pages per save and restores from a baseline
      snapshot on load; the resource takes a new baseline by itself whenever it saves while
      holding no live slots, which is the start save and the first slot of every run. A
      build whose sections are smaller than the slices refuses `fixed` at construction with
      the reason. Symbol-derived slices were rejected (a missed name leaks silently; the
      Windows DLL has no symbol sizes) and start-up calibration too (coverage is a sample).
      Verified 2026-09-12: `dirty` saves and loads exactly (`--leak-scan` zero bytes) on the
      pinned DLL, wafel 2023, bitfs-sbb 2026 and the Linux `.so`, at about 7 us against 41 to
      49 us for `fixed`, on MSVC, clang-cl, GCC 13, Clang 17, GCC 15 and Clang 21. `dirty`
      itself is slower than `fixed` on the BitFS search: the dirty set grows to 525 pages
      (pellets die, the level reloads), so `dirty` first ran the deterministic Tier D
      workload 3.6% slower than `fixed` and the 16-thread throughput workload about 20%
      slower (docs/performance-changelog.md). The maintainer's decision: the pipeline and
      the Tier D workloads select `fixed`; `dirty` is the code default and the mode for any
      other build and for Linux. Follow-up, same day, on the re-baseline idea: implemented
      as a baseline the resource takes at a save once the loads under the current one had
      written back four times the sections' size (in the search, the base save that opens a
      shot), measured, and dropped. It gained about 2% on the deterministic Tier D workload
      and nothing on the 16-thread one, where the cost model's frequent saves made it fire
      about a thousand times per thread: the dirty set regrows to about 500 pages within a
      shot whatever the baseline, because pellets die and the level reloads, and `fixed`'s
      five contiguous ranges stay cache-resident while `dirty`'s scattered pages do not
      (docs/performance-changelog.md). What stayed from the attempt: baselines hold
      copy-on-write pages filled in by the fault handler instead of a whole-section
      snapshot, so taking one copies nothing and memory is only the pages written since
      each began, and a baseline's pages are kept while any live slot's state names it.
      Final numbers, `dirty` against `fixed`: about 10% slower on the deterministic
      workload, about 25% on the throughput one. The decision stands. The two smaller
      leftovers are done: the libsm64 benchmark families anchor 60 frames after the run's
      first slot (the `dirty` rows measure 122 pages) with the frame-advance row registered
      last, and both baselines are re-saved.
- [x] **2.4 Object indices: keep them, verify them.** Decided by the maintainer 2026-09-08 after
      `dllcheck --objects` showed the live pool: BitFS spawns two objects running
      `bhvBitfsTiltingInvertedPyramid` (slot 84 at home x = -1945, the one the setup happens
      on; slot 83 at x = -2866), so the behavior alone cannot name the pyramid and the slot
      index stays the identifier. Home position happens to tell the two apart here, but that
      is not a rule that holds for every object, so it was not made the lookup. Instead each
      hardcoded slot is declared once, with the behavior and (optionally) the home the level
      script gives it (`BitFsExpectedObjects` in `tasfw-scripts/inc/BitFsObjects.hpp`: slots 84,
      83 and 85), and the `VerifyLayout` script verifies the declaration before the pipeline's
      first stage, so a spawn-order change fails start-up with a message instead of feeding
      scripts another object. `bitfs-turn --dry-run` runs the same script to the first
      stage's frame and prints its report. Verified: the three slots report `ok` on
      the pinned DLL and bitfs-sbb's 2026 DLL, and `test_libsm64_smoke.cpp` shows a wrong home, a
      wrong behavior and an empty slot each produce a `FAIL`.
- [x] **2.5 US ROM support.** Done 2026-09-13. `movies/1keyU.m64` (a whole US 1-key run)
      reaches BitFS at frame 3068 and the JP movie at 3001, by the same route; `m64splice`
      (on the `LevelTransitions`, `SpliceMovie` and `MarioTrace` scripts, with `dllcheck
      --levels` and `--trace` beside it) joined them into `movies/bitfs-pyramid-us.m64`,
      which plays the JP movie's 803 in-level frames identically on the US DLL once both
      are cut ten frames before entry: the US movie turns the 8-directions camera during
      the warp, and that offset persists across levels (docs/libsm64.md, "A movie for the
      US game"). Frame 3397 there is the JP movie's 3330, the same golden state, and the
      libsm64 test group passes on `res/sm64_us_0.dll` (`scripts/test.ps1` runs it there
      whenever the DLL exists; not in CI, by decision). `Rom` and
      `CountryCode` have the US values; `M64` reads the game a movie is for from its header
      and writes it back, and `ExportM64` marks an export with the source movie's;
      `LibSm64::CheckMovie` compares a movie with the game a DLL was declared to be
      (`LibSm64Config::countryCode`, kept declared by decision: from the DLL's file name, a
      `--version` flag, or the movie itself in the pipeline, whose `dllPattern` follows the
      movie through `{version}`). A re-basing helper for diffs (what `SpliceMovie` does by
      hand) was considered and not wanted.

## Phase 3: framework hardening

Goal: the core's implicit invariants become explicit and enforced. Status 2026-09-15: every
item is done except 3.21, a defect found while writing 3.17; then Phase 4.

- [x] **3.1 Frame cursor semantics.** Decided by the maintainer 2026-09-08: `Modify` leaves the
      cursor at the end of the child's diff on purpose, because the common case is to keep
      going from there. A caller that wants the child's stopping frame loads it (what
      `ScattershotThread` does, since blocks are keyed by that frame), and a caller that wants
      to roll back runs `Execute` and applies the returned diff later. Written into
      ARCHITECTURE.md; the TODOs that proposed changing `Modify` are gone.
- [x] **3.2 Encapsulation.** Done 2026-09-15 in three stages, each on the maintainer's
      decisions. `ScriptFriend` is retired: `Script` befriends `TopLevelScript` with the
      primary's constrained template-head, which MSVC 19.44 and 19.51 and clang-cl 19 and 22
      accept (docs/compilers.md has the forms that do not); the IntelliSense check in Visual
      Studio 2026 was the maintainer's. The root's copy of the level walk in
      `GetInputsMetadata` stays: one walk for both, ending in a private virtual the root
      overrides for the movie, cost 7 to 18 ns per uncached lookup on MSVC 19.51 against the
      root's own walk (docs/performance-changelog.md), so the override reads its levels
      through the friend. A metric script's own frames skip the root's `RecordMetrics` at the call
      site; the root sets its tag itself. `startSaveHandle`, `TopLevelScript::_m64` and
      `resource` are private. The resource's surface: the slot manager is private to
      `Resource` and its own data private, a slot is reached only through `SaveState`,
      `LoadState`, `DisposeState` and `HasState`, and the tests and benchmarks whose subject
      is the slot manager itself reach it through `PerfAccess` (tasfw-testing), the friend
      Scattershot already had; `SlotHandle`'s pointer and id are private with `Script` their
      friend; the start save is `SaveStart(frame)`, `LoadStart()` and `InitialFrame()` on
      `Resource`, the state itself private behind the protected predicate `IsStartSave` that
      `LibSm64`'s dirty mode asks; `sign` and `CopyVec3f` left `Script` for `ScriptMath` in
      tasfw-scripts; the five public methods without callers (`OptionalSave`, `RollForward`,
      `Restore`, `GetBaseDiff`, `TestAdhoc`) stay as escape hatches. The access contract
      (hard rule 9): `ReadState("symbol")` on `Script` is the one way a script sees game
      memory, and the 203 reads in 33 files that went through `resource->addr()`,
      `ScattershotThread`'s six included, go through it; `ExportSave<UState>(params...)`
      hands the script's state to a run on another resource through `Resource::State`, which
      `PyramidUpdateMem` now converts from (`const Resource<LibSm64Mem>&`), so the 23 sites
      that passed `*resource` read `ImportSave(ExportSave<PyramidUpdateMem>(pyramid))` and
      the drift test exports the same way; its second form takes a frame, loaded through the
      script's own loads and returned from after, and both carry `Resource::State`'s
      constraint so a frame is never taken for a state parameter, restated as a concept-id
      because Visual Studio 2026 18.10's IntelliSense never satisfies it asked of `State` in
      a requires-expression (docs/compilers.md, "Visual Studio 2026 IntelliSense"). The
      engine tests read the
      mock they own, which answers `ReadState("checksum")` for the scattershot mock. What
      the contract still owes, reads typed by symbol, their const-ness (the 53 helper
      prototypes taking game pointers non-const stay as they are), a guard against invalid
      access and the write side, `HackMemory`, is Phase 5's, with the hacks (the maintainer,
      2026-09-15).
- [x] **3.3 PyramidUpdate drift test.** `test_libsm64_pyramid.cpp` imports `PyramidUpdateMem` from
      the DLL before each of 240 frames (Mario walks to the pyramid's centre, then it settles;
      91 frames move the normal), advances both, and requires the normal to match
      bit-for-bit. Passes with max |diff| = 0 on MSVC and clang-cl (2026-09-07), which is also
      the bit-exactness test for the FP flags in docs/compilers.md. 2026-09-08: also max
      |diff| = 0 with GCC 15 and Clang 21 on Linux against bitfs-sbb's JP `.so`, and on
      Windows against bitfs-sbb's 2026 JP DLL (3.4, docs/libsm64.md). Learned on the way:
      terrain objects update before the player, so the pyramid reads Mario's previous-frame
      position (ARCHITECTURE.md).
- [x] **3.4 Linux parity.** Done 2026-09-13: the `ubuntu-26.04-gcc` and `ubuntu-26.04-clang`
      jobs pass the libsm64 test group, `dllcheck` with a zero-byte leak scan, the dry run
      and the Tier C count gates against the `.so` unlocked from the secret (2.1), so both
      halves of the "Done when" hold in CI. The one divergence from the Windows results,
      noted as this item asks and then resolved the same day: the CI-sized Tier D search
      first took a different path on the `.so`, identically in `dirty` and `full` mode and
      on both compilers, which pointed away from the save path and, on inspection, away
      from the game too: scattershot's block hash and RNG chain went through
      `std::hash<std::byte>`, which libstdc++ and MSVC's STL implement differently. With
      the framework's own `HashByte` (FNV-1a per byte, MSVC's value, so every Windows
      result stands) the Linux search reaches the DLL's exact counts with GCC 15 and Clang
      21, and one `perf/baselines/tierd-ci.json` serves every job (docs/compilers.md,
      docs/libsm64.md "Linux").
      Build with GCC/Clang, confirm the `mprotect`/`SIGSEGV` save path works,
      and note any divergence from MSVC results. *Done when:* the DLL-free tests run on Linux CI
      and the Linux `LibSm64` path passes the smoke test against a Linux libsm64 build.
      Progress 2026-09-08: both halves hold once, by hand. The DLL-free tests run on Linux CI
      (1.6), and in an Ubuntu 26.04 container the whole libsm64 test group passes against
      bitfs-sbb's JP `.so` with GCC 15 and Clang 21: every layout check, the identical
      golden state at frame 3330, save/load determinism and the drift test at max |diff| = 0.
      `dllcheck` there reads 21.0 us per frame advance and about 45 us per save or load
      (docs/performance-changelog.md). What keeps this open: the `.so` needs glibc 2.43
      (for `sqrtf`), which Ubuntu 24.04, CI's `ubuntu-latest`, does not have, so a CI run
      needs a 26.04 runner as well as the maintainer-only secret for the unlock key. Closed
      by 2.3 (2026-09-12): the Linux save path is the same `dirty` mode as on Windows, one
      page set per instance with a handler that finds the owner of a faulting address, so
      several `LibSm64` per process work; `fixed` is refused on the `.so` because its
      sections are smaller than the slices, and the tests fall back to `dirty` there. Two
      things had to change to get here, both in docs/libsm64.md: the decomp renamed the
      pyramid behaviors, now bridged by `LibSm64SymbolAliases`, and doctest's `<ciso646>`
      include is a `#warning` under Clang 21 (docs/compilers.md).
- [x] **3.5 Savestate memory budget.** Done 2026-09-14. The 8 GB cap was per resource, so
      16 threads could address 128 GB. Now `SlotBudget` in `SlotBudget.hpp` is a process-wide
      budget and its balance: a resource subtracts its limit from the balance when it is
      created (the one argument of `Resource`'s constructor; `LibSm64Config::savestateBudgetBytes`,
      16 MB for `PyramidUpdate`, 64 MB for the mock), or throws if the balance is too low,
      and adds it back when it dies. Whether it uses the memory does not matter. Nothing on
      the save or load path changed; no script and no run touches a resource for it. The
      application sets the budget: the pipeline from `resources.savestateBudgetMB` (default
      8192, what one thread alone was allowed before), giving each thread's game resource
      an equal share less the 16 MB a script's `PyramidUpdate` takes per thread. The
      maintainer's framing (2026-09-14): an application-wide budget and balance across all
      active resources, each subtracting its configured limit in full, and nothing more;
      not a scattershot setting, and not usage accounting. What the pipeline holds, from
      the slot line 3.6 added: at most 3 savestates per thread with the cost model off and
      25 to 27 with it on (35 to 38 MB of `fixed` slices), zero evictions in every run, so
      no count changed at the default; the checks are in docs/performance-changelog.md.
- [x] **3.6 Unify timing instrumentation.** Done 2026-09-13. `Resource::work`
      (`ResourceWork`, docs/performance.md "Existing instrumentation") holds the counts,
      their rdtsc cycles and the slot manager's high-water marks, pool reuses and evictions;
      every duration is rdtsc cycles, `ExecuteAdhocBase` included (it alone had recorded
      milliseconds through `std::chrono`, and the cheaper clock reads on the empty ad-hoc
      row as -15%); the Tier C benchmarks and `bitfs-turn` read the struct instead of
      copying fields, and the stage summary prints the slot line. Every gated count
      identical; the delta table is in docs/performance-changelog.md.
- [x] **3.7 Remove known non-zero-cost spots**, each gated by the suite. Done 2026-09-07:
      `Resource::setInputs()` with cached `gControllerPads`/`sm64_update`/`gGlobalTimer`
      pointers in `LibSm64` (measured within noise: `GetProcAddress` is 62 ns here); the six
      per-level `unordered_map`s in `Script` replaced by `LevelStack` (ad-hoc overhead -56%,
      child scripts -24 to -32%, recorded frames -22%, deep rewinds -46%; docs/performance.md).
      Also done 2026-09-07: per-level containers constructed on first use and reset in place
      on pop (no allocation per ad-hoc level, no save bank for scripts that never save);
      metrics entries created on first insert; the `dynamic_cast` in `GetMetrics`
      replaced by a per-type tag compare; statuses moved rather than copied out of finished
      scripts and `GetMetrics` returning a reference; and a `SlotHandle` move that
      copied the slot id, so every save a child handed to its parent on `Modify` was erased
      by the child's bank and replayed later (now pinned by a test). Allocation counts and
      timings are in docs/performance-changelog.md. Done 2026-09-14, from the 3.8 profile:
      scripts resolving symbols per execution (`LibSm64::addr` keeps a table of the names
      it resolved; 17 ns alone and 45 ns at 16 threads against 61 ns and 5.6 us through
      the loader lock, the new Tier B `Addr` rows), and virtual dispatch on `Resource` per
      frame, measured and closed: `LibSm64::advance` and `setInputs` together are 0.02%
      of the throughput run's samples, so the virtual call is not separable from noise.
      Last done 2026-09-14: the `std::map` head node MSVC allocates for each container a
      script touches, and one map node per cached frame. Every one of the 11 allocations
      of an empty child script and most of the 14 of a recorded frame were sentinel nodes
      of `M64Diff`'s and the per-level caches' maps, constructed and moved per status
      object and per level, about 9% of the throughput run's CPU with the map code (3.8).
      `FrameMap` and `FrameSet` (`tasfw/FrameMap.hpp`, ARCHITECTURE.md "Script
      hierarchy", pinned by `test_framemap.cpp`) now hold `M64Base::frames` and the five
      per-level containers; the metrics stay a node container because a metric script
      reads its previous states by reference while it may track another frame. Design
      presented under hard rule 10, prototyped at the maintainer's request and accepted on
      its numbers (2026-09-14): an empty sandbox 73 -> 25 ns and 2 -> 0 allocations, an
      empty child script 408 -> 197 ns and 11 -> 2, a recorded frame 753 -> 452 ns and
      14 -> 3, the movie's load 10,004 -> 27 allocations, `UpsertBlock` -42 to -57% (a
      solution carries a diff); Tier D deterministic -4.1% and throughput -2.9% in wall
      time against the same day's binary, every count identical, 0 regressions in the
      suite (docs/performance-changelog.md). Closed with it: nothing of this item remains.
- [x] **3.8 Hotspot investigations.** Work through the "known hotspots" list in
      docs/performance.md, measurement first, one PR each, with the Tier C/D delta table.
      Measured 2026-09-14 (docs/performance-changelog.md, "where the Tier D CPU time goes";
      the list in docs/performance.md now carries the numbers): `bitfs-turn`'s stage
      summary prints a `CPU time` line, the resource's advance, save and load as shares of
      the process CPU time over the stage, and a sampled profile of both Tier D workloads
      and the Tier C family attributed the rest. On the throughput run, the pipeline's
      shape, the resource takes 76% of the CPU (game 62%, `fixed` loads 13%, saves 1%) and
      the 24% outside it is 11% heap (7% of it the tilt-target metric script's `std::vector`
      status, copied per recorded frame), 5% map code, 3% `GetInputsMetadata`, 1.5% symbol
      resolution under the loader lock, and the search's own code; 95% of the frame
      advances are replays, the evaluation to the pyramid's equilibrium after each script.
      The deterministic gate run is 58% barrier spin-wait, the spread of a script's cost
      under `QueueThreadById`'s barriers, so its `process cycles` row measures waiting.
      Block decoding is 2 to 3%; `UpsertBlock`, the `print` section and the slot budget are
      nothing. Done from that list the same day: the framework's per-sandbox and
      per-frame allocations and map nodes (3.7, `FrameMap`), `addr` per call (3.7, the
      table), and the metric scripts' status objects (`std::array`s instead of `std::vector`s
      for the per-axis values in the tilt-target, osc-final and DR metric scripts and their
      solutions, the tilt-target metric script reading its previous states by reference; a
      stage-script change, measured in docs/performance-changelog.md). The
      `dr-oscillations` stage was profiled the same day for the two suspects the
      tilt-target workload never runs: the `PyramidUpdateMem` import and
      `CalculateOscillations` are below 0.02% of that stage's CPU (the crossing path runs
      rarely for what the search advances), so the list's items 2 and 3 close as
      measured; what the stage pays for is per script, its scripts being one frame each
      (docs/performance-changelog.md, "where the `dr-oscillations` stage's CPU time
      goes"). The movement-option weights, a `std::map<MovementOption, double>`
      `AddRandomMovementOption` took by value and every DR script built from a braced
      list (4.1% of that stage with the `movementOptions` set reassigned per script), are
      `initializer_list`s walked in key order and the options a bit vector that grows with
      the enum since the same day, designed under hard rule 10 and prototyped for the
      maintainer's decision:
      the draw is the same (the `dr` stage's first pass identical in deterministic mode,
      3.17 M scripts), wall -14% on that stage (docs/performance-changelog.md).
      The last item, the barriers of `Deterministic` mode, closed the same day: every call
      of `QueueThreadById` takes a ticket (call k of thread i is k·N+i) served in that
      order by a shared turn, so the upserts keep the order the barriers gave them while a
      thread waits only for its own turn; base-block selection and the end-of-shot
      counts take tickets too, since they read shared state, and a thread retires from
      the queue when its shots end. Designed under hard rule 10, prototyped on a branch,
      accepted by the maintainer on its numbers with the implementation in the `.t.hpp`
      and a four-thread reproduction added to the mock test: the deterministic Tier D
      run 112 -> 92 s, reproducible to the last count on two runs, on a different path
      than the barriers' (a thread selects its block at its ticket, not after a lockstep
      round), so the deterministic counts changed once and `perf/baselines/tierd-ci.json`
      was regenerated; the perf baselines' `TierD_Deterministic` row is re-saved with
      the next `-SaveBaseline` (docs/performance-changelog.md). Also done the same day:
      the Tier D rows carry `overheadPct`, the share of CPU time outside the resource,
      gated at 2 points like Tier C's (its same-binary spread that day was under 0.3
      points; docs/performance.md, "Tier D"). Block decoding at 8.4% of the `dr` stage
      is 4.3's. Found on the way and left as 3.14: deterministic mode with piped-in
      input solutions does not reproduce.
- [x] **3.9 Pool savestate buffers.** Done 2026-09-07: `SlotManager` keeps erased and evicted
      states in a bounded pool (32) that the next `CreateSlot` reuses, so a save into a
      recycled state is one copy. `dllcheck`: full save 1561 -> 191 us against a 222 us load,
      lightweight 285 -> 50 us against 53 us. Pooled memory counts toward the slot budget.
      Gated by the Tier B `SaveErase`/`Load` benchmarks (docs/performance-changelog.md).

- [x] **3.10 Make the compare concepts actually constrain.** Done 2026-09-13. The concepts in
      `ScriptCompareHelper.hpp` were `requires { std::same_as<...>; }` blocks, which check
      that the expression is well-formed and never that it holds: a comparator, terminator or
      parameter generator that could not be called at all was rejected, but any return type
      passed; and `AdhocCompareScript` there and `constructible_from_tuple` in
      `SharedLib.hpp` (`Concepts.hpp` now), which unpacked the tuple with `std::apply` around a lambda whose
      `static_assert` a requires-expression never instantiates, accepted every tuple and
      made a non-tuple a hard error inside `std::tuple_size`. Each is now a constraint on the
      call's result type, the two tuple ones through a partial specialization for
      `std::tuple<Ts...>`, so a wrong callable or tuple leaves no viable overload at the call
      site (docs/compilers.md, "A concept-id as a requirement expression is always
      satisfied"). No caller changed: every BitFS script passes generic lambdas of the right
      shape. `test_script_compare.cpp` pins it with static_asserts on each concept and on
      the `Compare` and `CompareAdhoc` calls themselves, and holds the first runtime tests
      of the family on the mock resource. `-Wno-missing-requires` is gone from
      `add_optimization_flags`, so GCC's warning is live again. Verified as the "Done when"
      asks with MSVC, clang-cl, GCC 13 and 15 and Clang 17 and 21, warnings as errors;
      compile-time only, so the machine code is the same instruction for instruction
      (docs/performance-changelog.md).
- [x] **3.11 Pluggable savestate policy.** Closed 2026-09-14: analysed and moved to Phase 5
      ("A savestate policy the run chooses", where the design discussion is recorded); nothing
      of it remains in this phase. The item as first
      written was wrong on one point, corrected by the maintainer: the policy is the
      scenario's, not the resource's. What stood on its own was done instead. The
      `shouldLoad` branch that never fired was a regression: `Load` and the revert path
      both compared the found save's frame with the target when the branch was written
      (2022-03-22), `Load` was corrected to compare with the cursor on 2022-04-09, and the
      2022-06-14 refactor that folded `Load` into the revert path kept the wrong one, which
      `LongLoad` copied in August 2022. Fixed 2026-09-13 in both; the counts and times are
      in docs/performance-changelog.md. The other weakness noted here, every scattershot
      pellet starting with empty frame counters, is ownership, not a bug: a pellet's
      counters belong to its ad-hoc level and die with it, so only a different policy can
      change what a rewound stretch costs. The counters and timings became one struct
      (3.6) and the budget is 3.5.

- [x] **3.12 Sixteen-thread hang under CPU pinning.** Found 2026-09-12 while pinning the
      perf suite to the performance cores: with the `^BM_LibSm64Scaling` family (16 threads,
      one DLL copy each, `dirty` saves) pinned to the i9-13900K's 16 performance-core logical
      CPUs at High priority, 1 launch in 10 hung at the 16-thread `FrameAdvance` row, for the
      current build and for the baseline commit's binary alike; unpinned, 0 in 20. Windows
      Error Reporting logged one thread dying with `0xC0000264` (`STATUS_RESOURCE_NOT_OWNED`,
      a lock released by a thread that does not hold it) in `ntdll.dll`; the other threads
      then wait at Google Benchmark's end barrier for a thread that no longer exists, and the
      process lives on with 5 s of CPU. Named 2026-09-13 from the three crash dumps of the
      12th (ntdll's public symbols and the DLL's own COFF symbols were enough; no PDB needed):
      the thread died in `RtlLeaveCriticalSection`, called from the game DLL's mingw-w64 TLS
      callback (`__dyn_tls_dtor` -> `__mingw_TLScallback` -> `__mingwthr_run_key_dtors`,
      `tlsthrd.c`), which the loader runs on **every** exiting thread for **every** loaded
      DLL. The critical section it had entered, `__mingwthr_cs`, lives in the last page of
      the DLL's `.bss`, and at the moment of the leave it read as pristine: unlocked, owner
      0. The lock was not released by the wrong thread; it was reset under the right one.
      The benchmark keeps one `LibSm64` per thread alive for the process and every thread
      ends its `FrameAdvance` measurement with a load before it exits, so one thread's final
      `dirty` load restored the last `.bss` page of *its* DLL (dirtied by the exiting
      thread's own first write to that critical section) while the exiting thread was
      between the enter and the leave in *that* DLL's callback. Pinning spread the threads'
      finishing times enough for the two to overlap. Nothing in the fault handler or the
      registry was at fault; the framework's own locks were never involved. Fixed in
      `LibSm64` by leaving the C runtime's bytes out of every savestate: `LibSm64KnownGameBytes`
      bounds the game's bytes of each section per known build (derived from the DLL's COFF
      symbol table by `scripts/dll_game_bytes.py`, verified at construction against that
      critical section), `full` copies the two ranges, `fixed` cuts its slices to them, and
      `dirty` copies and restores each page but for the part outside them; unknown builds
      (the Linux `.so`) stay whole (docs/libsm64.md, "The game's bytes"). The test in
      `test_libsm64_savemodes.cpp` sets that critical section's spin count and requires a
      load to leave it; on the build without the bounds a `dirty` or `full` load put the old
      count back, on both the JP and the US DLL. `fixed`, the pipeline's mode, never reached
      the tail and was never exposed; `dirty` (the default, the scaling family, the Linux
      CI) and `full` were. Gate (2026-09-13): 100 pinned launches of the family on the
      fixed Release binary, 100 ok, 0 hung (`scripts\perf_scaling_hang.ps1 -Binaries current
      -Placement pinned -Runs 100`). The control is weaker than hoped: the reference binary,
      built before the fix, also passed its 30 pinned launches that day, and 60 more earlier
      the same day, where on the 12th it hung 1 in 10, so the day's machine never produced
      the race on either binary; what shows the fix works is the crash dump (the reset
      section) and the test (the load that puts the old spin count back without the
      bounds), not the count. Tier B and D numbers in docs/performance-changelog.md: every
      row within noise of the reference, counts identical. `perf.ps1` keeps the scaling
      family and the throughput run unpinned, now by choice rather than necessity: 16
      threads on the 8 performance cores' SMT siblings is not the pipeline's shape
      (docs/performance.md).
- [x] **3.13 Scattershot on the mock resource.** Done 2026-09-14: `test_scattershot_mock.cpp`
      runs a `ScattershotThread` on `MockResource` (a four-byte bin of frame offset and a
      checksum byte, a movement that writes one random-stick frame, the cost model off so
      every count is exact) in well under a second, and pins what only the CI-sized Tier D
      pinned before: a seed reproduces its search, shots, scripts, blocks, solutions and the
      resource's work alike, single-threaded and on two, three and four threads in deterministic mode, and
      another seed does not; a limit of one slot evicts and replays without changing the
      search; and a bin that is not a function of state (it counts its own calls) fails the
      base-block validation of 4.5 and is counted, with the diagnostics' `error.m64`
      written. The thread reads the run's totals through `PerfAccess` in its `assertion()`.
      Found on the way: the 4.5 diagnostic sized its `error.m64` from the last frame of the
      total diff, undefined when the failing base block is the root with nothing applied;
      guarded. Identified 2026-09-14 when 3.5 went in with the pipeline-config test, the
      slot tests and the Tier D slot line as its only checks.
- [x] **3.14 Deterministic mode with piped-in input solutions.** Done 2026-09-14 (branch
      `queue-fixes`). Found the same day (3.8): the `dr` stage's second pass, which starts
      from the solutions its first pass piped in, differed between two runs of one binary
      in deterministic mode (8,334 and 7,788 scripts on the same seed) and, under CPU
      contention, hung with every thread spinning in `WaitForTurn` for a turn passed to a
      ticket no thread takes, while a first pass with no inputs reproduced to the last
      count. `Initialize` handed the input solutions out through a shared index, so which
      thread applied which input was timing order, and an input count that is not a
      multiple of the thread count left the threads with different queue-call counts and
      their later calls pairing across that boundary. Now the inputs go out in rounds
      keyed on the thread id: in round r thread i applies input r * threads + i when there
      is one and makes its queue call either way, so every thread makes the same number of
      calls (the shared index and its critical section are gone). Verified: the dr stage's
      second pass reproduces run to run and between a Release and a RelWithDebInfo build
      (1,004 blocks, 15,047 scripts), the mock test pins piped-in runs on three threads
      with two inputs and on two threads with three, and the no-input workloads keep their
      counts (the CI-sized Tier D). The counts of a stage with inputs change once, as they
      must: the dr scratch stage's first pass went from 9 to 148 solutions. A day of
      chasing a "build-dependent" divergence on the way was a stale binary: `test.ps1`
      builds only the tests target, so `bitfs-turn.exe` stays at whatever `build.ps1` last
      made (AGENTS.md, "Build and run").

- [x] **3.15 The ticket wait spins on libomp.** Done 2026-09-14 (#94).
      Seen in the clang-cl perf suite: the deterministic Tier D run's wall time fell 40%
      against the pre-branch binaries while its process cycles rose 57%, where the MSVC
      run's cycles fell with its wall time; `WaitForTurn` (3.8) spun until its ticket came
      up, and libomp's barriers had slept after their spin budget where vcomp's spun.
      `WaitForTurn` now spins for `SpinBudget` (4,096) pauses and then waits on the turn
      counter (`std::atomic::wait`), which `PassTurn` notifies after its store. Measured
      on both runtimes (docs/performance-changelog.md): cycles -59% (MSVC) and -5%
      (clang-cl) against the references where the spin read -29% and +57%, CPU outside the
      resource 24% where it was 56%, wall within 2% (MSVC) and 5% (clang-cl) of the spin
      version and 28 to 35% under the references, counts unchanged; a four-times-larger
      budget bought 1.5 s of wall for 22% more CPU and was not taken.

- [x] **3.16 `BM_M64_Save_10k` is 20% slower on clang-cl since the FrameMap commit.** Done
      2026-09-14 (#94). Seen in the clang-cl perf suite (1.4 to 1.7 ms;
      MSVC's build of the row went the other way, 2.1 to 1.8 ms) and bisected to 0fb3aaf,
      the sorted-vector containers of 3.7, with its parent still fast. An xperf profile of
      the benchmark on both builds (clang-cl, Release codegen with symbols) named it: a
      third of the samples in `FrameMap<uint64_t, Inputs>::operator[]`, a call per lookup
      that clang-cl does not inline into `M64::save`'s loop (four lookups per frame), where
      the map's lookups had been inlined and MSVC inlines both. `M64::save` walks the sorted
      frames once instead: 0.8 ms on both compilers (clang-cl -41% and MSVC -61% against
      their pre-branch references). Three rewrites of the loop had "changed nothing" the
      same day because they were measured with `perf.ps1 -NoBuild` after `test.ps1`, which
      builds only the tests: the perf binary was the old one every time (AGENTS.md, "Build
      and run").

- [x] **3.17 Agent instructions for TASing with the framework.** Done 2026-09-15:
      [docs/tasing.md](docs/tasing.md), with the maintainer's rules (use the framework's
      idioms and do not hack around it; bring up what seems impossible; the existing
      scripts as a guideline, not a constraint; the compare family; the fastest TAS and the
      most efficient script from the framework, the decomp and algorithms; ad-hoc for
      one-offs; metrics for decisions) as a section of AGENTS.md, "TASing with the
      framework", pointing to it. The shape decided as both: the rules where every agent
      reads first, the how-to where long-form material lives. The page covers the four
      things this item asked for below, with the pitfalls the committed scripts encode, and
      it states that there is no general-purpose script runner (a new script runs as a
      stage type, a test, a benchmark, a tool, or its own executable in a new folder with
      a `main.cpp`). The maintainer's clarifications of 2026-09-16 are written in: the
      speed-versus-efficiency tradeoff is contextual (movie frames usually, overall
      performance for the squish-cancel brute forcer), metrics also serve raw variables in
      the past and looks ahead, scattershot is a route finder in general, simulating the
      relevant subset of the game as a resource is often worth it, a rewind generally costs
      more than a frame advance. Writing it found 3.21 and 4.9. Added
      2026-09-14 (the maintainer): guidelines, for an agent or a person, on how to TAS
      with the framework
      once the pieces above are settled, so that Phase 4 starts from an agreed way of
      working rather than from the code alone: which tool to reach for (an ad-hoc attempt,
      a script class, a metric script, a scattershot stage), how a goal turns into a script
      and a run, how a result is checked (counts, reproduction, exports) and which of the
      hard rules bite while TASing. Not complicated, the maintainer's words; written
      after 3.2, whose encapsulation the guidelines describe as settled, and before
      Phase 4.
      *Done when:* the guidelines exist and an agent given the repository and them can
      create, run and check a new script without further instruction. The first half
      holds; the second is borne out, or not, by the first Phase 4 script written from the
      page alone.
- [x] **3.18 Perf baselines on the Visual Studio 2026 toolset.** Done 2026-09-15. With
      Visual Studio 2026 installed, `scripts\build.ps1` (vswhere, latest install) builds with
      its MSVC 19.51 and clang-cl 22; `perf\baselines\tyler-desktop*` and the references
      under `perf\reference\` were 19.44's and clang-cl 19's, against which 19.51 read most
      of the Script family 14 to 35% slower (3.19 found why and closed most of it) and the
      throughput Tier D 12% faster. Both baselines and both references are re-saved from
      the 3.19 code on the new toolset (`-SaveBaseline`, then `-Compiler clang`). While
      they were stale, a change was measured against master built with the same toolset
      and passed with `-Reference`, as 3.2's and 3.19's tables were; that remains the way
      to measure across a toolset change (docs/performance.md). The uncached `GetInputs`
      rows had proved placement-sensitive beyond the gate on 19.51 (master with one
      unrelated benchmark appended read them 15 to 28% over master's own binary, the
      counters showing byte-identical code retiring the same instructions, mispredicts and
      misses in more cycles); with 3.19 the same probe moves them 1 to 6%, so they need no
      rule of their own (docs/performance-changelog.md).
- [x] **3.19 The input walk's front-end cost.** Found and fixed 2026-09-15 while
      root-causing 3.2's perf rows (docs/performance-changelog.md). On MSVC 19.51
      `LevelStack::operator[]` was not inlined into `Script::GetInputsMetadata`, a call per
      container per level, twelve per uncached lookup at depth 1 and 22% of the loop's
      samples: the compiler had inlined the cold `Grow()` into the accessor, a function
      with one call site whatever its size, and then the accessor nowhere (19.44 had not;
      docs/compilers.md). `Grow()` is `TAS_FW_NOINLINE` and the accessor, 23
      instructions, inlines everywhere again. And the walk returned its 40-byte
      `InputsMetadata` by copying a local it assembled after the parent's answer, its
      several returns defeating MSVC's named return value optimization, 34% of the walk's
      samples on that copy: the recursion now writes into the caller's object
      (`GetInputsMetadata(frame, metadata)`), the by-value form being that object built in
      the return slot. Both behind unchanged interfaces, no behavior change, the engine
      tests unchanged and green on both compilers. MSVC 19.51 against master on the same
      toolset: the Script family 8 to 33% faster, every count identical, Tier D flat;
      against the 19.44 baseline the uncached `GetInputs` rows read +3.3%, -3.9% and
      -18.5% and `Execute_ChildOneFrame` -25.8%, with `AdvanceFrameWrite` and
      `AdvanceFrameRead` still +6 to +7% (the toolset's remainder). clang-cl 22: depth 16
      -18.3%, the rest within noise. The accessor's inlining was verified in MSVC's
      disassembly and clang's by measurement; GCC runs only in CI, whose Tier C gate is
      counts. The placement sensitivity went with the cause (3.18).
- [x] **3.20 Core file layout.** Done 2026-09-15 on the maintainer's decisions, no functional
      change, three commits so history survives: the multi-class headers of tasfw-core split
      into one header per concept (`Concepts.hpp` out of `SharedLib.hpp`; `M64.hpp` and `M64.cpp`
      out of `Inputs`; `SlotBudget.hpp`, `ResourceWork.hpp` with `get_time` and `SlotManager.hpp`
      out of `Resource.hpp`; `SlotHandle.hpp`, `ScriptMetadata.hpp`, `MetricScript.hpp`,
      `TopLevelScript.hpp` and `TopLevelScriptBuilder.hpp` out of `Script.hpp`, each with its
      `.t.hpp` where it has definitions), every declaration's text and relative order kept;
      then one member order for every class and the `.t.hpp` files in the header's order
      (AGENTS.md, "Conventions"), data members untouched so no layout moved; then the compare
      family's 32 entry points as `Script.compare.hpp`, a member include of `Script` (the
      maintainer's choice over a base class), since a constrained member template has to be
      defined in its class on MSVC. `<tasfw/Script.hpp>` stays a script's one include and
      pulls in the root and the builders at its bottom: `GetMetrics` reaches into
      `TopLevelScript`, so a script's translation unit needs both. Verified on MSVC 19.51,
      clang-cl 22 and GCC 14 (the container), tests unchanged, the suite flat
      (docs/performance-changelog.md). A declaration without a definition or a caller,
      `Script::AdvanceFrameRead(uint64_t&)`, went with it (the maintainer: meant for cost
      modelling once, never written). tasfw-scattershot got the same treatment the same
      day: `Configuration.hpp` with a `.t.hpp` for its member template, `Segment.hpp`,
      `Block.hpp`, `ScattershotSolution.hpp`, `ScattershotThread.hpp` over the existing
      `.t.hpp` and `ScattershotBuilder.hpp` for the three builders came out of
      `Scattershot.hpp`, which keeps the search, `HashByte` and the critical-region names and
      pulls in the thread and the builders at its bottom; `Scattershot` and `ScattershotThread`
      declare in the convention's order and their `.t.hpp` files follow; two commented-out
      blocks and two redundant forward declarations went.
- [ ] **3.21 The bare scattershot builder does not compile when used.** Found 2026-09-15
      writing 3.17. `ScattershotBuilder::ConfigureMetricScript` (ScattershotBuilder.hpp)
      calls `std::make_shared` without its template argument, and the same class's
      `PipeFrom` names a return type without the metric-script parameters it constructs
      with. Both are members of a class template no caller instantiates: every committed
      stage calls `ImportResourcePerThread` first, and the import and config builders
      override both members correctly, so the bare order has never been compiled and a
      stage written in the other order fails to build with an error deep in the header.
      Fix: the two forms the derived builders use, and a case in `test_scattershot_mock.cpp`
      that builds a search in the bare order. A bug fix behind an unchanged interface
      (hard rule 10 does not apply); the suite's delta table is still owed (rule 8), and
      docs/tasing.md drops its note on the order once it holds.

## Phase 4: the squish-cancel brute forcer

Goal: finish the thing the framework was built for.

- [x] **4.1 Re-enable the disabled stages.** Done 2026-09-08: `Pyramid.cpp` and `Surface.cpp`
      restored from `69792ad` into `tasfw-core/src/decomp` (plus a `free` for the surfaces
      `get_surfaces` mallocs per call), so the downhill-angle scripts link again;
      `dr-approach`, `dr-recover` (`phase`: `attempt-dr` / `c-up-trick`) and
      `pyramid-osc-approach` are stage types, and the dive-recover chain is in `config.json`
      after `osc-final`. Verified to link and run a few shots from a config; whether the
      chain finds anything is unknown (it never ran to completion in `main.cpp` either), so
      the stage descriptions say "unverified" until a real run says otherwise.
- [x] **4.2 Persist search state.** Assessed and closed 2026-09-19, not built. The ask was
      to serialize blocks and segments so a multi-hour run can be resumed; solutions already
      persist per stage (1.4), and that is the mechanism that stays: a stage's solutions
      become the next stage's root blocks, `--stage` reruns one stage from its input's file
      and `input` may name any earlier stage, so a search continues as a new stage entry of
      the same type with `input` set to the stopped one, and work is broken up judiciously
      with configurations (the maintainer, 2026-09-19). What that loses is the non-solution
      blocks. A checkpoint of the table would be small to write (a bin, a fitness and a
      segment per block, a seed and a script count per segment; the index rehashes) but
      tied to one binary and one stage entry, since a segment's seed decodes only by
      rerunning the identical `ApplyMovement`, so an edit to a stage script between save
      and resume turns every decoded block into a validation failure; it could not
      reproduce a deterministic run; and the `dr` stage's passes live in Stages.cpp, so a
      resume would carry the pass and its kept solutions too. The one run it would rescue
      is a stage that dies before its first solution, the tilt stages' shape
      (`maxSolutions` 1, up to a million shots, about ten shots a second at 200 pellets),
      whose fitness climb a restart loses; accepted. What an interruption loses today is
      everything, since solutions are written when the stage returns and nothing handles
      Ctrl+C: a cancellation that exports the solutions found so far is a Phase 5 item.
- [x] **4.3 Faster block decoding.** Assessed and closed 2026-09-19, not built. Each shot
      replays the base block's segment chain from the root (`DecodeBaseBlockDiffAndApply`),
      and the ask was a savestate cache per block, bounded by the memory budget. What there
      is to gain (docs/performance.md, "Known hotspots, measured"): 2.3% of the tilt-target
      throughput run, 8.4% of the `dr-oscillations` stage at 30,000 shots and 281,650
      blocks, growing with the log of the shot count; each oscillation pass of that stage
      restarts its table from the previous pass's solutions, so about 9% of the `dr` stage
      is the ceiling. What a bounded cache recovers, by `scripts/decode_cache_model.py`
      (the block tree under the search's own rule, a uniformly random base block and the
      root every 100th shot at the run's novelty rate, with a per-thread least-recently-used
      cache of decoded chain states that also keeps the ancestors a decode passes through;
      the ancestors many blocks share are the shallow ones, which are also the cheapest part
      of a chain): 64 states per thread (1.5 GB over 16 threads of 1.5 MB `fixed` states)
      save 35% of the replayed decode scripts on the profiled pass and 26% over a
      200,000-shot pass, 256 states (6 GB) 49% and 37%, 1,024 states (24 GB) 60% and 46%,
      and 52% at most on the Tier D tilt-target run whatever the size: about 4% of one
      stage for 6 GB, at 3 to 5 extra saves per shot. Three things stood between that and
      "add a cache". A `LibSm64Mem` is the bytes of its own DLL copy, the game's pointers
      included, and the copies sit at different base addresses, so a state cannot cross
      threads (docs/libsm64.md, "Why one copy per thread"). The DR metric script reads the
      previous frame's metrics (`BitfsDrMetrics.cpp`) and metrics are erased with the
      sandbox that recorded them, so a cached state arriving without its metrics makes the
      first `GetMetrics` on a hit walk back to the equilibrium frame, a load or a replay per
      step, on the one stage where the cache pays. And a savestate belongs to a level and is
      valid while that level's earlier inputs are unchanged (ARCHITECTURE.md, "Savestate
      ownership"), where a block cache is states from different input histories outliving
      their sandbox: `ExportSave` and `GetTotalDiff` cover the export, but the only import
      is the builder's at the start of a run, so the cache needs a new `Script` member that
      imports a state with its inputs and metrics into a running level (hard rule 10).
      Determinism was not the obstacle: decoding draws from the temp RNG seeded per segment
      and never from the thread's, and the deterministic queue takes one turn per decoded
      script, which a cache would still take, so shots, scripts, blocks and solutions would
      not change. The replay cost that does matter is item 9 of the hotspot list, the
      evaluation to the pyramid's equilibrium after every tilt-target script, 95% of that
      run's frame advances; fewer or cheaper evaluation frames (`PyramidUpdate` as the
      stand-in) is a stage-script change, not scheduled.
- [x] **4.4 Analysis.** Done 2026-09-19: the R script is `analysis/visualizer.py`, a live
      viewer rather than a plot at the end (the maintainer's ask, 2026-09-19: live updates
      at a rate set in a very lightweight app that starts itself when a run is configured
      for it, a tab per run so an earlier run stays in view, and no compute taken from the
      brute forcer). The search's side is a `Visualization` (`Visualization.hpp`: the
      viewer's path and interpreter, the plot's four columns, its bin sizes, range filters,
      and an optional fixed view, a window centered on a point that is never rescaled,
      which the maintainer asked for on 2026-09-19 so the BitFS stages show a 900-by-900
      square on the pyramid while other scripts choose their own) given to a builder's
      `Visualize()`: when the CSV opens the run writes it with the
      CSV's path to a JSON file beside the CSV and launches the viewer, detached, with that
      file, and at the end rewrites the file with `finished` and the row count; nothing on
      any hot path, nothing without a CSV. The pipeline takes a top-level `visualizer` (the
      script's path) and a per-stage `visualize` block, the `dr` stage titling a tab per
      pass. The viewer owns a localhost port; a later launch hands its file to the running
      viewer and exits, so a pipeline's runs are tabs of one window. It reads the CSV
      incrementally, folds rows into a newest-per-bin table (the R script's grouping, once
      per row, raw rows dropped), redraws only the selected tab and only when rows came in,
      at the refresh rate in the window, lowers its own priority, stops polling a finished
      tab and doubles a tab's bins past a segment cap; a tab's filter panel lists every CSV
      column in the manner of a shop's facets (the maintainer's ask, 2026-10-03: every
      column a min and max under the range seen, the columns the run names categorical as
      checkboxes with counts; it starts as the run's `filters`, and a change re-reads the
      CSV, the raw rows being gone). The categorical columns are declared, never inferred
      (the maintainer's rule, 2026-10-03): `Visualization::categorical`, a list of column
      names set in the `visualize` block, the maintainer's choice over a marker in the CSV
      header or a virtual beside `GetCsvLabels`, because the block is already where columns
      are named for the viewer (fine for now, the maintainer said, and open to a revisit
      once the panel has been used); `--once` renders a PNG headless (`--filter` a filter
      as text),
      which CI does on `tasfw-tests/data/scattershot_sample.csv`. Setup is none: with
      `python` on PATH (the unlock script's and the doc hook's requirement already) the
      viewer creates `analysis/.venv` and installs `requirements.txt` there on first start.
      The columns each stage emits are in README.md ("The viewer"). `test_visualization.cpp`
      pins the parameters file and the launch command, `test_pipeline.cpp` the config block.

- [x] **4.5 Base-block validation failures.** Done 2026-09-08. `ValidateBaseBlock` found
      state-bin mismatches on about 2% of shots (5 to 9 per 400-shot deterministic
      `tilt-target` run, single- and multi-threaded, lightweight and full saves, at different
      shots from run to run). Root cause, found by bisecting with the new
      `resources.costModel` switch (0 failures without automatic savestates, 8 with, same
      seed): `Script::Revert` moved every save of a reverted child into the parent's bank when
      none of them was synced, and a later backwards load from a level whose diff started
      after such a save restored a state made with reverted inputs (ARCHITECTURE.md,
      "Savestate ownership"). Dated 2022-06-18. Automatic savestates made it visible because
      they fill child banks; explicit saves alone rarely hit the pattern. Fixed in `Revert`
      and pinned by a mock-resource test. Verified on the three 400-shot deterministic runs
      (1 and 4 threads, lightweight and full saves): zero failures, at a wall-time cost
      recorded in docs/performance.md, since the old speed came partly from loading wrong
      saves instead of replaying. The Tier D deterministic run (1.3) does not exist yet;
      when it does, it asserts zero validation failures. Kept from the investigation: the
      failure counter and hex bins, the re-decode diagnostic that tells "decoding is not
      deterministic" from "the recording is wrong", `dllcheck --leak-scan`,
      `scripts/dll_symbols.py`, and the camera and controller coverage checks in
      `dllcheck --save-mode fixed`.

### After the makeover: the three squish-cancel goals

Once the BitFS squish-cancel brute forcer is running end to end again, the shorter-term
goals are (status as stated by the maintainer, 2026-09-08):

- [ ] **4.6 Enumerate every corner.** A brute forcer that enumerates the squish-cancel and
      fast-bully-battery possibilities on all 16 pyramid corners, not just the one the
      current pipeline targets. Progress exists in the current scripts and stages. Taken up
      2026-09-21 on the maintainer's word: the brute forcer is to target either pyramid
      (slots 84 and 83), any of its four corners and either edge of the corner (an
      unassisted squish cancel on the x or the z axis, `uscx` and `uscz`: 16 setups), and
      the first oscillation, flaky and inefficient since the scripts were written, should
      line up quickly for any of them. The first oscillation is the `dr` stage's first pass,
      from the fixer's rest to the tilt the oscillation runs at, and its second, the first
      crossing. Why it never lined up, measured from the fixer's rest with 300 deterministic
      shots on 16 threads (`--stage dr`): 0 solutions, 39,742 blocks, 246,444 scripts, and 0
      in the 50,000 shots the config allowed, the same from a rest in the target corner.
      Three causes, each seen in the game and confirmed in a float32 model. First, the
      pyramid keeps the adjusted remainder error only through frames whose goal (Mario's
      direction from the home) is a full 0.01 beyond the normal on both axes, so that
      `approach_by_increment` steps instead of snapping: from idle a stick sets Mario's
      speed to 8 at once, which leads both axes on the corner's diagonal only within about
      200 units of the home, and a random stick keeps the lead about one frame in three, so
      the initial phase's random inputs left the corridor within a few frames and the
      pellets died. Second, the tilt grows 0.02 a frame in |nX| + |nZ| and Mario cannot run
      slower than it (walking accelerates downhill and a released stick brakes only from
      16 speed) while the platform's low corner sinks into the lava as it tilts: at the 0.69
      the config called the regime, the equilibrium point is thirty units of height above
      the lava and every lineup arrived within twelve of it with no frame that keeps the
      pyramid stepping, since a lead is reversed only by moving the goal across the whole
      gap in one frame; at 0.6 there is room. Third, the search held the error exactly to
      the equilibrium frame's, and 0.01f added to the normal and taken away does not always
      round back: the error drifts by an ULP or two whenever the normal crosses a float
      binade (0.25, 0.5; from the old rest's -0.309 the x error goes from -57 to -55 at
      -0.509), so every path past that was rejected whatever the sticks. The check is
      intentional (the maintainer, 2026-09-21: the ARE is to be preserved throughout the
      oscillations, and the fixer is to reject the floats that rounding affects), and the
      model shows it can be met: of the float values a rest's normal can take, exactly half,
      every other ULP, step reversibly across the whole range the oscillations use on that
      axis, both binades included, and the fixer had landed on the other half on both
      axes (its x lattice reversible for 3 steps down, its z for 8). Done the same day:
      `Scattershot_BitfsDr` has a lineup move for the initial phase, `LEAD_TO_CORNER` (the
      corner's diagonal with a random deviation of up to 45 degrees, walked back until the
      game confirms both axes lead, a brake when the goal is two steps ahead, any stepping
      frame when nothing leads), and every frame any of its moves writes goes through
      `StepFrame`, the stick asked for or its nearest neighbor (16 HAU at a time to either
      side, then a brake) that keeps the pyramid stepping, so a pellet never spends frames
      on a path validation rejects; the fixer accepts a rest only when its normal steps
      reversibly over `minNormal` to `maxNormal` (0.13 to 0.61 in magnitude) on both axes
      and past the origin to `farNormal` (0.02) on the far side, or to the target when it
      lies there, since the final oscillation goes over to the adjacent corner along an
      edge (the maintainer's diagram, 2026-09-21: `uscz` along a z edge, the normal's x
      crossing the origin, `uscx` along an x edge, z crossing; the committed setup
      oscillates in corner 4 and runs toward corner 1 along the +z edge, its target's x
      still on the oscillation's side) (`BitFsAreFixer::StepsReversibly`, the game's
      arithmetic walked a step at a time each way with every step undone and the error
      recomputed, the oscillation's side being the rest's own; `bitfs-turn --test` checks
      the config's rest): in the model every value reversible on the oscillation's side
      crosses the origin intact, and the far side's own binades set the limit, +0.24 for x
      and -0.02 for z with the config's targets, which a setup whose target lies past the
      origin runs into (its own targets align the lattice differently, and the fixer's
      check says whether any rest serves); and the search's exact conservation stays as it
      was; the initial
      phase's exit and the oscillation's floor are `startXzSum`
      (0.6) and the last pass's solutions must reach `minXzSum` (0.69); the exit, the
      search's box and the direction the stage commits to follow the configured
      `quadrant`, and `platform` names the pyramid's slot, checked against
      `BitFsObjects.hpp` (the metric script and the search read it; the other stages still
      read slot 84). The fixer's rest is asked for on the corner's diagonal, (-2045, -615),
      and lands 117 units from the home in x and 146 in z (tilt 0.49): the rollout reaches
      about 120 units in -x and all four corners near the home (measured rests at
      (-112, 152), (-120, -190), (193, 177) and (197, -212)), so the four corners of slot 84
      are within this movie's reach and slot 83 needs its own way in. Measured from that
      rest, deterministic, 16 threads: the first pass finds 100 lineups in 16 shots (0.4 s,
      67,186 frame advances) at 0.69 and in 186 at 0.6; at 0.69 and at 0.65 the second pass
      finds nothing in 3,000 shots (the lava); at 0.6 it finds 4 first oscillations in
      3,000 shots, then 100 second ones in 267, 100 third in 880 and 100 fourth in 842, the
      tilt growing over the swings (0.61 to 0.73 at the second oscillation, median 0.67,
      the increment parity kept throughout), and the last pass, the fifth oscillation with
      the regime's tilt required, 12 in 3,000 shots (113,763 blocks), every one at 0.693
      with 19.5 to 30.9 speed and the parity kept; the stage end to end 345 s, 14.0 million
      frame advances, 613 thousand saves and 4.65 million loads on 16 threads; those runs
      held the error to a neighborhood while the drift was still taken for the game's
      doing. With the exact check on the reversible rest, the same config at 3,000 shots a
      pass: seed 7 runs end to end, the lineup 100 in 115 shots, the first crossing 6 in
      3,000, then 100 in 143, 315 and 996 shots and the last pass 5 in 3,000, 335 s; seed
      6 lines up in 189 shots, crosses 4 times in 3,000, finds 100 second oscillations in
      240 shots and none of the third in 3,000 (its ten roots had crossed at the x edge's
      middle rather than the chord's end); seed 8 lines up in 52 shots and finds no first
      crossing, its hundred lineups all the same seven-frame straight run with no
      continuation. Every normal in every block of those runs lies on the rest's lattice.
      Handing over at 0.55 or 0.5 instead is worse (no first crossing from any of the three
      seeds at 0.55; at 0.5 the lineups themselves are rare). The unchanged search from the
      same rest at 0.6: 0 in 300 shots. The lineup works from every corner once the rest is
      within about 190 units of the home (the first frame from idle leads both axes only
      there; at 196 it does not, and a rest at tilt 0.6 or more leaves the initial phase at
      once with nothing to lead), and the fixer's rollout from the movie's dive reaches such
      a rest in each corner with the right request and slide frames (measured 2026-09-21,
      the target's magnitudes with the corner's signs; rests relative to the home):
      corner 1 asked at (80, 80) with 4 slide frames rests at (95, 145), the lineup 100 in
      16 shots and the first crossing 31 in 3,000; corner 2 asked at (90, -90) with 2 rests
      at (142, -97), the lineup 100 in 16, no first crossing in 3,000; corner 3 asked at
      (-60, -60) with 4 rests at (-116, -123), the lineup 100 in 16, no first crossing;
      corner 4 as configured. A request of 30 to 60 units or slide frames the rollout
      cannot afford leave the fixer unsolved, and corners 1 and 2 need the slide frames,
      the dive's momentum carrying the rest past 190 otherwise.
      The fixer from the new rest solves in one round (26,644 frame advances in `--test`
      then, against 51,090 from the old one; 520,993 with the dive's yaw candidates, each
      failing candidate costing its rounds; 29,415 now, with the movie's dive and the
      distance candidates in place), on a reversible normal. `bitfs-turn --test` checks the
      first pass from the config's rest on one thread. Open: the first crossing is the
      bottleneck now (3 to 6 in 3,000 shots from two seeds, none from a third: the lineups
      the first pass hands over are at the same tilt by construction and mostly straight
      runs facing the corner, while the path that crosses runs on along the chord toward the
      far target, a 90-degree turn at 11 degrees a frame with the lava a few frames below,
      so only the bent lineups make it; searching the first oscillation straight from the
      rest is worse, the shots going to the lineup's early blocks whose pellets die, and so
      is turning uphill when the lava is near, which shrinks both margins and snaps). What
      the crossing needs, measured in the game from the config's rest (a straight run of k
      frames along the corner's diagonal, then the stick held toward the far target or
      past it): only k = 6 crosses, at margins of 0.036 and 0.042, 17 speed and 59 units
      above the lava, on the turn's seventh frame; k of 5 or less snaps (the lead too small
      for the turn's creeping increments) and k of 7 or more ends in the lava; and the turn
      has to be sharper than the far target's yaw, 22 or 45 degrees past it, the stick at
      the target itself snapping even at k = 6. The search's run-downhill phase offered only
      the minimum downhill angle toward the target, the soft turn, and offering the move
      that turns harder at random there as well only moved the luck between seeds (2, 0 and
      0 first crossings for seeds 6, 7 and 8 against 4, 6 and 0), and so did a move that
      looked for the crossing itself, plans of a few frames straight then the stick past
      the far target played ahead in a reverted sandbox until the metric recorded a
      crossing (1, 0 and 0 offered in the run-downhill phases; 0, 0 and 0 offered near the
      handover too, and the lineups slower, 1,565, 171 and 3,000 shots), so that move is
      gone. What the blocks of a first-crossing pass show instead (seed 6, every block
      exported): the swing runs its outbound leg turning uphill for ten to twenty frames to
      the x edge's middle, turns around there (speed 16 to 17, about (-300, -120) from the
      home) and dies on the return run at the frames the margins reach zero (9,108 of
      13,522 blocks in the pre-crossing phase, 8,091 of them finishing the turnaround near
      x -325), because with the tilt about (-0.5, 0.2) there the minimum downhill angle
      toward the far target is only 19 degrees from +z and the goal's motion on the
      reversing axis, x, is 0.003 to 0.006 a frame at the speed the return has built (15),
      short of the 0.01 a full step over the band needs; the eleven blocks that crossed did
      so at 21 speed with the goal moving 0.010 to 0.011. The crossing toward the other
      target, from the +x end with the tilt symmetric and the run at the chord's 45
      degrees, is the easy one (100 in 240 shots), and the same hard crossing recurs in
      the regime (seed 6's fourth pass, the same shape and 0 in 3,000; seed 7's found 100
      in 315). So the run-downhill move's deviation from the minimum angle now goes either
      way, toward the floor's downhill as before or, once the turnaround is done, toward
      the target and no further than it (the first crossing 30 and 15 in 3,000 shots for seeds
      6 and 7 against 4 and 6, the return runs living longer: 87 and 112 thousand
      move-frames in the pre-crossing phase against 58 and 102 thousand; seed 8 still 0,
      its pellets all dying before any return run). The other half of the loss is
      the frames right after the handover: from the lineups' handovers (Mario running into
      the low corner at 18 speed, 47 units above the lava, margins 0.02 to 0.07 the
      corner's way) every scripted path, replayed frame by frame from six lineups, ends
      within seven frames, in the lava (the corner sinks about five units a frame from the
      tilt and eight from Mario's own run into it, so four frames) or on a step short (the
      axis that has to reverse can reverse only from a margin the walking turn, 11 degrees
      a frame, cannot reach in time), and the search's survivors are the lucky stick
      variants (seed 6's first pass: 15 thousand of 24 thousand move-frames in the
      run-downhill phase died, two thirds on a step short and a third in the lava; seed
      8's: all of them). A random stick offered in the run-downhill and uphill-turn
      phases for that variety made it worse (seed 6's first crossing 0, seed 7's 30, seed
      8's 0: the random sticks are mostly analog-back, and a turnaround in those phases
      is rejected as untimely, 2,900 such deaths for seed 6), so it is not offered; and
      the spread between seeds (0 to 31 first crossings in 3,000 shots) makes three seeds
      too few to trust, so the deviation was measured against the unchanged search on
      seven more (seeds 1 to 5, 9 and 10): 37, 34, 53, 25, 10, 50 and 57 first crossings
      in 3,000 shots against 5, 4, 3, 3, 2, 4 and 3, about ten times as many. What the probe says the handover needs, a heading already along an
      edge (the replayed survivor left the handover at -73 degrees, 17 from the -x edge,
      with a z margin of 0.034, and reversed z on its third frame), the lineup can only
      hand over by chance while its deviation from the diagonal is capped at 45 degrees,
      and widening the cap to 90 degrees is worse (the lineups 100 in 1,736 and 750 shots
      for seeds 1 and 2 against 189 and 115, and no first crossing from either, 257 and
      260 blocks: the walk back toward the diagonal settles on the widest heading that
      still leads, so those lineups hand over with one margin at its minimum), so the cap
      stays at 45. The probes those measurements used, a swing test case (the scripted
      paths from a lineup, frame by frame) and pellet-death counters in the search (a
      table per pass, by phase and cause), were scratch and are gone; the block census is
      the exported CSV (`csvSamplePeriod` 1) read by hand. More roots are not the lever
      either: with `maxSolutions` 300 the lineup pass collects 181, 131 and 126 roots in
      its 3,000 shots for seeds 8, 5 and 4 and the first crossing is 0, 24 and 24 against
      0, 10 and 25 from 100 roots. Open: the handover itself, the lineups' roots deciding
      the first crossing by their exact margins (seed 8 none in 3,000 shots with either
      search; every deterministic path from a handover dies within seven frames). Two
      designs measured and undone: the lineup ending at the swing's first step (the
      metric leaving its initial phase at the first reversal) with the uphill turn
      offered beside the lead (seed 6: 0 lineups in 3,000 shots, 40,808 blocks), and the
      same with a move that aimed the jump (`Reverse`: the first frame's stick chosen so
      the reversing axis's lead lands just over 0.01, the second the stick at the chord's
      far end, both tried through the game; 0 lineups on seeds 6, 7 and 8), because a
      probe of that move's candidates from the handovers shows the jump is not about the
      aim: the lead shrinks by at most 0.017 a frame, the jump needs Mario's motion on the
      axis at 0.0101 in one frame, a heading past the corner's edge at 20 speed, and from
      the handover's heading (the diagonal, give or take 45 degrees) the walking turn
      cannot get there before the lava (47, 35, 23, 11 units above it, then off). Only a
      handover already heading near the edge (the replayed survivor: -73 degrees, 17 from
      the -x edge) with a lead of about 0.035 on the reversing axis reverses in time. So
      the lead move now builds both leads to `SwingLead` (0.035) first (its deviation
      within 45 degrees of the diagonal, where both grow, and no brake) and then lets the
      deviation reach the edges, the walk back toward the diagonal stopping at the widest
      heading that keeps both leads at that size. Measured, that is worse too: the lineups
      slower (69, 71 and 68 in 3,000 shots for seeds 6, 7 and 8 against 100 in 189, 115
      and 52) and no first crossing from any (234, 249 and 222 blocks); so the handover's
      requirements are being measured directly instead, a probe trying every stick
      sequence of up to four frames from each lineup's handover and reporting which roots
      can reverse an axis at all, with their heading, leads, speed and height: 5 of the
      100 lineups from the 45-degree walk, 3 of the 69 from the `SwingLead` one, every
      viable root heading within about 25 degrees of an edge (-67 or -73 degrees for the
      z reversal, -22 or -28 for the x one), its lead on the reversing axis 0.034 to 0.042,
      at full speed (18.4; every braked root, 13.7, is dead), the reversal landing 2 to 13
      units above the lava after four frames of turning at the walking rate (the jump
      itself a lattice coincidence the probe's 6,561 sequences hit for those roots only).
      The 45-degree walk without the brake and with half its draws from the outer half of
      the range is worse still (lineups 71 to 94 in 3,000 shots and no first crossing on
      any of seven seeds): the viable roots turned toward the edge on their last frame
      only, and draws biased toward the edges spend the reversing axis's lead before the
      handover. So the lead move is unchanged from its first form (the brake, uniform
      draws within 45 degrees), and the first crossing stays a lottery on the roots that
      the return-run deviation makes ten times more productive. What would change it is
      out of this search's moves: a handover that is not a run into the sinking corner
      (the tilt built some other way, or from a rest the fixer places higher on the
      platform). The maintainer's go (2026-09-21) to adjust the fixer's handover for the
      oscillation's sake; the candidate being measured: the oscillation starting at the
      rest's own tilt (0.49) with its first leg run toward the chord's far end from the
      rest, where both leads are zero and so any stick takes both axes a full step at
      once, no corner run and no sinking, the tilt built by the swings' ends as the regime
      already does. Measured so far: the first crossing 109, 75 and 104 in 3,000 shots for seeds 6, 7
      and 8 (the pass's cap is 100) against 30, 15 and 0 from the corner lineup, but those
      crossings are not the regime's swing: 60 frames after the rest at (-283, +317) with
      the tilt at (+0.06, +0.49), the normal having wandered across x = 0 to the +z edge
      (the flatter platform lets the search roam where the sinking corner used to force
      the chord swing), and nothing continues from them (the second oscillation 0 in
      3,000 shots from 11 to 15 blocks). With the initial phase exiting on the first frame,
      though, the run-downhill phase's turning moves spend the one-step leads within two
      frames, and a floor on the tilt (each axis 0.13 or more into the corner's quadrant,
      the fixer's reversibility range) alone leaves nothing (24 blocks). So the design as
      built: the initial phase is the first leg itself, `FirstLeg_1f` running the chord's
      direction (the end on the far side of the corner on the tilt's steeper axis, away
      from the lower edge; a random deviation of up to 45 degrees walked back until both
      axes step) until Mario can turn around (16 speed, the turnaround moves' threshold),
      and only then the regime's phases (the maintainer's third point, 2026-09-22, that
      the handoff should be above 0.6, is met by decoupling the leg's handover from the
      tilt: the leg hands over on speed alone, and `startXzSum` is the regime's floor
      only, the tilt from which the oscillation may not fall back, `minXzSum` by default).
      Measured, a floor of 0.65 or 0.69 kills the regime: the first oscillation 0 to 16 in
      3,000 shots and the second nothing from 1 to 10 blocks on seeds 6, 7 and 8, since the
      swings' tilt moves about 0.05 either side of its mean and a floor inside that band
      rejects every swing once the mean has touched it (the old design's 0.6 sat below its
      regime's 0.61 to 0.73). At 0.6 the regime runs (seeds 7 and 8 ten oscillations,
      317 and 25 last-pass solutions in 209 and 411 s) but the first oscillation pays
      (26 and 36 in 3,000 shots against 140 and 118 with no floor, seed 6 none either
      way), so 0.6 is the committed floor, the maintainer's wish and the highest the swings
      allow; at 0.55 all three seeds run ten oscillations (the first 100 in 2,456 and
      2,401 shots and 95 in 3,000; 539, 882 and 21 last-pass solutions in 398, 157 and
      107 s), the alternative if the first oscillation's shots matter more than the floor. And `normalSpecs.minAxis` keeps each axis of the normal that far into the
      quadrant. Measured: the leg hands over 100
      roots in 16 shots (seven frames, 17 speed, the tilt (-0.30, 0.19)), but with the
      floor at 0.13 the first oscillation dies at frame 3340 (804 blocks): the leg toward
      A steps the z normal down from the rest's 0.274, and 0.13 leaves 14 frames, fewer
      than the swing's outbound leg needs; the fixer's lattice is reversible to 0.02 on
      the far side, so the floor is 0.05. With it (ten oscillations asked, 3,000 shots a pass):
      the tilt grows fast, 0.49 at the rest to 0.59 at the first crossing and 0.69 at the
      second (seed 6), but seed 6 stops at the third oscillation (0 in 3,000 shots, 101,988
      blocks, Mario turning around at the -x end with the tilt (-0.43, 0.19)), seed 8 at
      the fifth (135,663 blocks) and seed 7 at the first (5,615 blocks). The later stops
      sit where a crossing must beat the speed of the crossing two before it
      (`ValidateCrossingData`): from the rest the first swing is long and fast (the
      second crossing at 24), so the fourth, the slow return from the -x end (8 speed
      after the turnaround, a step and a half a frame), cannot; measured with that
      ratchet off (a scratch switch, gone again): seed 6 runs the whole stage, ten
      oscillations in 129 s, the first and third (the returns from the -x end) 42 and 20
      in 3,000 shots and every other pass 100 in 23 to 495, and the last pass 523
      solutions at the regime's 0.69 in 3,000 shots; seed 7 likewise, 117 s, its first
      oscillation 4 in 3,000 shots and its third 100 in 1,829, the last pass 327; seed 8
      likewise, 121 s, its first oscillation 68 in 3,000, the last pass 545. With the rule
      as it stood the same three seeds stopped at the third, first and fifth oscillation.
      The maintainer's decision (2026-09-21): keep the rule's downhill half only, the run
      from a crossing must still beat the peak speed reached from the crossing two before
      it, the crossings' own speeds no longer compared. Measured, that half alone stops all
      three seeds at the third oscillation too (0 in 3,000 shots, 104,559, 102,045 and
      137,362 blocks; the first oscillation 70, 28 and 100 in 3,000 or fewer): the fourth
      crossing, the slow return from the -x end, cannot beat the peak reached from the
      second, about 30 from the long first swing. The maintainer's word on it (2026-09-21): the point is to
      improve every time, and it may be unimprovable past a few oscillations with these
      parameters. Then his clarification (2026-09-22): the speed gates were meant for
      crossings at the same normal; while the normal still grows toward the target sum the
      crossings are not comparable. So the rule is its original form again, both
      comparisons, applied only when the crossing's tilt is not above the tilt two
      crossings before. With it seeds 7 and 8 run ten oscillations (701 and 80 last-pass
      solutions at 3,000 shots a pass; 317 and 25 with the 0.6 floor), so the config asks
      for five again, the last pass a crossing toward the far end. From the rest the first leg always
      runs toward the far end, so the first crossing is toward it and the stage adds one
      oscillation (the direction commitment), the last pass landing on an odd oscillation,
      a crossing toward the far end as `osc-final` needs; the first such with the regime's
      tilt is the third (the second's tilt is 0.57 to 0.59), and the third is the slow one
      (the fourth crossing beating the second's peak): with `maxOscillations` 3 and 3,000
      shots a pass, the last pass finds 0, 1 and 60 solutions at 0.69 for seeds 6, 7 and
      8 (the second oscillation 100 in 184 to 265 shots before it). The committed config
      asks for 3 with its 30,000-shot last pass: on seed 6 the first oscillation 100 in
      17,736 shots, the second 100 in 331, and the last pass 3,365 solutions at 0.69 by
      24,000 shots (the run ended there without its summary, at the moment a debug
      instance of the executable started on the machine; not rerun). The first
      oscillation is the slow pass now (4 to 100 in 3,000 shots across seeds, 17,736 shots
      to 100 on seed 6), the return from the -x end with the tilt at (-0.48, 0.11) after
      the long first leg; the fixer's rest request looked like the lever (a rest with less x tilt and more
      z leaves the -x end less lopsided) but is not: of four requests only one other
      solves (rest (-2035, -553), normal (-0.17, 0.30)), and over seven seeds it gives 0,
      0, 0, 0, 266, 0 and 223 first oscillations in 3,000 shots against the committed
      rest's 26, 0, 152, 0, 226, 140 and 118; the first oscillation is a lottery over the
      search's stream, 0 to 226 in 3,000 shots, and the committed request stays. The
      maintainer's fourth point (2026-09-23): limit the fixer's rests so the oscillation
      starts near its regime, the same corner and a tilt not much below the target's. So
      the fixer accepts only a rest whose normal is in the corner it is asked for (`quadrant`,
      the oscillation's; the target's own when absent, since the maintainer wants the
      target's error matched from the oscillation's corner whether or not the target lies
      there, 2026-09-23) and whose tilt is at least its `minXzSum` (0.6 in the config; from idle a stick gives 8 speed and
      the first frame must step both axes, which holds within about 265 units of the home,
      a tilt of about 0.63 on the diagonal at most), and the config's request moved out to
      (-2140, -520) with 3 slide frames, which rests at (-146, 210) from the home, normal
      (-0.26, 0.37), tilt 0.633 (the requests at (-2122, -538) and (-2100, -560) the fixer
      cannot solve); From that rest the leg hands over (88 to 100 roots in 735 to 1,000 shots,
      the first frame's steps marginal at that distance) but no first oscillation comes on
      seeds 6, 7 and 8 at either floor (2,800 to 3,800 blocks): the rest is z-heavy, and
      from there the first stick toward the chord's A end moves the z goal only 0.0097
      (running toward A shortens the distance to the home, which takes from the step), so
      the leg fell back to the B end and the first crossing, from the +z edge, never came.
      Six requests near the diagonal with two to four slide frames give ten rests, every
      one z-heavy (z 205 to 231 from the home against x 146 to 183, the rollout's momentum
      carrying them that way, tilt 0.63 to 0.69) and every leg from them runs toward B:
      at tilt 0.6 the rest sits where a first frame from idle can barely step both axes at
      all (5.6 units each needed, 5.66 available on the diagonal), so the distance term
      decides the direction. Twelve x-ward requests at a floor of 0.55 give two rests on the diagonal, from
      (-2140, -570) and (-2150, -560) with two slide frames: (-153, 161) and (-159, 167)
      from the home, normals (-0.28, 0.29) and (-0.29, 0.30), tilts 0.573 and 0.593, the
      leg toward A from both (96 and 80 of the handovers), and the first oscillation 616
      and 519 in 1,500 shots against 0 to 226 in 3,000 from the old rest; every other
      solved rest is z-heavy again and gives nothing. The config's request is the second
      (the fixer's floor 0.55). From it, ten oscillations at 3,000 shots a pass: at the
      0.6 floor seeds 6 and 8 run through (the first oscillation 19 in 3,000 and 100 in
      937 shots, the last pass 353 and 638 solutions in 105 and 149 s) and seed 7 stops
      at the second, because its first pass's fastest solutions were wandering paths
      whose tilt had fallen to 0.35 (the floor engages only once the tilt has exceeded
      `startXzSum`, and a rest at 0.593 never touched 0.6 on them); at 0.55 seeds 6 and 7
      run through (523 and 177) and seed 8 finds no first oscillation. So the tilt is
      held from the first frame at a swing's amplitude (0.05) below the rest's own as
      well, and with that every run completes: at the 0.6 floor the first oscillation 100
      in 2,335 shots, 3 in 3,000 and 100 in 2,568 for seeds 6, 7 and 8, every later pass
      100 in 42 to 1,713, and the last pass 574, 1,017 and 407 solutions at the regime's
      tilt in 95, 139 and 122 s; at 0.55 the first oscillation 6, 100 and 16 and the last
      pass 103, 940 and 27. The committed floor is 0.6. The first oscillation is still the
      slow pass, 3 to 100 in 3,000 shots (the config gives it 30,000). The maintainer's
      next two points (2026-09-23): the dive itself can be redirected (the fixer takes the
      movie's dive as given, so the slide and the rollout inherit its line, and every rest
      it reaches falls along that line: against the requests the rests fell short in x by
      up to 46 and over in z by 12 to 54, more with more slide frames), and the rest's
      tilt should be no less than 0.02 below the target normal's own sum. So the fixer
      can start on the run before the dive (frame 3255, three run frames before the B
      press at 3258; every later stage starts where it does), steering those frames to a
      dive yaw, the movie's or up to `diveYawSteps` steps of 1024 to either side, and runs the
      landing search for each, and `minXzSum` is stated in the config as the target's sum less 0.02
      (0.553), an explicit parameter of the fixer by the maintainer's word, never derived. The dive's air frames keep the stick straight back as the approach always
      did: held at the dive's yaw the dive at 46 speed flies some 500 units and lands near
      (-90, -90) from the home, beyond the rollout's reach of any rest asked for. The redirected dive reaches rests from z-heavy to x-heavy (fifteen requests: (-158,
      155) to (-205, 124) from the home, tilts 0.55 to 0.71), and one of them is far
      better than any before: (-163, 138), normal (-0.30, 0.25), tilt 0.553, from the
      request (-2150, -560) with no slide frames, whose steeper axis is x, so its leg runs
      toward the B end and its first crossing is the easy one from the +x side: 1,331
      first oscillations in 1,500 shots on seed 6 (the z-heavy rests 0, the x-heavy ones
      further out 0 too, their leg's first step short on x the way the z-heavy ones' was
      on z). The whole stage from it, ten oscillations at 3,000 shots a pass with the 0.6
      floor: seeds 6 and 7 run through (the first oscillation 100 in 1,335 shots and 37
      in 3,000, the last pass 241 and 259 solutions in 135 and 114 s), seed 8 stops at
      the fourth (its third found 2). Against the diagonal rest's 574, 1,017 and 407 on
      all three seeds, the config kept the diagonal rest and the movie's dive (start
      frame 3269), the redirected dive an option of the fixer (`runFrames`,
      `diveYawSteps`) with its rests recorded here. The maintainer's word (2026-09-23):
      the dive is never a limiter; edit the approach from an earlier start for a better
      dive. So the fixer also chooses the dive's distance (`diveAirCandidates`: the air
      stick back, neutral or at the yaw, about 280, 340 and 500 units) and can start
      further back on the run for more turn. From frame 3250 with six or eight run frames
      the fixer rests at tilts 0.63 to 0.75 (thirty-six requests, twenty-one solved: 0.653
      on the diagonal at (-186, 182), 0.673 x-heavy at (-204, 178), (-210, 173) and (-228,
      156), 0.693 near the diagonal at (-195, 203), z-heavy from 0.693 to 0.753; ten run
      frames overshoot the ledge), so the dive limits nothing now. Then the maintainer
      (2026-09-23): the movement is the fixer's to work out, not the config's to spell out.
      So the knobs are gone (`runFrames`, `diveYawSteps`, `diveAirCandidates`,
      `slideFrames`; `restX`/`restZ` remain as an override): the fixer asks for the
      corner's diagonal at the radius whose resting tilt is the floor plus 0.02, plays every
      way onto the platform once with a straight rollout to its rest (`Ways`: run lengths
      to 12, the movie's dive yaw and up to four steps of 1024 either side, the three air
      sticks, 0 to 3 slide frames; from a dive slide the slide frames alone), orders the
      ways that rest on the platform by that rest's distance to the one asked for, and
      runs the landing search on each in turn, aiming the landing where the way's own
      rest-to-landing offset says and then bringing the rest to the one asked for through
      its measured response to the landing (`Response`, the ARE rounds' own), up to three
      shifts; the ways whose rest came within 20 units were taken first and any solving way
      after that (the maintainer, 2026-09-23: a rest as near the diagonal as the ways
      allow; since 2026-10-05 the positions that hold the error are tried nearest the rest
      asked for, each with the three ways resting nearest it, 4.8). The config starts every stage at
      3250 and asks for `quadrant` 4 at `minXzSum` 0.67 (the maintainer's floor). What limits is the
      first oscillation, and the numbers say why. The first frame from idle: a stick sets
      Mario's speed to 8, the walking frame adds 1.1 less speed/43, and the slope adds 1.7
      times its steepness when he faces within a quarter turn of downhill and takes it
      away otherwise; the ground step moves floor-normal-y times that, 8.5 units downhill
      of the boundary and 7.0 uphill of it at tilt 0.69. A full step on both axes along
      the diagonal's chord needs 0.01 of the distance to the point 500 below the home
      times root two, 8.1 units at tilt 0.69, so only the downhill side of the boundary
      steps, and only for a few degrees past it before the axis stepping toward the corner
      falls short (`scratchpad/first_frame_window.py`, the model the numbers below are
      from: about seven degrees at 0.59, five at 0.65, three and a half at 0.69, two at
      0.73). Off the diagonal the chord toward the end that relaxes the steeper axis lies
      on the uphill side by the tilt's angle from the diagonal, so that end has a first
      frame only while the angle is inside the window (the committed rest, 1.5 degrees
      off, does; a z-heavy rest at 0.69 has no first frame toward the -z end and only the
      +z end, which sinks the +z edge). A stick chosen from that (the downhill boundary
      on the end's side turned a random one to eight steps of 256 toward the corner and
      walked back to one, the rule's end first and the other when idle and the rule's has
      no first frame, the end Mario faces once walking) was tried and is worse: first
      oscillations per 3,000 shots on seeds 6 to 12, the chord's deviations against it,
      14/100, 100/0, 2/0, 100/0, 100/0, 100/3 and 100/100 (the leg toward the boundary
      curves with the tilt into the far end and the roots lose their variety), so the
      chord's deviations stay, and the model says why they work: 5.6 degrees toward the
      corner lands inside the window from a rest near the diagonal. From every rest at
      0.65 or more the first oscillation stays at 0 with either stick: 0 in
      3,000 shots for eight rests, 0 in 10,000 for the 0.653 diagonal and the 0.693
      near-diagonal ones, with the rest's tilt floor loosened to 0.15 below the rest as
      well (the pellets die at the floor, 0.04 below the rest, well above the lava; with
      the floor loosened they use 0.08 and still find nothing, while the committed rest
      drops from 100 to 0, so the floor stays). The cause is the lava: a crossing has
      Mario at the point whose goal is the normal, and the platform there is below the
      lava once the normal's steepness (the root of the squares) passes about 0.53 (the
      surface at his feet is 2,880 below the home less the normal's dot with his position
      over its y, the lava 191 below that; the crossings seen lie at 0.44 to 0.55, most
      under 0.52). The first leg moves the normal along the chord, whose steepness grows
      with every frame from the diagonal, so from a rest of sum S on it the first crossing
      must come within the square root of (0.55 squared less S squared over two) over
      0.0002 frames: 27 at 0.593, 22 at 0.653, 18 at 0.693 (at 0.52: 23, 17 and 12), and
      the fastest first oscillation the search finds takes 15 frames from the rest, the
      usual 20 to 25. A death census (2026-09-24, a scratch build counting every rejection
      and every move that ends a pellet, from the 0.673 rest and the 0.573 one, floors
      0.6, 3,000 shots) puts the lava second-hand at most: no pellet dies on the lava
      check; from 0.673 the deaths are 19,600 turn-uphill moves and 12,400 return-run
      moves that find no stick keeping both leads (the goal a full step past the normal on
      both axes), 4,900 at the rest's tilt floor and 750 at the regime's, and they come
      within 20 frames of the rest; from 0.573 the same moves fail (12,800 and 21,500) but
      the pellets live to 30 and 99 frames and 6 cross. At the failing frame the x lead is
      0.1 or more and the z lead under 0.02 in 24,000 of the 0.673 deaths (16,000 of the
      0.573 ones): the swing runs the lead of the axis it steps inward far ahead while the
      outward axis's runs out, and the reversal needs both to cross the band in one frame,
      so the leads at the turnaround, not the tilt itself, are what the higher rest
      shortens the time to build; the maintainer recalls much older versions oscillating
      from starts above 0.7, which is consistent if those starts carried leads (the old
      lineup arrived at speed with 0.03 to 0.04 on both axes) or did not hold the error
      exactly through the first swing (the maintainer: they were exact). So the leads are the
      difference, and the geometry says why a rest further out cannot build them: the goal
      is Mario's position over his distance to the point 500 below the home, so the axis
      the swing steps outward gains less goal per unit of his motion the further out he is
      (0.00118 per unit at 324 out against 0.00165 at 189), and at speed 20 along the chord
      that lead grows 0.004 a frame against 0.01 nearer in, while the inward axis's lead
      runs away (per-phase medians of the first-oscillation pass: from the 0.573 rest the
      swing reaches the -x end with leads -0.085 on x and -0.18 on z and crosses coming
      back; from x-heavy rests at 0.653 and 0.673 it reaches it with -0.018 on x and -0.15
      to -0.21 on z and dies, and from z-heavy rests at 0.653 and 0.673 the only steppable
      first frame points at the +z end, the swing runs across to +x and dies with +0.24 on
      x and +0.02 on z; 0 first oscillations in 3,000 shots for all four). Reweighting the
      turn-uphill phase (run forward 7 or 10 of 10, or a straight frame) changes none of
      them (the 0.573 rest: 6, 26, 4 and 1 against 6). The old lineup ran outward from a
      low rest where the goal answers strongly and arrived with 0.03 to 0.04 on both axes.
      So the first swing is now steered by the leads (`LeadRun_1f`, the maintainer's go,
      2026-09-24): in the run-downhill and turn-uphill phases before the first crossing, the
      frame's stick is the one of a fan around the face yaw (two steps of 2048 either side)
      that leaves the thinner lead largest among those keeping both axes stepping, the
      second best one time in three for variety, each candidate played in a block that
      reverts and the chosen one for keeps; the uphill turn stays as the frame that moves
      the phase on and the turnaround is the search's draw. From the 0.573 rest the first
      oscillation goes from 6 to 100 in 1,031 shots on seed 6 (seven seeds in the
      changelog). At 0.673 it stays at 0 from either side of the diagonal: the outward lead
      reaches 0.013 at the turnaround because the swing meets the platform's edge some 130
      units out, seven frames at speed 20, which buy 0.03 of lead that the turnaround eats;
      a rest that far out has no room to build the lead its return needs, and the old high
      starts crossed by the single-axis reversal needle catalogued above, not by a swing.
      The maintainer's next thought (2026-09-25), a swing that reverses by walking at low
      speed without the turnaround action: the lead run's fan was widened to a quarter turn
      either side (kept: 100 first oscillations in 677 shots against 1,031 on seed 6 from
      the 0.573 rest) and the first swing freed of the turn-uphill phase's 16-speed floor
      (kept), and a return move was tried that walks back toward the other end at any
      speed, each frame the stick leaving the least outward lead on both axes among those
      keeping both stepping, the search drawing when to start it once both leads reach
      0.02, with a crossing starting the next swing in the phase machine. It found nothing
      from the 0.673 rests and cost the 0.573 rest most of its first oscillations (9 in
      3,000; the few crossings still came through the turnaround), so it is out again. The
      reason a walking reversal is hard is the band: an axis whose lead is L must, each
      frame, either keep stepping out (its goal moving at least 0.02 - L that frame) or
      jump back past the band (at least L the other way); a walking turn crosses the zone
      between too slowly unless L is within a frame's motion of 0.01 at that very frame.
      The lead arithmetic, which is why the 0.57 to 0.6 rests cross and 0.67 does not: a
      swing's return consumes an axis's lead by its goal motion plus 0.01 a frame, and the
      axis reverses on the frame its lead sits in [0.01, that motion], so at a return speed
      whose goal motion is 0.025 the chance per axis is about 0.6 and both axes cross within
      a few frames when the leads at the turnaround are 0.08 or more (what the 0.573 swing
      carries, -0.085 on x and -0.18 on z), while the turnaround's standing frames cost
      0.03 an axis first; from 0.67 the outward lead peaks near 0.03 (seven frames of room
      at speed 20, or none uphill: at steepness 0.51 the slope takes 0.87 a frame, what the
      walk adds, so the speed stays at 8 and every frame snaps), which the turnaround
      spends whole. The uphill-first-step design (the fixer setting the error at the last
      snap so a swing can run outward on the light axis) falls to the same arithmetic and
      is closed: the light axis's swing is uphill at that tilt and never reaches the speed
      that steps, and a swing with a downhill component steepens the heavy axis into its
      edge. The rest's radius is the lead budget, and 0.6 is about where it suffices, and the
      rest must sit on the diagonal: asked for at tilt 0.6, a rest 8 units off it, (-166,
      174), gives 100 first oscillations in 3,000 shots, while (-189, 152) and (-157, 198),
      20 to either side, give none (from the z-heavy side the swing runs at the +z edge and
      from the x-heavy side at the -x end without the lead it needs). So the fixer's
      near-rest tolerance was 8 units (was 20; since 2026-10-05 a bound of 20 on the positions
      the fixer tries, measured on the stage, 4.8): with it the floors 0.58 and 0.6 rest at
      (-166, 174) and (-172, 168), 0.613 both, and cross (100 in 3,000 and 100 in 2,613
      shots), where the 20 let a 0.58 floor rest 17 units z-heavy and cross never. The
      maintainer's config at the time (dr `startXzSum` 0.67 with a rest at 0.6, forcing the
      tilt to climb to 0.67 before any dip, and `minFirstCrossingSpeed` 25) found no first
      oscillation for those two settings besides. The maintainer then held to his memory of
      oscillations from starts above 0.7, and the pre-fixer config shows how those began:
      Mario idle at the target normal itself ((-0.18, 0.39), sum 0.573, at (-100, 218) from
      the home) and the initial phase ending only once the tilt had reached `minXzSum`
      0.69, the old lineup running it up with random sticks and handing over with speed and
      leads. That handover is back as a mode (2026-09-25): a `handoverXzSum` above the
      rest's tilt (the stage passes the rest's tilt in `NormalSpecsDto::restXzSum`; the
      switch was `startXzSum` itself for a day, which made the working configuration, a
      0.573 rest under a 0.6 regime floor, run the lineup by mistake) makes the initial
      phase the lineup, random sticks with the chord leg and a corner-ward lead run
      (`LeadRun_1f` toward the corner) mixed in, exiting at the tilt asked for, and the old
      moves take the swing from there. From the diagonal 0.573 rest it hands over at 0.69
      three times in 10,000 shots (65 to 115 frames after the rest, speed 17, leads 0.046
      on both axes) and the first oscillation from those three comes 100 times in 926
      shots: the oscillation at 0.69 is not the hard part, the lineup to it is. From the
      old-style z-heavy rest at the target normal the lineup never passes 0.593 (0 in
      10,000 at 0.6 and at 0.69). A corner-ward lead run alone cannot even start the
      lineup: straight into the corner from these rests the first frame moves the goal
      0.0095 an axis, under the step, the cross term of the goal's geometry cutting an
      outward move on both axes where it helps a chord step. `bitfs-turn --test`'s
      first-pass case sets `startXzSum` to 0 so that it checks the leg from the rest
      whatever handover the config asks for. The maintainer's direction then (2026-09-26):
      the start may be low, but every oscillation must build speed and tilt consistently
      without a huge shot count. A per-pass death census from the 0.573 rest (floor 0.6,
      3,000 shots a pass, seed 6) found the third oscillation's pass, 25 solutions in
      3,000, losing 49,000 pellets to moves that find no stick keeping both axes stepping
      and 18,000 to the regime floor, and the fourth 107,000 and 21,000: the same
      lead-starving turn the first swing had, in every swing. So the lead run steers every
      swing now, not only the first: the passes go to 37, 396, 43, 177, 137 shots and the
      last 124 in 3,000 on seed 6 (from 37, 1,439, 29, 25 in 3,000 and 818), 28, 1,456,
      25, 79 and 393 on seed 7; the first oscillation is the one pass still over a few
      hundred shots. Ten oscillations on seeds 6 to 8: the tilt at the crossings climbs
      two hundredths a swing (0.63 to 0.69 at the first, then 0.69, 0.71, 0.73, 0.75) with
      passes 2 to 5 at 25 to 182 shots, and every seed stops at the sixth oscillation,
      where 0.75 on the diagonal is the platform's lava ceiling: the oscillation count has
      to end before it, which the config's 5 does. The speed at the crossings stays at 13
      to 21; a lead run that took the fastest stick once both leads were past 0.05 built
      it (25 at the second crossing) and cost the passes their reliability (the first
      oscillation 33, 46 and 0 in 3,000 on seeds 6 to 8), so the thinner lead stays the
      score. A rest off the diagonal with the leg toward the diagonal would have
      the steepness fall first and give 24 to 28 frames at 0.67, but that leg's first
      frame is uphill and snaps; the one design that would open it is the fixer setting
      the error after that first step rather than at the rest (the step's snap is a few
      thousand ULPs, and the landing search would measure the normal one frame later with
      a stated stick), a change to the fixer's contract for the maintainer to decide. So
      the config keeps the rest at 0.593 (the maintainer's 0.67 finds no oscillation from
      any rest the fixer reaches) and the fixer's start at the movie's dive slide. The
      first leg toward the other chord end gives no first oscillation on any of the three
      seeds, so the leg's rule stands. The rule is the maintainer's "gain speed each crossing"; as it stood, from the rest
      the first swing was long and fast and later crossings could not beat it. The speed it
      climbs from is the config's to set (the maintainer, 2026-09-23): `minFirstCrossingSpeed`
      asks a forward speed of Mario at the first crossing, 0 asking none. The handover tilt cannot rise with this search: 0.55, 0.65 and 0.69 each
      give no first crossing on seeds 6, 7, 9 and 10 (the lineups themselves are quick at
      0.65 and 0.69, 100 in 16 shots, with no continuation, 112 to 201 blocks), against
      30 to 57 at 0.6; the maintainer expects it nearer the regime's 0.69, so raising it
      waits on the handover being solved;
      `platform` for the other stages; the edge as a knob
      (the target normal and `osc-final`'s quadrant pair express it today); and one
      decision for the maintainer, the oscillation starting at 0.6 for a regime of 0.69.
      The maintainer's own oscillations from years ago, traced (2026-09-26;
      `movies/bitfs-osc-final-jp.m64`, frames 3315 to 3604, with `dllcheck --trace` now
      printing the pyramid's normal): exact, every frame from 3365 on a full step on both
      axes through fifteen axis reversals (3316 to 3364 snap the normal into place by hand,
      the movie's own fixer). The oscillation is Mario running the chord across the corner,
      between (-361, -15) and (0, 364) from the home, perpendicular to the diagonal, so the
      normal trades one axis for the other and the tilt holds (0.693 and 0.713 turn about,
      the two axes reversing a frame apart at each end); the turnaround is the stick held
      back, `ACT_TURNING_AROUND` for five frames from 27 speed and the finish from 8 gaining
      1.7 a frame, downhill because the lagging normal keeps the platform tilted toward the
      end just left; speed 17 at the first reversal and 28 by the sixth, the leads 0.1 to
      0.28 mid-swing, a half period of 26 to 31 frames. The search's own five-oscillation
      solution (seed 6, from the 0.573 rest, the lead run in every swing) is the same
      motion: the chord from (-315, 70) to (-55, 316), the reversals none to three frames
      apart, speeds 16 to 25, the tilt 0.593, 0.633, 0.693, 0.693, 0.713, 0.733 at the
      crossings, two hundredths gained for each frame between the two axes' reversals when
      the second to reverse is the one stepping outward (the other order loses as much,
      which is what the regime floor rejects). What differs is the start alone: the movie's
      oscillation grew out of a nearly flat platform (0.03 at 3372), Mario at 200 to 270
      from the home with both leads 0.1 to 0.26 and the normal chasing him two hundredths
      a frame for 23 frames (0.233 at 3419 to 0.693 at 3442), the turnaround at 0.43 to
      0.51 while the speed was still rising, the normal catching up during the return; the
      pipeline starts from a rest, where the leads are zero and a swing adds only the
      frames between its reversals. That start tried in the pipeline (the fixer at
      `minXzSum` 0.3 rests at (-82, 89), tilt 0.333, 154,757 frame advances; the handover
      mode from it): to 0.69, 27, 20 and 14 handovers in 10,000 shots on seeds 6 to 8
      (against 3 and 6 from the 0.573 rest) and a first oscillation 0, 1 and 0 in 3,000
      under the 0.67 floor, the swings arriving at 0.733 and dying where their axes
      reverse in the inward order; to 0.5 or 0.6, 100 handovers in 36 and 35 shots, with
      balanced leads (0.09 and 0.09, 0.16 and 0.07) at 20 to 24 speed, and every
      continuation dead within five frames: the swing moves run on outward until the
      thinner lead starves, and the movie's answer, a turnaround at rising speed, the
      phase rules reject (a turnaround counts only from the uphill-turn phase, which a
      speed drop opens); without a handover the leg from the 0.333 rest gives 100 in 23
      shots and no oscillation, the tilt-must-not-fall rule below the regime and
      `minAxis` leaving a low swing no room (the floor is not the killer: under 0.6 the 0.69
      handover gives the same 27 and 0, the plain leg 100 and 0). So the movie's start needs the stage's rules
      to change (a turnaround from a rising run, and the floor and the monotonic climb
      deferred until the oscillation is established), the maintainer's rules and his
      decision; the working start remains the diagonal rest at 0.57 to 0.61 under a 0.6
      floor, whose oscillation is the movie's. The maintainer's direction on that
      (2026-09-26): the turnaround check generalized to the frame before having run uphill,
      carried by the metric so that no frame is loaded for it; the climb kept, with one
      shift outstanding at most; the rest deferred only if that is not enough. Built:
      `BitfsDrMetrics` records `ranUphill` (Mario's floor angle against his facing, the
      test `apply_slope_accel` makes) and `maxXzSum`; a turnaround begins only from a frame
      that ran uphill (before, only from the uphill-turn phase, which a speed drop opened),
      the run-downhill phase offers the turnaround move when the last frame did, and below
      the regime the tilt may sit at most 0.02 under its highest yet (before, it had to
      rise on every frame). Measured at 3,000 shots a pass, five oscillations: from the
      committed rest (0.613, floor 0.6) seeds 6 to 8 went 24, 2,764, 0; 23, 15, 55, 135,
      291; 25, 1,251, 66, 91, 87 before and 24, 1,420, 53, 17, 29, 0; 23, 6, 40, 83, 186;
      25, 12, 54, 77, 158 after (seed 6 further, seeds 7 and 8 fewer first oscillations);
      from the 0.333 rest under the maintainer's floors (0.67, the last pass 0.69) the
      handover at 0.6 runs through on seed 6, 44, 90 in 3,000, 48, 77, 57 in 3,000 and 111
      at the last pass, at 0.5 too (36, 72, 16, 57, 47 and 30), at 0.69 and without a
      handover still not (27 in 10,000 and 0; 23 and 0); seeds 7 and 8 at 0.6 hand over in
      31 and 38 shots and die within two frames, their ten kept lineups being the fastest,
      which run straight into the corner with the z lead at 0.01 to 0.04, where seed 6's
      ten kept a balanced one, by the sampled CSV. A lineup counting as the first pass's
      solution only with 0.05 of lead on both axes was tried on that reading and reverted:
      seeds 6 and 7 then hand over in 45 shots and nothing follows, seed 6's working
      start included, while seed 8 goes 44, 40 in 3,000, 123, 1,174 and stops at the
      fourth: the gate reshuffles which seed's roots continue rather than making them
      continue, so the viable roots are not the balanced-lead lineups the sample
      suggested. The pool is: with the stage's `maxSolutions` at 1,000 instead of 100
      (so the lineup pass keeps 700 lineups from 3,000 shots rather than the first 100
      from 40, and every pass likewise) the same start runs through on all three seeds
      under the 0.67 floor with the last pass at 0.69: seed 6 721, 218 in 3,000, 795,
      1,000 in 2,399, 417 and 408 at the last pass; seed 7 746, 1,000 in 1,847, 1,000 in
      976, 1,000 in 1,999, 471 and 186; seed 8 722, 79 in 3,000, 1,000 in 2,210, 551, 194
      and 46; 270 to 320 s a stage. That is the maintainer's ask met from a low start
      (every oscillation building speed and tilt, no pass over 3,000 shots) and the
      configuration for it is his to set: the fixer's `minXzSum` 0.3, `handoverXzSum`
      0.6, the dr stage's `maxSolutions` 1,000, `startXzSum` 0.67 and `minXzSum` 0.69.
      Open: whether `keepTop` (10) or the lineup pass's cap is the better knob, since the
      roots' variety, not the leads, is what the first oscillation needs. The committed
      pipeline's inconsistent runs (the maintainer, 2026-10-03: oscillations that progress
      quickly one run and stall early the next) had a cause of their own, found from four
      logged runs: the first-oscillation pass found its 100 solutions every time, but the
      direction commit after it read `solutions[0]`, an arbitrary solution since a run's
      solutions come back in block order, and when that one was of the minority direction
      the stage dropped the 97 to 99 others and carried 1 to 3 slow ones at tilt 0.653,
      from which the next pass made no block in 5,000 shots. The commit now follows the
      direction most of the `keepTop` fastest solutions took, and the stage logs each
      pass's hand-over. The runs that still stall after that fix have one mechanism,
      measured on 28 logged runs of `--stage dr` (2026-10-03, Release, 16 threads, the
      config's non-deterministic mode; a run takes 18 to 180 s): the leg hands over with
      the normal's x at about -0.22 and Mario near the pyramid's x centre, from where x
      decays 0.01 a frame to the `minAxis` floor (0.05) in 17 frames, frame 3340 to 3357,
      and the first swing has to carry Mario far enough toward -x to turn that decay
      around in time. The search finds such a chain in about half the passes: the first
      oscillation yielded 100 solutions in 12 of 28 passes and 0 to 15 in the rest; a slow
      pass's population never reaches frame 3356, a fast pass's recovering lineage stands
      200 units toward -x with x at -0.27 at 3357 and crossing 2 counted. It is neither a
      budget nor a roots problem: the leg kept to 1,000 solutions (first oscillation 364,
      1, 3, 3, 1, 291), 20,000 shots a pass (7, 100, 1, 6, 2, 100, one full yield at shot
      11,564) and a root restart every 10 shots (5, 0, 1, 100, 1, 100) left the odds where
      5,000 shots and 100 roots put them (15, 0, 100, 0, 3, 100); the stage ran through in
      2, 4, 4 and 3 of 6 runs, a thin pass of 1 to 7 solutions continuing about two times
      in five now that the hand-over keeps the fastest. A deterministic seed replays
      exactly (seed 6 twice, identical passes). The remedies are the maintainer's to
      choose: hand the leg over with x margin (its solutions picked by |x| rather than the
      first 100 found, or its end asked for earlier); let the pre-regime search see the
      margin (its fitness is speed and its bin holds no normal, so a recovering lineage
      competes on speed alone); a lower floor for the first swing (0.05 was chosen over
      0.13, which left it too few frames); or a retry of a pass that yields fewer than
      `keepTop` from the same roots under a derived seed, which bounds the stage's failure
      at the lottery's odds to the power of the retries without touching the search.
      Measured further the same day: what kills the slow first-oscillation populations is
      the lava, not the floor (at `minAxis` 0.02 and 0.0 the odds are the same, 0, 0, 100,
      100, 3, 100 and 100, 2, 0, 9, 0, 100, and the slow populations still end at frame
      3354, standing on the sinking +z end with a median clearance of 3 units over the lava
      at -3071, which the end loses at about 4 a frame as the tilt pours into z; the first
      swing's fitness is speed, speed is downhill, and downhill is the sinking end, so a
      run succeeds when a lineage happens to run toward -x early instead: every winner is
      190 to 220 units toward -x with 50 to 70 of clearance at its second crossing). And
      what carries the chain from one oscillation to the next, from the 58 runs'
      hand-overs: into the second oscillation, the speed at the first crossing, the
      turnaround's 16 (every hand-over whose fastest root was under 16.1 died, 10 of 10,
      whatever its tilt or count; every one over 17.5 continued, 26 of 26), which is what
      `minFirstCrossingSpeed` asks and the config now sets to 17 (six runs: 100, 0, 0, 100,
      1, 100 at the first oscillation, the 1 carried at 17.4 and through to 459, no death
      at the second oscillation); through the later ones, the tilt sum at the crossing,
      with a ceiling near 0.73 (roots at 0.69 to 0.71 gave the next pass 100 to 583,
      at 0.733 12 to 142, at 0.753 nothing, 3 of 3; the sum climbs about 0.02 an
      oscillation, so from a regime entered at 0.67 the ceiling is the fourth or fifth
      oscillation, the maintainer's own experience of years ago). Both are rules of the
      script now, configurable in `normalSpecs` (the maintainer's go, 2026-10-03, on his
      principle that a solution that cannot be continued is not one): `maxXzSum`, the
      ceiling the tilt of a solution carried into a next pass may not exceed, and
      `minLavaClearance`, the least height of Mario over the lava a state may have.
      Measured, neither value helps and the config asks none of either: a ceiling of 0.733
      starves the fourth and fifth passes, whose crossings mostly sit above it by then (of
      the runs reaching the fourth oscillation 1 in 3 through, against about 17 in 20
      without; applied to the last pass too, 0 in 5); a clearance of 20 gave the first
      oscillation 100 solutions in 5 runs of 6 and then 2 of 8, 7 in 14 pooled, the same
      one in two as without (10 and 5: 2 and 1 in 6). Two lessons: six runs cannot tell a
      coin flip from a fix, judge the first oscillation on fifteen or more; and pruning the
      drowning states does not make the search find the lineage that leaves the end, so
      the first swing's lottery stands, and what is left for it is steering. The search's
      base block is drawn uniformly from the table (fitness decides only which block keeps
      a bin), so a region's share of the shots is its share of the blocks, which is why the
      sinking end, where the population runs, takes the shots and the lineage that turned
      starves, and why a breakthrough comes early, while the table is small, or never.
      The steering rule built on that (the maintainer's go, 2026-10-03): `maxRetreat`,
      how far the first swing may fall back from the leg's end along the way to its
      target before the state is refused (90 in the config; the metric keeps the leg's
      end, `legEndX`/`legEndZ`, and progress is the displacement from it along the rough
      target angle), so the states that run on to the sinking end never fill the table.
      Measured 2026-10-04 on 16 sequential runs a setting with nothing else on the
      machine, the first oscillation (full, thin) and the stage through: none, 8 and 3,
      11; 90, 8 and 1, 9. The rule does nothing (60 cuts the turnaround's own overshoot of
      up to 90 units, 3 of 16; a first table taken while series overlapped on the machine,
      a queued series having started on a stale exit marker, had said 14 of 16 at 90 and
      was wrong: thread interleaving is what this lottery turns on, so one search at a
      time). So refusing the wrong-way states does not give the lineage that leaves what
      it lacks, and the config asks no `maxRetreat`. The first oscillation stands at about
      one in two full and two in three through with the thin hand-overs the speed floor
      lets through. A retry exists as a diagnostic, `passRetries` (0 in the config,
      none): a pass that yields fewer than `keepTop` solutions (the last pass, none) runs
      again from the same roots under the run's seed plus 7,919 times the attempt, its
      solutions pooled, each retry logged and given its own viewer tab. With 3, 16
      sequential runs went 13 through against 11, the first oscillation ending none and
      the three failures all at the last pass from roots at the tilt ceiling; but the
      maintainer's rule stands (2026-10-04): retries should not be needed, a retry
      re-rolls the lottery rather than removing it, so the committed config asks none and
      the first oscillation's cause is the open item. What is known: the pass either
      produces a second crossing at turnaround speed within its first hundred shots a
      thread or produces only slow ones (12 to 16) that cannot continue; roots, shots,
      restarts, the tilt floor, lava clearance and the retreat limit do not move it. What
      is not known is what, pellet by pellet, keeps the turning lineage from arising in
      the slow runs: the next step is a census inside the first-oscillation pass (which
      rule rejects each pellet, at which frame, after which move; how many reach the
      turnaround and the second crossing) on a seed that fails deterministically against
      one that succeeds. Done 2026-10-04 on seeds 6 to 11 (three cross, three never do):
      the rules are not the killer (5,400 rejections of 200,000 scripts, lava included;
      55,000 pellets end because no stick keeps both axes stepping; 139,000 states
      validate, 2,500 of them turning around, 29,000 running after the turn, and in a
      failing seed none of those ever at the second crossing); the crossing lineages never
      left the leg's end (the first crossings 120 to 180 units toward -x, 260 to 280 out
      on +z, at speed 18 to 20, at frames 3349 to 3350, ten frames after the leg ended at
      about -110 and 240); the population at large runs the other way first (every
      turnaround the census could place begins 20 to 160 units toward +x, after the lead
      steering carried Mario out, where no crossing comes before the sinking end is under
      him). The reversal that can cross must come within a frame or two of the leg's end:
      an uphill frame, which costs about a unit of speed, then the turnaround, which the
      game refuses under 16, from a leg that ends at the first frame of 16, drawn as a
      one-in-eleven move followed by another; three seeds in six never draw it in time.
      Two changes at that source: `legExitSpeed` in `normalSpecs` (17.5 in the config,
      16 by default), the speed at which the leg hands over, so the uphill frame and the
      turnaround fit; and the first swing's move weights, the uphill turn and the
      turnaround drawn as often as the run while `currentCrossing` is 1, the later
      swings unchanged. With those two, the six seeds all find the first oscillation
      (100, 100, 100, 100, 100 and seed 6's 4; five run through, seed 9 dying at its
      fourth pass at the ceiling), where three found none before. The thin seed is the
      return's needle: a crossing is the frame the reversing axis's goal jumps a full step
      past the normal, about six units a frame toward the far end on that axis, and the
      after-turn moves ran the downhill angle, which from the sinking end points along the
      slope's axis and leaves the other axis to the deviation's luck (the first crossings
      came at speed 18 to 20, from the lineages that drew a heading well toward -z). A
      return that ran straight at the chord's far end (the rough target angle, a
      deviation of up to 2048 either side, four draws in six beside the two downhill
      runs) was tried on top and is worse, reverted: five deterministic seeds of six found
      no first oscillation and 16 runs gave 3 full, 6 thin, 7 none; the downhill run
      keeps the speed the crossing needs where the straight run loses it uphill. The two
      changes that stand, measured: deterministic seeds 6 to 11, the first oscillation on
      all six (five full, seed 6 with 4) against three before, with the weights alone four
      of six; 16 non-deterministic runs, the first oscillation found in 15 (9 full, 6
      thin) against 11 before, one run dying there, and the stage through 11 of 16, four
      of the five failures now at the fourth and fifth passes. The census of the fixed
      source, seed 6 (4 solutions) against seed 7 (100): both cross at the same place and
      moment, 140 units toward -x, 285 out on +z, at frames 3351 to 3352, eleven frames
      after the leg's end, seed 7's lineage with 12 units over the lava, which lived, and
      seed 6's with 10, which drowned before its next frame; the exit speed bought the
      turnaround but spent a frame or two of the leg, so the crossing lands at the lava's
      edge. Two changes tried at that. `legMinClearance` in `normalSpecs`, the least
      height over the lava at the leg's end for the leg to count as a solution, is wrong:
      at 60 (the leg's ends run 55 to 65) seeds 6, 7 and 9 found no first oscillation at
      all and seed 8 found it in 50 shots, where without it all four do, so it stays at 0
      as a diagnostic; why a higher leg's end crosses worse is not explained. The first
      swing's weights raised to three times in four against the run: the six deterministic
      seeds all full, but 16 plain runs found the first oscillation in 12 (7 full) against
      15 (9 full), so the weights stay as they were. The first crossing's speed floor at
      none: 15 of 16 found (7 full), 12 through, and one run carried a single root that
      made no block at all, which the floor is there to refuse; it stays at 17. The census
      says what the crossing is: Mario crosses in the frame his turnaround finishes
      (`ACT_FINISH_TURNING_AROUND`, 17.6 speed, facing the target), both axes reversing by
      a full step in that one frame, 10 to 12 units over the lava, and the lineage that
      lives is the one the search plays on from (seed 7's 15 crossing states had 119
      children a frame later, seed 6's 49 none). Two leads open. The deterministic runs
      find the first oscillation far more often than the plain ones (11 full of 12 seeds
      against about 8 of 16 runs, the crossing by the twelfth shot a thread against the
      hundredth or never), with the same per-shot statistics. The threads' race is not it:
      on one thread (no race) seeds 6 to 11 found the first oscillation full in 4 and not
      at all in 2, at the plain runs' odds (3 full, 3 thin, 2 none of 8 on sixteen threads
      the same hour), so what helps the deterministic mode is its lockstep itself, the
      threads advancing a move at a time in turn, which is not understood yet. A return
      frame that crosses the moment a stick makes it (a fan of nine sticks around the
      downhill angle each return frame, the first whose frame reverses both axes toward
      the target, at the first crossing's speed floor) was tried and is out again: seeds
      6 and 7 thin (3 solutions each) where the plain return had seed 7 full; the stick is
      not what the double reversal waits on, the two leads' timing is. Nor do the leg's
      roots: every leg solution carried (`keepTop` 100) gave 8 full of 16 plain runs, the
      ten fastest 4 of 8 the same hour. And the deterministic mode's edge was the seeds:
      sixteen fresh seeds (12 to 27) in that mode found the first oscillation full in 8,
      thin in 2 and not at all in 6, the plain runs' odds; seeds 6 to 11, reused for every
      build's trial all day, are a lucky set, and a binary trial on them says little (a
      lesson, with the earlier one about six runs). So every setting tried leaves the
      first oscillation at about one pass in two within 5,000 shots: the crossing's rarity
      is the swing's own, a double full-step reversal at the first crossing's speed in the
      last frame or two over the lava, and the search's uniform draw finds it at a rate the
      budget decides, though not alone: at 15,000 shots a pass 12 of 16 came full (one in 11,626
      shots) and one still found nothing, its table saturated near 3,400 blocks with 10,000
      shots re-firing the same owners, so the bins decide what is reachable (the pre-regime
      bin held the cell, four yaw quarters, the action and the phase, nothing of the leads,
      the speed or the tilt's progress). The richer bin, the first swing's leads in 0.005
      regions, its speed in 2-unit regions, the z normal and the yaw in 32 regions beside
      the cell, is the fix on the search's side: the first oscillation's pass keeps about
      20,000 blocks instead of 3,400, and 16 plain runs of the committed config found the
      first oscillation in all 16 (13 full, the thin ones 8 to 22) against 15 (9 full); the
      4 that then failed died at the last pass, the climb. And the one lever
      that moved everything was the rest's tilt: the fixer asked for 0.55 rests at 0.573
      (frame 3327, normal (-0.289, 0.284)), and from there 16 of 16 plain runs ran through,
      the first oscillation found in all (9 full, 7 thin, every thin one carried on) and the
      last pass 54 to 1,505 solutions at tilt 0.693, against 11 of 16 and 20 to 450 from the
      0.613 rest, whose runs die at the last pass with roots at 0.753. The lower rest gives
      the first swing its margin over the lava and the climb its room under the ceiling at
      once; the maintainer took it the same day: the fixer's floor is 0.55 in the config
      (the rest at 0.573) and the oscillation passes fire 2,000 shots instead of 5,000. The two together, the richer bin from the 0.573
      rest: 16 of 16 plain runs through, the first oscillation's pass 46 to 100 solutions
      in every run (never under 46 where the 0.613 rest's thin passes were 2 to 22) and
      the last pass 19 to 3,434, runs 38 to 161 s (the richer bin's tables, up to 184,000
      blocks in a last pass, cost the time). What the later swings have that the first
      lacks, and an alignment after the fixer must produce (the maintainer's direction,
      2026-10-04): a Mario already running the chord with the normal lagging him equally
      on both axes, so the end ahead is uphill, the turnaround legal anywhere near it, and
      the two leads reach zero in the same frame on the way back. From a standing rest the
      only such run is the leg itself, and the leg's end decides whether its lead-building
      run has room: toward A, the +z edge, it is cut off at about 0.03 of lead by the
      sinking end; toward B, the -x side, the later swings turn with room. The leg points
      at A on a measurement from the lineup era ("the other end gives 0 on every seed",
      which no longer holds: B leaves the rest as well);
      the leg pointed at B (the two signs in `FirstLeg_1f` negated) leaves the rest as
      easily as toward A (100 legs in 16 shots) and on 16 plain runs of the current config
      found the first oscillation in all 16, full in 6 (407 to 1,526 shots), 14 through:
      better than nothing, not the later swings' pace. Its baseline, the leg toward A on
      the same config, and a census of the B source reading the first swing against the
      second in one run, are measured. The baseline is the same: 16 found, 6 full, 15 through,
      so the leg's end is not the lever. The census (seed 6, the B source) names what is:
      the second oscillation's pass runs 93 percent of its moves as the lead-steered run in
      the turning-uphill phase, 0.8 percent failing, turnarounds beginning over seventeen
      frames, 178 drownings in 52,000 states, 100 solutions in 16 shots, because the normal
      is still on the far side of the center from the crossing it starts at, so the chord
      ahead is uphill and the turnaround legal anywhere. The first swing's pass runs the
      downhill run and the return, 7 to 22 percent of its moves failing, because the rest
      is an equilibrium: the normal starts at Mario and chases him from behind, the chord
      is level and then downhill, the uphill frame the turnaround rule asks for never comes
      on it, and the lineages that veer to find one hit a wall (toward A the lava, 4,700
      drownings; toward B the quadrant floor, 5,574 states refused in the frame the z
      normal reaches `minAxis`), 48 states crossing in two frames. Being measured: the
      first swing's turnaround freed of the uphill rule (the move's offer and the
      validation, `currentCrossing` 1 only), on the B source: 16 plain runs, the first
      oscillation full in all 16, in 161 to 517 shots (median about 290) where the same
      config and rule took 737 to 2,000 and came full in 6; 15 through, the one failure a
      dead root at the fourth pass; runs 21 to 51 s. The rule was written for swings that
      start at a crossing, where the end ahead is uphill; the first swing starts at an
      equilibrium, where it is not, and the rule made it veer into the wall. With the leg
      toward A, the committed end, the same exemption: 16 of 16 full, 163 to 1,757 shots
      (median about 430), 15 through, the failure at the last pass; so the exemption is
      the fix and the leg's end a preference (B's first pass is about a third quicker).
      The exemption stands (the maintainer, 2026-10-04: "definitely keep it"), the first
      swing's `currentCrossing` 1 freed of the rule in the turnaround's offer and in the
      validation; the leg stays at A. A regression case guards it in `bitfs-turn --test` (the
      maintainer's ask): in every corner, the config's target normal mirrored into it, the
      fixer rests and the first oscillation's pass comes full within 2,000 shots, one
      thread, deterministic; the stage's per-pass yield (`passSolutions` on the solution
      set) is what it reads. The probe behind it: the fixer rests in corners 1 to 3 as in 4
      (frames 3325 to 3329) and their first oscillations came in 155, 214 and 142 shots on
      16 threads. After it, the climb: the sum rises about 0.02 an oscillation and the
      fifth pass works at the lava's limit. The fixer is at its limit (rests at 0.65 and
      above drown the first swing, off the diagonal it has no first frame).
- [ ] **4.7 A solutions database.** Store the input files (or compressed versions) with
      numerical data about each, queryable and filterable. Nothing exists yet; the per-stage
      solution files from 1.4 (`solutions/<stage>.json`: diffs plus named metrics) are the
      nearest thing and a natural import format.
- [ ] **4.8 Fine-tuned targeting.** A squish-cancel brute forcer that hits floating-point
      precise target values when given a sufficiently close starting m64 (the ARE machinery
      in `TiltTargetShot` is the seed of this). Progress exists. Assessed 2026-09-21, the
      first of the Phase 4 items taken up; the maintainer's verdict on the current fixer:
      flaky and inefficient after an enormous amount of time. What it searches: `tilt-target`
      draws random inputs from the start frame, evaluates every script by running the game to
      the pyramid's equilibrium (95% of the Tier D run's frame advances, 297 per pellet), bins
      Mario's position at 10 units and scores `-|ARE|`. What it is looking for, by
      `scripts/are_cell_model.py` (float32, the pyramid's own arithmetic): with Mario at rest
      the normal snaps to the goal his position defines (`approach_by_increment` within
      0.01), so the resting normal and with it the ARE are pure functions of the rest
      position. Along x the ARE moves 16 ULPs per float step of x (0.00012 units), along z 4
      per float step (0.00006 units); the rest positions inside the ±100 neighborhood on both
      axes are cells 0.0016 by 0.003 units, one every 5.1 units on each axis, half of them
      with the step parity the oscillation stage requires; the coupling through the distance
      moves the other axis's ARE by 0.06 ULP per float step, which is what puts the exact
      values in between. The stage chain then asks `tilt-z` to hold ARE_x at the integer
      `tilt-x` found while ARE_z lands within 100: one float position of x per 5 units, or
      none, on a 16-ULP lattice, which is the luck the million-shot budgets pay for, and
      nothing downstream asks for it (`dr` and `osc-final` conserve whatever ARE the
      equilibrium frame has; the box is `tilt-range`'s own). The maintainer's proposal
      (2026-09-21): dive onto the platform, cut the speed in the air, dive-recover near the
      center and land at rest, then tune the landing spot until the ARE is right, since air
      movement targets float-precise positions within a handful of frames. The model backs
      it: the ARE measured at one rest gives the correction outright (ARE_x/16 float steps
      of x, ARE_z/4 of z, or one band of 5.1 units on one axis when the parity is wrong), so
      after a first landing the target cell is known and the loop is a targeted landing (a
      nested sweep of the last air frames' sticks, the compare family's kind of work) and a
      re-measure, a few million frame advances per round rather than a stage. Scattershot has
      no place in that loop: its 10-unit bins are coarser than the cells' 5.1-unit period, so
      an ARE fitness is noise to it. Open before building: whether two or three air frames of
      stick input reach a cell in the game (one frame's stick lattice is 0.006 to 0.16 units
      of position; the prototype measures the landing density), whether ±100 ULPs is the real
      requirement, and where the rest position may be (the `tilt-range` box or anywhere the
      oscillations can start). Built the same day on the maintainer's answers (the rest
      position is free, the tolerance stays, aim well under a few million frame advances, no
      A press after the level entry since the run is an A-button challenge, and the next item
      is the first oscillation's flakiness): `BitFsAreFixer` in tasfw-scripts, the
      `are-fixer` stage type, the `are-fix` stage of config.json, and its test under
      `bitfs-turn --test` (the executable's own optional tests, `Tests.cpp`, which neither CI
      nor tasfw-tests runs). The landing that rests is the movie's own dive recover: the
      movie dives onto the platform shedding speed in the air (frames 3261 to 3268), lands in
      the dive slide at 3269 and rolls out to a rest at 3285; the stage starts at 3269 and
      owns the rollout, whose fifteen air frames are steered with B on the first frame and
      no other button, and Mario rests where it lands (`ACT_FREEFALL_LAND_STOP` is stationary
      at once). Two earlier landings are gone: a jump steered with A held and a ground pound
      pressed A, and a walk to the dive's 29 speed from the idle frame cannot reach it, since
      the platform's tilt follows Mario with a lag, so a straight run is uphill, circling
      wide stalls under 20 and the direction to the center turns as fast as his face can
      while he circles; an idle start needs the oscillation stage's way of building speed.
      One round: Newton on the steered frames' constant stick aims the landing at the wanted
      spot; the rest is measured; the correction to the nearest cell with the right parity
      comes from the model's Jacobian; the rest's response to the landing is measured from
      two probe landings, because the platform tilts from near flat to the resting normal
      after the landing and carries Mario tens of units with it, so the rest moves more than
      a landing shift, with cross terms; the last three air frames' sticks are measured one
      frame at a time (the 500 nearest the needed change per frame, a load and an advance
      each; the landing frame moves a quarter step or more of its velocity, measured from the
      current stick), combined with the forward speed each leaves behind, looked up in a
      grid for the combinations predicted inside the cell, and the nearest prediction is
      played out; where it rests re-targets the same lattices until one rests inside the
      tolerance. The script's cursor stays on the rollout's first frame throughout: every
      trial is an ad-hoc block that reverts, the lattices are one `TestAdhoc` per stick, the
      candidates are a `CompareAdhoc` whose terminator is the tolerance and whose comparator
      keeps the nearer rest, and the winner's diff is applied once (the maintainer,
      2026-09-21: a script's own `Rollback` and `Load` are a code smell; a Modify block
      returning false reverts, and the compare family fits where it fits). On the pinned
      movie from frame 3269 with the config's target and ±100 ULPs: solved in two rounds,
      ARE (-57, 21), steps (13, 49), equilibrium frame 3331 (the frame whose state the frame
      before already had; the diff ends before it), the rest 170 units from the center with
      the normal (-0.31, -0.10), for about 6,000 frame advances, 8 saves and 3,055 loads with
      the cost model on (the automatic saves are timing-dependent, so those vary by a few
      dozen between runs; 51,090 advances, 3,269 of them the seek to the start frame, 1 save and
      3,055 loads with it off, identical on both compilers), against the Tier D tilt-target
      run's 17.8 million frame advances for 52 solutions on one axis. Fixed on the way, in
      `Script.t.hpp`: the status-carrying `ExecuteAdhoc<T>` and `ModifyAdhoc<T>` handed the
      body the status object where their concept, the how-to and the compare family's
      candidates name a pointer, so no conforming lambda had ever compiled against them;
      `test_script_lifecycle.cpp` pins the pointer (`TestAdhoc<T>` is protected, so it
      cannot be exercised from a script). Wired in the same day at the maintainer's word:
      config.json's three tilt stages are gone, `are-fix` is the first stage and `dr`'s
      input, and every later stage starts at 3269, since the fixer's solutions do (a piped
      diff is applied from its first frame). A 300-shot smoke run of `dr` from that rest
      went through the plumbing (39,707 blocks, no base-block validation failure, no
      solution at that size against the configured 50,000); how the first oscillation
      behaves from it is the next item. 2026-10-04: the maintainer asked for an exact match
      (`tolerance` 0) and never got one. Assessed in the game's float32 arithmetic, in a
      two-dimensional extension of `scripts/are_cell_model.py` cross-checked against a
      compiled copy of `StepsReversibly` row for row: along x every float whose error to
      -0.17944f is 0 fails the reversibility walk at the 0.25 edge (-0.24944 + 0.01 - 0.01
      lands on another float), and within ±100 only the errors 1 mod 4 survive on x (1, -3,
      5, ...) and the even ones on z; one ULP more negative, -0.17944002f, 0 survives on x.
      Over the 770 cells of the admissible region (quadrant 4, tilt at least 0.5, within 330
      units of the home), 382 hold positions the fixer accepts at ±100 (about 106 float
      positions each), 12 hold one position each at ±1 (all (1, 0), at 253 to 328 units from
      the home, tilts 0.59 to 0.77), none at 0. So exactness is a property of the target
      float's residue class, not of search effort: a target taken from a state the game
      reached through the same tilts is a survivor by construction, a decimal need not be.
      Built the same day and the next at the maintainer's word: `tolerance` 0 is an exact
      match (the play's distance is in ULPs, which it only ever compares), and validation
      refuses what cannot be held before any frame: `LeastError` per axis (the 0.01 bands of
      the corner, the floats around each band's value, the least error that steps
      reversibly; milliseconds), then `Rests`, every lattice cell's box of float32 positions
      through `RestingNormal`, the game's goal arithmetic, kept where one passes every rule
      `Solved` applies (`Holds`), the cells bounded by the tilt Mario can idle on
      (`mario_floor_is_slippery`'s 0.7880108 on a default floor; without it the far cells,
      where z's floats are finer, held six times as many positions as the platform does);
      the pyramid for the home is the one the movie's own dive lands on, the movie's
      inputs played to the dive slide in a block that reverts, 19 frames (at frame 3250
      Mario stands 64 units from the other pyramid's home, running toward this one 922
      units away, so neither nearness nor heading tells them apart: the nearest once sent
      every landing a thousand units off, and the nearest ahead picked the one under his
      feet, which `Approach` silently corrected while validation's refusals and the
      near-rest filter were computed on the wrong pyramid). The
      status carries the least errors and the refusal, which the stage prints, and
      `bitfs-turn --test` pins the numbers (25 positions at ±1, 133,572 at ±100, 25 exact
      for the x target shifted one ULP; `RestingNormal` reproduces the game's resting
      normal from the solved rest). With a survivor target (-0.1792, 0.393, 21 exact
      positions) the search still failed, and the trace said why: the settle after a
      landing, some forty frames of the pyramid tilting to its rest and carrying Mario by a
      rounded amount each, maps landings onto a staircase of rests about seven times
      coarser than the floats (165 landings in one neighbourhood, 23 distinct rests; z
      offsets near the exact position -13, -8, -3, +1, +5, +9, never 0), so a given exact
      position is reached by about one landing attempt in ten, and the search had only ever
      aimed at the one nearest the asked rest, replaying it (9,521 plays, 157 distinct
      landings). The search is now position-driven: the positions that hold the error, the
      cells nearest the rest asked for first and a cell's most central position first, each
      tried with the three ways resting nearest it (`Correction`, the continuous zero of
      the error and its ±1 bands, is gone); candidates are played once per predicted
      landing; the prediction window is never finer than the prediction's own resolution
      (`NoiseX`, `NoiseZ`, the retarget thresholds of before, named); and a failed run
      reports the nearest rest it saw. The first exact match, at the third position, was
      89 units off the corner's diagonal (normal (-0.409, 0.183)), and the `dr` stage's
      first oscillation came but nothing after it, as 4.6 predicts for a rest off the
      diagonal; of the 26 exact positions of -0.1792 and 0.393 one lies within 10 units of
      it, the unreachable one. So the near-rest tolerance is back as a bound (`NearRest`, 20
      units, to the diagonal unless `restX`/`restZ` name a point; 8 at first, then measured
      at the maintainer's question whether 4.6's changes still need it that tight: from rests
      at tilt 0.57 at ±100, three `dr` runs each, the stage went through 4 of 4 on the
      diagonal, 6 of 6 at 10 to 13 units off, 7 of 8 at 18 to 21, 5 of 6 at 29 to 33 and
      slower, 2 of 8 at 37 to 41, 0 of 6 at 60 to 65): only such positions are
      tried or accepted (`Solved`), and validation refuses when none holds the error there.
      A survey of the survivor target pairs within 40 ULPs of those found 643 of 861 with an
      exact position within 10 units of the diagonal; over the whole region (x in 0.13 to
      0.25, z in 0.25 to 0.5), 13% of float pairs are survivors, of which 68% have an exact
      position within 20 units of the diagonal at a tilt of 0.50 to 0.70 (1.7 positions each
      on average; 99% with any tilt Mario can idle at; within 8 units it was 38% and 78%), and of the 24 such pairs nearest the
      targets the fixer reached the position for one. The fixer, run over the pairs nearest
      the targets, solved the second: -0.1792 with z four ULPs lower, 0.3929998875,
      rests exactly at (-2091.21, -561.29), normal (-0.2692, 0.2830), tilt 0.552, 5.3 units
      off the diagonal, in 76,837 frame advances with the cost model on, 2 s, identical on
      both compilers; ±100 takes 130,900, down from 198,046 (docs/performance-changelog.md).
      From that rest the `dr` stage goes through at the first try: the leg in 17 shots,
      oscillations 1 to 3 in 77, 37 and 448, and 236 solutions at oscillation 4 in 2,000,
      20 s in all, the error still exactly 0 on both axes at every solution's normal. Tried
      the same day, at the maintainer's wish for every survivor pair to yield a usable exact
      rest, and out again: measuring the error after the oscillations' own walk over the
      range, so that a rest off the surviving chains, which merges into one at its first
      binade crossing, counts on that chain's error (4.4 times the positions at ±100, 5
      near-diagonal candidates a pair instead of 1.2, 7 of the 24 pairs nearest the targets
      solving instead of 1, and the `dr` stage going through from such a rest with the
      walked error conserved). But the solutions' normals read (0, 1) by the plain measure:
      the axes peak at 0.49 to 0.54 in these runs, the chord's ends, so whether an axis
      crosses 0.5 at all is the run's, z had not, and the setup would have got one ULP. A
      merge is safe only at an edge the oscillation is sure to cross, and above a
      near-diagonal rest's own tilt there is none, so the measure stays the reversible one
      and the candidate count what it is. What remains toward every pair: a rest the
      oscillations can start from at higher tilts (unmeasured) and a second way to rest, the
      dive slide decelerating to a stop, whose settle samples another staircase. Built next
      at the maintainer's word (the second way, and "the min walk after landing and
      entering idle"), 2026-10-05: the walk after the landing, `Walks`, a stick held from
      the land at one of four yaws from Mario's facing for one or two frames, seven variants
      tried per way after the landing left idle, the landing still placed to the float by
      the fine frames and the settle starting from another state: 4 of the 24 pairs nearest
      the targets instead of 1, two of them by a walk. A walk from the rest itself (a probe:
      every one- and two-frame stick from the config's exact rest, 23,352 walks) moves
      Mario 0.37 units at the least on its first frame and comes to 14,289 distinct rests,
      none an exact position within 6 units: a re-roll of the staircase, not a nudge. The
      second way, the dive slide decelerating to a stop (a probe: every stick on the dive's
      last air frame lands in the slide, 89% come to idle on the pyramid in 50 to 55
      frames, and no two landings rest the same), was built as a mode of the search, the
      landing's velocity folded into an effective landing through the measured
      d(rest)/d(velocity) of 5 to 7 units per unit of speed, and taken out the same day on
      three measurements: the slide stops only under about 18 speed before the platform
      tilts past 15 degrees (`mario_floor_is_slope`), and the dive lands at 21 from the run,
      so the aim reaches a few units and of nine slide searches one came within a unit, the
      rest ending 7 to 59 units off or with a slide that did not stop; the velocity's share
      spreads the stick combinations some twenty times thinner over the effective landing,
      so a window that holds three candidates for a rollout held none for the slide even at
      2,000 sticks a frame; and where it had converged, plays whose effective landings
      differ by under 0.001 units rested 0.03 apart, hundreds to thousands of ULPs, the
      slide being chaotic at the float scale (the scan with it: 8 of 24, every one by a
      rollout). What the attempt left behind is the rollout's: the aim's stick was never
      what was asked, since `GetClosestInputByYawExact` and `...Hau` settle on one ray of
      raw sticks and its few magnitudes (straight back, the full stick whatever the
      magnitude, so the Newton's forward probe did nothing; docs/tasing.md), and `Stick`
      now picks the stick whose effect on the air movement is nearest the aim from a
      yaw-sorted table of the distinct sticks (`StickEffects`); the constant stick is aimed
      at the rest itself, a Newton with halved steps on the rollout and its settle, in place
      of the landing Newton and the response-driven attempts after it; and a way whose aim
      ends more than `NearAim` (4 units) off is given up before the fine frames. With these
      10 of the 24 pairs solve (851 s in all), the exact config's cost is
      unchanged at 77,000 frame advances and ±100's falls from 130,900 to 76,600
      (docs/performance-changelog.md).
      The maintainer notes (2026-10-05) that the oscillations need not start from a rest.
      What a rest does for the error is the pyramid's snap: `approach_by_increment` sets an
      axis's normal to the goal exactly whenever the goal is within 0.01 of it, and the error
      on that axis is the goal's float on the last such frame; from there the chain steps
      while the goal outruns the normal (the dr stage's conservation rule). A rest is the
      case where both axes snap at one position, and that position is the settle's, passive
      and rounded onto the staircase. A run sets them one axis at a time at positions of
      Mario's own: along z near the config's rest the x goal moves 0.0011 a frame at 8 speed,
      so the x normal tracks it, snapping every frame, while the z goal moves 0.0137 and z
      steps; the x error is then the goal's float on the last frame before the turn toward
      x, and the z error the same on the last frame of the run along x. The condition on
      each axis is one float curve of positions (every x float has a z float or two on it)
      in place of the lattice's one or two points per pair within the band, and no settle
      lies between the stick and the position. The arithmetic filter (13%) stays; the
      geometry and the staircase go. Not designed: the fine-frame search on ground movement
      (the walking speed, the slope, the carry), the frames at which each axis's tracking
      ends, the parity and the tilt at the hand-over, and the dr stage's start from a running
      state in place of its leg from idle.
      Measured the same day (a probe from the config's rest, a run of fourteen frames along
      one axis then the other, not committed): the tracking axis snaps every frame to the
      goal of Mario's position on the frame before (the pyramid reads his object's position,
      a frame behind his state), the other steps by exactly 0.01; along z from the rest at 8
      to 12 speed the x goal moves 0.001 to 0.003 a frame and the z goal 0.018 to 0.026;
      turning to x, the x normal still snaps for the first three frames while the speed
      along x builds and steps from the fourth, so the x error is the goal's float from the
      position at the end of the turn's second frame; along x first it is z that tracks, at
      0.001 to 0.005 a frame, and the run leaves the platform at 350 units from the home on
      the twelfth frame, the speed having built from 8 to 25, so the setting runs are short
      or slow. Built the same day, at the maintainer's word ("the old fixer fixed one axis
      at a time. Probably the way to go"; the old tilt-target chain did, but through an
      equilibrium each): the rest sets x alone (`SolvedRest`: the error within the
      tolerance, reversible, in the corner, near the rest asked for; the landing search
      aims each play's rest at the x curve's point nearest it, `CurvePoint`, the rest's x
      with its z moved by the error over the goal's slope in z), so the lattice of
      positions, `Rests`, its near-rest filter and the walk variants' reason go, and the
      walks stay as another staircase per way; then the turn sets z (`SearchTurn`): from
      the rest a run of two or three fine frames along x in the leg's direction (one more
      when x's step parity at the turn needs flipping), then the full stick at the chord's
      yaw until z has stepped twice after its last snap; the fine frames' sticks are those
      within the one-frame turn limit of the facing and at a magnitude above the speed, so
      every combination shares one speed and each stick turns Mario to its own yaw, and
      their effects on the position that sets z are measured end to end through the turn
      (`TurnLattice`, 240 to 340 distinct a frame), the combinations' errors predicted by
      the goal's slope (the error is the target less the value, so a movement that raises
      the goal lowers it), those within 64 ULPs of slack computed exactly on the predicted
      floats, which carries the goal's curvature (hundreds of ULPs over a movement of a few
      units, so the exact error is the rank and not a cut), and the nearest played
      (`MaxTurnPlays` 500, a play eight frames or so), re-ranked every twenty plays by the
      median of the plays' offsets from their predictions, since the actual errors scatter
      some hundred ULPs about a bias (the platform's carry, in the game's float matrices),
      the rounds closing in from the nearest played; any play that sets z, a lattice's as
      well as a candidate's, is kept (`_solvedTurn`), and a round whose nearest play set z
      without being accepted ends the search, the tilt, the corner or the band being the
      reason. The hand-over (`handoverFrame`, the dr stage's `equilibriumFrame` by its
      `input:handoverFrame`) is the last frame z snapped on, two frames of the chord
      verified stepping after it; `SolvedTurn` requires both errors, reversible, the
      parities equal, the corner, z the steeper axis (so the leg the dr stage runs from the
      hand-over, x toward the corner and z away, is the run's and the turn's own
      directions), the tilt floor and the diagonal band. The ask places the hand-over on
      the diagonal at the tilt floor plus 0.06 and 20 units toward z (`ZMargin`), since the
      run's drift takes 0.02 (z tracks a goal that shrinks as Mario runs out) and z must
      stay the steeper axis, and the rest 50 units inward along x (`RunShift`, the run's
      length); the step parities at the turn are reckoned from the ask's goals (x's flips
      once a frame to the turn) and, when they would differ, the rest is asked one x band
      toward the home (`ParityAsked`), the one extra run frame covering a turn a frame off
      the reckoning: extra frames alone ran the hand-over out of the band, and the drift and
      the band ate a 0.04 margin. The dr stage needed no change: it accepts a walking start,
      reads its rest's normal from the frame it is given, and conserves the error from that
      frame plus one. Measured on the config's targets: the rest at frame 3332, x exact, the
      hand-over at 3337, both exact, 98,549 frame advances with the cost model on and 2 s
      (433,568 through the test harness, which replays every load from the stage's start:
      the turn search's plays are short and many); the dr stage from it went through at the
      first try, the leg in 30 shots, oscillations 1 to 4 in 73, 17, 85 and 49, and 394
      solutions at oscillation 5 in 2,000, 31 s; ±100 79,282 advances. Of the 24 survivor
      pairs nearest the targets, 24 hand over (10 by the rest alone, before this), in
      117 s. The plays' bias was found by the traces: predicted exact, they landed 100 to
      300 ULPs under, fourteen of fifteen on one side, which is what the re-ranking by the
      median corrects. The config's other stages (dr, tilt-target, osc-final) now carry the
      fixer's targets too, the setup's normal being one value, which the every-corner test
      mirrors from the fixer's stage.
      The decomp's `bhv_tilting_inverted_pyramid_loop` does not reproduce the settle (it
      carries Mario's height along where the game resets him to the floor each frame; 1.8
      units off), so which positions a landing can reach is not predicted, only found by
      play.
- [ ] **4.9 Defects in the stage scripts.** Found 2026-09-15 writing 3.17; docs/tasing.md
      lists them as not to copy. `Scattershot_BitfsDrRecover::IsSolution` reads its
      previous state from the current frame (`prevState`,
      Scattershot_BitfsDrRecover.hpp:657), so the `c-up-trick` phase's `ACT_DECELERATING`
      check compares a frame with itself. `Scattershot_BitfsDrApproach` offers
      `CustomMoves::C_UP_TRICK` in its C-up phase and `ApplyMovement` never dispatches it,
      so `CUpTrick()` there is dead and the phase runs on random inputs. `CustomMoves::REWIND`
      is checked by all three searches' `ApplyMovement` and offered by none of their
      `SelectMovementOptions`. `BitFsPyramidOscillation_TurnThenRunDownhill.cpp:150` reseeds
      the fine sweep's uphill half from the coarse midpoint (`midHau`) rather than the
      coarse winner (`midHau2`), which may or may not be intended. None is fixed here: each
      changes what a stage searches, so each needs that stage's counts before and after,
      and the dive-recover chain the first two belong to has never run to completion (4.1).
      Tier C is untouched either way: since 2026-09-21 it runs on frozen copies of the
      scripts (`tasfw-perf/workloads/`). Found 2026-09-21 building 4.8:
      `Scattershot_BitfsDr::Pbd` and `BitFsScApproach_AttemptDr` expect `ACT_DIVE_SLIDE` on
      the frame after the dive's B press, but a dive from a run is airborne for about ten
      frames first (the game gives it 20 of vertical speed), so neither move ever applies;
      `Pbd` has weight 0 in every phase anyway.
- [x] **4.10 The viewer's cost, gated.** Done 2026-09-19. The suite proved the search's side
      of 4.4 costs nothing (its Tier D runs had no viewer), and CI's `--once` that the viewer
      draws; nothing measured the viewer while it tailed a live run, which is the cost the
      maintainer cares about (2026-09-19: no compute taken from the brute forcer). Now the
      suite runs both Tier D workloads a second time with the viewer tailing them headless
      and gates the viewer's CPU, its longest redraw, the run's wall against the plain run
      and the deterministic counts (docs/performance.md, "Tier D"); two runs the same evening
      (performance-changelog.md): the viewer at 1.7% of one core on the deterministic
      workload and 4.1% then 5.5% on the throughput one, redraws of 118 and 274 ms at most,
      the walls within 2% of the plain runs and the deterministic counts identical to the
      frame advance. The window shows the same cost next to its refresh rate, its own CPU
      over the last 30 seconds (the maintainer, 2026-09-19: the measured figure alone, no
      estimate, since the process clock resolves to 0.05% of one core over the window). Done
      as planned: the viewer has a headless follow mode
      (`TASFW_VISUALIZER_HEADLESS=1`, so the search's launch command needs nothing new): the
      same polling loop and newest-per-bin table, the Agg backend redrawing on the same
      schedule with no window, one PNG at the end and a summary JSON beside the parameters
      file with its CPU seconds, ticks, rows, redraws and the longest redraw; and the perf
      suite runs the throughput workload once more with a CSV every tenth novel block and
      the viewer attached headless, interleaved with the plain run like the reference and
      current binaries are, gating the viewer's CPU at 10% of one core over the run (the
      maintainer's choice, 2026-09-19, over the 5% first planned: the viewer stretches its
      interval so that a redraw is at most 5% of the wait before it, and parsing the rows
      and starting the interpreter are the rest, a third of a one-minute run's share) and
      its longest redraw at one second, the run's wall against the plain run under the
      existing 10% gate, and, on the deterministic workload with the viewer attached, every
      count identical. docs/performance.md gets the row and the gates.

## Phase 5: toward a game- and console-agnostic framework

Not scheduled. Listed so decisions in earlier phases do not paint us into a corner.

- `Resource::getCurrentFrame` and the `gControllerPads` write in `Script::SetInputs` are SM64-specific; they belong in the resource.
- `Inputs`/`M64` assume an N64 controller and the Mupen m64 format; console-agnosticism
  means the controller and movie format become properties of the resource or console, and
  the frame stays as the resource's indexable step (ARCHITECTURE.md, "Resource and savestates").
- A second resource (another libsm64 build or an emulator core) is the real test of the abstraction.
- **One declared source for a game's vocabulary.** How the game's structs, constants and
  reimplemented logic enter the repository today is copying: headers copied by hand at
  different decomp revisions, the physics reimplemented twice, and the decomp pin and the
  DWARF table as after-the-fact checks (docs/decomp.md). Haphazard by the maintainer's own
  account (2026-09-13); it stays because it is verified. When the framework takes a second
  game, decide this once: a game module derived from a declared source (a DWARF layout, a
  decomp revision), or an access contract that needs no copies (the typed `ReadState`
  Phase 5 owes, below), and the copies go.
- **Generators for new scripts and resources.** The end user (AGENTS.md, "Who it is for")
  should be able to start a script or a resource from a working skeleton and tweak it,
  rather than from the templates' declarations: a script class with its
  `CustomScriptStatus` and the three lifecycle methods, a scattershot thread with the
  overrides it must provide, a resource with `save`, `load`, `advance`, `setInputs`,
  `addr`, `getStateSize` and `getCurrentFrame` over a state type, each placed in the
  right directory and added to its CMake target so it builds at once. Cross-platform is a
  requirement (the maintainer, 2026-09-14); the repository's tooling of that kind is
  already Python (`scripts/unlock_libsm64.py`, `perf_compare.py`, the DLL scripts), so a
  `scripts/new_script.py` and `new_resource.py` are the natural shape, with the skeletons
  kept as templates the generator fills in, not as strings in the script.
- **A cancellation handler that exports solutions** (from 4.2's close, 2026-09-19). An
  interrupted stage writes nothing: solutions are saved when the stage returns
  (`main.cpp`), and nothing handles Ctrl+C, so a million-shot stage killed with 900
  solutions in memory loses them all. The design as discussed: a stop request on
  `Scattershot`, an atomic flag each thread reads in the per-shot stop check it already
  makes beside the shot and solution counts (in deterministic mode inside the queued turn,
  so every thread stops at the same round); the pipeline sets it from a SIGINT handler,
  the stage returns what it has, `Save` runs as it does now, the `dr` stage's pass loop
  checks the same flag before its next pass, and the pipeline exits non-zero rather than
  run the next stage on a partial set. One relaxed atomic load per shot; the handler is
  optional and the pipeline's only. A rule 10 change to the search's surface, agreed before
  it is written; a continuation is then a stage entry with `input` set to the stopped one.
- **Per-scenario movement options** (done 2026-09-14: `CustomMoves`, on C++23).
  `BasicMoves` (then `MovementOption`) was one enum for every scenario's scripted moves plus the framework's
  three input groups (stick magnitude, direction, buttons, which `RandomInputs` reads), a
  compromise the maintainer would rather not keep as scenarios add moves. The script
  author's side had to stay plain, `AddRandomMovementOption({{X, 4}, {Y, 1}})`, and no
  C++20 shape managed it: a member function template deducing the enum from that call
  fails (the inner braces are a non-deduced context for `std::pair<T, double>`, the snag
  the first attempt hit), a fifth thread template parameter threads the enum through every
  type that names the thread, and a deducible entry type or an id with a converting
  constructor costs a word or a concept per entry. C++23's explicit object parameter is
  the missing piece: the three calls have an overload taking `this Self&`, constrained on
  `Self::CustomMoves` being an enum, so the enum is known from the object before
  the braced list is considered and nothing is deduced from it. A search declares
  a public `enum class CustomMoves` nested in its class, the magic name the way
  `CustomScriptStatus` is one (and public like it), and writes today's syntax; a foreign enum does not compile;
  the framework's input groups stay in `BasicMoves` through the same calls. The
  selected options are a second bit vector, and the draw is the 3.8 list walk over either
  enum (`DrawOption`). The cost is the toolchain floor (GCC 14, Clang 18, MSVC 17.2; CI
  moved to GCC 14 and Clang 18), none at run time: the deterministic Tier D and dr counts
  are identical (docs/performance-changelog.md), and `test_scattershot_mock.cpp` runs a
  search on its own enum and pins the typing with static assertions. The five searches
  moved their moves out of `BasicMoves` the same day, each `CustomMoves` listing them in the
  order they had, so every draw walks the same order and every count is identical
  (docs/performance-changelog.md); `BasicMoves` is the three input groups only.
- **Hacks as a kind of input.** Today a direct write into game memory (`//! UNSAFE`) cannot
  be replayed from a savestate, which is why hard rule 1 forbids it. The intent is to make
  hacks a special input type the framework applies at a frame like any other input, so they
  are part of the diff, replayed on decode and reverted with the sandbox. Design not yet
  discussed; nothing should assume inputs are only controller states. The write side of the
  access contract goes with it, `HackMemory` beside `ReadState`, and `ReadState`'s own
  remainder from 3.2: reads typed by symbol, const, and guarded against invalid access (the
  maintainer, 2026-09-15).
- **A savestate policy the run chooses** (3.11, tabled here 2026-09-13). What the design
  discussion settled before tabling it:
  - The policy is a property of the scenario, not of the resource type. It is configured
    per run at the top-level script, `UseSavePolicy()` on `TopLevelScriptBuilder` and the
    scattershot builders, with the agnostic policy and today's parameters when nothing is
    said; the resource only supplies what it measured (`ResourceWork`). Not a template
    parameter of `TopLevelScript` (that welds the rule to the script class, so one workload
    under two policies is two instantiations and the pipeline cannot choose from
    config.json) and not one of `Script` (every reusable script would carry the scenario's
    rule): the run holds the policy and the root keeps a type-erased view of it.
  - The per-frame decision stays static. The root caches the policy's terms and refreshes
    them through the view at cold points only (the start of the run, after a save, after a
    load); the replay loop compares a counter with a cached number, no call.
  - Two policies were named. *Agnostic*, today's: it assumes nothing about the search and
    saves once a replayed stretch has proven hot, its thresholds derived from the measured
    save, load and advance cost (a save when the stretch's replay history has cost one
    save, a jump to a save ahead when the frames it skips cost one load), with given
    thresholds as an option of the same policy: a never case replaces `costModel: false`,
    and a given threshold makes automatic saves timing-independent, which is what an exact
    gate or a test needs. *Interval*: save whenever the cursor lands on a multiple of N
    unless a save already exists there, keep at most M, evicting the earliest frame's
    (the slot manager would have to learn frames for that; it orders by touch and by
    creation today); replays never earn a save. A separate "fixed thresholds" policy was
    rejected as too similar to the agnostic one to be a type.
  - A preemptive policy built from a profile of a test run was considered and judged
    harder than it looks: in a scattershot a frame's replay heat says little about a save's
    worth there, since the next rewind-and-write invalidates the save before a load can
    use it, so a profile would have to measure what a save served before it died, not how
    often its frame was passed.
  - Why it is tabled: the engine's per-level bookkeeping is not policy-neutral. The diff,
    the save bank, the two lookup caches and the load tracker serve any policy (they are
    how a save is found, owned and invalidated), but `frameCounter`, its increment per
    replayed frame, its erasure at every write site, the state-owner accounting that
    says whose counter is charged, and `OptionalSave` are the agnostic model's own state,
    which an interval policy would carry for nothing. A real abstraction lets a policy
    own its state and has the engine report events to it (a frame replayed under an
    owner, writes erased from a frame, a level popped), with that state following the
    script hierarchy the way `LevelStack` does. That is the same shape question as 3.2 and
    the console-agnostic resource above, decided once, with a second workload in hand.
    Until then `useCostModel` stays on the resource and the pipeline sets it there.
