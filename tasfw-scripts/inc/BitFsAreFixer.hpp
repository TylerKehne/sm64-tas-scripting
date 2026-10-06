#pragma once

#include <tasfw/Script.hpp>
#include <LibSm64.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

// Sets the adjusted remainder error the squish-cancel setup needs on both axes of the
// pyramid's normal, one axis at a time, and hands Mario over running with both axes
// stepping (ROADMAP 4.8). An axis's error is the goal's float on the last frame the normal
// snapped to it: approach_by_increment sets the normal to the goal exactly whenever the
// goal is within 0.01 of it, and from the last such frame the chain steps by 0.01 while the
// goal outruns the normal, which the oscillation stage keeps up. The goal is a pure function
// of Mario's position (RestingNormal), so each axis is set by where Mario is on one frame.
// The x axis is set by a rest: a dive recover, the setup being for an A-button-challenge
// run, from a dive onto the platform (the movie's own way in, its air frames shedding
// speed), the slide, then the rollout, whose air frames are steered and which rests where
// it lands; at a rest both axes snap to the goal, and the rest only has to put the x goal
// on the right float, a curve of positions rather than a point. The z axis is set by the
// turn out of a short run from that rest along x, during which z keeps snapping while x
// steps: the frame z last snaps on, as Mario turns toward the chord the oscillation's
// first leg runs, sets it from his position the frame before, which the run's last frames'
// sticks place (SearchTurn). The script's cursor stays on the rollout's first frame while it
// searches the landing and on the rest while it searches the turn: every trial is an ad-hoc
// block that reverts, and the one play that sets each axis is applied.
class BitFsAreFixer : public Script<LibSm64>
{
public:
	struct Args
	{
		float targetNx = 0;
		float targetNz = 0;
		int tolerance = 100;      // ULPs of the target normal, on both axes; 0 is an exact match. Validation refuses one the arithmetic cannot hold (LeastError)
		// The range of the normal, in magnitude on each axis, that the oscillations step it
		// through, and how far past the origin into the adjacent corner the final oscillation
		// takes an axis. A set value must step reversibly across all of it
		// (StepsReversibly), or the error it sets does not survive. Every value that
		// steps reversibly on the target's side also crosses the origin intact, but the far
		// side has its own binades: for the config's setup x survives to 0.24 there and z to
		// 0.02, none further (ROADMAP 4.6).
		float minNormal = 0.13f;
		float maxNormal = 0.61f;
		float farNormal = 0.02f;
		// The corner the hand-over is in, as the signs of the normal's x and z there (1: +x +z,
		// 2: +x -z, 3: -x -z, 4: -x +z; 0: the target's own corner): the oscillation's corner,
		// which need not be the target's, since the final oscillation can carry an axis across
		// the origin and the walk to the target then crosses it too (StepsReversibly covers the
		// far side). The error matched is the target's wherever the hand-over lies.
		int quadrant = 0;
		// The hand-over's tilt (|nX| + |nZ|) at least this, so the oscillation starts near its
		// regime rather than building the tilt over its first swings (ROADMAP 4.6; the config
		// states it, the target normal's own sum less 0.02). 0 accepts any tilt.
		float minXzSum = 0;
		// Where to hand over; both 0 asks for the corner's diagonal at the radius whose
		// resting tilt is the floor plus 0.06 (RestAsked places the rest the run's length
		// before it; the run's drift and the parity's band take half the margin). The way there is the fixer's own (Ways): the run before the dive when the
		// stage starts on it, the dive's yaw and air stick, and the slide frames before the
		// rollout.
		float restX = 0;
		float restZ = 0;
		int fineFrames = 3;       // the rollout's last air frames, and the run's last frames before the turn, swept stick by stick (2 or 3)
		int sticksPerFrame = 500; // sticks measured per fine frame
		int verifyPerRound = 10;  // predicted landings played out per round, nearest first
		int maxRounds = 5;
	};

