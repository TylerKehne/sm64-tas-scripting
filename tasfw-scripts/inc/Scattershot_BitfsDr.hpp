#pragma once
#include <array>
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
    std::array<int, 3> incrementFrames = { -1, -1, -1 };
};

class NormalSpecsDto
{
public:
    bool onlyMinMajor = true;
    // The tilt (|nX| + |nZ|) from which the oscillation's regime holds, a floor the tilt may
    // not fall below once it has been there (startXzSum, minXzSum by default; the swings' ends
    // build the tilt from the rest's own up to it), the tilt a solution of the last pass must
    // have (minXzSum), and how far into the corner's quadrant each axis of the normal must
    // stay (minAxis: the fixer guarantees the rest's lattice steps reversibly from its
    // minNormal on, and short of it the search roams the platform; ROADMAP 4.6).
    float startXzSum = 0;
    float minXzSum = 0;
    // The forward speed at which the leg from the rest ends and the first swing begins (the
    // game's turnaround needs 16). The swing can cross only if it reverses within a frame or
    // two of the leg's end: an uphill frame, which costs about a unit of speed, then the
    // turnaround, which the game refuses under 16; a leg handed over at 16 cannot (the census
    // of 2026-10-04, ROADMAP 4.6).
    float legExitSpeed = 16.0f;
    // The least height of Mario over the lava at the leg's end for the leg to count as a
    // solution (0: none). A diagnostic: at 60, where the leg's ends run 55 to 65, three
    // deterministic seeds of four found no first oscillation at all and all four do
    // without it, so the leg's height is not what the first swing's crossing turns on
    // (2026-10-04, ROADMAP 4.6).
    float legMinClearance = 0;
    // The ceiling a solution's tilt may not exceed: past it the swing's ends sink under the
    // lava, so a root carried there cannot be continued (the maintainer, 2026-10-03; near
    // 0.73 from the committed rest, the sum climbing about 0.02 an oscillation; 2 asks none).
    float maxXzSum = 2.0f;
    float minAxis = 0;
    // The least height of Mario over the lava a state may have: lower, on a sinking end, the
    // surface is under him before the swing can leave it (the first swing's trap; 0 asks none).
    float minLavaClearance = 0;
    // How far (units) the first swing may fall back from the leg's end along the way to its
    // target, the chord's far end, before the state is refused: the leg ends on the sinking
    // end, downhill is toward it, and the search shoots every block alike, so a population
    // that runs on there fills the table, drowns, and starves the lineage that turns (the
    // maintainer, 2026-10-03). The turnaround itself overshoots a few frames' worth.
    float maxRetreat = INFINITY;
    // The old lineup's handover: when handoverXzSum lies above the rest's own tilt (which
    // the stage sets from the fixer's solution; 2 when unknown), the initial phase runs the
    // tilt up to handoverXzSum, with speed and leads, before the oscillation counts. 0 asks
    // none: the first leg from the rest.
    float handoverXzSum = 0;
    float restXzSum = 2.0f;
    float minMajor = 0;
    float maxMajor = 0;
    float regionsMajor = 0;
    float minMinor = 0;
    float maxMinor = 0;
    float regionsMinor = 0;
};

