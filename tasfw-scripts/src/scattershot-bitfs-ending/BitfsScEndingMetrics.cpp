#include <Scattershot_BitfsScEnding.hpp>
#include <ScriptMath.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

int16_t BitfsScEndingMetrics::RoughTargetAngle(int oscQuadrant, int targetQuadrant)
{
    // The run from the oscillation's corner to the adjacent target corner, along the edge the
    // two share: toward +x (16384), -x (-16384), +z (0) or -z (-32768).
    switch (oscQuadrant)
    {
    case 1:
        if (targetQuadrant == 2) return -32768;
        if (targetQuadrant == 4) return -16384;
        break;
    case 2:
        if (targetQuadrant == 3) return -16384;
        if (targetQuadrant == 1) return 0;
        break;
    case 3:
        if (targetQuadrant == 4) return 0;
        if (targetQuadrant == 2) return 16384;
        break;
    case 4:
        if (targetQuadrant == 1) return 16384;
        if (targetQuadrant == 3) return -32768;
        break;
    default:
        break;
    }
    throw std::runtime_error("Invalid quadrants: the target corner must be adjacent to the oscillation corner");
}

const char* BitfsScEndingMetrics::PhaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::SWING: return "SWING";
    case Phase::DIVE: return "DIVE";
    case Phase::BRAKE: return "BRAKE";
    case Phase::EDGE: return "EDGE";
    case Phase::AIR: return "AIR";
    }
    return "?";
}

const char* BitfsScEndingMetrics::ApproachName(Approach approach)
{
    switch (approach)
    {
    case Approach::NONE: return "NONE";
    case Approach::DIVE_RECOVER: return "DIVE_RECOVER";
    case Approach::BRAKE: return "BRAKE";
    }
    return "?";
}

BitfsScEndingMetrics::BitfsScEndingMetrics(const BitfsScEndingArgs& args) : _args(args)
{
    _roughTargetAngle = RoughTargetAngle(args.oscQuadrant, args.targetQuadrant);
    _edgeX = gSineTable[uint16_t(_roughTargetAngle) >> 4];
    _edgeZ = gCosineTable[uint16_t(_roughTargetAngle) >> 4];

    // The edge the run follows lies on the side whose sign the two corners share.
    if (CornerSignZ(args.oscQuadrant) == CornerSignZ(args.targetQuadrant))
    {
        _outX = 0.0f;
        _outZ = float(CornerSignZ(args.oscQuadrant));
    }
    else
    {
        _outX = float(CornerSignX(args.oscQuadrant));
        _outZ = 0.0f;
    }

    _squishOutX = float(args.squishEdgeX);
    _squishOutZ = float(args.squishEdgeZ);
    _squishAlongX = -_squishOutZ;
    _squishAlongZ = _squishOutX;
}

bool BitfsScEndingMetrics::validation() { return int64_t(GetCurrentFrame()) >= _args.initialFrame; }

bool BitfsScEndingMetrics::execution()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    SetStateVariables(marioState, pyramid);

    int64_t currentFrame = GetCurrentFrame();
    CustomScriptStatus lastFrameState;
    if (currentFrame > _args.initialFrame)
        lastFrameState = GetMetrics(currentFrame - 1);

    // The normal the error is measured against: the config's target, or without one the
    // hand-over's own, the frame after initialFrame (the frame the error is conserved from),
    // carried from there; up to that frame this frame's, so the counts read zero.
    if (_args.targetGiven)
    {
        CustomStatus.targetNx = _args.targetNx;
        CustomStatus.targetNz = _args.targetNz;
    }
    else if (currentFrame > _args.initialFrame + 1 && lastFrameState.initialized)
    {
        CustomStatus.targetNx = lastFrameState.targetNx;
        CustomStatus.targetNz = lastFrameState.targetNz;
    }
    else
    {
        CustomStatus.targetNx = pyramid->oTiltingPyramidNormalX;
        CustomStatus.targetNz = pyramid->oTiltingPyramidNormalZ;
    }
    CalculateARE(pyramid, CustomStatus.targetNx, CustomStatus.targetNz);

    // The first frame: the run, nothing before it.
    if (!lastFrameState.initialized)
        return true;

    CalculatePhase(lastFrameState, marioState, pyramid);

    if ((CustomStatus.phase == Phase::BRAKE || CustomStatus.phase == Phase::EDGE) && CustomStatus.onPlatform)
        CustomStatus.edgeDistance = EdgeDistance(marioState, pyramid);

    Probe(marioState, pyramid);

    return true;
}

bool BitfsScEndingMetrics::assertion() { return CustomStatus.initialized; }

Object* BitfsScEndingMetrics::Pyramid()
{
    Object* objectPool = (Object*)(ReadState("gObjectPool"));
    return &objectPool[_args.platform];
}

void BitfsScEndingMetrics::SetStateVariables(MarioState* marioState, Object* pyramid)
{
    CustomStatus.initialized = true;
    CustomStatus.frame = GetCurrentFrame();
    CustomStatus.marioX = marioState->pos[0];
    CustomStatus.marioY = marioState->pos[1];
    CustomStatus.marioZ = marioState->pos[2];
    CustomStatus.marioAction = marioState->action;
    CustomStatus.fSpd = marioState->forwardVel;
    CustomStatus.faceYaw = marioState->faceAngle[1];
    CustomStatus.pyraNormX = pyramid->oTiltingPyramidNormalX;
    CustomStatus.pyraNormY = pyramid->oTiltingPyramidNormalY;
    CustomStatus.pyraNormZ = pyramid->oTiltingPyramidNormalZ;
    CustomStatus.xzSum = std::fabs(pyramid->oTiltingPyramidNormalX) + std::fabs(pyramid->oTiltingPyramidNormalZ);
    CustomStatus.onPlatform = marioState->marioObj->platform == pyramid;
}

