// bitfs-turn --test: the executable's own checks, on the game the config names (Tests.hpp).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Tests.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>

#include "PipelineConfig.hpp"
#include "SolutionSet.hpp"
#include "Stages.hpp"

#include <BitFsAreFixer.hpp>
#include <BitFsObjects.hpp>
#include <MarioDeath.hpp>
#include <Sm64ActionNames.hpp>
#include <Scattershot_BitfsDr.hpp>
#include <Scattershot_BitfsScEnding.hpp>

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

	// Where a test's stages write their files (the oscillation stage's pass caches), away
	// from the config's output directory, which a real run's caches live in.
	fs::path TestOutputDirectory()
	{
		fs::path directory = fs::temp_directory_path() / "tasfw-bitfs-turn-tests";
		fs::create_directories(directory);
		return directory;
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

	// The uscz setup the fixer's regime and the first oscillation were measured on (ROADMAP
	// 4.6 and 4.8; config.json at git 065470c): the target normal in corner 4, exact, and the
	// oscillation stage's values that a config for another corner sets differently. The
	// cases that hold those measurements put these over the config's, so that they hold
	// whichever setup the config carries (corner 3's since 2026-11-21, whose target holds
	// only within 2 ULPs and whose first oscillation is scarcer by design).
	constexpr double MeasuredTargetNx = -0.1792;
	constexpr double MeasuredTargetNz = 0.3929998875;
	constexpr int MeasuredQuadrant = 4;

	void MeasuredFixerSetup(StageConfig& stage)
	{
		stage.args["targetNx"] = MeasuredTargetNx;
		stage.args["targetNz"] = MeasuredTargetNz;
		stage.args["quadrant"] = MeasuredQuadrant;
		stage.args["tolerance"] = 0;
	}

	void MeasuredOscillationSetup(StageConfig& stage)
	{
		stage.args["targetNx"] = MeasuredTargetNx;
		stage.args["targetNz"] = MeasuredTargetNz;
		stage.args["quadrant"] = MeasuredQuadrant;
		stage.args["firstCrossingDirection"] = "far";
		stage.args["moves"] = { { "leadRun", 9 }, { "probeRun", 4 }, { "turnUphill", 1 }, { "turnAround", 1 } };
		stage.args["normalSpecs"]["startXzSum"] = 0.57;
		stage.args["normalSpecs"]["minXzSum"] = 0.59;
		stage.args["normalSpecs"]["maxExitXzSum"] = 2;
		stage.args["normalSpecs"]["legExitSpeed"] = 17.5;
		stage.args["normalSpecs"]["speedGates"] = "run";
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
	class FixerRoot : public TopLevelScript<Sm64Resource>
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

	// A pass's solution replayed to its last frame (Apply), for the crossing check: Mario's
	// facing there, and the pyramid's normal there and on the two frames before it.
	class SolutionEnd : public TopLevelScript<Sm64Resource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			int64_t frame = -1;
			int16_t faceYaw = 0;
			float normalX[3] = {}; // at the last frame, the frame before, the one before that
			float normalZ[3] = {};
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		SolutionEnd(int64_t startFrame, const M64Diff& diff, int platform) : _startFrame(startFrame), _diff(diff), _platform(platform) {}

		bool validation() override { return true; }
		bool execution() override
		{
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			Object* objectPool = (Object*)(ReadState("gObjectPool"));
			Object* pyramid = &objectPool[_platform];
			LongLoad(_startFrame);
			Apply(_diff);
			CustomStatus.frame = int64_t(GetCurrentFrame());
			CustomStatus.faceYaw = marioState->faceAngle[1];
			for (int k = 0; k < 3; k++)
			{
				if (k > 0)
					Load(CustomStatus.frame - k);
				CustomStatus.normalX[k] = pyramid->oTiltingPyramidNormalX;
				CustomStatus.normalZ[k] = pyramid->oTiltingPyramidNormalZ;
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _startFrame;
		const M64Diff& _diff;
		int _platform;
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
	std::vector<Sm64Resource*> pointers { &resources[0] };

	StageContext context { pipeline, stage, pointers, resources[0], nullptr };
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

	StageContext again { pipeline, stage, pointers, resources[0], nullptr };
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
	pipeline.outputDirectory = TestOutputDirectory();
	const StageConfig& fixerStage = StageOfType(pipeline, "are-fixer");
	StageConfig stage = StageOfType(pipeline, "dr-oscillations");
	stage.args["maxOscillations"] = 1; // the lineup alone
	// The leg is the last pass only when it counts as the way lastCrossingDirection asks, the
	// opposite of the first crossing's. Asked the other way, the stage adds the first
	// oscillation with its whole budget of shots (2026-10-17), which a config whose first
	// crossing heads near does (corner 3's: over an hour on one thread).
	if (stage.args.contains("firstCrossingDirection"))
		stage.args["lastCrossingDirection"] = stage.args["firstCrossingDirection"].get<std::string>() == "far" ? "near" : "far";
	stage.args["normalSpecs"]["startXzSum"] = 0.0; // the leg from the rest, whatever handover the config asks (a handover above the rest's tilt is the old lineup, a lottery measured in ROADMAP 4.6)
	stage.args["firstShots"] = 200;    // 16 shots on 16 threads; a cap for one
	stage.scattershot["deterministic"] = true;
	stage.scattershot["csvSamplePeriod"] = 0;
	stage.scattershot["maxSolutions"] = 20; // enough to see the lineups differ; the pass stops there
	stage.visualize.reset();
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	std::vector<Sm64Resource*> pointers { &resources[0] };

	StageContext fixerContext { pipeline, fixerStage, pointers, resources[0], nullptr };
	SolutionSet rest = RunStage(fixerContext);
	REQUIRE(rest.solutions.size() == 1);
	const ResourceWork before = resources[0].work;

	StageContext context { pipeline, stage, pointers, resources[0], &rest };
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

	// The pass's cache (ROADMAP 4.2): the leg's roots, written as the pass ended, with the
	// leg's yield and the hand-over frame; and a run started at oscillation 1 reads them back
	// and carries the leg's yield in front of its own.
	fs::path passFile = pipeline.PassFile(stage.name, 0);
	REQUIRE(fs::exists(passFile));
	SolutionSet cached = SolutionSet::Load(passFile);
	CHECK(cached.solutions.size() == output.solutions.size());
	REQUIRE(cached.passSolutions.size() == 1);
	CHECK(cached.passSolutions[0] == int64_t(cached.solutions.size()));
	CHECK(cached.Metric("equilibriumFrame") == rest.Metric("handoverFrame"));
	// And the pass's whole pool beside it (ROADMAP 4.21), for the rewind: the leg's is its
	// carried set, since the leg hands on all it found.
	fs::path poolFile = passFile;
	poolFile.replace_extension(".pool.json");
	REQUIRE(fs::exists(poolFile));
	SolutionSet pool = SolutionSet::Load(poolFile);
	CHECK(pool.solutions.size() == cached.solutions.size());
	CHECK(pool.passSolutions == cached.passSolutions);

	PipelineConfig resumed = pipeline;
	resumed.SetStart(stage.name + ":1");
	StageConfig second = stage;
	second.args["maxOscillations"] = 2;
	// The way the one oscillation from this rest crosses, the first crossing's (the config
	// states it; "far" from the uscz rest, whose leg runs to the -x -z end): asked the other
	// way, the stage would add an oscillation at its start (2026-10-17) and run two passes.
	second.args["lastCrossingDirection"] = stage.args.contains("firstCrossingDirection") ? stage.args["firstCrossingDirection"].get<std::string>() : std::string("far");
	second.args["lastShots"] = 50; // the first oscillation's pass, as short as it goes: the start is what is checked, not a find
	StageContext resumedContext { resumed, second, pointers, resources[0], &rest };
	SolutionSet again = RunStage(resumedContext);
	REQUIRE(again.passSolutions.size() == 2);
	CHECK(again.passSolutions[0] == cached.passSolutions[0]);

	// The rewind (the maintainer, 2026-10-06, kept 2026-10-10, ROADMAP 4.12; from the whole
	// pools since 2026-10-12, ROADMAP 4.21): a pass that yields nothing hands on the solutions
	// of the earlier passes whose crossing heads toward the squish-cancel quadrant that meet the
	// last pass's rules, read from the pool files under this run's rules. The first
	// oscillation's pass is made to refuse every solution (an end clearance no root can have)
	// under three oscillations, so that pass is the leg (pass 0, the final pass's parity), and
	// the leg's pool, held to the last pass's rules under a tilt floor it meets (this run's,
	// not the rules of the run that wrote the file), is what the stage returns, the failed
	// pass's yield recorded as none.
	PipelineConfig fallbackRun = pipeline;
	fallbackRun.SetStart(stage.name + ":1");
	StageConfig third = stage;
	third.args["maxOscillations"] = 3;
	third.args["shots"] = 20;
	third.args["requireIncrementParity"] = false;
	third.args["normalSpecs"]["minXzSum"] = 0.1;
	third.args["normalSpecs"]["minEndClearance"] = 1000.0;
	StageContext fallbackContext { fallbackRun, third, pointers, resources[0], &rest };
	SolutionSet fallen = RunStage(fallbackContext);
	REQUIRE(fallen.passSolutions.size() == 2);
	CHECK(fallen.passSolutions[1] == 0);
	REQUIRE(!fallen.solutions.empty());
	CHECK(fallen.solutions.size() == cached.solutions.size());
	for (const SolutionRecord& solution : fallen.solutions)
		CHECK(Metric(solution, "currentOscillation") == 0);
}

// ROADMAP 4.6: the first oscillation comes from the fixer's hand-over in every corner, full
// within the pass's budget (the hand-over is a run with both axes stepping, 4.8; from a rest
// the leg handed over from an equilibrium, where the normal had no lag and the chord ahead
// was downhill), so the first swing may begin its turnaround
// from any frame where the later swings turn only from one the slope took speed on; with
// that it crosses like them (2026-10-04: 16 plain runs of 16 full against 6 before). Each
// corner's setup is the measured uscz setup with its target normal mirrored into the corner,
// whatever setup the config carries. One thread, deterministic, so a failure replays.
TEST_CASE("dr-oscillations: the first oscillation comes from the fixer's hand-over in every corner")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	pipeline.threads = 1;
	pipeline.outputDirectory = TestOutputDirectory();
	StageConfig fixerTemplate = StageOfType(pipeline, "are-fixer");
	StageConfig drTemplate = StageOfType(pipeline, "dr-oscillations");
	MeasuredFixerSetup(fixerTemplate);
	MeasuredOscillationSetup(drTemplate);
	const float targetNx = std::fabs(float(MeasuredTargetNx)); // the measured setup's normal, mirrored into each corner below
	const float targetNz = std::fabs(float(MeasuredTargetNz));
	const int budget = 2000; // shots of the first oscillation's pass
	// The least first-oscillation solutions the pass must find within the budget, a fixed
	// number rather than the pass's own cap (the maintainer, 2026-10-12: at the config's cap of
	// 1,000 the corners found 768, 530, 1,000 and 772, and 530 is plenty for the ten the next
	// pass carries; a cap raised for the pool's variety is not a bar for the search).
	const int full = 200;
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	std::vector<Sm64Resource*> pointers { &resources[0] };

	for (int quadrant = 1; quadrant <= 4; quadrant++)
	{
		CAPTURE(quadrant);
		const float nx = targetNx * float(BitfsDrMetrics::CornerSignX(quadrant));
		const float nz = targetNz * float(BitfsDrMetrics::CornerSignZ(quadrant));

		StageConfig fixerStage = fixerTemplate;
		fixerStage.args["quadrant"] = quadrant;
		fixerStage.args["targetNx"] = nx;
		fixerStage.args["targetNz"] = nz;
		StageContext fixerContext { pipeline, fixerStage, pointers, resources[0], nullptr };
		SolutionSet rest = RunStage(fixerContext);
		REQUIRE_MESSAGE(rest.solutions.size() == 1, "the fixer hands over in corner ", quadrant);
		CHECK(Metric(rest.solutions[0], "pyraNormX") * BitfsDrMetrics::CornerSignX(quadrant) > 0);
		CHECK(Metric(rest.solutions[0], "pyraNormZ") * BitfsDrMetrics::CornerSignZ(quadrant) > 0);

		StageConfig stage = drTemplate;
		stage.args["quadrant"] = quadrant;
		stage.args["targetNx"] = nx;
		stage.args["targetNz"] = nz;
		stage.args.erase("firstCrossingDirection"); // the setup's states corner 4's; each corner's leg decides its own (corners 1 and 3 cross near first)
		stage.args["maxOscillations"] = 3; // the leg, the first oscillation, then a last pass of one shot
		stage.args["shots"] = budget;
		stage.args["lastShots"] = 1;
		stage.args["passRetries"] = 0;
		stage.scattershot["deterministic"] = true;
		stage.scattershot["csvSamplePeriod"] = 0;
		stage.visualize.reset();
		StageContext context { pipeline, stage, pointers, resources[0], &rest };
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

		// A pass's solution is the crossing that reached its oscillation (the maintainer,
		// 2026-10-17, and his original design, 2026-10-19; ROADMAP 4.12): every solution of
		// the first oscillation's pool, replayed, ends on the frame the pyramid's normal first
		// steps a full 0.01 on both axes toward the chord end Mario runs to (the frame before
		// it did not on both), and he faces that end, the opposite of the solution's
		// roughTargetAngle, within a quarter turn: the direction the stage's pass parity and
		// the ending read from a solution is his heading at it. Before that day any later
		// block of the lineage was a solution too, and a final pass whose tilt window opened
		// after the next turnaround handed over states running the other way.
		fs::path poolFile = pipeline.PassFile(stage.name, 1);
		poolFile.replace_extension(".pool.json");
		REQUIRE(fs::exists(poolFile));
		SolutionSet pool = SolutionSet::Load(poolFile);
		CHECK(pool.solutions.size() == size_t(output.passSolutions[1]));
		Configuration config = pipeline.ScattershotConfiguration(stage);
		M64 m64(config.M64Path);
		REQUIRE(m64.load());
		const int platform = stage.args.contains("platform") ? stage.args["platform"].get<int>() : 84;
		// And every solution of the pass crosses the one way the leg fixed (the maintainer,
		// 2026-10-17: one direction per oscillation, no pass exploring the other).
		const int16_t expectedAngle = Scattershot_BitfsDr::CrossingAngle(
			Scattershot_BitfsDr::FirstCrossingAngle(float(Metric(rest.solutions[0], "pyraNormX")), float(Metric(rest.solutions[0], "pyraNormZ")), quadrant), 1, quadrant);
		int index = 0;
		for (const SolutionRecord& solution : pool.solutions)
		{
			CAPTURE(index);
			CHECK(Metric(solution, "currentOscillation") == 1);
			CHECK(int16_t(Metric(solution, "roughTargetAngle")) == expectedAngle);
			auto end = TopLevelScriptBuilder<SolutionEnd>::Build(m64).ImportResource(&resources[0]).Run(int64_t(stage.startFrame), solution.m64Diff, platform);
			const int16_t heading = int16_t(int16_t(Metric(solution, "roughTargetAngle")) + 0x8000);
			const float towardX = gSineTable[uint16_t(heading) >> 4];
			const float towardZ = gCosineTable[uint16_t(heading) >> 4];
			auto stepsToward = [&](int k) // from frame - k - 1 to frame - k, a full step on both axes toward the heading
			{
				const float dx = end.normalX[k] - end.normalX[k + 1];
				const float dz = end.normalZ[k] - end.normalZ[k + 1];
				return dx * towardX > 0.0f && dz * towardZ > 0.0f && std::fabs(dx) >= 0.0099999f && std::fabs(dz) >= 0.0099999f;
			};
			CHECK_MESSAGE(stepsToward(0), "the normal does not step toward Mario's heading ", heading, " on both axes at frame ", end.frame);
			CHECK_MESSAGE(!stepsToward(1), "the normal already stepped toward Mario's heading ", heading, " on both axes the frame before ", end.frame);
			CHECK_MESSAGE(std::abs(int(int16_t(end.faceYaw - heading))) < 0x4000, "Mario faces ", end.faceYaw, " at frame ", end.frame, " where the solution's direction says ", heading);
			index++;
		}
	}
}

// ROADMAP 4.8: the regime the fixer works in, locked down (measured 2026-10-06; README, the
// are-fixer bullet, states it): every corner (the measured uscz target normal mirrored into
// each, whatever target the config carries) at the tilt floor
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
	StageConfig fixerTemplate = StageOfType(pipeline, "are-fixer");
	MeasuredFixerSetup(fixerTemplate);
	const float targetNx = std::fabs(float(MeasuredTargetNx));
	const float targetNz = std::fabs(float(MeasuredTargetNz));
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	std::vector<Sm64Resource*> pointers { &resources[0] };

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
		StageContext context { pipeline, stage, pointers, resources[0], nullptr };
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
	std::vector<Sm64Resource*> pointers { &resources[0] };
	Configuration config = pipeline.ScattershotConfiguration(stage);
	M64 m64(config.M64Path);
	REQUIRE(m64.load());

	// The measured uscz target in its own corner, exact: a config's target may hold only within
	// a tolerance (corner 3's does), and then that refusal comes before the two looked for below.
	BitFsAreFixer::Args args;
	args.targetNx = float(MeasuredTargetNx);
	args.targetNz = float(MeasuredTargetNz);
	args.tolerance = 0;
	args.quadrant = MeasuredQuadrant;
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

namespace
{
	// What the ending's squish probe could leave behind, read at a frame: Mario, the pyramid's
	// normal and the bully the probe moves.
	struct ProbeTraceFrame
	{
		int64_t frame = -1;
		float marioX = 0;
		float marioY = 0;
		float marioZ = 0;
		float marioVelY = 0;
		float fSpd = 0;
		uint32_t action = 0;
		float nX = 0;
		float nY = 0;
		float nZ = 0;
		float bullyX = 0;
		float bullyY = 0;
		float bullyZ = 0;
		float bullyFwd = 0;
		int32_t bullyAction = 0;
		bool operator==(const ProbeTraceFrame&) const = default;
	};

	// Plays the movie from `startFrame` to Mario's departure from the pyramid and records
	// `frames` frames from it on, advanced to and then loaded back to. With the ending's
	// metric script it asks for every recorded frame's metrics, which the engine computes on
	// the advance anyway, and that runs the squish probe on the frames the next frame's face
	// squishes him on; with the default metric script it is the plain replay the other is
	// compared with.
	template <class TMetricScript>
	class ProbeTrace : public TopLevelScript<Sm64Resource, TMetricScript>
	{
	public:
		class CustomScriptStatus
		{
		public:
			int64_t departureFrame = -1;
			std::vector<ProbeTraceFrame> forward;  // the frames from the departure on, advanced to
			std::vector<ProbeTraceFrame> reloaded; // the same frames, loaded back to
			int probedFrames = 0;
			int squishCancels = 0;
			std::vector<float> probeSpeeds; // Mario's speed after the probe's recorded placement, per probed frame
			std::vector<uint32_t> probeActions; // and his action
			std::vector<float> probeHeights;    // and his height
			std::vector<int> probeCollisions;   // and the objects the game found overlapping him
			std::vector<int> probeBullyHits;    // whether the bully was among them
			std::vector<int> probeInvinc;       // and his invincibility timer after
			std::vector<int64_t> probeFrames;   // and the frame
			std::vector<int> catches;           // the catch read on each counted frame
			std::vector<float> targetNx;        // the normal the metric measures the error against, per counted frame
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		ProbeTrace(int64_t startFrame, int frames) : _startFrame(startFrame), _frames(frames) {}

		bool validation() override { return true; }
		bool execution() override
		{
			this->LongLoad(_startFrame);

			// Read after the load: gCamera points nowhere before the level is up.
			MarioState* marioState = *(MarioState**)(this->ReadState("gMarioState"));
			Object* objectPool = (Object*)(this->ReadState("gObjectPool"));
			Object* pyramid = &objectPool[84];
			Object* bully = &objectPool[27];
			for (int i = 0; i < 300 && CustomStatus.departureFrame < 0; i++)
			{
				this->AdvanceFrameRead();
				if (marioState->marioObj->platform != pyramid)
					CustomStatus.departureFrame = int64_t(this->GetCurrentFrame());
			}
			if (CustomStatus.departureFrame < 0)
				return false;

			for (int k = 0; k < _frames; k++)
			{
				Record(CustomStatus.forward, true, marioState, pyramid, bully);
				this->AdvanceFrameRead();
			}
			for (int k = 0; k < _frames; k++)
			{
				this->Load(CustomStatus.departureFrame + k);
				Record(CustomStatus.reloaded, false, marioState, pyramid, bully);
			}
			return true;
		}
		bool assertion() override { return CustomStatus.departureFrame >= 0; }

	private:
		int64_t _startFrame;
		int _frames;

		void Record(std::vector<ProbeTraceFrame>& into, bool count, MarioState* marioState, Object* pyramid, Object* bully)
		{
			if constexpr (!std::is_same_v<TMetricScript, DefaultMetricScript<Sm64Resource>>)
			{
				const auto& metrics = this->GetMetrics(int64_t(this->GetCurrentFrame()));
				if (count && metrics.probed)
				{
					CustomStatus.probedFrames++;
					CustomStatus.probeSpeeds.push_back(metrics.probeSpeed);
					CustomStatus.probeActions.push_back(metrics.probeAction);
					CustomStatus.probeHeights.push_back(metrics.probeHeight);
					CustomStatus.probeCollisions.push_back(metrics.probeCollisions);
					CustomStatus.probeBullyHits.push_back(metrics.probeBullyHit ? 1 : 0);
					CustomStatus.probeInvinc.push_back(metrics.probeInvincTimer);
					CustomStatus.probeFrames.push_back(int64_t(this->GetCurrentFrame()));
				}
				if (count)
				{
					CustomStatus.catches.push_back(metrics.catchPlacements);
					CustomStatus.targetNx.push_back(metrics.targetNx);
				}
				if (count && metrics.squishCancel)
					CustomStatus.squishCancels++;
			}

			ProbeTraceFrame f;
			f.frame = int64_t(this->GetCurrentFrame());
			f.marioX = marioState->pos[0];
			f.marioY = marioState->pos[1];
			f.marioZ = marioState->pos[2];
			f.marioVelY = marioState->vel[1];
			f.fSpd = marioState->forwardVel;
			f.action = marioState->action;
			f.nX = pyramid->oTiltingPyramidNormalX;
			f.nY = pyramid->oTiltingPyramidNormalY;
			f.nZ = pyramid->oTiltingPyramidNormalZ;
			f.bullyX = bully->oPosX;
			f.bullyY = bully->oPosY;
			f.bullyZ = bully->oPosZ;
			f.bullyFwd = bully->oForwardVel;
			f.bullyAction = bully->oAction;
			into.push_back(f);
		}
	};

	// The maintainer's uscx example movie (pyramid_-1945_northeast.m64, beside the config's
	// movie): the oscillations' last crossing at 4418, the departure over the -z edge at 4443,
	// the squish at 4450, which his bully hack cancels from the frame before (ROADMAP 4.11,
	// 2026-10-10; the frames here are the framework's, one more than the replay tool's).
	std::string ExampleMovie(const Configuration& config)
	{
		return (std::filesystem::path(config.M64Path).parent_path() / "pyramid_-1945_northeast.m64").string();
	}
	constexpr int64_t ExampleCrossing = 4418;

	BitfsScEndingArgs ExampleArgs()
	{
		BitfsScEndingArgs args;
		args.initialFrame = ExampleCrossing;
		args.oscQuadrant = 1;
		args.targetQuadrant = 2;
		args.platform = 84;
		args.targetGiven = false;
		args.squishEdgeX = 0;
		args.squishEdgeZ = -1;
		args.bully = 27;
		args.probeWindow = 10;
		return args;
	}
}

// The ending's squish probe (Scattershot_BitfsScEnding.hpp) writes the bully into game memory
// inside the metric script's sandbox, the one write hard rule 1 allows, on the frames after a
// departure the next frame's face squishes Mario on. On the maintainer's example movie it
// must find his cancel: the frame before the squish, and no other, probed, and Mario idle at
// the knockback's speed well above the lava from it. And it must leave no trace: the frames
// after it, advanced to and loaded back to, are byte for byte the plain replay's, the bully
// included; and it is deterministic, two runs reading the same from it (cost model off: the
// probe's own save is the only one).
TEST_CASE("sc-ending: the squish probe finds the maintainer's cancel and leaves the game as it found it")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	std::vector<Sm64Resource*> pointers { &resources[0] };
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64(ExampleMovie(config));
	REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie");

	const int frames = 10;

	auto plain = TopLevelScriptBuilder<ProbeTrace<DefaultMetricScript<Sm64Resource>>>::Build(m64).ImportResource(&resources[0]).Run(ExampleCrossing, frames);
	REQUIRE_MESSAGE(plain.asserted, "the movie never leaves the platform");
	REQUIRE(plain.forward.size() == size_t(frames));
	CHECK(plain.forward == plain.reloaded);
	CHECK(plain.departureFrame == 4443);

	BitfsScEndingArgs args = ExampleArgs();
	auto probed = TopLevelScriptBuilder<ProbeTrace<BitfsScEndingMetrics>>::Build(m64).ImportResource(&resources[0]).ConfigureMetricScript(args).Run(ExampleCrossing, frames);
	REQUIRE(probed.asserted);
	CHECK(probed.departureFrame == plain.departureFrame);
	REQUIRE(probed.probedFrames == 1); // the frame before the squish, and no other
	CHECK(probed.probeFrames[0] == 4449);
	CHECK(probed.squishCancels == 1);
	CHECK(probed.probeActions[0] == ACT_IDLE);
	CHECK(std::fabs(probed.probeSpeeds[0]) >= args.squishMinSpeed);
	CHECK(probed.probeHeights[0] - BitfsScEndingMetrics::LavaHeight >= args.squishMinClearance);
	CHECK(probed.probeBullyHits[0] == 1); // the bully reached Mario
	CHECK(probed.probeInvinc[0] > 0);     // and its knockback took (the game sets the invincibility timer with it)
	CHECK(probed.forward == plain.forward);
	CHECK(probed.reloaded == plain.forward);

	auto again = TopLevelScriptBuilder<ProbeTrace<BitfsScEndingMetrics>>::Build(m64).ImportResource(&resources[0]).ConfigureMetricScript(args).Run(ExampleCrossing, frames);
	REQUIRE(again.asserted);
	CHECK(again.probeSpeeds == probed.probeSpeeds);
	CHECK(again.squishCancels == probed.squishCancels);
	CHECK(again.forward == plain.forward);

	char line[320];
	std::snprintf(line, sizeof(line), "departure at frame %lld (Mario in 0x%08X %s); the catch read as %d %d %d %d %d %d %d %d %d %d; probed at frame %lld: Mario in 0x%08X %s at speed %.1f, %.1f above the lava, %d object(s) on him, the bully %d, invincibility %d",
		(long long)probed.departureFrame, unsigned(plain.forward[0].action), Sm64ActionName(plain.forward[0].action),
		probed.catches[0], probed.catches[1], probed.catches[2], probed.catches[3], probed.catches[4], probed.catches[5], probed.catches[6], probed.catches[7], probed.catches[8], probed.catches[9],
		(long long)probed.probeFrames[0], unsigned(probed.probeActions[0]), Sm64ActionName(probed.probeActions[0]), double(probed.probeSpeeds[0]),
		double(probed.probeHeights[0] - BitfsScEndingMetrics::LavaHeight), probed.probeCollisions[0], probed.probeBullyHits[0], probed.probeInvinc[0]);
	MESSAGE(line);
}

// Without a target (the maintainer, 2026-10-06), the ending conserves the hand-over's own
// lattice: the metric measures the error against the normal of the frame after
// initialFrame and carries that reference, so it reads the same on every later frame while
// the normal itself moves a step a frame, and the probe runs on the frames the face catches
// him on, whatever the normal. With a target, the normal the probe reads on the cancel's
// frame, the probe runs only where the target is within a step on both axes, and finds the
// cancel there.
TEST_CASE("sc-ending: without a target the error is measured against the hand-over; with one the probe runs where the target is")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	std::vector<Sm64Resource*> pointers { &resources[0] };
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64(ExampleMovie(config));
	REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie");

	const int frames = 10;

	auto plain = TopLevelScriptBuilder<ProbeTrace<DefaultMetricScript<Sm64Resource>>>::Build(m64).ImportResource(&resources[0]).Run(ExampleCrossing, frames);
	REQUIRE(plain.asserted);

	BitfsScEndingArgs args = ExampleArgs();
	auto probed = TopLevelScriptBuilder<ProbeTrace<BitfsScEndingMetrics>>::Build(m64).ImportResource(&resources[0]).ConfigureMetricScript(args).Run(ExampleCrossing, frames);
	REQUIRE(probed.asserted);
	REQUIRE(probed.probedFrames == 1);
	CHECK(probed.forward == plain.forward);
	CHECK(probed.reloaded == plain.forward);
	REQUIRE(probed.targetNx.size() == size_t(frames));
	for (float reference : probed.targetNx)
		CHECK(reference == probed.targetNx[0]); // one reference, carried
	CHECK(probed.targetNx[0] != plain.forward[1].nX); // and not the frame's own normal, which has stepped on

	// The target: the normal the probe read on the cancel's frame, the frame after the one it
	// ran on. The probe runs there and finds the cancel; a target a step further, which the
	// fall never reaches with the squish, finds none.
	const size_t cancelIndex = size_t(probed.probeFrames[0] + 1 - probed.departureFrame);
	REQUIRE(cancelIndex < plain.forward.size());
	args.targetGiven = true;
	args.targetNx = plain.forward[cancelIndex].nX;
	args.targetNz = plain.forward[cancelIndex].nZ;
	auto pinned = TopLevelScriptBuilder<ProbeTrace<BitfsScEndingMetrics>>::Build(m64).ImportResource(&resources[0]).ConfigureMetricScript(args).Run(ExampleCrossing, frames);
	REQUIRE(pinned.asserted);
	REQUIRE(pinned.probedFrames == 1);
	CHECK(pinned.probeFrames[0] == probed.probeFrames[0]);
	CHECK(pinned.squishCancels == 1);
	CHECK(pinned.forward == plain.forward);

	args.targetNx = plain.forward[cancelIndex + 2].nX;
	args.targetNz = plain.forward[cancelIndex + 2].nZ;
	auto missed = TopLevelScriptBuilder<ProbeTrace<BitfsScEndingMetrics>>::Build(m64).ImportResource(&resources[0]).ConfigureMetricScript(args).Run(ExampleCrossing, frames);
	REQUIRE(missed.asserted);
	CHECK(missed.probedFrames == 0);
	CHECK(missed.squishCancels == 0);
	CHECK(missed.forward == plain.forward);
}

namespace
{
	// Mario at the squish edge, a frame at a time: what the ending's leave and the moves after
	// it rest on (NutStop, NutLand, StrainBack).
	struct EdgeFrame
	{
		int64_t frame = 0;
		uint32_t action = 0;
		float y = 0;
		float fSpd = 0;
		bool onPlatform = false; // the pyramid is his platform at the frame's end
		float outside = 0;       // past the squish edge by the frame's own face (negative: inside)
		float nX = 0;
	};

	// From the frame of a stop at the squish edge: one frame of a stick straight off the edge at
	// `magnitude` (none when 0), then neutral frames, each recorded.
	class EdgeTrace : public TopLevelScript<Sm64Resource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			std::vector<EdgeFrame> frames;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		EdgeTrace(int64_t stopFrame, int frames, float magnitude, int outX, int outZ)
			: _stopFrame(stopFrame), _frames(frames), _magnitude(magnitude), _outX(outX), _outZ(outZ) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_stopFrame);
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			Object* objectPool = (Object*)(ReadState("gObjectPool"));
			Object* pyramid = &objectPool[84];
			Camera* camera = *(Camera**)(ReadState("gCamera"));
			Record(marioState, pyramid);
			for (int k = 0; k < _frames; k++)
			{
				Inputs inputs(0, 0, 0);
				if (k == 0 && _magnitude > 0.0f)
				{
					auto stick = Inputs::GetClosestInputByYawHau(atan2s(float(_outZ), float(_outX)), _magnitude, camera->yaw);
					inputs = Inputs(0, stick.first, stick.second);
				}
				AdvanceFrameWrite(inputs);
				Record(marioState, pyramid);
				if (MarioDeath::IsDying(*marioState))
					break;
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _stopFrame;
		int _frames;
		float _magnitude;
		int _outX;
		int _outZ;

		void Record(MarioState* marioState, Object* pyramid)
		{
			const auto face = BitfsScEndingMetrics::Face::Of(pyramid);
			EdgeFrame f;
			f.frame = int64_t(GetCurrentFrame());
			f.action = marioState->action;
			f.y = marioState->pos[1];
			f.fSpd = marioState->forwardVel;
			f.onPlatform = marioState->marioObj->platform == pyramid;
			f.outside = face.Out(_outX, _outZ, marioState->pos[0], marioState->pos[2]) - face.EdgeOut(_outX, _outZ, marioState->pos[0], marioState->pos[2]);
			f.nX = pyramid->oTiltingPyramidNormalX;
			CustomStatus.frames.push_back(f);
		}
	};

	const EdgeFrame& FrameAt(const std::vector<EdgeFrame>& frames, int64_t frame)
	{
		for (const EdgeFrame& f : frames)
		{
			if (f.frame == frame)
				return f;
		}
		throw std::runtime_error("no frame " + std::to_string(frame) + " in the trace");
	}
}

