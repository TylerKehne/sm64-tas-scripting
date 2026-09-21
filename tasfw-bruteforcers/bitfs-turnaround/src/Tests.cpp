// bitfs-turn --test: the executable's own checks, on the game the config names (Tests.hpp).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Tests.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>

#include "PipelineConfig.hpp"
#include "SolutionSet.hpp"
#include "Stages.hpp"

namespace fs = std::filesystem;

namespace
{
	fs::path g_config;

	// One resource on the config's first DLL, built in place like the pipeline's (a LibSm64
	// is never moved), cost model off so every count is exact.
	void OneResource(const PipelineConfig& pipeline, std::vector<LibSm64>& resources)
	{
		LibSm64Config config;
		config.dllPath = pipeline.DllPaths().at(0);
		config.saveMode = pipeline.saveMode;
		if (!LibSm64::VersionFromPath(config.dllPath, config.countryCode))
			config.countryCode = pipeline.countryCode;
		resources.reserve(1);
		resources.emplace_back(config);
		resources.back().useCostModel = false;
	}

	// `type` by value as a string_view: a `const std::string&` bound to a literal makes GCC's
	// -Wdangling-reference take the returned reference for one into that temporary
	// (docs/compilers.md).
	const StageConfig& StageOfType(const PipelineConfig& pipeline, std::string_view type)
	{
		for (const StageConfig& stage : pipeline.stages)
		{
			if (stage.type == type)
				return stage;
		}
		throw std::runtime_error("config has no stage of type \"" + std::string(type) + "\"");
	}

	double Metric(const SolutionRecord& record, const char* name)
	{
		auto it = record.metrics.find(name);
		REQUIRE_MESSAGE(it != record.metrics.end(), "metric ", name);
		return it->second;
	}
}

int RunTests(const fs::path& config, int argc, char** argv)
{
	g_config = config;
	doctest::Context context(argc, argv);
	return context.run();
}

// ROADMAP 4.8: the are-fixer stage of the config rests the pyramid inside its tolerance with
// the step parity the oscillations need, from its start frame, and presses no A (the setup
// is for an A-button-challenge run: none after the level entry).
TEST_CASE("are-fixer: the config's stage solves within its tolerance without an A press")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	const StageConfig& stage = StageOfType(pipeline, "are-fixer");
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	StageContext context { pipeline, stage, resources, nullptr };
	SolutionSet output = RunStage(context);
	const ResourceWork& work = resources[0].work;
	char line[256];
	std::snprintf(line, sizeof(line), "stage %s: %zu solution(s); %llu frame advances, %llu saves, %llu loads on the first resource",
		stage.name.c_str(), output.solutions.size(), (unsigned long long)work.frameAdvances, (unsigned long long)work.saves,
		(unsigned long long)work.loads);
	MESSAGE(line);
	REQUIRE(output.solutions.size() == 1);

	const SolutionRecord& solution = output.solutions[0];
	double tolerance = stage.args.contains("tolerance") ? stage.args["tolerance"].get<double>() : 100.0;
	CHECK(std::fabs(Metric(solution, "adjustedRemainderError0")) <= tolerance);
	CHECK(std::fabs(Metric(solution, "adjustedRemainderError2")) <= tolerance);
	CHECK(std::fmod(std::fabs(Metric(solution, "incrementFrames0")), 2.0) == std::fmod(std::fabs(Metric(solution, "incrementFrames2")), 2.0));
	CHECK(Metric(solution, "equilibriumFrame") > double(stage.startFrame));
	CHECK(!solution.m64Diff.frames.empty());
	// The pair is named, not destructured: the message's lambda cannot capture a structured
	// binding under Clang 18 with OpenMP (docs/compilers.md).
	for (const auto& entry : solution.m64Diff.frames)
	{
		CHECK_MESSAGE((entry.second.buttons & Buttons::A) == 0, "A pressed at frame ", entry.first);
	}
}
