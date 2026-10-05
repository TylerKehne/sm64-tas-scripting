#include <BitFsAreFixer.hpp>

#include <ScriptMath.hpp>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Trig.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <tuple>
#include <unordered_set>

// --- Arithmetic -----------------------------------------------------------------------------

float BitFsAreFixer::Ulp(float target)
{
	return std::fabs(std::nextafter(target, std::numeric_limits<float>::infinity()) - target);
}

float BitFsAreFixer::Drag(float forwardVel)
{
	if (forwardVel < 0.0f)
	{
		forwardVel += 0.35f;
		return forwardVel > 0.0f ? 0.0f : forwardVel;
	}
	forwardVel -= 0.35f;
	return forwardVel < 0.0f ? 0.0f : forwardVel;
}

void BitFsAreFixer::Solve(const double j[2][2], double bx, double bz, double& x, double& z)
{
	double det = j[0][0] * j[1][1] - j[0][1] * j[1][0];
	x = (j[1][1] * bx - j[0][1] * bz) / det;
	z = (-j[1][0] * bx + j[0][0] * bz) / det;
}

uint64_t BitFsAreFixer::Bucket(int64_t ix, int64_t iz)
{
	return (uint64_t(uint32_t(ix)) << 32) | uint32_t(iz);
}

bool BitFsAreFixer::IsRollout(uint32_t action)
{
	return action == ACT_FORWARD_ROLLOUT || action == ACT_BACKWARD_ROLLOUT;
}

void BitFsAreFixer::AdjustedRemainderError(float normal, float target, float& error, int& steps)
{
	float ulp = Ulp(target);
	int direction = ScriptMath::Sign(target - normal);
	float value = normal;
	error = std::numeric_limits<float>::infinity();
	steps = 0;
	for (int i = 0; i < 200; i++)
	{
		if (std::fabs(target - value) <= 0.005f)
		{
			error = (target - value) / ulp;
			steps = i * direction;
			return;
		}
		value += direction * 0.01f;
	}
}

bool BitFsAreFixer::StepsReversibly(float normal, float target, float minNormal, float maxNormal, float farNormal)
{
	// The rest's own side is the oscillation's; the target may lie past the origin, where the
	// final oscillation goes over to the adjacent corner.
	int side = ScriptMath::Sign(normal);
	float low = side < 0 ? -maxNormal : minNormal;
	float high = side < 0 ? -minNormal : maxNormal;
	if (side == 0 || normal < low || normal > high)
		return false;
	// The walk goes past the origin as far as the final oscillation does: farNormal, or a
	// couple of steps beyond the target when the target is over there.
	float farSide = farNormal; // not `far`: a Windows header defines it away
	if (ScriptMath::Sign(target) != side)
		farSide = std::max(farSide, std::fabs(target) + 0.02f);
	float walkLow = side < 0 ? -maxNormal : -farSide;
	float walkHigh = side < 0 ? farSide : maxNormal;
	float error;
	int steps;
	AdjustedRemainderError(normal, target, error, steps);
	if (!std::isfinite(error))
		return false;
	for (float direction : { 0.01f, -0.01f })
	{
		float value = normal;
		for (int n = 0; n < 200; n++)
		{
			float next = value + direction;
			if (next < walkLow || next > walkHigh)
				break;
			float back = next - direction;
			float errorNext;
			int stepsNext;
			AdjustedRemainderError(next, target, errorNext, stepsNext);
			if (back != value || errorNext != error)
				return false;
			value = next;
		}
	}
	return true;
}

// --- The lifecycle --------------------------------------------------------------------------

bool BitFsAreFixer::validation()
{
	MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
	_pyramidBehavior = (const BehaviorScript*)(ReadState("bhvBitfsTiltingInvertedPyramid"));
	if (marioState->floor == nullptr)
		return false;
	if (marioState->action == ACT_WALKING)
	{
		// On the run before the dive: the pyramid is the one the dive lands on (Approach).
		_pyramid = nullptr;
	}
	else
	{
		if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)
			return false;
		_pyramid = marioState->floor->object;
		if (_pyramid == nullptr || _pyramid->behavior != _pyramidBehavior)
			return false;
	}
	return _args.fineFrames >= 2 && _args.fineFrames <= MaxFineFrames && _args.tolerance > 0
		&& _args.minNormal > 0.0f && _args.maxNormal > _args.minNormal && _args.maxNormal < 1.0f && _args.farNormal >= 0.0f && _args.farNormal < 1.0f
		&& _args.minXzSum >= 0.0f && _args.minXzSum < 2.0f * _args.maxNormal && _args.quadrant >= 0 && _args.quadrant <= 4;
}

bool BitFsAreFixer::execution()
{
	_mario = *(MarioState**)(ReadState("gMarioState"));
	_camera = *(Camera**)(ReadState("gCamera"));

	// Every way onto the platform, the one whose straight rollout rests nearest the rest
	// asked for first: first the ways whose rest can be brought within NearRest of it, then,
	// if none solves, any way. Each way's approach and landing search is one block, kept
	// when it rests inside the tolerance and reverted otherwise.
	std::vector<Way> ways = Ways();
	CustomStatus.ways = int(ways.size());
	for (bool wantNear : { true, false }) // not "near": a Windows macro (docs/compilers.md)
		for (const Way& way : ways)
		{
			if (ModifyAdhoc([&]() { return Approach(way) && SearchLanding(way, wantNear); }).executed)
			{
				CustomStatus.runFrames = way.runFrames;
				CustomStatus.diveYaw = way.diveYaw;
				CustomStatus.diveAir = way.diveAir;
				CustomStatus.slideFrames = way.slideFrames;
				return true;
			}
		}
	return false;
}

