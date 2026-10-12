#pragma once
#include <sm64/UltraTypes.hpp>

// struct Controller alone, out of the decomp's include/types.h (scripts/decomp_pin.json). The
// two OS pointers at its end are left as their forward declarations.

struct OSContStatus;
struct OSContPad;

struct Controller
{
	/*0x00*/ s16 rawStickX;	//
	/*0x02*/ s16 rawStickY;	//
	/*0x04*/ float stickX;	// [-64, 64] positive is right
	/*0x08*/ float stickY;	// [-64, 64] positive is up
	/*0x0C*/ float stickMag;	// distance from center [0, 64]
	/*0x10*/ u16 buttonDown;
	/*0x12*/ u16 buttonPressed;
	/*0x14*/ OSContStatus* statusData;
	/*0x18*/ OSContPad* controllerData;
};
