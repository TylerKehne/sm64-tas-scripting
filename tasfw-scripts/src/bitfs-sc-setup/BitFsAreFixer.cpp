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
#include <set>
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

float BitFsAreFixer::BandValue(float target, int band)
{
	float value = target;
	for (int n = 0; n < std::abs(band); n++)
		value += band > 0 ? 0.01f : -0.01f;
	return value;
}

bool BitFsAreFixer::Stepped(float before, float after)
{
	return after == float(before + 0.01f) || after == float(before - 0.01f);
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

void BitFsAreFixer::RestingNormal(float dx, float dz, float& nx, float& nz)
{
	// bhv_tilting_inverted_pyramid_loop (tasfw-core/src/decomp/Pyramid.cpp) with Mario on the
	// platform: the goal (dx, 500, dz) normalised, its reciprocal taken in double and rounded.
	float d = std::sqrt(dx * dx + 500.0f * 500.0f + dz * dz);
	d = float(1.0 / d);
	nx = dx * d;
	nz = dz * d;
}

int BitFsAreFixer::LeastError(float target, int side, float minNormal, float maxNormal, float farNormal)
{
	// Each band of 0.01 within the range on the corner's side, and the floats around its value
	// (the error moves one ULP per float in the target's binade, two or four in the coarser
	// ones, so this many floats hold every error within MaxLeastError): the least error among
	// those that step reversibly over the whole range.
	int least = -1;
	const int bands = int(maxNormal / 0.01f) + 2;
	for (int band = -bands; band <= bands; band++)
	{
		const float center = BandValue(target, band);
		if (ScriptMath::Sign(center) != side || std::fabs(center) < minNormal || std::fabs(center) > maxNormal)
			continue;
		const int span = int(float(MaxLeastError) * Ulp(target) / Ulp(center)) + 8;
		float value = center;
		for (int n = 0; n < span; n++)
			value = std::nextafter(value, -std::numeric_limits<float>::infinity());
		for (int n = -span; n <= span; n++, value = std::nextafter(value, std::numeric_limits<float>::infinity()))
		{
			float error;
			int steps;
			AdjustedRemainderError(value, target, error, steps);
			if (!std::isfinite(error) || std::fabs(error) > float(MaxLeastError) || (least >= 0 && std::fabs(error) >= float(least)))
				continue;
			if (StepsReversibly(value, target, minNormal, maxNormal, farNormal))
				least = int(std::lround(std::fabs(error)));
		}
		if (least == 0)
			break;
	}
	return least;
}

// --- The lifecycle --------------------------------------------------------------------------

bool BitFsAreFixer::validation()
{
	_mario = *(MarioState**)(ReadState("gMarioState"));
	MarioState* marioState = _mario;
	_pyramidBehavior = (const BehaviorScript*)(ReadState("bhvBitfsTiltingInvertedPyramid"));
	if (!(_args.fineFrames >= 2 && _args.fineFrames <= MaxFineFrames && _args.tolerance >= 0
			&& _args.minNormal > 0.0f && _args.maxNormal > _args.minNormal && _args.maxNormal < 1.0f && _args.farNormal >= 0.0f && _args.farNormal < 1.0f
			&& _args.minXzSum >= 0.0f && _args.minXzSum < 2.0f * _args.maxNormal && _args.quadrant >= 0 && _args.quadrant <= 4))
	{
		CustomStatus.refusal = "an argument is out of range";
		return false;
	}

	// Whether the target can be held at all, before any frame: the smallest error each axis
	// admits under the reversibility rule. Each axis is set at a float position on its own
	// curve, x by the rest and z by the turn, and the curves run through the whole band, so an
	// error the arithmetic can hold is one the search can set.
	CustomStatus.leastError[0] = LeastError(_args.targetNx, CornerSignX(_args), _args.minNormal, _args.maxNormal, _args.farNormal);
	CustomStatus.leastError[2] = LeastError(_args.targetNz, CornerSignZ(_args), _args.minNormal, _args.maxNormal, _args.farNormal);
	for (int axis : { 0, 2 })
	{
		if (CustomStatus.leastError[size_t(axis)] < 0 || CustomStatus.leastError[size_t(axis)] > _args.tolerance)
		{
			CustomStatus.refusal = "no normal within the tolerance of the target steps reversibly over the range";
			return false;
		}
	}

	if (marioState->floor == nullptr)
	{
		CustomStatus.refusal = "Mario has no floor";
		return false;
	}
	if (marioState->action == ACT_WALKING)
	{
		// On the run before the dive: the pyramid is the one the movie's own dive lands on.
		_pyramid = LandingPyramid();
	}
	else if (marioState->action == ACT_DIVE || marioState->action == ACT_DIVE_SLIDE)
	{
		_pyramid = marioState->floor->object;
	}
	else
	{
		CustomStatus.refusal = "the start frame is not the run before the dive, the dive or its slide";
		return false;
	}
	if (_pyramid == nullptr || _pyramid->behavior != _pyramidBehavior)
	{
		CustomStatus.refusal = "no tilting pyramid under Mario or near him";
		return false;
	}
	return true;
}

bool BitFsAreFixer::execution()
{
	_mario = *(MarioState**)(ReadState("gMarioState"));
	_camera = *(Camera**)(ReadState("gCamera"));

	// Every way onto the platform, played once with a straight rollout to where it rests
	// (Ways), nearest the rest asked for first. For each, the landing search for a rest that
	// sets x (SearchLanding: the settle's rounding puts rests on a staircase coarser than the
	// floats, and about one rest in five near the x curve lies on it), then from that rest the
	// run and turn that set z (SearchTurn); a way whose landing left idle fails is tried again
	// with a walk after the landing, which settles onto another staircase. Each way's approach,
	// landing search and turn search is one block, kept when the turn sets z and reverted
	// otherwise.
	std::vector<Way> ways = Ways();
	CustomStatus.ways = int(ways.size());
	float askX, askZ;
	RestAsked(askX, askZ);
	std::sort(ways.begin(), ways.end(), [&](const Way& a, const Way& b)
		{
			return std::hypot(double(a.rest.pos[0]) - askX, double(a.rest.pos[2]) - askZ) < std::hypot(double(b.rest.pos[0]) - askX, double(b.rest.pos[2]) - askZ);
		});
	if (ways.size() > MaxWays)
		ways.resize(MaxWays);
	for (const Way& way : ways)
	{
		for (const Walk& walk : Walks)
		{
			_walk = walk;
			if (ModifyAdhoc([&]() { return Approach(way) && SearchLanding(askX, askZ) && SearchTurn(); }).executed)
			{
				CustomStatus.runFrames = way.runFrames;
				CustomStatus.diveYaw = way.diveYaw;
				CustomStatus.diveAir = way.diveAir;
				CustomStatus.slideFrames = way.slideFrames;
				CustomStatus.walkFrames = walk.frames;
				CustomStatus.walkYaw = walk.dYaw;
				return true;
			}
		}
		_walk = Walk();
	}
	if (_nearestTurn.reached)
		Finish(_nearestTurn, false);
	else if (_nearest.reached)
	{
		// No turn was ever reached: the nearest rest, in the hand-over's place.
		Turn turn;
		turn.reached = true;
		turn.frame = _nearest.frame;
		turn.pos = _nearest.pos;
		turn.normal = _nearest.normal;
		turn.error = _nearest.error;
		turn.steps = _nearest.steps;
		Finish(turn, false);
	}
	return false;
}

std::vector<BitFsAreFixer::Way> BitFsAreFixer::Ways()
{
	// From a dive slide only the slide frames vary. From the run before the dive, its length,
	// the dive's yaw (the movie's and up to MaxYawSteps steps of 1024 to either side) and its
	// air stick (straight back lands about 280 units out, neutral 340, at the yaw 500) vary
	// too, since the rollout can only land along the line the dive and the slide give it.
	// Each way is played once with a straight rollout to its rest, in a block that reverts;
	// the ways that rest on the platform are kept, with where they rest.
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
						if (rests)
							ways.push_back(way);
					}
			}
	return ways;
}

