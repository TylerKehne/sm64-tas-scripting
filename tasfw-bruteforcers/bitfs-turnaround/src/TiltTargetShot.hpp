#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class TiltTargetShotArgs
{
public:
    int64_t InitialFrame = -1;
    float TargetNx = 0;
    float TargetNz = 0;
    bool TargetXDimension = true;
    bool FixNonTargetDimensionARE = false;
    int Neighborhood = 0;
    int ErrorType = 1;
    int TargetARE = 0;
};

class TiltTargetShotSolution
{
public:
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    std::vector<float> error = { INFINITY, INFINITY, INFINITY };
    std::vector<float> remainderError = { INFINITY, INFINITY, INFINITY };
    std::vector<float> adjustedRemainderError = { INFINITY, INFINITY, INFINITY };
    std::vector<int> incrementFrames = { 0, 0, 0 };
    int64_t equilibriumFrame = -1;
};

class TiltTargetShotMetrics : public Script<LibSm64>
{
public:
    int64_t _startFrame = 0;
    float _targetNx = 0;
    float _targetNz = 0;
    float _targetRemainderX = 0;
    float _targetRemainderZ = 0;

    bool _targetX = true;
    bool _fixOtherAxis = false;

    int64_t _neighborhood = 0;

    class CustomScriptStatus
    {
    public:
        bool targetX = true;
        bool fixOtherAxis = false;

        bool initialized = false;

        bool isEquilibrium = false;
        bool isMoving = true;
        bool isOnPyramid = false;

        std::vector<float> normal = { INFINITY, INFINITY, INFINITY };

        std::vector<float> target = { INFINITY, INFINITY, INFINITY };
        std::vector<float> errorRaw = { INFINITY, INFINITY, INFINITY };
        std::vector<float> error = { INFINITY, INFINITY, INFINITY };
        std::vector<float> remainderErrorRaw = { INFINITY, INFINITY, INFINITY };
        std::vector<float> remainderError = { INFINITY, INFINITY, INFINITY };
        std::vector<float> adjustedRemainderError = { INFINITY, INFINITY, INFINITY };
        std::vector<int> incrementFrames = { 0, 0, 0 };

        std::vector<float> minErrorRaw = { INFINITY, INFINITY, INFINITY };
        std::vector<float> minError = { INFINITY, INFINITY, INFINITY };
        int64_t minErrorFrame = 0;

        std::vector<float> minRemainderErrorRaw = { INFINITY, INFINITY, INFINITY };
        std::vector<float> minRemainderError = { INFINITY, INFINITY, INFINITY };
        int64_t minRemainderErrorFrame = 0;

        uint32_t action = ACT_UNINITIALIZED;
        float forwardVel = INFINITY;
        int16_t faceAngle = 0;
        std::vector<float> marioPos = { INFINITY, INFINITY, INFINITY };

        int64_t equilibriumFrame = -1;
        int64_t frame = -1;

        int64_t otherAxisSolutionFrame = -1;
        int64_t otherAxisAdjustedSolutionFrame = -1;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    TiltTargetShotMetrics() = default;
    TiltTargetShotMetrics(const TiltTargetShotArgs& args)
        : _startFrame(args.InitialFrame), _targetNx(args.TargetNx), _targetNz(args.TargetNz), _targetX(args.TargetXDimension),
        _fixOtherAxis(args.FixNonTargetDimensionARE), _neighborhood(args.Neighborhood)
    {
        float targetHundrethX = std::floorf(_targetNx * 100.0f) / 100.0f;
        float targetHundrethZ = std::floorf(_targetNz * 100.0f) / 100.0f;

        _targetRemainderX = _targetNx - targetHundrethX;
        _targetRemainderZ = _targetNz - targetHundrethZ;
    }