	class CustomScriptStatus
	{
	public:
		// The hand-over: the last frame the z axis snapped on, from which the oscillation
		// stage starts with both axes stepping; the normal there and its errors and steps. When
		// not solved, the nearest hand-over or rest seen (the smallest larger-axis error), so
		// that a failed run says how far off it ended.
		bool solved = false;
		int64_t handoverFrame = -1;
		std::array<float, 3> normal = { 0, 0, 0 };
		std::array<float, 3> handoverPos = { 0, 0, 0 };
		std::array<float, 3> adjustedRemainderError = { 0, 0, 0 };
		std::array<int, 3> incrementFrames = { 0, 0, 0 };
		// The rest that set x: its frame, position and normal, and the x error there.
		int64_t restFrame = -1;
		std::array<float, 3> restPos = { 0, 0, 0 };
		std::array<float, 3> restNormal = { 0, 0, 0 };
		float restErrorX = 0;
		int rounds = 0;      // of the landing search
		int turnRounds = 0;  // of the turn search
		int framesAfterRest = 0; // frames from the rest to the hand-over: the run and the turn's frames to z's last snap
		// The way taken: frames of the run before the dive (0 from a dive slide), the dive's
		// yaw and air stick (0 back, 1 neutral, 2 at the yaw), the slide frames before the
		// rollout, and how many ways rest on the platform at all.
		int runFrames = 0;
		int16_t diveYaw = 0;
		int diveAir = 0;
		int slideFrames = 0;
		int ways = 0;
		// The walk after the landing, when one was taken (Walks): its frames and the stick's yaw
		// from Mario's facing at the landing.
		int walkFrames = 0;
		int16_t walkYaw = 0;
		// What validation found of the target before any frame: the smallest |ARE| each axis
		// admits under the reversibility rule (LeastError; -1: none within MaxLeastError), and
		// why the script refused to run when it did (a literal; nullptr when it ran).
		std::array<int, 3> leastError = { -1, 0, -1 };
		const char* refusal = nullptr;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	explicit BitFsAreFixer(const Args& args) : _args(args) {}

	bool validation();
	bool execution();
	bool assertion();

	// CalculateARE as the pipeline's metric scripts compute it: step the normal by 0.01
	// toward the target until within 0.005 of it; the error is the miss in ULPs of the
	// target, the steps are signed toward it.
	static void AdjustedRemainderError(float normal, float target, float& error, int& steps);

	// Whether the tilt's steps keep that error from this normal everywhere from farNormal
	// past the origin on the far side to maxNormal on the target's side: 0.01f added to a
	// value and taken away does not always round back (across the binades 0.25 and 0.5 it
	// does for half of the values), and a value it does not round back to has another error.
	// Walked from the normal a step at a time each way, every step undone and the error
	// recomputed, in the game's own float arithmetic; a normal outside [minNormal, maxNormal]
	// in magnitude on the target's side fails. A value that merges into another chain at a
	// crossing is not accepted on the strength of that chain: the oscillations' axes peak
	// near 0.5, so whether an axis crosses that edge at all is the run's (ROADMAP 4.8).
	static bool StepsReversibly(float normal, float target, float minNormal, float maxNormal, float farNormal);
	static int CornerSignX(const Args& args); // the sign of the normal's x in the hand-over's corner (Args::quadrant, or the target's)
	static int CornerSignZ(const Args& args);

	// The goal the pyramid's normal approaches with Mario (dx, dz) from its home, in the
	// game's own float arithmetic (bhv_tilting_inverted_pyramid_loop: (dx, 500, dz)
	// normalised, which approach_by_increment snaps to within 0.01, from the position of
	// Mario's object, a frame behind his state). A rest's normal is this goal exactly.
	static void RestingNormal(float dx, float dz, float& nx, float& nz);

