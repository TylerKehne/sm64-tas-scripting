#include <doctest/doctest.h>
#include <tasfw/LevelStack.hpp>
#include <tasfw/Script.hpp>
#include <tasfw/testing/FakeResource.hpp>

#include <map>
#include <vector>

// LevelStack is the per-ad-hoc-level container behind every Script; these pin the
// properties Script relies on (ARCHITECTURE.md, "LevelStack") and the no-allocation
// pop/push cycle that tasfw-perf gates.

namespace
{
	struct Resettable
	{
		int value = 0;
		int resets = 0;
		void Reset()
		{
			value = 0;
			resets++;
		}
	};
}

TEST_CASE("LevelStack creates levels on demand and erases from the top down")
{
	LevelStack<std::vector<int>> stack;
	CHECK(stack.size() == 0);
	CHECK_FALSE(stack.contains(0));

	stack[2].push_back(2);
	CHECK(stack.size() == 3);
	CHECK(stack.contains(0));
	CHECK(stack.contains(1));
	CHECK(stack.contains(2));
	CHECK_FALSE(stack.contains(3));
	CHECK_FALSE(stack.contains(-1));
	CHECK(stack[0].empty());
	CHECK(stack[1].empty());

	stack.erase(1);
	CHECK(stack.size() == 1);
	CHECK(stack.contains(0));
	CHECK_FALSE(stack.contains(1));

	stack.erase(0);
	CHECK(stack.size() == 0);
	CHECK_FALSE(stack.contains(0));

	stack.erase(5); // nothing above level 5 exists; no-op
	CHECK(stack.size() == 0);
}

TEST_CASE("Popped levels are reset in place and their storage is reused")
{
	LevelStack<std::vector<int>> stack;
	stack[1].reserve(64);
	stack[1].push_back(7);
	std::vector<int>* level1 = &stack[1];

	stack.erase(1);
	CHECK_FALSE(stack.contains(1));

	// The same object comes back, emptied but with its capacity intact (clear(), not a
	// fresh vector).
	std::vector<int>& again = stack[1];
	CHECK(&again == level1);
	CHECK(again.empty());
	CHECK(again.capacity() >= 64);

	// Level 0 is reset the same way.
	stack[0].reserve(32);
	stack[0].push_back(1);
	std::vector<int>* level0 = &stack[0];
	stack.erase(0);
	CHECK(&stack[0] == level0);
	CHECK(stack[0].empty());
	CHECK(stack[0].capacity() >= 32);
}

TEST_CASE("A type with Reset() is reset through it")
{
	LevelStack<Resettable> stack;
	stack[1].value = 5;
	Resettable* level1 = &stack[1];

	stack.erase(1);
	CHECK(&stack[1] == level1);
	CHECK(stack[1].value == 0);
	CHECK(stack[1].resets == 1);

	stack.erase(0); // resets level 1 then level 0
	CHECK(stack[1].resets == 2);
	CHECK(stack[0].resets == 1);
}

TEST_CASE("References to a level survive pushes and pops of other levels")
{
	LevelStack<std::map<int, int>> stack;
	std::map<int, int>& level1 = stack[1];
	level1[1] = 10;
	std::map<int, int>& level0 = stack[0];
	level0[0] = 100;

	for (int level = 2; level < 40; level++)
		stack[level][level] = level;
	CHECK(&stack[1] == &level1);
	CHECK(&stack[0] == &level0);
	CHECK(level1.at(1) == 10);

	stack.erase(2);
	CHECK(&stack[1] == &level1);
	CHECK(level1.at(1) == 10);
	CHECK(level0.at(0) == 100);
}

TEST_CASE("Erasing a level destroys its contents: slot handles release their slots")
{
	FakeResource resource;
	LevelStack<std::map<int64_t, SlotHandle<FakeResource>>> saveBank;

	int64_t slot = resource.slotManager.CreateSlot();
	saveBank[1].emplace(5, SlotHandle<FakeResource>(&resource, slot));
	REQUIRE(resource.slotManager.isValid(slot));

	saveBank.erase(1);
	CHECK_FALSE(resource.slotManager.isValid(slot));
	CHECK(saveBank[1].empty());
}

TEST_CASE("BaseScriptStatus::Reset returns every field to its default")
{
	BaseScriptStatus status;
	status.validated = status.executed = status.asserted = true;
	status.validationDuration = 1;
	status.executionDuration = 2;
	status.assertionDuration = 3;
	status.totalDuration = 4;
	status.saveDuration = 5;
	status.loadDuration = 6;
	status.advanceFrameDuration = 7;
	status.nLoads = 8;
	status.nSaves = 9;
	status.nFrameAdvances = 10;
	status.m64Diff.frames[3] = Inputs(1, 2, 3);

	status.Reset();

	BaseScriptStatus fresh;
	CHECK(status.validated == fresh.validated);
	CHECK(status.executed == fresh.executed);
	CHECK(status.asserted == fresh.asserted);
	CHECK(status.validationDuration == fresh.validationDuration);
	CHECK(status.executionDuration == fresh.executionDuration);
	CHECK(status.assertionDuration == fresh.assertionDuration);
	CHECK(status.totalDuration == fresh.totalDuration);
	CHECK(status.saveDuration == fresh.saveDuration);
	CHECK(status.loadDuration == fresh.loadDuration);
	CHECK(status.advanceFrameDuration == fresh.advanceFrameDuration);
	CHECK(status.nLoads == fresh.nLoads);
	CHECK(status.nSaves == fresh.nSaves);
	CHECK(status.nFrameAdvances == fresh.nFrameAdvances);
	CHECK(status.m64Diff.frames.empty());
}
