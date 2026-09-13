# Compilers and cross-platform compatibility

Cross-platform compatibility is a requirement, not a nicety. The code must build clean with
**MSVC, Clang and GCC**, through Ninja, on Windows and Linux. The reason is practical
experience: features that the standard and vendor documentation say are supported are
sometimes not supported in practice, and the only way to know is to build with more than
one compiler. Three compilers with three different template implementations is the minimum
that gives real confidence in C++20 code like this.

## Supported toolchains

| Toolchain | Status (2026-09-08) | How to build |
|---|---|---|
| MSVC 19.44 (VS 2022), Ninja | primary; warning-free at `/W3` | `scripts\build.ps1` (preset `msvc-<config>`) |
| clang-cl 19.1 (VS "C++ Clang tools for Windows"), Ninja | warning-free at `/W4` locally and in CI (windows-clang-cl job) | `scripts\build.ps1 -Compiler clang` (preset `clang-cl-<config>`) |
| GCC 13 on Linux (Ubuntu 24.04), Ninja | warning-free at `-Wall -Wextra` in CI (ubuntu-gcc job) and in the 24.04 container below; cannot run the game there, the Linux libsm64 `.so` needs glibc 2.43 (docs/libsm64.md) | `cmake --preset gcc-release`, then `cmake --build --preset gcc-release` |
| Clang 17 on Linux (Ubuntu 24.04), Ninja | warning-free at `-Wall -Wextra` in CI (ubuntu-clang job) and in the 24.04 container below | presets `clang-<config>` |
| GCC 15.2 on Linux (Ubuntu 26.04), Ninja | warning-free at `-Wall -Wextra` in the 26.04 container below; `LibSm64`'s `mprotect`/`SIGSEGV` save path passes the libsm64 test group against bitfs-sbb's JP `.so`, drift test max diff 0 (2026-09-08) | same presets, or the container commands below |
| Clang 21.1 on Linux (Ubuntu 26.04), Ninja | as GCC 15.2, once the tests target got `-Wno-#warnings` (pitfall below) | same |

Every build goes through a `CMakePresets.json` preset named `<compiler>-<config>`
(`msvc-release`, `clang-cl-debug`, `gcc-relwithdebinfo`, ...), with build and test presets
of the same names. The build directory is `build\<Config>` for MSVC and
`build\<Config>-<compiler>` otherwise (`build\Release-clang`, `build/Release-gcc`), so every
compiler can coexist. `build.ps1` only adds the Visual Studio environment on top of the
Windows presets, and its `-CMakeArgs` passes extra cache variables through. `-KeepGoing`
passes `-k 0` to ninja so every error in the tree is reported in one pass, which is what
you want when checking a compiler for the first time.

