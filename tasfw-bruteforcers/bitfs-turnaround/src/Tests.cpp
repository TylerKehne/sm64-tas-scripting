// bitfs-turn --test: the executable's own checks, on the game the config names (Tests.hpp).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Tests.hpp"

#include <chrono>
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
#include <BitFsObjects.hpp>
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

	// A fixer solution's invariants: both errors within the tolerance with the step parities
	// equal, both set values stepping reversibly over the range the oscillations tilt through,
	// the hand-over in the corner asked for at the tilt floor or above, the rest before it and
	// after the start, the rest's normal what the fixer's model computes from the rest position
	// (its arithmetic is the game's, which LeastError and the turn's prediction rest on), the x
	// error the rest's, and no A pressed (the setup is for an A-button-challenge run).
	void CheckHandover(const SolutionRecord& solution, const StageConfig& stage)
	{
		BitFsAreFixer::Args defaults;
		const double tolerance = stage.args.contains("tolerance") ? stage.args["tolerance"].get<double>() : double(defaults.tolerance);
		const float minNormal = stage.args.contains("minNormal") ? stage.args["minNormal"].get<float>() : defaults.minNormal;
		const float maxNormal = stage.args.contains("maxNormal") ? stage.args["maxNormal"].get<float>() : defaults.maxNormal;
		const float farNormal = stage.args.contains("farNormal") ? stage.args["farNormal"].get<float>() : defaults.farNormal;
		const float minXzSum = stage.args.contains("minXzSum") ? stage.args["minXzSum"].get<float>() : defaults.minXzSum;
		BitFsAreFixer::Args corner;
		corner.targetNx = stage.args["targetNx"].get<float>();
		corner.targetNz = stage.args["targetNz"].get<float>();
		corner.quadrant = stage.args.contains("quadrant") ? stage.args["quadrant"].get<int>() : 0;

		CHECK(std::fabs(Metric(solution, "adjustedRemainderError0")) <= tolerance);
		CHECK(std::fabs(Metric(solution, "adjustedRemainderError2")) <= tolerance);
		CHECK(std::fmod(std::fabs(Metric(solution, "incrementFrames0")), 2.0) == std::fmod(std::fabs(Metric(solution, "incrementFrames2")), 2.0));
		CHECK(BitFsAreFixer::StepsReversibly(float(Metric(solution, "pyraNormX")), corner.targetNx, minNormal, maxNormal, farNormal));
		CHECK(BitFsAreFixer::StepsReversibly(float(Metric(solution, "pyraNormZ")), corner.targetNz, minNormal, maxNormal, farNormal));
		CHECK(Metric(solution, "pyraNormX") * BitFsAreFixer::CornerSignX(corner) > 0);
		CHECK(Metric(solution, "pyraNormZ") * BitFsAreFixer::CornerSignZ(corner) > 0);
		CHECK(std::fabs(Metric(solution, "pyraNormX")) + std::fabs(Metric(solution, "pyraNormZ")) >= minXzSum);
		CHECK(Metric(solution, "restFrame") > double(stage.startFrame));
		CHECK(Metric(solution, "handoverFrame") > Metric(solution, "restFrame"));
		CHECK(!solution.m64Diff.frames.empty());
		const ExpectedObject& pyramid = BitFsExpectedObjects.front(); // slot 84, the setup's pyramid
		float modelNx, modelNz;
		BitFsAreFixer::RestingNormal(float(Metric(solution, "restX")) - pyramid.homeX, float(Metric(solution, "restZ")) - pyramid.homeZ, modelNx, modelNz);
		CHECK(modelNx == float(Metric(solution, "restNormX")));
		CHECK(modelNz == float(Metric(solution, "restNormZ")));
		CHECK(Metric(solution, "restErrorX") == Metric(solution, "adjustedRemainderError0"));
		// The pair is named, not destructured: the message's lambda cannot capture a structured
		// binding under Clang 18 with OpenMP (docs/compilers.md).
		for (const auto& entry : solution.m64Diff.frames)
		{
			CHECK_MESSAGE((entry.second.buttons & Buttons::A) == 0, "A pressed at frame ", entry.first);
		}
	}

	// The fixer run on its own, so that a refusal's reason and the least errors can be read.
	class FixerRoot : public TopLevelScript<LibSm64>
	{
	public:
		class CustomScriptStatus
		{
		public:
			ScriptStatus<BitFsAreFixer> fixer;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		FixerRoot(int64_t startFrame, const BitFsAreFixer::Args& args) : _startFrame(startFrame), _args(args) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_startFrame);
			CustomStatus.fixer = Modify<BitFsAreFixer>(_args);
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _startFrame;
		BitFsAreFixer::Args _args;
	};
}

