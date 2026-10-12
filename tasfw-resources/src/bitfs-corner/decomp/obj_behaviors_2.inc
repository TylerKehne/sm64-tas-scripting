s32 clamp_s16(s16 *value, s16 minimum, s16 maximum) {
    if (*value <= minimum) {
        *value = minimum;
    } else if (*value >= maximum) {
        *value = maximum;
    } else {
        return FALSE;
    }

    return TRUE;
}

s32 clamp_f32(f32 *value, f32 minimum, f32 maximum) {
    if (*value <= minimum) {
        *value = minimum;
    } else if (*value >= maximum) {
        *value = maximum;
    } else {
        return FALSE;
    }

    return TRUE;
}

s32 approach_f32_ptr(f32 *px, f32 target, f32 delta) {
    if (*px > target) {
        delta = -delta;
    }

    *px += delta;

    if ((*px - target) * delta >= 0) {
        *px = target;
        return TRUE;
    }
    return FALSE;
}

s32 obj_forward_vel_approach(f32 target, f32 delta) {
    return approach_f32_ptr(&o->oForwardVel, target, delta);
}

s32 obj_face_yaw_approach(s16 targetYaw, s16 deltaYaw) {
    o->oFaceAngleYaw = approach_s16_symmetric(o->oFaceAngleYaw, targetYaw, deltaYaw);

    if ((s16) o->oFaceAngleYaw == targetYaw) {
        return TRUE;
    }

    return FALSE;
}

s32 obj_face_roll_approach(s16 targetRoll, s16 deltaRoll) {
    o->oFaceAngleRoll = approach_s16_symmetric(o->oFaceAngleRoll, targetRoll, deltaRoll);

    if ((s16) o->oFaceAngleRoll == targetRoll) {
        return TRUE;
    }

    return FALSE;
}

s32 oscillate_toward(s32 *value, f32 *vel, s32 target, f32 velCloseToZero, f32 accel,
                            f32 slowdown) {
    s32 startValue = *value;
    *value += (s32) *vel;

    if (*value == target
        || ((*value - target) * (startValue - target) < 0 && *vel > -velCloseToZero
            && *vel < velCloseToZero)) {
        *value = target;
        *vel = 0.0f;
        return TRUE;
    } else {
        if (*value >= target) {
            accel = -accel;
        }
        if (*vel * accel < 0.0f) {
            accel *= slowdown;
        }

        *vel += accel;
    }

    return FALSE;
}
