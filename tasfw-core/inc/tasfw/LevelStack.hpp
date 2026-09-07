#pragma once
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#ifndef LEVELSTACK_H
#define LEVELSTACK_H

// Per-ad-hoc-level bookkeeping container for Script.
//
// Scripts keep one container (diff status, save bank, caches, ...) per ad-hoc nesting
// level. Levels are created in increasing order and destroyed in decreasing order, so this
// is a stack indexed by level, not a map. It used to be std::unordered_map<int64_t, T>,
// which hashed on every access and allocated on every level push (the dominant cost of an
// empty ExecuteAdhoc, per tasfw-perf).
//
// Level 0 lives inline. Higher levels are heap-allocated once and then reused: erase()
// resets a level's contents to T() and lowers the size, and the next push at that level
// reuses the storage. References to a level stay valid across pushes and pops of other
// levels (a level is never moved), which Script relies on.
template <class T>
class LevelStack
{
public:
	LevelStack() = default;
	LevelStack(const LevelStack&) = delete;
	LevelStack& operator=(const LevelStack&) = delete;
	LevelStack(LevelStack&&) = default;
	LevelStack& operator=(LevelStack&&) = default;

	// Grows to include `level` (default-constructed), like std::map::operator[].
	T& operator[](int64_t level)
	{
		while (_size <= uint64_t(level))
		{
			if (_size >= 1)
			{
				uint64_t index = _size - 1;
				if (index == _higher.size())
					_higher.push_back(std::make_unique<T>());
			}
			_size++;
		}
		return at(uint64_t(level));
	}

	bool contains(int64_t level) const
	{
		return level >= 0 && uint64_t(level) < _size;
	}

	uint64_t size() const { return _size; }

	// Removes `level` and everything above it. Contents are destroyed (SlotHandles release
	// their slots); storage is kept for reuse.
	void erase(int64_t level)
	{
		uint64_t keep = level < 0 ? 0 : uint64_t(level);
		while (_size > keep)
		{
			_size--;
			at(_size) = T();
		}
	}

private:
	T& at(uint64_t level)
	{
		return level == 0 ? _level0 : *_higher[level - 1];
	}

	T _level0 {};
	std::vector<std::unique_ptr<T>> _higher;
	uint64_t _size = 0;
};

#endif