void BitFsAreFixer::RestAsked(float& x, float& z) const
{
	// The hand-over asked for, the run's length back along x: the run from the rest steps x
	// outward in the leg's direction (LegSignX) and the turn then leads z inward, which is the
	// oscillation's first leg from a hand-over whose steeper axis is z (SolvedTurn requires
	// it), so the hand-over is asked for on the z side of the diagonal by a margin and the
	// rest RunShift inward of it along x.
	if (_args.restX != 0.0f || _args.restZ != 0.0f)
	{
		x = _args.restX - float(LegSignX()) * float(RunShift);
		z = _args.restZ;
		ParityAsked(x, z);
		return;
	}
	// The corner's diagonal at the radius whose resting tilt is the floor (or twice minNormal,
	// which each axis needs anyway) plus 0.06, of which the run's drift takes about 0.02 (z
	// tracks a goal that shrinks as Mario runs out along x) and the parity's band another
	// 0.01 (ParityAsked): on the diagonal each axis of
	// the normal is half the tilt S, r / (sqrt 2 d) with d the distance to the point 500
	// below the home, so r = 500 (S / sqrt 2) / sqrt(1 - S^2 / 2).
	const double s = std::min(double(std::max(_args.minXzSum, 2.0f * _args.minNormal)) + 0.06, 1.2);
	const double r = 500.0 * (s / std::sqrt(2.0)) / std::sqrt(1.0 - s * s / 2.0);
	x = float(_pyramid->oPosX + CornerSignX(_args) * r / std::sqrt(2.0) - LegSignX() * RunShift);
	z = float(_pyramid->oPosZ + CornerSignZ(_args) * (r / std::sqrt(2.0) + ZMargin));
	ParityAsked(x, z);
}

void BitFsAreFixer::ParityAsked(float& x, float& z) const
{
	// The step parities at the turn must be equal: x's is the rest's goal's, flipped once a
	// frame to the turn (the fine frames and the turn's two frames before z's last snap), z's
	// the hand-over goal's. When the ask's would differ, the rest is asked one x band toward
	// the home (the goal's 0.01 over its slope, some six units), which flips x's; the turn's
	// frames vary by one, which the extra run frame then covers, where extra frames alone
	// would run the hand-over out of the diagonal's band.
	float gx, gz, error;
	int stepsX, stepsZ;
	RestingNormal(x - _pyramid->oPosX, z - _pyramid->oPosZ, gx, gz);
	AdjustedRemainderError(gx, _args.targetNx, error, stepsX);
	RestingNormal(float(x + LegSignX() * RunShift) - _pyramid->oPosX, z - _pyramid->oPosZ, gx, gz);
	AdjustedRemainderError(gz, _args.targetNz, error, stepsZ);
	if ((std::abs(stepsX) + _args.fineFrames + 2) % 2 == std::abs(stepsZ) % 2)
		return;
	double j[2][2];
	JacobianAt(double(x) - _pyramid->oPosX, double(z) - _pyramid->oPosZ, j);
	x = float(x - LegSignX() * 0.01 / j[0][0]);
}

