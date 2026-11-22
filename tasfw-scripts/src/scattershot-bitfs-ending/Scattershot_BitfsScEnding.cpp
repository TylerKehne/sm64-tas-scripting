#include <Scattershot_BitfsScEnding.hpp>
#include <MarioDeath.hpp>
#include <Sm64ActionNames.hpp>
#include <ScriptMath.hpp>
#include <algorithm>
#include <cstdio>
#include <limits>

void Scattershot_BitfsScEnding::SelectMovementOptions()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    auto state = GetMetrics(GetCurrentFrame());
    const uint32_t action = marioState->action;
    const float speed = marioState->forwardVel;
    const bool running = action == ACT_WALKING || action == ACT_FINISH_TURNING_AROUND;
    const bool stopped = action == ACT_FREEFALL_LAND_STOP || action == ACT_BRAKING_STOP;
    // The brake begins within BrakeReach of the squish edge, where the maintainer's example
    // movies begin their approach, and a stick held back is offered only there: on the run
    // it is a turnaround in the middle of the platform, half of the fourth run's rows.
    const float edgeDistance = SquishEdgeDistance(marioState, pyramid);
    const bool nearEdge = edgeDistance <= BrakeReach;
    const bool freeYaw = nearEdge || state.phase != BitfsScEndingMetrics::Phase::SWING;
    // The approach begins within the dive recover's reach of the edge (DrReach plus the
    // landing's), the turn toward the aim counted: the run turns 2048 a frame, so the turn
    // from its heading to the aim's takes its frames at the run's speed (the uscz movie
    // turns its last five frames, 150 units, from 8 to 17 thousand). Until then the run
    // keeps its speed: the minimum downhill angle toward the target corner, which at the
    // hand-over's tilt is the movie's own heading, up the low edge toward the far corner
    // (the movie runs 7 frames at it from 28 to 35; a random stick ran uphill and bled to
    // 25). The dive recover does its own turn (Dive); it is offered where the speed the
    // turn leaves, TurnSpeedCost a frame of it, is still the dive's 29, and the edge not
    // yet nearer than the dive's window.
    float aimX, aimZ;
    AimPoint(CatchFace(BitfsScEndingMetrics::Face::Of(pyramid)), PushDistance(marioState), aimX, aimZ);
    const int16_t aimYaw = atan2s(aimZ - marioState->pos[2], aimX - marioState->pos[0]);
    const float turnFrames = std::fabs(float(int16_t(aimYaw - marioState->faceAngle[1]))) / 2048.0f;
    const bool approaching = edgeDistance - turnFrames * std::max(speed, 1.0f) <= DrReach + DrLandingReach;
    const bool diveSpeed = speed - TurnSpeedCost * turnFrames >= 29.0f;
    const bool diveReaches = approaching && edgeDistance >= DrReach - 2.0f * DrLandingReach;

    // The random frame's stick: mostly full, any yaw where a turnaround is wanted; its buttons
    // from ApplyMovement's probabilities.
    AddRandomMovementOption(
        {
            {BasicMoves::MAX_MAGNITUDE, 4},
            {BasicMoves::RANDOM_MAGNITUDE, 2}
        });

    AddRandomMovementOption(
        {
            {BasicMoves::MATCH_FACING_YAW, 2},
            {BasicMoves::ANTI_FACING_YAW, freeYaw ? 1 : 0},
            {BasicMoves::SAME_YAW, 2},
            {BasicMoves::RANDOM_YAW, freeYaw ? 8 : 0}
        });

    AddRandomMovementOption(
        {
            {BasicMoves::RANDOM_BUTTONS, 1}
        });

    switch (state.phase)
    {
    case BitfsScEndingMetrics::Phase::SWING:
        // The run toward the target corner and its edge, and the two ways the approach begins:
        // the dive recover from 29 speed where it keeps the band, or, within reach of the edge,
        // the stick held back or released. The turn toward the edge is uphill and costs about
        // a unit of speed a frame, so it is offered once the run is fast (the examples turn at
        // 34 to 37 and dive at 31; the sixth run turned from the hand-over's 13 to 21 and never
        // reached 29 again): until then the run gathers speed along the low edge.
        AddRandomMovementOption(
            {
                // The run is the minimum downhill angle and little else throughout: the dive
                // needs 29 after its turn toward the aim, which costs about half a unit of speed
                // a frame for five frames, so the run must gather 32 or more first (the movie
                // turns at 35; from the day's roots, 23 at the crossing, the runs reached 34 at
                // most seven frames in, and with the turn drawn from the crossing on, where the
                // approach already begins for most roots, none completed it: every frame in
                // the dive's window still headed up the low edge, 2026-10-12). The turn to the
                // edge and the stick within a quarter turn serve the brake's approach.
                {CustomMoves::NO_SCRIPT, 1},
                {CustomMoves::RUN_RANDOM, running && approaching ? 2 : 0},
                {CustomMoves::RUN_DOWNHILL_MIN, 8},
                {CustomMoves::RUN_DOWNHILL, 2},
                {CustomMoves::TURN_UPHILL, running && speed > 16.0f && approaching ? 1 : 0},
                {CustomMoves::RUN_TO_EDGE, running && speed >= 27.0f && approaching ? 4 : 0},
                {CustomMoves::DIVE, running && diveSpeed && diveReaches ? 6 * _args.weightDive : 0},
                {CustomMoves::HOLD_BACK, running && nearEdge ? 2 * _args.weightBrake : 0},
                {CustomMoves::RELEASE, running && nearEdge ? _args.weightBrake : 0}
            });
        break;

    case BitfsScEndingMetrics::Phase::DIVE:
        // The dive recover's moves after the dive, nothing else: from the slide the rollout's
        // press and first frame, from the rollout its flight to the landing stop. They are
        // the frames the recover played as one move until 2026-10-13, steered by the same
        // compares; they are moves of their own for the CSV's rows (Dive).
        AddMovementOption(action == ACT_FORWARD_ROLLOUT ? CustomMoves::LANDING : CustomMoves::ROLLOUT);
        break;

    case BitfsScEndingMetrics::Phase::BRAKE:
        AddRandomMovementOption(
            {
                {CustomMoves::NO_SCRIPT, 1},
                {CustomMoves::TURN_UPHILL, running ? 1 : 0},
                {CustomMoves::HOLD_BACK, 3 * _args.weightBrake},
                {CustomMoves::RELEASE, 3 * _args.weightBrake},
                {CustomMoves::WALK_OFF, speed < 16.0f ? _args.weightWalkOff : 0},
                {CustomMoves::PARALLEL, _args.weightParallel},
                {CustomMoves::CUP_DECEL, speed < 16.0f ? _args.weightCUp : 0}
            });
        break;

    case BitfsScEndingMetrics::Phase::EDGE:
        AddRandomMovementOption(
            {
                {CustomMoves::NO_SCRIPT, 1},
                {CustomMoves::WALK_OFF, 2 * _args.weightWalkOff},
                {CustomMoves::LEAVE_ALONG, stopped ? 3 * _args.weightWalkOff : 0},
                {CustomMoves::PARALLEL, _args.weightParallel},
                {CustomMoves::QUICKTURN, stopped ? 3 * _args.weightQuickturn : 0},
                {CustomMoves::PUNCH_CANCEL, action == ACT_FREEFALL_LAND_STOP || action == ACT_IDLE ? 2 * _args.weightPunchCancel : 0},
                {CustomMoves::CUP_DECEL, 2 * _args.weightCUp},
                {CustomMoves::WAIT, 2 * _args.weightWait},
                {CustomMoves::WAIT_FOR_TILT, stopped ? 3 * _args.weightWait : 0},
                // NutStop fails wherever the tilt has no NUT spot, and a failed move ends the
                // pellet, so it is drawn about as often as the random frame.
                {CustomMoves::NUT_STOP, (stopped || action == ACT_IDLE) && edgeDistance <= NutStopReach ? _args.weightNutStop : 0}
            });
        break;

    case BitfsScEndingMetrics::Phase::AIR:
    {
        // The platform frames NutLand seeks are to be had while Mario is shallower than the
        // catch (the top within FloorReach above him: deeper, the sweep squishes him instead)
        // and the edge, as it will stand a frame on, is about to cover him or within a
        // sideways stick's reach of it (SidewaysStrain, across the facing; along it a stick
        // moves him 1.5, so the reach is taken as the smaller).
        const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
        const auto next = BitfsScEndingMetrics::NextFace(pyramid);
        const float outside = face.Out(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2])
            - next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]);
        const float topAbove = face.TopHeight(marioState->pos[0], marioState->pos[2]) - marioState->pos[1];
        const bool platformInReach = !state.onPlatform && topAbove < BitfsScEndingMetrics::FloorReach && outside <= 2.0f;
        // The fall to the catch is the whole fall, so it is drawn as often as the one-frame
        // strain: a pellet that leaves the platform draws its next move from here, and the
        // fall's first frames are the ones that cannot be had again.
        const bool falling = (action & ACT_FLAG_AIR) != 0 && !state.onPlatform;
        AddRandomMovementOption(
            {
                {CustomMoves::NO_SCRIPT, 1},
                {CustomMoves::WAIT, 1},
                {CustomMoves::STRAIN_BACK, 6 * _args.weightStrainBack},
                {CustomMoves::AIR_DIVE, action == ACT_FREEFALL ? 2 * _args.weightAirDive : 0},
                {CustomMoves::NUT_LAND, platformInReach ? 2 * _args.weightNutLand : 0},
                {CustomMoves::FALL_TO_CATCH, falling ? 6 * _args.weightFallToCatch : 0}
            });
        break;
    }
    }
}

bool Scattershot_BitfsScEnding::ApplyMovement()
{
    auto state = GetMetrics(GetCurrentFrame());

    if (!CheckMovementOptions(CustomMoves::NO_SCRIPT))
    {
        if (CheckMovementOptions(CustomMoves::RUN_RANDOM))
            return RunRandom();
        if (CheckMovementOptions(CustomMoves::RUN_DOWNHILL_MIN))
            return RunDownhill_1f();
        if (CheckMovementOptions(CustomMoves::RUN_DOWNHILL))
            return RunDownhill_1f(false);
        if (CheckMovementOptions(CustomMoves::TURN_UPHILL))
            return TurnUphill_1f();
        if (CheckMovementOptions(CustomMoves::RUN_TO_EDGE))
            return RunToEdge();
        if (CheckMovementOptions(CustomMoves::DIVE))
            return Dive();
        if (CheckMovementOptions(CustomMoves::ROLLOUT))
            return Rollout();
        if (CheckMovementOptions(CustomMoves::LANDING))
            return Landing();
        if (CheckMovementOptions(CustomMoves::HOLD_BACK))
            return HoldBack();
        if (CheckMovementOptions(CustomMoves::RELEASE) || CheckMovementOptions(CustomMoves::WAIT))
        {
            AdvanceFrameWrite(Inputs(0, 0, 0));
            return true;
        }
        if (CheckMovementOptions(CustomMoves::WAIT_FOR_TILT))
            return WaitForTilt();
        if (CheckMovementOptions(CustomMoves::WALK_OFF))
            return WalkOff();
        if (CheckMovementOptions(CustomMoves::LEAVE_ALONG))
            return LeaveAlong();
        if (CheckMovementOptions(CustomMoves::PUNCH_CANCEL))
            return PunchCancel();
        if (CheckMovementOptions(CustomMoves::PARALLEL))
            return Parallel();
        if (CheckMovementOptions(CustomMoves::QUICKTURN))
            return Quickturn();
        if (CheckMovementOptions(CustomMoves::CUP_DECEL))
            return CUpDecel();
        if (CheckMovementOptions(CustomMoves::STRAIN_BACK))
            return StrainBack();
        if (CheckMovementOptions(CustomMoves::AIR_DIVE))
            return AirDive();
        if (CheckMovementOptions(CustomMoves::NUT_STOP))
            return NutStop();
        if (CheckMovementOptions(CustomMoves::NUT_LAND))
            return NutLand();
        if (CheckMovementOptions(CustomMoves::FALL_TO_CATCH))
            return FallToCatch();
    }

    // The random frame. No A (the setup is for an A-button-challenge run), no B (the dive
    // recover's moves press it, and no random frame is drawn in the recover), Z and C-up now
    // and then, since anything but A is allowed (the maintainer, 2026-10-05). On the run it
    // must keep the pyramid stepping; afterwards the validation judges the frame.
    Inputs inputs = RandomInputs(
        {
            {Buttons::A, 0},
            {Buttons::B, 0},
            {Buttons::Z, 0.05},
            {Buttons::C_UP, 0.1}
        });
    if (state.phase == BitfsScEndingMetrics::Phase::SWING)
        return StepInputs(inputs);

    AdvanceFrameWrite(inputs);
    return true;
}

