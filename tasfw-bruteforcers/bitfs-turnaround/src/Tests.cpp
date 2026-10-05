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

#include <BitFsAreFixer.hpp>
#include <Scattershot_BitfsDr.hpp>

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
	// The rest's normal must step reversibly over the range the oscillations use, or the error
	// it sets does not survive them (ROADMAP 4.6: half of the float values do not).
	BitFsAreFixer::Args defaults;
	float minNormal = stage.args.contains("minNormal") ? stage.args["minNormal"].get<float>() : defaults.minNormal;
	float maxNormal = stage.args.contains("maxNormal") ? stage.args["maxNormal"].get<float>() : defaults.maxNormal;
	float farNormal = stage.args.contains("farNormal") ? stage.args["farNormal"].get<float>() : defaults.farNormal;
	CHECK(BitFsAreFixer::StepsReversibly(float(Metric(solution, "pyraNormX")), stage.args["targetNx"].get<float>(), minNormal, maxNormal, farNormal));
	CHECK(BitFsAreFixer::StepsReversibly(float(Metric(solution, "pyraNormZ")), stage.args["targetNz"].get<float>(), minNormal, maxNormal, farNormal));
	// The rest is in the corner asked for (the target's own when the config names none), at
	// the tilt the oscillation starts near its regime from.
	float minXzSum = stage.args.contains("minXzSum") ? stage.args["minXzSum"].get<float>() : defaults.minXzSum;
	BitFsAreFixer::Args corner;
	corner.targetNx = stage.args["targetNx"].get<float>();
	corner.targetNz = stage.args["targetNz"].get<float>();
	corner.quadrant = stage.args.contains("quadrant") ? stage.args["quadrant"].get<int>() : 0;
	CHECK(Metric(solution, "pyraNormX") * BitFsAreFixer::CornerSignX(corner) > 0);
	CHECK(Metric(solution, "pyraNormZ") * BitFsAreFixer::CornerSignZ(corner) > 0);
	CHECK(std::fabs(Metric(solution, "pyraNormX")) + std::fabs(Metric(solution, "pyraNormZ")) >= minXzSum);
	CHECK(Metric(solution, "equilibriumFrame") > double(stage.startFrame));
	CHECK(!solution.m64Diff.frames.empty());
	// The pair is named, not destructured: the message's lambda cannot capture a structured
	// binding under Clang 18 with OpenMP (docs/compilers.md).
	for (const auto& entry : solution.m64Diff.frames)
	{
		CHECK_MESSAGE((entry.second.buttons & Buttons::A) == 0, "A pressed at frame ", entry.first);
	}
}

// ROADMAP 4.6: the dr-oscillations stage's first pass runs the swing's first leg from the
// fixer's rest within a few shots. From a rest the pyramid conserves the adjusted remainder
// error only through frames whose goal leads its normal by a full step on both axes, which a
// random stick does about one frame in three, so the pass is a scripted run toward the
// chord's end (Scattershot_BitfsDr::FirstLeg_1f) until Mario can turn around; before it,
// 50,000 shots found nothing. One thread, deterministic, cost model off, so the counts are exact.
TEST_CASE("dr-oscillations: the first pass runs the swing's first leg from the config's rest")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	pipeline.threads = 1;
	const StageConfig& fixerStage = StageOfType(pipeline, "are-fixer");
	StageConfig stage = StageOfType(pipeline, "dr-oscillations");
	stage.args["maxOscillations"] = 1; // the lineup alone
	stage.args["normalSpecs"]["startXzSum"] = 0.0; // the leg from the rest, whatever handover the config asks (a handover above the rest's tilt is the old lineup, a lottery measured in ROADMAP 4.6)
	stage.args["firstShots"] = 200;    // 16 shots on 16 threads; a cap for one
	stage.scattershot["deterministic"] = true;
	stage.scattershot["csvSamplePeriod"] = 0;
	stage.scattershot["maxSolutions"] = 20; // enough to see the lineups differ; the pass stops there
	stage.visualize.reset();
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	StageContext fixerContext { pipeline, fixerStage, resources, nullptr };
	SolutionSet rest = RunStage(fixerContext);
	REQUIRE(rest.solutions.size() == 1);
	const ResourceWork before = resources[0].work;

	StageContext context { pipeline, stage, resources, &rest };
	SolutionSet output = RunStage(context);
	const ResourceWork& work = resources[0].work;
	char line[256];
	std::snprintf(line, sizeof(line), "stage %s, first pass: %zu solution(s); %llu frame advances, %llu saves, %llu loads",
		stage.name.c_str(), output.solutions.size(), (unsigned long long)(work.frameAdvances - before.frameAdvances),
		(unsigned long long)(work.saves - before.saves), (unsigned long long)(work.loads - before.loads));
	MESSAGE(line);
	REQUIRE(!output.solutions.empty());

	// The leg keeps the rest's tilt (one axis steps away from the corner, the other toward it).
	double restTilt = std::fabs(Metric(rest.solutions[0], "pyraNormX")) + std::fabs(Metric(rest.solutions[0], "pyraNormZ")) - 0.01;
	int quadrant = stage.args.contains("quadrant") ? stage.args["quadrant"].get<int>() : 4;
	for (const SolutionRecord& solution : output.solutions)
	{
		CHECK(Metric(solution, "xzSum") >= restTilt);
		CHECK(Metric(solution, "pyraNormX") * BitfsDrMetrics::CornerSignX(quadrant) > 0);
		CHECK(Metric(solution, "pyraNormZ") * BitfsDrMetrics::CornerSignZ(quadrant) > 0);
		CHECK(Metric(solution, "currentOscillation") == 0);
		for (const auto& entry : solution.m64Diff.frames)
		{
			CHECK_MESSAGE((entry.second.buttons & Buttons::A) == 0, "A pressed at frame ", entry.first);
		}
	}
}