	// The smallest |ARE| within MaxLeastError that some normal on the corner's side of the axis
	// holds and steps reversibly over the range, or -1. Each 0.01 band of the range has one
	// float with the target's error exactly and its neighbours' errors around it, and a binade
	// crossing keeps every other float, so a target off the surviving residue holds no error
	// under some size on that axis at all: from the config's x target, -0.17944f, none under 1
	// (its z target holds 0). A lower bound on the tolerance an axis can be set to; milliseconds.
	static int LeastError(float target, int side, float minNormal, float maxNormal, float farNormal);
	static constexpr int MaxLeastError = 100;

private:
	static constexpr int MaxFineFrames = 3;
	static constexpr int MaxRunFrames = 12;  // of the run before the dive (from frame 3250 the movie dives after 8, and the ledge ends the run soon after)
	static constexpr int MaxYawSteps = 4;    // steps of 1024 to either side of the movie's dive yaw
	static constexpr int MaxSlideFrames = 3; // of the dive slide before the rollout
	static constexpr double NearRest = 20.0;      // units of position: how near the hand-over asked for the hand-over must lie, to the corner's diagonal when the config asks for the diagonal. Measured 2026-10-05 on the dr stage from rests at tilt 0.57: through at its usual rate up to about 20 units off, slipping near 30, mostly failing at 40 (ROADMAP 4.8)
	static constexpr double NearAim = 4.0;        // units of position: a way whose constant stick cannot bring the rest this near the rest asked for is given up before the fine frames, whose reach is a unit or two
	static constexpr size_t MaxWays = 8;          // ways tried at most, those resting nearest the rest asked for first
	static constexpr double RunShift = 50.0;      // units of position the run from the rest to the hand-over covers along x (five or six frames from idle at 8 to 13 speed): the rest is asked for that far back from the hand-over asked for
	static constexpr double ZMargin = 20.0;       // units of position the hand-over is asked for on the z side of the diagonal, so that z is its steeper axis after the run's drift (about 0.035 of the normal at the setup's radius): the leg from it runs x toward the corner and z away, the run's and the turn's own directions
	static constexpr int MaxTurnFrames = 8;       // frames of the turn played at most before z must have stepped twice after its last snap
	static constexpr int MaxExtraRun = 1;         // run frames before the fine frames tried at most: one flips the parity of x's steps at the turn when the turn comes a frame off the ask's reckoning (ParityAsked); more would run the hand-over out of the diagonal's band
	static constexpr int16_t TurnLimit = 0x800;   // the yaw a walking Mario turns toward the stick in one frame (update_walking_speed): the run's fine sticks lie within it of his facing and of each other, so each turns him to its own yaw and their effects add
	static constexpr float RunMagnitude = 0.75f;  // the least magnitude of the run's fine sticks, in the unit disk: above his speed a stick accelerates Mario the same whatever its magnitude, so every combination shares one speed and their effects add
	static constexpr int MaxTurnPlays = 500;      // candidates of the turn search played at most per lattice: a play is the run and turn's eight frames or so, and the plays' errors scatter some hundred ULPs about the prediction, so an exact z takes a couple of hundred
	static constexpr double Slack = 64.0;         // ULPs of the z error: how far from the target's float a combination's first-order error may lie to be computed exactly and, exactly, to be played; the goal's curvature over a unit of movement is some fifty

	// A walk after the landing: a stick held from the land, at a yaw from Mario's facing there,
	// for some frames, before the settle. The settle after a landing left idle rounds Mario
	// onto a staircase of rests coarser than the floats (a position in one of its gaps is
	// reached by no landing); a walk first moves him a unit or so while the platform's first
	// steps carry him, so the settle starts from another state and rounds onto another
	// staircase. Each walk is another try at the rest, after the landing left idle.
	struct Walk
	{
		int16_t dYaw = 0;
		int frames = 0;
	};
	static constexpr std::array<Walk, 8> Walks = { { { 0, 0 }, { 0, 1 }, { 0x4000, 1 }, { -0x4000, 1 }, { int16_t(0x8000), 1 }, { 0, 2 }, { 0x4000, 2 }, { -0x4000, 2 } } };
	static constexpr int ObjectPoolCapacity = 240; // OBJECT_POOL_CAPACITY: the slots of gObjectPool
	// Units of position: the landing prediction's resolution along x and z (a couple of float
	// steps of x at the platform's radius). The cell the predictions are looked up in is never
	// finer than this, so at a tight tolerance the candidates within the noise are played and
	// the one that rests exactly is found by play, not prediction; and a play whose rest asks
	// a smaller correction does not move the target.
	static constexpr double NoiseX = 0.0005;
	static constexpr double NoiseZ = 0.0003;
	using Sticks = std::array<std::pair<int8_t, int8_t>, MaxFineFrames>; // the fine frames' sticks

