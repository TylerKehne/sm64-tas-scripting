#pragma once

#include <cstring>
#include <iostream>
#include <vector>

#include <tasfw/Inputs.hpp>
#include <tasfw/SharedLib.hpp>
#include <tasfw/Script.hpp>

#ifndef GAME_H
	#define GAME_H

// structs like this can be aggregate-initialized
// like SegVal {".data", 0xDEADBEEF, 12345678};
struct SegVal
{
	std::string name;
	void* address;
	size_t length;

	static SegVal fromSectionData(const std::string& name, SectionInfo info)
	{
		return {name, info.address, info.length};
	}
};

class Game;
class Script;

class Slot
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
	Game* game = nullptr;
	Script* script = nullptr;
	int64_t frame = -1;

	Slot() = default;

	Slot(Game* game, Script* script, int64_t frame) : game(game), script(script), frame(frame) { }

	~Slot();
};

class SlotHandle
{
public:
	Game* game = NULL;
	int64_t slotId = -1;

	SlotHandle(Game* game, int64_t slotId) : game(game), slotId(slotId) { }

	~SlotHandle();
};

class SlotManager
{
private:
	friend class Game;
	friend class SlotHandle;

	Game* _game = NULL;
	std::map<int64_t, Slot> slotsById;
	std::map<int64_t, int64_t> slotIdsByLastAccess;
	std::map<int64_t, int64_t> slotLastAccessOrderById;

	SlotManager(Game* game) : _game(game) { }

	int64_t CreateSlot(Script* script, int64_t frame);
	void EraseOldestSlot();
	void EraseSlot(int64_t slotId);
	void UpdateSlot(int64_t slotId);
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

	SharedLib dll;
	std::vector<SegVal> segment;
	Slot startSave = Slot();
	SlotHandle startSaveHandle = SlotHandle(this, -1);
	SlotManager slotManager = SlotManager(this);

	Game(const std::filesystem::path& dllPath) : dll(dllPath)
	{
		// constructor of SharedLib will throw if it can't load
		void* processID = dll.get("sm64_init");

		// Macro evalutes to nothing on Linux and __stdcall on Windows
		// looks cleaner
		using pICFUNC = int(TAS_FW_STDCALL*)();

		pICFUNC sm64_init = pICFUNC(processID);

		sm64_init();

		auto sects = dll.readSections();
		segment		 = std::vector<SegVal> {
			 SegVal {".data", sects[".data"].address, sects[".data"].length},
			 SegVal {".bss", sects[".bss"].address, sects[".bss"].length},
		 };

		save_state_initial();
	}

	void advance_frame();
	void save_state_initial();
	int64_t save_state(Script* script, int64_t frame);
	void load_state(int64_t slotId);
	void* addr(const char* symbol);
	uint32_t getCurrentFrame();
	bool shouldSave(int64_t framesSinceLastSave) const;
	bool shouldLoad(int64_t framesAhead) const;

private:
	friend class Slot;
	friend class SlotManager;

	const int64_t _saveMemLimit = 1024 * 1024 * 1024;	 // 1 GB
	int64_t _currentSaveMem = 0;
};

#endif