    bool validation()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));

        if (marioState->floor == nullptr)
            return false;

        Object* pyramid = marioState->floor->object;
        if (pyramid == nullptr || pyramid->behavior != pyramidBehavior)
            return false;

        return GetCurrentFrame() >= _startFrame - 2;
    }

    bool execution()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* pyramid = marioState->floor->object;

        CustomStatus.initialized = true;
        CustomStatus.frame = GetCurrentFrame();
        CustomStatus.faceAngle = marioState->faceAngle[1];
        CustomStatus.targetX = _targetX;
        CustomStatus.fixOtherAxis = _fixOtherAxis;
        CustomStatus.target = { _targetNx , INFINITY, _targetNz };

        CustomStatus.normal = { pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ };

        CustomStatus.errorRaw = { _targetNx - pyramid->oTiltingPyramidNormalX, INFINITY, _targetNz - pyramid->oTiltingPyramidNormalX };

        float errorIncX = std::fabs(std::nextafter(_targetNx, INFINITY) - _targetNx);
        float errorIncZ = std::fabs(std::nextafter(_targetNz, INFINITY) - _targetNz);
        CustomStatus.error =
        {
            (_targetNx - pyramid->oTiltingPyramidNormalX) / errorIncX,
            INFINITY,
            (_targetNz - pyramid->oTiltingPyramidNormalZ) / errorIncZ
        };

        float closestHundrethX = std::floorf(pyramid->oTiltingPyramidNormalX * 100.0f) / 100.0f;
        float closestHundrethZ = std::floorf(pyramid->oTiltingPyramidNormalZ * 100.0f) / 100.0f;
        float remainderX = closestHundrethX - pyramid->oTiltingPyramidNormalX;
        float remainderZ = closestHundrethZ - pyramid->oTiltingPyramidNormalZ;
        CustomStatus.remainderErrorRaw =
        {
            std::remainderf(remainderX - _targetRemainderX + 0.5f, 1.0f) - 0.5f,
            INFINITY,
            std::remainderf(remainderX - _targetRemainderX + 0.5f, 1.0f) - 0.5f
        };

        CustomStatus.remainderError =
        {
            CustomStatus.remainderErrorRaw[0] / errorIncX,
            INFINITY,
            CustomStatus.remainderErrorRaw[2] / errorIncZ
        };

        float normalX = pyramid->oTiltingPyramidNormalX;
        for (int i = 0; i < 200; i++)
        {
            if (std::fabs(_targetNx - normalX) <= 0.005f)
            {
                CustomStatus.adjustedRemainderError[0] = (_targetNx - normalX) / errorIncX;
                CustomStatus.incrementFrames[0] = i;
                break;
            }

            normalX += sign(CustomStatus.error[0]) * 0.01f;
        }

        float normalZ = pyramid->oTiltingPyramidNormalZ;
        for (int i = 0; i < 200; i++)
        {
            if (std::fabs(_targetNz - normalZ) <= 0.005f)
            {
                CustomStatus.adjustedRemainderError[2] = (_targetNz - normalZ) / errorIncZ;
                CustomStatus.incrementFrames[2] = i;
                break;
            }

            normalZ += sign(CustomStatus.error[2]) * 0.01f;
        }

        auto prevState = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame() - 1);

        if (_targetX && CustomStatus.error[0] < prevState.minError[0])
        {
            float frameError = (CustomStatus.errorRaw[2] - prevState.minErrorRaw[2]) / 0.01f;
            if (!_fixOtherAxis || frameError == std::floor(frameError))
            {
                CustomStatus.minErrorRaw = { CustomStatus.errorRaw[0], CustomStatus.errorRaw[1], CustomStatus.errorRaw[2] };
                CustomStatus.minError = { CustomStatus.error[0], CustomStatus.error[1], CustomStatus.error[2] };
                CustomStatus.minErrorFrame = GetCurrentFrame();
            }
        }
        else if (!_targetX && CustomStatus.error[2] < prevState.minError[2])
        {
            float frameError = (CustomStatus.errorRaw[0] - prevState.minErrorRaw[0]) / 0.01f;
            if (!_fixOtherAxis || frameError == std::floor(frameError))
            {
                CustomStatus.minErrorRaw = { CustomStatus.errorRaw[0], CustomStatus.errorRaw[1], CustomStatus.errorRaw[2] };
                CustomStatus.minError = { CustomStatus.error[0], CustomStatus.error[1], CustomStatus.error[2] };
                CustomStatus.minErrorFrame = GetCurrentFrame();
            }
        }

        if (_targetX && CustomStatus.remainderError[0] < prevState.minRemainderError[0])
        {
            float frameError = (CustomStatus.errorRaw[2] - prevState.minErrorRaw[2]) / 0.01f;
            if (!_fixOtherAxis || frameError == std::floor(frameError))
            {
                CustomStatus.minRemainderErrorRaw = { CustomStatus.remainderErrorRaw[0], CustomStatus.remainderErrorRaw[1], CustomStatus.remainderErrorRaw[2] };
                CustomStatus.minRemainderError = { CustomStatus.remainderError[0], CustomStatus.remainderError[1], CustomStatus.remainderError[2] };
                CustomStatus.minRemainderErrorFrame = GetCurrentFrame();
            }
        }
        else if (!_targetX && CustomStatus.remainderError[2] < prevState.minRemainderError[2])
        {
            float frameError = (CustomStatus.errorRaw[0] - prevState.minErrorRaw[0]) / 0.01f;
            if (!_fixOtherAxis || frameError == std::floor(frameError))
            {
                CustomStatus.minRemainderErrorRaw = { CustomStatus.remainderErrorRaw[0], CustomStatus.remainderErrorRaw[1], CustomStatus.remainderErrorRaw[2] };
                CustomStatus.minRemainderError = { CustomStatus.remainderError[0], CustomStatus.remainderError[1], CustomStatus.remainderError[2] };
                CustomStatus.minRemainderErrorFrame = GetCurrentFrame();
            }
        }

        CustomStatus.action = marioState->action;
        CustomStatus.forwardVel = marioState->forwardVel;
        CustomStatus.marioPos = { marioState->pos[0], marioState->pos[1], marioState->pos[2] };
        CustomStatus.isMoving = marioState->forwardVel != 0 || marioState->action != ACT_IDLE;
        CustomStatus.isOnPyramid = true;

        CustomStatus.otherAxisSolutionFrame = prevState.otherAxisSolutionFrame;
        CustomStatus.otherAxisAdjustedSolutionFrame = prevState.otherAxisAdjustedSolutionFrame;
        CustomStatus.equilibriumFrame = prevState.equilibriumFrame;
        CustomStatus.isEquilibrium = CheckEquilibrium();
        if (!CustomStatus.isEquilibrium)
            CustomStatus.equilibriumFrame = -1;
        else if (CustomStatus.fixOtherAxis)// && CustomStatus.equilibriumFrame != prevState.equilibriumFrame)
        {
            if (isOtherAxisSolution(CustomStatus.equilibriumFrame, true))
                CustomStatus.otherAxisAdjustedSolutionFrame = CustomStatus.equilibriumFrame;

            if (isOtherAxisSolution(CustomStatus.equilibriumFrame, false))
                CustomStatus.otherAxisSolutionFrame = CustomStatus.equilibriumFrame;
        }

        return true;
    }

    bool assertion() { return true; }

