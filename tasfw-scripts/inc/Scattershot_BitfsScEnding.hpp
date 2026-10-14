#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <Scattershot.hpp>
#include <ScriptMath.hpp>
#include <BitFSPyramidOscillation.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Trig.hpp>

// The ending of the squish-cancel setup (ROADMAP 4.11), one search from the oscillation
// stage's last crossing: the final run along the edge into the target corner, the approach
// (a dive recover steered inside the band where the pyramid is still Mario's platform, or a
// run braked to a stop), the leave over the squish edge, the far edge of that corner (a
// slow walk off, a run off parallel to the edge, a quickturn to idle, a C-up deceleration
// onto a NUT spot, a wait for the displacement), and the air after it (straining back over
// the platform, landing on the edge again). The squish spot is past that edge (the
// maintainer, 2026-10-06), the high side at the target's tilt, so the leave is a fall. It does
// not optimize any one of these: every option is a move the config weights, a solution
// records which ones it took, and the only judgment is the maintainer's two rules
// (2026-10-05, the target's frame corrected 2026-10-06). After the departure, the first
// frame at whose end the pyramid is not Mario's platform, a bully hacked next to Mario in
// its knockback action, the game played one frame, must leave Mario idle with the
// knockback's speed and well above the lava, the pyramid's normal the target's two floats
// exactly on that frame: the squish cancel. The hack is the metric script's probe, inside
// its sandbox (AGENTS.md, hard rule 1's exception).
//
// What the game makes of the fall (ROADMAP 4.11, 2026-10-10, read in the decompilation and
// measured on the maintainer's movies): Mario falls beside the squish edge, a few units
// outside it, while the untilting face sweeps outward 3 to 4 a frame; the squish is the
// frame whose new edge first covers his position at its start, with the top more than 78
// above him (so his floor is the lava) and the side face a ceiling within 150 of it. The
// cancel is in act_squished: the bully's push has set him 115 along their line and given
// him the bully's speed four times over, the airborne action cancels into the squish
// before any knockback step, and the squish, under a ceiling whose normal y is above
// -0.5, walks him 10 along the ceiling's horizontal normal in four ground quarter steps,
// one snapping him up onto the top where the push landed him, a later one stepping off
// it over 100 above the lava, which un-squishes him to idle with the speed intact. So
// the push lands him downhill along the edge, on the top where it stands over 100 and
// within 78 above him, a few units inside the edge: the metric's probe places the bully
// for exactly that, and the search's rules keep the states that can get there.
class BitfsScEndingArgs
{
public:
    int64_t initialFrame = 0; // the fixer's hand-over: the adjusted remainder error conserved is the one a frame after it, as the oscillation stage conserves it
    int oscQuadrant = 4;      // the corner the oscillations ran in, as the signs of nX, nZ (1 +x+z, 2 +x-z, 3 -x-z, 4 -x+z); the stage derives it from targetQuadrant and targetEdge
    int targetQuadrant = 1;   // the adjacent corner the final run goes toward, along the edge the two share
    int platform = 84;        // the pyramid's gObjectPool slot (BitFsObjects.hpp)
    // The normal the squish cancel needs, exact on the squish cancel frame, the one the probe
    // reads; or none (targetGiven false: the config without targetNx and targetNz), when the
    // error is conserved against the hand-over's own normal, the lattice the fixer set, and
    // a squish cancel at any normal on it counts, the solution recording which (the
    // maintainer, 2026-10-06: which normals the ending reaches, before one is chosen).
    bool targetGiven = true;
    float targetNx = 0;
    float targetNz = 0;
    // The squish edge: the side of the platform the squish spot is past, as the outward unit
    // vector along one axis (+x: squishEdgeX 1, squishEdgeZ 0). The config names it by the
    // axis the ledge runs along, targetEdge "x" or "z" (the maintainer's uscx and uscz), and
    // the stage derives the outward side from the target corner's sign on the other axis and
    // the oscillation corner as the one across the run's edge (the target corner's other
    // edge). A departure anywhere else is refused, and the piped-in solutions' last crossing
    // must run toward it.
    int squishEdgeX = 1;
    int squishEdgeZ = 0;
    // How far from the pyramid's home toward the edge the run follows Mario must stay, in
    // units along that direction: 0 keeps him on the edge's half of the platform, a
    // negative value lets him cross it. The first run (2026-10-06) spent blocks on lineages
    // that ran across the platform, with no squish cancel anywhere near them.
    float edgeSide = 0.0f;
    // The most a departed state may be moving off the squish edge, in units a frame (0 asks
    // none). The squish needs Mario on the lava just inside the edge, which recedes about 3 a
    // frame and widens as much as the tilt flattens over the fall, so his outward drift must
    // stay within a few units a frame: the maintainer's examples leave at 6.5 and 7.2. The
    // first run without a target left at 25 to 30 and landed 100 to 250 units out, 396,000
    // rows the probe could only knock about in the air (2026-10-06); air strain takes about
    // a unit a frame off, so a state past the cap is not coming back.
    float maxLeaveSpeed = 12.0f;

