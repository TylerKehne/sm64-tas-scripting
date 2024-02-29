#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class StateTracker_BitfsDrApproach : public Script<LibSm64>
{
public:
    enum class Phase
    {
        RUN_DOWNHILL,
        TURN_UPHILL,
        ATTEMPT_DR,
        QUICKTURN,
        C_UP_TRICK
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
        int16_t roughTargetAngle = 0;
        Phase phase = Phase::RUN_DOWNHILL;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    StateTracker_BitfsDrApproach() = default;
    StateTracker_BitfsDrApproach(int64_t initialFrame, int oscQuadrant, int targetQuadrant, float minXzSum)
    {
        SetRoughTargetAngle(oscQuadrant, targetQuadrant);

        this->normRegimeThreshold = minXzSum;
        this->initialFrame = initialFrame;
    }

    bool validation() { return GetCurrentFrame() >= initialFrame; }

    bool execution() 
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        SetStateVariables(marioState, pyramid);

        // Calculate recursive metrics
        int64_t currentFrame = GetCurrentFrame();
        CustomScriptStatus lastFrameState;
        if (currentFrame > initialFrame)
            lastFrameState = GetTrackedState<StateTracker_BitfsDrApproach>(currentFrame - 1);

        if (!lastFrameState.initialized)
            return true;

        CustomStatus.phase = lastFrameState.phase;
        CalculatePhase(lastFrameState, marioState, pyramid);

        return true;
    }

    bool assertion() { return CustomStatus.initialized == true; }

private:
    int64_t initialFrame = 0;
    int16_t roughTargetAngle = 16384;
    float normRegimeThreshold = 0.69f;

    void SetRoughTargetAngle(int oscQuadrant, int targetQuadrant)
    {
        switch (oscQuadrant)
        {
        case 1:
        {
            if (targetQuadrant == 2)
            {
                roughTargetAngle = -32768;
                break;
            }

            if (targetQuadrant == 4)
            {
                roughTargetAngle = -16384;
                break;
            }

            throw std::runtime_error("Invalid quadrants");
        }

        case 2:
        {
            if (targetQuadrant == 3)
            {
                roughTargetAngle = -16384;
                break;
            }

            if (targetQuadrant == 1)
            {
                roughTargetAngle = 0;
                break;
            }

            throw std::runtime_error("Invalid quadrants");
        }

        case 3:
        {
            if (targetQuadrant == 4)
            {
                roughTargetAngle = 0;
                break;
            }

            if (targetQuadrant == 2)
            {
                roughTargetAngle = 16384;
                break;
            }

            throw std::runtime_error("Invalid quadrants");
        }

        case 4:
        {
            if (targetQuadrant == 1)
            {
                roughTargetAngle = 16384;
                break;
            }

            if (targetQuadrant == 3)
            {
                roughTargetAngle = -32768;
                break;
            }

            throw std::runtime_error("Invalid quadrants");
        }

        default:
            throw std::runtime_error("Invalid quadrants");
        }
    }

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
        CustomStatus.roughTargetAngle = roughTargetAngle;
        CustomStatus.initialized = true;
    }

    void CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid)
    {
        switch (lastFrameState.phase)
        {
            case Phase::RUN_DOWNHILL:
                if (lastFrameState.fSpd > CustomStatus.fSpd)
                    CustomStatus.phase = Phase::TURN_UPHILL;
                break;

            case Phase::TURN_UPHILL:
                if (marioState->action == ACT_DIVE || marioState->action == ACT_DIVE_SLIDE)
                    CustomStatus.phase = Phase::ATTEMPT_DR;
                else if (lastFrameState.fSpd < CustomStatus.fSpd)
                    CustomStatus.phase = Phase::RUN_DOWNHILL;
                break;

            case Phase::ATTEMPT_DR:
                if (marioState->action == ACT_FREEFALL_LAND_STOP)
                    CustomStatus.phase = Phase::C_UP_TRICK;
                break;

            case Phase::QUICKTURN:
                if (marioState->action == ACT_IDLE)
                    CustomStatus.phase = Phase::C_UP_TRICK;
                break;
        }
    }
};

