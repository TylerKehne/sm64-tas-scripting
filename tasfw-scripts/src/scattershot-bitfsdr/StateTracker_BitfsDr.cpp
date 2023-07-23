#include <Scattershot_BitfsDr.hpp>

bool StateTracker_BitfsDr::ValidateCrossingData(const StateTracker_BitfsDr::CustomScriptStatus& state, float componentThreshold)
{
    int crossings = state.crossingData.size();
    if (crossings > 2)
    {
        auto lastCrossing0 = state.crossingData.rbegin();
        auto lastCrossing2 = ++(++state.crossingData.rbegin());
        if (lastCrossing0->speed <= lastCrossing2->speed || lastCrossing0->maxDownhillSpeed <= lastCrossing2->maxSpeed)
            return false;

        if (std::fabs(lastCrossing0->nX) < componentThreshold && std::fabs(lastCrossing0->nZ) < componentThreshold)
            return false;
    }

    return true;
}

bool StateTracker_BitfsDr::validation() { return GetCurrentFrame() >= 3515; }

bool StateTracker_BitfsDr::execution()
{
    MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
    const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
    Object* objectPool = (Object*)(resource->addr("gObjectPool"));
    Object* pyramid = &objectPool[84];

    SetStateVariables(marioState, pyramid);

    // Calculate recursive metrics
    int64_t currentFrame = GetCurrentFrame();
    CustomScriptStatus lastFrameState;
    if (currentFrame > 3515)
        lastFrameState = GetTrackedState<StateTracker_BitfsDr>(currentFrame - 1);

    if (!lastFrameState.initialized)
        return true;

    CustomStatus.reachedNormRegime = lastFrameState.reachedNormRegime;
    if (CustomStatus.xzSum > normRegimeThreshold)
        CustomStatus.reachedNormRegime |= true;

    CustomStatus.xzSumStartedIncreasing = lastFrameState.xzSumStartedIncreasing;
    if (CustomStatus.xzSum > lastFrameState.xzSum + 0.001f)
        CustomStatus.xzSumStartedIncreasing |= true;

    CustomStatus.roughTargetAngle = lastFrameState.roughTargetAngle;
    CustomStatus.phase = lastFrameState.phase;
    CalculateOscillations(lastFrameState, marioState, pyramid);

    CalculatePhase(lastFrameState, marioState, pyramid);

    return true;
}

bool StateTracker_BitfsDr::assertion() { return CustomStatus.initialized == true; }

void StateTracker_BitfsDr::SetStateVariables(MarioState* marioState, Object* pyramid)
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
    CustomStatus.initialized = true;
}

void StateTracker_BitfsDr::CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid)
{
    if (CustomStatus.phase == Phase::INITIAL)
        return;

    int targetXDirection = sign(gSineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);
    int targetZDirection = sign(gCosineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);

    int tiltDirectionX, tiltDirectionZ, targetTiltDirectionX, targetTiltDirectionZ;
    float normalDiffX, normalDiffZ;

    tiltDirectionX = sign(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
    targetTiltDirectionX = targetXDirection;
    normalDiffX = fabs(CustomStatus.pyraNormX - lastFrameState.pyraNormX);

    tiltDirectionZ = Script::sign(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
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
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        ExecuteAdhoc([&]()
            {
                //No point in doing all this if it won't validate anyway
                if (CustomStatus.currentCrossing > 1 && CustomStatus.crossingData[CustomStatus.currentCrossing - 1].speed >= marioState->forwardVel)
                    return false;

                for (int i = 0; i < 50; i++)
                {
                    maxDownhillSpeed = marioState->forwardVel;

                    auto m64 = M64();
                    auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                        .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                        .Run(marioState->faceAngle[1]);
                    if (!status.asserted)
                        return true;

                    // Attempt to run downhill with minimum angle
                    int16_t intendedYaw = status.angleFacing;
                    auto stick = Inputs::GetClosestInputByYawExact(
                        intendedYaw, 32, camera->yaw, status.downhillRotation);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                    if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING || marioState->forwardVel <= maxDownhillSpeed)
                        return true;
                }

                return true;
            });

        CustomStatus.crossingData.emplace_back(
            GetCurrentFrame(), CustomStatus.fSpd, CustomStatus.xzSum, CustomStatus.pyraNormX, CustomStatus.pyraNormZ, marioState->forwardVel, maxDownhillSpeed);
        CustomStatus.currentCrossing++;

        if (!lastFrameState.crossingData.empty())
        {
            // Get number of frames since last crossing
            int64_t lastCrossing = lastFrameState.crossingData.rbegin()->frame;

            if (GetCurrentFrame() - lastCrossing >= minOscillationFrames)
                CustomStatus.currentOscillation++;
        }
    }
    else if (!CustomStatus.crossingData.empty() && marioState->forwardVel > CustomStatus.crossingData.rbegin()->maxSpeed)
        CustomStatus.crossingData.rbegin()->maxSpeed = marioState->forwardVel;
}

void StateTracker_BitfsDr::CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid)
{
    int32_t targetAngleDiffA = abs(int16_t(roughTargetAngleA - marioState->faceAngle[1]));
    int32_t targetAngleDiffB = abs(int16_t(roughTargetAngleB - marioState->faceAngle[1]));

    switch (lastFrameState.phase)
    {
    case Phase::INITIAL:
        if (lastFrameState.initialized && CustomStatus.reachedNormRegime)
        {
            CustomStatus.crossingData.emplace_back(
                GetCurrentFrame(), 0, CustomStatus.xzSum, CustomStatus.pyraNormX, marioState->forwardVel, 0.f);
            CustomStatus.currentCrossing++;

            // choose further target angle
            CustomStatus.roughTargetAngle =
                targetAngleDiffA <= targetAngleDiffB ? roughTargetAngleB : roughTargetAngleA;
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
