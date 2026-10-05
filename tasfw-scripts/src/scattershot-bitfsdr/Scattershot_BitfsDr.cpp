#include <Scattershot_BitfsDr.hpp>
#include <ScriptMath.hpp>
#include <limits>

void Scattershot_BitfsDr::SelectMovementOptions()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    AddRandomMovementOption(
        {
            {BasicMoves::MAX_MAGNITUDE, 4},
            {BasicMoves::ZERO_MAGNITUDE, 0},
            {BasicMoves::SAME_MAGNITUDE, 0},
            {BasicMoves::RANDOM_MAGNITUDE, 1}
        });

    AddRandomMovementOption(
        {
            {BasicMoves::MATCH_FACING_YAW, 1},
            {BasicMoves::ANTI_FACING_YAW, 2},
            {BasicMoves::SAME_YAW, 4},
            {BasicMoves::RANDOM_YAW, 16}
        });

    AddRandomMovementOption(
        {
            {BasicMoves::SAME_BUTTONS, 0},
            {BasicMoves::NO_BUTTONS, 0},
            {BasicMoves::RANDOM_BUTTONS, 10}
        });

    auto state = GetMetrics(GetCurrentFrame());
    switch (state.phase)
    {
        case BitfsDrMetrics::Phase::INITIAL:
            // From the rest, only a frame that keeps the pyramid stepping on both axes
            // conserves the adjusted remainder error; a random stick does so about one frame
            // in three, so the first leg is a scripted move with random deviations. With a
            // handover above the rest's tilt, the frames after the first run into the corner
            // by the leads (LeadRun_1f toward the corner) until the tilt is there.
            if (Handover())
                AddRandomMovementOption(
                    {
                        {CustomMoves::NO_SCRIPT, 6},
                        {CustomMoves::FIRST_LEG, 2},
                        {CustomMoves::LEAD_RUN, 2}
                    });
            else
                AddRandomMovementOption(
                    {
                        {CustomMoves::NO_SCRIPT, 1},
                        {CustomMoves::FIRST_LEG, 9}
                    });
            break;

        case BitfsDrMetrics::Phase::RUN_DOWNHILL:
            // The swings are steered by the leads, not the downhill angle (LeadRun_1f; ROADMAP
            // 4.6): the downhill-angle moves run one lead dry and the swing dies at its turn,
            // from the rest and in every later oscillation alike (the per-pass census). The
            // uphill turn stays as the frame that drops the speed and moves the phase on; a
            // handover's own first swing keeps the old moves it was measured with.
            // A turnaround is offered straight from the run when the last frame ran uphill,
            // the one frame the validation lets a turnaround begin from, and on the first
            // swing from any frame (the validation's exemption, below).
            {
                bool canTurnAround = marioState->action == ACT_WALKING && marioState->forwardVel > 16.0f
                    && (GetMetrics(GetCurrentFrame() - 1).ranUphill || state.currentCrossing == 1);
                // The first swing's reversal is its needle (the census of 2026-10-04, ROADMAP 4.6):
                // it must come within a frame or two of the leg's end, an uphill frame then the
                // turnaround while the speed is still over the turnaround's 16, before the lead
                // steering carries Mario out to the +x side, where no crossing comes before the
                // sinking end is under him. So the first swing draws the uphill turn and the
                // turnaround as often as the run, where the later swings keep the run's weight
                // (three times in four against the run was measured no better: 16 plain runs
                // 7 full first oscillations against 9).
                bool firstSwing = state.currentCrossing == 1;
                if (state.currentCrossing > 1 || !Handover())
                    AddRandomMovementOption(
                        {
                            {CustomMoves::LEAD_RUN, firstSwing ? 4 : 9},
                            {CustomMoves::TURN_UPHILL, marioState->forwardVel <= 16.0f ? 0 : (firstSwing ? 4 : 1)},
                            {CustomMoves::TURN_AROUND, canTurnAround ? (firstSwing ? 4 : 1) : 0}
                        });
                else
                    AddRandomMovementOption(
                        {
                            {CustomMoves::NO_SCRIPT, 0},
                            {CustomMoves::RUN_DOWNHILL_MIN, 5},
                            {CustomMoves::TURN_UPHILL, marioState->forwardVel <= 16.0f ? 0 : 1},
                            {CustomMoves::TURN_AROUND, canTurnAround ? 1 : 0}
                        });
            }
            break;

        case BitfsDrMetrics::Phase::RUN_DOWNHILL_PRE_CROSSING:
            AddRandomMovementOption(
                {
                    {CustomMoves::RUN_DOWNHILL_MIN, 1},
                    {CustomMoves::RUN_DOWNHILL, 1}
                });
            break;

        case BitfsDrMetrics::Phase::TURN_UPHILL:
        {
            auto prevState = GetMetrics(GetCurrentFrame() - 1);
            bool avoidDoubleTurnaround = prevState.marioAction == ACT_FINISH_TURNING_AROUND && state.marioAction == ACT_WALKING;
            bool firstSwing = state.currentCrossing == 1; // the first swing's turnaround as often as the run (see RUN_DOWNHILL)

            if (state.currentCrossing > 1 || !Handover())
                AddRandomMovementOption(
                    {
                        {CustomMoves::LEAD_RUN, firstSwing ? 5 : 10},
                        {CustomMoves::TURN_AROUND, state.marioAction != ACT_WALKING || avoidDoubleTurnaround ? 0 : (firstSwing ? 5 : 1)}
                    });
            else
                AddRandomMovementOption(
                    {
                        {CustomMoves::NO_SCRIPT, 0},
                        {CustomMoves::TURN_UPHILL, 10},
                        {CustomMoves::RUN_FORWARD, 0},
                        {CustomMoves::TURN_AROUND, state.marioAction != ACT_WALKING || avoidDoubleTurnaround ? 0 : 1},
                        {CustomMoves::PBD, 0}
                    });
            break;
        }

        case BitfsDrMetrics::Phase::TURN_AROUND:
            AddMovementOption(CustomMoves::TURN_AROUND);
            break;

        case BitfsDrMetrics::Phase::ATTEMPT_DR:
            AddMovementOption(CustomMoves::NO_SCRIPT);
            break;

        case BitfsDrMetrics::Phase::QUICKTURN:
            AddMovementOption(CustomMoves::QUICKTURN);
            break;

        default:
            AddMovementOption(CustomMoves::NO_SCRIPT);
    }
}

