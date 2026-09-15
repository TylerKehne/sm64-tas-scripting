#pragma once

// The framework's input groups, which RandomInputs reads. A search's own moves are its
// public nested `enum class CustomMoves` (Scattershot.hpp), through the same three calls.
enum class BasicMoves
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
    RANDOM_BUTTONS
};
