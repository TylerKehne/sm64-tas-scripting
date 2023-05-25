#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class BinaryStateBin
{
public:
    static const int nBytes = 16;
    std::uint8_t bytes[nBytes];

    BinaryStateBin()
    {
        for (int i = 0; i < nBytes; i++)
            bytes[i] = 0;
    }

    bool operator==(const BinaryStateBin&) const = default;

    void AddValueBits(uint8_t& bitCursor, uint8_t bitsToAllocate, uint64_t value)
    {
        if (bitCursor < 0 || bitCursor >= nBytes * 8)
            throw std::runtime_error("Bit cursor out of range.");

        uint64_t maxValue = uint64_t(1 << uint64_t(bitsToAllocate));
        if (value >= maxValue)
            throw std::runtime_error("Value too large for bits allocated.");

        AddBits(bitCursor, bitsToAllocate, value);
    }

    template <typename T>
        requires std::integral<T> || std::floating_point<T>
    void AddRegionBits(uint8_t& bitCursor, uint8_t bitsToAllocate, T value, T min, T max, uint64_t nRegions)
    {
        if (bitCursor < 0 || bitCursor >= nBytes * 8)
            throw std::runtime_error("Bit cursor out of range.");

        if (value < min || value > max)
            throw std::runtime_error("Value out of range.");

        if (nRegions == 0)
            throw std::runtime_error("Invalid number of regions.");

        uint64_t maxRegions = uint64_t(1 << uint64_t(bitsToAllocate));
        if (nRegions > maxRegions)
            nRegions = maxRegions;

        double regionSize = double(max - min) / double(nRegions);
        uint64_t region = double(value - min) / regionSize;
        if (region == nRegions) // Could happen if value is close to max?
            region--;

        // Sanity check
        if (region >= nRegions)
            throw std::runtime_error("Region out of range.");

        AddBits(bitCursor, bitsToAllocate, region);
    }

private:
    void AddBits(uint8_t& bitCursor, uint8_t bitsToAllocate, uint64_t value)
    {
        for (int bit = 0; bit < bitsToAllocate; bit++)
        {
            int trueBit = bitCursor + bit;
            int byteIndex = trueBit >> 3;
            int byteBit = trueBit % 8;

            uint64_t bitMask = uint64_t(1 << uint64_t(bit));
            if (bitMask & value)
                bytes[byteIndex] |= 1 << byteBit;
        }

        bitCursor += bitsToAllocate;
    }
};

class StateTracker_BitfsDr;

class StateTracker_BitfsDr : public Script<LibSm64>
{
public:
    class CustomScriptStatus
    {
    public:
        bool initialized = false;
        float marioX = 0;
        float marioY = 0;
        float marioZ = 0;
        float fSpd = 0;
        float pyraNormX = 0;
        float pyraNormY = 0;
        float pyraNormZ = 0;
        float xzSum = 0;
        std::set<int64_t> framesCrossedEqPoint;
        std::set<float> speedPassingEqPoint;
        int currentOscillation = 0;
        int currentCrossing = 0;
        bool reachedNormRegime = false;
        bool xzSumStartedIncreasing = false;
        int16_t roughTargetAngle = 16384; // TODO: config
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    StateTracker_BitfsDr() = default;

    bool validation() { return GetCurrentFrame() >= 3515; }

    bool execution() 
    {
        int minOscillationFrames = 15;
        float normRegimeThreshold = 0.69f;

        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        CustomStatus.marioX = marioState->pos[0];
        CustomStatus.marioY = marioState->pos[1];
        CustomStatus.marioZ = marioState->pos[2];
        CustomStatus.fSpd = marioState->forwardVel;
        CustomStatus.pyraNormX = pyramid->oTiltingPyramidNormalX;
        CustomStatus.pyraNormY = pyramid->oTiltingPyramidNormalY;
        CustomStatus.pyraNormZ = pyramid->oTiltingPyramidNormalZ;
        CustomStatus.xzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);
        CustomStatus.initialized = true;

