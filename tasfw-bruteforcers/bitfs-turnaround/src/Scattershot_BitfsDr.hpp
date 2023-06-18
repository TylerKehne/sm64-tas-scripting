#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class StateTracker_BitfsDr : public Script<LibSm64>
{
public:
    // TODO: enable configuration so hardcoding isn't necessary
    const int16_t roughTargetAngleA = -24576;
    const int16_t roughTargetAngleB = 8192;
    const int minOscillationFrames = 15;
    const float normRegimeThreshold = 0.69f;

    enum class Phase
    {
        INITIAL,
        RUN_DOWNHILL,
        TURN_UPHILL,
        TURN_AROUND,
        ATTEMPT_DR,
        QUICKTURN,
        RUN_DOWNHILL_PRE_CROSSING
    };

    class CrossingDto
    {
    public:
        uint64_t frame = 0;
        float speed = 0;
        float xzSum = 0;
        float nX = 0;
        float maxSpeed = 0;
        float maxDownhillSpeed = 0;
    };

    class CustomScriptStatus
    {
    public:
        bool initialized = false;
        float marioX = 0;
        float marioY = 0;
        float marioZ = 0;
        uint64_t marioAction = 0;
        float fSpd = 0;
        float pyraNormX = 0;
        float pyraNormY = 0;
        float pyraNormZ = 0;
        float xzSum = 0;
        std::vector<CrossingDto> crossingData;
        int currentOscillation = 0;
        int currentCrossing = 0;
        bool reachedNormRegime = false;
        bool xzSumStartedIncreasing = false;
        int16_t roughTargetAngle = 8192; 
        Phase phase = Phase::INITIAL;
        bool facingRoughTargetAngle = false;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    static bool ValidateCrossingData(const StateTracker_BitfsDr::CustomScriptStatus& state)
    {
        int crossings = state.crossingData.size();
        if (crossings > 2)
        {
            auto lastCrossing0 = state.crossingData.rbegin();
            auto lastCrossing2 = ++(++state.crossingData.rbegin());
            if (lastCrossing0->speed <= lastCrossing2->speed || lastCrossing0->maxDownhillSpeed <= lastCrossing2->maxSpeed)
                return false;
        }

        return true;
    }

    StateTracker_BitfsDr() = default;

    bool validation() { return GetCurrentFrame() >= 3515; }

    bool execution() 
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        SetStateVariables(marioState, pyramid);

        // Calculate recursive metrics
        int64_t currentFrame = GetCurrentFrame();
        auto lastFrameState = GetTrackedState<StateTracker_BitfsDr>(currentFrame - 1);
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

    bool assertion() { return CustomStatus.initialized == true; }

private:
    void SetStateVariables(MarioState* marioState, Object* pyramid)
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

    void CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid)
    {
        if (CustomStatus.phase == Phase::INITIAL)
            return;

        int targetXDirection = sign(gSineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);
        int targetZDirection = sign(gCosineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);

        int tiltDirectionX, tiltDirectionZ, targetTiltDirectionX, targetTiltDirectionZ;
        float normalDiffX, normalDiffZ;
        //if (fabs(pyramid->oTiltingPyramidNormalX) < fabs(pyramid->oTiltingPyramidNormalZ))
        //{
            tiltDirectionX = sign(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
            targetTiltDirectionX = targetXDirection;
            normalDiffX = fabs(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
        //}
        //else
        //{
            tiltDirectionZ = Script::sign(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
            targetTiltDirectionZ = targetZDirection;
            normalDiffZ = fabs(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
        //}

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
                        auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                        auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                            ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, marioState->faceAngle[1]);
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
                GetCurrentFrame(), CustomStatus.fSpd, CustomStatus.xzSum, CustomStatus.pyraNormX, marioState->forwardVel, maxDownhillSpeed);
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

    void CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid)
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
                if (lastFrameState.fSpd < CustomStatus.fSpd)
                {
                    //CustomStatus.roughTargetAngle =
                    //    CustomStatus.roughTargetAngle == roughTargetAngleA ? roughTargetAngleB : roughTargetAngleA;
                    CustomStatus.phase = Phase::TURN_UPHILL;
                }
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
                {
                    CustomStatus.phase = Phase::RUN_DOWNHILL;
                    //CustomStatus.roughTargetAngle =
                    //    CustomStatus.roughTargetAngle == roughTargetAngleA ? roughTargetAngleB : roughTargetAngleA;
                }
                break;
        }

        if (targetAngleDiffA <= targetAngleDiffB)
            CustomStatus.facingRoughTargetAngle = CustomStatus.roughTargetAngle == roughTargetAngleA;
        else
            CustomStatus.facingRoughTargetAngle = CustomStatus.roughTargetAngle == roughTargetAngleB;
    }
};

