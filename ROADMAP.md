# Roadmap

Status as of 2026-09-07. Items are ordered; each phase makes the next one safe to do with
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
- [x] **1.1 DLL layout self-check.** `LibSm64::layoutCheckReport()` cross-checks the copied
      structs against relationships the game guarantees (Mario's object sits in `gObjectPool` at a
      multiple of `sizeof(Object)`, its behavior is `bhvMario`, `oPos` and `gfx.pos` mirror
      `MarioState::pos`, the floor normal is unit length) and, in lightweight mode, that every hot
      symbol lies inside the saved slices. `Resource::verifyLayout()` throws on failure and runs
      once per scattershot thread after the start frame loads. `dllcheck.exe <dll> <m64> <frame>
      [--lightweight]` runs it standalone and prints frame-advance and save/load cost. Result
      (2026-09-07): the pinned 2022 DLL and wafel's 2023 DLL both pass every check, so the newer
      DLL is a drop-in replacement as far as layout goes.
- [x] **1.2 Correctness test tier.** `tasfw-tests` (doctest), run by `scripts\test.ps1` or
      `ctest`, 49 cases / 700 assertions on 2026-09-07:
      - DLL-free: joystick mapping checked against a brute force over all 65,536 stick
        positions, `M64` round trip and gap filling, `BinaryStateBin` packing and clamping,
        `SlotManager` LRU eviction, `LevelStack` (on-demand levels, in-place reset and
        storage reuse, reference stability, slot release on erase), and the script engine's
        invariants on `FakeResource` (diff recording, movie fallback, Execute/Modify/Test/ad-hoc
        semantics, exact restore on `Load`, bit-identical replays, hierarchy input resolution,
        `Rollback`, cache invalidation after rewriting a frame, a child's saves surviving
        `Modify`, and state trackers: recursive metrics, queries ahead of the cursor computed
        in a sandbox, states following the diff on Execute/Modify, unasserted states stored
        as defaults, the tracker type guard); and the bitfs-turn pipeline library (config
        schema and path resolution, scattershot override layering, rejected keys and
        references, solution-file round trip, selection, `input:<metric>` arguments).
      - libsm64 smoke test (skips unless `TASFW_LIBSM64`/`TASFW_M64` are set): loads the DLL,
        passes the layout check at frame 3330, plays the movie twice with identical Mario and
        pyramid state, and pins that state to exact golden values. Any one-frame change to the
        movie or the engine before frame 3330 fails it.
      Runs in CI (DLL-free part) on all four compilers. `PyramidUpdate` against the DLL is
      covered by 3.3; not yet covered: the scattershot loop end to end (Tier D territory).
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
      - [ ] Tier B resource benchmarks. Done 2026-09-07: frame advance, save (recycled and
        fresh) and load, full and lightweight, in `bench_libsm64.cpp`; `perf.ps1` finds the
        DLL like `test.ps1` and the family is skipped without it. Still to do: thread
        scaling 1 to 16 and memory per slot.
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
      (`cmake/WarningsAsErrors.cmake`) and every CI job passes it; on the way,
      `tasfw-scripts-scattershot-bitfs-dr` got the `add_optimization_flags` call every
      sibling had (LTO, and the GCC `-Wno-missing-requires`; docs/performance.md change log). All four compilers are
      warning-free at their default levels, which is the caveat: MSVC builds at `/W1`
      because CMake stopped adding `/W3` in 3.15, and GCC/Clang run without `-Wall`.
      Raising the levels is 1.7.