// What the game does at the squish edge (ROADMAP 4.11, 2026-10-17), on the maintainer's
// northeast example from its landing stop at 4442, 4 units inside the -z edge with no speed.
// Standing still, the platform's carry moves Mario with it but the edge sweeps outward
// faster, so he drifts inward every frame and never leaves: a wait for the displacement is
// no leave here. A one-frame stick off the edge at 4 steps him off, the sweep covers him on
// the next frame while he is just under the top and he lands, his forward speed steps him
// off again on the third landing frame, he lands once more, and the landing stop then zeroes
// his speed with him inside: two touches, no leave. At 8 he lands on his first two air
// frames and steps off again at once each time, escapes the sweep on the third, falls seven
// frames and reaches the lava 11 units outside the edge, not squished. Each frame on the
// platform steps the normal's x toward him, each frame off it toward flat.
TEST_CASE("sc-ending: at the squish edge a stopped Mario is carried inward, a slow leave touches down and a fast one escapes")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64(ExampleMovie(config));
	REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie");
	const int64_t stop = 4442;

	auto still = TopLevelScriptBuilder<EdgeTrace>::Build(m64).ImportResource(&resources[0]).Run(stop, 28, 0.0f, 0, -1);
	REQUIRE(still.frames.size() == 29u);
	CHECK(still.frames.front().action == ACT_FREEFALL_LAND_STOP);
	CHECK(still.frames.front().fSpd == 0.0f);
	CHECK(still.frames.front().outside < -3.0f);
	CHECK(still.frames.front().outside > -5.0f);
	for (size_t i = 1; i < still.frames.size(); i++)
	{
		CHECK(still.frames[i].onPlatform);
		CHECK(still.frames[i].outside < still.frames[i - 1].outside - 0.5f); // the carry: inward, over half a unit a frame
		CHECK(still.frames[i].outside > still.frames[i - 1].outside - 1.2f);
	}
	CHECK(still.frames.back().action == ACT_IDLE);

	auto slow = TopLevelScriptBuilder<EdgeTrace>::Build(m64).ImportResource(&resources[0]).Run(stop, 28, 4.0f, 0, -1);
	REQUIRE(slow.frames.size() == 29u);
	CHECK(FrameAt(slow.frames, 4443).action == ACT_WALKING);
	CHECK(FrameAt(slow.frames, 4444).action == ACT_FREEFALL);
	CHECK(FrameAt(slow.frames, 4445).action == ACT_FREEFALL_LAND);
	CHECK(FrameAt(slow.frames, 4447).action == ACT_FREEFALL_LAND);
	CHECK(FrameAt(slow.frames, 4448).action == ACT_FREEFALL);
	CHECK(FrameAt(slow.frames, 4449).action == ACT_FREEFALL_LAND);
	CHECK(FrameAt(slow.frames, 4453).action == ACT_FREEFALL_LAND_STOP);
	CHECK(FrameAt(slow.frames, 4453).fSpd == 0.0f);
	for (int64_t frame = 4453; frame <= 4470; frame++)
		CHECK(FrameAt(slow.frames, frame).onPlatform);

	auto fast = TopLevelScriptBuilder<EdgeTrace>::Build(m64).ImportResource(&resources[0]).Run(stop, 28, 8.0f, 0, -1);
	REQUIRE(FrameAt(fast.frames, 4457).frame == 4457);
	CHECK(FrameAt(fast.frames, 4443).action == ACT_FREEFALL);
	CHECK(FrameAt(fast.frames, 4444).action == ACT_FREEFALL_LAND);
	CHECK(FrameAt(fast.frames, 4445).action == ACT_FREEFALL);
	CHECK(FrameAt(fast.frames, 4446).action == ACT_FREEFALL_LAND);
	for (int64_t frame = 4447; frame <= 4455; frame++)
	{
		CHECK(FrameAt(fast.frames, frame).action == ACT_FREEFALL);
		CHECK(!FrameAt(fast.frames, frame).onPlatform);
	}
	CHECK(FrameAt(fast.frames, 4456).y == BitfsScEndingMetrics::LavaHeight);
	CHECK(FrameAt(fast.frames, 4456).action != ACT_SQUISHED);
	CHECK(FrameAt(fast.frames, 4456).outside > 10.0f);
	// The normal's x steps toward Mario after a frame on the platform and toward flat after one off it.
	for (int64_t frame = 4443; frame <= 4449; frame++)
	{
		const float step = FrameAt(fast.frames, frame).nX - FrameAt(fast.frames, frame - 1).nX;
		CHECK(std::fabs(step - (FrameAt(fast.frames, frame - 1).onPlatform ? 0.01f : -0.01f)) < 0.0005f);
	}
}

