#include <tasfw/scripts/BitFsScApproach.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

class AttemptDrStatus
{
public:
	float relHeight = 0;
	bool landed = false;
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
		return false;

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

	//move on to next frame in incumbent path if we encounter unexpected action
	if (marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE)
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
	auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
	auto uphillAngleStatus = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, 0);
	if (!uphillAngleStatus.validated)
		return false;

	//attempt DR
	int16_t uphillAngleDiff = uphillAngleStatus.floorAngle + 0x8000 - marioState->faceAngle[1];
	Rotation rotation = uphillAngleDiff > 0 ? Rotation::COUNTERCLOCKWISE : Rotation::CLOCKWISE;

	auto status = ModifyCompareAdhoc<AttemptDrStatus, std::tuple<int8_t, int8_t>>(
		[&](auto iteration, auto& params) //paramsGenerator
		{
			if (iteration >= 5)
				return false;

			int16_t intendedYaw = marioState->faceAngle[1] + 4096 * iteration * rotation;
			params = std::tuple(Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw));

			return true;
		},
		[&](auto customStatus, int8_t stick_x, int8_t stick_y) //script
		{
			AdvanceFrameWrite(Inputs(Buttons::B, stick_x, stick_y));
			customStatus->relHeight = marioState->pos[1] - marioState->floorHeight;

			if (marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP)
				return false;

			if (abs(customStatus->relHeight) < 4.0f)
			{
				//Ensure second frame of DR is low enough
				if (marioState->action == ACT_FORWARD_ROLLOUT)
				{
					customStatus->landed = ModifyCompareAdhoc<std::tuple<>, std::tuple<int8_t, int8_t>>(
						[&](auto iteration, auto& params) //paramsGenerator
						{
							if (iteration >= 5)
								return false;

							int16_t intendedYaw = marioState->faceAngle[1] + 4096 * iteration * rotation;
							params = std::tuple(Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw));

							return true;
						},
						[&](auto customStatus, int8_t stick_x, int8_t stick_y) //script
						{
							AdvanceFrameWrite(Inputs(0, stick_x, stick_y));
							float relHeight = marioState->pos[1] - marioState->floorHeight;

							if (marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP)
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
			if (challenger->relHeight < incumbent->relHeight)
				return challenger;

			return incumbent;
		},
		[&](auto status) { return status->landed; } //terminator
	);

	if (!status.executed)
	{
		CustomStatus.terminate = true;
		return false;
	}

	CustomStatus.drLanded = status.landed;
	CustomStatus.drRelHeight = status.relHeight;

	return true;
}

bool BitFsScApproach_AttemptDr::assertion()
{
	return CustomStatus.drLanded;
}
