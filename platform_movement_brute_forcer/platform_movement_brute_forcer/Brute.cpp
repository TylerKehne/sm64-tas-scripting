#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <set>

#include "Brute.hpp"
#include "Game.hpp"
#include "Macros.hpp"
#include "Movement.hpp"
#include "Trig.hpp"
#include "Magic.hpp"
#include "Inputs.hpp"

void calc_next_node(bool isLeaf, Slot* saveState, bool recurse, int16_t startIndex, Slot* saveStateTemp, Slot* saveStateNext) {
	//favor angles facing the real universe
	game.load_state(saveState);
	float fyaw_to_main_uni = atan2(*marioX(game), *marioZ(game)) * 32768 / M_PI;
	fyaw_to_main_uni = fyaw_to_main_uni - fmodf(fyaw_to_main_uni, 16);

	int16_t hau_index = startIndex;
	int hau_offset_sign = 1;

	/*
	printf("marioX - %.8f, marioX^2 - %.8f\n", *marioX(game), pow(*marioX(game), 2));
	printf("marioY - %.8f, marioY^2 - %.8f\n", *marioY(game), pow(*marioY(game), 2));
	printf("marioZ - %.8f, marioZ^2 - %.8f\n", *marioZ(game), pow(*marioZ(game), 2));
	*/

	if (isLeaf == false) {
		printf("starting dist: %.8f\n", sqrt(pow(*marioX(game), 2) + pow(*marioY(game), 2) + pow(*marioZ(game), 2)));
		printf("starting yaw: %.1f\n", fyaw_to_main_uni - fmodf(fyaw_to_main_uni, 16));
	}

	map<pair<int8_t, int8_t>, pair<int16_t, float>> input_yawmag_map;
	bool crouchslide_returned_to_main_uni = false;
	bool distance_precheck_occurred = false;

	while (true) {
		int16_t fYaw = int(fyaw_to_main_uni) + 16 * hau_index * hau_offset_sign;


		if (isLeaf == false) {
			printf("updating fYaw to %d (hau index: %d)\n", fYaw, hau_index);
		}

		/* Get initial state variables now so we always have access to them. This reduces how often we need to load state */
		game.load_state(saveState);
		float hSpd = *marioHSpd(game);
		float x = *marioX(game);
		float y = *marioY(game);
		float z = *marioZ(game);
		float* marioFloorNormalX = (float*)(*marioFloorPtr(game) + 0x1C);
		float floorNormalX = *marioFloorNormalX;
		float* marioFloorNormalY = (float*)(*marioFloorPtr(game) + 0x20);
		float floorNormalY = *marioFloorNormalY;
		float* marioFloorNormalZ = (float*)(*marioFloorPtr(game) + 0x24);
		float floorNormalZ = *marioFloorNormalZ;
		int16_t* marioFloorType = (int16_t*)(*marioFloorPtr(game) + 0x0);
		int16_t floorType = *marioFloorType;
		uint64_t* marioAreaCameraPtr = (uint64_t*)(*marioAreaPtr(game) + 0x48);
		int16_t* marioAreaCameraYaw = (int16_t*)(*marioAreaCameraPtr + 0x2);
		int16_t camYaw = *marioAreaCameraYaw;

		//check if Mario moves directly into freefall
		bool is_freefall_angle = false;
		*marioFYaw(game) = fYaw;
		set_inputs(game, Inputs(0b0000000000010000, 0, 0));
		game.advance_frame();

		if (*marioAction(game) == 0x0100088C) { //freefall
			for (int dust_frames = 0; dust_frames < 4; dust_frames++) {
				is_freefall_angle = check_freefall_outcome(0, 0, fYaw, false, isLeaf, recurse, saveStateTemp, saveStateNext, &crouchslide_returned_to_main_uni, dust_frames,
					[](bool isLeaf, Slot* s) { calc_next_node(isLeaf, s); });
			}
		}

		/* For leaf nodes, check to see if the crouchslide distance can even possibly return to the main universe */
		if (isLeaf) {
			if (!distance_precheck_occurred && distance_precheck(x, y, z, hSpd, marioFloorNormalY, marioFloorType) == false) {
				printf("L2 node failed crouchslide distance precheck.\n");
				return;
			}

			distance_precheck_occurred = true;
		}
		

		if (is_freefall_angle == false) {
			set<pair<int16_t, float>> yawmags_tested;

			for (int16_t input_x = -128; input_x < 128; input_x++) {
				for (int16_t input_y = -128; input_y < 128; input_y++) {
					//avoid testing redundant inputs
					if (input_yawmag_map.count(pair<int8_t, int8_t>(input_x, input_y)) == 1 && yawmags_tested.count(input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)]) == 1) {
						continue;
					}

					//calculate intended input
					*marioAreaCameraYaw = camYaw;

					if (input_yawmag_map.count(pair<int8_t, int8_t>(input_x, input_y)) == 0) {
						calc_intended_yawmag(input_x, input_y, *marioAreaCameraYaw);
						int16_t intYaw = *marioIntYaw(game) - (*marioIntYaw(game) % 16);
						input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)] = pair<int16_t, float>(intYaw, *marioIntMag(game));

						if (yawmags_tested.count(input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)]) == 1) {
							continue;
						}
					}
					else {
						*marioIntYaw(game) = int(input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)].first);
						*marioIntMag(game) = float(input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)].second);
					}

					yawmags_tested.insert(pair<int16_t, float>(*marioIntYaw(game), *marioIntMag(game)));

					/* input inbounds precheck */
					update_mario_state(x, y, z, hSpd, fYaw);
					if (input_precheck(floorNormalX, floorNormalY, floorNormalZ, floorType) == false) {
						continue;
					}

					/* If the precheck passes, test crouchslide */
					game.load_state(saveState);
					*marioFYaw(game) = fYaw;
					*marioMYaw(game) = fYaw;
					*marioXSlidSpd(game) = *marioHSpd(game) * gSineTable[(uint16_t)(*marioFYaw(game)) >> 4];
					*marioZSlidSpd(game) = *marioHSpd(game) * gCosineTable[(uint16_t)(*marioFYaw(game)) >> 4];

					set_inputs(game, Inputs(0b0010000000010000, input_x, input_y)); /* Z and R buttons with joystick input */
					game.advance_frame();

					//printf("%d %d\n", *marioFYaw(game) + 32768, *marioMYaw(game));
					//printf("Step 1 Actual: %.8f %.8f %d %d\n", *marioX(game), *marioZ(game), *marioFYaw(game), *marioMYaw(game));

					/* Check if Mario bonks in crouchslide. If so, this input won't work. */
					if (abs(*marioHSpd(game)) < 1000.0) {
						continue;
					}
					else if (*marioAction(game) == 0x0100088C) {
						/*
						* if (input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)] != pair<int16_t, float>(*marioIntYaw(game), *marioIntMag(game))) {
						*     printf("(%d, %.8f) (%d, %.8f)\n", input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)].first, input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)].second, *marioIntYaw(game), *marioIntMag(game));
						*     printf("%#X %#X\n", input_yawmag_map[pair<int8_t, int8_t>(input_x, input_y)].first - 1187, *marioIntYaw(game) - 1187);
						*     printf("%d %d\n", input_x, input_y);
						* }
						*/

						for (int dust_frames = 0; dust_frames < 4; dust_frames++) {
							check_freefall_outcome(input_x, input_y, fYaw, true, isLeaf, recurse, saveStateTemp, saveStateNext, &crouchslide_returned_to_main_uni, dust_frames,
								[](bool isLeaf, Slot* s) { calc_next_node(true, s); });
						}
						
					}
				}
			}
			
		}

		if (isLeaf == false) {
			printf("tested all inputs for fYaw %d\n", fYaw);
		}

		if (hau_offset_sign == 1 && hau_index != 0) {
			hau_offset_sign = -1;
		} else {
			hau_offset_sign = 1;
			hau_index += 1;
		}

		int max_hau_index = 3;
		if (crouchslide_returned_to_main_uni == true) {
			max_hau_index = 30;
		}
		if (isLeaf == true && hau_index > max_hau_index) {
			printf("Tested L2 node.\n");
			return;
		}

		/* End when all HAUs have been tested */
		if (hau_index > 2048) {
			break;
		}
	}

	printf("fin\n");
	return;
}

