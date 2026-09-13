#include <doctest/doctest.h>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Surface.hpp>
#include <sm64/Types.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// The sm64 headers against the DLL's debug info (ROADMAP 2.2). sm64_layout.inc is the table
// of field offsets and struct sizes scripts/dll_layout.py wrote from the pinned DLL's DWARF
// (docs/libsm64.md, "Struct layouts"). Every field the DLL declares that the headers also
// declare must sit at the DLL's offset, and every struct must have the DLL's size. Names the
// DLL has and the headers do not are the newer decomp's (docs/decomp.md); they are counted,
// and only a table where most names are absent fails, since that means a struct the
// headers no longer describe at all.

namespace
{
	// Makes the struct type dependent, so that a member the headers lack turns the
	// requires-expression false at instantiation instead of being a hard error at definition
	// (GCC and Clang check non-dependent expressions even in a discarded if-constexpr branch).
	template <class Dummy, class S>
	struct Dependent
	{
		using type = S;
	};

	struct LayoutReport
	{
		std::map<std::string, int> checkedPerStruct;
		std::vector<std::string> sizeMismatches;
		std::vector<std::string> offsetMismatches;
		std::vector<std::string> absent;
	};

	template <class Dummy = void>
	LayoutReport CheckLayout()
	{
		LayoutReport report;
#define TASFW_LAYOUT_STRUCT(S, SIZE)                                                            \
	{                                                                                            \
		report.checkedPerStruct[#S];                                                               \
		if (sizeof(S) != std::size_t(SIZE))                                                        \
			report.sizeMismatches.push_back(                                                         \
				std::string(#S) + " is " + std::to_string(sizeof(S)) + " bytes, the DLL's is " #SIZE); \
	}
#define TASFW_LAYOUT_FIELD(S, F, OFFSET)                                                        \
	{                                                                                            \
		using T = typename Dependent<Dummy, S>::type;                                              \
		if constexpr (requires(T t) { t.F; })                                                      \
		{                                                                                          \
			report.checkedPerStruct[#S]++;                                                           \
			if (offsetof(T, F) != std::size_t(OFFSET))                                               \
				report.offsetMismatches.push_back(std::string(#S "::" #F " is at ")                    \
					+ std::to_string(offsetof(T, F)) + ", the DLL's is at " #OFFSET);                    \
		}                                                                                          \
		else                                                                                       \
			report.absent.push_back(#S "::" #F);                                                     \
	}
#include "sm64_layout.inc"
#undef TASFW_LAYOUT_STRUCT
#undef TASFW_LAYOUT_FIELD
		return report;
	}

	std::string Join(const std::vector<std::string>& items)
	{
		std::string text;
		for (const auto& item : items)
			text += "\n  " + item;
		return text;
	}
}

TEST_CASE("sm64 headers: every field offset and struct size matches the pinned DLL's DWARF")
{
	LayoutReport report = CheckLayout();

	int checked = 0;
	// Not a structured binding: INFO captures its argument in a lambda, and Clang 17 with
	// -fopenmp rejects capturing a structured binding ("not yet supported in OpenMP";
	// docs/compilers.md).
	for (const auto& entry : report.checkedPerStruct)
	{
		INFO("struct " << entry.first);
		CHECK(entry.second > 0);
		checked += entry.second;
	}
	CHECK(report.checkedPerStruct.size() == 7);

	{
		INFO("struct sizes that differ from the DLL's:" << Join(report.sizeMismatches));
		CHECK(report.sizeMismatches.empty());
	}
	{
		INFO("fields whose offset differs from the DLL's:" << Join(report.offsetMismatches));
		CHECK(report.offsetMismatches.empty());
	}
	{
		INFO("names the DLL declares that the headers do not:" << Join(report.absent));
		CHECK(report.absent.size() * 10 < std::size_t(checked));
	}
}
