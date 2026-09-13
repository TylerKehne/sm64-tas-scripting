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
| `ObjectFields.hpp` | `include/object_fields.h` | Refresh 13 | +26 -20 | `#pragma once` and the include; clang-format's comment wrapping and `+ 1` to `+1` |
| `Sm64.hpp` | `include/sm64.h` | Refresh 13 | +248 -132 | `#pragma once`; the includes; clang-format's wrapping of the action flag lists |
| `SurfaceTerrains.hpp` | `include/surface_terrains.h` | Refresh 15, `1372ae1` (2021-10-14) | +1 -0 | `#pragma once`; copied later than the rest |
| `Camera.hpp` | `src/game/camera.h` | Refresh 13 | +20 -674 | `struct Camera` alone, out of a 600-line header |
| `UltraTypes.hpp` | `include/PR/ultratypes.h` | Refresh 13 | +3 -18 | the typedefs alone, on `<stdint.h>` |
| `Trig.hpp` | `include/trig_tables.inc.c` | Refresh 13 | +942 -150 | the three tables as `constexpr std::array`, `gCosineTable` written out; the values wrap at other columns, hence the size |
| `Surface.hpp` | `include/surface_terrains.h` | Refresh 13 | +20 -180 | a selection of the surface types with `SURFACE_CLASS_*`, and the declarations of the reimplemented surface functions; mostly the framework's own |

The pins were found by matching each copy against every "Refresh" commit on upstream
master (the decomp squashes its development into those): Refresh 13 fits every file best
except `SurfaceTerrains.hpp`, which is Refresh 15's. The pinned DLL (built 2021-06-17) is
from between Refresh 13 and 14: it lacks Refresh 14's `oUpVel` and `animList`, and so do
the headers, which is why the 42 names the DLL's layout table has and the headers lack are
all later additions (libsm64.md, "Struct layouts").

The reimplemented files are cited in the same JSON under `derived`, all at Refresh 13 by
assumption (their comments name a `tasfw-decomp` library at commit `69792ad` that no longer
exists anywhere reachable, so the revision is inferred from the headers'):

| File | Upstream functions |
|---|---|
| `tasfw-core/src/decomp/Math.cpp` | `mtxf_align_terrain_normal`, `vec3f_cross`, `vec3f_normalize` (`src/engine/math_util.c`); `linear_mtxf_mul_vec3f` (`src/game/object_helpers.c`) |
| `tasfw-core/src/decomp/Surface.cpp` | `find_floor` (`src/engine/surface_collision.c`); `load_object_surfaces`, `read_surface_data`, `get_object_vertices`, `surface_has_force`, `transform_object_vertices` as `transform_surfaces` (`src/engine/surface_load.c`); `mario_floor_is_slope` and `mario_get_floor_class` as `floor_is_slope` and `get_floor_class` (`src/game/mario.c`) |
| `tasfw-core/src/decomp/Pyramid.cpp` | `bhv_tilting_inverted_pyramid_loop`, `create_transform_from_normals`, `approach_by_increment` (`src/game/behaviors/tilting_inverted_pyramid.inc.c`); `simulate_platform_tilt` is the framework's own composition of them |
| `tasfw-resources/src/PyramidUpdate.cpp` | the same tilt loop, surface loading, `find_floor` and floor class on its own surface type |

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

Moving to a newer decomp revision is not the same as moving to a newer DLL. The DLL's fork
tracks the decomp's development line ahead of master (libsm64.md, "Reproducing the DLL
from source"), so a DLL can export names that no upstream commit had yet, which is what
`LibSm64SymbolAliases` bridges (libsm64.md, "Renamed symbols"). The DWARF table is the
check on offsets; the pin is the check on where the text came from.