bool distance_precheck(float x, float y, float z, float hSpd, float* marioFloorNormalY, int16_t* marioFloorType)
{
	float lossFactorMin;
	float lossFactorMax;
	float dist_to_main_uni = sqrt(pow(x, 2) + pow(z, 2));
	if (*marioFloorType == 0x15) {
		lossFactorMin = 0.89;
		lossFactorMax = 0.94;
	}
	else if (*marioFloorType == 0x13) {
		lossFactorMin = 0.95;
		lossFactorMax = 1.0;
	}
	else {
		lossFactorMin = 0.89;
		lossFactorMax = 0.94;
	}

	float crouchSlideDistMin[4];
	float crouchSlideDistMax[4];

	/* Check if crouchslide qfs could reach the main universe based on distance, or if they overshoot */
	for (int qf = 0; qf < 4; qf++) {
		crouchSlideDistMin[qf] = abs(hSpd) * lossFactorMin * *marioFloorNormalY * (qf + 1) / 4.0f;
		crouchSlideDistMax[qf] = abs(hSpd) * lossFactorMax * *marioFloorNormalY * (qf + 1) / 4.0f;

		if (crouchSlideDistMin[qf] <= dist_to_main_uni && crouchSlideDistMax[qf] >= dist_to_main_uni) {
			return true;
		}
	}

	/* If a full frame of crouchsliding won't reach the main map, proceed with freefall frames. */
	for (int crouchslideQf = 0; crouchslideQf < 4; crouchslideQf++) {
		for (int qf = 1; qf > 0; qf++) {
			float distMin = crouchSlideDistMin[crouchslideQf] + abs(hSpd) * lossFactorMin * qf / 4.0f;
			float distMax = crouchSlideDistMax[crouchslideQf] + abs(hSpd) * lossFactorMax * qf / 4.0f;

			if (distMin <= dist_to_main_uni && distMax >= dist_to_main_uni) {
				return true;
			}
			else if (distMin >= dist_to_main_uni && distMax >= dist_to_main_uni) {
				break;
			}
		}
	}

	return false;
}