    // The squish probe.
    int bully = 27;                    // the bully's gObjectPool slot (BitFsObjects.hpp: the lowest of the level's four); -1 probes nothing, so no solution is ever found
    float bullySpeed = 100.0f;         // the knockback speed the hacked bully carries along the push: the knockback hands it to Mario about four times over (the maintainer's example at 50 left him at 206), which is what the test below reads
    int probeFrames = 1;               // frames played after the hack before the state is read
    int probeWindow = 10;              // frames after the departure in which the probe runs; a state past it is refused
    float squishMinSpeed = 50.0f;      // the squish cancel: Mario idle after the probe, at least this speed either way (the knockback can leave him moving backward),
    float squishMinClearance = 100.0f; // and at least this far above the lava

    bool pauseBuffer = true; // the dive recover's pause between the dive and its rollout (PBDR): a dive that lands on its own frame needs it, since the rollout's B must be a press and the dive's B is still down

    // The moves' weights in the draw; 0 leaves an option out.
    double weightDive = 1;       // the dive recover
    double weightBrake = 1;      // holding back and releasing the stick
    double weightWalkOff = 1;    // a slow walk off the edge
    double weightParallel = 1;   // a run off parallel to the edge
    double weightQuickturn = 1;  // the one-frame walk out of a landing stop into idle
    double weightCUp = 1;        // C-up with a small stick: a deceleration under one unit of speed
    double weightWait = 1;       // a neutral frame standing: the displacement alone (a NUT spot)
    double weightStrainBack = 1; // in the air after the departure, the stick back over the platform
};

class BitfsScEndingMetrics : public Script<Sm64Resource>
{
public:
    // SWING: the final run toward the target corner. DIVE: from the dive to its rollout's
    // landing, the flight over the platform included where it rises out of the band (the
    // maintainer, 2026-10-12; past the platform's edge it is a departure like any). BRAKE:
    // from the first braking frame; back to SWING if Mario accelerates again
    // (the oscillation stage's own turnarounds look like a brake until they do). EDGE: stopped
    // on the platform after an approach. AIR: from the departure on, whatever Mario does.
    enum class Phase { SWING, DIVE, BRAKE, EDGE, AIR };
    enum class Approach { NONE, DIVE_RECOVER, BRAKE };
    static const char* PhaseName(Phase phase);          // the enumerator's name, what the CSV holds
    static const char* ApproachName(Approach approach);

    class CustomScriptStatus
    {
    public:
        bool initialized = false;
        int64_t frame = -1;
        float marioX = 0;
        float marioY = 0;
        float marioZ = 0;
        uint32_t marioAction = 0;
        float fSpd = 0;
        int16_t faceYaw = 0;
        float pyraNormX = 0;
        float pyraNormY = 0;
        float pyraNormZ = 0;
        float xzSum = 0;
        bool onPlatform = false; // the pyramid is Mario's platform at this frame's end
        std::array<float, 3> adjustedRemainderError = { INFINITY, INFINITY, INFINITY };
        std::array<int, 3> incrementFrames = { -1, -1, -1 }; // 0.01 steps from the normal to the target on each axis, signed
        float targetNx = 0; // the normal the error and the steps are measured against: the config's, or without one the hand-over's, the frame after initialFrame
        float targetNz = 0;
        float edgeDistance = -1; // Mario's distance to the top's nearest outer edge (BRAKE and EDGE frames on the platform; -1 elsewhere)

        Phase phase = Phase::SWING;
        Approach approach = Approach::NONE;
        bool walkedFromStop = false; // this frame: walking out of a landing or braking stop at zero speed
        bool quickturn = false;      // such a walk went to idle on the next frame

