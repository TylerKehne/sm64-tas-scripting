#pragma once
#include <sm64/Types.hpp>

// The surface pool's node and the spatial partition, from the decomp's src/engine/surface_load.h,
// and the level's bounds from src/engine/surface_collision.h (scripts/decomp_pin.json).

// Range level area is 16384x16384 (-8192 to +8192 in x and z)
#define LEVEL_BOUNDARY_MAX 0x2000 // 8192
#define CELL_SIZE (1 << 10)	  // 0x400

#define CELL_HEIGHT_LIMIT 20000
#define FLOOR_LOWER_LIMIT -11000
#define FLOOR_LOWER_LIMIT_MISC (FLOOR_LOWER_LIMIT + 1000)

#define NUM_CELLS (2 * LEVEL_BOUNDARY_MAX / CELL_SIZE)
#define NUM_CELLS_INDEX (NUM_CELLS - 1)

struct SurfaceNode
{
	struct SurfaceNode* next;
	struct Surface* surface;
};

enum
{
	SPATIAL_PARTITION_FLOORS,
	SPATIAL_PARTITION_CEILS,
	SPATIAL_PARTITION_WALLS
};

typedef struct SurfaceNode SpatialPartitionCell[3];
