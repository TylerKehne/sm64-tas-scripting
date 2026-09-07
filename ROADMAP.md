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

## Phase 0: where things stand

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
- [ ] **1.2 Correctness test tier.** Add a `tasfw-tests` target (Catch2 or doctest via FetchContent) with:
      - DLL-free unit tests: `Inputs` yaw/magnitude mapping round-trip, `M64` load/save
        round-trip, `BinaryStateBin` bit packing, `SlotManager` eviction order.
      - One deterministic smoke test that needs the DLL: load the source .m64, `LongLoad` to a
        fixed frame, run `BitFsPyramidOscillation` (or a shorter script), and assert a hash of the
        resulting `M64Diff`. Skips with a clear message when `res/` is missing.
      *Done when:* `ctest` passes locally and the smoke test catches a deliberate one-frame change.
- [ ] **1.3 Performance test suite.** Implement [docs/performance.md](docs/performance.md) as a
      `tasfw-perf` target tree, Release/RelWithDebInfo only:
      - [x] Tier A microbenchmarks (Google Benchmark, DLL-free): hashing, state bins, input
        mapping, m64 I/O, `SlotManager`, `Script` per-operation overhead, hierarchy depth,
        state trackers. `scripts\perf.ps1` runs and compares; first baseline committed
        (2026-09-07). Still to do: run it in CI.
      - Tier B resource benchmarks: frame advance latency, save/load full vs lightweight,
        thread scaling 1 to 16, memory per slot.
      - Tier C framework workloads with exact-count gates: fixed scripts, `PyramidUpdate`
        probes, a tracker sweep; report replay ratio and overhead %.
      - Tier D scattershot end to end: a deterministic exact-count run and a throughput run.
      - JSON output, checked-in baselines under `perf/baselines/`, a compare script that prints
        the delta table for PRs, and the regression policy from the spec.
      *Done when:* a deliberate extra frame advance in `LoadBase` fails Tier C, a deliberate
      10% slowdown in `GetHash` fails Tier A, and a PR template asks for the delta table.
- [ ] **1.4 Split `main.cpp`.** Turn each experiment into a named stage selected from a JSON
      pipeline config (start frame, m64, thread count, DLL directory, stage parameters). Replace
      every `C:\repos` literal, including the `error.m64` dump in `ScattershotThread.t.hpp`, with
      config-derived paths. *Done when:* `bitfs-turn.exe --config x.json` runs one stage and the
      source contains no absolute paths.
- [ ] **1.5 Warnings and the bugs behind them.** Done 2026-09-07: C4715/-Wreturn-type in the
      `TurnAround` lambda, the `printf("%d", uint64_t)` calls, the root `Segment` argument order
      (RngHash was being truncated into `nScripts`), `Rotation::Negate`, the two empty-body
      `if (...);` statements in the Approach/Recover stages (now `return false`), the
      always-false `&&` in `TurnUphill_1f`, eight dropped `.executed` results, missing
      `override`s, unhandled `switch` cases, `main` returning `false`, a bool/s32 compare in
      `PyramidUpdate`, int16-to-int8 narrowing in `Inputs.cpp`. Remaining: three MSVC C4244
      `_Ty`-to-`float` warnings from `std::vector<float>` initializer lists in
      `TiltTargetShot.hpp`, range-v3's deprecated `compressed_tuple`, and Google Benchmark's
      `/MP` under clang-cl. `TASFW_WARNINGS_AS_ERRORS` exists (off by default). *Done when:*
      both builds are warning-free and the option is on in CI.