double BitFsAreFixer::DistanceToAsked(float x, float z, bool handover) const
{
	if (_args.restX != 0.0f || _args.restZ != 0.0f)
	{
		float askX, askZ;
		RestAsked(askX, askZ);
		if (handover)
		{
			askX = _args.restX;
			askZ = _args.restZ;
		}
		return std::hypot(double(x) - askX, double(z) - askZ);
	}
	if (!handover)
	{
		float askX, askZ;
		RestAsked(askX, askZ);
		return std::hypot(double(x) - askX, double(z) - askZ);
	}
	// The diagonal's own tilt is the hand-over's to choose: the distance is to the line from
	// the home along the corner's diagonal.
	const double dx = double(x) - _pyramid->oPosX;
	const double dz = double(z) - _pyramid->oPosZ;
	return std::fabs(dx * CornerSignZ(_args) - dz * CornerSignX(_args)) / std::sqrt(2.0);
}

bool BitFsAreFixer::SearchLanding(float askX, float askZ)
{
	CustomStatus.rounds = 0;
	_rolloutFrames = 0;
	_aim = Aim();
	_fine = Sticks();

	// The constant stick aimed until the rest is within a unit of the rest asked for (AimAt);
	// the play at that aim is kept when its rest sets x.
	AdhocScriptStatus<Play> aimed;
	if (!AimAt(askX, askZ, aimed) || std::hypot(double(askX) - aimed.rest.pos[0], double(askZ) - aimed.rest.pos[2]) > NearAim)
		return false; // out of the constant stick's reach: the fine frames move the rest by a unit or two
	Landing landing = aimed.landing;
	Rest rest = aimed.rest;
	if (SolvedRest(rest))
	{
		Apply(aimed.m64Diff);
		RestFound(rest);
		return true;
	}

	for (int round = 1; round <= _args.maxRounds; round++)
	{
		CustomStatus.rounds = round;

		// The landing that rests on the x curve: its point nearest the rest (CurvePoint), the
		// rest's own x and its z moved by the error over the goal's slope in z, a few float
		// steps, through the response of the rest to the landing measured here (the platform
		// tilts from near flat to the resting normal after the landing and carries Mario with
		// it, by an amount that depends on where he landed). Beyond the fine frames' reach,
		// the constant stick is aimed at the rest asked for again first.
		double response[2][2];
		if (!Response(landing, rest, response))
			return false;
		float curveX, curveZ;
		CurvePoint(rest, curveX, curveZ);
		double dx, dz;
		Solve(response, double(curveX) - rest.pos[0], double(curveZ) - rest.pos[2], dx, dz);
		if (std::fabs(dx) > 1.0 || std::fabs(dz) > 1.0)
		{
			_fine = Sticks();
			if (!AimAt(askX, askZ, aimed))
				return false;
			landing = aimed.landing;
			rest = aimed.rest;
			if (SolvedRest(rest))
			{
				Apply(aimed.m64Diff);
				RestFound(rest);
				return true;
			}
			if (!Response(landing, rest, response))
				return false;
			CurvePoint(rest, curveX, curveZ);
			Solve(response, double(curveX) - rest.pos[0], double(curveZ) - rest.pos[2], dx, dz);
		}
		const float x = float(landing.x + dx);
		const float z = float(landing.z + dz);

		if (Fine(x, z, response, landing, rest))
		{
			RestFound(rest);
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

Object* BitFsAreFixer::LandingPyramid()
{
	Object* pyramid = nullptr;
	ExecuteAdhoc([&]()
	{
		for (int n = 0; n < 60; n++)
		{
			AdvanceFrameRead();
			if (_mario->action == ACT_DIVE_SLIDE && _mario->floor != nullptr)
			{
				pyramid = _mario->floor->object;
				break;
			}
			if (_mario->action != ACT_WALKING && _mario->action != ACT_DIVE)
				break;
		}
		return false; // looked at, never kept
	});
	return pyramid != nullptr && pyramid->behavior == _pyramidBehavior ? pyramid : nullptr;
}

const std::vector<BitFsAreFixer::StickEffect>& BitFsAreFixer::StickEffects()
{
	static const std::vector<StickEffect> effects = []()
	{
		std::vector<StickEffect> list;
		std::unordered_set<uint64_t> seen;
		for (int x = -128; x <= 127; x++)
			for (int y = -128; y <= 127; y++)
			{
				auto [yaw, mag] = Inputs::GetIntendedYawMagFromInput(int8_t(x), int8_t(y), 0);
				if (mag == 0.0f || !seen.insert((uint64_t(uint16_t(yaw)) << 32) | std::bit_cast<uint32_t>(mag)).second)
					continue;
				list.push_back({ yaw, mag / 32.0f, { int8_t(x), int8_t(y) } });
			}
		std::sort(list.begin(), list.end(), [](const StickEffect& a, const StickEffect& b)
			{ return uint16_t(a.baseYaw) != uint16_t(b.baseYaw) ? uint16_t(a.baseYaw) < uint16_t(b.baseYaw) : a.mag < b.mag; });
		return list;
	}();
	return effects;
}

std::pair<int8_t, int8_t> BitFsAreFixer::Stick(Aim aim) const
{
	double mag = std::sqrt(double(aim.forward) * aim.forward + double(aim.sideways) * aim.sideways);
	if (mag == 0.0)
		return { 0, 0 };
	if (mag > 1.0)
	{
		aim.forward = float(aim.forward / mag);
		aim.sideways = float(aim.sideways / mag);
		mag = 1.0;
	}
	// The sticks in yaw order outward from the aim's yaw, each one's effect (coss(dYaw) * mag
	// forward, sins(dYaw) * mag sideways) against the aim, as far as a stick's yaw alone could
	// still bring it nearer than the nearest so far.
	const uint16_t faceYaw = uint16_t(_mario->faceAngle[1]);
	const uint16_t cameraYaw = uint16_t(_camera->yaw);
	const uint16_t target = uint16_t(faceYaw + uint16_t(atan2s(aim.forward, aim.sideways)) - cameraYaw);
	const std::vector<StickEffect>& effects = StickEffects();
	const size_t n = effects.size();
	size_t up = size_t(std::lower_bound(effects.begin(), effects.end(), target, [](const StickEffect& e, uint16_t yaw) { return uint16_t(e.baseYaw) < yaw; }) - effects.begin()) % n;
	size_t down = (up + n - 1) % n;
	auto apart = [&](size_t i) { return std::abs(int(int16_t(uint16_t(effects[i].baseYaw) - target))); };
	std::pair<int8_t, int8_t> best = { 0, 0 };
	double bestDistance = mag * mag; // the neutral stick's
	for (size_t scanned = 0; scanned < n; scanned++)
	{
		const size_t i = apart(up) <= apart(down) ? up : down;
		const double theta = apart(i) * (2.0 * 3.14159265358979323846 / 65536.0);
		const double bound = theta >= 3.14159265358979323846 / 2.0 ? mag : mag * std::sin(theta);
		if (bound * bound >= bestDistance)
			break;
		const StickEffect& e = effects[i];
		const int16_t dYaw = int16_t(uint16_t(e.baseYaw) + cameraYaw - faceYaw);
		const double forward = double(e.mag) * coss(dYaw) - aim.forward;
		const double sideways = double(e.mag) * sins(dYaw) - aim.sideways;
		const double distance = forward * forward + sideways * sideways;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = e.stick;
		}
		if (i == up)
			up = (up + 1) % n;
		else
			down = (down + n - 1) % n;
	}
	return best;
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
		Object* landedOn = _mario->floor->object;
		if (landedOn == nullptr || landedOn->behavior != _pyramidBehavior)
			return false;
		if (landedOn != _pyramid)
			return false; // not the pyramid the movie's dive lands on, whose home the rest is asked from
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
	// The plan's stick is one stick for the rollout, from the yaws at its start (Mario does not
	// turn in the air).
	landing = Landing();
	const int F = _args.fineFrames;
	const std::pair<int8_t, int8_t> plan = Stick(_aim);
	for (int i = 0; i < frames; i++)
	{
		int which = _rolloutFrames > 0 ? i - (_rolloutFrames - F) : -1;
		std::pair<int8_t, int8_t> stick = which >= 0 && which < F ? fine[size_t(which)] : plan;
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
	// The walk after the landing, when the try has one (Walks): the stick at its yaw from
	// Mario's facing at the land, for its frames.
	const uint16_t faceYaw = uint16_t(_mario->faceAngle[1]);
	for (int n = 0; n < _walk.frames; n++)
	{
		auto stick = Inputs::GetClosestInputByYawHau(int16_t(faceYaw + _walk.dYaw), 32, _camera->yaw);
		AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
	}
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
			if (!_nearest.reached || std::fabs(rest.error[0]) < std::fabs(_nearest.error[0]))
				_nearest = rest;
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
		play->distance = std::fabs(play->rest.error[0]);
		return true;
	});
}

bool BitFsAreFixer::SolvedRest(const Rest& rest) const
{
	// The rest sets x: its error within the tolerance, surviving the oscillations' crossings,
	// in the corner, and near the rest asked for (a landing can rest on the curve far from it,
	// further off than the run then brings the hand-over to the diagonal).
	return rest.reached && std::fabs(rest.error[0]) <= float(_args.tolerance)
		&& rest.normal[0] * float(CornerSignX(_args)) > 0.0f
		&& StepsReversibly(rest.normal[0], _args.targetNx, _args.minNormal, _args.maxNormal, _args.farNormal)
		&& DistanceToAsked(rest.pos[0], rest.pos[2], false) <= NearRest;
}

void BitFsAreFixer::CurvePoint(const Rest& rest, float& x, float& z) const
{
	// The error is (target - value) in ULPs, and a rest's goal moved by that puts the chain on
	// the target: along z the goal's x moves a fraction of an ULP per float step (sixteen along
	// x), so the point is the rest's x with its z moved by the error over the slope; on an
	// axis, where the slope in z vanishes, x moves instead.
	double j[2][2];
	Jacobian(rest.pos[0], rest.pos[2], j);
	const double move = double(rest.error[0]) * Ulp(_args.targetNx);
	x = rest.pos[0];
	z = rest.pos[2];
	if (std::fabs(j[0][1]) > 1e-6)
		z = float(rest.pos[2] + move / j[0][1]);
	else
		x = float(rest.pos[0] + move / j[0][0]);
}

void BitFsAreFixer::RestFound(const Rest& rest)
{
	CustomStatus.restFrame = rest.frame;
	CustomStatus.restPos = rest.pos;
	CustomStatus.restNormal = rest.normal;
	CustomStatus.restErrorX = rest.error[0];
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

void BitFsAreFixer::Finish(const Turn& turn, bool solved)
{
	CustomStatus.solved = solved;
	CustomStatus.handoverFrame = turn.frame;
	CustomStatus.normal = turn.normal;
	CustomStatus.handoverPos = turn.pos;
	CustomStatus.adjustedRemainderError = turn.error;
	CustomStatus.incrementFrames = turn.steps;
	CustomStatus.framesAfterRest = turn.framesAfterRest;
}

// --- The model ------------------------------------------------------------------------------

void BitFsAreFixer::Jacobian(double x, double z, double j[2][2]) const
{
	JacobianAt(x - double(_pyramid->oPosX), z - double(_pyramid->oPosZ), j);
}

void BitFsAreFixer::JacobianAt(double dx, double dz, double j[2][2])
{
	// The goal the pyramid rests at: (dx, 500, dz) normalised.
	double d2 = dx * dx + 500.0 * 500.0 + dz * dz;
	double d3 = d2 * std::sqrt(d2);
	j[0][0] = (d2 - dx * dx) / d3;
	j[0][1] = -dx * dz / d3;
	j[1][0] = -dx * dz / d3;
	j[1][1] = (d2 - dz * dz) / d3;
}

bool BitFsAreFixer::AimAt(float x, float z, AdhocScriptStatus<Play>& play)
{
	// Newton on the constant stick, each play the rollout and its settle (Measure): the rest is
	// what is aimed, through the platform's carry after the landing. A step that rests no
	// nearer is halved, twice, before the iteration ends where it was.
	auto playAt = [&](Aim aim, AdhocScriptStatus<Play>& out)
	{
		Aim saved = _aim;
		_aim = aim;
		out = Measure(_fine);
		_aim = saved;
		return out.executed;
	};
	auto miss = [&](const AdhocScriptStatus<Play>& at) { return std::hypot(double(x) - at.rest.pos[0], double(z) - at.rest.pos[2]); };

	AdhocScriptStatus<Play> base;
	if (!playAt(_aim, base))
		return false;
	if (_rolloutFrames == 0)
	{
		// The first flight says how long it lasts, which is where the fine frames sit; every
		// play from here on lays its sticks out the same way.
		_rolloutFrames = base.landing.frames;
		if (!playAt(_aim, base))
			return false;
	}
	for (int iteration = 0; iteration < 4; iteration++)
	{
		const double ex = double(x) - base.rest.pos[0];
		const double ez = double(z) - base.rest.pos[2];
		if (std::fabs(ex) < 1.0 && std::fabs(ez) < 1.0)
			break;

		// The rest's response to the stick, from two more plays.
		const float delta = 0.1f;
		AdhocScriptStatus<Play> forward, sideways;
		if (!playAt({ _aim.forward + delta, _aim.sideways }, forward) || !playAt({ _aim.forward, _aim.sideways + delta }, sideways))
			break;
		double j[2][2] = { { (forward.rest.pos[0] - base.rest.pos[0]) / delta, (sideways.rest.pos[0] - base.rest.pos[0]) / delta },
			{ (forward.rest.pos[2] - base.rest.pos[2]) / delta, (sideways.rest.pos[2] - base.rest.pos[2]) / delta } };
		if (j[0][0] * j[1][1] - j[0][1] * j[1][0] == 0.0)
			break;
		double stepForward, stepSideways;
		Solve(j, ex, ez, stepForward, stepSideways);
		bool nearer = false;
		for (double fraction : { 1.0, 0.5, 0.25 })
		{
			Aim next = { float(_aim.forward + fraction * stepForward), float(_aim.sideways + fraction * stepSideways) };
			const float mag = std::sqrt(next.forward * next.forward + next.sideways * next.sideways);
			if (mag > 1.0f)
			{
				next.forward /= mag;
				next.sideways /= mag;
			}
			AdhocScriptStatus<Play> after;
			if (!playAt(next, after) || miss(after) >= miss(base))
				continue;
			_aim = next;
			base = after;
			nearer = true;
			break;
		}
		if (!nearer)
			break;
	}
	play = base;
	_rolloutFrames = base.landing.frames;
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
		if (!SolvedRest(rest))
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

	// Every distinct stick the game would read (StickEffects, and the neutral one), whose
	// forward and sideways effect on the speed lies in the box; the nearest to the box's
	// center first, at most sticksPerFrame.
	struct Pick
	{
		std::pair<int8_t, int8_t> stick;
		double distance;
	};
	std::vector<Pick> picks;
	bool included = false;
	auto consider = [&](std::pair<int8_t, int8_t> stick, double forward, double sideways)
	{
		double ef = (forward - forwardCenter) / halfForward;
		double es = (sideways - sidewaysCenter) / halfSideways;
		bool isMust = stick == mustInclude;
		if (!isMust && (std::fabs(ef) > 1.0 || std::fabs(es) > 1.0))
			return;
		picks.push_back({ stick, isMust ? -1.0 : ef * ef + es * es });
		included |= isMust;
	};
	consider({ 0, 0 }, 0.0, 0.0);
	for (const StickEffect& e : StickEffects())
	{
		uint16_t dYaw = uint16_t(uint16_t(e.baseYaw) + uint16_t(cameraYaw) - faceYaw);
		consider(e.stick, 1.5 * coss(dYaw) * e.mag, 10.0 * sins(dYaw) * e.mag);
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
	// The cell the predictions are looked up in: the tolerance, or the prediction's own
	// resolution where the tolerance is finer than it (NoiseX, NoiseZ, through d(normal) / d(landing)).
	predictions.tolX = std::max(double(_args.tolerance) * Ulp(_args.targetNx), NoiseX * std::fabs(j[0][0]));
	predictions.tolZ = std::max(double(_args.tolerance) * Ulp(_args.targetNz), NoiseZ * std::fabs(j[1][1]));
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
					const double px = head.x + tailEffect.dx;
					const double pz = head.z + tailEffect.dz;
					double predicted = Distance(px, pz, targetX, targetZ);
					if (predicted >= 1.0)
						continue;
					auto at = std::lower_bound(candidates.begin(), candidates.end(), predicted,
						[](const Candidate& k, double p) { return k.predicted < p; });
					if (at - candidates.begin() >= count)
						continue;
					// One candidate per predicted landing: another combination predicted to land
					// on the same floats lands the same and would only be played again.
					const float fx = float(px), fz = float(pz);
					if (std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& k) { return k.x == fx && k.z == fz; }))
						continue;
					Sticks sticks = head.sticks;
					sticks[size_t(last)] = tailEffect.stick;
					candidates.insert(at, { predicted, fx, fz, sticks });
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
	// Play the nearest predictions out, the first whose rest sets x winning, else the nearest
	// rest. Where a play rests says how far the landing target itself was off one that rests
	// on the x curve (the curve's point nearest that rest), so the target moves by that and
	// the next candidates come from the same predictions for the moved target; when the move
	// is below the prediction's noise, the next-nearest predictions for the same target are
	// played instead, each predicted landing once.
	double targetX = x, targetZ = z;
	std::vector<Predictions::Candidate> candidates;
	std::set<std::pair<float, float>> playedLandings; // the landings predicted for the candidates played, over every retarget: a moved target brings the same combinations back
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
			while (played < 3 * _args.verifyPerRound && next < candidates.size())
			{
				const Predictions::Candidate& candidate = candidates[next++];
				if (!playedLandings.insert({ candidate.x, candidate.z }).second)
					continue;
				params = std::tuple(candidate.sticks);
				return true;
			}
			return false;
		},
		[&](Play* play, Sticks sticks) //script
		{
			played++;
			play->sticks = sticks;
			if (!Rollout(play->landing, sticks) || !AdvanceToRest(play->rest))
				return false;
			play->distance = std::fabs(play->rest.error[0]);
			float curveX, curveZ;
			CurvePoint(play->rest, curveX, curveZ);
			double dx, dz;
			Solve(response, double(curveX) - play->rest.pos[0], double(curveZ) - play->rest.pos[2], dx, dz);
			if (std::fabs(dx) > NoiseX || std::fabs(dz) > NoiseZ)
			{
				targetX = double(play->landing.x) + dx;
				targetZ = double(play->landing.z) + dz;
				retarget = true;
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
			return play->executed && SolvedRest(play->rest);
		});
}

