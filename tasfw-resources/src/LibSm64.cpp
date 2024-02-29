#include "LibSm64.hpp"

#if !defined(_WIN32)
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>

static void* align_pointer(void* ptr, intptr_t alignment) {
	intptr_t x = reinterpret_cast<uintptr_t>(ptr);
	if (x % alignment == 0) {
		return ptr;
	}
	intptr_t mask = alignment-1;
	x &= ~mask;
	return reinterpret_cast<void*>(x);
}

std::vector<uint8_t*> regions_of_interest;

static void handler(int sig, siginfo_t* si, void* unused)
{
	mprotect(
		align_pointer(si->si_addr, pagesize), pagesize,
		PROT_READ | PROT_EXEC | PROT_WRITE);
	regions_of_interest.push_back((uint8_t*)align_pointer(si->si_addr, pagesize));
	return;
}

#endif
LibSm64::LibSm64(const LibSm64Config& config) : config(config), dll(config.dllPath)
{
	slotManager._saveMemLimit = int64_t(8000) * 1024 * 1024; //8 GB

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
#if !defined(_WIN32)

	original_buf1.resize(segment[0].length);
	original_buf2.resize(segment[1].length);

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(original_buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(original_buf2.data(), temp, segment[1].length);

	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sigaction(SIGSEGV, &sa, NULL);

	mprotect(
		align_pointer(sections[".data"].address, pagesize),
		(sections[".data"].length & (~(pagesize - 1))) + pagesize,
		PROT_READ | PROT_EXEC);

	mprotect(
		align_pointer(sections[".bss"].address, pagesize),
		(sections[".bss"].length & (~(pagesize - 1))) + pagesize,
		PROT_READ | PROT_EXEC);
#endif
}

void LibSm64::save(LibSm64Mem& state) const
{
#if defined(_WIN32)
	if (config.lightweight)
	{
		state.buf1.resize(200000);
		state.buf2.resize(1300000);

		uint8_t* dataPtr = reinterpret_cast<uint8_t*>(segment[0].address);
		memcpy(state.buf1.data(), dataPtr, 100000);
		memcpy(state.buf1.data() + 100000, dataPtr + 20 * 100000, 100000);

		uint8_t* bssPtr = reinterpret_cast<uint8_t*>(segment[1].address);
		memcpy(state.buf2.data(), bssPtr, 6 * 100000);
		memcpy(state.buf2.data() + 6 * 100000, bssPtr + 17 * 100000, 6 * 100000);
		memcpy(state.buf2.data() + 12 * 100000, bssPtr + 47 * 100000, 100000);

		return;
	}

	state.buf1.resize(segment[0].length);
	state.buf2.resize(segment[1].length);

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(state.buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(state.buf2.data(), temp, segment[1].length);
#else
	state.changed_regions.reserve(regions_of_interest.size());
	state.region_count_at_save_time = regions_of_interest.size();
	for (const auto region : regions_of_interest) {
		auto* data = state.changed_regions[region].data();
		memcpy(data, region, pagesize);
	}
#endif
}

void LibSm64::load(const LibSm64Mem& state)
{
#if defined(_WIN32)
	if (config.lightweight)
	{
		uint8_t* dataPtr = reinterpret_cast<uint8_t*>(segment[0].address);
		memcpy(dataPtr, state.buf1.data(), 100000);
		memcpy(dataPtr + 20 * 100000, state.buf1.data() + 100000, 100000);

		uint8_t* bssPtr = reinterpret_cast<uint8_t*>(segment[1].address);
		memcpy(bssPtr, state.buf2.data(), 6 * 100000);
		memcpy(bssPtr + 17 * 100000, state.buf2.data() + 6 * 100000, 6 * 100000);
		memcpy(bssPtr + 47 * 100000, state.buf2.data() + 12 * 100000, 100000);

		return;
}

	memcpy(segment[0].address, state.buf1.data(), segment[0].length);
	memcpy(segment[1].address, state.buf2.data(), segment[1].length);
#else
	if (regions_of_interest.size() != state.region_count_at_save_time) {
		memcpy(segment[0].address, original_buf1.data(), segment[0].length);
		memcpy(segment[1].address, original_buf2.data(), segment[1].length);
	}
	for (const auto& pair : state.changed_regions) {
		memcpy(pair.first, pair.second.data(), pagesize);
	}
#endif
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
#if defined(_WIN32)
	return state.buf1.capacity() + state.buf2.capacity();
#else
	return state.changed_regions.size()*pagesize;
#endif
}

uint32_t LibSm64::getCurrentFrame() const
{
	return *(uint32_t*)(addr("gGlobalTimer")) - 1;
}