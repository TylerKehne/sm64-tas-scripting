#pragma once
// The game's frame for the corner, private to BitfsCorner.cpp (ROADMAP 4.13).
//
// The copied game files under decomp/ are the decomp's own text at the pinned revision
// (scripts/decomp_pin.json, docs/decomp.md), included into the body of one class: their
// functions become its members and the globals they define become its data, so sixteen
// resources in a process share nothing and the text stays diffable against upstream. The
// globals the search's state is made of (BitfsCornerMem::Globals, the base) are defined
// there instead of in the copies; the copies keep the transient ones. What a copy reaches
// that the corner has not got (a level's loader, a spawn, a cutscene, a camera mode other
// than the held 8-direction one and C-up, a sound) is a stub below: a no-op where the
// game's state is untouched, a refusal otherwise, never a guess.

// The decomp's AVOID_UB builds give its known undefined behavior the value the game is
// observed to produce (a missing return that in practice returns the last call's result),
// and choose the portable forms of sm64.h's half-word macros over the big-endian pointer
// trick (which on x64 reads the other half); the game's own build is one. Defined before
// any header, since Sm64.hpp decides on it when first included.
#define AVOID_UB 1

#include "BitfsCorner.hpp"
#include <cstdio>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/SurfaceTerrains.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

// The copies come through the system include path (tasfw-resources/CMakeLists.txt), so
// that their warnings are the decomp's and not the project's.
#include <decomp/Prelude.hpp>
#include <decomp/object_constants.h>
#include <decomp/object_list_processor.h>
#include <decomp/surface_collision.h>
#include <decomp/math_util.h>
#include <decomp/behavior_script.h>
#include <decomp/interaction.h>
#include <decomp/mario.h>
#include <decomp/mario_step.h>
#include <decomp/mario_animation_ids.h>
#include <decomp/level_update.h>
#include <decomp/ingame_menu.h>

// level_update.c's play modes, defined in the file itself (the copied header has none).
#define PLAY_MODE_NORMAL 0
#define PLAY_MODE_PAUSED 2
#include <decomp/camera.h>
#include <decomp/sounds.h>
#include <decomp/course_table.h>
#include <decomp/level_table.h>
#include <decomp/mario_geo_switch_case_ids.h>
#include <decomp/model_ids.h>

class BitfsCornerGame : public BitfsCornerMem::Globals
{
public:
	explicit BitfsCornerGame(const BitfsCornerLevel& level);
	// One game frame over the corner: the controller, the objects in the game's order,
	// the camera held, the geo pass's animation step, the timer.
	void Frame();

