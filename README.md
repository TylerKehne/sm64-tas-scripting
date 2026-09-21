# sm64-tas-scripting
A C++23 framework for scripting and brute-forcing Super Mario 64 TAS inputs. The game runs
inside a native x64 DLL (wafel's libsm64); scripts drive it with savestates and frame
advances, and a multithreaded "scattershot" search explores input space on top of that.
The current driver is a squish-cancel setup brute forcer for the BitFS tilting pyramid.

Still very much a work in progress. Start with:

- [AGENTS.md](AGENTS.md): rules and conventions for anyone (human or AI) changing the code.
- [ARCHITECTURE.md](ARCHITECTURE.md): how scripts, savestates and scattershot fit together.
- [ROADMAP.md](ROADMAP.md): what is planned and in what order.
- [docs/libsm64.md](docs/libsm64.md): where the game DLL comes from and what depends on it.
- [docs/decomp.md](docs/decomp.md): what was copied from the decompilation, at which revision, and how the copies and the DLL are checked against each other.
- [docs/performance.md](docs/performance.md): performance is a correctness requirement; how it is measured and gated.
- [docs/performance-changelog.md](docs/performance-changelog.md): the measured history, one delta table per hot-path change.
- [docs/tasing.md](docs/tasing.md): how to TAS with the framework: which tool to reach for, how a goal becomes a script and a run, how a result is checked.

These files are kept current by design: a Claude Code hook in [.claude/](.claude/) reviews
them after every change (see AGENTS.md, "Documentation must match the repository").

# Building instructions
CMake 3.22+ and a C++23 compiler with OpenMP (MSVC 2022, GCC 14 or Clang 18 and newer). The dependencies (nlohmann/json; doctest and
Google Benchmark for the tests and benchmarks) are downloaded by CMake, verified by hash and
cached in `build\downloads`, so later builds work offline; no vcpkg needed.

**Windows (supported path)**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # Debug
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release
```

The script finds the latest Visual Studio (2026 or 2022) with vswhere, imports the x64
developer environment,
uses the cmake/ninja bundled with Visual Studio if none are on PATH, and configures from the
`CMakePresets.json` preset for the compiler and config (`msvc-release`, or `clang-cl-release`
with `-Compiler clang`). Output goes to `build\<Config>\out\bitfs-turn.exe`
(`build\<Config>-clang` for clang-cl). Visual Studio and VS Code pick up the same presets
when you open the folder, and `-CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces the
CI build.

Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1`. Benchmarks:
`scripts\perf.ps1`. Both accept `-Compiler clang` for the clang-cl build.

**Linux** (builds and passes the DLL-free tests in CI with GCC 14 and Clang 18; the game
path passes the smoke test against a Linux libsm64 `.so` on Ubuntu 26.04, which the `.so`
needs for its glibc, see [docs/libsm64.md](docs/libsm64.md); there is no macOS preset)

```bash
cmake --preset gcc-release          # or clang-release; build/Release-gcc, build/Release-clang
cmake --build --preset gcc-release
ctest --preset gcc-release
```

# Runtime inputs
The executable needs files that are not in git (see [docs/libsm64.md](docs/libsm64.md)):

- `res/sm64_jp_0.dll` through `res/sm64_jp_23.dll`, one copy of the libsm64 DLL per thread
  (on Linux `.so` copies and `"dllPattern": "sm64_{version}_{}.so"`). One command makes them from
  a ROM: `python scripts\unlock_libsm64.py --rom <sm64 jp>.z64 --out res --copies 24`
  (docs/libsm64.md); never commit a ROM or an unlocked binary.
- The source movie `config.json` names is committed under `movies/` (`bitfs-pyramid-jp.m64`,
  which CI, the tests and the perf suite use too), beside `bitfs-osc-final-jp.m64`, the
  oscillations done by hand to frame 3604, which nothing runs today (it was the source of a
  final-oscillation experiment), and two US movies: `1keyU.m64`, a whole 1-key run,
  and `bitfs-pyramid-us.m64`, the JP movie's BitFS part on the US way there, which the tests
  run on the US game (`unlock_libsm64.py --version us` makes `res/sm64_us_0.dll`) when it is
  there (docs/libsm64.md, "A movie for the US game").

# Running the pipeline

```
bitfs-turn [--config <file>] [--stage <name>] [--list] [--dry-run]
```

`bitfs-turn.exe` runs the BitFS squish-cancel pipeline described by its config: every
stage in order, or one stage with `--stage <name>`. Without `--config` it reads the
`config.json` next to the executable, which the build writes from
[tasfw-bruteforcers/bitfs-turnaround/config.json](tasfw-bruteforcers/bitfs-turnaround/config.json)
whenever the executable target builds, so a Visual Studio play button, which builds only
that target, keeps the copy current too.
`--list` prints the stage types and the configured stages; `--dry-run` resolves every path,
checks the files exist, loads one DLL, runs the `VerifyLayout` script to the first stage's
start frame and prints its report (struct layout, the hardcoded object slots), exiting 1 on
any `FAIL`; a real run makes the same check before its first stage. Both are safe. A full
run is not a smoke test: 16 threads, hours, thousands of exported `.m64` files, and the
viewer's window with a tab per stage and per `dr` pass ("The viewer").

Each stage writes its solutions to `<outputDirectory>/solutions/<stage>.json` (input diffs
plus named metrics). A stage run alone reads its input from the file its input stage wrote
last time, so a long pipeline can be advanced one stage at a time, and a search that needs
more shots continues as a new stage entry of the same type with `input` set to the earlier
one: its solutions become the new run's root blocks. The blocks themselves are never saved
(ROADMAP 4.2), and solutions are written when a stage finishes, so an interrupted stage
writes nothing (a cancellation that exports what was found is a Phase 5 item). Stages with
`"export": true` also write one movie per solution under `<outputDirectory>/m64/<stage>/`.
Scattershot CSVs and the `error.m64` dump go to `<outputDirectory>` as well, and a stage
with a `visualize` block opens the viewer on its CSV as it is written ("The viewer"). The stage
summary prints wall time and the frame advances, saves and loads summed over threads, which
are the fixed-workload numbers a performance change has to report (AGENTS.md, hard rule 8),
then the slot manager's line: the most savestates and bytes any thread held at once, and the
evictions and pool reuses (what the per-thread memory cap is up against; ROADMAP 3.5), and
where the threads' CPU time went: the resource's own advance, save and load as shares of
the process CPU time over the stage, with the cost of each, and the share outside the
resource, which is the framework, the scripts and the search (docs/performance.md, "Tier D").

# Configuration

One JSON file. Relative paths resolve against the file's own directory, unless the file has
`"baseDirectory"` (the build sets it in the copy next to the executable). Keys starting with
`_` are comments; any other unknown key is an error, with the location in the message.

```json
{
	"resources": { "dllDirectory": "../../res", "dllPattern": "sm64_{version}_{}.dll", "threads": 16, "saveMode": "fixed" },
	"m64": "../../res/source.m64",
	"outputDirectory": "../../analysis",
	"visualizer": "../../analysis/visualizer.py",
	"visualize": { "view": { "x": -715, "y": -1945, "width": 900, "height": 900 } },
	"scattershot": { "maxShots": 3000, "maxSolutions": 100, "seed": 6, "deterministic": false, "...": "any Configuration field" },
	"stages": [
		{
			"name": "tilt-x", "type": "tilt-target", "startFrame": 3330,
			"scattershot": { "maxShots": 30000, "deterministic": true, "seed": 111 },
			"args": { "targetNx": -0.17944, "targetNz": 0.3936, "targetDimension": "x" }
		},
		{
			"name": "tilt-z", "type": "tilt-target", "startFrame": 3330, "input": "tilt-x",
			"args": { "targetNx": -0.17944, "targetNz": 0.3936, "targetDimension": "z",
			          "fixNonTargetDimensionARE": true, "targetARE": "input:adjustedRemainderError0" },
			"select": { "sortBy": ["adjustedRemainderError0"], "take": 1 },
			"visualize": { "filters": [{ "column": "MarioFSpd", "max": 8 }] },
			"export": true
		}
	]
}
```

- `resources`: the DLL directory and file pattern (`{version}` becomes the game the movie's
  header names, `jp` or `us`, so the DLLs follow the movie; `{}` the thread index, one copy
  per thread), the thread count, the save mode (`saveMode`: `full`, `fixed` or `dirty`;
  `dirty` when absent, `fixed` in the committed config because it measures faster for this
  search; docs/libsm64.md, "Savestates"), `costModel` (default
  true; false disables the replay-versus-load cost model so a run is timing-independent,
  for diagnosis), and `savestateBudgetMB` (default 8192), the process budget for savestate
  memory: a resource subtracts its limit from the balance when it is created, whether it
  ever uses it or not, or fails if the balance is too low. The pipeline gives each thread's
  game resource an equal share of the budget less the 16 MB a script's `PyramidUpdate`
  takes per thread. The default is what one thread alone was allowed before the budget
  existed; the BitFS stages hold at most 27 savestates per thread (38 MB of `fixed`
  slices), and the stage summary's slot line shows the high-water mark and any eviction.
- `scattershot`: defaults for every stage, in the field names of `Configuration`
  (`pelletMaxScripts`, `pelletMaxFrameDistance`, `maxBlocks`, `maxShots`, `pelletsPerShot`,
  `shotsPerUpdate`, `startFromRootEveryNShots`, `maxConsecutiveFailedPellets`,
  `maxSolutions`, `seed`, `fitnessTieGoesToNewBlock`, `deterministic`, `csvSamplePeriod`).
  A stage's own `scattershot` object overrides them.
- `visualizer`: the viewer's path (`analysis/visualizer.py`), which a stage's `visualize`
  needs ("The viewer"). `visualize` at the top level holds defaults for every stage's
  block, overridden key by key by the stage's own; it enables nothing on its own. The
  committed one fixes a 900-by-900 window on the pyramid.
- A stage has a unique `name`, a `type` (`bitfs-turn --list`), a `startFrame`, optionally its
  own `m64`, an `input` (the name of an earlier stage), `args` for the stage type, a `select`
  applied to its output, `export`, and a `visualize` block that opens the viewer on the
  stage's CSV: `title` (the stage's name unless given), `interpreter` (`pythonw` on Windows,
  `python3` elsewhere), the plot's columns `x`, `y`, `angle` and `speed` (`MarioZ`,
  `MarioX`, `MarioFYaw`, `MarioFSpd`), its bin sizes `binX`, `binY`, `binAngle` and
  `binSpeed` (0.1, 0.1, 16, 0.1), `filters` (`[{ "column", "min", "max" }]`, either bound
  optional), `view` (`{ "x", "y", "width", "height" }`: a fixed window of the plot's units
  centered on x, y, drawn square to its units and never rescaled; without one the plot
  follows its data) and `sampledOnly` (true). An empty block takes every default; only a
  stage with a `csvSamplePeriod` has a CSV to show.
- Numeric `args` accept a number or `"input:<metric>"`, which takes that metric from the
  first solution of the input stage (for example the equilibrium frame the tilt search found).
  The argument names of each type are checked in `Stages.cpp`; an unknown one is an error.

The committed config is the pipeline as it was last run: the three tilt stages that fix
the adjusted remainder errors, the oscillations, the final oscillation, then the
dive-recover chain, with a CSV and the viewer on every stage (every hundredth novel block
on the tilt stages, whose novelty is high, every tenth elsewhere).

# The viewer

`analysis/visualizer.py` plots a run's CSV live: an arrow per state bin at Mario's position,
pointing along his facing yaw with his speed as its length, colored from orange for the
oldest shot to green for the newest (the CSV's `Shot` is each thread's own count, so the
first minute of a run is one shot and all green) and fading with the frame. A stage with a
`visualize` block
starts it when the run's CSV opens: the search writes the plot's parameters and the CSV's
path to `<csv>.visualizer.json` beside the CSV and launches the viewer detached, so nothing
is run by hand. One window holds a tab per run: the first viewer owns a localhost port, a
later launch hands its run to it and exits, and a viewer left open collects the next
pipeline's runs too. A tab closes with the "Close tab" button, Ctrl+W or a middle click on
its header, and the window stays for the next run. A tab whose run configured a `view`
keeps that window on every redraw, square to its units; the Autoscale box applies to the
other tabs. The refresh rate is set in the window and kept in
`analysis/visualizer_settings.json` with the port and the segment cap, and next to it the
window shows what the viewer costs: its own CPU time over the last 30 seconds as a share
of one core and of the machine, from the process clock, which resolves to about 0.05% of
one core over that window. The viewer reads
only what the CSV gained since its last look, redraws only the selected tab and only when
rows came in, runs at below-normal priority so the brute forcer's threads win any
contested core, stops polling a run that finished (the search rewrites the parameters
file with `finished` and the row count), doubles a tab's bins past the cap, and stretches
its interval so that a redraw is never more than 5% of the wait before it: a big tab
redraws less often, not more expensively.

Setup is none beyond `python` on PATH (3.9 or newer; the unlock script and the doc hook
need it already): on first start the viewer creates `analysis/.venv`, installs
`analysis/requirements.txt` (numpy, matplotlib) into it and re-executes itself from there;
later starts skip the install until the requirements file changes.
A small window says so while it installs, and `analysis/visualizer.log` has the details
when nothing appears (on Windows the viewer is two `pythonw` processes, the venv's launcher
and the interpreter it runs). `TASFW_VISUALIZER_NO_BOOTSTRAP=1` disables the install,
which is how CI runs it. On Linux the window needs `python3-tk`. Headless,
`python analysis/visualizer.py --once --csv <file> [--out <png>] [--rows N]` renders one
PNG, which CI does on `tasfw-tests/data/scattershot_sample.csv`. With
`TASFW_VISUALIZER_HEADLESS=1` in the environment the viewer tails a run with no window
instead, on the window's schedule with fixed defaults, and writes what it cost beside the
parameters file (`*.visualizer.summary.json`), which the perf suite gates
(docs/performance.md, "Tier D"); CI runs that mode too, on the sample with a finished
parameters file, and checks the summary and the image.

The CSV's columns are `Shot`, `Frame`, `Sampled` (1 for a row the sample period chose, 0 for
one the search forced) and then the stage type's own:

| Stage type | Columns after the first three |
|---|---|
| every type | `MarioX`, `MarioY`, `MarioZ`, `MarioFYaw`, `MarioFSpd`, `MarioAction`, `PlatNormX`, `PlatNormY`, `PlatNormZ` |
| `tilt-target` | plus `MarioYVel` |
| `osc-final` | plus `Phase`, `MarioYVel`, `NormalDistance` |
| `dr-oscillations` | plus `Oscillation`, `Crossing`, `Phase` |
| `dr-approach`, `dr-recover` | plus `Phase`, `MarioYVel` |

A `visualize` column or filter the stage does not emit is reported in the tab's title and the
log, and that tab stays empty.