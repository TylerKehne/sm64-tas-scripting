#pragma once

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <string>
#include <tuple>
#include <type_traits>
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

template <template <typename...> class Template, typename... Args>
void derived_from_specialization_impl(const Template<Args...>&);

template <class T, template <typename...> class Template>
concept derived_from_specialization_of = requires(const T& t)
{
	derived_from_specialization_impl<Template>(t);
};

// T constructible from the elements of TTuple, a std::tuple<Ts...> possibly cv-qualified (a
// container's element), the way ExecuteFromTuple builds a script from a parameter tuple. A
// class template rather than a requires-expression over std::apply: apply's deduced return
// type makes a mismatch a hard error inside the library instead of a constraint that does not
// hold (docs/compilers.md, ROADMAP 3.10).
template <class T, class TTuple>
struct constructible_from_tuple_impl : std::false_type {};

template <class T, typename... Ts>
struct constructible_from_tuple_impl<T, std::tuple<Ts...>> : std::bool_constant<std::constructible_from<T, Ts...>> {};

template <class T, typename TTuple>
concept constructible_from_tuple = constructible_from_tuple_impl<T, std::remove_cv_t<TTuple>>::value;

struct SectionInfo
{
	void* address;
	size_t length;
};

// structs like this can be aggregate-initialized
// like SegVal {".data", 0xDEADBEEF, 12345678};
struct SegVal
{
	std::string name;
	void* address;
	size_t length;

	static SegVal fromSectionData(const std::string& name, SectionInfo info)
	{
		return { name, info.address, info.length };
	}
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

	void* get(const char* symbol) const;

	// Like get, but returns nullptr instead of throwing when the library does not export the
	// symbol. For optional symbols and for LibSm64's renamed-symbol fallback.
	void* tryGet(const char* symbol) const noexcept;

	// Reads out a list of sections.
	// Do cache the results, as this WILL re-read the file each time it's run.
	std::unordered_map<std::string, SectionInfo> readSections();
};

#endif