- [ ] **1.6 Build hygiene and compiler matrix.** Delete the stale `build/` artifacts, add
      presets for MSVC and clang-cl matching `scripts/build.ps1`, and run a GitHub Actions
      matrix (windows-msvc, windows-clang-cl, ubuntu-gcc, ubuntu-clang) building everything
      and running the DLL-free tests and Tier A benchmarks. *Done when:* the matrix is green
      on `master`. Status (2026-09-08): everything but the merge is in place on the `agent`
      branch. `CMakePresets.json` has one `<compiler>-<config>` configure preset per compiler
      and config (`msvc-release`, `clang-cl-debug`, `gcc-release`, `clang-relwithdebinfo`,
      ...) with the build directories `build.ps1` always used, plus build and test presets of
      the same names; `build.ps1` configures and builds through them; the CI matrix
      configures from the four release presets with `TASFW_WARNINGS_AS_ERRORS=ON`, runs the
      DLL-free tests and every Tier A family, and is green on windows-msvc,
      windows-clang-cl, ubuntu-gcc (GCC 13) and ubuntu-clang (Clang 17). The Visual Studio
      and Ninja Multi-Config leftovers in `build/` are gone. Getting the first matrix green
      (2026-09-07) took three runs: CMake 4 rejecting nlohmann/json's minimum version, three
      missing `template` keywords MSVC had accepted, f-suffixed `std::` math functions, a
      missing `<cmath>` (docs/compilers.md). Also fixed there: the CMake compiler-ID bug that
      left MSVC builds without any `/arch` flag, and FP contraction is now off on every
      compiler (docs/compilers.md).
- [ ] **1.7 Raise the warning levels.** At `/W3` MSVC reports 292 warnings (2026-09-08):
      249 C4244 (narrowing conversions), 18 C4018 (signed/unsigned compares), 14 C4267
      (`size_t` narrowing), 8 C4996, 2 C4101, 1 C4065; by tree 162 `tasfw-core`, 67
      `tasfw-scattershot`, 24 `tasfw-scripts`, 22 `tasfw-bruteforcers`, 11 `tasfw-resources`,
      2 `tasfw-perf`, 1 `tasfw-tests`. GCC and Clang have not been tried with `-Wall -Wextra`.
      Fix them tree by tree, each PR with the perf delta table, since most are in hot code
      and a narrowing fix can change a type. *Done when:* `/W3` and `-Wall -Wextra` are on
      for every first-party target and the 1.5 option still passes on all four compilers.

## Phase 2: loosen the grip of the pinned DLL

Goal: the DLL becomes a reproducible, swappable artifact instead of a mystery binary.

- [ ] **2.1 Document and script DLL production.** Record which wafel release the 2022 DLL came from,
      how to unlock a `.dll.locked` against a JP ROM with `libsm64_lock`, and add a script that
      makes the N per-thread copies. *Done when:* a fresh machine can populate `res/` from a wafel
      release plus a ROM by following the doc.