bool input_precheck(float floorNormalX, float floorNormalY, float floorNormalZ, int16_t floorType)
{
	update_sliding(floorNormalX, floorNormalY, floorNormalZ);
	*marioX(game) = float(*marioX(game) + *marioXSlidSpd(game) * floorNormalY / 4.0);
	*marioZ(game) = float(*marioZ(game) + *marioZSlidSpd(game) * floorNormalY / 4.0);

	if (check_if_inbounds(*marioX(game), *marioZ(game)) == true) {
		//printf("Step 1 Predicted: %.9f %.9f %d %d\n", *marioX(game), *marioZ(game), *marioFYaw(game), *marioMYaw(game));

		*marioX(game) = float(*marioX(game) + *marioHSpd(game) * gSineTable[(uint16_t)(*marioFYaw(game)) >> 4] / 4.0);
		*marioZ(game) = float(*marioZ(game) + *marioHSpd(game) * gCosineTable[(uint16_t)(*marioFYaw(game)) >> 4] / 4.0);

		if (check_if_inbounds(*marioX(game), *marioZ(game)) == false) {
			return false;
		}
		//printf("Step 2 Predicted: %.9f %.9f %d %d\n", *marioX(game), *marioZ(game), *marioFYaw(game), *marioMYaw(game));
	}
	else {
		return false;
	}

	return true;
}

void update_mario_state(float x, float y, float z, float hSpd, int16_t fYaw)
{
	*marioFYaw(game) = fYaw;
	*marioMYaw(game) = fYaw;
	*marioHSpd(game) = hSpd;
	*marioXSlidSpd(game) = *marioHSpd(game) * gSineTable[(uint16_t)(*marioFYaw(game)) >> 4];
	*marioZSlidSpd(game) = *marioHSpd(game) * gCosineTable[(uint16_t)(*marioFYaw(game)) >> 4];
	*marioX(game) = x;
	*marioY(game) = y;
	*marioZ(game) = z;
}

bool check_if_pos_in_main_universe(float x, float z, bool input_matters, bool* crouchslide_returned_to_main_uni, int16_t fYaw, int16_t input_x, int16_t input_y)
{
	if (abs(x) < 10000.0f && abs(z) < 10000.0f) {
		if (input_matters) {
			printf("FRAME ENDED IN MAIN UNIVERSE: %.9f %.9f %.9f %d %d %d\n", *marioX(game), *marioY(game), *marioZ(game), fYaw, input_x, input_y);
			*crouchslide_returned_to_main_uni = true;
		}
		else {
			printf("FRAME ENDED IN MAIN UNIVERSE: %.9f %.9f %.9f %d\n", *marioX(game), *marioY(game), *marioZ(game), fYaw);
		}

		return true;
	}

	return false;
}

