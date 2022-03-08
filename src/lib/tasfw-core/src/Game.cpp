#include <cstdlib>
#include <chrono>

#include <tasfw/Game.hpp>

#ifdef _MSC_VER
	#include <intrin.h>
#else
	// Provides __rdtsc outside MSVC
	#include <x86intrin.h>
#endif

#if 1
static inline uint64_t get_time()
{
	return __rdtsc();
}
#else
	#include <sys/time.h>
static inline uint64_t get_time()
{
	struct timeval st
	{
	};
	gettimeofday(&st, nullptr);
	return st.tv_sec * 1000000 + st.tv_usec;
}
#endif

Slot::~Slot()
{
	if (game)
		game->_currentSaveMem -= buf1.size() + buf2.size();
}

void Game::advance_frame()
{
	auto start = get_time();

	void* processID = dll.get("sm64_update");

	using pICFUNC = void(TAS_FW_STDCALL*)();

	pICFUNC sm64_update;
	sm64_update = pICFUNC(processID);

	sm64_update();

	_totalFrameAdvanceTime += get_time() - start;

	nFrameAdvances++;
}

bool Game::save_state(Slot* slot)
{
	int64_t additionalMem = segment[0].length + segment[1].length;
	if (_currentSaveMem + additionalMem > _saveMemLimit)
		return false;

	auto start = get_time();

	slot->buf1.resize(segment[0].length);
	slot->buf2.resize(segment[1].length);
	slot->game = this;

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(slot->buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(slot->buf2.data(), temp, segment[1].length);

	_totalSaveStateTime += get_time() - start;

	_currentSaveMem += additionalMem;
	nSaveStates++;

	return true;
}

void Game::load_state(Slot* slot)
{
	uint64_t start = get_time();

	memcpy(segment[0].address, slot->buf1.data(), segment[0].length);
	memcpy(segment[1].address, slot->buf2.data(), segment[1].length);

	_totalLoadStateTime = get_time() - start;

	nLoadStates++;
}

void* Game::addr(const char* symbol)
{
	return dll.get(symbol);
}

uint32_t Game::getCurrentFrame()
{
	return *(uint32_t*) (addr("gGlobalTimer")) - 1;
}

bool Game::shouldSave(uint64_t framesSinceLastSave)
{
	double estTimeToSave = double(_totalSaveStateTime) / nSaveStates;
	double estTimeToLoadFromRecent =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * framesSinceLastSave;

	// TODO: Reduce number of automatic load states in script
	return estTimeToSave <= 3 * estTimeToLoadFromRecent;
}
