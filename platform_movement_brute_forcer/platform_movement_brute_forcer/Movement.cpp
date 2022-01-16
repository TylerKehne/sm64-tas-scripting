#include <cstdint>

#include "Movement.hpp"
#include "Macros.hpp"
#include "Trig.hpp"
#include "Game.hpp"
#include "Magic.hpp"

void set_inputs(Game game, Inputs input) {
	uint16_t* buttonDllAddr = (uint16_t*)game.addr("gControllerPads");
	buttonDllAddr[0] = input.buttons;

	int8_t* xStickDllAddr = (int8_t*)(game.addr("gControllerPads") + 2);
	xStickDllAddr[0] = input.stick_x;

	int8_t* yStickDllAddr = (int8_t*)(game.addr("gControllerPads") + 3);
	yStickDllAddr[0] = input.stick_y;
}

void update_sliding_angle(float accel, float lossFactor, float normalX, float normalZ) {
	int32_t newFacingDYaw = 0;
	int16_t facingDYaw = 0;

	int16_t slopeAngle = atan2s(normalZ, normalX);
	float steepness = sqrt(pow(normalX, 2) + pow(normalZ, 2));

	*marioXSlidSpd(game) = float(*marioXSlidSpd(game) + accel * steepness * gSineTable[uint16_t(slopeAngle) >> 4]);
	*marioZSlidSpd(game) = float(*marioZSlidSpd(game) + accel * steepness * gCosineTable[uint16_t(slopeAngle) >> 4]);

	*marioXSlidSpd(game) = float(*marioXSlidSpd(game) * lossFactor);
	*marioZSlidSpd(game) = float(*marioZSlidSpd(game) * lossFactor);

	*marioMYaw(game) = atan2s(*marioZSlidSpd(game), *marioXSlidSpd(game));

	facingDYaw = ((*marioFYaw(game) - *marioMYaw(game) + 32768) % 65536) - 32768;
	newFacingDYaw = facingDYaw;

	if (newFacingDYaw > 0 && newFacingDYaw <= 0x4000) {
		newFacingDYaw -= 0x0200;

		if (newFacingDYaw < 0) {
			newFacingDYaw = 0;
		}
	}
	else if (newFacingDYaw > -0x4000 && newFacingDYaw < 0) {
		newFacingDYaw += 0x200;

		if (newFacingDYaw > 0) {
			newFacingDYaw = 0;
		}
	}
	else if (newFacingDYaw > 0x4000 && newFacingDYaw < 0x8000) {
		newFacingDYaw += 0x200;

		if (newFacingDYaw > 0x8000) {
			newFacingDYaw = 0x8000;
		}
	}
	else if (newFacingDYaw > -0x8000 && newFacingDYaw < -0x4000) {
		newFacingDYaw -= 0x200;

		if (newFacingDYaw < -0x8000) {
			newFacingDYaw = -0x8000;
		}
	}

	*marioFYaw(game) = *marioMYaw(game) + newFacingDYaw;

	*marioHSpd(game) = sqrt(pow(*marioXSlidSpd(game), 2) + pow(*marioZSlidSpd(game), 2));

	if (newFacingDYaw < -0x4000 || newFacingDYaw > 0x4000) {
		*marioHSpd(game) = float(*marioHSpd(game) * -1.0);
	}
}

void update_sliding(float normalX, float normalZ, int16_t floorType) {
	float lossFactor = 0.0;
	float accel = 0.0;
	float oldSpeed = 0.0;
	float newSpeed = 0.0;

	uint16_t intendedDYaw = (*marioIntYaw(game) - *marioMYaw(game)) % 65536;
	//printf("update_sliding\n%d %d %d\n", *marioIntYaw(game), *marioMYaw(game), intendedDYaw);
	float forward;
	float sideward;

	forward = gCosineTable[intendedDYaw >> 4];
	sideward = gSineTable[intendedDYaw >> 4];

	if (forward < 0.0 && *marioHSpd(game) >= 0.0) {
		forward = float(forward * 0.5 + 0.5 * *marioHSpd(game) / 100.0);
	}

	if (floorType == 0x15) {
		accel = 5.0f;
		lossFactor = float(*marioIntMag(game) / 32.0 * forward * 0.02 + 0.92);
	}
	else if (floorType == 0x13) {
		accel = 10.0f;
		lossFactor = float(*marioIntMag(game) / 32.0 * forward * 0.02 + 0.98);
	}
	else {
		accel = 7.0f;
		lossFactor = float(*marioIntMag(game) / 32.0 * forward * 0.02 + 0.92);
	}

	oldSpeed = sqrt(pow(*marioXSlidSpd(game), 2) + pow(*marioZSlidSpd(game), 2));

	*marioXSlidSpd(game) = *marioXSlidSpd(game) + *marioZSlidSpd(game) * (*marioIntMag(game) / 32.0) * sideward * 0.05;
	*marioZSlidSpd(game) = *marioZSlidSpd(game) - *marioXSlidSpd(game) * (*marioIntMag(game) / 32.0) * sideward * 0.05;

	newSpeed = sqrt(pow(*marioXSlidSpd(game), 2) + pow(*marioZSlidSpd(game), 2));

	if (oldSpeed > 0.0 && newSpeed > 0.0) {
		*marioXSlidSpd(game) = *marioXSlidSpd(game) * float(oldSpeed / newSpeed);
		*marioZSlidSpd(game) = *marioZSlidSpd(game) * float(oldSpeed / newSpeed);
	}

	update_sliding_angle(accel, lossFactor, normalX, normalZ);
}

void calc_intended_yawmag(int8_t stickX, int8_t stickY, int16_t camYaw) {
	float intStickX = 0;
	float intStickY = 0;

	//printf("%d, %d, %d\n", stickX, stickY, camYaw);

	if (stickX <= -8) {
		intStickX = stickX + 6;
	}
	else if (stickX >= 8) {
		intStickX = stickX - 6;
	}

	if (stickY <= -8) {
		intStickY = stickY + 6;
	}
	else if (stickY >= 8) {
		intStickY = stickY - 6;
	}

	float stickMag = sqrt(pow(intStickX, 2) + pow(intStickY, 2));

	if (stickMag > 64.0) {
		intStickX = float(intStickX * 64.0 / stickMag);
		intStickY = float(intStickY * 64.0 / stickMag);
		stickMag = 64.0;
	}

	stickMag = float(((stickMag / 64.0) * (stickMag / 64.0)) * 64.0);
	*marioIntMag(game) = float(stickMag / 2.0);

	if (*marioIntMag(game) > 0.0) {
		*marioIntYaw(game) = atan2s(-intStickY, intStickX) + camYaw;
	}
	else {
		*marioIntYaw(game) = *marioFYaw(game);
	}
}