// ROADMAP 4.6: the first oscillation comes from the fixer's rest in every corner, full within
// the pass's budget. The swing's first leg hands over from an equilibrium, where the normal
// has no lag and the chord ahead is downhill, so the first swing may begin its turnaround
// from any frame where the later swings turn only from one the slope took speed on; with
// that it crosses like them (2026-10-04: 16 plain runs of 16 full against 6 before). Each
// corner's setup is the config's target normal mirrored into it. One thread, deterministic,
// so a failure replays.
TEST_CASE("dr-oscillations: the first oscillation comes from the fixer's rest in every corner")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	pipeline.threads = 1;
	const StageConfig& fixerTemplate = StageOfType(pipeline, "are-fixer");
	const StageConfig& drTemplate = StageOfType(pipeline, "dr-oscillations");
	const float targetNx = std::fabs(drTemplate.args["targetNx"].get<float>());
	const float targetNz = std::fabs(drTemplate.args["targetNz"].get<float>());
	const int budget = 2000; // shots of the first oscillation's pass; its full yield is the scattershot's maxSolutions
	const int full = drTemplate.scattershot.contains("maxSolutions") ? drTemplate.scattershot["maxSolutions"].get<int>()
		: pipeline.scattershotDefaults.value("maxSolutions", 100);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	for (int quadrant = 1; quadrant <= 4; quadrant++)
	{
		CAPTURE(quadrant);
		const float nx = targetNx * float(BitfsDrMetrics::CornerSignX(quadrant));
		const float nz = targetNz * float(BitfsDrMetrics::CornerSignZ(quadrant));

		StageConfig fixerStage = fixerTemplate;
		fixerStage.args["quadrant"] = quadrant;
		fixerStage.args["targetNx"] = nx;
		fixerStage.args["targetNz"] = nz;
		StageContext fixerContext { pipeline, fixerStage, resources, nullptr };
		SolutionSet rest = RunStage(fixerContext);
		REQUIRE_MESSAGE(rest.solutions.size() == 1, "the fixer rests in corner ", quadrant);
		CHECK(Metric(rest.solutions[0], "pyraNormX") * BitfsDrMetrics::CornerSignX(quadrant) > 0);
		CHECK(Metric(rest.solutions[0], "pyraNormZ") * BitfsDrMetrics::CornerSignZ(quadrant) > 0);

		StageConfig stage = drTemplate;
		stage.args["quadrant"] = quadrant;
		stage.args["targetNx"] = nx;
		stage.args["targetNz"] = nz;
		stage.args["maxOscillations"] = 3; // the leg, the first oscillation, then a last pass of one shot
		stage.args["shots"] = budget;
		stage.args["lastShots"] = 1;
		stage.args["passRetries"] = 0;
		stage.scattershot["deterministic"] = true;
		stage.scattershot["csvSamplePeriod"] = 0;
		stage.visualize.reset();
		StageContext context { pipeline, stage, resources, &rest };
		SolutionSet output = RunStage(context);

		REQUIRE_MESSAGE(output.passSolutions.size() >= 2, "the leg and the first oscillation ran in corner ", quadrant);
		char line[160];
		std::snprintf(line, sizeof(line), "corner %d: the leg found %lld, the first oscillation %lld of %d within %d shots",
			quadrant, (long long)output.passSolutions[0], (long long)output.passSolutions[1], full, budget);
		MESSAGE(line);
		CHECK(output.passSolutions[0] > 0);
		CHECK(output.passSolutions[1] >= full);
		for (const SolutionRecord& solution : output.solutions)
		{
			for (const auto& entry : solution.m64Diff.frames)
			{
				CHECK_MESSAGE((entry.second.buttons & Buttons::A) == 0, "A pressed at frame ", entry.first);
			}
		}
	}
}
