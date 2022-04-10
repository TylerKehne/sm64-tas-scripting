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

SlotHandle::~SlotHandle()
{
	if (slotId != -1)
		game->slotManager.EraseSlot(slotId);
}

int64_t SlotManager::CreateSlot(Script* script, int64_t frame, int64_t adhocLevel)
{
	while (true)
	{
		int64_t additionalMem = _game->segment[0].length + _game->segment[1].length;
		if (_game->_currentSaveMem + additionalMem <= _game->_saveMemLimit)
		{
			//NOTE: IDs/Order will not overflow on realistic timescales
			int64_t slotId = slotsById.size() == 0 ? 0 : std::prev(slotsById.end())->first + 1;
			slotsById[slotId] = Slot(_game, script, frame, adhocLevel);
			int64_t slotOrder = slotIdsByLastAccess.size() == 0 ? 0 : std::prev(slotIdsByLastAccess.end())->first + 1;
			slotIdsByLastAccess[slotOrder] = slotId;
			slotLastAccessOrderById[slotId] = slotOrder;

			//Save memory into slot
			slotsById[slotId].buf1.resize(_game->segment[0].length);
			slotsById[slotId].buf2.resize(_game->segment[1].length);

			int64_t* temp = reinterpret_cast<int64_t*>(_game->segment[0].address);
			memcpy(slotsById[slotId].buf1.data(), temp, _game->segment[0].length);

			temp = reinterpret_cast<int64_t*>(_game->segment[1].address);
			memcpy(slotsById[slotId].buf2.data(), temp, _game->segment[1].length);

			_game->_currentSaveMem += additionalMem;

			return slotId;
		}

		if (slotsById.size() == 0)
			throw std::runtime_error("Not enough game slot memory allocated");

		// If save memory is full, remove the earliest save and try again
		EraseOldestSlot();
	}
}

void SlotManager::EraseSlot(int64_t slotId)
{
	int64_t slotOrder = slotLastAccessOrderById[slotId];
	slotsById.erase(slotId);
	slotLastAccessOrderById.erase(slotId);
	slotIdsByLastAccess.erase(slotOrder);
}

void SlotManager::UpdateSlot(int64_t slotId)
{
	//NOTE: Order will not overflow on realistic timescales
	int64_t slotOrder = slotLastAccessOrderById[slotId];
	slotIdsByLastAccess.erase(slotOrder);
	int64_t newSlotOrder = slotIdsByLastAccess.size() == 0 ? 0 : std::prev(slotIdsByLastAccess.end())->first + 1;
	slotIdsByLastAccess[newSlotOrder] = slotId;
	slotLastAccessOrderById[slotId] = newSlotOrder;

	//Load slot memory
	memcpy(_game->segment[0].address, slotsById[slotId].buf1.data(), _game->segment[0].length);
	memcpy(_game->segment[1].address, slotsById[slotId].buf2.data(), _game->segment[1].length);
}

void SlotManager::EraseOldestSlot()
{
	int64_t slotId = slotIdsByLastAccess.begin() == slotIdsByLastAccess.end() ? -1 : slotIdsByLastAccess.begin()->second;
	if (slotId == -1)
		return;

	//Remove slot handle from script hierarchy, triggering slot deletion
	slotsById[slotId].script->DeleteSave(slotsById[slotId].frame, slotsById[slotId].adhocLevel);
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

int64_t Game::save_state(Script* script, int64_t frame, int64_t adhocLevel)
{
	auto start = get_time();
	int64_t slotId = slotManager.CreateSlot(script, frame, adhocLevel);
	_totalSaveStateTime += get_time() - start;

	nSaveStates++;

	return slotId;
}

void Game::save_state_initial()
{
	auto start = get_time();

	int64_t additionalMem = segment[0].length + segment[1].length;
	if (_currentSaveMem + additionalMem > _saveMemLimit)
		throw std::runtime_error("Not enough game slot memory allocated");

	startSave.buf1.resize(segment[0].length);
	startSave.buf2.resize(segment[1].length);
	startSave.game = this;

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(startSave.buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(startSave.buf2.data(), temp, segment[1].length);

	_totalSaveStateTime += get_time() - start;

	_currentSaveMem += additionalMem;
	nSaveStates++;
}

void Game::load_state(int64_t slotId)
{
	uint64_t start = get_time();

	if (slotId == -1)
	{
		memcpy(segment[0].address, startSave.buf1.data(), segment[0].length);
		memcpy(segment[1].address, startSave.buf2.data(), segment[1].length);
	}
	else
		slotManager.UpdateSlot(slotId);

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

bool Game::shouldSave(int64_t framesSinceLastSave) const
{
	if (nSaveStates == 0 || framesSinceLastSave < 0)
		return true;

	double estTimeToSave = double(_totalSaveStateTime) / nSaveStates;
	double estTimeToLoadFromRecent =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * framesSinceLastSave;

	// TODO: Reduce number of automatic load states in script
	return estTimeToSave < 2 * estTimeToLoadFromRecent;
}

bool Game::shouldLoad(int64_t framesAhead) const
{
	if (nLoadStates == 0 || framesAhead < 0)
		return true;

	double estTimeToLoad = double(_totalLoadStateTime) / nLoadStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * framesAhead;

	return estTimeToLoad < estTimeToFrameAdvance;
}
