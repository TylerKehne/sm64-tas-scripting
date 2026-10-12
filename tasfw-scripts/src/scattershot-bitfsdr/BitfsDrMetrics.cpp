#include <Scattershot_BitfsDr.hpp>
#include <ScriptMath.hpp>
#include <sm64/Trig.hpp>
#include <algorithm>
#include <cmath>

float BitfsDrMetrics::TurnaroundClearance(float normalX, float normalY, float normalZ,
    float marioX, float marioZ, int16_t faceYaw, float speed, float pyramidX, float pyramidY, float pyramidZ)
{
    // The turnaround sheds about 4.8 of speed a frame (the brake example's 26.6, 21.5, 16.8,
    // 12.0, 7.2; 2026-10-06), so Mario stops this many frames on, half his speed times them
    // further along his facing.
    const int frames = int(std::ceil(std::max(speed, 0.0f) / 4.8f));
    const float distance = speed * float(frames) / 2.0f;
    const float x = marioX - pyramidX + distance * gSineTable[uint16_t(faceYaw) >> 4];
    const float z = marioZ - pyramidZ + distance * gCosineTable[uint16_t(faceYaw) >> 4];

    // The normal those frames on, stepping toward where he stops as the pyramid steps it
    // toward him (bhv_tilting_inverted_pyramid_loop's goal: his offset, 500 up, normalized).
    const float d = std::sqrt(x * x + 500.0f * 500.0f + z * z);
    float nx = normalX, ny = normalY, nz = normalZ;
    for (int i = 0; i < frames; i++)
    {
        nx = StepToward(x / d, nx, 0.01f);
        ny = StepToward(500.0f / d, ny, 0.01f);
        nz = StepToward(z / d, nz, 0.01f);
    }

    // The face's height where he stops, under the normal made unit.
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float height = pyramidY + (307.0f - (nx / length) * x - (nz / length) * z) / (ny / length);
    return height - Scattershot_BitfsDr::LavaHeight;
}

// The pyramid's own step (approach_by_increment in the tilting pyramid's behavior): toward
// the goal by the whole increment while the goal is at least that far, else onto it.
float BitfsDrMetrics::StepToward(float goal, float value, float increment)
{
    if (goal > value)
        return goal - value >= increment ? value + increment : goal;
    return value - goal >= increment ? value - increment : goal;
}

bool BitfsDrMetrics::ValidateCrossingData(const BitfsDrMetrics::CustomScriptStatus& state, float componentThreshold, float minCrossingAxisDiff)
{
    int crossings = int(state.crossingData.size());
    if (crossings > 2)
    {
        auto lastCrossing0 = state.crossingData.rbegin();
        auto lastCrossing2 = ++(++state.crossingData.rbegin());
        // Gain speed each crossing (the maintainer's rule): this crossing faster than the one
        // two before it, and its downhill run faster than that one's peak, whatever the tilt
        // did between, below the regime and in it alike (the maintainer, 2026-10-10); only a
        // crossing in the regime is never compared with one before it, so the regime's first
        // oscillation is free of the rule (ROADMAP 4.6, 4.12).
        const bool comparable = lastCrossing0->inRegime == lastCrossing2->inRegime;
        if (comparable
            && (lastCrossing0->speed <= lastCrossing2->speed || lastCrossing0->maxDownhillSpeed <= lastCrossing2->maxSpeed))
            return false;

        // The crossing's normal at least minCrossingAxisDiff further along one axis than the
        // other, from the second crossing on (the maintainer, 2026-10-10; ROADMAP 4.12): the
        // normal lags Mario by that much at the example movies' crossings (0.27 to 0.31), where
        // ours, caught up at the turn, had 0.13 to 0.19 after the +z end's.
        if (std::fabs(std::fabs(lastCrossing0->nX) - std::fabs(lastCrossing0->nZ)) < minCrossingAxisDiff)
            return false;

        if (std::fabs(lastCrossing0->nX) < componentThreshold && std::fabs(lastCrossing0->nZ) < componentThreshold)
            return false;
    }

    return true;
}