class BitfsDrMetrics : public Script<LibSm64>
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
        int64_t initialFrame = -1;
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
        float maxXzSum = 0; // the highest tilt since the rest (the climb below the regime may sit one shift under it)
        bool ranUphill = false; // the slope took speed this frame: Mario's floor angle against his facing, as apply_slope_accel decides it
        float legEndX = 0; // where the leg ended (the initial phase's exit): the first swing's progress toward its target is measured from here
        float legEndZ = 0;
        float legEndY = 0; // Mario's height there: what the first swing has over the lava is decided at the leg's end
        int16_t roughTargetAngle = 8192;
        Phase phase = Phase::INITIAL;
        bool facingRoughTargetAngle = false;
        std::array<float, 3> adjustedRemainderError = { INFINITY, INFINITY, INFINITY };
        std::array<int, 3> incrementFrames = { -1, -1, -1 };
        int64_t frame = -1;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    static bool ValidateCrossingData(const BitfsDrMetrics::CustomScriptStatus& state, float componentThreshold);

    // The corner a quadrant names, as the signs of the pyramid normal's x and z there (1: +x +z,
    // 2: +x -z, 3: -x -z, 4: -x +z, the convention of BitFsPyramidOscillation_RunDownhill), and
    // the two directions the tilt reverses between while Mario oscillates across that corner
    // (the chord across the quadrant, perpendicular to its diagonal).
    static int CornerSignX(int quadrant) { return quadrant < 3 ? 1 : -1; }
    static int CornerSignZ(int quadrant) { return quadrant == 1 || quadrant == 4 ? 1 : -1; }
    static int16_t RoughTargetAngleA(int quadrant) { return int16_t(-8192 + 16384 * (quadrant - 1)); }
    static int16_t RoughTargetAngleB(int quadrant) { return int16_t(24576 + 16384 * (quadrant - 1)); }

    BitfsDrMetrics() = default;
    BitfsDrMetrics(int64_t initialFrame, int quadrant, NormalSpecsDto normalSpecsDto, int minOscillationFrames, float targetNx, float targetNz, int platform = 84)
    {
        roughTargetAngleA = RoughTargetAngleA(quadrant);
        roughTargetAngleB = RoughTargetAngleB(quadrant);
        cornerSignX = CornerSignX(quadrant);
        cornerSignZ = CornerSignZ(quadrant);

        this->minOscillationFrames = minOscillationFrames;
        this->normalSpecsDto = normalSpecsDto;
        this->initialFrame = initialFrame;
        this->platform = platform;

        targetNormal = { targetNx, INFINITY, targetNz};
    }

    bool validation();
    bool execution();
    bool assertion();

private:
    int16_t roughTargetAngleA = -24576;
    int16_t roughTargetAngleB = 8192;
    int cornerSignX = -1;
    int cornerSignZ = 1;
    int minOscillationFrames = 15;
    NormalSpecsDto normalSpecsDto;
    int64_t initialFrame = 0;
    int platform = 84; // the pyramid's gObjectPool slot (BitFsObjects.hpp)
    std::array<float, 3> targetNormal = { INFINITY, INFINITY, INFINITY };

    void SetStateVariables(MarioState* marioState, Object* pyramid);
    void CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid);
    bool Handover() const { return normalSpecsDto.handoverXzSum > normalSpecsDto.restXzSum + 0.005f; } // the old lineup: the initial phase runs the tilt up to handoverXzSum first
    void CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid);
    void CalculateARE(Object* pyramid);
};

using Alias_ScattershotThread_BitfsDr = ScattershotThread<BinaryStateBin<16>, LibSm64, BitfsDrMetrics, Scattershot_BitfsDr_Solution>;
using Alias_Scattershot_BitfsDr = Scattershot<BinaryStateBin<16>, LibSm64, BitfsDrMetrics, Scattershot_BitfsDr_Solution>;

class Scattershot_BitfsDr : public Alias_ScattershotThread_BitfsDr
{
public:
    // This search's moves; the draw walks a list in this order.
    enum class CustomMoves { NO_SCRIPT, PBD, RUN_DOWNHILL, RUN_DOWNHILL_MIN, REWIND, TURN_UPHILL, RUN_FORWARD, TURN_AROUND, QUICKTURN, FIRST_LEG, LEAD_RUN };

    // How far from the pyramid's home the search lets Mario be, on each axis: the platform's
    // half-width plus what the tilt and a fall past the edge add.
    static constexpr float SearchHalfExtent = 500.0f;
    // The level's lava surface; the pyramid's low side dips into it as it tilts.
    static constexpr float LavaHeight = -3071.0f;

