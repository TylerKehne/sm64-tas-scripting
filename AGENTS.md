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
| `tasfw-bruteforcers/bitfs-turnaround/` | The only executable (`bitfs-turn.exe`). `main.cpp` chains the BitFS pipeline stages. |
| `analysis/` | R script that plots scattershot CSV output. CSVs are gitignored. |
| `res/` | Gitignored runtime inputs: 24 copies of the libsm64 DLL, source .m64 files, and thousands of exported solution .m64 files. |
| `scripts/` | `build.ps1`, the supported build entry point on Windows. |
| `docs/` | Provenance of the DLL and other reference notes. |

## Build and run

- Primary platform is Windows with MSVC. Linux code paths exist but have not been built recently.
- Build: `powershell -ExecutionPolicy Bypass -File scripts\build.ps1` (add `-Config Release`, `-Clean`).
  It finds Visual Studio via vswhere, so cmake and ninja do not need to be on PATH.
- Output: `build\<Config>\out\bitfs-turn.exe` with `config.json` copied next to it.
- Dependencies (nlohmann/json, range-v3) are fetched by CMake. OpenMP is required.
- Runtime inputs are not in git. You need `res\sm64_jp_0.dll` .. `res\sm64_jp_23.dll` and the
  .m64 files named in `main.cpp` and `config.json`. See [docs/libsm64.md](docs/libsm64.md).
- **Do not run `bitfs-turn.exe` as a smoke test.** It launches the full 16-thread pipeline,
  runs for hours, and writes thousands of .m64 files into `res/` and CSVs into `analysis/`.
  A real smoke test is ROADMAP item 1.2; the performance suite is 1.3.
- Performance numbers come from `Release` or `RelWithDebInfo` builds only. Debug uses `/Od`.

## Hard rules

1. **Game state must be a pure function of (start save, inputs).** Only change the game by
   advancing frames with inputs (`AdvanceFrameWrite`, `Apply`). Direct pokes into DLL memory
   (marked `//! UNSAFE` where they exist) cannot be replayed from a savestate and silently
   corrupt the search. Reads are fine.
2. **Do not edit struct layouts under `tasfw-core/inc/sm64/`** unless you are deliberately
   moving to a different DLL build. The DLL is ground truth; the headers mirror it.
3. **Keep scattershot deterministic.** All randomness goes through `GetTempRng`/`GetRng`.
   `ApplyMovement` must be a deterministic function of game state plus that RNG, because
   blocks are re-created by replaying scripts from recorded seeds. Never use `rand`, time,
   thread ids, or mutable state that survives across pellets.
4. **Respect the script lifecycle.** `validation()` and `assertion()` run in a reverted
   sandbox and must not rely on side effects; `execution()` is the only phase whose input
   diff can persist. Results leave a script through `CustomStatus`, not member side effects.
5. **No new absolute paths in source.** Route paths through `config.json` or `Configuration`.
   The existing ones in `main.cpp` and `ScattershotThread.t.hpp` are on the roadmap to remove.
6. **Do not commit** anything under `res/`, `build/`, `out/`, or `analysis/*.csv`.
7. Do not "simplify" the lightweight save offsets in `LibSm64.cpp` without measuring; they are
   tuned to the pinned DLL build and are the main reason the search is fast.
8. **Performance regressions are bugs.** Changes under `tasfw-core`, `tasfw-scattershot` or
   `tasfw-resources` must include the perf suite delta table (docs/performance.md), or until
   it exists, before/after wall time and frame-advance/save/load counts on a stated fixed
   workload in a Release build. No per-frame heap allocation, no I/O under a critical section,
   and nothing that adds a frame advance without a measured reason.

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
- Naming: PascalCase types and methods, `_camelCase` private members, `CustomStatus` for the
  public result object.
- MSVC-specific workarounds exist (`ScriptFriend`, `using` directives in `ScattershotThread`).
  Leave a comment when you add one.

## Verifying a change

Until the test tiers in ROADMAP 1.2 and 1.3 exist, verification means:

1. `scripts\build.ps1` succeeds with **no new warnings** (there are 29 baseline warnings;
   the list is in ROADMAP 1.5).
2. Reason explicitly about determinism and savestate purity for anything touching
   `Script.t.hpp`, `ScattershotThread.t.hpp` or `LibSm64.cpp`.
3. For anything on a hot path, measure. Build `-Config Release`, run a fixed workload before
   and after, and report wall time plus `nFrameAdvances`, `nSaves` and `nLoads`. Counts must
   not go up; time must not regress. See docs/performance.md for what counts as a hot path.
4. Say in your summary exactly what you could not run.

Work on a branch and open a PR against `master`; that is how the repo has always been merged.

## Known problems you will run into

- `main.cpp` is an experiment log, not a program. Stages are enabled by commenting code in
  and out. Do not try to "fix" it piecemeal; see ROADMAP 1.4.
- Warning C4715 in the `TurnAround` lambda of `Scattershot_BitfsDr.cpp` is a real bug (not
  all paths return a value).
- `build/` may contain a stale mix of Visual Studio and Ninja Multi-Config artifacts from
  2025. `scripts\build.ps1` uses `build\<Config>` and does not touch the old files; delete
  them if they confuse tooling.
- The pyramid object is found as `gObjectPool[84]`, a level-specific index.
- One DLL copy per thread is required on Windows (a path loads once per process), so
  `Configuration::ResourcePaths` must have at least `TotalThreads` entries.

## Glossary

- **BitFS**: Bowser in the Fire Sea. **DR**: dive recover. **SC**: squish cancel.
- **Pyramid normal / xzSum**: the tilting platform's surface normal; `|nX| + |nZ|` measures tilt.
- **Crossing / oscillation**: the tilt direction reversing past equilibrium; an oscillation is
  a pair of crossings at least `minOscillationFrames` apart.
- **HAU**: hexadecimal angle unit, an SM64 angle divided by 16.
- **ARE**: adjusted remainder error, distance in float ULPs from a target normal after
  stepping in 0.01 increments (see `CalculateARE`).
- **Block / segment / pellet / shot**: scattershot terms, defined in ARCHITECTURE.md.
