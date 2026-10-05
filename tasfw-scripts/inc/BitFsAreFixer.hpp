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

// Comes to rest on the pyramid where its resting normal carries the adjusted remainder
// error the squish-cancel setup needs (ROADMAP 4.8). With Mario at rest the pyramid snaps
// its normal to the goal his position defines, so the resting normal and its ARE are
// functions of the rest position alone: the script lands Mario near a wanted spot, measures
// the ARE at equilibrium, computes the position that zeroes it (the nearest cell of the
// lattice scripts/are_cell_model.py describes, on a band whose step parity the oscillation
// stage accepts) and lands there. The landing is a dive recover, the setup being for an
// A-button-challenge run: from a dive onto the platform (the movie's own way in, its air
// frames shedding speed), the slide, then the rollout, whose air frames are steered and
// which rests where it lands. The last air frames before the landing are swept stick by
// stick for the combination whose landing rests inside the cell. The script's cursor stays
// on the rollout's first frame while it searches: every trial is an ad-hoc block that
// reverts, and the one play that rests inside the tolerance is applied at the end.
class BitFsAreFixer : public Script<LibSm64>
{
public:
	struct Args
	{
		float targetNx = 0;
		float targetNz = 0;
		int tolerance = 100;      // ULPs of the target normal, on both axes
		// The range of the normal, in magnitude on each axis, that the oscillations step it
		// through, and how far past the origin into the adjacent corner the final oscillation
		// takes an axis. The rest's normal must step reversibly across all of it
		// (StepsReversibly), or the error the rest sets does not survive. Every value that
		// steps reversibly on the target's side also crosses the origin intact, but the far
		// side has its own binades: for the config's setup x survives to 0.24 there and z to
		// 0.02, none further (ROADMAP 4.6).
		float minNormal = 0.13f;
		float maxNormal = 0.61f;
		float farNormal = 0.02f;
		// The corner the rest is in, as the signs of the normal's x and z there (1: +x +z, 2:
		// +x -z, 3: -x -z, 4: -x +z; 0: the target's own corner): the oscillation's corner,
		// which need not be the target's, since the final oscillation can carry an axis across
		// the origin and the walk to the target then crosses it too (StepsReversibly covers the
		// far side). The error matched is the target's wherever the rest lies.
		int quadrant = 0;
		// The rest's tilt (|nX| + |nZ|) at least this, so the oscillation starts near its
		// regime rather than building the tilt over its first swings (ROADMAP 4.6; the config
		// states it, the target normal's own sum less 0.02). 0 accepts any tilt. From idle a
		// stick gives 8 speed and the first frame must step both axes, which holds within about
		// 265 units of the home: a tilt of about 0.63 on the corner's diagonal at most.
		float minXzSum = 0;
		// Where to come to rest; both 0 asks for the corner's diagonal at the radius whose
		// resting tilt is the floor plus 0.02 (RestAsked). The way there is the fixer's own
		// (Ways): the run before the dive when the stage starts on it, the dive's yaw and air
		// stick, and the slide frames before the rollout.
		float restX = 0;
		float restZ = 0;
		int fineFrames = 3;       // the rollout's last air frames, swept stick by stick (2 or 3)
		int sticksPerFrame = 500; // sticks measured per fine frame
		int verifyPerRound = 10;  // predicted landings played out per round, nearest first
		int maxRounds = 5;
	};

	class CustomScriptStatus
	{
	public:
		bool solved = false;
		int64_t equilibriumFrame = -1;
		std::array<float, 3> normal = { 0, 0, 0 };
		std::array<float, 3> restPos = { 0, 0, 0 };
		std::array<float, 3> adjustedRemainderError = { 0, 0, 0 };
		std::array<int, 3> incrementFrames = { 0, 0, 0 };
		int rounds = 0;
		// The way taken: frames of the run before the dive (0 from a dive slide), the dive's
		// yaw and air stick (0 back, 1 neutral, 2 at the yaw), the slide frames before the
		// rollout, and how many ways rest on the platform at all.
		int runFrames = 0;
		int16_t diveYaw = 0;
		int diveAir = 0;
		int slideFrames = 0;
		int ways = 0;
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
	// in magnitude on the target's side fails.
	static bool StepsReversibly(float normal, float target, float minNormal, float maxNormal, float farNormal);
	static int CornerSignX(const Args& args); // the sign of the normal's x in the rest's corner (Args::quadrant, or the target's)
	static int CornerSignZ(const Args& args);

private:
	static constexpr int MaxFineFrames = 3;
	static constexpr int MaxRunFrames = 12;  // of the run before the dive (from frame 3250 the movie dives after 8, and the ledge ends the run soon after)
	static constexpr int MaxYawSteps = 4;    // steps of 1024 to either side of the movie's dive yaw
	static constexpr int MaxSlideFrames = 3; // of the dive slide before the rollout
	static constexpr double NearRest = 8.0;  // units of position: how near the rest asked for a way's rest must come to be taken before the ways that cannot (the oscillation wants the diagonal within about ten; ROADMAP 4.6)
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
		double distance = std::numeric_limits<double>::infinity(); // the rest's ARE in tolerances, the larger axis
	};