        // The departure: the first frame at whose end the pyramid is not Mario's platform.
        bool departed = false;
        int64_t departureFrame = -1;
        uint32_t leaveAction = 0;      // Mario on the frame before it
        float leaveSpeed = 0;
        int16_t leaveYaw = 0;
        bool leaveCUp = false;         // C-up held on the input that produced the departure frame
        float leaveEdgeDistance = -1;
        bool leaveAtSquishEdge = false; // the edge nearest him at the departure is the squish edge and he is past it
        int strainBackFrames = 0;      // frames after it with the pyramid his platform again while airborne
        int nutLandings = 0;           // landings on the platform after it

        // The catch (AIR, within the window): CatchPlacements on this frame, -2 where it was not
        // looked at (on the platform, past the window), -1 no squish next frame, 0 a pancake,
        // else the pushes that cancel it; and the squish probe, on the frames with one.
        int catchPlacements = -2;
        bool probed = false;
        bool squishCancel = false;
        int probeIndex = -1;     // the placement that succeeded, else the one whose knockback took (Mario's invincibility timer set), else the fastest
        float probeDx = 0;       // the bully's offset from Mario in that placement
        float probeDz = 0;
        uint32_t probeAction = 0; // Mario a frame after it
        float probeSpeed = 0;
        float probeHeight = 0;
        float probeNormalX = 0;   // the pyramid's normal on that frame
        float probeNormalZ = 0;
        bool probeTargetMet = false; // and whether it was the target's two floats
        int probeCollisions = 0;  // objects the game found overlapping Mario on that frame
        bool probeBullyHit = false; // the bully among them
        int16_t probeInvincTimer = 0; // Mario's invincibility timer after it: the knockback sets it
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    static constexpr float LavaHeight = -3071.0f; // the level's lava surface

    // The squish and its cancel in the decompilation's constants: a dynamic floor or ceiling
    // with the ceiling within SquishSpace of the floor sets INPUT_SQUISHED
    // (update_mario_geometry_inputs); find_floor takes a floor up to FloorReach above Mario,
    // so under the top by more than that his floor is the lava and the side face a ceiling;
    // the bully's push sets him PushPadding past the two hitbox radii
    // (push_mario_out_of_object); act_squished, under a ceiling whose normal y is above
    // SteepCeiling, walks him SquishWalk along its horizontal normal in four ground quarter
    // steps, and a quarter step over a floor more than LeaveGroundHeight below him leaves the
    // ground (perform_ground_quarter_step), the instant un-squish to idle with the
    // knockback's speed intact.
    static constexpr float SquishSpace = 150.0f;
    static constexpr float FloorReach = 78.0f;
    static constexpr float PushPadding = 5.0f;
    static constexpr float SteepCeiling = -0.5f;
    static constexpr float SquishWalk = 10.0f;
    static constexpr float LeaveGroundHeight = 100.0f;
    // The slack the catch and its window are judged with, so that the game's own rounding
    // (the face's vertices are shorts, truncated toward zero, and its normals come from
    // them) never refuses a cancel the game would make (the maintainer, 2026-10-11: compute
    // the bound and allow a little less), each the geometry's measured error against the
    // game and no more: the heights within 3 (a side's within 2.5), the edges within 1,
    // the side's normal within 0.003 (a slack of 0.01 on it let catches at -0.508 through,
    // 99 pancakes of a 50,000-shot run's 265 catches; the edges' 3 fired the probe a frame
    // early on 165 more).
    static constexpr float CatchSlack = 3.0f;
    static constexpr float DepthSlack = 1.0f;
    static constexpr float SteepSlack = 0.003f;

    // The pyramid's top face as the game places it (bhv_tilting_inverted_pyramid_loop,
    // mtxf_align_terrain_normal with yaw 0; measured against the DLL's own find_floor and
    // find_ceil on the maintainer's movies, within a unit: ROADMAP 4.11, 2026-10-10). The
    // square (+-307, 307, +-307) of the object's frame, the apex at its origin; the frame's
    // up is the normalized normal, all three stored components (nX and nZ alone are not
    // the tilt), its left (uy, -ux, 0) / s with s the length of (ux, uy), its forward
    // (-ux uz, -uy uz, s^2) / s. The x-aligned edges lie at a constant z; the z-aligned ones
    // are skewed, their x moving with z. test_bitfs_ending_geometry.cpp pins it.
    class Face
    {
    public:
        static constexpr float HalfWidth = 307.0f; // the top's half-width, and its height in the frame
        static Face Of(float normalX, float normalY, float normalZ, float homeX, float homeY, float homeZ);
        static Face Of(const Object* pyramid);