private:
    bool CheckEquilibrium()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* pyramid = marioState->floor->object;

        auto prevState2 = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame() - 2);
        auto prevState1 = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame() - 1);
        bool wasMoving2 = prevState2.forwardVel != 0 || prevState2.action != ACT_IDLE;
        bool wasMoving1 = prevState1.forwardVel != 0 || prevState1.action != ACT_IDLE;

        if (wasMoving2 || wasMoving1)
            return false;

        if (_targetX && CustomStatus.error[0] > prevState1.error[0]
            || !_targetX && CustomStatus.error[2] > prevState1.error[2])
        {
            if (CustomStatus.minErrorFrame > 0
                && GetTrackedState<TiltTargetShotMetrics>(CustomStatus.minErrorFrame - 1).isMoving == false
                && GetTrackedState<TiltTargetShotMetrics>(CustomStatus.minErrorFrame - 2).isMoving == false)
            {
                CustomStatus.equilibriumFrame = CustomStatus.minErrorFrame - 1;
                return true;
            }
        }

        if (0.00001f < std::fabs(prevState1.marioPos[0] - prevState2.marioPos[0])
            || 0.00001f < std::fabs(prevState1.marioPos[2] - prevState2.marioPos[2]))
            return false;

        if (0.00001f < std::fabs(CustomStatus.error[0] - prevState1.error[0])
            || 0.00001f < std::fabs(CustomStatus.error[2] - prevState1.error[2]))
            return false;

        CustomStatus.equilibriumFrame = GetCurrentFrame() - 1;
        return true;
    }

    // Detect whether this TAS was a solution for a prior 1D run
    bool isOtherAxisSolution(int64_t equilibriumFrame, bool isAdjusted)
    {
        if (!CustomStatus.fixOtherAxis)
            return false;

        // This cannot be later than the current frame based on how this is calculated
        auto prevState = GetTrackedState<TiltTargetShotMetrics>(equilibriumFrame);

        TiltTargetShotMetrics::CustomScriptStatus eqState;
        if (equilibriumFrame + 1 == GetCurrentFrame())
            eqState = CustomStatus;
        else
            eqState = GetTrackedState<TiltTargetShotMetrics>(equilibriumFrame + 1);

        std::vector<float> solutionError;
        if (isAdjusted)
            solutionError = eqState.adjustedRemainderError;
        else
            solutionError = eqState.error;

        if (false)
        {
            if (!eqState.targetX)
            {
                if (0.00001f < std::fabs(prevState.normal[2] - eqState.normal[2]))
                    return false;
            }
            else if (0.00001f < std::fabs(prevState.normal[0] - eqState.normal[0]))
                return false;
        }

        return !eqState.targetX ?
            std::fabs(solutionError[0]) <= _neighborhood
            : std::fabs(solutionError[2]) <= _neighborhood;
    }
};

