#include <windows.h>
#include <stdlib.h>
#include <chrono>

#include "Game.hpp"


#include <intrin.h>
#pragma intrinsic(__rdtsc)


#if 1
static inline uint64_t get_time() { return __rdtsc(); }
#else
#include <sys/time.h>
static inline uint64_t get_time() {
	struct timeval st {};
	gettimeofday(&st, nullptr);
	return st.tv_sec * 1000000 + st.tv_usec;
}
#endif

void Game::advance_frame() {
	auto start = get_time();

	FARPROC processID = GetProcAddress(dll, "sm64_update");

	typedef void(__stdcall* pICFUNC)();

	pICFUNC sm64_update;
	sm64_update = pICFUNC(processID);

	sm64_update();

	_totalFrameAdvanceTime += get_time() - start;

	nFrameAdvances++;

}


void Game::save_state(Slot* slot) {
	auto start = get_time();
	slot->buf1.resize(segment[0].virtual_size);
	slot->buf2.resize(segment[1].virtual_size);

	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(slot->buf1.data(), temp, segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(slot->buf2.data(), temp, segment[1].virtual_size);

	_totalSaveStateTime += get_time() - start;

	nSaveStates++;

}

void Game::load_state(Slot* slot) {
	auto start = get_time();

	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf1.data(), segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf2.data(), segment[1].virtual_size);

	_totalLoadStateTime = get_time() - start;

	nLoadStates++;

}

intptr_t Game::addr(const char* symbol) {
	return reinterpret_cast<intptr_t>(GetProcAddress(dll, symbol));
}

uint32_t Game::getCurrentFrame()
{
	return *(uint32_t*)(addr("gGlobalTimer")) - 1;
}

bool Game::shouldSave(uint64_t framesSinceLastSave)
{
	double estTimeToSave = ((double)_totalSaveStateTime) / nSaveStates;
	double estTimeToLoadFromRecent = (((double)_totalFrameAdvanceTime)/ nFrameAdvances) * framesSinceLastSave;

	//TODO: Reduce number of automatic load states in script
	return estTimeToSave <= 2 * estTimeToLoadFromRecent;
}