        // A point's coordinate from the home along the outward unit of a side (outX, outZ:
        // one of them +-1, the other 0), and the coordinate that side's edge has there, at
        // the point's position along the edge: the point is outside the top by the difference.
        float Out(int outX, int outZ, float x, float z) const;
        float EdgeOut(int outX, int outZ, float x, float z) const;
        // The end of that edge toward the unit (alongX, alongZ) along it, as the coordinate
        // along it from the home.
        float CornerAlong(int outX, int outZ, float alongX, float alongZ) const;
        float TopHeight(float x, float z) const;                      // the top's plane at a point
        float SideHeight(int outX, int outZ, float x, float z) const; // the side's plane under that edge at a point
        float SideNormalY(int outX, int outZ) const;                  // that side's normal, its y
        void SideNormalDirection(int outX, int outZ, float& dirX, float& dirZ) const; // and the unit direction of its horizontal part
        float Rise(float dirX, float dirZ) const;                     // the top's rise per unit moved along a horizontal unit direction (negative downhill)

        float ux = 0, uy = 1, uz = 0, s = 1;
        float homeX = 0, homeY = 0, homeZ = 0;
    };
    // The face a frame on while the pyramid is not Mario's platform: each stored component
    // approaches flat by 0.01 (approach_by_increment).
    static Face NextFace(const Object* pyramid);

    // One push of the probe: the bully's placement, as the unit direction of the push and the
    // fraction of the push-out distance the bully stands behind Mario against it, and the
    // top's height above the lava at the spot the push lands him on.
    struct Placement
    {
        float dirX = 0;
        float dirZ = 0;
        float fraction = 0;
        float spotHeight = 0;
    };
    // The catch: whether the next frame's face, `next`, squishes Mario where he is (its edge
    // covering him, the top more than FloorReach above him, the side within SquishSpace of
    // the lava); -1 when it does not. When it does, the pushes that cancel the squish, by
    // the game's own steps: each lands him on the top where it stands over LeaveGroundHeight
    // and within FloorReach above him, short of the top's end, downhill along the edge at
    // the probe's three distances (the fractions 0.3, 0.4 and 0.5 of `minDistance`, the
    // push-out distance), and inside the edge by half the squished walk, so that a quarter
    // step snaps him up onto the top and a later one steps off it; and the side must be
    // steep. Their count: 0 is a squish no push cancels, a pancake. Every bound carries its
    // slack (DepthSlack on the edge, CatchSlack on the heights, SteepSlack on the side's
    // normal), the catch's own included.
    static int CatchPlacements(const Face& next, int outX, int outZ, float x, float y, float z, float minDistance, std::array<Placement, 3>& placements);

    // The corner a quadrant names, as the signs of the normal's x and z there (the
    // convention of BitfsDrMetrics), and the direction of the run from the oscillation
    // corner to the target corner along the edge they share.
    static int CornerSignX(int quadrant) { return quadrant < 3 ? 1 : -1; }
    static int CornerSignZ(int quadrant) { return quadrant == 1 || quadrant == 4 ? 1 : -1; }
    static int16_t RoughTargetAngle(int oscQuadrant, int targetQuadrant);

    BitfsScEndingMetrics() = default;
    BitfsScEndingMetrics(const BitfsScEndingArgs& args);

    bool validation();
    bool execution();
    bool assertion();

private:
    BitfsScEndingArgs _args;
    int16_t _roughTargetAngle = 0;
    float _edgeX = 1.0f; // unit vector along the edge, in the run's direction
    float _edgeZ = 0.0f;
    float _outX = 0.0f;  // unit vector off the edge, away from the platform
    float _outZ = 1.0f;
    float _squishOutX = 1.0f;   // unit vector off the squish edge, away from the platform
    float _squishOutZ = 0.0f;
    float _squishAlongX = 0.0f; // unit vector along the squish edge (either way; the probe takes its lower end)
    float _squishAlongZ = 1.0f;

    // The decomp's object_constants.h: the bully's oAction values (bhv_bully_loop).
    static constexpr int32_t BullyActKnockback = 2;

    struct ProbeResult
    {
        uint32_t action = 0;
        float speed = 0;
        float height = 0;
        int collisions = 0;
        bool bullyHit = false;
        int16_t invincTimer = 0;
        float normalX = 0;
        float normalZ = 0;
    };

