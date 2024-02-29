#pragma once
#include <Scattershot.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <cmath>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

class Scattershot_BitfsDr_Solution
{
public:
    float fSpd = 0;
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    float xzSum = 0;
    int currentOscillation = 0;
    int16_t roughTargetAngle = 0;
};

class NormalSpecsDto
{
public:
    bool onlyMinMajor = true;
    float minXzSum = 0;
    float minMajor = 0;
    float maxMajor = 0;
    float regionsMajor = 0;
    float minMinor = 0;
    float maxMinor = 0;
    float regionsMinor = 0;
};

class StateTracker_BitfsDr : public Script<LibSm64>
{
public:
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
        float nZ = 0;
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

    static bool ValidateCrossingData(const StateTracker_BitfsDr::CustomScriptStatus& state, float componentThreshold);

    StateTracker_BitfsDr() = default;
    StateTracker_BitfsDr(int64_t initialFrame, int quadrant, NormalSpecsDto normalSpecsDto, int minOscillationFrames)
    {
        roughTargetAngleA = -8192 + 16384 * (quadrant - 1);
        roughTargetAngleB = 24576 + 16384 * (quadrant - 1);

        this->minOscillationFrames = minOscillationFrames;
        this->normalSpecsDto = normalSpecsDto;
        this->initialFrame = initialFrame;
    }

    bool validation();
    bool execution();
    bool assertion();

private:
    int16_t roughTargetAngleA = -24576;
    int16_t roughTargetAngleB = 8192;
    int minOscillationFrames = 15;
    NormalSpecsDto normalSpecsDto;
    int64_t initialFrame = 0;

    void SetStateVariables(MarioState* marioState, Object* pyramid);
    void CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid);
    void CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid);
};

using Alias_ScattershotThread_BitfsDr = ScattershotThread<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDr, Scattershot_BitfsDr_Solution>;
using Alias_Scattershot_BitfsDr = Scattershot<BinaryStateBin<16>, LibSm64, StateTracker_BitfsDr, Scattershot_BitfsDr_Solution>;

class Scattershot_BitfsDr : public Alias_ScattershotThread_BitfsDr
{
public:
    Scattershot_BitfsDr(Alias_Scattershot_BitfsDr& scattershot, int targetOscillation, NormalSpecsDto normalSpecsDto)
        : Alias_ScattershotThread_BitfsDr(scattershot), _targetOscillation(targetOscillation), _normalSpecsDto(normalSpecsDto) {}

    void SelectMovementOptions();
    bool ApplyMovement();
    BinaryStateBin<16> GetStateBin();
    bool ValidateState();
    float GetStateFitness();

    std::string GetCsvLabels() override;
    bool ForceAddToCsv() override;
    std::string GetCsvRow() override;
    bool IsSolution() override;
    Scattershot_BitfsDr_Solution GetSolutionState() override;

private:
    int _targetOscillation = -1;
    NormalSpecsDto _normalSpecsDto;

    bool Pbd();
    bool TurnUphill();
    bool RunForwardThenTurnAround();
    bool RunDownhillThenTurnUphill();
    bool TurnAroundThenRunDownhill();
    bool TurnAround();
    bool RunDownhill_1f(bool min = true);
    bool TurnUphill_1f();
    bool Quickturn();
};
