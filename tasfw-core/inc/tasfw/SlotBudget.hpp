#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>

#ifndef SLOTBUDGET_H
#define SLOTBUDGET_H

// Process-wide savestate memory (ROADMAP 3.5): a budget the application sets once, before
// it creates resources, and the balance left of it. A resource subtracts its limit from the
// balance when it is created, or throws if the balance is too low, and adds it back when it
// dies. No budget, the default: no limit.
struct SlotBudget
{
	static inline std::atomic<int64_t> budget = 0;
	static inline std::atomic<int64_t> balance = 0;

	static void Set(int64_t bytes)
	{
		balance += bytes - budget;
		budget = bytes;
	}
	static void Take(int64_t bytes)
	{
		if (budget == 0)
			return;
		if (balance.fetch_sub(bytes) < bytes)
		{
			balance += bytes;
			throw std::runtime_error("savestate budget too low for another resource (SlotBudget; resources.savestateBudgetMB in the pipeline)");
		}
	}
	static void Give(int64_t bytes)
	{
		if (budget != 0)
			balance += bytes;
	}
};

#endif