Warning levels are set by `cmake/Warnings.cmake` on every first-party target, found by
walking the directory tree so that the dependencies CMake fetches are left alone: `/W3` on
MSVC, `/W4` on clang-cl and `-Wall -Wextra` on GCC and Clang (CMake itself adds no `/W`
flag since 3.15, policy CMP0092). The same module puts `/WX` or `-Werror` behind
`TASFW_WARNINGS_AS_ERRORS`; every CI job passes it, and locally
`build.ps1 -CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces the CI build. It is off by
default so that a newer compiler's new warnings cannot block a fresh build. The conventions
the tree follows to stay clean at these levels: a narrowing conversion that is intended is
written as an explicit cast of the same expression (`float(a * b - c * d)`, so the
arithmetic and the result are unchanged), a parameter an override or a callback does not
use is left unnamed (`Script<TResource>* /*currentScript*/`), and dead locals and fields
are deleted rather than silenced.

## GCC and Clang locally

There is no Linux toolchain on the Windows machine, but Docker Desktop is installed. A
container with the CI toolchain, the repository mounted read-write and the build tree
inside the container (a build tree on the mount is slow):

```bash
docker run -d --name tasfw-linux -v "C:/repos/sm64-tas-scripting:/src" -w /src ubuntu:24.04 sleep infinity
docker exec tasfw-linux bash -c "apt-get update -qq && apt-get install -y -qq ninja-build g++-13 clang-17 libomp-17-dev cmake python3"
docker exec tasfw-linux cmake -S /src -B /tmp/build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DTASFW_WARNINGS_AS_ERRORS=ON
docker exec tasfw-linux cmake --build /tmp/build-gcc -- -k 0
docker exec -w /tmp/build-gcc tasfw-linux ./out/tasfw-tests
```

The same with `clang++-17` and `/tmp/build-clang`. The dependency tarballs come from the
shared `build/downloads` cache, so nothing is downloaded twice. From Git Bash, prefix
`docker` commands with `MSYS_NO_PATHCONV=1`, or the container paths in the arguments are
rewritten into Windows paths. The container is throwaway: `docker rm -f tasfw-linux`.
Docker Desktop is usually not running; start it and wait for `docker ps` to answer. A
restart of Docker Desktop stops the container (`Exited (255)`); `docker start tasfw-linux`
brings it back with its packages installed, but build trees under `/tmp` did not survive
that on 2026-09-08, so reconfigure.

That image matches CI (GCC 13, Clang 17, CMake 3.28, glibc 2.39). To run the game on Linux
the `.so` needs glibc 2.43 (docs/libsm64.md), which means a second container from Ubuntu
26.04 (GCC 15.2, Clang 21.1, CMake 4.2):

```bash
docker run -d --name tasfw-linux-26 -v "C:/repos/sm64-tas-scripting:/src" -w /src ubuntu:26.04 sleep infinity
docker exec tasfw-linux-26 bash -c "apt-get update -qq && apt-get install -y -qq ninja-build g++ clang libomp-dev cmake python3 binutils"
docker exec tasfw-linux-26 cmake -S /src -B /tmp/build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DTASFW_WARNINGS_AS_ERRORS=ON
docker exec tasfw-linux-26 cmake --build /tmp/build-gcc -- -k 0
docker exec -w /tmp/build-gcc tasfw-linux-26 ./out/dllcheck /src/res/sm64_jp_0.so /src/movies/bitfs-pyramid-jp.m64 3330
docker exec -w /tmp/build-gcc -e TASFW_LIBSM64=/src/res/sm64_jp_0.so -e TASFW_M64=/src/movies/bitfs-pyramid-jp.m64 -e TASFW_FRAME=3330 tasfw-linux-26 ./out/tasfw-tests -tc='libsm64*'
```

The same with `-DCMAKE_CXX_COMPILER=clang++` and `/tmp/build-clang`. Build with both
containers before calling a change done: the 24.04 one is what CI runs, the 26.04 one is
where the Linux game path is tested and where the newest compilers see the code first.

## Policy

1. A change is not done until it builds with MSVC, Clang and GCC with no warnings; CI
   builds all four with warnings as errors, so one warning anywhere fails the matrix. On a
   Windows machine that means MSVC and clang-cl locally plus GCC through the Linux CI job;
   on Linux it means GCC and Clang locally.
2. When one compiler rejects or miscompiles something the standard allows, do not argue with
   the compiler in the code. Write the portable form, add a one-line comment naming the
   compiler and version and pointing here, and add the case to the pitfall list below.
3. Prefer the form that every compiler accepts over the form that is merely shorter. In
   template-heavy code that usually means: explicit template parameter lists over abbreviated
   function templates, in-class definitions of constrained member templates, and forwarding
   functions over using-declarations for overloaded names.
4. `-Wall -Wextra`-class warnings from Clang are signal, not noise. Several of the Clang-only
   warnings on first contact pointed at real bugs (see "What Clang found" below).

## Known pitfalls

Each entry records the construct, the compiler that has trouble, and the workaround in use.

### MSVC: out-of-class definition of a constrained member template of a class template

Defining a constrained member function template of a class template outside the class,
matching the in-class declaration's `requires`-clause or concept-constrained parameter, is
valid C++20 but MSVC has failed to match the definition to the declaration. Reported by the
repo owner: <https://stackoverflow.com/questions/72425473/msvc-when-implementing-template-method-of-template-class-outside-of-the-class/72425702>.
Workaround in this codebase: keep the definition's template-head in the exact same form as
the declaration, and when in doubt define the member inside the class body. Watch the
`ExecuteAdhoc` / `ModifyAdhoc` / `TestAdhoc` family in `Script.hpp` and `Script.t.hpp`,
which mixes abbreviated (`AdhocScript auto`) and explicit template forms for this reason.

### MSVC: friend class template for `TopLevelScript`

MSVC did not accept the friend template declaration that would let `TopLevelScript` reach
`Script`'s private members, and where MSVC accepted a form, Visual Studio's IntelliSense
(a different front end) rejected it, or the other way round; the maintainer kept hitting one
or the other. Workaround: `ScriptFriend<TResource>`, a class of static accessors in
`Script.hpp`. Do not add new callers outside `TopLevelScript`, and treat IntelliSense as a
fourth front end when retiring it (ROADMAP 3.2).

### MSVC: dependent base members need using-declarations

Members of a dependent base (`Script<TResource>` inside `ScattershotThread<...>`) must be
brought in with `using Script<TResource>::LongLoad;` etc. or MSVC fails to find them. GCC and
Clang need `this->` or the same using-declarations; the using-declarations satisfy all three.

### MSVC accepts a missing `template` keyword on dependent member templates

`script->ExecuteStateTracker<T>(...)` where `script` has a dependent type must be written
`script->template ExecuteStateTracker<T>(...)`; otherwise GCC and Clang parse the `<` as
less-than and fail. MSVC compiles the omission. Found by the first Linux CI run in
`ScriptFriend::ExecuteStateTracker` (`Script.hpp`). Same rule for `foo.template bar<T>()`
and `typename` on dependent nested types.

### libstdc++ 13 does not declare the f-suffixed math functions in `std`

`std::sqrtf`, `std::floorf`, `std::fabsf` and friends compile with MSVC's STL but not with
libstdc++ 13 ("`sqrtf` is not a member of `std`"). Use the unsuffixed `std::sqrt(float)`
overloads, which select the same single-precision instruction, or the global `sqrtf`.

### CMake 4 rejects dependencies with `cmake_minimum_required` below 3.5

The GitHub runners ship CMake 4.x, which errors (not warns) on doctest 2.4.11's
`cmake_minimum_required(VERSION 3.0)` (nlohmann/json did the same until 3.12, which declares
`3.5...4.0`). The root `CMakeLists.txt` sets `CMAKE_POLICY_VERSION_MINIMUM 3.5` before the
first `FetchContent_MakeAvailable`; CMake 3.31 and newer honor it, older versions ignore it.
Bumping doctest past 3.5 removes the need.

### MSVC accepts using-declarations that name inaccessible overloads

`using Base::Save;` where `Save` has a public and a private overload is ill-formed
([namespace.udecl]: every named declaration must be accessible). Clang rejects it; MSVC
compiles it. Found in `tasfw-perf/src/bench_script.cpp` on the first Clang build.
Workaround: a forwarding function per public overload.

### Clang 19.1: crash on a namespace-scope `static` structured binding

```cpp
const auto static [a, b] = MakePair();   // namespace scope
void f() { a.contains(x); }              // clang-cl 19.1.5: access violation while parsing f
```

Workaround in `tasfw-core/src/core/Inputs.cpp`: a named `static const auto` plus two
`static const auto&` references. Same code, no runtime cost.

### Clang 21 with libstdc++ 15: doctest's `<ciso646>` include is a `#warning`