int RunTests(const fs::path& config, int argc, char** argv)
{
	g_config = config;
	doctest::Context context(argc, argv);
	return context.run();
}

// ROADMAP 4.8: the are-fixer stage of the config sets both errors inside its tolerance with
// the step parity the oscillations need and hands over running, from its start frame, and
// presses no A (the setup is for an A-button-challenge run: none after the level entry); and
// it is deterministic, the same run from the same state handing over on the same frame with
// the same normal (cost model off, so the counts are exact too).
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
	CheckHandover(solution, stage);

	StageContext again { pipeline, stage, resources, nullptr };
	SolutionSet second = RunStage(again);
	REQUIRE(second.solutions.size() == 1);
	for (const char* name : { "handoverFrame", "restFrame", "pyraNormX", "pyraNormZ", "adjustedRemainderError0", "adjustedRemainderError2", "restX", "restZ" })
		CHECK_MESSAGE(Metric(second.solutions[0], name) == Metric(solution, name), "the second run's ", name);
}

// The target's floats decide what the fixer can hold (2026-10-04): a normal's 0.01 steps round
// back across the 0.25 and 0.5 binades for a quarter of the floats of x's range from the
// setup's target and half of z's, so from -0.17944f no x error under 1 ULP steps reversibly
// and an exact match is impossible, where z holds 0. Validation refuses what cannot be held
// before any frame, in the game's own arithmetic, quickly; the stage reports the least errors.
TEST_CASE("are-fixer: the least error the target admits")
{
	BitFsAreFixer::Args args;
	args.targetNx = -0.17944f;
	args.targetNz = 0.3936f;
	args.quadrant = 4;
	args.minXzSum = 0.5f;
	const auto started = std::chrono::steady_clock::now();
	CHECK(BitFsAreFixer::LeastError(args.targetNx, BitFsAreFixer::CornerSignX(args), args.minNormal, args.maxNormal, args.farNormal) == 1);
	CHECK(BitFsAreFixer::LeastError(args.targetNz, BitFsAreFixer::CornerSignZ(args), args.minNormal, args.maxNormal, args.farNormal) == 0);
	// One ULP more negative, the x target's own chain survives the crossings and 0 is held.
	args.targetNx = std::nextafter(-0.17944f, -1.0f);
	CHECK(BitFsAreFixer::LeastError(args.targetNx, BitFsAreFixer::CornerSignX(args), args.minNormal, args.maxNormal, args.farNormal) == 0);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	char line[200];
	std::snprintf(line, sizeof(line), "the three checks took %.0f ms", ms);
	MESSAGE(line);
}

// ROADMAP 4.6: the dr-oscillations stage's first pass runs the swing's first leg from the
// fixer's hand-over within a few shots (a run with both axes stepping, 4.8; it was a rest).
// The pyramid conserves the adjusted remainder
// error only through frames whose goal leads its normal by a full step on both axes, which a
// random stick does about one frame in three, so the pass is a scripted run toward the
// chord's end (Scattershot_BitfsDr::FirstLeg_1f) until Mario can turn around; before it,
// 50,000 shots found nothing. One thread, deterministic, cost model off, so the counts are exact.
TEST_CASE("dr-oscillations: the first pass runs the swing's first leg from the config's hand-over")
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

// ROADMAP 4.6: the first oscillation comes from the fixer's hand-over in every corner, full
// within the pass's budget (the hand-over is a run with both axes stepping, 4.8; from a rest
// the leg handed over from an equilibrium, where the normal had no lag and the chord ahead
// was downhill), so the first swing may begin its turnaround
// from any frame where the later swings turn only from one the slope took speed on; with
// that it crosses like them (2026-10-04: 16 plain runs of 16 full against 6 before). Each
// corner's setup is the config's target normal mirrored into it. One thread, deterministic,
// so a failure replays.
TEST_CASE("dr-oscillations: the first oscillation comes from the fixer's hand-over in every corner")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	pipeline.threads = 1;
	const StageConfig& fixerTemplate = StageOfType(pipeline, "are-fixer");
	const StageConfig& drTemplate = StageOfType(pipeline, "dr-oscillations");
	const float targetNx = std::fabs(fixerTemplate.args["targetNx"].get<float>()); // the setup's normal, which every stage of the config carries
	const float targetNz = std::fabs(fixerTemplate.args["targetNz"].get<float>());
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
		REQUIRE_MESSAGE(rest.solutions.size() == 1, "the fixer hands over in corner ", quadrant);
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

