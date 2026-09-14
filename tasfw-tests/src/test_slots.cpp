#include <doctest/doctest.h>
#include <tasfw/testing/MockResource.hpp>

#include <stdexcept>

TEST_CASE("SlotManager saves and restores resource state")
{
	MockResource resource;
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
	MockResource resource;
	auto& slots = resource.slotManager;
	int64_t a = slots.CreateSlot();
	int64_t b = slots.CreateSlot();
	int64_t c = slots.CreateSlot();
	CHECK(a < b);
	CHECK(b < c);
}

TEST_CASE("At the memory cap the least recently touched slot is evicted")
{
	MockResource resource;
	auto& slots = resource.slotManager;
	// CreateSlot admits a slot while currentMem + averageSlotSize <= limit, so this limit
	// holds exactly three MockState-sized slots.
	slots._saveMemLimit = int64_t(4 * sizeof(MockState)) - 1;

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

	// The resource's counters saw the eviction and the high-water marks (ROADMAP 3.6).
	CHECK(resource.work.evictions == 1);
	CHECK(resource.work.slotsLiveMax == 3);
	CHECK(resource.work.slotBytesMax == uint64_t(3 * sizeof(MockState)));
}

TEST_CASE("A cap smaller than one slot still admits exactly one slot at a time")
{
	// CreateSlot admits into an empty manager unconditionally (the projected size of a new
	// slot is the average of existing ones, i.e. zero), so a tiny cap degrades to a single
	// slot that is evicted by the next create rather than throwing.
	MockResource resource;
	auto& slots = resource.slotManager;
	slots._saveMemLimit = 1;

	int64_t s1 = slots.CreateSlot();
	CHECK(slots.isValid(s1));
	int64_t s2 = slots.CreateSlot();
	CHECK_FALSE(slots.isValid(s1));
	CHECK(slots.isValid(s2));
	CHECK(slots.slotsById.size() == 1);
}

TEST_CASE("Erased and evicted slots are pooled and the next save reuses their storage")
{
	MockResource resource;
	auto& slots = resource.slotManager;

	int64_t a = slots.CreateSlot();
	CHECK(slots._pool.size() == 0);
	slots.EraseSlot(a);
	CHECK(slots._pool.size() == 1);
	CHECK(slots._pooledMem == int64_t(sizeof(MockState)));

	// The recycled state holds the new save, not the old one.
	resource.advance();
	uint64_t atB = resource.checksum();
	int64_t b = slots.CreateSlot();
	CHECK(b != a);
	CHECK(slots._pool.size() == 0);
	CHECK(slots._pooledMem == 0);
	CHECK(resource.work.poolReuses == 1);
	resource.advance();
	slots.LoadSlot(b);
	CHECK(resource.checksum() == atB);

	// Eviction at the memory cap goes through the pool as well: two slots fit, the third
	// evicts the oldest and reuses its storage.
	slots._saveMemLimit = int64_t(3 * sizeof(MockState)) - 1;
	int64_t c = slots.CreateSlot();
	int64_t d = slots.CreateSlot();
	CHECK_FALSE(slots.isValid(b));
	CHECK(slots.isValid(c));
	CHECK(slots.isValid(d));
	CHECK(resource.work.poolReuses == 2);
	CHECK(slots._pool.size() == 0);
	CHECK(slots.slotsById.size() == 2);

	// The pool is bounded; states beyond the bound are released.
	slots._saveMemLimit = int64_t(64 * sizeof(MockState));
	slots._maxPooledStates = 2;
	std::vector<int64_t> ids;
	for (int i = 0; i < 5; i++)
		ids.push_back(slots.CreateSlot());
	for (int64_t id : ids)
		slots.EraseSlot(id);
	CHECK(slots._pool.size() == 2);
	CHECK(slots._pooledMem == int64_t(2 * sizeof(MockState)));
	CHECK(slots._currentSaveMem == int64_t(2 * sizeof(MockState))); // c and d
}

TEST_CASE("Resource counters and LoadState(-1) restore the start save")
{
	MockResource resource;
	resource.save(resource.startSave);
	uint64_t start = resource.checksum();

	resource.FrameAdvance();
	resource.FrameAdvance();
	int64_t slot = resource.SaveState();
	CHECK(resource.work.frameAdvances == 2);
	CHECK(resource.work.saves == 1);

	resource.LoadState(-1);
	CHECK(resource.checksum() == start);
	CHECK(resource.getCurrentFrame() == 0);
	CHECK(resource.work.loads == 1);

	resource.LoadState(slot);
	CHECK(resource.getCurrentFrame() == 2);
}

TEST_CASE("A resource takes its limit from the process budget when it is created, or throws")
{
	const int64_t MB = 1024 * 1024;
	SlotBudget::Set(100 * MB);
	{
		MockResource a; // 64 MB
		CHECK(SlotBudget::balance == 36 * MB);
		CHECK_THROWS_AS(MockResource(), std::runtime_error); // another 64 MB does not fit
		CHECK(SlotBudget::balance == 36 * MB);
	}
	CHECK(SlotBudget::balance == 100 * MB); // given back
	SlotBudget::Set(0);
}

TEST_CASE("Erasing a slot disposes of its state, through the resource and at eviction")
{
	MockResource resource;
	auto& slots = resource.slotManager;
	int before = MockState::disposed;

	int64_t a = slots.CreateSlot();
	int64_t b = slots.CreateSlot();
	CHECK(resource.HasState(a));
	resource.DisposeState(a); // what a SlotHandle's release does
	CHECK_FALSE(resource.HasState(a));
	CHECK(resource.HasState(b));
	CHECK(MockState::disposed == before + 1);

	// Eviction erases too, so the state going to the pool is disposed of first.
	slots._saveMemLimit = int64_t(3 * sizeof(MockState)) - 1;
	slots.CreateSlot();
	slots.CreateSlot(); // evicts b
	CHECK_FALSE(resource.HasState(b));
	CHECK(MockState::disposed == before + 2);
}
