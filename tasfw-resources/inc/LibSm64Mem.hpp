#pragma once
#include <cstdint>
#include <vector>

#ifndef LIBSM64MEM_H
#define LIBSM64MEM_H

struct LibSm64DirtyPages;

// A savestate of the game's DLL (LibSm64.hpp), one alternative of the SM64 resources' shared
// state (Sm64Mem.hpp). Full and Fixed fill buf1/buf2 (the game's bytes of the sections, or
// the fixed slices packed). Dirty records which baseline the state is relative to, which
// pages of the page index space (.data's pages then .bss's) the game had written since that
// baseline began, and those pages' contents in index order. SlotManager recycles these
// objects, so the vectors keep their capacity and a save into a warm slot allocates nothing.
class LibSm64Mem
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
	int baseline = -1;                  // the baseline this state was saved under and holds a reference on; -1 when none
	LibSm64DirtyPages* dirty = nullptr; // where that reference is counted (the resource's, which outlives every state)
	std::vector<uint64_t> written;
	std::vector<uint8_t> pages;

	// The slot manager erasing this state's slot: give the baseline its reference back.
	void dispose();
};

#endif
