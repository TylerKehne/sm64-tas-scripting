#include <cmath>
#include "surface.hpp"

void find_floor(Vec3f* marioPos, struct Surface* surfaces, int surfaceCount, struct Surface** floor) {
    int i;
    struct Surface* surf;
    int x1, z1, x2, z2, x3, z3;
    float nx, ny, nz;
    float oo;
    float height;

    *floor = NULL;

    // Iterate through the list of floors until there are no more floors.
    for (i = 0; i < surfaceCount; i++) {
        surf = surfaces;
        surfaces++;

        if (surf->normal.y <= 0.1) continue;

        x1 = surf->vertex1[0];
        z1 = surf->vertex1[2];
        x2 = surf->vertex2[0];
        z2 = surf->vertex2[2];

        // Check that the point is within the triangle bounds.
        if ((z1 - (*marioPos)[2]) * (x2 - x1) - (x1 - (*marioPos)[0]) * (z2 - z1) < 0) {
            continue;
        }

        // To slightly save on computation time, set this later.
        x3 = surf->vertex3[0];
        z3 = surf->vertex3[2];

        if ((z2 - (*marioPos)[2]) * (x3 - x2) - (x2 - (*marioPos)[0]) * (z3 - z2) < 0) {
            continue;
        }
        if ((z3 - (*marioPos)[2]) * (x1 - x3) - (x3 - (*marioPos)[0]) * (z1 - z3) < 0) {
            continue;
        }

        nx = surf->normal.x;
        ny = surf->normal.y;
        nz = surf->normal.z;
        oo = surf->originOffset;

        // Find the height of the floor at a given location.
        height = -((*marioPos)[0] * nx + nz * (*marioPos)[2] + oo) / ny;
        // Checks for floor interaction with a 78 unit buffer.
        if ((*marioPos)[1] - (height + -78.0f) < 0.0f) {
            continue;
        }

        *floor = surf;
        break;
    }
}

bool floor_is_slope(struct Surface* floor) {
    float normY;

    switch (get_floor_class(floor)) {
    case SURFACE_VERY_SLIPPERY:
        normY = 0.9961947f; // ~cos(5 deg)
        break;

    case SURFACE_SLIPPERY:
        normY = 0.9848077f; // ~cos(10 deg)
        break;

    default:
        normY = 0.9659258f; // ~cos(15 deg)
        break;

    case SURFACE_NOT_SLIPPERY:
        normY = 0.9396926f; // ~cos(20 deg)
        break;
    }

    return floor->normal.y <= normY;
}

static short get_floor_class(struct Surface* floor) {
    short floorClass = SURFACE_CLASS_DEFAULT;

    if (floor != NULL) {
        switch (floor->type) {
        case SURFACE_NOT_SLIPPERY:
        case SURFACE_HARD_NOT_SLIPPERY:
        case SURFACE_SWITCH:
            floorClass = SURFACE_CLASS_NOT_SLIPPERY;
            break;

        case SURFACE_SLIPPERY:
        case SURFACE_NOISE_SLIPPERY:
        case SURFACE_HARD_SLIPPERY:
        case SURFACE_NO_CAM_COL_SLIPPERY:
            floorClass = SURFACE_CLASS_SLIPPERY;
            break;

        case SURFACE_VERY_SLIPPERY:
        case SURFACE_ICE:
        case SURFACE_HARD_VERY_SLIPPERY:
        case SURFACE_NOISE_VERY_SLIPPERY_73:
        case SURFACE_NOISE_VERY_SLIPPERY_74:
        case SURFACE_NOISE_VERY_SLIPPERY:
        case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
            floorClass = SURFACE_CLASS_VERY_SLIPPERY;
            break;
        }
    }

    // Crawling allows Mario to not slide on certain steeper surfaces.
    //if (m->action == ACT_CRAWLING && m->floor->normal.y > 0.5f && floorClass == SURFACE_CLASS_DEFAULT) {
    //    floorClass = SURFACE_CLASS_NOT_SLIPPERY;
    //}

    return floorClass;
}

