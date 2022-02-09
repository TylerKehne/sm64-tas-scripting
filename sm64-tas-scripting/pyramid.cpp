#include <cmath>
#include "surface.hpp"
#include "pyramid.hpp"
#include "math.hpp"
#include "ObjectFields.hpp"
#include "Trig.hpp"
#include "Types.hpp"

void simulate_platform_tilt(struct Object* marioObj, struct Object* pyramidPlatform, short* floorAngle, bool* isSlope) {
    Vec3f marioPos = { marioObj->oPosX, marioObj->oPosY, marioObj->oPosZ };
    struct Surface* pyramidSurfaces;
    int numSurfaces = get_surfaces(pyramidPlatform, &pyramidSurfaces);

    Vec3f pyramidPos = { pyramidPlatform->oPosX, pyramidPlatform->oPosY, pyramidPlatform->oPosZ };
    Vec3f pyramidNormal = { pyramidPlatform->oTiltingPyramidNormalX, pyramidPlatform->oTiltingPyramidNormalY, pyramidPlatform->oTiltingPyramidNormalZ };

    struct Object* marioPlatform = marioObj->platform;
    bool onPlatform = (pyramidPlatform == marioPlatform);

    Mat4 transform;

    bhv_tilting_inverted_pyramid_loop(pyramidNormal, pyramidPos, marioPos, onPlatform, transform);

    transform_surfaces(pyramidSurfaces, numSurfaces, &transform);

    struct Surface* floor;

    find_floor(&marioPos, pyramidSurfaces, numSurfaces, &floor);

    if (floor == NULL) {
        //Probably a better way of handling this
        *floorAngle = 0;
        *isSlope = false;
    }
    else {
        *floorAngle = atan2s(floor->normal.z, floor->normal.x);
        *isSlope = floor_is_slope(floor);
    }
}

void bhv_tilting_inverted_pyramid_loop(Vec3f& platNormal, Vec3f& platPos, Vec3f& marioPos, bool onPlatform, Mat4& transform) {
    float dx;
    float dy;
    float dz;
    float d;

    Vec3f dist;
    Vec3f posBeforeRotation;
    Vec3f posAfterRotation;

    create_transform_from_normals(transform, platNormal, platPos);

    if (onPlatform) {
        dist[0] = marioPos[0] - platPos[0];
        dist[1] = marioPos[1] - platPos[1];
        dist[2] = marioPos[2] - platPos[2];
        linear_mtxf_mul_vec3f(transform, posBeforeRotation, dist);

        dx = marioPos[0] - platPos[0];
        dy = 500.0f;
        dz = marioPos[2] - platPos[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);

        //! Always true since dy = 500, making d >= 500.
        if (d != 0.0f) {
            // Normalizing
            d = 1.0 / d;
            dx *= d;
            dy *= d;
            dz *= d;
        }
        else {
            dx = 0.0f;
            dy = 1.0f;
            dz = 0.0f;
        }

        // Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object. 
        // Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
        platNormal[0] = approach_by_increment(dx, platNormal[0], 0.01f);
        platNormal[1] = approach_by_increment(dy, platNormal[1], 0.01f);
        platNormal[2] = approach_by_increment(dz, platNormal[2], 0.01f);
        create_transform_from_normals(transform, platNormal, platPos);

        // If Mario is on the platform, adjust his position for the platform tilt.
        linear_mtxf_mul_vec3f(transform, posAfterRotation, dist);
        marioPos[0] += posAfterRotation[0] - posBeforeRotation[0];
        marioPos[1] += posAfterRotation[1] - posBeforeRotation[1];
        marioPos[2] += posAfterRotation[2] - posBeforeRotation[2];
    }
    else {
        dx = 0.0f;
        dy = 1.0f;
        dz = 0.0f;

        // Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object. 
        // Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
        platNormal[0] = approach_by_increment(dx, platNormal[0], 0.01f);
        platNormal[1] = approach_by_increment(dy, platNormal[1], 0.01f);
        platNormal[2] = approach_by_increment(dz, platNormal[2], 0.01f);
    }
}

float approach_by_increment(float goal, float src, float inc) {
    float newVal;

    if (src <= goal) {
        if (goal - src < inc) {
            newVal = goal;
        }
        else {
            newVal = src + inc;
        }
    }
    else if (goal - src > -inc) {
        newVal = goal;
    }
    else {
        newVal = src - inc;
    }

    return newVal;
}

void create_transform_from_normals(Mat4& transform, Vec3f& normal, Vec3f& pos) {
    Vec3f tempNormal = { normal[0], normal[1], normal[2] };
    Vec3f tempPos = { pos[0], pos[1], pos[2] };

    mtxf_align_terrain_normal(transform, tempNormal, tempPos, 0);
}