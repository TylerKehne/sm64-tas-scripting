#pragma once

#include <LibSm64.hpp>
#include <tasfw/Script.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Mario, the camera and the carried-over state, one sample per frame of a frame range of the
// source movie. Two traces tell whether a movie plays the same on two games (m64splice: the
// spliced part on the target version against the donor movie on its own), and where they
// part: SameMovement compares what inputs produce (position, speed, action, facing); the
// rest (health, coins, lives, cap flags, the camera's mode and yaw, the R-button camera
// selection, the RNG seed) is what a movie carries into a level from the way it got there,
// which is what to look at when two traces part at the first frame the player controls.
// Reads through resource->addr(), each symbol resolved once. Not a hot path.
class MarioTrace : public TopLevelScript<LibSm64>
{
public:
	struct Sample
	{
		int64_t frame = 0;
		float pos[3] = {};
		float forwardVel = 0;
		uint32_t action = 0;
		int16_t faceYaw = 0;
		int16_t intendedYaw = 0;
		int16_t health = 0;
		int16_t coins = 0;
		int8_t lives = 0;
		uint32_t flags = 0;         // MarioState::flags: caps and the like
		uint8_t cameraMode = 0;     // gCamera->mode
		int16_t cameraYaw = 0;      // gCamera->yaw: what the stick is relative to
		int16_t selectionFlags = 0; // sSelectionFlags: the R-button camera choice, kept across levels (0 if the build lacks it)
		int16_t movementFlags = 0;  // gCameraMovementFlags (0 if the build lacks it)
		int16_t dirBaseYaw = 0;     // s8DirModeBaseYaw: the 8-directions camera's yaw for the area (0 if the build lacks it)
		int16_t dirYawOffset = 0;   // s8DirModeYawOffset: the C-button steps added to it, kept across levels (0 if the build lacks it)
		uint16_t randomSeed = 0;    // gRandomSeed16 (0 if the build lacks it)

		bool SameMovement(const Sample& other) const
		{
			return pos[0] == other.pos[0] && pos[1] == other.pos[1] && pos[2] == other.pos[2]
				&& forwardVel == other.forwardVel && action == other.action && faceYaw == other.faceYaw;
		}
		std::string Describe() const;
	};

	class CustomScriptStatus
	{
	public:
		std::vector<Sample> samples; // one per frame, firstFrame..lastFrame
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	MarioTrace(int64_t firstFrame, int64_t lastFrame) : _firstFrame(firstFrame), _lastFrame(lastFrame) {}

	bool validation() override { return _firstFrame >= 0 && _lastFrame >= _firstFrame; }
	bool execution() override;
	bool assertion() override { return true; }

private:
	int64_t _firstFrame;
	int64_t _lastFrame;
};
