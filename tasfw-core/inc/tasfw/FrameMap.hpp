#pragma once
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef FRAMEMAP_H
#define FRAMEMAP_H

// A map keyed by frame, kept as a vector sorted by key (ROADMAP 3.7).
//
// Everything the framework keeps per frame has the same life: entries arrive at increasing
// frames (a replay, a write, a save), are cut as a suffix when a write invalidates what
// follows it, and are looked up by a frame or by the nearest frame at or below one. A sorted
// vector does that with a binary search, an append and a resize, in one contiguous block;
// std::map did it with a node per entry and, on MSVC, a sentinel node allocated whenever a
// map is constructed or move-constructed, which made an empty child script 11 allocations
// (every status object carries an M64Diff through the sandboxes, Run and the result). A
// FrameMap allocates nothing until its first entry and keeps its storage across clear(),
// so a level reused by LevelStack and a status reset in place allocate nothing more.
//
// The interface is the subset of std::map the framework uses, with the same meanings:
// insert and emplace do not overwrite an existing key, operator[] default-constructs a
// missing one, erase takes a key, a position or a range, lower_bound and upper_bound are
// the ordered lookups, and std::insert_iterator works through insert(hint, value). What
// differs: value_type is std::pair<Key, T>, not std::pair<const Key, T>, so that entries
// can shift, and an insert or erase invalidates every iterator and reference into the map,
// as a vector's do. Inserting below the last key shifts the entries after it (a write
// after a rewind lands in the middle of a diff); the tail is short in practice, and
// Write_RewindOne in tasfw-perf measures it.
template <class Key, class T>
class FrameMap
{
public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<Key, T>;
	using container_type = std::vector<value_type>;
	using size_type = typename container_type::size_type;
	using iterator = typename container_type::iterator;
	using const_iterator = typename container_type::const_iterator;
	using reverse_iterator = typename container_type::reverse_iterator;
	using const_reverse_iterator = typename container_type::const_reverse_iterator;

	FrameMap() = default;

	// Two maps are equal when they hold the same entries, as two std::maps are.
	bool operator==(const FrameMap&) const = default;

	iterator begin() noexcept { return _entries.begin(); }
	iterator end() noexcept { return _entries.end(); }
	const_iterator begin() const noexcept { return _entries.begin(); }
	const_iterator end() const noexcept { return _entries.end(); }
	const_iterator cbegin() const noexcept { return _entries.cbegin(); }
	const_iterator cend() const noexcept { return _entries.cend(); }
	reverse_iterator rbegin() noexcept { return _entries.rbegin(); }
	reverse_iterator rend() noexcept { return _entries.rend(); }
	const_reverse_iterator rbegin() const noexcept { return _entries.rbegin(); }
	const_reverse_iterator rend() const noexcept { return _entries.rend(); }

	bool empty() const noexcept { return _entries.empty(); }
	size_type size() const noexcept { return _entries.size(); }
	// Keeps the storage: a level popped and pushed again, or a status reset in place, does
	// not allocate for its next entry.
	void clear() noexcept { _entries.clear(); }
	void reserve(size_type n) { _entries.reserve(n); }

	iterator lower_bound(const Key& key) { return std::lower_bound(_entries.begin(), _entries.end(), key, KeyLess()); }
	iterator upper_bound(const Key& key) { return std::upper_bound(_entries.begin(), _entries.end(), key, KeyLess()); }
	const_iterator lower_bound(const Key& key) const { return std::lower_bound(_entries.begin(), _entries.end(), key, KeyLess()); }
	const_iterator upper_bound(const Key& key) const { return std::upper_bound(_entries.begin(), _entries.end(), key, KeyLess()); }

	iterator find(const Key& key)
	{
		iterator it = lower_bound(key);
		return it != _entries.end() && !(key < it->first) ? it : _entries.end();
	}

	const_iterator find(const Key& key) const
	{
		const_iterator it = lower_bound(key);
		return it != _entries.end() && !(key < it->first) ? it : _entries.end();
	}

	bool contains(const Key& key) const { return find(key) != _entries.end(); }
	size_type count(const Key& key) const { return contains(key) ? 1 : 0; }

	T& at(const Key& key)
	{
		iterator it = find(key);
		if (it == _entries.end())
			throw std::out_of_range("FrameMap::at: no entry for the key");
		return it->second;
	}