void BitfsScEndingMetrics::CalculateARE(Object* pyramid, float targetNx, float targetNz)
{
    // The adjusted remainder error on each axis: the distance in ULPs from the target of the
    // normal stepped by 0.01 until within half a step of it, and the steps that took
    // (BitfsDrMetrics::CalculateARE; the glossary in AGENTS.md).
    float errorIncX = std::fabs(std::nextafter(targetNx, INFINITY) - targetNx);
    float errorIncZ = std::fabs(std::nextafter(targetNz, INFINITY) - targetNz);

    float errorX = (targetNx - pyramid->oTiltingPyramidNormalX) / errorIncX;
    float errorZ = (targetNz - pyramid->oTiltingPyramidNormalZ) / errorIncZ;

    float normalX = pyramid->oTiltingPyramidNormalX;
    for (int i = 0; i < 200; i++)
    {
        if (std::fabs(targetNx - normalX) <= 0.005f)
        {
            CustomStatus.adjustedRemainderError[0] = (targetNx - normalX) / errorIncX;
            CustomStatus.incrementFrames[0] = i * ScriptMath::Sign(errorX);
            break;
        }

        normalX += ScriptMath::Sign(errorX) * 0.01f;
    }

    float normalZ = pyramid->oTiltingPyramidNormalZ;
    for (int i = 0; i < 200; i++)
    {
        if (std::fabs(targetNz - normalZ) <= 0.005f)
        {
            CustomStatus.adjustedRemainderError[2] = (targetNz - normalZ) / errorIncZ;
            CustomStatus.incrementFrames[2] = i * ScriptMath::Sign(errorZ);
            break;
        }

        normalZ += ScriptMath::Sign(errorZ) * 0.01f;
    }
}

void BitfsScEndingMetrics::CalculatePhase(const CustomScriptStatus& last, MarioState* marioState, Object* pyramid)
{
    const uint32_t action = marioState->action;

    // What carries from frame to frame.
    CustomStatus.phase = last.phase;
    CustomStatus.approach = last.approach;
    CustomStatus.quickturn = last.quickturn;
    CustomStatus.departed = last.departed;
    CustomStatus.departureFrame = last.departureFrame;
    CustomStatus.leaveAction = last.leaveAction;
    CustomStatus.leaveSpeed = last.leaveSpeed;
    CustomStatus.leaveYaw = last.leaveYaw;
    CustomStatus.leaveCUp = last.leaveCUp;
    CustomStatus.leaveEdgeDistance = last.leaveEdgeDistance;
    CustomStatus.leaveAtSquishEdge = last.leaveAtSquishEdge;
    CustomStatus.strainBackFrames = last.strainBackFrames;
    CustomStatus.nutLandings = last.nutLandings;

    if (last.departed)
    {
        // The air: the pyramid may be his platform again for a frame or two of straining back,
        // or under a landing on the edge; both are counted, neither moves the target rule.
        CustomStatus.phase = Phase::AIR;
        if (CustomStatus.onPlatform && (action & ACT_FLAG_AIR))
            CustomStatus.strainBackFrames++;
        if (CustomStatus.onPlatform && !(action & ACT_FLAG_AIR) && (last.marioAction & ACT_FLAG_AIR))
            CustomStatus.nutLandings++;
        return;
    }

    // The dive recover's flight (the maintainer, 2026-10-12): a rollout that rises over the
    // band of 4 with the pyramid still the floor under him, and lands back on it, is the
    // approach, not a departure; the normal steps toward flat on both axes while he is out
    // of the band, which the catch's rules judge from the normal itself. Past the
    // platform's edge the floor is the lava's, and the flight is a departure like any.
    const bool recoverFlight = (action == ACT_DIVE || action == ACT_DIVE_SLIDE || action == ACT_FORWARD_ROLLOUT)
        && marioState->floor != nullptr && marioState->floor->object == pyramid;
    if (!CustomStatus.onPlatform && !recoverFlight)
    {
        // The departure (the maintainer, 2026-10-05): the pyramid is not Mario's platform at
        // this frame's end, so from here its normal returns toward flat unless he lands back on
        // it. The target is judged on the squish cancel's frame, by the probe.
        CustomStatus.phase = Phase::AIR;
        CustomStatus.departed = true;
        CustomStatus.departureFrame = GetCurrentFrame();
        // An approach that departs on its own frame still names itself: a dive from a run
        // leaves the band at once unless the slope rises under it (the first run recorded
        // 2,689 such dives as no approach at all, 2026-10-06).
        if (CustomStatus.approach == Approach::NONE)
        {
            if (action == ACT_DIVE || action == ACT_DIVE_SLIDE || action == ACT_FORWARD_ROLLOUT)
                CustomStatus.approach = Approach::DIVE_RECOVER;
            else if (action == ACT_TURNING_AROUND || action == ACT_BRAKING || action == ACT_DECELERATING || action == ACT_BRAKING_STOP)
                CustomStatus.approach = Approach::BRAKE;
        }
        CustomStatus.leaveAction = last.marioAction;
        CustomStatus.leaveSpeed = last.fSpd;
        CustomStatus.leaveYaw = last.faceYaw;
        CustomStatus.leaveCUp = (GetInputs(GetCurrentFrame() - 1).buttons & Buttons::C_UP) != 0;
        CustomStatus.leaveEdgeDistance = last.edgeDistance;
        CustomStatus.leaveAtSquishEdge = OverSquishEdge(marioState, pyramid);
        return;
    }

    switch (last.phase)
    {
    case Phase::SWING:
        if (action == ACT_DIVE || action == ACT_DIVE_SLIDE || action == ACT_FORWARD_ROLLOUT)
        {
            CustomStatus.phase = Phase::DIVE;
            CustomStatus.approach = Approach::DIVE_RECOVER;
        }
        else if (action == ACT_TURNING_AROUND || action == ACT_BRAKING || action == ACT_DECELERATING || action == ACT_BRAKING_STOP)
        {
            CustomStatus.phase = Phase::BRAKE;
            CustomStatus.approach = Approach::BRAKE;
        }
        break;

    case Phase::BRAKE:
        if ((action & ACT_FLAG_STATIONARY) || marioState->forwardVel == 0.0f)
            CustomStatus.phase = Phase::EDGE;
        else if ((action == ACT_WALKING || action == ACT_FINISH_TURNING_AROUND) && marioState->forwardVel > last.fSpd)
        {
            // Running again: an oscillation's turnaround, not the ending's brake.
            CustomStatus.phase = Phase::SWING;
            CustomStatus.approach = Approach::NONE;
        }
        break;

    case Phase::DIVE:
        if (action == ACT_FREEFALL_LAND_STOP || action == ACT_FREEFALL_LAND || action == ACT_IDLE)
            CustomStatus.phase = Phase::EDGE;
        break;

    case Phase::EDGE:
        // The quickturn: a minimum-magnitude stick for one frame takes a landing or braking
        // stop into walking at zero speed, and a released stick then takes that straight to
        // idle, where the stop's own animation would have held Mario for several frames.
        CustomStatus.walkedFromStop = action == ACT_WALKING && last.fSpd == 0.0f
            && (last.marioAction == ACT_FREEFALL_LAND_STOP || last.marioAction == ACT_BRAKING_STOP);
        if (action == ACT_IDLE && last.walkedFromStop)
            CustomStatus.quickturn = true;
        break;

    case Phase::AIR:
        break;
    }
}

