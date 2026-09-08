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
- [ ] **1.3 Performance test suite.** Implement [docs/performance.md](docs/performance.md) as a
      `tasfw-perf` target tree, Release/RelWithDebInfo only:
      - [x] Tier A microbenchmarks (Google Benchmark, DLL-free): hashing, state bins, input
        mapping, m64 I/O, `SlotManager`, `Script` per-operation overhead, hierarchy depth,
        state trackers. `scripts\perf.ps1` runs and compares; first baseline committed
        (2026-09-07). Heap allocations per iteration are counted on every benchmark and
        gated at 0.1 (2026-09-07). Still to do: run it in CI.
      - [ ] Tier B resource benchmarks. Done 2026-09-07: frame advance, save (recycled and
        fresh) and load, full and lightweight, in `bench_libsm64.cpp`; `perf.ps1` finds the
        DLL like `test.ps1` and the family is skipped without it. Still to do: thread
        scaling 1 to 16 and memory per slot.
      - Tier C framework workloads with exact-count gates: fixed scripts, `PyramidUpdate`
        probes, a tracker sweep; report replay ratio and overhead %.
      - Tier D scattershot end to end: a deterministic exact-count run and a throughput run.
      - JSON output, checked-in baselines under `perf/baselines/`, a compare script that prints
        the delta table for PRs, and the regression policy from the spec.
      *Done when:* a deliberate extra frame advance in `LoadBase` fails Tier C, a deliberate
      10% slowdown in `GetHash` fails Tier A, and a PR template asks for the delta table.
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
- [ ] **1.5 Warnings and the bugs behind them.** Done 2026-09-07: C4715/-Wreturn-type in the
      `TurnAround` lambda, the `printf("%d", uint64_t)` calls, the root `Segment` argument order
      (RngHash was being truncated into `nScripts`), `Rotation::Negate`, the two empty-body
      `if (...);` statements in the Approach/Recover stages (now `return false`), the
      always-false `&&` in `TurnUphill_1f`, eight dropped `.executed` results, missing
      `override`s, unhandled `switch` cases, `main` returning `false`, a bool/s32 compare in
      `PyramidUpdate`, int16-to-int8 narrowing in `Inputs.cpp`. Remaining: three MSVC C4244
      `_Ty`-to-`float` warnings from `std::vector<float>` initializer lists in
      `TiltTargetShot.hpp` and Google Benchmark's `/MP` under clang-cl (range-v3's deprecated
      `compressed_tuple` went with range-v3 in 1.4). `TASFW_WARNINGS_AS_ERRORS` exists (off by default). *Done when:*
      both builds are warning-free and the option is on in CI.
- [ ] **1.6 Build hygiene and compiler matrix.** Delete the stale `build/` artifacts, add
      presets for MSVC and clang-cl matching `scripts/build.ps1`, and run a GitHub Actions
      matrix (windows-msvc, windows-clang-cl, ubuntu-gcc, ubuntu-clang) building everything
      and running the DLL-free tests and Tier A benchmarks. *Done when:* the matrix is green
      on `master`. Status (2026-09-07): the matrix is **green on `roadmap/1.1-1.5`** for
      windows-msvc, windows-clang-cl, ubuntu-gcc (GCC 13) and ubuntu-clang (Clang 17), each
      including the perf-binary smoke run. Getting there took three runs: CMake 4 rejecting
      nlohmann/json's minimum version, three missing `template` keywords MSVC had accepted,
      f-suffixed `std::` math functions, a missing `<cmath>` (docs/compilers.md). Still to do:
      presets, deleting stale `build/` artifacts, DLL-free tests in CI. Also fixed here:
      the CMake compiler-ID bug that left MSVC builds without any `/arch` flag, and FP
      contraction is now off on every compiler (docs/compilers.md).

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

- [ ] **3.1 Frame cursor semantics.** `Modify` leaves the cursor at the end of the child's diff,
      not where the child stopped; callers compensate with extra `Load`s (see the TODOs in
      `ScattershotThread.t.hpp`). Decide the rule, document it, and remove the workarounds.
- [ ] **3.2 Encapsulation.** Make `Script` internals private and retire `ScriptFriend` if current
      MSVC accepts the friend template. Mark `resource` and `startSaveHandle` private.
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

- [ ] **4.5 Base-block validation failures.** `ScattershotThread::ValidateBaseBlock` found a
      state-bin mismatch and dumped `error.m64` during a 4-thread, 55-shot, deterministic
      `tilt-target` smoke run (2026-09-07). The dump used to land silently in `analysis/`, so
      nobody knows how often this happens or what it costs. Count it in the end-of-run
      summary, reproduce with a single thread, and find out whether it is a determinism bug
      (hard rule 3) or an expected consequence of blocks being decoded from segment chains.
      *Done when:* the cause is documented and a Tier D run reports zero unexplained
      validation failures.

## Phase 5: toward a game-agnostic framework

Not scheduled. Listed so decisions in earlier phases do not paint us into a corner.

- `Resource::getCurrentFrame` and the `gControllerPads` write in `Script::SetInputs` are SM64-specific; they belong in the resource.
- `Inputs`/`M64` assume an N64 controller and the Mupen m64 format.
- A second resource (another libsm64 build or an emulator core) is the real test of the abstraction.
