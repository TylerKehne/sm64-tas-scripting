#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Camera.hpp"
#include "Game.hpp"
#include "Inputs.hpp"
#include "Script.hpp"
#include "Trig.hpp"
#include "Types.hpp"
#include "BitFsConfig.hpp"

#ifdef SM64_TASFW_CONFIG_PATH
#define SM64_TASFW_HAS_DEFAULT_CONFIG true
#else
#define SM64_TASFW_CONFIG_PATH ""
#define SM64_TASFW_HAS_DEFAULT_CONFIG false
#endif

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

int main(int argc, const char* argv[])
{
	std::filesystem::path cfgPath;
	if constexpr (!SM64_TASFW_HAS_DEFAULT_CONFIG) {
		if (argc != 2)
		{
			std::cout << "Usage:\n";
			std::cout << argv[0] << " <config file>\n";
			return 1;
		}
		cfgPath = argv[1];
	}
	else {
		if (argc > 2) {
			std::cout << "Usage:\n";
			std::cout << argv[0] << " [config file]\n";
			return 1;
		}
		cfgPath = (argc == 2)? argv[1] : SM64_TASFW_CONFIG_PATH;
	}
	
	BitFs_ConfigData cfg = BitFs_ConfigData::load(cfgPath);
	
	auto lib_path = cfg.libSM64;
	auto m64_path = cfg.m64File;

	M64 m64 = M64(m64_path);
	m64.load();

	auto status = TopLevelScript::Main<MainScript, Game>(m64, lib_path);

	m64.save();

	return 0;
}
