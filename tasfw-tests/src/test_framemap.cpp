#include <doctest/doctest.h>
#include <tasfw/FrameMap.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// FrameMap is the container behind every per-frame structure of Script and behind
// M64Diff::frames (ROADMAP 3.7). These pin the std::map meanings it keeps: ordered by key,
// insert and emplace do not overwrite, operator[] default-constructs, erase by key, position
// or range, the ordered lookups, std::insert_iterator, and equality by contents.

TEST_CASE("FrameMap keeps its entries ordered by key whatever the insertion order")
{
	FrameMap<int64_t, std::string> map;
	CHECK(map.empty());
	CHECK(map.size() == 0);

	map[30] = "thirty";
	map[10] = "ten";
	map[20] = "twenty";
	map[40] = "forty";
	CHECK(map.size() == 4);

	std::vector<int64_t> keys;
	for (const auto& [frame, value] : map)
		keys.push_back(frame);
	CHECK(keys == std::vector<int64_t> { 10, 20, 30, 40 });
	CHECK(map.begin()->first == 10);
	CHECK(map.rbegin()->first == 40);
	CHECK(map.at(20) == "twenty");
	CHECK(map[30] == "thirty");
}

TEST_CASE("FrameMap insert and emplace keep an existing key's value, operator[] replaces it")
{
	FrameMap<int64_t, int> map;
	auto first = map.insert({ 5, 1 });
	CHECK(first.second);
	CHECK(first.first->first == 5);

	auto again = map.insert({ 5, 2 });
	CHECK_FALSE(again.second);
	CHECK(again.first == first.first);
	CHECK(map.at(5) == 1);

	auto emplaced = map.emplace(5, 3);
	CHECK_FALSE(emplaced.second);
	CHECK(map.at(5) == 1);

	map[5] = 4;
	CHECK(map.at(5) == 4);
	CHECK(map.size() == 1);

	// A missing key is default-constructed by operator[], counted once.
	CHECK(map[7] == 0);
	CHECK(map.size() == 2);
	CHECK(map.count(7) == 1);
	CHECK(map.count(6) == 0);
}

TEST_CASE("FrameMap ordered lookups match std::map's")
{
	FrameMap<int64_t, int> map;
	for (int64_t frame : { 10, 20, 30 })
		map[frame] = int(frame);

	CHECK(map.lower_bound(20)->first == 20);
	CHECK(map.upper_bound(20)->first == 30);
	CHECK(map.lower_bound(25)->first == 30);
	CHECK(map.upper_bound(30) == map.end());
	CHECK(map.lower_bound(5)->first == 10);
	CHECK(map.upper_bound(5)->first == 10);
	CHECK(map.find(20)->second == 20);
	CHECK(map.find(25) == map.end());
	CHECK(map.contains(30));
	CHECK_FALSE(map.contains(31));

	// The latest entry at or before a frame, the way Script finds a save.
	auto latest = std::prev(map.upper_bound(25));
	CHECK(latest->first == 20);

	const FrameMap<int64_t, int>& view = map;
	CHECK(view.find(10)->second == 10);
	CHECK(view.lower_bound(15)->first == 20);
	CHECK(view.at(30) == 30);
	CHECK_THROWS_AS((void)view.at(15), std::out_of_range);
}

TEST_CASE("FrameMap erases by key, by position and by range")
{
	FrameMap<int64_t, int> map;
	for (int64_t frame = 0; frame < 10; frame++)
		map[frame] = int(frame);

	CHECK(map.erase(3) == 1);
	CHECK(map.erase(3) == 0);
	CHECK_FALSE(map.contains(3));
	CHECK(map.size() == 9);

	// A write at frame 5 cuts everything after it, as Script's caches are cut.
	map.erase(map.upper_bound(5), map.end());
	CHECK(map.size() == 5);
	CHECK(map.rbegin()->first == 5);

	map.erase(map.find(0));
	CHECK(map.begin()->first == 1);
	CHECK(map.size() == 4);

	map.clear();
	CHECK(map.empty());
	map[1] = 1;
	CHECK(map.size() == 1);
}

TEST_CASE("FrameMap takes a range and an insert_iterator without overwriting")
{
	FrameMap<int64_t, int> source;
	source[1] = 1;
	source[2] = 2;
	source[3] = 3;

	FrameMap<int64_t, int> dest;
	dest[2] = 20;
	dest.insert(source.begin(), source.end());
	CHECK(dest.size() == 3);
	CHECK(dest.at(1) == 1);
	CHECK(dest.at(2) == 20);
	CHECK(dest.at(3) == 3);

	// Move-only values move through std::insert_iterator, the way child saves reach a parent.
	FrameMap<int64_t, std::unique_ptr<int>> child;
	child.emplace(7, std::make_unique<int>(7));
	child.emplace(9, std::make_unique<int>(9));
	FrameMap<int64_t, std::unique_ptr<int>> parent;
	parent.emplace(8, std::make_unique<int>(8));
	std::move(child.begin(), child.end(), std::insert_iterator(parent, parent.end()));
	CHECK(parent.size() == 3);
	CHECK(*parent.at(7) == 7);
	CHECK(*parent.at(8) == 8);
	CHECK(*parent.at(9) == 9);
	CHECK(child.at(7) == nullptr);
}

TEST_CASE("FrameMap compares by contents")
{
	FrameMap<uint64_t, int> a;
	FrameMap<uint64_t, int> b;
	CHECK(a == b);
	a[3] = 1;
	CHECK(a != b);
	b[3] = 1;
	CHECK(a == b);
	b[3] = 2;
	CHECK(a != b);
}

TEST_CASE("FrameSet is an ordered set of frames")
{
	FrameSet<int64_t> set;
	CHECK(set.empty());
	CHECK(set.insert(5).second);
	CHECK(set.insert(2).second);
	CHECK_FALSE(set.insert(5).second);
	CHECK(set.size() == 2);
	CHECK(set.contains(2));
	CHECK_FALSE(set.contains(3));
	CHECK(*set.lower_bound(3) == 5);
	CHECK(set.lower_bound(6) == set.end());
	CHECK(set.erase(2) == 1);
	CHECK(set.erase(2) == 0);
	CHECK(*set.begin() == 5);
	set.clear();
	CHECK(set.empty());
}
