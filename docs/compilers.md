# Compilers and cross-platform compatibility

Cross-platform compatibility is a requirement, not a nicety. The code must build clean with
**MSVC, Clang and GCC**, through Ninja, on Windows and Linux. The reason is practical
experience: features that the standard and vendor documentation say are supported are
sometimes not supported in practice, and the only way to know is to build with more than
one compiler. Three compilers with three different template implementations is the minimum
that gives real confidence in C++20 code like this.

## Supported toolchains

| Toolchain | Status (2026-09-07) | How to build |
|---|---|---|
| MSVC 19.44 (VS 2022), Ninja | primary; builds clean, 29 warnings | `scripts\build.ps1` |
| clang-cl 19.1 (VS "C++ Clang tools for Windows"), Ninja | builds after the workarounds below; see status in ROADMAP 1.6 | `scripts\build.ps1 -Compiler clang` |
| GCC 13+ on Linux, Ninja | required; status in ROADMAP 1.6; `LibSm64` has a separate `mprotect` save path | `cmake -G Ninja` or `scripts/build.sh` once it exists |
| Clang 17+ on Linux, Ninja | required; same as GCC | same |

Build directories are `build\<Config>` for MSVC and `build\<Config>-clang` for clang-cl,
so both can coexist. `-KeepGoing` passes `-k 0` to ninja so every error in the tree is
reported in one pass, which is what you want when checking a compiler for the first time.

## Policy

1. A change is not done until it builds with MSVC, Clang and GCC with no new warnings. On
   a Windows machine that means MSVC and clang-cl locally plus GCC through the Linux CI job
   (or a container, see below); on Linux it means GCC and Clang locally.
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
`Script`'s private members. Workaround: `ScriptFriend<TResource>`, a class of static
accessors in `Script.hpp`. Do not add new callers outside `TopLevelScript`.

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

The GitHub runners ship CMake 4.4, which errors (not warns) on nlohmann/json 3.11.2's
`cmake_minimum_required(VERSION 3.1)`. The root `CMakeLists.txt` sets
`CMAKE_POLICY_VERSION_MINIMUM 3.5` before `FetchContent_MakeAvailable`; CMake 3.31 and
newer honor it, older versions ignore it. Bumping the dependency removes the need.

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

### clang-cl silently ignores GNU-style flags

`clang-cl` accepts a subset of GNU-style options (`-march=`, `-flto`, `-std:`), but anything
it does not recognise is **ignored with only a `-Wunknown-argument` warning**, not rejected.
`-ffp-contract=off` was ignored this way for a day, which left FP contraction on in every
clang-cl build while the CMake file said otherwise. Pass such flags as `/clang:<flag>`
(handled in `AddOptimizationFlags.cmake` via `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`). Treat
`-Wunknown-argument` as an error in spirit: it means a flag you rely on is not applied.

### Clang: `-Winconsistent-missing-override`

Classes that mark some overriding functions `override` and not others warn on Clang. The
scattershot script classes in `tasfw-scripts` and the bruteforcer do this. Fix is mechanical:
mark every override.

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
ROADMAP 3.10.

## What Clang found on first contact (2026-09-07)

Warnings MSVC did not emit, worth acting on (ROADMAP 1.5):

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
  settings; matching it is an empirical question. ROADMAP 3.3 adds a drift test that runs
  `PyramidUpdate` against `LibSm64` for a few hundred frames and requires identical normals,
  which is the test that catches any of this going wrong.
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
