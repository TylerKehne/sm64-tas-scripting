#include <filesystem>
#include <nlohmann/json.hpp>

const std::filesystem::path& getPathToSelf();

struct BitFs_ConfigData {
	std::filesystem::path libSM64;
	std::filesystem::path m64File;
	
	// add extra config details here...
	
	static BitFs_ConfigData load(const std::filesystem::path& path);
};