std::vector<BitFsAreFixer::Way> BitFsAreFixer::Ways()
{
	// From a dive slide only the slide frames vary. From the run before the dive, its length,
	// the dive's yaw (the movie's and up to MaxYawSteps steps of 1024 to either side) and its
	// air stick (straight back lands about 280 units out, neutral 340, at the yaw 500) vary
	// too, since the rollout can only land along the line the dive and the slide give it.
	// Each way is played once with a straight rollout to its rest, in a block that reverts,
	// and the ways that rest on the platform are ordered by that rest's distance to the rest
	// asked for.
	const bool running = _mario->action == ACT_WALKING;
	const int16_t movieYaw = _mario->faceAngle[1];
	std::vector<Way> ways;
	for (int run = 0; run <= (running ? MaxRunFrames : 0); run++)
		for (int k = 0; k <= (running ? MaxYawSteps : 0); k++)
			for (int side : { 1, -1 })
			{
				if (k == 0 && side < 0)
					continue;
				for (int air = 0; air < (running ? 3 : 1); air++)
					for (int slide = 0; slide <= MaxSlideFrames; slide++)
					{
						Way way;
						way.runFrames = run;
						way.diveYaw = int16_t(movieYaw + side * k * 1024);
						way.diveAir = air;
						way.slideFrames = slide;
						bool rests = ExecuteAdhoc([&]()
							{
								if (!Approach(way))
									return false;
								_rolloutFrames = 0;
								_aim = Aim();
								return Rollout(way.landing, Sticks()) && AdvanceToRest(way.rest);
							}).executed;
						if (!rests)
							continue;
						float x, z;
						RestAsked(x, z);
						way.distance = std::hypot(double(way.rest.pos[0]) - double(x), double(way.rest.pos[2]) - double(z));
						ways.push_back(way);
					}
			}
	std::stable_sort(ways.begin(), ways.end(), [](const Way& a, const Way& b) { return a.distance < b.distance; });
	return ways;
}

void BitFsAreFixer::RestAsked(float& x, float& z) const
{
	if (_args.restX != 0.0f || _args.restZ != 0.0f)
	{
		x = _args.restX;
		z = _args.restZ;
		return;
	}
	// The corner's diagonal at the radius whose resting tilt is the floor (or twice minNormal,
	// which each axis needs anyway) plus 0.02: on the diagonal each axis of the normal is half
	// the tilt S, r / (sqrt 2 d) with d the distance to the point 500 below the home, so
	// r = 500 (S / sqrt 2) / sqrt(1 - S^2 / 2).
	const double s = std::min(double(std::max(_args.minXzSum, 2.0f * _args.minNormal)) + 0.02, 1.2);
	const double r = 500.0 * (s / std::sqrt(2.0)) / std::sqrt(1.0 - s * s / 2.0);
	x = float(_pyramid->oPosX + CornerSignX(_args) * r / std::sqrt(2.0));
	z = float(_pyramid->oPosZ + CornerSignZ(_args) * r / std::sqrt(2.0));
}