BinaryStateBin<16> Scattershot_BitfsScEnding::GetStateBin()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    auto metrics = GetMetrics(GetCurrentFrame());

    int actionValue;
    switch (marioState->action)
    {
        case ACT_WALKING: actionValue = 0; break;
        case ACT_FINISH_TURNING_AROUND: actionValue = 1; break;
        case ACT_TURNING_AROUND: actionValue = 2; break;
        case ACT_BRAKING: actionValue = 3; break;
        case ACT_BRAKING_STOP: actionValue = 4; break;
        case ACT_DECELERATING: actionValue = 5; break;
        case ACT_IDLE: actionValue = 6; break;
        case ACT_FREEFALL: actionValue = 7; break;
        case ACT_FREEFALL_LAND: actionValue = 8; break;
        case ACT_FREEFALL_LAND_STOP: actionValue = 9; break;
        case ACT_DIVE: actionValue = 10; break;
        case ACT_DIVE_SLIDE: actionValue = 11; break;
        case ACT_FORWARD_ROLLOUT: actionValue = 12; break;
        default: actionValue = 13;
    }

    const float xMin = pyramid->oPosX - SearchHalfExtent;
    const float xMax = pyramid->oPosX + SearchHalfExtent;
    const float zMin = pyramid->oPosZ - SearchHalfExtent;
    const float zMax = pyramid->oPosZ + SearchHalfExtent;
    const float xPosValue = std::clamp(marioState->pos[0], xMin, xMax);
    const float zPosValue = std::clamp(marioState->pos[2], zMin, zMax);

    uint8_t bitCursor = 0;
    BinaryStateBin<16> state;

    if (!metrics.initialized)
    {
        state.AddValueBits(bitCursor, 1, 0);
        state.AddValueBits(bitCursor, 4, actionValue);
        state.AddRegionBitsByRegionSize(bitCursor, 8, xPosValue, xMin, xMax, 100.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 8, zPosValue, zMin, zMax, 100.0f);
        return state;
    }

    // The scenario so far keeps its own populations: the phase, the approach and the
    // quickturn; in the dive recover, near the edge and in the air the position to the unit
    // (each of the recover's rows is a dive of its own, which a coarser bin would merge). Speed is the
    // fitness on the run, not a bin: with it the first run made 416,000 blocks in 1,730
    // shots, near-duplicates a block apart, and would have hit the block cap before the
    // leave was explored (2026-10-06).
    const bool fine = metrics.phase == BitfsScEndingMetrics::Phase::DIVE || metrics.phase == BitfsScEndingMetrics::Phase::EDGE
        || metrics.phase == BitfsScEndingMetrics::Phase::AIR;
    state.AddValueBits(bitCursor, 1, 1);
    state.AddValueBits(bitCursor, 4, actionValue);
    state.AddValueBits(bitCursor, 3, int(metrics.phase));
    state.AddValueBits(bitCursor, 2, int(metrics.approach));
    state.AddValueBits(bitCursor, 1, metrics.quickturn ? 1 : 0);
    if (metrics.departed)
    {
        // After the departure the bin is fine (the maintainer, 2026-10-17): the position to
        // the quarter unit across and the unit up, the speed to the unit, the facing to the
        // HAU, and the pyramid's normal as the tilt |nX| + |nZ| and the axes' difference
        // |nX| - |nZ| to the hundredth (the components step by 0.01, so rounding reads the
        // step they are on) and its y to the ten-thousandth. A squish cancel is kept once
        // per block, so this keeps squish cancels a fraction of a unit apart as distinct
        // solutions, and the falling states on the way to them apart as well; what it
        // costs in blocks and time is measured in ROADMAP 4.11.
        const float yMin = BitfsScEndingMetrics::LavaHeight - 128.0f;
        const float yMax = pyramid->oPosY + 512.0f;
        const float normalX = std::fabs(pyramid->oTiltingPyramidNormalX);
        const float normalZ = std::fabs(pyramid->oTiltingPyramidNormalZ);
        const long tiltHundredths = std::clamp(std::lround((normalX + normalZ) / 0.01f), 0L, 255L);
        const long axisDiffHundredths = std::clamp(std::lround((normalX - normalZ) / 0.01f), -200L, 200L) + 200L;
        const long normalYTenThousandths = std::clamp(std::lround(pyramid->oTiltingPyramidNormalY / 0.0001f), 0L, 16383L);
        state.AddValueBits(bitCursor, 1, 1);
        state.AddRegionBitsByRegionSize(bitCursor, 12, xPosValue, xMin, xMax, 0.25f);
        state.AddRegionBitsByRegionSize(bitCursor, 12, zPosValue, zMin, zMax, 0.25f);
        state.AddRegionBitsByRegionSize(bitCursor, 10, std::clamp(marioState->pos[1], yMin, yMax), yMin, yMax, 1.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 9, std::clamp(marioState->forwardVel, -256.0f, 256.0f), -256.0f, 256.0f, 1.0f);
        state.AddValueBits(bitCursor, 12, uint16_t(marioState->faceAngle[1]) >> 4);
        state.AddValueBits(bitCursor, 8, uint64_t(tiltHundredths));
        state.AddValueBits(bitCursor, 9, uint64_t(axisDiffHundredths));
        state.AddValueBits(bitCursor, 14, uint64_t(normalYTenThousandths));
    }
    else
    {
        state.AddValueBits(bitCursor, 1, 0);
        state.AddRegionBitsByRegionSize(bitCursor, 10, xPosValue, xMin, xMax, fine ? 1.0f : 5.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 10, zPosValue, zMin, zMax, fine ? 1.0f : 5.0f);
        state.AddRegionBitsByNRegions(bitCursor, 5, int(marioState->faceAngle[1]), -32768, 32767, 32);
    }

    // The normal as what matters of it: the steps to the target on each axis, and their side.
    state.AddValueBits(bitCursor, 6, std::clamp(std::abs(metrics.incrementFrames[0]), 0, 63));
    state.AddValueBits(bitCursor, 6, std::clamp(std::abs(metrics.incrementFrames[2]), 0, 63));
    state.AddValueBits(bitCursor, 1, metrics.incrementFrames[0] < 0 ? 1 : 0);
    state.AddValueBits(bitCursor, 1, metrics.incrementFrames[2] < 0 ? 1 : 0);

    if (metrics.departed)
    {
        state.AddValueBits(bitCursor, 4, int(std::clamp<int64_t>(int64_t(GetCurrentFrame()) - metrics.departureFrame, 0, 15)));
        state.AddValueBits(bitCursor, 2, std::clamp(metrics.strainBackFrames, 0, 3));
        state.AddValueBits(bitCursor, 2, std::clamp(metrics.nutLandings, 0, 3));
        state.AddValueBits(bitCursor, 1, metrics.leaveCUp ? 1 : 0);
    }

    return state;
}