- [ ] **1.6 Build hygiene and compiler matrix.** Delete the stale `build/` artifacts, add
      presets for MSVC and clang-cl matching `scripts/build.ps1`, and run a GitHub Actions
      matrix (windows-msvc, windows-clang-cl, ubuntu-gcc, ubuntu-clang) building everything
      and running the DLL-free tests and Tier A benchmarks. *Done when:* the matrix is green
      on `master`. Status: clang-cl builds locally after the workarounds in docs/compilers.md;
      the workflow runs on every push and PR and re-emits failures as annotations. The first
      runs found CMake 4 rejecting nlohmann/json's minimum version, a missing `template`
      keyword MSVC had accepted, and `std::sqrtf` (all fixed, see docs/compilers.md). Also fixed here:
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
- [ ] **3.3 PyramidUpdate drift test.** Run `PyramidUpdate` and `LibSm64` side by side for a few
      hundred frames of oscillation and assert identical normals, on every compiler in the
      matrix. This is also the bit-exactness test for the FP flags in docs/compilers.md.
      *Done when:* the test exists, is part of the DLL smoke tier, and passes on MSVC, clang-cl
      and GCC.
- [ ] **3.4 Linux parity.** Build with GCC/Clang, confirm the `mprotect`/`SIGSEGV` save path works,
      and note any divergence from MSVC results. *Done when:* the DLL-free tests run on Linux CI
      and the Linux `LibSm64` path passes the smoke test against a Linux libsm64 build.
- [ ] **3.5 Savestate memory budget.** The 8 GB cap is per resource, so 16 threads can address
      128 GB. Make it a global budget in `Configuration`.
- [ ] **3.6 Unify timing instrumentation.** `ExecuteAdhocBase` records milliseconds via
      `std::chrono` while everything else is rdtsc cycles. Pick one unit, expose the counters
      as a struct the perf suite can read, and print them consistently.
- [ ] **3.7 Remove known non-zero-cost spots**, each gated by the suite: cache `gControllerPads`
      and `sm64_update` pointers instead of `GetProcAddress` per frame; let scripts resolve
      symbols once; drop the `dynamic_cast` in `GetTrackedState`; mark concrete resources
      `final` or move to CRTP if measurement shows virtual dispatch on the per-frame path
      matters; stop default-inserting into the per-level bookkeeping maps on hot lookups.
- [ ] **3.8 Hotspot investigations.** Work through the "known hotspots" list in
      docs/performance.md, measurement first, one PR each, with the Tier C/D delta table.
- [ ] **3.9 Pool savestate buffers.** `dllcheck` measures a full save at 1.4 ms against a
      0.19 ms load, and lightweight at 0.28 ms against 0.04 ms: each `SaveState` allocates and
      zero-fills fresh `std::vector`s because slots are never reused. Recycle evicted slot
      buffers. *Done when:* save cost is within 2x of load cost in both modes, gated by Tier B.

## Phase 4: the squish-cancel brute forcer

Goal: finish the thing the framework was built for.

- [ ] **4.1 Re-enable the disabled stages.** `Scattershot_BitfsDrApproach` and
      `Scattershot_BitfsDrRecover` (`ATTEMPT_DR`, `C_UP_TRICK` phases) are commented out in
      `main.cpp`. Bring them back as pipeline stages with their own configs.
- [ ] **4.2 Persist search state.** Serialize blocks/segments/solutions so a multi-hour run can be
      resumed and so stages can be re-run from saved solutions without recomputation.
- [ ] **4.3 Faster block decoding.** Each shot replays the whole segment chain from the root.
      Cache savestates per block (bounded by the memory budget) and measure the gain with the
      Tier D throughput run; the deterministic run must produce identical counts.
- [ ] **4.4 Analysis.** Keep the R plotting script working from the new CSV paths, or port it to
      Python so it can run in CI. Record which columns each stage emits.

## Phase 5: toward a game-agnostic framework

Not scheduled. Listed so decisions in earlier phases do not paint us into a corner.

- `Resource::getCurrentFrame` and the `gControllerPads` write in `Script::SetInputs` are SM64-specific; they belong in the resource.
- `Inputs`/`M64` assume an N64 controller and the Mupen m64 format.
- A second resource (another libsm64 build or an emulator core) is the real test of the abstraction.
