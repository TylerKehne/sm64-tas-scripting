#pragma once
#include <cstdint>
#include <functional>
#include "Slot.hpp"

#ifndef BRUTE_H
#define BRUTE_H

void calc_next_node(bool isLeaf, Slot* saveState, bool recurse = false, int16_t startIndex = 0, Slot* saveStateTemp = NULL, Slot* saveStateNext = NULL);
bool distance_precheck(float x, float y, float z, float hSpd, float* marioFloorNormalY, int16_t* marioFloorType);
bool input_precheck(float floorNormalX, float floorNormalY, float floorNormalZ, int16_t floorType);
void update_mario_state(float x, float y, float z, float hSpd, int16_t fYaw);
bool check_if_pos_in_main_universe(float x, float z, bool input_matters, bool* crouchslide_returned_to_main_uni, int16_t fYaw, int16_t input_x, int16_t input_y);
bool check_freefall_outcome(int16_t input_x, int16_t input_y, int16_t fYaw, bool input_matters, bool isLeaf, bool recurse, Slot* saveStateTemp, Slot* saveStateNext, bool* crouchslide_returned_to_main_uni, int dust_frames, std::function<void(bool, Slot*)> execute_recursion);

#endif