const char* BitfsDrMetrics::PhaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::INITIAL: return "INITIAL";
    case Phase::RUN_DOWNHILL: return "RUN_DOWNHILL";
    case Phase::TURN_UPHILL: return "TURN_UPHILL";
    case Phase::TURN_AROUND: return "TURN_AROUND";
    case Phase::ATTEMPT_DR: return "ATTEMPT_DR";
    case Phase::QUICKTURN: return "QUICKTURN";
    case Phase::RUN_DOWNHILL_PRE_CROSSING: return "RUN_DOWNHILL_PRE_CROSSING";
    }
    return "?";
}

bool BitfsDrMetrics::validation() { return int64_t(GetCurrentFrame()) >= initialFrame; }

bool BitfsDrMetrics::execution()
{
    MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
    Object* objectPool = (Object*)(ReadState("gObjectPool"));
    Object* pyramid = &objectPool[platform];

    CustomStatus.initialFrame = initialFrame;
    SetStateVariables(marioState, pyramid);

    CalculateARE(pyramid);

    // Calculate recursive metrics
    int64_t currentFrame = GetCurrentFrame();
    CustomScriptStatus lastFrameState;
    if (currentFrame > initialFrame)
        lastFrameState = GetMetrics(currentFrame - 1);

    if (!lastFrameState.initialized)
        return true;

    CustomStatus.reachedNormRegime = lastFrameState.reachedNormRegime;
    if (CustomStatus.xzSum > normalSpecsDto.startXzSum)
        CustomStatus.reachedNormRegime |= true;

    CustomStatus.xzSumStartedIncreasing = lastFrameState.xzSumStartedIncreasing;
    if (CustomStatus.xzSum > lastFrameState.xzSum + 0.001f)
        CustomStatus.xzSumStartedIncreasing |= true;
    CustomStatus.maxXzSum = CustomStatus.xzSum > lastFrameState.maxXzSum ? CustomStatus.xzSum : lastFrameState.maxXzSum;

    CustomStatus.roughTargetAngle = lastFrameState.roughTargetAngle;
    CustomStatus.legEndX = lastFrameState.legEndX;
    CustomStatus.legEndZ = lastFrameState.legEndZ;
    CustomStatus.legEndY = lastFrameState.legEndY;
    CustomStatus.phase = lastFrameState.phase;
    CalculateOscillations(lastFrameState, marioState);

    CalculatePhase(lastFrameState, marioState, pyramid);

    return true;
}

bool BitfsDrMetrics::assertion() { return CustomStatus.initialized == true; }

void BitfsDrMetrics::SetStateVariables(MarioState* marioState, Object* pyramid)
{
    CustomStatus.marioX = marioState->pos[0];
    CustomStatus.marioY = marioState->pos[1];
    CustomStatus.marioZ = marioState->pos[2];
    CustomStatus.fSpd = marioState->forwardVel;
    CustomStatus.pyraNormX = pyramid->oTiltingPyramidNormalX;
    CustomStatus.pyraNormY = pyramid->oTiltingPyramidNormalY;
    CustomStatus.pyraNormZ = pyramid->oTiltingPyramidNormalZ;
    CustomStatus.xzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);
    CustomStatus.marioAction = marioState->action;
    int16_t floorDYaw = int16_t(marioState->floorAngle - marioState->faceAngle[1]);
    CustomStatus.ranUphill = marioState->marioObj->platform == pyramid && !(floorDYaw > -0x4000 && floorDYaw < 0x4000);
    CustomStatus.initialized = true;
    CustomStatus.frame = GetCurrentFrame();
}

