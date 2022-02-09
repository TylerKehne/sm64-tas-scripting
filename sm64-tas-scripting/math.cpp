#include "Trig.hpp"
#include "math.hpp"
#include <cmath>

void linear_mtxf_mul_vec3f(Mat4& m, Vec3f& dst, Vec3f& v) {
    for (int i = 0; i < 3; i++) {
        dst[i] = m[0][i] * v[0] + m[1][i] * v[1] + m[2][i] * v[2];
    }
}

void mtxf_align_terrain_normal(Mat4& dest, Vec3f& upDir, Vec3f& pos, s16 yaw) {
    Vec3f lateralDir = { gSineTable[yaw], 0, gCosineTable[yaw] };
    Vec3f leftDir;
    Vec3f forwardDir;

    vec3f_normalize(upDir);

    vec3f_cross(leftDir, upDir, lateralDir);
    vec3f_normalize(leftDir);

    vec3f_cross(forwardDir, leftDir, upDir);
    vec3f_normalize(forwardDir);

    dest[0][0] = leftDir[0];
    dest[0][1] = leftDir[1];
    dest[0][2] = leftDir[2];
    dest[3][0] = pos[0];

    dest[1][0] = upDir[0];
    dest[1][1] = upDir[1];
    dest[1][2] = upDir[2];
    dest[3][1] = pos[1];

    dest[2][0] = forwardDir[0];
    dest[2][1] = forwardDir[1];
    dest[2][2] = forwardDir[2];
    dest[3][2] = pos[2];

    dest[0][3] = 0.0f;
    dest[1][3] = 0.0f;
    dest[2][3] = 0.0f;
    dest[3][3] = 1.0f;
}

/// Make vector 'dest' the cross product of vectors a and b.
void vec3f_cross(Vec3f& dest, Vec3f& a, Vec3f& b) {
    dest[0] = a[1] * b[2] - b[1] * a[2];
    dest[1] = a[2] * b[0] - b[2] * a[0];
    dest[2] = a[0] * b[1] - b[0] * a[1];
}

/// Scale vector 'dest' so it has length 1
void vec3f_normalize(Vec3f& dest) {
    //! Possible division by zero
    float invsqrt = 1.0f / sqrtf(dest[0] * dest[0] + dest[1] * dest[1] + dest[2] * dest[2]);

    dest[0] *= invsqrt;
    dest[1] *= invsqrt;
    dest[2] *= invsqrt;
}