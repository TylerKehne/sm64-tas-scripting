#include <BitFsScApproach.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

class AttemptDrStatus
{
public:
	float relHeight = 0;
	bool landed = false;
	bool offPlatform = false;
};

bool BitFsScApproach_AttemptDr::validation()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));

	//verify action
	uint32_t action = marioState->action;
	if (action != ACT_WALKING && action != ACT_FINISH_TURNING_AROUND)
		CustomStatus.terminate = true;

	//verify dive is possible
	if (marioState->forwardVel < 29.0f)
	{
		CustomStatus.terminate = true;
		return false;
	}

	if (action != ACT_WALKING)
	{
		CustomStatus.terminate = true;
		return false;
	}
		

	// Check if Mario is on the pyramid platform
	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior = (const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	return true;
}

bool BitFsScApproach_AttemptDr::execution()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));
	Camera* camera = *(Camera**)(resource->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//attempt to dive straight forward
	auto inputs = Inputs::GetClosestInputByYawHau(marioState->faceAngle[1], 32, camera->yaw);
	AdvanceFrameWrite(Inputs(Buttons::B | Buttons::START, inputs.first, inputs.second));

	//float z = marioState->pos[2];
	//if (z > -356.01 && z < -355.99)
	//	CustomStatus.terminate = false;

	//move on to next frame in incumbent path if we encounter unexpected action
	if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)// && marioState->action != ACT_BACKWARD_AIR_KB)
		CustomStatus.terminate = true;

	CustomStatus.diveRelHeight = marioState->pos[1] - marioState->floorHeight;
	if (marioState->action != ACT_DIVE_SLIDE)
		return false;

	CustomStatus.diveLanded = true;

	//complete pause-buffer
	AdvanceFrameWrite(Inputs(0, 0, 0));
	AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
	AdvanceFrameWrite(Inputs(0, 0, 0));

	auto m64 = M64();
	auto uphillAngleStatus = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
		.ImportSave<PyramidUpdateMem>(GetCurrentFrame(), *resource, pyramid)
		.Run(0);
	if (!uphillAngleStatus.validated)
		return false;

	//attempt DR
	int16_t uphillAngleDiff = uphillAngleStatus.floorAngle + 0x8000 - marioState->faceAngle[1];
	Rotation rotation = uphillAngleDiff > 0 ? Rotation::COUNTERCLOCKWISE : Rotation::CLOCKWISE;

	//First 2 frames are the most important
	auto status = ModifyCompareAdhoc<AttemptDrStatus, std::tuple<int8_t, int8_t>>(
		[&](auto iteration, auto& params) //paramsGenerator
		{
			if (iteration >= 33)
				return false;

			int16_t intendedYaw = marioState->faceAngle[1] + 512 * iteration * rotation;
			params = std::tuple(Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw));

			return true;
		},
		[&](auto customStatus, int8_t stick_x, int8_t stick_y) //script
		{
			AdvanceFrameWrite(Inputs(Buttons::B, stick_x, stick_y));
			customStatus->relHeight = marioState->pos[1] - marioState->floorHeight;

			//don't want to land right away
			if (marioState->action != ACT_FORWARD_ROLLOUT)
				return false;

			if (abs(customStatus->relHeight) < 4.0f)
			{
				//Ensure second frame of DR is low enough
				if (marioState->action == ACT_FORWARD_ROLLOUT)
				{
					customStatus->landed = ModifyCompareAdhoc<std::tuple<>, std::tuple<int8_t, int8_t>>(
						[&](auto iteration, auto& params) //paramsGenerator
						{
							if (iteration >= 33)
								return false;

							int16_t intendedYaw = marioState->faceAngle[1] + 512 * iteration * rotation;
							params = std::tuple(Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw));

							return true;
						},
						[&](auto customStatus, int8_t stick_x, int8_t stick_y) //script
						{
							AdvanceFrameWrite(Inputs(0, stick_x, stick_y));
							float relHeight = marioState->pos[1] - marioState->floorHeight;

							if (marioState->action != ACT_FORWARD_ROLLOUT)
								return false;

							if (marioState->floor->object != pyramid)
								return false;

							return abs(relHeight) < 4.0f;
						},
						[&](auto incumbent, auto challenger) { return incumbent; }, //comparator
						[&](auto status) { return true; } //terminator
					).executed;
				}
				else
					customStatus->landed = true;
			}

			return marioState->floor->object == pyramid;
		},
		[&](auto incumbent, auto challenger) //comparator
		{
			if (challenger->relHeight != 0 && challenger->relHeight < incumbent->relHeight)
				return challenger;

			return incumbent;
		},
		[&](auto challenger) { return challenger->landed; } //terminator
	);

	//landed here means not actually landed, but within 4 units on both frames
	AdhocScriptStatus<AttemptDrStatus> status2;
	if (status.executed && status.landed)
	{
		while (true)
		{
			//Continue low DR until we pass platform edge
			status2 = ModifyCompareAdhoc<AttemptDrStatus, std::tuple<int8_t, int8_t>>(
				[&](auto iteration, auto& params) //paramsGenerator
				{
					if (iteration >= 33)
						return false;

					//sweep right to left
					int16_t intendedYaw = marioState->faceAngle[1] - 0x4000 + 512 * iteration * rotation;
					params = std::tuple(Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw));

					return true;
				},
				[&](auto customStatus, int8_t stick_x, int8_t stick_y) //script
				{
					AdvanceFrameWrite(Inputs(0, stick_x, stick_y));
					float relHeight = marioState->pos[1] - marioState->floorHeight;
					customStatus->relHeight = relHeight;

					if (marioState->action != ACT_FORWARD_ROLLOUT)
						return false;

					if (marioState->floor->object != pyramid)
					{
						customStatus->offPlatform = true;
						return true;
					}

					return abs(relHeight) < 4.0f;
				},
				[&](auto incumbent, auto challenger) //comparator
				{
					//Get as close to +4 as possible if still above platform
					if (challenger->relHeight != 0 && challenger->relHeight > incumbent->relHeight)
						return challenger;

					return incumbent;
				},
				[&](auto challenger) //terminator
				{
					return challenger->offPlatform;
				});

			if (!status2.executed || status2.offPlatform)
				break;
		}
	}

	CustomStatus.drLanded = status.executed && status2.executed && status.landed && status2.offPlatform;
	CustomStatus.drRelHeight = status.relHeight;

	return true;
}

bool BitFsScApproach_AttemptDr::assertion()
{
	return CustomStatus.drLanded;
}
