#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>
#include <Scattershot_BitfsDrRecover.hpp>

class BitfsOscFinalArgs
{
public:
    int OscQuadrant;
    int TargetQuadrant;
    float TargetNx;
    float TargetNz;
    int64_t InitialFrame;
};

class BitfsOscSolution
{
public:
    float fSpd = 0;
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    float xzSum = 0;
};

class BitfsOscFinalMetrics : public Script<LibSm64>
{
public:
    enum class Phase
    {
        RUN_DOWNHILL,
        TURN_UPHILL,
        BRAKE,
        FALLING,
        POSTBRAKE
    };

    class CustomScriptStatus
    {
    public:
        bool initialized = false;

        std::vector<float> normal = { INFINITY, INFINITY, INFINITY };
        std::vector<float> target = { INFINITY, INFINITY, INFINITY };
        std::vector<float> adjustedRemainderError = { INFINITY, INFINITY, INFINITY };
        std::vector<int> incrementFrames = { 0, 0, 0 };

        uint32_t action = ACT_UNINITIALIZED;
        float forwardVel = INFINITY;
        int16_t faceAngle = 0;
        int16_t roughTargetAngle = 0;
        float normalDistance = -1.0f;

        int64_t frame = -1;

        Phase phase = Phase::RUN_DOWNHILL;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    BitfsOscFinalMetrics() = default;
    BitfsOscFinalMetrics(const BitfsOscFinalArgs& args) : _args(args) { }

    bool validation()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        //if (marioState->floor == nullptr)
        //    return false;

        //Object* pyramid = marioState->floor->object;
        //if (pyramid == nullptr || pyramid->behavior != pyramidBehavior)
        //    return false;

        _pyramid = pyramid;

        SetRoughTargetAngle(_args.OscQuadrant, _args.TargetQuadrant);

        return GetCurrentFrame() >= _args.InitialFrame;
    }

    bool execution()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        CustomStatus.initialized = true;
        CustomStatus.frame = GetCurrentFrame();
        CustomStatus.faceAngle = marioState->faceAngle[1];
        CustomStatus.target = { _args.TargetNx , INFINITY, _args.TargetNz };
        CustomStatus.forwardVel = marioState->forwardVel;
        CustomStatus.action = marioState->action;
        CustomStatus.roughTargetAngle = roughTargetAngle;

        CustomStatus.normal = { _pyramid->oTiltingPyramidNormalX, _pyramid->oTiltingPyramidNormalY, _pyramid->oTiltingPyramidNormalZ };

        auto m64 = M64();
        auto status = TopLevelScriptBuilder<DetectEdge>::Build(m64)
            .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, _pyramid)
            .Run();

        CustomStatus.normalDistance = status.asserted ? status.normalDistance : -1.0f;

        CalculateARE(_pyramid);
        CalculatePhase();

        return true;
    }

    bool assertion() { return true; }

private:
    BitfsOscFinalArgs _args;
    int16_t roughTargetAngle = 0;
    Object* _pyramid = nullptr;

    void CalculatePhase()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        auto lastFrameState = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame() - 1);
        if (!lastFrameState.initialized)
            return;

        switch (lastFrameState.phase)
        {
        case Phase::RUN_DOWNHILL:
            if (lastFrameState.forwardVel > CustomStatus.forwardVel)
                CustomStatus.phase = Phase::TURN_UPHILL;
            break;

        case Phase::TURN_UPHILL:
            if (marioState->action == ACT_TURNING_AROUND || marioState->action == ACT_BRAKING)
                CustomStatus.phase = Phase::BRAKE;
            else if (lastFrameState.forwardVel < CustomStatus.forwardVel)
                CustomStatus.phase = Phase::RUN_DOWNHILL;
            break;

        case Phase::BRAKE:
            if (marioState->action == ACT_FREEFALL || marioState->action == ACT_FREEFALL_LAND)
                CustomStatus.phase = Phase::FALLING;
            else if (marioState->forwardVel == 0)
                CustomStatus.phase = Phase::POSTBRAKE;
            break;

        case Phase::POSTBRAKE:
            if (marioState->action == ACT_FREEFALL || marioState->action == ACT_FREEFALL_LAND)
                CustomStatus.phase = Phase::FALLING;
            break;
        default:
            break;
        }
    }

    void CalculateARE(Object* pyramid)
    {
        float errorIncX = std::fabs(std::nextafter(_args.TargetNx, INFINITY) - _args.TargetNx);
        float errorIncZ = std::fabs(std::nextafter(_args.TargetNz, INFINITY) - _args.TargetNz);

        float errorX = (_args.TargetNx - pyramid->oTiltingPyramidNormalX) / errorIncX;
        float errorZ = (_args.TargetNz - pyramid->oTiltingPyramidNormalZ) / errorIncZ;

        float normalX = pyramid->oTiltingPyramidNormalX;
        for (int i = 0; i < 200; i++)
        {
            if (std::fabs(_args.TargetNx - normalX) <= 0.005f)
            {
                CustomStatus.adjustedRemainderError[0] = (_args.TargetNx - normalX) / errorIncX;
                CustomStatus.incrementFrames[0] = i * sign(errorX);
                break;
            }

            normalX += sign(errorX) * 0.01f;
        }

        float normalZ = pyramid->oTiltingPyramidNormalZ;
        for (int i = 0; i < 200; i++)
        {
            if (std::fabs(_args.TargetNz - normalZ) <= 0.005f)
            {
                CustomStatus.adjustedRemainderError[2] = (_args.TargetNz - normalZ) / errorIncZ;
                CustomStatus.incrementFrames[2] = i * sign(errorZ);
                break;
            }

            normalZ += sign(errorZ) * 0.01f;
        }
    }

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
};

