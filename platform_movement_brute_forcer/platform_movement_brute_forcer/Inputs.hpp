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
	uint16_t buttons;
	int8_t stick_x;
	int8_t stick_y;

	Inputs(uint16_t buttons, int8_t stick_x, int8_t stick_y)
		: buttons(buttons), stick_x(stick_x), stick_y(stick_y) {}

	static void set_inputs(Inputs inputs);
	static void PopulateInputMappings();
	static std::pair<int8_t, int8_t> GetClosestInputByYawHau(int16_t intendedYaw, float intendedMag, int16_t cameraYaw);
};

class M64
{
public:
	const char* fileName = "";
	std::vector<Inputs> frames;

	M64() {}
	M64(const char* fileName)
		: fileName(fileName) {}

	int load();
	int save(long initFrame = 0);
};

class M64Diff:public M64
{
public:
	M64& baseM64;
	long long initFrame = 0;

	M64Diff(M64& baseM64, long long initFrame)
		: baseM64(baseM64), initFrame(initFrame) {}

	void apply();
};

#endif