bool BitFsAreFixer::SearchLanding(const Way& way, bool wantNear)
{
	CustomStatus.rounds = 0;
	_rolloutFrames = 0;
	_aim = Aim();
	_fine = Sticks();

	// Land where the rest asked for should follow: the platform tilts after the landing and
	// carries Mario, by about what the way's own straight rollout showed.
	float askX, askZ;
	RestAsked(askX, askZ);
	float x = askX - (way.rest.pos[0] - way.landing.x);
	float z = askZ - (way.rest.pos[2] - way.landing.z);
	Landing landing;
	if (!AimAt(x, z, landing))
		return false;
	AdhocScriptStatus<Play> first = Measure(_fine);
	if (!first.executed)
		return false;

	// Bring the rest to the one asked for: the rest's response to the landing, measured from
	// two sticks near the current one (the platform tilts after the landing and carries
	// Mario, more the further out he lands), gives the landing shift; up to three times,
	// while the rest is more than a few units off and each shift brings it nearer, the step
	// bounded to what a rollout can be steered by.
	auto miss = [&](const Rest& at) { return std::hypot(double(askX) - at.pos[0], double(askZ) - at.pos[2]); };
	for (int attempt = 0; attempt < 3 && miss(first.rest) >= 4.0; attempt++)
	{
		double response[2][2];
		if (!Response(landing, first.rest, response) || response[0][0] * response[1][1] - response[0][1] * response[1][0] == 0.0)
			break;
		double dx, dz;
		Solve(response, double(askX) - first.rest.pos[0], double(askZ) - first.rest.pos[2], dx, dz);
		const double step = std::hypot(dx, dz);
		if (step > 60.0)
		{
			dx *= 60.0 / step;
			dz *= 60.0 / step;
		}
		const float nextX = float(landing.x + dx);
		const float nextZ = float(landing.z + dz);
		Landing again;
		if (!AimAt(nextX, nextZ, again))
			break;
		AdhocScriptStatus<Play> next = Measure(_fine);
		if (!next.executed || miss(next.rest) >= miss(first.rest))
			break;
		x = nextX;
		z = nextZ;
		landing = again;
		first = next;
	}
	if (wantNear && miss(first.rest) > NearRest)
		return false;
	Rest rest = first.rest;
	if (Solved(rest))
	{
		Apply(first.m64Diff);
		Finish(rest);
		return true;
	}

	for (int round = 1; round <= _args.maxRounds; round++)
	{
		CustomStatus.rounds = round;

		// The rest position that zeroes the error, and the landing that reaches it: the
		// platform tilts from near flat to the resting normal after the landing and carries
		// Mario with it, by an amount that depends on where he landed, so the rest answers a
		// landing shift through the response measured here rather than one to one.
		double shiftRest[2];
		if (!Correction(rest, shiftRest[0], shiftRest[1]))
			return false;
		double response[2][2];
		if (!Response(landing, rest, response))
			return false;
		double dx, dz;
		Solve(response, shiftRest[0], shiftRest[1], dx, dz);
		float x = float(landing.x + dx);
		float z = float(landing.z + dz);

		// Beyond the fine frames' reach: re-aim the constant stick first.
		if (std::fabs(dx) > 1.0 || std::fabs(dz) > 1.0)
		{
			_fine = Sticks();
			if (!AimAt(x, z, landing))
				return false;
		}

		if (Fine(x, z, response, landing, rest))
		{
			Finish(rest);
			return true;
		}
		if (!rest.reached)
			return false;
	}
	return false;
}

bool BitFsAreFixer::assertion()
{
	return CustomStatus.solved;
}

// --- The moves ------------------------------------------------------------------------------

std::pair<int8_t, int8_t> BitFsAreFixer::Stick(Aim aim) const
{
	float mag = std::sqrt(aim.forward * aim.forward + aim.sideways * aim.sideways);
	if (mag == 0.0f)
		return { 0, 0 };
	if (mag > 1.0f)
	{
		aim.forward /= mag;
		aim.sideways /= mag;
		mag = 1.0f;
	}
	// The air movement reads coss(dYaw) as the forward share and sins(dYaw) as the sideways one.
	int16_t dYaw = atan2s(aim.forward, aim.sideways);
	return Inputs::GetClosestInputByYawExact(int16_t(_mario->faceAngle[1] + dYaw), mag * 32.0f, _camera->yaw);
}

bool BitFsAreFixer::Approach(const Way& way)
{
	// From the run: the way's run frames with the stick at the dive's yaw and the B press
	// with it, then the dive's air frames with its air stick. From a dive: its remaining air
	// frames with the stick straight back to shed speed. Then the way's slide frames before
	// the rollout, the stick straight back; B stays up so the rollout's press is a press.
	// Ends on the frame the rollout starts.
	auto onPyramid = [&]() { return _mario->floor != nullptr && _mario->floor->object == _pyramid; };
	if (_mario->action == ACT_WALKING)
	{
		auto ahead = Inputs::GetClosestInputByYawHau(way.diveYaw, 32, _camera->yaw);
		for (int n = 0; n < way.runFrames; n++)
		{
			AdvanceFrameWrite(Inputs(0, ahead.first, ahead.second));
			if (_mario->action != ACT_WALKING)
				return false;
		}
		AdvanceFrameWrite(Inputs(Buttons::B, ahead.first, ahead.second));
		if (_mario->action != ACT_DIVE)
			return false;
		// The dive's air frames: the stick straight back (the shortest dive), neutral, or at the
		// dive's yaw (the longest, some 500 units), the dive's distance being the fixer's to
		// choose along with its yaw.
		for (int n = 0; n < 60 && _mario->action == ACT_DIVE; n++)
		{
			std::pair<int8_t, int8_t> stick = way.diveAir == 0 ? Inputs::GetClosestInputByYawHau(int16_t(_mario->faceAngle[1] + 0x8000), 32, _camera->yaw)
				: way.diveAir == 1 ? std::pair<int8_t, int8_t>(0, 0) : ahead;
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
		}
		if (_mario->action != ACT_DIVE_SLIDE || _mario->floor == nullptr)
			return false;
		_pyramid = _mario->floor->object;
		if (_pyramid == nullptr || _pyramid->behavior != _pyramidBehavior)
			return false;
	}
	for (int n = 0; n < 60 && _mario->action == ACT_DIVE; n++)
	{
		auto back = Inputs::GetClosestInputByYawHau(int16_t(_mario->faceAngle[1] + 0x8000), 32, _camera->yaw);
		AdvanceFrameWrite(Inputs(0, back.first, back.second));
	}
	if (_mario->action != ACT_DIVE_SLIDE || !onPyramid())
		return false;
	for (int n = 0; n < way.slideFrames; n++)
	{
		auto back = Inputs::GetClosestInputByYawHau(int16_t(_mario->faceAngle[1] + 0x8000), 32, _camera->yaw);
		AdvanceFrameWrite(Inputs(0, back.first, back.second));
		if (_mario->action != ACT_DIVE_SLIDE || !onPyramid())
			return false;
	}
	return true;
}