// --- The turn: the run from the rest and the turn that sets z -------------------------------

int BitFsAreFixer::LegSignX() const
{
	// The direction the oscillation's first leg runs on each axis from a hand-over whose
	// steeper axis is z (Scattershot_BitfsDr::FirstLeg_1f): x toward the corner, z away from
	// it. The run from the rest steps x that way and the turn leads z that way, so the
	// hand-over is a leg in progress.
	return CornerSignX(_args);
}

int BitFsAreFixer::LegSignZ() const
{
	return -CornerSignZ(_args);
}

std::pair<int8_t, int8_t> BitFsAreFixer::StickToward(int16_t yaw) const
{
	return Inputs::GetClosestInputByYawHau(yaw, 32, _camera->yaw);
}

bool BitFsAreFixer::PlayTurn(const Sticks& fine, Turn& turn)
{
	// From the rest: the run's extra frames with the stick along x in the leg's direction and
	// its fine frames with their sticks, x stepping every frame and z snapping to the goal of
	// Mario's position the frame before; then the turn, the full stick at the chord's yaw,
	// until z has stepped twice, in the leg's direction, after its last snap. The hand-over is
	// that last snap's frame, and the position the frame before it is what set z. A frame that
	// is not a walk on the pyramid, or on which x does not step, or on which z neither snaps
	// nor steps, ends the play.
	turn = Turn();
	const int F = _args.fineFrames;
	const int64_t restFrame = int64_t(GetCurrentFrame());
	const std::pair<int8_t, int8_t> run = StickToward(atan2s(0.0f, float(LegSignX())));
	const std::pair<int8_t, int8_t> chord = StickToward(atan2s(float(LegSignZ()), float(LegSignX())));
	float nxBefore = _pyramid->oTiltingPyramidNormalX;
	float nzBefore = _pyramid->oTiltingPyramidNormalZ;
	std::array<float, 3> posBefore = { _mario->pos[0], _mario->pos[1], _mario->pos[2] };
	int afterSnap = -1; // steps of z since its last snap; -1 before any
	int played = 0;
	auto frame = [&](std::pair<int8_t, int8_t> stick)
	{
		float goalX, goalZ;
		RestingNormal(posBefore[0] - _pyramid->oPosX, posBefore[2] - _pyramid->oPosZ, goalX, goalZ);
		AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
		if (_mario->action != ACT_WALKING || _mario->floor == nullptr || _mario->floor->object != _pyramid)
			return false;
		const float nx = _pyramid->oTiltingPyramidNormalX;
		const float nz = _pyramid->oTiltingPyramidNormalZ;
		// On the first frame the goal is still the rest's and the normal stays snapped to it;
		// from the second, x steps in the leg's direction every frame and never snaps again.
		if (played++ == 0 ? nx != goalX : (!Stepped(nxBefore, nx) || (nx - nxBefore) * float(LegSignX()) < 0.0f))
			return false;
		if (nz == goalZ)
		{
			afterSnap = 0;
			turn.frame = int64_t(GetCurrentFrame());
			turn.framesAfterRest = int(turn.frame - restFrame);
			turn.posBefore = posBefore;
			turn.pos = { _mario->pos[0], _mario->pos[1], _mario->pos[2] };
			turn.normal = { nx, _pyramid->oTiltingPyramidNormalY, nz };
		}
		else if (Stepped(nzBefore, nz) && afterSnap >= 0 && (nz - nzBefore) * float(LegSignZ()) > 0.0f)
			afterSnap++;
		else
			return false;
		nxBefore = nx;
		nzBefore = nz;
		posBefore = { _mario->pos[0], _mario->pos[1], _mario->pos[2] };
		return true;
	};
	for (int n = 0; n < _extraRun; n++)
		if (!frame(run))
			return false;
	for (int i = 0; i < F; i++)
		if (!frame(fine[size_t(i)]))
			return false;
	for (int n = 0; n < MaxTurnFrames && afterSnap < 2; n++)
		if (!frame(chord))
			return false;
	if (afterSnap < 2)
		return false;
	turn.reached = true;
	turn.stepping = true;
	AdjustedRemainderError(turn.normal[0], _args.targetNx, turn.error[0], turn.steps[0]);
	AdjustedRemainderError(turn.normal[2], _args.targetNz, turn.error[2], turn.steps[2]);
	if (!_nearestTurn.reached || std::max(std::fabs(turn.error[0]), std::fabs(turn.error[2])) < std::max(std::fabs(_nearestTurn.error[0]), std::fabs(_nearestTurn.error[2])))
		_nearestTurn = turn;
	return true;
}

