#include <tasfw/scripts/BitFSPyramidOscillation.hpp>
#include <tasfw/scripts/General.hpp>

#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

#include <array>
#include <cmath>

int16_t getRoughTargetNormal(
	int quadrant, int iteration, int16_t initWalkingAngle)
{
	int16_t baseAngle	  = quadrant * 0x4000 - 0x2000 + 0x4000;
	int16_t initAngleDiff = baseAngle - initWalkingAngle;
	baseAngle = abs(initAngleDiff) < 0x4000 ? baseAngle + 0x8000 : baseAngle;
	return iteration & 1U ? baseAngle + 0x8000 : baseAngle;
}

bool BitFsPyramidOscillation::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (game->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	// Check that Mario is idle
	if (marioState->action != ACT_IDLE)
		return false;

	return true;
}

bool BitFsPyramidOscillation::execution()
{
	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (game->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = *(MarioState**) (game->addr("gMarioState"));
	Camera* camera		   = *(Camera**) (game->addr("gCamera"));
	Object* pyramid		   = marioState->floor->object;

	float normal = 0;
	auto adhocStatus = ExecuteAdhoc([&]()
		{
			MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
			normal = marioState->floor->normal.y;
			return true;
		});

	class CompareJumpStatus : public CompareStatus<CompareJumpStatus>
	{
	public:
		float y = 0;

		const AdhocScriptStatus<CompareJumpStatus>& Comparator(
			const AdhocScriptStatus<CompareJumpStatus>& a,
			const AdhocScriptStatus<CompareJumpStatus>& b) const
		{
			if (a.y >= b.y)
				return a;

			return b;
		}

		bool Terminator(const AdhocScriptStatus<CompareJumpStatus>& status) const override
		{
			return status.y >= -2930;
		}
	};

	auto compareStatus = Compare<CompareJumpStatus>(
		[&](CompareJumpStatus& customStatus) {
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			customStatus.y = marioState->pos[1];
			return true;
		},
		[&](CompareJumpStatus& customStatus) {
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			customStatus.y = marioState->pos[1];
			return true;
		},
		[&](CompareJumpStatus& customStatus) {
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			AdvanceFrameWrite(Inputs(Buttons::A, 0, 0));
			customStatus.y = marioState->pos[1];
			return true;
		}
	);

	auto adhocStatus2 = ExecuteAdhoc<CompareJumpStatus>([&](CompareJumpStatus& customStatus)
		{
			MarioState* marioState = *(MarioState**)(game->addr("gMarioState"));
			customStatus.y = marioState->pos[1];
			return true;
		});

	int16_t initAngle	 = -32768;
	auto initAngleStatus = Test<GetMinimumDownhillWalkingAngle>(initAngle);
	auto stick = Inputs::GetClosestInputByYawExact(
		initAngleStatus.angleFacing, 32, camera->yaw,
		initAngleStatus.downhillRotation);
	AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

	uint64_t preTurnFrame = GetCurrentFrame();
	OptionalSave();

	// Initialize base oscillation params dto
	auto baseOscParams		  = BitFsPyramidOscillation_ParamsDto {};
	baseOscParams.quadrant	  = _quadrant;
	baseOscParams.targetXzSum = _targetXzSum;

	// Initial run downhill
	auto oscillationParams = baseOscParams;
	oscillationParams.roughTargetNormal =
		getRoughTargetNormal(_quadrant, -1, initAngle);
	oscillationParams.roughTargetAngle = initAngleStatus.angleFacing;
	auto initRunStatus =
		Modify<BitFsPyramidOscillation_RunDownhill>(oscillationParams);
	if (!initRunStatus.asserted)
		return false;

	// Record initial XZ sum, don't want to decrease this (TODO: optimize angle
	// of first frame and record this before run downhill)
	CustomStatus.initialXzSum = fabs(pyramid->oTiltingPyramidNormalX) +
		fabs(pyramid->oTiltingPyramidNormalZ);

	// We want to turn uphill as late as possible, and also turn around as late
	// as possible, without sacrificing XZ sum
	uint64_t minFrame = initRunStatus.framePassedEquilibriumPoint == -1 ?
		initRunStatus.m64Diff.frames.begin()->first :
		initRunStatus.framePassedEquilibriumPoint;
	uint64_t maxFrame = initRunStatus.m64Diff.frames.rbegin()->first;
	CustomStatus.finalXzSum[1] = initRunStatus.finalXzSum;
	std::vector<std::pair<int64_t, int64_t>> oscillationMinMaxFrames;
	for (int i = 0; i < 200; i++)
	{
		oscillationMinMaxFrames.emplace_back(minFrame, maxFrame);

		// Start at the latest ppossible frame and work backwards. Stop when the
		// max speed at the equilibrium point stops increasing.
		oscillationParams = baseOscParams;
		oscillationParams.roughTargetNormal =
			getRoughTargetNormal(_quadrant, i, initAngle);
		oscillationParams.prevMaxSpeed = CustomStatus.maxSpeed[i & 1U];
		oscillationParams.brake		   = false;
		oscillationParams.initialXzSum = CustomStatus.finalXzSum[(i & 1U) ^ 1U];
		auto turnRunStatus = Execute<BitFsPyramidOscillation_Iteration>(
			oscillationParams, minFrame, maxFrame);

		// If path was affected by ACT_FINISH_TURNING_AROUND taking too long to
		// expire, retry the PREVIOUS oscillation with braking + quickturn Then
		// run another oscillation, compare the speeds, and continue with the
		// diff that has the higher speed
		if (i > 0 &&
			(turnRunStatus.finishTurnaroundFailedToExpire ||
			 !turnRunStatus.asserted))
		{
			M64Diff nonBrakeDiff	   = GetDiff();
			int64_t minFrameBrake	   = oscillationMinMaxFrames[i - 1].first;
			int64_t maxFrameBrake	   = oscillationMinMaxFrames[i - 1].second;
			auto oscillationParamsPrev = baseOscParams;
			oscillationParamsPrev.roughTargetNormal =
				getRoughTargetNormal(_quadrant, i - 1, initAngle);
			oscillationParamsPrev.prevMaxSpeed =
				CustomStatus.maxSpeed[(i & 1U) ^ 1U];
			oscillationParamsPrev.brake		   = true;
			oscillationParamsPrev.initialXzSum = CustomStatus.finalXzSum[i & 1U];
			auto turnRunStatusBrake = Modify<BitFsPyramidOscillation_Iteration>(
				oscillationParamsPrev, minFrameBrake, maxFrameBrake);
			if (turnRunStatusBrake.asserted)
			{
				int64_t minFrame2 =
					turnRunStatusBrake.framePassedEquilibriumPoint;
				int64_t maxFrame2 =
					turnRunStatusBrake.m64Diff.frames.rbegin()->first;
				auto turnRunStatus2 =
					Execute<BitFsPyramidOscillation_Iteration>(
						oscillationParams, minFrame2, maxFrame2);
				if (turnRunStatus2.passedEquilibriumSpeed >
					turnRunStatus.passedEquilibriumSpeed)
				{
					oscillationMinMaxFrames[i - 1] = {
						minFrameBrake, maxFrameBrake};
					oscillationMinMaxFrames[i] = {minFrame2, maxFrame2};
					CustomStatus.maxSpeed[(i & 1U)] =
						turnRunStatusBrake.speedBeforeTurning;
					turnRunStatus = turnRunStatus2;
				}
				else
					Apply(nonBrakeDiff);
			}
		}

		// Terminate when path fails to increase speed and XZ sum target has
		// been reached in both directions
		if (turnRunStatus.asserted &&
			(turnRunStatus.passedEquilibriumSpeed >
				 CustomStatus.maxPassedEquilibriumSpeed[i & 1] ||
			 CustomStatus.finalXzSum[(i & 1) ^ 1] < _targetXzSum ||
			 CustomStatus.finalXzSum[(i & 1)] < _targetXzSum))
		{
			CustomStatus.finalXzSum[i & 1] = turnRunStatus.finalXzSum;
			CustomStatus.maxSpeed[(i & 1) ^ 1] =
				turnRunStatus.speedBeforeTurning;
			CustomStatus.maxPassedEquilibriumSpeed[i & 1] =
				turnRunStatus.passedEquilibriumSpeed;
			Apply(turnRunStatus.m64Diff);
			minFrame = turnRunStatus.framePassedEquilibriumPoint;
			maxFrame = turnRunStatus.m64Diff.frames.rbegin()->first;
			Save(minFrame);
		}
		else
			break;
	}

	return true;
}

bool BitFsPyramidOscillation::assertion()
{
	if (IsDiffEmpty())
		return false;

	if (CustomStatus.finalXzSum[0] < _targetXzSum ||
		CustomStatus.finalXzSum[1] < _targetXzSum)
		return false;

	return true;
}