bool Scattershot_BitfsScEnding::ValidateState()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    // A dying Mario is a failed lineage (MarioDeath.hpp): the death is never played out.
    if (MarioDeath::IsDying(*marioState))
        return false;

    // Near the platform, and not above where it could carry him.
    if (std::fabs(marioState->pos[0] - pyramid->oPosX) > SearchHalfExtent)
        return false;
    if (std::fabs(marioState->pos[2] - pyramid->oPosZ) > SearchHalfExtent)
        return false;
    if (marioState->pos[1] > pyramid->oPosY + 465.0f)
        return false;

    // And on the side of the platform the edge is on (edgeSide): the squish cancel is at
    // that edge, and a lineage that has run across the platform has nothing to come back to.
    {
        float edgeX, edgeZ, outX, outZ;
        EdgeDirections(edgeX, edgeZ, outX, outZ);
        if (outX * (marioState->pos[0] - pyramid->oPosX) + outZ * (marioState->pos[2] - pyramid->oPosZ) < _args.edgeSide)
            return false;
    }

    // The actions of the run, the approaches, the leave and the squish itself. Anything else
    // (the lava boost, a punch, the slide stopped on the stomach, first person) is a lineage
    // with no ending.
    switch (marioState->action)
    {
    case ACT_WALKING:
    case ACT_FINISH_TURNING_AROUND:
    case ACT_TURNING_AROUND:
    case ACT_BRAKING:
    case ACT_BRAKING_STOP:
    case ACT_DECELERATING:
    case ACT_IDLE:
    case ACT_FREEFALL:
    case ACT_FREEFALL_LAND:
    case ACT_FREEFALL_LAND_STOP:
    case ACT_DIVE:
    case ACT_DIVE_SLIDE:
    case ACT_FORWARD_ROLLOUT:
    case ACT_SQUISHED: // the landing on the lava under the face: the squish cancel's own frame
        break;
    default:
        return false;
    }

    // Double turnaround (ideally should prevent this in movement choices)
    if (marioState->action == ACT_WALKING && marioState->forwardVel < 0)
        return false;

    // No pause outside the dive recover (the maintainer, 2026-10-13): START is the pause
    // buffer's, pressed on the dive's frame and on the menu's, both of which leave Mario on
    // the dive slide; no move draws it elsewhere, and a frame that did is refused here.
    if ((GetInputs(GetCurrentFrame() - 1).buttons & Buttons::START) && marioState->action != ACT_DIVE_SLIDE)
        return false;

    auto state = GetMetrics(GetCurrentFrame());
    if (!state.initialized)
        return false;

    // A ground action on the lava is the lava boost a frame later, invulnerable and over, so
    // the frame is kept only when the probe judged it (the normal within the probe's frames
    // of the target): the edge the run follows stands 4 to 30 units above the lava at the
    // tilts of the ending, under the 100 the game needs to leave the ground, so a step past
    // it is never a fall but a step onto the lava floor, still walking, and that frame is the
    // one place a squish cancel from this edge can be. Every other such frame is a dead end,
    // two thirds of the second run's table (43,784 of 66,013 rows, 2026-10-06). Airborne
    // over the lava he is a candidate throughout (Scattershot_BitfsDr has the plain rule).
    if (marioState->floorHeight == BitfsScEndingMetrics::LavaHeight && !(marioState->action & ACT_FLAG_AIR) && !state.probed)
        return false;

    // Conserve the adjusted remainder error exactly, on both axes, from the frame after the
    // hand-over on and through the departure, as the oscillation stage does
    // (Scattershot_BitfsDr::ValidateState): a tilt short of a full step snaps the normal to
    // the goal, and the target, which the squish cancel's frame must have exactly, is off
    // the lattice for good. Off the platform the steps toward flat are full on their own.
    if (int64_t(GetCurrentFrame()) - _args.initialFrame >= 2)
    {
        auto initialState = GetMetrics(_args.initialFrame + 1);
        if (state.adjustedRemainderError[0] != initialState.adjustedRemainderError[0])
            return false;
        if (state.adjustedRemainderError[2] != initialState.adjustedRemainderError[2])
            return false;
    }

    // The lattice pins the leave (the maintainer's example movies, 2026-10-06; ROADMAP 4.11):
    // off the platform both axes step toward flat together, so on the squish cancel's frame
    // each must have been the same number of steps further from flat than the target at the
    // leave, the fall's length. On the platform the run takes the squish axis one step toward
    // flat a frame and the run's axis one step away from it, and so does a strain back over
    // the platform in the air. Once the squish axis is nearer flat than the run's axis, or
    // nearer than the target, nothing brings it back: the lineage ends here instead of
    // filling the table (two thirds of the third run's rows were past that point). Without
    // a target there is no frame to pin, and any leave may be the one.
    if (_args.targetGiven)
    {
        const bool squishX = _args.squishEdgeX != 0;
        const int toFlatSquish = StepsToFlat(state, squishX ? 0 : 2);
        const int toFlatRun = StepsToFlat(state, squishX ? 2 : 0);
        if (toFlatSquish < 0 || toFlatSquish < toFlatRun)
            return false;
        // And the fall itself takes at least MinFallFrames, each a step toward flat: a leave
        // with fewer left on the squish axis cannot land on the target's frame, and on the
        // platform the count only falls (the fourth run's frontier arrived at the edge with 0
        // to 3 left).
        if (!state.departed && toFlatSquish < MinFallFrames)
            return false;
    }

    // A dive recover's landing (the maintainer, 2026-10-12): within the landing's reach of the
    // squish edge, where it is the approach; further inside, the flight that left the band
    // was a departure in the middle of the platform, with nothing to come back to.
    if (!state.departed && state.approach == BitfsScEndingMetrics::Approach::DIVE_RECOVER
        && state.phase == BitfsScEndingMetrics::Phase::EDGE
        && GetMetrics(GetCurrentFrame() - 1).phase == BitfsScEndingMetrics::Phase::DIVE
        && SquishEdgeDistance(marioState, pyramid) > DrLandingReach)
        return false;

    // Departed: over the squish edge (the maintainer, 2026-10-06: the squish spot is past the
    // far edge; a step onto the lava at the run's edge, or a dive leaving the band over the
    // platform, has no squish cancel to find), and the squish test has its window.
    if (state.departed && !state.leaveAtSquishEdge)
        return false;
    if (state.departed && int64_t(GetCurrentFrame()) - state.departureFrame > _args.probeWindow)
        return false;

    // And not moving off the edge faster than a landing under the face allows (maxLeaveSpeed).
    if (state.departed && _args.maxLeaveSpeed > 0.0f)
    {
        float outX, outZ, alongX, alongZ;
        SquishDirections(outX, outZ, alongX, alongZ);
        if (outX * marioState->vel[0] + outZ * marioState->vel[2] > _args.maxLeaveSpeed)
            return false;
    }

    // On the platform: the time budget (the maintainer, 2026-10-11: compute the bound, allow a
    // little less). The tilt across the squish edge falls 0.01 a frame while Mario is on the
    // edge's half of the platform, the catch needs it at the catch face's, the least that
    // keeps the side steep, and the fall from the edge to the catch is FallFrames of it; what
    // is left is for reaching the edge. The frames that takes, at the least: the distance to
    // the edge at his position from the half's middle, or from him once he is on it, at his
    // speed or a walk-off's. A state that cannot make it is refused, BudgetSlack frames
    // allowed (the first runs' leaves came 16 frames late with the tilt spent, 2026-10-11).
    if (!state.departed)
    {
        const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
        const float across = _args.squishEdgeX != 0 ? pyramid->oTiltingPyramidNormalX : pyramid->oTiltingPyramidNormalZ;
        const float framesLeft = (std::fabs(across) - std::fabs(CatchAcross(BitfsScEndingMetrics::Face::Of(pyramid)))) / 0.01f - float(FallFrames);
        const float out = face.Out(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]);
        const float distance = face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]) - std::max(out, 0.0f);
        const float framesNeeded = std::max(distance, 0.0f) / std::max(marioState->forwardVel, WalkOffSpeed);
        if (framesNeeded > framesLeft + BudgetSlack)
            return false;
    }

    // Departed and not yet squished: beside the squish edge, where the untilting face's sweep
    // can still catch him (ROADMAP 4.11, 2026-10-10: the squish is the frame whose new edge
    // first covers his position at its start, and the maintainer's movies fall 0 to 6.5
    // outside the edge), and up the edge from the corner enough for the cancel: where the
    // shortest push lands him the top must still stand over 100 (LeastEdgeHeight), less
    // CornerSlack. Both by the face as the game places it. And when the next frame's face
    // squishes him here, the squish must be one the probe's pushes can cancel
    // (CatchPlacements): a pancake is the end of the lineage. A landing back on the
    // platform is the lattice rules' above.
    if (state.departed && marioState->action != ACT_SQUISHED && !state.onPlatform)
    {
        // No further outside the edge than the sweep covers in the frames the window has
        // left (the edge's move over the next frame, read from the stepped normal), or than
        // SweepReach, whichever is less, plus the slack: a state the sweep cannot reach in
        // time only fills the table (2.5 million departed rows of a run the block cap ended,
        // 2026-10-11, all but one of them never caught).
        const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
        const auto next = BitfsScEndingMetrics::NextFace(pyramid);
        const float x = marioState->pos[0];
        const float z = marioState->pos[2];
        const float edge = face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z);
        const float outside = face.Out(_args.squishEdgeX, _args.squishEdgeZ, x, z) - edge;
        const float sweep = next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z) - edge;
        const float framesLeft = float(_args.probeWindow - (int64_t(GetCurrentFrame()) - state.departureFrame));
        if (outside > std::min(SweepReach, sweep * framesLeft) + BitfsScEndingMetrics::CatchSlack)
            return false;

        // And the tilt across the edge still a step over the catch's: it flattens a step a frame
        // in the air, and a side no longer steep cancels nothing (the uscz crossing's falls
        // reached the catch's height at -0.10 to -0.12, 2026-10-11).
        const float across = _args.squishEdgeX != 0 ? pyramid->oTiltingPyramidNormalX : pyramid->oTiltingPyramidNormalZ;
        if (std::fabs(across) + 0.0005f < std::fabs(CatchAcross(BitfsScEndingMetrics::Face::Of(pyramid))) + 0.01f)
            return false;

        // And able to reach the catch's window along the edge: somewhere in what he can still
        // reach along it in the frames the fall has left (ReachAlongEdge: his forward speed's
        // drift, the strain across and along his facing, a dive in the air), the edge on the
        // face the fall ends on must stand in the window of heights, from LeastEdgeHeight
        // less CornerSlack to SquishSpace plus the depth two sweeps lower the side. The
        // edge's height is linear along it, so the two ends of his reach bound it (the
        // 50,000-shot run of 2026-10-11 caught 1,105 states, all near the corner where the
        // edge stands 128 at the catch, 405 squished to a pancake, none cancelled; the three
        // squish cancels had left at 278 along the edge and drifted to 206). Until 2026-11-20
        // the rule carried him along the edge by his velocity alone, which at a step off is
        // the walk's, about a unit a frame: it refused the maintainer's fourth example at
        // its leave, where the edge stands 20 under the window and the fall moves him 54 units
        // up it (ROADMAP 4.11). The face is the one expected when the fall's frames are up,
        // not the latest the cancel allows: a brake's leave comes with the tilt nearly spent,
        // so for it the two are the same, but a dive recover's landing leaves with 0.06 more
        // across, where the latest face put the edge 20 lower than it stands at the catch
        // and refused every one of its leaves (194 of 243, 2026-10-13).
        const float pushDistance = PushDistance(marioState);
        const float fallLeft = std::max(0.0f, float(FallFrames) - float(int64_t(GetCurrentFrame()) - state.departureFrame));
        float alongLow, alongHigh;
        ReachAlongEdge(marioState, pyramid, fallLeft, alongLow, alongHigh);
        const auto catchFace = ExpectedCatchFace(x, z, face, fallLeft);
        float edgeX, edgeZ;
        EdgePoint(catchFace, alongLow, edgeX, edgeZ);
        const float heightLow = catchFace.TopHeight(edgeX, edgeZ) - BitfsScEndingMetrics::LavaHeight;
        EdgePoint(catchFace, alongHigh, edgeX, edgeZ);
        const float heightHigh = catchFace.TopHeight(edgeX, edgeZ) - BitfsScEndingMetrics::LavaHeight;
        if (std::max(heightLow, heightHigh) < LeastEdgeHeight(catchFace, pushDistance) - CornerSlack
            || std::min(heightLow, heightHigh) > BitfsScEndingMetrics::SquishSpace + SweepDepthSlack)
            return false;

        std::array<BitfsScEndingMetrics::Placement, 3> placements;
        if (BitfsScEndingMetrics::CatchPlacements(next, _args.squishEdgeX, _args.squishEdgeZ, x, marioState->pos[1], z, pushDistance, placements) == 0)
            return false;
    }

    return true;
}

float Scattershot_BitfsScEnding::GetStateFitness()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    auto state = GetMetrics(GetCurrentFrame());
    if (!state.initialized)
        return -std::numeric_limits<float>::infinity();

    switch (state.phase)
    {
    case BitfsScEndingMetrics::Phase::SWING:
        return marioState->forwardVel;

    case BitfsScEndingMetrics::Phase::DIVE:
    case BitfsScEndingMetrics::Phase::BRAKE:
    case BitfsScEndingMetrics::Phase::EDGE:
    case BitfsScEndingMetrics::Phase::AIR:
    default:
        // Once anything decelerates, speed is no measure (the maintainer, 2026-10-10): the
        // fastest leaves land past the face, where the example's walk off at 5 to 8 lands
        // under its edge. Time instead, the earlier the better, and the validation rules do
        // the pruning (the steps to the target that stood here until then, with the lattice
        // pin for target mode, are among them).
        return -float(GetCurrentFrame());
    }
}

std::string Scattershot_BitfsScEnding::GetCsvLabels()
{
    return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Phase,Approach,StepsX,StepsZ,Departed,SquishCancel,MarioYVel,ProbeAction,ProbeSpeed,ProbeHeight,SquishEdgeDist,Catch,LeaveAction");
}

bool Scattershot_BitfsScEnding::ForceAddToCsv()
{
    // The departure, the frames the next frame's face squishes him on, the squish cancels,
    // and a dive recover's slide, rollout and landing stop (the maintainer, 2026-10-13: every
    // landing that validates, not one in ten, and the recover itself in the viewer): what a
    // run's leaves and recoveries are read by. Every departed frame was 2.5 million rows of
    // a run the block cap ended (2026-10-11); the sampled rows keep the rest.
    const auto state = GetMetrics(GetCurrentFrame());
    if (state.departed)
        return int64_t(GetCurrentFrame()) == state.departureFrame || state.catchPlacements >= 0 || state.squishCancel;
    if (state.phase == BitfsScEndingMetrics::Phase::DIVE)
        return true;
    return state.phase == BitfsScEndingMetrics::Phase::EDGE && state.approach == BitfsScEndingMetrics::Approach::DIVE_RECOVER
        && GetMetrics(GetCurrentFrame() - 1).phase == BitfsScEndingMetrics::Phase::DIVE;
}

