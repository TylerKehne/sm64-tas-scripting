#pragma once

#include <windows.h>
#include <iostream>
#include <string.h>
#include <libloaderapi.h>
#include <vector>

#include "Magic.hpp"
#include "Slot.hpp"
#include "Inputs.hpp"

#ifndef GAME_H
#define GAME_H

using namespace std;

class Game
{
public:
	double _avgFrameAdvanceTime = 0;
	double _avgLoadStateTime = 0;
	double _avgSaveStateTime = 0;
	double _avgAllocSlotTime = 0;
	uint64_t nFrameAdvances = 0;
	uint64_t nLoadStates = 0;
	uint64_t nSaveStates = 0;
	uint64_t nAllocSlots = 0;

	std::string version;
	HMODULE dll;
	std::vector<SegVal> segment;
	Slot startSave = Slot();

	Game(string vers, const char* dll_path) {
		version = vers;
		dll = LoadLibraryA(dll_path);

		if (dll) {
			FARPROC processID = GetProcAddress(dll, "sm64_init");

			typedef int(__stdcall* pICFUNC)();

			pICFUNC sm64_init;
			sm64_init = pICFUNC(processID);

			sm64_init();
		}
		else {
			cerr << "Bad dll!" << endl;
			exit(EXIT_FAILURE);
		}

		map<string, vector<SegVal>> SEGMENTS = {
			{ "us", { SegVal(".data", 1302528, 2836576),
				SegVal(".bss", 14045184, 4897408) }
			}, { "jp", { SegVal(".data", 1294336, 2406112),
				SegVal(".bss", 13594624, 4897408) }
			}
		};

		segment = SEGMENTS.at(version);

		startSave = alloc_slot();
		save_state(&startSave);
	}

	void advance_frame();
	Slot alloc_slot();
	void save_state(Slot* slot);
	void load_state(Slot* slot);
	intptr_t addr(const char* symbol);
	uint32_t getCurrentFrame();
	bool shouldSave(uint64_t framesSinceLastSave);
};



#endif