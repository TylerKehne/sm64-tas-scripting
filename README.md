# sm64-tas-scripting
A C++20 framework for scripting and brute-forcing Super Mario 64 TAS inputs. The game runs
inside a native x64 DLL (wafel's libsm64); scripts drive it with savestates and frame
advances, and a multithreaded "scattershot" search explores input space on top of that.
The current driver is a squish-cancel setup brute forcer for the BitFS tilting pyramid.

Still very much a work in progress. Start with:

- [AGENTS.md](AGENTS.md): rules and conventions for anyone (human or AI) changing the code.
- [ARCHITECTURE.md](ARCHITECTURE.md): how scripts, savestates and scattershot fit together.
- [ROADMAP.md](ROADMAP.md): what is planned and in what order.
- [docs/libsm64.md](docs/libsm64.md): where the game DLL comes from and what depends on it.
- [docs/performance.md](docs/performance.md): performance is a correctness requirement; how it is measured and gated.

These files are kept current by design: a Claude Code hook in [.claude/](.claude/) reviews
them after every change (see AGENTS.md, "Documentation must match the repository").

# Building instructions
CMake 3.22+ and a C++20 compiler with OpenMP. The dependencies (nlohmann/json; doctest and
Google Benchmark for the tests and benchmarks) are downloaded by CMake, verified by hash and
cached in `build\downloads`, so later builds work offline; no vcpkg needed.

**Windows (supported path)**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # Debug
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release
```

The script finds Visual Studio 2022 with vswhere, imports the x64 developer environment,
uses the cmake/ninja bundled with Visual Studio if none are on PATH, and configures from the
`CMakePresets.json` preset for the compiler and config (`msvc-release`, or `clang-cl-release`
with `-Compiler clang`). Output goes to `build\<Config>\out\bitfs-turn.exe`
(`build\<Config>-clang` for clang-cl). Visual Studio and VS Code pick up the same presets
when you open the folder, and `-CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces the
CI build.

Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1`. Benchmarks:
`scripts\perf.ps1`. Both accept `-Compiler clang` for the clang-cl build.

**Linux** (builds and passes the DLL-free tests in CI with GCC 13 and Clang 17; the game
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
  (on Linux `.so` copies and `"dllPattern": "sm64_jp_{}.so"`). Unlock them from a ROM as
  docs/libsm64.md describes; never commit a ROM or an unlocked binary.
- The source `.m64` movies referenced by `config.json`.

# Running the pipeline

```
bitfs-turn [--config <file>] [--stage <name>] [--list] [--dry-run]
```

`bitfs-turn.exe` runs the BitFS squish-cancel pipeline described by its config: every
stage in order, or one stage with `--stage <name>`. Without `--config` it reads the
`config.json` next to the executable, which the build writes from
[tasfw-bruteforcers/bitfs-turnaround/config.json](tasfw-bruteforcers/bitfs-turnaround/config.json).
`--list` prints the stage types and the configured stages; `--dry-run` resolves every path,
checks the files exist, loads one DLL and prints its layout report. Both are safe. A full
run is not a smoke test: 16 threads, hours, and thousands of exported `.m64` files.

Each stage writes its solutions to `<outputDirectory>/solutions/<stage>.json` (input diffs
plus named metrics). A stage run alone reads its input from the file its input stage wrote
last time, so a long pipeline can be advanced one stage at a time. Stages with
`"export": true` also write one movie per solution under `<outputDirectory>/m64/<stage>/`.
Scattershot CSVs and the `error.m64` dump go to `<outputDirectory>` as well. The stage
summary prints wall time and the frame advances, saves and loads summed over threads, which
are the fixed-workload numbers a performance change has to report (AGENTS.md, hard rule 8).

# Configuration

One JSON file. Relative paths resolve against the file's own directory, unless the file has
`"baseDirectory"` (the build sets it in the copy next to the executable). Keys starting with
`_` are comments; any other unknown key is an error, with the location in the message.

```json
{
	"resources": { "dllDirectory": "../../res", "dllPattern": "sm64_jp_{}.dll", "threads": 16, "lightweight": true },
	"m64": "../../res/source.m64",
	"outputDirectory": "../../analysis",
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
			"export": true
		}
	]
}
```

- `resources`: the DLL directory and file pattern (`{}` becomes the thread index; one copy
  per thread), the thread count, whether saves are lightweight, and `costModel` (default
  true; false disables the replay-versus-load cost model so a run is timing-independent,
  for diagnosis).
- `scattershot`: defaults for every stage, in the field names of `Configuration`
  (`pelletMaxScripts`, `pelletMaxFrameDistance`, `maxBlocks`, `maxShots`, `pelletsPerShot`,
  `shotsPerUpdate`, `startFromRootEveryNShots`, `maxConsecutiveFailedPellets`,
  `maxSolutions`, `seed`, `fitnessTieGoesToNewBlock`, `deterministic`, `csvSamplePeriod`).
  A stage's own `scattershot` object overrides them.
- A stage has a unique `name`, a `type` (`bitfs-turn --list`), a `startFrame`, optionally its
  own `m64`, an `input` (the name of an earlier stage), `args` for the stage type, a `select`
  applied to its output, and `export`.
- Numeric `args` accept a number or `"input:<metric>"`, which takes that metric from the
  first solution of the input stage (for example the equilibrium frame the tilt search found).
  The argument names of each type are checked in `Stages.cpp`; an unknown one is an error.

The committed config reproduces the pipeline exactly as it was last run.