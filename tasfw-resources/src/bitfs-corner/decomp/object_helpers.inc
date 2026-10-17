#define o gCurrentObject

void obj_apply_scale_to_matrix(struct Object *obj, Mat4 dst, Mat4 src) {
    dst[0][0] = src[0][0] * obj->header.gfx.scale[0];
    dst[1][0] = src[1][0] * obj->header.gfx.scale[1];
    dst[2][0] = src[2][0] * obj->header.gfx.scale[2];
    dst[3][0] = src[3][0];

    dst[0][1] = src[0][1] * obj->header.gfx.scale[0];
    dst[1][1] = src[1][1] * obj->header.gfx.scale[1];
    dst[2][1] = src[2][1] * obj->header.gfx.scale[2];
    dst[3][1] = src[3][1];

    dst[0][2] = src[0][2] * obj->header.gfx.scale[0];
    dst[1][2] = src[1][2] * obj->header.gfx.scale[1];
    dst[2][2] = src[2][2] * obj->header.gfx.scale[2];
    dst[3][2] = src[3][2];

    dst[0][3] = src[0][3];
    dst[1][3] = src[1][3];
    dst[2][3] = src[2][3];
    dst[3][3] = src[3][3];
}

f32 lateral_dist_between_objects(struct Object *obj1, struct Object *obj2) {
    f32 dx = obj1->oPosX - obj2->oPosX;
    f32 dz = obj1->oPosZ - obj2->oPosZ;

    return sqrtf(dx * dx + dz * dz);
}

f32 dist_between_objects(struct Object *obj1, struct Object *obj2) {
    f32 dx = obj1->oPosX - obj2->oPosX;
    f32 dy = obj1->oPosY - obj2->oPosY;
    f32 dz = obj1->oPosZ - obj2->oPosZ;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

f32 approach_f32_symmetric(f32 value, f32 target, f32 increment) {
    f32 dist;

    if ((dist = target - value) >= 0.0f) {
        if (dist > increment) {
            value += increment;
        } else {
            value = target;
        }
    } else {
        if (dist < -increment) {
            value -= increment;
        } else {
            value = target;
        }
    }

    return value;
}

s16 approach_s16_symmetric(s16 value, s16 target, s16 increment) {
    s16 dist = target - value;

    if (dist >= 0) {
        if (dist > increment) {
            value += increment;
        } else {
            value = target;
        }
    } else {
        if (dist < -increment) {
            value -= increment;
        } else {
            value = target;
        }
    }

    return value;
}

s32 cur_obj_rotate_yaw_toward(s16 target, s16 increment) {
    s16 startYaw;

    startYaw = (s16) o->oMoveAngleYaw;
    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, target, increment);

    if ((o->oAngleVelYaw = (s16)((s16) o->oMoveAngleYaw - startYaw)) == 0) {
        return TRUE;
    } else {
        return FALSE;
    }
}

s16 obj_angle_to_object(struct Object *obj1, struct Object *obj2) {
    f32 z1, x1, z2, x2;
    s16 angle;

    z1 = obj1->oPosZ; z2 = obj2->oPosZ; // ordering of instructions..
    x1 = obj1->oPosX; x2 = obj2->oPosX;

    angle = atan2s(z2 - z1, x2 - x1);
    return angle;
}

s16 obj_turn_toward_object(struct Object *obj, struct Object *target, s16 angleIndex, s16 turnAmount) {
    f32 a, b, c, d;
    UNUSED s32 unused;
    s16 targetAngle, startAngle;

    switch (angleIndex) {
        case O_MOVE_ANGLE_PITCH_INDEX:
        case O_FACE_ANGLE_PITCH_INDEX:
            a = target->oPosX - obj->oPosX;
            c = target->oPosZ - obj->oPosZ;
            a = sqrtf(a * a + c * c);

            b = -obj->oPosY;
            d = -target->oPosY;

            targetAngle = atan2s(a, d - b);
            break;

        case O_MOVE_ANGLE_YAW_INDEX:
        case O_FACE_ANGLE_YAW_INDEX:
            a = obj->oPosZ;
            c = target->oPosZ;
            b = obj->oPosX;
            d = target->oPosX;

            targetAngle = atan2s(c - a, d - b);
            break;
    }

    startAngle = o->rawData.asU32[angleIndex];
    o->rawData.asU32[angleIndex] = approach_s16_symmetric(startAngle, targetAngle, turnAmount);
    return targetAngle;
}

void obj_set_pos(struct Object *obj, s16 x, s16 y, s16 z) {
    obj->oPosX = x;
    obj->oPosY = y;
    obj->oPosZ = z;
}

void obj_set_angle(struct Object *obj, s16 pitch, s16 yaw, s16 roll) {
    obj->oFaceAnglePitch = pitch;
    obj->oFaceAngleYaw = yaw;
    obj->oFaceAngleRoll = roll;

    obj->oMoveAnglePitch = pitch;
    obj->oMoveAngleYaw = yaw;
    obj->oMoveAngleRoll = roll;
}