	// --- The game's memory beside the state: the level, the pool, the transients.
	const BitfsCornerLevel& level;
	Object gObjectPool[OBJECT_POOL_CAPACITY] = {};
	// The static surfaces are the level's; the game's pool and partition are read through
	// these as its code reads them.
	s32 gNumStaticSurfaces = 0;
	s32 gNumStaticSurfaceNodes = 0;
	SpatialPartitionCell (*gStaticSurfacePartition)[NUM_CELLS] = nullptr;
	Object* gCurrentObject = nullptr;
	const BehaviorScript* gCurBhvCommand = nullptr;
	ObjectNode* gObjectLists = nullptr;
	u32 gObjectCounter = 0;
	s16 gPrevFrameObjectCount = 0;
	s16 gCheckingSurfaceCollisionsForCamera = 0;
	s16 gFindFloorIncludeSurfaceIntangible = 0;
	s32 gNumFindFloorMisses = 0;
	s16* gEnvironmentRegions = nullptr;
	s32 gEnvironmentLevels[20] = {};
	s16 gMarioCurrentRoom = 0;
	s8 gDoorAdjacentRooms[60][2] = {};
	// Behaviors the copies compare an object's against; none of the corner's objects runs
	// them, so no object's pointer equals them.
	const BehaviorScript* bhvDddWarp = nullptr;
	const BehaviorScript* bhvTree = nullptr;
	const BehaviorScript* bhvGiantPole = nullptr;
	const BehaviorScript* bhvJumpingBox = nullptr;
	const BehaviorScript* bhvLllTumblingBridge = nullptr;
	// Behaviors the copies would spawn objects with; the spawn is the refusal.
	const BehaviorScript* bhvBobombBullyDeathSmoke = nullptr;
	const BehaviorScript* bhvMovingYellowCoin = nullptr;
	const BehaviorScript* bhvObjectWaterWave = nullptr;
	const BehaviorScript* bhvObjectBubble = nullptr;
	// The one the bully is: its own copy, so that its checks on itself hold.
	const BehaviorScript* bhvSmallBully = nullptr;
	// obj_behaviors.c's statics: the floor the object step found, whether to orient on it,
	// and a Yoshi the corner has not got.
	Surface* sObjFloor = nullptr;
	s8 sOrientObjWithFloor = TRUE;
	s8 sYoshiDead = FALSE;
	// The display list an oriented object's matrix would go into: a scratch matrix, since
	// the corner draws nothing (the geo pass drops the throw matrix after).
	Mat4 _displayListScratch = {};
	void* alloc_display_list(u32) { return &_displayListScratch; }
	// The save file: read by one floor type the corner has not got (a castle warp).
	s16 gCurrSaveFileNum = 1;
	// level_update.c's: set by a triple jump the search never makes (no A).
	s8 gSpecialTripleJump = FALSE;
	s32 save_file_get_total_star_count(s32, s32, s32)
	{
		Refuse("the save file (a look-up warp floor)");
	}
	// src/engine/graph_node.c's constants.
	Vec3f gVec3fZero = {0.0f, 0.0f, 0.0f};
	Vec3s gVec3sZero = {0, 0, 0};
	Vec3f gVec3fOne = {1.0f, 1.0f, 1.0f};
	// object_list_processor.c's transients: counted per frame, read by nothing the corner
	// runs; the free list only receives (the corner never spawns).
	s16 gNumRoomedObjectsInMarioRoom = 0;
	s16 gNumRoomedObjectsNotInMarioRoom = 0;
	ObjectNode gFreeObjectList = {};
	GraphNode gObjParentGraphNode = {};
	// rendering_graph_node.c's animation globals: what the geo pass sets for the bones it
	// would draw; the frame step beside them is the state.
	u8 gCurAnimType = 0;
	u8 gCurAnimEnabled = 0;
	s16 gCurrAnimFrame = 0;
	f32 gCurAnimTranslationMultiplier = 0.0f;
	u16* gCurrAnimAttribute = nullptr;
	s16* gCurAnimData = nullptr;

	// Says so on stderr first when the resource asks (BitfsCorner::ReportRefusals): a search's
	// thread cannot report the exception, so the process would end without a word.
	bool reportRefusals = false;
	[[noreturn]] void Refuse(const char* what) const
	{
		if (reportRefusals)
		{
			std::fprintf(stderr, "BitfsCorner: refused at frame %lld: %s\n", (long long)frame, what);
			std::fflush(stderr);
		}
		throw std::runtime_error(std::string("BitfsCorner: the frame reached what the corner has not got: ") + what);
	}