using Alias_ScattershotThread_BitfsDr = ScattershotThread<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDr>;
using Alias_Scattershot_BitfsDr = Scattershot<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDr>;

class Scattershot_BitfsDr : public Alias_ScattershotThread_BitfsDr
{
public:
    Scattershot_BitfsDr(Alias_Scattershot_BitfsDr& scattershot, int id)
        : Alias_ScattershotThread_BitfsDr(scattershot, id) {}

    void SelectMovementOptions()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        AddRandomMovementOption(
            {
                {MovementOption::MAX_MAGNITUDE, 4},
                {MovementOption::ZERO_MAGNITUDE, 0},
                {MovementOption::SAME_MAGNITUDE, 0},
                {MovementOption::RANDOM_MAGNITUDE, 1}
            });

        AddRandomMovementOption(
            {
                {MovementOption::MATCH_FACING_YAW, 1},
                {MovementOption::ANTI_FACING_YAW, 2},
                {MovementOption::SAME_YAW, 4},
                {MovementOption::RANDOM_YAW, 16}
            });

        AddRandomMovementOption(
            {
                {MovementOption::SAME_BUTTONS, 0},
                {MovementOption::NO_BUTTONS, 0},
                {MovementOption::RANDOM_BUTTONS, 10}
            });

        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
        switch (state.phase)
        {
            case StateTracker_BitfsDr::Phase::INITIAL:
                AddMovementOption(MovementOption::NO_SCRIPT);
                break;

            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL:
                AddRandomMovementOption(
                    {
                        {MovementOption::NO_SCRIPT, 0},
                        {MovementOption::RUN_DOWNHILL_MIN, 20},
                        {MovementOption::TURN_UPHILL, marioState->forwardVel <= 16.0f ? 0 : 1}
                    });
                break;

            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING:
                AddRandomMovementOption(
                    {
                        {MovementOption::RUN_DOWNHILL_MIN, 1},
                        {MovementOption::RUN_DOWNHILL, 1}
                    });
                break;

            case StateTracker_BitfsDr::Phase::TURN_UPHILL:
            {
                auto prevState = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame() - 1);
                bool avoidDoubleTurnaround = prevState.marioAction == ACT_FINISH_TURNING_AROUND && state.marioAction == ACT_WALKING;

                AddRandomMovementOption(
                    {
                        {MovementOption::NO_SCRIPT, 0},
                        {MovementOption::TURN_UPHILL, 10},
                        {MovementOption::RUN_FORWARD, 0},
                        {MovementOption::TURN_AROUND, state.marioAction != ACT_WALKING || avoidDoubleTurnaround ? 0 : 1},
                        {MovementOption::PBD, 0}
                    });
                break;
            }

            case StateTracker_BitfsDr::Phase::TURN_AROUND:
                AddMovementOption(MovementOption::TURN_AROUND);
                break;

            case StateTracker_BitfsDr::Phase::ATTEMPT_DR:
                AddMovementOption(MovementOption::NO_SCRIPT);
                break;

            case StateTracker_BitfsDr::Phase::QUICKTURN:
                AddMovementOption(MovementOption::QUICKTURN);
                break;

