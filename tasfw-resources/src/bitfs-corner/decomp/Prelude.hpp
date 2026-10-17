#pragma once
// What the copied game files assume of their build environment, from the decomp's headers
// that are not copied whole (the pinned revision of scripts/decomp_pin.json): the small
// definitions of include/PR/ultratypes.h, include/macros.h, src/engine/graph_node.h and
// src/game/camera.h that the copies use. The PC build's choices where the decomp has them:
// no segmented memory, so a segmented address is the pointer itself.

#include <sm64/UltraTypes.hpp>

#include <cstddef>
#include <cstdint>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define UNUSED
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))
#define BAD_RETURN(cmd) void // the decomp's AVOID_UB form: a function declared to return what it never does

#define VIRTUAL_TO_PHYSICAL(addr) ((uintptr_t)(addr))
#define PHYSICAL_TO_VIRTUAL(addr) ((uintptr_t)(addr))
#define segmented_to_virtual(addr) ((void*)(addr))

// src/engine/graph_node.h: the render flags on a graph node's `flags`.
#define GRAPH_RENDER_ACTIVE (1 << 0)
#define GRAPH_RENDER_CHILDREN_FIRST (1 << 1)
#define GRAPH_RENDER_BILLBOARD (1 << 2)
#define GRAPH_RENDER_Z_BUFFER (1 << 3)
#define GRAPH_RENDER_INVISIBLE (1 << 4)
#define GRAPH_RENDER_HAS_ANIMATION (1 << 5)

// <math.h>'s pi, which the decomp's build has and MSVC's does not without a define.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// include/types.h: an animation's flags, which the project's copy of it leaves out.
#define ANIM_FLAG_NOLOOP (1 << 0)	 // 0x01
#define ANIM_FLAG_FORWARD (1 << 1)	 // 0x02
#define ANIM_FLAG_2 (1 << 2)		 // 0x04
#define ANIM_FLAG_HOR_TRANS (1 << 3)	 // 0x08
#define ANIM_FLAG_VERT_TRANS (1 << 4) // 0x10
#define ANIM_FLAG_5 (1 << 5)		 // 0x20
#define ANIM_FLAG_6 (1 << 6)		 // 0x40
#define ANIM_FLAG_7 (1 << 7)		 // 0x80

// include/PR/os_cont.h: the controller's buttons as the game reads them.
#define CONT_A 0x8000
#define CONT_B 0x4000
#define CONT_G 0x2000
#define CONT_START 0x1000
#define CONT_UP 0x0800
#define CONT_DOWN 0x0400
#define CONT_LEFT 0x0200
#define CONT_RIGHT 0x0100
#define CONT_L 0x0020
#define CONT_R 0x0010
#define CONT_E 0x0008
#define CONT_D 0x0004
#define CONT_C 0x0002
#define CONT_F 0x0001
#define A_BUTTON CONT_A
#define B_BUTTON CONT_B
#define L_TRIG CONT_L
#define R_TRIG CONT_R
#define Z_TRIG CONT_G
#define START_BUTTON CONT_START
#define U_JPAD CONT_UP
#define L_JPAD CONT_LEFT
#define R_JPAD CONT_RIGHT
#define D_JPAD CONT_DOWN
#define U_CBUTTONS CONT_E
#define L_CBUTTONS CONT_C
#define R_CBUTTONS CONT_F
#define D_CBUTTONS CONT_D

// src/game/memory.h: with no segmented memory, an address is itself.
#define virtual_to_segmented(segment, addr) ((void*)(addr))

// src/game/rendering_graph_node.h: how an animation moves its object, set for the geo pass.
#define ANIM_TYPE_NONE 0
#define ANIM_TYPE_TRANSLATION 1
#define ANIM_TYPE_VERTICAL_TRANSLATION 2
#define ANIM_TYPE_LATERAL_TRANSLATION 3
#define ANIM_TYPE_NO_TRANSLATION 4
#define ANIM_TYPE_ROTATION 5

// src/game/obj_behaviors.c: the flags object_step returns.
#define OBJ_COL_FLAG_GROUNDED (1 << 0)
#define OBJ_COL_FLAG_HIT_WALL (1 << 1)
#define OBJ_COL_FLAG_UNDERWATER (1 << 2)
#define OBJ_COL_FLAG_NO_Y_VEL (1 << 3)
#define OBJ_COL_FLAGS_LANDED (OBJ_COL_FLAG_GROUNDED | OBJ_COL_FLAG_NO_Y_VEL)

// src/game/object_helpers.h: a path's waypoint flags, which the track platform follows.
#define WAYPOINT_FLAGS_END -1
#define WAYPOINT_FLAGS_INITIALIZED 0x8000
#define WAYPOINT_MASK_00FF 0x00FF
#define WAYPOINT_FLAGS_PLATFORM_ON_TRACK_PAUSE 3