bool Scattershot_BitfsDr::ApplyMovement()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    auto state = GetMetrics(GetCurrentFrame());

    // Scripts
    if (!CheckMovementOptions(CustomMoves::NO_SCRIPT))
    {
        if (CheckMovementOptions(CustomMoves::REWIND))
        {
            int64_t currentFrame = GetCurrentFrame();
            int maxRewind = int((currentFrame - config.StartFrame) / 2);
            int rewindFrames = (GetTempRng() % 100) * maxRewind / 100;
            Load(currentFrame - rewindFrames);
        }

        // A one-frame move that finds no frame keeping the pyramid stepping (StepFrame) ends
        // the pellet: the state has no continuation that conserves the error.
        if (CheckMovementOptions(CustomMoves::RUN_DOWNHILL_MIN))
            return RunDownhill_1f();
        else if (CheckMovementOptions(CustomMoves::RUN_DOWNHILL))
            return RunDownhill_1f(false);
        else if (CheckMovementOptions(CustomMoves::PBD) && Pbd())
            return true;
        else if (CheckMovementOptions(CustomMoves::TURN_UPHILL))
        {
            if (state.phase == BitfsDrMetrics::Phase::TURN_UPHILL && (GetTempRng() % 4) == 0)
            {
                int64_t intendedYaw = marioState->faceAngle[1] + ((GetTempRng() % 2048) - 1024);
                return StepFrame(int16_t(intendedYaw), 32);
            }
            return TurnUphill_1f();
        }
        else if (CheckMovementOptions(CustomMoves::RUN_FORWARD) && RunForwardThenTurnAround())
            return true;
        else if (CheckMovementOptions(CustomMoves::TURN_AROUND))
        {
            TurnAround();
            if (marioState->action == ACT_FINISH_TURNING_AROUND)
                Rollback(GetCurrentFrame() - 1);
            return true;
        }
        else if (CheckMovementOptions(CustomMoves::QUICKTURN) && Quickturn())
            return true;
        else if (CheckMovementOptions(CustomMoves::FIRST_LEG))
            return FirstLeg_1f(); // no random frame when no stick leads: the pellet ends
        else if (CheckMovementOptions(CustomMoves::LEAD_RUN))
            return LeadRun_1f(state.phase == BitfsDrMetrics::Phase::INITIAL);
    }

    /*
    if (marioState->action != ACT_FINISH_TURNING_AROUND && GetTempRng() % 2 == 0)
    {
        int64_t intendedYaw = marioState->faceAngle[1] + 0x8000;
        auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
        AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
        return true;
    }
    */

    // Random input
    return StepInputs(RandomInputs(
        {
            {Buttons::A, 0},
            {Buttons::B, marioState->action == ACT_DIVE_SLIDE ? 1 : 0},
            {Buttons::Z, 0},
            {Buttons::C_UP, 0}
        }));
}

