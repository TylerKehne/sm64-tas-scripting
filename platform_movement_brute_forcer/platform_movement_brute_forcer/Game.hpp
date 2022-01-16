#include <windows.h>
#include <iostream>
#include <string.h>
#include <libloaderapi.h>
#include <vector>

#include "Magic.hpp"
#include "Slot.hpp"
#include "Inputs.hpp"

using namespace std;

#ifndef GAME_H
#define GAME_H

#pragma once
class Game
{
public:
	string version;
	HMODULE dll;
	vector<SegVal> segment;
	Slot startSave;

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

	uint32_t advance_frame(bool returnFrame = false);
	Slot alloc_slot();
	void save_state(Slot* slot);
	void load_state(Slot* slot);
	intptr_t addr(const char* symbol);
	uint32_t getCurrentFrame();
};

extern Game game;

#endif