doctest (2.4.11, and still 2.4.12) does `#include <ciso646>` under Clang to probe for
libc++. libstdc++ 15 answers with `#warning "<ciso646> is not a standard header since
C++20"`, and Clang reports `#warning` directives (`-W#warnings`) even from system headers,
so the `-isystem` treatment from `cmake/SystemIncludes.cmake` does not help and `-Werror`
fails every test translation unit. Seen with Clang 21.1 on Ubuntu 26.04 (2026-09-08); GCC
does not take that include path and clang-cl uses the MSVC STL. Workaround:
`tasfw-tests/CMakeLists.txt` adds `-Wno-#warnings` to the tests target for Clang with the
GNU front end only. Remove it when doctest drops the include.

### CMake 3.28: no `$<CXX_COMPILER_FRONTEND_VARIANT>` generator expression

That generator expression arrived in CMake 3.30. The project's minimum is 3.22 and Ubuntu
24.04's apt CMake is 3.28.3, which fails the generate step with "Expression did not evaluate
to a known generator expression", and only when the enclosing `$<AND>` gets that far, so
the GCC configure passed while the Clang one failed (2026-09-08). Use the variable
`CMAKE_CXX_COMPILER_FRONTEND_VARIANT` in an `if()` instead, as `AddOptimizationFlags.cmake`
and `Warnings.cmake` do.

