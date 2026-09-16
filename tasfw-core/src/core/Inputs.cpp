#include <tasfw/Inputs.hpp>
#include <map>
#include <sys/types.h>
#include <ios>
#include <system_error>
#include <sm64/Trig.hpp>

#include <cmath>
#include <iostream>
#include <fstream>
#include <limits>
#include <unordered_map>

#undef max
#undef min

std::pair<
	std::unordered_map<int16_t, std::map<float, std::pair<int8_t, int8_t>>>,
	std::unordered_map<int8_t, std::unordered_map<int8_t, std::pair<int16_t, float>>>>
PopulateInputMappings()
{
	std::unordered_map<int16_t, std::map<float, std::pair<int8_t, int8_t>>>
		yawMagToInputs;
	std::unordered_map<
		int8_t, std::unordered_map<int8_t, std::pair<int16_t, float>>>
		inputsToYawMag;
	for (int16_t stickX = -128; stickX <= 127; stickX++)
	{
		for (int16_t stickY = -128; stickY <= 127; stickY++)
		{
			float adjustedStickX = 0;
			float adjustedStickY = 0;

			if (stickX <= -8)
				adjustedStickX = float(stickX + 6);
			else if (stickX >= 8)
				adjustedStickX = float(stickX - 6);

			if (stickY <= -8)
				adjustedStickY = float(stickY + 6);
			else if (stickY >= 8)
				adjustedStickY = float(stickY - 6);

			float stickMag = sqrtf(
				adjustedStickX * adjustedStickX + adjustedStickY * adjustedStickY);

			if (stickMag > 64)
			{
				adjustedStickX *= 64 / stickMag;
				adjustedStickY *= 64 / stickMag;
				stickMag = 64;
			}

			float intendedMag = ((stickMag / 64.0f) * (stickMag / 64.0f)) * 64.0f;
			intendedMag				= intendedMag / 2.0f;

			int16_t baseIntendedYaw = 0;
			if (intendedMag > 0.0f)
			{
				baseIntendedYaw = atan2s(-adjustedStickY, adjustedStickX);
				// stickX/stickY are int16_t loop counters that stay within int8_t range.
				std::pair<int8_t, int8_t> stick {int8_t(stickX), int8_t(stickY)};
				if (!yawMagToInputs.contains(baseIntendedYaw))
					yawMagToInputs[baseIntendedYaw] =
						std::map<float, std::pair<int8_t, int8_t>> {
							{intendedMag, stick}};
				else if (!yawMagToInputs[baseIntendedYaw].contains(intendedMag))
					yawMagToInputs[baseIntendedYaw][intendedMag] = stick;
			}

			inputsToYawMag[int8_t(stickX)][int8_t(stickY)] = {baseIntendedYaw, intendedMag};
		}
	}
	return {yawMagToInputs, inputsToYawMag};
}

// Not a structured binding on purpose: Clang 19.1 (clang-cl) crashes with an access violation
// while parsing a function that references a namespace-scope `static` structured binding.
// See docs/compilers.md. Two references to the members of a named static are equivalent.
static const auto inputMappings = PopulateInputMappings();
static const auto& yawMagToInputs = inputMappings.first;
static const auto& inputsToYawMag = inputMappings.second;

std::pair<int8_t, int8_t> Inputs::GetClosestInputByYawHau(
	int16_t intendedYaw, float intendedMag, int16_t cameraYaw, Rotation bias)
{
	// intendedYaw = baseIntendedYaw + cameraYaw

	if (intendedMag == 0.0f)
		return {0, 0};

	int16_t minIntendedYaw = intendedYaw - (intendedYaw & 15);
	int16_t maxIntendedYaw = minIntendedYaw + 15;

	int16_t minBaseIntendedYaw = minIntendedYaw - cameraYaw;
	int16_t maxBaseIntendedYaw = maxIntendedYaw - cameraYaw;

	std::pair<int8_t, int8_t> closestInput = {0, 0};
	float closestMagDistance = std::numeric_limits<float>::infinity();

	bool foundMatchingYawHau = false;
	int hauOffset						 = 0;
	while (true)
	{
		for (int16_t yaw = int16_t(minBaseIntendedYaw + 16 * hauOffset);
				 yaw != int16_t(maxBaseIntendedYaw + 16 * hauOffset); yaw++)
		{
			if (yawMagToInputs.contains(yaw))
			{
				foundMatchingYawHau = true;
				if (yawMagToInputs.at(yaw).contains(intendedMag))
					return yawMagToInputs.at(yaw).at(intendedMag);

				auto upper = yawMagToInputs.at(yaw).upper_bound(intendedMag);
				auto lower = upper == yawMagToInputs.at(yaw).begin() ? yawMagToInputs.at(yaw).end() : std::prev(upper);
				float magDistance = std::numeric_limits<float>::infinity();
				if (upper == yawMagToInputs.at(yaw).end())
				{
					magDistance = intendedMag - (*lower).first;
					if (magDistance < closestMagDistance)
					{
						closestMagDistance = magDistance;
						closestInput = (*lower).second;
					}
				}
				else if (lower == yawMagToInputs.at(yaw).end())
				{
					magDistance = (*upper).first - intendedMag;
					if (magDistance < closestMagDistance)
					{
						closestMagDistance = magDistance;
						closestInput = (*upper).second;
					}
				}
				else
				{
					float lowerMagDistance = intendedMag - (*lower).first;
					float upperMagDistance = (*upper).first - intendedMag;

					if (lowerMagDistance <= upperMagDistance)
					{
						if (lowerMagDistance < closestMagDistance)
						{
							closestMagDistance = lowerMagDistance;
							closestInput = (*lower).second;
						}
					}
					else
					{
						if (upperMagDistance < closestMagDistance)
						{
							closestMagDistance = upperMagDistance;
							closestInput = (*upper).second;
						}
					}
				}
			}
		}

		// Check both positive and negative HAU offsets for the closest mag if
		// either of them finds a matching yaw
		if (bias == Rotation::NONE)
		{
			if (hauOffset == 0)
			{
				if (foundMatchingYawHau)
					break;
				hauOffset = 1;
			}
			else if (hauOffset > 0)
				hauOffset *= -1;
			else if (foundMatchingYawHau)
				break;
			else
				hauOffset = -hauOffset + 1;
		}
		else if (bias == Rotation::COUNTERCLOCKWISE)
		{
			if (foundMatchingYawHau)
				break;
			hauOffset++;
		}
		else
		{
			if (foundMatchingYawHau)
				break;
			hauOffset--;
		}
	}

	return closestInput;
}

