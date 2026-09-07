#include <doctest/doctest.h>
#include <tasfw/testing/FakeResource.hpp>

#include <stdexcept>

TEST_CASE("SlotManager saves and restores resource state")
{
	FakeResource resource;
	auto& slots = resource.slotManager;

	for (int i = 0; i < 5; i++)
		resource.advance();
	uint64_t at5 = resource.checksum();

	int64_t slot = slots.CreateSlot();
	CHECK(slots.isValid(slot));

	for (int i = 0; i < 3; i++)
		resource.advance();
	CHECK(resource.checksum() != at5);

	slots.LoadSlot(slot);
	CHECK(resource.checksum() == at5);
	CHECK(resource.getCurrentFrame() == 5);

	slots.EraseSlot(slot);
	CHECK_FALSE(slots.isValid(slot));
}

TEST_CASE("Slot ids are unique and increasing")
{
	FakeResource resource;
	auto& slots = resource.slotManager;
	int64_t a = slots.CreateSlot();
	int64_t b = slots.CreateSlot();
	int64_t c = slots.CreateSlot();
	CHECK(a < b);
	CHECK(b < c);
}

TEST_CASE("At the memory cap the least recently touched slot is evicted")
{
	FakeResource resource;
	auto& slots = resource.slotManager;
	// CreateSlot admits a slot while currentMem + averageSlotSize <= limit, so this limit
	// holds exactly three FakeState-sized slots.
	slots._saveMemLimit = int64_t(4 * sizeof(FakeState)) - 1;

	int64_t s1 = slots.CreateSlot();
	int64_t s2 = slots.CreateSlot();
	int64_t s3 = slots.CreateSlot();
	REQUIRE(slots.isValid(s1));
	REQUIRE(slots.isValid(s2));
	REQUIRE(slots.isValid(s3));

	slots.LoadSlot(s1); // touch s1 so s2 becomes the oldest

	int64_t s4 = slots.CreateSlot();
	CHECK(slots.isValid(s1));
	CHECK_FALSE(slots.isValid(s2));
	CHECK(slots.isValid(s3));
	CHECK(slots.isValid(s4));
}

TEST_CASE("A cap smaller than one slot still admits exactly one slot at a time")
{
	// CreateSlot admits into an empty manager unconditionally (the projected size of a new
	// slot is the average of existing ones, i.e. zero), so a tiny cap degrades to a single
	// slot that is evicted by the next create rather than throwing.
	FakeResource resource;
	auto& slots = resource.slotManager;
	slots._saveMemLimit = 1;

	int64_t s1 = slots.CreateSlot();
	CHECK(slots.isValid(s1));
	int64_t s2 = slots.CreateSlot();
	CHECK_FALSE(slots.isValid(s1));
	CHECK(slots.isValid(s2));
	CHECK(slots.slotsById.size() == 1);
}

TEST_CASE("Resource counters and LoadState(-1) restore the start save")
{
	FakeResource resource;
	resource.save(resource.startSave);
	uint64_t start = resource.checksum();

	resource.FrameAdvance();
	resource.FrameAdvance();
	int64_t slot = resource.SaveState();
	CHECK(resource.nFrameAdvances == 2);
	CHECK(resource.nSaveStates == 1);

	resource.LoadState(-1);
	CHECK(resource.checksum() == start);
	CHECK(resource.getCurrentFrame() == 0);
	CHECK(resource.nLoadStates == 1);

	resource.LoadState(slot);
	CHECK(resource.getCurrentFrame() == 2);
}