	// --- What the copies call that the corner has not got. No-ops where the game's state is
	// untouched (sound, the scene graph, the debug text, the profiler); refusals otherwise.
	void play_sound(s32, f32*) {}
	void stop_sound(u32, f32*) {}
	void set_sound_moving_speed(u8, u8) {}
	void raise_background_noise(s32) {}
	void lower_background_noise(s32) {}
	// --- The pause (ROADMAP 4.22): level_update.c's play modes and ingame_menu.c's pause
	// menu, the state in BitfsCornerMem::Globals. The menu reads the controller the game
	// copies player 1's into (game_init.c's read_controller_inputs), the same here.
	Controller* gPlayer3Controller = nullptr; // gPlayer1Controller, set once
	s16 gDialogID = -1;        // no dialog is ever open on the corner
	s16 gDialogTextAlpha = 0;  // the menu's drawing
	s16 gDialogColorFadeTimer = 0;
	s8 gDialogCameraAngleIndex = 1; // CAM_SELECTION_MARIO; read by the camera options, a refusal
	s16 get_dialog_id() { return gDialogID; }
	// level_update.c: the pause is pressed when START is, Mario tangible and no dialog or
	// warp in progress (the corner has neither).
	u32 pressed_pause()
	{
		u32 val4 = get_dialog_id() >= 0;
		u32 intangible = (gMarioState->action & ACT_FLAG_INTANGIBLE) != 0;
		return !intangible && !val4 && (gPlayer1Controller->buttonPressed & START_BUTTON) ? TRUE : FALSE;
	}
	void set_play_mode(s16 playMode) { sCurrPlayMode = playMode; }
	void level_set_transition(s16 length, void*) { sTransitionTimer = length; }
	// play_mode_paused: the menu's first frame opens it, the frame after the one that
	// closed it returns to play; the exit the menu's second option asks for is a warp.
	s32 play_mode_paused()
	{
		if (gPauseScreenMode == 0)
			set_menu_mode(RENDER_PAUSE_SCREEN);
		else if (gPauseScreenMode == 1)
		{
			raise_background_noise(1);
			gCameraMovementFlags &= ~CAM_MOVE_PAUSE_SCREEN;
			set_play_mode(PLAY_MODE_NORMAL);
		}
		else
			Refuse("the pause menu's exit from the course");
		return 0;
	}
	// The menu's drawing is nothing to the state; the castle's menu (the level is a course),
	// the course-complete screen and a dialog are never reached; the camera's options,
	// the cursor's third line, pick the camera selection the corner has not got.
	void create_dl_ortho_matrix() {}
	void shade_screen() {}
	void render_pause_my_score_coins() {}
	void render_pause_red_coins() {}
	void change_dialog_camera_angle() {} // sets the menu's camera index from the selection, which the corner holds
	void highlight_last_course_complete_stars() { Refuse("the castle's pause menu"); }
	void print_hud_pause_colorful_str() { Refuse("the castle's pause menu"); }
	void render_pause_castle_menu_box(s16, s16) { Refuse("the castle's pause menu"); }
	void render_pause_castle_main_strings(s16, s16) { Refuse("the castle's pause menu"); }
	void render_pause_camera_options(s16, s16, s8*, s16) { Refuse("the pause menu's camera options (the camera's selection)"); }
	s16 render_course_complete_screen() { Refuse("the course-complete screen"); }
	void print_peach_letter_message() { Refuse("a dialog"); }
	void render_dialog_entries() { Refuse("a dialog"); }
	void play_infinite_stairs_music() {}
	void play_cap_music(u16) {}
	void stop_cap_music() {}
	void fadeout_cap_music() {}
	u16 gAudioRandom = 0; // the audio thread's; read by Mario's sounds alone
	Vec3f gGlobalSoundSource = {0.0f, 0.0f, 0.0f};
	void stop_sounds_from_source(f32*) {}
	void cur_obj_play_sound_1(s32) {} // spawn_sound.c's
	void cur_obj_play_sound_2(s32) {}
	void play_puzzle_jingle() {}
	void set_camera_shake_from_point(s16, f32, f32, f32) {}
	void spawn_wind_particles(s16, s16) {} // particles are not modelled
	void spawn_mist_particles() {}
	// Spawns: the corner has the objects it was given and makes none.
	Object* spawn_object(Object*, s32 model, const BehaviorScript*)
	{
		Refuse(("spawning an object (model " + std::to_string(model) + ")").c_str());
	}
	Object* spawn_object_abs_with_rot(Object*, s16, s32 model, const BehaviorScript*, s16, s16, s16, s16, s16, s16)
	{
		Refuse(("spawning an object (model " + std::to_string(model) + ")").c_str());
	}
	void spawn_default_star(f32, f32, f32)
	{
		Refuse("spawning a star");
	}
	// The track platform's balls (object_helpers.c), spawned as it moves or resets: the
	// platform moves when Mario rides it, which the search never does.
	void platform_on_track_update_pos_or_spawn_ball(s32, f32, f32, f32)
	{
		Refuse("the track platform moving (its track balls)");
	}
	void debug_print_speed_action_normal(MarioState*) {}
	s32 gDebugLevelSelect = FALSE;
	// Objects Mario holds or rides: none in the corner, so these are refusals when he does.
	void mario_stop_riding_object(MarioState* m)
	{
		if (m->riddenObj != NULL)
			Refuse("Mario riding an object");
	}
	void mario_drop_held_object(MarioState* m)
	{
		if (m->heldObj != NULL)
			Refuse("Mario holding an object");
	}
	void mario_throw_held_object(MarioState* m)
	{
		if (m->heldObj != NULL)
			Refuse("Mario throwing an object");
	}
	void mario_grab_used_object(MarioState*)
	{
		Refuse("Mario grabbing an object");
	}
	// interaction.c's: a grab happens only when a grabbable object was collided with, which
	// the input flag says; without one it answers no, as the game's does.
	u32 mario_check_object_grab(MarioState* m)
	{
		if (m->input & INPUT_INTERACT_OBJ_GRABBABLE)
			Refuse("Mario grabbing an object");
		return FALSE;
	}
	void mario_blow_off_cap(MarioState*, f32)
	{
		Refuse("Mario's cap flying off (an object the corner cannot spawn)");
	}
	// The camera: held in the 8-direction mode, whose yaw is constant (Lakitu's flight, the
	// position side of update_camera, is not modelled, so pos and focus stay where the
	// import left them), with the C-up mode run from camera.inc: the flags, the mode,
	// Mario's head, and pos and focus from the held ones. A shake is nothing to the state.
	// The statics below are camera.c's that the copied text reaches beside the state's own
	// (BitfsCornerMem::Globals): the position side's, and those of modes the corner has
	// not got, read on paths it refuses.
	void set_camera_shake_from_hit(s16) {}
	PlayerCameraState* sMarioCamState = nullptr; // &gPlayerCameraState[0], set once
	LakituState gLakituState = {};
	u8 sCameraSoundFlags = 0;
	s16 sStatusFlags = 0;
	s16 s2ndRotateFlags = 0;
	s16 sAreaYaw = 0;
	s16 sAreaYawChange = 0;
	f32 sLakituDist = 0.0f;
	s16 sLakituPitch = 0;
	f32 sPanDistance = 0.0f;
	f32 gCameraZoomDist = 800.0f;
	s32 gCurrLevelArea = 0; // the level number times 16 plus the area's index, as reset_camera sets it
	// set_camera_mode's table of mode transitions (sModeTransitions), as a switch over the
	// two modes the corner has: C-up's own update, and the 8-direction mode as the held
	// camera, whose transition point is where it is.
	s32 mode_transition(s16 mode, Camera* c, Vec3f focus, Vec3f pos)
	{
		if (mode == CAMERA_MODE_C_UP)
			return update_c_up(c, focus, pos);
		if (mode == CAMERA_MODE_8_DIRECTIONS)
		{
			vec3f_copy(focus, c->focus);
			vec3f_copy(pos, c->pos);
			return c->yaw;
		}
		Refuse(("the camera's mode changing to " + std::to_string(mode)).c_str());
	}
	// Lakitu's flight from one mode's camera to the next: exit_c_up asks for it after its
	// search for an open direction, in modes the corner has not got.
	void transition_next_state(Camera*, s16)
	{
		Refuse("the camera flying between modes (a mode the corner has not got)");
	}
	void load_level_init_text(u32) {} // a dialog
	NumTimesCalled gNumCalls = {}; // the collision functions' call counts, debug
	// level_update.c's slide timer, which two floor types the corner has not got start
	// and stop: refusals, as the warp above.
	void pss_begin_slide(MarioState*)
	{
		Refuse("a slide timer floor");
	}
	void pss_end_slide(MarioState*)
	{
		Refuse("a slide timer floor");
	}

