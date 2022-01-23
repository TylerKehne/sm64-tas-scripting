#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>
#include <map>

#ifndef INPUTS_H
#define INPUTS_H

enum Buttons
{
	C_RIGHT = 1 << 0,
	C_LEFT = 1 << 1,
	C_DOWN = 1 << 2,
	C_UP = 1 << 3,
	R = 1 << 4,
	L = 1 << 5,
	D_RIGHT = 1 << 8,
	D_LEFT = 1 << 9,
	D_DOWN = 1 << 10,
	D_UP = 1 << 11,
	START = 1 << 12,
	Z = 1 << 13,
	B = 1 << 14,
	A = 1 << 15
};

class Inputs {
public:
	uint16_t buttons = 0;
	int8_t stick_x = 0;
	int8_t stick_y = 0;

	Inputs() {}

	Inputs(uint16_t buttons, int8_t stick_x, int8_t stick_y)
		: buttons(buttons), stick_x(stick_x), stick_y(stick_y) {}

	static void set_inputs(Inputs inputs);
	static void PopulateInputMappings();
	static std::pair<int8_t, int8_t> GetClosestInputByYawHau(int16_t intendedYaw, float intendedMag, int16_t cameraYaw, int32_t direction = 0);
};

class M64Base
{
public:
	uint64_t initFrame = -1;
	std::map<uint64_t, Inputs> frames;

	M64Base() {}

	Inputs getInputs(uint64_t frame);
};

class M64 : public M64Base
{
public:
	const char* fileName;

	M64(const char* fileName) : fileName(fileName)
	{
		initFrame = 0;
	}

	int load();
	int save(long initFrame = 0);
};

class M64Diff: public M64Base
{
public:
	M64Diff() : M64Base() {}

	M64Diff(uint64_t initFrame) : M64Base()
	{
		this->initFrame = initFrame;
	}
};

#endif