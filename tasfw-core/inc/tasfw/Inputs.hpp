#pragma once
#include <cstdint>
#include <utility>

#ifndef INPUTS_H
#define INPUTS_H

class Rotation
{
public:
	enum Value : int8_t
	{
		CLOCKWISE = -1,
		NONE = 0,
		COUNTERCLOCKWISE = 1
	};

	Rotation() = default;
	constexpr Rotation(Value v) : value(v) {}

	constexpr operator Value() const { return value; }

	Rotation Negate()
	{
		switch (value) {
			case Rotation::CLOCKWISE:
				return Rotation::COUNTERCLOCKWISE;
			case Rotation::COUNTERCLOCKWISE:
				return Rotation::CLOCKWISE;
			case Rotation::NONE:
				return Rotation::NONE;
		}
		return Rotation::NONE; // unreachable; keeps every compiler's -Wreturn-type quiet
	}

private:
	Value value;
};

enum Buttons
{
	C_RIGHT = 1U << 0U,
	C_LEFT = 1U << 1U,
	C_DOWN = 1U << 2U,
	C_UP = 1U << 3U,
	R = 1U << 4U,
	L = 1U << 5U,
	D_RIGHT = 1U << 8U,
	D_LEFT = 1U << 9U,
	D_DOWN = 1U << 10U,
	D_UP = 1U << 11U,
	START = 1U << 12U,
	Z = 1U << 13U,
	B = 1U << 14U,
	A = 1U << 15U
};

class Inputs
{
public:
	uint16_t buttons = 0;
	int8_t stick_x = 0;
	int8_t stick_y = 0;

	Inputs() = default;
	Inputs(Inputs&&) noexcept = default;
	Inputs(const Inputs&) = default;
	Inputs& operator=(const Inputs&) = default;
	Inputs& operator=(Inputs&& other) noexcept = default;
	bool operator==(const Inputs& other) const = default;

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

#endif
