#include <cstdlib>
#include <chrono>

#include <tasfw/Script.hpp>



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

bool Game::shouldSave(int64_t estFrameAdvances) const
{
	if (nSaveStates == 0 || estFrameAdvances < 0)
		return true;

	double estTimeToSave = double(_totalSaveStateTime) / nSaveStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * estFrameAdvances;

	return estTimeToSave < estTimeToFrameAdvance;
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
