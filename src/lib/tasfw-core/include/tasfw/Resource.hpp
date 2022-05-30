#pragma once

#include <cstring>
#include <iostream>
#include <vector>

#include <tasfw/Inputs.hpp>
#include <tasfw/SharedLib.hpp>

#include <cstdlib>
#include <chrono>

#include <tasfw/Script.hpp>

#ifndef RESOURCE_H
#define RESOURCE_H

template <class TState>
class Resource;

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

template <derived_from_specialization_of<Resource> TResource>
class SlotHandle
{
public:
	TResource* resource = NULL;
	int64_t slotId = -1;

	SlotHandle(TResource* resource, int64_t slotId) : resource(resource), slotId(slotId) { }

	SlotHandle(SlotHandle<TResource>&&) = default;
	SlotHandle<TResource>& operator = (SlotHandle<TResource>&&) = default;

	SlotHandle(const SlotHandle<TResource>&) = delete;
	SlotHandle<TResource>& operator= (const SlotHandle<TResource>&) = delete;

	~SlotHandle();

	bool isValid();
};

/*
template <class TState>
class Slot
{
public:
	TState state;
	//std::vector<uint8_t> buf1;
	//std::vector<uint8_t> buf2;

	Slot() = default;

	Slot() { }
};
*/

template <class TState>
class SlotManager
{
public:
	Resource<TState>* _resource = NULL;
	std::map<int64_t, TState> slotsById;
	std::map<int64_t, int64_t> slotIdsByLastAccess;
	std::map<int64_t, int64_t> slotLastAccessOrderById;
	int64_t nextSlotId = 1; //slot IDs are unique

	int64_t _saveMemLimit = 0;
	int64_t _currentSaveMem = 0;

	SlotManager(Resource<TState>* resource) : _resource(resource) { }

	int64_t CreateSlot();
	void EraseOldestSlot();
	void EraseSlot(int64_t slotId);
	void LoadSlot(int64_t slotId);
	bool isValid(int64_t slotId);
};

template <class TState>
class Resource
{
public:
	uint64_t _totalFrameAdvanceTime = 0;
	uint64_t _totalLoadStateTime = 0;
	uint64_t _totalSaveStateTime = 0;
	uint64_t nFrameAdvances = 0;
	uint64_t nLoadStates = 0;
	uint64_t nSaveStates = 0;

	TState startSave = TState();
	SlotManager<TState> slotManager = SlotManager<TState>(this);

	Resource() = default;

	Resource(const Resource<TState>&) = delete;
	Resource& operator= (const Resource<TState>&) = delete;

	int64_t SaveState();
	void LoadState(int64_t slotId);
	void FrameAdvance();
	bool shouldSave(int64_t framesSinceLastSave) const;
	bool shouldLoad(int64_t framesAhead) const;

	virtual void save(TState& state) = 0;
	virtual void load(TState& state) = 0;
	virtual void advance() = 0;
	virtual void* addr(const char* symbol) = 0;
	virtual std::size_t getStateSize(const TState& state) = 0;
	//TODO: make this resource-agnostic
	virtual uint32_t getCurrentFrame() = 0;
};

class LibSm64Mem
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
};

class LibSm64 : public Resource<LibSm64Mem>
{
public:
	SharedLib dll;
	std::vector<SegVal> segment;

	LibSm64(const std::filesystem::path& dllPath) : dll(dllPath)
	{
		slotManager._saveMemLimit = 1024 * 1024 * 1024; //1 GB

		// constructor of SharedLib will throw if it can't load
		void* processID = dll.get("sm64_init");

		// Macro evalutes to nothing on Linux and __stdcall on Windows
		// looks cleaner
		using pICFUNC = int(TAS_FW_STDCALL*)();

		pICFUNC sm64_init = pICFUNC(processID);

		sm64_init();

		auto sections = dll.readSections();
		segment = std::vector<SegVal>
		{
			SegVal {".data", sections[".data"].address, sections[".data"].length},
			SegVal {".bss", sections[".bss"].address, sections[".bss"].length},
		};

		//Initial save
		save(startSave);
	}

	void save(LibSm64Mem& state)
	{
		state.buf1.resize(segment[0].length);
		state.buf2.resize(segment[1].length);

		int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
		memcpy(state.buf1.data(), temp, segment[0].length);

		temp = reinterpret_cast<int64_t*>(segment[1].address);
		memcpy(state.buf2.data(), temp, segment[1].length);
	}

	void load(LibSm64Mem& state)
	{
		memcpy(segment[0].address, state.buf1.data(), segment[0].length);
		memcpy(segment[1].address, state.buf2.data(), segment[1].length);
	}

	void advance()
	{
		void* processID = dll.get("sm64_update");

		using pICFUNC = void(TAS_FW_STDCALL*)();

		pICFUNC sm64_update;
		sm64_update = pICFUNC(processID);

		sm64_update();
	}

	void* addr(const char* symbol)
	{
		return dll.get(symbol);
	}

	std::size_t getStateSize(const LibSm64Mem& state)
	{
		return state.buf1.size() + state.buf2.size();
	}

	uint32_t getCurrentFrame()
	{
		return *(uint32_t*)(addr("gGlobalTimer")) - 1;
	}
};

//Include template method implementations
#include "tasfw/Resource.t.hpp"

#endif