#include "LibSm64.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>

static void* align_pointer(void* ptr, intptr_t alignment) {
	intptr_t x = reinterpret_cast<uintptr_t>(ptr);
	if (x % alignment == 0) {
		return ptr;
	}
	intptr_t mask = alignment-1;
	x &= ~mask;
	return reinterpret_cast<void*>(x);
}

std::vector<uint8_t*> regions_of_interest;

static void handler(int /*sig*/, siginfo_t* si, void* /*unused*/)
{
	mprotect(
		align_pointer(si->si_addr, pagesize), pagesize,
		PROT_READ | PROT_EXEC | PROT_WRITE);
	regions_of_interest.push_back((uint8_t*)align_pointer(si->si_addr, pagesize));
	return;
}

#endif
LibSm64::LibSm64(const LibSm64Config& config) : dll(config.dllPath), config(config)
{
	slotManager._saveMemLimit = int64_t(8000) * 1024 * 1024; //8 GB

	// constructor of SharedLib will throw if it can't load
	void* processID = dll.get("sm64_init");

	// Macro evalutes to nothing on Linux and __stdcall on Windows
	// looks cleaner
	using pICFUNC = int(TAS_FW_STDCALL*)();

	pICFUNC sm64_init = pICFUNC(processID);

	sm64_init();

	_sm64Update = UpdateFn(dll.get("sm64_update"));
	_controllerPads = static_cast<uint8_t*>(dll.get("gControllerPads"));
	_globalTimer = static_cast<const uint32_t*>(dll.get("gGlobalTimer"));

	auto sections = dll.readSections();
	segment = std::vector<SegVal>
	{
		SegVal {".data", sections[".data"].address, sections[".data"].length},
		SegVal {".bss", sections[".bss"].address, sections[".bss"].length},
	};
#if !defined(_WIN32)

	original_buf1.resize(segment[0].length);
	original_buf2.resize(segment[1].length);

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(original_buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(original_buf2.data(), temp, segment[1].length);

	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sigaction(SIGSEGV, &sa, NULL);

	mprotect(
		align_pointer(sections[".data"].address, pagesize),
		(sections[".data"].length & (~(pagesize - 1))) + pagesize,
		PROT_READ | PROT_EXEC);

	mprotect(
		align_pointer(sections[".bss"].address, pagesize),
		(sections[".bss"].length & (~(pagesize - 1))) + pagesize,
		PROT_READ | PROT_EXEC);
#endif
}

void LibSm64::save(LibSm64Mem& state) const
{
#if defined(_WIN32)
	if (config.lightweight)
	{
		state.buf1.resize(LibSm64LightweightBuf1Size);
		state.buf2.resize(LibSm64LightweightBuf2Size);

		for (const LibSm64LightweightSlice& slice : LibSm64LightweightSlices)
		{
			const uint8_t* src = reinterpret_cast<const uint8_t*>(segment[slice.segment].address) + slice.offset;
			uint8_t* dst = (slice.segment == 0 ? state.buf1.data() : state.buf2.data()) + slice.bufOffset;
			memcpy(dst, src, slice.length);
		}

		return;
	}

	state.buf1.resize(segment[0].length);
	state.buf2.resize(segment[1].length);

	int64_t* temp = reinterpret_cast<int64_t*>(segment[0].address);
	memcpy(state.buf1.data(), temp, segment[0].length);

	temp = reinterpret_cast<int64_t*>(segment[1].address);
	memcpy(state.buf2.data(), temp, segment[1].length);
#else
	state.changed_regions.reserve(regions_of_interest.size());
	state.region_count_at_save_time = regions_of_interest.size();
	for (const auto region : regions_of_interest) {
		auto* data = state.changed_regions[region].data();
		memcpy(data, region, pagesize);
	}
#endif
}

void LibSm64::load(const LibSm64Mem& state)
{
#if defined(_WIN32)
	if (config.lightweight)
	{
		for (const LibSm64LightweightSlice& slice : LibSm64LightweightSlices)
		{
			uint8_t* dst = reinterpret_cast<uint8_t*>(segment[slice.segment].address) + slice.offset;
			const uint8_t* src = (slice.segment == 0 ? state.buf1.data() : state.buf2.data()) + slice.bufOffset;
			memcpy(dst, src, slice.length);
		}

		return;
	}

	memcpy(segment[0].address, state.buf1.data(), segment[0].length);
	memcpy(segment[1].address, state.buf2.data(), segment[1].length);
#else
	if (regions_of_interest.size() != state.region_count_at_save_time) {
		memcpy(segment[0].address, original_buf1.data(), segment[0].length);
		memcpy(segment[1].address, original_buf2.data(), segment[1].length);
	}
	for (const auto& pair : state.changed_regions) {
		memcpy(pair.first, pair.second.data(), pagesize);
	}
#endif
}

void LibSm64::advance()
{
	_sm64Update();
}

void LibSm64::setInputs(const Inputs& inputs)
{
	// OSContPad layout: u16 button, s8 stick_x, s8 stick_y (then errno, unused here).
	memcpy(_controllerPads, &inputs.buttons, sizeof(uint16_t));
	_controllerPads[2] = static_cast<uint8_t>(inputs.stick_x);
	_controllerPads[3] = static_cast<uint8_t>(inputs.stick_y);
}

void* LibSm64::addr(const char* symbol) const
{
	return dll.get(symbol);
}

std::size_t LibSm64::getStateSize(const LibSm64Mem& state) const
{
#if defined(_WIN32)
	return state.buf1.capacity() + state.buf2.capacity();
#else
	return state.changed_regions.size()*pagesize;
#endif
}

uint32_t LibSm64::getCurrentFrame() const
{
	return *_globalTimer - 1;
}

bool LibSm64::pointsIntoGameData(const void* p) const
{
	for (const SegVal& seg : segment)
	{
		const char* begin = static_cast<const char*>(seg.address);
		const char* ptr = static_cast<const char*>(p);
		if (ptr >= begin && ptr < begin + seg.length)
			return true;
	}
	return false;
}

std::vector<std::string> LibSm64::layoutCheckReport() const
{
	// OBJECT_POOL_CAPACITY in the decomp. The pool is a static array in .bss.
	constexpr std::ptrdiff_t objectPoolCapacity = 240;

	std::vector<std::string> lines;
	auto ok = [&](const std::string& what) { lines.push_back("ok: " + what); };
	auto fail = [&](const std::string& what) { lines.push_back("FAIL: " + what); };
	auto hex = [](const void* p)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%p", p);
		return std::string(buf);
	};

	// --- Checks valid at any frame ------------------------------------------------------
	MarioState* marioState = *(MarioState**)(addr("gMarioState"));
	MarioState* marioStates = (MarioState*)(addr("gMarioStates"));
	if (marioState == marioStates)
		ok("gMarioState points at gMarioStates[0]");
	else
		fail("gMarioState (" + hex(marioState) + ") != &gMarioStates[0] (" + hex(marioStates) + "); pointer width or symbol resolution is wrong");

	Object* objectPool = (Object*)(addr("gObjectPool"));
	if (pointsIntoGameData(objectPool))
		ok("gObjectPool lies in the DLL's data sections");
	else
		fail("gObjectPool (" + hex(objectPool) + ") is not inside .data/.bss");

	uint32_t timer = *(uint32_t*)(addr("gGlobalTimer"));
	ok("gGlobalTimer readable (" + std::to_string(timer) + ")");

	// --- Checks that need Mario to exist (inside a level) --------------------------------
	Object* marioObj = *(Object**)(addr("gMarioObject"));
	if (marioObj == nullptr)
	{
		lines.push_back("note: gMarioObject is null (not in a level); in-level layout checks skipped");
		return lines;
	}

	if (!pointsIntoGameData(marioObj))
	{
		fail("gMarioObject (" + hex(marioObj) + ") is not inside .data/.bss; refusing to dereference");
		return lines;
	}

	std::ptrdiff_t byteOffset = reinterpret_cast<const char*>(marioObj) - reinterpret_cast<const char*>(objectPool);
	if (byteOffset >= 0 && byteOffset % std::ptrdiff_t(sizeof(Object)) == 0 && byteOffset / std::ptrdiff_t(sizeof(Object)) < objectPoolCapacity)
		ok("gMarioObject is gObjectPool[" + std::to_string(byteOffset / std::ptrdiff_t(sizeof(Object))) + "] (sizeof(Object) = " + std::to_string(sizeof(Object)) + " matches the pool stride)");
	else
		fail("gMarioObject is " + std::to_string(byteOffset) + " bytes into gObjectPool, not a multiple of sizeof(Object) = " + std::to_string(sizeof(Object)) + " within " + std::to_string(objectPoolCapacity) + " entries; struct Object layout is wrong");

	const void* bhvMario = addr("bhvMario");
	if (marioObj->behavior == bhvMario)
		ok("gMarioObject->behavior == bhvMario (Object::behavior offset)");
	else
		fail("gMarioObject->behavior (" + hex(marioObj->behavior) + ") != bhvMario (" + hex(bhvMario) + "); Object::behavior offset is wrong");

	if (marioState->marioObj == marioObj)
		ok("gMarioState->marioObj == gMarioObject (MarioState::marioObj offset)");
	else
		fail("gMarioState->marioObj (" + hex(marioState->marioObj) + ") != gMarioObject (" + hex(marioObj) + "); MarioState layout is wrong");

	// mario.c (update_mario_inputs / copy_mario_state_to_object) mirrors MarioState::pos into
	// both oPosX/Y/Z and header.gfx.pos every frame Mario is updated.
	bool posMatches = marioObj->oPosX == marioState->pos[0] && marioObj->oPosY == marioState->pos[1] && marioObj->oPosZ == marioState->pos[2];
	if (posMatches)
		ok("gMarioObject->oPos[XYZ] == gMarioState->pos (Object::rawData indexing and MarioState::pos offset)");
	else
		fail("gMarioObject->oPos (" + std::to_string(marioObj->oPosX) + ", " + std::to_string(marioObj->oPosY) + ", " + std::to_string(marioObj->oPosZ)
			+ ") != gMarioState->pos (" + std::to_string(marioState->pos[0]) + ", " + std::to_string(marioState->pos[1]) + ", " + std::to_string(marioState->pos[2])
			+ "); Object field indexing or MarioState::pos offset is wrong");

	const f32* gfxPos = marioObj->header.gfx.pos;
	if (gfxPos[0] == marioState->pos[0] && gfxPos[1] == marioState->pos[1] && gfxPos[2] == marioState->pos[2])
		ok("gMarioObject->header.gfx.pos == gMarioState->pos (GraphNodeObject layout)");
	else
		fail("gMarioObject->header.gfx.pos (" + std::to_string(gfxPos[0]) + ", " + std::to_string(gfxPos[1]) + ", " + std::to_string(gfxPos[2])
			+ ") != gMarioState->pos; GraphNode/GraphNodeObject layout is wrong");

	Surface* floor = marioState->floor;
	if (floor == nullptr)
		lines.push_back("note: gMarioState->floor is null (Mario airborne or out of bounds); Surface checks skipped");
	else if (!pointsIntoGameData(floor))
		fail("gMarioState->floor (" + hex(floor) + ") is not inside .data/.bss; MarioState::floor offset is wrong");
	else
	{
		float n = floor->normal.x * floor->normal.x + floor->normal.y * floor->normal.y + floor->normal.z * floor->normal.z;
		if (n > 0.999f && n < 1.001f)
			ok("gMarioState->floor->normal is unit length (Surface::normal offset)");
		else
			fail("gMarioState->floor->normal has squared length " + std::to_string(n) + "; Surface layout is wrong");

		if (floor->object == nullptr || pointsIntoGameData(floor->object))
			ok("gMarioState->floor->object is null or inside game data (Surface::object offset)");
		else
			fail("gMarioState->floor->object (" + hex(floor->object) + ") is not inside .data/.bss; Surface::object offset is wrong");
	}

	Camera* camera = *(Camera**)(addr("gCamera"));
	if (camera != nullptr && pointsIntoGameData(camera))
		ok("gCamera points into game data");
	else
		fail("gCamera (" + hex(camera) + ") is null or outside .data/.bss");

	// --- Lightweight save coverage --------------------------------------------------------
	// Lightweight mode only saves fixed slices of .data/.bss. Every piece of state the search
	// depends on must lie inside a slice, or savestates silently stop restoring it. This is
	// the check that a different DLL build is expected to fail.
	if (config.lightweight)
	{
		auto covered = [&](const void* p, size_t size) -> bool
		{
			const char* ptr = static_cast<const char*>(p);
			for (const LibSm64LightweightSlice& slice : LibSm64LightweightSlices)
			{
				const char* begin = static_cast<const char*>(segment[slice.segment].address) + slice.offset;
				if (ptr >= begin && ptr + size <= begin + slice.length)
					return true;
			}
			return false;
		};
		auto sectionOffset = [&](const void* p) -> std::string
		{
			for (const SegVal& seg : segment)
			{
				const char* begin = static_cast<const char*>(seg.address);
				const char* ptr = static_cast<const char*>(p);
				if (ptr >= begin && ptr < begin + seg.length)
					return seg.name + "+" + std::to_string(ptr - begin);
			}
			return "outside sections";
		};
		auto checkCoverage = [&](const char* symbol, const void* p, size_t size)
		{
			if (covered(p, size))
				ok(std::string("lightweight slices cover ") + symbol + " (" + sectionOffset(p) + ", " + std::to_string(size) + " bytes)");
			else
				fail(std::string("lightweight slices do NOT cover ") + symbol + " (" + sectionOffset(p) + ", " + std::to_string(size)
					+ " bytes); lightweight savestates would not restore it. Use full saves or re-derive LibSm64LightweightSlices for this DLL");
		};

		checkCoverage("gMarioStates", marioStates, 2 * sizeof(MarioState));
		checkCoverage("gObjectPool", objectPool, objectPoolCapacity * sizeof(Object));
		checkCoverage("gGlobalTimer", addr("gGlobalTimer"), sizeof(uint32_t));
		checkCoverage("gControllerPads", addr("gControllerPads"), 4 * 6);
		checkCoverage("gMarioObject", addr("gMarioObject"), sizeof(void*));
		checkCoverage("gCamera", addr("gCamera"), sizeof(void*));
		checkCoverage("*gCamera", camera, sizeof(Camera));
		if (floor != nullptr)
			checkCoverage("gMarioState->floor (surface pool)", floor, sizeof(Surface));

		// Symbols that may not exist in every build: check when present.
		for (const char* symbol : {"gRandomSeed16", "gCurrentArea", "sSurfacePool", "gAreas"})
		{
			void* p = nullptr;
			try { p = addr(symbol); }
			catch (const std::exception&) { continue; }
			checkCoverage(symbol, p, sizeof(void*));
		}

		// Camera state. The game turns a raw stick into Mario's intended yaw through the camera,
		// so any of this that a load does not restore makes a replay diverge from the run that
		// recorded it. These were not part of the original slice selection; they are reported
		// as warnings rather than failures until ROADMAP 4.5 settles what the search needs.
		// Sizes are the decomp's, generous where the x64 layout is unknown.
		auto warnCoverage = [&](const char* symbol, size_t size)
		{
			void* p = nullptr;
			try { p = addr(symbol); }
			catch (const std::exception&) { return; }
			if (covered(p, size))
				ok(std::string("lightweight slices cover ") + symbol + " (" + sectionOffset(p) + ")");
			else
				lines.push_back(std::string("WARN: lightweight slices do NOT cover ") + symbol + " (" + sectionOffset(p) + ", "
					+ std::to_string(size) + " bytes); a lightweight load does not restore it");
		};
		warnCoverage("gLakituState", 136);
		warnCoverage("gPlayerCameraState", 2 * 72);
		warnCoverage("gCameraMovementFlags", 2);
		warnCoverage("sModeTransition", 64);
		warnCoverage("sMarioCamState", sizeof(void*));
		warnCoverage("sModeOffsetYaw", 2);
		warnCoverage("sYawSpeed", 2);
		warnCoverage("sCUpCameraPitch", 2);
		warnCoverage("sFOVState", 16);
		warnCoverage("sCameraStoreCUp", 32);
		warnCoverage("sPanDistance", 4);
		warnCoverage("sZeroZoomDist", 4);
		warnCoverage("sSelectionFlags", 2);
		warnCoverage("sCButtonsPressed", 2);

		// Controller state: buttonPressed is an edge against the previous frame's buttonDown,
		// so a load that leaves the old buttonDown behind changes whether the next frame's
		// A is a press or a hold.
		warnCoverage("gControllers", 3 * 40);
		warnCoverage("gControllerBits", 2);
		warnCoverage("gPlayer1Controller", sizeof(void*));
	}

	return lines;
}

void LibSm64::verifyLayout()
{
	std::vector<std::string> report = layoutCheckReport();
	std::string failures;
	for (const std::string& line : report)
	{
		if (line.rfind("FAIL: ", 0) == 0)
			failures += "\n  " + line;
	}
	if (!failures.empty())
	{
		throw std::runtime_error("LibSm64 layout check failed for " + config.dllPath.string()
			+ ". The struct headers in tasfw-core/inc/sm64 do not match this DLL build (see docs/libsm64.md):" + failures);
	}
}