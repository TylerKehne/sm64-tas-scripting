#include "BitFsConfig.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>



BitFs_ConfigData BitFs_ConfigData::load(const std::filesystem::path& path)
{
	// Note to Tyler: nlohmann::json behaves like std::map/std::unordered_map
	// in that accessing it using operator[] implicitly creates the element if it
	// doesn't exist.

	nlohmann::json json;
	{
		std::ifstream stream(path);
		if (!stream.good()) {
			throw std::runtime_error("The config file doesn't exist");
		}
		stream >> json;
	}
	if (!json.is_object())
	{
		throw std::runtime_error("Invalid file");
	}
	// Additional entries may be added
	return BitFs_ConfigData {
		.libSM64 = json.at("libsm64").get<std::string>(),
		.m64File = json.at("m64_file").get<std::string>()
	};
}