bool BitFsAreFixer::Rollout(Landing& landing, const Sticks& fine, int frames)
{
	landing = Landing();
	const int F = _args.fineFrames;
	for (int i = 0; i < frames; i++)
	{
		int which = _rolloutFrames > 0 ? i - (_rolloutFrames - F) : -1;
		std::pair<int8_t, int8_t> stick = which >= 0 && which < F ? fine[size_t(which)] : Stick(_aim);
		AdvanceFrameWrite(Inputs(i == 0 ? Buttons::B : 0, stick.first, stick.second));
		if (IsRollout(_mario->action))
			continue;
		if (i == 0)
			return false;
		landing.frames = i + 1;
		landing.x = _mario->pos[0];
		landing.z = _mario->pos[2];
		landing.landed = _mario->action == ACT_FREEFALL_LAND_STOP && _mario->floor != nullptr && _mario->floor->object == _pyramid;
		return landing.landed;
	}
	return frames < 200; // still in the air after the frames asked for
}

bool BitFsAreFixer::AdvanceToRest(Rest& rest)
{
	rest = Rest();
	for (int n = 0; n < 200; n++)
	{
		std::array<float, 3> normal = { _pyramid->oTiltingPyramidNormalX, _pyramid->oTiltingPyramidNormalY, _pyramid->oTiltingPyramidNormalZ };
		std::array<float, 3> pos = { _mario->pos[0], _mario->pos[1], _mario->pos[2] };
		uint32_t action = _mario->action;
		AdvanceFrameWrite(Inputs(0, 0, 0));
		std::array<float, 3> normalNow = { _pyramid->oTiltingPyramidNormalX, _pyramid->oTiltingPyramidNormalY, _pyramid->oTiltingPyramidNormalZ };
		std::array<float, 3> posNow = { _mario->pos[0], _mario->pos[1], _mario->pos[2] };
		if (action == ACT_IDLE && _mario->action == ACT_IDLE && normalNow == normal && posNow == pos)
		{
			// The frame whose state the frame before already had: the cursor is on it, and the
			// diff ends before it.
			rest.reached = true;
			rest.frame = int64_t(GetCurrentFrame());
			rest.normal = normal;
			rest.pos = pos;
			AdjustedRemainderError(normal[0], _args.targetNx, rest.error[0], rest.steps[0]);
			AdjustedRemainderError(normal[2], _args.targetNz, rest.error[2], rest.steps[2]);
			return true;
		}
	}
	return false;
}

AdhocScriptStatus<BitFsAreFixer::Play> BitFsAreFixer::Measure(const Sticks& fine)
{
	return ExecuteAdhoc<Play>([&](Play* play)
	{
		play->sticks = fine;
		if (!Rollout(play->landing, fine) || !AdvanceToRest(play->rest))
			return false;
		play->distance = std::max(std::fabs(play->rest.error[0]), std::fabs(play->rest.error[2])) / _args.tolerance;
		return true;
	});
}

bool BitFsAreFixer::Solved(const Rest& rest) const
{
	return rest.reached && std::fabs(rest.error[0]) <= float(_args.tolerance) && std::fabs(rest.error[2]) <= float(_args.tolerance)
		&& std::abs(rest.steps[0]) % 2 == std::abs(rest.steps[2]) % 2
		&& rest.normal[0] * float(CornerSignX(_args)) > 0.0f && rest.normal[2] * float(CornerSignZ(_args)) > 0.0f
		&& std::fabs(rest.normal[0]) + std::fabs(rest.normal[2]) >= _args.minXzSum
		&& StepsReversibly(rest.normal[0], _args.targetNx, _args.minNormal, _args.maxNormal, _args.farNormal)
		&& StepsReversibly(rest.normal[2], _args.targetNz, _args.minNormal, _args.maxNormal, _args.farNormal);
}

int BitFsAreFixer::CornerSignX(const Args& args)
{
	if (args.quadrant == 0)
		return args.targetNx < 0.0f ? -1 : 1;
	return args.quadrant == 1 || args.quadrant == 2 ? 1 : -1;
}

int BitFsAreFixer::CornerSignZ(const Args& args)
{
	if (args.quadrant == 0)
		return args.targetNz < 0.0f ? -1 : 1;
	return args.quadrant == 1 || args.quadrant == 4 ? 1 : -1;
}

void BitFsAreFixer::Finish(const Rest& rest)
{
	CustomStatus.solved = true;
	CustomStatus.equilibriumFrame = rest.frame;
	CustomStatus.normal = rest.normal;
	CustomStatus.restPos = rest.pos;
	CustomStatus.adjustedRemainderError = rest.error;
	CustomStatus.incrementFrames = rest.steps;
}

// --- The model ------------------------------------------------------------------------------