	// memory.c's load of one of Mario's animations into its buffer: the level holds them
	// all, resolved, so the load is a pointer (set_mario_animation reads targetAnim after).
	s32 load_patchable_table(MarioAnimation* a, u32 index)
	{
		const Animation* animation = level.MarioAnimation(index);
		if (a->currentAnimAddr == reinterpret_cast<const u8*>(animation))
			return FALSE;
		a->targetAnim = const_cast<Animation*>(animation);
		a->currentAnimAddr = reinterpret_cast<u8*>(a->targetAnim);
		return TRUE;
	}
	void geo_remove_child(GraphNode*) {}
	void geo_add_child(GraphNode*, GraphNode*) {}
	void reset_debug_objectinfo() {}
	void stub_debug_5() {}
	void try_print_debug_mario_object_info() {}

	// --- The behavior interpreter's dispatch for the corner's scripts: their loops are
	// BEGIN_LOOP, SET_INT, CALL_NATIVE and END_LOOP (a BREAK ends a static object's); the
	// game's command table is not copied, so any other command is a refusal.
	s32 RunBehaviorCommand(u8 command)
	{
		switch (command)
		{
		case 0x08:
			return bhv_cmd_begin_loop();
		case 0x09:
			return bhv_cmd_end_loop();
		case 0x0A:
			return bhv_cmd_break();
		case 0x0C:
			return bhv_cmd_call_native();
		case 0x10:
			return bhv_cmd_set_int();
		default:
			Refuse(("behavior command 0x" + std::to_string(command) + " of " + ObjectName(gCurrentObject)).c_str());
		}
	}

