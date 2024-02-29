#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class DetectEdge : public TopLevelScript<PyramidUpdate>
{
public:
    class CustomScriptStatus
    {
    public:
        std::vector<float> vertex1;
        std::vector<float> vertex2;
        float normalDistance = -1;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    DetectEdge() { }

    bool validation()
    {
        PyramidUpdateMem::Sm64MarioState* marioState = (PyramidUpdateMem::Sm64MarioState*)(resource->addr("gMarioStates"));

        return marioState->floorId == -1;
    }

    bool execution()
    {
        PyramidUpdateMem::Sm64MarioState* marioState = (PyramidUpdateMem::Sm64MarioState*)(resource->addr("gMarioStates"));
        PyramidUpdateMem::Sm64Object* pyramid = (PyramidUpdateMem::Sm64Object*)(resource->addr("Pyramid"));

        AdvanceFrameRead();

        // Get platform floor edges
        std::vector<Edge> edges;
        std::unordered_set<Edge, EdgeHash> presentEdges;
        std::unordered_set<Edge, EdgeHash> sharedEdges;
        for (auto& surface : pyramid->surfaces[1])
        {
            Edge edge1 = Edge(surface.vertex1, surface.vertex2);
            Edge edge2 = Edge(surface.vertex2, surface.vertex3);
            Edge edge3 = Edge(surface.vertex3, surface.vertex1);

            edges.push_back(edge1);
            if (!presentEdges.contains(edge1))
                presentEdges.insert(edge1);
            else
                sharedEdges.insert(edge1);

            edges.push_back(edge2);
            if (!presentEdges.contains(edge2))
                presentEdges.insert(edge2);
            else
                sharedEdges.insert(edge2);

            edges.push_back(edge3);
            if (!presentEdges.contains(edge3))
                presentEdges.insert(edge3);
            else
                sharedEdges.insert(edge3);
        }

        float minNormalDistance = -1;
        const Edge* closestEdge = nullptr;
        for (auto& edge : edges)
        {
            if (sharedEdges.contains(edge))
                continue;

            std::pair<float, float> dLine = { edge.vertex2[0] - edge.vertex1[0], edge.vertex2[2] - edge.vertex1[2] };
            std::pair<float, float> dMarioToLine = { marioState->posX - edge.vertex1[0], marioState->posZ - edge.vertex1[2] };

            float crossProduct = dLine.first * dMarioToLine.second - dLine.second * dMarioToLine.first;
            float dMarioToLineMag = std::sqrt(dMarioToLine.first * dMarioToLine.first + dMarioToLine.second * dMarioToLine.second);

            float normalDistance = dMarioToLineMag == 0 ? 0 : std::fabs(crossProduct) / dMarioToLineMag;
            if (minNormalDistance == -1 || normalDistance < minNormalDistance)
            {
                minNormalDistance = normalDistance;
                closestEdge = &edge;
            }
        }

        if (closestEdge != nullptr)
        {
            CustomStatus.vertex1 = closestEdge->vertex1;
            CustomStatus.vertex2 = closestEdge->vertex2;
            CustomStatus.normalDistance = minNormalDistance;

            return true;
        }

        return false;
    }

    bool assertion()
    {
        return CustomStatus.normalDistance != -1;
    }

private:
    class Edge
    {
    public:
        std::vector<float> vertex1 = { 0, 0, 0 };
        std::vector<float> vertex2 = { 0, 0, 0 };

        Edge(Vec3s vertex1, Vec3s vertex2)
        {
            this->vertex1[0] = vertex1[0];
            this->vertex1[1] = vertex1[1];
            this->vertex1[2] = vertex1[2];

            this->vertex2[0] = vertex2[0];
            this->vertex2[1] = vertex2[1];
            this->vertex2[2] = vertex2[2];
        }

        bool operator==(const Edge& toCompare) const
        {
            return vertex1 == toCompare.vertex1 && vertex2 == toCompare.vertex2
                || vertex1 == toCompare.vertex2 && vertex2 == toCompare.vertex1;
        }
    };

    struct EdgeHash
    {
        std::size_t operator()(const Edge& edge) const
        {
            return std::min(edge.vertex1[0], edge.vertex2[0]);
        }
    };
};

class StateTracker_BitfsDrRecover : public Script<LibSm64>
{
public:
    enum class Phase
    {
        INITIAL,
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
        Phase phase = Phase::INITIAL;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    StateTracker_BitfsDrRecover() = default;
    StateTracker_BitfsDrRecover(int64_t initialFrame, int oscQuadrant, int targetQuadrant, float minXzSum)
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
            lastFrameState = GetTrackedState<StateTracker_BitfsDrRecover>(currentFrame - 1);

        if (!lastFrameState.initialized)
            return true;

        CustomStatus.phase = lastFrameState.phase;
        CalculatePhase(lastFrameState, marioState, pyramid);

        return true;
    }

    bool assertion() { return CustomStatus.initialized == true; }

private:
    int16_t roughTargetAngle = 16384;
    float normRegimeThreshold = 0.69f;
    int64_t initialFrame = 0;

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
            case Phase::INITIAL:
                if (marioState->action == ACT_DIVE || marioState->action == ACT_DIVE_SLIDE)
                    CustomStatus.phase = Phase::ATTEMPT_DR;
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

class Scattershot_BitfsDrRecover_Solution
{
public:
    float fSpd = 0;
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    float xzSum = 0;
};

using Alias_ScattershotThread_BitfsDrRecover = ScattershotThread<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDrRecover, Scattershot_BitfsDrRecover_Solution>;
using Alias_Scattershot_BitfsDrRecover = Scattershot<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDrRecover, Scattershot_BitfsDrRecover_Solution>;

class Scattershot_BitfsDrRecover : public Alias_ScattershotThread_BitfsDrRecover
{
public:
    Scattershot_BitfsDrRecover(Alias_Scattershot_BitfsDrRecover& scattershot, StateTracker_BitfsDrRecover::Phase lastPhase)
        : Alias_ScattershotThread_BitfsDrRecover(scattershot), _lastPhase(lastPhase) { }

    void SelectMovementOptions()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));

        auto state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());
        switch (state.phase)
        {
            case StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR:
                AddMovementOption(MovementOption::NO_SCRIPT);
                AddMovementOption(MovementOption::RANDOM_BUTTONS);

                AddRandomMovementOption(
                    {
                        {MovementOption::MAX_MAGNITUDE, 1},
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

            case StateTracker_BitfsDrRecover::Phase::QUICKTURN:
                AddMovementOption(MovementOption::QUICKTURN);
                break;

            case StateTracker_BitfsDrRecover::Phase::C_UP_TRICK:
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
            else if (CheckMovementOptions(MovementOption::QUICKTURN))
            {
                Quickturn();
                return true;
            }
            else if (CheckMovementOptions(MovementOption::C_UP_TRICK))
            {
                return CUpTrick();
            }
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
    }

    BinaryStateBin<16> GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto trackedState = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());
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
            case StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR: phaseValue = 0; break;
            case StateTracker_BitfsDrRecover::Phase::QUICKTURN: phaseValue = 1; break;
            case StateTracker_BitfsDrRecover::Phase::C_UP_TRICK: phaseValue = 2; break;
            case StateTracker_BitfsDrRecover::Phase::INITIAL: phaseValue = 3; break;
            default: phaseValue = 4;
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

            float posRegionSize = trackedState.phase == StateTracker_BitfsDrRecover::Phase::C_UP_TRICK ? 0.25f
                : trackedState.phase == StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR ? 1.0f : 5.0f;
            state.AddRegionBitsByRegionSize(bitCursor, 12, xPosValue, xMin, xMax, posRegionSize);
            state.AddRegionBitsByRegionSize(bitCursor, 12, zPosValue, zMin, zMax, posRegionSize);
            //state.AddRegionBitsByRegionSize(bitCursor, 7, fSpeedValue, 0.f, 64.0f, 0.5f);

            state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.xzSum, 0.f, 0.8f, 0.005f);
            state.AddRegionBitsByRegionSize(bitCursor, 8, trackedState.pyraNormX, -0.7f, 0.7f, 0.01f);

            state.AddValueBits(bitCursor, 16, uint16_t(marioState->faceAngle[1]));
            //state.AddRegionBitsByRegionSize(bitCursor, 15, int(marioState->faceAngle[1]), -32768, 32767, 16);
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

        if (marioState->action == ACT_FREEFALL && marioState->vel[1] == 0)
            return false; //freefall without having done nut spot chain

        if (marioState->floorHeight > -3071 && marioState->pos[1] > marioState->floorHeight + 4)
            return false; //above pyra by over 4 units

        //if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL)
        //    return false; //diving/dring above lava

        // Double turnaround (ideally should prevent this in movement choices)
        if (marioState->action == ACT_WALKING && marioState->forwardVel < 0)
            return false;

        // Reject DR lands too far from the edge
        if (marioState->action == ACT_FREEFALL_LAND_STOP)
        {
            auto m64 = M64();
            auto status = TopLevelScriptBuilder<DetectEdge>::Build(m64)
                .ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
                .Run();

            if (!status.asserted || status.normalDistance >= 2.0f)
                return false;
        }

        // Check custom metrics
        float xNorm = pyramid->oTiltingPyramidNormalX;
        float zNorm = pyramid->oTiltingPyramidNormalZ;
        auto state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());
        auto lastFrameState = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame() - 1);

        if (state.initialized && state.phase == StateTracker_BitfsDrRecover::Phase::C_UP_TRICK
            && state.marioAction == ACT_FIRST_PERSON);

        // Reject departures from norm regime
        float normRegimeThreshold = 0.69f;
        if (fabs(xNorm) + fabs(zNorm) < normRegimeThreshold - 0.02f)
            return false;

        // Reject untimely turnarounds
        if (marioState->action == ACT_TURNING_AROUND)
            return false;

        return true;
    }

    float GetStateFitness()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());
        if (state.initialized)
        {
            switch (state.phase)
            {
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

        auto state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());

        int phaseValue;
        switch (state.phase)
        {
            case StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR: phaseValue = 0; break;
            case StateTracker_BitfsDrRecover::Phase::QUICKTURN: phaseValue = 1; break;
            case StateTracker_BitfsDrRecover::Phase::C_UP_TRICK: phaseValue = 2; break;
            case StateTracker_BitfsDrRecover::Phase::INITIAL: phaseValue = 3; break;
            default: phaseValue = 4;
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
        const auto& state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());
        const auto& prevState = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());

        switch (_lastPhase)
        {
            case StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR:
            {
                if (state.phase == StateTracker_BitfsDrRecover::Phase::C_UP_TRICK)
                    return true;

                break;
            }

            case StateTracker_BitfsDrRecover::Phase::C_UP_TRICK:
            {
                if (state.phase != StateTracker_BitfsDrRecover::Phase::C_UP_TRICK)
                    return false;

                if (marioState->action != ACT_FREEFALL_LAND && marioState->vel[1] == -4.0f)
                    return true;

                if (marioState->action == ACT_FREEFALL && prevState.marioAction == ACT_DECELERATING && marioState->vel[1] == -4.0f)
                    return true;

                break;
            }
        }

        return false;
    }

    Scattershot_BitfsDrRecover_Solution GetSolutionState() override
    {
        const auto& state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());

        auto solution = Scattershot_BitfsDrRecover_Solution();
        solution.fSpd = state.fSpd;
        solution.pyraNormX = state.pyraNormX;
        solution.pyraNormY = state.pyraNormY;
        solution.pyraNormZ = state.pyraNormZ;
        solution.xzSum = state.xzSum;

        return solution;
    }