	const T& at(const Key& key) const
	{
		const_iterator it = find(key);
		if (it == _entries.end())
			throw std::out_of_range("FrameMap::at: no entry for the key");
		return it->second;
	}

	// The entry for the key, default-constructed if missing. The common case is a frame past
	// every entry, which is an append.
	T& operator[](const Key& key)
	{
		if (_entries.empty() || _entries.back().first < key)
		{
			_entries.emplace_back(key, T());
			return _entries.back().second;
		}
		iterator it = lower_bound(key);
		if (it == _entries.end() || key < it->first)
			it = _entries.emplace(it, key, T());
		return it->second;
	}

	// Like std::map::insert: an existing key keeps its value, and the result says which.
	std::pair<iterator, bool> insert(const value_type& value) { return Emplace(value); }
	std::pair<iterator, bool> insert(value_type&& value) { return Emplace(std::move(value)); }
	// The hint is ignored (the key decides the position); this is the overload
	// std::insert_iterator needs.
	iterator insert(const_iterator, const value_type& value) { return Emplace(value).first; }
	iterator insert(const_iterator, value_type&& value) { return Emplace(std::move(value)).first; }

	template <class InputIt>
	void insert(InputIt first, InputIt last)
	{
		for (; first != last; ++first)
			Emplace(*first);
	}

	template <class... Args>
	std::pair<iterator, bool> emplace(Args&&... args)
	{
		return Emplace(value_type(std::forward<Args>(args)...));
	}

	size_type erase(const Key& key)
	{
		iterator it = find(key);
		if (it == _entries.end())
			return 0;
		_entries.erase(it);
		return 1;
	}

	iterator erase(const_iterator position) { return _entries.erase(position); }
	iterator erase(const_iterator first, const_iterator last) { return _entries.erase(first, last); }

private:
	struct KeyLess
	{
		bool operator()(const value_type& entry, const Key& key) const { return entry.first < key; }
		bool operator()(const Key& key, const value_type& entry) const { return key < entry.first; }
	};

	template <class V>
	std::pair<iterator, bool> Emplace(V&& value)
	{
		if (_entries.empty() || _entries.back().first < value.first)
		{
			_entries.push_back(std::forward<V>(value));
			return { std::prev(_entries.end()), true };
		}
		iterator it = lower_bound(value.first);
		if (it != _entries.end() && !(value.first < it->first))
			return { it, false };
		return { _entries.insert(it, std::forward<V>(value)), true };
	}

	container_type _entries;
};

// A set of frames, the same way: a sorted vector of keys with the subset of std::set the
// framework uses (Script's load tracker).
template <class Key>
class FrameSet
{
public:
	using key_type = Key;
	using value_type = Key;
	using container_type = std::vector<Key>;
	using size_type = typename container_type::size_type;
	using iterator = typename container_type::const_iterator;
	using const_iterator = typename container_type::const_iterator;

	FrameSet() = default;

	const_iterator begin() const noexcept { return _keys.begin(); }
	const_iterator end() const noexcept { return _keys.end(); }
	bool empty() const noexcept { return _keys.empty(); }
	size_type size() const noexcept { return _keys.size(); }
	void clear() noexcept { _keys.clear(); }

	const_iterator lower_bound(const Key& key) const { return std::lower_bound(_keys.begin(), _keys.end(), key); }
	const_iterator upper_bound(const Key& key) const { return std::upper_bound(_keys.begin(), _keys.end(), key); }

	const_iterator find(const Key& key) const
	{
		const_iterator it = lower_bound(key);
		return it != _keys.end() && !(key < *it) ? it : _keys.end();
	}

	bool contains(const Key& key) const { return find(key) != _keys.end(); }

	std::pair<const_iterator, bool> insert(const Key& key)
	{
		if (_keys.empty() || _keys.back() < key)
		{
			_keys.push_back(key);
			return { std::prev(_keys.end()), true };
		}
		typename container_type::iterator it = std::lower_bound(_keys.begin(), _keys.end(), key);
		if (it != _keys.end() && !(key < *it))
			return { it, false };
		return { _keys.insert(it, key), true };
	}

	size_type erase(const Key& key)
	{
		typename container_type::iterator it = std::lower_bound(_keys.begin(), _keys.end(), key);
		if (it == _keys.end() || key < *it)
			return 0;
		_keys.erase(it);
		return 1;
	}

	const_iterator erase(const_iterator first, const_iterator last) { return _keys.erase(first, last); }

private:
	container_type _keys;
};

#endif
