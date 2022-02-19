#include <iostream>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdint>
#include <filesystem>

#include "Inputs.hpp"
#include "Game.hpp"
#include "Trig.hpp"
#include "Script.hpp"
#include "Types.hpp"
#include "Camera.hpp"

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
		auto status = Script::Modify<BitFsPyramidOscillation>();
		return true;
	}

	bool validation()
	{
		//Save m64Diff to M64
		for (auto& [frame, inputs] : BaseStatus.m64Diff.frames)
		{
			_m64.frames[frame] = inputs;
		}

		return true;
	}
};

int main(int argc, const char* argv[]) {
  namespace fs = std::filesystem;
  if (argc < 3) {
    std::cout << "Requires 2 arguments:\n";
    std::cout << argv[0] << " <m64 file> <libsm64 path>\n";
    return 1;
  }
  
  auto lib_path = fs::absolute(fs::path(argv[1]));
  auto m64_path = fs::absolute(fs::path(argv[2]));

	M64 m64 = M64(m64_path);
	m64.load();
  
	auto status = TopLevelScript::Main<MainScript, Game>(m64, lib_path);

	m64.save();

	return 0;
}
