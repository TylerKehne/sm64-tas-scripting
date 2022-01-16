#pragma once
#include <cstdint>

#include "Inputs.hpp"
#include "Game.hpp"

using namespace std;

#ifndef MOVEMENT_H
#define MOVEMENT_H

void set_inputs(Game game, Inputs input);

void update_sliding_angle(float accel, float lossFactor, float normalX, float normalZ);
void update_sliding(float normalX, float normalZ, int16_t floorType);

void calc_intended_yawmag(int8_t stickX, int8_t stickY, int16_t camYaw);

#endif