BitfsScEndingMetrics::Face BitfsScEndingMetrics::Face::Of(float normalX, float normalY, float normalZ, float homeX, float homeY, float homeZ)
{
    Face face;
    const float length = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    face.ux = normalX / length;
    face.uy = normalY / length;
    face.uz = normalZ / length;
    face.s = std::sqrt(face.ux * face.ux + face.uy * face.uy);
    face.nx = normalX;
    face.ny = normalY;
    face.nz = normalZ;
    face.homeX = homeX;
    face.homeY = homeY;
    face.homeZ = homeZ;
    return face;
}

BitfsScEndingMetrics::Face BitfsScEndingMetrics::Face::Of(const Object* pyramid)
{
    return Of(pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, pyramid->oPosX, pyramid->oPosY, pyramid->oPosZ);
}

// The air model (AirStep): the decomp copies' arithmetic, float by float in their order
// (tilting_inverted_pyramid.inc, math_util.inc, object_helpers.inc, surface_load.inc,
// surface_collision.inc, mario_actions_airborne.inc, mario_step.inc,
// platform_displacement.inc). A double appears only where the decomp writes one.

// The inverted pyramid's collision: the top's square and its two triangles.
static constexpr int16_t TopVertices[4][3] = { { 307, 307, -306 }, { -306, 307, 307 }, { 307, 307, 307 }, { -306, 307, -306 } };
static constexpr int TopTriangles[2][3] = { { 0, 1, 2 }, { 0, 3, 1 } };

float BitfsScEndingMetrics::AirStep::ApproachByIncrement(float goal, float src, float inc)
{
    float newVal;
    if (src <= goal)
    {
        if (goal - src < inc)
            newVal = goal;
        else
            newVal = src + inc;
    }
    else if (goal - src > -inc)
        newVal = goal;
    else
        newVal = src - inc;
    return newVal;
}

float BitfsScEndingMetrics::AirStep::ApproachF32(float current, float target, float inc, float dec)
{
    if (current < target)
    {
        current += inc;
        if (current > target)
            current = target;
    }
    else
    {
        current -= dec;
        if (current < target)
            current = target;
    }
    return current;
}

void BitfsScEndingMetrics::AirStep::Normalize(float v[3])
{
    const float invsqrt = 1.0f / std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] *= invsqrt;
    v[1] *= invsqrt;
    v[2] *= invsqrt;
}

void BitfsScEndingMetrics::AirStep::Cross(float dest[3], const float a[3], const float b[3])
{
    dest[0] = a[1] * b[2] - b[1] * a[2];
    dest[1] = a[2] * b[0] - b[2] * a[0];
    dest[2] = a[0] * b[1] - b[0] * a[1];
}

void BitfsScEndingMetrics::AirStep::LinearMul(const float rows[3][3], float dest[3], const float v[3])
{
    for (int i = 0; i < 3; i++)
        dest[i] = rows[0][i] * v[0] + rows[1][i] * v[1] + rows[2][i] * v[2];
}

BitfsScEndingMetrics::AirStep::Pyramid BitfsScEndingMetrics::AirStep::Pyramid::Of(const Object* pyramid)
{
    return Of(pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, pyramid->oPosX, pyramid->oPosY, pyramid->oPosZ);
}

BitfsScEndingMetrics::AirStep::Pyramid BitfsScEndingMetrics::AirStep::Pyramid::Of(float nx, float ny, float nz, float homeX, float homeY, float homeZ)
{
    Pyramid pyramid;
    pyramid.homeX = homeX;
    pyramid.homeY = homeY;
    pyramid.homeZ = homeZ;
    pyramid.nx = nx;
    pyramid.ny = ny;
    pyramid.nz = nz;
    pyramid.Place();
    return pyramid;
}