void BitFsAreFixer::Jacobian(double x, double z, double j[2][2]) const
{
	// The goal the pyramid rests at: (dx, 500, dz) normalised.
	double dx = x - double(_pyramid->oPosX);
	double dz = z - double(_pyramid->oPosZ);
	double d2 = dx * dx + 500.0 * 500.0 + dz * dz;
	double d3 = d2 * std::sqrt(d2);
	j[0][0] = (d2 - dx * dx) / d3;
	j[0][1] = -dx * dz / d3;
	j[1][0] = -dx * dz / d3;
	j[1][1] = (d2 - dz * dz) / d3;
}

bool BitFsAreFixer::Correction(const Rest& rest, double& dx, double& dz) const
{
	double j[2][2];
	Jacobian(rest.pos[0], rest.pos[2], j);
	double ulpX = Ulp(_args.targetNx);
	double ulpZ = Ulp(_args.targetNz);
	int directionX = ScriptMath::Sign(_args.targetNx - rest.normal[0]);
	int directionZ = ScriptMath::Sign(_args.targetNz - rest.normal[2]);
	int countX = std::abs(rest.steps[0]);
	int countZ = std::abs(rest.steps[2]);

	// Zeroing the error moves the normal by the error itself; a whole band of 0.01 on one
	// axis changes that axis's step count by one, which is how the parity is fixed. Take the
	// admissible option that moves Mario least.
	bool found = false;
	double best = std::numeric_limits<double>::infinity();
	for (int i = -1; i <= 1; i++)
	{
		for (int k = -1; k <= 1; k++)
		{
			if (i != 0 && k != 0)
				continue;
			int stepsX = directionX != 0 ? countX - i * directionX : std::abs(i);
			int stepsZ = directionZ != 0 ? countZ - k * directionZ : std::abs(k);
			if (stepsX < 0 || stepsZ < 0 || stepsX % 2 != stepsZ % 2)
				continue;
			double px, pz;
			Solve(j, double(rest.error[0]) * ulpX + i * 0.01, double(rest.error[2]) * ulpZ + k * 0.01, px, pz);
			double cost = std::fabs(px) + std::fabs(pz);
			if (cost < best)
			{
				best = cost;
				dx = px;
				dz = pz;
				found = true;
			}
		}
	}
	return found;
}

bool BitFsAreFixer::AimAt(float x, float z, Landing& landing)
{
	auto play = [&](Aim aim, Landing& out)
	{
		Aim saved = _aim;
		_aim = aim;
		bool landed = ExecuteAdhoc([&]() { return Rollout(out, _fine); }).executed;
		_aim = saved;
		return landed;
	};

	Landing base;
	if (!play(_aim, base))
		return false;
	if (_rolloutFrames == 0)
	{
		// The first flight says how long a rollout lasts, which is where the fine frames sit;
		// every play from here on lays its sticks out the same way.
		_rolloutFrames = base.frames;
		if (!play(_aim, base))
			return false;
	}
	for (int iteration = 0; iteration < 4; iteration++)
	{
		double ex = double(x) - base.x;
		double ez = double(z) - base.z;
		if (std::fabs(ex) < 1.0 && std::fabs(ez) < 1.0)
			break;

		// The landing's response to the stick, from two more plays.
		const float delta = 0.1f;
		Landing forward, sideways;
		if (!play({ _aim.forward + delta, _aim.sideways }, forward) || !play({ _aim.forward, _aim.sideways + delta }, sideways))
			break;
		double j[2][2] = { { (forward.x - base.x) / delta, (sideways.x - base.x) / delta }, { (forward.z - base.z) / delta, (sideways.z - base.z) / delta } };
		if (j[0][0] * j[1][1] - j[0][1] * j[1][0] == 0.0)
			break;
		double stepForward, stepSideways;
		Solve(j, ex, ez, stepForward, stepSideways);
		Aim next = { float(_aim.forward + stepForward), float(_aim.sideways + stepSideways) };
		float mag = std::sqrt(next.forward * next.forward + next.sideways * next.sideways);
		if (mag > 1.0f)
		{
			next.forward /= mag;
			next.sideways /= mag;
		}
		Landing after;
		if (!play(next, after) || std::hypot(double(x) - after.x, double(z) - after.z) >= std::hypot(ex, ez))
			break;
		_aim = next;
		base = after;
	}
	landing = base;
	_rolloutFrames = base.frames;
	return true;
}

bool BitFsAreFixer::Response(const Landing& landing, const Rest& rest, double response[2][2])
{
	// Two sticks near the current one on the last air frame, moving the landing a little
	// forward and a little sideways; where each rests, against where the current one does.
	const size_t last = size_t(_args.fineFrames - 1);
	auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(_fine[last].first, _fine[last].second, _camera->yaw);
	uint16_t dYaw = uint16_t(yaw - uint16_t(_mario->faceAngle[1]));
	Aim current = { float(coss(dYaw) * (mag / 32.0f)), float(sins(dYaw) * (mag / 32.0f)) };
	Aim probes[2] = { { current.forward + 0.2f, current.sideways }, { current.forward, current.sideways + 0.08f } };
	double dLanding[2][2], dRest[2][2]; // a column per probe
	for (int k = 0; k < 2; k++)
	{
		Sticks sticks = _fine;
		sticks[last] = Stick(probes[k]);
		AdhocScriptStatus<Play> probe = Measure(sticks);
		if (!probe.executed)
			return false;
		dLanding[0][k] = double(probe.landing.x) - landing.x;
		dLanding[1][k] = double(probe.landing.z) - landing.z;
		dRest[0][k] = double(probe.rest.pos[0]) - rest.pos[0];
		dRest[1][k] = double(probe.rest.pos[2]) - rest.pos[2];
	}
	double det = dLanding[0][0] * dLanding[1][1] - dLanding[0][1] * dLanding[1][0];
	if (det == 0.0)
		return false;
	double inverse[2][2] = { { dLanding[1][1] / det, -dLanding[0][1] / det }, { -dLanding[1][0] / det, dLanding[0][0] / det } };
	for (int i = 0; i < 2; i++)
		for (int k = 0; k < 2; k++)
			response[i][k] = dRest[i][0] * inverse[0][k] + dRest[i][1] * inverse[1][k];
	return true;
}

