# Roadmap

Status as of 2026-09-15: Phases 1 to 3 are done (Phase 3's last item was 3.17, the
guidelines for TASing with the framework, docs/tasing.md; the rest landed in #94, #95, #97
and #98 and 3.2's access contract last; the latent defect in the bare scattershot builder
found while writing 3.17 is 4.15); Phase 4 has 4.1 and 4.5
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
item is done; then Phase 4.

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
      `PyramidUpdateMem` now converts from (`const Resource<Sm64Mem>&`), so the 23 sites
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
      more than a frame advance. Writing it found 4.15 and 4.9. Added
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
      with configurations (the maintainer, 2026-09-19). For the ending's debugging loop
      (2026-10-06) the mechanism gained `start`, a top-level config option and `--start`:
      a full run begins at the stage named, from the files, or at an oscillation pass of
      the `dr` stage, from the roots each pass now writes to
      `<stage>.oscillation<n>.json` as it ends, and fails when the file it needs is missing. What that loses is the non-solution
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
      Since 2026-10-06 (the maintainer's ask, for the ending's debugging) a categorical
      column is text where text makes sense: the stages write Mario's action in hex with
      its decomp name in parentheses (`Sm64ActionNames.hpp`, generated from the copied
      `Sm64.hpp` by `scripts/action_names.py`; CI's `--check` keeps it current), their
      phases and approaches by name and the ending's flags as true or false, and the viewer
      reads a hex cell as a number shown as written, the name left out of the comparison,
      and, in a categorical column only, a text as a value shown and filtered as written
      (`Phase in SWING AIR`); CI pins it on a second sample (`scattershot_text_sample.csv`),
      and `--once --csv` takes `--categorical`. Since 2026-10-13 (the maintainer's ask) a
      value's count follows the filter on every other column, as a shop's facets do, and a
      value stays listed at 0 when that filter passes none of its rows; the ranges still
      cover every row. CI pins the counts on the text sample; what the change costs is in
      docs/performance-changelog.md.

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
      crossings before. The maintainer's intent (2026-10-06) is that the swings in the
      regime build speed, not tilt; the sum had still grown 0.02 an oscillation there, so
      the rule had never applied in the regime (seven swings handed over at 16 to 21 where
      three had at 23 to 24). Applying it there whatever the sum did, to every crossing once
      the regime was reached, was built and measured the same day, from the same pass-1
      roots:
      no lineage above 0.652, the last 0.02 step under the regime floor, survived the
      third, fourth or fifth pass (none among their sampled blocks), the carried roots sat
      at 0.652 from the third pass on, and the fifth pass found nothing in 2,000 shots
      where the third, in the same direction at the same tilt, had found its hundred in
      32 (and nothing again from the same roots with the end-clearance rule off): there the
      crossing met the one two before it at the same tilt, the rule's original case, and the
      speed rule is the one rule that differs between the two passes. A second run on the
      knob died at its sixth pass, its hand-over speeds falling 25.8, 23.5, 20.7, 19.2 at
      0.652, since what survives the comparison is the lineage whose earlier crossing was
      slow enough to beat. The crossing speed is the swing's own, from a turnaround to a
      stop down the chord: 24 to 26 at the second pass, 22 to 24 at the third, 16 to 21 at
      the seventh as the tilt grows and the far end sinks, whatever came before; so a
      crossing does not beat the one two before it, and the stage has only ever survived
      the rule by growing the tilt every oscillation, which exempted every crossing. The
      run before the change went through all seven passes, the tilt 0.672 to 0.732. The
      maintainer's form (the same day): the rule compares two crossings in the regime
      whatever the tilt did (`regimeSpeedRatchet`, true in the config), two below it at the
      same tilt as before, and never a crossing in the regime with one before it; each
      crossing records whether the regime had been reached (`CrossingDto::inRegime`), and
      `test_bitfs_dr_rules.cpp` pins the cases. Since 2026-10-10 the rule applies below the
      regime and in it alike, whatever the tilt did, never across the regime's edge, and the
      knob is gone (4.12). Measured twice from the same pass-1
      roots, 2,000 shots a pass: passes 2 and 3 full (hand-overs 24.6 to 26.5 at 0.652 to
      0.672, then 23 to 24.5 at 0.692), the fourth 3 and 100 (25.3 to 25.7 at 0.732; 23.2 to
      23.9 at 0.672 to 0.712), the fifth nothing both times, so the fall-back handed the
      third pass's roots to the ending, which found nothing in 50,000 shots. The crossings
      the regime form compares are the ones the chord bounds, so it starves where it
      applies, as the first form did; the rule's own shape is the open question. The fifth
      pass of the second run, instrumented: 6,010 fifth-crossing states in 2,000 shots,
      every one compared (both crossings in the regime), none faster than the third
      crossing's 24.15 (the fastest 24.09, the median 19.5; the downhill-peak condition was
      met by 1,015 of the 1,225 states it was computed for, so the speed condition alone
      refused them all). The ten roots all descended from one third-crossing ancestor
      (keepTop keeps the ten fastest, which were its subtree), so the bar was one number.
      The speed was the same at every tilt (medians 19.2 to 19.6 from 0.672 to 0.732), so
      the tilt's growth is not what slowed the crossing; the crossing speeds fall along a
      lineage (24.6, 24.2, 20.8 to 23.9, then a median of 19.5), the cause unmeasured. Found
      on the way and fixed (the maintainer's go, 2026-10-06): the downhill-peak prediction's
      early-out compared the crossing with the one before it where the rule compares with the
      one two before, so a crossing slower than the opposite direction's last had no peak and,
      when the rule applied, failed its second condition for that alone; it now skips the
      prediction only when the speed condition itself fails. The example movies' oscillation,
      measured the same day against ours (the ctypes driver, outside the framework; all eight
      pyramid movies share one oscillation, mirrored): the same chord, across the low corner
      between its two edges, the same turn (three frames of turning around down to 2 to 4) and
      the same gain after it (2.0 to 2.2 a frame in both), but the example keeps its tilt sum
      at 0.705 to 0.727 through all eight swings, the two components stepping opposite ways on
      every frame (the normal rotates between (0.47, 0.26) and (0.20, 0.51)), so its swings
      stay 30 to 32 frames and its turn points 50 to 127 above the lava, and the crossing
      speed climbs 18, 24.5, 26.6, 29.7, 30.9, 31.0 and holds near 30 into the final run;
      ours grows the sum 0.61 to 0.75 over as many swings, on the seven frames where both
      components step outward together, so the swings shorten 27 to 19 frames, the -x end
      sinks from 38 to 24 above the lava and the crossing speed falls 26 to 23. The speed the
      ending needs is the example's constant-tilt oscillation at the regime's sum, not more
      tilt. Why the example is faster at the same tilt (measured the same day): the crossing
      comes when Mario's target normal, normalize(dx, 500, dz), meets the lagging normal, and
      the speed at it is the turn's 2 to 4 plus about 2 a frame since the turn. The example's
      normal rides 0.19 to 0.31 of sum above the targets its turn points ask for (turns at
      280 and 296 from the home, inside the edges, where the target sum is 0.40 to 0.52, the
      normal at 0.71), so at every turn the normal is 0.27 to 0.41 behind on the axis of the
      next run, the crossing comes 13 to 16 frames after the turn, and the sum never moves,
      the targets being inside the normal on one axis and outside on the other on every
      frame. Ours turns 30 to 50 units farther out, at 319 to 344, past the edges on the
      side faces, where the target sum is 0.57 to 0.70 against a normal at 0.63 to 0.73: a
      margin of 0.02 to 0.09, the normal all but caught up at every turn, the crossing 8 to
      12 frames after it, 6 to 8 of speed short of the example, and the chord's middle pulls
      both components outward on the frames that grow the sum. Measured against the low
      corner's diagonal: the two chords are equally wide across it (about 220 to 240 either
      side), but the example's turn points lie 123 to 216 from the center along it and move
      inward swing by swing, ours 217 to 291 and moving outward; its crossings come 67 to 106
      off the diagonal, ours 25 to 78. The example begins each turnaround while the slope
      under Mario is still steeply uphill (0.3 to 0.47 of the unit normal along his motion),
      at 23 to 28 with the floor still rising under him; ours begins it when that slope has
      flattened to 0.0 to 0.1, the crest, which is the moment the normal has caught up with
      him on the run's axis and the lag is spent. Moving farther out buys tilt (the sum grows
      only on frames where Mario is outside the normal on both axes) and spends lag; the
      example separates the two in time, the rest setting the tilt and the swings turning
      inside, where the stage mixes them: its swings must carry the sum from the fixer's
      0.55 to the regime, and a pass keeps the fastest crossing of one oscillation, where
      turning inside pays only two oscillations later. Within the regime alone (the
      maintainer's question; our swings from the first turn at 0.672, six pass-7 lineages,
      all one ancestor's): the turns are alike in speed (22 to 27), length (4 to 6 frames)
      and drift (23 to 51), and differ only in where they happen: the example's begin on a
      slope of 0.28 to 0.47 uphill and end 84 to 127 above the lava where the position asks
      for a sum of 0.40 to 0.52 against a normal at 0.71; ours begin at 0.01 to 0.22, the
      crest, and end 18 to 54 above the lava where the position asks for 0.60 to 0.71
      against a normal at 0.67 to 0.73, which is also why our sum keeps growing 0.02 a swing
      in the regime. The speed is in the lag, and the lag
      is in turning inside the edges with the sum already at the regime's. The maintainer's
      first answer (2026-10-06): a pass keeps its `keepTop` fastest per 0.01 by 0.01 cell of
      the normal instead of per pass, so every tilt a pass reached is carried and the lineages
      whose tilt went another way get their later swings; built the same day, the hand-over
      line printing the cells (the line stays; the keep went back to the pass's fastest on
      2026-10-07 with 4.12's design). Measured from the same pass-1 roots at 1,000 solutions a pass,
      with the regime speed rule on and off: the hand-over carries 380 to 540 roots in 53 to
      92 cells, the stage goes through all seven passes either way (the regime rule no
      longer starves it, enough lineages beating their earlier crossing), in 170 to 190
      seconds where ten roots took 25 to 45 (the ending's result in these scratch runs is
      void: their configs carried `targetEdge` x, under which the edge-side rule refused
      every hand-over state at its first frame, found 2026-10-08). But the
      speed does not climb: the fastest root falls 29.7 and 30.0 at the second pass, 27 to
      28 at the third, 26 to 27, 25.7, 25.8, then 24.3 at the seventh, and the cells with a
      root at 25 or more go 43 to 52, then 0 by the seventh. Balanced cells, (-0.35, 0.34),
      are carried at every pass and are no faster. The fastest and the most balanced
      seventh-pass lineages replayed: margin 0.02 to 0.07 at every regime turn, the turn
      ending 20 to 50 above the lava, the crossing 8 to 11 frames after it, 20 to 25.7. So
      the cells keep the tilts the search reaches, and none of them is a lineage that turns
      with the normal behind: the search does not produce one, and the lever is on the
      move side, the turnaround taken up the slope as the example takes it, 6 to 8 frames
      into a climb of 0.3 to 0.47, where ours comes at the crest; the dilemma is 4.12's.
      The first-oscillation test under the working copy's cap of 1,000 (the committed
      config's is 100) finds 913 in corner 4 within its 2,000 shots, the same 913 with the
      per-pass keep and with the per-cell one (the leg hands the same roots over either
      way), so the shortfall is the cap's, not the selection's; the other corners fill
      1,000. With
      the rule's original form seeds 7 and 8 run ten oscillations (701 and 80 last-pass
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
      the first oscillation's cause is the open item. The other failure, a pass with
      no solutions in the middle of the stage (oscillation 3 of a run of 2026-10-06, and
      oscillation 7 of the run before it with `maxOscillations` 7), has its mechanism
      measured: the roots hand over just past the crossing heading for the -x end at 23
      to 26 speed, 10 to 22 units above the lava, with the x tilt already the larger
      component (0.37 to 0.43 against a z of 0.27 to 0.32) and growing as the normal
      follows Mario; the -x end of the chord is then 0 to 3 units above the lava when he
      arrives and sinks about 3.6 a frame, so the three frames of a turnaround put him on
      the lava (the pass's CSV has every lineage's last frames at 0 to 10 above the lava
      at the end, and three roots replayed by hand lava-boost on the turnaround), while a
      run toward that end is downhill, so the turnaround rule has no uphill frame to begin
      from either. The pass that succeeded turned there 23 to 75 above the lava with an x
      tilt of 0.31 to 0.34. The selection brings it on: `keepTop` keeps the fastest at the
      crossing, and speed there correlates with the x tilt (0.53 over the 16 hand-over
      candidates of the run's second pass, the fastest at 0.40 to 0.43 and 11 to 22
      above the lava, the slowest at 0.29 to 0.34 and 40 to 63 above), so the ten carried
      are the ten most likely to drown at the next end, and the pass dies when all ten
      are. Done the same day as a root rule, `minEndClearance` (20 in the committed config):
      a solution of a pass with a pass after it counts only if a turnaround begun from its
      frame would end that high above the lava, Mario running on to a stop at the game's
      4.8 a frame, the normal stepping toward him meanwhile and the face's height read
      from the collision data under the unit normal (`BitfsDrMetrics::TurnaroundClearance`,
      game-free; `test_bitfs_dr_rules.cpp` holds it to the recorded hand-overs: the three
      roots replayed at -20 to +3, the slower candidates at 33 to 59). It is the precise
      form of the `maxXzSum` ceiling the config turned off: the sum was the wrong proxy
      and starved later passes, where the floor under the turnaround is what fails, and
      the ranking among the roots that pass stays speed. A chord-end form was tried first
      and dropped: the fast roots ran 30 to 50 units past the measured end, so a fixed
      end put the dead roots at 15 to 21. Under the rule the same run, restarted at its
      second pass from the same roots, went through: 100 solutions at every pass to the
      sixth and 40 at the seventh, the carried roots at 0.672 of tilt where the dead
      ones had been at 0.692 to 0.712, in 42 seconds. And a pass under the speed rule that
      yields nothing no longer ends the run: the stage hands on the roots carried from the
      last earlier pass whose crossing heads toward the squish-cancel quadrant, held to the
      last pass's rules (`handOverFallback`, the maintainer's design of 2026-10-06, kept on
      2026-10-10 over two forms tried between, 4.12; the leg test's third part pins it with
      a pass made to refuse everything). What is known: the pass either
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
      fixer rests and the first oscillation's pass finds at least 200 solutions within
      2,000 shots, one thread, deterministic; the stage's per-pass yield (`passSolutions`
      on the solution set) is what it reads. The bar was the pass's own cap until
      2026-10-12, when the cap went to 1,000 for the pool's variety and the corners found
      768, 530, 1,000 and 772 within the budget; the maintainer set the bar at a fixed 200
      ("530 is still plenty" for the ten the next pass carries). The case reads the dr
      stage's pool cap and `keepTop` from the config it is given, so its yields follow
      them: under a working-copy config with the pool at 10,000 and `keepTop` 100 the
      same build found 768, 606, 425 and 1,047, and passed. The probe behind it: the fixer rests in corners 1 to 3 as in 4
      (frames 3325 to 3329) and their first oscillations came in 155, 214 and 142 shots on
      16 threads. After it, the climb: the sum rises about 0.02 an oscillation and the
      fifth pass works at the lava's limit. The fixer is at its limit (rests at 0.65 and
      above drown the first swing, off the diagonal it has no first frame).
      The maintainer (2026-10-13), for when the ending is settled: perhaps the brute forcer
      run separately on every error pair the fixer can reach and every relevant sum and
      difference of the normal. The scale, from 4.8's count and the corner resource's
      timings (the fixer 0.3 s on one thread; the dr stage about 84 s and 50,000 ending
      shots about 15 s on 16 threads): a setup has 28,147,276,212 pairs, and a run for
      each, at least 100 s, comes to some 90,000 years; the fixer alone, run 16 at a time,
      to about 17 years. The sum and difference pick a cell, one 0.01 step on each axis,
      and every cell holds every pair exactly once, as one float. So the cells are what
      can be enumerated: a setup has at most about 2,300 (48 steps on each axis over the
      oscillations' 0.13 to 0.61), so at most about 37,000 runs for the 16 setups, some 40
      days at 100 s, and fewer cells are relevant. The untargeted ending already records
      each squish cancel's exact normal (`probeNormX`, `probeNormZ`), and a targeted run
      (4.8) is the way to an exact float in a working cell. Open, to measure first once
      the ending is stable, with a few hundred pairs run on one working cell: whether the
      pair changes whether a squish cancel happens there. Within a cell a pair moves the
      normal at most 0.005 on an axis, under 2 units of the squish edge's height (the
      top's half-width is 307) against the catch window's 20.
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
      The regime, measured 2026-10-06 (the maintainer: "it would be good to lock down what
      that is") and locked by the regime case of `bitfs-turn --test`, a line per case
      (README, the are-fixer bullet, states it): every corner at the floor 0.5, exact and
      within 100; within 100 at 0.4 and 0.6 everywhere, exact there at 0.4 in corners 3 and
      4 and at 0.6 in corners 1, 3 and 4; from the run every corner, from the dive its own
      corner, from the slide corner 4; a hand-over named; two fine frames within 100; the
      former normal at tolerance 1. Found measuring it, and done: from the dive the fixer
      held the remaining air frames' stick straight back and reached no rest near a corner-4
      ask (its rests 45 to 190 units off, all in corner 3); it now varies that stick as from
      the run, 12 ways for 4, and reaches corner 3 exact from 3262 in 183,358 frame advances
      (corner 4 stays out of the dive's reach: its yaw is the movie's). And a landing round
      that rested where the round before did replayed itself to the round budget, the
      nearest prediction for the moved target being the play itself; such a round now ends
      the way (the config's stage 98,549 to 98,540 advances, docs/performance-changelog.md).
      Found and left, the boundary: at the far corners' floor ends the ways rest within 1
      to 4 units of the ask with x errors of 100,000 to 300,000 ULPs and the fine frames'
      plays jump the rest units for a landing moved a fraction (corner 2 at 0.4 and 0.6
      finds no rest on the x curve), and corner 1 at 0.4 exact leaves z one ULP off after
      the five turn rounds, the goal's slope steeper near the home; within 100 ULPs every
      corner hands over at every floor. Two fine frames find no rest at an exact match: a
      frame's 500 sticks give 250,000 combinations where three give 125 million, and the
      landing's cell is the prediction's noise, 0.0005 by 0.0003 units; within 100 they
      hand over (237,091 advances). A validation refusing two frames at an exact match
      would be a rule from one measurement; the header's comment says it instead.
      Counted 2026-10-13, at the maintainer's question how many error pairs the fixer can
      reach (a compiled copy of `StepsReversibly` and `AdjustedRemainderError`, not
      committed): each float within 0.005 of a target is one possible error on that axis,
      and for the config's targets 167,772 of x's 671,089 survive and 167,771 of z's
      335,545, so 28,147,276,212 of the 225,180,558,505 pairs: exactly an eighth, the 13%
      above rounded. Within ±100 of the targets, 5,151 of the 40,401 pairs survive. The
      count does not depend on the target; only the share does. The oscillations carry both
      axes above 0.5, where the floats are coarsest and one 0.01 step spans 167,772 of them,
      so about 167,772 chains survive on an axis wherever its target lies. Below 0.5 the
      floats are finer and the same chains are spread over more of them: half of the
      possible errors of a target between 0.25 and 0.5, a quarter of one between 0.125 and
      0.25, all of one above 0.5 (targets drawn across each of those ranges gave the same
      counts). z loses one chain more: the chain that touches both ends of its target's
      band, whose error depends on the side it is walked from, which the walk refuses. So
      the survey's 13% of float pairs, about 8.4 × 10^12 of them, is the same 2.8 × 10^10
      chain pairs 300 times over (a chain has 12 values of x and 25 of z in that region).
      That is what the arithmetic allows; the fixer itself has been run on the 24 above.
- [ ] **4.9 Defects in the stage scripts.** Found 2026-09-15 writing 3.17; docs/tasing.md
      lists them as not to copy. Two left with their scripts on 2026-10-05, when the ending
      stage (4.11) replaced `osc-final`, `dr-approach` and `dr-recover`:
      `Scattershot_BitfsDrRecover::IsSolution` read its previous state from the current
      frame, so its C-up phase's `ACT_DECELERATING` check compared a frame with itself, and
      `Scattershot_BitfsDrApproach` offered a C-up move its `ApplyMovement` never
      dispatched (that header's C-up trick also drew its yaw without adding the range's
      lower bound). `CustomMoves::REWIND` is checked by `Scattershot_BitfsDr::ApplyMovement`
      and offered by no `SelectMovementOptions`. `BitFsPyramidOscillation_TurnThenRunDownhill.cpp:150` reseeds
      the fine sweep's uphill half from the coarse midpoint (`midHau`) rather than the
      coarse winner (`midHau2`), which may or may not be intended. Neither is fixed here:
      each changes what a stage searches, so each needs that stage's counts before and after.
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
- [ ] **4.11 The ending.** Taken up 2026-10-05 on the maintainer's word: the earlier stages
      target an ARE and build the oscillations reliably, and the ending is to explore the
      full space of valid squish-cancel scenarios rather than optimize one. The scenarios,
      as described: with enough speed a pause-buffered dive recover that stays within 4
      units of the platform, so the pyramid keeps tilting through the air, landing at the
      edge with the speed killed at once; or a run to the edge braked by holding back,
      valid, with less tilt. At the edge: a slow run off with air strain into the squish
      spot; a turn of about 90 degrees to fall off parallel, which allows more speed; a
      quickturn (a minimum-magnitude walk for one frame at zero speed, into idle at once)
      and a fall at any angle; a C-up deceleration under one unit of speed onto a NUT spot,
      displaced into freefall without a step (running past one does nothing: a NUT spot
      needs an action that cancels to freefall); a frame or two of straining back over the
      platform after the slide-off for more tilt; landings on NUT spots along the edge
      after it, for the same. Built the same day as one stage, `sc-ending`
      (`Scattershot_BitfsScEnding` and `BitfsScEndingMetrics`, tasfw-scripts), replacing
      `osc-final`, `dr-approach`, `dr-recover` and `dr-nut`, which never ran to completion
      (4.1) and carried the defects of 4.9; `DetectEdge` moved to its own header. Every
      option is a weighted move (`moves` in the config, 0 leaving one out), the phases are
      the run, the dive, the brake, the edge and the air, and a solution records the
      approach, Mario's action, speed, yaw and C-up at the leave, the quickturn, the
      strain-back frames, the NUT landings and the probe's placement, so the solutions file
      says which ways through exist. The rules (the maintainer, 2026-10-05): the departure
      is the first frame at whose end the pyramid is not Mario's platform (every later
      frame has a lower tilt unless he lands back on it); and the solution is a frame within
      `probeWindow` of it from which the squish probe succeeds: the lowest of the level's
      four bullies (slot 27, verified by the layout check) hacked next to Mario in its
      knockback action, at `bullySpeed` along the push, the game played `probeFrames`,
      leaves Mario idle with at least `squishMinSpeed` and `squishMinClearance` above the
      lava, which is how a successful squish cancel ends (idle with the knockback's speed),
      with the pyramid's normal the target's two floats exactly on that frame (the
      maintainer's correction of 2026-10-06: the day before, the rule was the frame before
      the departure, which left the strain-back's and the NUT landings' tilt uncounted). The
      error is conserved exactly on every frame, through the departure, so the target stays
      on the lattice, and the probe runs only where it is within `probeFrames` steps on
      both axes, which off the platform means both counts equal and falling one a frame. Only the bully's position matters, for the
      displacement: roughly parallel to the ledge, downhill, so that Mario ends up inside it
      and can snap up and slide off on the squish's push. The probe tries nine placements
      (along the edge toward its lower end and 5 and 10 degrees in over the platform, each at
      0.3, 0.4 and 0.5 of the push distance `interact_bully` sets, the bully moving along the
      push; the set the example movies settled, below) and records the one that succeeded,
      else the one whose knockback took (the game sets Mario's invincibility timer with it; a
      knockback's speed is capped at 16, under a run's, so the fastest placement hid it for a
      while). The hacked bully needs its
      position, action, speed and yaw, a zero interaction status and intangible timer, and
      nothing else: the far-away flag an object beyond its drawing distance carries does not
      stop the collision pass (tried and dropped). The probe writes game memory inside the metric
      script's sandbox, the one write hard rule 1 now allows (its wording amended with the
      maintainer's approval the same day): a save of the frame first, then only written
      frame advances and reads until the sandbox ends, since a plain advance takes no
      savestate and the revert loads the clean save, and the fixed slices cover the object
      pool, so the bully comes back with it. `bitfs-turn --test` pins it: a walk off the
      platform from frame 3330 toward -z, probed on the frames whose normal is within a step
      of the target (the normal a frame after the departure), is byte for byte the plain
      replay, advanced to and loaded back to, two runs read the same from the probe, and
      the first probe's knockback takes (Mario in the air knockback at 16, the
      invincibility timer set); a walk toward +z instead steps onto the lava, and its probed
      frame is a lava boost, invulnerable, which is one thing the search's lineages will meet. What the departure rule makes of a dive from a run: its 20 of vertical speed
      takes Mario over 4 units unless the slope rises under him, so only a dive steep enough
      uphill keeps the pyramid his platform, the rollout's 30 the same, and the pause buffer
      (`pauseBuffer`, on by default) is what lets a dive that lands on its own frame roll out
      on the slide's first frame, B released during the pause: a TAS needs the pause for
      exactly that case. The maintainer's first run (2026-10-06, the build before the
      target's frame was corrected, so only a departure whose previous frame already had
      the exact target was probed): 1,730 shots, 416,000 blocks, no solution, with lineages
      that had run across the platform to its far side, where no squish cancel is. Two
      changes from it: `edgeSide` refuses a state on the far side of the pyramid's home
      from the edge the run follows (0, the edge's half, by default), and speed left the
      state bin for the fitness alone, since the bins were a block apart in speed and the
      run would have hit the block cap before the leave was explored. The maintainer's
      second run, under the corrected rule, read from its CSV at 66,013 rows: 43,784 were
      departed, their median height the lava's and their actions walking, turning around,
      decelerating and braking, so Mario walking off the dipped edge onto the lava floor
      and lava-boosting a frame later, one-frame dead ends the validation let through and
      the forced CSV rows drew; 2,689 were dives leaving the band on their first frame,
      recorded as no approach because the departure was handled before the approach; 41
      had equal step counts, which an off-platform leave needs since both fall one a frame;
      and 27 were within a step of the target, among them the target itself exactly, so the
      lattice does pass through it. Three changes from that: a ground action on the lava
      is refused unless the probe judged the frame (below), the approach is named on a
      departing frame too, and the fitness after the run counts the difference of the two
      step counts twice on top of their sum. The third run's rows then showed no freefall at
      all (the maintainer, 2026-10-06), and the rows say why: along the +z edge Mario's
      height on the platform is 4 to 30 units above the lava at the ending's tilts, under
      the 100 the game needs to leave the ground, so a step past that edge is never a fall
      but a step onto the lava floor, still walking, with the lava boost a frame later;
      every one of that run's 3,723 departed rows was a dive, a dive slide or a rollout,
      the dive's 20 of vertical speed against a slope of about 0.2 along the edge taking it
      out of the band at once. So the leaves described as falls are, from this edge, steps
      onto the lava, and the lava rule now keeps exactly the lava-floor frames the probe
      judged (the normal within its frames of the target) and drops the rest. The
      maintainer then placed the squish spot (2026-10-06): past the far edge of the target
      corner, the +x edge here, the high side at the target's tilt (90 to 250 units above
      the lava, by the same arithmetic), so the leave is a real fall after all. The stage
      takes it as `targetEdge`, `x` or `z`, the axis the squish ledge runs along (the
      maintainer's uscx and uscz; the committed +x edge is z-aligned, so `z`), its outward
      side from the corner's sign on the other axis, and derives the oscillation corner across
      the run's edge; it refuses a departure over any other edge (the nearest outer edge of the top,
      from `DetectEdge`, and Mario past its line), points the walk-off, the parallel run,
      the strain-back and the probe's placements at that edge, and checks at the config
      that the input's last crossing has Mario running toward it, since the orientation is
      otherwise easy to get wrong. What that heading is took a trace to settle: the dr
      solutions' `roughTargetAngle` is the chord end the normal steps toward after the
      reversal, and Mario runs toward the other, so the check takes its opposite (the
      committed hand-off, traced from an exported solution: `roughTargetAngle` -24576,
      Mario at (-235, +209) from the home at yaw 6699, toward +x and +z, the +z edge's
      middle, then along it to corner 1). With that the stage ran its first shots from the
      saved dr solutions (2 shots, 362 blocks, no refusal). The maintainer's fourth run
      (2026-10-06) showed no freefall either, and his example movies settled what the search
      was missing (`movies/pyramid_*.m64`, eight hand-made setups played to the squish spot
      with the bully left out, one per pyramid and corner; `movies/uscx_5.m64`, a brake
      approach that needs Mario placed before frame 3020 at x -2180, z -745, yaw 30008; all
      replayed through the DLL with a ctypes driver outside the framework, since the tools
      refuse to poke the game). What a squish cancel is in the game: the leave is over the
      high edge of the small-tilt axis, next to the low corner, where the drop to the lava is
      over 100 (a step or a landing stop leaves the ground) and the face's underside is under
      160 above the lava at the landing spot; Mario falls 7 to 8 frames onto the lava, the
      untilting face sweeps over him, and he is squished with the ceiling 135 to 142 above
      the lava. Both axes step toward flat on every fall frame, so the squish normal is the
      leave normal less 0.07 to 0.08 on both, and the lattice pins the leave: on the platform
      the run takes the squish axis a step toward flat a frame and the run's axis a step away
      from it, so the leave must come where the two counts meet, with the fall's length left
      on both. Two approaches seen: a run around the low corner along the low edge (4 to 13
      units above the lava) and a dive uphill toward the high edge (the dive lands on its own
      frame, the pause freezes three frames, B on the fourth gives the rollout, and the
      pyramid stays Mario's platform through both), a landing stop at the edge, a slow walk
      off, air strain; or a run along the low edge, the stick held back to about 7 speed, a
      step off, two frames of straining back (the pyramid Mario's platform again mid-air, the
      lagging axis stepping the other way those frames, a two-step correction each), the
      fall, the squish. The maintainer's bully hack reproduced (a bully launched 49 frames
      earlier arriving as Mario lands: idle at 206 speed, 114 above the lava), and the probe's
      own recipe with it: the bully 0.4 of the push-out distance behind Mario, the push along
      the ledge downhill turned about 5 degrees in, the bully moving along the push at 100,
      leaves Mario idle at 413 speed (negative in one example, so the speed is read either
      way) 110 to 114 above the lava; exactly along the ledge he lands on the lava and stays
      squished, 20 degrees in he is squished on the face with no speed, along Mario's facing
      nothing transfers, and at 0.6 of the distance and over the bully never touches him. The
      DLL's collision data gives the top face: a square of half-width 307 whose center the
      tilt carries 307 units along the normal's horizontal part toward the low side, so the
      far edge stands about 230 out at the ending's tilts and the +z side dips under the
      lava in the low corner (the lava-floor rows were Mario walking into that, not off an
      edge). Changes from all this (the maintainer's go, 2026-10-06): the dive's pause buffer
      is four frames (the rollout's press came a frame early and was lost, so no run had ever
      rolled out); the squish action passes the validation (the squish frame itself was
      refused, so no solution could have been recorded); the probe's bully moves along the
      push, the placements are the nine above, and the speed test is absolute; the lattice
      prunes a state whose squish axis is nearer flat than the run's axis or than the target,
      or has fewer than seven steps left before a leave (the shortest fall), which were two
      thirds of the table; the dive is offered only where the floor rises under it 16 or more
      a frame (uphill, toward the high side), the stick held back and the brake only within
      160 of the squish edge (the random held-back stick had made half the rows turnarounds
      and stops mid-platform), and a run toward the squish edge veering up to 40 degrees to
      the high side, held one to six frames, is a move of its own, offered from 27 speed.
      Four runs of 50,000 shots on the saved dr solutions then found no departure at all,
      and a census of the 107 hand-offs says why: every one is in the submerged low corner,
      218 to 278 units short of center in x and 0 to 32 above the lava, at 13 to 20 speed,
      with 15 to 22 frames to its lattice deadline, so the far edge needs 23 to 31 units of x
      a frame on average, where the run straight at it is uphill at a dead 13.7, cannot pass
      about 27 along the lava line (the diagonal that keeps him out of the lava), and never
      reaches the dive's 29 facing the edge; the northeast example at the same lattice point
      (18 frames left) was at the same spot at 33.9 speed, out of a 17-frame downhill run
      from a turnaround near the center. The ending is therefore not reachable from the dr
      stage's hand-over, and the fix is upstream, the maintainer's call: a hand-over with the
      downhill run still ahead (the last turnaround), or a lattice with more frames left at
      it. The driver and the census scripts stayed in the session's scratchpad; a replay with
      hacks for hand-made movies is a tool to add if the examples keep being read
      (`dllcheck`, or a script). The target normal is optional since the same day (the
      maintainer's ask): without `targetNx` and `targetNz` the stage conserves the
      hand-over's own lattice, measuring the error against the normal of the frame after
      `initialFrame` and carrying that reference, accepts a squish cancel at any normal on
      it (the solution records it), probes every frame of the window, and leaves the
      lattice's frame pins and the step-count fitness aside, speed standing in; a
      `bitfs-turn --test` case pins the reference and the window. Its first run on the
      saved hand-offs (50,000 shots) went where the pins had not let the targeted runs go:
      396,000 departed rows, falls from up to 192 above the lava over the +x edge, 1,658
      frames with Mario squished, no cancel. The CSV now carries what the probe made of a
      probed frame (`ProbeAction`, `ProbeSpeed`, `ProbeHeight`), and those said why: the
      leaves were run-offs at 25 to 30, landing 100 to 250 units outside the face, where the
      bully can only knock Mario about in the air; the squishes were at frames 3477 and
      later, the x tilt flattened to 0.01 to 0.1 and the face's edge 78 above the lava, so
      the probe's few idles had no speed and stood under the 100 the cancel needs (the
      examples squish under an edge 135 to 142 above the lava, with 0.2 of tilt left on
      the small axis). A cap on a departed state's outward speed, `maxLeaveSpeed` (12 in
      the config; the examples leave at 6.5 and 7.2, and the edge recedes about 3 a frame
      while widening as the tilt flattens, so a faster leave cannot land under it), brought
      the leaves to 2.5 to 10, and the landings still fell 60 to 190 units outside, from
      edges 150 to 190 high reached too late: the same hand-off finding, from the far side.
      One
      asymmetry to expect there (the maintainer, 2026-10-06): `uscz`, along a z edge with the
      normal's x crossing the origin, is much more lenient, where `uscx`, along an x edge,
      generally needs the movement to the squish spot optimized, Mario having to reach it
      before the normal's y gets too low; the committed setup is `uscz`, so the stage's moves
      are only really judged on an `uscx` corner, and the fitness, speed on the run and the
      frame after it since 2026-10-10, is where that would show first. Left for
      later: the step helpers (`PyramidGoal`, `Steps`, `StepFrame`, the one-frame downhill
      and uphill moves) are copies of `Scattershot_BitfsDr`'s, the third and fourth copies
      having gone with the removed searches; a shared home for them is a tasfw-scripts
      design question. Found building the test: `TopLevelScriptBuilder::ConfigureMetricScript`
      on an unconfigured builder declares the unconfigured builder as its return type and
      cannot be instantiated, so a metric script is configured after `ImportResource` (the
      imported builder's is right); a one-line fix in tasfw-core with its mock-resource
      test, for its own turn. Found 2026-10-08, running the stage apart from the pipeline:
      (a) from the northeast example movie's own last crossing (frame 4418 of
      `pyramid_-1945_northeast.m64`, the example's geometry: `targetQuadrant` 2,
      `targetEdge` x, the x-aligned -z edge, no target normal; a one-record input set with
      the direction and `equilibriumFrame` as metrics), 50,000 shots: 975,000 blocks,
      365,000 sampled rows, 259,000 of them departed, Mario squished in 8,900 at the
      example's own spot (300 to 350 from the home along x, -250 along z), and no squish
      cancel: on a squished Mario the probe's bully leaves him squished (7,540 rows) or idle
      at speed 0 on the lava (1,382), where the maintainer's hack gave idle at 413 and 110
      above the lava; probed in the air he is knocked back (air knockback, 150,000 rows); so
      the probe's placement or its frame does not reproduce the hack, and that is the
      ending's binding piece before any hand-over question. (b) From the oscillation stage's
      hand-over (the 712 roots of 4.12's run) under the committed config, 50,000 shots: the
      search runs (13 percent futility, 33 discovery), 129,000 departed rows sampled, Mario
      squished in 1,080, no squish cancel, the leaves at 0 to 12 off the edge landing 275 to
      425 from the home along x against a face edge at 252 to 381 at those tilts; the run
      then dies at the block cap (4.14) before writing anything. (c) Every ending run of the
      scratch configs of 2026-10-06 to 08 was dead on arrival: those configs carried
      `targetEdge` x from before the semantics were settled, under which the edge-side
      rule (`edgeSide`, the run's edge from the oscillation and target corners) refused
      every hand-over state at its first frame, 100 percent futility and as many blocks as
      roots, with no word from the stage; the stage should refuse to start when the
      hand-over itself fails its rules, naming the rule. (d) `VerifyLayout`'s home check on
      slot 85, the track platform, fails for a start frame after the platform has moved,
      since its home follows it (seen at frame 4418 of the example movie: home (-3684,
      -3131, 0) against the spawn's (-5744, -3072, 0)); a stage starting late in a movie
      needs that slot's `checkHome` off or the check to compare the spawn position (2.4).
      Narrowed 2026-10-10: the probe is right. Run by the metric script itself over the
      example's own landing (a scratch test case driving `BitfsScEndingMetrics` with the
      movie's inputs from frame 4440), on the frame before the squish, Mario in free fall
      62.5 above the lava at (244, -234) at 7.9, it records its placement 3 (5 degrees in,
      0.3 of the distance) as idle at 413 and 110 above the lava: a squish cancel by the
      stage's own test. The push must go downhill along the edge (+x there): the other
      way, at 0.3 or 0.4, Mario is squished on the lava and boosted. What the search lacks
      is that frame: its nearest states from the example's crossing (288 free-fall rows
      within 50 of the spot and 45 to 80 above the lava) sit at 261 to 275 along x and -251
      to -253 along z, 15 to 20 past the face's -z edge, at 4 to 6.5 with the normal at
      (0.42, 0.19) against the example's (0.37, 0.22), five frames of steps later, and every
      probe on them is an air knockback: pushed +x from there Mario is not under the face.
      Target mode does not reach it either: from the dr hand-over under the committed
      targets 265,000 departed rows, 34,500 of them at the target normal, none squished;
      from the example's crossing with its own squish-frame normal as the target 7,720
      departed rows, none squished. So the ending's search leaves a few frames late and a
      few units far for the push to land Mario on the face, and nothing in its rules or
      fitness aims at the spot, whose tolerance the example puts at about 15 units and 0.03
      of normal; a rule or fitness on the pre-squish geometry (where the push lands him
      against the face's sweeping edge, from the normal's lattice) is the next piece. The
      maintainer's direction (2026-10-10): once anything decelerates, speed is no fitness;
      time instead, and validation rules to prune. Built the same day: the fitness after the
      run (the dive, the brake, the edge, the air) is the frame, the earlier the better, in
      place of speed and of target mode's step term; and a departed state must stay on the
      platform's side of the squish edge's line until the squish, the line 307 from the home
      along the edge's outward direction less 307 times the normal's component that way,
      which the untilting face sweeps outward 3 a frame (the example leaves 17 inside it and
      meets it on the squish's frame; the search's nearest states were 2 to 4 outside it).
      Under that rule alone the example run's squishes sat on the line but 300 to 350 along
      the edge, where the probe's push, about 70 along it, carries Mario off the face's far
      edge at 420 into a knockback on the lava (the example's 244 lands at 311), and only
      two of 52,892 departed rows were a fall from the face's top; so a departed Mario must
      also have `PushReach` (90) of face ahead of him in the push's direction. Measured with
      both rules: from the example's crossing 43,502 departed rows, 7,607 squished, still at
      300 to 325 along the edge (the face's far edge there is at 417 to 436, so the reach
      admits them), four of them a fall from the top, and no cancel; from the dr hand-over
      under the line rule alone 47,122 departed rows and under both 69,499, no squish at all
      either way, the departures
      15 to 45 frames after the hand-over with the normal's x already flattened from -0.45
      to -0.2 and 0, where the example leaves 24 frames after its crossing with its normal
      still deep in its corner, (0.43, 0.28). That is the geometry, not the rules: the
      committed setup is `uscz`, the squish over the +x edge, the high edge of the large
      axis, reached by a run across the tilt that flattens it on the way, and every example
      movie is `uscx`, the squish over the high edge of the small axis next to the low
      corner, reached by a run deeper into the low side that keeps the tilt. The rules and
      the probe here were calibrated on the `uscx` examples; no `uscz` movie exists to
      calibrate against, and the maintainer's own word is that `uscz` differs (the lenient
      kind, the normal's x crossing the origin). The one setup the ending has reproduced a
      cancel for by its own probe is the example's; the pipeline's own corner in that
      geometry is `targetQuadrant` 3 with `targetEdge` x, the -z edge next to the low corner
      (-x, +z), with the hand-over heading there, which the ending's direction check names.
      The `uscz` movie (the maintainer, 2026-10-10:
      `movies/uscz_pyramid_-1945_southeast_-p15259_p82917_p41379.m64.m64`, the normal at its
      squish in the name) replayed through the game outside the framework (the scratchpad's
      `dll_play.py`, `edge_probe.py` and `point_probe.py`, the last two calling the DLL's own
      `find_floor` and `find_ceil`): crossings at 3336, 3365, 3397, 3422 and 3452 (11.6,
      24.1, 28.8, 27.2 and 29.0; the hand-over's normal (-0.42, 0.26), sum 0.68, Mario
      heading +x and +z, like the dr stage's own hand-overs at 26 to 28), a 12-frame run to
      (51, 389), a dive and rollout to a landing stop at (234, 307) on the +x edge, a walk
      off at 5 (frame 3474, 106 above the lava, the normal (-0.23, 0.45)), eight frames of
      fall beside the edge, the squish at 3482 at (273, 288) with the normal (-0.153,
      0.414), and no bully in the movie. What the game's own geometry says against the
      rules of the morning: the face's edge is not where the line rule put it. The face is
      the square (+-307, 307, +-307) of the object's frame, placed by
      `mtxf_align_terrain_normal` from the normalized normal (the game normalizes all three
      stored components, y included, so nX and nZ alone are not the tilt): its x-aligned
      edges lie at z = +-307 s + 307 uz with s the length of (ux, uy), its z-aligned edges
      are skewed, x = +-307 uy / s + 307 ux - Z ux uz / s along the edge's own Z. That
      matches `find_floor` within a unit on every frame of both movies, where the line rule
      (307 + 307 n.out from the raw components) sits 6 to 9 inside the real +x edge on the
      `uscz` movie and 7 to 11 outside the real -z edge on the `uscx` one. And both movies
      fall outside the edge, not inside it: `uscz` 0 to 3.6 out (two frames 0.4 in, where
      the face is his floor again), `uscx` 4.5 to 6.5 out, each riding beside the edge as
      the untilting face sweeps out 3 to 4 a frame; the squish comes on the frame whose new
      edge first covers Mario's position at the frame's start (the flag is set from that
      position against the frame's transform, before his step). So the line rule refused
      every frame of the `uscz` fall and passed the `uscx` one only by its own error. The
      cancel, read in the decompilation (`act_squished`, `interact_bully`,
      `perform_ground_quarter_step`) and confirmed by the numbers: at the frame's start
      Mario's x, z are under the side face with the top more than 78 above him, so his floor
      is the lava and the side face a ceiling 150 or less above it (`INPUT_SQUISHED`); the
      bully's `push_mario_out_of_object` sets him 115 from the bully along their line and
      `bully_knock_back_mario` hands him the bully's speed times 4.13 (413 at 100); the
      airborne action cancels to `ACT_SQUISHED` before any knockback step, and
      `act_squished`, with the frame's stale 150 of space, pancakes him and then, the
      ceiling's normal y being above -0.5 ("steep"), walks him 10 along the ceiling's
      horizontal normal in four ground quarter steps: a step whose floor is the top face
      within 78 above him snaps him up onto it, and a step whose floor is the lava with him
      more than 100 above it is `GROUND_STEP_LEFT_GROUND`, the instant un-squish to idle with
      the speed intact (the `uscx` probe's idle at -413, 110 above, is Mario at the face's
      height one step past its edge). Hence the conditions, with E the face's height at the
      squish spot, F at the pushed spot, g the face's slope along the edge (the along
      component over uy) and P the push (57 to 80 by the fraction): E at most 150 (the
      ceiling), 100 < F = E - gP <= y + 78 (the pushed spot's face his floor and over 100),
      y < E - 78, the pushed spot within the walk's 10 of the edge, and the side face steep,
      -0.707 (uy + ux / s) > -0.5 for the +x edge, which at nZ 0.41 needs |nX| of 0.15 or
      more: the movie's -0.15259 is the least that does (the game's normal there -0.4959,
      the `uscx` movie's -0.4988). The push is downhill because F must lie under E. The
      `uscz` movie's own squish cannot be cancelled by any push (measured: every placement
      a pancake at 0 speed, the frame before a knockback): caught 13.7 above the lava where
      the +x edge stands 99, the pushed spot's face is 59 downhill, or over 100 but more
      than 78 above him uphill. Set at (254, 202) at the start of the squish frame, 37.7
      up where the edge stands 146, the push straight down the edge at 0.3 to 0.5 gives
      idle at -413.2, 105 to 116 above, and free fall at -413 the frame after: for this
      setup the catch is 60 to 100 up the edge from the corner (E 130 to 150, z 195 to 230
      at this normal) between 27 and 68 above the lava, and the placement straight along the
      skewed edge (which recedes outward 0.08 a unit along +z, so the pushed spot is 5 to 6
      inside by itself); the `uscx` edge's 5 degrees in puts him too deep for the walk
      (squished on the face at 0 speed) and 5 out on the lava. Consequences: the line rule
      and `PushReach` are wrong and go; their replacements are the exact edge, a departed
      Mario on or within a few units outside it until the catch, and the catch frame's E, F
      and y window above; the probe's placements should come from the geometry (the pushed
      spot 2 to 9 inside the exact edge) rather than three fixed angles; and the run must
      leave up the edge from the corner, not at it, which is the stage's aim (`PyramidGoal`,
      the edge moves) and the maintainer's decision. The dr side needs nothing for `uscz`:
      the movie's hand-over crossing is the stage's own. The maintainer's word on the movie (2026-10-10):
      its cancel was never verified with his bully hack, and it may well be too close to
      the corner; so the only verified `uscz` cancel is the moved catch above, and the
      movie stands as the example of the oscillations, the run and the leave, not of the
      squish spot. Built 2026-10-11 on the maintainer's go (the four pieces, the corner
      distance computed and allowed a little less): `BitfsScEndingMetrics::Face`, the top
      face as the game places it, from the normalized stored normal with the skew of the
      z-aligned edges and the collision data's extents (307 on the positive sides, 306 on the
      negative), with the top's and the sides' planes, the edges, the corners and the sides'
      normals, pinned by `test_bitfs_ending_geometry.cpp` against the game's own `find_floor`
      and `find_ceil` on both movies (the edges within 1.5, the top within 1.5, a side within
      3: the game truncates its vertices to shorts toward zero, which puts an edge on a
      negative side up to a unit nearer the apex); `CatchPlacements`, the catch and its
      cancel in the decompilation's constants (the frame's face covering Mario, the top over
      78 above him, the side within 150 of the lava; the side steep; three pushes, downhill
      along the edge at the fractions 0.3 to 0.5 of the push-out distance, each landing him
      on the top where it stands over 100 and within 78 above him, short of the top's end,
      and inside the edge by the middle of the squished walk's reach, 2.5 a quarter step
      over the lava and 2.5 uy over the top; every bound with its slack, `CatchSlack` 3 on
      the heights, `DepthSlack` 1 on the edge, `SteepSlack` 0.003 on the side's normal, the
      measured errors of the geometry, see below), pinned by the same test on the uscz movie's own catch (none),
      the moved catch (three) and the uscx movie's (three); the probe gated on it and placing
      its bully from it, three placements where it had nine, on the catch's frame alone
      where it had every frame of the window (`catchPlacements` on the metric, `Catch` in
      the CSV); `OverSquishEdge` and `EdgeDistance` by the face, in place of the `DetectEdge`
      runs on the stand-in (the ending no longer includes it; the midpoint's coordinate those
      used was up to 14 off the skewed edge at the corner, enough to refuse the uscz movie's
      own departure); the rules: the line rule and `PushReach` gone, a departed Mario at
      most `SweepReach` (26, eight sweeps) outside the exact edge, the edge at his position
      standing at least `LeastEdgeHeight` (100 plus the top's fall over the shortest push)
      less `CornerSlack` (5), and a catch with no cancelling push refused; and the run to the
      edge aiming at `AimPoint`, where the edge stands at the middle of the catch's window
      of heights, within 15 degrees, in place of the veer toward the high side. The probe
      tests run on the uscx movie now (`pyramid_-1945_northeast.m64` beside the config's
      movie): from its crossing at 4418 the probe runs on one frame, 4449, and finds the
      cancel there (idle at -413, 109 above the lava), leaves the game as it found it, and
      with the cancel frame's normal as the target runs there and nowhere else, with a
      normal two steps on nowhere at all. The walk off the committed movie's frame 3330 the
      tests used before stands where the +x edge is 189 high and tilts lower as he walks,
      so no catch comes of it; the example movie is the honest setting. The first run on the
      dr hand-over under these rules (8,000 shots asked, 50,000 before) hit the block cap at
      4 million blocks after 85 seconds (4.14 ended the process): 2.5 million departed rows,
      all but one never caught, states falling further outside the edge than any sweep could
      reach in the window's remaining frames, kept alive by `SweepReach` alone and binned to
      the unit. So the bound is now the sweep's travel over the frames the window has left
      (the edge's move over the next frame, read from the stepped normal), within
      `SweepReach`, plus the slack; and the CSV's forced rows are the departure, the frames
      with a catch, the squish cancels and, since 2026-10-13 at the maintainer's ask, every
      dive recover landing that validates (40 forced beside 8 sampled in an 8,000-shot
      run), the sampled rows keeping the rest. The 8,000-shot runs under
      that bound (27 and 37 seconds, 1.1 and 1.2 million blocks, 57,000 and 47,000 departed
      rows) caught nothing: 10,000 and 4,900 of their leaves stood where the edge is 125 to
      150 high, but with the normal's x spent, 0.0 to -0.1, where the edge no longer sweeps
      and the side is no longer steep (the movie leaves at -0.23 and is caught at -0.16), the
      aim having been read off this frame's face, whose edge at the hand-over's -0.42 stands
      far too high everywhere. So the aim is read off the catch's face instead
      (`CatchFace`): the target's normal when one is given, else the normal's component
      across the squish edge flattened to the last value at which the side is still steep,
      the latest catch the cancel allows. Measured with that aim (8,000 shots: 26 and 36
      seconds, 1.27 and 1.31 million blocks, 6.5 and 9.7 million frame advances, 62,000 and
      123,000 saves, 1.4 and 1.9 million loads; 89,000 and 57,000 departed rows): still no
      catch. The leaves come too late for the tilt: from the uscz movie's crossing at 3452
      they fall at 3476 to 3503, the median 38 frames after it, with the normal's x at -0.01
      to -0.08, where the movie leaves 22 frames after it at -0.23 and is caught at -0.16;
      from the dr hand-over at 3457 to 3489, the median 21 frames, at -0.06 to -0.14, under
      the 0.15 the side needs at that tilt. The brake is the approach of half the leaves,
      the dive of none. So the next piece is the run's timing, a moves question: the edge
      reached and left within about 20 frames of the crossing, while the component across
      the squish edge still stands at 0.15 or more (the lattice rule above already pins the
      steps left on the platform in target mode; without a target nothing does), the dive
      offered where the movie dives, and the brake sooner. The maintainer decides the form.
      The block cap (4.14) stays the limiter of any longer run: 4 million blocks in 85
      seconds before the sweep bound, 1.3 million in 30 under it. Built the same day on the
      maintainer's go, with his old approach logic as the model (`BitFsScApproach_AttemptDr`,
      and the deleted `Scattershot_BitfsDrRecover`, which refused a landing stop over 2 from
      an edge): the dive recover is one move, the dive straight ahead with the pause buffer,
      then the rollout a frame at a time with the stick, among the yaws within a quarter turn
      toward uphill, that keeps it lowest within 4 of the platform (the compare family, as
      his script), until it lands; the move is reverted when the rollout leaves the platform
      or the band, or lands over `DrLandingReach` (30 now, 10 at first; see below) inside the
      squish edge, and the gate on the floor's rise under the dive is gone. The brake and the held-back stick are
      offered within `BrakeReach` (80) of the squish edge, not 160, and the run's
      exploration keeps the stick full. Measured (8,000 shots: 23 and 26 seconds, 1.35 million
      blocks each, 6.0 and 7.9 million frame advances, 48,000 and 69,000 saves, 1.3 million
      loads each): from the uscz movie's crossing still no catch; from the dr hand-over the
      first catches of any run, 41 rows at the normal (-0.19, 0.36), 45 to 53 above the
      lava, at 254 to 286 along the +x edge, the probe tried on all of them (36 knockbacks,
      where the frame's new edge had not quite reached him, 5 squishes with no un-squish)
      and no cancel: the edge stands 128 at those catches, the bottom of the window, so the
      pushed spot's top is 94 to 104 where it must exceed 100 and stay within 78 above him.
      The tilt is right now and the height is right; the landing is 50 units too near the
      corner (the aim's window at that normal is 200 to 230 along the edge). The next piece
      is the pull toward the aim: the dive recover lands where the dive's arc ends, about
      200 ahead of the dive, so the run must turn to +x sooner or the landing's reach must be
      judged against the aim's window along the edge, not the edge alone. Built the same day
      on the maintainer's go, with the time budget he asked about (nothing on the platform
      refused a state that could no longer make it): on every platform frame the tilt across
      the squish edge has (|across| - the catch's) / 0.01 frames before the catch, less
      `FallFrames` (7, the movies' fall from the edge to the catch), and reaching the edge
      takes at the least the distance from the half's middle, or from Mario once he is on
      the edge's half, at his speed or `WalkOffSpeed` (4), so a state whose need exceeds what
      is left by more than `BudgetSlack` (2) is refused; and the dive recover's landing is
      kept only where the edge, on the catch's face, stands in the catch's window of heights
      at that position (`InCatchWindow`), the window's bounds with their slack. Measured (8,000
      shots): from the dr hand-over the first squish cancels of the pipeline, three solutions
      in 20 seconds and 453,000 blocks (6.3 million frame advances, 72,000 saves, 1.2 million
      loads; 303 catch rows, the probe on all of them, 104 squishes, 196 knockbacks, 3 idles
      at -413 speed 115 above the lava), the budget having cut the table by two thirds; all
      three the same leave, a brake at 3455 from 17 to 12, a walk off the +x edge at 3458 at
      12 (`maxLeaveSpeed`), a fall drifting up the edge 12 a frame, the catch at 3465 at
      (248, 206) 68 above the lava with the normal (-0.189, 0.383), the push 0.3 behind down
      the edge; `scratchpad/dll_play.py` replays the exported movie with that placement and
      reads idle at -413.3, 117.7 above the lava, on the face the frame after. The three
      movies and `ending.json` are in `analysis/m64/ending-real/` (gitignored). From the
      uscz movie's own crossing still nothing: 167,000 blocks, 7,000 departed rows, no catch;
      its leaves stand at the window's height with the normal's x at -0.1 to -0.2, so the
      budget lets them through, and why the sweep does not reach them is the next thing to
      read off that run. Then a full run on the pipeline's own hand-overs, which the block cap
      (4.14) must not end first. Read off it the same night: its falls reached the catch's
      height with the normal's x at -0.10 to -0.12, past steep, the top within 78 of Mario;
      the leaves had come at -0.18, let through by the budget's slack (the steep slack in
      the catch's tilt, a fall of 7 where the catch waits 8 or 9 frames for the top to stand
      78 over him, the two frames' slack). Tightened (the catch's tilt without its slack, the
      fall at 8) the 8,000-shot runs found nothing where they had found three cancels, with
      a fifth of the departures left, so the budget stands as it was and only a departed
      state whose tilt across the edge is no longer a step over the catch's is refused at
      once: the leaves the budget lets through late no longer hold the window's ten frames.
      The uscz crossing's leaves are late because its run is, five frames behind the movie's
      22 to the edge, and no rule makes a run faster: that is the moves again, or more shots. A 50,000-shot run from the dr hand-over under the budget as it stands (80 seconds,
      1.03 million blocks, 24.7 million frame advances, 255,000 saves, 4.0 million loads):
      1,105 catches, the probe on all of them, 405 squished to a pancake, 700 knockbacks, no
      cancel, every catch near the corner, 270 to 285 along the edge, where the edge stands
      128 at the catch; the three cancels had left at 278 and drifted up the edge to 206
      during the fall, 12 a frame. So the window along the edge is now judged on every
      departed state, at the position the fall will carry him to (his position along the
      edge plus his drift along it over the frames the fall has left), on the catch's face:
      the edge there must stand between `LeastEdgeHeight` less `CornerSlack` and
      `SquishSpace` plus `SweepDepthSlack` (11, two sweeps' depth lowering the side), in
      place of the corner rule on this frame's face. Measured (50,000 shots, 76 seconds, 677,000
      blocks, 24.1 million frame advances, 245,000 saves, 4.1 million loads): one cancel, a
      run off the edge at 20 with no brake, caught at (249, 206) 68 above the lava at the
      normal (-0.189, 0.403), the push straight down the edge at 0.3, idle at -413 at 113
      above (replayed outside the framework the same way; its movie is beside the first
      three in `analysis/m64/ending-real/`); 265 catches at 170 to 230 along the edge, all
      in the window now, and among them 99 pancakes whose side the geometry had as -0.508,
      steep within the 0.01 slack and not in the game, and 165 knockbacks fired a frame early,
      the frame's edge 0.3 to 2.4 short of Mario within the 3 of slack. So the slacks are
      the geometry's measured errors and no more: the edge's `DepthSlack` 1, the side's
      normal `SteepSlack` 0.003, the heights' `CatchSlack` 3 as before. Both movies' cancels
      stand under them (the uscx movie's side is -0.5017 by the geometry, -0.4988 in the game). Measured under them (50,000 shots, 76 seconds, 566,000 blocks, 23.7 million
      frame advances, 265,000 saves, 4.1 million loads): 54 catches, all at 196 to 210 along
      the edge and 49 above the lava with the normal at (-0.18, 0.39), 45 of them probed a
      frame early still (the frame's edge a unit short of him, the sweep 3.3), 9 pancakes,
      no cancel; from the uscz crossing 764 departures and no catch. Four cancels in all so
      far, from four runs of 8,000 to 50,000 shots that found 3, 0, 1 and 0: the rules now
      keep the search in the window (the catches are where the cancels were) and the rate is
      the search's, a cancel in tens of thousands of shots, so what comes next is the
      exploration rather than another rule: longer runs, which the block cap (4.14, 1
      million in the committed config, 4 million in the scratch runs) must not end, and a
      look at which leaves cancel (the four were a brake to 12 and a run off at 20, none a
      dive) to weight the moves. The uscz crossing's run is the other thread: five frames
      slower to the edge than the movie's, its leaves come at -0.18 and are never caught. The dive recover itself landed in none of the night's runs (no row of any run carries
      its approach): the dive needs 29, the run holds 29 to 30 for the first six frames
      after the hand-over, 300 from the edge, and bleeds to 25 as the turn toward the aim
      takes it uphill, while the movie runs along the low side at 28 and dives at 31 some
      190 from the edge; and a dive taken 300 out lands 100 short of the edge, which the
      landing's reach of 10 refused. So (the maintainer's go, 2026-10-11) the turn toward
      the aim is offered only once the aim is within `DrReach` (200, the movie's dive and
      rollout), the dive only where the edge is between the recover's reach and the
      landing's, and the landing stop counts within `DrLandingReach` 30 of the edge, close
      enough to walk off in a frame or two. Under that gating alone (50,000 shots, 56 seconds, 547,000
      blocks) still no dive landed and, the run bleeding to 25 before the edge was within the
      dive's reach, no catch either: the run's exploration move, a random stick within a
      quarter turn, took it uphill. So until the edge is within the recover's reach the run
      draws the minimum downhill angle toward the target corner six times in nine (the
      deviation three, the random stick two), which at the hand-over's tilt is the uscz
      movie's own heading, up the low edge toward the far corner, where it runs 12 frames
      from 28 to 31 and dives; the approach, with the turn toward the aim, begins at the
      edge's distance (`DrReach` plus the landing's), not the aim's. Measured (50,000 shots, 54
      seconds, 487,000 blocks, 19.3 million frame advances, 152,000 saves, 2.7 million
      loads): still no dive landed, 29 catches at 195 along the edge and 57 above, 21
      pancakes, no cancel. The run's speed by frame, the median of the sampled rows: 26 to
      29 for the first eight frames after the hand-over, 25 at the tenth, 22 at the
      twentieth, 19 at the twenty-fourth; the dive's 29 is never there where the edge is
      within its reach. The movie's run gains from 28 to 31 on the same heading, so the
      difference is in the hand-over states or in how the stage's downhill angle is taken,
      and reading that is where the dive recover stands: no dive recover has landed in any
      run of 2026-10-11. The brake and the run off the edge remain the stage's leaves. Read
      2026-10-12 with a scratch script on the game from the uscz movie's crossing, four runs
      of twelve frames beside each other: the stage's minimum downhill angle, exact, gains
      exactly as the movie does (27.7 to 35.9 by the ninth frame against the movie's 35.4 by
      the eighth), the margin inside the range the same, the movie's heading held the same
      until it runs off the low edge; so the angle was never at fault, the search's other
      draws were (a random stick, a neutral frame, the uphill turn), and the movie's run is
      eight frames at that angle, then five frames of turning toward +x at the walking
      rate's 2048, from 35 down to 31, then the dive at 31 some 190 from the edge. The dive
      recover from the movie's own dive point, run with the stage's steering, showed the
      rollout landing at once: the steering took a landing over the flight wherever it came.
      It now follows the maintainer's script in full: a landing counts only within
      `DrLandingReach` of the squish edge where the edge will stand in the window, else the
      rollout flies on, its first two frames the lowest, then the highest within the band of
      4 over the platform and the lowest where none is; from the movie's dive point it then
      flies at 3 to 6 over the platform for four frames to 23 inside the edge, one frame
      and one turn step short of the movie's landing at 3 inside because the script dove a
      frame earlier than the movie (the movie's dive input is the frame after its last turn
      step; the frozen frames, the rollout's press and its heights over the floor, 3 to 4,
      match the movie's). And the approach begins early enough for the turn: `approaching`
      is the edge within `DrReach` plus the landing's reach plus the turn's distance, the
      frames the turn from the run's heading to the aim's takes at 2048 a frame, at the
      run's speed. The corner run that followed (50,000 shots in 23 seconds, 550,000 blocks,
      from the day's 736 dr roots at their fifth oscillation, 23 at the crossing) still
      landed no dive, with 818 sampled run frames at 29 or more in the dive's window; a
      scratch trace of the move's attempts showed every one taken on the run's heading up
      the low edge, 5,000 to 9,000, before the turn, the dive flying over that edge and
      still in the air 75 to 90 above the lava four frames on, no rollout step possible. So the dive
      is offered facing the aim, within an eighth of a turn of it, as the movie dives after
      its turn. Traced again under that gate, the dives faced the squish edge and their
      rollouts flew to 10 to 25 from it, 20 to 36 above the floor, where neither a landing
      nor a step over the platform was left: a rollout rises about 26 a frame of its own,
      and stays within 4 of the platform only where the floor rises under it nearly as fast,
      which it does under the movie's dive (24 a frame, uphill on both axes at 46) and not
      under a dive straight at the squish edge (13). So the dive is offered only where the
      floor rises along the facing, at the dive's speed, by at least `DiveRise` (16) a
      frame. That was the dive's condition before 2026-10-11 and was removed then so that
      the search would judge each dive by its outcome; it cannot, because the stick in the
      rollout's air only bends its path a little and never lowers its arc. Traced under
      that condition (8,000 shots on the corner from the day's 736 dr roots, which cross at
      23): no dive was offered at all; every frame in the dive's window at 29 or more had
      Mario heading up the low edge, 4,000 to 10,500, with the floor falling along his
      facing, 0 to 15 a frame, where the aim lay at 23,000 to 26,000. The turn toward the
      aim costs about a unit of speed a frame for four or five frames, so the run has to
      reach the turn at 33 or more for the dive's 29 after it; the movie reaches it at 35
      from a crossing at 28, these roots reach the window at 29 to 31 from their 23. So
      until the approach the run draws the minimum downhill angle eight times in eleven and
      its deviation twice, the random stick not at all. What that cannot make up is the
      speed of the hand-over itself, which the dr stage decides (the day's config keeps a
      hundred roots a pass where it kept ten, so slower roots reach the ending).
      Measured next (50,000 shots on the corner, 2026-10-12): the runs reach 34 at most,
      seven frames after the crossing, and then lose half a unit a frame; in the dive's
      window every frame at 29 or more still heads up the low edge. The turn drawn a
      frame at a time (`RUN_TO_EDGE`, one to six frames, against the run's other draws
      and a fitness that is the speed) is never completed, so the turn is now the dive
      recover's own: `Dive` turns toward the aim with a full stick each frame, the aim
      drawn across the catch's window of heights (`AimPoint` takes the fraction; nearer
      the corner the turn is shorter), and dives as soon as the facing is within one turn
      step of it and the edge within the recover's reach; the move is offered where the
      speed the turn would leave, `TurnSpeedCost` (0.5) a frame of it, is still the dive's
      29, and the run keeps the minimum downhill angle at eight draws in fourteen
      throughout. Traced on the corner (8,000 shots, the move's outcomes): the turn costs
      0.5 a frame and takes a frame or two more than the direction to the aim at its start
      says, since that direction turns as Mario nears the edge; of 478 attempts 12 dived, at 29.0 from
      starts of 32.4 to 33.3, and every rollout then climbed to 36 to 39 over the floor at
      27 of speed with the edge 9 to 18 away: **the corner resource does not model the
      pause**, its `Frame` runs the level on every frame whatever the buttons
      (`BitfsCornerGame::Frame`, `tasfw-resources/src/BitfsCorner.cpp`), so the three
      frames the pause buffer means to freeze are three frames of the dive slide, uphill,
      44 down to 27 and 86 units on. On the game, where the pause freezes them (the
      movie's replica of 2026-10-12 showed it: the rollout's first frame moves from the
      dive's spot), the same build and roots in 8,000 shots: 18 dives, 7 landed within 0.1
      to 7.3 of the squish edge in the catch's window, the rollouts 2.6 to 6.1 over the
      floor at 41 to 43 for four to five frames, as the movie's. So the dive recover works
      as a move; on the corner it cannot until the resource freezes the level through a
      pause as the game does (`play_mode_normal` enters the pause on the frame START is
      pressed, after that frame's update; the pause menu's first frame opens, its second
      takes the START or A that closes it, the third returns to play, none of the three
      updating the level or the camera, the controller read and the global timer going
      on: level_update.c's `play_mode_paused`, ingame_menu.c's
      `render_pause_courses_and_castle`), which is the resource's design and the
      maintainer's call (the ending on the game meanwhile, or the dive recover without the
      pause: one slide frame, about six of speed and forty units, the rollout then landing
      seven frames out). Measured (50,000 shots on the game, 256 seconds, 398,000 blocks):
      63 dives, 20 landing stops 1.4 to 21.7 inside the squish edge in the catch's
      window, and not one became a block: every rollout that landed had flown 4.8 to
      6.1 over the platform for three frames, which is a departure by the rule of
      2026-10-05 (the pyramid not his platform at a frame's end: the game's
      `update_mario_platform` asks for under 4 over the floor), in the middle of the
      platform, which `ValidateState` refuses; the rollouts that kept under 4 flew past
      the edge without landing. The arithmetic: a rollout climbs 30 on its first frame
      and 4 less each frame after, so the band holds only where the floor rises 26 or
      more under it, the dive's speed times the top's slope along the heading. The uscz
      movie, replayed frame by frame (`dll_play.py`): it turns one step past +x to
      19,910, dives at 30.7 for 45.9, and its rollout stays at 3.3, 4.0 and 3.9 over the
      platform, the platform kept, because its top slopes 0.58 along that heading (the
      normal (-0.293, 0.787, 0.394), its y low since the oscillations swing Mario far
      from the centre) and 45.9 times 0.58 is 26.5. Ours dive at 29.0 for 44.2 on a top
      whose normal is (-0.37, 0.85, 0.32), slope 0.56 at best along the steepest
      heading: 24.8 a frame, so the first rollout frame is 5 over the platform whatever
      the heading. The dive recover is short at every step from these roots: 23 at the
      crossing against the movie's 27.7, 29 at the dive against 30.7, and the top less
      steep. What could change that, for the maintainer: the hand-over (two more units of speed, or
      a normal whose y is nearer 0.79, from the dr stage), or the rule (a rollout that
      lands back on the platform within its reach counted as the approach it is, not a
      departure: off the platform the run's axis steps toward flat for those frames,
      0.03 of it, which the catch's rules would then judge). The maintainer chose the
      rule and the pause (2026-10-12): a dive recover's flight over the platform, above
      the band with the pyramid still the floor under Mario, is the approach, not a
      departure (`BitfsScEndingMetrics`; past the platform's edge it is one like any),
      and a landing further than `DrLandingReach` inside the squish edge is refused
      (`ValidateState`); the corner freezes the pause's frames (4.22). Measured (50,000
      shots on the corner, 23 seconds, 432,000 blocks): the recover lands and goes on,
      93 sampled landing stops 0 to 16 inside the edge, 372 rows on the edge after one,
      2,218 departed rows from one, and none of them caught, nor any of the brake's
      8,777 (the day's roots catch nothing on the corner, where the roots of the day
      before caught 1,105 states on the game). The recover's leaves go the way Mario
      faces at the landing, 45 to 65 degrees outward from the edge's line (a walk from a
      landing stop starts in the facing; from idle it starts in the stick's direction),
      1 to 7 units a frame outward, and the falls end at 83 above the lava at the
      lowest: the catch needs Mario 78 below the top, six or seven frames of fall, by
      which the drift has carried him past what the sweep covers, and the sweep rule
      ends the lineage. In the four squish cancels of 2026-10-11 Mario stepped off the edge moving nearly along it, toward the far corner. Next: the
      leave after a recover's landing along the edge (the quickturn to idle, then a walk
      whose stick lies near the edge's line), and the day's roots against the day
      before's. Done 2026-10-13 (the maintainer: proceed, and no pause outside the dive
      recover): the walk off the edge is drawn from straight off to the edge's own line;
      a new move, `LEAVE_ALONG`, from a stop is the squish cancels' leave as one move (the
      quickturn to idle, then a stick along the edge toward the catch window turned
      outward by up to an eighth of a turn, a full or half stick); the recover's aim and
      landing are judged on the catch it is heading for (`ExpectedCatchFace`: the
      component across the edge stepped toward flat for `FramesToCatch` frames, no further
      than the latest catch) where the run to the edge keeps the latest; the CSV gains
      `LeaveAction`, the action Mario left the platform from; and `ValidateState` refuses
      a frame that pressed START with Mario not on the dive slide (no move draws START,
      the dive recover's pause buffer is the one place it is written, in every stage).
      Measured on the corner, 50,000 shots in 14 to 16 seconds, the day before's roots
      (28 at the crossing) cut from their squish cancel movies a few frames before the crossing's
      end, three seeds: 108, 62 and 75 squish cancels, then 43 and 17 with the recover's aim
      drawn past the window; every squish cancel the brake's or a run-off's, Mario stepping off the edge
      moving nearly along it, toward the far corner (895 of 1,582 such falls caught on their sixth frame at 48 to 74 above the lava,
      185 to 240 along the edge; of the falls straight off, none); the same roots on the
      game, 8,000 shots, 0 to 84 squish cancels by the cut and the seed, so a run of 8,000 says
      little. The day's roots (23 at the crossing): no catch by any route, in 50,000 shots
      on either resource. The dive recover: it lands (90 to 467 landing stops sampled in
      50,000 shots from the fast roots) and leaves, 356 to 20,754 departed rows, and not
      one of its falls is caught: its landings come 240 to 306 along the edge, 30 to 80
      nearer the corner than the catches whose squish cancels, where the edge is too low for the
      window, and the window's rule ends the fall on its sixth frame (the aim does not
      move the landing: it is where the rollout comes down, set by the dive point at the
      end of the run up the low edge and the heading the turn can afford). A recover that
      ends in a squish cancel needs its dive from further up the edge or a heading further toward the far
      corner, which these roots have not the speed for, or a walk up the edge after the
      landing, which the time budget has not the frames for. So the brake's leave is the
      pipeline's ending from the roots it has, and the roots are what decides the yield:
      the hand-over's speed and tilt (4.12). The maintainer's note on the falls straight
      off the edge (2026-10-13): he has had them caught by hand, stepping off at a low
      speed and managing the drift with the stick in the air. So the strain in the air
      (`STRAIN_BACK`) is now that management: among seven sticks on the line in and out of
      the platform, from a full stick back to a full stick out, the one that holds Mario a
      sweep clear of the edge as it will stand a frame on while the top is within
      `FloorReach` above him (covered there he lands on the top, the game's floor search
      reaching that far up) and `StrainTarget` (1) outside it once he is deeper, for the
      next sweep to cover him; a hold just outside the edge at every depth landed the
      falls on the top and the yield fell to 22, 16 and 37 on the three seeds. With the
      hold by depth: 93, 64 and 36, then 33, 82 and 104 with the stick out among the
      choices, the yield of the falls along the edge as before, and still none of the
      3,981 falls straight off caught: those left at 2 to 16 a frame (median 12), the
      slow ones overtaken by the sweep within a frame or two and landed on the top, the
      fast ones 6 to 17 outside the edge on the sixth frame, where the stick's 1.5 a
      frame cannot bring them back before the sweep rule ends them. A fall straight off
      that is caught needs its leave at about the sweep's pace, 3 to 4 a frame, which the
      walk off from idle at the stick's magnitude 2 to 4 gives; what the maintainer's
      hand-made fall did exactly (its leave speed, its frames, where it was caught) would
      pin the rule, as the uscz movie pinned the oscillations.
      The dive recover's route, measured (2026-10-13; the maintainer: plenty of squish
      cancels by the brake, none by the dive recover). First a move for the landing's
      wait, `WAIT_FOR_TILT`: neutral frames from a stop until the tilt across the edge has
      just the steps the leave and the fall need (the recover lands with 0.30 across where
      the squish cancels leave at 0.26, and the sampled waits never strung the frames
      together); its leaves came at -0.27 to -0.19, none caught. Then three scratch traces on his ten
      `dr-final` roots at 29.6 to 29.8 (the corner, 50,000 shots a run): which rule
      refuses each departed state and on which frame of its fall, what each dive attempt
      ends in, and the speed before and after every minimum-downhill step of the run.
      What they say. (1) The dive is rare. Of the 575 to 700 attempts a run that complete
      their turn toward the aim, all but 1 to 14 end the turn at 28.0 to 28.9, under the
      29 the game's dive needs (`check_ground_dive_or_punch`): the turn starts from 31 to
      33 and costs 0.6 to 0.7 a frame over 6 or 7 frames, where the uscz movie starts it
      at 35.4. Half of the run's minimum-downhill steps were uphill, 0.5 lost where 1.3 is
      gained: the top face is two triangles whose normals the game's vertex rounding sets
      up to 15 angle units apart, and `GetMinimumDownhillWalkingAngle` reported a facing
      1 unit inside the downhill half of the look-ahead's triangle, which the triangle
      under the step put outside it. Fixed with `DownhillMargin` (64 units) in the
      look-ahead (docs/tasing.md, "Pitfalls"): the uphill steps from a given state went
      from 150 of 295 to none. The run still peaks at 34 on these roots and loses speed
      from there: it runs along the low edge 0 to 8 above the lava where the movie's run
      is 7 to 14 above it (the hand-over's normal is (-0.47, 0.22) against the movie's
      (-0.42, 0.26), which puts the low corner 12 units lower), so the fastest heading
      steps into the lava and the
      lineages that live have turned uphill. The margin changes the oscillation stage's
      swings too, the same look-ahead (4.12); no perf tier runs it (Tier D's stages are
      tilt-target ones, and Tier C runs frozen copies), so no baseline moves. (2) The
      landings came 254 to 307 along the edge, 30
      to 80 nearer the corner than the catches that cancel (194 to 225), because the aim
      was read with this frame's tilt along the run's axis, 0.27 at the dive, where the
      catch comes at 0.40: on the platform the normal approaches the goal Mario's position
      sets ((dx, 500, dz) normalized) a step a frame. `ExpectedCatchFace` now steps that
      axis too, toward the goal for the frames on the platform and toward flat for the
      fall's; the landings moved to 257 to 272. (3) The window rule judged a leave on the
      latest face the cancel allows, where a dive recover's leave, with 0.06 more tilt
      across than a brake's, has its edge 20 higher at the catch; it refused 194 of 243
      such leaves. The rule now judges the face expected once the fall's frames have passed
      (`ExpectedCatchFace` with the frames left), for the brake the same face as before.
      With the three: 1, 3 and 0 landings in three runs and 68, 37 and 94 squish cancels,
      all the brake's. Where the dive recover's falls end now, from the run with 3
      landings and 5,080 departed rows: the leaves come at -0.27 to -0.17 across (median
      -0.24), 121 to 143 above the lava, 233 to 281 along the edge; on the sixth frame of
      the fall Mario is 70 to 82 above the lava with the top 45 to 70 above him, short of
      the 78 the squish needs, and on that frame the tilt-spent rule ends 4,846 of the
      lineages. The side's steepness limit is not a fixed 0.15 across: it rises as the
      normal's y and the run's axis flatten in the air, 0.14 at the leave and 0.18 to
      0.20 on the sixth frame, so a fall loses 0.02 of its margin each frame, not the 0.01
      the budget counts, and a seventh frame is never there. The brake's catches are at the
      same limit on their sixth frame and are deep enough because they left from 138 to
      154 above the lava at 194 to 225 along the edge, where the top stands 145 to 160.
      So a dive recover that cancels needs its fall 30 to 40 further up the edge than it
      lands, which a leave along the edge gives (`LEAVE_ALONG`, 8 a frame over the stop
      and six frames of fall) or a landing there, which the turn cannot afford at 33.
      Open: the roots' tilt direction (the movie's hand-over keeps the run's edge out of
      the lava; a `dr-final` crossing with less tilt across and more along, its
      `minCrossingAxisDiff` nearer the movie's 0.16 than these roots' 0.25), and the
      recover's leave as part of its move. The example movie's own recover, replayed
      (`dll_play.py`): the dive at 3465 from 30.7, a three-frame rollout within the band,
      the landing stop at (234, 307) 116 above the lava, two frames of stop, a walk off at
      5.2 straight out at 106 above the lava, two frames with the top under him again as
      the edge swept out, the platform's displacement carrying him 7 out and 8 down, and
      the squish on the lava at 3482, 0 above it, with the edge at 99: a pancake, as 4.11
      measured before.
      Why the viewer showed no dive recover (the maintainer, 2026-10-13: not one dive
      recover action in the viewer, over many configurations). The recover runs. His
      newest ending run (31,729 shots, roots whose run reaches 35.7) has 1,140 landing
      stops by it, and 218,000 of its 315,000 rows are its lineages. One of its squish
      cancels is a dive recover's, replayed on the game like every saved solution: the
      dive at 3441, the pause at 3442 to 3444, the rollout's press at 3445, the departure
      at 3453 from a landing stop, and the push's -413 at 3461. The viewer could not show
      it, for two reasons. First, the recover was one move, from its turn to its landing
      stop, and the search writes a row only where a move ends: no row held the dive's,
      the slide's or the rollout's action, or the phase `DIVE`. Second, the viewer skipped
      the forced rows (a `sampledOnly` switch, true by default): 925 of the 1,140 landing
      rows were forced, and so was the row of the dive recover's squish cancel. Unreachable
      then, since no move ended on the slide or in the recover's flight: the `DIVE` phase's
      draw, the `ROLLOUT` move and the random B press on the slide. The maintainer's choices, the
      same day. First, `Sampled` is a checkbox column like any categorical one, in every
      tab, and the viewer reads every row: the `sampledOnly` switch is gone from
      `Visualization`, the pipeline's keys and the viewer, and CI filters the text sample's
      one forced row by it. Second, the recover is three moves, so that the viewer shows
      it: `Dive` (the turn, the dive and the pause, ending on the slide), `Rollout` (the
      press and the rollout's first frame) and `Landing` (the rest of the flight, to a
      landing stop in reach). Every frame after the pause is steered by the same compare
      as before; the dive phase draws only those two moves, and no random frame presses
      B, so the unreachable code is gone. Each of the three moves' rows is forced, so a
      new dive recover shows as `ACT_DIVE_SLIDE`, `ACT_FORWARD_ROLLOUT` and
      `ACT_FREEFALL_LAND_STOP` rows, and the dive phase's position is binned to the unit,
      so that two dives a few units apart stay two blocks. What it costs: a shot that
      starts from a slide or rollout block replays the steered rollout in each of its
      pellets, about 230 frames where an average pellet runs about 6, and a dive whose
      rollout cannot land in reach leaves its slide in the table. Measured on his
      `dr-final` roots, on the corner, deterministic, 30,000 shots, seeds 6 to 9, the
      build before against this one (docs/performance-changelog.md): solutions 400 and
      396 in all, landing stops 1,498 and 1,526, frame advances 3% more, loads 11% more,
      blocks 9% more, and 424 against 405 seconds over seeds 6 to 8; each run of the split
      wrote 249 to 278 slide rows and 311 to 359 rollout rows. The seeds' own spread, 58
      to 147 solutions with the same code, is wider than any difference the split makes.
      Squish cancels by both approaches, the same day. The maintainer's ending run of
      10:42 saved one dive recover squish cancel among its 621 solutions (number 560,
      replayed on the game like every saved solution: the dive at 3441, the departure at
      3453, the bully tried at 3461). His run of 13:32, with the split, saved 257: 245 by
      the brake and 12 by the dive recover. Of the dive recover's, all but one fell off
      from the landing stop itself, with no step of Mario's own: a NUT spot. The bully was
      tried at 3461 (one at 3462), with Mario 53 to 64 above the lava at x 249 and z 245
      to 261 from the pyramid's home, and the push left each idle at 413 on the frame
      after. The normal on that frame, the squish cancel's, against the setup's target
      (-0.1792, 0.393), in 0.01 steps toward flat: the brake's 244 at (-0.1692, 0.383), one
      step past the target on both axes, so a frame late, the normal having been the
      target's exactly on the frame before; the dive recover's 11 at (-0.1892, 0.323), one
      step short of the target on x and seven past it on z; one of each at x -0.1792, the
      target's, with z two (the brake's) and four (the dive recover's) steps past it. The
      stage ran without a target (`targetNx` is left out of its config), so it took every
      normal; with one, the probe would have refused them all.
      Pruning the corner, tried and dropped (the maintainer, 2026-10-13). Many of the ending's
      blocks are states in the corner of the squish edge, where the edge stands too low for a
      squish cancel. He asked for a rule that prunes them without ever cutting a legitimate
      option. His run of 16:51 (323,176 rows, 379 squish cancels) shows what they are: most
      of the corner's EDGE states move at 4 to 16 speed, many still heading into the corner,
      with 5 to 11 frames left before the side under the edge is too flat. Every solution
      that passes through the corner does it running at 25 to 28 back up the edge. The rule
      tried was a bound that proves a state hopeless from the game's own limits. Per facing,
      it tracked the most speed the walk allows: a turn of 2048 a frame, a gain of up to 1.1
      plus the slope's pull when facing downhill, the same pull lost when facing uphill, and a
      walk entered at up to 8. It added the platform's carry, and after the departure the
      air's speed and the stick's sideways push of up to 10. A state was kept if some
      departure could pass the departed window rule and some catch after it, within the
      probe's window and while the side is steep, could find Mario in the catch window. Every
      solution on disk was replayed through the game and checked against it (4,153 solutions
      from 18 runs, 7,420 frames on the platform), and no version refused any of them. How
      much it refuses depends on what Mario may do between the departure and the catch. In
      the run's EDGE rows: if he may land back on the platform and walk again, 0%; if he only
      falls, staying off the platform, 16% (48% if a catch cannot come within two frames of
      the departure, which holds at these speeds but is not proven). The third case is a
      hover over the platform: the edge's sweep covering him again, or the NUT cycle, in which
      he lands on a NUT spot, leaves the ground again at once, and moves down with the top
      while his falling speed keeps growing and the normal steps toward him. With hovers
      allowed and every part of the bound made sound, it refuses 0.4%. A first prototype said
      5%, because it took the carry after the departure from the departure frame's own
      normals and positions. The maintainer chose to drop it at 0.4%. Why the corner cannot
      be proven hopeless: a Mario who stays deep in the corner near the platform's centre line
      makes the pyramid tilt further toward the corner, which keeps the side steep down to
      almost no tilt across the edge, so a catch can still come late. The sideways push in
      the air moves him up to 10 a frame along the edge, and a hover keeps the side steep for
      its frames. The push's own limit does not help either. The bully pushes Mario at least
      54 along the edge toward the corner, and `CatchPlacements` refuses a push that would
      carry him past the top's end. But near this corner the pushed spot must also stand over
      100 above the lava, and the edge drops below that first: 140 units (median over the
      run's EDGE rows) before the push would overrun the corner. With that limit added to the
      bound's catch check, it still refused the same 96 of 22,142 rows. The push's limit is
      the nearer one only where the edge is nearly level along its length (the normal's z
      part about 0.2 to 0.3), which is not where the corner states are. Two ways to prune
      more were left untried: a rule that accepts one of the assumptions above, or an
      empirical rule, backed by a search for a squish cancel that starts from deep-corner
      states (cheap on the corner resource) and finds none. Facts
      verified on the way, for later rules: the pyramid's six triangles are the default floor
      class, so the slope's pull is 1.7 times the floor's steepness at any speed, and the
      brake takes 4 a frame but only begins from 16 speed. A walk entered from a slower action
      starts at up to 8 on any frame (`set_mario_action_moving`). Freefall's first frame does
      not move Mario down, since gravity is applied after the step; after 2 to 7 frames he
      has fallen 4, 12, 24, 40, 60 and 84. Before the departure Mario is always on the ground:
      stationary actions snap him to the floor, and freefall needs him 100 above it, which is
      already a departure. The pyramid's carry, how far it moves Mario with its tilt each
      frame, is the change of its transform applied to his offset from the home,
      `(T_new - T_old) . dist` (bhv_tilting_inverted_pyramid_loop). Over |nx| 0.02 to 0.7, ny
      0.5 to 1 and |nz| up to 0.85 it is at most 13.2 a frame across the edge, and there the
      top reaches 532 across and 434 along from the home.

- [ ] **4.12 The hand-over's lag: speed against a normal further back.** Opened 2026-10-07
      on the maintainer's word: the oscillation stage selects first of all for speed at the
      crossing, where the squish cancel is usually better served by giving up speed for a
      normal further back, behind Mario, so that he has the time to cross the platform to
      the squish spot before the face comes up under him; solving that dilemma is the
      stage's design question, and the one the ending waits on. What the measurements of
      2026-10-06 (4.6) say about it: the example's oscillation has both, since the
      crossing speed is itself made of the lag (the turn's 2 to 4 plus about 2 a frame
      until Mario's target normal catches the normal), so a normal 0.2 to 0.3 of sum
      behind what his position asks for gives the example its 30 at the crossing and its
      time at once; our lineages have spent the lag by the time they turn (a margin of
      0.02 to 0.07, the turn at the crest, the crossing 7 to 12 frames later), because
      the sum has to climb from the fixer's 0.55 to the regime, which only running outside
      the normal does, and because a pass keeps the fastest crossing of one oscillation,
      where turning inside pays two oscillations later; keeping the fastest per normal cell
      (4.6) widened what is carried without producing such a lineage, so the answer is in
      what the search tries and what it is judged by, not in what it keeps. Candidates: the
      hand-over judged by the margin, or by the frames from the turn to the crossing, in
      place of the crossing speed; the tilt set before the swings, the fixer handing over
      at the regime's sum or a rest between, so the swings have nothing to build; the
      turnaround offered and favoured up the slope, 6 to 8 frames into a climb of 0.3 or
      more, as the example takes it, and the swing's end steered up the face rather than
      along the edge. Done when the stage hands the ending roots at the regime's tilt with
      the normal behind Mario by a stated margin, the example's 0.2 or more, and the
      ending's run from them reaches the squish spot. The maintainer's sketch (2026-10-07),
      recalling `osc-final`: oscillate for speed to the regime and in it, the ten fastest
      carried each pass; from the third oscillation in the regime require the speed to
      improve on the oscillation two before; when no improvement is found, rewind to the
      last oscillation oriented away from the target quadrant and run one final oscillation
      without the speed restriction, for a variety of crossings. Against the stage as it
      stands: the first three steps are the passes, the keep and the regime speed rule as
      built (the rule compares two crossings in the regime, which is the third regime
      crossing on); the rewind is the fall-back, which today takes the last pass of the
      final pass's parity and hands its roots straight on; the new piece is the free final
      pass from the pass of the other parity. `osc-final` itself (the committed copy) was a
      brake-to-target search from the dr hand-over, its solution the normal within ten
      steps of the target after the brake, its fitness the speed on the run and the
      slowness in the brake, with no gate on the downhill peak; the downhill-peak
      comparison is the dr speed rule's own second condition. What the measurements say of
      the sketch: the last pass's solutions already span 12 to 24 at the crossing under the
      rule, the ending found nothing in them, and the lag is spent at the turn before any
      speed rule applies, so a free final pass widens the crossings without putting the
      normal behind Mario; the margin at the hand-over is the number to read it by. Built
      2026-10-07 as the maintainer asked: the per-cell keep removed (the fastest `keepTop`
      again), the final pass free of the speed rule and keeping every solution
      (`ValidateCrossingData`'s `speedRule`, false on the final pass; the unit test pins it),
      and the rewind running the final oscillation from the roots of the last pass whose
      crossing heads away from the squish-cancel quadrant (the parity of the final pass's
      roots), a free pass that finds nothing rewinding a pass pair further; the leg test's
      third part pins the rewind from the leg. The final oscillation starts from a crossing
      heading away from the squish-cancel quadrant and ends at one heading toward it, which
      is the committed parity (the last pass toward the far end). Measured the same day from
      the pass-1 roots, the speed rule on, 1,000 solutions a pass: at seven oscillations the
      rule never starved (passes 2 to 6 full or nearly, 28 to 30 at the second, 25 to 27 at
      the fifth, 5 roots at the sixth) and the free final pass found 116 at 12.7 to 21.5; at
      nine, the sixth pass found nothing, the stage rewound to the fourth pass's ten roots
      and the free final oscillation found 1,449 solutions in 53 cells at 9.9 to 27.1, in
      154 seconds; the ending's nothing from either hand-over is void (below). Five of
      the 1,449 replayed (the two fastest, the median, the two slowest; the ending's nothing in
      these scratch runs is void, their configs carrying `targetEdge` x, see below): the last turn before
      the hand-over ended 14 to 33 above the lava with a margin of -0.01 to +0.08, and at the
      hand-over the position asked for 0.72 to 0.78 of sum against the normal's 0.712, so
      the normal is not behind Mario in any of them: the free pass widens the crossings, as
      foreseen, without the lag the ending needs. The maintainer's second approach
      (2026-10-08) replaced the rewind: no rewinding; a pass under the speed rule that
      yields nothing drops the rule, runs again from the same roots without it, and the
      stage goes on without the rule to the final oscillation, which moves to the next
      oscillation after that pass whose crossing heads toward the squish-cancel quadrant,
      so two or three oscillations run without the rule (two when the failed pass's
      crossing heads away, three when it heads toward); only the final pass runs under the
      final pass's configuration and hands on every solution, the passes before it carrying
      their `keepTop` fastest (`speedRule` on `Scattershot_BitfsDr`, set by the stage; the
      leg test's third part pins the second attempt, with a pass made to refuse everything
      by a rule the speed rule has no part in, since a short run has no failure the speed
      rule alone causes). Measured the same day from the pass-1 roots at nine oscillations,
      the speed rule on: passes 2 to 6 under the rule (28 to 30 at the second, 27 at the
      fifth, 30 solutions at the sixth), the seventh found nothing, the rule dropped, the
      seventh again without it found 937 (its ten carried at 24.5 to 25.2), the eighth 833
      (24.1 to 24.9), and the final ninth 712 in 40 cells at 12.5 to 24.5, three oscillations
      without the rule since the seventh's crossing heads toward the squish-cancel quadrant;
      96 seconds; the ending's nothing from the 712 in that run is void, its scratch config
      carrying `targetEdge` x. What the ending does from them under the committed config, and
      what stands in its way, is 4.11's finding of 2026-10-08. The maintainer's third design
      (2026-10-10), replacing the second: no oscillations without the speed rule; a pass that
      yields nothing rewinds to the last crossing heading toward the squish-cancel quadrant and
      hands its roots on (the form of 2026-10-06); and a new rule for the lag itself, the
      crossing's normal at least `minCrossingAxisDiff` further along one axis than the
      other, from the second crossing on, set to the example's 0.26: at the example's
      crossings |nX| and |nZ| differ by 0.27 to 0.31 (0.21 at its first), the normal still
      pointing at the end Mario just left, where ours differ by 0.33, 0.29, 0.23 and 0.13
      after the -x end's turn and by 0.17 to 0.19 after the +z end's. Built the same day;
      the unit test pins the rule and the leg test's third part the rewind. Measured from
      the pass-1 roots at nine oscillations, the speed rule on: the second pass found 922,
      its ten carried at 29.0 to 30.2, the third 317, carried at 27.2 to 28.8, the fourth
      nothing in 2,000 shots, so the stage handed the third's ten on. Three of them replayed:
      crossings at 26.3, 30.0 to 31.7 and 28.1 to 29.8, the axes 0.27 to 0.37 apart at each,
      13 to 16 frames after the turn (where the lineages before the rule crossed 8 to 12
      frames after it at 21 to 27), the turns still past the edges (ending 35 to 56 above
      the lava at 308 to 384 from the home) and the margin at them still -0.06 to +0.06: the
      rule buys the later crossing and its speed without moving the turn inward. The ending
      from the ten under the committed config, the block cap raised to 4,000,000 so it could
      run on: 3,963,000 blocks, 1,647,000 rows sampled, 1,230,000 of them departed at 0 to
      16 off the edge, Mario squished in 17,370, no squish cancel (every probe a knockback
      or the lava boost), then the cap (4.14). The stage's own test cases pass; the hand-over
      is three oscillations now, since the fourth pass finds nothing under the rule. The
      maintainer's amendment (2026-10-10): the speed rule applies in the regime only, so the
      swings below it build tilt free of it and the stage reaches the regime before the rule
      can starve a pass; with a regime crossing never compared with one before it, the
      regime's first oscillation is free as well, and the rewind, holding its roots to the
      last pass's tilt floor, hands on an oscillation made in the regime or nothing. The
      `regimeSpeedRatchet` knob went with it. The `--test` case "the first oscillation comes
      from the fixer's hand-over in every corner" ran 100 minutes in its second corner on
      2026-10-11 and was stopped; on 2026-10-12 it ran through its four corners every
      time, in 22 minutes on master's code and about 55 on the branch, one thread, on the
      game, where the look-ahead's frame now costs a game frame per call (4.19). It
      crawls, it does not stall; its yields repeat run to run (the case is deterministic);
      the group is not CI's. Measured as before: the second pass 909 (ten
      carried at 29.0 to 30.2, their sum 0.672, in the regime already), the third 352 (28.0
      to 29.4 at 0.692), the fourth nothing, the third's ten handed on; the fourth dies on
      the speed rule itself, its crossing and the second's both in the regime (nothing either
      with the axis floor at 0.2 or off), the crossing speed not beating the second's 29 to
      30. So the regime's one free oscillation is the third pass, and the hand-over is the
      three-oscillation one at 28 to 29 until a crossing in the regime can beat the one two
      before it. Later the same day the maintainer had the rule apply below the regime as
      well, whatever the tilt did there too, the crossing across the regime's edge alone
      uncompared; measured once more from the same roots: the second pass 557 (ten carried
      at 28.8 to 30.2), the third 337 (28.2 to 28.7), the fourth nothing, the third's ten
      handed on, the same hand-over as under the regime-only form, since the second crossing
      beats the leg's end and the third the first crossing anyway.
      The uscz movie's crossings as the dr stage defines them (the first frame both
      components of the normal step toward the far corner), read 2026-10-13 from the game:
      frame 3336 toward +x+z at 11.6 of speed, the tilt 0.731 with the axes 0.049 apart;
      3369 toward -x-z at 29.7, 0.616, 0.394 apart; 3397 toward +x+z at 28.8, 0.688, 0.054
      apart; 3422 toward -x-z at 27.2, 0.636, 0.350 apart; and the hand-over, 3452 toward
      +x+z at 29.0, the tilt 0.687 with the normal (-0.423, 0.846, 0.264), 0.159 apart,
      Mario at (-229, 222) 20 above the lava. Its tilt over the last two oscillations runs
      from 0.616 to 0.688. Against the committed config: `minXzSum` 0.69 refuses the
      hand-over's 0.687; `startXzSum` 0.67 refuses the swings, which dip to 0.616;
      `minCrossingAxisDiff` 0.26 refuses every crossing toward +x+z (0.054 and 0.159;
      the 0.27 to 0.31 it was set from are the uscx movies'); and the gain-speed rule
      refuses the crossing at 3422, 27.2 after 29.7 at the one two before it. To admit
      the example's own oscillation: `minXzSum` 0.68, `startXzSum` 0.61,
      `minCrossingAxisDiff` 0.05 (0.15 admits the hand-over alone), and the gain-speed
      rule relaxed for the crossings toward the near corner.
      The maintainer's run with `startXzSum` 0.66, `minXzSum` 0.68, `minCrossingAxisDiff`
      0.15, `keepTop` 100 and 5,000 shots a pass (2026-10-13, 01:34): the crossings toward
      the squish-cancel corner hand on 24 to 28 at the first oscillation, 25.7 to 28.3 at
      the third and 24 to 26 at the fifth; the other way, 27 to 30 at the second and 25.8
      to 28.1 at the fourth; the sixth pass found 3 at 24.4 to 24.8, so the rewind handed
      the ending 1,333 hand-overs from the fifth and the third (speed 21.9 in the middle,
      28.3 at most, 4 of them at 28 or more with the axes within 0.2 and the tilt 0.67 to
      0.70), and the ending found 18 squish cancels, every one the brake's, squished at
      frame 3458, from the third oscillation's roots. The swings reach as far as the
      example's (x -442 to 54, z -170 to 412 against its -330 to 59, 30 to 317, in the
      sampled rows) and run as fast in them (30 to 33), so what the hand-over lacks is the
      speed at the crossing itself: the example's comes 14 frames after its turnaround at
      29, ours sooner, before the run has built it, which is this item's lag.
      The maintainer's thought (2026-10-13): oscillate at a lower tilt, where the swings
      keep their speed, and raise the tilt as fast as possible at the end; and the
      crossing need not be at 29, since the run builds speed after it (the ending's dives
      came from runs that began their turn at 32 to 33 five or six frames after a crossing
      at about 26). A scratch run of the dr stage toward that (the committed config with
      `startXzSum` 0.58, `maxXzSum` 0.68, `minXzSum` 0.68, `minCrossingAxisDiff` 0.15,
      `keepTop` 100, 20,000 shots a pass, from the fixer's solutions): the carried
      crossings held 28.2 to 29.9 at the first oscillation, 29.0 to 30.4 at the second and
      28.2 to 30.6 at the third, at tilts of 0.61 to 0.67, where the run of 01:34 at the
      floor 0.66 had 24 to 28, 27 to 30 and 25.7 to 28.3; then the fourth, pinned at the
      ceiling's last cell (0.672), fell to 20.8 to 24.0 with 370 found, and the fifth
      found nothing, the exit's 0.68 a step above the roots. So a low tilt does keep the
      crossing's speed, and the stage has no way yet to raise the tilt at the end: a
      ceiling under the exit tilt is refused by the config check (`maxXzSum` not below
      `minXzSum`, Stages.cpp), and a last pass from roots under the exit must climb in one
      swing, which 20,000 shots did not do for a step. The climb at the end is the
      stage's design question, the maintainer's.
      Built on the branch `osc-two-stages` (the maintainer's design, 2026-10-13): two
      oscillation stages. The first oscillates at a lower tilt and ends the near way
      (`lastCrossingDirection`: "far" is Mario leaving the last crossing toward the squish
      edge's corner, which the ending needs; "near" the other way); the second, `dr-final`,
      takes the first's output as its input and continues its lineages, beginning at the
      oscillation after the input's with the input's solutions as the roots the pass
      before carried, its `maxOscillations` the oscillations it adds, one here, which
      ends far; the count of oscillations is settled in one place (`settleLastPass`: the
      passes alternate, so one more where the last would cross the wrong way). The first
      run (the committed config with the first stage at `startXzSum` 0.58 and `minXzSum`
      0.62, from the fixer's solutions, 5,000 shots a pass; the final stage 20,000): the
      first stage's passes carried 25, 28, 27.5 and 27 at oscillations 1 to 4, its fifth
      found nothing and the rewind handed on 864 near crossings at tilts 0.63 to 0.71
      (speed 24.3 in the middle, 29.7 at most, 224 at 26 or more); the final oscillation
      found 353 far crossings at tilts 0.69 to 0.73, all 353 replayed on the game, at 12.7
      to 25.7 of speed, 21 in the middle: the swing that raises the tilt costs four to six
      of speed, and the ending found no squish cancel from them in 100,000 shots. The
      maintainer then asked for `keepTop` on the final stage's hand-over too: `lastKeepTop`
      (0, all, by default; the committed `dr-final` keeps 100) trims the last pass's
      solutions to the fastest, and the rewind's hand-over the same way. A second run of
      the same lineup: the first stage's fourth oscillation found nothing and the rewind
      handed on 912 near crossings from its second, the final oscillation from them found
      2,040 far crossings at tilts 0.69 to 0.71 and handed on its 100 fastest, 27.9 to
      29.6, the axes 0.17 to 0.29 apart, and the ending found 3 squish cancels in 50,000
      shots from them, the brake's, with 165 dive recover landings sampled and none of
      their falls caught (4.11). The final pass's yield swings between runs: from the
      first run's 864 roots it found 353 in the pipeline and then 0 three times when run
      alone from the same file at 20,000 and 50,000 shots, the stage not deterministic in
      the config. What to try next: a smaller climb (the first stage's exit tilt nearer
      the final's), more shots on the final pass, and the first stage's own settings,
      which the maintainer meant to lower further.
      The run after the hand-over, measured 2026-10-13 (4.11): half of its minimum-downhill
      steps were uphill until `GetMinimumDownhillWalkingAngle` kept its facing
      `DownhillMargin` inside the downhill half (the top face's two triangles), which the
      oscillation stage's swings draw from too. Measured once each way from his `are-fix`
      rest, the passes' crossing speeds were alike: 27.1 to 27.9, 30.2 to 30.7 and 28.1
      to 29.2 at the first three crossings with it, 27.1 to 27.9, 30.3 to 31.1 and 29.4 to
      29.8 without it, and the fourth found by neither, so no difference beyond one run's
      spread. No perf tier runs the look-ahead, so no baseline moves. On his `dr-final`
      roots the ending's run then goes along the low
      edge 0 to 8 above the lava and peaks at 34; the movie's hand-over, with less tilt
      across and more along, keeps that run 7 to 14 above the lava and reaches 35.4
      before its turn.
- [x] **4.13 A BitFS resource: the pyramid corner of the game reimplemented from the decomp.**
      Opened 2026-10-11 on the maintainer's word: a resource that runs only what the
      squish-cancel search needs of the game (Mario's physics, the lava, the two tilting
      pyramids, the track platform), so that the stages can run on it or on `LibSm64`,
      the one for the search and the other to compare and to check. Decided the same day:
      **same structs, same symbols.** The resource owns the decomp's own types from
      `sm64/` (`MarioState`, `Object` with its `rawData`, `Surface`, `Camera`) and serves
      the symbols the scripts already read, `gMarioStates`, `gObjectPool` with the same
      slots (84, 83, 85, 27, Mario's 89), `gCamera`, the `bhv*` sentinels; so
      `(MarioState*)ReadState("gMarioStates")` is the same line on either resource,
      `VerifyLayout` and `BitFsExpectedObjects` run on it unchanged, and hard rule 2 holds
      by construction. Not the shape of `PyramidUpdate`, whose `PyramidUpdateMem` is a
      parallel model (its own `Sm64MarioState` and `Sm64Surface`, and an empty
      `UpdateMario`), which is why every user of it converts through
      `ExportSave<PyramidUpdateMem>` and no `LibSm64` script can run on it.
      Why (docs/performance.md): in `fixed` mode a frame advance is 14.9 us, a save 42.7
      and a load 41.2 (Tier B); the game is 62% of the measured run's CPU and loads 13%,
      and 95% of the frame advances replay known inputs because saves are rationed by
      their cost. A state of ten to fifteen kilobytes (one `MarioState`, the modelled
      objects, the camera, the transformed dynamic surfaces) makes a save or load a memcpy,
      about a hundred times cheaper, and a frame Mario's quarter steps over the cell's
      surfaces plus the pyramid loop, five to ten times cheaper; and with saves that cheap
      the agnostic cost model saves nearly everywhere, so most of the replay share goes,
      the gain no change to the DLL path reaches (ARCHITECTURE.md, "Resource and
      savestates": the ratio of a load to an advance is what decides which algorithms are
      affordable). Expected: five to ten times on the throughput runs, about two on the
      deterministic run, whose CPU is 56% outside the resource already (the ticket queue,
      3.15); the per-frame framework cost on the mock is 0.2 us
      (`BM_Script_ExecuteAdhoc_OneFrame`) and is not the limit. Kilobyte states also reopen
      4.3's per-block savestate cache, closed on gigabytes.
      What the stages read of the game, measured in the ending, dr and approach scripts
      (2026-10-11), and so what the port must carry beyond the pyramid loop (bit-exact
      already, 3.3): the actions walking, turning around and finishing it, braking and its
      stop, decelerating, idle, dive, dive slide, the forward and backward rollouts,
      freefall and its landings, squished, and backward air knockback; the bully of slot 27
      (`oBullyPrevX/Y/Z`, `oBullyKBTimerAndMinionKOCounter`, `oIntangibleTimer`,
      `oInteractStatus`, `oAction`, `hitboxRadius`), so its behavior and Mario's collision
      with objects are not optional for the ending; ceilings and walls (the squish's
      steep-ceiling walk, the pushes at the edge), found in the game's cell order, since a
      tie takes the cell's first surface; Mario's animation table, since the turn's finish,
      the landing stops and the rollouts end on `is_anim_at_end`; the camera's yaw, which
      Mario's intended yaw adds: mode 14 (8 directions) at -16384 through the frames
      traced (3319 to 3330), a mode that turns only on a C press, which no BitFS script
      makes, so the resource holds the constant and refuses a C press; both pyramids (the
      `pyramid_-2866_*` movies are solutions on slot 83's); `gGlobalTimer` once. Imported
      from the DLL at construction, never written by hand: the static surfaces with their
      partition, the animation headers, the objects' collision data. The object set,
      settled the same day from `dllcheck --objects` through `scripts/dll_symbols.py` (89
      live objects at 3330): at the pyramids' height nothing but the two pyramids and, 725
      units from slot 84's home, a `bhvStaticObject` (one of 23, a model with no behavior
      and no collision); every other object is on the level's upper floor, 3,500 units and
      more above (the red coins, the amps, the flamethrowers, the sinking platforms, the
      other three small bullies at y 410 to 1,331), or across the lava (the track platform
      3,866 units away; slot 27's bully 4,694, at (2598, -2764, 465), which only the
      ending's probe brings to the pyramid). So the resource models Mario, the two pyramids
      and the bully the probe places, and the track platform only if a stage turns out to
      read it (one read today). The trace's RNG seed stays 0 over the frames traced, so no
      object seems to draw from it; the harness below says, rather than the assumption.
      The scripts: class templates on the resource with explicit instantiations at the
      bottom of their `.cpp` (`template class Scattershot_BitfsScEnding<LibSm64>;` and the
      other), so the header split and the compile times stay, and a stage picks its
      resource from `config.json`; `Script`, `TopLevelScript` and `ScattershotThread` are
      templates on the resource already, so the framework does not change. Two things do
      want rule 10's word before they are written: `ImportSave` of a `LibSm64Mem` state
      into the resource (the constructor-from-`Resource<Sm64Mem>` shape `PyramidUpdateMem`
      has), and what a state's surface pointers (`m->floor` into the resource's pool) do
      when a save crosses resources between threads. A consolidation falls out: the
      physics reimplemented twice (`tasfw-core/src/decomp` and `PyramidUpdate`, Phase 5's
      "One declared source") has this resource as its one home, and both retire into it.
      Two nets, the first built before the physics (2026-10-11): the parity harness,
      `test_libsm64_parity.cpp`, in the drift test's shape: two resources from the same
      state under the same inputs, and after every frame everything the search reads
      compared bit for bit (every value field of `gMarioStates`, Mario's object, his
      platform and the surfaces under, over and beside him, the corner's four objects with
      their transforms, the camera, the timer and the RNG seed; pointers as what they point
      at). The fuzz: segments of up to 150 frames from a save at the anchor, the stick on a
      random walk, B and the odd Z, never A or a C button, each segment ending on the frame
      Mario touches the lava, the search's own boundary; 6,000 frames in the suite (under a
      second), `TASFW_PARITY_FRAMES` for a long run. The game against a second copy of
      itself first, since the comparator must find nothing there: a million frames
      identical. Then the whole game against the cut one: a million frames identical in the
      corner (931,812 of them on the pyramid, 45,091 airborne, 17,827 of 18,667 segments
      ended in the lava), after a first run without the boundary found the whole game with
      a floor at -3058.7 where the cut had the lava at -3071, 132 frames into a segment:
      the boosts had carried Mario across the lava sea onto a sinking platform the cut
      unloads, 3,000 units from the corner, which is why the boundary is where it is. A
      port is one decomp function at a time, its fields turned on in the diff as it lands,
      and a mismatch is a bug, never a rate (hard rule 1). The solution movies are the
      second input set since 2026-10-12: every movie the pipeline exported under
      `analysis/m64` (the fixer's dives and rollouts, the oscillations, the ending's
      leaves with their C-up) is played from the frame the movies leave the source, 3250,
      on the corner and on the game cut to the corner's objects, compared after every
      frame; 40 of them in a test run, all with `TASFW_PARITY_MOVIE_LIMIT` 0. The whole
      game is not the reference there because the way onto the pyramid passes a yellow
      coin (slot 23, on the pyramid's top; frame 3277 of the fixer's movies) that the
      corner has not got and Mario never meets on it: it changes his coin count and
      heals him, nothing the search reads, and the first run found exactly that
      (`mario.numCoins: 2 vs 1`). Against the cut: all 86,441 movies to their end with
      no difference, 16,810,738 frames, 13,881,459 of them on the pyramid, in 8 minutes.
      That the coin is all the whole game adds along those paths is its own case: the
      whole game with that coin unloaded against the cut game, everything compared (the
      camera, the particles and the other objects' effects on Mario included), the same
      86,441 movies and 16,810,738 frames with no difference. So the corner differs from
      the whole game along the solutions in the coin count and the heal, and in nothing
      else. The second net: every solution found on the
      resource is replayed on the DLL before it is exported, a few hundred frames each, so
      the resource can miss a solution and never invent one. Floating point: contraction off on every compiler (clang's default is
      on), under the flags the pyramid's bit-exact port already runs on.
      Measured first, before any physics (2026-10-11, `dllcheck --keep-objects`, the
      neutral-input frame at 3330 of the JP movie, 2,000 frames timed and the pages
      written the median of 30, `fixed` mode on tyler-desktop; docs/libsm64.md): all 89
      objects, 15.5 us and 52 pages a frame; every object but 84, 83, 85, 27 and Mario
      deactivated the way `obj_mark_for_deletion` does it and unloaded by the next frame,
      6.3 us and 17 pages; the pyramid and Mario alone 5.5 us and 14, each of the other
      three about 0.3 us. So the rest of the level is 60% of the frame, and the floor the
      resource has to beat on an advance is the 5.5 us of the game loop, Mario and one
      pyramid with its collision reload, against which its saves are the hundredfold and
      its advances two to five times. The same cut is a 2.4x on every advance of the DLL
      path for a write before the start save, which keeps hard rule 1's function of (start
      save, inputs) and replays; whether the search's outcome survives it was the harness's
      first question, answered for the corner (a million frames identical, below); the
      pipeline's use of it, a write into the game before the start save, is a rule 1
      question for the maintainer, open. Then the resource's own Tier B row. Port order, by what the
      ending exercises: surface collision, the ground and air steps, the actions, the
      interactions (lava, bully), the bully, the track platform last (one read, slot 85
      at -5744). The dr stage needs the first half and benefits first.
      The resource's skeleton, 2026-10-11: `BitfsCorner` (`tasfw-resources/inc/BitfsCorner.hpp`;
      the name is the maintainer's to change), three parts. `BitfsCornerLevel` is the level
      as data, read once from the running game at a frame inside it and shared by every
      resource in the process, so a run loads the game once rather than a copy per thread:
      the static surfaces with their partition node for node in the game's cell order (a
      tie between floors goes to the first in a cell's list, so the order is physics), the
      four behaviors' scripts to their loop's end, the three surface objects' collision
      data. `BitfsCornerMem` is a state, 41,504 bytes: Mario, the five objects by their
      game slots, the sixteen object list heads, the camera, the controller, the timer,
      the RNG seed, the time stop, the dynamic surfaces with their nodes and partition
      (112 and 300 at 3330, every surface object in Mario's tangible range, modelled or
      not; 160 and 640 is the room), the frame and the next inputs; a state records the
      addresses its pointers were valid at and a load elsewhere relocates them, which is
      how a state crosses threads and how one converts from the game
      (`BitfsCornerMem(game, level)`, the shape `ExportSave<Sm64Mem>(level)` takes).
      `BitfsCorner` owns the live globals and a full 240-slot pool on the heap, serves the
      scripts' symbols from them (`gMarioState` and the other pointer-to-pointer symbols
      as the game has them, the behaviors by name as the identity an object's `behavior`
      carries), refuses any other name, and `advance` refuses until the frame is ported.
      `test_bitfs_corner.cpp` checks the level against the game's own counts (1,215 static
      surfaces, 3,649 nodes, 377 non-empty lists identical), the imported state against
      Mario and the pyramid in the game, a save and a load, and a state moved to a second
      resource with every pointer landed in its memory. Two new copied headers carry it,
      `sm64/SurfaceLoad.hpp` and `sm64/Controller.hpp`, pinned like the rest
      (docs/decomp.md); the structs they add, `SurfaceNode` and `Controller`, are read out
      of the game and are in the layout table `test_sm64_layout.cpp` checks since
      2026-10-12, with the camera's four (`PlayerCameraState` and the C-up mode's three):
      13 structs, 851 fields, and `scripts/dll_layout.py` sizes a struct the game only
      embeds from its embedding, or checks its fields alone (docs/libsm64.md, "Struct
      layouts"). Found on the way and owed by the frame's port: the game nulls an
      object's `throwMatrix` in its geo pass after drawing it (`geo_process_object`), and
      that pass is where an animation's frame advances, so a frame boundary has the
      matrix null and the corner's frame has to run that part of the pass for its objects
      (the pyramid's behavior sets the matrix again before its collision loads); and
      `gObjectListArray` has sixteen heads of which the game initializes `NUM_OBJ_LISTS`,
      13, the rest null.
      The frame, ported 2026-10-11 (the maintainer's go on the shape the same day): the
      decomp's files for it (surface_load and surface_collision, math_util,
      behavior_script's `cur_obj_update` with the four commands the corner's loops use,
      object_helpers and obj_behaviors as far as the bully reaches, object_list_processor's
      update order, platform_displacement, object_collision, interaction's path to the
      lava, the death barrier and the bully, mario and mario_step with the stationary,
      moving, airborne, object and automatic action files whole and `act_squished` of the
      cutscene one, the pyramid's and the bully's behaviors, game_init's
      `adjust_analog_stick`, graph_node's animation frame and rendering_graph_node's
      animation step) copied at Refresh 13 into `tasfw-resources/src/bitfs-corner/decomp/`
      and included into the body of one class, `BitfsCornerGame`, whose base is the
      state's globals under the decomp's names and whose other members are the pool, the
      level and the transients: 45 copies, each pinned with its residual
      (scripts/decomp_pin.json; docs/decomp.md, "The BitFS corner's copies"). The four
      objects (the two pyramids, the track platform and the bully) are each optional and
      off unless asked for (the maintainer, 2026-10-11: `BitfsCornerObjects`, the state
      import's parameter, so `ExportSave<Sm64Mem>(level, objects)` names what a run
      has; the default is Mario alone); an object left out has an empty slot, its surfaces
      are gone after the first frame, and a pointer the game had to it is null. The track
      platform's path (40 waypoints) is the level's, read from the object's start waypoint
      and re-pointed at import like the collision data; its init native, which chose path
      and collision from the game's tables, is a refusal, and so is its moving (it spawns
      its track balls), which the search never asks of it. What a copy reaches that the
      corner has not got is a refusal (a spawn, a warp, a camera mode, a cutscene action, a
      behavior command or interaction type outside the four and the one the corner's
      scripts use); sound, the scene graph and the debug text are no-ops. Settled on the
      way: a CALL_NATIVE resolves by its position in the script it is run from, since the
      game exports not every native (`bhv_mario_update` is not one) and the scripts'
      definitions in `data/behavior_data.c` name them in order; an object's behavior stack
      is relocated with its command pointer (END_LOOP pops it); `AVOID_UB` is defined for
      the copies, which is the game's build's own choice and which makes sm64.h's
      `GET_HIGH_S16_OF_32` the shift rather than the big-endian pointer trick, the one
      wrong-half read that cost an afternoon; Mario's animations are the level's, all 209
      resolved once, and the one load in `set_mario_animation` is a pointer; a pointer to an
      object outside the model (Mario's `interactObj`, the coins he took) is compared as
      absent; and the particles are not modelled, so `oActiveParticleFlags` is not compared.
      Measured by the harness, 2026-10-11, the corner with all four objects against the
      game from the anchor: under neutral inputs 1,000,000 frames identical; under the fuzz
      1,000,000 frames identical (931,812 on the pyramid, 45,091 airborne, 17,827 of 18,667
      segments ended in the lava: standing, walking, turning, braking, the dive and the
      rollouts, freefall and the landings, the lava touch), on MSVC and on clang-cl; and
      the corner with the pyramid alone against the game cut to the pyramid, 1,000,000
      fuzz frames identical (against the whole game they diverge where the boosts carry
      Mario to the other pyramid, which is the option doing what it says). The bully's
      interaction, the same day: the harness places it on both sides at every segment's
      start the way the ending's probe does (`PlaceBullyAtRandom`: 40 to 200 units from
      Mario, up to a quarter turn off his facing, at up to the probe's speed, pushing or
      chasing), and the bully's death, which spawns smoke and a coin the corner refuses,
      ends the segment rather than the run (`Options::refusalEndsSegment`, the one refusal
      accepted by design, since the search reads the probe's frame and never the death).
      Under neutral inputs 1,000,000 frames identical (26,198 segments, 3,609 ended by the
      death), under the fuzz 1,000,000 identical (43,383 segments, 2,768 by the death):
      the push, the knockback, the chase, Mario's attacks on it. The track platform
      stands 3,866 units away through all of it; its behavior runs, and its moving (which
      Mario riding it would start) is a refusal the search never meets. Tier B, the same
      evening: `^BM_BitfsCorner_` and `^BM_BitfsCornerScaling` in `bench_libsm64.cpp`
      (docs/performance.md, "Tier B"), the game's rows on a corner imported from the game
      at the rows' anchor, the frame-advance rows in the 193-frame window before Mario's
      death there (the game's row plays through the death and the level's reload;
      `windowFrames`, exact). Measured (MSVC, tyler-desktop): a frame 1.82 us against the
      game's 14.5 to 15, a save 1.9 against 42.7, a load 0.68 against 41.2, `addr` 7.7 ns
      against 12.5, the state 41,784 bytes; the frame scales 99% to 8 threads and 77% at
      16, the save 82 / 76 / 41 / 8%. The save's cost and its scaling are the state's
      shape: the slot manager's pool moves a state into a map node per save and back per
      erase, which `LibSm64Mem`, its buffers behind pointers, pays as a swap and the
      corner's flat 41 KB state as a copy each way plus a 41 KB node, three copies and an
      allocation per save where a load is one copy, so sixteen threads meet in the process
      heap. The maintainer's rule and decision (2026-10-11): a state is copied when it is
      saved to or loaded from a resource and never inside the framework, so the slot
      manager, not the corner, changed: it pools the map's own nodes (`std::map::extract`),
      a save into a recycled slot is that one copy and no allocation, and no state type has
      to know about it (`test_slots.cpp`, "A recycled state is the same object"). Found on
      the way: `addr`
      compared through a `std::string`, an allocation for every behavior name a script
      asks for (a `std::string_view` now). The suite against the 2026-09-15 baseline: no
      count, allocation or efficiency regression, Tier D unchanged, the corner rows NEW;
      four Tier A rows read +11 to +16% against that reference and the same against a
      build of HEAD itself, a shift in the merged history, not here
      (docs/performance-changelog.md). The two families' baselines were added to both
      compilers' directories by a filtered `-SaveBaseline`, which adds or replaces just
      the run's families and leaves the other families and the reference binaries alone
      (`perf_compare.py baseline --add`, written for this on the maintainer's ask, since a
      whole save would have re-measured thirteen families and replaced the reference for
      two new files); the corner rows gate absolute until the next whole save. The slot
      manager change, measured the same evening (docs/performance-changelog.md): the
      corner's save 1.9 to 0.7 us, its load's cost, and 0.7 us at sixteen threads where it
      was 19.6 (efficiency 10 to 94%); one allocation fewer per save on every resource;
      the slot manager's own rows 6 to 19% faster; Tier D's counts unchanged.
      How a script runs on either resource, decided 2026-10-12 after the template form
      was shown and found ugly (every child-script call `this->template Modify<X<TResource>>`
      on Clang and GCC): the two resources are kinds of one, `Sm64Resource`, an abstract
      class over `Resource<Sm64Mem>` that adds nothing, and `Sm64Mem` is the one state type
      they share, holding either's savestate (the corner's behind a pointer, so a game
      slot's node stays small) or nothing until a save fills it, and refusing to hand one
      resource the other's; a script is `Script<Sm64Resource>` and pays nothing per frame
      for not knowing which it has, the operations it reaches being `Resource`'s virtuals
      already. Done the same day in tasfw-resources alone (`Sm64Resource.hpp`, `Sm64Mem.hpp`,
      the state types in `LibSm64Mem.hpp` and `BitfsCornerMem.hpp`), no framework file
      touched, the suite's table in docs/performance-changelog.md: every steady-state row
      and Tier D within noise, one allocation more per fresh corner slot (the block behind
      the pointer, which the pool then recycles). The scripts' one-line change to
      `Script<Sm64Resource>` is done (every script in tasfw-scripts and the pipeline's, the
      two test scripts on the game, the examples in docs/tasing.md; `VerifyLayout` keeps
      the DLL's header for `LibSm64ObjectPoolCapacity`; the frozen perf workloads stay
      `Script<LibSm64>`, which is still valid): built and tested on both compilers, the
      dry run passing, Tier D within 1% of the reference with every count unchanged
      (docs/performance-changelog.md). Until the resource switch, `StageContext` still
      holds the `LibSm64` vector and the scattershot stages hand the builder
      `static_cast<Sm64Resource*>` pointers into it. The pipeline's switch, the same day:
      a stage's `"resource"` (`game` or `corner`, with `"objects"` and `"lead"`; README.md,
      "Configuration"), `StageContext` holding `Sm64Resource*` per thread and the game
      beside them, the corners imported by a script (`CornerImport`, `ExportSave` of the
      level and of a state) `lead` frames before the start frame, and every corner
      solution replayed on the game and on a corner (`ReplayProbe`, `VerifyOnGame`) before
      it counts. The deterministic Tier D stage on the corner
      (`perf/tierd-deterministic-corner.json`, the row `TierD_DeterministicCorner`) is the
      game's search bit for bit: 52 solutions in 600 shots, 111,860 blocks, 524,380
      scripts, every solution's inputs and metrics identical, all 52 reproduced on the
      game, in 18.4 s against 94.6 (docs/performance.md, "Tier D"). Found on the way: the
      framework's stack overflow on a frame before the start save (4.17), and that the
      ending's C-up leave presses a C button, which the corner refuses because its camera
      is held: an exact C-up needs the camera's first-person mode from camera.c (the
      `CAM_MOVE_C_UP_MODE` flag that sets Mario's `INPUT_FIRST_PERSON`, the mode's entry
      and exit, and the yaw his stick is read against while it lasts; the ending's random
      button draw weights C-up 0.1 as well, so the move cannot be left out by config), the
      corner's next port (the maintainer's decision, 2026-10-12: port it). And that a
      death ended a corner script where the game plays on: the ARE fixer's dive trials
      reached the lava, Mario died, the game plays 48 frames of death and reloads the
      level, and the corner refused the warp (`level_trigger_warp`), which ended the
      whole fixer rather than the one trial. The maintainer's rule (2026-10-12): any
      death action simply fails validation, and the reload is never reached, on either
      resource; `MarioDeath::IsDying` (tasfw-scripts/inc/MarioDeath.hpp: a lava boost,
      health below a segment, a death action, no floor) fails the three searches' state
      validation and the fixer's trials at the first dying frame, and the corner's
      refusal at the warp stays as the backstop a script must never reach. With it the
      fixer runs on the corner: the same hand-over as on the game to the float, in 0.3 s
      against 1.8, and a quarter fewer frames on both (docs/performance-changelog.md); a
      short dr pass reproduces 1,000 of 1,000 solutions on the game. The camera's C-up
      mode followed the same day: `decomp/camera.inc`, the game's own text for the mode
      (its entry on a C-up press in the 8-direction mode, the zoom in, Mario's head on
      the stick, the exit on A, B or another C button, and `set_camera_mode` for his
      first-person action), the rest of the camera held as before (Lakitu's flight and
      the other modes are not modelled; C-left, C-right and C-down, which would turn or
      zoom it, stay refusals), the mode's four statics in the state and read out of the
      game at the import (the DLL exports them), its three structs in `sm64/Camera.hpp`
      beside `Camera` and in the layout table. The harness's fuzz presses C-up one frame
      in fifty and compares `gCameraMovementFlags`: exact over a million frames in each
      of the five corner cases, the camera's yaw never turned by it (the 8-direction
      mode's yaw holds through C-up, since the game runs no Lakitu transition on the way
      in or out of it from that mode). The pause followed (4.22), the same harness exact
      with it. The ending stage runs on the corner: deterministic,
      seed 6, 300 shots from the dr solutions, 20,253 blocks and 118,508 scripts on the
      corner and on the game alike (no solution at that length on either; the game found
      3 in 8,000 on 2026-10-11), 1.3 s against 5.8 (docs/performance-changelog.md). Found
      on the way: with the CSV off, a stage whose `ForceAddToCsv` says yes writes to a
      file that was never opened (4.18). The solution movies followed the same day (above,
      with the harness), and then the last condition, `PyramidUpdate` and
      `tasfw-core/src/decomp` gone (the maintainer's word, 2026-10-12: the stand-in is
      deprecated, what it was made for the corner now does, and the calculation belongs in
      the same script hierarchy). `GetMinimumDownhillWalkingAngle` is one script on
      `Sm64Resource` now: a frame of its own, reverted with it, after which it reads the
      normal of the floor under Mario (the surface objects update before the player in a
      frame, so that normal is the one his next step walks on whatever the frame's
      inputs); its 14 callers, the frozen Tier C copies among them, run it as
      `Test<GetMinimumDownhillWalkingAngle>`. Its first version kept the `Surface*` of
      Mario's floor across the frame and read the normal there, and the dr stage's
      second oscillation then failed in six runs of seven (the maintainer, 2026-10-12:
      the first oscillation used to always succeed). The cause, traced on a deterministic
      run: the game rebuilds its surface pool every frame, and the pyramid's six surfaces
      move down six slots whenever the other pyramid's collision unloads, at 1000 units
      from Mario, which is right where the oscillations turn around; the kept pointer then
      points at a slot nothing reloaded, still holding the old normal, so the script answered
      the tilt before the step where the stand-in had answered the tilt after it (169 of
      196 stale answers in 1,000 shots; 6 more were Mario crossing to the top face's other
      triangle, 21 the surface's own rounding). The script now reads Mario's floor again
      after the frame (docs/tasing.md, "Pitfalls": a `Surface*` is good for one frame);
      on seed 6 the dr stage reaches the same passes as it did on the stand-in, 1,000
      solutions at the second oscillation where the kept pointer gave none
      (docs/performance-changelog.md). With that the three files
      of `tasfw-core/src/decomp` and their headers, the stand-in's three files, its drift
      test and the `DownhillAngle_PyramidUpdate` row were removed; the dr metric's 0.01 step is
      its own helper; `test_libsm64_downhill.cpp` runs Mario from the anchor's rest until
      the normal is moving, then checks the script's frame is reverted, its angle is
      Mario's floor read after that frame and nearer the normal after the tilt than
      before it, and the game and the corner answer alike.
      The dr stage, deterministic, 150 shots from the fixer's solutions, before and after:
      the same 1,000 solutions, 3,385 blocks and 20,981 scripts on the corner and on the
      game, the corner's frame advances 139,675 to 139,696, the game's 89,747 to 89,659
      (docs/performance-changelog.md). The whole dr stage on seed 6, deterministic, is
      the same search to the block on either resource and takes 84.3 s on the corner to
      438.6 s on the game, about five times faster in every pass; a Debug build hides
      that, since the corner's frame is this repository's own code (README.md, "Running
      the pipeline"). Every condition below is met.
      Done when: the harness is exact over its fuzz and the solution corpus; every stage
      runs on either resource from the config; a Tier D run on the resource reports its
      delta table and work counts beside `LibSm64`'s; every solution the resource finds
      reproduces on the DLL; `PyramidUpdate` and `tasfw-core/src/decomp` are gone.
- [ ] **4.14 The block cap ends the process, not the run.** Found 2026-10-08: `UpsertBlock`
      throws "Block cap reached" at `MaxBlocks` from inside the threads' parallel region, so
      the exception is never caught, `std::terminate` ends the process with 0xC0000409, and
      nothing is written: no solutions file, no summary, the stage's log ending mid-line
      after a loop's statistics. ARCHITECTURE.md and docs/tasing.md say the run stops at
      the cap, which is the intent; the ending stage hits it at about 970,000 blocks in
      every run that gets going (4.11), so its 50,000-shot runs have never finished. Fix:
      the cap ends the search as the shot and solution caps do, the blocks found so far
      kept and the solutions written, with a line saying the cap was hit; a bug fix behind
      an unchanged interface in tasfw-scattershot (the documented behavior), with a case in
      `test_scattershot_mock.cpp` that reaches a small cap, and the suite's delta table
      (rule 8). Until then a stage that may reach the cap needs `maxBlocks` raised or its
      shots cut: the committed config's cap is 10,000,000 since 2026-10-12, about 1 GB of
      blocks with their segments, the cap's behavior unchanged (the maintainer's choice).
- [ ] **4.15 The bare scattershot builder does not compile when used.** Found 2026-09-15
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
- [ ] **4.16 The scaling family's efficiency gate trips on unchanged code.** Seen
      2026-10-11 while measuring the slot manager change (docs/performance-changelog.md):
      `^BM_LibSm64Scaling` is gated on efficiency, the rate at n threads over the rate at
      one thread, within 5 points of the reference at 2, 4 and 8 threads. Two runs of the
      same binaries, minutes apart, tripped it on different rows each time (`SaveErase` at 8
      threads by 5.1 points, then `FrameAdvance` at 2 and 4 by 6.3 and 5.1), while every
      per-thread time stayed within 3% of the reference: the one-thread row that divides
      every other moves by a few percent between runs, and the family is unpinned by
      design. Fix: measure the run-to-run spread of the ratio over, say, ten runs of one
      binary and set the tolerance from it, or gate these rows on the per-thread time
      against the reference (which the compare already has) and report the efficiency
      without gating it, as the 16-thread rows are. A `scripts/perf_compare.py` change
      with docs/performance.md's "Gate" paragraph following.
- [ ] **4.17 A load to a frame before the start save overflows the stack.** Found
      2026-10-12 running the tilt-target stage on the corner: the corner was imported at
      the stage's start frame, the stage rewinds ten frames before it
      (`TiltTargetShot`, `LongLoad(_initialFrame - 10)`), and the framework, finding no
      save at or before that frame, recursed until the stack ran out (a 0xC00000FD exit,
      no message, the crash dump's one frame repeated). On the game it never shows: a
      resource played from frame 0 has its start save there, and any frame of the movie
      is reachable. The pipeline avoids it by importing the corner `lead` frames early
      (README.md, "Configuration"), but the framework should refuse such a frame with a
      message naming it and the start save's frame, in `Script::LongLoad` or the save
      lookup under it (`GetLatestSave`), with a case in `test_script_*.cpp` on the mock
      resource. A `tasfw-core` change: the maintainer's call first (rule 10).
- [ ] **4.18 A forced CSV row is written with the CSV off.** Found 2026-10-12 running the
      ending stage with `csvSamplePeriod` 0 (the scratch configs of 4.13; the committed
      config samples every tenth block, so it never showed). `ScattershotThread::AddCsvRow`
      runs the stage's `ForceAddToCsv` whether or not the CSV was opened (`OpenCsv`
      returns at once when the period is 0), so a stage that forces rows, the ending for
      its departures, writes each one to a stream that was never opened, prints "Error
      writing to CSV row -1: 6" and "Retrying CSV row -1" per forced row (40 of them in a
      300-shot run, on the game and on the corner alike) and goes on. The search is
      unharmed; the frames the forced check spends are wasted. Fix: `AddCsvRow` returns
      before the forced check when `CsvEnabled` is false, a bug fix behind an unchanged
      interface in tasfw-scattershot; the frame-advance and script counts of a run with
      the CSV off go down by the forced checks (the Tier D workloads force nothing, their
      counts stay), with the suite's delta table (rule 8) and a case in
      `test_scattershot_mock.cpp` whose thread forces a row with the CSV off.
- [ ] **4.19 Tier C on the corner.** Since the `PyramidUpdate` stand-in was removed (4.13,
      2026-10-12), the downhill look-ahead is a frame of a child script on the resource
      the script runs on. The frozen Tier C workload `PyramidOscillation` is the old
      oscillation script, which asks for that look-ahead every frame, so on the game the
      row is now mostly game frames: 188,621 frame advances an iteration where it had
      42,923, 2.9 s where it had 0.68 (docs/performance-changelog.md), so a change in the
      framework shows less in it than before. The pipeline's own stages ask rarely and were not
      moved. The rows are meant to measure the framework, so the fix is a corner variant
      of the Tier C workloads (the corner imported from the game at the anchor, as the
      Tier B corner rows do), the game rows kept beside it or retired once the corner
      rows are baselined; the frozen copies run on `Sm64Resource` already, so this is
      `bench_framework.cpp` and a baseline, no script change.
- [ ] **4.20 The first oscillation's yield with the exact look-ahead.** Since 2026-10-12
      `GetMinimumDownhillWalkingAngle` answers with the game's own floor normal one frame
      on, where the `PyramidUpdate` stand-in answered 6 to 30 angle units off it. The
      every-corner case of `bitfs-turn --test` finds fewer first-oscillation solutions
      within its 2,000 shots with the exact answer than with the stand-in's (768, 530,
      1,000 and 772 against 1,000, 680, 1,000 and 1,000 in corners 1 to 4, at the cap of
      1,000; both pass the case's bar of 200), and the seed-6 dr stage reaches its first
      oscillation in 1,975 shots against the stand-in's 1,269. The oscillation scripts
      compare Mario's facing with the look-ahead's `angleFacing` to the unit
      (`BitFsPyramidOscillation_RunDownhill.cpp`) and round it to a HAU, so an answer on
      the exact boundary of the downhill range may be a worse search target than one a
      little inside it. Done when: the yields are measured with a margin inside the range
      (and with the pyramid's own normal after the frame, the other exact reading) on the
      same seeds, and the better rule is in the script with its numbers in
      docs/performance-changelog.md, or the exact answer is kept with the reason written
      down. Measured 2026-10-13 with the facing 64 units inside the range
      (`DownhillMargin`, added for the ending's run, 4.11): the every-corner case finds
      1,000, 478, 749 and 804 in corners 1 to 4, against the exact edge's 768, 530, 1,000
      and 772, a difference within one run's spread. Still to measure: the seed-6 dr stage
      with the margin, and the pyramid's own normal after the frame.
- [x] **4.21 The dr stage's rewind under mixed tilts.** Done 2026-10-12, the maintainer's
      decision ("not a bad idea, and it should be minXzSum"): eligibility is the
      solution's (the exit tilt `minXzSum` and the step parity), the direction the pass's;
      every pass writes its whole pool beside its pass file
      (`<stage>.oscillation<n>.pool.json`), and a pass that finds nothing hands on every
      solution meeting the exit rules from every earlier right-direction pass's pool,
      instead of the carried roots of the nearest such pass (README.md, "Configuration").
      The rules are the rewinding run's, applied when it rewinds, so a run resumed at a
      later oscillation under other rules reads the pool files right; a pass file without
      a pool file, from a run before this, is filtered as before. The record of the
      problem: the rewind (`Stages.cpp`, the maintainer's rule of 2026-10-06) handed the
      ending the roots of the nearest earlier
      pass whose crossing heads the right way, keeping only those at the exit tilt
      (`minXzSum`, 0.69) with the step parity. The carried sets are mixed from the second
      oscillation on (lineages climb from the rest's 0.53 at different rates, and the
      hand-over keeps the ten fastest whatever their tilt), so a pass that fails right
      after its lineage reached the regime has nothing behind it at the exit tilt and the
      run ends with no solutions (the maintainer saw one end at a failed fourth
      oscillation, 2026-10-12; in the 16:46 run that day the rewind handed on 99 of
      oscillation 3's 100 roots at 0.69). Eligible roots in a pass the rewind skips, or
      in the part of a pool the hand-over dropped, were lost. The maintainer's view: the
      behavior is hard to define while a set mixes regime and sub-regime tilts; the
      definition above keeps the two apart, and the exit tilt stays `minXzSum`, the
      ending's requirement, not the regime's floor.

- [x] **4.22 The corner resource and the pause.** Done 2026-10-12, the maintainer's word
      ("fix the pause as well"). The problem: `BitfsCornerGame::Frame` ran the level on
      every frame whatever the buttons, so a pause did nothing on the corner, where in the
      game START enters the pause after that frame's update and the next three frames
      update neither the level nor the camera (the pause menu opens, takes the START or
      A that closes it, and returns to play; the controller is read, the scene behind the
      menu is still drawn, so animations advance, and the global timer runs on). The
      pause-buffered dive recover (`pauseBuffer`, the example movies' recipe) is made of
      those frozen frames: on the corner they were three frames of the dive slide uphill,
      44 down to 27 of speed, and no rollout landed, where the game landed 7 of 18 dives
      in 8,000 shots. Built: the play mode and the pause menu's state in
      `BitfsCornerMem::Globals` (`sCurrPlayMode`, `sTransitionTimer`, `gPauseScreenMode`,
      `gMenuMode`, `gDialogBoxState`, `gDialogLineNum`, the cursor's hold-key pair);
      level_update.c's pause entry and `play_mode_paused` as members of
      `BitfsCornerGame` (the menu's exit from the course, a warp, is a refusal, as is a
      transition timer still running); ingame_menu.c's pause menu as a pinned copy
      (`decomp/ingame_menu.inc` with `ingame_menu.h`: the menu's state machine, its
      cursor with the stick, the course's menu; the camera options on the cursor's third
      line, which pick a camera selection the corner has not got, a refusal); the menu's
      render after the geo pass, as `render_game` orders it; the pause screen's zoom of
      the drawn camera left out (it moves the scene graph's camera, not the game's). The
      import reads the play mode and the menu from a build that exports them, normal play
      otherwise. The parity harness's fuzz presses a pause one frame in fifty (START, then
      neutral, START, neutral, the stick neutral through them: a stick pushed on the
      menu's frame moves its cursor when Mario stands idle) and starts its generator
      fresh each segment, so a pause cut by a segment's end is not carried into the next
      (its closing press would open one): the corner is exact against the game over a
      million frames with 37,522 START presses, and with the bully on Mario with 36,728.
      The ending's dive recover then lands on the corner (4.11).
- [ ] **4.23 The replay check on the game runs on one copy of it.** Found 2026-10-13
      timing the dr stage's rewind, which the maintainer found slow next to the
      oscillation passes. Gathering the rewind's solutions is not the cost: 0.004 s for
      about 910 of them with the pools in memory, 0.23 s read back from the pool files.
      The cost is what follows every corner stage with many solutions. `VerifyOnGame`
      replays each one on the game and on a corner, one solution after another on one
      copy of each, and the game's replay takes 2.2 s for 912 solutions (about 210
      frames each at about 11 µs a frame), where the four oscillation passes before it
      took about 7 s together. The export used to replay them on the game a second time
      (3.0 s) and now replays them on the corner (1.2 s;
      docs/performance-changelog.md). Still to do: split the game's replay check across
      several copies of the game, which would take about 0.15 s with 16. An all-corner run loads one copy
      today (`main.cpp`, `BuildResources`), so the start-up time and memory of the extra
      copies need measuring before deciding how many to load.
## Phase 5: toward a game- and console-agnostic framework

Not scheduled. Listed so decisions in earlier phases do not paint us into a corner.

- `Resource::getCurrentFrame` and the `gControllerPads` write in `Script::SetInputs` are SM64-specific; they belong in the resource.
- `Inputs`/`M64` assume an N64 controller and the Mupen m64 format; console-agnosticism
  means the controller and movie format become properties of the resource or console, and
  the frame stays as the resource's indexable step (ARCHITECTURE.md, "Resource and savestates").
- A second resource (another libsm64 build or an emulator core) is the real test of the
  abstraction. The first is the BitFS resource of 4.13, the pyramid corner of the game on
  the decomp's own structs, serving the same symbols as `LibSm64` (decided 2026-10-11).
- **One declared source for a game's vocabulary.** How the game's structs, constants and
  reimplemented logic enter the repository today is copying: headers copied by hand at
  different decomp revisions, the physics as the corner resource's copies of the decomp's
  own text (the two rewrites of it were removed with 4.13), and the decomp pin and the DWARF table
  as after-the-fact checks (docs/decomp.md). Haphazard by the maintainer's own
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
  maintainer, 2026-09-15). The ending's squish probe (4.11, 2026-10-05) is the first
  consumer and the measure of the design: a write inside a metric script's sandbox, which
  hard rule 1 now allows under its discipline, that this input type would replace.
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