void BitfsScEndingMetrics::AirStep::Pyramid::Place()
{
    // mtxf_align_terrain_normal with yaw 0, then transform_object_vertices (the scale 1) and
    // read_surface_data for each of the top's triangles.
    const float lateral[3] = { gSineTable[0], 0.0f, gCosineTable[0] };
    float up[3] = { nx, ny, nz };
    Normalize(up);
    float left[3];
    Cross(left, up, lateral);
    Normalize(left);
    float forward[3];
    Cross(forward, left, up);
    Normalize(forward);
    for (int i = 0; i < 3; i++)
    {
        rows[0][i] = left[i];
        rows[1][i] = up[i];
        rows[2][i] = forward[i];
    }
    const float home[3] = { homeX, homeY, homeZ };
    int16_t world[4][3];
    for (int v = 0; v < 4; v++)
    {
        const float vx = float(TopVertices[v][0]);
        const float vy = float(TopVertices[v][1]);
        const float vz = float(TopVertices[v][2]);
        for (int i = 0; i < 3; i++)
            world[v][i] = int16_t(vx * rows[0][i] + vy * rows[1][i] + vz * rows[2][i] + home[i]);
    }
    for (int t = 0; t < 2; t++)
    {
        Triangle& triangle = top[t];
        for (int k = 0; k < 3; k++)
        {
            for (int i = 0; i < 3; i++)
                triangle.v[k][i] = world[TopTriangles[t][k]][i];
        }
        const int32_t x1 = triangle.v[0][0], y1 = triangle.v[0][1], z1 = triangle.v[0][2];
        const int32_t x2 = triangle.v[1][0], y2 = triangle.v[1][1], z2 = triangle.v[1][2];
        const int32_t x3 = triangle.v[2][0], y3 = triangle.v[2][1], z3 = triangle.v[2][2];
        float tx = float((y2 - y1) * (z3 - z2) - (z2 - z1) * (y3 - y2));
        float ty = float((z2 - z1) * (x3 - x2) - (x2 - x1) * (z3 - z2));
        float tz = float((x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2));
        float mag = std::sqrt(tx * tx + ty * ty + tz * tz);
        mag = float(1.0 / mag);
        tx *= mag;
        ty *= mag;
        tz *= mag;
        triangle.nx = tx;
        triangle.ny = ty;
        triangle.nz = tz;
        triangle.originOffset = -(tx * float(x1) + ty * float(y1) + tz * float(z1));
    }
}

void BitfsScEndingMetrics::AirStep::Pyramid::Tilt(float& marioX, float& marioY, float& marioZ, bool onPlatform)
{
    float dx, dy, dz;
    float dist[3] = { 0.0f, 0.0f, 0.0f };
    float before[3] = { 0.0f, 0.0f, 0.0f };
    if (onPlatform)
    {
        dist[0] = marioX - homeX;
        dist[1] = marioY - homeY;
        dist[2] = marioZ - homeZ;
        LinearMul(rows, before, dist);
        dx = marioX - homeX;
        dy = 500.0f;
        dz = marioZ - homeZ;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        d = float(1.0 / d);
        dx *= d;
        dy *= d;
        dz *= d;
    }
    else
    {
        dx = 0.0f;
        dy = 1.0f;
        dz = 0.0f;
    }
    nx = ApproachByIncrement(dx, nx, 0.01f);
    ny = ApproachByIncrement(dy, ny, 0.01f);
    nz = ApproachByIncrement(dz, nz, 0.01f);
    Place();
    if (onPlatform)
    {
        float after[3];
        LinearMul(rows, after, dist);
        marioX += after[0] - before[0];
        marioY += after[1] - before[1];
        marioZ += after[2] - before[2];
    }
}

float BitfsScEndingMetrics::AirStep::Pyramid::FindFloor(float x, float y, float z, bool& isTop) const
{
    // find_floor and find_floor_from_list on the top's triangles, the lava the static floor.
    const int32_t xi = int16_t(x);
    const int32_t yi = int16_t(y);
    const int32_t zi = int16_t(z);
    float height = LavaHeight;
    isTop = false;
    for (const Triangle& triangle : top)
    {
        const int32_t x1 = triangle.v[0][0], z1 = triangle.v[0][2];
        const int32_t x2 = triangle.v[1][0], z2 = triangle.v[1][2];
        if ((z1 - zi) * (x2 - x1) - (x1 - xi) * (z2 - z1) < 0)
            continue;
        const int32_t x3 = triangle.v[2][0], z3 = triangle.v[2][2];
        if ((z2 - zi) * (x3 - x2) - (x2 - xi) * (z3 - z2) < 0)
            continue;
        if ((z3 - zi) * (x1 - x3) - (x3 - xi) * (z1 - z3) < 0)
            continue;
        if (triangle.ny == 0.0f)
            continue;
        const float topHeight = -(float(xi) * triangle.nx + triangle.nz * float(zi) + triangle.originOffset) / triangle.ny;
        if (float(yi) - (topHeight + -78.0f) < 0.0f)
            continue;
        if (topHeight > height)
        {
            height = topHeight;
            isTop = true;
        }
        break;
    }
    return height;
}