private:
    StateTracker_BitfsDrRecover::Phase _lastPhase = StateTracker_BitfsDrRecover::Phase::C_UP_TRICK;

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

                auto state = GetTrackedState<StateTracker_BitfsDrRecover>(GetCurrentFrame());

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
                    return false;

                // Verify Mario is facing uphill (unlikely to fail)
                if (std::abs(marioState->faceAngle[1] - (status.floorAngle + 0x8000)) >= 0x4000)
                    return false;

                int16_t minAngle = status.floorAngle - 16385;
                int16_t maxAngle = status.floorAngle + 16385;
                float maxIntendedMag = (1.7f * status.steepness - 0.1f) / (1.0f - 1.0f / 43.0f);
                Inputs inputs = Inputs(0, 0, 0);
                while (inputs == Inputs(0, 0, 0))
                {
                    int16_t intendedYaw = GetTempRng() % (maxAngle - minAngle + 1);
                    float intendedMag = float(GetTempRng() % 65536) * maxIntendedMag / 65535.0f;
                    auto tempInputs = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
                    auto intendedInputs = Inputs::GetIntendedYawMagFromInput(tempInputs.first, tempInputs.second, camera->yaw);

                    if (intendedInputs.second == 0 || intendedInputs.second >= maxIntendedMag)
                        continue;

                    inputs = Inputs(C_UP, tempInputs.first, tempInputs.second);
                }

                AdvanceFrameWrite(inputs);
                AdvanceFrameWrite(Inputs(0, 0, 0));

                return true;
            }).executed;
    }
};