	// One way onto the platform (the run, the dive and the slide), with where a straight
	// rollout from it lands and rests, against the rest asked for.
	struct Way
	{
		int runFrames = 0;
		int16_t diveYaw = 0;
		int diveAir = 0;
		int slideFrames = 0;
		Landing landing;
		Rest rest;
		double distance = 0; // the rest's distance to the rest asked for
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
			double predicted = 0; // in tolerances
			Sticks sticks = {};
		};

		std::vector<Head> heads;
		std::vector<Effect> tail; // the landing frame's effects
		std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
		double bucketX = 0;
		double bucketZ = 0;
		double j[2][2] = {}; // d(normal) / d(landing)
		double tolX = 0;     // the tolerance in the normal's units
		double tolZ = 0;
		int last = 0;        // the landing frame's index among the fine frames

		double Distance(double px, double pz, double targetX, double targetZ) const;       // in tolerances, through the normal the landing would rest at
		std::vector<Candidate> Nearest(double targetX, double targetZ, int count) const; // the sticks predicted nearest the target, at most count of them
	};

	Args _args;
	Object* _pyramid = nullptr;
	const BehaviorScript* _pyramidBehavior = nullptr;
	MarioState* _mario = nullptr;
	Camera* _camera = nullptr;
	int _rolloutFrames = 0; // frames the rollout flies with the current plan
	Aim _aim;               // the plan: the constant stick of the steered frames
	Sticks _fine = {};      // and the fine frames' sticks

	// Arithmetic.
	static float Ulp(float target);
	static float Drag(float forwardVel); // approach_f32(forwardVel, 0, 0.35, 0.35): the air drag at the start of an air frame
	static void Solve(const double j[2][2], double bx, double bz, double& x, double& z);
	static uint64_t Bucket(int64_t ix, int64_t iz);
	static bool IsRollout(uint32_t action);

	// The moves.
	std::pair<int8_t, int8_t> Stick(Aim aim) const;
	std::vector<Way> Ways();                                              // every way onto the platform whose straight rollout rests on it, nearest the rest asked for first
	bool Approach(const Way& way);                                        // its run, dive and slide; ends on the rollout's frame
	void RestAsked(float& x, float& z) const;                             // Args::restX/restZ, or the corner's diagonal at the tilt floor's radius
	bool SearchLanding(const Way& way, bool wantNear);                       // the landing search from the rollout's frame (wantNear: only if the rest comes within NearRest of the one asked for); applies the play that rests inside the tolerance
	bool Rollout(Landing& landing, const Sticks& fine, int frames = 200); // from the cursor: the rollout with the plan's sticks, to its landing or for that many frames
	bool AdvanceToRest(Rest& rest);                                       // neutral frames until the pyramid and Mario stop changing
	AdhocScriptStatus<Play> Measure(const Sticks& fine);                  // a rollout and its rest, reverted; the diff comes back
	bool Solved(const Rest& rest) const;
	void Finish(const Rest& rest);

	// The model: where the rest must be, and how it answers the landing.
	void Jacobian(double x, double z, double j[2][2]) const;                        // d(normal) / d(rest position)
	bool Correction(const Rest& rest, double& dx, double& dz) const;                // to the nearest cell with the right parity
	bool AimAt(float x, float z, Landing& landing);                                 // Newton on the constant stick until the rollout lands near (x, z)
	bool Response(const Landing& landing, const Rest& rest, double response[2][2]); // d(rest) / d(landing), from two probes on the last air frame

	// The fine frames: measure, predict, play.
	bool Fine(float x, float z, const double response[2][2], Landing& landing, Rest& rest); // the fine frames' sticks whose landing rests inside the cell around (x, z)
	std::vector<std::pair<int8_t, int8_t>> SticksNear(double forwardCenter, double sidewaysCenter, double halfForward, double halfSideways,
		std::pair<int8_t, int8_t> mustInclude) const;                                        // the distinct sticks whose effect on the speed lies in the box
	std::vector<Effect> Lattice(bool last, const std::vector<std::pair<int8_t, int8_t>>& sticks); // each stick's one frame from the cursor's state, reverted
	bool MeasureLattices(const Sticks& current, const Miss& miss, double scale, Lattices& lattices);
	Predictions Predict(const Lattices& lattices, const Sticks& current, double dirX, double dirZ, const double j[2][2]) const;
	AdhocScriptStatus<Play> PlayCandidates(const Predictions& predictions, double x, double z, const double response[2][2], int& played);
};