BitfsScEndingMetrics::AirStep::Mario BitfsScEndingMetrics::AirStep::Mario::Of(const MarioState* marioState, const Object* pyramid)
{
    Mario mario;
    mario.x = marioState->pos[0];
    mario.y = marioState->pos[1];
    mario.z = marioState->pos[2];
    mario.vx = marioState->vel[0];
    mario.vy = marioState->vel[1];
    mario.vz = marioState->vel[2];
    mario.forwardVel = marioState->forwardVel;
    mario.yaw = marioState->faceAngle[1];
    mario.action = marioState->action;
    mario.onPlatform = marioState->marioObj->platform == pyramid;
    mario.floorHeight = marioState->floorHeight;
    mario.floorIsTop = marioState->floor != nullptr && marioState->floor->object == pyramid;
    return mario;
}

void BitfsScEndingMetrics::AirStep::UpdateAirWithoutTurn(Mario& mario, int16_t intendedYaw, float intendedMag)
{
    float sidewaysSpeed = 0.0f;
    const float dragThreshold = 32.0f;
    mario.forwardVel = ApproachF32(mario.forwardVel, 0.0f, 0.35f, 0.35f);
    if (intendedMag > 0.0f)
    {
        const int16_t intendedDYaw = int16_t(intendedYaw - mario.yaw);
        const float magnitude = intendedMag / 32.0f;
        mario.forwardVel += magnitude * gCosineTable[uint16_t(intendedDYaw) >> 4] * 1.5f;
        sidewaysSpeed = magnitude * gSineTable[uint16_t(intendedDYaw) >> 4] * 10.0f;
    }
    if (mario.forwardVel > dragThreshold)
        mario.forwardVel -= 1.0f;
    if (mario.forwardVel < -16.0f)
        mario.forwardVel += 2.0f;
    float slideVelX = mario.forwardVel * gSineTable[uint16_t(mario.yaw) >> 4];
    float slideVelZ = mario.forwardVel * gCosineTable[uint16_t(mario.yaw) >> 4];
    slideVelX += sidewaysSpeed * gSineTable[uint16_t(int16_t(mario.yaw + 0x4000)) >> 4];
    slideVelZ += sidewaysSpeed * gCosineTable[uint16_t(int16_t(mario.yaw + 0x4000)) >> 4];
    mario.vx = slideVelX;
    mario.vz = slideVelZ;
}

bool BitfsScEndingMetrics::AirStep::PerformAirStep(Mario& mario, const Pyramid& pyramid)
{
    // The four quarter steps (perform_air_quarter_step: no walls, no ceiling over the top),
    // then gravity.
    bool landed = false;
    for (int i = 0; i < 4; i++)
    {
        const float nextX = mario.x + mario.vx / 4.0f;
        const float nextY = mario.y + mario.vy / 4.0f;
        const float nextZ = mario.z + mario.vz / 4.0f;
        bool isTop;
        const float floorHeight = pyramid.FindFloor(nextX, nextY, nextZ, isTop);
        if (nextY <= floorHeight)
        {
            mario.x = nextX;
            mario.z = nextZ;
            mario.floorHeight = floorHeight;
            mario.floorIsTop = isTop;
            mario.y = floorHeight;
            landed = true;
            break;
        }
        mario.x = nextX;
        mario.y = nextY;
        mario.z = nextZ;
        mario.floorHeight = floorHeight;
        mario.floorIsTop = isTop;
    }
    mario.vy -= 4.0f;
    if (mario.vy < -75.0f)
        mario.vy = -75.0f;
    return landed;
}

bool BitfsScEndingMetrics::AirStep::Frame(Mario& mario, Pyramid& pyramid, int16_t intendedYaw, float intendedMag, bool bPressed)
{
    uint32_t action = mario.action;
    if (action == ACT_WALKING)
    {
        // check_ground_dive_or_punch: the dive needs B, 29 of speed and a stick over 48 raw,
        // which is an intended magnitude over 18; set_mario_action_airborne adds 15 of speed.
        if (!bPressed || mario.forwardVel < 29.0f || intendedMag <= 18.0f)
            return false;
        float forwardVel = mario.forwardVel + 15.0f;
        if (forwardVel > 48.0f)
            forwardVel = 48.0f;
        pyramid.Tilt(mario.x, mario.y, mario.z, mario.onPlatform);
        mario.vy = 20.0f;
        mario.forwardVel = forwardVel;
        action = ACT_DIVE;
    }
    else if (action == ACT_DIVE_SLIDE)
    {
        // act_dive_slide: B is the rollout's press; act_forward_rollout's first frame.
        if (!bPressed)
            return false;
        pyramid.Tilt(mario.x, mario.y, mario.z, mario.onPlatform);
        mario.vy = 30.0f;
        action = ACT_FORWARD_ROLLOUT;
    }
    else if (action == ACT_DIVE || action == ACT_FORWARD_ROLLOUT)
        pyramid.Tilt(mario.x, mario.y, mario.z, mario.onPlatform);
    else
        return false;
    UpdateAirWithoutTurn(mario, intendedYaw, intendedMag);
    mario.landed = PerformAirStep(mario, pyramid);
    mario.action = mario.landed ? (action == ACT_DIVE ? ACT_DIVE_SLIDE : ACT_FREEFALL_LAND_STOP) : action;
    // update_mario_platform, at the end of the frame.
    bool isTop;
    const float floorHeight = pyramid.FindFloor(mario.x, mario.y, mario.z, isTop);
    mario.onPlatform = isTop && std::fabs(mario.y - floorHeight) < 4.0f;
    return true;
}

// The collision data's square is (307, 307, -306) to (-306, 307, 307): the edges on the
// negative sides are a unit nearer the apex.
static float FaceExtent(int sign)
{
    return sign > 0 ? BitfsScEndingMetrics::Face::HalfWidth : -(BitfsScEndingMetrics::Face::HalfWidth - 1.0f);
}

float BitfsScEndingMetrics::Face::Out(int outX, int outZ, float x, float z) const
{
    return float(outX) * (x - homeX) + float(outZ) * (z - homeZ);
}

