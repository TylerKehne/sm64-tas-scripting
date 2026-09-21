# AGENTS.md

Working instructions for AI coding agents and new contributors. Keep this file short and
operational. Long-form material lives in [ARCHITECTURE.md](ARCHITECTURE.md),
[ROADMAP.md](ROADMAP.md) and [docs/](docs/).

## What this project is

sm64-tas-scripting ("TASFW") is a C++23 framework for scripting and brute-forcing
Super Mario 64 TAS inputs. The game itself runs inside a native x64 DLL (wafel's libsm64,
built from the SM64 decompilation). Scripts drive it with savestates and frame advances,
and a multithreaded "scattershot" search explores input space on top of that. The purpose
is to make TASing a game easier and more organized, without managing the overhead of state
more than necessary; every design choice serves that.

The concrete goal driving the code today is a brute forcer for the squish-cancel setup on
the BitFS tilting pyramid. That setup is for an A-button-challenge run: the source movie
presses A on its way to the level, and nothing after the BitFS entry may (the maintainer,
2026-09-21), so no BitFS script or stage presses A. The framework is intended to become
game- and console-agnostic later.

## Who it is for

The end user is a person, or an agent, who writes scripts and resources and runs them:
reasonably comfortable with coding, not necessarily a C++ expert. The domain is
complex on its own, and the framework exists to hide as much of that complexity as it can
in its internals, so that creating a script or a resource and running it is smooth and
intuitive. This drives design decisions (the maintainer, 2026-09-14): between a base class
that is harder to write and a script that is harder to write, take the harder base class.
A design that puts a template argument, a wrapper, a new call or a new concept in front of
the script author to buy something internal is the wrong design, even when it is the
easier one to implement; hard rules 9 and 10 below are this principle applied to the
resource and to the framework's surface, and the `BasicMoves` discussion in
ROADMAP.md (Phase 5) is a worked example.

## Performance is a correctness requirement

This project is in C++ because it has to push millions of game frames through a search.
A change that is cleaner but slower is a regression and is treated like a bug. Every change
to a hot path ships with before/after numbers, and new hot-path features ship with a
benchmark. The measurement plan, the gated metrics and the regression policy are in
[docs/performance.md](docs/performance.md). Read it before touching `tasfw-core`,
`tasfw-scattershot` or `tasfw-resources`.

## Cross-platform compatibility is a requirement

The code must build clean with **MSVC, Clang and GCC** through Ninja, on Windows and Linux.
Compilers disagree about what the standard allows, and this project has hit
documented-but-unsupported features before. Rules:

- Build with every compiler you can reach before calling a change done. On Windows:
  `scripts\build.ps1 -Config Release` and `scripts\build.ps1 -Config Release -Compiler clang`;
  GCC via the Linux CI job or a container (docs/compilers.md). On Linux: GCC and Clang.
- When a compiler rejects valid code, write the form every compiler accepts, leave a one-line
  comment naming the compiler and version, and add the case to
  [docs/compilers.md](docs/compilers.md). Never add a `#if _MSC_VER` fork for a language
  feature; forks are for platform APIs only (`SharedLib`, `LibSm64`).
- Prefer explicit template parameter lists to abbreviated function templates, in-class
  definitions for constrained member templates, and forwarding functions over
  using-declarations of overloaded names. Each of these has bitten this codebase.
- Treat Clang-only warnings as signal; on first contact Clang pointed at two real bugs.

Design principle: **as close to zero-cost abstractions as we can get.** The framework is
templates and concepts on purpose, so that scripts, resources and metric scripts resolve at
compile time and `if constexpr` removes what is unused. In anything executed per frame,
prefer static dispatch over virtual calls, `std::function`, `dynamic_cast`, string-keyed
lookups or heap allocation. An abstraction that costs at runtime in a hot path has to earn
its place with numbers, and "it is cleaner" is not a number.

## Repo map

