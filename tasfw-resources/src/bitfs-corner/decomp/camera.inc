void focus_on_mario(Vec3f focus, Vec3f pos, f32 posYOff, f32 focYOff, f32 dist, s16 pitch, s16 yaw) {
    Vec3f marioPos;

    marioPos[0] = sMarioCamState->pos[0];
    marioPos[1] = sMarioCamState->pos[1] + posYOff;
    marioPos[2] = sMarioCamState->pos[2];

    vec3f_set_dist_and_angle(marioPos, pos, dist, pitch + sLakituPitch, yaw);

    focus[0] = sMarioCamState->pos[0];
    focus[1] = sMarioCamState->pos[1] + focYOff;
    focus[2] = sMarioCamState->pos[2];
}

void store_lakitu_cam_info_for_c_up(struct Camera *c) {
    vec3f_copy(sCameraStoreCUp.pos, c->pos);
    vec3f_sub(sCameraStoreCUp.pos, sMarioCamState->pos);
    // Only store the y value, and as an offset from Mario, for some reason
    vec3f_set(sCameraStoreCUp.focus, 0.f, c->focus[1] - sMarioCamState->pos[1], 0.f);
}

/**
 * Start C-Up mode. The actual mode change is handled in update_mario_inputs() in mario.c
 *
 * @see update_mario_inputs
 */
