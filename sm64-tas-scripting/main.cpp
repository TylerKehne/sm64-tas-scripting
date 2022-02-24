
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>

#include "BitFsConfig.hpp"
#include "Camera.hpp"
#include "Game.hpp"
#include "Inputs.hpp"
#include "Script.hpp"
#include "Trig.hpp"
#include "Types.hpp"

using namespace std;

#pragma comment(lib, "Ws2_32.lib")

class MainScript : public TopLevelScript
{
public:
	MainScript(M64& m64, Game* game) : TopLevelScript(m64, game) {}

	bool verification() { return true; }

	bool execution()
	{
		Load(3564);
		Save();
		auto status = Script::Modify<BitFsPyramidOscillation>(0.74f, 4);
		return true;
	}

	bool validation()
	{
		// Save m64Diff to M64
		for (auto& [frame, inputs]: BaseStatus.m64Diff.frames)
		{
			_m64.frames[frame] = inputs;
		}

		return true;
	}
};

#if defined(_WIN32)
	#include <windows.h>
static std::filesystem::path get_own_path()
{
	auto buffer = std::unique_ptr<wchar_t[]>(new wchar_t[MAX_PATH]());
	GetModuleFileNameW(0, buffer.get(), MAX_PATH);
	return buffer.get();
}
#elif defined(__linux__)
	#include <linux/limits.h>
static std::filesystem::path get_own_path()
{
	auto buffer = std::unique_ptr<char[]>(new char[PATH_MAX + 1]());
	(void) readlink("/proc/self/exe", buffer.get(), PATH_MAX);
	return buffer.get();
}
#endif

int main(int argc, const char* argv[])
{
	namespace fs = std::filesystem;
	fs::path cfgPath =
		((argc >= 2) ? fs::path(argv[1]) :
									 get_own_path().parent_path() / "config.json");

	BitFs_ConfigData cfg = BitFs_ConfigData::load(cfgPath);

	auto lib_path = cfg.libSM64;
	auto m64_path = cfg.m64File;

	M64 m64 = M64(m64_path);
	m64.load();

	auto status = TopLevelScript::Main<MainScript, Game>(m64, lib_path);

	m64.save();

	return 0;
}
