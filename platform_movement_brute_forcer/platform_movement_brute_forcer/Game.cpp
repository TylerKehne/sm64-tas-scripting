#include <windows.h>
#include <stdlib.h>

#include "Game.hpp"

Game game = Game("jp", ".\\sm64_jp.dll");

uint32_t Game::advance_frame(bool returnFrame) {
	FARPROC processID = GetProcAddress(dll, "sm64_update");

	typedef void(__stdcall* pICFUNC)();

	pICFUNC sm64_update;
	sm64_update = pICFUNC(processID);

	sm64_update();

	if (returnFrame)
		return getCurrentFrame();
	else
		return 0;
}

Slot Game::alloc_slot() {
	return Slot(segment[0].virtual_size, segment[1].virtual_size);
}

void Game::save_state(Slot* slot) {
	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(slot->buf1.data(), temp, segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(slot->buf2.data(), temp, segment[1].virtual_size);
}

void Game::load_state(Slot* slot) {
	int64_t *temp = reinterpret_cast<int64_t*>(dll) + segment[0].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf1.data(), segment[0].virtual_size);

	temp = reinterpret_cast<int64_t*>(dll) + segment[1].virtual_address / sizeof(int64_t);
	memmove(temp, slot->buf2.data(), segment[1].virtual_size);
}

intptr_t Game::addr(const char* symbol) {
	return reinterpret_cast<intptr_t>(GetProcAddress(dll, symbol));
}

uint32_t Game::getCurrentFrame()
{
	return *(uint32_t*)(game.addr("gGlobalTimer")) - 1;
}