        int64_t currentFrame = GetCurrentFrame();
        auto lastFrameState = GetTrackedState<StateTracker_BitfsDr>(currentFrame - 1);
        if (!lastFrameState.initialized)
            return true;

        CustomStatus.roughTargetAngle = lastFrameState.roughTargetAngle;
        int targetXDirection = sign(gSineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);
        int targetZDirection = sign(gCosineTable[(uint16_t)(CustomStatus.roughTargetAngle) >> 4]);

        int tiltDirection, targetTiltDirection;
        float normalDiff;
        if (fabs(pyramid->oTiltingPyramidNormalX) < fabs(pyramid->oTiltingPyramidNormalZ))
        {
            tiltDirection = sign(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
            targetTiltDirection = targetXDirection;
            normalDiff = fabs(CustomStatus.pyraNormX - lastFrameState.pyraNormX);
        }
        else
        {
            tiltDirection = Script::sign(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
            targetTiltDirection = targetZDirection;
            normalDiff = fabs(CustomStatus.pyraNormZ - lastFrameState.pyraNormZ);
        }

        if (CustomStatus.xzSum > normRegimeThreshold)
            CustomStatus.reachedNormRegime = lastFrameState.reachedNormRegime | true;

        if (CustomStatus.xzSum > lastFrameState.xzSum + 0.001f)
            CustomStatus.xzSumStartedIncreasing = lastFrameState.xzSumStartedIncreasing | true;

        CustomStatus.framesCrossedEqPoint = lastFrameState.framesCrossedEqPoint;
        CustomStatus.currentOscillation = lastFrameState.currentOscillation;
        CustomStatus.currentCrossing = lastFrameState.currentCrossing;
        CustomStatus.speedPassingEqPoint = lastFrameState.speedPassingEqPoint;
        if (tiltDirection == targetTiltDirection && normalDiff >= 0.0099999f)
        {
            if (CustomStatus.roughTargetAngle == 16384)
                CustomStatus.roughTargetAngle = -32768;
            else
                CustomStatus.roughTargetAngle = 16384;

            CustomStatus.framesCrossedEqPoint.insert(currentFrame);
            CustomStatus.speedPassingEqPoint.insert(CustomStatus.fSpd);
            CustomStatus.currentCrossing++;

            if (!lastFrameState.framesCrossedEqPoint.empty())
            {
                // Get number of frames since last crossing
                int64_t lastCrossing = *lastFrameState.framesCrossedEqPoint.rbegin();

                if (currentFrame - lastCrossing >= minOscillationFrames)
                    CustomStatus.currentOscillation++;
            }
        }

        return true;
    }

    bool assertion() { return CustomStatus.initialized == true; }
};

class Scattershot_BitfsDr : public ScattershotThread<BinaryStateBin, LibSm64, StateTracker_BitfsDr>
{
public:
    Scattershot_BitfsDr(Scattershot<BinaryStateBin, LibSm64, StateTracker_BitfsDr>& scattershot, int id)
        : ScattershotThread<BinaryStateBin, LibSm64, StateTracker_BitfsDr>(scattershot, id) {}

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
        AddMovementOption(MovementOption::NO_SCRIPT, state.reachedNormRegime ? 0.1 : 1.0);