AdhocScriptStatus<BitFsAreFixer::TurnPlay> BitFsAreFixer::MeasureTurn(const Sticks& fine)
{
	AdhocScriptStatus<TurnPlay> play = ExecuteAdhoc<TurnPlay>([&](TurnPlay* status)
	{
		status->sticks = fine;
		return PlayTurn(fine, status->turn);
	});
	// Whichever measure played it, a turn that sets z is the search's answer.
	if (play.executed && !_solvedTurn.executed && SolvedTurn(play.turn))
		_solvedTurn = play;
	return play;
}

bool BitFsAreFixer::SolvedTurn(const Turn& turn) const
{
	// Both errors within the tolerance and surviving the crossings, the step parities equal
	// (the final oscillation's crossing needs them so), the corner's signs, z the steeper axis
	// (the leg the hand-over is in progress on runs x toward the corner and z away from it
	// only then), the tilt floor, near the hand-over asked for, and both axes stepping.
	const float nx = turn.normal[0];
	const float nz = turn.normal[2];
	return turn.reached && turn.stepping
		&& std::fabs(turn.error[0]) <= float(_args.tolerance) && std::fabs(turn.error[2]) <= float(_args.tolerance)
		&& std::abs(turn.steps[0]) % 2 == std::abs(turn.steps[2]) % 2
		&& nx * float(CornerSignX(_args)) > 0.0f && nz * float(CornerSignZ(_args)) > 0.0f
		&& std::fabs(nz) >= std::fabs(nx)
		&& std::fabs(nx) + std::fabs(nz) >= _args.minXzSum
		&& StepsReversibly(nx, _args.targetNx, _args.minNormal, _args.maxNormal, _args.farNormal)
		&& StepsReversibly(nz, _args.targetNz, _args.minNormal, _args.maxNormal, _args.farNormal)
		&& DistanceToAsked(turn.pos[0], turn.pos[2], true) <= NearRest;
}