### clang-cl silently ignores GNU-style flags

`clang-cl` accepts a subset of GNU-style options (`-march=`, `-flto`, `-std:`), but anything
it does not recognise is **ignored with only a `-Wunknown-argument` warning**, not rejected.
`-ffp-contract=off` was ignored this way for a day, which left FP contraction on in every
clang-cl build while the CMake file said otherwise. Pass such flags as `/clang:<flag>`
(handled in `AddOptimizationFlags.cmake` via `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`). Treat
`-Wunknown-argument` as an error in spirit: it means a flag you rely on is not applied.

### clang-cl: Google Benchmark's `/MP`

Google Benchmark's own `CMakeLists.txt` appends `/W4 /MP` to `CMAKE_CXX_FLAGS` under any
MSVC-style compiler. clang-cl accepts `/MP` and ignores it, and its driver says so on every
source (`-Wunused-command-line-argument`, 21 warnings: 20 in `benchmark`, one in
`benchmark_main`). The flag sits in benchmark's directory scope, so
`tasfw-perf/CMakeLists.txt` silences that one warning on those two targets after
`FetchContent_MakeAvailable`. Nothing first-party is affected.

### MSVC CRT: `getenv` is deprecated

MSVC's CRT marks `std::getenv` deprecated in favour of `_dupenv_s`. cl only says so at
`/W3` (C4996), clang-cl at its default level (`-Wdeprecated-declarations`), so under
warnings as errors the clang-cl build is the one that fails. The tests and benchmarks read
their optional inputs through one `tasfw::testing::Env`
(`tasfw-testing/inc/tasfw/testing/Env.hpp`), and `tasfw-testing` defines
`_CRT_SECURE_NO_WARNINGS` for its consumers. Do not fork to `_dupenv_s`, and do not put the
define in a header: it has to precede the first CRT include of the translation unit.

### CI's newer compilers warn inside dependency headers

The first matrix run with `TASFW_WARNINGS_AS_ERRORS` on (2026-09-08) failed both Windows
jobs on headers we do not own while the same option passed locally: the runner's MSVC
warned at doctest 2.4.11's forward declarations of `std` types (`doctest.h` line 539; the
runner's warning text is not in the annotations, only its C2220), and its clang-cl, newer
than the 19.1 in Visual Studio here, deprecated nlohmann/json 3.11.2's spaced
`operator "" _json` (`-Wdeprecated-literal-operator`; json 3.12 writes `operator ""_json`,
ROADMAP 1.8). The general answer is `cmake/SystemIncludes.cmake`: every fetched dependency's
interface include directories are re-declared as system directories, so its headers get
`-isystem` on GCC and Clang, `-imsvc` on clang-cl and `-external:I` with `-external:W0` on
MSVC, and warnings from them never reach `-Werror`. Apply `tasfw_system_includes` to any
dependency added later.

### clang-cl: a GNU-style `-Wall` is MSVC's `/Wall`

