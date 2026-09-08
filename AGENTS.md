# AGENTS.md

Working instructions for AI coding agents and new contributors. Keep this file short and
operational. Long-form material lives in [ARCHITECTURE.md](ARCHITECTURE.md),
[ROADMAP.md](ROADMAP.md) and [docs/](docs/).

## What this project is

sm64-tas-scripting ("TASFW") is a C++20 framework for scripting and brute-forcing
Super Mario 64 TAS inputs. The game itself runs inside a native x64 DLL (wafel's libsm64,
built from the SM64 decompilation). Scripts drive it with savestates and frame advances,
and a multithreaded "scattershot" search explores input space on top of that.

The concrete goal driving the code today is a brute forcer for the squish-cancel setup on
the BitFS tilting pyramid. The framework is intended to become game-agnostic later.

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
templates and concepts on purpose, so that scripts, resources and state trackers resolve at
compile time and `if constexpr` removes what is unused. In anything executed per frame,
prefer static dispatch over virtual calls, `std::function`, `dynamic_cast`, string-keyed
lookups or heap allocation. An abstraction that costs at runtime in a hot path has to earn
its place with numbers, and "it is cleaner" is not a number.

## Repo map

| Path | What it is |
|---|---|
| `tasfw-core/` | Engine: `Script`, `TopLevelScript`, `Resource`, savestate slots, m64 I/O, input math. Header-heavy templates. |
| `tasfw-core/inc/sm64/` | Hand-copied decomp structs, enums and trig tables. Must match the DLL's x64 layout. |
| `tasfw-resources/` | `LibSm64` (drives the game DLL) and `PyramidUpdate` (standalone reimplementation of pyramid tilt physics used as a fast stand-in). |
| `tasfw-scattershot/` | Header-only OpenMP brute-force search (blocks, segments, solutions, CSV export). |
| `tasfw-scripts/` | Reusable BitFS scripts (pyramid oscillation, downhill angle search, dive-recover attempts) and scattershot stages. |
| `tasfw-bruteforcers/bitfs-turnaround/` | The only executable (`bitfs-turn.exe`): the BitFS pipeline as config-selected stages (`config.json`, `Stages.cpp`, `PipelineConfig`). `--list`, `--dry-run`, `--stage`. |
| `tasfw-perf/` | Performance suite (Tier A microbenchmarks on an in-memory fake resource). Release only. |
| `tasfw-tools/` | `dllcheck`: DLL layout self-check plus frame-advance and savestate cost measurement. |
| `tasfw-tests/` | Correctness tests (doctest). DLL-free tests always run; the libsm64 smoke test runs when `res\` has the DLL and movie. |
| `tasfw-testing/` | Header-only test support shared by tests and benchmarks (`FakeResource`). |
| `perf/` | Committed benchmark baselines per machine; `perf/results/` is gitignored. |
| `analysis/` | R script that plots scattershot CSV output; also the pipeline's default output directory (CSVs, `solutions/*.json`, `m64/`), all gitignored. |
| `res/` | Gitignored runtime inputs: 24 copies of the libsm64 DLL, source .m64 files, and thousands of exported solution .m64 files. |
| `scripts/` | `build.ps1`, the supported build entry point on Windows. |
| `cmake/` | `AddOptimizationFlags` (arch flag, FP determinism, LTO, OpenMP; applied to every first-party target), `Warnings` (`/W3`, `/W4`, `-Wall -Wextra` on every first-party target, and `TASFW_WARNINGS_AS_ERRORS`) and `SystemIncludes` (fetched dependencies as system headers, so their warnings never count). |
| `docs/` | Provenance of the DLL and other reference notes. |

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
  by CMake, hash-pinned and cached in `build\downloads` for offline builds. OpenMP is required.
- Runtime inputs are not in git. You need `res\sm64_jp_0.dll` .. `res\sm64_jp_23.dll` (on
  Linux, `.so` copies) and the .m64 files named in `config.json`.
  [docs/libsm64.md](docs/libsm64.md) says where to get either and how to unlock them.
- `bitfs-turn.exe --list` and `--dry-run` are safe: no search runs. **Running it without
  arguments runs every configured stage**: 16 threads, hours, thousands of .m64 files under
  `analysis/m64/`. `--stage <name>` runs one stage from the previous stage's saved solutions
  (README.md, "Running the pipeline").
- Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1` (add `-Config Release`,
  `-Compiler clang`, `-Filter '*Script*'`). Under a second without the DLL, a few seconds with it.
- The DLL-level check is `build\Release\out\dllcheck.exe <dll> <m64> <frame> [--lightweight]
  [--leak-scan [frames]]` (docs/libsm64.md). It plays to a frame, verifies the struct layouts
  against the game, and prints frame-advance and save/load cost. Takes under a second.
  `--leak-scan` lists every byte range of `.data`/`.bss` that a load does not restore;
  `python scripts\dll_symbols.py <dll> -` names them from the DLL's exports.
- Performance numbers come from `Release` or `RelWithDebInfo` builds only. Debug uses `/Od`.
- Perf suite: `powershell -ExecutionPolicy Bypass -File scripts\perf.ps1` builds Release,
  runs `tasfw-perf.exe`, and compares against `perf\baselines\<computername>.json`. Tier A
  needs nothing; the Tier B and C (libsm64) families run when `res\` has the DLL and movie,
  or pass `-Dll`/`-M64` (thread scaling also needs the copies `sm64_jp_1.dll` to
  `sm64_jp_16.dll`); Tier D runs `bitfs-turn` on `perf\tierd-*.json` when the 16 DLL
  copies exist (about five minutes; `-NoTierD` skips it). Anything missing is skipped.

## Hard rules

1. **Game state must be a pure function of (start save, inputs).** Only change the game by
   advancing frames with inputs (`AdvanceFrameWrite`, `Apply`). Direct pokes into DLL memory
   (marked `//! UNSAFE` where they exist) cannot be replayed from a savestate and silently
   corrupt the search. Reads are fine. Any desync (a replay of the same inputs from the same
   save reaching a different state, a decoded block not matching its recording) is a
   critical error: establish the root cause; never tolerate it as a rate.
2. **Do not edit struct layouts under `tasfw-core/inc/sm64/`** unless you are deliberately
   moving to a different DLL build. The DLL is ground truth; the headers mirror it.
3. **Keep scattershot deterministic.** All randomness goes through `GetTempRng`/`GetRng`.
   `ApplyMovement` must be a deterministic function of game state plus that RNG, because
   blocks are re-created by replaying scripts from recorded seeds. Never use `rand`, time,
   thread ids, or mutable state that survives across pellets.
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
7. Do not "simplify" the lightweight save offsets in `LibSm64.cpp` without measuring; they are
   tuned to the pinned DLL build and are the main reason the search is fast.
8. **Performance regressions are bugs.** Changes under `tasfw-core`, `tasfw-scattershot` or
   `tasfw-resources` must include the perf suite delta table (docs/performance.md) and, for
   anything the DLL workload exercises, before/after wall time and the frame-advance/save/load
   counts `bitfs-turn` prints per stage, on a stated fixed workload in a Release build. No
   per-frame heap allocation, no I/O under a critical section, and nothing that adds a frame
   advance without a measured reason.

## Conventions

- Formatting target is `.clang-format` (tabs, Allman braces). `tasfw-scattershot/` and the
  newer bruteforcer headers use four spaces instead. Match the file you are in; do not
  reformat files wholesale in a functional change.
- Templates are header-only: declarations in `Foo.hpp`, definitions in `Foo.t.hpp` included at
  the bottom of the `.hpp`. Keep that split.
- A script is `class X : public Script<LibSm64>` with a nested `CustomScriptStatus` and the
  three lifecycle methods. Child scripts are run with `Execute<X>` (revert), `Modify<X>`
  (keep diff if asserted) or `Test<X>` (revert and drop the diff from the status).
- Ad-hoc lambdas use `ExecuteAdhoc` / `ModifyAdhoc` / `TestAdhoc` with the same semantics.
  Use them for one-off attempts; write a script class when the logic is heavy or reused.
  Both manage savestates and reverts for you.
- Naming: PascalCase types and methods, `_camelCase` private members, `CustomStatus` for the
  public result object.
- Compiler workarounds exist (`ScriptFriend`, `using` directives in `ScattershotThread`, the
  named static in `Inputs.cpp`). Each is catalogued in docs/compilers.md; add yours there.

## Verifying a change

Verification means:

1. `scripts\build.ps1` succeeds with **no warnings** on MSVC **and** with
   `-Compiler clang`. CI builds every compiler with `TASFW_WARNINGS_AS_ERRORS=ON`;
   `-CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces that locally.
2. `scripts\test.ps1` passes, on both compilers. With the DLL and movie in `res\` it also
   runs the libsm64 smoke test, which pins Mario's exact state at frame 3330. Anything
   touching `tasfw-core` needs a test in `tasfw-tests` for the behavior it changes.
3. Reason explicitly about determinism and savestate purity for anything touching
   `Script.t.hpp`, `ScattershotThread.t.hpp` or `LibSm64.cpp`; `test_script.cpp` encodes
   those invariants on the fake resource, so extend it rather than arguing in prose.
4. For anything on a hot path, measure. Run `scripts\perf.ps1` and paste its delta table;
   it exits non-zero on a time regression over 10%, an allocation increase, or any increase
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
hands the agent the procedure in `.claude/hooks/doc-review-prompt.md`. Flagged changes whose
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
- Warning C4715 in the `TurnAround` lambda of `Scattershot_BitfsDr.cpp` is a real bug (not
  all paths return a value).
- The pyramid object is found as `gObjectPool[84]`, a level-specific index.
- One DLL copy per thread is required on Windows and Linux (`LoadLibrary` and `dlopen` both
  hand back the already-loaded image for a path), so `Configuration::ResourcePaths` must have
  at least `TotalThreads` entries. On Linux the copies are not enough yet: `LibSm64`'s
  dirty-page save path is single-instance per process (ROADMAP 3.4).

## Glossary

- **BitFS**: Bowser in the Fire Sea. **DR**: dive recover. **SC**: squish cancel.
- **Pyramid normal / xzSum**: the tilting platform's surface normal; `|nX| + |nZ|` measures tilt.
- **Crossing / oscillation**: the tilt direction reversing past equilibrium; an oscillation is
  a pair of crossings at least `minOscillationFrames` apart.
- **HAU**: hexadecimal angle unit, an SM64 angle divided by 16.
- **ARE**: adjusted remainder error, distance in float ULPs from a target normal after
  stepping in 0.01 increments (see `CalculateARE`).
- **Block / segment / pellet / shot**: scattershot terms, defined in ARCHITECTURE.md.