    Object* Pyramid();
    void SetStateVariables(MarioState* marioState, Object* pyramid);
    void CalculateARE(Object* pyramid, float targetNx, float targetNz);
    void CalculatePhase(const CustomScriptStatus& lastFrameState, MarioState* marioState, Object* pyramid);
    // Mario's distance inside the top's nearest outer edge, by the face (negative outside it).
    float EdgeDistance(const MarioState* marioState, const Object* pyramid);
    // At a departure: whether the outer edge nearest Mario is the squish edge and he is on
    // it or past it, by the face.
    bool OverSquishEdge(const MarioState* marioState, const Object* pyramid);

    // The squish probe: on a frame the next frame's face squishes Mario on, a bully hacked
    // next to him in its knockback action and the game played probeFrames, in a sandbox that
    // is reverted, for each placement that can cancel it (CatchPlacements). The hack is
    // the one write into game memory the framework allows (AGENTS.md, hard rule 1): made after
    // a save of this frame, and followed inside its sandbox by written frame advances and
    // reads only, never a load, a save or a child, so no savestate carries it and the
    // sandbox's revert loads the clean save.
    void Probe(MarioState* marioState, Object* pyramid);
    ProbeResult ProbeOnce(MarioState* marioState, Object* bully, float offsetX, float offsetZ, int16_t pushYaw);
};

class BitfsScEndingSolution
{
public:
    float fSpd = 0;
    float pyraNormX = 0;
    float pyraNormY = 0;
    float pyraNormZ = 0;
    float xzSum = 0;
    int approach = 0;          // BitfsScEndingMetrics::Approach
    uint32_t leaveAction = 0;
    float leaveSpeed = 0;
    int16_t leaveYaw = 0;
    int leaveCUp = 0;
    int quickturn = 0;
    float leaveEdgeDistance = -1;
    int strainBackFrames = 0;
    int nutLandings = 0;
    int64_t departureFrame = -1;
    int64_t squishFrame = -1;  // the frame the probe succeeded from
    int probeIndex = -1;
    float probeDx = 0;
    float probeDz = 0;
    float probeSpeed = 0;
    float probeHeight = 0;
    float probeNormalX = 0;    // the normal on the squish cancel frame
    float probeNormalZ = 0;
};

using Alias_ScattershotThread_BitfsScEnding = ScattershotThread<BinaryStateBin<16>, Sm64Resource, BitfsScEndingMetrics, BitfsScEndingSolution>;
using Alias_Scattershot_BitfsScEnding = Scattershot<BinaryStateBin<16>, Sm64Resource, BitfsScEndingMetrics, BitfsScEndingSolution>;

class Scattershot_BitfsScEnding : public Alias_ScattershotThread_BitfsScEnding
{
public:
    // This search's moves; the draw walks a list in this order. Each is one frame unless
    // said otherwise.
    enum class CustomMoves
    {
        NO_SCRIPT,        // the random frame
        RUN_RANDOM,       // a run frame with the stick within 90 degrees of the facing: exploration that never turns around
        RUN_DOWNHILL_MIN, // the minimum downhill angle toward the target corner
        RUN_DOWNHILL,     // the same with a deviation
        TURN_UPHILL,      // toward uphill by up to 2048
        RUN_TO_EDGE,      // a full stick toward the squish edge, veering up to 40 degrees away from the run's edge, held one to six frames: the brake's way to the edge
        DIVE,             // the dive recover's first move: the turn to the aim, drawn across the catch's window, and the dive straight ahead at 29 speed or more once the edge is in reach, with the pause buffer (four frames); it ends on the dive's slide
        ROLLOUT,          // the dive recover's second move: B on the slide for the rollout and its first frame, each the stick that keeps it lowest over the platform
        LANDING,          // the dive recover's third move: the rollout steered within the band over the platform to a landing stop within DrLandingReach of the squish edge
        HOLD_BACK,        // the stick against the facing: the turnaround, then braking
        RELEASE,          // a neutral stick: braking, decelerating
        WALK_OFF,         // a small stick off the edge, from straight off to the edge's own line
        LEAVE_ALONG,      // from a stop: the quickturn to idle, then a walk off along the edge toward the catch window, within 45 degrees of the edge's line; three frames
        PARALLEL,         // a full stick along the edge
        QUICKTURN,        // from a landing or braking stop: a minimum-magnitude stick, then neutral; two frames
        CUP_DECEL,        // C-up with a small stick or none
        WAIT,             // a neutral frame
        WAIT_FOR_TILT,    // stopped on the platform: neutral frames until the tilt across the squish edge has just the steps the leave and the fall need; up to sixteen
        STRAIN_BACK       // in the air: the stick in or out on the platform's line, at the magnitude that holds Mario where the sweeping edge catches him
    };