float BitfsScEndingMetrics::Face::EdgeOut(int outX, int outZ, [[maybe_unused]] float x, float z) const
{
    // A vertex (X, HalfWidth, Z) of the top lies at home + X left + HalfWidth up + Z forward,
    // left (uy, -ux, 0) / s and forward (-ux uz, -uy uz, s^2) / s.
    if (outX != 0)
    {
        // A z-aligned edge, at the point's z (its local Z there): its x moves with Z.
        const float X = FaceExtent(outX);
        const float Z = (z - homeZ - HalfWidth * uz) / s;
        const float edgeX = X * uy / s + HalfWidth * ux - Z * ux * uz / s;
        return float(outX) * edgeX;
    }
    // An x-aligned edge: one z along its length.
    const float Z = FaceExtent(outZ);
    const float edgeZ = Z * s + HalfWidth * uz;
    return float(outZ) * edgeZ;
}

float BitfsScEndingMetrics::Face::CornerAlong(int outX, int outZ, float alongX, float alongZ) const
{
    if (outX != 0)
    {
        // The z-aligned edge's ends: along z.
        const float Z = FaceExtent(alongZ > 0.0f ? 1 : -1);
        return alongZ * (Z * s + HalfWidth * uz);
    }
    // The x-aligned edge's ends: along x, at the edge's own Z.
    const float X = FaceExtent(alongX > 0.0f ? 1 : -1);
    const float Z = FaceExtent(outZ);
    return alongX * (X * uy / s + HalfWidth * ux - Z * ux * uz / s);
}

float BitfsScEndingMetrics::Face::TopHeight(float x, float z) const
{
    // The plane through the top's center, home + HalfWidth up, with the normal up.
    const float centerX = homeX + HalfWidth * ux;
    const float centerY = homeY + HalfWidth * uy;
    const float centerZ = homeZ + HalfWidth * uz;
    return centerY - (ux * (x - centerX) + uz * (z - centerZ)) / uy;
}

float BitfsScEndingMetrics::Face::SideHeight(int outX, int outZ, float x, float z) const
{
    // The side's plane holds the apex and the edge, Y = k X for a z-aligned edge and Y = k Z
    // for an x-aligned one, k the top's height over the edge's extent; the point's local X
    // and Z from its x and z, then its height.
    const float lx = uy / s;
    const float ly = -ux / s;
    const float fx = -ux * uz / s;
    const float fy = -uy * uz / s;
    const float fz = s;
    const float dx = x - homeX;
    const float dz = z - homeZ;
    float X, Y, Z;
    if (outX != 0)
    {
        const float k = HalfWidth / FaceExtent(outX);
        X = (dx - fx * dz / fz) / (lx + k * ux - k * fx * uz / fz);
        Y = k * X;
        Z = (dz - Y * uz) / fz;
    }
    else
    {
        const float k = HalfWidth / FaceExtent(outZ);
        Z = dz / (fz + k * uz);
        Y = k * Z;
        X = (dx - Y * ux - Z * fx) / lx;
    }
    return homeY + X * ly + Y * uy + Z * fy;
}

float BitfsScEndingMetrics::Face::SideNormalY(int outX, int outZ) const
{
    // The plane Y = k X (a z-aligned edge) or Y = k Z (an x-aligned one), k signed by the
    // edge's extent, has the normal (k, -1, 0) or (0, -1, k) over its length, outward and
    // down; in the world, k left - up or -up + k forward.
    const float k = HalfWidth / FaceExtent(outX != 0 ? outX : outZ);
    const float ly = -ux / s;
    const float fy = -uy * uz / s;
    return ((outX != 0 ? k * ly : 0.0f) - uy + (outZ != 0 ? k * fy : 0.0f)) / std::sqrt(1.0f + k * k);
}

void BitfsScEndingMetrics::Face::SideNormalDirection(int outX, int outZ, float& dirX, float& dirZ) const
{
    const float k = HalfWidth / FaceExtent(outX != 0 ? outX : outZ);
    const float lx = uy / s;
    const float fx = -ux * uz / s;
    const float fz = s;
    const float nx = (outX != 0 ? k * lx : 0.0f) - ux + (outZ != 0 ? k * fx : 0.0f);
    const float nz = -uz + (outZ != 0 ? k * fz : 0.0f);
    const float length = std::sqrt(nx * nx + nz * nz);
    dirX = nx / length;
    dirZ = nz / length;
}

float BitfsScEndingMetrics::Face::Rise(float dirX, float dirZ) const
{
    return -(ux * dirX + uz * dirZ) / uy;
}

BitfsScEndingMetrics::Face BitfsScEndingMetrics::NextFace(const Object* pyramid)
{
    // approach_by_increment(goal, src, 0.01f) toward flat, (0, 1, 0), on each component.
    auto step = [](float goal, float src)
    {
        if (src <= goal)
            return goal - src < 0.01f ? goal : src + 0.01f;
        return goal - src > -0.01f ? goal : src - 0.01f;
    };
    return Face::Of(step(0.0f, pyramid->oTiltingPyramidNormalX), step(1.0f, pyramid->oTiltingPyramidNormalY), step(0.0f, pyramid->oTiltingPyramidNormalZ),
        pyramid->oPosX, pyramid->oPosY, pyramid->oPosZ);
}

