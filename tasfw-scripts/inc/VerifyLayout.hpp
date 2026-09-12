#pragma once

#include <LibSm64.hpp>
#include <tasfw/Script.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// What a script expects to find in a gObjectPool slot. Scripts address level objects by slot
// (`objectPool[84]` is the BitFS pyramid), which is the maintainer's decision (ROADMAP 2.4):
// several objects share a behavior, so the slot is the identifier. The slot is a side effect
// of spawn order, though, so each expectation is verified by VerifyLayout before a search
// starts: the slot must be active and run the named behavior, and, when checkHome is set,
// sit at the home position the level script spawned it at. Home is what tells the two BitFS
// pyramids apart (their behavior runs SET_HOME), but it is not a rule that holds for every
// object: many never set it and moving objects may update it, so an expectation can leave it
// unchecked. The BitFS list is BitFsObjects.hpp.
struct ExpectedObject
{
	int slot;
	const char* behavior; // exported behavior symbol, pinned-DLL spelling (LibSm64SymbolAliases apply)
	float homeX;
	float homeY;
	float homeZ;
	bool checkHome = true;
};

// Does the game look the way the copied decomp structs and the scripts say it does? Loads
// to `frame` (which should be inside a level) and cross-checks what it reads through
// resource->addr() against relationships the game guarantees: Mario's object is a whole
// slot of gObjectPool, its behavior is bhvMario, its position fields mirror MarioState, the
// floor normal is unit length, and every ExpectedObject sits in its slot. A mismatch means
// the headers in tasfw-core/inc/sm64 do not describe this DLL build, or the level does not
// spawn what the scripts hardcode. One line per check in CustomStatus.lines, prefixed
// "ok: ", "FAIL: " or "note: "; the script asserts when nothing failed. Not a hot path: the
// pipeline runs it once before its first stage (every DLL copy is the same file), the dry
// run and dllcheck print its lines, and the libsm64 tests hand it a wrong list
// (ROADMAP 1.1, 2.4).
class VerifyLayout : public TopLevelScript<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		std::vector<std::string> lines;
		int failures = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	VerifyLayout(int64_t frame, std::vector<ExpectedObject> expected) : _frame(frame), _expected(std::move(expected)) {}

	bool validation() override { return true; }
	bool execution() override;
	bool assertion() override { return CustomStatus.failures == 0; }

private:
	int64_t _frame;
	std::vector<ExpectedObject> _expected;
};