    // How far from the pyramid's home the search lets Mario be, on each axis.
    static constexpr float SearchHalfExtent = 500.0f;
    // How far outside the squish edge a departed Mario may fall, at most: the untilting
    // face's edge sweeps outward about 3.3 a frame (307 times the 0.01 step, over the
    // normal's length), and the longest fall a catch can end is eight frames, from the
    // top's 150 down to the lowest catch 27 above the lava; the maintainer's movies fall 0
    // to 6.5 outside it. Within that, what the sweep covers in the window's remaining
    // frames (ValidateState).
    static constexpr float SweepReach = 26.0f;
    // What the catch's bound on the edge's height at Mario's position is relaxed by, so the
    // run is not refused for a catch the sweep and his drift could still bring into the
    // window (the maintainer, 2026-10-11: compute the bound and allow a little less).
    static constexpr float CornerSlack = 5.0f;
    // What the catch's upper bound on the edge's height is relaxed by: the side under the
    // edge falls about 1.7 a unit inward, so a catch two sweeps deep squishes under an edge
    // that stands that much over SquishSpace.
    static constexpr float SweepDepthSlack = 11.0f;
    // How far from the squish edge the brake is offered: the maintainer's movies begin
    // their approach about 90 out; offered at 160 the search braked 140 out and crawled
    // the last 70 at 11 for ten frames, the tilt spent before the leave (2026-10-11).
    static constexpr float BrakeReach = 80.0f;
    // How far inside the squish edge the dive recover's landing stop may be: close enough
    // to walk off in a frame or two (the maintainer, 2026-10-11; the movies land 3 to 10
    // inside it, his old stage refused a landing over 2 from any edge, and at 10 no dive
    // of the night's runs landed).
    static constexpr float DrLandingReach = 30.0f;
    // How far the dive recover carries Mario, dive and rollout: the uscz movie's 200. The
    // recover, its turn toward the aim counted, is offered within this of the edge, so
    // that the run keeps its speed along the low edge until then (the movie dives at 31
    // some 190 from the edge; the night's runs turned early, bled to 25 and never had the
    // dive's 29 where it could reach the edge), and the dive itself where the edge is
    // within its reach.
    static constexpr float DrReach = 200.0f;
    // What a frame of the turn toward the aim costs in speed, for whether a run can still
    // dive at 29 after it: the turn is uphill on both axes, where the slope takes about
    // 0.85 a frame and the run's own gain gives back 1.1 less a 43rd of the speed, 0.4 at
    // 30 and nothing over 32 (the uscz movie's turn, 35 to 31 in five frames). A little
    // less than the cost at 30, so the gate refuses a little less than it could.
    static constexpr float TurnSpeedCost = 0.5f;
    // The least the floor must rise a frame under a dive, along its heading at its speed, for
    // the rollout to stay within the band of 4 over the platform: the movies' dives rise 24
    // to 27, a dive straight at the squish edge 13, which the rollout out-climbs.
    static constexpr float DiveRise = 16.0f;
    // The frames after the dive's pause the rollout is kept lowest over the platform (its
    // press and its first frame, the Rollout move), and the most the recover takes from the
    // pause to its landing stop (the Landing move has the rest).
    static constexpr int RolloutLowFrames = 2;
    static constexpr int RolloutFrames = 12;
    // The time budget on the platform (the maintainer, 2026-10-11): the fall from the edge to
    // the catch, in frames of the tilt flattening (the first solutions' 7, the uscz movie's
    // 8), the slowest a leave moves (a walk off from a stop), and the frames allowed over
    // the budget, so that the bound, computed, refuses a little less than it could: at 8
    // with the catch's tilt taken without its slack the 8,000-shot runs found nothing where
    // they had found three squish cancels, a fifth of the departures left (2026-10-11).
    static constexpr int FallFrames = 7;
    // The frames from the edge's reach to the leave, between the run and the fall: a brake
    // or a landing stop, the quickturn and the step off (the squish cancels of 2026-10-11 and
    // 2026-10-13 leave two to three frames after their brake).
    static constexpr int LeaveFrames = 3;
    static constexpr float WalkOffSpeed = 4.0f;
    static constexpr float BudgetSlack = 2.0f;
    // Where the strain in the air holds Mario once he is deep enough for the squish: this far
    // past the squish edge as it will stand a frame on, for the next sweep to cover him;
    // before that it holds him a sweep further out, clear of the edge.
    static constexpr float StrainTarget = 1.0f;
    // The shortest fall a leave can have: a step leaves the ground only over a drop of more
    // than 100 units, and a fall from rest covers 84 in six frames and 112 in seven.
    static constexpr int MinFallFrames = 7;