class Scattershot_BitfsDrApproach_Solution
{
public:
    float fSpd = 0;
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    float xzSum = 0;
};

using Alias_ScattershotThread_BitfsDrApproach = ScattershotThread<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDrApproach, Scattershot_BitfsDrApproach_Solution>;
using Alias_Scattershot_BitfsDrApproach = Scattershot<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDrApproach, Scattershot_BitfsDrApproach_Solution>;

class Scattershot_BitfsDrApproach : public Alias_ScattershotThread_BitfsDrApproach
{
public:
    Scattershot_BitfsDrApproach(Alias_Scattershot_BitfsDrApproach& scattershot)
        : Alias_ScattershotThread_BitfsDrApproach(scattershot) { }

    void SelectMovementOptions()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        auto state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());
        switch (state.phase)
        {
            case StateTracker_BitfsDrApproach::Phase::RUN_DOWNHILL:
                AddRandomMovementOption(
                    {
                        {MovementOption::NO_SCRIPT, 0},
                        {MovementOption::RUN_DOWNHILL_MIN, 5},
                        {MovementOption::RUN_DOWNHILL, 5},
                        {MovementOption::TURN_UPHILL, marioState->forwardVel <= 16.0f ? 0 : 2}
                    });
                break;

            case StateTracker_BitfsDrApproach::Phase::TURN_UPHILL:
            {
                auto prevState = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame() - 1);
                bool avoidDoubleTurnaround = prevState.marioAction == ACT_FINISH_TURNING_AROUND && state.marioAction == ACT_WALKING;

                AddRandomMovementOption(
                    {
                        {MovementOption::NO_SCRIPT, 0},
                        {MovementOption::TURN_UPHILL, 10},
                        {MovementOption::RUN_FORWARD, 0},
                        {MovementOption::PBD, state.marioAction == ACT_WALKING ? 10 : 0}
                    });
                break;
            }

            case StateTracker_BitfsDrApproach::Phase::ATTEMPT_DR:
                AddMovementOption(MovementOption::NO_SCRIPT);
                AddMovementOption(MovementOption::RANDOM_BUTTONS);

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
                        {MovementOption::RANDOM_YAW, 1}
                    });
                break;

            case StateTracker_BitfsDrApproach::Phase::QUICKTURN:
                AddMovementOption(MovementOption::QUICKTURN);
                break;

            case StateTracker_BitfsDrApproach::Phase::C_UP_TRICK:
                AddMovementOption(MovementOption::C_UP_TRICK);
                break;

            default:
                AddMovementOption(MovementOption::NO_SCRIPT);
        }
    }

    bool ApplyMovement()
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
            else if (CheckMovementOptions(MovementOption::PBD))
            {
                Pbd();
                return true;
            }
            else if (CheckMovementOptions(MovementOption::TURN_UPHILL))
            {
                TurnUphill_1f();
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

        // We want solutions to be just before the DR
        if ((marioState->action == ACT_FORWARD_ROLLOUT || marioState->action == ACT_FREEFALL_LAND_STOP)
            && fabs(marioState->pos[1] - marioState->floorHeight) < 4.0f)
            Load(GetCurrentFrame() - 1);

        return true;
    }

    BinaryStateBin<16> GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto trackedState = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());
        float norm_regime_min = 0.69f;
        int nRegions = trackedState.xzSum >= norm_regime_min ? 32 : 4;

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
        switch (trackedState.phase)
        {
            case StateTracker_BitfsDrApproach::Phase::ATTEMPT_DR: phaseValue = 0; break;
            case StateTracker_BitfsDrApproach::Phase::QUICKTURN: phaseValue = 1; break;
            case StateTracker_BitfsDrApproach::Phase::RUN_DOWNHILL: phaseValue = 2; break;
            case StateTracker_BitfsDrApproach::Phase::TURN_UPHILL: phaseValue = 3; break;
            case StateTracker_BitfsDrApproach::Phase::C_UP_TRICK: phaseValue = 4; break;
            default: phaseValue = 5;
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

        if (trackedState.initialized)
        {
            state.AddValueBits(bitCursor, 1, 1);
            state.AddValueBits(bitCursor, 4, actionValue);
            state.AddValueBits(bitCursor, 3, phaseValue);

            bool finePos = trackedState.phase == StateTracker_BitfsDrApproach::Phase::C_UP_TRICK
                || trackedState.phase == StateTracker_BitfsDrApproach::Phase::ATTEMPT_DR;
            float posRegionSize = finePos ? 1.0f : 5.0f;
            state.AddRegionBitsByRegionSize(bitCursor, 11, xPosValue, xMin, xMax, posRegionSize);
            state.AddRegionBitsByRegionSize(bitCursor, 11, zPosValue, zMin, zMax, posRegionSize);

            state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.xzSum, 0.f, 0.8f, 0.005f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.pyraNormX, -0.7f, 0.7f, 0.01f);

            state.AddRegionBitsByRegionSize(bitCursor, 7, int(marioState->faceAngle[1]), -32768, 32767, 1024);
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
        if (marioState->action != ACT_DIVE_SLIDE &&
            marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_FREEFALL &&
            marioState->action != ACT_FREEFALL_LAND &&
            marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING
            && marioState->action != ACT_DECELERATING && marioState->action != ACT_IDLE)
        {
            return false;
        } 

        if (marioState->action == ACT_FORWARD_ROLLOUT || marioState->action == ACT_FREEFALL_LAND_STOP)
            return false;

        if (marioState->action == ACT_FREEFALL && marioState->vel[1] > -20.0)
            return false; //freefall without having done nut spot chain

        if (marioState->floorHeight > -3071 && marioState->pos[1] > marioState->floorHeight + 4)
            return false; //above pyra by over 4 units

        if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL)
            return false; //diving/dring above lava

        // Double turnaround (ideally should prevent this in movement choices)
        if (marioState->action == ACT_WALKING && marioState->forwardVel < 0)
            return false;

        // Check custom metrics
        float xNorm = pyramid->oTiltingPyramidNormalX;
        float zNorm = pyramid->oTiltingPyramidNormalZ;
        auto state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());
        auto lastFrameState = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame() - 1);

        if (state.initialized&& state.phase == StateTracker_BitfsDrApproach::Phase::C_UP_TRICK
            && state.marioAction != ACT_FREEFALL && state.marioAction != ACT_FREEFALL_LAND);

        // Reject departures from norm regime
        float normRegimeThreshold = 0.69f;
        if (fabs(xNorm) + fabs(zNorm) < normRegimeThreshold - 0.02f)
            return false;

        // Reject untimely turnarounds
        if (marioState->action == ACT_TURNING_AROUND)
            return false;

        if (marioState->forwardVel < 29.0f && state.phase == StateTracker_BitfsDrApproach::Phase::TURN_UPHILL)
            return false;

        return true;
    }

    float GetStateFitness()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());
        if (state.initialized)
        {
            switch (state.phase)
            {
                case StateTracker_BitfsDrApproach::Phase::RUN_DOWNHILL:
                case StateTracker_BitfsDrApproach::Phase::TURN_UPHILL:
                case StateTracker_BitfsDrApproach::Phase::ATTEMPT_DR:
                    return marioState->forwardVel;

                default:
                    return -float(GetCurrentFrame());
            }
        }

        return -std::numeric_limits<float>::infinity();
    }

    std::string GetCsvLabels() override
    {
        return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Phase,MarioYVel");
    }

    bool ForceAddToCsv() override
    {
        return false;
    }

    std::string GetCsvRow() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());

        int phaseValue;
        switch (state.phase)
        {
        case StateTracker_BitfsDrApproach::Phase::ATTEMPT_DR: phaseValue = 0; break;
        case StateTracker_BitfsDrApproach::Phase::QUICKTURN: phaseValue = 1; break;
        case StateTracker_BitfsDrApproach::Phase::RUN_DOWNHILL: phaseValue = 2; break;
        case StateTracker_BitfsDrApproach::Phase::TURN_UPHILL: phaseValue = 3; break;
        case StateTracker_BitfsDrApproach::Phase::C_UP_TRICK: phaseValue = 4; break;
        default: phaseValue = 5;
        }

        char line[256];
        sprintf(line, "%f,%f,%f,%d,%f,%d,%f,%f,%f,%d,%f",
            marioState->pos[0],
            marioState->pos[1],
            marioState->pos[2],
            marioState->faceAngle[1],
            marioState->forwardVel,
            marioState->action,
            pyramid->oTiltingPyramidNormalX,
            pyramid->oTiltingPyramidNormalY,
            pyramid->oTiltingPyramidNormalZ,
            phaseValue,
            marioState->vel[1]);

        return std::string(line);
    }

    bool IsSolution() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        if (marioState->action != ACT_DIVE_SLIDE)
            return false;

        AdvanceFrameRead();

        if (marioState->action != ACT_FREEFALL_LAND && marioState->action != ACT_FORWARD_ROLLOUT)
            return false;

        if (fabs(marioState->pos[1] - marioState->floorHeight) >= 4.0f)
            return false;

        return true;
    }

    Scattershot_BitfsDrApproach_Solution GetSolutionState() override
    {
        const auto& state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());

        auto solution = Scattershot_BitfsDrApproach_Solution();
        solution.fSpd = state.fSpd;
        solution.pyraNormX = state.pyraNormX;
        solution.pyraNormY = state.pyraNormY;
        solution.pyraNormZ = state.pyraNormZ;
        solution.xzSum = state.xzSum;

        return solution;
    }