void transform_surfaces(struct Surface* surfaces, int numSurfaces, Mat4* transform) {
    int maxY, minY;
    float nx, ny, nz;
    float mag;

    for (int i = 0; i < numSurfaces; i++) {
        Vec3s v1 = { surfaces->vertex1[0], surfaces->vertex1[1], surfaces->vertex1[2] };
        Vec3s v2 = { surfaces->vertex2[0], surfaces->vertex2[1], surfaces->vertex2[2] };
        Vec3s v3 = { surfaces->vertex3[0], surfaces->vertex3[1], surfaces->vertex3[2] };

        for (int j = 0; j < 3; j++) {
            surfaces->vertex1[j] = (short)(v1[0] * (*transform)[0][j] + v1[1] * (*transform)[1][j] + v1[2] * (*transform)[2][j] + (*transform)[3][j]);
            surfaces->vertex2[j] = (short)(v2[0] * (*transform)[0][j] + v2[1] * (*transform)[1][j] + v2[2] * (*transform)[2][j] + (*transform)[3][j]);
            surfaces->vertex3[j] = (short)(v3[0] * (*transform)[0][j] + v3[1] * (*transform)[1][j] + v3[2] * (*transform)[2][j] + (*transform)[3][j]);
        }

        // (v2 - v1) x (v3 - v2)
        nx = (surfaces->vertex2[1] - surfaces->vertex1[1]) * (surfaces->vertex3[2] - surfaces->vertex2[2]) - (surfaces->vertex2[2] - surfaces->vertex1[2]) * (surfaces->vertex3[1] - surfaces->vertex2[1]);
        ny = (surfaces->vertex2[2] - surfaces->vertex1[2]) * (surfaces->vertex3[0] - surfaces->vertex2[0]) - (surfaces->vertex2[0] - surfaces->vertex1[0]) * (surfaces->vertex3[2] - surfaces->vertex2[2]);
        nz = (surfaces->vertex2[0] - surfaces->vertex1[0]) * (surfaces->vertex3[1] - surfaces->vertex2[1]) - (surfaces->vertex2[1] - surfaces->vertex1[1]) * (surfaces->vertex3[0] - surfaces->vertex2[0]);
        mag = sqrtf(nx * nx + ny * ny + nz * nz);

        // Could have used min_3 and max_3 for this...
        minY = surfaces->vertex1[1];
        if (surfaces->vertex2[1] < minY) {
            minY = surfaces->vertex2[1];
        }
        if (surfaces->vertex3[1] < minY) {
            minY = surfaces->vertex3[1];
        }

        maxY = surfaces->vertex1[1];
        if (surfaces->vertex2[1] > maxY) {
            maxY = surfaces->vertex2[1];
        }
        if (surfaces->vertex3[1] > maxY) {
            maxY = surfaces->vertex3[1];
        }

        mag = (float)(1.0 / mag);
        nx *= mag;
        ny *= mag;
        nz *= mag;

        surfaces->normal.x = nx;
        surfaces->normal.y = ny;
        surfaces->normal.z = nz;

        surfaces->originOffset = -(nx * surfaces->vertex1[0] + ny * surfaces->vertex1[1] + nz * surfaces->vertex1[2]);

        surfaces->lowerY = minY - 5;
        surfaces->upperY = maxY + 5;

        surfaces++;
    }
}

/**
 * Initializes a Surface struct using the given vertex data
 * @param vertexData The raw data containing vertex positions
 * @param vertexIndices Helper which tells positions in vertexData to start reading vertices
 */