    Scattershot_BitfsScEnding(Alias_Scattershot_BitfsScEnding& scattershot, const BitfsScEndingArgs& args)
        : Alias_ScattershotThread_BitfsScEnding(scattershot), _args(args),
        _roughTargetAngle(BitfsScEndingMetrics::RoughTargetAngle(args.oscQuadrant, args.targetQuadrant)) {}

    void SelectMovementOptions() override;
    bool ApplyMovement() override;
    BinaryStateBin<16> GetStateBin() override;
    bool ValidateState() override;
    float GetStateFitness() override;

    std::string GetCsvLabels() override;
    bool ForceAddToCsv() override;
    std::string GetCsvRow() override;
    bool IsSolution() override;
    BitfsScEndingSolution GetSolutionState() override;

private:
    // A frame of the dive recover's rollout, as the compare judges it.
    struct RolloutStep
    {
        bool landed = false;
        float height = 0; // Mario above his floor
    };

    BitfsScEndingArgs _args;
    int16_t _roughTargetAngle = 0;

    Object* Pyramid();
    void EdgeDirections(float& edgeX, float& edgeZ, float& outX, float& outZ) const; // the run's edge
    void SquishDirections(float& outX, float& outZ, float& alongX, float& alongZ) const; // the squish edge
    int16_t SquishEdgeYaw() const; // straight off the squish edge
    // How far inside the squish edge Mario is, along its outward direction, by the face
    // (negative outside it).
    float SquishEdgeDistance(const MarioState* marioState, const Object* pyramid) const;
    // The push-out distance of the probe's bully: the two hitbox radii and the padding.
    float PushDistance(const MarioState* marioState) const;
    // The least the squish edge may stand above the lava at a catch: the top must still
    // stand over LeaveGroundHeight where the shortest push lands him, downhill along the
    // edge by half the push-out distance.
    float LeastEdgeHeight(const BitfsScEndingMetrics::Face& face, float pushDistance) const;
    // The point on the squish edge at a coordinate along it from the home, and the point
    // where the edge stands at a fraction of the catch's window of heights (LeastEdgeHeight
    // to SquishSpace) from its low end, the middle by default: the aim of the run to the
    // edge, and of the dive recover's turn.
    void EdgePoint(const BitfsScEndingMetrics::Face& face, float along, float& x, float& z) const;
    void AimPoint(const BitfsScEndingMetrics::Face& face, float pushDistance, float& x, float& z, float fraction = 0.5f) const;
    // The normal's component across the squish edge at the catch, and the face there, for
    // the aim, the landing and the time budget: with a target, the target's; without one,
    // this frame's component flattened to the last value at which the side is still steep,
    // with the catch's slack, the latest catch the cancel allows (the run across the
    // platform flattens that component a step a frame, and a leave at this frame's tilt
    // aims where the edge will stand far too high).
    float CatchAcross(const Object* pyramid) const;
    BitfsScEndingMetrics::Face CatchFace(const Object* pyramid) const;
    // The face at the catch a dive recover from here is heading for, for its aim and its
    // landing, and the face a departed Mario's fall ends on, for the window rule (the run
    // to the edge keeps the latest catch's, the brake's leave being the search's to
    // place): this frame's component across the squish edge stepped toward flat once a
    // frame for the frames the catch is away, and no further than CatchAcross (the
    // maintainer, 2026-10-13: the aim at the latest catch the cancel allows put the dive
    // recover's landings 20 to 40 units nearer the corner than the catches that cancel,
    // where the edge was too low by the time the fall was caught); and the component
    // along it stepped as the game will step it, toward the goal Mario's position sets
    // while he is on the platform (it grows while he is on the edge's half of the run's
    // axis: 0.27 at the dive and 0.40 at the catches that cancel, so an aim on this
    // frame's value stood 40 to 60 units nearer the corner than the window, 2026-10-13)
    // and toward flat once he is off it. FramesToCatch is that count: the frames to the
    // edge at the run's speed or a walk-off's, LeaveFrames for the stop and the step off,
    // and the fall's; a departed Mario's count is what the fall has left.
    float FramesToCatch(const MarioState* marioState, const Object* pyramid) const;
    BitfsScEndingMetrics::Face ExpectedCatchFace(const MarioState* marioState, const Object* pyramid, float framesToCatch) const;
    // Whether the squish edge, on the catch's face, stands within the catch's window of
    // heights at Mario's position along it: where a landing must be for the fall from it
    // to be caught where the cancel works.
    bool InCatchWindow(const MarioState* marioState, const Object* pyramid) const;
    bool InCatchWindow(const MarioState* marioState, const Object* pyramid, const BitfsScEndingMetrics::Face& catchFace) const;
    // The steps the normal's axis (0 x, 2 z) is further from flat than the target, from the
    // metric's count of steps to the target: negative when it is nearer flat than the target.
    int StepsToFlat(const BitfsScEndingMetrics::CustomScriptStatus& state, int axis) const;