std::string Scattershot_BitfsScEnding::GetCsvRow()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    auto state = GetMetrics(GetCurrentFrame());

    // The action in hex with its name, the phase and approach by name and the flags as true
    // or false, as the viewer shows them: a categorical column is text where text makes
    // sense (the maintainer, 2026-10-06).
    char line[400];
    snprintf(line, sizeof(line), "%f,%f,%f,%d,%f,0x%08X (%s),%f,%f,%f,%s,%s,%d,%d,%s,%s,%f,0x%08X (%s),%f,%f,%f,%d,0x%08X (%s)",
        marioState->pos[0],
        marioState->pos[1],
        marioState->pos[2],
        marioState->faceAngle[1],
        marioState->forwardVel,
        unsigned(marioState->action),
        Sm64ActionName(marioState->action),
        pyramid->oTiltingPyramidNormalX,
        pyramid->oTiltingPyramidNormalY,
        pyramid->oTiltingPyramidNormalZ,
        BitfsScEndingMetrics::PhaseName(state.phase),
        BitfsScEndingMetrics::ApproachName(state.approach),
        state.incrementFrames[0],
        state.incrementFrames[2],
        state.departed ? "true" : "false",
        state.squishCancel ? "true" : "false",
        marioState->vel[1],
        // The probe's recorded placement on a probed frame (the one that succeeded, else the one
        // whose knockback took, else the fastest), zeros where the probe did not run: what the
        // bully made of Mario a frame on, for reading a run's squish frames.
        state.probed ? unsigned(state.probeAction) : 0u,
        state.probed ? Sm64ActionName(state.probeAction) : "-",
        state.probed ? state.probeSpeed : 0.0f,
        state.probed ? state.probeHeight : 0.0f,
        // Mario's distance inside the squish edge (negative outside it), and the catch: -2
        // not looked at, -1 no squish next frame, 0 a pancake, else the pushes that cancel it.
        SquishEdgeDistance(marioState, pyramid),
        state.catchPlacements,
        // The action Mario left the platform from, on the departed rows: a rollout flying off
        // the edge and a walk off a landing stop are both the dive recover's, and read apart here.
        state.departed ? unsigned(state.leaveAction) : 0u,
        state.departed ? Sm64ActionName(state.leaveAction) : "-");

    return std::string(line);
}

bool Scattershot_BitfsScEnding::IsSolution()
{
    const auto& state = GetMetrics(GetCurrentFrame());
    return state.initialized && state.squishCancel;
}

BitfsScEndingSolution Scattershot_BitfsScEnding::GetSolutionState()
{
    const auto& state = GetMetrics(GetCurrentFrame());

    auto solution = BitfsScEndingSolution();
    solution.fSpd = state.fSpd;
    solution.pyraNormX = state.pyraNormX;
    solution.pyraNormY = state.pyraNormY;
    solution.pyraNormZ = state.pyraNormZ;
    solution.xzSum = state.xzSum;
    solution.approach = int(state.approach);
    solution.leaveAction = state.leaveAction;
    solution.leaveSpeed = state.leaveSpeed;
    solution.leaveYaw = state.leaveYaw;
    solution.leaveCUp = state.leaveCUp ? 1 : 0;
    solution.quickturn = state.quickturn ? 1 : 0;
    solution.leaveEdgeDistance = state.leaveEdgeDistance;
    solution.strainBackFrames = state.strainBackFrames;
    solution.nutLandings = state.nutLandings;
    solution.departureFrame = state.departureFrame;
    solution.squishFrame = state.frame;
    solution.probeIndex = state.probeIndex;
    solution.probeDx = state.probeDx;
    solution.probeDz = state.probeDz;
    solution.probeSpeed = state.probeSpeed;
    solution.probeHeight = state.probeHeight;
    solution.probeNormalX = state.probeNormalX;
    solution.probeNormalZ = state.probeNormalZ;

    return solution;
}

Object* Scattershot_BitfsScEnding::Pyramid()
{
    Object* objectPool = (Object*)(ReadState("gObjectPool"));
    return &objectPool[_args.platform];
}

void Scattershot_BitfsScEnding::EdgeDirections(float& edgeX, float& edgeZ, float& outX, float& outZ) const
{
    // Along the edge in the run's direction, and off it away from the platform: the edge
    // lies on the side whose sign the oscillation corner and the target corner share.
    edgeX = gSineTable[uint16_t(_roughTargetAngle) >> 4];
    edgeZ = gCosineTable[uint16_t(_roughTargetAngle) >> 4];
    if (BitfsScEndingMetrics::CornerSignZ(_args.oscQuadrant) == BitfsScEndingMetrics::CornerSignZ(_args.targetQuadrant))
    {
        outX = 0.0f;
        outZ = float(BitfsScEndingMetrics::CornerSignZ(_args.oscQuadrant));
    }
    else
    {
        outX = float(BitfsScEndingMetrics::CornerSignX(_args.oscQuadrant));
        outZ = 0.0f;
    }
}

void Scattershot_BitfsScEnding::SquishDirections(float& outX, float& outZ, float& alongX, float& alongZ) const
{
    // Off the squish edge, away from the platform, and along it (either way).
    outX = float(_args.squishEdgeX);
    outZ = float(_args.squishEdgeZ);
    alongX = -outZ;
    alongZ = outX;
}

int16_t Scattershot_BitfsScEnding::SquishEdgeYaw() const
{
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    return atan2s(outZ, outX);
}

float Scattershot_BitfsScEnding::SquishEdgeDistance(float x, float z, const BitfsScEndingMetrics::Face& face) const
{
    return face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z) - face.Out(_args.squishEdgeX, _args.squishEdgeZ, x, z);
}

float Scattershot_BitfsScEnding::SquishEdgeDistance(const MarioState* marioState, const Object* pyramid) const
{
    return SquishEdgeDistance(marioState->pos[0], marioState->pos[2], BitfsScEndingMetrics::Face::Of(pyramid));
}

float Scattershot_BitfsScEnding::PushDistance(const MarioState* marioState) const
{
    // The bully's hitbox radius from the object when there is one; a small bully's 73 else.
    float bullyRadius = 73.0f;
    if (_args.bully >= 0)
    {
        Object* objectPool = (Object*)(ReadState("gObjectPool"));
        bullyRadius = objectPool[_args.bully].hitboxRadius;
    }
    return bullyRadius + marioState->marioObj->hitboxRadius + BitfsScEndingMetrics::PushPadding;
}

float Scattershot_BitfsScEnding::LeastEdgeHeight(const BitfsScEndingMetrics::Face& face, float pushDistance) const
{
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    return BitfsScEndingMetrics::LeaveGroundHeight + std::fabs(face.Rise(alongX, alongZ)) * 0.5f * pushDistance;
}

void Scattershot_BitfsScEnding::EdgePoint(const BitfsScEndingMetrics::Face& face, float along, float& x, float& z) const
{
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    x = face.homeX + alongX * along;
    z = face.homeZ + alongZ * along;
    const float edgeOut = face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z);
    x += outX * edgeOut;
    z += outZ * edgeOut;
}

void Scattershot_BitfsScEnding::AimPoint(const BitfsScEndingMetrics::Face& face, float pushDistance, float& x, float& z, float fraction) const
{
    // The edge's height is linear along it: read at two points, then the coordinate where it
    // is the fraction asked for of the catch's window, from its low end, kept within ten
    // units of the edge's ends.
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    float x0, z0, x1, z1;
    EdgePoint(face, 0.0f, x0, z0);
    EdgePoint(face, 100.0f, x1, z1);
    const float height0 = face.TopHeight(x0, z0) - BitfsScEndingMetrics::LavaHeight;
    const float height1 = face.TopHeight(x1, z1) - BitfsScEndingMetrics::LavaHeight;
    const float least = LeastEdgeHeight(face, pushDistance);
    const float wanted = least + fraction * (BitfsScEndingMetrics::SquishSpace - least);
    float along = 0.0f;
    if (height1 != height0)
        along = (wanted - height0) / (height1 - height0) * 100.0f;
    const float endForward = face.CornerAlong(_args.squishEdgeX, _args.squishEdgeZ, alongX, alongZ);
    const float endBack = -face.CornerAlong(_args.squishEdgeX, _args.squishEdgeZ, -alongX, -alongZ);
    along = std::clamp(along, endBack + 10.0f, endForward - 10.0f);
    EdgePoint(face, along, x, z);
}

void Scattershot_BitfsScEnding::ReachAlongEdge(const MarioState* marioState, const Object* pyramid, float frames, float& low, float& high) const
{
    // The air's controls, as update_air_without_turn applies them: the forward speed moves
    // him along his facing, and a stick changes it by up to ForwardStrain a frame, less the
    // drag, so over the frames that part of his reach grows with their triangle number; a
    // stick across his facing moves him SidewaysStrain a frame, at once and not kept, of
    // which the share along the edge is his facing's share across it; and B on a fall adds
    // the dive's speed along his facing. Each bound is the most the control can do, so the
    // range holds every position he can come to.
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    const uint16_t yaw = uint16_t(marioState->faceAngle[1]) >> 4;
    const float facingAlong = alongX * gSineTable[yaw] + alongZ * gCosineTable[yaw];
    const float facingOut = outX * gSineTable[yaw] + outZ * gCosineTable[yaw];
    const float position = alongX * (marioState->pos[0] - pyramid->oPosX) + alongZ * (marioState->pos[2] - pyramid->oPosZ);
    const float drift = marioState->forwardVel * facingAlong * frames;
    const float strain = SidewaysStrain * std::fabs(facingOut) * frames
        + (ForwardStrain + AirDrag) * std::fabs(facingAlong) * frames * (frames + 1.0f) / 2.0f;
    const float dive = marioState->action == ACT_FREEFALL ? AirDiveSpeed * facingAlong * frames : 0.0f;
    low = position + drift - strain + std::min(dive, 0.0f);
    high = position + drift + strain + std::max(dive, 0.0f);
}

float Scattershot_BitfsScEnding::CatchAcross(const BitfsScEndingMetrics::Face& face) const
{
    float nx = face.nx;
    const float ny = face.ny;
    float nz = face.nz;
    if (_args.targetGiven)
        return _args.squishEdgeX != 0 ? _args.targetNx : _args.targetNz;

    // The component across the squish edge, a step at a time toward flat while the side
    // stays steep (the first runs of 2026-10-11 left with it spent, at 0.0 to -0.1, where
    // the edge no longer sweeps and no catch comes).
    float& across = _args.squishEdgeX != 0 ? nx : nz;
    const float sign = across < 0.0f ? -1.0f : 1.0f;
    float magnitude = std::fabs(across);
    while (magnitude > 0.01f)
    {
        across = sign * (magnitude - 0.01f);
        if (BitfsScEndingMetrics::Face::Of(nx, ny, nz, face.homeX, face.homeY, face.homeZ).SideNormalY(_args.squishEdgeX, _args.squishEdgeZ)
            <= BitfsScEndingMetrics::SteepCeiling - BitfsScEndingMetrics::SteepSlack)
            break;
        magnitude -= 0.01f;
    }
    return sign * magnitude;
}

BitfsScEndingMetrics::Face Scattershot_BitfsScEnding::CatchFace(const BitfsScEndingMetrics::Face& face) const
{
    float nx = face.nx;
    float nz = face.nz;
    (_args.squishEdgeX != 0 ? nx : nz) = CatchAcross(face);
    if (_args.targetGiven)
        (_args.squishEdgeX != 0 ? nz : nx) = _args.squishEdgeX != 0 ? _args.targetNz : _args.targetNx;
    return BitfsScEndingMetrics::Face::Of(nx, face.ny, nz, face.homeX, face.homeY, face.homeZ);
}

