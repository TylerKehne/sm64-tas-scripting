#include <winsock.h>
#include <iostream>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdint>

#include "Inputs.hpp"
#include "Magic.hpp"
#include "Game.hpp"
#include "Trig.hpp"
#include "Macros.hpp"
#include "Brute.hpp"
#include "Movement.hpp"
#include "Script.hpp"
#include "Types.hpp"
#include "Camera.hpp"

using namespace std;

#pragma comment(lib, "Ws2_32.lib")

Inputs MainScript::GetInputs(uint64_t frame)
{
	if (BaseStatus.m64Diff.frames.count(frame))
		return BaseStatus.m64Diff.frames[frame];
	else if (_m64->frames.count(frame))
		return _m64->frames[frame];

	return Inputs(0, 0, 0);
}

bool MainScript::verification()
{
	return true;
}

bool MainScript::execution()
{
	StartToFrame(3531);
	auto status = Script::Modify<BitFsPyramidOscillation_RunDownhill>();
	return true;
}

bool MainScript::validation()
{
	//Save m64Diff to M64
	for (auto& [frame, inputs] : BaseStatus.m64Diff.frames)
	{
		_m64->frames[frame] = inputs;
	}

	return true;
}

int main(int argc, char* argv[]) {
	M64 m64 = M64("C:\\Users\\Tyler\\Downloads\\TestWrite2.m64");
	m64.load();

	long scriptFrame = 3531;
	Slot scriiptSaveState = game.alloc_slot();
	/*
	do
	{
		M64Diff copyLastInput5Times = M64Diff(m64, m64.frames.size());
		long long baseSize = copyLastInput5Times.baseM64.frames.size();
		for (int i = 0; i < 5; i++)
		{
			copyLastInput5Times.frames.push_back(copyLastInput5Times.baseM64.frames[baseSize - 1]);
		}

		copyLastInput5Times.apply();
	}
	while (false);
	*/

	/*
	int frame = 0;
	for (frame = 0; frame < 3528; frame = game.advance_frame(true))
	{
		set_inputs(game, m64.frames[frame]);
	}
	*/

	//m64.frames[4000] = Inputs(Buttons::A | Buttons::Z | Buttons::R, 50, 30);
	//m64.save();

	Inputs::PopulateInputMappings();

	auto status = Script::Main<MainScript>(&m64);

	//auto status = Script::Modify<AdvanceM64FromStartToFrame>(&m64, 3531);
	//auto status2 = Script::Modify<BitFsPyramidOscillation_RunDownhill>(&m64);

	m64.save();

	return 0;
	//int16_t cameraYaw = Script::Execute<GetNextFrameCameraYaw>(m64).cameraYaw;
	/*
	Camera* camera = *(Camera**)(game.addr("gCamera"));
	int16_t cameraYaw = camera->yaw;
	auto inputs = Inputs::GetClosestInputByYawHau(11952, 16.0f, cameraYaw);

	m64.frames[3800] = Inputs(Buttons::A | Buttons::Z | Buttons::R, inputs.first, inputs.second);
	set_inputs(game, m64.frames[3800]);
	game.advance_frame();

	MarioState* marioState = *(MarioState**)(game.addr("gMarioState"));
	float intendedMag = marioState->intendedMag;
	int16_t intendedYaw = marioState->intendedYaw;

	m64.save();
	*/
	//int var1 = status.customStatus.var1;

	

	//cout << status.executed << endl;

	//m64.save();
	

	//Run through the m64
	/*
	for (int frame = 0; frame < m64.size(); frame++) {
		//hacks to get m64 to sync
		if (frame == 3020) {
			*marioX(game) = -2250.1f;
			*marioZ(game) = -715;
		}

		if (frame == 3051) {
			*bullyX(game) = -2289.91f;
			*bullyY(game) = -2950.0;
			*bullyZ(game) = -731.34f;
			*bullyYaw1(game) = 16384;
			*bullyHSpd(game) = 75000000.0f;
		}

		if (frame == 3076) {
			*bullyX(game) = -2239.0;
			*bullyY(game) = -2950.0;
			*bullyZ(game) = -573.61f;
			*bullyYaw1(game) = 0;
			*bullyHSpd(game) = 50000000.0f;
		}


		if (frame == 3099) {
			*trackPlatAction(game) = 2;
			*trackPlatX(game) = -1331.77f;
		}


		if (frame == 3104) {
			*bullyX(game) = -1719.7f;
			*bullyY(game) = -2950.0;
			*bullyZ(game) = -461.0;
			*bullyYaw1(game) = -30000;
			*bullyHSpd(game) = 14381759.0f;
		}

		//Deactivating all of the objects but the bully, Mario, the
		//tilting pyramid platforms, and the track platform
		if (frame == 3110) {
			for (int obj = 0; obj < 108; obj++) {
				if (obj == 27 || obj == 89 || obj == 83 || obj == 84 || obj == 85) {
					continue;
				}

				//seems to be either 48 or 100 to deactivate. I think it's 180
				short* active_flag = (short*)(game.addr("gObjectPool") + (obj * 1392 + 180));
				*active_flag = *active_flag & 0xFFFE;
			}

		}

		if (frame == 3274) {
			game.save_state(&backup);
			break;
		}

		int16_t num_stars = *(int16_t*)(game.addr("gMarioStates") + 230);

		if (frame % 1000 == 0) {
			printf("Frame %05d stars %02d\n", frame, num_stars);
		}


		set_inputs(game, m64.at(frame));
		game.advance_frame();
	}

	std::fprintf(stderr, "starting brute force\n");

	calc_next_node(false, &backup, true, 0, &saveStateTemp, &saveStateNext);

	std::fprintf(stderr, "finished or crashed\n");

	return 0;
	*/
}