        switch (marioState->action)
        {
            case ACT_FINISH_TURNING_AROUND:
                AddRandomMovementOption(
                    {
                        {MovementOption::PBD, 0},
                        {MovementOption::RUN_DOWNHILL, 3},
                        {MovementOption::TURN_UPHILL, 1},
                        {MovementOption::TURN_AROUND, 0}
                    });
                break;
            case ACT_TURNING_AROUND:
                AddRandomMovementOption(
                    {
                        {MovementOption::PBD, 0},
                        {MovementOption::RUN_DOWNHILL, 5},
                        {MovementOption::TURN_UPHILL, 5},
                        {MovementOption::TURN_AROUND, 15}
                    });
                break;
            case ACT_WALKING:
                AddRandomMovementOption(
                    {
                        {MovementOption::PBD, marioState->forwardVel >= 29.0f ? 5 : 0},
                        {MovementOption::RUN_DOWNHILL, 5},
                        {MovementOption::TURN_UPHILL, 5},
                        {MovementOption::TURN_AROUND, 20}
                    });
                break;
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

                    if (CheckMovementOptions(MovementOption::RUN_DOWNHILL) && RunDownhill())
                        return true;
                    else if (CheckMovementOptions(MovementOption::PBD) && Pbd())
                        return true;
                    else if (CheckMovementOptions(MovementOption::TURN_UPHILL) && TurnUphill())
                        return true;
                    else if (CheckMovementOptions(MovementOption::TURN_UPHILL) && TurnAround())
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

    BinaryStateBin GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto trackedState = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());
        float norm_regime_min = 0.69f;
        int nRegions = trackedState.xzSum >= norm_regime_min ? 16 : 4;
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
        
        float xMin = -2430.0f;
        float xMax = -1450.0f;
        float yMin = -3071.0f;
        float yMax = -2760.0f;
        float zMin = -1190.0f;
        float zMax = -200.0f;

        float xPosValue = marioState->pos[0] >= xMax ? xMax
            : marioState->pos[0] <= xMin ? xMin : marioState->pos[0];  

        float yPosValue = marioState->pos[1] >= yMax ? yMax
            : marioState->pos[1] <= yMin ? yMin : marioState->pos[1];

        float zPosValue = marioState->pos[2] >= zMax ? zMax
            : marioState->pos[2] <= zMin ? zMin : marioState->pos[2];

        float ySpeedValue = marioState->vel[1] > 32.0f ? 32.0f
            : marioState->vel[1] < 0 ? 0 : marioState->vel[1];

        float fSpeedValue = marioState->forwardVel > 64.0f ? 64.0f
            : marioState->forwardVel < 0 ? 0 : marioState->forwardVel;
        
        uint8_t bitCursor = 0;
        BinaryStateBin state;

        state.AddValueBits(bitCursor, 12, nRegions);
        state.AddValueBits(bitCursor, 11, actionValue);
        state.AddValueBits(bitCursor, 4, trackedState.currentCrossing > 15 ? 15 : trackedState.currentCrossing);
        state.AddRegionBits(bitCursor, 4, ySpeedValue, 0.f, 32.0f, 16);
        state.AddRegionBits(bitCursor, 8, fSpeedValue, 0.f, 64.0f, nRegions <= 4 ? 4 : nRegions);
        state.AddRegionBits(bitCursor, 14, int(marioState->faceAngle[1]), -32768, 32767, nRegions);
        state.AddRegionBits(bitCursor, 12, xPosValue, xMin, xMax, nRegions);
        state.AddRegionBits(bitCursor, 12, yPosValue, yMin, yMax, nRegions);
        state.AddRegionBits(bitCursor, 12, zPosValue, zMin, zMax, nRegions);

        //state.AddRegionBits(bitCursor, 12, pyramid->oTiltingPyramidNormalX, -0.7f, 0.7f, 2 * regionMultiplier);
        //state.AddRegionBits(bitCursor, 12, pyramid->oTiltingPyramidNormalY, 0.7f, 1.0f, 2 * regionMultiplier);
        //state.AddRegionBits(bitCursor, 12, pyramid->oTiltingPyramidNormalZ, -0.7f, 0.7f, 2 * regionMultiplier);

        //float xZSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);
        //state.AddRegionBits(bitCursor, 12, xZSum, 0.f, 0.8f, 2 * regionMultiplier);

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
            return false;//freefall without having done nut spot chain

        if (marioState->floorHeight > -3071 && marioState->pos[1] > marioState->floorHeight + 4 &&
            marioState->vel[1] != 22.0)
            return false;//above pyra by over 4 units

        if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL)
            return false; //diving/dring above lava

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