	struct Rest
	{
		bool reached = false;
		int64_t frame = -1;
		std::array<float, 3> normal = { 0, 0, 0 };
		std::array<float, 3> pos = { 0, 0, 0 };
		std::array<float, 3> error = { 0, 0, 0 };
		std::array<int, 3> steps = { 0, 0, 0 };
	};

	// The turn out of the run from the rest: the last frame z snapped on (the hand-over), the
	// position the frame before it that set z, the normal there with its errors and steps, and
	// whether both axes stepped in the leg's directions on the two frames after.
	struct Turn
	{
		bool reached = false;
		int64_t frame = -1;
		int framesAfterRest = 0;
		std::array<float, 3> pos = { 0, 0, 0 };
		std::array<float, 3> posBefore = { 0, 0, 0 };
		std::array<float, 3> normal = { 0, 0, 0 };
		std::array<float, 3> error = { 0, 0, 0 };
		std::array<int, 3> steps = { 0, 0, 0 };
		bool stepping = false;
	};

	// The constant stick of the rollout's steered frames, as the components the air movement
	// reads (forward along the face yaw, sideways to its right), in the unit disk.
	struct Aim
	{
		float forward = 0;
		float sideways = 0;
	};

	// Where a rollout put Mario: his position on the frame it landed, and how many frames it flew.
	struct Landing
	{
		bool landed = false;
		float x = 0;
		float z = 0;
		int frames = 0;
	};

	// One rollout played out to its rest: the status of a trial and of the compare's candidates.
	struct Play
	{
		Sticks sticks = {};
		Landing landing;
		Rest rest;
		double distance = std::numeric_limits<double>::infinity(); // the rest's x error in ULPs
	};

	// One run and turn played out from the rest: the status of a turn trial.
	struct TurnPlay
	{
		Sticks sticks = {};
		Turn turn;
	};

	// One way onto the platform (the run, the dive and the slide), with where a straight
	// rollout from it lands and rests.
	struct Way
	{
		int runFrames = 0;
		int16_t diveYaw = 0;
		int diveAir = 0;
		int slideFrames = 0;
		Landing landing;
		Rest rest;
	};

	// One stick's effect over one air frame from a known state: the movement, the forward
	// speed it leaves, and the velocity the game moved with (a landing frame moves a quarter
	// step or more of it).
	struct Effect
	{
		std::pair<int8_t, int8_t> stick = { 0, 0 };
		float dx = 0;
		float dz = 0;
		float forwardVel = 0;
		float vx = 0;
		float vz = 0;
	};

	// One of the distinct sticks the game reads: the yaw it means less the camera's, and its
	// magnitude in the unit disk. The air movement reads a stick as (coss(dYaw) * mag,
	// sins(dYaw) * mag), so the stick nearest an aim is found among these in yaw order
	// (StickEffects, Stick); a lookup by yaw settles on one ray of sticks and its few
	// magnitudes, and straight back that ray is the full stick whatever magnitude is asked.
	struct StickEffect
	{
		int16_t baseYaw = 0;
		float mag = 0;
		std::pair<int8_t, int8_t> stick = { 0, 0 };
	};

	// How far the current sticks' landing misses the target, along the face yaw and to its right.
	struct Miss
	{
		double forward = 0;
		double sideways = 0;
	};

	// The one-frame lattices of the fine frames along the current sticks, from where the
	// rollout was when the first of them began.
	struct Lattices
	{
		std::vector<std::vector<Effect>> frames;
		std::vector<Effect> representative; // the current stick's own effect on each frame
		float x0 = 0;
		float z0 = 0;
	};