using Alias_ScattershotThread_BitfsOscFinal = ScattershotThread<BinaryStateBin<16>, LibSm64, BitfsOscFinalMetrics, BitfsOscSolution>;
using Alias_Scattershot_BitfsOscFinal = Scattershot<BinaryStateBin<16>, LibSm64, BitfsOscFinalMetrics, BitfsOscSolution>;

class BitfsOscFinal : public Alias_ScattershotThread_BitfsOscFinal
{
public:
    BitfsOscFinalArgs _args;
    
    enum class ErrorType
    {
        ABSOLUTE_ERROR,
        ADJUSTED
    };

    BitfsOscFinal(Alias_Scattershot_BitfsOscFinal& scattershot, const BitfsOscFinalArgs& args)
        : Alias_ScattershotThread_BitfsOscFinal(scattershot), _args(args) {}

    void SelectMovementOptions() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
        switch (state.phase)
        {
        case BitfsOscFinalMetrics::Phase::RUN_DOWNHILL:
            AddRandomMovementOption(
                {
                    {MovementOption::NO_SCRIPT, 0},
                    {MovementOption::RUN_DOWNHILL_MIN, 5},
                    {MovementOption::RUN_DOWNHILL, 5},
                    {MovementOption::TURN_UPHILL, marioState->forwardVel <= 16.0f ? 0 : 2}
                });
            break;

        case BitfsOscFinalMetrics::Phase::TURN_UPHILL:
        {
            //auto prevState = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame() - 1);
            //bool avoidDoubleTurnaround = prevState.action == ACT_FINISH_TURNING_AROUND && state.action == ACT_WALKING;

            AddRandomMovementOption(
                {
                    {MovementOption::NO_SCRIPT, 10},
                    {MovementOption::TURN_UPHILL, 10},
                    {MovementOption::RUN_FORWARD, 10}
                });
            break;
        }

        case BitfsOscFinalMetrics::Phase::BRAKE:
            AddMovementOption(MovementOption::NO_SCRIPT);
            break;

        default:
            AddMovementOption(MovementOption::NO_SCRIPT);
        }

    }

    bool ApplyMovement() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());

        if (state.phase == BitfsOscFinalMetrics::Phase::RUN_DOWNHILL || state.phase == BitfsOscFinalMetrics::Phase::TURN_UPHILL)
        {
            auto m64 = M64();
            auto status = TopLevelScriptBuilder<DetectEdge>::Build(m64)
                .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                .Run();

            if (status.asserted && status.normalDistance < 100.0f && marioState->pos[1] > -2991.0f && GetTempRng() % 2 == 0)
            {
                AdvanceFrameWrite(Inputs(0, 0, 0));
                return true;
            }
        }

        // Scripts
        if (!CheckMovementOptions(MovementOption::NO_SCRIPT))
        {
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
            else if (CheckMovementOptions(MovementOption::TURN_UPHILL))
            {
                TurnUphill_1f();
                return true;
            }
        }

        if (state.phase == BitfsOscFinalMetrics::Phase::BRAKE)
        {
            AdvanceFrameWrite(Inputs(0, 0, 0));
            return true;
        }

        if (state.phase == BitfsOscFinalMetrics::Phase::POSTBRAKE)
        {
            if (GetTempRng() % 2 != 0)
            {
                AdvanceFrameWrite(Inputs(0, 0, 0));
                return true;
            }

            if (GetTempRng() % 2 == 0)
            {
                float intendedMag = (GetTempRng() % 1024) / 8.0f / 32.0f;
                int16_t intendedYaw = GetTempRng();
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
                return true;
            }
        }

        // Random input
        if (false && GetTempRng() % 4 == 0)
        {
            AdvanceFrameWrite(Inputs(0, 0, 0));
            return true;
        }

        AdvanceFrameWrite(RandomInputs({}));

        return true;
    }

    BinaryStateBin<16> GetStateBin() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

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

        int16_t faceAngle = marioState->faceAngle[1];
        auto currentFrame = GetCurrentFrame();

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
        case ACT_BRAKING_STOP: actionValue = 12; break;
        default: actionValue = 13;
        }

        int phaseValue;
        auto trackedState = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
        if (ExecuteAdhoc([&]() { return IsSolution(); }).executed)
        {
            state.AddValueBits(bitCursor, 2, 0);
            state.AddValueBits(bitCursor, 32, *(uint32_t*)&trackedState.normal[1]);
            state.AddValueBits(bitCursor, 13, ((uint16_t)(marioState->faceAngle[1])) >> 4);
            state.AddRegionBitsByRegionSize(bitCursor, 4, std::clamp(marioState->forwardVel, 0.f, 10.0f),
                0.f, 10.0f, 1.0f);
        }
        else if (trackedState.initialized)
        {
            state.AddValueBits(bitCursor, 2, 1);
            state.AddValueBits(bitCursor, 4, actionValue);
            state.AddValueBits(bitCursor, 3, (int)trackedState.phase);

            bool finePos = false;
            float posRegionSize = finePos ? 1.0f : 10.0f;
            state.AddRegionBitsByRegionSize(bitCursor, 11, xPosValue, xMin, xMax, posRegionSize);
            state.AddRegionBitsByRegionSize(bitCursor, 11, zPosValue, zMin, zMax, posRegionSize);

            state.AddRegionBitsByRegionSize(bitCursor, 8, std::fabs(trackedState.normal[0]) + std::fabs(trackedState.normal[2]), 0.f, 0.8f, 0.005f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.normal[0], -0.7f, 0.7f, 0.01f);

            state.AddRegionBitsByRegionSize(bitCursor, 4, std::clamp(marioState->forwardVel, 0.f, 32.0f),
                0.f, 32.0f, 5.0f);

            state.AddValueBits(bitCursor, 13, ((uint16_t)(marioState->faceAngle[1])) >> 10);
        }
        else
        {
            state.AddValueBits(bitCursor, 2, 2);
            state.AddValueBits(bitCursor, 4, actionValue);
            state.AddRegionBitsByRegionSize(bitCursor, 8, xPosValue, xMin, xMax, 100.0f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, zPosValue, zMin, zMax, 100.0f);
        }

        return state;
    }

    bool ValidateState() override
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

        // Action check
        if (marioState->action != ACT_FREEFALL &&
            marioState->action != ACT_FREEFALL_LAND &&
            marioState->action != ACT_DECELERATING &&
            marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_BRAKING &&
            marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING
            && marioState->action != ACT_BRAKING_STOP)
        {
            return false;
        }

        //if (marioState->floorHeight == -3071)
        //    return false;

        auto initialState = GetTrackedState<BitfsOscFinalMetrics>(_args.InitialFrame);
        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
        if (state.adjustedRemainderError[0] != initialState.adjustedRemainderError[0]
            || state.adjustedRemainderError[2] != initialState.adjustedRemainderError[2])
            return false;

        if (std::fabs(state.normal[0]) + std::fabs(state.normal[2])
            < std::fabs(initialState.normal[0]) + std::fabs(initialState.normal[2]) - 0.025f)
            return false;

        if (state.normalDistance > 100.0f)
        {
            if (!ExecuteAdhoc([&]() {return RunOffInTime(); }).executed)
                return false;

            if (!ExecuteAdhoc([&]() {return StopInTime(); }).executed)
                return false;
        }

        return true;
    }

    float GetStateFitness() override
    {
        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
        if (state.initialized)
        {
            switch (state.phase)
            {
            case BitfsOscFinalMetrics::Phase::RUN_DOWNHILL:
                return state.forwardVel;
            //case BitfsOscFinalMetrics::Phase::TURN_UPHILL:
            //    return state.forwardVel;
            //case BitfsOscFinalMetrics::Phase::TURN_UPHILL:
            //    return state.forwardVel;

            case BitfsOscFinalMetrics::Phase::BRAKE:
                return -state.forwardVel;

            case BitfsOscFinalMetrics::Phase::POSTBRAKE:
                //return -std::abs(std::abs(2) - std::abs(0));
                return float(GetCurrentFrame());

            default:
                return -float(GetCurrentFrame());
            }
        }

        return -std::numeric_limits<float>::infinity();
        //if (state.phase == BitfsOscFinalMetrics::Phase::BRAKE)
        //    return std::abs(std::abs(state.incrementFrames[2]) <= std::abs(state.incrementFrames[0]));

        //return -float(GetCurrentFrame());
    }

    std::string GetCsvLabels() override
    {
        return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Phase,MarioYVel,NormalDistance");
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

        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());

        char line[256];
        sprintf(line, "%f,%f,%f,%d,%f,%d,%f,%f,%f,%d,%f,%f",
            marioState->pos[0],
            marioState->pos[1],
            marioState->pos[2],
            marioState->faceAngle[1],
            marioState->forwardVel,
            marioState->action,
            pyramid->oTiltingPyramidNormalX,
            pyramid->oTiltingPyramidNormalY,
            pyramid->oTiltingPyramidNormalZ,
            state.phase,
            marioState->vel[1],
            state.normalDistance);

        return std::string(line);
    }

    bool IsSolution() override
    {
        //AdvanceFrameRead();
        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());

        return state.normalDistance < 10.0f && state.phase == BitfsOscFinalMetrics::Phase::POSTBRAKE;

        //return state.action == ACT_FREEFALL
        //    && state.forwardVel < 10.0f
        //    && std::abs(state.incrementFrames[0]) == std::abs(state.incrementFrames[2]);
    }

    BitfsOscSolution GetSolutionState() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto solution = BitfsOscSolution();

        auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
        solution.fSpd = state.forwardVel;
        solution.pyraNormX = state.normal[0];
        solution.pyraNormY = state.normal[1];
        solution.pyraNormZ = state.normal[2];
        solution.xzSum = std::fabs(state.normal[0]) + std::fabs(state.normal[2]);

        return solution;
    }

