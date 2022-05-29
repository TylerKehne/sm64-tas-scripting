#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>

#ifndef SHAREDLIB_H
#define SHAREDLIB_H

#if defined(_WIN32)
#define NOMINMAX
	#include <windows.h>

	#define TAS_FW_STDCALL __stdcall
#elif defined(__linux__)
	#define TAS_FW_STDCALL
#endif

template <template <class...> class Template, class... Args>
void derived_from_specialization_impl(const Template<Args...>&);

template <class T, template <class...> class Template>
concept derived_from_specialization_of = requires(const T& t) {
	derived_from_specialization_impl<Template>(t);
};

struct SectionInfo
{
	void* address;
	size_t length;
};

class SharedLib
{
	std::string libFileName;
#if defined(_WIN32)
	HMODULE handle;
#elif defined(__linux__)
	void* handle;
#endif
public:
	SharedLib(const std::filesystem::path& path);
	~SharedLib();

	void* get(const char* symbol);

	// Reads out a list of sections.
	// Do cache the results, as this WILL re-read the file each time it's run.
	std::unordered_map<std::string, SectionInfo> readSections();
};

#endif