private:
    int _targetOscillation = -1;

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
                AdvanceFrameWrite(Inputs(0, 0, 0));
                AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
                AdvanceFrameWrite(Inputs(0, 0, 0));

                return true;
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

                auto state = GetTrackedState<StateTracker_BitfsDrApproach>(GetCurrentFrame());

                auto m64 = M64();
                auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                    .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                    .Run(state.roughTargetAngle, marioState->faceAngle[1]);
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
                auto status = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
                    .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                    .Run(0);
                if (!status.asserted)
                    return true;

                int16_t uphillAngle = status.floorAngle + 0x8000;

                //cap intended yaw diff at 2048
                int16_t intendedYaw = uphillAngle;
                if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                    intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(uphillAngle - marioState->faceAngle[1]));

                // Don't always turn uphill full distance
                if (GetTempRng() % 8 == 0)
                {
                    int16_t angleDiff = intendedYaw - marioState->faceAngle[1];
                    int16_t maxTurn = GetTempRng() % 2048;
                    angleDiff = std::clamp(angleDiff, int16_t(-maxTurn), maxTurn);
                    intendedYaw = marioState->faceAngle[1] + angleDiff;
                }

                auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

                return true;
            }).executed;

        return marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING;
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
                    .Run(marioState->faceAngle[1]);
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

    bool CUpTrick()
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
                    .Run(marioState->faceAngle[1]);
                if (!status.asserted)
                    return true;

                // Get closest cardinal controller input to uphill angle
                int16_t uphillCardinalYaw = (((uint16_t)(status.floorAngle + 0x8000 - camera->yaw + 0x2000) >> 14) << 14) + camera->yaw;
                auto inputs = Inputs::GetClosestInputByYawHau(
                    uphillCardinalYaw, nextafter(0.0f, 1.0f), camera->yaw); // Min mag
                AdvanceFrameWrite(Inputs(Buttons::C_UP, inputs.first, inputs.second));
                AdvanceFrameWrite(Inputs(0, 0, 0));

                return true;
            }).executed;
    }
};