private:
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

                auto state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());

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
            });

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
            });

        return marioState->action == ACT_FINISH_TURNING_AROUND || marioState->action == ACT_WALKING;
    }

    bool RunOffInTime()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        auto initialState = GetTrackedState<BitfsOscFinalMetrics>(_args.InitialFrame);
        BitfsOscFinalMetrics::CustomScriptStatus state;

        for (int i = 0; i < 100; i++)
        {
            state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());
            if (state.adjustedRemainderError[0] != initialState.adjustedRemainderError[0]
                || state.adjustedRemainderError[2] != initialState.adjustedRemainderError[2])
                return false;

            if (marioState->action != ACT_WALKING
                && marioState->action != ACT_TURNING_AROUND
                && marioState->action != ACT_FINISH_TURNING_AROUND
                && marioState->action != ACT_BRAKING
                && marioState->action != ACT_DECELERATING)
                break;

            auto stick = Inputs::GetClosestInputByYawHau(state.roughTargetAngle, 32.0f, camera->yaw);
            AdvanceFrameWrite(Inputs(stick.first, stick.second, 0));
        }

        return std::abs(state.incrementFrames[2]) <= std::abs(state.incrementFrames[0]);
    }

    bool StopInTime()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        auto initialState = GetTrackedState<BitfsOscFinalMetrics>(_args.InitialFrame);
        BitfsOscFinalMetrics::CustomScriptStatus state;

        for (int i = 0; i < 100; i++)
        {
            state = GetTrackedState<BitfsOscFinalMetrics>(GetCurrentFrame());

            if (state.adjustedRemainderError[0] != initialState.adjustedRemainderError[0]
                || state.adjustedRemainderError[2] != initialState.adjustedRemainderError[2])
                return true;

            if (marioState->forwardVel == 0)
                return true;

            if (marioState->action != ACT_WALKING
                && marioState->action != ACT_TURNING_AROUND
                && marioState->action != ACT_FINISH_TURNING_AROUND
                && marioState->action != ACT_BRAKING
                && marioState->action != ACT_DECELERATING)
                break;

            AdvanceFrameWrite(Inputs(0, 0, 0));
        }

        return std::abs(state.incrementFrames[2]) >= std::abs(state.incrementFrames[0]);
    }
};