float Scattershot_BitfsScEnding::FramesToCatch(float edgeDistance, float forwardVel) const
{
    return std::max(edgeDistance, 0.0f) / std::max(forwardVel, WalkOffSpeed) + float(LeaveFrames) + float(FallFrames);
}

BitfsScEndingMetrics::Face Scattershot_BitfsScEnding::ExpectedCatchFace(float x, float z, const BitfsScEndingMetrics::Face& face, float framesToCatch) const
{
    if (_args.targetGiven)
        return CatchFace(face);
    float nx = face.nx;
    float nz = face.nz;
    float& across = _args.squishEdgeX != 0 ? nx : nz;
    float& along = _args.squishEdgeX != 0 ? nz : nx;
    const float frames = std::floor(framesToCatch);
    const float latest = CatchAcross(face);
    const float sign = across < 0.0f ? -1.0f : 1.0f;
    across = sign * std::max(std::fabs(across) - 0.01f * frames, std::fabs(latest));

    // The run's axis: on the platform the normal approaches the goal Mario's position sets
    // (the game's (dx, 500, dz) normalized, dx and dz his offset from the home) by 0.01 a
    // frame, and off it the goal is flat. The frames on the platform are all but the
    // fall's; the goal is read from where he is now, close enough to where the aim is
    // drawn (0.54 at the dive, 0.43 at the landing; the normal reaches neither).
    auto approach = [](float value, float goal, float steps)
    {
        const float move = std::min(std::fabs(goal - value), 0.01f * steps);
        return value + (goal > value ? move : -move);
    };
    const float fallFrames = std::min(frames, float(FallFrames));
    const float dx = x - face.homeX;
    const float dz = z - face.homeZ;
    const float goal = (_args.squishEdgeX != 0 ? dz : dx) / std::sqrt(dx * dx + 500.0f * 500.0f + dz * dz);
    along = approach(along, goal, frames - fallFrames);
    along = approach(along, 0.0f, fallFrames);
    return BitfsScEndingMetrics::Face::Of(nx, face.ny, nz, face.homeX, face.homeY, face.homeZ);
}

BitfsScEndingMetrics::Face Scattershot_BitfsScEnding::ExpectedCatchFace(const MarioState* marioState, const Object* pyramid) const
{
    const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
    return ExpectedCatchFace(marioState->pos[0], marioState->pos[2], face,
        FramesToCatch(SquishEdgeDistance(marioState->pos[0], marioState->pos[2], face), marioState->forwardVel));
}

int Scattershot_BitfsScEnding::StepsToFlat(const BitfsScEndingMetrics::CustomScriptStatus& state, int axis) const
{
    // The metric counts steps to the target with the sign of target minus normal; toward flat
    // is toward zero, so the count is read against the target's sign (the hand-over's normal
    // when no target is given).
    const float target = axis == 0 ? state.targetNx : state.targetNz;
    return (target < 0.0f ? 1 : -1) * state.incrementFrames[axis];
}

void Scattershot_BitfsScEnding::PyramidGoal(const MarioState* marioState, const Object* pyramid, float& goalX, float& goalZ)
{
    // The normal the pyramid tilts toward while Mario stands where he is, as
    // bhv_tilting_inverted_pyramid_loop computes it: his direction from the pyramid's home
    // while he is on it, flat when he is not.
    if (marioState->marioObj->platform != pyramid)
    {
        goalX = 0.0f;
        goalZ = 0.0f;
        return;
    }
    float dx = marioState->pos[0] - pyramid->oPosX;
    float dy = 500.0f;
    float dz = marioState->pos[2] - pyramid->oPosZ;
    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    d = float(1.0 / d);
    goalX = dx * d;
    goalZ = dz * d;
}

bool Scattershot_BitfsScEnding::Steps(const MarioState* marioState, const Object* pyramid)
{
    float goalX, goalZ;
    PyramidGoal(marioState, pyramid, goalX, goalZ);

    // approach_by_increment(goal, src, 0.01f) moves src the whole increment only when the goal
    // is at least that far away on that side; otherwise src becomes the goal.
    auto fullStep = [](float goal, float normal)
    {
        bool up = normal <= goal && !(goal - normal < 0.01f);
        bool down = normal > goal && !(goal - normal > -0.01f);
        return up || down;
    };
    return fullStep(goalX, pyramid->oTiltingPyramidNormalX) && fullStep(goalZ, pyramid->oTiltingPyramidNormalZ);
}

bool Scattershot_BitfsScEnding::StepInputs(Inputs inputs)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    return ModifyAdhoc([&]()
        {
            AdvanceFrameWrite(inputs);
            return Steps(marioState, pyramid);
        }).executed;
}

bool Scattershot_BitfsScEnding::StepFrame(int16_t intendedYaw, float intendedMag, uint16_t buttons, Rotation bias)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));

    auto stick = Inputs::GetClosestInputByYawExact(intendedYaw, intendedMag, camera->yaw, bias);
    if (StepInputs(Inputs(buttons, stick.first, stick.second)))
        return true;

    // The variants: the yaw a step of 16 HAU at a time to either side, the nearer first and
    // the side to start on drawn, so two pellets in the same state do not always resolve the
    // same way.
    int first = GetTempRng() % 2 == 0 ? 1 : -1;
    for (int k = 1; k <= 8; k++)
    {
        for (int side : { first, -first })
        {
            stick = Inputs::GetClosestInputByYawHau(int16_t(intendedYaw + side * k * 256), intendedMag, camera->yaw);
            if (StepInputs(Inputs(buttons, stick.first, stick.second)))
                return true;
        }
    }

    // A released stick brakes from 16 speed, and keeps braking below it.
    bool brakes = marioState->action == ACT_BRAKING ? marioState->forwardVel > 5.0f : marioState->action == ACT_WALKING && marioState->forwardVel >= 16.0f;
    return brakes && StepInputs(Inputs(buttons, 0, 0));
}

bool Scattershot_BitfsScEnding::RunDownhill_1f(bool min)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    bool stepped = false;
    ModifyAdhoc([&]()
        {
            if (marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                return true;

            auto status = Test<GetMinimumDownhillWalkingAngle>(_roughTargetAngle, marioState->faceAngle[1]);
            if (!status.asserted)
                return true;

            int16_t intendedYaw;
            int16_t minAngle = marioState->action == ACT_TURNING_AROUND ? status.angleFacingAnalogBack : status.angleFacing;
            if (min)
                intendedYaw = minAngle;
            else
            {
                // A deviation of up to 45 degrees from the minimum angle, toward the floor's
                // downhill or toward the target and no further than it (Scattershot_BitfsDr).
                int16_t toFloor = ScriptMath::Sign(int16_t(status.floorAngle - minAngle));
                int16_t toTarget = int16_t(_roughTargetAngle - minAngle);
                if (marioState->action == ACT_TURNING_AROUND || GetTempRng() % 2 == 0)
                    intendedYaw = int16_t(minAngle + toFloor * int16_t(GetTempRng() % 8192));
                else
                    intendedYaw = int16_t(minAngle + ScriptMath::Sign(toTarget) * std::min(std::abs(int(toTarget)), int(GetTempRng() % 8192)));
            }

            stepped = StepFrame(intendedYaw, 32, 0, status.downhillRotation);
            return stepped;
        });

    return stepped && (marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING);
}

bool Scattershot_BitfsScEnding::TurnUphill_1f()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    bool stepped = false;
    ModifyAdhoc([&]()
        {
            if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                return true;

            auto status = Test<GetMinimumDownhillWalkingAngle>(0);
            if (!status.asserted)
                return true;

            int16_t uphillAngle = status.floorAngle + 0x8000;

            // Toward uphill by 2048 at most
            int16_t intendedYaw = uphillAngle;
            if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                intendedYaw = marioState->faceAngle[1] + 2048 * ScriptMath::Sign(int16_t(uphillAngle - marioState->faceAngle[1]));

            stepped = StepFrame(intendedYaw, 32);
            return stepped;
        });

    return stepped && (marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING);
}

bool Scattershot_BitfsScEnding::RunRandom()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
        return false;

    // The run's exploration: a full stick within 90 degrees of the facing, so the run bends
    // but never turns around or slows (the brake is its own move, offered near the edge; a
    // random magnitude here bled the speed the approach needs, 2026-10-11).
    int16_t intendedYaw = int16_t(marioState->faceAngle[1] + int(GetTempRng() % 32769) - 16384);
    const bool stepped = StepFrame(intendedYaw, 32.0f);
    return stepped && (marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING);
}

bool Scattershot_BitfsScEnding::RunToEdge()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
        return false;

    // A full stick toward the aim on the squish edge (AimPoint on the catch's face: where the
    // edge will stand at the middle of the catch's window of heights, up the edge from the
    // corner; ROADMAP 4.11, 2026-10-11), within 15 degrees either way, held for one to six
    // frames: the run turns
    // at the walking rate, 2048 a frame, so a turn of 70 degrees from the hand-over's
    // heading is seven frames of the same stick, which one-frame draws almost never string
    // together (the fifth run's fast rows all still faced the run's edge's side).
    Object* pyramid = Pyramid();
    float aimX, aimZ;
    AimPoint(CatchFace(BitfsScEndingMetrics::Face::Of(pyramid)), PushDistance(marioState), aimX, aimZ);
    const int16_t aimYaw = atan2s(aimZ - marioState->pos[2], aimX - marioState->pos[0]);
    const int16_t intendedYaw = int16_t(aimYaw + int(GetTempRng() % 5462) - 2731);
    const int frames = 1 + int(GetTempRng() % 6);
    for (int i = 0; i < frames; i++)
    {
        if (!StepFrame(intendedYaw, 32))
            return i > 0;
        if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
            return false;
    }
    return true;
}