BinaryStateBin<16> Scattershot_BitfsDr::GetStateBin()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    auto metrics = GetMetrics(GetCurrentFrame());

    int actionValue;
    switch (marioState->action)
    {
        case ACT_BRAKING: actionValue = 0; break;
        case ACT_DIVE: actionValue = 1; break;
        case ACT_DIVE_SLIDE: actionValue = 2; break;
        case ACT_FORWARD_ROLLOUT: actionValue = 3; break;
        case ACT_FREEFALL_LAND_STOP: actionValue = 4; break;
        case ACT_FREEFALL: actionValue = 5; break;
        case ACT_FREEFALL_LAND: actionValue = 6; break;
        case ACT_TURNING_AROUND: actionValue = 7; break;
        case ACT_FINISH_TURNING_AROUND: actionValue = 8; break;
        case ACT_WALKING: actionValue = 9; break;
        case ACT_DECELERATING: actionValue = 10; break;
        case ACT_IDLE: actionValue = 11; break;
        default: actionValue = 12;
    }

    int phaseValue;
    switch (metrics.phase)
    {
        case BitfsDrMetrics::Phase::INITIAL: phaseValue = 0; break;
        case BitfsDrMetrics::Phase::ATTEMPT_DR: phaseValue = 1; break;
        case BitfsDrMetrics::Phase::QUICKTURN: phaseValue = 2; break;
        case BitfsDrMetrics::Phase::RUN_DOWNHILL: phaseValue = 3; break;
        case BitfsDrMetrics::Phase::TURN_AROUND: phaseValue = 4; break;
        case BitfsDrMetrics::Phase::TURN_UPHILL: phaseValue = 5; break;
        case BitfsDrMetrics::Phase::RUN_DOWNHILL_PRE_CROSSING: phaseValue = 6; break;
        default: phaseValue = 7;
    }
        
    Object* pyramid = Pyramid();
    float xMin = pyramid->oPosX - SearchHalfExtent;
    float xMax = pyramid->oPosX + SearchHalfExtent;
    float zMin = pyramid->oPosZ - SearchHalfExtent;
    float zMax = pyramid->oPosZ + SearchHalfExtent;

    float xPosValue = std::clamp(marioState->pos[0], xMin, xMax);
    float zPosValue = std::clamp(marioState->pos[2], zMin, zMax);
        
    uint8_t bitCursor = 0;
    BinaryStateBin<16> state;

    if (metrics.initialized && metrics.reachedNormRegime && metrics.phase > BitfsDrMetrics::Phase::INITIAL)
    {
        state.AddValueBits(bitCursor, 2, 0);
        state.AddValueBits(bitCursor, 4, actionValue);
        state.AddValueBits(bitCursor, 3, phaseValue);
        state.AddValueBits(bitCursor, 4, std::clamp(metrics.currentCrossing, 0, 15));
        state.AddRegionBitsByRegionSize(bitCursor, 10, xPosValue, xMin, xMax, 5.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 10, zPosValue, zMin, zMax, 5.0f);
        //state.AddValueBits(bitCursor, 13, std::abs(marioState->faceAngle[1]) >> 4);
        state.AddRegionBitsByNRegions(bitCursor, 7, int(marioState->faceAngle[1]), -32768, 32767, 32);

        if (metrics.currentCrossing > 0)
        {
            if (!_normalSpecsDto.onlyMinMajor && metrics.currentOscillation >= _targetOscillation)
            {
                state.AddValueBits(bitCursor, 1, 1);
                unsigned int minimumBitsMajor = static_cast<unsigned int>(std::log2(_normalSpecsDto.regionsMajor)) + 1;
                unsigned int minimumBitsMinor = static_cast<unsigned int>(std::log2(_normalSpecsDto.regionsMinor)) + 1;

                float nMajor = 0;
                float nMinor = 0;
                if (std::fabs(metrics.crossingData.rbegin()->nZ) >= std::fabs(metrics.crossingData.rbegin()->nX))
                {
                    nMajor = std::clamp(std::fabs(metrics.crossingData.rbegin()->nZ), _normalSpecsDto.minMajor, _normalSpecsDto.maxMajor);
                    nMinor = std::clamp(std::fabs(metrics.crossingData.rbegin()->nX), _normalSpecsDto.minMinor, _normalSpecsDto.maxMinor);
                }
                else
                {
                    nMajor = std::clamp(std::fabs(metrics.crossingData.rbegin()->nX), _normalSpecsDto.minMajor, _normalSpecsDto.maxMajor);
                    nMinor = std::clamp(std::fabs(metrics.crossingData.rbegin()->nZ), _normalSpecsDto.minMinor, _normalSpecsDto.maxMinor);
                }

                state.AddRegionBitsByNRegions(bitCursor, minimumBitsMajor, nMajor, _normalSpecsDto.minMajor, _normalSpecsDto.maxMajor, uint64_t(_normalSpecsDto.regionsMajor));
                state.AddRegionBitsByNRegions(bitCursor, minimumBitsMinor, nMinor, _normalSpecsDto.minMinor, _normalSpecsDto.maxMinor, uint64_t(_normalSpecsDto.regionsMinor));
            }
            else
            {
                state.AddValueBits(bitCursor, 1, 0);
                state.AddRegionBitsByRegionSize(bitCursor, 8, metrics.crossingData.rbegin()->nZ, -0.7f, 0.7f, 0.005f);
                state.AddRegionBitsByRegionSize(bitCursor, 8, metrics.crossingData.rbegin()->nX, -0.7f, 0.7f, 0.005f);
            }

            int framesSinceCrossing = std::clamp(int(GetCurrentFrame() - metrics.crossingData.rbegin()->frame), 0, 63);
            if (metrics.phase == BitfsDrMetrics::Phase::RUN_DOWNHILL_PRE_CROSSING)
                state.AddValueBits(bitCursor, 6, framesSinceCrossing);
        }
    }
    else
    {
        // Below the regime, the first swing from the rest. Its state is the two leads (the goal
        // past the normal on each axis, which the crossing spends to the frame) and the tilt's
        // progress as much as Mario's cell, and a bin of the cell, four yaw quarters, the action
        // and the phase kept one owner, the fastest, for lineages whose crossing differs by a
        // frame: the first oscillation came one run in two under any weights, speed floor,
        // root set, budget or thread count, the table saturating near 3,400 blocks with
        // 10,000 shots unspent (2026-10-04, ROADMAP 4.6). So the bin also holds the yaw in 32
        // regions, the speed in 2-unit regions, the z normal (a step a frame) and both leads in
        // 0.005 regions.
        state.AddValueBits(bitCursor, 2, 1);
        state.AddValueBits(bitCursor, 4, actionValue);
        state.AddValueBits(bitCursor, 3, phaseValue);
        state.AddRegionBitsByRegionSize(bitCursor, 8, xPosValue, xMin, xMax, 5.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 8, zPosValue, zMin, zMax, 5.0f);
        state.AddRegionBitsByNRegions(bitCursor, 7, int(marioState->faceAngle[1]), -32768, 32767, 32);
        state.AddRegionBitsByRegionSize(bitCursor, 5, std::clamp(marioState->forwardVel, 0.0f, 63.9f), 0.0f, 64.0f, 2.0f);
        state.AddRegionBitsByRegionSize(bitCursor, 8, std::clamp(pyramid->oTiltingPyramidNormalZ, -0.68f, 0.599f), -0.68f, 0.6f, 0.005f);
        float leadX, leadZ;
        Leads(marioState, pyramid, 0, 0, leadX, leadZ);
        state.AddRegionBitsByRegionSize(bitCursor, 5, std::clamp(leadX, 0.0f, 0.159f), 0.0f, 0.16f, 0.005f);
        state.AddRegionBitsByRegionSize(bitCursor, 5, std::clamp(leadZ, 0.0f, 0.159f), 0.0f, 0.16f, 0.005f);
    }

    return state;
}

