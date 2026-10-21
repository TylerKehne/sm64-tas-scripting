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
    // The speed the minimum-downhill run from the crossing reaches, probed there in a sandbox
    // and rolled back (BitfsDrMetrics; the maintainer's original design): the speed the roots
    // and hand-overs are ranked by, the crossing itself being what is persisted.
    float downhillPeak = 0;
};

// The weights of the swing's moves after a crossing while the run is downhill, the draw's
// odds (the config's "moves" of a dr-oscillations stage; the maintainer, 2026-10-19: which
// weights give the most squish cancels in the least time is a measured question). The first
// swing from the leg and the uphill-turn phase keep their own weights (SelectMovementOptions).
class BitfsDrMoveWeights
{
public:
    int leadRun = 9;    // LeadRun_1f: the stick of a fan around the facing that leaves the thinner lead largest
    int probeRun = 4;   // ProbeRun_1f: one frame downhill at the minimum angle from the facing, the probe's run
    int turnUphill = 1; // TurnUphill_1f, from over the turnaround's 16 of speed
    int turnAround = 1; // TurnAround, from a frame the slope took speed on
};

// Where the "gain speed each crossing" rule compares a crossing with the one two before it
// (the config's speedRule; the maintainer, 2026-10-19): nowhere, in the regime only (both
// crossings in it), or below the regime and in it alike (both below or both in it), never
// across the regime's edge.
enum class SpeedRule { OFF, REGIME, EVERYWHERE };
// Which of the rule's two comparisons apply (the config's speedGates; the maintainer,
// 2026-10-19): both, the crossing's speed against the earlier crossing's and the probed
// downhill run's peak against the peak that earlier swing reached; the run only; or the
// crossing only.
enum class SpeedGates { BOTH, RUN, CROSSING };

class NormalSpecsDto
{
public:
    bool onlyMinMajor = true;
    // The tilt (|nX| + |nZ|) from which the oscillation's regime holds, a floor the tilt may
    // not fall below once it has been there (startXzSum, minXzSum by default; the swings' ends
    // build the tilt from the rest's own up to it), the tilt a solution of the last pass must
    // have at its crossing (minXzSum), and how far into the corner's quadrant each axis of the normal must
    // stay (minAxis: the fixer guarantees the rest's lattice steps reversibly from its
    // minNormal on, and short of it the search roams the platform; ROADMAP 4.6).
    float startXzSum = 0;
    float minXzSum = 0;
    // The oscillation the regime begins at (the maintainer, 2026-10-17): before it a lineage
    // is outside the regime whatever its tilt, under the climb rule and the sub-regime bins,
    // its crossings compared among themselves; from it the tilt over startXzSum is the
    // regime, with its floor. 0 asks none (the regime from the rest on). Counted from the
    // rest, as the metric script counts.
    int minOscillations = 0;
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
    // The tilt a solution of the last pass, a hand-over to the next stage, may not exceed
    // (2 asks none): a ceiling on the hand-overs' tilt, so that they are alike in tilt (the
    // maintainer, 2026-10-17). maxXzSum above exempts the last pass; this is its own.
    float maxExitXzSum = 2.0f;
    // The least height above the lava a turnaround begun from a solution's frame would end
    // at (BitfsDrMetrics::TurnaroundClearance), for a solution of a pass with a pass after it
    // to be a root the next pass can continue from (0 asks none). The precise form of
    // maxXzSum: what fails at a sinking end is the floor under the turnaround, not the sum
    // (the maintainer's run of 2026-10-06 lost its third pass to roots at -20 to +3 here,
    // every lineage ending at the end 0 to 10 above the lava; the pass that succeeded turned
    // 23 to 75 above; ROADMAP 4.6).
    float minEndClearance = 0;
    // The least difference between |nX| and |nZ| at a crossing, from the second crossing on
    // (0: none). The maintainer's rule of 2026-10-10 (ROADMAP 4.12): at the example movies'
    // crossings the normal sits 0.27 to 0.31 further along one axis than the other, the lag
    // its speed comes from; ours sat 0.13 to 0.19 after the turn at the +z end.
    float minCrossingAxisDiff = 0;
    // Where the speed rule compares crossings (SpeedRule above). REGIME, the committed
    // setting, from 2026-10-18; EVERYWHERE was the rule's first form (2026-10-10) and gave
    // fewer squish cancels when tried again (ROADMAP 4.12).
    SpeedRule speedRule = SpeedRule::REGIME;
    // Which of the speed rule's two comparisons apply (SpeedGates above): both, the
    // committed setting, the downhill run only, or the crossing speed only.
    SpeedGates speedGates = SpeedGates::BOTH;
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

class BitfsDrMetrics : public Script<Sm64Resource>
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
        bool inRegime = false; // the regime's tilt had been reached by this crossing
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

