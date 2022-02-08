#pragma once
#include "Types.hpp"

#ifndef SURFACE_H
#define SURFACE_H

#define TERRAIN_LOAD_CONTINUE    0x0041 // Stop loading vertices but continues to load other collision commands

#define SURFACE_0004                         0x0004 // Unused, has no function and has parameters
#define SURFACE_FLOWING_WATER                0x000E // Water (flowing), has parameters
#define SURFACE_VERY_SLIPPERY                0x0013 // Very slippery, mostly used for slides
#define SURFACE_SLIPPERY                     0x0014 // Slippery
#define SURFACE_NOT_SLIPPERY                 0x0015 // Non-slippery, climbable
#define SURFACE_DEEP_MOVING_QUICKSAND        0x0024 // Moving quicksand (flowing, depth of 160 units)
#define SURFACE_SHALLOW_MOVING_QUICKSAND     0x0025 // Moving quicksand (flowing, depth of 25 units)
#define SURFACE_MOVING_QUICKSAND             0x0027 // Moving quicksand (flowing, depth of 60 units)
#define SURFACE_NOISE_SLIPPERY               0x002A // Slippery floor with noise
#define SURFACE_HORIZONTAL_WIND              0x002C // Horizontal wind, has parameters
#define SURFACE_INSTANT_MOVING_QUICKSAND     0x002D // Quicksand (lethal, flowing)
#define SURFACE_ICE                          0x002E // Slippery Ice, in snow levels and THI's water floor
#define SURFACE_HARD_SLIPPERY                0x0035 // Hard and slippery (Always has fall damage)
#define SURFACE_HARD_VERY_SLIPPERY           0x0036 // Hard and very slippery (Always has fall damage)
#define SURFACE_HARD_NOT_SLIPPERY            0x0037 // Hard and Non-slippery (Always has fall damage)
#define SURFACE_NOISE_VERY_SLIPPERY_73       0x0073 // Very slippery floor with noise, unused
#define SURFACE_NOISE_VERY_SLIPPERY_74       0x0074 // Very slippery floor with noise, unused
#define SURFACE_NOISE_VERY_SLIPPERY          0x0075 // Very slippery floor with noise, used in CCM
#define SURFACE_NO_CAM_COL_VERY_SLIPPERY     0x0078 // Surface with no cam collision flag, very slippery with noise (THI)
#define SURFACE_NO_CAM_COL_SLIPPERY          0x0079 // Surface with no cam collision flag, slippery with noise (CCM, PSS and TTM slides)
#define SURFACE_SWITCH                       0x007A // Surface with no cam collision flag, non-slippery with noise, used by switches and Dorrie

#define SURFACE_CLASS_DEFAULT       0x0000
#define SURFACE_CLASS_VERY_SLIPPERY 0x0013
#define SURFACE_CLASS_SLIPPERY      0x0014
#define SURFACE_CLASS_NOT_SLIPPERY  0x0015

void find_floor(Vec3f* marioPos, struct Surface* surfaces, int surfaceCount, struct Surface** floor);
bool floor_is_slope(struct Surface* floor);
static short get_floor_class(struct Surface* floor);
void transform_surfaces(struct Surface* surfaces, int numSurfaces, Mat4* transform);
int get_surfaces(struct Object* obj, struct Surface** surfaces);
static void load_object_surfaces(short** data, short* vertexData, struct Surface** surfaces);
static short surface_has_force(short surfaceType);
static void get_object_vertices(struct Object* obj, short** data, short* vertexData);
static void read_surface_data(short* vertexData, short** vertexIndices, struct Surface* surface);

#endif