bool Scattershot_BitfsDr::ValidateState()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    // Position sanity check: near the platform, and not above where it could carry him
    if (std::fabs(marioState->pos[0] - pyramid->oPosX) > SearchHalfExtent)
        return false;

    if (std::fabs(marioState->pos[2] - pyramid->oPosZ) > SearchHalfExtent)
        return false;

    if (marioState->pos[1] > pyramid->oPosY + 465.0f)
        return false;

    // Quadrant check
    //if (pyramid->oTiltingPyramidNormalZ < -.15 || pyramid->oTiltingPyramidNormalX > 0.15)
    //    return false;

    // Action check
    if (marioState->action != ACT_BRAKING && marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE &&
        marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_FREEFALL &&
        marioState->action != ACT_FREEFALL_LAND && marioState->action != ACT_TURNING_AROUND &&
        marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
    {
        return false;
    }

    if (marioState->action == ACT_FREEFALL && marioState->vel[1] > -20.0)
        return false; //freefall without having done nut spot chain

    if (marioState->floorHeight > LavaHeight && marioState->pos[1] > marioState->floorHeight + 4 && marioState->vel[1] != 22.0)
        return false; //above pyra by over 4 units

    if (marioState->floorHeight == LavaHeight && marioState->action != ACT_FREEFALL)
        return false; //diving/dring above lava

    // Over the lava by at least minLavaClearance: lower, on a sinking end, the surface is under
    // Mario before the swing can leave it (the first swing's trap: the leg's populations run
    // downhill to the +z end and drown there about one run in two, 2026-10-03).
    if (marioState->pos[1] - LavaHeight < _normalSpecsDto.minLavaClearance)
        return false;

    // Double turnaround (ideally should prevent this in movement choices)
    if (marioState->action == ACT_WALKING && marioState->forwardVel < 0)
        return false;

    // Check custom metrics
    float xNorm = pyramid->oTiltingPyramidNormalX;
    float zNorm = pyramid->oTiltingPyramidNormalZ;
    auto state = GetMetrics(GetCurrentFrame());
    auto lastFrameState = GetMetrics(GetCurrentFrame() - 1);

    // The first swing leaves the end the leg ran to: Mario's progress toward the swing's
    // target, the chord's far end, may not fall more than maxRetreat below the leg's end. The
    // leg ends on the sinking end and downhill is toward it; the search shoots every block
    // alike, so a population that runs on there fills the table, drowns at the lava, and
    // starves the lineage that turned (the first oscillation's one run in two, 2026-10-03).
    if (state.phase != BitfsDrMetrics::Phase::INITIAL && state.currentCrossing == 1 && _normalSpecsDto.maxRetreat != INFINITY)
    {
        float toTargetX = gSineTable[uint16_t(state.roughTargetAngle) >> 4];
        float toTargetZ = gCosineTable[uint16_t(state.roughTargetAngle) >> 4];
        float progress = (marioState->pos[0] - state.legEndX) * toTargetX + (marioState->pos[2] - state.legEndZ) * toTargetZ;
        if (progress < -_normalSpecsDto.maxRetreat)
            return false;
    }

    //Herd to correct quadrant initially
    //if (!state.reachedNormRegime && marioState->pos[0] >= -2000.0f)
    //    return false;

    // Reject departures from the tilt the oscillation started at
    if (state.phase != BitfsDrMetrics::Phase::INITIAL
        && state.reachedNormRegime
        && fabs(xNorm) + fabs(zNorm) < _normalSpecsDto.startXzSum)
        return false;

    // And from the rest's own tilt, a swing's amplitude below it, from the first frame: from
    // a rest near the regime (ROADMAP 4.6) the search would otherwise wander the tilt down
    // until the regime's floor is first reached, and the fastest of those paths are what
    // the next pass would start from.
    auto rest = GetMetrics(state.initialFrame);
    if (state.phase != BitfsDrMetrics::Phase::INITIAL
        && fabs(xNorm) + fabs(zNorm) < fabs(rest.pyraNormX) + fabs(rest.pyraNormZ) - 0.05f)
        return false;

    // The tilt stays in the corner's quadrant, each axis at least minAxis (the fixer
    // guarantees the rest's lattice steps reversibly from its minNormal on, and short of it
    // the search roams the platform instead of swinging across the corner)
    if (state.phase != BitfsDrMetrics::Phase::INITIAL
        && (xNorm * float(_cornerSignX) < _normalSpecsDto.minAxis || zNorm * float(_cornerSignZ) < _normalSpecsDto.minAxis))
        return false;

    // Validate major and minor horizontal norms are in correct windows
    if (_targetOscillation > 0 && state.currentOscillation >= _targetOscillation && !_normalSpecsDto.onlyMinMajor)
    {
        float xNormCrossing = std::fabs(state.crossingData.rbegin()->nX);
        float zNormCrossing = std::fabs(state.crossingData.rbegin()->nZ);

        if (std::fabs(zNormCrossing) >= std::fabs(xNormCrossing))
        {
            if (std::clamp(zNormCrossing, _normalSpecsDto.minMajor, _normalSpecsDto.maxMajor) != zNormCrossing)
                return false;

            if (std::clamp(xNormCrossing, _normalSpecsDto.minMinor, _normalSpecsDto.maxMinor) != xNormCrossing)
                return false;
        }
        else
        {
            if (std::clamp(xNormCrossing, _normalSpecsDto.minMajor, _normalSpecsDto.maxMajor) != xNormCrossing)
                return false;

            if (std::clamp(zNormCrossing, _normalSpecsDto.minMinor, _normalSpecsDto.maxMinor) != zNormCrossing)
                return false;
        }
    }

    // A turnaround begins only from a frame the slope took speed on: Mario ran uphill the
    // frame before (the metric's ranUphill; the maintainer, 2026-09-26), in place of the
    // phase machine's uphill turn, which stood in for it and needed a speed drop first. The
    // chord's ends are uphill for a swing that starts at a crossing, the lagging normal
    // keeping the platform tilted toward the end just left, so the oscillation's own
    // turnarounds pass; a lineup's, from a downhill run, does not (ROADMAP 4.6). Not the
    // first swing's: it starts at the rest's equilibrium, where the normal has no lag and
    // the chord ahead is level and then downhill, so no frame on it ran uphill, and under
    // the rule the lineages left the chord to find one and ran into the lava or the
    // quadrant floor (the census of 2026-10-04). Free to turn from any frame, the first
    // swing crosses like the later ones: 16 plain runs of 16 full against 6 (the
    // maintainer, 2026-10-04).
    if (marioState->action == ACT_TURNING_AROUND && lastFrameState.marioAction != ACT_TURNING_AROUND
        && state.phase != BitfsDrMetrics::Phase::INITIAL && !lastFrameState.ranUphill && state.currentCrossing > 1)
        return false;

    // Below the regime the tilt climbs, with one shift outstanding at most (the maintainer,
    // 2026-09-26): once it has begun to rise it may sit up to one step an axis, the 0.02 of a
    // reversal pair in the inward order, under the highest tilt yet, and a second such shift
    // waits until a later one has repaid the first. Before, it had to rise on every frame.
    if (state.phase != BitfsDrMetrics::Phase::INITIAL
        && !state.reachedNormRegime
        && state.xzSumStartedIncreasing
        && state.xzSum < state.maxXzSum - 0.0201f)
        return false;

    // (Not on the first swing, which may reverse by walking at any speed; the maintainer,
    // 2026-09-25.)
    if (state.phase == BitfsDrMetrics::Phase::TURN_UPHILL && marioState->forwardVel <= 16.0f && state.currentCrossing > 1)
        return false;

    // Ensure we gain speed each crossing. Check both directions separately to account for axis asymmetry
    if (!BitfsDrMetrics::ValidateCrossingData(state, _normalSpecsDto.minMajor))
        return false;

    // The first crossing at least the speed asked for (the leg's end is crossingData[0], the
    // first crossing [1]): the ratchet above climbs from it.
    if (_minFirstCrossingSpeed > 0.0f && state.crossingData.size() >= 2 && state.crossingData[1].speed < _minFirstCrossingSpeed)
        return false;

    // Conserve the adjusted remainder error exactly, on both axes, from the equilibrium frame
    // on: the oscillations must hand the setup the error the fixer set. A tilt short of a full
    // step snaps the normal to the goal and moves it by thousands of ULPs; a full step keeps
    // it only when 0.01f added to the normal takes away exactly, which across a float binade
    // (0.25, 0.5) holds for half of the values a rest can take, so the fixer rests on one whose
    // lattice steps reversibly over the range the oscillations use (BitFsAreFixer::StepsReversibly).
    if (GetCurrentFrame() - state.initialFrame >= 2)
    {
        auto initialState = GetMetrics(state.initialFrame + 1);
        if (state.adjustedRemainderError[0] != initialState.adjustedRemainderError[0])
            return false;

        if (state.adjustedRemainderError[2] != initialState.adjustedRemainderError[2])
            return false;
    }

    return true;
}

