#include "Trig.hpp"


constexpr uint16_t atan2_lookup(float z, float x) {
	uint16_t angle = 0;

	if (x == 0) {
		angle = gArctanTable[0];
	}
	else {
		angle = gArctanTable[(int32_t)(z / x * 1024 + 0.5f)];
	}

	return angle;
}

int16_t atan2s(float z, float x) {
	int16_t angle = 0;

	if (x >= 0) {
		if (z >= 0) {
			if (z >= x) {
				angle = atan2_lookup(x, z);
			}
			else {
				angle = 0x4000 - atan2_lookup(z, x);
			}
		}
		else {
			z = -z;

			if (z < x) {
				angle = 0x4000 + atan2_lookup(z, x);
			}
			else {
				angle = 0x8000 - atan2_lookup(x, z);
			}
		}
	}
	else {
		x = -x;

		if (z < 0) {
			z = -z;

			if (z >= x) {
				angle = 0x8000 + atan2_lookup(x, z);
			}
			else {
				angle = 0xC000 - atan2_lookup(z, x);
			}
		}
		else {
			if (z < x) {
				angle = 0xC000 + atan2_lookup(z, x);
			}
			else {
				angle = -atan2_lookup(x, z);
			}
		}
	}

	return ((angle + 32768) % 65536) - 32768;
}