std::pair<int8_t, int8_t> Inputs::GetClosestInputByYawExact(
	int16_t intendedYaw, float intendedMag, int16_t cameraYaw, Rotation bias)
{
	// intendedYaw = baseIntendedYaw + cameraYaw

	if (intendedMag == 0.0f)
		return {0, 0};

	int16_t baseIntendedYaw = intendedYaw - cameraYaw;

	std::pair<int8_t, int8_t> closestInput = std::pair(0, 0);
	float closestMagDistance = std::numeric_limits<float>::infinity();

	bool foundMatchingYaw = false;
	int offset						= 0;
	while (true)
	{
		int16_t yaw = baseIntendedYaw + offset;
		if (yawMagToInputs.contains(yaw))
		{
			foundMatchingYaw = true;
			if (yawMagToInputs.at(yaw).contains(intendedMag))
				return yawMagToInputs.at(yaw).at(intendedMag);

			auto upper = yawMagToInputs.at(yaw).upper_bound(intendedMag);
			auto lower = upper == yawMagToInputs.at(yaw).begin() ? yawMagToInputs.at(yaw).end() : std::prev(upper);
			float magDistance = std::numeric_limits<float>::infinity();
			if (upper == yawMagToInputs.at(yaw).end())
			{
				magDistance = intendedMag - (*lower).first;
				if (magDistance < closestMagDistance)
				{
					closestMagDistance = magDistance;
					closestInput = (*lower).second;
				}
			}
			else if (lower == yawMagToInputs.at(yaw).end())
			{
				magDistance = (*upper).first - intendedMag;
				if (magDistance < closestMagDistance)
				{
					closestMagDistance = magDistance;
					closestInput = (*upper).second;
				}
			}
			else
			{
				float lowerMagDistance = intendedMag - (*lower).first;
				float upperMagDistance = (*upper).first - intendedMag;

				if (lowerMagDistance <= upperMagDistance)
				{
					if (lowerMagDistance < closestMagDistance)
					{
						closestMagDistance = lowerMagDistance;
						closestInput = (*lower).second;
					}
				}
				else
				{
					if (upperMagDistance < closestMagDistance)
					{
						closestMagDistance = upperMagDistance;
						closestInput = (*upper).second;
					}
				}
			}
		}

		// Check both positive and negative HAU offsets for the closest mag if
		// either of them finds a matching yaw
		if (bias == Rotation::NONE)
		{
			if (offset == 0)
			{
				if (foundMatchingYaw)
					break;
				offset = 1;
			}
			else if (offset > 0)
				offset *= -1;
			else if (foundMatchingYaw)
				break;
			else
				offset = -offset + 1;
		}
		else if (bias == Rotation::COUNTERCLOCKWISE)
		{
			if (foundMatchingYaw)
				break;
			offset++;
		}
		else
		{
			if (foundMatchingYaw)
				break;
			offset--;
		}
	}

	return closestInput;
}

std::pair<int16_t, float> Inputs::GetIntendedYawMagFromInput(
	int8_t stickX, int8_t stickY, int16_t cameraYaw)
{
	int16_t baseIntendedYaw = inputsToYawMag.at(stickX).at(stickY).first;
	float intendedMag = inputsToYawMag.at(stickX).at(stickY).second;

	return {baseIntendedYaw + cameraYaw, intendedMag};
}

bool Inputs::HauEquals(int16_t angle1, int16_t angle2)
{
	int16_t hau1 = angle1 - (angle1 & 15);
	int16_t hau2 = angle2 - (angle2 & 15);

	return hau1 == hau2;
}
