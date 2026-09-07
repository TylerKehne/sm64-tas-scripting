#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <tasfw/Resource.hpp>

// A Resource with a tiny, cheap state. Benchmarks built on it measure the framework's own
// bookkeeping (script hierarchy, savestate slots, caches, tracking) rather than memcpy or
// the game DLL. The state is 256 bytes so save/load are real copies but negligible.
struct FakeState
{
	uint32_t frame = 0;
	uint16_t buttons = 0;
	int8_t stickX = 0;
	int8_t stickY = 0;
	uint64_t checksum = 0;
	std::array<uint8_t, 240> payload {};
};

class FakeResource : public Resource<FakeState>
{
public:
	FakeResource()
	{
		slotManager._saveMemLimit = int64_t(1) << 40;
	}

	void save(FakeState& state) const override { state = _state; }
	void load(const FakeState& state) override { _state = state; }

	void advance() override
	{
		_state.buttons = _pad.buttons;
		_state.stickX = _pad.stickX;
		_state.stickY = _pad.stickY;
		_state.frame++;
		_state.checksum = _state.checksum * 6364136223846793005ull
			+ (uint64_t(_state.buttons) << 16) + uint64_t(uint8_t(_state.stickX)) * 256 + uint8_t(_state.stickY) + 1442695040888963407ull;
	}

	// The framework only asks for gControllerPads (Script::SetInputs); layout matches the DLL's pad.
	void* addr(const char*) const override { return const_cast<Pad*>(&_pad); }

	std::size_t getStateSize(const FakeState&) const override { return sizeof(FakeState); }
	uint32_t getCurrentFrame() const override { return _state.frame; }

	uint64_t checksum() const { return _state.checksum; }

private:
	struct Pad
	{
		uint16_t buttons = 0;
		int8_t stickX = 0;
		int8_t stickY = 0;
	};

	Pad _pad {};
	FakeState _state {};
};