clang-cl accepts most GNU-style flags, but `-Wall` collides with the CL option `/Wall`
(the same option with the other prefix), which clang-cl maps to `-Weverything`: 117 unique
warnings on this tree, from "`long long` is incompatible with C++98" to old-style casts.
clang-cl's spelling of GNU `-Wall -Wextra` is `/W4` (`/W1` to `/W3` are `-Wall`). The
`-Wextra` half has no CL homonym and would work on its own; `cmake/Warnings.cmake` uses
`/W4`. Found 2026-09-08 (ROADMAP 1.7).

### GCC 13: `-Wdangling-reference` on a reference returned past a temporary

`const json& RequireObject(const json& parent, const char* key, const std::string& where)`
returns a reference into `parent`, but a call with a string literal binds a temporary
`std::string` to `where`, and GCC 13's heuristic assumes the returned reference might refer
to that temporary. The warning is in `-Wall` and there was no real bug. The fix that keeps
the function honest is to take `where` by value as a `std::string_view`, which the heuristic
does not consider; GCC 14 narrowed the check, but 13 is the CI compiler.

### `static` function declarations in a header

`sm64/Surface.hpp` declared the decomp's file-local helpers (`get_floor_class`, ...) as
`static` next to the public ones. A `static` declaration in a header is a separate unused
function in every translation unit that includes it and does not define it, which clang
reports (`-Wunused-function`) per translation unit. The declarations moved into
`Surface.cpp`, the only place that defines and calls them. The struct layouts in that
directory are untouched (AGENTS.md hard rule 2 is about layouts).

### `-Wunused-parameter` at `-Wextra`

The no-op default virtuals on `Script` (`TrackState`, `EraseTrackedStates`, ...), the
generator, comparator and terminator lambdas passed to the compare helpers, and the
decomp's `get_object_vertices` all have parameters they do not use. The convention is to
leave the parameter unnamed with the name in a comment, `Type /*name*/`, never
`(void)name` or a `[[maybe_unused]]` that hides a parameter that should be used.

### `-Wtype-limits` on `uint8_t < 0`

`BinaryStateBin` checked `bitCursor < 0 || bitCursor >= nBytes * 8` on a `uint8_t`; the
first half is always false and GCC says so. The check is now only the upper bound.

### Clang: `-Winconsistent-missing-override`

Classes that mark some overriding functions `override` and not others warn on Clang. The
scattershot script classes in `tasfw-scripts` and the bruteforcer do this. Fix is mechanical:
mark every override.

### MSVC: a hot accessor stops inlining when its slow path grows

`LevelStack::operator[]` is called on every bookkeeping access in `Script`. When its body
contained the growth loop (an `std::optional::emplace` and a `vector::push_back`), MSVC
19.44 with LTO stopped inlining it and every access became a call: a depth-16 `LongLoad`
went from 4.1 to 4.9 us and an empty `ExecuteAdhoc` from 262 to 295 ns, while clang-cl
inlined it regardless and showed no change. Keeping the inline body to a compare and a
pointer select, with the loop in a separate `Grow()`, restored inlining and gained 8 to 15%
over the previous code on MSVC. For anything called per frame, keep the inline body tiny and
move the rare path out; and measure on both compilers, because only one of them will tell you.

## What GCC found on first contact (2026-09-07)

`-Wmissing-requires` on every concept in `ScriptCompareHelper.hpp` (`ScriptParamsGenerator`,
`ScriptComparator`, `ScriptTerminator`, `AdhocScriptComparator`, `AdhocScriptTerminator`).
They are written as

```cpp
concept ScriptTerminator = requires { std::same_as<std::invoke_result_t<F, ...>, bool>; };
```

which only checks that the expression *is well-formed*, never that it is *true*: a
requirement-body expression is not evaluated. Every one of these concepts is satisfied by
anything, so the `Compare` family is effectively unconstrained and a wrong comparator fails
deep inside the instantiation instead of at the call. The fix is `requires
std::same_as<...>` (a nested requirement) or dropping the `requires` block for a plain
constraint expression. Not changed yet because callers may currently rely on the leniency;
ROADMAP 3.10. Until then `add_optimization_flags` passes `-Wno-missing-requires` to each
target it is called on (GCC only; Clang and MSVC do not know the flag, and the probe fails
there). `tasfw-scripts-scattershot-bitfs-dr` never had that call, which is why the GCC job
kept annotating this warning, and why that library was built without LTO, until 2026-09-08.

