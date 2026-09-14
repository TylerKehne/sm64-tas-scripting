# Roadmap

Status as of 2026-09-08. Items are ordered; each phase makes the next one safe to do with
an AI agent. Check boxes as work lands and keep "Done when" honest.

**Cross-cutting rules:**

- Performance is as important as correctness and the target is zero-cost abstractions (see
  [docs/performance.md](docs/performance.md)). Every item below must leave the gated
  performance counts unchanged or better, and any item that touches a hot path reports its
  delta table. Nothing in this roadmap is "done" if it made the search slower.
- Every item must build clean with MSVC and clang-cl (see [docs/compilers.md](docs/compilers.md)).
  Compiler-specific workarounds are documented there, never hidden in `#if` forks.
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
      `MarioState::pos`, the floor normal is unit length), reading through `resource->addr()`
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

Goal: the core's implicit invariants become explicit and enforced.

- [x] **3.1 Frame cursor semantics.** Decided by the maintainer 2026-09-08: `Modify` leaves the
      cursor at the end of the child's diff on purpose, because the common case is to keep
      going from there. A caller that wants the child's stopping frame loads it (what
      `ScattershotThread` does, since blocks are keyed by that frame), and a caller that wants
      to roll back runs `Execute` and applies the returned diff later. Written into
      ARCHITECTURE.md; the TODOs that proposed changing `Modify` are gone.
- [ ] **3.2 Encapsulation.** Make `Script` internals private and retire `ScriptFriend` if current
      MSVC accepts the friend template. Mark `resource` and `startSaveHandle` private. Note
      from the maintainer: MSVC and Visual Studio IntelliSense disagree about such
      declarations and one or the other kept failing, which is why `ScriptFriend` exists;
      retiring it means checking both, not just the build. The rule this enforces is already
      in force by convention (AGENTS.md, hard rule 9, 2026-09-12): scripts touch the resource
      only through `resource->addr()` until a better access contract exists, and that
      contract is part of this item. Its shape is not settled (maintainer, 2026-09-12): it is
      adjacent to hack support (Phase 5), will probably admit only certain kinds of symbols,
      and will carry some guard against invalid memory access. Do not design it piecemeal.
      Known remaining direct access to fold in: the drift
      test (`test_libsm64_pyramid.cpp`) drives a locally constructed `PyramidUpdate` from inside a
      script instead of going through `ImportSave<PyramidUpdateMem>`, `SlotHandle` holds a
      public resource pointer, and `SlotManager` is all-public: `test_slots.cpp` and the Tier B
      benchmarks set `_saveMemLimit`, `_maxPooledStates` and `_pooledMem` directly (maintainer,
      2026-09-14: not ideal, wants better encapsulation; an accessor over a public member is not
      the answer, the surface is).
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
- [ ] **3.7 Remove known non-zero-cost spots**, each gated by the suite. Done 2026-09-07:
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
      timings are in docs/performance-changelog.md. Remaining: scripts resolving symbols per execution; the
      `std::map` head node MSVC allocates for each container a script actually touches, and
      one map node per cached frame (a flat or pooled container, measured); virtual dispatch
      on `Resource` per frame if measurement says it matters.
- [ ] **3.8 Hotspot investigations.** Work through the "known hotspots" list in
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
      nothing. Remaining, one PR each, in the order of the numbers: the tracker status in
      `TiltTargetShot.hpp` (arrays and references instead of vectors and copies; a stage
      script, gated by the Tier D throughput row); the framework's per-sandbox and
      per-frame allocations and map nodes (3.7's remainder, about 9%); `addr` per call
      (with 3.2's access contract); a profile of the `dr-oscillations` stage for the
      `PyramidUpdateMem` import and `CalculateOscillations`, which the tilt-target workload
      never runs; whether a thread-ordered ticket instead of N+1 barriers per script moves
      the deterministic run's wall time; and the Tier D row carrying the outside share
      once its run-to-run spread is known (reported, not gated, until then).
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
      resource's work alike, single-threaded and on two threads in deterministic mode, and
      another seed does not; a limit of one slot evicts and replays without changing the
      search; and a bin that is not a function of state (it counts its own calls) fails the
      base-block validation of 4.5 and is counted, with the diagnostics' `error.m64`
      written. The thread reads the run's totals through `PerfAccess` in its `assertion()`.
      Found on the way: the 4.5 diagnostic sized its `error.m64` from the last frame of the
      total diff, undefined when the failing base block is the root with nothing applied;
      guarded. Identified 2026-09-14 when 3.5 went in with the pipeline-config test, the
      slot tests and the Tier D slot line as its only checks.

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
  decomp revision), or an access contract that needs no copies (3.2, the `addr` replacement
  hard rule 9 waits for), and the copies go.
- **Hacks as a kind of input.** Today a direct write into game memory (`//! UNSAFE`) cannot
  be replayed from a savestate, which is why hard rule 1 forbids it. The intent is to make
  hacks a special input type the framework applies at a frame like any other input, so they
  are part of the diff, replayed on decode and reverted with the sandbox. Design not yet
  discussed; nothing should assume inputs are only controller states.
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