bool check_freefall_outcome(
	int16_t input_x,
	int16_t input_y,
	int16_t fYaw,
	bool input_matters,
	bool isLeaf,
	bool recurse,
	Slot* saveStateTemp,
	Slot* saveStateNext,
	bool* crouchslide_returned_to_main_uni,
	int dust_frames,
	std::function<void(bool, Slot*)> execute_recursion)
{
	/* If the frame limit is reached, something fluky happened like a pedro spot. Move on in this case */
	int frame;
	for (frame = 0; frame < 300; frame++) {
		set_inputs(game, Inputs(0b0000000000010000, 0, 0)); /* R button only, no joystick input */
		game.advance_frame();

		/* THIS IS ULTIMATELY WHAT WE ARE LOOKING FOR */
		check_if_pos_in_main_universe(*marioX(game), *marioZ(game), input_matters, crouchslide_returned_to_main_uni, fYaw, input_x, input_y);
		/* Continue execution regardless of whether the main universe was entered */

		/* This basically only happens if MAaio hits something and his speed zeroes. No need to keep checking if this happens. */
		if (abs(*marioHSpd(game)) < 1000.0) {
			break;
		}

		/* If Mario lands, this position is a candidate for a next level node, depending on what happens after. */
		if (*marioAction(game) == 0x04000471) { //freefall land
			/* Check to see if landing spot is stable or if Mario continues moving into the air, a slope etc. */
			for (int i = 0; i < 6; i++) {
				if (i < dust_frames) {
					set_inputs(game, Inputs(0b0000000000010000, 127, 127)); /* R button and joystick input for dust (cuts speed by 2% on flat surfaces) */
				}
				else {
					set_inputs(game, Inputs(0b0000000000010000, 0, 0)); /* R button only, no joystick input */
				}
				
				game.advance_frame();

				/* This is highly unlikely but it could happen due to changing speed, might as well check */
				if (check_if_pos_in_main_universe(*marioX(game), *marioZ(game), input_matters, crouchslide_returned_to_main_uni, fYaw, input_x, input_y)) {
					printf("Dust frames required: %d\n", dust_frames);
				}

				*marioActionTimer(game) = 1; /* don't let Mario leave this action and lose his speed */
				//printf("0x%.8X %.9f \n", *marioAction(game), *marioHSpd(game));
			}

			if (*marioAction(game) == 0x04000471 && abs(*marioHSpd(game)) > 1000.0f) { /* freefall land */
				if (isLeaf == false) {
					float mewFYawToMainUni = float(atan2(*marioX(game), *marioZ(game)) * 32768.0 / M_PI);
					mewFYawToMainUni = mewFYawToMainUni - fmodf(mewFYawToMainUni, 16);

					if (input_matters) {
						printf("Found new node: %.9f %d %d %d\n", *marioY(game), fYaw, input_x, input_y);
					}
					else {
						printf("Found new node: %.9f %d\n", *marioY(game), fYaw);
					}
					
					printf("Distance to main universe: %.9f\n", sqrt(pow(*marioX(game), 2) + pow(*marioZ(game), 2)));
					printf("Yaw to main universe: %.9f\n", mewFYawToMainUni);

					if (recurse == true) {
						/* test if this node will return to main map */
						game.save_state(saveStateTemp);

						/* allow camera yaw to stabilize, otherwise the solution is unlikely to validate */
						for (int i = 0; i < 10; i++) {
							set_inputs(game, Inputs(0b0000000000010000, 0, 0)); /* R button only, no joystick input */
							*marioActionTimer(game) = 1; /* don't let Mario leave this action and lose his speed */
							game.advance_frame();
						}

						*marioAction(game) = 0x04000440; /* walking */
						game.save_state(saveStateNext);
						printf("Testing L2 node with %d dust frames\n", dust_frames);
						execute_recursion(isLeaf, saveStateNext);
						game.load_state(saveStateTemp);
						return true;
					}
				}
			}

			break;
		}
	}

	if (frame == 300) {
		printf("Something weird happened: %.9f %.9f %.9f %.9f %d\n", *marioX(game), *marioY(game), *marioZ(game), *marioHSpd(game), fYaw);
	}

	return false;
}