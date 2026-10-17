#pragma once
#include "LibSm64.hpp"
#include "Sm64Resource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

#ifndef BITFSCORNER_H
#define BITFSCORNER_H

// The pyramid corner of Bowser in the Fire Sea as a resource of its own (ROADMAP 4.13): the
// game's frame reimplemented from the decomp for what the squish-cancel search reads, on the
// decomp's own structs and serving LibSm64's symbols, so that a script written for the SM64
// game (Script<Sm64Resource>) runs on either. Three parts:
//
// - BitfsCornerLevel (BitfsCornerMem.hpp): the level as data, read once from the running
//   game at a frame inside it (the static surfaces with their partition in the game's own
//   cell order, the corner's behavior scripts, the collision data of its surface objects)
//   and shared, immutable, by every resource in the process; so a run loads the game once,
//   not once per thread.
// - BitfsCornerMem (BitfsCornerMem.hpp): a savestate, a plain copy of the corner's state:
//   Mario, the corner's objects (the two tilting pyramids, the track platform and the
//   ending's bully, each optional and off unless asked for, and Mario's own, by the slots
//   they have in the game), the camera, the controller, the timer and the RNG seed, the
//   dynamic surfaces with their partition, the object lists and the frame. The game's
//   structs hold pointers; a state records the addresses its pointers were valid at, and a
//   load into another resource relocates them (the shared level needs none). It is one
//   alternative of the SM64 resources' shared state (Sm64Mem), and converts from the live
//   game, which is how a run on the game hands the corner over
//   (ExportSave<Sm64Mem>(level, objects) and the builder's ImportSave).
// - BitfsCorner: the resource. It owns the live state, a full-size gObjectPool whose other
//   slots never activate, and the frame: the game's update order over the corner's objects
//   (the surfaces, then Mario, then the bully, as sObjectListUpdateOrder has it), with the
//   camera held at the constant yaw the search plays under (its 8-direction mode; Lakitu's
//   flight is not modelled) and its C-up mode run from the game's own text (C-up enters
//   it, Mario brakes and looks, A, B or another C button leaves it). What the frame
//   reaches that the corner has not got ends in a refusal, an exception naming it, never
//   a guess. One thing it has not got makes no refusal, since Mario never meets it: the
//   level's coins. On the game he may collect one on the way onto the pyramid (the yellow
//   coin on its top, slot 23, at frame 3277 of the fixer's movies), which changes his coin
//   count and heals him, nothing the search reads; the parity harness shows that coin is
//   all the whole game adds along the solution movies.
//
// The frame is the game's own: the decomp's functions copied under src/bitfs-corner/decomp/
// (docs/decomp.md, "The BitFS corner's copies") and compiled into BitfsCornerGame.
// test_libsm64_parity.cpp holds it exact against the game, a million frames at a time, and
// tasfw-perf's ^BM_BitfsCorner families measure it beside the game's rows
// (docs/performance.md, "Tier B").

class BitfsCornerGame; // the frame, private to the resource (src/bitfs-corner/BitfsCornerGame.hpp)

class BitfsCorner : public Sm64Resource
{
public:
	// Its savestate limit, taken from the process budget while it lives (ROADMAP 3.5): a
	// state is a few tens of kilobytes, and the search keeps a few dozen per thread.
	static constexpr int64_t SavestateBudgetBytes = int64_t(64) * 1024 * 1024;

	explicit BitfsCorner(std::shared_ptr<const BitfsCornerLevel> level);
	~BitfsCorner() override;
	BitfsCorner(BitfsCorner&&) noexcept; // a vector of corners, one per thread, as the pipeline keeps them; defined where the game is complete
	void save(Sm64Mem& state) const override;
	void load(const Sm64Mem& state) override;
	void advance() override;
	void setInputs(const Inputs& inputs) override;
	void* addr(const char* symbol) const override;
	std::size_t getStateSize(const Sm64Mem& state) const override;
	uint32_t getCurrentFrame() const override;

	const BitfsCornerLevel& Level() const { return *_level; }

	// Whether a refusal (a frame reaching what the corner has not got, a symbol it does not
	// serve) is written to stderr before it is thrown. Off by default: the tests and the
	// parity harness catch refusals by the thousand. The pipeline turns it on, since a
	// refusal inside a search thread ends the process without a word (ROADMAP 4.14).
	void ReportRefusals(bool on);

private:
	// The save and the load on the corner's own alternative of the shared state.
	void SaveCorner(BitfsCornerMem& state) const;
	void LoadCorner(const BitfsCornerMem& state);

	// A pointer of a loaded state moved from the ranges it was saved under to this resource's.
	void Relocate(const BitfsCornerMem& from, void** pointer) const;
	template <class T>
	void Relocate(const BitfsCornerMem& from, T*& pointer) const
	{
		Relocate(from, reinterpret_cast<void**>(&pointer));
	}
	void RelocateObject(const BitfsCornerMem& from, Object& object) const;
	void RelocateGlobals(const BitfsCornerMem& from);

	std::shared_ptr<const BitfsCornerLevel> _level;
	// The game, the corner of it: the live globals (BitfsCornerMem::Globals, its base), the
	// pool of 240 slots and the copied game code that runs a frame over them; on the heap,
	// allocated once (a third of a megabyte), so a resource on the stack stays small. The
	// symbols the scripts read point into it.
	std::unique_ptr<BitfsCornerGame> _game;
	static constexpr std::size_t PoolBytes = std::size_t(LibSm64ObjectPoolCapacity) * sizeof(Object);
	int16_t _gCurrLevelNum = 0;
	int16_t _gCurrCourseNum = 0;
	int16_t _gCurrAreaIndex = 0;
};

#endif