namespace
{
	// A stick played at a frame of a movie: the game's result against the model's.
	struct AirCheck
	{
		int8_t sx = 0, sy = 0;
		bool modelPlays = false; // the model plays the frame (a walk's dive needs B and a stick over 48 raw)
		bool same = true;
		uint32_t gameAction = 0, modelAction = 0;
		float gameX = 0, modelX = 0, gameY = 0, modelY = 0, gameZ = 0, modelZ = 0;
		float gameSpeed = 0, modelSpeed = 0, gameVy = 0, modelVy = 0;
		bool gamePlatform = false, modelPlatform = false;
	};

	// From a frame of a movie: a grid of sticks (64 yaws at 5 magnitudes, and neutral), each
	// played in the game for one frame with the buttons asked for, against the model's frame
	// from the same state (BitfsScEndingMetrics::AirStep).
	class AirGrid : public TopLevelScript<Sm64Resource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			std::vector<AirCheck> checks;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		AirGrid(int64_t frame, uint16_t buttons) : _frame(frame), _buttons(buttons) {}

		bool validation() override { return true; }
		bool execution() override
		{
			using AirStep = BitfsScEndingMetrics::AirStep;
			LongLoad(_frame);
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			Object* objectPool = (Object*)(ReadState("gObjectPool"));
			Object* pyramid = &objectPool[84];
			Camera* camera = *(Camera**)(ReadState("gCamera"));
			const auto mario = AirStep::Mario::Of(marioState, pyramid);
			const auto model = AirStep::Pyramid::Of(pyramid);
			std::vector<std::pair<int8_t, int8_t>> sticks { { 0, 0 } };
			for (int yaw = 0; yaw < 65536; yaw += 1024)
			{
				for (float magnitude : { 4.0f, 8.0f, 16.0f, 24.0f, 32.0f })
					sticks.push_back(Inputs::GetClosestInputByYawHau(int16_t(yaw), magnitude, camera->yaw));
			}
			for (auto stick : sticks)
			{
				AirCheck check;
				check.sx = stick.first;
				check.sy = stick.second;
				auto predicted = mario;
				auto predictedPyramid = model;
				const auto intended = stick == std::pair<int8_t, int8_t>(0, 0)
					? std::pair<int16_t, float>(marioState->faceAngle[1], 0.0f)
					: Inputs::GetIntendedYawMagFromInput(stick.first, stick.second, camera->yaw);
				check.modelPlays = AirStep::Frame(predicted, predictedPyramid, intended.first, intended.second, (_buttons & Buttons::B) != 0);
				ExecuteAdhoc([&]()
					{
						AdvanceFrameWrite(Inputs(_buttons, stick.first, stick.second));
						check.gameAction = marioState->action;
						check.gameX = marioState->pos[0];
						check.gameY = marioState->pos[1];
						check.gameZ = marioState->pos[2];
						check.gameSpeed = marioState->forwardVel;
						check.gameVy = marioState->vel[1];
						check.gamePlatform = marioState->marioObj->platform == pyramid;
						return true;
					});
				if (check.modelPlays)
				{
					check.modelAction = predicted.action;
					check.modelX = predicted.x;
					check.modelY = predicted.y;
					check.modelZ = predicted.z;
					check.modelSpeed = predicted.forwardVel;
					check.modelVy = predicted.vy;
					check.modelPlatform = predicted.onPlatform;
					check.same = check.gameAction == check.modelAction && check.gameX == check.modelX && check.gameY == check.modelY && check.gameZ == check.modelZ
						&& check.gameSpeed == check.modelSpeed && check.gameVy == check.modelVy && check.gamePlatform == check.modelPlatform;
				}
				else
					check.same = check.gameAction == ACT_MOVE_PUNCHING; // the game punched: a stick too small for the dive
				CustomStatus.checks.push_back(check);
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _frame;
		uint16_t _buttons;
	};
}

// The air model is the game, bit for bit (ROADMAP 4.11, 2026-10-21): at every frame of both
// example movies' dive recovers, the dive's frame (B on the walk), the rollout's press on the
// slide after the pause, the rollout's air frames and the landing, each of 321 sticks played
// in the game gives the model's position, speed, vertical speed, action and platform exactly,
// and the sticks too small for a dive punch, which the model refuses.
TEST_CASE("sc-ending: the air model is the game's at every dive recover frame of the example movies")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	const fs::path movies = fs::path(config.M64Path).parent_path();
	const std::string uscz = (movies / "uscz_pyramid_-1945_southeast_-p15259_p82917_p41379.m64.m64").string();
	const std::string northeast = ExampleMovie(config);
	struct Case
	{
		std::string movie;
		int64_t frame;
		uint16_t buttons;
	};
	const Case cases[] = {
		{ uscz, 3465, Buttons::B }, { uscz, 3469, Buttons::B }, { uscz, 3470, 0 }, { uscz, 3471, 0 }, { uscz, 3472, 0 },
		{ northeast, 4433, Buttons::B }, { northeast, 4437, Buttons::B }, { northeast, 4438, 0 }, { northeast, 4439, 0 }, { northeast, 4440, 0 },
	};
	for (const Case& c : cases)
	{
		M64 m64(c.movie);
		REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie: ", c.movie);
		auto status = TopLevelScriptBuilder<AirGrid>::Build(m64).ImportResource(&resources[0]).Run(c.frame, c.buttons);
		REQUIRE(status.checks.size() == 321u);
		int played = 0;
		int mismatches = 0;
		const AirCheck* first = nullptr;
		for (const AirCheck& check : status.checks)
		{
			played += check.modelPlays ? 1 : 0;
			if (!check.same)
			{
				mismatches++;
				if (first == nullptr)
					first = &check;
			}
		}
		CHECK_MESSAGE(mismatches == 0, "frame ", c.frame, " of ", c.movie, ": ", mismatches, " sticks differ from the model; the first, (", int(first->sx), ", ", int(first->sy), "): game action ",
			first->gameAction, " at (", first->gameX, ", ", first->gameY, ", ", first->gameZ, ") speed ", first->gameSpeed, " vy ", first->gameVy, " platform ", first->gamePlatform,
			"; model action ", first->modelAction, " at (", first->modelX, ", ", first->modelY, ", ", first->modelZ, ") speed ", first->modelSpeed, " vy ", first->modelVy, " platform ", first->modelPlatform);
		// The dive's frame: about half the grid's sticks are too small for a dive and punch.
		if (c.buttons == Buttons::B && (c.frame == 3465 || c.frame == 4433))
			CHECK(played == 164);
		else
			CHECK(played == 321);
	}
}