bool Scattershot_BitfsScEnding::Dive()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();

    if (marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
        return false;
    if (marioState->forwardVel < 29.0f)
        return false;

    // The recover's first move, reverted unless it dives: the turn, the dive and the pause.
    // The rollout and its landing are the next two (Rollout, Landing), each a block of its
    // own with a CSV row, so that the viewer shows the slide, the rollout and the landing
    // stop (the maintainer, 2026-10-13); a dive whose rollout cannot land in reach leaves
    // its slide in the table, a lineage the next move ends.
    return ModifyAdhoc([&]()
        {
            // The turn toward the aim first, a full stick at it each frame (the run turns 2048
            // a frame), then the dive as soon as the edge is within the recover's reach: the
            // turn is uphill and costs about half a unit of speed a frame, and so does every
            // frame run straight at the aim after it, so a run that arrives slow fails here,
            // its frames reverted (the uscz movie turns its last five frames from 35 to 31 and
            // dives 200 from the edge; drawn a frame at a time, the turn was never completed,
            // and with a dive point drawn across the window the runs that waited for it
            // arrived at 27, 2026-10-12). The aim is drawn across the catch's window of
            // heights: nearer the corner the turn is shorter.
            // The aim drawn across the catch's window (an aim past its high end, tried
            // 2026-10-13, moved no landing: the landing is where the rollout comes down, set by
            // the dive point and the heading the turn can afford, not by the aim).
            const float aimFraction = float(GetTempRng() % 101) / 100.0f;
            bool turned = false;
            for (int frame = 0; frame < 24; frame++)
            {
                if (marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
                    return false;
                float aimX, aimZ;
                AimPoint(ExpectedCatchFace(marioState, pyramid), PushDistance(marioState), aimX, aimZ, aimFraction);
                const int16_t aimYaw = atan2s(aimZ - marioState->pos[2], aimX - marioState->pos[0]);
                // Within a turn step of the aim: the game ends a turn on the stick's own yaw from
                // there, and the frame saved is half a unit of speed.
                turned = std::abs(int(int16_t(aimYaw - marioState->faceAngle[1]))) <= 2048;
                const float edgeDistance = SquishEdgeDistance(marioState, pyramid);
                if (turned && edgeDistance <= DrReach + DrLandingReach)
                    break;
                // Nearer than the dive's window before the turn is done: too late.
                if (edgeDistance < DrReach - 2.0f * DrLandingReach)
                    return false;
                // The stick at the aim, or as far toward it as the game turns without a
                // turnaround (analog_stick_held_back: a stick over 0x471C from the facing is
                // one, which ends the run; the facing turns 2048 a frame either way).
                int16_t turnYaw = aimYaw;
                const int away = int(int16_t(aimYaw - marioState->faceAngle[1]));
                if (std::abs(away) > 0x4000)
                    turnYaw = int16_t(marioState->faceAngle[1] + (away > 0 ? 0x4000 : -0x4000));
                if (!StepFrame(turnYaw, 32))
                    return false;
            }
            if (!turned || marioState->action != ACT_WALKING || marioState->forwardVel < 29.0f)
                return false;

            // The dive goes the way Mario faces (the stick full: a dive needs over 48 of raw
            // magnitude). Before it is written, the model plays it and the rollout after it
            // with each of the recover's sticks held: a dive none of them lands in reach is
            // not taken, its frames reverted (the maintainer, 2026-10-21; before it a gate on
            // the floor's rise under the dive, 16 a frame along the facing at the dive's
            // speed, stood in for this: the 8,000-shot corner runs' dives up the low edge
            // rose 13 and ended 20 to 36 over the floor with the edge 10 to 25 away).
            auto stick = Inputs::GetClosestInputByYawHau(marioState->faceAngle[1], 32, camera->yaw);
            {
                using AirStep = BitfsScEndingMetrics::AirStep;
                auto mario = AirStep::Mario::Of(marioState, pyramid);
                auto model = AirStep::Pyramid::Of(pyramid);
                const auto intended = Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, camera->yaw);
                if (!AirStep::Frame(mario, model, intended.first, intended.second, true) || !mario.floorIsTop)
                    return false;
                if (!RecoverReachable(mario, model, RolloutFrames))
                    return false;
            }
            if (!_args.pauseBuffer)
            {
                AdvanceFrameWrite(Inputs(Buttons::B, stick.first, stick.second));
                if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)
                    return false;
            }
            else
            {
                // The pause buffer: the dive's frame pauses the game too (the level still updates
                // on it), the paused frame releases B, the next unpauses, and the one after is the
                // unpause's own transition; none of the three updates the level, and a press on
                // any of them is lost (the maintainer's example movies replayed, 2026-10-06). A
                // dive that lands on its own frame then has its slide's first update still ahead
                // with B up, for the rollout's press. The corner resource pauses as the game
                // does (ROADMAP 4.22).
                AdvanceFrameWrite(Inputs(Buttons::B | Buttons::START, stick.first, stick.second));
                if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)
                    return false;
                AdvanceFrameWrite(Inputs(0, 0, 0));
                AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
                AdvanceFrameWrite(Inputs(0, 0, 0));
            }

            // On the dive's slide, or in its air short of it: the rollout's moves are next.
            return marioState->action == ACT_DIVE || marioState->action == ACT_DIVE_SLIDE;
        }).executed;
}

bool Scattershot_BitfsScEnding::Rollout()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)
        return false;

    // The recover's second move: the rollout's press and its first frame, steered by the
    // model (RecoverFrame). It ends in the rollout, for the landing's move; a rollout that
    // lands this early must land in reach.
    return ModifyAdhoc([&]()
        {
            const Recover recover = StartRecover();
            for (int frame = 0; frame < RolloutFirstFrames; frame++)
            {
                if (!RecoverFrame(recover, RolloutFrames - frame))
                    return false;
                if (marioState->action == ACT_FREEFALL_LAND_STOP)
                    return LandedInReach(recover.insideMax);
            }
            return marioState->action == ACT_FORWARD_ROLLOUT;
        }).executed;
}

bool Scattershot_BitfsScEnding::Landing()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    if (marioState->action != ACT_FORWARD_ROLLOUT)
        return false;

    // The recover's third move: the rest of the rollout's flight, steered by the model, to
    // its landing stop, in reach.
    return ModifyAdhoc([&]()
        {
            const Recover recover = StartRecover();
            for (int frame = RolloutFirstFrames; frame < RolloutFrames && marioState->action != ACT_FREEFALL_LAND_STOP; frame++)
            {
                if (!RecoverFrame(recover, RolloutFrames - frame))
                    return false;
            }
            return LandedInReach(recover.insideMax);
        }).executed;
}

Scattershot_BitfsScEnding::Recover Scattershot_BitfsScEnding::StartRecover()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    // Whether this recover aims at a landing spot (landingTargeted of them do) and which: a
    // point of the catch's window drawn across it, as the run's aim is, as its coordinate
    // along the squish edge; the zone it lands in, and whether it takes only the band's
    // flights.
    Recover recover;
    recover.insideMax = LandingZones[GetTempRng() % 3];
    recover.targeted = double(GetTempRng() % 1000) < _args.landingTargeted * 1000.0;
    recover.bandOnly = double(GetTempRng() % 1000) < _args.landingBand * 1000.0;
    if (recover.targeted)
    {
        float x, z;
        AimPoint(ExpectedCatchFace(marioState, pyramid), PushDistance(marioState), x, z, float(GetTempRng() % 101) / 100.0f);
        float outX, outZ, alongX, alongZ;
        SquishDirections(outX, outZ, alongX, alongZ);
        recover.targetAlong = alongX * (x - pyramid->oPosX) + alongZ * (z - pyramid->oPosZ);
    }
    return recover;
}

bool Scattershot_BitfsScEnding::RecoverFrame(const Recover& recover, int framesLeft)
{
    using AirStep = BitfsScEndingMetrics::AirStep;
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();

    // The frame's range of inputs: of the recover's sticks, as the game reads them, those
    // whose flight held from here lands in the zone, and keeps the pyramid his platform
    // on every frame when this recover takes only those (HeldLanding), each with its
    // landing's coordinate along the edge. Then the one landing nearest the drawn spot,
    // or one drawn from the range; the game's frame is the check.
    const auto mario = AirStep::Mario::Of(marioState, pyramid);
    const auto model = AirStep::Pyramid::Of(pyramid);
    struct Candidate
    {
        std::pair<int8_t, int8_t> stick;
        float along;
    };
    std::array<Candidate, 1 + RecoverYaws * 2> candidates;
    int count = 0;
    auto consider = [&](std::pair<int8_t, int8_t> stick)
    {
        const auto intended = stick == std::pair<int8_t, int8_t>(0, 0)
            ? std::pair<int16_t, float>(marioState->faceAngle[1], 0.0f)
            : Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, camera->yaw);
        float along;
        if (HeldLanding(mario, model, intended.first, intended.second, recover.insideMax, recover.bandOnly, framesLeft, along))
            candidates[count++] = { stick, along };
    };
    consider({ 0, 0 });
    for (int i = 0; i < RecoverYaws; i++)
    {
        for (float magnitude : RecoverMagnitudes)
            consider(Inputs::GetClosestInputByYawHau(int16_t(i * (65536 / RecoverYaws)), magnitude, camera->yaw));
    }
    if (count == 0)
        return false;
    const Candidate* chosen = &candidates[GetTempRng() % count];
    if (recover.targeted)
    {
        for (int i = 0; i < count; i++)
        {
            if (std::fabs(candidates[i].along - recover.targetAlong) < std::fabs(chosen->along - recover.targetAlong))
                chosen = &candidates[i];
        }
    }
    const uint16_t buttons = marioState->action == ACT_DIVE_SLIDE ? Buttons::B : 0;
    AdvanceFrameWrite(Inputs(buttons, chosen->stick.first, chosen->stick.second));
    return marioState->floor != nullptr && marioState->floor->object == pyramid;
}

bool Scattershot_BitfsScEnding::HeldLanding(BitfsScEndingMetrics::AirStep::Mario mario, BitfsScEndingMetrics::AirStep::Pyramid pyramid, int16_t intendedYaw, float intendedMag, float insideMax, bool band, int framesLeft, float& along) const
{
    using AirStep = BitfsScEndingMetrics::AirStep;
    for (int frame = 0; frame < framesLeft; frame++)
    {
        // B on the slide for the rollout's press, as the move writes it; before it the dive's
        // own air and its landing, when the dive did not land on its frame. With `band`,
        // every frame must end with the pyramid Mario's platform, within 4 of the top (the
        // maintainer, 2026-10-21: so the pyramid tilts toward him throughout; a flight above
        // the band flattened the run's axis to 0.36 by the landing where the squish cancels
        // need about 0.42, which the side under the edge is steep enough at).
        if (!AirStep::Frame(mario, pyramid, intendedYaw, intendedMag, mario.action == ACT_DIVE_SLIDE))
            return false;
        if (!mario.floorIsTop || (band && !mario.onPlatform))
            return false;
        if (!mario.landed || mario.action == ACT_DIVE_SLIDE)
            continue;
        if (!LandedInReach(mario.x, mario.z, pyramid.ToFace(), insideMax))
            return false;
        float outX, outZ, alongX, alongZ;
        SquishDirections(outX, outZ, alongX, alongZ);
        along = alongX * (mario.x - pyramid.homeX) + alongZ * (mario.z - pyramid.homeZ);
        return true;
    }
    return false;
}

bool Scattershot_BitfsScEnding::RecoverReachable(const BitfsScEndingMetrics::AirStep::Mario& mario, const BitfsScEndingMetrics::AirStep::Pyramid& pyramid, int framesLeft) const
{
    float along;
    if (HeldLanding(mario, pyramid, mario.yaw, 0.0f, DrLandingReach, false, framesLeft, along))
        return true;
    for (int i = 0; i < RecoverYaws; i++)
    {
        for (float magnitude : RecoverMagnitudes)
        {
            if (HeldLanding(mario, pyramid, int16_t(i * (65536 / RecoverYaws)), magnitude, DrLandingReach, false, framesLeft, along))
                return true;
        }
    }
    return false;
}

bool Scattershot_BitfsScEnding::LandedInReach(float x, float z, const BitfsScEndingMetrics::Face& face, float insideMax) const
{
    // Within the zone inside the squish edge. Where the edge stands at the landing is the
    // fall's business (the maintainer's third example, 2026-10-21): a landing at the edge's
    // low end reaches the catch window by a dive in the air along the rising edge.
    const float inside = SquishEdgeDistance(x, z, face);
    return inside >= -1.0f && inside <= insideMax;
}

bool Scattershot_BitfsScEnding::LandedInReach(float insideMax)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    if (marioState->action != ACT_FREEFALL_LAND_STOP)
        return false;
    return LandedInReach(marioState->pos[0], marioState->pos[2], BitfsScEndingMetrics::Face::Of(pyramid), insideMax);
}