void obj_copy_pos_and_angle(struct Object *dst, struct Object *src) {
    obj_copy_pos(dst, src);
    obj_copy_angle(dst, src);
}

void obj_copy_pos(struct Object *dst, struct Object *src) {
    dst->oPosX = src->oPosX;
    dst->oPosY = src->oPosY;
    dst->oPosZ = src->oPosZ;
}

void obj_copy_angle(struct Object *dst, struct Object *src) {
    dst->oMoveAnglePitch = src->oMoveAnglePitch;
    dst->oMoveAngleYaw = src->oMoveAngleYaw;
    dst->oMoveAngleRoll = src->oMoveAngleRoll;

    dst->oFaceAnglePitch = src->oFaceAnglePitch;
    dst->oFaceAngleYaw = src->oFaceAngleYaw;
    dst->oFaceAngleRoll = src->oFaceAngleRoll;
}

void obj_set_gfx_pos_from_pos(struct Object *obj) {
    obj->header.gfx.pos[0] = obj->oPosX;
    obj->header.gfx.pos[1] = obj->oPosY;
    obj->header.gfx.pos[2] = obj->oPosZ;
}

/**
 * Multiply a vector by a matrix of the form
 * | ? ? ? 0 |
 * | ? ? ? 0 |
 * | ? ? ? 0 |
 * | 0 0 0 1 |
 * i.e. a matrix representing a linear transformation over 3 space.
 */
void linear_mtxf_mul_vec3f(Mat4 m, Vec3f dst, Vec3f v) {
    s32 i;
    for (i = 0; i < 3; i++) {
        dst[i] = m[0][i] * v[0] + m[1][i] * v[1] + m[2][i] * v[2];
    }
}

/**
 * Multiply a vector by the transpose of a matrix of the form
 * | ? ? ? 0 |
 * | ? ? ? 0 |
 * | ? ? ? 0 |
 * | 0 0 0 1 |
 * i.e. a matrix representing a linear transformation over 3 space.
 */