namespace
{
	// From a frame of a movie: the inputs given, one a frame, each frame's action and speed
	// recorded.
	class StopTrace : public TopLevelScript<Sm64Resource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			std::vector<std::pair<uint32_t, float>> frames; // action, forward speed after each input
			std::vector<std::array<float, 4>> positions;     // x, y, z and whether the pyramid is his platform, after each input
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		StopTrace(int64_t frame, std::vector<Inputs> inputs) : _frame(frame), _inputs(std::move(inputs)) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_frame);
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			for (const Inputs& inputs : _inputs)
			{
				AdvanceFrameWrite(inputs);
				CustomStatus.frames.emplace_back(marioState->action, marioState->forwardVel);
				Object* objectPool = (Object*)(ReadState("gObjectPool"));
				CustomStatus.positions.push_back({ marioState->pos[0] + 1945.0f, marioState->pos[1] + 3071.0f, marioState->pos[2] + 715.0f, marioState->marioObj->platform == &objectPool[84] ? 1.0f : 0.0f });
			}
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _frame;
		std::vector<Inputs> _inputs;
	};
}

// A landing stop's first frame still carries the landing's speed (ROADMAP 4.11, 2026-10-21):
// the game reads the stick before the stop zeroes the speed, so a stick on that frame cancels
// the stop into a walk at the landing's speed, and the quickturn, the smallest stick into a
// walk under a unit of speed and then idle, is from the stop's second frame. The northeast
// movie's landing at 4441, 46 of speed, 4 units inside the -z edge: the walk at 46 runs off
// it in the same frame.
TEST_CASE("sc-ending: a stick on a landing stop's first frame keeps the landing's speed; the quickturn is from its second")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64(ExampleMovie(config));
	REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie");
	const int16_t cameraYaw = -24576; // the movie's camera at the landing
	auto least = Inputs::GetClosestInputByYawHau(cameraYaw, std::nextafter(0.0f, 1.0f), cameraYaw);
	const Inputs stick(0, least.first, least.second);
	const Inputs neutral(0, 0, 0);

	auto first = TopLevelScriptBuilder<StopTrace>::Build(m64).ImportResource(&resources[0]).Run(4441, std::vector<Inputs> { stick, neutral });
	REQUIRE(first.frames.size() == 2u);
	CHECK(first.frames[0].first != ACT_FREEFALL_LAND_STOP); // the walk at the landing's speed, which runs off the -z edge in the same frame
	CHECK(first.frames[0].second > 40.0f);
	CHECK(first.frames[1].first != ACT_IDLE);

	auto second = TopLevelScriptBuilder<StopTrace>::Build(m64).ImportResource(&resources[0]).Run(4441, std::vector<Inputs> { neutral, stick, neutral });
	REQUIRE(second.frames.size() == 3u);
	CHECK(second.frames[0].first == ACT_FREEFALL_LAND_STOP);
	CHECK(second.frames[0].second == 0.0f);
	CHECK(second.frames[1].first == ACT_WALKING);
	CHECK(second.frames[1].second < 1.0f); // the walk's first frame at the smallest stick: under a unit of speed
	CHECK(second.frames[2].first == ACT_IDLE);
}