float Scattershot_BitfsDr::GetStateFitness()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    auto state = GetMetrics(GetCurrentFrame());
    if (state.initialized)
    {
        switch (state.phase)
        {
        case BitfsDrMetrics::Phase::INITIAL:
            // The tilt toward the corner: of two lineups in one bin, the one further along.
            return state.pyraNormX * float(_cornerSignX) + state.pyraNormZ * float(_cornerSignZ);

        case BitfsDrMetrics::Phase::RUN_DOWNHILL:
            return marioState->forwardVel;

        case BitfsDrMetrics::Phase::RUN_DOWNHILL_PRE_CROSSING:
            if (marioState->action == ACT_TURNING_AROUND)
                return 0;
            else
                return marioState->forwardVel;

        case BitfsDrMetrics::Phase::TURN_UPHILL:
        default:
            if (state.crossingData.empty())
                return -float(GetCurrentFrame());

            return float(state.crossingData.rbegin()->frame) - float(GetCurrentFrame());
        }
    }

    return -std::numeric_limits<float>::infinity();
}

std::string Scattershot_BitfsDr::GetCsvLabels()
{
    return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Oscillation,Crossing,Phase,XzSum");
}

bool Scattershot_BitfsDr::ForceAddToCsv()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

    if (marioState->action == ACT_FORWARD_ROLLOUT || marioState->action == ACT_FREEFALL_LAND_STOP)
        return true;

    return false;
}

std::string Scattershot_BitfsDr::GetCsvRow()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    auto state = GetMetrics(GetCurrentFrame());

    int phaseValue;
    switch (state.phase)
    {
    case BitfsDrMetrics::Phase::INITIAL: phaseValue = 0; break;
    case BitfsDrMetrics::Phase::ATTEMPT_DR: phaseValue = 1; break;
    case BitfsDrMetrics::Phase::QUICKTURN: phaseValue = 2; break;
    case BitfsDrMetrics::Phase::RUN_DOWNHILL: phaseValue = 3; break;
    case BitfsDrMetrics::Phase::TURN_AROUND: phaseValue = 4; break;
    case BitfsDrMetrics::Phase::TURN_UPHILL: phaseValue = 5; break;
    case BitfsDrMetrics::Phase::RUN_DOWNHILL_PRE_CROSSING: phaseValue = 6; break;
    default: phaseValue = 7;
    }

    char line[256];
    snprintf(line, sizeof(line), "%f,%f,%f,%d,%f,%d,%f,%f,%f,%d,%d,%d,%f",
        marioState->pos[0],
        marioState->pos[1],
        marioState->pos[2],
        marioState->faceAngle[1],
        marioState->forwardVel,
        marioState->action,
        pyramid->oTiltingPyramidNormalX,
        pyramid->oTiltingPyramidNormalY,
        pyramid->oTiltingPyramidNormalZ,
        state.currentOscillation,
        state.currentCrossing,
        phaseValue,
        state.xzSum); // the tilt, |nX| + |nZ| of the normal above, so a run can be filtered on it

    return std::string(line);
}

