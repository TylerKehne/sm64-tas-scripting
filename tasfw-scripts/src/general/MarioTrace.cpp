#include <MarioTrace.hpp>

#include <sm64/Camera.hpp>
#include <sm64/Types.hpp>

#include <cstdio>
#include <exception>

std::string MarioTrace::Sample::Describe() const
{
	char buf[320];
	std::snprintf(buf, sizeof(buf),
		"frame %lld pos=(%.9g, %.9g, %.9g) fwd=%.9g action=0x%08X yaw=%d intended=%d camera(mode=%u, yaw=%d, selection=0x%X, movement=0x%X, 8dir=%d%+d)"
		" health=0x%X coins=%d lives=%d flags=0x%08X rng=0x%04X",
		(long long)frame, pos[0], pos[1], pos[2], forwardVel, unsigned(action), int(faceYaw), int(intendedYaw),
		unsigned(cameraMode), int(cameraYaw), unsigned(uint16_t(selectionFlags)), unsigned(uint16_t(movementFlags)), int(dirBaseYaw), int(dirYawOffset),
		unsigned(uint16_t(health)), int(coins), int(lives), unsigned(flags), unsigned(randomSeed));
	return buf;
}

bool MarioTrace::execution()
{
	auto optional = [&](const char* symbol) -> const void*
	{
		try
		{
			return ReadState(symbol);
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	};
	MarioState* const* marioState = static_cast<MarioState* const*>(ReadState("gMarioState"));
	Camera* const* camera = static_cast<Camera* const*>(ReadState("gCamera"));
	const int16_t* selectionFlags = static_cast<const int16_t*>(optional("sSelectionFlags"));
	const int16_t* movementFlags = static_cast<const int16_t*>(optional("gCameraMovementFlags"));
	const int16_t* dirBaseYaw = static_cast<const int16_t*>(optional("s8DirModeBaseYaw"));
	const int16_t* dirYawOffset = static_cast<const int16_t*>(optional("s8DirModeYawOffset"));
	const uint16_t* randomSeed = static_cast<const uint16_t*>(optional("gRandomSeed16"));

	LongLoad(_firstFrame);
	CustomStatus.samples.reserve(size_t(_lastFrame - _firstFrame + 1));
	for (int64_t frame = _firstFrame; frame <= _lastFrame; frame++)
	{
		Sample s;
		s.frame = frame;
		if (const MarioState* m = *marioState)
		{
			for (int i = 0; i < 3; i++)
				s.pos[i] = m->pos[i];
			s.forwardVel = m->forwardVel;
			s.action = m->action;
			s.faceYaw = m->faceAngle[1];
			s.intendedYaw = m->intendedYaw;
			s.health = m->health;
			s.coins = m->numCoins;
			s.lives = m->numLives;
			s.flags = m->flags;
		}
		if (const Camera* c = *camera)
		{
			s.cameraMode = c->mode;
			s.cameraYaw = c->yaw;
		}
		if (selectionFlags)
			s.selectionFlags = *selectionFlags;
		if (movementFlags)
			s.movementFlags = *movementFlags;
		if (dirBaseYaw)
			s.dirBaseYaw = *dirBaseYaw;
		if (dirYawOffset)
			s.dirYawOffset = *dirYawOffset;
		if (randomSeed)
			s.randomSeed = *randomSeed;
		CustomStatus.samples.push_back(s);
		if (frame < _lastFrame)
			AdvanceFrameRead();
	}
	return true;
}
