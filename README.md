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
CMake 3.22+ and a C++20 compiler with OpenMP. Dependencies (nlohmann/json, range-v3) are
downloaded by CMake's FetchContent; no vcpkg needed.

**Windows (supported path)**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # Debug
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release
```

The script finds Visual Studio 2022 with vswhere, imports the x64 developer environment and
uses the cmake/ninja bundled with Visual Studio if none are on PATH. Output goes to
`build\<Config>\out\bitfs-turn.exe`. Opening the folder in Visual Studio also works through
its CMake integration and the presets in `CMakePresets.json`.

Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1`. Benchmarks:
`scripts\perf.ps1`. Both accept `-Compiler clang` for the clang-cl build.

**Linux / macOS (untested recently)**

```bash
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

# Runtime inputs
The executable needs files that are not in git (see [docs/libsm64.md](docs/libsm64.md)):

- `res/sm64_jp_0.dll` through `res/sm64_jp_23.dll`, one copy of the libsm64 DLL per thread.
- The source `.m64` movies referenced by `config.json` and `main.cpp`.

Running `bitfs-turn.exe` starts the full BitFS pipeline: many threads, hours of runtime,
thousands of exported `.m64` files. It is not a smoke test.

# Configuration system
_Written by [@jgcodes2020](https://github.com/jgcodes2020)_

The configuration system uses a JSON file, and enables easy configuration of mostly non-changing parameters. When built with a build type other than `Debug`, the built executable always takes a config file as its first argument. When built for `Debug`, it will default to reading a file named `config.json` placed in the same directory as itself.

The current schema is as follows:
```json
{
	"libsm64": "Full path to libsm64",
	"m64_file": "Full path to .m64 file being used"
}
```