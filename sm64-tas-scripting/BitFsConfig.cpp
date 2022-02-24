#include "BitFsConfig.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
	#include <windows.h>
const std::filesystem::path& getPathToSelf()
{
	static std::filesystem::path cached = [&] {
		auto buffer = std::unique_ptr<wchar_t[]>(new wchar_t[MAX_PATH]());
		DWORD res = GetModuleFileNameW(0, buffer.get(), MAX_PATH);
		if (res == 0) {
			throw std::system_error(GetLastError(), std::system_category());
		}
		return buffer.get();
	}();
	return cached;
}
#elif defined(__linux__)
	#include <linux/limits.h>
	#include <sys/types.h>
const std::filesystem::path& getPathToSelf()
{
	static std::filesystem::path cached = [&] {
		auto buffer = std::unique_ptr<char[]>(new char[PATH_MAX + 1]());
		ssize_t res = readlink("/proc/self/exe", buffer.get(), PATH_MAX);
		if (res < 0) {
			throw std::system_error(res, std::generic_category());
		}
		return buffer.get();
	}();
	return cached;
}
#endif

static std::filesystem::path resolvePathwithSelf(const std::filesystem::path& path) {
	return std::filesystem::canonical(getPathToSelf() / path);
}

BitFs_ConfigData BitFs_ConfigData::load(const std::filesystem::path& path)
{
	// Note to Tyler: nlohmann::json behaves like std::map/std::unordered_map
	// in that accessing it using operator[] implicitly creates the element if it
	// doesn't exist.

	nlohmann::json json;
	{
		std::ifstream stream(path);
		if (!stream.good())
		{
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
		.libSM64 = resolvePathwithSelf(json.at("libsm64").get<std::string>()),
		.m64File = resolvePathwithSelf(json.at("libsm64").get<std::string>())};
}