int BitfsScEndingMetrics::CatchPlacements(const Face& next, int outX, int outZ, float x, float y, float z, float minDistance, std::array<Placement, 3>& placements)
{
    // The squish: INPUT_SQUISHED at the frame's start, from where Mario is against the frame's
    // face: inside its edge, the top more than FloorReach above him (his floor the lava, not
    // the top), the side within SquishSpace of the lava.
    const float marioOut = next.Out(outX, outZ, x, z);
    if (next.EdgeOut(outX, outZ, x, z) - marioOut < -DepthSlack)
        return -1;
    if (next.TopHeight(x, z) <= y + FloorReach - CatchSlack)
        return -1;
    if (next.SideHeight(outX, outZ, x, z) - LavaHeight > SquishSpace + CatchSlack)
        return -1;

    // The cancel: the side steep, and the squished walk along its normal, SquishWalk in four
    // quarter steps, each the floor's normal y times a quarter: the lava's 1 for the first,
    // the top's uy once a step has snapped him onto it. The pushed spot is inside the edge
    // by the middle of the walk's reach across it, so the first step is over the top and the
    // last is past the edge, with the game's rounding either way.
    if (next.SideNormalY(outX, outZ) <= SteepCeiling - SteepSlack)
        return 0;
    float walkX, walkZ;
    next.SideNormalDirection(outX, outZ, walkX, walkZ);
    const float across = float(outX) * walkX + float(outZ) * walkZ;
    const float quarter = SquishWalk / 4.0f;
    const float spotDepth = quarter * across * (1.0f + 1.5f * next.uy);

    // Downhill along the edge: the top falls that way, so the pushed spot's top can be
    // within FloorReach above him while the top over him is further.
    const float alongX = -float(outZ);
    const float alongZ = float(outX);
    const float downhill = next.Rise(alongX, alongZ) <= 0.0f ? 1.0f : -1.0f;
    const float pushX = alongX * downhill;
    const float pushZ = alongZ * downhill;
    const float cornerAlong = next.CornerAlong(outX, outZ, pushX, pushZ);
    const float marioAlong = pushX * (x - next.homeX) + pushZ * (z - next.homeZ);

    static constexpr float fractions[3] = { 0.3f, 0.4f, 0.5f };
    int count = 0;
    for (float fraction : fractions)
    {
        // The push: the rest of the push-out distance, along the edge and in over it by what
        // puts the spot spotDepth inside the edge there (the edge's own coordinate at the
        // spot: the z-aligned edges are skewed); the along part shortened for the inward one,
        // and the edge read again there.
        const float push = (1.0f - fraction) * minDistance;
        float along = push;
        float in = 0.0f;
        for (int pass = 0; pass < 2 && std::fabs(in) < push; pass++)
        {
            in = spotDepth - (next.EdgeOut(outX, outZ, x + pushX * along, z + pushZ * along) - marioOut);
            if (std::fabs(in) < push)
                along = std::sqrt(push * push - in * in);
        }
        if (std::fabs(in) >= push)
            continue;
        const float spotX = x + pushX * along - float(outX) * in;
        const float spotZ = z + pushZ * along - float(outZ) * in;

        // On the top, short of its end; the top there over LeaveGroundHeight, for the step off
        // it to leave the ground, and within FloorReach above him, for it to be his floor.
        if (marioAlong + along > cornerAlong - CatchSlack)
            continue;
        const float height = next.TopHeight(spotX, spotZ);
        if (height - LavaHeight <= LeaveGroundHeight - CatchSlack)
            continue;
        if (height > y + FloorReach + CatchSlack)
            continue;

        Placement& placement = placements[count++];
        placement.dirX = (pushX * along - float(outX) * in) / push;
        placement.dirZ = (pushZ * along - float(outZ) * in) / push;
        placement.fraction = fraction;
        placement.spotHeight = height - LavaHeight;
    }
    return count;
}

bool BitfsScEndingMetrics::OverSquishEdge(const MarioState* marioState, const Object* pyramid)
{
    // The outer edge nearest Mario must be the squish edge, and he must be on it or past it
    // (within a unit: the game's vertices are shorts).
    const Face face = Face::Of(pyramid);
    const float x = marioState->pos[0];
    const float z = marioState->pos[2];
    static constexpr int sides[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    float nearest = INFINITY;
    int nearestX = 0;
    int nearestZ = 0;
    for (const auto& side : sides)
    {
        const float distance = std::fabs(face.EdgeOut(side[0], side[1], x, z) - face.Out(side[0], side[1], x, z));
        if (distance < nearest)
        {
            nearest = distance;
            nearestX = side[0];
            nearestZ = side[1];
        }
    }
    if (nearestX != _args.squishEdgeX || nearestZ != _args.squishEdgeZ)
        return false;
    return face.Out(_args.squishEdgeX, _args.squishEdgeZ, x, z) >= face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z) - 1.0f;
}