bool Scattershot_BitfsDr::IsSolution()
{
    const auto& state = GetMetrics(GetCurrentFrame());
    if (!state.initialized)
        return false;

    if (_targetOscillation == 0)
    {
        // The leg hands over only what the first swing can cross from: the first crossing
        // comes about eleven frames after the leg's end and the sinking end loses about four
        // units a frame meanwhile (the census of 2026-10-04, ROADMAP 4.6).
        return state.phase > BitfsDrMetrics::Phase::INITIAL && state.legEndY - LavaHeight >= _normalSpecsDto.legMinClearance;
    }

    if (_targetOscillation < 0 || state.currentOscillation < _targetOscillation)
        return false;

    // A solution of a pass with a pass after it is a root that pass can continue from: not
    // one at the tilt ceiling, where the swing's ends sink under the lava (the maintainer,
    // 2026-10-03). The last pass's solutions go to the next stage, which judges them itself;
    // the sum climbs about 0.02 an oscillation, so a ceiling on them ends the stage a pass
    // short (measured: every run at the ceiling died at its fourth or fifth oscillation).
    if (!_finalPass && state.xzSum > _normalSpecsDto.maxXzSum)
        return false;

    // The last pass hands its solutions on: they must have the regime's tilt, which the
    // swings build up from the tilt the oscillation started at.
    return !_finalPass || state.xzSum >= _normalSpecsDto.minXzSum;
}

Scattershot_BitfsDr_Solution Scattershot_BitfsDr::GetSolutionState()
{
    const auto& state = GetMetrics(GetCurrentFrame());

    auto solution = Scattershot_BitfsDr_Solution();
    solution.fSpd = state.fSpd;
    solution.pyraNormX = state.pyraNormX;
    solution.pyraNormY = state.pyraNormY;
    solution.pyraNormZ = state.pyraNormZ;
    solution.xzSum = state.xzSum;
    solution.currentOscillation = state.currentOscillation;
    solution.roughTargetAngle = state.roughTargetAngle;
    solution.incrementFrames = state.incrementFrames;

    return solution;
}

bool Scattershot_BitfsDr::Pbd()
{
    return ModifyAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
            Camera* camera = *(Camera**)(ReadState("gCamera"));

            // Validate conditions for dive
            if (marioState->action != ACT_WALKING || marioState->forwardVel < 29.0f)
                return false;

            int16_t intendedYaw = marioState->faceAngle[1] + GetTempRng() % 32768 - 16384;
            auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
            AdvanceFrameWrite(Inputs(Buttons::B | Buttons::START, stick.first, stick.second));

            if (marioState->action != ACT_DIVE_SLIDE)
                return false;

            AdvanceFrameWrite(Inputs(0, 0, 0));
            AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
            AdvanceFrameWrite(Inputs(0, 0, 0));

            return true;
        }).executed;
}

bool Scattershot_BitfsDr::TurnUphill()
{
    return ModifyAdhoc([&]()
        {

            for (int i = 0; i < 10 && GetTempRng() % 4 < 3; i++)
            {
                if (!TurnUphill_1f())
                    return true;
            }

            return RunForwardThenTurnAround();
        }).executed;
}

bool Scattershot_BitfsDr::RunForwardThenTurnAround()
{
    return ModifyAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));

            for (int i = 0; marioState->action == ACT_FINISH_TURNING_AROUND || (i < 15 && GetTempRng() % 10 < 9); i++)
            {
                if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                    return true;

                if (!StepFrame(marioState->faceAngle[1], 32))
                    return true;
            }

            // May need an extra walking frame to avoid double turnaround glitch
            if (marioState->action == ACT_WALKING && marioState->prevAction == ACT_FINISH_TURNING_AROUND)
            {
                if (!StepFrame(marioState->faceAngle[1], 32))
                    return true;
            }

            return TurnAroundThenRunDownhill();
        }).executed;
}

bool Scattershot_BitfsDr::RunDownhillThenTurnUphill()
{
    return ModifyAdhoc([&]()
        {
            for (int i = 0; i < 15 && GetTempRng() % 20 < 19; i++)
            {
                if (!RunDownhill_1f())
                    return true;
            }

            return TurnUphill();
        }).executed;
}

bool Scattershot_BitfsDr::TurnAroundThenRunDownhill()
{
    return ModifyAdhoc([&]()
        {
            TurnAround();

            // Run downhill until past equilibrium point
            for (int i = 0; i < 30; i++)
            {
                auto state = GetMetrics(GetCurrentFrame());

                if (!RunDownhill_1f())
                    return true;

                auto nextState = GetMetrics(GetCurrentFrame());
                if (nextState.currentCrossing > state.currentCrossing)
                    break;
            }

            return true;
        }).executed;
}

bool Scattershot_BitfsDr::TurnAround()
{
    return ModifyAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
            Object* pyramid = Pyramid();

            if (marioState->action != ACT_WALKING || marioState->forwardVel <= 16.0f)
                return true;

            for (int i = 0; i < 30; i++)
            {
                auto state = GetMetrics(GetCurrentFrame());

                // Turn 2048 towrds uphill
                auto m64 = M64();
                auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                    .ImportSave(ExportSave<PyramidUpdateMem>(pyramid))
                    .Run(state.roughTargetAngle, marioState->faceAngle[1]);
                if (!status.asserted)
                    return true;

                // Attempt to run downhill with minimum angle
                int16_t intendedYaw = status.angleFacingAnalogBack;
                if (!StepFrame(intendedYaw, 32, 0, status.downhillRotation))
                    return true;

                if (marioState->action == ACT_FINISH_TURNING_AROUND)
                    break;

                if (marioState->action != ACT_TURNING_AROUND)
                    return true;
            }

            return true; // turnaround finished (break) or frame budget exhausted
        }).executed;
}