// ROADMAP 4.8: the regime the fixer works in, locked down (measured 2026-10-06; README, the
// are-fixer bullet, states it): every corner (the config's target normal mirrored into each) at the tilt floor
// 0.5, exact and within 100 ULPs; within 100 at the floors 0.4 and 0.6 in every corner, and
// exact there in the corners the movie's dive flies toward (0.4 in corners 3 and 4, 0.6 in
// corners 1, 3 and 4: the far corners' rests at the floors' ends lie beyond the fine frames'
// reach, or the turn leaves z a ULP off); the start on the run before the dive (every
// corner, the fixer choosing the dive's yaw), in the dive (the dive's own corner, 3, with
// its yaw given) and on the dive slide (corner 4, where the movie's dive lands); a hand-over
// point named; two fine frames within 100 (exact needs three: two frames' combinations are
// too sparse for the landing's cell); and the setup's former normal, -0.17944 and 0.3936,
// at tolerance 1 (its x holds no error under 1). Each case is the stage run on one resource
// with the cost model off, its hand-over checked for every invariant; the line per case is
// what to compare when the regime moves. About twenty minutes; -tce="*regime*" skips it.
TEST_CASE("are-fixer: the regime: every corner, the tolerances, the tilt floors and the start frames")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	const StageConfig& fixerTemplate = StageOfType(pipeline, "are-fixer");
	const float targetNx = std::fabs(fixerTemplate.args["targetNx"].get<float>());
	const float targetNz = std::fabs(fixerTemplate.args["targetNz"].get<float>());
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	struct Case
	{
		int quadrant;
		int tolerance;
		double minXzSum;
		int64_t startFrame;
		const char* note;
		double restX = 0, restZ = 0;
		int fineFrames = 3;
		float nx = 0, nz = 0; // the targets' magnitudes when not the config's
	};
	std::vector<Case> cases;
	for (int quadrant = 1; quadrant <= 4; quadrant++)
	{
		cases.push_back({ quadrant, 0, 0.5, fixerTemplate.startFrame, "" });
		for (double floor : { 0.4, 0.5, 0.6 })
			cases.push_back({ quadrant, 100, floor, fixerTemplate.startFrame, "" });
	}
	for (int quadrant : { 3, 4 })
		cases.push_back({ quadrant, 0, 0.4, fixerTemplate.startFrame, "" });
	for (int quadrant : { 1, 3, 4 })
		cases.push_back({ quadrant, 0, 0.6, fixerTemplate.startFrame, "" });
	cases.push_back({ 3, 0, 0.5, 3262, ", from the dive" });
	cases.push_back({ 4, 0, 0.5, 3269, ", from the dive slide" });
	cases.push_back({ 4, 0, 0.5, fixerTemplate.startFrame, ", the hand-over named", -2110.0, -556.0 });
	cases.push_back({ 4, 100, 0.5, fixerTemplate.startFrame, ", two fine frames", 0, 0, 2 });
	cases.push_back({ 4, 1, 0.5, fixerTemplate.startFrame, ", the former normal at tolerance 1", 0, 0, 3, 0.17944f, 0.3936f });

	for (const Case& c : cases)
	{
		CAPTURE(c.quadrant);
		CAPTURE(c.tolerance);
		CAPTURE(c.minXzSum);
		CAPTURE(c.startFrame);
		CAPTURE(c.note);
		StageConfig stage = fixerTemplate;
		stage.startFrame = c.startFrame;
		stage.args["quadrant"] = c.quadrant;
		BitFsAreFixer::Args corner;
		corner.quadrant = c.quadrant;
		stage.args["targetNx"] = (c.nx != 0 ? c.nx : targetNx) * float(BitFsAreFixer::CornerSignX(corner));
		stage.args["targetNz"] = (c.nz != 0 ? c.nz : targetNz) * float(BitFsAreFixer::CornerSignZ(corner));
		stage.args["tolerance"] = c.tolerance;
		stage.args["minXzSum"] = c.minXzSum;
		stage.args["fineFrames"] = c.fineFrames;
		if (c.restX != 0)
		{
			stage.args["restX"] = c.restX;
			stage.args["restZ"] = c.restZ;
		}
		const ResourceWork before = resources[0].work;
		const auto started = std::chrono::steady_clock::now();
		StageContext context { pipeline, stage, resources, nullptr };
		SolutionSet output = RunStage(context);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		const unsigned long long advances = (unsigned long long)(resources[0].work.frameAdvances - before.frameAdvances);
		char line[320];
		if (output.solutions.size() == 1)
		{
			const SolutionRecord& solution = output.solutions[0];
			std::snprintf(line, sizeof(line), "corner %d, tolerance %d, floor %.2f, from %lld%s: hand-over at %lld, %d frames after the rest, tilt %.3f, ARE (%.0f, %.0f); %llu frame advances, %.1f s",
				c.quadrant, c.tolerance, c.minXzSum, (long long)c.startFrame, c.note, (long long)Metric(solution, "handoverFrame"), int(Metric(solution, "framesAfterRest")),
				std::fabs(Metric(solution, "pyraNormX")) + std::fabs(Metric(solution, "pyraNormZ")), Metric(solution, "adjustedRemainderError0"), Metric(solution, "adjustedRemainderError2"),
				advances, seconds);
			MESSAGE(line);
			CheckHandover(solution, stage);
			if (c.restX != 0)
				CHECK(std::hypot(Metric(solution, "handoverX") - c.restX, Metric(solution, "handoverZ") - c.restZ) <= 20.0);
		}
		else
		{
			std::snprintf(line, sizeof(line), "corner %d, tolerance %d, floor %.2f, from %lld%s: NOT SOLVED; %llu frame advances, %.1f s",
				c.quadrant, c.tolerance, c.minXzSum, (long long)c.startFrame, c.note, advances, seconds);
			MESSAGE(line);
			CHECK_MESSAGE(output.solutions.size() == 1, "the fixer hands over");
		}
	}
}

