#include <tasfw/scripts/BitFSPyramidOscillation.hpp>
#include <tasfw/scripts/General.hpp>

#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>
#include <tasfw/Script.hpp>

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::validation()
{
	// Check if Mario is on the pyramid platform
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));

	Surface* floor = marioState->floor;
	if (!floor)
		return false;

	Object* floorObject = floor->object;
	if (!floorObject)
		return false;

	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
	if (floorObject->behavior != pyramidBehavior)
		return false;

	if (marioState->forwardVel < 16.0f)
		return false;

	// Check that Mario can enter walking action
	return marioState->action == ACT_WALKING;
}

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::execution()
{
	const BehaviorScript* pyramidBehavior =
		(const BehaviorScript*) (resource->addr("bhvBitfsTiltingInvertedPyramid"));
	MarioState* marioState = (MarioState*) (resource->addr("gMarioStates"));
	Camera* camera		   = *(Camera**) (resource->addr("gCamera"));

	// Turn around
	if (_oscillationParams.brake)
	{
		// Works with short oscialltion cycles, but takes an extra turnaround
		// frame
		auto status = Modify<BrakeToIdle>();
		if (!status.asserted)
		{
			CustomStatus.tooDownhill = (marioState->action == ACT_LAVA_BOOST);
			return false;
		}
	}
	else
	{
		do
		{
			auto inputs = Inputs::GetClosestInputByYawHau(
				marioState->faceAngle[1] + 0x8000, 32, camera->yaw);
			AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

			// If double turnaround glitch occurs, run for one extra frame in
			// current direction to change animation
			if (marioState->action == ACT_WALKING &&
				marioState->prevAction == ACT_TURNING_AROUND)
			{
				CustomStatus.finishTurnaroundFailedToExpire = true;

				Rollback(GetCurrentFrame() - 1);
				inputs = Inputs::GetClosestInputByYawHau(
					marioState->faceAngle[1], 32, camera->yaw);
				AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));

				if ((marioState->action != ACT_WALKING) ||
					marioState->floor->object == nullptr ||
					marioState->floor->object->behavior != pyramidBehavior)
					return false;

				// Try and turn around again
				inputs = Inputs::GetClosestInputByYawHau(
					marioState->faceAngle[1] + 0x8000, 32, camera->yaw);
				AdvanceFrameWrite(Inputs(0, inputs.first, inputs.second));
			}

			if (marioState->action != ACT_TURNING_AROUND &&
				marioState->action != ACT_FINISH_TURNING_AROUND)
			{
				CustomStatus.tooDownhill =
					(marioState->action == ACT_LAVA_BOOST);
				return false;
			}

			if (marioState->floor->object == nullptr ||
				marioState->floor->object->behavior != pyramidBehavior)
			{
				CustomStatus.tooDownhill = (marioState->floor->object == nullptr);
				return false;
			}
		} while (marioState->action == ACT_TURNING_AROUND);

		Rollback(GetCurrentFrame() - 1);
	}

	auto status =
		Modify<BitFsPyramidOscillation_RunDownhill>(_oscillationParams);

	CustomStatus.framePassedEquilibriumPoint =
		status.framePassedEquilibriumPoint;
	CustomStatus.maxSpeed				= status.maxSpeed;
	CustomStatus.passedEquilibriumSpeed = status.passedEquilibriumSpeed;
	CustomStatus.finalXzSum				= status.finalXzSum;
	CustomStatus.tooUphill				= status.tooUphill;

	return true;
}

bool BitFsPyramidOscillation_TurnAroundAndRunDownhill::assertion()
{
	return !IsDiffEmpty();
}