// The punch from a landing stop (ROADMAP 4.11, 2026-10-21): B on the stop's first frame, the
// landing's speed still there, cancels the stop into the standing punch, whose frames step
// the punch's own 10, 7, 5 and 3 in the facing. The northeast movie's landing at 4441 faces
// -z about 7 units inside the -z edge. The punch alone never falls: a ground step past an
// edge more than 100 above the lava takes one quarter step and stops, the punch ignores
// that it left the ground, and the edge sweeps outward about 3.5 a frame, so by the next
// frame it is under him again; he moves 2 to 4 a frame beside it at the platform's height,
// still punching, the pyramid his platform again on the third frame. The punch cancel, a
// full stick at the facing on the punch's second frame, is a walk that keeps the punch's
// speed, and the walk's step off the edge is the fall.
TEST_CASE("sc-ending: a punch from a landing stop steps 10, then 7, and never falls off the sweeping edge; the punch cancel's walk does")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64(ExampleMovie(config));
	REQUIRE_MESSAGE(m64.load(), "the example movie is not beside the config's movie");
	const int16_t cameraYaw = -24576;
	const Inputs punch(Buttons::B, 0, 0);
	const Inputs neutral(0, 0, 0);
	auto facing = Inputs::GetClosestInputByYawHau(-28518, 32, cameraYaw); // the landing's facing
	const Inputs stick(0, facing.first, facing.second);

	// The punch held through: 10, 7, 5, 3, all on the ground beside the edge, none a fall.
	auto held = TopLevelScriptBuilder<StopTrace>::Build(m64).ImportResource(&resources[0]).Run(4441, std::vector<Inputs> { punch, neutral, neutral, neutral });
	REQUIRE(held.frames.size() == 4u);
	const float speeds[4] = { 10.0f, 7.0f, 5.0f, 3.0f };
	for (size_t i = 0; i < 4; i++)
	{
		CHECK(held.frames[i].first == ACT_PUNCHING);
		CHECK(held.frames[i].second == speeds[i]);
		CHECK(held.positions[i][1] > 120.0f); // the platform's height above the lava, never the fall
		CHECK(held.positions[i][2] < -200.0f); // past the -z edge's line of the landing frame
	}
	CHECK(held.positions[0][3] == 0.0f); // past the edge: not his platform on the first frame
	CHECK(held.positions[2][3] == 1.0f); // the edge swept under him: his platform again

	// The punch cancel: the punch's frame, then the stick held, a walk at the punch's speed or
	// more that steps off, and the fall, which the sweeping edge may catch once or twice on
	// its way (a touch down that his speed steps off again, as the walk-off at 8 above has)
	// before he is clear of it and falling.
	auto cancel = TopLevelScriptBuilder<StopTrace>::Build(m64).ImportResource(&resources[0]).Run(4441, std::vector<Inputs> { punch, stick, stick, stick, stick, stick, stick, stick });
	REQUIRE(cancel.frames.size() == 8u);
	CHECK(cancel.frames[0].first == ACT_PUNCHING);
	CHECK(cancel.frames[0].second == 10.0f);
	const bool walkingOrAir = cancel.frames[1].first == ACT_WALKING || (cancel.frames[1].first & ACT_FLAG_AIR) != 0;
	CHECK(walkingOrAir);
	CHECK(cancel.frames[1].second > 9.0f);
	CHECK(cancel.frames[1].second < 13.0f);
	std::string trace;
	for (size_t i = 0; i < cancel.frames.size(); i++)
		trace += std::string(Sm64ActionName(cancel.frames[i].first)) + " " + std::to_string(cancel.frames[i].second) + " at " + std::to_string(cancel.positions[i][1]) + "; ";
	INFO("the frames after B and the stick held: ", trace);
	CHECK(cancel.frames.back().first == ACT_FREEFALL);
	CHECK(cancel.positions.back()[1] < 110.0f); // clear of the edge and falling
}