// ROADMAP 4.8: what the fixer refuses before any frame, with the reason the stage prints: a
// target the arithmetic cannot hold within the tolerance (the setup's former x target at an
// exact match; the least errors say which axis), a start frame that is neither the run before
// the dive, the dive nor its slide (the rollout, here), and an argument out of range.
TEST_CASE("are-fixer: the refusals, before any frame")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	const StageConfig& stage = StageOfType(pipeline, "are-fixer");
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(stage);
	M64 m64(config.M64Path);
	REQUIRE(m64.load());

	BitFsAreFixer::Args args;
	args.targetNx = stage.args["targetNx"].get<float>();
	args.targetNz = stage.args["targetNz"].get<float>();
	args.tolerance = 0;
	args.quadrant = stage.args.contains("quadrant") ? stage.args["quadrant"].get<int>() : 0;
	args.minXzSum = stage.args.contains("minXzSum") ? stage.args["minXzSum"].get<float>() : 0.0f;

	BitFsAreFixer::Args former = args;
	former.targetNx = -0.17944f;
	former.targetNz = 0.3936f;
	auto refused = TopLevelScriptBuilder<FixerRoot>::Build(m64).ImportResource(&resources[0]).Run(int64_t(stage.startFrame), former);
	CHECK(!refused.fixer.validated);
	REQUIRE(refused.fixer.refusal != nullptr);
	CHECK(std::string(refused.fixer.refusal) == "no normal within the tolerance of the target steps reversibly over the range");
	CHECK(refused.fixer.leastError[0] == 1);
	CHECK(refused.fixer.leastError[2] == 0);

	auto rollout = TopLevelScriptBuilder<FixerRoot>::Build(m64).ImportResource(&resources[0]).Run(int64_t(3275), args);
	CHECK(!rollout.fixer.validated);
	REQUIRE(rollout.fixer.refusal != nullptr);
	CHECK(std::string(rollout.fixer.refusal) == "the start frame is not the run before the dive, the dive or its slide");

	BitFsAreFixer::Args narrow = args;
	narrow.fineFrames = 1;
	auto range = TopLevelScriptBuilder<FixerRoot>::Build(m64).ImportResource(&resources[0]).Run(int64_t(stage.startFrame), narrow);
	CHECK(!range.fixer.validated);
	REQUIRE(range.fixer.refusal != nullptr);
	CHECK(std::string(range.fixer.refusal) == "an argument is out of range");
}