float BitfsScEndingMetrics::EdgeDistance(const MarioState* marioState, const Object* pyramid)
{
    const Face face = Face::Of(pyramid);
    const float x = marioState->pos[0];
    const float z = marioState->pos[2];
    static constexpr int sides[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    float nearest = INFINITY;
    for (const auto& side : sides)
        nearest = std::min(nearest, face.EdgeOut(side[0], side[1], x, z) - face.Out(side[0], side[1], x, z));
    return nearest;
}

void BitfsScEndingMetrics::Probe(MarioState* marioState, Object* pyramid)
{
    if (!CustomStatus.departed || _args.bully < 0 || CustomStatus.onPlatform)
        return;
    if (int64_t(GetCurrentFrame()) - CustomStatus.departureFrame > int64_t(_args.probeWindow))
        return;

    // The frame the squish comes: the next frame's face covering Mario where he is, and the
    // pushes that can cancel it (CatchPlacements; ROADMAP 4.11, 2026-10-10). On any other
    // frame a probe is a knockback whatever the placement, and a squish no push cancels is a
    // pancake, so neither is played.
    Object* objectPool = (Object*)(ReadState("gObjectPool"));
    Object* bully = &objectPool[_args.bully];
    const float minDistance = bully->hitboxRadius + marioState->marioObj->hitboxRadius + PushPadding;
    std::array<Placement, 3> placements;
    const int count = CatchPlacements(NextFace(pyramid), _args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[1], marioState->pos[2], minDistance, placements);
    CustomStatus.catchPlacements = count;
    if (count <= 0)
        return;

    // The normal must be the target's on the frame the probe reads (the maintainer,
    // 2026-10-06), so with a target the probe runs only where the target is within the
    // probe's frames on both axes: off the platform each axis steps 0.01 toward flat a
    // frame, and the probe checks the frame itself.
    if (_args.targetGiven
        && (std::abs(CustomStatus.incrementFrames[0]) > _args.probeFrames || std::abs(CustomStatus.incrementFrames[2]) > _args.probeFrames))
        return;

    // interact_bully begins with push_mario_out_of_object(m, o, 5.0f): Mario is set this far
    // from the bully's center, along the line from the bully to him. So a bully placed a
    // fraction of that distance behind Mario, against the direction wanted, displaces him the
    // rest of the way in it, and the bully's own motion along the push is what the knockback
    // hands him as speed (along his facing it hands him nothing). Which placement worked is
    // in the solution (probeIndex, probeDx, probeDz).

    // The clean state of this frame, before any write: every placement's sandbox loads it
    // back, and nothing after this point saves.
    Save();
    CustomStatus.probed = true;

    for (int index = 0; index < count; index++)
    {
        const Placement& placement = placements[index];
        const float offsetX = -placement.dirX * placement.fraction * minDistance;
        const float offsetZ = -placement.dirZ * placement.fraction * minDistance;
        ProbeResult result = ProbeOnce(marioState, bully, offsetX, offsetZ, atan2s(placement.dirZ, placement.dirX));

        const bool targetMet = !_args.targetGiven || (result.normalX == _args.targetNx && result.normalZ == _args.targetNz);
        const bool success = targetMet && result.action == ACT_IDLE
            && std::fabs(result.speed) >= _args.squishMinSpeed
            && result.height - LavaHeight >= _args.squishMinClearance;

        // The record: the placement that succeeded, else the one whose knockback took (the
        // game sets Mario's invincibility timer with it; the knockback's speed is capped
        // under a run's, so speed alone would hide it), else the fastest.
        const bool better = index == 0 || success
            || result.invincTimer > CustomStatus.probeInvincTimer
            || (result.invincTimer == CustomStatus.probeInvincTimer && std::fabs(result.speed) > std::fabs(CustomStatus.probeSpeed));
        if (better)
        {
            CustomStatus.probeIndex = index;
            CustomStatus.probeDx = offsetX;
            CustomStatus.probeDz = offsetZ;
            CustomStatus.probeAction = result.action;
            CustomStatus.probeSpeed = result.speed;
            CustomStatus.probeHeight = result.height;
            CustomStatus.probeNormalX = result.normalX;
            CustomStatus.probeNormalZ = result.normalZ;
            CustomStatus.probeTargetMet = targetMet;
            CustomStatus.probeCollisions = result.collisions;
            CustomStatus.probeBullyHit = result.bullyHit;
            CustomStatus.probeInvincTimer = result.invincTimer;
        }
        if (success)
        {
            CustomStatus.squishCancel = true;
            return;
        }
    }
}

BitfsScEndingMetrics::ProbeResult BitfsScEndingMetrics::ProbeOnce(MarioState* marioState, Object* bully, float offsetX, float offsetZ, int16_t pushYaw)
{
    ProbeResult result;
    Object* pyramid = Pyramid();
    ExecuteAdhoc([&]()
        {
            //! UNSAFE: the squish probe writes the bully (AGENTS.md, hard rule 1's exception).
            // From here to the end of this sandbox: written frame advances and reads only. A
            // load, a save or a child here could carry these bytes into a savestate the search
            // reuses; the sandbox's revert loads the save Probe made before them.
            bully->oPosX = marioState->pos[0] + offsetX;
            bully->oPosY = marioState->pos[1];
            bully->oPosZ = marioState->pos[2] + offsetZ;
            bully->oBullyPrevX = bully->oPosX;
            bully->oBullyPrevY = bully->oPosY;
            bully->oBullyPrevZ = bully->oPosZ;
            bully->oVelX = 0.0f;
            bully->oVelY = 0.0f;
            bully->oVelZ = 0.0f;
            bully->oForwardVel = _args.bullySpeed;
            bully->oMoveAngleYaw = pushYaw;
            bully->oFaceAngleYaw = pushYaw;
            bully->oAction = BullyActKnockback;
            bully->oBullyKBTimerAndMinionKOCounter = 0;
            bully->oInteractStatus = 0;
            bully->oIntangibleTimer = 0;

            for (int i = 0; i < _args.probeFrames; i++)
                AdvanceFrameWrite(Inputs(0, 0, 0));

            result.action = marioState->action;
            result.speed = marioState->forwardVel;
            result.height = marioState->pos[1];
            result.collisions = marioState->marioObj->numCollidedObjs;
            for (int i = 0; i < result.collisions && i < 4; i++)
                result.bullyHit |= marioState->marioObj->collidedObjs[i] == bully;
            result.invincTimer = marioState->invincTimer;
            result.normalX = pyramid->oTiltingPyramidNormalX;
            result.normalZ = pyramid->oTiltingPyramidNormalZ;
            return false;
        });
    return result;
}
