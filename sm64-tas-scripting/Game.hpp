#pragma once

#include <windows.h>
#include <iostream>
#include <string.h>
#include <libloaderapi.h>
#include <vector>

#include "Inputs.hpp"

#ifndef GAME_H
#define GAME_H

using namespace std;

class SegVal {
public:
	string name;
	intptr_t virtual_address;
	int64_t virtual_size;

	SegVal(string nm, intptr_t virt_addr, int64_t virt_size) {
		name = nm;
		virtual_address = virt_addr;
		virtual_size = virt_size;
	}
};

class Game;

class Slot
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
	Game* game = NULL;

	Slot() {}

	~Slot();
};

class Game
{
public:
	uint64_t _totalFrameAdvanceTime = 0;
	uint64_t _totalLoadStateTime = 0;
	uint64_t _totalSaveStateTime = 0;
	uint64_t nFrameAdvances = 0;
	uint64_t nLoadStates = 0;
	uint64_t nSaveStates = 0;

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

		save_state(&startSave);
	}

	void advance_frame();
	bool save_state(Slot* slot);
	void load_state(Slot* slot);
	intptr_t addr(const char* symbol);
	uint32_t getCurrentFrame();
	bool shouldSave(uint64_t framesSinceLastSave);

private:
	friend class Slot;

	const int64_t _saveMemLimit = 1024 * 1024 * 1024; //1 GB
	int64_t _currentSaveMem = 0;
};



#endif