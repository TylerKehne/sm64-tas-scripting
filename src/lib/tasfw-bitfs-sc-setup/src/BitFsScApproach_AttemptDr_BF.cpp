#include <tasfw/scripts/BitFsScApproach.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>

bool BitFsScApproach_AttemptDr_BF::validation()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));
		
	// Check if Mario is on the pyramid platform
	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*)(resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	return true;
}

bool BitFsScApproach_AttemptDr_BF::execution()
{
	MarioState* marioState = (MarioState*)(resource->addr("gMarioStates"));
	Camera* camera = *(Camera**)(resource->addr("gCamera"));
	Object* pyramid = marioState->floor->object;

	//advance 1 frame at a time along the previous path
	//each frame, do the following:
	//	1. attempt to PB dive straining forward
	//  2. if speed is too low to dive, proceed to the next frame in the path
	//	3. if already facing uphill and dive fails to land immediately, proceed to the next frame in the path
	//  4. if dive fails to land immediately, try turning towards the uphill angle by 2048
	//	5. if dive lands, try PBDR at five straining angles between straight forward and 90° towards uphill, recording height above platform
	//	6. If DR lands or is less than 4 units above platform, return diff
	//  7. if already facing uphill and lowest DR height fails to come in under 4 units above platform and lowest DR height is worse than before, proceed to next frame
	//	8. if not already facing uphill or DR height improves, turn 2048 uphill and go back to step 1
	//for now, return first valid solution, otherwise track the closest
	Load(_minFrame);
	auto status = DynamicModifyCompareAdhoc<BitFsScApproach_AttemptDr::CustomScriptStatus, std::tuple<>>(
		[&](auto iteration, auto& params) { return true; }, //paramsGenerator
		[&](auto customStatus) //script
		{
			bool terminate = false;
			bool marioIsFacingUphill = false;
			*customStatus = DynamicModifyCompareAdhoc<BitFsScApproach_AttemptDr::CustomScriptStatus, std::tuple<>>(
				[&](auto iteration, auto& params) { return !terminate; }, //paramsGenerator
				[&](auto customStatus) //script
				{
					*customStatus = Modify<BitFsScApproach_AttemptDr>();
					terminate = customStatus->terminate;
					if (terminate || !customStatus->diveLanded)
						return false;

					return true;
				},
				[&]() //mutator
				{
					// Turn 2048 towrds uphill
					auto m64 = M64();
					auto save = ImportedSave(PyramidUpdateMem(*resource, pyramid), GetCurrentFrame());
					auto uphillAngleStatus = BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle::MainFromSave<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>(m64, save, 0);
					if (!uphillAngleStatus.validated)
						return false;

					int16_t floorAngle = uphillAngleStatus.floorAngle + 0x8000;
					if (abs(int16_t(floorAngle - marioState->faceAngle[1])) <= 2048)
						marioIsFacingUphill = true;

					//cap intended yaw diff at 2048
					int16_t intendedYaw = floorAngle;
					if (abs(int16_t(floorAngle - marioState->faceAngle[1])) >= 16384)
						intendedYaw = marioState->faceAngle[1] + 2048 * sign(int16_t(floorAngle - marioState->faceAngle[1]));

					auto inputs = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
					AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
					return true;
				},
				[&](auto incumbent, auto challenger) //comparator
				{
					if (challenger->drRelHeight > -4.0f && challenger->drRelHeight < incumbent->drRelHeight)
						return challenger;

					terminate = marioIsFacingUphill;
					return incumbent;
				},
				[&](auto status) { return status->drLanded; } //terminator
				).substatus;

			return true;
		},
		[&]() //mutator
		{
			AdvanceFrameRead();
			return GetCurrentFrame() <= _maxFrame;
		},
		[&](auto incumbent, auto challenger) //comparator
		{
			if (challenger->drRelHeight > -4.0f && challenger->drRelHeight < incumbent->drRelHeight)
				return challenger;

			return incumbent;
		},
		[&](auto status) { return status->drLanded; } //terminator
		);

	if (!status.executed)
		return false;

	CustomStatus.diveLanded = status.substatus.diveLanded;
	CustomStatus.diveRelHeight = status.substatus.diveRelHeight;
	CustomStatus.drLanded = status.substatus.drLanded;
	CustomStatus.drRelHeight = status.substatus.drRelHeight;

	return true;
}

bool BitFsScApproach_AttemptDr_BF::assertion()
{
	return CustomStatus.drLanded;
}
