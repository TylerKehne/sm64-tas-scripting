enum DialogBoxState {
    DIALOG_STATE_OPENING,
    DIALOG_STATE_VERTICAL,
    DIALOG_STATE_HORIZONTAL,
    DIALOG_STATE_CLOSING
};

void handle_menu_scrolling(s8 scrollDirection, s8 *currentIndex, s8 minIndex, s8 maxIndex) {
    u8 index = 0;

    if (scrollDirection == MENU_SCROLL_VERTICAL) {
        if (gPlayer3Controller->rawStickY > 60) {
            index++;
        }

        if (gPlayer3Controller->rawStickY < -60) {
            index += 2;
        }
    } else if (scrollDirection == MENU_SCROLL_HORIZONTAL) {
        if (gPlayer3Controller->rawStickX > 60) {
            index += 2;
        }

        if (gPlayer3Controller->rawStickX < -60) {
            index++;
        }
    }

    if (((index ^ gMenuHoldKeyIndex) & index) == 2) {
        if (currentIndex[0] == maxIndex) {
            //! Probably originally a >=, but later replaced with an == and an else statement.
            currentIndex[0] = maxIndex;
        } else {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            currentIndex[0]++;
        }
    }

    if (((index ^ gMenuHoldKeyIndex) & index) == 1) {
        if (currentIndex[0] == minIndex) {
            // Same applies to here as above
        } else {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            currentIndex[0]--;
        }
    }

    if (gMenuHoldKeyTimer == 10) {
        gMenuHoldKeyTimer = 8;
        gMenuHoldKeyIndex = 0;
    } else {
        gMenuHoldKeyTimer++;
        gMenuHoldKeyIndex = index;
    }

    if ((index & 3) == 0) {
        gMenuHoldKeyTimer = 0;
    }
}

void set_menu_mode(s16 mode) {
    if (gMenuMode == -1) {
        gMenuMode = mode;
    }
}

void render_pause_course_options(s16 x, s16 y, s8 *index, s16 yIndex) {
    handle_menu_scrolling(MENU_SCROLL_VERTICAL, index, 1, 3);

    if (index[0] == 3) {
        render_pause_camera_options(x - 42, y - 42, &gDialogCameraAngleIndex, 110);
    }
}

s16 render_pause_courses_and_castle(void) {
    s16 num;

    switch (gDialogBoxState) {
        case DIALOG_STATE_OPENING:
            gDialogLineNum = 1;
            gDialogTextAlpha = 0;
            level_set_transition(-1, NULL);
            play_sound(SOUND_MENU_PAUSE, gGlobalSoundSource);

            if (gCurrCourseNum >= COURSE_MIN && gCurrCourseNum <= COURSE_MAX) {
                change_dialog_camera_angle();
                gDialogBoxState = DIALOG_STATE_VERTICAL;
            } else {
                highlight_last_course_complete_stars();
                gDialogBoxState = DIALOG_STATE_HORIZONTAL;
            }
            break;
        case DIALOG_STATE_VERTICAL:
            shade_screen();
            render_pause_my_score_coins();
            render_pause_red_coins();

            if (gMarioStates[0].action & ACT_FLAG_PAUSE_EXIT) {
                render_pause_course_options(99, 93, &gDialogLineNum, 15);
            }

            if (gPlayer3Controller->buttonPressed & A_BUTTON
             || gPlayer3Controller->buttonPressed & START_BUTTON)
            {
                level_set_transition(0, NULL);
                play_sound(SOUND_MENU_PAUSE_2, gGlobalSoundSource);
                gDialogBoxState = DIALOG_STATE_OPENING;
                gMenuMode = -1;

                if (gDialogLineNum == 2) {
                    num = gDialogLineNum;
                } else {
                    num = 1;
                }

                return num;
            }
            break;
        case DIALOG_STATE_HORIZONTAL:
            shade_screen();
            print_hud_pause_colorful_str();
            render_pause_castle_menu_box(160, 143);
            render_pause_castle_main_strings(104, 60);

            if (gPlayer3Controller->buttonPressed & A_BUTTON
             || gPlayer3Controller->buttonPressed & START_BUTTON)
            {
                level_set_transition(0, NULL);
                play_sound(SOUND_MENU_PAUSE_2, gGlobalSoundSource);
                gMenuMode = -1;
                gDialogBoxState = DIALOG_STATE_OPENING;

                return 1;
            }
            break;
    }

    if (gDialogTextAlpha < 250) {
        gDialogTextAlpha += 25;
    }

    return 0;
}

s16 render_menus_and_dialogs(void) {
    s16 mode = 0;

    create_dl_ortho_matrix();

    if (gMenuMode != -1) {
        switch (gMenuMode) {
            case 0:
                mode = render_pause_courses_and_castle();
                break;
            case 1:
                mode = render_pause_courses_and_castle();
                break;
            case 2:
                mode = render_course_complete_screen();
                break;
            case 3:
                mode = render_course_complete_screen();
                break;
        }

        gDialogColorFadeTimer = (s16) gDialogColorFadeTimer + 0x1000;
    } else if (gDialogID != -1) {
        // The Peach "Dear Mario" message needs to be repositioned separately
        if (gDialogID == 20) {
            print_peach_letter_message();
            return mode;
        }

        render_dialog_entries();
        gDialogColorFadeTimer = (s16) gDialogColorFadeTimer + 0x1000;
    }
    return mode;
}
