#include "tasfw/resources/LibSm64.hpp"

LibSm64::LibSm64(const std::filesystem::path& dllPath) : dll(dllPath)
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
}

void LibSm64::save(LibSm64Mem& state) const
{
	state.buf1.resize(segment[0].length);
	state.buf2.resize(segment[1].length);

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(state.buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(state.buf2.data(), temp, segment[1].length);
}

void LibSm64::load(const LibSm64Mem& state)
{
	memcpy(segment[0].address, state.buf1.data(), segment[0].length);
	memcpy(segment[1].address, state.buf2.data(), segment[1].length);
}

void LibSm64::advance()
{
	void* processID = dll.get("sm64_update");

	using pICFUNC = void(TAS_FW_STDCALL*)();

	pICFUNC sm64_update;
	sm64_update = pICFUNC(processID);

	sm64_update();
}

void* LibSm64::addr(const char* symbol) const
{
	return dll.get(symbol);
}

std::size_t LibSm64::getStateSize(const LibSm64Mem& state) const
{
	return state.buf1.capacity() + state.buf2.capacity();
}

uint32_t LibSm64::getCurrentFrame() const
{
	return *(uint32_t*)(addr("gGlobalTimer")) - 1;
}