bool Scattershot_BitfsScEnding::PunchCancel()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    if (marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_IDLE)
        return false;

    // B cancels the stop, or idle, into the standing punch, whose first frame steps the
    // punch's own 10 in the facing (sPunchingForwardVelocities; a landing stop's first
    // frame gives up the landing's speed to it). Then a full stick at the facing each frame:
    // the stick cancels the punch into a walk that keeps the 10 (check_common_action_exits)
    // and gains about 0.9 a frame, the facing kept, and the walk's step off the edge is
    // the fall. A punch whose own step crossed the edge is still on it by the next frame,
    // the edge having swept under him; the walk from there falls the same.
    AdvanceFrameWrite(Inputs(Buttons::B, 0, 0));
    if (marioState->action != ACT_PUNCHING)
        return false;
    const int16_t facing = marioState->faceAngle[1];
    auto stick = Inputs::GetClosestInputByYawHau(facing, 32, camera->yaw);
    for (int frame = 0; frame < 3; frame++)
    {
        AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
        if (marioState->action & ACT_FLAG_AIR)
            return true;
        if (marioState->action != ACT_WALKING)
            return false;
    }
    return false;
}

bool Scattershot_BitfsScEnding::HoldBack()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));

    // The stick against the facing: the turnaround from 16 speed, a walk back under it.
    auto stick = Inputs::GetClosestInputByYawHau(int16_t(marioState->faceAngle[1] + 0x8000), 32, camera->yaw);
    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
    return true;
}

bool Scattershot_BitfsScEnding::LeaveAlong()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();
    if (!Quickturn())
        return false;

    // The edge's line toward the catch window (the aim's side of Mario), turned outward by
    // up to an eighth of a turn; a full or half stick, since the walk from idle starts at
    // the stick's magnitude up to 8 and the squish cancels left at 12 to 15.
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    float aimX, aimZ;
    AimPoint(ExpectedCatchFace(marioState, pyramid), PushDistance(marioState), aimX, aimZ);
    const float toward = alongX * (aimX - marioState->pos[0]) + alongZ * (aimZ - marioState->pos[2]);
    const float sign = toward < 0.0f ? -1.0f : 1.0f;
    const int16_t lineYaw = atan2s(sign * alongZ, sign * alongX);
    const int rotation = int16_t(atan2s(outZ, outX) - lineYaw) > 0 ? 1 : -1;
    const int16_t intendedYaw = int16_t(lineYaw + rotation * int(GetTempRng() % 8193));
    const float magnitude = GetTempRng() % 2 == 0 ? 32.0f : 16.0f;
    auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, magnitude, camera->yaw);
    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
    return marioState->action == ACT_WALKING || (marioState->action & ACT_FLAG_AIR) != 0;
}

bool Scattershot_BitfsScEnding::WalkOff()
{
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);

    // A small stick off the squish edge, anywhere from straight off to the edge's own line
    // either way; the magnitude is the walk's target speed. From idle the walk starts the
    // way the stick points (the game sets the facing to it), so a leave near the edge's
    // line drifts little outward while it falls, which is what the sweep then catches: the
    // squish cancels of 2026-10-11 left along the edge, and the dive recover's leaves of
    // 2026-10-12, within 45 degrees of straight off, drifted out of the sweep's reach.
    static constexpr float Magnitudes[4] = { 2.0f, 4.0f, 8.0f, 16.0f };
    int16_t intendedYaw = int16_t(atan2s(outZ, outX) + int(GetTempRng() % 32769) - 16384);
    float magnitude = Magnitudes[GetTempRng() % 4];
    auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, magnitude, camera->yaw);
    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
    return true;
}

bool Scattershot_BitfsScEnding::Parallel()
{
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);

    // A full stick along the squish edge, either way, within 15 degrees of it.
    int16_t along = atan2s(alongZ, alongX);
    if (GetTempRng() % 2 == 0)
        along = int16_t(along + 0x8000);
    int16_t intendedYaw = int16_t(along + int(GetTempRng() % 5462) - 2731);
    auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
    return true;
}

bool Scattershot_BitfsScEnding::Quickturn()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));

    if (marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_BRAKING_STOP)
        return false;

    // A landing stop's first frame still carries the landing's speed: the stop zeroes it on
    // that frame (stationary_ground_step), after the game has read the stick
    // (check_common_landing_cancels), so a stick there cancels the stop into a walk at the
    // landing's speed, in the landing's facing (the maintainer, 2026-10-21: the dive recover's
    // leaves faced outward and never came through here; bitfs-turn --test pins the frame).
    // The stop gets that frame first. A braking stop is already at zero.
    if (marioState->forwardVel != 0.0f)
    {
        AdvanceFrameWrite(Inputs(0, 0, 0));
        if ((marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_BRAKING_STOP) || marioState->forwardVel != 0.0f)
            return false;
    }

    // One frame of the smallest stick the game reads (a cardinal of the camera: the other rays
    // have no small magnitude) takes the stop into walking at zero speed and turns the facing
    // by up to 2048 toward it; the released stick then goes straight to idle.
    int16_t cardinal = int16_t(camera->yaw + 0x4000 * int(GetTempRng() % 4));
    auto stick = Inputs::GetClosestInputByYawHau(cardinal, std::nextafter(0.0f, 1.0f), camera->yaw);
    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
    if (marioState->action != ACT_WALKING)
        return false;
    AdvanceFrameWrite(Inputs(0, 0, 0));
    return marioState->action == ACT_IDLE;
}

bool Scattershot_BitfsScEnding::CUpDecel()
{
    Camera* camera = *(Camera**)(ReadState("gCamera"));

    // C-up with a small stick or none: walking turns to decelerating under first person,
    // which a released stick cannot end before the speed is gone, a unit a frame; under one
    // unit the ground step barely moves him while the displacement does (the NUT spot).
    static constexpr float Magnitudes[3] = { 2.0f, 4.0f, 8.0f };
    int draw = int(GetTempRng() % 4);
    if (draw == 0)
    {
        AdvanceFrameWrite(Inputs(Buttons::C_UP, 0, 0));
        return true;
    }
    int16_t intendedYaw = int16_t(GetTempRng() % 65536);
    auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, Magnitudes[draw - 1], camera->yaw);
    AdvanceFrameWrite(Inputs(Buttons::C_UP, stick.first, stick.second));
    return true;
}

bool Scattershot_BitfsScEnding::WaitForTilt()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    // The frames the budget has left for the leave, as ValidateState counts them: the steps
    // across the edge down to the catch's, less the fall's.
    auto framesLeft = [&]()
    {
        const float across = _args.squishEdgeX != 0 ? pyramid->oTiltingPyramidNormalX : pyramid->oTiltingPyramidNormalZ;
        return (std::fabs(across) - std::fabs(CatchAcross(BitfsScEndingMetrics::Face::Of(pyramid)))) / 0.01f - float(FallFrames);
    };
    if (framesLeft() <= float(LeaveFrames))
        return false;
    for (int frame = 0; frame < 16; frame++)
    {
        if (!(marioState->action & ACT_FLAG_STATIONARY))
            return false;
        AdvanceFrameWrite(Inputs(0, 0, 0));
        if (marioState->marioObj->platform != pyramid)
            return false;
        if (framesLeft() <= float(LeaveFrames))
            return true;
    }
    return true;
}

bool Scattershot_BitfsScEnding::AirDive()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    if (marioState->action != ACT_FREEFALL)
        return false;
    // B on a fall is the dive (act_freefall): the speed along the facing up by 15, the fall's
    // vertical speed kept, then the dive's own air step with the strain's stick.
    if (!StrainBack(Buttons::B))
        return false;
    return marioState->action == ACT_DIVE;
}

bool Scattershot_BitfsScEnding::StrainBack(uint16_t buttons)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);

    // In the air past the squish edge: the stick on the line in and out of the platform, from
    // a full stick back to a full stick out, at the one that holds Mario where the sweep can
    // catch him (the
    // maintainer, 2026-10-13: he manages the drift off the edge with the stick by hand).
    // The edge sweeps outward about 3.3 a frame; a Mario it covers while the top is within
    // FloorReach above him lands on the top (the game's floor search reaches that far up),
    // and one it covers deeper is squished. So while he is not yet deep enough the stick
    // keeps him a sweep clear of the edge as it will stand a frame on, and once he is, just
    // StrainTarget outside it, for the next sweep to cover him. A stick back takes 1.5 a
    // frame off the speed at full magnitude and a stick out adds it, so a leave slower than
    // the sweep is pushed out ahead of it and one faster than the sweep by more than that
    // is beyond the hold for a frame or two (a full stick back every frame pulled slow
    // leaves back over the platform; no stick let a walk off at 3 to 7 a frame drift past
    // the sweep; a hold just outside the edge at every depth landed them on the top,
    // 2026-10-13).
    const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
    const auto next = BitfsScEndingMetrics::NextFace(pyramid);
    const float sweep = next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2])
        - face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]);
    const int16_t inYaw = atan2s(-outZ, -outX);
    const int16_t outYaw = atan2s(outZ, outX);
    static constexpr float Magnitudes[7] = { -32.0f, -16.0f, -8.0f, 0.0f, 8.0f, 16.0f, 32.0f }; // negative: back in
    struct Strain
    {
        float outside = 0; // past the edge as it stands after the frame
        float depth = 0;   // the top at his position above him, after the frame
    };
    auto error = [&](const Strain* strain)
    {
        const float target = strain->depth > BitfsScEndingMetrics::FloorReach + BitfsScEndingMetrics::CatchSlack ? StrainTarget : sweep + StrainTarget;
        return std::fabs(strain->outside - target);
    };
    auto step = ModifyCompareAdhoc<Strain, std::tuple<int8_t, int8_t>>(
        [&](auto iteration, auto& params) //paramsGenerator
        {
            if (iteration >= 7)
                return false;
            const float magnitude = Magnitudes[iteration];
            params = std::tuple(Inputs::GetClosestInputByYawHau(magnitude < 0.0f ? inYaw : outYaw, std::fabs(magnitude), camera->yaw));
            return true;
        },
        [&](auto status, int8_t stickX, int8_t stickY) //script
        {
            AdvanceFrameWrite(Inputs(buttons, stickX, stickY));
            status->outside = next.Out(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2])
                - next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]);
            status->depth = next.TopHeight(marioState->pos[0], marioState->pos[2]) - marioState->pos[1];
            return true;
        },
        [&](auto incumbent, auto challenger) //comparator
        {
            return error(challenger) < error(incumbent) ? challenger : incumbent;
        },
        [&](auto) { return false; } //terminator
    );
    return step.executed;
}