	// A CALL_NATIVE's target: the game's address, resolved by its position among the
	// natives of the script being run, whose names the script's definition gives
	// (data/behavior_data.c at the pinned revision); the game exports not every one.
	void CallNative(void* gameAddress)
	{
		const BitfsCornerLevel::Behavior* behavior = nullptr;
		for (const BitfsCornerLevel::Behavior& candidate : level.behaviors)
			if (gCurBhvCommand >= candidate.script.data() && gCurBhvCommand < candidate.script.data() + candidate.script.size())
				behavior = &candidate;
		if (behavior == nullptr)
			Refuse(("a native called from a script the level did not copy, by " + ObjectName(gCurrentObject)).c_str());
		size_t ordinal = 0;
		while (ordinal < behavior->natives.size() && behavior->natives[ordinal] != gameAddress)
			ordinal++;
		if (ordinal == behavior->natives.size())
			Refuse(("a native the level did not read in " + behavior->name).c_str());
		if (behavior->name == "bhvMario")
		{
			// try_print_debug_mario_level_info, bhv_mario_update, try_do_mario_debug_object_spawn
			if (ordinal == 1)
				return bhv_mario_update();
			return; // the two debug natives around it print and spawn nothing in a released game
		}
		if (behavior->name == "bhvBitfsTiltingInvertedPyramid")
		{
			// bhv_platform_normals_init, bhv_tilting_inverted_pyramid_loop, load_object_collision_model
			if (ordinal == 0)
				return bhv_platform_normals_init();
			if (ordinal == 1)
				return bhv_tilting_inverted_pyramid_loop();
			return load_object_collision_model();
		}
		if (behavior->name == "bhvSmallBully")
		{
			// bhv_small_bully_init, bhv_bully_loop
			if (ordinal == 0)
				return bhv_small_bully_init();
			return bhv_bully_loop();
		}
		if (behavior->name == "bhvPlatformOnTrack")
		{
			// bhv_platform_on_track_init, bhv_platform_on_track_update: the init chose the
			// path and the collision from the game's tables before the import; running it
			// again would need them.
			if (ordinal == 0)
				Refuse("the track platform's init (run by the game before the import)");
			return bhv_platform_on_track_update();
		}
		Refuse(("native " + std::to_string(ordinal) + " of " + behavior->name + ", whose natives are not named here").c_str());
	}