	// Where every combination of the leading fine frames leaves Mario, plus what the speed it
	// leaves him with adds on the landing frame, in a grid of cell-sized buckets that the
	// landing frame's effects are looked up against.
	struct Predictions
	{
		struct Head
		{
			double x = 0;
			double z = 0;
			Sticks sticks = {}; // the leading frames' sticks, the landing frame's still to fill
		};
		struct Candidate
		{
			double predicted = 0; // in windows (the tolerance, or the prediction's resolution)
			float x = 0;          // the landing predicted, as the floats Mario's position takes: candidates predicting the same pair land the same and one of them is enough
			float z = 0;
			Sticks sticks = {};
		};

		std::vector<Head> heads;
		std::vector<Effect> tail; // the landing frame's effects
		std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
		double bucketX = 0;
		double bucketZ = 0;
		double j[2][2] = {}; // d(normal) / d(landing)
		double tolX = 0;     // the window (the tolerance, or one ULP for an exact match) in the normal's units
		double tolZ = 0;
		int last = 0;        // the landing frame's index among the fine frames

		double Distance(double px, double pz, double targetX, double targetZ) const;       // in windows, through the normal the landing would rest at
		std::vector<Candidate> Nearest(double targetX, double targetZ, int count) const; // the sticks predicted nearest the target, at most count of them, one per predicted landing
	};

	// One stick's effect on the turn, end to end: the movement of the position that sets z
	// (the one before z's last snap) when that stick replaces the current one on its frame
	// and the run and turn play on, from the current sticks' own.
	struct TurnEffect
	{
		std::pair<int8_t, int8_t> stick = { 0, 0 };
		int16_t yaw = 0; // the yaw the stick means: a walking Mario turns at most TurnLimit a frame, so two sticks' effects add only when their yaws are within it of each other
		double dx = 0;
		double dz = 0;
	};

	Args _args;
	Object* _pyramid = nullptr;
	const BehaviorScript* _pyramidBehavior = nullptr;
	MarioState* _mario = nullptr;
	Camera* _camera = nullptr;
	int _rolloutFrames = 0; // frames the rollout flies with the current plan
	Aim _aim;               // the plan: the constant stick of the steered frames
	Sticks _fine = {};      // and the fine frames' sticks
	Walk _walk;             // and the walk after the landing (none for the ways' straight rollouts)
	int _extraRun = 0;      // frames of the run from the rest before its fine frames (0 or 1: one more flips the parity of x's steps at the turn)
	AdhocScriptStatus<TurnPlay> _solvedTurn; // the first play of the turn search that set z, whichever measure played it (MeasureTurn)
	Rest _nearest;          // the rest seen with the smallest x error
	Turn _nearestTurn;      // the turn seen with the smallest larger-axis error

	// Arithmetic.
	static float Ulp(float target);
	static float Drag(float forwardVel); // approach_f32(forwardVel, 0, 0.35, 0.35): the air drag at the start of an air frame
	static void Solve(const double j[2][2], double bx, double bz, double& x, double& z);
	static uint64_t Bucket(int64_t ix, int64_t iz);
	static bool IsRollout(uint32_t action);
	static float BandValue(float target, int band); // the normal that many steps of 0.01 from the target, stepped as the game steps
	static bool Stepped(float before, float after); // the normal moved by the game's 0.01f step between the two frames, either way

	// The moves.
	Object* LandingPyramid(); // the pyramid the movie's own dive lands on, from the run: the movie's inputs played to the dive slide in a block that reverts (validation; at frame 3250 Mario stands beside the other pyramid's home, so no rule of nearness or heading tells them apart)
	static const std::vector<StickEffect>& StickEffects(); // every distinct stick, by its yaw; built once
	std::pair<int8_t, int8_t> Stick(Aim aim) const;        // the stick whose effect on the air movement is nearest the aim, from the cursor's face and camera yaws
	std::vector<Way> Ways();                                              // every way onto the platform whose straight rollout rests on it
	bool Approach(const Way& way);                                        // its run, dive and slide; ends on the rollout's frame
	void RestAsked(float& x, float& z) const;                             // where the rest is asked for: Args::restX/restZ, or the corner's diagonal at the tilt floor's radius, the run's length back along x
	void ParityAsked(float& x, float& z) const;                           // the rest asked for moved one x band toward the home when the step parities at the turn would differ
	double DistanceToAsked(float x, float z, bool handover) const;        // of a rest (to the rest asked for) or of a hand-over (to the diagonal, or the point named)
	bool SearchLanding(float askX, float askZ);                           // the landing search from the rollout's frame for a rest that sets x near the rest asked for; applies the play that does
	bool Rollout(Landing& landing, const Sticks& fine, int frames = 200); // from the cursor: the rollout with the plan's sticks, to its landing or for that many frames
	bool AdvanceToRest(Rest& rest);                                       // neutral frames until the pyramid and Mario stop changing
	AdhocScriptStatus<Play> Measure(const Sticks& fine);                  // a rollout and its rest, reverted; the diff comes back
	bool SolvedRest(const Rest& rest) const;                              // the rest sets x: its error within the tolerance, reversible, in the corner, near the rest asked for
	void CurvePoint(const Rest& rest, float& x, float& z) const;          // the position nearest the rest whose goal has the x target's float exactly: the rest's x, its z moved by the error over the goal's slope in z
	void RestFound(const Rest& rest);                                     // the status's rest: the one that set x

