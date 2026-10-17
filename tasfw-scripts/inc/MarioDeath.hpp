#pragma once
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>

#include <cstdint>

#ifndef MARIODEATH_H
#define MARIODEATH_H

// Whether Mario is dying: the state every BitFS script treats as a failed trial (the
// maintainer, 2026-10-12: any death action simply fails validation). The game plays a death
// out and reloads the level some 48 frames after it triggers the warp; no lineage of the
// squish-cancel search survives a death, so a script stops at the first dying frame and the
// reload is never reached, on the game or on the corner resource, which has no level to
// reload and would refuse there.
//
// Dying is: a lava boost (the lava ends every BitFS lineage, and the game triggers the death
// warp from the boost itself once his health is gone, with no death action in between); his
// health below one segment, which the next action change turns into a death action; a death
// action; or no floor under him, on which the game warps at its next frame.
namespace MarioDeath
{
	inline bool IsDeathAction(uint32_t action)
	{
		switch (action)
		{
		case ACT_STANDING_DEATH:
		case ACT_QUICKSAND_DEATH:
		case ACT_ELECTROCUTION:
		case ACT_SUFFOCATION:
		case ACT_DEATH_ON_STOMACH:
		case ACT_DEATH_ON_BACK:
		case ACT_EATEN_BY_BUBBA:
		case ACT_DROWNING:
		case ACT_WATER_DEATH:
		case ACT_DEATH_EXIT:
		case ACT_UNUSED_DEATH_EXIT:
		case ACT_FALLING_DEATH_EXIT:
		case ACT_SPECIAL_DEATH_EXIT:
		case ACT_DEATH_EXIT_LAND:
			return true;
		default:
			return false;
		}
	}

	inline bool IsDying(const MarioState& mario)
	{
		return mario.action == ACT_LAVA_BOOST || mario.health < 0x100 || IsDeathAction(mario.action) || mario.floor == nullptr;
	}
}

#endif