using Alias_ScattershotThread_TiltTargetShot = ScattershotThread<BinaryStateBin<16>, LibSm64, TiltTargetShotMetrics, TiltTargetShotSolution>;
using Alias_Scattershot_TiltTargetShot = Scattershot<BinaryStateBin<16>, LibSm64, TiltTargetShotMetrics, TiltTargetShotSolution>;

class TiltTargetShot : public Alias_ScattershotThread_TiltTargetShot
{
public:
    enum class ErrorType
    {
        ABSOLUTE_ERROR,
        ADJUSTED
    };

    TiltTargetShot(Alias_Scattershot_TiltTargetShot& scattershot, const TiltTargetShotArgs& args)
        : Alias_ScattershotThread_TiltTargetShot(scattershot), _initialFrame(args.InitialFrame), _targetNX(args.TargetNx),
        _targetNZ(args.TargetNz), _neighborhood(args.Neighborhood), _errorType((ErrorType)args.ErrorType), _targetARE(args.TargetARE) {}

    bool validation() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));

        // TODO: add method in TopLevelScriptBuilder
        LongLoad(_initialFrame - 10);

        // verify we are on the platform
        if (marioState->floor == nullptr)
            return false;

        _pyramid = marioState->floor->object;
        if (_pyramid == nullptr || _pyramid->behavior != pyramidBehavior)
            return false;

        if (std::fabs(_pyramid->oTiltingPyramidNormalX) + std::fabs(_pyramid->oTiltingPyramidNormalZ) > 0.6f)
            return false;

        _initialNX = _pyramid->oTiltingPyramidNormalX;
        _initialNZ = _pyramid->oTiltingPyramidNormalZ;

        if (!GetTrackedState<TiltTargetShotMetrics>(_initialFrame + 1).isEquilibrium)
            return false;

        return true;
    }

    void SelectMovementOptions()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        AddMovementOption(MovementOption::NO_SCRIPT);
    }

    bool ApplyMovement()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));

        int64_t initialFrame = _initialFrame;
        volatile int64_t currentFrame = GetCurrentFrame();
        auto state = GetTrackedState<TiltTargetShotMetrics>(currentFrame + 1);
        if (false && _errorType == ErrorType::ABSOLUTE_ERROR)
        {
            auto diff = GetDiff();
            initialFrame = diff.frames.empty() ? GetCurrentFrame() : diff.frames.begin()->first;
        }

        if (state.fixOtherAxis)
            initialFrame = _errorType == ErrorType::ABSOLUTE_ERROR ? state.otherAxisSolutionFrame : state.otherAxisAdjustedSolutionFrame;

        if (initialFrame == -1)
            initialFrame = _initialFrame;

        int odds = state.fixOtherAxis ? 2 : 2;

        if (GetCurrentFrame() < initialFrame || GetTempRng() % odds == 0)
            Load(initialFrame);
        else if (GetCurrentFrame() > initialFrame && GetTempRng() % odds != 0)
        {
            int framesToRewind = 1 + GetTempRng() % (GetCurrentFrame() - initialFrame);
            Load(GetCurrentFrame() - framesToRewind);
        }
        
        do
        {
            if (GetTempRng() % 4 == 0)
                AdvanceFrameRead();
            else
                AdvanceFrameWrite(SelectRandomInputs());

            if (!VerifyOnPyramid())
                return false;
        } while (GetTempRng() % 2 != 0);
        
        // Advance solutions to equilibrium in preparation for next stage
        if (ExecuteAdhoc([&]() { return IsSolution(); }).executed)
        {
            TiltTargetShotMetrics::CustomScriptStatus eqState;
            ExecuteAdhoc([&]() { eqState = GetEquilibriumTrackedState(); return true; });
            while (GetCurrentFrame() < eqState.frame - 1) // GetEquilibriumTrackedState() is actually the eq frame + 1
                AdvanceFrameWrite(GetInputs(GetCurrentFrame()));
        }

        return true;
    }

    BinaryStateBin<16> GetStateBin()
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

        TiltTargetShotMetrics::CustomScriptStatus trackedState;
        ExecuteAdhoc([&]() { trackedState = GetEquilibriumTrackedState(); return true; });
        if (!trackedState.initialized)
        {
            state.AddValueBits(bitCursor, 2, 2);
            return state;
        }

        std::vector<float> solutionError;
        if (_errorType == ErrorType::ADJUSTED)
        {
            state.AddValueBits(bitCursor, 1, 0);
            solutionError = trackedState.adjustedRemainderError;
        }
        else
        {
            state.AddValueBits(bitCursor, 1, 1);
            solutionError = trackedState.error;
        }

        // Encode solution uniqueness if bin is a solution
        if (ExecuteAdhoc([&]() { return IsSolution(); }).executed)
        {
            state.AddValueBits(bitCursor, 2, 0);

            //int64_t nRegions = (int64_t)_neighborhood * _solutionRadius * 2 + 1;
            if (trackedState.targetX)
            {
                state.AddValueBits(bitCursor, 1, 0);
                state.AddValueBits(bitCursor, 8, solutionError[0] + _neighborhood);
            }
            else
            {
                state.AddValueBits(bitCursor, 1, 1);
                state.AddValueBits(bitCursor, 8, solutionError[2] + _neighborhood);
            }

            if (trackedState.fixOtherAxis)
            {
                state.AddValueBits(bitCursor, 1, 0);
                if (trackedState.targetX)
                {
                    state.AddValueBits(bitCursor, 1, 0);
                    state.AddValueBits(bitCursor, 8, solutionError[2] + _neighborhood);
                }
                else
                {
                    state.AddValueBits(bitCursor, 1, 1);
                    state.AddValueBits(bitCursor, 8, solutionError[0] + _neighborhood);
                }
            }
            else
                state.AddValueBits(bitCursor, 1, 1);

            state.AddValueBits(bitCursor, 1, 0);
            return state;
        }
        else
            state.AddValueBits(bitCursor, 1, 1);

        // Encode 1D ARE from other axis so different values don't compete with each other
        auto nextState = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame() + 1);
        if (trackedState.fixOtherAxis
            && (trackedState.targetX ? std::fabs(nextState.adjustedRemainderError[2]) <= _neighborhood
                : std::fabs(nextState.adjustedRemainderError[0]) <= _neighborhood))
        {
            state.AddValueBits(bitCursor, 1, 0);
            if (trackedState.targetX)
            {
                state.AddValueBits(bitCursor, 1, 0);
                state.AddValueBits(bitCursor, 8, nextState.adjustedRemainderError[2] + _neighborhood);
            }
            else
            {
                state.AddValueBits(bitCursor, 1, 1);
                state.AddValueBits(bitCursor, 8, nextState.adjustedRemainderError[0] + _neighborhood);
            }
        }
        else
            state.AddValueBits(bitCursor, 1, 1);

        // For whatever reason I get better results with this for 2D only
        state.AddValueBits(bitCursor, 1, trackedState.fixOtherAxis);
        if (trackedState.fixOtherAxis)
        {
            int frameDiff = std::clamp(int(currentFrame - _initialFrame), 0, 255);
            state.AddValueBits(bitCursor, 8, frameDiff);

            state.AddRegionBitsByRegionSize(bitCursor, 32, xPosValue, xMin, xMax, 10.0f);
            state.AddRegionBitsByRegionSize(bitCursor, 32, zPosValue, zMin, zMax, 10.0f);
        }
        
        state.AddValueBits(bitCursor, 13, std::abs(faceAngle) >> 4);

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

        // Action check
        if (marioState->action != ACT_DIVE_SLIDE &&
            marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_FREEFALL &&
            marioState->action != ACT_FREEFALL_LAND &&
            marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_BRAKING &&
            marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING
            && marioState->action != ACT_DECELERATING && marioState->action != ACT_IDLE)
        {
            return false;
        }

        if (marioState->action == ACT_FORWARD_ROLLOUT || marioState->action == ACT_FREEFALL_LAND_STOP)
            return false;

        if (marioState->action == ACT_FREEFALL)
            return false;

        if (marioState->floorHeight == -3071)
            return false;

        // Check custom metrics
        float xNorm = pyramid->oTiltingPyramidNormalX;
        float zNorm = pyramid->oTiltingPyramidNormalZ;
        
        if (_errorType == ErrorType::ABSOLUTE_ERROR)
        {
            auto state = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame());
            if (state.targetX)
            {
                if (std::fabs(state.adjustedRemainderError[0]) > _neighborhood)
                    return false;
            }
            else if (std::fabs(state.adjustedRemainderError[2]) > _neighborhood)
                return false;
        }

        if (true && GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame()).fixOtherAxis)
        {
            TiltTargetShotMetrics::CustomScriptStatus state;
            ExecuteAdhoc([&]() { state = GetEquilibriumTrackedState(); return true; });
            if (!state.initialized)
                return false;

            auto nextState = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame() + 1);
            if (state.frame > nextState.frame && nextState.initialized)
                state = nextState;
            
            if (state.targetX)
            {
                if (std::fabs(state.adjustedRemainderError[2]) > _neighborhood)
                    return false;
            }
            else if (std::fabs(state.adjustedRemainderError[0]) > _neighborhood)
                return false;
        }

        return true;
    }

    float GetStateFitness()
    {
        auto state = GetEquilibriumTrackedState();
        if (!state.initialized)
            return -INFINITY;

        if (state.fixOtherAxis)
        {
            if (state.targetX)
            {
                if (std::fabs(state.adjustedRemainderError[2]) > _neighborhood)
                    return -INFINITY;
            }
            else if (std::fabs(state.adjustedRemainderError[0]) > _neighborhood)
                return -INFINITY;
        }

        if (_errorType == ErrorType::ADJUSTED)
            return state.targetX ? -std::fabs(std::roundf(state.adjustedRemainderError[0])) : -std::fabs(std::roundf(state.adjustedRemainderError[2]));
        else
            return state.targetX ? -std::fabs(std::roundf(state.error[0])) : -std::fabs(std::roundf(state.error[2]));
    }

    std::string GetCsvLabels() override
    {
        return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,MarioYVel");
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

        char line[256];
        sprintf(line, "%f,%f,%f,%d,%f,%d,%f,%f,%f,%f",
            marioState->pos[0],
            marioState->pos[1],
            marioState->pos[2],
            marioState->faceAngle[1],
            marioState->forwardVel,
            marioState->action,
            pyramid->oTiltingPyramidNormalX,
            pyramid->oTiltingPyramidNormalY,
            pyramid->oTiltingPyramidNormalZ,
            marioState->vel[1]);

        return std::string(line);
    }

    bool IsSolution() override
    {
        auto state = GetEquilibriumTrackedState();

        if (!state.initialized)
            return false;

        // ensure there is ample room to manuever after
        if (std::fabs(state.normal[0]) + std::fabs(state.normal[2]) > 0.6f)
            return false;

        std::vector<float> solutionError;
        if (_errorType == ErrorType::ADJUSTED)
            solutionError = state.adjustedRemainderError;
        else
            solutionError = state.error;

        if (state.fixOtherAxis)
        {
            if (state.targetX)
            {
                if (state.adjustedRemainderError[2] != _targetARE)
                    return false;
            }
            else if (state.adjustedRemainderError[0] != _targetARE)
                return false;

            if (state.incrementFrames[0] % 2 != state.incrementFrames[2] % 2)
                return false;
        }

        return state.targetX ?
            std::fabs(solutionError[0]) <= _neighborhood
            : std::fabs(solutionError[2]) <= _neighborhood;
    }

    TiltTargetShotSolution GetSolutionState() override
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetEquilibriumTrackedState();

        auto solution = TiltTargetShotSolution();

        solution.pyraNormX = state.normal[0];
        solution.pyraNormY = state.normal[1];
        solution.pyraNormZ = state.normal[2];
        solution.error = state.error;
        solution.remainderError = state.remainderError;
        solution.adjustedRemainderError = state.adjustedRemainderError;
        solution.equilibriumFrame = state.frame - 1;
        solution.incrementFrames = state.incrementFrames;

        return solution;
    }

