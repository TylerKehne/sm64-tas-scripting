#pragma once
#include <cstdint>
#include <memory>
#include <optional>
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
// Every level, including level 0, is constructed on first access: a script that never
// touches a container pays nothing for it, which matters because MSVC's std::map allocates
// its head node in the constructor and a Script has six such containers. Higher levels are
// heap-allocated once and then reused: erase() resets a level's contents in place (clear()
// for containers, Reset() for BaseScriptStatus) and lowers the size, and the next push at
// that level reuses the storage without allocating. References to a level stay valid across
// pushes and pops of other levels (a level is never moved), which Script relies on.
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
	//
	// The fast path is one compare and a pointer select so that it inlines into every
	// caller; the growth loop lives in Grow(). With the loop inline here MSVC stopped
	// inlining the accessor and every bookkeeping access in the ancestor walk became a call
	// (+19% on a depth-16 LongLoad, +13% on an empty ExecuteAdhoc; clang was unaffected).
	T& operator[](int64_t level)
	{
		uint64_t target = uint64_t(level);
		if (target >= _size) [[unlikely]]
			Grow(target);
		return at(target);
	}

	bool contains(int64_t level) const
	{
		return level >= 0 && uint64_t(level) < _size;
	}

	uint64_t size() const { return _size; }

	// Removes `level` and everything above it. Contents are reset in place (SlotHandles
	// release their slots); storage is kept for reuse.
	void erase(int64_t level)
	{
		uint64_t keep = level < 0 ? 0 : uint64_t(level);
		while (_size > keep)
		{
			_size--;
			ResetInPlace(at(_size));
		}
	}

private:
	T& at(uint64_t level)
	{
		return level == 0 ? *_level0 : *_higher[level - 1];
	}

	void Grow(uint64_t target)
	{
		while (_size <= target)
		{
			if (_size == 0)
			{
				if (!_level0)
					_level0.emplace();
			}
			else if (_size - 1 == _higher.size())
				_higher.push_back(std::make_unique<T>());
			_size++;
		}
	}

	// Empties a level without replacing the object. A container's clear() keeps its head
	// node and (for vectors) its capacity; a type with Reset() knows how to zero itself.
	// Assigning a fresh T() would construct a temporary, which for a std::map on MSVC is an
	// allocation per pop.
	static void ResetInPlace(T& value)
	{
		if constexpr (requires { value.Reset(); })
			value.Reset();
		else if constexpr (requires { value.clear(); })
			value.clear();
		else
			value = T();
	}

	std::optional<T> _level0;
	std::vector<std::unique_ptr<T>> _higher;
	uint64_t _size = 0;
};

#endif