	std::string ObjectName(const Object* object) const
	{
		if (object == nullptr)
			return "no object";
		for (const BitfsCornerLevel::Behavior& behavior : level.behaviors)
			if (behavior.script.data() == object->behavior)
				return "slot " + std::to_string(object - gObjectPool) + " (" + behavior.name + ")";
		return "slot " + std::to_string(object - gObjectPool);
	}

	// interaction.c's statics the corner never sets (doors, teleports, the PSS slide).
	u8 sDisplayingDoorText = FALSE;
	u8 sJustTeleported = FALSE;
	u8 sPssSlideStarted = FALSE;
	// The level's numbers, as mario.c reads them.
	s16 gCurrLevelNum = 0;
	s16 gCurrCourseNum = 0;
	s16 gCurrAreaIndex = 0;

	[[noreturn]] void RefuseInteraction(u32 interactTypes)
	{
		Refuse(("an interaction of type 0x" + std::to_string(interactTypes) + " (not the bully's)").c_str());
	}
	// A warp (a death, a level exit): the search is over there. The game's returns what the
	// warp decided; the corner's never returns.
	s32 level_trigger_warp(MarioState*, s32 warpOp)
	{
		Refuse(("a warp, op " + std::to_string(warpOp) + " (Mario died or left the level)").c_str());
	}
	// The cutscene actions: squished is the corner's; the rest are refusals, as is anything
	// submerged (the corner has no water).
	s32 mario_execute_cutscene_action(MarioState* m)
	{
		if (check_for_instant_quicksand(m))
			return TRUE;
		switch (m->action)
		{
		case ACT_SQUISHED:
			return act_squished(m);
		default:
			Refuse(("cutscene action 0x" + std::to_string(m->action)).c_str());
		}
	}
	s32 mario_execute_submerged_action(MarioState* m)
	{
		Refuse(("submerged action 0x" + std::to_string(m->action)).c_str());
	}

	// --- The copied game files. object_helpers.inc defines `o` for the behaviors after it.
#include <decomp/math_util.inc>
#include <decomp/surface_collision.inc>
#include <decomp/surface_load.inc>
#include <decomp/platform_displacement.inc>
#include <decomp/game_init.inc>
#include <decomp/area.inc>
#include <decomp/ingame_menu.inc>
#include <decomp/camera.inc>
#include <decomp/rendering_graph_node.inc>
#include <decomp/graph_node.inc>
#include <decomp/behavior_script.inc>
#include <decomp/object_collision.inc>
#include <decomp/spawn_object.inc>
#include <decomp/object_list_processor.inc>
#include <decomp/interaction.inc>
#include <decomp/mario.inc>
#include <decomp/mario_step.inc>
#include <decomp/mario_actions_stationary.inc>
#include <decomp/mario_actions_moving.inc>
#include <decomp/mario_actions_airborne.inc>
#include <decomp/mario_actions_object.inc>
#include <decomp/mario_actions_automatic.inc>
#include <decomp/mario_actions_cutscene.inc>
#include <decomp/object_helpers.inc>
#include <decomp/obj_behaviors.inc>
#include <decomp/obj_behaviors_2.inc>
#include <decomp/tilting_inverted_pyramid.inc>
#include <decomp/platform_on_track.inc>
#include <decomp/bully.inc>
#undef o
};

// math_util.h's min and max would rewrite the standard library's for whatever follows.
#undef min
#undef max