static void read_surface_data(short* vertexData, short** vertexIndices, struct Surface* surface) {
    int x1, y1, z1;
    int x2, y2, z2;
    int x3, y3, z3;
    int maxY, minY;
    float nx, ny, nz;
    float mag;
    short offset1, offset2, offset3;

    offset1 = 3 * (*vertexIndices)[0];
    offset2 = 3 * (*vertexIndices)[1];
    offset3 = 3 * (*vertexIndices)[2];

    x1 = *(vertexData + offset1 + 0);
    y1 = *(vertexData + offset1 + 1);
    z1 = *(vertexData + offset1 + 2);

    x2 = *(vertexData + offset2 + 0);
    y2 = *(vertexData + offset2 + 1);
    z2 = *(vertexData + offset2 + 2);

    x3 = *(vertexData + offset3 + 0);
    y3 = *(vertexData + offset3 + 1);
    z3 = *(vertexData + offset3 + 2);

    // (v2 - v1) x (v3 - v2)
    nx = (y2 - y1) * (z3 - z2) - (z2 - z1) * (y3 - y2);
    ny = (z2 - z1) * (x3 - x2) - (x2 - x1) * (z3 - z2);
    nz = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);
    mag = sqrtf(nx * nx + ny * ny + nz * nz);

    // Could have used min_3 and max_3 for this...
    minY = y1;
    if (y2 < minY) {
        minY = y2;
    }
    if (y3 < minY) {
        minY = y3;
    }

    maxY = y1;
    if (y2 > maxY) {
        maxY = y2;
    }
    if (y3 > maxY) {
        maxY = y3;
    }

    mag = (f32)(1.0 / mag);
    nx *= mag;
    ny *= mag;
    nz *= mag;

    surface->vertex1[0] = x1;
    surface->vertex2[0] = x2;
    surface->vertex3[0] = x3;

    surface->vertex1[1] = y1;
    surface->vertex2[1] = y2;
    surface->vertex3[1] = y3;

    surface->vertex1[2] = z1;
    surface->vertex2[2] = z2;
    surface->vertex3[2] = z3;

    surface->normal.x = nx;
    surface->normal.y = ny;
    surface->normal.z = nz;

    surface->originOffset = -(nx * x1 + ny * y1 + nz * z1);

    surface->lowerY = minY - 5;
    surface->upperY = maxY + 5;
}

/**
 * Applies an object's transformation to the object's vertices.
 */
static void get_object_vertices(struct Object* obj, short** data, short* vertexData) {
    short* vertices;
    int numVertices;

    numVertices = *(*data);
    (*data)++;

    vertices = *data;

    // Go through all vertices, rotating and translating them to transform the object.
    while (numVertices--) {
        *vertexData++ = *(vertices++);
        *vertexData++ = *(vertices++);
        *vertexData++ = *(vertices++);
    }

    *data = vertices;
}

static short surface_has_force(short surfaceType) {
    short hasForce = FALSE;

    switch (surfaceType) {
    case SURFACE_0004: // Unused
    case SURFACE_FLOWING_WATER:
    case SURFACE_DEEP_MOVING_QUICKSAND:
    case SURFACE_SHALLOW_MOVING_QUICKSAND:
    case SURFACE_MOVING_QUICKSAND:
    case SURFACE_HORIZONTAL_WIND:
    case SURFACE_INSTANT_MOVING_QUICKSAND:
        hasForce = TRUE;
        break;

    default:
        break;
    }
    return hasForce;
}

static void load_object_surfaces(short** data, short* vertexData, struct Surface** surfaces) {
    int surfaceType;
    int i;
    int numSurfaces;
    short hasForce;

    surfaceType = *(*data);
    (*data)++;

    numSurfaces = *(*data);
    (*data)++;

    hasForce = surface_has_force(surfaceType);
    
    for (i = 0; i < numSurfaces; i++) {
        read_surface_data(vertexData, data,  *surfaces);
        (*surfaces)->type = surfaceType;

        if (hasForce) {
            *data += 4;
        }
        else {
            *data += 3;
        }

        (*surfaces)++;
    }
}

int count_surfaces(short* data) {
    int surfaceCount = 0;

    while (*data != TERRAIN_LOAD_CONTINUE) {
        int surfaceType;
        int i;
        int numSurfaces;
        short hasForce;

        surfaceType = *data;
        data++;

        numSurfaces = *data;
        data++;

        hasForce = surface_has_force(surfaceType);

        if (hasForce) {
            data += 4 * numSurfaces;
        }
        else {
            data += 3 * numSurfaces;
        }

        surfaceCount += numSurfaces;
    }

    return surfaceCount;
}

int get_surfaces(struct Object* obj, struct Surface** surfaces) {
    struct Surface* s;
    short vertexData[600];
    short* collisionData = (short*)obj->collisionData;
    collisionData++;
    get_object_vertices(obj, &collisionData, vertexData);

    int surfaceCount = count_surfaces(collisionData);

    *surfaces = (struct Surface*)malloc(surfaceCount * sizeof(Surface));
    s = *surfaces;

    while (*collisionData != TERRAIN_LOAD_CONTINUE) {
        load_object_surfaces(&collisionData, vertexData, surfaces);
    }

    *surfaces = s;
    return surfaceCount;
}