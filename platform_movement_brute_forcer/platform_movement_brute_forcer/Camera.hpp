#pragma once
#include "UltraTypes.hpp"
#include "Types.hpp"

/**
 * The main camera struct. Gets updated by the active camera mode and the current level/area. In
 * update_lakitu, its pos and focus are used to calculate lakitu's next position and focus, which are
 * then used to render the game->
 */
struct Camera
{
    /*0x00*/ u8 mode; // What type of mode the camera uses (see defines above)
    /*0x01*/ u8 defMode;
    /**
     * Determines what direction Mario moves in when the analog stick is moved.
     *
     * @warning This is NOT the camera's xz-rotation in world space. This is the angle calculated from the
     *          camera's focus TO the camera's position, instead of the other way around like it should
     *          be. It's effectively the opposite of the camera's actual yaw. Use
     *          vec3f_get_dist_and_angle() if you need the camera's yaw.
     */
    /*0x02*/ s16 yaw;
    /*0x04*/ Vec3f focus;
    /*0x10*/ Vec3f pos;
    /*0x1C*/ Vec3f unusedVec1;
    /// The x coordinate of the "center" of the area. The camera will rotate around this point.
    /// For example, this is what makes the camera rotate around the hill in BoB
    /*0x28*/ f32 areaCenX;
    /// The z coordinate of the "center" of the area. The camera will rotate around this point.
    /// For example, this is what makes the camera rotate around the hill in BoB
    /*0x2C*/ f32 areaCenZ;
    /*0x30*/ u8 cutscene;
    /*0x31*/ u8 filler31[0x8];
    /*0x3A*/ s16 nextYaw;
    /*0x3C*/ u8 filler3C[0x28];
    /*0x64*/ u8 doorStatus;
    /// The y coordinate of the "center" of the area. Unlike areaCenX and areaCenZ, this is only used
    /// when paused. See zoom_out_if_paused_and_outside
    /*0x68*/ f32 areaCenY;
};