// --- The fine frames: measure, predict, play ------------------------------------------------

bool BitFsAreFixer::Fine(float x, float z, const double response[2][2], Landing& landing, Rest& rest)
{
	// Where the current sticks' landing misses the target, along the face yaw and to its right.
	uint16_t faceYaw = uint16_t(_mario->faceAngle[1]);
	double dirX = sins(faceYaw), dirZ = coss(faceYaw);
	double perpX = sins(uint16_t(faceYaw + 0x4000)), perpZ = coss(uint16_t(faceYaw + 0x4000));
	double missX = double(x) - landing.x, missZ = double(z) - landing.z;
	Miss miss = { missX * dirX + missZ * dirZ, missX * perpX + missZ * perpZ };

	// d(normal) / d(landing): the model's Jacobian at the rest the target lands at, through the response.
	double g[2][2], j[2][2];
	Jacobian(landing.x + response[0][0] * (x - landing.x) + response[0][1] * (z - landing.z) + (rest.pos[0] - landing.x),
		landing.z + response[1][0] * (x - landing.x) + response[1][1] * (z - landing.z) + (rest.pos[2] - landing.z), g);
	for (int r = 0; r < 2; r++)
		for (int c = 0; c < 2; c++)
			j[r][c] = g[r][0] * response[0][c] + g[r][1] * response[1][c];

	const Sticks current = _fine;
	int played = 0;
	for (int widen = 0; widen < 3; widen++)
	{
		Lattices lattices;
		if (!MeasureLattices(current, miss, double(1 << widen), lattices))
			return false;
		Predictions predictions = Predict(lattices, current, dirX, dirZ, j);
		AdhocScriptStatus<Play> best = PlayCandidates(predictions, x, z, response, played);
		if (!best.executed)
			continue; // nothing predicted inside the cell: widen the boxes
		_fine = best.sticks;
		landing = best.landing;
		rest = best.rest;
		if (!Solved(rest))
			return false; // the rounds continue from the nearest rest played
		Apply(best.m64Diff);
		return true;
	}
	rest = Rest();
	return false;
}

std::vector<std::pair<int8_t, int8_t>> BitFsAreFixer::SticksNear(double forwardCenter, double sidewaysCenter, double halfForward,
	double halfSideways, std::pair<int8_t, int8_t> mustInclude) const
{
	uint16_t faceYaw = uint16_t(_mario->faceAngle[1]);
	int16_t cameraYaw = _camera->yaw;

	// Every distinct stick the game would read, whose forward and sideways effect on the
	// speed lies in the box; the nearest to the box's center first, at most sticksPerFrame.
	struct Pick
	{
		std::pair<int8_t, int8_t> stick;
		double distance;
	};
	std::vector<Pick> picks;
	std::unordered_set<uint64_t> seen;
	bool included = false;
	for (int x = -128; x <= 127; x++)
	{
		for (int y = -128; y <= 127; y++)
		{
			auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(int8_t(x), int8_t(y), cameraYaw);
			if (mag == 0.0f && (x != 0 || y != 0))
				continue;
			if (!seen.insert((uint64_t(uint16_t(yaw)) << 32) | std::bit_cast<uint32_t>(mag)).second)
				continue;
			uint16_t dYaw = uint16_t(yaw - faceYaw);
			double forward = 1.5 * coss(dYaw) * (mag / 32.0);
			double sideways = 10.0 * sins(dYaw) * (mag / 32.0);
			double ef = (forward - forwardCenter) / halfForward;
			double es = (sideways - sidewaysCenter) / halfSideways;
			bool isMust = int8_t(x) == mustInclude.first && int8_t(y) == mustInclude.second;
			if (!isMust && (std::fabs(ef) > 1.0 || std::fabs(es) > 1.0))
				continue;
			picks.push_back({ { int8_t(x), int8_t(y) }, isMust ? -1.0 : ef * ef + es * es });
			included |= isMust;
		}
	}
	if (!included)
		picks.push_back({ mustInclude, -1.0 });
	std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.distance < b.distance; });
	if (picks.size() > size_t(_args.sticksPerFrame))
		picks.resize(size_t(_args.sticksPerFrame));

	std::vector<std::pair<int8_t, int8_t>> sticks;
	sticks.reserve(picks.size());
	for (const Pick& pick : picks)
		sticks.push_back(pick.stick);
	return sticks;
}