    // finalPass: this pass's solutions leave the stage, so they must have the regime's tilt
    // (minXzSum), not only the oscillation count. minFirstCrossingSpeed: Mario's forward speed
    // at the first crossing at least this (0: no floor), the speed the "gain speed each
    // crossing" rule ratchets up from.
    Scattershot_BitfsDr(Alias_Scattershot_BitfsDr& scattershot, int targetOscillation, NormalSpecsDto normalSpecsDto, int quadrant = 4, int platform = 84,
        bool finalPass = false, float minFirstCrossingSpeed = 0.0f)
        : Alias_ScattershotThread_BitfsDr(scattershot), _targetOscillation(targetOscillation), _normalSpecsDto(normalSpecsDto),
        _cornerSignX(BitfsDrMetrics::CornerSignX(quadrant)), _cornerSignZ(BitfsDrMetrics::CornerSignZ(quadrant)), _platform(platform), _finalPass(finalPass),
        _minFirstCrossingSpeed(minFirstCrossingSpeed) {}

    void SelectMovementOptions() override;
    bool ApplyMovement() override;
    BinaryStateBin<16> GetStateBin() override;
    bool ValidateState() override;
    float GetStateFitness() override;

    std::string GetCsvLabels() override;
    bool ForceAddToCsv() override;
    std::string GetCsvRow() override;
    bool IsSolution() override;
    Scattershot_BitfsDr_Solution GetSolutionState() override;

private:
    int _targetOscillation = -1;
    NormalSpecsDto _normalSpecsDto;
    int _cornerSignX = -1;
    int _cornerSignZ = 1;
    int _platform = 84;
    bool _finalPass = false;
    float _minFirstCrossingSpeed = 0.0f;

    Object* Pyramid();

    // The normal the pyramid tilts toward while Mario stands where he is, as
    // bhv_tilting_inverted_pyramid_loop computes it (tasfw-core/src/decomp/Pyramid.cpp): his
    // direction from the pyramid's home while he is on it, flat when he is not.
    static void PyramidGoal(const MarioState* marioState, const Object* pyramid, float& goalX, float& goalZ);

    // Whether the pyramid's next tilt will be a full step on both axes: the goal a full 0.01
    // from the normal on each axis, by approach_by_increment's own test (a step short of that
    // snaps the normal to the goal and changes the adjusted remainder error the search
    // conserves). With signs, the steps must also go the corner's way.
    static bool Steps(const MarioState* marioState, const Object* pyramid, int signX = 0, int signZ = 0);

    // One frame that keeps the pyramid stepping on both axes: the stick asked for, or the
    // nearest of a few variants of it (the yaw a step of 16 HAU at a time to either side, then
    // a released stick when that brakes) that does, each tried through the game and reverted
    // when it does not; false, with nothing written, when none does. Every frame the search
    // writes once Mario is on the pyramid goes through here, so a pellet never spends frames
    // on a path the conservation check rejects.
    bool StepFrame(int16_t intendedYaw, float intendedMag, uint16_t buttons = 0, Rotation bias = Rotation::NONE);
    bool StepInputs(Inputs inputs); // the same for a stick already chosen (the random inputs)

    bool Pbd();
    bool TurnUphill();
    bool RunForwardThenTurnAround();
    bool RunDownhillThenTurnUphill();
    bool TurnAroundThenRunDownhill();
    bool TurnAround();
    bool RunDownhill_1f(bool min = true);
    bool TurnUphill_1f();
    bool Quickturn();
    bool FirstLeg_1f();
    bool LeadRun_1f(bool toCorner = false); // toCorner: both leads outward, the lineup's run into the corner
    bool Handover() const { return _normalSpecsDto.handoverXzSum > _normalSpecsDto.restXzSum + 0.005f; } // the old lineup (NormalSpecsDto::handoverXzSum)
    static void Leads(const MarioState* marioState, const Object* pyramid, int dirX, int dirZ, float& leadX, float& leadZ); // the goal past the normal in the directions given
};
