# The decompilation and this repository

What of the SM64 decompilation ([n64decomp/sm64](https://github.com/n64decomp/sm64), CC0)
is in this repository, at which revision, and how that is checked. Three strands, each
anchored differently:

1. **The game DLL** is built from a fork of the decomp that is not public
   ([docs/libsm64.md](libsm64.md), "Reproducing the DLL from source"). Its anchor is its
   own DWARF debug info (libsm64.md, "Struct layouts").
2. **The copied headers** under `tasfw-core/inc/sm64/` are hand-copied decomp files. Their
   anchor is `scripts/decomp_pin.json`: the upstream commit and path each came from, and the
   hash of the edits made while copying. `scripts/decomp_diff.py` verifies it.
3. **The reimplemented functions** (`tasfw-core/src/decomp/`, `PyramidUpdate`) are C++
   rewrites of decomp functions on the copied structs. The same file cites which functions
   at which revision; nothing diffs a rewrite. `PyramidUpdate`'s behavior is checked against
   the DLL by the drift test (docs/performance.md, ROADMAP 3.3); `src/decomp/` is not
   (AGENTS.md, "Known problems").

Copying is the pattern the repository has, not the one it wants: the headers were copied at
different times, the physics is reimplemented twice, and both checks here are after the
fact. It stays because it is verified. What should replace it is a Phase 5 question
(ROADMAP.md, "One declared source for a game's vocabulary").

## The pin

`scripts/decomp_pin.json` lists every copied file with its upstream path and commit, a note
on what changed in the copy, and the residual: the sha256 of the normalized diff from the
upstream file to the copy (CRLF and backslash continuations joined, whitespace collapsed,
empty lines dropped, so that clang-format's wrapping does not count). The residual is the
editing done while copying, and it is what the check holds fixed.

| Copy | Upstream file | Commit | Residual | What the residual is |
|---|---|---|---|---|
| `Types.hpp` | `include/types.h` | Refresh 13, `6d87c42` (2020-12-03) | +61 -127 | `#pragma once` and `sm64/UltraTypes.hpp` for the decomp's includes; `IS_64_BIT` resolved to 1 (the `ptrData` union); `Controller`, `SpTask` and the other structs the framework never reads dropped |
| `ObjectFields.hpp` | `include/object_fields.h` | Refresh 13 | +26 -32 | `#pragma once` and the include; `IS_64_BIT` resolved to 1 (the pointer fields through `ptrData`, as `Types.hpp` has them); clang-format's comment wrapping and `+ 1` to `+1` |
| `Sm64.hpp` | `include/sm64.h` | Refresh 13 | +248 -132 | `#pragma once`; the includes; clang-format's wrapping of the action flag lists |
| `SurfaceTerrains.hpp` | `include/surface_terrains.h` | Refresh 15, `1372ae1` (2021-10-14) | +1 -0 | `#pragma once`; copied later than the rest |
| `Camera.hpp` | `src/game/camera.h` | Refresh 13 | +40 -677 | `struct Camera` and `struct PlayerCameraState` alone, out of a 600-line header |
| `UltraTypes.hpp` | `include/PR/ultratypes.h` | Refresh 13 | +3 -18 | the typedefs alone, on `<stdint.h>` |
| `Trig.hpp` | `include/trig_tables.inc.c` | Refresh 13 | +942 -150 | the three tables as `constexpr std::array`, `gCosineTable` written out; the values wrap at other columns, hence the size |
| `Surface.hpp` | `include/surface_terrains.h` | Refresh 13 | +20 -180 | a selection of the surface types with `SURFACE_CLASS_*`, and the declarations of the reimplemented surface functions; mostly the framework's own |
| `SurfaceLoad.hpp` | `src/engine/surface_load.h` | Refresh 13 | +12 -22 | `SurfaceNode`, the partition's enum and `SpatialPartitionCell` with `NUM_CELLS`, plus the level bounds and limits from `src/engine/surface_collision.h` they rest on; the externs and prototypes dropped; for the BitFS corner resource (ROADMAP 4.13) |
| `Controller.hpp` | `include/types.h` | Refresh 13 | +8 -309 | `struct Controller` alone, its two OS pointers on forward declarations; for the BitFS corner resource |
| `Area.hpp` | `src/game/area.h` | Refresh 13 | +23 -127 | `struct Area` alone, what it points at beside the camera on forward declarations; for the BitFS corner resource |

The pins were found by matching each copy against every "Refresh" commit on upstream
master (the decomp squashes its development into those): Refresh 13 fits every file best
except `SurfaceTerrains.hpp`, which is Refresh 15's. The pinned DLL (built 2021-06-17) is
from between Refresh 13 and 14: it lacks Refresh 14's `oUpVel` and `animList`, and so do
the headers, which is why the 42 names the DLL's layout table has and the headers lack are
all later additions (libsm64.md, "Struct layouts").

Nothing reimplements the game outside the corner's copies any more. `tasfw-core/src/decomp`
(`Math.cpp`, `Surface.cpp`, `Pyramid.cpp`) and `PyramidUpdate` re-wrote the pyramid's tilt,
the surface loading and the floor rules on their own types, for the look-ahead the
oscillation scripts take; since 2026-10-12 that look-ahead is a frame of a child script on
the resource the script runs on (`GetMinimumDownhillWalkingAngle`), the corner being cheap
enough, and both were removed (ROADMAP 4.13). The pin's `derived` list, which cited them by
upstream function, is empty and stays for the next file of that kind.

## The BitFS corner's copies

`BitfsCorner` (ROADMAP 4.13) runs the game's frame for the pyramid corner from the decomp's
own text: 48 files under `tasfw-resources/src/bitfs-corner/decomp/`, the `.inc` ones the
functions of a `src/` file the frame reaches, included into the body of one class
(`BitfsCornerGame`, `src/bitfs-corner/BitfsCornerGame.hpp`) so that they become its members
and the globals they define its data, and the `.h` ones the headers they compile against
with their includes dropped. Each is pinned in `scripts/decomp_pin.json` like the headers
above, all at Refresh 13; the residual is what was left out of the file (a `.inc` keeps only
what the corner reaches, so `-1483` lines on `object_helpers.inc` is the rest of the file)
plus the edits made while copying, which are of a few kinds and are named where they are
made: `static` dropped from functions (a member cannot be) and `register` from locals; the
decomp's own `AVOID_UB` forms (`BitfsCornerGame.hpp` defines `AVOID_UB` before any header,
which is also what gives `sm64.h`'s `GET_HIGH_S16_OF_32` its shift form rather than the
big-endian pointer trick, wrong on x64); `return dest` for the vector copies' `return
&dest`; a C conversion from `void *` made explicit; a function-pointer parameter made a
pointer to member; the surface allocator pointed at the state's dynamic pool (the static
surfaces are the level's); the interpreter's command table replaced by a switch over the
commands the corner's scripts use, and its `CALL_NATIVE` resolved by position; the
animation load replaced by a pointer into the level's resolved copies; the particle loop
of `bhv_mario_update` and the handler table of `mario_process_interactions` reduced to the
corner's one case; in `camera.inc` (the C-up mode and what it reaches, the rest of
`camera.c` held), `set_camera_mode`'s table of mode transitions replaced by a switch over
the corner's two modes, the JP build's `#ifndef` resolved, and `radial_camera_input`'s
unread, uninitialized return value initialized; in `math_util.inc`, `mtxf_copy`'s words
copied through `f32` pointers rather than `u32` (a float read through an integer pointer is
undefined in C++, and GCC 14 took `mtxf_mul`'s result as never written); in
`mario_actions_airborne.inc`, two locals the decomp sets only for the action arguments the
game passes (`act_freefall`'s animation, `act_crazy_box_bounce`'s least speed) started at
their first case's value (docs/compilers.md); in `ingame_menu.inc` (the pause menu,
ROADMAP 4.22) the JP build's `#ifdef` resolved, the EU forms dropped, and
`render_pause_course_options` cut to its cursor and its call of the camera options. The three structs the C-up mode's state
is made of (`LinearTransitionPoint`, `ModeTransitionInfo`, `CameraStoredInfo`) live in
`sm64/Camera.hpp` beside `Camera` and `PlayerCameraState`, and the corner's `camera.h`
copy leaves them to it. `Prelude.hpp` beside them is the framework's own: the few definitions
of `ultratypes.h`, `macros.h`, `graph_node.h`, `os_cont.h` and the like that the copies
assume. The copies come through a system include path (`tasfw-resources/CMakeLists.txt`), so
their warnings are the decomp's; what link-time code generation still reports (a missing
return, an address of a parameter) is edited in the copy and recorded in its residual.

## Checking and moving the pin

    python scripts\decomp_diff.py                       # fetches the pinned files from GitHub
    python scripts\decomp_diff.py --checkout C:\repos\sm64   # or reads them from a local clone
    python scripts\decomp_diff.py --show                # prints every residual diff

Every copy prints `ok` with its residual size, or `CHANGED` with the current diff and exit
code 1: a copy was edited without moving its pin, or a pin was moved without re-checking
the copy. CI runs it on one Linux job (`build.yml`), so a change to a copied header has to
come with a change to the pin. After a deliberate edit, or after moving a pin (say, to the
Refresh whose names a newer DLL exports), read `--show`, then `--update` records the
residuals as they are now; the diff of `decomp_pin.json` in the pull request shows what
was accepted.

`tasfw-scripts/inc/Sm64ActionNames.hpp`, the Mario actions' names the stages' CSV rows carry
beside the action's hex (README.md, "The viewer"), is generated from `Sm64.hpp` by
`scripts/action_names.py`; after `Sm64.hpp` moves, run it again. CI's `--check` on the same
job fails while the header is stale.

Moving to a newer decomp revision is not the same as moving to a newer DLL. The DLL's fork
tracks the decomp's development line ahead of master (libsm64.md, "Reproducing the DLL
from source"), so a DLL can export names that no upstream commit had yet, which is what
`LibSm64SymbolAliases` bridges (libsm64.md, "Renamed symbols"). The DWARF table is the
check on offsets; the pin is the check on where the text came from.
