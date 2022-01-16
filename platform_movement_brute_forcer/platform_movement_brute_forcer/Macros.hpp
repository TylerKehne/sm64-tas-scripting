#pragma once

#include "Game.hpp"

using namespace std;

#ifndef MACROS_H
#define MACROS_H

//Pointers and definitions.
#define marioX(game) (float*)(game.addr("gMarioStates") + 60)
#define marioY(game) (float*)(game.addr("gMarioStates") + 64)
#define marioZ(game) (float*)(game.addr("gMarioStates") + 68)
#define marioAction(game) (uint32_t*)(game.addr("gMarioStates") + 12)
#define marioHSpd(game) (float*)(game.addr("gMarioStates") + 84)
#define marioFYaw(game) (int16_t*)(game.addr("gMarioStates") + 46)
#define marioMYaw(game) (int16_t*)(game.addr("gMarioStates") + 56)
#define marioXSlidSpd(game) (float*)(game.addr("gMarioStates") + 88)
#define marioZSlidSpd(game) (float*)(game.addr("gMarioStates") + 92)
#define marioIntYaw(game) (int16_t*)(game.addr("gMarioStates") + 36)
#define marioIntMag(game) (float*)(game.addr("gMarioStates") + 32)
#define marioAreaPtr(game) (uint64_t*)(game.addr("gMarioStates") + 0xB8)
#define marioActionTimer(game) (uint16_t*)(game.addr("gMarioStates") + 0x1A)

#define bullyX(game) (float*)(game.addr("gObjectPool") + (27 * 1392 + 240))
#define bullyY(game) (float*)(game.addr("gObjectPool") + (27 * 1392 + 244))
#define bullyZ(game) (float*)(game.addr("gObjectPool") + (27 * 1392 + 248))
#define bullyHSpd(game) (float*)(game.addr("gObjectPool") + (27 * 1392 + 264))
#define bullyYaw1(game) (uint16_t*)(game.addr("gObjectPool") + (27 * 1392 + 280))

#define trackPlatX(game) (float*)(game.addr("gObjectPool") + (85 * 1392 + 240))
#define trackPlatAction(game) (uint32_t*)(game.addr("gObjectPool") + (85 * 1392 + 412))

#define marioFloorPtr(game) (uint64_t*)(game.addr("gMarioStates") + 0x70)

/*
* #define marioCameraYaw = (int16_t*)(game.addr("gCamera") + 0x2)
* #define marioAreaPtr = (uint64_t*)(game.addr("gMarioStates") + 0xB8)
* #define marioAreaCameraPtr = (uint64_t*)(*marioAreaPtr(game) + 0x48)
* #define marioAreaCameraYaw = (int16_t*)(*marioAreaCameraPtr(game) + 0x2)
*/

#define DEFACTO_MULT float(0.8386462330818176) // 0.8275776505470276, 0.8324692845344543, 0.8386462330818176
#define MIN_YAW -31376 // -31792, -31632, -31376
#define MAX_YAW -31184 // -31376, -31280, -31184
#define MOD_TARGET_POS_X -1665
#define MOD_TARGET_POS_Z -356

#endif