    // The crossing rules: gain speed on the crossing two before where speedRule compares the
    // two (SpeedRuleCompares), in the comparisons speedGates asks; the normal's components at
    // least componentThreshold on one axis and minCrossingAxisDiff apart.
    static bool ValidateCrossingData(const BitfsDrMetrics::CustomScriptStatus& state, float componentThreshold, float minCrossingAxisDiff, SpeedRule speedRule,
        SpeedGates speedGates = SpeedGates::BOTH);
    // Whether the speed rule compares a crossing with the one two before it, from where each
    // is: in the regime or below it.
    static bool SpeedRuleCompares(SpeedRule speedRule, bool inRegime, bool earlierInRegime);
    static const char* PhaseName(Phase phase); // the enumerator's name, what the CSV holds

    // The corner a quadrant names, as the signs of the pyramid normal's x and z there (1: +x +z,
    // 2: +x -z, 3: -x -z, 4: -x +z, the convention of BitFsPyramidOscillation_RunDownhill), and
    // the two directions the tilt reverses between while Mario oscillates across that corner
    // (the chord across the quadrant, perpendicular to its diagonal).
    static int CornerSignX(int quadrant) { return quadrant < 3 ? 1 : -1; }
    static int CornerSignZ(int quadrant) { return quadrant == 1 || quadrant == 4 ? 1 : -1; }
    static int16_t RoughTargetAngleA(int quadrant) { return int16_t(-8192 + 16384 * (quadrant - 1)); }
    static int16_t RoughTargetAngleB(int quadrant) { return int16_t(24576 + 16384 * (quadrant - 1)); }

    // The height above the lava Mario's floor would have at the end of a turnaround begun
    // now: he runs on to a stop at the game's turnaround deceleration (about 4.8 a frame),
    // the pyramid steps its normal toward him meanwhile (approach_by_increment, 0.01 an axis
    // a frame), and the face's height there comes from the collision data (a square 307 wide
    // either way, 307 above the pyramid's position, perpendicular to the normal made unit as
    // the game's transform makes it). Negative: the floor is under the lava before he has
    // stopped, and on a sinking end no later turn does better. Game-free, so a test can hold
    // it to recorded hand-overs (test_bitfs_dr_rules.cpp).
    static float StepToward(float goal, float value, float increment); // the pyramid's 0.01 step
    static float TurnaroundClearance(float normalX, float normalY, float normalZ,
        float marioX, float marioZ, int16_t faceYaw, float speed, float pyramidX, float pyramidY, float pyramidZ);

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
    void CalculateOscillations(CustomScriptStatus lastFrameState, MarioState* marioState);
    bool Handover() const { return normalSpecsDto.handoverXzSum > normalSpecsDto.restXzSum + 0.005f; } // the old lineup: the initial phase runs the tilt up to handoverXzSum first
    bool RegimeAt(float xzSum, int oscillation) const { return xzSum > normalSpecsDto.startXzSum && oscillation >= normalSpecsDto.minOscillations; } // the regime: the tilt over startXzSum from the oscillation minOscillations on
    void CalculatePhase(CustomScriptStatus lastFrameState, MarioState* marioState, Object* pyramid);
    void CalculateARE(Object* pyramid);
};

using Alias_ScattershotThread_BitfsDr = ScattershotThread<BinaryStateBin<16>, Sm64Resource, BitfsDrMetrics, Scattershot_BitfsDr_Solution>;
using Alias_Scattershot_BitfsDr = Scattershot<BinaryStateBin<16>, Sm64Resource, BitfsDrMetrics, Scattershot_BitfsDr_Solution>;