private:
    bool _preserveOtherAxis = false;
    int64_t _initialFrame = 0;
    float _targetNX = 0;
    float _targetNZ = 0;
    Object* _pyramid = nullptr;
    int64_t _neighborhood = 0;
    float _initialNX = 0;
    float _initialNZ = 0;
    ErrorType _errorType;
    int _targetARE = 0;

    bool VerifyOnPyramid()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));

        return marioState->marioObj->platform != nullptr && marioState->marioObj->platform->behavior == pyramidBehavior;
    }

    /// <summary>
    /// Get inputs for frame that been selected for perturbation
    /// </summary>
    /// <param name="frame"></param>
    /// <returns></returns>
    Inputs SelectRandomInputs()
    {
        if (GetTempRng() % 16 == 0)
            return Inputs(0, 0, 0);

        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));

        Inputs newInputs;
                
        // new random input
        if (GetTempRng() % 2 == 0)
        {
            int16_t intendedYaw;
            auto state = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame());
            if (_errorType == ErrorType::ABSOLUTE_ERROR && GetTempRng() % 4 != 0)
                intendedYaw = GetTempRng();
            else if (GetTempRng() % 8 != 0)
                intendedYaw = GetTempRng();
            else
                intendedYaw = RandomCardinalYaw();

            float intendedMag = RandomMag(GetTempRng() % 4 == 0);

            auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
            return Inputs(0, inputs.first, inputs.second);
        }

        return PerturbExistingInput();
    }

    int16_t GetYawDiff()
    {
        int16_t intendedYawDiff = 16;
        for (int i = 0; i < 10; i++)
        {
            // 50% chance of extending yaw deviation by 1
            if (GetTempRng() % 8 == 0)
                break;

            intendedYawDiff += 16;
        }

        if (GetTempRng() % 2 == 0)
            intendedYawDiff *= -1;

        return intendedYawDiff;
    }

    Inputs PerturbExistingInput()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));

        auto currentInputs = GetInputs(GetCurrentFrame());
        auto intendedInputs = Inputs::GetIntendedYawMagFromInput(currentInputs.stick_x, currentInputs.stick_y, camera->yaw);

        if (intendedInputs.second == 0)
        {
            if (_errorType == ErrorType::ABSOLUTE_ERROR || GetTempRng() % 2 != 0)
                intendedInputs.first = GetTempRng();
            else
                intendedInputs.first = RandomCardinalYaw();

            intendedInputs.second = RandomMag(GetTempRng() % 2 == 0);
        }
        else
        {
            intendedInputs.first += GetYawDiff();
            if (GetTempRng() % 2 == 0)
                intendedInputs.second = RandomMag(GetTempRng() % 4 == 0);
        }

        auto inputs = Inputs::GetClosestInputByYawHau(intendedInputs.first, intendedInputs.second, camera->yaw);
        return Inputs(0, inputs.first, inputs.second);
    }

    float RandomMag(bool lowMag)
    {
        auto state = GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame());
        if (_errorType == ErrorType::ABSOLUTE_ERROR && GetTempRng() % 2 == 0)
            return 32.0f;

        if (!lowMag || state.fixOtherAxis && GetTempRng() % 2 == 0)
            return (GetTempRng() % 1024) / 32.0f;
        else
            return (GetTempRng() % 1024) / 32.0f / 32.0f;
    }

    int16_t RandomCardinalYaw()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        int16_t intendedYaw;
        if (marioState->action == ACT_IDLE)
        {
            intendedYaw = GetTempRng() % 2 ? 0 : -32768;
        }

        else if (std::abs(marioState->faceAngle[1]) < 16384)
            intendedYaw = 0;
        else
            intendedYaw = -32768;

        if (!GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame()).targetX)
            intendedYaw += 16384;

        if (GetTrackedState<TiltTargetShotMetrics>(GetCurrentFrame()).fixOtherAxis && GetTempRng() % 2)
            intendedYaw += 16384;

        return intendedYaw += GetYawDiff();
    }

    /// <summary>
    /// Select frames for perturbation
    /// </summary>
    /// <returns></returns>
    std::set<int64_t> SelectFrames()
    {
        std::set<int64_t> chosenFrames;
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        TiltTargetShotMetrics::CustomScriptStatus state;
        ExecuteAdhoc([&]() {
            state = GetEquilibriumTrackedState();
            return true;
            });
        int64_t lastFrame = false && state.initialized ? state.frame - 1 : GetCurrentFrame();
        //int64_t lastFrame = GetCurrentFrame();
        int64_t totalFrames = lastFrame - _initialFrame + 1;
        //totalFrames *= 2;

        int64_t frame = GetTempRng() % totalFrames + _initialFrame;
        chosenFrames.insert(frame);

        while (chosenFrames.size() < totalFrames)
        {
            if (GetTempRng() % 4 == 0)
                break;

            // Don't choose frames that have already been chosen
            frame = GetTempRng() % (totalFrames - chosenFrames.size()) + _initialFrame;
            for (int i = _initialFrame; i <= lastFrame; i++)
            {
                if (chosenFrames.contains(i))
                {
                    frame++;
                    continue;
                }

                if (i == frame)
                {
                    chosenFrames.insert(frame);
                    break;
                }
            }
        }

        if (false && GetTempRng() % totalFrames == 0)
            chosenFrames.insert(_initialFrame);

        if (GetTempRng() % 2 == 0)
            chosenFrames.insert(lastFrame + 1);

        return chosenFrames;
    }

    Inputs SelectRandomInputs2(int64_t frame)
    {
        if (GetTempRng() % 4 == 0)
            return Inputs(0, 0, 0);

        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        Inputs newInputs;
        int64_t currentFrame = GetCurrentFrame();
        ExecuteAdhoc([&]()
            {
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Load(frame);

                if (GetTempRng() % 16 == 0)
                {
                    newInputs = Inputs(0, 0, 0);
                    return true;
                }

                // 50% chance of random input
                if (false && GetTempRng() % 4 == 0)
                {
                    int16_t intendedYaw = GetTempRng();
                    float intendedMag = 0;
                    if (GetTempRng() % 16 != 0)
                        intendedMag = (GetTempRng() % 1024) / 8.0f / 32.0f;

                    //if (marioState->action == ACT_DECELERATING && NewHash() % 2 == 0)
                    //    intendedMag /= 32.0f;

                    auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
                    newInputs = Inputs(0, inputs.first, inputs.second);
                    return true;
                }

                int16_t intendedYawDiff = 16;
                for (int i = 0; i < 10; i++)
                {
                    // 50% chance of extending yaw deviation by 1
                    if (GetTempRng() % 8 == 0)
                        break;

                    intendedYawDiff += 16;
                }

                auto currentInputs = GetInputs(frame);
                auto intendedInputs = Inputs::GetIntendedYawMagFromInput(currentInputs.stick_x, currentInputs.stick_y, camera->yaw);
                //intendedInputs.first = NewHash() % 2 ? 0 : -32768;

                if (intendedInputs.second == 0)
                {
                    if (marioState->forwardVel == 0 || GetTempRng() % 2 == 0)
                        intendedInputs.first = GetTempRng() % 2 ? 0 : -32768;
                    else
                        intendedInputs.first = marioState->faceAngle[1];
                }


                if (GetTempRng() % 2 == 0)
                    intendedInputs.first += intendedYawDiff;
                else
                    intendedInputs.first -= intendedYawDiff;

                // 50% chance of random mag
                if (GetTempRng() % 8 == 0)// || marioState->action == ACT_DECELERATING)
                {
                    intendedInputs.second = (GetTempRng() % 1024) / 8.0f / 16.0f;
                    //if (marioState->action == ACT_DECELERATING && NewHash() % 2 == 0)
                    //    intendedInputs.second /= 32.0f;
                }

                auto inputs = Inputs::GetClosestInputByYawHau(intendedInputs.first, intendedInputs.second, camera->yaw);
                newInputs = Inputs(0, inputs.first, inputs.second);

                return true;
            }
        );

        //TODO: revert frame cursor jumps in Execute methods
        Load(currentFrame);

        return newInputs;
    }

    TiltTargetShotMetrics::CustomScriptStatus GetEquilibriumTrackedState()
    {
        TiltTargetShotMetrics::CustomScriptStatus trackedState;
        int64_t initialFrame = GetCurrentFrame();

        for (int i = 1; i <= 200; i++)
        {
            if (!TrackedStateExists<TiltTargetShotMetrics>(initialFrame + i))
                Load(initialFrame + i);

            trackedState = GetTrackedState<TiltTargetShotMetrics>(initialFrame + i);
            if (!trackedState.isOnPyramid || trackedState.equilibriumFrame != -1)
                break;
        }
        if (trackedState.equilibriumFrame == -1)
            return TiltTargetShotMetrics::CustomScriptStatus();

        return GetTrackedState<TiltTargetShotMetrics>(trackedState.equilibriumFrame + 1);
    }
};