            default:
                AddMovementOption(MovementOption::NO_SCRIPT);
        }
    }

    bool ApplyMovement()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

                // Scripts
                if (!CheckMovementOptions(MovementOption::NO_SCRIPT))
                {
                    if (CheckMovementOptions(MovementOption::REWIND))
                    {
                        int64_t currentFrame = GetCurrentFrame();
                        int maxRewind = (currentFrame - config.StartFrame) / 2;
                        int rewindFrames = (GetTempRng() % 100) * maxRewind / 100;
                        Load(currentFrame - rewindFrames);
                    }

                    if (CheckMovementOptions(MovementOption::RUN_DOWNHILL_MIN))
                    {
                        RunDownhill_1f();
                        return true;
                    }
                    else if (CheckMovementOptions(MovementOption::RUN_DOWNHILL))
                    {
                        RunDownhill_1f(false);
                        return true;
                    }
                    else if (CheckMovementOptions(MovementOption::PBD) && Pbd())
                        return true;
                    else if (CheckMovementOptions(MovementOption::TURN_UPHILL))
                    {
                        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                        if (state.phase == StateTracker_BitfsDr::Phase::TURN_UPHILL && (GetTempRng() % 4) == 0)
                        {
                            int64_t intendedYaw = marioState->faceAngle[1] + ((GetTempRng() % 2048) - 1024);
                            auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                            AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
                        }
                        else
                            TurnUphill_1f();

                        return true;
                    }
                    else if (CheckMovementOptions(MovementOption::RUN_FORWARD) && RunForwardThenTurnAround())
                        return true;
                    else if (CheckMovementOptions(MovementOption::TURN_AROUND))
                    {
                        TurnAround();
                        if (marioState->action == ACT_FINISH_TURNING_AROUND)
                            Rollback(GetCurrentFrame() - 1);
                        return true;
                    }
                    else if (CheckMovementOptions(MovementOption::QUICKTURN) && Quickturn())
                        return true;
                }

                // Random input
                AdvanceFrameWrite(RandomInputs(
                    {
                        {Buttons::A, 0},
                        {Buttons::B, marioState->action == ACT_DIVE_SLIDE ? 1 : 0},
                        {Buttons::Z, 0},
                        {Buttons::C_UP, 0}
                    }));

                return true;
            }).executed;
    }

    BinaryStateBin<16> GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto trackedState = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
        float norm_regime_min = 0.69f;
        int nRegions = trackedState.xzSum >= norm_regime_min ? 32 : 4;
        if (trackedState.xzSum >= norm_regime_min && trackedState.currentOscillation > 0)
            nRegions *= trackedState.currentOscillation;

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
            default: actionValue = 10;
        }

        int phaseValue;
        switch (trackedState.phase)
        {
            case StateTracker_BitfsDr::Phase::INITIAL: phaseValue = 0; break;
            case StateTracker_BitfsDr::Phase::ATTEMPT_DR: phaseValue = 1; break;
            case StateTracker_BitfsDr::Phase::QUICKTURN: phaseValue = 2; break;
            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL: phaseValue = 3; break;
            case StateTracker_BitfsDr::Phase::TURN_AROUND: phaseValue = 4; break;
            case StateTracker_BitfsDr::Phase::TURN_UPHILL: phaseValue = 5; break;
            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING: phaseValue = 6; break;
            default: phaseValue = 7;
        }
        
        float xMin = -2430.0f;
        float xMax = -1450.0f;
        float yMin = -3071.0f;
        float yMax = -2760.0f;
        float zMin = -1190.0f;
        float zMax = -200.0f;

        float xPosValue = std::clamp(marioState->pos[0], xMin, xMax);
        float yPosValue = std::clamp(marioState->pos[1], yMin, yMax);
        float zPosValue = std::clamp(marioState->pos[2], zMin, zMax);
        float ySpeedValue = std::clamp(marioState->vel[1], 0.f, 32.0f);
        float fSpeedValue = std::clamp(marioState->forwardVel, 0.f, 64.0f);
        
        uint8_t bitCursor = 0;
        BinaryStateBin<16> state;

        if (trackedState.initialized && trackedState.reachedNormRegime)
        {
            state.AddValueBits(bitCursor, 1, 1);
            state.AddValueBits(bitCursor, 4, actionValue);
            state.AddValueBits(bitCursor, 3, phaseValue);
            state.AddValueBits(bitCursor, 4, std::clamp(trackedState.currentCrossing, 0, 15));
            state.AddRegionBitsByRegionSize(bitCursor, 8, xPosValue, xMin, xMax, 20.0f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, zPosValue, zMin, zMax, 20.0f);
            state.AddRegionBitsByNRegions(bitCursor, 7, int(marioState->faceAngle[1]), -32768, 32767, 32);

            if (trackedState.currentCrossing > 0)
            {
                state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.crossingData.rbegin()->xzSum, 0.f, 0.8f, 0.005f);
                state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.crossingData.rbegin()->nX, -0.7f, 0.7f, 0.01f);

                int framesSinceCrossing = std::clamp(int(GetCurrentFrame() - trackedState.crossingData.rbegin()->frame), 0, 63);
                if (trackedState.phase == StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING)
                    state.AddValueBits(bitCursor, 6, framesSinceCrossing);
            }
        }
        else
        {
            state.AddValueBits(bitCursor, 1, 0);
            state.AddValueBits(bitCursor, 4, actionValue);
            state.AddValueBits(bitCursor, 3, phaseValue);
            state.AddRegionBitsByRegionSize(bitCursor, 8, xPosValue, xMin, xMax, 100.0f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, zPosValue, zMin, zMax, 100.0f);
        }

        return state;
    }

    bool ValidateState()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        // Position sanity check
        if (marioState->pos[0] < -2430 || marioState->pos[0] > -1450)
            return false;

        if (marioState->pos[2] < -1190 || marioState->pos[2] > -200)
            return false;

        if (marioState->pos[1] > -2760)
            return false;

        // Quadrant check
        if (pyramid->oTiltingPyramidNormalZ < -.15 || pyramid->oTiltingPyramidNormalX > 0.15)
            return false; 

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

        if (marioState->floorHeight > -3071 && marioState->pos[1] > marioState->floorHeight + 4 && marioState->vel[1] != 22.0)
            return false; //above pyra by over 4 units

        if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL)
            return false; //diving/dring above lava

        // Double turnaround (ideally should prevent this in movement choices)
        if (marioState->action == ACT_WALKING && marioState->forwardVel < 0)
            return false;

        // Check custom metrics
        float xNorm = pyramid->oTiltingPyramidNormalX;
        float zNorm = pyramid->oTiltingPyramidNormalZ;
        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
        auto lastFrameState = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame() - 1);

        //Herd to correct quadrant initially
        if (!state.reachedNormRegime && marioState->pos[0] >= -2000.0f)
            return false;

        // Reject departures from norm regime
        float normRegimeThreshold = 0.69f;
        if (state.reachedNormRegime && fabs(xNorm) + fabs(zNorm) < normRegimeThreshold - 0.02f)
            return false;

        // Reject untimely turnarounds
        if (marioState->action == ACT_TURNING_AROUND
            && state.phase != StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING
            && state.phase != StateTracker_BitfsDr::Phase::TURN_AROUND
            && state.phase != StateTracker_BitfsDr::Phase::INITIAL)
            return false;

        //If outside of norm regime, force norm to increase
        if (!state.reachedNormRegime && state.xzSumStartedIncreasing && state.xzSum < lastFrameState.xzSum)
            return false;

        if (state.phase == StateTracker_BitfsDr::Phase::TURN_UPHILL && marioState->forwardVel <= 16.0f)
            return false;

        // Ensure we gain speed each crossing. Check both directions separately to account for axis asymmetry
        if (!StateTracker_BitfsDr::ValidateCrossingData(state))
            return false;

        if (state.currentOscillation >= 8 && lastFrameState.currentOscillation == state.currentOscillation - 1)
        {
            char fileName[128];
            printf("\nosc%d\n", state.currentOscillation);
            sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\res\\bitfs_osc_%d_%f_%f_%f_%f.m64",
                state.currentOscillation, pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->forwardVel);
            ExportM64(fileName);
        }

        /*
        if (marioState->action == ACT_FORWARD_ROLLOUT && fabs(xNorm) > .3 && fabs(xNorm) + fabs(zNorm) > .65 &&
            marioState->pos[0] + marioState->pos[2] > (-1945 - 715)) //make sure Mario is going toward the right/east edge
        {  
            #pragma omp critical
            {
                char fileName[128];
                printf("\ndr\n");
                sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\res\\bitfs_dr_%f_%f_%f_%f.m64",
                    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1]);
                //ExportM64(fileName);
            }
        }

        //check on hspd > 1 confirms we're in dr land rather than quickstopping,
        //which gives the same action
        if (marioState->action == ACT_FREEFALL_LAND_STOP && marioState->pos[1] > -2980 && marioState->forwardVel > 1
            && fabs(xNorm) > .29 && fabs(marioState->pos[0]) > -1680)
        {
            #pragma omp critical
            {
                char fileName[128];
                printf("\ndrland\n");
                sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\res\\bitfs_drland_%f_%f_%f_%f.m64",
                    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1]);
                ExportM64(fileName);
            }
        }
        */

        return true;
    }

    float GetStateFitness()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
        if (state.initialized)
        {
            switch (state.phase)
            {
            case StateTracker_BitfsDr::Phase::INITIAL:
                return state.xzSum;

            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL:
                return marioState->forwardVel;

            case StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING:
                if (marioState->action == ACT_TURNING_AROUND)
                    return 0;
                else
                    return marioState->forwardVel;

            case StateTracker_BitfsDr::Phase::TURN_UPHILL:
            default:
                if (state.crossingData.empty())
                    return -float(GetCurrentFrame());

                return float(state.crossingData.rbegin()->frame) - float(GetCurrentFrame());
            }
        }

        return -std::numeric_limits<float>::infinity();
    }

    std::string GetCsvLabels() override
    {
        return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Oscillation,Crossing,Phase");
    }

    bool ForceAddToCsv() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        if (marioState->action == ACT_FORWARD_ROLLOUT || marioState->action == ACT_FREEFALL_LAND_STOP)
            return true;

        return false;
    }

    std::string GetCsvRow() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

        int phaseValue;
        switch (state.phase)
        {
        case StateTracker_BitfsDr::Phase::INITIAL: phaseValue = 0; break;
        case StateTracker_BitfsDr::Phase::ATTEMPT_DR: phaseValue = 1; break;
        case StateTracker_BitfsDr::Phase::QUICKTURN: phaseValue = 2; break;
        case StateTracker_BitfsDr::Phase::RUN_DOWNHILL: phaseValue = 3; break;
        case StateTracker_BitfsDr::Phase::TURN_AROUND: phaseValue = 4; break;
        case StateTracker_BitfsDr::Phase::TURN_UPHILL: phaseValue = 5; break;
        case StateTracker_BitfsDr::Phase::RUN_DOWNHILL_PRE_CROSSING: phaseValue = 6; break;
        default: phaseValue = 7;
        }

        char line[256];
        sprintf(line, "%f,%f,%f,%d,%f,%d,%f,%f,%f,%d,%d,%d",
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
            phaseValue);

        return std::string(line);
    }