std::vector<std::pair<int8_t, int8_t>> BitFsAreFixer::SticksWithin(int16_t faceYaw) const
{
	std::vector<std::pair<int8_t, int8_t>> sticks;
	for (const StickEffect& e : StickEffects())
	{
		const int16_t dYaw = int16_t(uint16_t(e.baseYaw) + uint16_t(_camera->yaw) - uint16_t(faceYaw));
		if (std::abs(int(dYaw)) <= int(TurnLimit) && e.mag >= RunMagnitude)
			sticks.push_back(e.stick);
	}
	return sticks;
}

std::vector<BitFsAreFixer::TurnEffect> BitFsAreFixer::TurnLattice(int frame, const std::vector<std::pair<int8_t, int8_t>>& sticks, const Turn& current)
{
	// Each stick in the current sticks' place on that frame, the run and turn played on and
	// reverted: its effect is the movement of the position that set z, kept only when the turn
	// came on the same frame (another frame is another chain of snaps, not a movement), one
	// effect per distinct movement.
	std::vector<TurnEffect> effects;
	std::set<std::pair<double, double>> seen;
	for (const std::pair<int8_t, int8_t>& stick : sticks)
	{
		Sticks fine = _fine;
		fine[size_t(frame)] = stick;
		AdhocScriptStatus<TurnPlay> play = MeasureTurn(fine);
		if (_solvedTurn.executed)
			break;
		if (!play.executed || play.turn.frame != current.frame)
			continue;
		const double dx = double(play.turn.posBefore[0]) - current.posBefore[0];
		const double dz = double(play.turn.posBefore[2]) - current.posBefore[2];
		if (seen.insert({ dx, dz }).second)
			effects.push_back({ stick, Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, _camera->yaw).first, dx, dz });
	}
	return effects;
}

