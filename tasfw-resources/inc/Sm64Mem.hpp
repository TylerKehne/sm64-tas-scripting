#pragma once
#include <tasfw/Resource.hpp>
#include "BitfsCornerMem.hpp"
#include "LibSm64Mem.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#ifndef SM64MEM_H
#define SM64MEM_H

// The one savestate type of the SM64 resources (Sm64Resource.hpp). The framework fixes a
// resource's state type once (Resource<TState>), so for a script to run on the game
// (LibSm64) or on the corner (BitfsCorner) without being written twice, the two share this
// type: a state holds the alternative of the resource that wrote it, or nothing until a save
// fills it, and a resource handed the other resource's alternative refuses (the maintainer,
// 2026-10-12). The corner's alternative lives behind a pointer: inline, its 41 KB would make
// every game slot's node that much larger and the fresh-slot rows that much slower (the
// 2026-10-12 table in docs/performance-changelog.md); behind a pointer the block is made
// once per fresh corner slot and the pool recycles it, since the slot manager never copies
// or moves a state.
class Sm64Mem
{
public:
	Sm64Mem() = default;
	Sm64Mem(LibSm64Mem state) : _state(std::move(state)) { }
	Sm64Mem(BitfsCornerMem state) : _state(std::make_unique<BitfsCornerMem>(std::move(state))) { }
	// The corner's state from the live game: what a script on the game exports for a run on
	// the corner, ExportSave<Sm64Mem>(level, objects), and the builder's ImportSave takes.
	Sm64Mem(const Resource<Sm64Mem>& game, const BitfsCornerLevel& level, BitfsCornerObjects objects = {})
		: _state(std::make_unique<BitfsCornerMem>(game, level, objects))
	{
	}
	// A copy is the alternative's copy; a move takes it over.
	Sm64Mem(const Sm64Mem& other) { *this = other; }
	Sm64Mem& operator=(const Sm64Mem& other)
	{
		if (const LibSm64Mem* game = std::get_if<LibSm64Mem>(&other._state))
			_state = *game;
		else if (const CornerPtr* corner = std::get_if<CornerPtr>(&other._state))
			_state = std::make_unique<BitfsCornerMem>(**corner);
		else
			_state = std::monostate();
		return *this;
	}
	Sm64Mem(Sm64Mem&&) = default;
	Sm64Mem& operator=(Sm64Mem&&) = default;

	bool IsGame() const { return std::holds_alternative<LibSm64Mem>(_state); }
	bool IsCorner() const { return std::holds_alternative<CornerPtr>(_state); }

	// The alternative a resource writes: the one already there, or a fresh one in place of
	// nothing. Writing over the other resource's alternative is refused.
	LibSm64Mem& Game()
	{
		if (std::holds_alternative<std::monostate>(_state))
			_state.emplace<LibSm64Mem>();
		return const_cast<LibSm64Mem&>(std::as_const(*this).Game());
	}
	BitfsCornerMem& Corner()
	{
		if (std::holds_alternative<std::monostate>(_state))
			_state.emplace<CornerPtr>(std::make_unique<BitfsCornerMem>());
		return const_cast<BitfsCornerMem&>(std::as_const(*this).Corner());
	}
	// The alternative a resource reads; a refusal when the state is the other resource's or
	// has not been written.
	const LibSm64Mem& Game() const
	{
		if (const LibSm64Mem* game = std::get_if<LibSm64Mem>(&_state))
			return *game;
		Refuse("the game");
	}
	const BitfsCornerMem& Corner() const
	{
		if (const CornerPtr* corner = std::get_if<CornerPtr>(&_state))
			return **corner;
		Refuse("the corner");
	}

	// The slot manager erasing this state's slot (SlotManager.hpp): the game's state gives
	// its baseline back; the corner's has nothing to give.
	void dispose()
	{
		if (LibSm64Mem* game = std::get_if<LibSm64Mem>(&_state))
			game->dispose();
	}

private:
	using CornerPtr = std::unique_ptr<BitfsCornerMem>;

	[[noreturn]] void Refuse(const char* whose) const
	{
		throw std::runtime_error(std::string("Sm64Mem: the state is not ") + whose + "'s"
			+ (std::holds_alternative<std::monostate>(_state) ? " (it has not been written)" : " (it is the other resource's)"));
	}

	std::variant<std::monostate, LibSm64Mem, CornerPtr> _state;
};

#endif