// The dive in the air (ROADMAP 4.11, 2026-10-21): B on a fall is a dive that adds 15 to the
// speed along the facing, 48 at most, and keeps the fall's vertical speed and the facing. The
// maintainer's third example, SingleBullyBlueprint2024.m64, leaves a landing stop at 8 and
// dives in the air on its second fall frame, 3584 to 3585, at 21.7, falling at 8.
TEST_CASE("sc-ending: B on a fall is a dive at the fall's speed plus 15, the vertical speed kept")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);
	Configuration config = pipeline.ScattershotConfiguration(pipeline.stages.at(0));
	M64 m64((fs::path(config.M64Path).parent_path() / "SingleBullyBlueprint2024.m64").string());
	REQUIRE_MESSAGE(m64.load(), "the third example movie is not beside the config's movie");
	const Inputs dive(Buttons::B, 0, 0);
	const Inputs neutral(0, 0, 0);
	auto trace = TopLevelScriptBuilder<StopTrace>::Build(m64).ImportResource(&resources[0]).Run(3584, std::vector<Inputs> { dive, neutral });
	REQUIRE(trace.frames.size() == 2u);
	CHECK(trace.frames[0].first == ACT_DIVE);
	CHECK(trace.frames[0].second > 20.0f); // 6.44 at the fall, plus 15, less the air's drag
	CHECK(trace.frames[0].second < 22.0f);
	CHECK(trace.positions[0][1] < 103.0f); // the fall went on: 8 down from 110.6 above the lava, no rise
	CHECK(trace.frames[1].first == ACT_DIVE);
}

