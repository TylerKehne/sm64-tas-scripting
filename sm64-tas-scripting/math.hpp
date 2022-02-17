#pragma once
#include "Types.hpp"

#ifndef MATH_H
#define MATH_H

void linear_mtxf_mul_vec3f(Mat4& m, Vec3f& dst, Vec3f& v);
void mtxf_align_terrain_normal(Mat4& dest, Vec3f& upDir, Vec3f& pos, s16 yaw);
void vec3f_cross(Vec3f& dest, Vec3f& a, Vec3f& b);
void vec3f_normalize(Vec3f& dest);

#endif