private:
    bool Pbd()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

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

    bool TurnUphill()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                for (int i = 0; i < 10 && GetTempRng() % 4 < 3; i++)
                {
                    if (!TurnUphill_1f())
                        return true;
                }

                return RunForwardThenTurnAround();
            }).executed;
    }

    bool RunForwardThenTurnAround()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                for (int i = 0; marioState->action == ACT_FINISH_TURNING_AROUND || (i < 15 && GetTempRng() % 10 < 9); i++)
                {
                    if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                        return true;

                    auto stick = Inputs::GetClosestInputByYawExact(
                        marioState->faceAngle[1], 32, camera->yaw);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
                }

                // May need an extra walking frame to avoid double turnaround glitch
                if (marioState->action == ACT_WALKING && marioState->prevAction == ACT_FINISH_TURNING_AROUND)
                {
                    auto stick = Inputs::GetClosestInputByYawExact(
                        marioState->faceAngle[1], 32, camera->yaw);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
                }

                return TurnAroundThenRunDownhill();
            }).executed;
    }

    bool RunDownhillThenTurnUphill()
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

    bool TurnAroundThenRunDownhill()
    {
        return ModifyAdhoc([&]()
            {
                TurnAround();

                // Run downhill until past equilibrium point
                for (int i = 0; i < 30; i++)
                {
                    auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                    if (!RunDownhill_1f())
                        return true;

                    auto nextState = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
                    if (nextState.currentCrossing > state.currentCrossing)
                        break;
                }

                return true;
            }).executed;
    }

    bool TurnAround()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                if (marioState->action != ACT_WALKING || marioState->forwardVel <= 16.0f)
                    return true;

                for (int i = 0; i < 30; i++)
                {
                    auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                    // Turn 2048 towrds uphill
                    auto m64 = M64();
                    auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                    auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                        ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, state.roughTargetAngle, marioState->faceAngle[1]);
                    if (!status.asserted)
                        return true;

                    // Attempt to run downhill with minimum angle
                    int16_t intendedYaw = status.angleFacingAnalogBack;
                    auto stick = Inputs::GetClosestInputByYawExact(intendedYaw, 32, camera->yaw, status.downhillRotation);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                    if (marioState->action == ACT_FINISH_TURNING_AROUND)
                        break;

                    if (marioState->action != ACT_TURNING_AROUND)
                        return true;
                }
            }).executed;
    }

    bool RunDownhill_1f(bool min = true)
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        ModifyAdhoc([&]()
            {
                if (marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                    return true;

                auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                auto m64 = M64();
                auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                    .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                    .Main(state.roughTargetAngle, marioState->faceAngle[1]);
                if (!status.asserted)
                    return true;

                int16_t intendedYaw;
                int16_t minAngle = marioState->action == ACT_TURNING_AROUND ? status.angleFacingAnalogBack : status.angleFacing;
                if (min)
                    intendedYaw = minAngle;
                else
                {
                    int16_t diff = minAngle - status.floorAngle;
                    if (diff > 0)
                        intendedYaw = minAngle - GetTempRng() % 8192;
                    else
                        intendedYaw = minAngle + GetTempRng() % 8192;
                }

                auto stick = Inputs::GetClosestInputByYawExact(
                    intendedYaw, 32, camera->yaw, status.downhillRotation);
                AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                return true;
            }).executed;

        return marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING;
    }

    bool TurnUphill_1f()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        ModifyAdhoc([&]()
            {
                if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                    return true;

                // Turn 2048 towrds uphill
                auto m64 = M64();
                auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                    ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, 0);
                if (!status.asserted)
                    return true;

                int16_t uphillAngle = status.floorAngle + 0x8000;

                //cap intended yaw diff at 2048
                int16_t intendedYaw = uphillAngle;
                if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                    intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(uphillAngle - marioState->faceAngle[1]));

                auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

                return true;
            }).executed;

        return marioState->action == ACT_FINISH_TURNING_AROUND && marioState->action == ACT_WALKING;
    }

    bool Quickturn()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                if (marioState->action != ACT_FREEFALL_LAND_STOP)
                    return false;

                AdvanceFrameWrite(Inputs(0, 0, 0));

                // Turn 2048 towrds uphill
                auto m64 = M64();
                auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                    .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                    .Main(marioState->faceAngle[1]);
                if (!status.asserted)
                    return true;

                // Get closest cardinal controller input to uphill angle
                int16_t uphillCardinalYaw = (((uint16_t)(status.floorAngle + 0x8000 - camera->yaw + 0x2000) >> 14) << 14) + camera->yaw;
                auto inputs = Inputs::GetClosestInputByYawHau(
                    uphillCardinalYaw, nextafter(0.0f, 1.0f), camera->yaw); // Min mag
                AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
                AdvanceFrameWrite(Inputs(0, 0, 0));

                return true;
            }).executed;
    }
};