- [ ] **2.2 Generate the sm64 headers.** Replace the hand-copied `tasfw-core/inc/sm64/*.hpp` with
      headers generated from the decomp source (or from wafel's `sm64_layout` DWARF dump) for the
      exact DLL build. *Done when:* regenerating for a new DLL is one command and 1.1 passes.
- [ ] **2.3 Replace hardcoded lightweight-save offsets.** Derive the hot regions of `.data`/`.bss`
      from symbol addresses (Mario state, object pool, surfaces, camera, RNG, timers) or adopt the
      dirty-page tracking that the Linux branch already sketches. *Done when:* lightweight mode
      works unchanged on a different DLL build and is no slower than today.
- [ ] **2.4 Find objects by behavior, not index.** Replace `gObjectPool[84]` with a scan for
      `bhvLllTiltingInvertedPyramid`. *Done when:* no numeric object indices remain in scripts.
- [ ] **2.5 US ROM support.** `CountryCode` already exists; make the m64 header check and DLL
      choice follow it. *Done when:* the smoke test passes on both JP and US DLLs.

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
      retiring it means checking both, not just the build.
- [x] **3.3 PyramidUpdate drift test.** `test_libsm64.cpp` imports `PyramidUpdateMem` from
      the DLL before each of 240 frames (Mario walks to the pyramid's centre, then it settles;
      91 frames move the normal), advances both, and requires the normal to match
      bit-for-bit. Passes with max |diff| = 0 on MSVC and clang-cl (2026-09-07), which is also
      the bit-exactness test for the FP flags in docs/compilers.md. GCC needs a Linux DLL
      (3.4). Learned on the way: terrain objects update before the player, so the pyramid
      reads Mario's previous-frame position (ARCHITECTURE.md).
- [ ] **3.4 Linux parity.** Build with GCC/Clang, confirm the `mprotect`/`SIGSEGV` save path works,
      and note any divergence from MSVC results. *Done when:* the DLL-free tests run on Linux CI
      and the Linux `LibSm64` path passes the smoke test against a Linux libsm64 build.
- [ ] **3.5 Savestate memory budget.** The 8 GB cap is per resource, so 16 threads can address
      128 GB. Make it a global budget in `Configuration`.
- [ ] **3.6 Unify timing instrumentation.** `ExecuteAdhocBase` records milliseconds via
      `std::chrono` while everything else is rdtsc cycles. Pick one unit, expose the counters
      as a struct the perf suite can read, and print them consistently.
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
      timings are in the change log. Remaining: scripts resolving symbols per execution; the
      `std::map` head node MSVC allocates for each container a script actually touches, and
      one map node per cached frame (a flat or pooled container, measured); virtual dispatch
      on `Resource` per frame if measurement says it matters.
- [ ] **3.8 Hotspot investigations.** Work through the "known hotspots" list in
      docs/performance.md, measurement first, one PR each, with the Tier C/D delta table.
- [x] **3.9 Pool savestate buffers.** Done 2026-09-07: `SlotManager` keeps erased and evicted
      states in a bounded pool (32) that the next `CreateSlot` reuses, so a save into a
      recycled state is one copy. `dllcheck`: full save 1561 -> 191 us against a 222 us load,
      lightweight 285 -> 50 us against 53 us. Pooled memory counts toward the slot budget.
      Gated by the Tier B `SaveErase`/`Load` benchmarks (docs/performance.md change log).

- [ ] **3.10 Make the compare concepts actually constrain.** The concepts in
      `ScriptCompareHelper.hpp` test whether `std::same_as<...>` is a valid *expression*, not
      whether it holds, so they accept anything (GCC's `-Wmissing-requires`, docs/compilers.md).
      Rewrite as nested requirements, then fix whatever callers stop compiling. *Done when:*
      a comparator with the wrong signature fails at the call site on all three compilers.
- [ ] **3.11 Pluggable savestate policy.** `shouldSave`/`shouldLoad` (replay-versus-load by
      measured average cost) and the LRU slot manager are one policy for every resource, and
      a naive one. The right policy depends on the resource: a full-game DLL with slow loads
      wants something different from a custom state machine where a load costs about a frame.
      Abstract the policy behind the resource (a policy type chosen per resource, resolved at
      compile time like everything else) so alternatives can be measured against each other
      on Tier B and C. Two known weaknesses of the current one, both measured while fixing
      4.5: the `shouldLoad` branch in `LoadBase` never fires (the lookup only returns saves at
      or before the target), and every scattershot pellet starts with empty frame counters at
      its level, so the same rewind-and-replay stretch is paid several times per pellet before
      an automatic save appears (about +10% frame advances and saves against the old, wrong
      reuse; docs/performance.md, 2026-09-08). *Done when:* the current policy is one
      implementation of the abstraction with identical counts, and a second policy exists and
      is compared.

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
      and pinned by a fake-resource test. Verified on the three 400-shot deterministic runs
      (1 and 4 threads, lightweight and full saves): zero failures, at a wall-time cost
      recorded in docs/performance.md, since the old speed came partly from loading wrong
      saves instead of replaying. The Tier D deterministic run (1.3) does not exist yet;
      when it does, it asserts zero validation failures. Kept from the investigation: the
      failure counter and hex bins, the re-decode diagnostic that tells "decoding is not
      deterministic" from "the recording is wrong", `dllcheck --leak-scan`,
      `scripts/dll_symbols.py`, and the camera and controller coverage checks in
      `layoutCheckReport`.

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

## Phase 5: toward a game-agnostic framework

Not scheduled. Listed so decisions in earlier phases do not paint us into a corner.

- `Resource::getCurrentFrame` and the `gControllerPads` write in `Script::SetInputs` are SM64-specific; they belong in the resource.
- `Inputs`/`M64` assume an N64 controller and the Mupen m64 format.
- A second resource (another libsm64 build or an emulator core) is the real test of the abstraction.
- **Hacks as a kind of input.** Today a direct write into game memory (`//! UNSAFE`) cannot
  be replayed from a savestate, which is why hard rule 1 forbids it. The intent is to make
  hacks a special input type the framework applies at a frame like any other input, so they
  are part of the diff, replayed on decode and reverted with the sandbox. Design not yet
  discussed; nothing should assume inputs are only controller states.