bool BitFsAreFixer::SearchTurn()
{
	// The run's sticks along x, their effects on the position that sets z measured end to end
	// (TurnLattice), and the combinations whose position puts the z goal on the target's float
	// (its error predicted by the goal's slope at the current position, then computed exactly
	// in the game's arithmetic on the predicted floats) played out, the first turn that sets z
	// kept, whichever measure played it. The z band the turn sets is the current turn's, whose
	// step parity must match x's at the turn; one more run frame flips x's. The rounds continue
	// from the nearest turn played.
	CustomStatus.turnRounds = 0;
	_solvedTurn = AdhocScriptStatus<TurnPlay>();
	const int F = _args.fineFrames;
	const int16_t runYaw = atan2s(0.0f, float(LegSignX()));
	_fine.fill(StickToward(runYaw));
	auto keep = [&]()
	{
		Apply(_solvedTurn.m64Diff);
		_fine = _solvedTurn.sticks;
		Finish(_solvedTurn.turn, true);
		return true;
	};
	for (_extraRun = 0; _extraRun <= MaxExtraRun; _extraRun++)
	{
		AdhocScriptStatus<TurnPlay> current = MeasureTurn(_fine);
		if (_solvedTurn.executed)
			return keep();
		if (!current.executed)
			continue;
		if (std::abs(current.turn.steps[0]) % 2 != std::abs(current.turn.steps[2]) % 2)
			continue;

		std::vector<std::pair<int8_t, int8_t>> sticks = SticksWithin(runYaw);
		for (int round = 1; round <= _args.maxRounds; round++)
		{
			CustomStatus.turnRounds = round;
			std::vector<std::vector<TurnEffect>> lattices;
			lattices.resize(size_t(F));
			for (int i = 0; i < F; i++)
			{
				lattices[size_t(i)] = TurnLattice(i, sticks, current.turn);
				if (_solvedTurn.executed)
					return keep();
				if (lattices[size_t(i)].empty())
					return false;
			}

			// The z error of a combination, in ULPs, to first order from the goal's slope at
			// the current position: the current error less a·dx + b·dz, since the error is the
			// target less the value. The leading frames' combinations are bucketed by their
			// a·dx + b·dz, and the landing frame's effects look up the buckets within the
			// tolerance and the slack (the prediction's own noise) of the error they leave; each
			// combination found has its error computed exactly, in the game's arithmetic on the
			// predicted floats, which carries the goal's curvature (hundreds of ULPs over a
			// movement of a few units), and the nearest are played first, the rounds closing in
			// from the nearest played. Two sticks' effects add only when Mario can turn from the
			// one's yaw to the other's in a frame.
			double j[2][2];
			Jacobian(current.turn.posBefore[0], current.turn.posBefore[2], j);
			const double a = j[1][0] / Ulp(_args.targetNz);
			const double b = j[1][1] / Ulp(_args.targetNz);
			struct Head
			{
				double dx, dz;
				int16_t lastYaw;
				Sticks sticks;
			};
			std::vector<Head> heads;
			std::unordered_map<int64_t, std::vector<uint32_t>> buckets;
			auto add = [&](double dx, double dz, int16_t lastYaw, const Sticks& combination)
			{
				buckets[int64_t(std::floor(a * dx + b * dz))].push_back(uint32_t(heads.size()));
				heads.push_back({ dx, dz, lastYaw, combination });
			};
			auto adjacent = [](int16_t yawA, int16_t yawB) { return std::abs(int(int16_t(uint16_t(yawA) - uint16_t(yawB)))) <= int(TurnLimit); };
			if (F == 2)
			{
				for (const TurnEffect& ea : lattices[0])
				{
					Sticks combination = _fine;
					combination[0] = ea.stick;
					add(ea.dx, ea.dz, ea.yaw, combination);
				}
			}
			else
			{
				heads.reserve(lattices[0].size() * lattices[1].size());
				for (const TurnEffect& ea : lattices[0])
					for (const TurnEffect& eb : lattices[1])
					{
						if (!adjacent(ea.yaw, eb.yaw))
							continue;
						Sticks combination = _fine;
						combination[0] = ea.stick;
						combination[1] = eb.stick;
						add(ea.dx + eb.dx, ea.dz + eb.dz, eb.yaw, combination);
					}
			}
			struct Candidate
			{
				double error;
				double movement;
				Sticks sticks;
			};
			std::vector<Candidate> candidates;
			const double slack = double(_args.tolerance) + Slack;
			for (const TurnEffect& tail : lattices[size_t(F - 1)])
			{
				const double u = double(current.turn.error[2]) - a * tail.dx - b * tail.dz;
				for (int64_t bucket = int64_t(std::floor(u - slack)); bucket <= int64_t(std::floor(u + slack)); bucket++)
				{
					auto it = buckets.find(bucket);
					if (it == buckets.end())
						continue;
					for (uint32_t h : it->second)
					{
						const Head& head = heads[h];
						if (!adjacent(head.lastYaw, tail.yaw))
							continue;
						const float px = float(double(current.turn.posBefore[0]) + head.dx + tail.dx);
						const float pz = float(double(current.turn.posBefore[2]) + head.dz + tail.dz);
						float gx, gz, errorZ;
						int stepsZ;
						RestingNormal(px - _pyramid->oPosX, pz - _pyramid->oPosZ, gx, gz);
						AdjustedRemainderError(gz, _args.targetNz, errorZ, stepsZ);
						if (!std::isfinite(errorZ) || std::abs(stepsZ) % 2 != std::abs(current.turn.steps[0]) % 2
							|| !StepsReversibly(gz, _args.targetNz, _args.minNormal, _args.maxNormal, _args.farNormal))
							continue;
						Sticks combination = head.sticks;
						combination[size_t(F - 1)] = tail.stick;
						candidates.push_back({ double(errorZ), std::hypot(head.dx + tail.dx, head.dz + tail.dz), combination });
					}
				}
			}
			if (candidates.empty())
				return false;
			// The candidates predicted nearest first; the plays' errors scatter about a bias (the
			// carry's float noise, and the prediction's own), so after every twenty plays the
			// candidates still to play are re-ranked by their prediction shifted by the median
			// of the offsets seen, and the first turn that sets z is kept.
			auto rank = [&](size_t from, double offset)
			{
				std::sort(candidates.begin() + std::ptrdiff_t(from), candidates.end(), [&](const Candidate& p, const Candidate& q)
					{
						const double dp = std::fabs(p.error + offset), dq = std::fabs(q.error + offset);
						return dp != dq ? dp < dq : p.movement < q.movement;
					});
			};
			rank(0, 0.0);
			AdhocScriptStatus<TurnPlay> best;
			std::vector<double> offsets; // actual less predicted, of the plays that landed near
			for (size_t next = 0; next < candidates.size() && next < size_t(MaxTurnPlays); next++)
			{
				if (next > 0 && next % 20 == 0 && !offsets.empty())
				{
					std::vector<double> sorted = offsets;
					std::sort(sorted.begin(), sorted.end());
					rank(next, -sorted[sorted.size() / 2]);
				}
				const Candidate& candidate = candidates[next];
				AdhocScriptStatus<TurnPlay> play = MeasureTurn(candidate.sticks);
				if (_solvedTurn.executed)
					return keep();
				if (!play.executed)
					continue;
				if (std::fabs(double(play.turn.error[2]) - candidate.error) < 4.0 * Slack)
					offsets.push_back(double(play.turn.error[2]) - candidate.error);
				if (!best.executed || std::fabs(play.turn.error[2]) < std::fabs(best.turn.error[2]))
					best = play;
			}
			if (!best.executed)
				return false;
			if (std::fabs(best.turn.error[2]) <= float(_args.tolerance))
				return false; // z set and the turn still refused: the tilt, the corner or the band, which the sticks do not change
			_fine = best.sticks;
			current = best;
		}
	}
	return false;
}