class Scattershot_BitfsDr : public Alias_ScattershotThread_BitfsDr
{
public:
    // This search's moves; the draw walks a list in this order.
    enum class CustomMoves { NO_SCRIPT, PBD, RUN_DOWNHILL, RUN_DOWNHILL_MIN, REWIND, TURN_UPHILL, RUN_FORWARD, TURN_AROUND, QUICKTURN, FIRST_LEG, LEAD_RUN, PROBE_RUN };

    // How far from the pyramid's home the search lets Mario be, on each axis: the platform's
    // half-width plus what the tilt and a fall past the edge add.
    static constexpr float SearchHalfExtent = 500.0f;
    // The level's lava surface; the pyramid's low side dips into it as it tilts.
    static constexpr float LavaHeight = -3071.0f;

    // The first crossing's direction as its roughTargetAngle, the chord end the normal steps
    // toward after it and the end Mario came from: the leg's own direction, which the rest's
    // normal fixes (LegDirection), so the stage knows every pass's direction at its start.
    // CrossingAngle: the roughTargetAngle after the crossing that reached an oscillation, the
    // first's at the odd ones and the chord's other end at the even ones (the maintainer,
    // 2026-10-17: one direction per oscillation, every pass holding to it; ValidateState).
    static int16_t FirstCrossingAngle(float restNormalX, float restNormalZ, int quadrant);
    static int16_t CrossingAngle(int16_t firstCrossingAngle, int oscillation, int quadrant);

    // Whether a crossing count is one a pass with this target makes (the maintainer's root
    // mixture, 2026-10-20): the target's parity, at or under it. A lineage ends at its first
    // crossing, so no block holds a crossing beyond its root's, and a pass's roots all have
    // the parity before the target's (the pass before's, or a rewind's aligned pools); a
    // count of the target's parity is therefore always a crossing made in this pass, from a
    // root at the target's oscillation less one or, filled in from an earlier aligned pool,
    // less three or five, which crosses once like the rest. The crossing rules judge these
    // crossings only: an input's crossings passed their own stage's rules, which may differ
    // (the maintainer, 2026-10-18).
    static bool PassCrossing(int oscillation, int targetOscillation);

    // A pass's solution is the crossing that reached its oscillation (IsSolution; the
    // maintainer, 2026-10-17). finalPass: this pass's solutions leave the stage, so that
    // crossing must have the regime's tilt (minXzSum), not only the oscillation count.
    // minFirstCrossingSpeed: Mario's forward speed
    // at the first crossing at least this (0: no floor), the speed the "gain speed each
    // crossing" rule ratchets up from.
    Scattershot_BitfsDr(Alias_Scattershot_BitfsDr& scattershot, int targetOscillation, NormalSpecsDto normalSpecsDto, int quadrant = 4, int platform = 84,
        bool finalPass = false, float minFirstCrossingSpeed = 0.0f, BitfsDrMoveWeights moves = BitfsDrMoveWeights())
        : Alias_ScattershotThread_BitfsDr(scattershot), _targetOscillation(targetOscillation), _normalSpecsDto(normalSpecsDto),
        _cornerSignX(BitfsDrMetrics::CornerSignX(quadrant)), _cornerSignZ(BitfsDrMetrics::CornerSignZ(quadrant)), _quadrant(quadrant), _platform(platform), _finalPass(finalPass),
        _minFirstCrossingSpeed(minFirstCrossingSpeed), _moves(moves) {}

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
    int _quadrant = 4;
    int _platform = 84;
    bool _finalPass = false;
    float _minFirstCrossingSpeed = 0.0f;
    BitfsDrMoveWeights _moves; // the swing's moves after a crossing, by weight

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
    bool ProbeRun_1f(); // one frame downhill at the minimum angle from the facing: the frame the metric's probe plays from a crossing, so the search can make the run the probe measured (the maintainer, 2026-10-19)
    bool Handover() const { return _normalSpecsDto.handoverXzSum > _normalSpecsDto.restXzSum + 0.005f; } // the old lineup (NormalSpecsDto::handoverXzSum)
    static void Leads(const MarioState* marioState, const Object* pyramid, int dirX, int dirZ, float& leadX, float& leadZ); // the goal past the normal in the directions given
    static void LegDirection(float restNormalX, float restNormalZ, int cornerSignX, int cornerSignZ, int& signX, int& signZ); // the leg's way along the chord: the far side of the corner on the rest's steeper axis
};