| Path | What it is |
|---|---|
| `tasfw-core/` | Engine: `Script`, `TopLevelScript`, `Resource`, savestate slots, m64 I/O, input math. Header-heavy templates, one header per concept under `inc/tasfw/`; `<tasfw/Script.hpp>` is a script's one include, it pulls in the root and the builders at its bottom. |
| `tasfw-core/inc/sm64/` | Hand-copied decomp structs, enums and trig tables. Must match the DLL's x64 layout. |
| `tasfw-resources/` | `LibSm64` (drives the game DLL) and `PyramidUpdate` (standalone reimplementation of pyramid tilt physics used as a fast stand-in). |
| `tasfw-scattershot/` | Header-only OpenMP brute-force search (blocks, segments, solutions, CSV export, and the launch of the CSV's viewer: `Visualization.hpp`). One header per concept under `inc/`; `<Scattershot.hpp>` is the one include, it pulls in the thread and the builders at its bottom. |
| `tasfw-scripts/` | Reusable BitFS scripts (pyramid oscillation, downhill angle search, dive-recover attempts, the ARE fixer of ROADMAP 4.8: `BitFsAreFixer`, a rest with the target's adjusted remainder error from the movie's dive onto the platform, its rollout steered), scattershot stages, and the scripts that look at the game rather than play it (`VerifyLayout`, `LevelTransitions`, `MarioTrace`, `SpliceMovie`), which the tools and tests run. |
| `tasfw-bruteforcers/bitfs-turnaround/` | The only executable (`bitfs-turn.exe`): the BitFS pipeline as config-selected stages (`config.json`, `Stages.cpp`, `PipelineConfig`). `--list`, `--dry-run`, `--stage`, and `--test`, its own optional doctest cases on the config's game (`Tests.cpp`), which neither CI nor `tasfw-tests` runs. |
| `tasfw-perf/` | Performance suite: Tier A microbenchmarks on an in-memory mock resource, Tier B on the game DLL, Tier C framework workloads through the frozen script copies under `workloads/` (docs/performance.md, "Tier C"; they do not follow `tasfw-scripts/`). Release only. |
| `tasfw-tools/` | `dllcheck`: runs the `VerifyLayout` script against a DLL, reports fixed-slice coverage and what a savestate holds of the sections (`bytes:`, docs/libsm64.md "The game's bytes"), measures frame-advance and savestate cost, and lists a movie's level transitions (`--levels`) or Mario and the camera around a frame (`--trace`). `m64splice`: a movie for one game version out of two, the first up to the frame it enters a level, the second from its own, played and checked frame by frame (docs/libsm64.md, "A movie for the US game"). |
| `tasfw-tests/` | Correctness tests (doctest), one file per subject; `script_fixtures.hpp` and `libsm64_env.hpp` hold what the `test_script_*` and `test_libsm64_*` files share. DLL-free tests always run; the libsm64 tests run when `res\` has the DLL and movie. `test_sm64_layout.cpp` compiles `sm64_layout.inc`, the pinned DLL's field offsets and struct sizes, against the copied headers, so the layout is checked without the game (docs/libsm64.md, "Struct layouts"). |
| `tasfw-testing/` | Header-only test support shared by tests and benchmarks: `MockResource`, and `PerfAccess`, the friend through which a test or benchmark whose subject is Scattershot's table or the resource's slot manager reaches their internals (everything else uses the public operations). |
| `perf/` | Committed benchmark baselines, one directory per machine and compiler (`tyler-desktop\`, `tyler-desktop-clang\`): a JSON file per benchmark family plus `context.json`, written by `scripts\perf.ps1 -SaveBaseline` and read through `perf_compare.py compare`, not by hand (the one hand edit is a renamed benchmark's row key, numbers untouched, as when the metric-script rows were renamed); `tierd-ci.json` is CI's Tier D baseline. `perf/results/` is gitignored. |
| `analysis/` | `visualizer.py`, the scattershot viewer (a live plot of a run's CSV, one tab per run; the search launches it, README.md "The viewer") with `requirements.txt`, which it installs into its own `analysis/.venv` on first start; also the pipeline's default output directory (CSVs, their `*.visualizer.json`, `solutions/*.json`, `m64/`), all gitignored. |
| `res/` | Gitignored runtime inputs: 24 copies of the libsm64 DLL (made by `scripts/unlock_libsm64.py`), other source .m64 files, and thousands of exported solution .m64 files. |
| `movies/` | The committed source movies: `bitfs-pyramid-jp.m64` (JP, 3,804 frames; the tests, the perf suite, CI and `config.json` use it), `bitfs-osc-final-jp.m64` (JP, 3,726 frames; the oscillations done by hand to frame 3604, once the source of a final-oscillation experiment, unreferenced now), `1keyU.m64` (US, 7,628 frames; a whole 1-key run, the source of the US way into BitFS) and `bitfs-pyramid-us.m64` (US, 3,871 frames; `1keyU.m64` to its BitFS entry, then the JP movie from its own: its frame 3397 is the JP movie's 3330, the libsm64 tests run on it when `res\` has the US DLL). |
| `scripts/` | `build.ps1` (the supported build entry point on Windows), `test.ps1`, `perf.ps1` and its compare script, `unlock_libsm64.py` (the game from a ROM or the CI key), `perf_scaling_hang.ps1`, `dll_symbols.py`, `dll_layout.py` (the layout table from a DLL's DWARF), `dll_game_bytes.py` (a build's `LibSm64KnownGameBytes` entry from its COFF symbols: where the game's bytes of `.data` and `.bss` end and the C runtime's begin), `decomp_diff.py` with `decomp_pin.json` (the copied decomp files against their pinned upstream revision; docs/decomp.md), `decode_cache_model.py` (a model of a scattershot run's block tree and what a per-thread savestate cache would save of its decoding; ROADMAP 4.3), `are_cell_model.py` (where Mario must rest on the pyramid for a wanted adjusted remainder error, in the game's float32 arithmetic: the width and spacing of the position bands per axis; ROADMAP 4.8), `fetch_deps.py` (the dependency tarballs into `build/downloads` with retries, from the CMake files' own names, URLs and hashes; CI runs it after restoring that directory from its cache). |
| `cmake/` | `AddOptimizationFlags` (arch flag, FP determinism, LTO unless `TASFW_LTO=OFF`, OpenMP; applied to every first-party target), `Warnings` (`/W3`, `/W4`, `-Wall -Wextra` on every first-party target, and `TASFW_WARNINGS_AS_ERRORS`) and `SystemIncludes` (fetched dependencies as system headers, so their warnings never count). |
| `docs/` | Provenance of the DLL (libsm64.md), what was copied from the decomp and at which revision (decomp.md), compiler pitfalls, performance, and how to TAS with the framework (tasing.md). |

## Build and run

- Primary platform is Windows; both MSVC and clang-cl must build clean. Linux code paths
  exist but have not been built recently.
- Build: `powershell -ExecutionPolicy Bypass -File scripts\build.ps1` (add `-Config Release`,
  `-Clean`, `-Compiler clang`, `-KeepGoing`, `-CMakeArgs '-D...'`). It finds Visual Studio via
  vswhere, so cmake and ninja do not need to be on PATH, and configures from the
  `CMakePresets.json` preset `<compiler>-<config>` (`msvc-release`, `clang-cl-debug`; on
  Linux `gcc-release`, `clang-release`), the same presets the IDEs and CI use. clang-cl
  comes from the VS "C++ Clang tools" component.
- Output: `build\<Config>\out\bitfs-turn.exe` with `config.json` written next to it (the
  committed one plus a `baseDirectory`, so its relative paths resolve into the source tree).
- Dependencies (nlohmann/json; doctest and Google Benchmark for tests and perf) are fetched
  by CMake, hash-pinned and cached in `build\downloads` for offline builds;
  `python scripts\fetch_deps.py` fills that directory with retries, which CI does after
  restoring it from its cache, since GitHub's downloads fail now and then. OpenMP is required.
- The game is not in git. You need `res\sm64_jp_0.dll` .. `res\sm64_jp_23.dll` (on Linux,
  `.so` copies): `python scripts\unlock_libsm64.py --rom <sm64 jp>.z64 --out res --copies 24`
  fetches the pinned bitfs-sbb build, unlocks it and writes the copies
  ([docs/libsm64.md](docs/libsm64.md)); `--rom <sm64 us>.z64 --version us` writes
  `res\sm64_us_0.dll`, the US game, which the tests run on too when it is there. The source
  movies are committed under `movies\`.
  CI unlocks the same build from the maintainer-only
  `LIBSM64_KEY` secret and runs everything exact on it: the libsm64 tests, `dllcheck`'s
  layout checks and leak scan, the pipeline's dry run, and the Tier C and CI-sized Tier D
  count gates (docs/libsm64.md, "Continuous integration"); forks and outside pull requests
  skip those steps.
- `bitfs-turn.exe --list` and `--dry-run` are safe: no search runs (`--dry-run` loads one
  DLL, runs the `VerifyLayout` script to the first stage's frame and prints its report,
  hardcoded object slots included; it exits 1 on a `FAIL`. A real run makes the same check
  before its first stage). **Running it without
  arguments runs every configured stage**: 16 threads, hours, thousands of .m64 files under
  `analysis/m64/`, and the viewer's window, a tab per stage (README.md, "The viewer").
  `--stage <name>` runs one stage from the previous stage's saved solutions
  (README.md, "Running the pipeline").
- Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1` (add `-Config Release`,
  `-Compiler clang`, `-Filter '*Script*'`). Under a second without the DLL, a few seconds with it.
  It builds only the `tasfw-tests` target: a count check with `bitfs-turn.exe` after a source
  change needs `scriptsuild.ps1` first, or it runs the previous build (ROADMAP 3.14).
- The DLL-level check is `build\Release\out\dllcheck.exe <dll> <m64> <frame>
  [--version jp|us] [--save-mode full|fixed|dirty] [--leak-scan [frames]] [--objects] [--dirty-scan [frames]]
  [--dirty-replay] [--levels] [--trace [frames]]` (docs/libsm64.md). It plays to a frame,
  verifies the struct layouts
  against the game, and prints frame-advance and save/load cost in the chosen save mode.
  Takes under a second. `--leak-scan` lists every byte range of `.data`/`.bss` that a load
  does not restore; `--objects` lists every active object in `gObjectPool` with its
  behavior, params, position and home; `--dirty-scan` and `--dirty-replay` count the 4 KB
  pages the game writes per frame (under pattern inputs from the frame, or while replaying
  the movie to it) and which of them the fixed slices miss; `--levels` lists every frame at
  which the movie changes level or area (how the frame a movie enters a level is found);
  `--trace` prints Mario, the camera and what a movie carries between levels for the frames
  before the frame; `python scripts\dll_symbols.py
  <dll> -` names the offsets in any of these outputs from the DLL's exports.
- A movie for another game version is made with `build\Release\out\m64splice.exe <dll a>
  <a.m64> <dll b> <b.m64> <level> <out.m64> [--lead frames]`: movie a to the frame it enters
  the level, movie b from its own, played through the game and compared frame by frame with
  movie b on its own DLL (docs/libsm64.md, "A movie for the US game").
- Performance numbers come from `Release` or `RelWithDebInfo` builds only. Debug uses `/Od`.
- Perf suite: `powershell -ExecutionPolicy Bypass -File scripts\perf.ps1` builds Release,
  runs `tasfw-perf.exe`, and compares against `perf\baselines\<computername>\`. Tier A
  needs nothing; the Tier B and C (libsm64) families run when `res\` has the DLL and movie,
  or pass `-Dll`/`-M64` (thread scaling also needs the copies `sm64_jp_1.dll` to
  `sm64_jp_16.dll`); Tier D runs `bitfs-turn` on `perf\tierd-*.json` when the 16 DLL
  copies exist, the two workloads plain and again with the viewer tailing them headless
  (about ten minutes with the reference; `-NoTierD` skips it). Anything missing
  is skipped. Time gates against the baseline commit's own binaries, which `-SaveBaseline`
  keeps under `perf\reference\` (gitignored) and every run launches interleaved with the
  current build, so the machine's drift cancels; without them the compare is absolute and
  says so. The runner refuses to start next to a VM or a busy CPU, keeps the single-thread
  rows and the deterministic Tier D run on performance cores (never 16 threads packed onto
  them: not the pipeline's shape, and the configuration that exposed the savestate race of
  ROADMAP 3.12), switches the power plan for the run, and asks for
  `-SetupDefender` once per machine (docs/performance.md, "Running the suite").

## Hard rules

1. **Game state must be a pure function of (start save, inputs).** Only change the game by
   advancing frames with inputs (`AdvanceFrameWrite`, `Apply`). Direct pokes into DLL memory
   (marked `//! UNSAFE` where they exist) cannot be replayed from a savestate and silently
   corrupt the search. Reads are fine. Any desync (a replay of the same inputs from the same
   save reaching a different state, a decoded block not matching its recording) is a
   critical error: establish the root cause; never tolerate it as a rate.
2. **Do not edit struct layouts under `tasfw-core/inc/sm64/`** unless you are deliberately
   moving to a different DLL build. The DLL is ground truth; the headers mirror it.
   `test_sm64_layout.cpp` checks them against the pinned DLL's DWARF and
   `scripts/decomp_diff.py` against their upstream revision (docs/decomp.md); a deliberate
   move regenerates the table and moves the pin.
3. **Keep scattershot deterministic.** All randomness goes through `GetTempRng`/`GetRng`.
   `ApplyMovement` must be a deterministic function of game state plus that RNG, because
   blocks are re-created by replaying scripts from recorded seeds. Never use `rand`, time,
   thread ids, or mutable state that survives across pellets. The hash behind that RNG and
   the block table is the framework's own (`HashByte`), so a seed replays the same search
   on every platform; nothing that must reproduce goes through `std::hash` or the
   iteration order of an unordered container (docs/compilers.md).
4. **Respect the script lifecycle.** `validation()` and `assertion()` run in a reverted
   sandbox and must not rely on side effects; `execution()` is the only phase whose input
   diff can persist. Results leave a script through `CustomStatus`, not member side effects.
5. **No absolute paths in source.** Route paths through `config.json` (`PipelineConfig`) or
   `Configuration`. There are none left; keep it that way.
6. **Do not commit** anything under `res/`, `build/`, `out/`, or `analysis/*.csv`, and never
   a ROM or an unlocked libsm64 binary (`.dll` or `.so`) anywhere; do not vendor the locked
   ones either, link to wafel or bitfs-sbb (docs/libsm64.md). If CI ever needs the game,
   the unlock key lives in a maintainer-only secret and the job skips without it, so forks
   and outside pull requests never see it.
7. Do not change what a `LibSm64` save mode copies, or when `dirty` takes a baseline, without
   the Tier B and D numbers; the `fixed` slices (`LibSm64FixedSlices` in `LibSm64.hpp`) are
   tuned to the pinned DLL build and are what the BitFS search runs on (docs/libsm64.md,
   "Savestates"). No mode copies or restores the C runtime's state at the sections' edges
   (`LibSm64KnownGameBytes`, derived per build by `scripts/dll_game_bytes.py`): the loader
   writes it from every thread of the process, and a savestate that carried it hung the
   16-thread runs (docs/libsm64.md, "The game's bytes").
8. **Performance regressions are bugs.** Changes under `tasfw-core`, `tasfw-scattershot` or
   `tasfw-resources` must include the perf suite delta table (docs/performance.md) and, for
   anything the DLL workload exercises, before/after wall time and the frame-advance/save/load
   counts `bitfs-turn` prints per stage, on a stated fixed workload in a Release build. No
   per-frame heap allocation, no I/O under a critical section, and nothing that adds a frame
   advance without a measured reason.
9. **Scripts do not touch the resource.** `Script` owns every interaction with it: frames
   advance through `AdvanceFrameRead`/`AdvanceFrameWrite`, saves and loads through `Save`,
   `Load`, `LongLoad` and the child-script and ad-hoc runners, the frame through
   `GetCurrentFrame`. Game memory is read with `ReadState("symbol")`, the one way a script
   sees it (its typed, const and guarded form, and the write side, `HackMemory`, are Phase
   5's, with the hacks), and a script's state reaches a run on another resource through
   `ExportSave` and the builder's `ImportSave`. Savestate management is
   automatic in the normal case; the
   manual methods are escape hatches, and a design that needs a script author to call or
   know something new is the wrong design. This holds for user scripts, stage scripts and
   the framework's own scripts (`ScattershotThread`) alike. A check on the game is a script
   (`VerifyLayout`, which reads through `ReadState` like any other); only a tool or test whose
   subject is the resource itself (savestate cost, the leak scan) drives it directly, outside
   any script, with its own loop. Anything a resource needs (a baseline, a mode) it decides
   for itself from what it already observes, in the framework's own terms, never through a
   hook a script has to call or vocabulary borrowed from one pipeline.
10. **Framework changes are designed first.** Anything that changes the shape of `tasfw-core`
    or `tasfw-scattershot` (a method or virtual on `Script`, `TopLevelScript` or `Resource`,
    a free function in their headers, a new concept or term) is proposed to the maintainer
    and agreed before it is written, however small, and even when it fixes a violation of
    another rule: say what the concept is in the framework's own terms, who calls it, what
    it costs, and which alternatives leave the framework unchanged. A class's public surface
    is its contract: each member it exposes is a decision about what information that
    component shares and with whom, so nothing is added for convenience alone, and a member
    that only repeats access the class already grants does not belong. Bug fixes and measured
    optimizations behind an unchanged interface do not need this. The Stop hook names any
    framework header a turn changed and asks where that discussion happened.

## Conventions

- Formatting target is `.clang-format` (tabs, Allman braces). `tasfw-scattershot/` and the
  newer bruteforcer headers use four spaces instead. Match the file you are in; do not
  reformat files wholesale in a functional change.
- Templates are header-only: declarations in `Foo.hpp`, definitions in `Foo.t.hpp` included at
  the bottom of the `.hpp`. Keep that split. One header per concept (`SlotManager.hpp`,
  `TopLevelScript.hpp`, `M64.hpp`), each with its own `.t.hpp` where it has definitions; a
  file that only completes another (`TopLevelScript.hpp`, `TopLevelScriptBuilder.hpp`,
  `Script.compare.hpp`, the compare family's entry points as a member include of `Script`)
  refuses to be included first. A class declares its members in one order: nested types,
  construction, the lifecycle the author implements, the runners, the cursor and the
  inputs, saves and loads, state; then friends, data, and the internals grouped by concern,
  a one-line comment naming each group; the `.t.hpp` defines in that order.
- A script is `class X : public Script<LibSm64>` with a nested `CustomScriptStatus` and the
  three lifecycle methods. One that reads metrics names its metric script once,
  `using MetricScript = T;`, and calls `GetMetrics(frame)`; a metric script and a root name
  nothing. Child scripts are run with `Execute<X>` (revert), `Modify<X>`
  (keep diff if asserted) or `Test<X>` (revert and drop the diff from the status).
- Ad-hoc lambdas use `ExecuteAdhoc` / `ModifyAdhoc` / `TestAdhoc` with the same semantics.
  Use them for one-off attempts; write a script class when the logic is heavy or reused.
  Both manage savestates and reverts for you.
- Naming: PascalCase types and methods, `_camelCase` private members, `CustomStatus` for the
  public result object.
- A script's shape (the maintainer, 2026-09-21): its helpers are private static members of
  the script class, never namespace-scope functions; a method does one step of the
  algorithm, split where it reads better, without obsessing; a compare call's lambdas
  carry their role as an inline comment (`//paramsGenerator`, `//script`, `//comparator`,
  `//terminator`, as `BitFsScApproach_AttemptDr.cpp` does); and a script's own `Load` or
  `Rollback` to undo a trial is a code smell, since an ad-hoc body that returns false is
  reverted to where it began (docs/tasing.md, "Saves and loads are automatic").
- Compiler workarounds exist (`using` directives in `ScattershotThread`, the named static in
  `Inputs.cpp`). Each is catalogued in docs/compilers.md; add yours there.

## TASing with the framework

[docs/tasing.md](docs/tasing.md) is the how-to: which tool to reach for, how a goal becomes
a script and a run, how a result is checked, and the pitfalls. The maintainer's rules
(2026-09-15, clarified 2026-09-16):

- Use the framework's methods and idioms for TASing; do not hack around it.
- If something you want to do seems impossible, bring it up: it may be, with some direction.
- Use the existing scripts as a guideline, not a constraint; there may be better ways within
  the framework. Experiment.
- The compare methods may help even though they are not used much.
- Use the framework, the game's mechanics from the decompilation and general algorithmic
  knowledge to make TASes as fast as possible and scripts that perform well and are efficient.
  The two trade off and it is not always possible to improve one without hurting the other;
  which matters more depends on the context: usually, in a TASing context, saving movie
  frames; for the squish-cancel brute forcer, overall performance.
- Ad-hoc scripts for simple or one-off tasks; script classes for heavy or modular ones that
  other scripts may want to use.
- Metrics are a powerful feature for making informed decisions within a script, in a
  performant and organized way; also for raw variables a script wants to look at in the
  past without rewinding, and looking into the future can sometimes be worth it too.

## Verifying a change

Verification means:

1. `scripts\build.ps1` succeeds with **no warnings** on MSVC **and** with
   `-Compiler clang`. CI builds every compiler with `TASFW_WARNINGS_AS_ERRORS=ON`;
   `-CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces that locally.
2. `scripts\test.ps1` passes, on both compilers. With the DLL in `res\` it also runs the
   libsm64 tests (`test_libsm64_*.cpp`); the smoke test among them pins Mario's exact state
   at frame 3330 of the committed movie (CI runs the same group on the bitfs-sbb build when
   it has the key), and with `res\sm64_us_0.dll` there too the group runs again on the US
   game at frame 3397 of `movies\bitfs-pyramid-us.m64`, the same state. Anything touching
   `tasfw-core` needs a test in `tasfw-tests` for the behavior it changes.
3. Reason explicitly about determinism and savestate purity for anything touching
   `Script.t.hpp`, `ScattershotThread.t.hpp` or `LibSm64.cpp`; the `test_script_*.cpp`
   files encode those invariants on the mock resource (fixtures in `script_fixtures.hpp`),
   so extend them rather than arguing in prose.
4. For anything on a hot path, measure. Run `scripts\perf.ps1` and paste its delta table;
   it exits non-zero on a time regression over 10% against the reference (the baseline
   commit's binaries run in the same session), an allocation increase, or any increase
   in an exact work count (Tier C and D rows: frame advances, saves, loads, shots, scripts,
   blocks, solutions). With the DLL in `res\` that covers Tiers A to D; without it, run a
   fixed DLL workload before and after in Release and report wall time plus the frame
   advances, saves and loads `bitfs-turn` prints. Counts must not go up; time must not
   regress. See docs/performance.md for what counts as a hot path.
5. Say in your summary exactly what you could not run.

Work on a branch and open a PR against `master`; that is how the repo has always been merged.

## Documentation must match the repository

The documentation (this file, ARCHITECTURE.md, ROADMAP.md, README.md, docs/) describes the
repository as it is now, so that the next agent can pick up where the last one stopped
without re-discovering anything. After every change, reconcile it:

- **Extension**: the change does what the docs already ask for or plan, or adds something
  under an existing policy (a roadmap item progressed, a new test, benchmark, tool, script
  or flag of an established kind, a re-measured number, a new compiler pitfall). Update the
  docs yourself, in place, in the existing style.
- **Deviation**: the change contradicts, weakens or bypasses a documented rule, invariant,
  roadmap goal or claim, or introduces a rule, constraint or design decision the docs never
  anticipated. Do not edit the docs for it. Report it, propose the options (revert, or amend
  the docs with exact wording), and wait for the maintainer's approval.

Clarifications count as extensions: when a conversation settles what something is for or how
it behaves, write it into the doc that should have said so. Work you identify but leave for
later (a missing test, a cleanup, a measurement) becomes a ROADMAP.md item; it is not
offered in chat as a follow-up.

Claude Code enforces this with a Stop hook in `.claude/settings.json`. At every prompt
`.claude/hooks/doc-review.py` fingerprints every tracked or untracked non-`.md` file outside
`docs/`; when the turn ends with that fingerprint changed, the hook blocks the stop once and
hands the agent the procedure in `.claude/hooks/doc-review-prompt.md`; when the changed files
include framework headers (`tasfw-core/inc/tasfw/`, `tasfw-scattershot/inc/`) it names them
and asks where the design discussion hard rule 10 requires took place. Flagged changes whose
review was interrupted stay pending until a review completes. State lives in the OS temp
directory under `tasfw-doc-review/`, never in the repo. The hook needs `git` and `python`
(or `python3`) on PATH and runs under bash (Git Bash on Windows); `TASFW_DOC_REVIEW=0`
disables it and `python .claude/hooks/doc-review.py check` prints what a review would see.
Agents without hooks follow the same procedure by hand at the end of every change.

## Known problems you will run into

- `tasfw-core/src/decomp/` (`Math.cpp`, `Surface.cpp`, `Pyramid.cpp`) reimplements pieces of
  the game on the copied structs so the downhill-angle scripts can predict the floor angle
  after the pyramid tilts without advancing a frame. It duplicates physics that
  `PyramidUpdate` also has on its own surface type, and it is not covered by the drift test.
  `scripts/decomp_pin.json` cites the upstream functions each file came from (docs/decomp.md).
- Warning C4715 in the `TurnAround` lambda of `Scattershot_BitfsDr.cpp` is a real bug (not
  all paths return a value).
- The pyramid object is `gObjectPool[84]` and the track platform `gObjectPool[85]`,
  level-specific indices that stay by decision (ROADMAP 2.4: two objects share the pyramid
  behavior). They are declared with their behavior and home in `BitFsObjects.hpp` and
  verified by the `VerifyLayout` script the pipeline runs before its first stage, so a
  spawn-order change fails at start-up. Any new hardcoded slot goes into that list.
- One DLL copy per thread is required on Windows and Linux (`LoadLibrary` and `dlopen` both
  hand back the already-loaded image for a path), so `Configuration::ResourcePaths` must have
  at least `TotalThreads` entries.
- The `dirty` save mode works by deliberate first-write page faults. A debugger stops on
  each unless told to ignore access violations; run with `"saveMode": "full"` when
  debugging, and expect test harnesses that install their own `SIGSEGV` handler to need
  the re-arming `LibSm64` already does (docs/libsm64.md, "Linux").
- The DLL's `.data` and `.bss` are not all game: their edges hold the mingw-w64 runtime's
  own state, and the Windows loader runs the DLL's TLS callback on **every** thread of the
  process at its exit (a Windows thread-pool worker retiring counts), which takes a
  critical section in the last page of `.bss`. `LibSm64` leaves those bytes out of every
  savestate (`LibSm64KnownGameBytes`); a new DLL build needs its entry from
  `scripts/dll_game_bytes.py`, or it is saved whole and the race is back (docs/libsm64.md,
  "The game's bytes"; ROADMAP 3.12).

## Glossary

- **BitFS**: Bowser in the Fire Sea. **DR**: dive recover. **SC**: squish cancel. **ABC**:
  A button challenge, a run that never presses A; the setup is for one, so no A after the
  level entry.
- **Pyramid normal / xzSum**: the tilting platform's surface normal; `|nX| + |nZ|` measures tilt.
- **Crossing / oscillation**: the tilt direction reversing past equilibrium; an oscillation is
  a pair of crossings at least `minOscillationFrames` apart.
- **HAU**: hexadecimal angle unit, an SM64 angle divided by 16.
- **ARE**: adjusted remainder error, distance in float ULPs from a target normal after
  stepping in 0.01 increments (see `CalculateARE`).
- **Block / segment / pellet / shot**: scattershot terms, defined in ARCHITECTURE.md.
