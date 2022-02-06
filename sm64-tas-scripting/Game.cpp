#include <windows.h>
#include <stdlib.h>
#include <chrono>

#include "Game.hpp"

void Game::advance_frame() {
	auto start = std::chrono::high_resolution_clock::now();

	FARPROC processID = GetProcAddress(dll, "sm64_update");

	typedef void(__stdcall* pICFUNC)();

	pICFUNC sm64_update;
	sm64_update = pICFUNC(processID);

	sm64_update();

	auto finish = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	if (!nFrameAdvances)
		_avgFrameAdvanceTime = duration;
	else
		_avgFrameAdvanceTime = (_avgFrameAdvanceTime * nFrameAdvances + duration) / (nFrameAdvances + 1);

	nFrameAdvances++;

}


void Game::save_state(Slot* slot) {
	slot->buf1.resize(segment[0].virtual_size);
	slot->buf2.resize(segment[1].virtual_size);
	auto start = std::chrono::high_resolution_clock::now();

	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(slot->buf1.data(), temp, segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(slot->buf2.data(), temp, segment[1].virtual_size);

	auto finish = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	if (!nSaveStates)
		_avgSaveStateTime = duration;
	else
		_avgSaveStateTime = (_avgSaveStateTime * nSaveStates + duration) / (nSaveStates + 1);

	nSaveStates++;

}

void Game::load_state(Slot* slot) {
	auto start = std::chrono::high_resolution_clock::now();

	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf1.data(), segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf2.data(), segment[1].virtual_size);

	auto finish = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	if (!nLoadStates)
		_avgLoadStateTime = duration;
	else
		_avgLoadStateTime = (_avgLoadStateTime * nLoadStates + duration) / (nLoadStates + 1);

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
	double estTimeToSave = _avgAllocSlotTime;
	double estTimeToLoadFromRecent = _avgFrameAdvanceTime * framesSinceLastSave;

	//TODO: Reduce number of automatic load states in script
	return estTimeToSave <= 2 * estTimeToLoadFromRecent;
}