bool Scattershot_BitfsScEnding::NutStop()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();
    if (marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_BRAKING_STOP && marioState->action != ACT_IDLE)
        return false;

    // A NUT spot (the glossary): a spot at the edge from which the next frame's carry leaves
    // Mario off the platform, by the game's floor test on his position and the face's
    // vertices truncated to shorts, so that an action which cancels to freefall falls with no
    // step of his own. Measured 2026-10-17 (ROADMAP 4.11): on corner 1's half of the edge the
    // carry takes a stopped Mario inward, so the spot is a band under a unit wide at the edge
    // that acts on the frame right after he stops in it, and some tilts have none. The search
    // for one is played in the game: a one-frame stick toward the edge, neutral frames to the
    // stop (the walk starts at the stick's magnitude and the deceleration takes a unit a
    // frame, so where he stops moves continuously with the magnitude), and one frame more,
    // reverted, to see whether the carry takes him off. The magnitude is bisected between a
    // stop that stays and a walk that steps off, then the stops just short of the edge are
    // tried, about a fifth of a unit apart. The first stop that falls is kept, and its fall's
    // first frame is StrainBack's stick: a NUT spot is only worth taking with the strain on
    // that frame (the maintainer, 2026-10-17). False, nothing written, when no stop falls.
    const int16_t yaw = int16_t(SquishEdgeYaw() + int(GetTempRng() % 21846) - 10923); // within 60 degrees of straight off
    float stays = 0.0f;  // the largest magnitude whose stop stayed on the platform
    float leaves = 8.0f; // the smallest whose walk stepped off
    struct Stop
    {
        float outside = 0; // past the edge at the stop, by the face
    };
    auto stop = ModifyCompareAdhoc<Stop, std::tuple<float>>(
        [&](auto iteration, auto& params) //paramsGenerator
        {
            static constexpr int Bisections = 6;
            static constexpr int Probes = 5;
            if (iteration >= Bisections + Probes)
                return false;
            const float magnitude = iteration < Bisections ? 0.5f * (stays + leaves)
                : stays - float(iteration - Bisections + 1) * 0.2f / std::ceil(std::max(stays, 1.0f));
            if (magnitude <= 0.0f)
                return false;
            params = std::tuple(magnitude);
            return true;
        },
        [&](auto status, float magnitude) //script
        {
            auto stick = Inputs::GetClosestInputByYawHau(yaw, magnitude, camera->yaw);
            AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
            for (int frame = 0; frame < 10 && marioState->action != ACT_IDLE; frame++)
            {
                if ((marioState->action & ACT_FLAG_AIR) || marioState->marioObj->platform != pyramid || MarioDeath::IsDying(*marioState))
                {
                    leaves = std::min(leaves, magnitude);
                    return false;
                }
                AdvanceFrameWrite(Inputs(0, 0, 0));
            }
            if (marioState->action != ACT_IDLE || marioState->marioObj->platform != pyramid)
                return false;
            stays = std::max(stays, magnitude);
            status->outside = -SquishEdgeDistance(marioState, pyramid);
            // The frame after the stop, reverted: the carry alone.
            return ExecuteAdhoc([&]()
                {
                    AdvanceFrameWrite(Inputs(0, 0, 0));
                    return (marioState->action & ACT_FLAG_AIR) != 0 && !MarioDeath::IsDying(*marioState);
                }).executed;
        },
        [&](auto incumbent, auto) { return incumbent; }, //comparator
        [&](auto) { return true; } //terminator: the first stop that falls
    );
    if (!stop.executed)
        return false;
    return StrainBack();
}

bool Scattershot_BitfsScEnding::NutLand()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();
    if (!(marioState->action & ACT_FLAG_AIR) || marioState->marioObj->platform == pyramid)
        return false;
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);

    // A frame with the pyramid Mario's platform while he is in the air beside the edge (the
    // maintainer, 2026-10-17): over the top within the band of 4 above it the game makes the
    // pyramid his platform for the frame without a landing, and it tilts toward him on the
    // next (the strain-back frame of the metrics), which works only at a low vertical speed;
    // or a touch down on the top, from which his forward speed, which the landing keeps,
    // steps him off again on the first landing frame when it beats the edge's sweep (about
    // 4.5 a frame; measured at 8, and at 3 the touch held him three frames and the landing
    // stop then left him inside). Either steps the run's axis toward him instead of toward
    // flat for its frames. Among the sticks on the line in and out of the platform, from none
    // to full, the gentlest that gives one: a hover first, a touch second; the frames are the
    // stick's and, for a touch, the neutral one that steps him off.
    const int16_t inYaw = atan2s(-outZ, -outX);
    const int16_t outYaw = atan2s(outZ, outX);
    static constexpr float Magnitudes[7] = { 0.0f, -8.0f, 8.0f, -16.0f, 16.0f, -32.0f, 32.0f }; // negative: back in
    struct Platform
    {
        int frames = 0; // 1 a hover, 2 a touch and the step off it
    };
    auto step = ModifyCompareAdhoc<Platform, std::tuple<int8_t, int8_t>>(
        [&](auto iteration, auto& params) //paramsGenerator
        {
            if (iteration >= 7)
                return false;
            const float magnitude = Magnitudes[iteration];
            params = magnitude == 0.0f ? std::tuple<int8_t, int8_t>(0, 0)
                : std::tuple(Inputs::GetClosestInputByYawHau(magnitude < 0.0f ? inYaw : outYaw, std::fabs(magnitude), camera->yaw));
            return true;
        },
        [&](auto status, int8_t stickX, int8_t stickY) //script
        {
            AdvanceFrameWrite(Inputs(0, stickX, stickY));
            if (MarioDeath::IsDying(*marioState) || marioState->marioObj->platform != pyramid)
                return false;
            if (marioState->action & ACT_FLAG_AIR)
            {
                status->frames = 1;
                return true;
            }
            AdvanceFrameWrite(Inputs(0, 0, 0));
            if (MarioDeath::IsDying(*marioState) || !(marioState->action & ACT_FLAG_AIR) || marioState->marioObj->platform == pyramid)
                return false;
            status->frames = 2;
            return true;
        },
        [&](auto incumbent, auto challenger) //comparator
        {
            return challenger->frames < incumbent->frames ? challenger : incumbent;
        },
        [&](auto status) { return status->frames == 1; } //terminator: a hover is the best there is
    );
    return step.executed;
}

bool Scattershot_BitfsScEnding::FallToCatch()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();
    const auto state = GetMetrics(GetCurrentFrame());
    if (!state.departed || !(marioState->action & ACT_FLAG_AIR) || marioState->marioObj->platform == pyramid)
        return false;
    float outX, outZ, alongX, alongZ;
    SquishDirections(outX, outZ, alongX, alongZ);
    const float pushDistance = PushDistance(marioState);

    // The squish comes on the frame whose face covers Mario with the top more than FloorReach
    // above him. A fall from the edge drops 60 in six air frames and 84 in seven, and the tilt
    // across the edge flattens a step every one of them, so the sooner the top is that far
    // above him the steeper the side still is. The edge rises toward its high end: a Mario who
    // moves up it as he falls has the top over him rise as well, and is deep enough a frame
    // sooner (the maintainer's fourth example, 2026-11-20: 54 units up the edge in six air
    // frames, the top 20 higher over him, squished with the side's normal y at -0.499; a
    // frame later it is -0.511 and a pancake). No one-frame move does this: StrainBack's
    // stick is on the line in and out of the platform, and the frames have to follow each
    // other from the first.
    //
    // So the fall is one move. Each frame its sticks are played in the game (FallYaws
    // directions from his facing at each of FallMagnitudes, and neutral: across his facing a
    // stick moves him SidewaysStrain, along it it changes his speed by ForwardStrain), and
    // the one kept is, in this order: one after which the next frame squishes him where a
    // push can cancel it, which ends the move on the probe's frame (CatchPlacements says
    // where a push can; whether the frame squishes is asked of the game, a frame played and
    // reverted, since the model's edge is good to a unit and the example's catch is 0.01
    // inside it: a fall the model called caught 0.34 outside was not squished); one that
    // holds him beside the sweeping edge as StrainBack does, within a unit; one that leaves
    // the edge at his position no higher than the height this fall climbs to, drawn across
    // the catch's window as the run's aim is; and the one that puts the top highest above
    // him. A stick that lands him, takes the fall onto the platform or into the lava is not
    // a fall and is left out. False, nothing written, when the first frame has no such
    // stick; else the frames stand, to the catch, the last frame he still falls on, or the
    // end of the probe's window.
    const float least = LeastEdgeHeight(BitfsScEndingMetrics::Face::Of(pyramid), pushDistance);
    const float climbTo = least + (BitfsScEndingMetrics::SquishSpace - least) * float(GetTempRng() % 101) / 100.0f;
    struct Fall
    {
        int catchPlacements = -1; // the next frame's face on him after the frame (CatchPlacements)
        bool squished = false;    // and the game squishes him on that next frame
        float outside = 0;        // past the edge as it stands after the frame
        float depth = 0;          // the top at his position above him, after the frame
        float edgeHeight = 0;     // the edge above the lava at his position along it, after the frame
    };
    bool fell = false;
    while (int64_t(GetCurrentFrame()) - state.departureFrame < int64_t(_args.probeWindow))
    {
        const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
        const auto next = BitfsScEndingMetrics::NextFace(pyramid);
        const float sweep = next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2])
            - face.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, marioState->pos[0], marioState->pos[2]);
        const int16_t facing = marioState->faceAngle[1];
        auto rank = [&](const Fall* fall)
        {
            const int caught = !fall->squished ? 1 : fall->catchPlacements > 0 ? 0 : 2; // a pancake last
            const float hold = fall->depth > BitfsScEndingMetrics::FloorReach + BitfsScEndingMetrics::CatchSlack ? StrainTarget : sweep + StrainTarget;
            const float miss = std::max(0.0f, std::fabs(fall->outside - hold) - StrainTarget);
            const float over = std::max(0.0f, fall->edgeHeight - climbTo);
            return std::tuple<int, float, float, float>(caught, std::floor(miss), std::floor(over), -fall->depth);
        };
        auto step = ModifyCompareAdhoc<Fall, std::tuple<int8_t, int8_t>>(
            [&](auto iteration, auto& params) //paramsGenerator
            {
                if (iteration > FallYaws * 2)
                    return false;
                if (iteration == 0)
                    params = std::tuple<int8_t, int8_t>(int8_t(0), int8_t(0));
                else
                {
                    const int16_t yaw = int16_t(facing + int((iteration - 1) / 2) * (65536 / FallYaws));
                    params = std::tuple(Inputs::GetClosestInputByYawHau(yaw, FallMagnitudes[(iteration - 1) % 2], camera->yaw));
                }
                return true;
            },
            [&](auto status, int8_t stickX, int8_t stickY) //script
            {
                AdvanceFrameWrite(Inputs(0, stickX, stickY));
                if (!(marioState->action & ACT_FLAG_AIR) || marioState->marioObj->platform == pyramid || MarioDeath::IsDying(*marioState))
                    return false;
                const float x = marioState->pos[0];
                const float z = marioState->pos[2];
                status->outside = next.Out(_args.squishEdgeX, _args.squishEdgeZ, x, z) - next.EdgeOut(_args.squishEdgeX, _args.squishEdgeZ, x, z);
                status->depth = next.TopHeight(x, z) - marioState->pos[1];
                float edgeX, edgeZ;
                EdgePoint(next, alongX * (x - pyramid->oPosX) + alongZ * (z - pyramid->oPosZ), edgeX, edgeZ);
                status->edgeHeight = next.TopHeight(edgeX, edgeZ) - BitfsScEndingMetrics::LavaHeight;
                std::array<BitfsScEndingMetrics::Placement, 3> placements;
                status->catchPlacements = BitfsScEndingMetrics::CatchPlacements(BitfsScEndingMetrics::NextFace(pyramid),
                    _args.squishEdgeX, _args.squishEdgeZ, x, marioState->pos[1], z, pushDistance, placements);
                // The model's catch allows a unit either way on the edge, so the game is asked:
                // a neutral frame from here, reverted (the squish is set from his position at
                // the frame's start, before any push, so the probe's frame squishes when this
                // one does).
                status->squished = status->catchPlacements >= 0 && ExecuteAdhoc([&]()
                    {
                        AdvanceFrameWrite(Inputs(0, 0, 0));
                        return marioState->action == ACT_SQUISHED;
                    }).executed;
                return true;
            },
            [&](auto incumbent, auto challenger) //comparator
            {
                return rank(challenger) < rank(incumbent) ? challenger : incumbent;
            },
            [&](auto status) { return status->squished && status->catchPlacements > 0; } //terminator: a squish the probe's pushes can cancel
        );
        if (!step.executed)
            return fell;
        fell = true;
        if (step.squished)
            return true;
    }
    return fell;
}