std::vector<BitFsAreFixer::Effect> BitFsAreFixer::Lattice(bool last, const std::vector<std::pair<int8_t, int8_t>>& sticks)
{
	// Each stick's one frame from the same state, looked at and reverted: a frame that must
	// stay in the air, or the landing frame.
	std::vector<Effect> effects;
	effects.reserve(sticks.size());
	float x0 = _mario->pos[0];
	float z0 = _mario->pos[2];
	for (const std::pair<int8_t, int8_t>& stick : sticks)
	{
		TestAdhoc([&]()
		{
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
			if (last ? _mario->action == ACT_FREEFALL_LAND_STOP : IsRollout(_mario->action))
				effects.push_back({ stick, _mario->pos[0] - x0, _mario->pos[2] - z0, _mario->forwardVel, _mario->vel[0], _mario->vel[2] });
			return true;
		});
	}
	return effects;
}

bool BitFsAreFixer::MeasureLattices(const Sticks& current, const Miss& miss, double scale, Lattices& lattices)
{
	const int F = _args.fineFrames;
	const int multiplierSum = F * (F + 1) / 2; // a forward change on fine frame i moves every later fine frame too
	const double halfForward = std::max(0.3, 1.5 * std::fabs(miss.forward) / multiplierSum) * scale;
	const double halfSideways = std::max(0.6, 1.5 * std::fabs(miss.sideways) / F) * scale;
	const uint16_t faceYaw = uint16_t(_mario->faceAngle[1]);
	lattices.frames.assign(size_t(F), {});
	lattices.representative.assign(size_t(F), Effect());

	// The rollout flown to the first fine frame, then each frame's lattice from there along
	// the current sticks; all of it reverted.
	return ExecuteAdhoc([&]()
	{
		Landing flown;
		if (!Rollout(flown, current, _rolloutFrames - F) || !IsRollout(_mario->action))
			return false;
		lattices.x0 = _mario->pos[0];
		lattices.z0 = _mario->pos[2];
		for (int i = 0; i < F; i++)
		{
			// The box: around the current stick's effect, shifted by this frame's share of the miss.
			const std::pair<int8_t, int8_t>& stick = current[size_t(i)];
			auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, _camera->yaw);
			uint16_t dYaw = uint16_t(yaw - faceYaw);
			double centerForward = 1.5 * coss(dYaw) * (mag / 32.0) + miss.forward / multiplierSum;
			double centerSideways = 10.0 * sins(dYaw) * (mag / 32.0) + miss.sideways / F;
			std::vector<Effect>& frame = lattices.frames[size_t(i)];
			frame = Lattice(i == F - 1, SticksNear(centerForward, centerSideways, halfForward, halfSideways, stick));
			auto it = std::find_if(frame.begin(), frame.end(), [&](const Effect& e) { return e.stick == stick; });
			if (it == frame.end())
				return false;
			lattices.representative[size_t(i)] = *it;
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
		}
		return true;
	}).executed;
}

BitFsAreFixer::Predictions BitFsAreFixer::Predict(const Lattices& lattices, const Sticks& current, double dirX, double dirZ, const double j[2][2]) const
{
	const int F = _args.fineFrames;
	Predictions predictions;
	predictions.last = F - 1;
	predictions.tail = lattices.frames[size_t(F - 1)];
	for (int r = 0; r < 2; r++)
		for (int c = 0; c < 2; c++)
			predictions.j[r][c] = j[r][c];
	predictions.tolX = double(_args.tolerance) * Ulp(_args.targetNx);
	predictions.tolZ = double(_args.tolerance) * Ulp(_args.targetNz);
	predictions.bucketX = predictions.tolX / std::fabs(j[0][0]);
	predictions.bucketZ = predictions.tolZ / std::fabs(j[1][1]);

	// The landing frame moves a fraction of its velocity: the quarter steps before the floor.
	const Effect& tailRep = lattices.representative[size_t(F - 1)];
	double tailSpeed2 = double(tailRep.vx) * tailRep.vx + double(tailRep.vz) * tailRep.vz;
	double fraction = tailSpeed2 > 0.0 ? (double(tailRep.dx) * tailRep.vx + double(tailRep.dz) * tailRep.vz) / tailSpeed2 : 1.0;

	// Every combination of the leading frames: where it leaves Mario, plus what the forward
	// speed it leaves him with adds on the frames after (the drag applies at each frame's
	// start, the landing frame's share scaled by the fraction).
	const std::vector<Effect>& first = lattices.frames[0];
	if (F == 2)
	{
		for (const Effect& ea : first)
		{
			double extra = (double(Drag(ea.forwardVel)) - double(Drag(lattices.representative[0].forwardVel))) * fraction;
			Sticks sticks = current;
			sticks[0] = ea.stick;
			predictions.heads.push_back({ lattices.x0 + ea.dx + dirX * extra, lattices.z0 + ea.dz + dirZ * extra, sticks });
		}
	}
	else
	{
		const std::vector<Effect>& second = lattices.frames[1];
		predictions.heads.reserve(first.size() * second.size());
		for (const Effect& ea : first)
		{
			double carried = double(Drag(ea.forwardVel)) - double(Drag(lattices.representative[0].forwardVel));
			for (const Effect& eb : second)
			{
				float speed = float(carried + eb.forwardVel);
				double extra = carried + (double(Drag(speed)) - double(Drag(lattices.representative[1].forwardVel))) * fraction;
				Sticks sticks = current;
				sticks[0] = ea.stick;
				sticks[1] = eb.stick;
				predictions.heads.push_back({ lattices.x0 + ea.dx + eb.dx + dirX * extra, lattices.z0 + ea.dz + eb.dz + dirZ * extra, sticks });
			}
		}
	}
	predictions.grid.reserve(predictions.heads.size());
	for (uint32_t h = 0; h < predictions.heads.size(); h++)
	{
		const Predictions::Head& head = predictions.heads[h];
		predictions.grid[Bucket(int64_t(std::floor(head.x / predictions.bucketX)), int64_t(std::floor(head.z / predictions.bucketZ)))].push_back(h);
	}
	return predictions;
}