void linear_mtxf_transpose_mul_vec3f(Mat4 m, Vec3f dst, Vec3f v) {
    s32 i;
    for (i = 0; i < 3; i++) {
        dst[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    }
}

void obj_apply_scale_to_transform(struct Object *obj) {
    f32 scaleX = obj->header.gfx.scale[0];
    f32 scaleY = obj->header.gfx.scale[1];
    f32 scaleZ = obj->header.gfx.scale[2];

    obj->transform[0][0] *= scaleX;
    obj->transform[0][1] *= scaleX;
    obj->transform[0][2] *= scaleX;

    obj->transform[1][0] *= scaleY;
    obj->transform[1][1] *= scaleY;
    obj->transform[1][2] *= scaleY;

    obj->transform[2][0] *= scaleZ;
    obj->transform[2][1] *= scaleZ;
    obj->transform[2][2] *= scaleZ;
}

void obj_scale(struct Object *obj, f32 scale) {
    obj->header.gfx.scale[0] = scale;
    obj->header.gfx.scale[1] = scale;
    obj->header.gfx.scale[2] = scale;
}

void cur_obj_scale(f32 scale) {
    o->header.gfx.scale[0] = scale;
    o->header.gfx.scale[1] = scale;
    o->header.gfx.scale[2] = scale;
}

void cur_obj_init_animation(s32 animIndex) {
    struct Animation **anims = o->oAnimations;
    geo_obj_init_animation(&o->header.gfx, &anims[animIndex]);
}

void cur_obj_init_animation_with_sound(s32 animIndex) {
    struct Animation **anims = o->oAnimations;
    geo_obj_init_animation(&o->header.gfx, &anims[animIndex]);
    o->oSoundStateID = animIndex;
}

void cur_obj_enable_rendering(void) {
    o->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE;
}

void cur_obj_disable_rendering(void) {
    o->header.gfx.node.flags &= ~GRAPH_RENDER_ACTIVE;
}

void cur_obj_unhide(void) {
    o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
}

void cur_obj_hide(void) {
    o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
}

void cur_obj_set_pos_relative(struct Object *other, f32 dleft, f32 dy, f32 dforward) {
    f32 facingZ = coss(other->oMoveAngleYaw);
    f32 facingX = sins(other->oMoveAngleYaw);

    f32 dz = dforward * facingZ - dleft * facingX;
    f32 dx = dforward * facingX + dleft * facingZ;

    o->oMoveAngleYaw = other->oMoveAngleYaw;

    o->oPosX = other->oPosX + dx;
    o->oPosY = other->oPosY + dy;
    o->oPosZ = other->oPosZ + dz;
}

void obj_set_face_angle_to_move_angle(struct Object *obj) {
    obj->oFaceAnglePitch = obj->oMoveAnglePitch;
    obj->oFaceAngleYaw = obj->oMoveAngleYaw;
    obj->oFaceAngleRoll = obj->oMoveAngleRoll;
}

u32 get_object_list_from_behavior(const BehaviorScript *behavior) {
    u32 objectList;

    // If the first behavior command is "begin", then get the object list header
    // from there
    if ((behavior[0] >> 24) == 0) {
        objectList = (behavior[0] >> 16) & 0xFFFF;
    } else {
        objectList = OBJ_LIST_DEFAULT;
    }

    return objectList;
}

struct Object *cur_obj_nearest_object_with_behavior(const BehaviorScript *behavior) {
    struct Object *obj;
    f32 dist;

    obj = cur_obj_find_nearest_object_with_behavior(behavior, &dist);

    return obj;
}

f32 cur_obj_dist_to_nearest_object_with_behavior(const BehaviorScript *behavior) {
    struct Object *obj;
    f32 dist;

    obj = cur_obj_find_nearest_object_with_behavior(behavior, &dist);
    if (obj == NULL) {
        dist = 15000.0f;
    }

    return dist;
}

struct Object *cur_obj_find_nearest_object_with_behavior(const BehaviorScript *behavior, f32 *dist) {
    uintptr_t *behaviorAddr = (uintptr_t *) segmented_to_virtual(behavior);
    struct Object *closestObj = NULL;
    struct Object *obj;
    struct ObjectNode *listHead;
    f32 minDist = 0x20000;

    listHead = &gObjectLists[get_object_list_from_behavior(behaviorAddr)];
    obj = (struct Object *) listHead->next;

    while (obj != (struct Object *) listHead) {
        if (obj->behavior == behaviorAddr) {
            if (obj->activeFlags != ACTIVE_FLAG_DEACTIVATED && obj != o) {
                f32 objDist = dist_between_objects(o, obj);
                if (objDist < minDist) {
                    closestObj = obj;
                    minDist = objDist;
                }
            }
        }
        obj = (struct Object *) obj->header.next;
    }

    *dist = minDist;
    return closestObj;
}

void cur_obj_set_vel_from_mario_vel(f32 f12, f32 f14) {
    f32 sp4 = gMarioStates[0].forwardVel;
    f32 sp0 = f12 * f14;

    if (sp4 < sp0) {
        o->oForwardVel = sp0;
    } else {
        o->oForwardVel = sp4 * f14;
    }
}

s32 cur_obj_check_if_near_animation_end(void) {
    u32 animFlags = (s32) o->header.gfx.animInfo.curAnim->flags;
    s32 animFrame = o->header.gfx.animInfo.animFrame;
    s32 nearLoopEnd = o->header.gfx.animInfo.curAnim->loopEnd - 2;
    s32 isNearEnd = FALSE;

    if (animFlags & ANIM_FLAG_NOLOOP && nearLoopEnd + 1 == animFrame) {
        isNearEnd = TRUE;
    }

    if (animFrame == nearLoopEnd) {
        isNearEnd = TRUE;
    }

    return isNearEnd;
}

s32 cur_obj_check_if_at_animation_end(void) {
    s32 animFrame = o->header.gfx.animInfo.animFrame;
    s32 lastFrame = o->header.gfx.animInfo.curAnim->loopEnd - 1;

    if (animFrame == lastFrame) {
        return TRUE;
    } else {
        return FALSE;
    }
}

s32 cur_obj_check_anim_frame(s32 frame) {
    s32 animFrame = o->header.gfx.animInfo.animFrame;

    if (animFrame == frame) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void cur_obj_move_after_thrown_or_dropped(f32 forwardVel, f32 velY) {
    o->oMoveFlags = 0;
    o->oFloorHeight = find_floor_height(o->oPosX, o->oPosY + 160.0f, o->oPosZ);

    if (o->oFloorHeight > o->oPosY) {
        o->oPosY = o->oFloorHeight;
    } else if (o->oFloorHeight < -10000.0f) {
        //! OoB failsafe
        obj_copy_pos(o, gMarioObject);
        o->oFloorHeight = find_floor_height(o->oPosX, o->oPosY, o->oPosZ);
    }

    o->oForwardVel = forwardVel;
    o->oVelY = velY;

    if (o->oForwardVel != 0) {
        cur_obj_move_y(/*gravity*/ -4.0f, /*bounciness*/ -0.1f, /*buoyancy*/ 2.0f);
    }
}

/**
 * Mark an object to be unloaded at the end of the frame.
 */
void obj_mark_for_deletion(struct Object *obj) {
    //! This clears all activeFlags. Since some of these flags disable behavior,
    //  setting it to 0 could potentially enable unexpected behavior. After an
    //  object is marked for deletion, it still updates on that frame (I think),
    //  so this is worth looking into.
    obj->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void cur_obj_become_intangible(void) {
    // When the timer is negative, the object is intangible and the timer
    // doesn't count down
    o->oIntangibleTimer = -1;
}

void cur_obj_become_tangible(void) {
    o->oIntangibleTimer = 0;
}

void cur_obj_update_floor_height(void) {
    struct Surface *floor;
    o->oFloorHeight = find_floor(o->oPosX, o->oPosY, o->oPosZ, &floor);
}

struct Surface *cur_obj_update_floor_height_and_get_floor(void) {
    struct Surface *floor;
    o->oFloorHeight = find_floor(o->oPosX, o->oPosY, o->oPosZ, &floor);
    return floor;
}

void apply_drag_to_value(f32 *value, f32 dragStrength) {
    f32 decel;

    if (*value != 0) {
        //! Can overshoot if |*value| > 1/(dragStrength * 0.0001)
        decel = (*value) * (*value) * (dragStrength * 0.0001L);

        if (*value > 0) {
            *value -= decel;
            if (*value < 0.001L) {
                *value = 0;
            }
        } else {
            *value += decel;
            if (*value > -0.001L) {
                *value = 0;
            }
        }
    }
}

void cur_obj_apply_drag_xz(f32 dragStrength) {
    apply_drag_to_value(&o->oVelX, dragStrength);
    apply_drag_to_value(&o->oVelZ, dragStrength);
}

s32 cur_obj_move_xz(f32 steepSlopeNormalY, s32 careAboutEdgesAndSteepSlopes) {
    struct Surface *intendedFloor;

    f32 intendedX = o->oPosX + o->oVelX;
    f32 intendedZ = o->oPosZ + o->oVelZ;

    f32 intendedFloorHeight = find_floor(intendedX, o->oPosY, intendedZ, &intendedFloor);
    f32 deltaFloorHeight = intendedFloorHeight - o->oFloorHeight;

    UNUSED f32 unused;
    UNUSED f32 ny;

    o->oMoveFlags &= ~OBJ_MOVE_HIT_EDGE;

    if (o->oRoom != -1 && intendedFloor != NULL) {
        if (intendedFloor->room != 0 && o->oRoom != intendedFloor->room && intendedFloor->room != 18) {
            // Don't leave native room
            return FALSE;
        }
    }

    if (intendedFloorHeight < -10000.0f) {
        // Don't move into OoB
        o->oMoveFlags |= OBJ_MOVE_HIT_EDGE;
        return FALSE;
    } else if (deltaFloorHeight < 5.0f) {
        if (!careAboutEdgesAndSteepSlopes) {
            // If we don't care about edges or steep slopes, okay to move
            o->oPosX = intendedX;
            o->oPosZ = intendedZ;
            return TRUE;
        } else if (deltaFloorHeight < -50.0f && (o->oMoveFlags & OBJ_MOVE_ON_GROUND)) {
            // Don't walk off an edge
            o->oMoveFlags |= OBJ_MOVE_HIT_EDGE;
            return FALSE;
        } else if (intendedFloor->normal.y > steepSlopeNormalY) {
            // Allow movement onto a slope, provided it's not too steep
            o->oPosX = intendedX;
            o->oPosZ = intendedZ;
            return TRUE;
        } else {
            // We are likely trying to move onto a steep downward slope
            o->oMoveFlags |= OBJ_MOVE_HIT_EDGE;
            return FALSE;
        }
    } else if ((ny = intendedFloor->normal.y) > steepSlopeNormalY || o->oPosY > intendedFloorHeight) {
        // Allow movement upward, provided either:
        // - The target floor is flat enough (e.g. walking up stairs)
        // - We are above the target floor (most likely in the air)
        o->oPosX = intendedX;
        o->oPosZ = intendedZ;
        //! Returning FALSE but moving anyway (not exploitable; return value is
        //  never used)
    }

    // We are likely trying to move onto a steep upward slope
    return FALSE;
}

void cur_obj_move_update_underwater_flags(void) {
    f32 decelY = (f32)(sqrtf(o->oVelY * o->oVelY) * (o->oDragStrength * 7.0f)) / 100.0L;

    if (o->oVelY > 0) {
        o->oVelY -= decelY;
    } else {
        o->oVelY += decelY;
    }

    if (o->oPosY < o->oFloorHeight) {
        o->oPosY = o->oFloorHeight;
        o->oMoveFlags |= OBJ_MOVE_UNDERWATER_ON_GROUND;
    } else {
        o->oMoveFlags |= OBJ_MOVE_UNDERWATER_OFF_GROUND;
    }
}

void cur_obj_move_update_ground_air_flags(UNUSED f32 gravity, f32 bounciness) {
    o->oMoveFlags &= ~OBJ_MOVE_BOUNCE;

    if (o->oPosY < o->oFloorHeight) {
        // On the first frame that we touch the ground, set OBJ_MOVE_LANDED.
        // On subsequent frames, set OBJ_MOVE_ON_GROUND
        if (!(o->oMoveFlags & OBJ_MOVE_ON_GROUND)) {
            if (clear_move_flag(&o->oMoveFlags, OBJ_MOVE_LANDED)) {
                o->oMoveFlags |= OBJ_MOVE_ON_GROUND;
            } else {
                o->oMoveFlags |= OBJ_MOVE_LANDED;
            }
        }

        o->oPosY = o->oFloorHeight;

        if (o->oVelY < 0.0f) {
            o->oVelY *= bounciness;
        }

        if (o->oVelY > 5.0f) {
            //! This overestimates since velY could be > 5 here
            //! without bounce (e.g. jump into misa).
            o->oMoveFlags |= OBJ_MOVE_BOUNCE;
        }
    } else {
        o->oMoveFlags &= ~OBJ_MOVE_LANDED;
        if (clear_move_flag(&o->oMoveFlags, OBJ_MOVE_ON_GROUND)) {
            o->oMoveFlags |= OBJ_MOVE_LEFT_GROUND;
        }
    }

    o->oMoveFlags &= ~OBJ_MOVE_MASK_IN_WATER;
}

f32 cur_obj_move_y_and_get_water_level(f32 gravity, f32 buoyancy) {
    f32 waterLevel;

    o->oVelY += gravity + buoyancy;
    if (o->oVelY < -78.0f) {
        o->oVelY = -78.0f;
    }

    o->oPosY += o->oVelY;
    if (o->activeFlags & ACTIVE_FLAG_UNK10) {
        waterLevel = -11000.0f;
    } else {
        waterLevel = find_water_level(o->oPosX, o->oPosZ);
    }

    return waterLevel;
}

void cur_obj_move_y(f32 gravity, f32 bounciness, f32 buoyancy) {
    f32 waterLevel;

    o->oMoveFlags &= ~OBJ_MOVE_LEFT_GROUND;

    if (o->oMoveFlags & OBJ_MOVE_AT_WATER_SURFACE) {
        if (o->oVelY > 5.0f) {
            o->oMoveFlags &= ~OBJ_MOVE_MASK_IN_WATER;
            o->oMoveFlags |= OBJ_MOVE_LEAVING_WATER;
        }
    }

    if (!(o->oMoveFlags & OBJ_MOVE_MASK_IN_WATER)) {
        waterLevel = cur_obj_move_y_and_get_water_level(gravity, 0.0f);
        if (o->oPosY > waterLevel) {
            //! We only handle floor collision if the object does not enter
            //  water. This allows e.g. coins to clip through floors if they
            //  enter water on the same frame.
            cur_obj_move_update_ground_air_flags(gravity, bounciness);
        } else {
            o->oMoveFlags |= OBJ_MOVE_ENTERED_WATER;
            o->oMoveFlags &= ~OBJ_MOVE_MASK_ON_GROUND;
        }
    } else {
        o->oMoveFlags &= ~OBJ_MOVE_ENTERED_WATER;

        waterLevel = cur_obj_move_y_and_get_water_level(gravity, buoyancy);
        if (o->oPosY < waterLevel) {
            cur_obj_move_update_underwater_flags();
        } else {
            if (o->oPosY < o->oFloorHeight) {
                o->oPosY = o->oFloorHeight;
                o->oMoveFlags &= ~OBJ_MOVE_MASK_IN_WATER;
            } else {
                o->oPosY = waterLevel;
                o->oVelY = 0.0f;
                o->oMoveFlags &= ~(OBJ_MOVE_UNDERWATER_OFF_GROUND | OBJ_MOVE_UNDERWATER_ON_GROUND);
                o->oMoveFlags |= OBJ_MOVE_AT_WATER_SURFACE;
            }
        }
    }

    if (o->oMoveFlags & (OBJ_MOVE_MASK_ON_GROUND | OBJ_MOVE_AT_WATER_SURFACE
        | OBJ_MOVE_UNDERWATER_OFF_GROUND)) {
        o->oMoveFlags &= ~OBJ_MOVE_IN_AIR;
    } else {
        o->oMoveFlags |= OBJ_MOVE_IN_AIR;
    }
}

s32 clear_move_flag(u32 *bitSet, s32 flag) {
    if (*bitSet & flag) {
        *bitSet &= flag ^ 0xFFFFFFFF;
        return TRUE;
    } else {
        return FALSE;
    }
}

s16 abs_angle_diff(s16 x0, s16 x1) {
    s16 diff = x1 - x0;

    if (diff == -0x8000) {
        diff = -0x7FFF;
    }

    if (diff < 0) {
        diff = -diff;
    }

    return diff;
}

void cur_obj_move_xz_using_fvel_and_yaw(void) {
    o->oVelX = o->oForwardVel * sins(o->oMoveAngleYaw);
    o->oVelZ = o->oForwardVel * coss(o->oMoveAngleYaw);

    o->oPosX += o->oVelX;
    o->oPosZ += o->oVelZ;
}

void cur_obj_move_y_with_terminal_vel(void) {
    if (o->oVelY < -70.0f) {
        o->oVelY = -70.0f;
    }

    o->oPosY += o->oVelY;
}

void cur_obj_compute_vel_xz(void) {
    o->oVelX = o->oForwardVel * sins(o->oMoveAngleYaw);
    o->oVelZ = o->oForwardVel * coss(o->oMoveAngleYaw);
}

s32 obj_check_if_collided_with_object(struct Object *obj1, struct Object *obj2) {
    s32 i;
    for (i = 0; i < obj1->numCollidedObjs; i++) {
        if (obj1->collidedObjs[i] == obj2) {
            return TRUE;
        }
    }

    return FALSE;
}

void cur_obj_set_behavior(const BehaviorScript *behavior) {
    o->behavior = (const BehaviorScript *) segmented_to_virtual(behavior);
}

s32 cur_obj_has_behavior(const BehaviorScript *behavior) {
    if (o->behavior == segmented_to_virtual(behavior)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

s32 obj_has_behavior(struct Object *obj, const BehaviorScript *behavior) {
    if (obj->behavior == segmented_to_virtual(behavior)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void cur_obj_set_pos_to_home(void) {
    o->oPosX = o->oHomeX;
    o->oPosY = o->oHomeY;
    o->oPosZ = o->oHomeZ;
}

s32 cur_obj_detect_steep_floor(s16 steepAngleDegrees) {
    struct Surface *intendedFloor;
    f32 intendedX, intendedFloorHeight, intendedZ;
    f32 deltaFloorHeight;
    f32 steepNormalY = coss((s16)(steepAngleDegrees * (0x10000 / 360)));

    if (o->oForwardVel != 0.0f) {
        intendedX = o->oPosX + o->oVelX;
        intendedZ = o->oPosZ + o->oVelZ;
        intendedFloorHeight = find_floor(intendedX, o->oPosY, intendedZ, &intendedFloor);
        deltaFloorHeight = intendedFloorHeight - o->oFloorHeight;

        if (intendedFloorHeight < -10000.0f) {
            o->oWallAngle = o->oMoveAngleYaw + 0x8000;
            return 2;
        } else if (intendedFloor->normal.y < steepNormalY && deltaFloorHeight > 0
                   && intendedFloorHeight > o->oPosY) {
            o->oWallAngle = atan2s(intendedFloor->normal.z, intendedFloor->normal.x);
            return 1;
        } else {
            return 0;
        }
    }

    return 0;
}

s32 cur_obj_resolve_wall_collisions(void) {
    s32 numCollisions;
    struct Surface *wall;
    struct WallCollisionData collisionData;

    f32 offsetY = 10.0f;
    f32 radius = o->oWallHitboxRadius;

    if (radius > 0.1L) {
        collisionData.offsetY = offsetY;
        collisionData.radius = radius;
        collisionData.x = (s16) o->oPosX;
        collisionData.y = (s16) o->oPosY;
        collisionData.z = (s16) o->oPosZ;

        numCollisions = find_wall_collisions(&collisionData);
        if (numCollisions != 0) {
            o->oPosX = collisionData.x;
            o->oPosY = collisionData.y;
            o->oPosZ = collisionData.z;
            wall = collisionData.walls[collisionData.numWalls - 1];

            o->oWallAngle = atan2s(wall->normal.z, wall->normal.x);
            if (abs_angle_diff(o->oWallAngle, o->oMoveAngleYaw) > 0x4000) {
                return TRUE;
            } else {
                return FALSE;
            }
        }
    }

    return FALSE;
}

void cur_obj_update_floor(void) {
    struct Surface *floor = cur_obj_update_floor_height_and_get_floor();
    o->oFloor = floor;

    if (floor != NULL) {
        if (floor->type == SURFACE_BURNING) {
            o->oMoveFlags |= OBJ_MOVE_ABOVE_LAVA;
        }
#ifndef VERSION_JP
        else if (floor->type == SURFACE_DEATH_PLANE) {
            //! This misses SURFACE_VERTICAL_WIND (and maybe SURFACE_WARP)
            o->oMoveFlags |= OBJ_MOVE_ABOVE_DEATH_BARRIER;
        }
#endif

        o->oFloorType = floor->type;
        o->oFloorRoom = floor->room;
    } else {
        o->oFloorType = 0;
        o->oFloorRoom = 0;
    }
}

void cur_obj_update_floor_and_resolve_wall_collisions(s16 steepSlopeDegrees) {
#ifdef VERSION_JP
    o->oMoveFlags &= ~OBJ_MOVE_ABOVE_LAVA;
#else
    o->oMoveFlags &= ~(OBJ_MOVE_ABOVE_LAVA | OBJ_MOVE_ABOVE_DEATH_BARRIER);
#endif

    if (o->activeFlags & (ACTIVE_FLAG_FAR_AWAY | ACTIVE_FLAG_IN_DIFFERENT_ROOM)) {
        cur_obj_update_floor();
        o->oMoveFlags &= ~(OBJ_MOVE_HIT_WALL | OBJ_MOVE_MASK_IN_WATER);

        if (o->oPosY > o->oFloorHeight) {
            o->oMoveFlags |= OBJ_MOVE_IN_AIR;
        }
    } else {
        o->oMoveFlags &= ~OBJ_MOVE_HIT_WALL;
        if (cur_obj_resolve_wall_collisions()) {
            o->oMoveFlags |= OBJ_MOVE_HIT_WALL;
        }

        cur_obj_update_floor();

        if (o->oPosY > o->oFloorHeight) {
            o->oMoveFlags |= OBJ_MOVE_IN_AIR;
        }

        if (cur_obj_detect_steep_floor(steepSlopeDegrees)) {
            o->oMoveFlags |= OBJ_MOVE_HIT_WALL;
        }
    }
}

void cur_obj_move_standard(s16 steepSlopeAngleDegrees) {
    f32 gravity = o->oGravity;
    f32 bounciness = o->oBounciness;
    f32 buoyancy = o->oBuoyancy;
    f32 dragStrength = o->oDragStrength;
    f32 steepSlopeNormalY;
    s32 careAboutEdgesAndSteepSlopes = FALSE;
    s32 negativeSpeed = FALSE;

    //! Because some objects allow these active flags to be set but don't
    //  avoid updating when they are, we end up with "partial" updates, where
    //  an object's internal state will be updated, but it doesn't move.
    //  This allows numerous glitches and is typically referred to as
    //  deactivation (though this term has a different meaning in the code).
    //  Objects that do this will be marked with //PARTIAL_UPDATE.
    if (!(o->activeFlags & (ACTIVE_FLAG_FAR_AWAY | ACTIVE_FLAG_IN_DIFFERENT_ROOM))) {
        if (steepSlopeAngleDegrees < 0) {
            // clang-format off
            careAboutEdgesAndSteepSlopes = TRUE; steepSlopeAngleDegrees = -steepSlopeAngleDegrees;
            // clang-format on
        }

        steepSlopeNormalY = coss(steepSlopeAngleDegrees * (0x10000 / 360));

        cur_obj_compute_vel_xz();
        cur_obj_apply_drag_xz(dragStrength);

        cur_obj_move_xz(steepSlopeNormalY, careAboutEdgesAndSteepSlopes);
        cur_obj_move_y(gravity, bounciness, buoyancy);

        if (o->oForwardVel < 0.0f) {
            negativeSpeed = TRUE;
        }
        o->oForwardVel = sqrtf(sqr(o->oVelX) + sqr(o->oVelZ));
        if (negativeSpeed == TRUE) {
            o->oForwardVel = -o->oForwardVel;
        }
    }
}

s32 cur_obj_within_12k_bounds(void) {
    if (o->oPosX < -12000.0f || 12000.0f < o->oPosX) {
        return FALSE;
    }

    if (o->oPosY < -12000.0f || 12000.0f < o->oPosY) {
        return FALSE;
    }

    if (o->oPosZ < -12000.0f || 12000.0f < o->oPosZ) {
        return FALSE;
    }

    return TRUE;
}

void cur_obj_move_using_vel_and_gravity(void) {
    if (cur_obj_within_12k_bounds()) {
        o->oPosX += o->oVelX;
        o->oPosZ += o->oVelZ;
        o->oVelY += o->oGravity; //! No terminal velocity
        o->oPosY += o->oVelY;
    }
}

s16 cur_obj_angle_to_home(void) {
    s16 angle;
    f32 dx = o->oHomeX - o->oPosX;
    f32 dz = o->oHomeZ - o->oPosZ;

    angle = atan2s(dz, dx);
    return angle;
}

void obj_build_transform_from_pos_and_angle(struct Object *obj, s16 posIndex, s16 angleIndex) {
    f32 translate[3];
    s16 rotation[3];

    translate[0] = obj->rawData.asF32[posIndex + 0];
    translate[1] = obj->rawData.asF32[posIndex + 1];
    translate[2] = obj->rawData.asF32[posIndex + 2];

    rotation[0] = obj->rawData.asS32[angleIndex + 0];
    rotation[1] = obj->rawData.asS32[angleIndex + 1];
    rotation[2] = obj->rawData.asS32[angleIndex + 2];

    mtxf_rotate_zxy_and_translate(obj->transform, translate, rotation);
}

void obj_set_throw_matrix_from_transform(struct Object *obj) {
    if (obj->oFlags & OBJ_FLAG_0020) {
        obj_build_transform_from_pos_and_angle(obj, O_POS_INDEX, O_FACE_ANGLE_INDEX);
        obj_apply_scale_to_transform(obj);
    }

    obj->header.gfx.throwMatrix = &obj->transform;

    //! Sets scale of gCurrentObject instead of obj. Not exploitable since this
    //  function is only called with obj = gCurrentObject
    cur_obj_scale(1.0f);
}

void obj_build_transform_relative_to_parent(struct Object *obj) {
    struct Object *parent = obj->parentObj;

    obj_build_transform_from_pos_and_angle(obj, O_PARENT_RELATIVE_POS_INDEX, O_FACE_ANGLE_INDEX);
    obj_apply_scale_to_transform(obj);
    mtxf_mul(obj->transform, obj->transform, parent->transform);

    obj->oPosX = obj->transform[3][0];
    obj->oPosY = obj->transform[3][1];
    obj->oPosZ = obj->transform[3][2];

    obj->header.gfx.throwMatrix = &obj->transform;

    //! Sets scale of gCurrentObject instead of obj. Not exploitable since this
    //  function is only called with obj = gCurrentObject
    cur_obj_scale(1.0f);
}

void obj_build_vel_from_transform(struct Object *a0) {
    f32 spC = a0->oUnkC0;
    f32 sp8 = a0->oUnkBC;
    f32 sp4 = a0->oForwardVel;

    a0->oVelX = a0->transform[0][0] * spC + a0->transform[1][0] * sp8 + a0->transform[2][0] * sp4;
    a0->oVelY = a0->transform[0][1] * spC + a0->transform[1][1] * sp8 + a0->transform[2][1] * sp4;
    a0->oVelZ = a0->transform[0][2] * spC + a0->transform[1][2] * sp8 + a0->transform[2][2] * sp4;
}

s16 cur_obj_reflect_move_angle_off_wall(void) {
    s16 angle = o->oWallAngle - ((s16) o->oMoveAngleYaw - (s16) o->oWallAngle) + 0x8000;
    return angle;
}

void obj_set_hitbox(struct Object *obj, struct ObjectHitbox *hitbox) {
    if (!(obj->oFlags & OBJ_FLAG_30)) {
        obj->oFlags |= OBJ_FLAG_30;

        obj->oInteractType = hitbox->interactType;
        obj->oDamageOrCoinValue = hitbox->damageOrCoinValue;
        obj->oHealth = hitbox->health;
        obj->oNumLootCoins = hitbox->numLootCoins;

        cur_obj_become_tangible();
    }

    obj->hitboxRadius = obj->header.gfx.scale[0] * hitbox->radius;
    obj->hitboxHeight = obj->header.gfx.scale[1] * hitbox->height;
    obj->hurtboxRadius = obj->header.gfx.scale[0] * hitbox->hurtboxRadius;
    obj->hurtboxHeight = obj->header.gfx.scale[1] * hitbox->hurtboxHeight;
    obj->hitboxDownOffset = obj->header.gfx.scale[1] * hitbox->downOffset;
}

f32 absf(f32 x) {
    if (x >= 0) {
        return x;
    } else {
        return -x;
    }
}

s32 absi(s32 x) {
    if (x >= 0) {
        return x;
    } else {
        return -x;
    }
}

s32 cur_obj_wait_then_blink(s32 timeUntilBlinking, s32 numBlinks) {
    s32 done = FALSE;
    s32 timeBlinking;

    if (o->oTimer >= timeUntilBlinking) {
        if ((timeBlinking = o->oTimer - timeUntilBlinking) % 2 != 0) {
            o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
            if (timeBlinking / 2 > numBlinks) {
                done = TRUE;
            }
        } else {
            o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        }
    }

    return done;
}

s32 cur_obj_is_mario_ground_pounding_platform(void) {
    if (gMarioObject->platform == o) {
        if (gMarioStates[0].action == ACT_GROUND_POUND_LAND) {
            return TRUE;
        }
    }

    return FALSE;
}

void cur_obj_push_mario_away(f32 radius) {
    f32 marioRelX = gMarioObject->oPosX - o->oPosX;
    f32 marioRelZ = gMarioObject->oPosZ - o->oPosZ;
    f32 marioDist = sqrtf(sqr(marioRelX) + sqr(marioRelZ));

    if (marioDist < radius) {
        //! If this function pushes Mario out of bounds, it will trigger Mario's
        //  oob failsafe
        gMarioStates[0].pos[0] += (radius - marioDist) / radius * marioRelX;
        gMarioStates[0].pos[2] += (radius - marioDist) / radius * marioRelZ;
    }
}

void cur_obj_push_mario_away_from_cylinder(f32 radius, f32 extentY) {
    f32 marioRelY = gMarioObject->oPosY - o->oPosY;

    if (marioRelY < 0.0f) {
        marioRelY = -marioRelY;
    }

    if (marioRelY < extentY) {
        cur_obj_push_mario_away(radius);
    }
}

void cur_obj_enable_rendering_if_mario_in_room(void) {
    s32 marioInRoom;

    if (o->oRoom != -1 && gMarioCurrentRoom != 0) {
        if (gMarioCurrentRoom == o->oRoom) {
            marioInRoom = TRUE;
        } else if (gDoorAdjacentRooms[gMarioCurrentRoom][0] == o->oRoom) {
            marioInRoom = TRUE;
        } else if (gDoorAdjacentRooms[gMarioCurrentRoom][1] == o->oRoom) {
            marioInRoom = TRUE;
        } else {
            marioInRoom = FALSE;
        }

        if (marioInRoom) {
            cur_obj_enable_rendering();
            o->activeFlags &= ~ACTIVE_FLAG_IN_DIFFERENT_ROOM;
            gNumRoomedObjectsInMarioRoom++;
        } else {
            cur_obj_disable_rendering();
            o->activeFlags |= ACTIVE_FLAG_IN_DIFFERENT_ROOM;
            gNumRoomedObjectsNotInMarioRoom++;
        }
    }
}

s32 obj_is_hidden(struct Object *obj) {
    if (obj->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) {
        return TRUE;
    } else {
        return FALSE;
    }
}

s32 cur_obj_was_attacked_or_ground_pounded(void) {
    s32 attacked = FALSE;

    if ((o->oInteractStatus & INT_STATUS_INTERACTED)
        && (o->oInteractStatus & INT_STATUS_WAS_ATTACKED)) {
        attacked = TRUE;
    }

    if (cur_obj_is_mario_ground_pounding_platform()) {
        attacked = TRUE;
    }

    o->oInteractStatus = 0;
    return attacked;
}