namespace
{
	// The maintainer's fourth example (pyramid_-1945_southwest_-p35726_p87575_-p20781.m64,
	// beside the config's movie; 2026-11-19): the uscx example turned half a turn. The
	// oscillations in corner 3, their last crossing at 4418 with the normal (-0.207, -0.498),
	// the final run along the -x edge into corner 4, the dive recover's landing at 4441, the
	// leave over the +z edge at 4443 where the edge stands 116 above the lava, a fall that
	// moves 54 units sideways up the edge, and the squish at 4450 with the normal the file's name
	// carries, the side's normal y at -0.499; no bully in the movie.
	fs::path SouthwestMovie(const PipelineConfig& pipeline)
	{
		return pipeline.m64.parent_path() / "pyramid_-1945_southwest_-p35726_p87575_-p20781.m64";
	}
	constexpr int64_t SouthwestCrossing = 4418;
	constexpr int64_t SouthwestLeave = 4443;
	constexpr int64_t SouthwestCatch = 4449;

	BitfsScEndingArgs SouthwestArgs()
	{
		BitfsScEndingArgs args;
		args.initialFrame = SouthwestCrossing;
		args.oscQuadrant = 3;
		args.targetQuadrant = 4;
		args.platform = 84;
		args.targetGiven = false;
		args.squishEdgeX = 0;
		args.squishEdgeZ = 1;
		args.bully = 27;
		args.probeWindow = 10;
		return args;
	}

	// The ending's search with the source movie's own inputs for its moves up to a frame, and
	// its own moves from that frame on: every frame is judged by the stage's validation, its
	// metric script and its probe, as a pellet's frames are.
	class ReplayedEnding : public Scattershot_BitfsScEnding
	{
	public:
		ReplayedEnding(Alias_Scattershot_BitfsScEnding& scattershot, const BitfsScEndingArgs& args, int64_t replayBefore)
			: Scattershot_BitfsScEnding(scattershot, args), _replayBefore(replayBefore) {}

		bool ApplyMovement() override
		{
			if (int64_t(GetCurrentFrame()) >= _replayBefore)
				return Scattershot_BitfsScEnding::ApplyMovement();
			AdvanceFrameWrite(GetInputs(int64_t(GetCurrentFrame())));
			return true;
		}

	private:
		int64_t _replayBefore;
	};

	// The search from the fourth example's last crossing on the game, one thread, deterministic:
	// the movie's inputs before `replayBefore`, the stage's moves after.
	std::vector<ScattershotSolution<BitfsScEndingSolution>> RunSouthwest(LibSm64& resource, const BitfsScEndingArgs& args, int64_t replayBefore, int maxShots)
	{
		PipelineConfig pipeline = PipelineConfig::Load(g_config);
		pipeline.threads = 1;
		pipeline.outputDirectory = TestOutputDirectory();
		StageConfig stage = StageOfType(pipeline, "sc-ending");
		stage.m64 = SouthwestMovie(pipeline);
		stage.startFrame = SouthwestCrossing;
		stage.scattershot["deterministic"] = true;
		stage.scattershot["csvSamplePeriod"] = 1000000; // a CSV of the rows the ending forces, in the test directory: with none, each forced row prints a write error (ROADMAP 4.24)
		stage.scattershot["seed"] = 6;
		stage.scattershot["pelletsPerShot"] = 200;
		stage.scattershot["pelletMaxScripts"] = 40; // the crossing to the catch is 31 frames: one pellet replays them all
		stage.scattershot["pelletMaxFrameDistance"] = 50;
		stage.scattershot["maxConsecutiveFailedPellets"] = 10;
		stage.scattershot["maxShots"] = maxShots;
		stage.scattershot["maxSolutions"] = 1;
		Configuration config = pipeline.ScattershotConfiguration(stage);
		return Scattershot_BitfsScEnding::ConfigureScattershot(config)
			.ImportResourcePerThread([&](int) -> Sm64Resource* { return &resource; })
			.ConfigureMetricScript(args)
			.Run<ReplayedEnding>(args, replayBefore);
	}
}

// The stage accepts the maintainer's fourth example (ROADMAP 4.11, 2026-11-20): with the
// movie's own inputs for its moves from the last crossing on, every frame passes the stage's
// validation and the probe finds the squish cancel on the frame before the movie's squish.
// The leave is the frame that failed before: Mario steps off where the edge stands 20 under
// the catch's window and moves sideways up it as he falls, and the rule on where the catch will
// come carried him along the edge by his velocity alone, about a unit a frame at the step
// off. It now asks whether the window is within what the air's controls can still reach.
TEST_CASE("sc-ending: the stage's rules accept the southwest example and its probe finds the squish cancel")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	REQUIRE_MESSAGE(fs::exists(SouthwestMovie(pipeline)), "the fourth example movie is not beside the config's movie");
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	BitfsScEndingArgs args = SouthwestArgs();
	auto solutions = RunSouthwest(resources[0], args, std::numeric_limits<int64_t>::max(), 5);
	REQUIRE(solutions.size() == 1);
	const BitfsScEndingSolution& solution = solutions[0].data;
	CHECK(solution.approach == int(BitfsScEndingMetrics::Approach::DIVE_RECOVER));
	CHECK(solution.departureFrame == SouthwestLeave);
	CHECK(solution.squishFrame == SouthwestCatch);
	CHECK(solution.probeNormalX == doctest::Approx(-0.357264161).epsilon(0.000001)); // the normal in the movie's name
	CHECK(solution.probeNormalZ == doctest::Approx(-0.207810417).epsilon(0.000001));
	CHECK(std::fabs(solution.probeSpeed) >= args.squishMinSpeed);
	CHECK(solution.probeHeight - BitfsScEndingMetrics::LavaHeight >= args.squishMinClearance);
}