double BitFsAreFixer::Predictions::Distance(double px, double pz, double targetX, double targetZ) const
{
	double ex = px - targetX, ez = pz - targetZ;
	return std::max(std::fabs(j[0][0] * ex + j[0][1] * ez) / tolX, std::fabs(j[1][0] * ex + j[1][1] * ez) / tolZ);
}

std::vector<BitFsAreFixer::Predictions::Candidate> BitFsAreFixer::Predictions::Nearest(double targetX, double targetZ, int count) const
{
	std::vector<Candidate> candidates;
	for (const Effect& tailEffect : tail)
	{
		double wantX = targetX - tailEffect.dx, wantZ = targetZ - tailEffect.dz;
		int64_t ix = int64_t(std::floor(wantX / bucketX)), iz = int64_t(std::floor(wantZ / bucketZ));
		for (int64_t di = -1; di <= 1; di++)
		{
			for (int64_t dk = -1; dk <= 1; dk++)
			{
				auto it = grid.find(Bucket(ix + di, iz + dk));
				if (it == grid.end())
					continue;
				for (uint32_t h : it->second)
				{
					const Head& head = heads[h];
					double predicted = Distance(head.x + tailEffect.dx, head.z + tailEffect.dz, targetX, targetZ);
					if (predicted >= 1.0)
						continue;
					auto at = std::lower_bound(candidates.begin(), candidates.end(), predicted,
						[](const Candidate& k, double p) { return k.predicted < p; });
					if (at - candidates.begin() >= count)
						continue;
					Sticks sticks = head.sticks;
					sticks[size_t(last)] = tailEffect.stick;
					candidates.insert(at, { predicted, sticks });
					if (candidates.size() > size_t(count))
						candidates.pop_back();
				}
			}
		}
	}
	return candidates;
}

AdhocScriptStatus<BitFsAreFixer::Play> BitFsAreFixer::PlayCandidates(const Predictions& predictions, double x, double z, const double response[2][2], int& played)
{
	// Play the nearest predictions out, the first that rests inside the tolerance winning,
	// else the nearest rest. Where a play rests says how far the target itself was off, so
	// the target moves by that and the next candidates come from the same predictions for
	// the moved target; when the move is below the prediction's noise, the next-nearest
	// predictions for the same target are played instead.
	double targetX = x, targetZ = z;
	std::vector<Predictions::Candidate> candidates;
	size_t next = 0;
	bool retarget = true;
	return CompareAdhoc<Play, std::tuple<Sticks>>(
		[&](int64_t, std::tuple<Sticks>& params) //paramsGenerator
		{
			if (retarget)
			{
				candidates = predictions.Nearest(targetX, targetZ, _args.verifyPerRound);
				next = 0;
				retarget = false;
			}
			if (played >= 3 * _args.verifyPerRound || next >= candidates.size())
				return false;
			params = std::tuple(candidates[next++].sticks);
			return true;
		},
		[&](Play* play, Sticks sticks) //script
		{
			played++;
			play->sticks = sticks;
			if (!Rollout(play->landing, sticks) || !AdvanceToRest(play->rest))
				return false;
			play->distance = std::max(std::fabs(play->rest.error[0]), std::fabs(play->rest.error[2])) / _args.tolerance;
			double shiftRest[2], dx, dz;
			if (Correction(play->rest, shiftRest[0], shiftRest[1]))
			{
				Solve(response, shiftRest[0], shiftRest[1], dx, dz);
				if (std::fabs(dx) > 0.0005 || std::fabs(dz) > 0.0003)
				{
					targetX = double(play->landing.x) + dx;
					targetZ = double(play->landing.z) + dz;
					retarget = true;
				}
			}
			return true;
		},
		[](const AdhocScriptStatus<Play>* incumbent, const AdhocScriptStatus<Play>* challenger) //comparator
		{
			if (!challenger->executed)
				return incumbent;
			if (!incumbent->executed)
				return challenger;
			return challenger->distance < incumbent->distance ? challenger : incumbent;
		},
		[&](const AdhocScriptStatus<Play>* play) //terminator
		{
			return play->executed && Solved(play->rest);
		});
}