void BitfsDrMetrics::CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState)
{
    if (CustomStatus.phase == Phase::INITIAL)
        return;

    int targetXDirection = ScriptMath::Sign(gSineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);
    int targetZDirection = ScriptMath::Sign(gCosineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);

    int tiltDirectionX, tiltDirectionZ, targetTiltDirectionX, targetTiltDirectionZ;
    float normalDiffX, normalDiffZ;

    tiltDirectionX = ScriptMath::Sign(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
    targetTiltDirectionX = targetXDirection;
    normalDiffX = fabs(CustomStatus.pyraNormX - lastFrameState.pyraNormX);

    tiltDirectionZ = ScriptMath::Sign(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
    targetTiltDirectionZ = targetZDirection;
    normalDiffZ = fabs(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);

    CustomStatus.crossingData = lastFrameState.crossingData;
    CustomStatus.currentOscillation = lastFrameState.currentOscillation;
    CustomStatus.currentCrossing = lastFrameState.currentCrossing;
    if (tiltDirectionX == targetTiltDirectionX && normalDiffX >= 0.0099999f
        && tiltDirectionZ == targetTiltDirectionZ && normalDiffZ >= 0.0099999f)
    {
        // toggle target angle unless we are already facing it initially
        if (CustomStatus.phase != Phase::RUN_DOWNHILL || CustomStatus.currentCrossing > 1)
        {
            if (CustomStatus.roughTargetAngle == roughTargetAngleA)
                CustomStatus.roughTargetAngle = roughTargetAngleB;
            else
                CustomStatus.roughTargetAngle = roughTargetAngleA;
        }

        // Calulate max downhill speed (slow but new crossings are relatively rare)
        float maxDownhillSpeed = 0;
        Camera* camera = *(Camera**)(ReadState("gCamera"));
        ExecuteAdhoc([&]()
            {
                // No point in doing all this if it cannot validate anyway: the speed rule compares
                // the crossing with the one two before it, the same direction's last (until
                // 2026-10-06 this compared with the one before, the opposite direction's, so a
                // crossing slower than that one had no peak and failed the rule's second
                // condition for that alone whenever the rule applied; ROADMAP 4.6).
                if (CustomStatus.currentCrossing > 1 && CustomStatus.crossingData[CustomStatus.currentCrossing - 2].speed >= marioState->forwardVel)
                    return false;

                for (int i = 0; i < 50; i++)
                {
                    maxDownhillSpeed = marioState->forwardVel;

                    auto status = Test<GetMinimumDownhillWalkingAngle>(marioState->faceAngle[1]);
                    if (!status.asserted)
                        return true;

                    // Attempt to run downhill with minimum angle
                    int16_t intendedYaw = status.angleFacing;
                    auto stick = Inputs::GetClosestInputByYawExact(
                        intendedYaw, 32, camera->yaw, status.downhillRotation);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                    if ((marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING) || marioState->forwardVel <= maxDownhillSpeed)
                        return true;
                }

                return true;
            });

        CustomStatus.crossingData.emplace_back(
            GetCurrentFrame(), CustomStatus.fSpd, CustomStatus.xzSum, CustomStatus.pyraNormX, CustomStatus.pyraNormZ, marioState->forwardVel, maxDownhillSpeed, CustomStatus.reachedNormRegime);
        CustomStatus.currentCrossing++;

        if (!lastFrameState.crossingData.empty())
        {
            // Get number of frames since last crossing
            int64_t lastCrossing = lastFrameState.crossingData.rbegin()->frame;

            if (int64_t(GetCurrentFrame() - lastCrossing) >= minOscillationFrames)
                CustomStatus.currentOscillation++;
        }
    }
    else if (!CustomStatus.crossingData.empty() && marioState->forwardVel > CustomStatus.crossingData.rbegin()->maxSpeed)
        CustomStatus.crossingData.rbegin()->maxSpeed = marioState->forwardVel;
}

void BitfsDrMetrics::CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* /*pyramid*/)
{
    int32_t targetAngleDiffA = abs(int16_t(roughTargetAngleA - marioState->faceAngle[1]));
    int32_t targetAngleDiffB = abs(int16_t(roughTargetAngleB - marioState->faceAngle[1]));

    switch (lastFrameState.phase)
    {
    case Phase::INITIAL:
        // The first leg from the rest ends when Mario is fast enough to turn around (the
        // turnaround moves' 16), the tilt in the corner's quadrant. With a handover asked
        // (handoverXzSum above the rest's tilt) the initial phase is the old lineup instead:
        // it runs the tilt up to it first and hands over there, with speed and leads
        // (the maintainer, 2026-09-25); otherwise the leg keeps the rest's tilt and the
        // swings' ends build it (ROADMAP 4.6).
        if (lastFrameState.initialized && marioState->forwardVel >= normalSpecsDto.legExitSpeed
            && (!Handover() || CustomStatus.xzSum >= normalSpecsDto.handoverXzSum)
            && CustomStatus.pyraNormX * float(cornerSignX) > 0 && CustomStatus.pyraNormZ * float(cornerSignZ) > 0
            && std::abs(CustomStatus.incrementFrames[0]) % 2 == std::abs(CustomStatus.incrementFrames[2]) % 2)
        {
            CustomStatus.crossingData.emplace_back(
                GetCurrentFrame(), 0.f, CustomStatus.xzSum, CustomStatus.pyraNormX, CustomStatus.pyraNormZ, marioState->forwardVel, 0.f, CustomStatus.reachedNormRegime);
            CustomStatus.currentCrossing++;

            // choose further target angle
            CustomStatus.roughTargetAngle =
                targetAngleDiffA <= targetAngleDiffB ? roughTargetAngleB : roughTargetAngleA;
            CustomStatus.legEndX = marioState->pos[0];
            CustomStatus.legEndZ = marioState->pos[2];
            CustomStatus.legEndY = marioState->pos[1];
            CustomStatus.phase = Phase::RUN_DOWNHILL;
        }
        break;

    case Phase::RUN_DOWNHILL:
        if (lastFrameState.fSpd > CustomStatus.fSpd)
            CustomStatus.phase = Phase::TURN_UPHILL;
        break;

    case Phase::TURN_UPHILL:
        if (marioState->action == ACT_TURNING_AROUND)
            CustomStatus.phase = Phase::TURN_AROUND;
        else if (marioState->action == ACT_DIVE || marioState->action == ACT_DIVE_SLIDE)
            CustomStatus.phase = Phase::ATTEMPT_DR;
        break;

    case Phase::TURN_AROUND:
        if (marioState->action == ACT_TURNING_AROUND)
            CustomStatus.phase = Phase::RUN_DOWNHILL_PRE_CROSSING;
        else if (marioState->action == ACT_FINISH_TURNING_AROUND)
            CustomStatus.phase = Phase::RUN_DOWNHILL;
        break;

    case Phase::RUN_DOWNHILL_PRE_CROSSING:
        if (CustomStatus.currentCrossing > lastFrameState.currentCrossing)
            CustomStatus.phase = Phase::RUN_DOWNHILL;
        break;

    case Phase::ATTEMPT_DR:
        if (marioState->action == ACT_FREEFALL_LAND_STOP)
            CustomStatus.phase = Phase::QUICKTURN;
        break;

    case Phase::QUICKTURN:
        if (marioState->action == ACT_IDLE)
            CustomStatus.phase = Phase::RUN_DOWNHILL;
        break;
    }

    if (targetAngleDiffA <= targetAngleDiffB)
        CustomStatus.facingRoughTargetAngle = CustomStatus.roughTargetAngle == roughTargetAngleA;
    else
        CustomStatus.facingRoughTargetAngle = CustomStatus.roughTargetAngle == roughTargetAngleB;
}

void BitfsDrMetrics::CalculateARE(Object* pyramid)
{
    float errorIncX = std::fabs(std::nextafter(targetNormal[0], INFINITY) - targetNormal[0]);
    float errorIncZ = std::fabs(std::nextafter(targetNormal[2], INFINITY) - targetNormal[2]);

    float errorX = (targetNormal[0] - pyramid->oTiltingPyramidNormalX) / errorIncX;
    float errorZ = (targetNormal[2] - pyramid->oTiltingPyramidNormalZ) / errorIncZ;

    float normalX = pyramid->oTiltingPyramidNormalX;
    for (int i = 0; i < 200; i++)
    {
        if (std::fabs(targetNormal[0] - normalX) <= 0.005f)
        {
            CustomStatus.adjustedRemainderError[0] = (targetNormal[0] - normalX) / errorIncX;
            CustomStatus.incrementFrames[0] = i * ScriptMath::Sign(errorX);
            break;
        }

        normalX += ScriptMath::Sign(errorX) * 0.01f;
    }

    float normalZ = pyramid->oTiltingPyramidNormalZ;
    for (int i = 0; i < 200; i++)
    {
        if (std::fabs(targetNormal[2] - normalZ) <= 0.005f)
        {
            CustomStatus.adjustedRemainderError[2] = (targetNormal[2] - normalZ) / errorIncZ;
            CustomStatus.incrementFrames[2] = i * ScriptMath::Sign(errorZ);
            break;
        }

        normalZ += ScriptMath::Sign(errorZ) * 0.01f;
    }
}