// The fall to the catch (ROADMAP 4.11, 2026-11-20). The squish needs the top more than 78
// above Mario, which a fall from the edge has on its seventh air frame; the example's is
// squished after six, because it moves sideways up the rising edge and the top over it rises 20,
// and a frame later its side is no longer steep. From the example's leave, the movie's
// inputs up to it, the stage's air moves find a squish cancel with the fall as one move
// (FALL_TO_CATCH), caught within six air frames; the one-frame air moves alone, whose
// stick is on the line in and out of the platform, are run beside it for the count.
TEST_CASE("sc-ending: from the southwest example's leave, the fall to the catch finds a squish cancel")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	REQUIRE_MESSAGE(fs::exists(SouthwestMovie(pipeline)), "the fourth example movie is not beside the config's movie");
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	BitfsScEndingArgs fall = SouthwestArgs();
	fall.weightStrainBack = 0;
	fall.weightAirDive = 0;
	fall.weightNutLand = 0;
	auto solutions = RunSouthwest(resources[0], fall, SouthwestLeave, 20);
	REQUIRE(solutions.size() == 1);
	const BitfsScEndingSolution& solution = solutions[0].data;
	CHECK(solution.departureFrame == SouthwestLeave);
	CHECK(solution.squishFrame - solution.departureFrame <= 6);
	CHECK(std::fabs(solution.probeSpeed) >= fall.squishMinSpeed);
	CHECK(solution.probeHeight - BitfsScEndingMetrics::LavaHeight >= fall.squishMinClearance);

	BitfsScEndingArgs oneFrame = SouthwestArgs();
	oneFrame.weightFallToCatch = 0;
	auto without = RunSouthwest(resources[0], oneFrame, SouthwestLeave, 20);
	char line[200];
	std::snprintf(line, sizeof(line), "the fall to the catch: caught %lld frames after the leave, normal (%.3f, %.3f); the one-frame air moves alone: %zu squish cancel(s) in 20 shots",
		(long long)(solution.squishFrame - solution.departureFrame), double(solution.probeNormalX), double(solution.probeNormalZ), without.size());
	MESSAGE(line);
}

namespace
{
	// The fourth example's oscillations: its rest, and its fifth and sixth crossings as the dr
	// stage's metric script counts them, 30 frames apart. At the sixth Mario runs at 31.06.
	constexpr int64_t SouthwestRest = 4183;
	constexpr int64_t SouthwestFifthCrossing = 4326;
	constexpr int64_t SouthwestSixthCrossing = 4356;
	constexpr int SouthwestRunOutFrames = 10; // the example's run out from its fifth crossing

	// The dr stage's search from the example's fifth crossing with the first half of the
	// oscillation in the example's order instead of drawn: the run out up the axis for the
	// example's ten frames, the turnaround ended facing down the slope, and two frames that
	// keep the facing. The rest of the way back is drawn as the stage draws it. The order
	// is read off the game's state, never counted, since the search replays a block's moves
	// from its seed (hard rule 3). Of the stage's rules only the death rule is kept: the
	// example's normal is off the 0.01 grid the rules conserve, so they would refuse every
	// frame.
	class ExampleOrderOscillation : public Scattershot_BitfsDr
	{
	public:
		ExampleOrderOscillation(Alias_Scattershot_BitfsDr& scattershot, NormalSpecsDto specs, BitfsDrMoveWeights moves)
			: Scattershot_BitfsDr(scattershot, 6, specs, 3, 84, true, 0.0f, moves) {}

		void SelectMovementOptions() override
		{
			const auto state = GetMetrics(GetCurrentFrame());
			const bool runOut = state.phase == BitfsDrMetrics::Phase::RUN_DOWNHILL || state.phase == BitfsDrMetrics::Phase::TURN_UPHILL;
			if (runOut && int64_t(GetCurrentFrame()) - SouthwestFifthCrossing < SouthwestRunOutFrames)
				AddMovementOption(CustomMoves::RUN_UP_AXIS);
			else if (runOut)
				AddMovementOption(CustomMoves::TURN_AROUND); // the frames Mario slows on; the frame he stops on is the next move's
			else if (state.marioAction == ACT_TURNING_AROUND)
				AddMovementOption(CustomMoves::FACE_DOWN_SLOPE);
			else if (GetMetrics(GetCurrentFrame() - 1).marioAction == ACT_TURNING_AROUND || GetMetrics(GetCurrentFrame() - 2).marioAction == ACT_TURNING_AROUND)
				AddMovementOption(CustomMoves::KEEP_FACING); // the two frames after the stop
			else
				Scattershot_BitfsDr::SelectMovementOptions(); // the rest of the way back, drawn
		}

		bool ValidateState() override
		{
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			return !MarioDeath::IsDying(*marioState);
		}

		// The first crossing after the start ends the pellet.
		bool IsSolution() override
		{
			return GetMetrics(GetCurrentFrame()).currentCrossing > GetMetrics(uint64_t(SouthwestFifthCrossing)).currentCrossing;
		}
	};

	struct ExampleOrderCrossings
	{
		size_t found = 0;  // the crossings the search made
		int64_t frame = 0; // the fastest of them: its frame, Mario's speed and the tilt
		float speed = 0;
		float tilt = 0;
	};

	ExampleOrderCrossings RunExampleOrderOscillation(LibSm64& resource, int maxShots)
	{
		PipelineConfig pipeline = PipelineConfig::Load(g_config);
		pipeline.threads = 1;
		pipeline.outputDirectory = TestOutputDirectory();
		StageConfig stage = StageOfType(pipeline, "dr-oscillations");
		stage.m64 = SouthwestMovie(pipeline);
		stage.startFrame = SouthwestFifthCrossing;
		stage.visualize.reset();
		stage.scattershot["deterministic"] = true;
		stage.scattershot["csvSamplePeriod"] = 0;
		stage.scattershot["seed"] = 6;
		stage.scattershot["pelletsPerShot"] = 200;
		stage.scattershot["pelletMaxScripts"] = 40; // a pellet from the start plays the whole oscillation, a move a frame but for the turnaround
		stage.scattershot["pelletMaxFrameDistance"] = 50;
		stage.scattershot["maxConsecutiveFailedPellets"] = 10;
		stage.scattershot["maxShots"] = maxShots;
		stage.scattershot["maxSolutions"] = 100000;
		Configuration config = pipeline.ScattershotConfiguration(stage);

		// What the metric script needs to count the example's crossings from its rest: the
		// leg's end at the turnaround's 16 of speed, a regime from the example's own tilt.
		NormalSpecsDto specs;
		specs.onlyMinMajor = true;
		specs.startXzSum = 0.70f;
		specs.minXzSum = 0.70f;
		specs.maxExitXzSum = 0.72f;
		specs.speedRule = SpeedRule::OFF;
		specs.minAxis = 0.05f;
		specs.restXzSum = 0.677f;
		specs.minMajor = 0.3f;
		specs.maxMajor = 0.601f;
		specs.regionsMajor = 10000;
		specs.minMinor = 0.19f;
		specs.maxMinor = 0.2f;
		specs.regionsMinor = 10000;

		BitfsDrMoveWeights moves;
		moves.keepFacing = 0.4; // the drawn frames of the way back may keep the facing too

		auto solutions = Scattershot_BitfsDr::ConfigureScattershot(config)
			.ImportResourcePerThread([&](int) -> Sm64Resource* { return &resource; })
			.ConfigureMetricScript(SouthwestRest, 3, specs, 15, -0.357264161f, -0.207810417f, 84)
			.Run<ExampleOrderOscillation>(specs, moves);

		ExampleOrderCrossings crossings;
		for (const auto& solution : solutions)
		{
			if (solution.m64Diff.frames.empty())
				continue;
			crossings.found++;
			if (solution.data.fSpd <= crossings.speed)
				continue;
			crossings.frame = int64_t(solution.m64Diff.frames.rbegin()->first) + 1; // the frame after the last input written
			crossings.speed = solution.data.fSpd;
			crossings.tilt = solution.data.xzSum;
		}
		return crossings;
	}
}

// The example movies' three choices as the dr stage's moves (ROADMAP 4.12, 2026-11-21): the
// run out up the axis, the turnaround ended facing down the slope and the frames that keep
// the facing. With those in the example's order from its fifth crossing and the rest of the
// way back drawn, the search's fastest crossing comes where the example's does, within a
// frame, and within two of its speed (the moves the stage had before reached 27.3 from
// there in 2,000 shots; ROADMAP 4.12).
TEST_CASE("dr-oscillations: the run up the axis, the turnaround facing down the slope and the kept facing make the southwest example's oscillation")
{
	PipelineConfig pipeline = PipelineConfig::Load(g_config);
	REQUIRE_MESSAGE(fs::exists(SouthwestMovie(pipeline)), "the fourth example movie is not beside the config's movie");
	std::vector<LibSm64> resources;
	OneResource(pipeline, resources);

	const int shots = 100;
	const ExampleOrderCrossings crossings = RunExampleOrderOscillation(resources[0], shots);
	REQUIRE(crossings.found > 0);
	CHECK(std::llabs((long long)(crossings.frame - SouthwestSixthCrossing)) <= 1);
	CHECK(crossings.speed >= 29.0f);

	char line[240];
	std::snprintf(line, sizeof(line), "the example crosses on frame %lld at 31.06; with the three moves in its order, %zu crossings in %d shots, the fastest on frame %lld at %.2f (tilt %.3f)",
		(long long)SouthwestSixthCrossing, crossings.found, shots, (long long)crossings.frame, double(crossings.speed), double(crossings.tilt));
	MESSAGE(line);
}
