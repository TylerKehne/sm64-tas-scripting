#pragma once
#include <stdint.h>
#include <filesystem>
#include <map>
#include <vector>

#ifndef INPUTS_H
	#define INPUTS_H

class Rotation
{
public:
	enum Value : int8_t
	{
		CLOCKWISE				 = -1,
		NONE						 = 0,
		COUNTERCLOCKWISE = 1
	};

	Rotation() = default;
	constexpr Rotation(Value v) : value(v) {}

	constexpr operator Value() const { return value; }

	Rotation Negate()
	{
		if (value == Rotation::CLOCKWISE)
			return Rotation::COUNTERCLOCKWISE;
		else if (value == Rotation::COUNTERCLOCKWISE)
			return Rotation::CLOCKWISE;

		return Rotation::NONE;
	}

private:
	Value value;
};

enum Buttons
{
	C_RIGHT = 1 << 0,
	C_LEFT	= 1 << 1,
	C_DOWN	= 1 << 2,
	C_UP		= 1 << 3,
	R				= 1 << 4,
	L				= 1 << 5,
	D_RIGHT = 1 << 8,
	D_LEFT	= 1 << 9,
	D_DOWN	= 1 << 10,
	D_UP		= 1 << 11,
	START		= 1 << 12,
	Z				= 1 << 13,
	B				= 1 << 14,
	A				= 1 << 15
};

class Inputs
{
public:
	uint16_t buttons = 0;
	int8_t stick_x	 = 0;
	int8_t stick_y	 = 0;

	Inputs() {}

	Inputs(uint16_t buttons, int8_t stick_x, int8_t stick_y) :
		buttons(buttons), stick_x(stick_x), stick_y(stick_y)
	{
	}

	static std::pair<int8_t, int8_t> GetClosestInputByYawHau(
		int16_t intendedYaw, float intendedMag, int16_t cameraYaw,
		Rotation bias = Rotation::NONE);
	static std::pair<int8_t, int8_t> GetClosestInputByYawExact(
		int16_t intendedYaw, float intendedMag, int16_t cameraYaw,
		Rotation bias = Rotation::NONE);
	static std::pair<int16_t, float> GetIntendedYawMagFromInput(
		int8_t stickX, int8_t stickY, int16_t cameraYaw);
	static bool HauEquals(int16_t angle1, int16_t angle2);
};

class M64Base
{
public:
	std::map<uint64_t, Inputs> frames;

	M64Base() {}
};

class M64 : public M64Base
{
public:
	std::filesystem::path fileName;

	M64() {}

	M64(const std::filesystem::path& fileName) : fileName(fileName) {}

	int load();
	int save(long initFrame = 0);
};

class M64Diff : public M64Base
{
public:
	M64Diff() : M64Base() {}
};

#endif