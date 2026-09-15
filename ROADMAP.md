# Roadmap

Status as of 2026-09-15: Phases 1 and 2 are done; of Phase 3 only 3.17 (guidelines for
TASing with the framework, after 3.2) remains, the rest landed in #94, #95, #97 and #98
and 3.2's access contract last; Phase 4 has 4.1 and 4.5
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
        `Modify`, and state trackers: recursive metrics, queries ahead of the cursor computed
        in a sandbox, states following the diff on Execute/Modify, unasserted states stored
        as defaults, the tracker type guard); and the bitfs-turn pipeline library (config
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
        state trackers. `scripts\perf.ps1` runs and compares; first baseline committed
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
        calls through `PyramidUpdate`, and a 500-frame `StateTracker_BitfsDr` sweep, with the
        cost model off so frame advances, saves and loads are exact; replay ratio and
        overhead % reported, overhead gated at 2 points.
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
      `CrossingDto::speed` in `StateTracker_BitfsDr.cpp`; the `TiltTargetShot.hpp` initializer
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
      `snprintf`, the tracked-state hooks and `BitFsPyramidOscillation_Iteration` take
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
item is done except 3.2, in progress, and 3.17, which follows it; then Phase 4.

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
      through the friend. A tracker's own frames skip the root's `TrackState` at the call
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
      constraint so a frame is never taken for a state parameter. The engine tests read the
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
      16 threads could address 128 GB. Now `SlotBudget` in `Resource.hpp` is a process-wide
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
      child scripts -24 to -32%, tracked frames -22%, deep rewinds -46%; docs/performance.md).
      Also done 2026-09-07: per-level containers constructed on first use and reset in place
      on pop (no allocation per ad-hoc level, no save bank for scripts that never save);
      tracked-state entries created on first insert; the `dynamic_cast` in `GetTrackedState`
      replaced by a per-type tag compare; statuses moved rather than copied out of finished
      scripts and `GetTrackedState` returning a reference; and a `SlotHandle` move that
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
      of an empty child script and most of the 14 of a tracked frame were sentinel nodes
      of `M64Diff`'s and the per-level caches' maps, constructed and moved per status
      object and per level, about 9% of the throughput run's CPU with the map code (3.8).
      `FrameMap` and `FrameSet` (`tasfw/FrameMap.hpp`, ARCHITECTURE.md "Script
      hierarchy", pinned by `test_framemap.cpp`) now hold `M64Base::frames` and the five
      per-level containers; the tracked states stay a node container because a tracker
      reads its previous states by reference while it may track another frame. Design
      presented under hard rule 10, prototyped at the maintainer's request and accepted on
      its numbers (2026-09-14): an empty sandbox 73 -> 25 ns and 2 -> 0 allocations, an
      empty child script 408 -> 197 ns and 11 -> 2, a tracked frame 753 -> 452 ns and
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
      the 24% outside it is 11% heap (7% of it the tilt-target tracker's `std::vector`
      status, copied per tracked frame), 5% map code, 3% `GetInputsMetadata`, 1.5% symbol
      resolution under the loader lock, and the search's own code; 95% of the frame
      advances are replays, the evaluation to the pyramid's equilibrium after each script.
      The deterministic gate run is 58% barrier spin-wait, the spread of a script's cost
      under `QueueThreadById`'s barriers, so its `process cycles` row measures waiting.
      Block decoding is 2 to 3%; `UpsertBlock`, the `print` section and the slot budget are
      nothing. Done from that list the same day: the framework's per-sandbox and
      per-frame allocations and map nodes (3.7, `FrameMap`), `addr` per call (3.7, the
      table), and the trackers' status objects (`std::array`s instead of `std::vector`s
      for the per-axis values in the tilt-target, osc-final and DR trackers and their
      solutions, the tilt-target tracker reading its previous states by reference; a
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
      `SharedLib.hpp`, which unpacked the tuple with `std::apply` around a lambda whose
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

- [ ] **3.17 Agent instructions for TASing with the framework.** Added 2026-09-14 (the
      maintainer): guidelines, for an agent or a person, on how to TAS with the framework
      once the pieces above are settled, so that Phase 4 starts from an agreed way of
      working rather than from the code alone: which tool to reach for (an ad-hoc attempt,
      a script class, a state tracker, a scattershot stage), how a goal turns into a script
      and a run, how a result is checked (counts, reproduction, exports) and which of the
      hard rules bite while TASing. Not complicated, the maintainer's words; to be written
      after 3.2, whose encapsulation the guidelines should describe as settled, and before
      Phase 4. Shape (a docs page or a section of AGENTS.md) to be decided then.
      *Done when:* the guidelines exist and an agent given the repository and them can
      create, run and check a new script without further instruction.
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
- [ ] **4.2 Persist search state.** Serialize blocks/segments so a multi-hour run can be
      resumed. Solutions already persist per stage (1.4); blocks and segments do not.
- [ ] **4.3 Faster block decoding.** Each shot replays the whole segment chain from the root.
      Cache savestates per block (bounded by the memory budget) and measure the gain with the
      Tier D throughput run; the deterministic run must produce identical counts.
- [ ] **4.4 Analysis.** Keep the R plotting script working from the new CSV paths, or port it to
      Python so it can run in CI. Record which columns each stage emits.

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
      current pipeline targets. Progress exists in the current scripts and stages.
- [ ] **4.7 A solutions database.** Store the input files (or compressed versions) with
      numerical data about each, queryable and filterable. Nothing exists yet; the per-stage
      solution files from 1.4 (`solutions/<stage>.json`: diffs plus named metrics) are the
      nearest thing and a natural import format.
- [ ] **4.8 Fine-tuned targeting.** A squish-cancel brute forcer that hits floating-point
      precise target values when given a sufficiently close starting m64 (the ARE machinery
      in `TiltTargetShot` is the seed of this). Progress exists.

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
