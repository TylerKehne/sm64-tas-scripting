#include "SelfPath.hpp"

#include <memory>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

const std::filesystem::path& getPathToSelf()
{
	static std::filesystem::path cached = []
	{
		auto buffer = std::make_unique<wchar_t[]>(MAX_PATH);
		DWORD length = GetModuleFileNameW(nullptr, buffer.get(), MAX_PATH);
		if (length == 0)
			throw std::system_error(int(GetLastError()), std::system_category());
		return std::filesystem::path(buffer.get());
	}();
	return cached;
}

#elif defined(__linux__)
#include <linux/limits.h>
#include <unistd.h>

const std::filesystem::path& getPathToSelf()
{
	static std::filesystem::path cached = []
	{
		auto buffer = std::make_unique<char[]>(PATH_MAX + 1);
		ssize_t length = readlink("/proc/self/exe", buffer.get(), PATH_MAX);
		if (length < 0)
			throw std::system_error(errno, std::generic_category());
		buffer[size_t(length)] = '\0';
		return std::filesystem::path(buffer.get());
	}();
	return cached;
}

#else

const std::filesystem::path& getPathToSelf()
{
	// No portable way to ask; the current directory is the best guess for the default
	// config location, and --config exists for everything else.
	static std::filesystem::path cached = std::filesystem::current_path() / "bitfs-turn";
	return cached;
}

#endif
