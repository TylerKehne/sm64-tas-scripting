#pragma once

enum class MovementOption
{
    // Joystick mag
    MAX_MAGNITUDE,
    ZERO_MAGNITUDE,
    SAME_MAGNITUDE,
    RANDOM_MAGNITUDE,

    // Input angle
    MATCH_FACING_YAW,
    ANTI_FACING_YAW,
    SAME_YAW,
    RANDOM_YAW,

    // Buttons
    SAME_BUTTONS,
    NO_BUTTONS,
    RANDOM_BUTTONS,

    // Scripts
    NO_SCRIPT,
    PBD,
    RUN_DOWNHILL,
    REWIND,
    TURN_UPHILL,
    TURN_AROUND
};