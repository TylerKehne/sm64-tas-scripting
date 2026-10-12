#pragma once
#include <sm64/Types.hpp>

// struct Area alone, out of the decomp's src/game/area.h (scripts/decomp_pin.json); what it
// points at beside the camera is left on forward declarations.

struct GraphNodeRoot;
struct ObjectWarpNode;
struct WarpNode;
struct InstantWarp;
struct SpawnInfo;
struct Camera;
struct UnusedArea28;
struct Whirlpool;

struct Area
{
	/*0x00*/ s8 index;
	/*0x01*/ s8 flags;	// Only has 1 flag: 0x01 = Is this the active area?
	/*0x02*/ u16 terrainType;	// default terrain of the level (set from level script cmd 0x31)
	/*0x04*/ struct GraphNodeRoot* unk04;	// geometry layout data
	/*0x08*/ s16* terrainData;	// collision data (set from level script cmd 0x2E)
	/*0x0C*/ s8* surfaceRooms;	// (set from level script cmd 0x2F)
	/*0x10*/ s16* macroObjects;	// Macro Objects Ptr (set from level script cmd 0x39)
	/*0x14*/ struct ObjectWarpNode* warpNodes;
	/*0x18*/ struct WarpNode* paintingWarpNodes;
	/*0x1C*/ struct InstantWarp* instantWarps;
	/*0x20*/ struct SpawnInfo* objectSpawnInfos;
	/*0x24*/ struct Camera* camera;
	/*0x28*/ struct UnusedArea28* unused28;	// Filled by level script 0x3A, but is unused.
	/*0x2C*/ struct Whirlpool* whirlpools[2];
	/*0x34*/ u8 dialog[2];	// Level start dialog number (set by level script cmd 0x30)
	/*0x36*/ u16 musicParam;
	/*0x38*/ u16 musicParam2;
};