        //If outside of norm regime, force norm to increase
        if (state.xzSumStartedIncreasing && state.xzSum < lastFrameState.xzSum + 0.009f)
            return false;

        // Ensure we gain speed each crossing. Check both directions separately to account for axis asymmetry
        int crossings = state.speedPassingEqPoint.size();
        if (crossings > 2)
        {
            float lastCrossingSpeed = *state.speedPassingEqPoint.rbegin();
            if (lastCrossingSpeed <= *((state.speedPassingEqPoint.rbegin()++)++))
                return false;

            if (crossings > 3)
            {
                float prevCrossingSpeed = *(state.speedPassingEqPoint.rbegin()++);
                if (prevCrossingSpeed <= *(((state.speedPassingEqPoint.rbegin()++)++)++))
                    return false;
            }
        }

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

        return true;
    }

    float GetStateFitness()
    {
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        return pyramid->oTiltingPyramidNormalY;
    }

    std::string GetCsvLabels() override
    {
        return std::string("MarioX,MarioY,MarioZ,MarioFYaw,MarioFSpd,MarioAction,PlatNormX,PlatNormY,PlatNormZ,Oscillation,Crossing");
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
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

        char line[256];
        sprintf(line, "%f,%f,%f,%d,%f,%d,%f,%f,%f,%d,%d",
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
            state.currentCrossing);

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

                for (int i = 0; i < 8 && GetTempRng() % 2 < 1; i++)
                {
                    // Turn 2048 towrds uphill
                    auto m64 = M64();
                    auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                    auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                        ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, 0);
                    if (!status.validated)
                        return false;

                    int16_t uphillAngle = status.floorAngle + 0x8000;

                    //cap intended yaw diff at 2048
                    int16_t intendedYaw = uphillAngle;
                    if (abs(int16_t(uphillAngle - marioState->faceAngle[1])) >= 16384)
                        intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(uphillAngle - marioState->faceAngle[1]));

                    auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                    AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

                    if (marioState->faceAngle[1] == uphillAngle)
                        return true;
                }
                return true;
            }).executed;
    }

    bool RunDownhill()
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));
                Object* objectPool = (Object*)(resource->addr("gObjectPool"));
                Object* pyramid = &objectPool[84];

                if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_TURNING_AROUND && marioState->action != ACT_WALKING)
                    return false;

                for (int i = 0; i < 10 && GetTempRng() % 10 < 8; i++)
                {
                    auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                    // Turn 2048 towrds uphill
                    auto m64 = M64();
                    auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                    auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                        ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, state.roughTargetAngle, marioState->faceAngle[1]);
                    if (!status.validated)
                        return false;

                    // Attempt to run downhill with minimum angle
                    int16_t intendedYaw = marioState->action == ACT_TURNING_AROUND ? status.angleFacingAnalogBack : status.angleFacing;
                    auto stick = Inputs::GetClosestInputByYawExact(
                        intendedYaw, 32, camera->yaw, status.downhillRotation);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                    // This is actually a failure, but reverting here would introduce an extra load.
                    // Allow scattershot validation to handle it instead.
                    if (marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING)
                        return true;
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
                    return false;

                for (int i = 0; i < 10 && GetTempRng() % 10 < 9; i++)
                {
                    auto state = GetTrackedState<StateTracker_BitfsDr>(GetCurrentFrame());

                    // Turn 2048 towrds uphill
                    auto m64 = M64();
                    auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
                    auto status = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle
                        ::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, state.roughTargetAngle, marioState->faceAngle[1]);
                    if (!status.validated)
                        return false;

                    // Attempt to run downhill with minimum angle
                    int16_t intendedYaw = status.angleFacingAnalogBack;
                    auto stick = Inputs::GetClosestInputByYawExact(
                        intendedYaw, 32, camera->yaw, status.downhillRotation);
                    AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

                    if (marioState->action != ACT_TURNING_AROUND)
                        return true;
                }

                return true;
            }).executed;
    }
};
