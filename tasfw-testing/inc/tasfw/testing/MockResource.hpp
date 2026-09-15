#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <tasfw/Resource.hpp>

// A Resource with a tiny, cheap, fully deterministic state. Shared by the perf suite and the
// unit tests: benchmarks built on it measure the framework's own bookkeeping (script
// hierarchy, savestate slots, caches, tracking) rather than memcpy or the game DLL, and tests
// use its rolling checksum to prove "state is a pure function of (start save, inputs)".
// The state is 256 bytes so save/load are real copies but negligible.
struct MockState
{
	uint32_t frame = 0;
	uint16_t buttons = 0;
	int8_t stickX = 0;
	int8_t stickY = 0;
	uint64_t checksum = 0;
	std::array<uint8_t, 240> payload {};

	// The slot manager's dispose() hook, counted so a test can see it fire (tasfw/Resource.hpp).
	static inline int disposed = 0;
	void dispose() { disposed++; }

	// What a script exports (Script::ExportSave) for a run on another MockResource: the
	// state as the resource stands, the way PyramidUpdateMem converts from a LibSm64.
	MockState() = default;
	explicit MockState(const Resource<MockState>& resource) { resource.save(*this); }
};

class MockResource : public Resource<MockState>
{
public:
	// 64 MB: a quarter of a million of these states, so nothing evicts unless a test or
	// benchmark lowers the limit itself; small enough to sit under a test's process budget.
	MockResource() : Resource(int64_t(64) << 20) { }

	void save(MockState& state) const override { state = _state; }
	void load(const MockState& state) override { _state = state; }

	void advance() override
	{
		_state.buttons = _pad.buttons;
		_state.stickX = _pad.stickX;
		_state.stickY = _pad.stickY;
		_state.frame++;
		_state.checksum = _state.checksum * 6364136223846793005ull
			+ (uint64_t(_state.buttons) << 16) + uint64_t(uint8_t(_state.stickX)) * 256 + uint8_t(_state.stickY) + 1442695040888963407ull;
	}

	void setInputs(const Inputs& inputs) override
	{
		_pad.buttons = inputs.buttons;
		_pad.stickX = inputs.stick_x;
		_pad.stickY = inputs.stick_y;
	}

	// The mock's memory, for Script::ReadState: "checksum" is the rolling checksum (what a
	// test's script hashes or scores), any other name the pad (ScattershotThread reads the
	// course and area through it; a test may poke it the way a script would).
	void* addr(const char* symbol) const override
	{
		if (std::strcmp(symbol, "checksum") == 0)
			return const_cast<uint64_t*>(&_state.checksum);
		return const_cast<Pad*>(&_pad);
	}

	std::size_t getStateSize(const MockState&) const override { return sizeof(MockState); }
	uint32_t getCurrentFrame() const override { return _state.frame; }

	uint64_t checksum() const { return _state.checksum; }
	const MockState& state() const { return _state; }

private:
	struct Pad
	{
		uint16_t buttons = 0;
		int8_t stickX = 0;
		int8_t stickY = 0;
	};

	Pad _pad {};
	MockState _state {};
};