bool Scattershot_BitfsDr::RunDownhill_1f(bool min)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    bool stepped = false;
    ModifyAdhoc([&]()
        {
            if (marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                return true;

            auto state = GetMetrics(GetCurrentFrame());

            auto m64 = M64();
            auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                .ImportSave(ExportSave<PyramidUpdateMem>(pyramid))
                .Run(state.roughTargetAngle, marioState->faceAngle[1]);
            if (!status.asserted)
                return true;

            int16_t intendedYaw;
            int16_t minAngle = marioState->action == ACT_TURNING_AROUND ? status.angleFacingAnalogBack : status.angleFacing;
            if (min)
                intendedYaw = minAngle;
            else
            {
                // A deviation of up to 45 degrees from the minimum angle, drawn either toward
                // the floor's downhill, the faster run, or, once the turnaround is done, toward
                // the target and no further than it: the crossing needs the goal to move a full
                // step on the reversing axis in one frame, and at the minimum angle toward a
                // target the tilt's cone does not reach (19 degrees from the run's axis when the
                // tilt is 0.5 on one axis and 0.2 on the other) that axis's share of Mario's
                // speed is too small (ROADMAP 4.6).
                int16_t toFloor = ScriptMath::Sign(int16_t(status.floorAngle - minAngle));
                int16_t toTarget = int16_t(state.roughTargetAngle - minAngle);
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

bool Scattershot_BitfsDr::TurnUphill_1f()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();

    bool stepped = false;
    ModifyAdhoc([&]()
        {
            if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                return true;

            // Turn 2048 towrds uphill
            auto m64 = M64();
            auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                .ImportSave(ExportSave<PyramidUpdateMem>(pyramid))
                .Run(0);
            if (!status.asserted)
                return true;

            int16_t uphillAngle = status.floorAngle + 0x8000;

            //cap intended yaw diff at 2048
            int16_t intendedYaw = uphillAngle;
            if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                intendedYaw = marioState->faceAngle[1] + 2048 * ScriptMath::Sign(int16_t(uphillAngle - marioState->faceAngle[1]));

            stepped = StepFrame(intendedYaw, 32);
            return stepped;
        });

    return stepped && (marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING);
}

bool Scattershot_BitfsDr::Quickturn()
{
    return ModifyAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
            Camera* camera = *(Camera**)(ReadState("gCamera"));
            Object* pyramid = Pyramid();

            if (marioState->action != ACT_FREEFALL_LAND_STOP)
                return false;

            if (!StepInputs(Inputs(0, 0, 0)))
                return false;

            // Turn 2048 towrds uphill
            auto m64 = M64();
            auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                .ImportSave(ExportSave<PyramidUpdateMem>(pyramid))
                .Run(marioState->faceAngle[1]);
            if (!status.asserted)
                return true;

            // Get closest cardinal controller input to uphill angle
            int16_t uphillCardinalYaw = (((uint16_t)(status.floorAngle + 0x8000 - camera->yaw + 0x2000) >> 14) << 14) + camera->yaw;
            auto inputs = Inputs::GetClosestInputByYawHau(
                uphillCardinalYaw, nextafter(0.0f, 1.0f), camera->yaw); // Min mag
            return StepInputs(Inputs(0, inputs.first, inputs.second)) && StepInputs(Inputs(0, 0, 0));
        }).executed;
}

Object* Scattershot_BitfsDr::Pyramid()
{
    Object* objectPool = (Object*)(ReadState("gObjectPool"));
    return &objectPool[_platform];
}

void Scattershot_BitfsDr::PyramidGoal(const MarioState* marioState, const Object* pyramid, float& goalX, float& goalZ)
{
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

bool Scattershot_BitfsDr::Steps(const MarioState* marioState, const Object* pyramid, int signX, int signZ)
{
    float goalX, goalZ;
    PyramidGoal(marioState, pyramid, goalX, goalZ);

    // approach_by_increment(goal, src, 0.01f) moves src the whole increment only when the goal
    // is at least that far away on that side; otherwise src becomes the goal.
    auto fullStep = [](float goal, float normal, int sign)
    {
        bool up = normal <= goal && !(goal - normal < 0.01f);
        bool down = normal > goal && !(goal - normal > -0.01f);
        return sign > 0 ? up : sign < 0 ? down : up || down;
    };
    return fullStep(goalX, pyramid->oTiltingPyramidNormalX, signX) && fullStep(goalZ, pyramid->oTiltingPyramidNormalZ, signZ);
}

bool Scattershot_BitfsDr::StepInputs(Inputs inputs)
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* pyramid = Pyramid();
    return ModifyAdhoc([&]()
        {
            AdvanceFrameWrite(inputs);
            return Steps(marioState, pyramid);
        }).executed;
}

bool Scattershot_BitfsDr::StepFrame(int16_t intendedYaw, float intendedMag, uint16_t buttons, Rotation bias)
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

bool Scattershot_BitfsDr::FirstLeg_1f()
{
    // One frame of the swing's first leg from the rest: toward the chord's end on the far
    // side of the corner on the tilt's steeper axis (away from the lower edge), so that axis
    // steps away from the corner and the other toward it. From the rest both leads are zero,
    // so the first stick takes both axes a full step at once with no lead to spend, where a
    // lineup that built the tilt by running into the corner arrived at the sinking corner
    // with a reversal still to find (ROADMAP 4.6). A full step needs the goal 0.01 beyond
    // the normal on each axis, five to seven units of position near the center, and Mario
    // covers that from idle at once (a stick sets his speed to 8) only with the slope's
    // push, on the downhill side of the chord's perpendicular and a few degrees past it
    // (ROADMAP 4.6 has the model), so the leg is the chord's direction with a random
    // deviation of up to 45 degrees either way, walked back toward it until the game
    // confirms the frame leads both axes (each try a ModifyAdhoc that keeps the frame or
    // not). A stick aimed at that window from the downhill boundary was tried and is
    // worse on six seeds of seven: the leg curves with the tilt into the far end and the
    // roots lose their variety.
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();

    if (marioState->action != ACT_IDLE && marioState->action != ACT_WALKING)
        return false;

    // No leg before the rest: a pellet from the stage's own start frame has no metrics yet.
    if (!GetMetrics(GetCurrentFrame()).initialized)
        return false;
    auto rest = GetMetrics(GetMetrics(GetCurrentFrame()).initialFrame);
    const bool reverseZ = std::fabs(rest.pyraNormZ) >= std::fabs(rest.pyraNormX);
    const int legSignX = reverseZ ? _cornerSignX : -_cornerSignX;
    const int legSignZ = reverseZ ? -_cornerSignZ : _cornerSignZ;

    auto leads = [&]()
    {
        return marioState->action == ACT_WALKING
            && marioState->marioObj->platform == pyramid
            && Steps(marioState, pyramid, legSignX, legSignZ);
    };
    auto tryInputs = [&](Inputs inputs)
    {
        return ModifyAdhoc([&]()
            {
                AdvanceFrameWrite(inputs);
                return leads();
            }).executed;
    };

    int16_t chord = atan2s(float(legSignZ), float(legSignX));
    int deviation = int(GetTempRng() % 17) - 8; // steps of 1024 (5.6 degrees) to either side
    int16_t preferred = int16_t(chord + deviation * 1024);
    for (int n = 0; n < 9; n++)
    {
        auto stick = Inputs::GetClosestInputByYawHau(int16_t(chord + deviation * 1024), 32, camera->yaw);
        if (tryInputs(Inputs(0, stick.first, stick.second)))
            return true;
        if (deviation == 0)
            break;
        deviation += deviation > 0 ? -1 : 1;
    }

    // No stick leads both axes along the chord (the tilt has caught up on one): any frame
    // that keeps the pyramid stepping rather than the pellet's end.
    return marioState->action != ACT_IDLE && StepFrame(preferred, 32);
}

bool Scattershot_BitfsDr::LeadRun_1f(bool toCorner)
{
    // toCorner: the lineup's run into the corner, both leads outward, from idle too (its
    // first frame steps both axes outward straight into the corner; the fan is around the
    // corner's direction then, the face yaw once walking).
    // One frame of the first swing from the rest, steered by the leads (the goal past the
    // normal in the direction it steps, on each axis). The swing needs both leads through
    // its turnaround and its return, and from a rest the axis it steps outward starves: the
    // goal is Mario's position over his distance to the point 500 below the home, so a unit
    // of motion buys less goal the further out he is on that axis, and the downhill-angle
    // moves let it run out while the inward axis's lead runs away (ROADMAP 4.6). So the
    // frame's stick is the one of a fan around the face yaw (a quarter turn to either side
    // in steps of 4096) that leaves the thinner lead largest, among those that keep both
    // axes stepping, the second best one time in three for the pellets' variety. The fan
    // reaches sticks that turn Mario by walking, at any speed (the maintainer, 2026-09-25:
    // the swing may reverse without the turnaround action, which the move refuses since it
    // stands Mario still); each candidate is played in a block that reverts and the chosen
    // one played again for keeps.
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Camera* camera = *(Camera**)(ReadState("gCamera"));
    Object* pyramid = Pyramid();

    if (marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND && !(toCorner && marioState->action == ACT_IDLE))
        return false;
    if (!GetMetrics(GetCurrentFrame()).initialized) // a pellet from the stage's own start frame, before the rest
        return false;

    float goalX, goalZ;
    PyramidGoal(marioState, pyramid, goalX, goalZ);
    const int16_t center = marioState->action == ACT_IDLE ? atan2s(float(_cornerSignZ), float(_cornerSignX)) : marioState->faceAngle[1];
    const int dirX = toCorner ? _cornerSignX : goalX >= pyramid->oTiltingPyramidNormalX ? 1 : -1;
    const int dirZ = toCorner ? _cornerSignZ : goalZ >= pyramid->oTiltingPyramidNormalZ ? 1 : -1;

    auto play = [&](std::pair<int8_t, int8_t> stick, float& thinner)
    {
        AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
        if ((marioState->action != ACT_WALKING && marioState->action != ACT_FINISH_TURNING_AROUND)
            || marioState->marioObj->platform != pyramid || !Steps(marioState, pyramid, dirX, dirZ))
            return false;
        float leadX, leadZ;
        Leads(marioState, pyramid, dirX, dirZ, leadX, leadZ);
        // The score is the thinner lead itself. Taking speed once both leads were past 0.05
        // was tried and costs the passes their reliability (the first oscillation 33, 46 and
        // 0 in 3,000 shots on seeds 6 to 8 against 396, 1,456 and 1,919; ROADMAP 4.6).
        thinner = std::min(leadX, leadZ);
        return true;
    };

    std::pair<int8_t, int8_t> best = { 0, 0 }, second = { 0, 0 };
    float bestLead = -1.0f, secondLead = -1.0f;
    for (int k = -8; k <= 8; k += 2)
    {
        auto stick = Inputs::GetClosestInputByYawHau(int16_t(center + k * 2048), 32, camera->yaw);
        float thinner = -1.0f;
        if (!ExecuteAdhoc([&]() { return play(stick, thinner); }).executed)
            continue;
        if (thinner > bestLead)
        {
            second = best;
            secondLead = bestLead;
            best = stick;
            bestLead = thinner;
        }
        else if (thinner > secondLead)
        {
            second = stick;
            secondLead = thinner;
        }
    }
    if (bestLead < 0.0f)
        return false;
    std::pair<int8_t, int8_t> chosen = secondLead >= 0.0f && GetTempRng() % 3 == 0 ? second : best;
    float thinner = -1.0f;
    return ModifyAdhoc([&]() { return play(chosen, thinner); }).executed;
}

void Scattershot_BitfsDr::Leads(const MarioState* marioState, const Object* pyramid, int dirX, int dirZ, float& leadX, float& leadZ)
{
    // The goal past the normal on each axis, in the direction given (0: the direction the
    // normal is stepping, so the lead comes out positive).
    float goalX, goalZ;
    PyramidGoal(marioState, pyramid, goalX, goalZ);
    const float dx = goalX - pyramid->oTiltingPyramidNormalX;
    const float dz = goalZ - pyramid->oTiltingPyramidNormalZ;
    leadX = dirX != 0 ? dx * float(dirX) : std::fabs(dx);
    leadZ = dirZ != 0 ? dz * float(dirZ) : std::fabs(dz);
}