## What Clang found on first contact (2026-09-07)

Warnings MSVC did not emit, all fixed under ROADMAP 1.5 on 2026-09-07 and kept here as
the record of what a second front end was worth:

- `-Wempty-body`: `if (...);` with the body on the next line, in
  `Scattershot_BitfsDrApproach.hpp` (line 528) and `Scattershot_BitfsDrRecover.hpp` (line 592).
  Both compute a rejection condition and discard it. These are in the currently disabled
  stages; the intended action is almost certainly `return false;`.
- `-Wreturn-type`: the `TurnAround` lambda in `Scattershot_BitfsDr.cpp` (line 695) can fall
  off the end. MSVC reports the same as C4715.
- `-Wunused-value`: three `ModifyAdhoc(...).executed;` statements whose result is dropped.
- `-Wswitch`: unhandled enum values in phase switches (may be intentional; make it explicit
  with `default:`).
- `-Wmain`: `main` returns `false`.
- Not a warning, but seen while reading the warning sites: `TurnUphill_1f` in
  `Scattershot_BitfsDr.cpp` returns `action == ACT_FINISH_TURNING_AROUND && action == ACT_WALKING`,
  which is always false. Its siblings use `||`.

## Floating-point determinism across compilers

The framework re-implements parts of the game's physics in C++ (`PyramidUpdate`, the
downhill-angle probes, the fixed-point tilt targeting) and compares its floats with the
game DLL's. Those floats must be bit-identical, on every compiler. The rules:

- **No FMA contraction.** With an AVX2 or newer target, GCC defaults to
  `-ffp-contract=fast` and Clang to `-ffp-contract=on`, both of which fuse `a*b+c` into an
  FMA with a single rounding. MSVC does not contract under its default `/fp:precise`.
  `cmake/AddOptimizationFlags.cmake` passes `-ffp-contract=off` to GCC/Clang and
  `/fp:precise` to MSVC on every target. Do not add `-ffast-math`, `/fp:fast`, or
  `/fp:contract` anywhere.
- **The game DLL is the reference.** It was compiled by wafel's toolchain with its own FP
  settings; matching it is an empirical question. The drift test in `tasfw-tests`
  (`libsm64: PyramidUpdate reproduces the DLL's pyramid normal bit-for-bit`) runs
  `PyramidUpdate` against `LibSm64` for 240 frames and requires identical normals. It passes
  with max |diff| = 0 on both MSVC and clang-cl with the flags above; it is the test that
  catches any of this going wrong.
- x87 is not a concern on x86-64 (SSE2 is the baseline), but 32-bit builds are unsupported
  for this reason as well as the DLL's.

## CMake pitfall: unquoted variables in `if()`

`if(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")` expands to `if(MSVC STREQUAL "MSVC")` and
CMake dereferences the bare token `MSVC` as the variable of that name, which is `1` on MSVC.
The comparison was always false, so MSVC builds never ran the `/arch` probe and were built
without any `/arch` flag while clang-cl got `-march=native`. Fixed 2026-09-07; always write
`if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")`.

## Toolchain notes

- OpenMP: MSVC provides the 2.0 runtime via `-openmp`; clang-cl uses `-Xclang -fopenmp`
  (OpenMP 5.1) and links `libomp`. Both are found by CMake's `FindOpenMP`. Behavioral
  differences between the two runtimes (scheduling, barrier cost) are a Tier D concern.
- LTO: MSVC `/GL` + `/LTCG`; clang-cl `-flto=thin` with `lld-link`. Both are enabled by
  `cmake/AddOptimizationFlags.cmake` through CMake's IPO support.
- `-march=native` is accepted by clang-cl and detected as such; MSVC gets an `/arch` flag
  from a configure-time probe.
- `__rdtsc` comes from `<intrin.h>` on MSVC and `<x86intrin.h>` elsewhere (`Resource.t.hpp`).