s32 set_mode_c_up(struct Camera *c) {
    if (!(gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {
        gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
        store_lakitu_cam_info_for_c_up(c);
        sCameraSoundFlags &= ~CAM_SOUND_C_UP_PLAYED;
        return 1;
    }
    return 0;
}

/**
 * Zoom the camera out of C-Up mode, avoiding moving into a wall, if possible, by searching for an open
 * direction.
 */
s32 exit_c_up(struct Camera *c) {
    struct Surface *surface;
    Vec3f checkFoc;
    Vec3f curPos;
    // Variables for searching for an open direction
    s32 searching = 0;
    /// The current sector of the circle that we are checking
    s32 sector;
    f32 ceilHeight;
    f32 floorHeight;
    f32 curDist;
    f32 d;
    s16 curPitch;
    s16 curYaw;
    s16 checkYaw = 0;
    Vec3f storePos; // unused
    Vec3f storeFoc; // unused

    if ((gCameraMovementFlags & CAM_MOVE_C_UP_MODE) && !(gCameraMovementFlags & CAM_MOVE_STARTED_EXITING_C_UP)) {
        // Copy the stored pos and focus. This is unused.
        vec3f_copy(storePos, sCameraStoreCUp.pos);
        vec3f_add(storePos, sMarioCamState->pos);
        vec3f_copy(storeFoc, sCameraStoreCUp.focus);
        vec3f_add(storeFoc, sMarioCamState->pos);

        vec3f_copy(checkFoc, c->focus);
        checkFoc[0] = sMarioCamState->pos[0];
        checkFoc[2] = sMarioCamState->pos[2];
        vec3f_get_dist_and_angle(checkFoc, c->pos, &curDist, &curPitch, &curYaw);
        vec3f_copy(curPos, c->pos);
        curDist = 80.f;

        // Search for an open direction to zoom out in, if the camera is changing to close, free roam,
        // or spiral-stairs mode
        if (sModeInfo.lastMode == CAMERA_MODE_SPIRAL_STAIRS || sModeInfo.lastMode == CAMERA_MODE_CLOSE
            || sModeInfo.lastMode == CAMERA_MODE_FREE_ROAM) {
            searching = 1;
            // Check the whole circle around Mario for an open direction to zoom out to
            for (sector = 0; sector < 16 && searching == 1; sector++) {
                vec3f_set_dist_and_angle(checkFoc, curPos, curDist, 0, curYaw + checkYaw);

                // If there are no walls this way,
                if (f32_find_wall_collision(&curPos[0], &curPos[1], &curPos[2], 20.f, 50.f) == 0) {

                    // Start close to Mario, check for walls, floors, and ceilings all the way to the
                    // zoomed out distance
                    for (d = curDist; d < gCameraZoomDist; d += 20.f) {
                        vec3f_set_dist_and_angle(checkFoc, curPos, d, 0, curYaw + checkYaw);

                        // Check if we're zooming out into a floor or ceiling
                        ceilHeight = find_ceil(curPos[0], curPos[1] - 150.f, curPos[2], &surface) + -10.f;
                        if (surface != NULL && ceilHeight < curPos[1]) {
                            break;
                        }
                        floorHeight = find_floor(curPos[0], curPos[1] + 150.f, curPos[2], &surface) + 10.f;
                        if (surface != NULL && floorHeight > curPos[1]) {
                            break;
                        }

                        // Stop checking this direction if there is a wall blocking the way
                        if (f32_find_wall_collision(&curPos[0], &curPos[1], &curPos[2], 20.f, 50.f) == 1) {
                            break;
                        }
                    }

                    // If there was no collision found all the way to the max distance, it's an opening
                    if (d >= gCameraZoomDist) {
                        searching = 0;
                    }
                }

                // Alternate left and right, checking each 1/16th (22.5 degrees) of the circle
                if (searching == 1) {
                    checkYaw = -checkYaw;
                    if (checkYaw < 0) {
                        checkYaw -= 0x1000;
                    } else {
                        checkYaw += 0x1000;
                    }
                }
            }

            // Update the stored focus and pos to the direction found in the search
            if (searching == 0) {
                vec3f_set_dist_and_angle(checkFoc, sCameraStoreCUp.pos, gCameraZoomDist, 0, curYaw + checkYaw);
                vec3f_copy(sCameraStoreCUp.focus, checkFoc);
                vec3f_sub(sCameraStoreCUp.pos, sMarioCamState->pos);
                vec3f_sub(sCameraStoreCUp.focus, sMarioCamState->pos);
            }

            gCameraMovementFlags |= CAM_MOVE_STARTED_EXITING_C_UP;
            transition_next_state(c, 15);
        } else {
            // Let the next camera mode handle it
            gCameraMovementFlags &= ~(CAM_MOVE_STARTED_EXITING_C_UP | CAM_MOVE_C_UP_MODE);
            vec3f_set_dist_and_angle(checkFoc, c->pos, curDist, curPitch, curYaw + checkYaw);
        }
        play_sound_cbutton_down();
    }
    return 0;
}

/**
 * The mode used when C-Up is pressed.
 */
s32 update_c_up(UNUSED struct Camera *c, Vec3f focus, Vec3f pos) {
    s16 pitch = sCUpCameraPitch;
    s16 yaw = sMarioCamState->faceAngle[1] + sModeOffsetYaw + DEGREES(180);

    focus_on_mario(focus, pos, 125.f, 125.f, 250.f, pitch, yaw);
    return sMarioCamState->faceAngle[1];
}

/**
 * Make Mario's head move in C-Up mode.
 */
void move_mario_head_c_up(UNUSED struct Camera *c) {
    UNUSED s16 pitch = sCUpCameraPitch;
    UNUSED s16 yaw = sModeOffsetYaw;

    sCUpCameraPitch += (s16)(gPlayer1Controller->stickY * 10.f);
    sModeOffsetYaw -= (s16)(gPlayer1Controller->stickX * 10.f);

    // Bound looking up to nearly 80 degrees.
    if (sCUpCameraPitch > 0x38E3) {
        sCUpCameraPitch = 0x38E3;
    }
    // Bound looking down to -45 degrees
    if (sCUpCameraPitch < -0x2000) {
        sCUpCameraPitch = -0x2000;
    }

    // Bound the camera yaw to +-120 degrees
    if (sModeOffsetYaw > 0x5555) {
        sModeOffsetYaw = 0x5555;
    }
    if (sModeOffsetYaw < -0x5555) {
        sModeOffsetYaw = -0x5555;
    }

    // Give Mario's neck natural-looking constraints
    sMarioCamState->headRotation[0] = sCUpCameraPitch * 3 / 4;
    sMarioCamState->headRotation[1] = sModeOffsetYaw * 3 / 4;
}

/**
 * Zooms the camera in for C-Up mode
 */
void move_into_c_up(struct Camera *c) {
    struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    f32 dist  = end->dist  - start->dist;
    s16 pitch = end->pitch - start->pitch;
    s16 yaw   = end->yaw   - start->yaw;

    // Linearly interpolate from start to end position's polar coordinates
    dist  = start->dist  + dist  * sModeInfo.frame / sModeInfo.max;
    pitch = start->pitch + pitch * sModeInfo.frame / sModeInfo.max;
    yaw   = start->yaw   + yaw   * sModeInfo.frame / sModeInfo.max;

    // Linearly interpolate the focus from start to end
    c->focus[0] = start->focus[0] + (end->focus[0] - start->focus[0]) * sModeInfo.frame / sModeInfo.max;
    c->focus[1] = start->focus[1] + (end->focus[1] - start->focus[1]) * sModeInfo.frame / sModeInfo.max;
    c->focus[2] = start->focus[2] + (end->focus[2] - start->focus[2]) * sModeInfo.frame / sModeInfo.max;

    vec3f_add(c->focus, sMarioCamState->pos);
    vec3f_set_dist_and_angle(c->focus, c->pos, dist, pitch, yaw);

    sMarioCamState->headRotation[0] = 0;
    sMarioCamState->headRotation[1] = 0;

    // Finished zooming in
    if (++sModeInfo.frame == sModeInfo.max) {
        gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;
    }
}

/**
 * The main update function for C-Up mode
 */
s32 mode_c_up_camera(struct Camera *c) {
    UNUSED u8 unused[12];

    // Play a sound when entering C-Up mode
    if (!(sCameraSoundFlags & CAM_SOUND_C_UP_PLAYED)) {
        play_sound_cbutton_up();
        sCameraSoundFlags |= CAM_SOUND_C_UP_PLAYED;
    }

    // Zoom in first
    if (gCameraMovementFlags & CAM_MOVING_INTO_MODE) {
        gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
        move_into_c_up(c);
        return 1;
    }

    if (!(gCameraMovementFlags & CAM_MOVE_STARTED_EXITING_C_UP)) {
        // Normal update
        move_mario_head_c_up(c);
        update_c_up(c, c->focus, c->pos);
    } else {
        // Exiting C-Up
        if (sStatusFlags & CAM_FLAG_TRANSITION_OUT_OF_C_UP) {
            // Retrieve the previous position and focus
            vec3f_copy(c->pos, sCameraStoreCUp.pos);
            vec3f_add(c->pos, sMarioCamState->pos);
            vec3f_copy(c->focus, sCameraStoreCUp.focus);
            vec3f_add(c->focus, sMarioCamState->pos);
            // Make Mario look forward
            camera_approach_s16_symmetric_bool(&sMarioCamState->headRotation[0], 0, 1024);
            camera_approach_s16_symmetric_bool(&sMarioCamState->headRotation[1], 0, 1024);
        } else {
            // Finished exiting C-Up
            gCameraMovementFlags &= ~(CAM_MOVE_STARTED_EXITING_C_UP | CAM_MOVE_C_UP_MODE);
        }
    }
    sPanDistance = 0.f;

    // Exit C-Up mode
    if (gPlayer1Controller->buttonPressed & (A_BUTTON | B_BUTTON | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS)) {
        exit_c_up(c);
    }
    return 0;
}

/**
 * Used to change the camera mode between its default/previous and certain Mario-oriented modes,
 *      namely: C_UP, WATER_SURFACE, CLOSE, and BEHIND_MARIO
 *
 * Stores the current pos and focus in sModeInfo->transitionStart, and
 * stores the next pos and focus into sModeInfo->transitionEnd. These two fields are used in
 * move_into_c_up().
 *
 * @param mode the mode to change to, or -1 to switch to the previous mode
 * @param frames number of frames the transition should last, only used when entering C_UP
 */
void set_camera_mode(struct Camera *c, s16 mode, s16 frames) {
    struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    if (mode == CAMERA_MODE_WATER_SURFACE && gCurrLevelArea == AREA_TTM_OUTSIDE) {
    } else {
        // Clear movement flags that would affect the transition
        gCameraMovementFlags &= (u16)~(CAM_MOVE_RESTRICT | CAM_MOVE_ROTATE);
        gCameraMovementFlags |= CAM_MOVING_INTO_MODE;
        if (mode == CAMERA_MODE_NONE) {
            mode = CAMERA_MODE_CLOSE;
        }
        sCUpCameraPitch = 0;
        sModeOffsetYaw = 0;
        sLakituDist = 0;
        sLakituPitch = 0;
        sAreaYawChange = 0;

        sModeInfo.newMode = (mode != -1) ? mode : sModeInfo.lastMode;
        sModeInfo.lastMode = c->mode;
        sModeInfo.max = frames;
        sModeInfo.frame = 1;

        c->mode = sModeInfo.newMode;
        gLakituState.mode = c->mode;

        vec3f_copy(end->focus, c->focus);
        vec3f_sub(end->focus, sMarioCamState->pos);

        vec3f_copy(end->pos, c->pos);
        vec3f_sub(end->pos, sMarioCamState->pos);

        sAreaYaw = mode_transition(sModeInfo.newMode, c, end->focus, end->pos);

        // End was updated by sModeTransitions
        vec3f_sub(end->focus, sMarioCamState->pos);
        vec3f_sub(end->pos, sMarioCamState->pos);

        vec3f_copy(start->focus, gLakituState.curFocus);
        vec3f_sub(start->focus, sMarioCamState->pos);

        vec3f_copy(start->pos, gLakituState.curPos);
        vec3f_sub(start->pos, sMarioCamState->pos);

        vec3f_get_dist_and_angle(start->focus, start->pos, &start->dist, &start->pitch, &start->yaw);
        vec3f_get_dist_and_angle(end->focus, end->pos, &end->dist, &end->pitch, &end->yaw);
    }
}

void vec3f_sub(Vec3f dst, Vec3f src) {
    dst[0] -= src[0];
    dst[1] -= src[1];
    dst[2] -= src[2];
}

s32 camera_approach_s16_symmetric_bool(s16 *current, s16 target, s16 increment) {
    s16 dist = target - *current;

    if (increment < 0) {
        increment = -1 * increment;
    }
    if (dist > 0) {
        dist -= increment;
        if (dist >= 0) {
            *current = target - dist;
        } else {
            *current = target;
        }
    } else {
        dist += increment;
        if (dist <= 0) {
            *current = target - dist;
        } else {
            *current = target;
        }
    }
    if (*current == target) {
        return FALSE;
    } else {
        return TRUE;
    }
}

void play_sound_cbutton_up(void) {
    play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
}

void play_sound_cbutton_down(void) {
    play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
}

void play_sound_cbutton_side(void) {
    play_sound(SOUND_MENU_CAMERA_TURN, gGlobalSoundSource);
}

void play_sound_button_change_blocked(void) {
    play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
}

/**
 * Handles input for radial, outwards radial, parallel tracking, and 8 direction mode.
 */
s32 radial_camera_input(struct Camera *c, UNUSED f32 unused) {
    s16 dummy = 0; // the corner's edit: the decomp returns it uninitialized, and no caller reads it

    if ((gCameraMovementFlags & CAM_MOVE_ENTERED_ROTATE_SURFACE) || !(gCameraMovementFlags & CAM_MOVE_ROTATE)) {

        // If C-L or C-R are pressed, the camera is rotating
        if (gPlayer1Controller->buttonPressed & (L_CBUTTONS | R_CBUTTONS)) {
            gCameraMovementFlags &= ~CAM_MOVE_ENTERED_ROTATE_SURFACE;
            //  @bug this does not clear the rotation flags set by the surface. It's possible to set
            //       both ROTATE_LEFT and ROTATE_RIGHT, locking the camera.
            //       Ex: If a surface set CAM_MOVE_ROTATE_RIGHT and the user presses C-R, it locks the
            //       camera until a different mode is activated
        }

        // Rotate Right and left
        if (gPlayer1Controller->buttonPressed & R_CBUTTONS) {
            if (sModeOffsetYaw > -0x800) {
                // The camera is now rotating right
                if (!(gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT)) {
                    gCameraMovementFlags |= CAM_MOVE_ROTATE_RIGHT;
                }

                if (c->mode == CAMERA_MODE_RADIAL) {
                    // if > ~48 degrees, we're rotating for the second time.
                    if (sModeOffsetYaw > 0x22AA) {
                        s2ndRotateFlags |= CAM_MOVE_ROTATE_RIGHT;
                    }

                    if (sModeOffsetYaw == DEGREES(105)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                } else {
                    if (sModeOffsetYaw == DEGREES(60)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                }
            } else {
                gCameraMovementFlags |= CAM_MOVE_RETURN_TO_MIDDLE;
                play_sound_cbutton_up();
            }
        }
        if (gPlayer1Controller->buttonPressed & L_CBUTTONS) {
            if (sModeOffsetYaw < 0x800) {
                if (!(gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT)) {
                    gCameraMovementFlags |= CAM_MOVE_ROTATE_LEFT;
                }

                if (c->mode == CAMERA_MODE_RADIAL) {
                    // if < ~48 degrees, we're rotating for the second time.
                    if (sModeOffsetYaw < -0x22AA) {
                        s2ndRotateFlags |= CAM_MOVE_ROTATE_LEFT;
                    }

                    if (sModeOffsetYaw == DEGREES(-105)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                } else {
                    if (sModeOffsetYaw == DEGREES(-60)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                }
            } else {
                gCameraMovementFlags |= CAM_MOVE_RETURN_TO_MIDDLE;
                play_sound_cbutton_up();
            }
        }
    }

    // Zoom in / enter C-Up
    if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            gCameraMovementFlags &= ~CAM_MOVE_ZOOMED_OUT;
            play_sound_cbutton_up();
        } else {
            set_mode_c_up(c);
        }
    }

    // Zoom out
    if (gPlayer1Controller->buttonPressed & D_CBUTTONS) {
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            gCameraMovementFlags |= CAM_MOVE_ALREADY_ZOOMED_OUT;
        } else {
            gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
            play_sound_cbutton_down();
        }
    }

    //! returning uninitialized variable
    return dummy;
}