	// The model: where the rest must be, and how it answers the landing.
	void Jacobian(double x, double z, double j[2][2]) const;                        // d(normal) / d(rest position)
	static void JacobianAt(double dx, double dz, double j[2][2]);                   // the same, dx and dz from the home
	bool AimAt(float x, float z, AdhocScriptStatus<Play>& play);                    // Newton on the constant stick until the rest is within a unit of (x, z); the play at the aim found
	bool Response(const Landing& landing, const Rest& rest, double response[2][2]); // d(rest) / d(landing), from two probes on the last air frame

	// The fine frames: measure, predict, play.
	bool Fine(float x, float z, const double response[2][2], Landing& landing, Rest& rest); // the fine frames' sticks whose landing, aimed at (x, z), rests on the x curve
	std::vector<std::pair<int8_t, int8_t>> SticksNear(double forwardCenter, double sidewaysCenter, double halfForward, double halfSideways,
		std::pair<int8_t, int8_t> mustInclude) const;                                        // the distinct sticks whose effect on the speed lies in the box
	std::vector<Effect> Lattice(bool last, const std::vector<std::pair<int8_t, int8_t>>& sticks); // each stick's one frame from the cursor's state, reverted
	bool MeasureLattices(const Sticks& current, const Miss& miss, double scale, Lattices& lattices);
	Predictions Predict(const Lattices& lattices, const Sticks& current, double dirX, double dirZ, const double j[2][2]) const;
	AdhocScriptStatus<Play> PlayCandidates(const Predictions& predictions, double x, double z, const double response[2][2], int& played);

	// The turn: the run from the rest and the turn that sets z.
	int LegSignX() const;                                                  // the direction the oscillation's first leg runs on each axis from this rest (Scattershot_BitfsDr::FirstLeg_1f: the tilt's steeper axis away from the corner, the other toward it)
	int LegSignZ() const;
	std::pair<int8_t, int8_t> StickToward(int16_t yaw) const;              // the full stick at that yaw
	bool PlayTurn(const Sticks& fine, Turn& turn);                         // from the rest: the run's extra frames and fine frames along x, then the turn toward the chord until z has stepped twice after its last snap; the turn found
	AdhocScriptStatus<TurnPlay> MeasureTurn(const Sticks& fine);           // a run and turn, reverted; the diff comes back
	bool SolvedTurn(const Turn& turn) const;                               // both errors within the tolerance, reversible, the step parities equal, the corner, the tilt floor, near the hand-over asked for, both axes stepping
	bool SearchTurn();                                                     // from the rest: the fine sticks whose turn sets z; applies the play that does
	std::vector<std::pair<int8_t, int8_t>> SticksWithin(int16_t faceYaw) const; // the distinct sticks within the turn limit of the facing and above the run's magnitude, by the yaw and magnitude the game reads
	std::vector<TurnEffect> TurnLattice(int frame, const std::vector<std::pair<int8_t, int8_t>>& sticks, const Turn& current); // each stick's effect on the turn, end to end, reverted
	void Finish(const Turn& turn, bool solved);
};
