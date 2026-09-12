#include <doctest/doctest.h>

#include <stdexcept>
#include <tasfw/SharedLib.hpp>

// SharedLib::get throws when a symbol is missing; tryGet returns nullptr instead. LibSm64::addr
// relies on tryGet to fall back to the decomp's renamed symbols (LibSm64SymbolAliases). No game
// DLL is needed: the platform's C runtime is a shared library every machine has.
#if defined(_WIN32)
static const char* const kLibrary = "kernel32.dll";
static const char* const kSymbol = "GetProcAddress";
#else
static const char* const kLibrary = "libc.so.6";
static const char* const kSymbol = "strlen";
#endif

TEST_CASE("SharedLib: get throws on a missing symbol, tryGet returns nullptr")
{
	SharedLib lib(kLibrary);

	void* p = lib.tryGet(kSymbol);
	CHECK(p != nullptr);
	CHECK(lib.get(kSymbol) == p);

	CHECK(lib.tryGet("tasfw_no_such_symbol_") == nullptr);
	CHECK_THROWS_AS(lib.get("tasfw_no_such_symbol_"), std::exception);

	// A failed lookup leaves the next one unaffected.
	CHECK(lib.tryGet(kSymbol) == p);
}