    // The pyramid's goal and whether its next tilt is a full step on both axes, as
    // Scattershot_BitfsDr has them (ROADMAP 4.11 notes the duplication).
    static void PyramidGoal(const MarioState* marioState, const Object* pyramid, float& goalX, float& goalZ);
    static bool Steps(const MarioState* marioState, const Object* pyramid);

    // A frame of the run (SWING): the stick asked for, or the nearest variant that keeps the
    // pyramid stepping on both axes; false, with nothing written, when none does. The frames
    // of the approach and the leave are written as they are, the stick the scenario asked
    // for: the validation holds the error exact on every frame, so a partial step ends the
    // lineage a frame later instead, and the variants would blur what the move was.
    bool StepFrame(int16_t intendedYaw, float intendedMag, uint16_t buttons = 0, Rotation bias = Rotation::NONE);
    bool StepInputs(Inputs inputs);

    bool RunDownhill_1f(bool min = true);
    bool TurnUphill_1f();
    bool RunRandom();
    bool RunToEdge();
    // The dive recover (the maintainer's BitFsScApproach_AttemptDr logic, 2026-10-11), as
    // three moves, so that the CSV has a row on the slide, one in the rollout and one at the
    // landing stop (the maintainer, 2026-10-13: while the recover was one move, the viewer
    // showed none of it). Dive: the turn toward the aim, drawn across the catch's window,
    // then the dive straight ahead as soon as the edge is in reach, and the pause buffer;
    // false, the frames reverted, when the turn is not done by the dive's window, the speed
    // is under the dive's 29 after it, or the floor does not rise under the dive. Rollout:
    // the rollout's press and its first frame. Landing: the rest of its flight, to the
    // landing stop. Every frame after the dive is the stick, among the yaws within a quarter
    // turn toward uphill, that keeps the rollout low over the platform (RolloutFrame); the
    // last two moves are false when the rollout leaves the platform, or lands further than
    // DrLandingReach inside the squish edge (LandedInReach).
    bool Dive();
    bool Rollout();
    bool Landing();
    bool RolloutFrame(int frame, int rotation);
    int RolloutRotation(const MarioState* marioState);
    bool LandedInReach();
    bool HoldBack();
    bool WalkOff();
    // The leave the squish cancels of 2026-10-11 and 2026-10-13 have, as one move from a stop: the
    // quickturn to idle, so that the walk starts the way the stick points, then the stick
    // along the edge toward the catch window, turned outward by up to 45 degrees; the walk
    // then drifts little outward while it falls, within what the sideways strain in the air
    // cancels and the sweep catches (a leave straight off, the dive recover's landings' way,
    // drifts out of the sweep's reach before the fall is deep enough). False, the frames
    // written, when the quickturn does not reach idle.
    bool LeaveAlong();
    bool Parallel();
    bool Quickturn();
    bool CUpDecel();
    bool StrainBack();
    // Neutral frames stopped on the platform until the tilt across the squish edge has just
    // the steps the leave and the fall need (the time budget's frames left down to
    // LeaveFrames): the dive recover lands with the tilt still 0.30 across where the squish
    // cancels' leaves have 0.26 (the maintainer's run of 2026-10-13), and a fall from there
    // is covered by the sweep 45 under the top, where the game lands Mario back on it. The
    // sampled draws never strung the waiting frames together. False, the frames written,
    // when Mario is not stopped, leaves the platform, or was already at the budget.
    bool WaitForTilt();
};
