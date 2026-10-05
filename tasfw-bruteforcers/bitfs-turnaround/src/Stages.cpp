#include "Stages.hpp"
#include "ExportSolutions.hpp"
#include "StageArgs.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <cstdio>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>

#include <BitFSPyramidOscillation.hpp>
#include <BitFsAreFixer.hpp>
#include <BitFsObjects.hpp>
#include <BitFsScApproach.hpp>
#include <Scattershot_BitfsDr.hpp>
#include <Scattershot_BitfsDrApproach.hpp>
#include <Scattershot_BitfsDrRecover.hpp>
#include "BitfsOscFinal.hpp"
#include "TiltTargetShot.hpp"

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	// --- Solution data as named numbers ---------------------------------------------------

	// A solution's per-axis values (std::array<float, 3> or std::array<int, 3>) as prefix0..2.
	template <class Values>
	void PutVector(std::map<std::string, double>& metrics, const char* prefix, const Values& values)
	{
		for (size_t i = 0; i < values.size(); i++)
			metrics[prefix + std::to_string(i)] = values[i];
	}

	std::map<std::string, double> Metrics(const TiltTargetShotSolution& data)
	{
		std::map<std::string, double> metrics {
			{ "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "equilibriumFrame", double(data.equilibriumFrame) }
		};
		PutVector(metrics, "error", data.error);
		PutVector(metrics, "remainderError", data.remainderError);
		PutVector(metrics, "adjustedRemainderError", data.adjustedRemainderError);
		PutVector(metrics, "incrementFrames", data.incrementFrames);
		return metrics;
	}

	// The same names as a tilt-target solution, so the oscillation stage's "input:" keys read either.
	std::map<std::string, double> Metrics(const ScriptStatus<BitFsAreFixer>& data)
	{
		std::map<std::string, double> metrics {
			{ "pyraNormX", data.normal[0] }, { "pyraNormY", data.normal[1] }, { "pyraNormZ", data.normal[2] },
			{ "equilibriumFrame", double(data.equilibriumFrame) }, { "restX", data.restPos[0] }, { "restZ", data.restPos[2] },
			{ "rounds", double(data.rounds) }
		};
		PutVector(metrics, "adjustedRemainderError", data.adjustedRemainderError);
		PutVector(metrics, "incrementFrames", data.incrementFrames);
		return metrics;
	}

	std::map<std::string, double> Metrics(const Scattershot_BitfsDr_Solution& data)
	{
		std::map<std::string, double> metrics {
			{ "fSpd", data.fSpd }, { "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "xzSum", data.xzSum }, { "currentOscillation", double(data.currentOscillation) },
			{ "roughTargetAngle", double(data.roughTargetAngle) }
		};
		PutVector(metrics, "incrementFrames", data.incrementFrames);
		return metrics;
	}

	std::map<std::string, double> Metrics(const BitfsOscSolution& data)
	{
		return {
			{ "fSpd", data.fSpd }, { "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "xzSum", data.xzSum }
		};
	}

	std::map<std::string, double> Metrics(const Scattershot_BitfsDrApproach_Solution& data)
	{
		return {
			{ "fSpd", data.fSpd }, { "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "xzSum", data.xzSum }
		};
	}

	std::map<std::string, double> Metrics(const Scattershot_BitfsDrRecover_Solution& data)
	{
		return {
			{ "fSpd", data.fSpd }, { "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "xzSum", data.xzSum }
		};
	}

	// Solutions cross stage boundaries as input diffs only, as main.cpp always did; the data
	// of the new stage's type starts out default.
	template <class TData>
	std::vector<ScattershotSolution<TData>> PipeIn(const SolutionSet* input)
	{
		std::vector<ScattershotSolution<TData>> solutions;
		if (input)
		{
			solutions.reserve(input->solutions.size());
			for (const SolutionRecord& record : input->solutions)
				solutions.emplace_back(TData(), record.m64Diff);
		}
		return solutions;
	}

	template <class TData>
	SolutionSet ToSet(const StageContext& context, const std::vector<ScattershotSolution<TData>>& solutions)
	{
		SolutionSet set;
		set.stage = context.stage.name;
		set.type = context.stage.type;
		set.startFrame = context.stage.startFrame;
		set.solutions.reserve(solutions.size());
		for (const ScattershotSolution<TData>& solution : solutions)
			set.solutions.push_back(SolutionRecord { solution.m64Diff, Metrics(solution.data) });
		return set;
	}

	std::string ArgsWhere(const StageContext& context)
	{
		return "stages[" + context.stage.name + "].args";
	}

	// The "platform" argument: the gObjectPool slot of the tilting pyramid a stage works on, one
	// of the two the level spawns (BitFsObjects.hpp, which the run verifies at start-up); 84,
	// the one the movie's dive lands on, when absent.
	int PyramidSlot(const json& a, const StageContext& context, const std::string& where)
	{
		int platform = int(ArgNumber(a, "platform", context.input, 84, where));
		for (const ExpectedObject& object : BitFsExpectedObjects)
		{
			if (object.slot == platform && std::string_view(object.behavior) == "bhvBitfsTiltingInvertedPyramid")
				return platform;
		}
		ConfigError("\"platform\" in " + where + " must be the slot of a tilting pyramid (BitFsObjects.hpp: 84 or 83), not " + std::to_string(platform));
	}

	M64 LoadMovie(const Configuration& config)
	{
		M64 m64(config.M64Path);
		if (!m64.load())
			throw std::runtime_error("cannot load movie " + config.M64Path.string());
		return m64;
	}

	// --- tilt-target: TiltTargetShot, one pass ----------------------------------------------

	SolutionSet RunTiltTarget(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a,
			{ "initialFrame", "targetNx", "targetNz", "neighborhood", "errorType", "targetDimension", "fixNonTargetDimensionARE",
				"fixTargetDimensionARE", "targetARE", "minNx", "maxNx", "minNz", "maxNz" },
			where);

		TiltTargetShotArgs args;
		args.InitialFrame = int64_t(ArgNumber(a, "initialFrame", context.input, double(context.stage.startFrame), where));
		args.TargetNx = float(ArgNumber(a, "targetNx", context.input, where));
		args.TargetNz = float(ArgNumber(a, "targetNz", context.input, where));
		args.Neighborhood = int(ArgNumber(a, "neighborhood", context.input, 100, where));

		std::string errorType = ArgString(a, "errorType", "adjusted", where);
		if (errorType == "adjusted")
			args.ErrorType = int(TiltTargetShot::ErrorType::ADJUSTED);
		else if (errorType == "absolute")
			args.ErrorType = int(TiltTargetShot::ErrorType::ABSOLUTE_ERROR);
		else
			ConfigError("\"errorType\" in " + where + " must be \"adjusted\" or \"absolute\"");

		std::string dimension = ArgString(a, "targetDimension", "x", where);
		if (dimension != "x" && dimension != "z")
			ConfigError("\"targetDimension\" in " + where + " must be \"x\" or \"z\"");
		args.TargetXDimension = dimension == "x";

		args.FixNonTargetDimensionARE = ArgBool(a, "fixNonTargetDimensionARE", false, where);
		args.FixTargetDimensionARE = ArgBool(a, "fixTargetDimensionARE", false, where);
		args.TargetARE = int(ArgNumber(a, "targetARE", context.input, 0, where));
		args.minNx = float(ArgNumber(a, "minNx", context.input, 0, where));
		args.maxNx = float(ArgNumber(a, "maxNx", context.input, 0, where));
		args.minNz = float(ArgNumber(a, "minNz", context.input, 0, where));
		args.maxNz = float(ArgNumber(a, "maxNz", context.input, 0, where));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<TiltTargetShotSolution>(context.input);
		auto solutions = TiltTargetShot::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureMetricScript(args)
			.Visualize(context.stage.visualize)
			.Run<TiltTargetShot>(args);
		return ToSet(context, solutions);
	}

	// --- dr-oscillations: Scattershot_BitfsDr once per target oscillation ----------------------

	SolutionSet RunDrOscillations(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a,
			{ "equilibriumFrame", "quadrant", "platform", "minOscillationFrames", "targetNx", "targetNz", "maxOscillations",
				"firstShots", "shots", "lastShots", "lastMaxSolutions", "keepTop", "passRetries", "requireIncrementParity", "minFirstCrossingSpeed", "normalSpecs" },
			where);

		int64_t equilibriumFrame = int64_t(ArgNumber(a, "equilibriumFrame", context.input, where));
		int quadrant = int(ArgNumber(a, "quadrant", context.input, 4, where));
		if (quadrant < 1 || quadrant > 4)
			ConfigError("\"quadrant\" in " + where + " must be 1 to 4 (the corner: 1 is +x +z, 2 is +x -z, 3 is -x -z, 4 is -x +z)");
		int platform = PyramidSlot(a, context, where);
		int minOscillationFrames = int(ArgNumber(a, "minOscillationFrames", context.input, 15, where));
		float targetNx = float(ArgNumber(a, "targetNx", context.input, where));
		float targetNz = float(ArgNumber(a, "targetNz", context.input, where));
		int maxOscillations = int(ArgNumber(a, "maxOscillations", context.input, 5, where));
		long long firstShots = (long long)ArgNumber(a, "firstShots", context.input, 50000, where);
		long long shots = (long long)ArgNumber(a, "shots", context.input, 30000, where);
		long long lastShots = (long long)ArgNumber(a, "lastShots", context.input, 30000, where);
		int lastMaxSolutions = int(ArgNumber(a, "lastMaxSolutions", context.input, 1000000, where));
		size_t keepTop = size_t(ArgNumber(a, "keepTop", context.input, 10, where));
		int passRetries = int(ArgNumber(a, "passRetries", context.input, 0, where));
		bool requireIncrementParity = ArgBool(a, "requireIncrementParity", true, where);
		float minFirstCrossingSpeed = float(ArgNumber(a, "minFirstCrossingSpeed", context.input, 0.0, where)); // 0: no floor

		const json& specsJson = RequireObject(a, "normalSpecs", where);
		const std::string specsWhere = where + ".normalSpecs";
		RejectUnknownKeys(specsJson,
			{ "onlyMinMajor", "startXzSum", "minXzSum", "legExitSpeed", "legMinClearance", "maxXzSum", "minAxis", "minLavaClearance", "maxRetreat", "handoverXzSum", "minMajor", "maxMajor", "regionsMajor", "minMinor", "maxMinor", "regionsMinor" }, specsWhere);
		NormalSpecsDto specs;
		specs.onlyMinMajor = ArgBool(specsJson, "onlyMinMajor", true, specsWhere);
		specs.minXzSum = float(ArgNumber(specsJson, "minXzSum", context.input, specsWhere));
		specs.startXzSum = float(ArgNumber(specsJson, "startXzSum", context.input, double(specs.minXzSum), specsWhere));
		if (specs.startXzSum > specs.minXzSum)
			ConfigError("\"startXzSum\" in " + specsWhere + " must not exceed \"minXzSum\"");
		// The old lineup's handover (0: none), against the rest's own tilt from the input's
		// solution: above it, the initial phase runs the tilt up to it first.
		specs.handoverXzSum = float(ArgNumber(specsJson, "handoverXzSum", context.input, 0.0, specsWhere));
		std::optional<double> restNx = context.input ? context.input->Metric("pyraNormX") : std::nullopt;
		std::optional<double> restNz = context.input ? context.input->Metric("pyraNormZ") : std::nullopt;
		specs.restXzSum = restNx && restNz ? float(std::fabs(*restNx) + std::fabs(*restNz)) : 2.0f;
		specs.legExitSpeed = float(ArgNumber(specsJson, "legExitSpeed", context.input, 16.0, specsWhere));
		specs.legMinClearance = float(ArgNumber(specsJson, "legMinClearance", context.input, 0.0, specsWhere));
		specs.maxXzSum = float(ArgNumber(specsJson, "maxXzSum", context.input, 2.0, specsWhere));
		if (specs.maxXzSum < specs.minXzSum)
			ConfigError("\"maxXzSum\" in " + specsWhere + " must not be below \"minXzSum\"");
		specs.minAxis = float(ArgNumber(specsJson, "minAxis", context.input, 0.0, specsWhere));
		specs.minLavaClearance = float(ArgNumber(specsJson, "minLavaClearance", context.input, 0.0, specsWhere));
		specs.maxRetreat = float(ArgNumber(specsJson, "maxRetreat", context.input, double(INFINITY), specsWhere));
		specs.minMajor = float(ArgNumber(specsJson, "minMajor", context.input, specsWhere));
		specs.maxMajor = float(ArgNumber(specsJson, "maxMajor", context.input, specsWhere));
		specs.regionsMajor = float(ArgNumber(specsJson, "regionsMajor", context.input, specsWhere));
		specs.minMinor = float(ArgNumber(specsJson, "minMinor", context.input, specsWhere));
		specs.maxMinor = float(ArgNumber(specsJson, "maxMinor", context.input, specsWhere));
		specs.regionsMinor = float(ArgNumber(specsJson, "regionsMinor", context.input, specsWhere));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		using Solution = ScattershotSolution<Scattershot_BitfsDr_Solution>;

		auto run = [&](int targetOscillation, const std::vector<Solution>& input, bool finalPass, int attempt)
		{
			std::optional<Visualization> visualize = context.stage.visualize; // one viewer tab per pass, and per retry
			if (visualize)
				visualize->title += " (oscillation " + std::to_string(targetOscillation) + (attempt > 0 ? ", retry " + std::to_string(attempt) : "") + ")";
			return Scattershot_BitfsDr::ConfigureScattershot(config)
				.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
				.PipeFrom(input)
				.ConfigureMetricScript(equilibriumFrame, quadrant, specs, minOscillationFrames, targetNx, targetNz, platform)
				.Visualize(visualize)
				.Run<Scattershot_BitfsDr>(targetOscillation, specs, quadrant, platform, finalPass, minFirstCrossingSpeed);
		};

		// The first pass is the swing's first leg from the rest, until Mario can turn around,
		// its solutions the roots of the first oscillation's pass. (Searching for the first
		// oscillation straight from the rest was tried and is worse: the shots go to the
		// lineup's early blocks, whose pellets mostly die, and end after ten failures.)
		// A pass that yields too few roots for the next (fewer than keepTop; for the last pass,
		// none) runs again from the same roots under a seed derived from the run's, up to
		// passRetries times, its solutions pooled. The first oscillation comes about one run in
		// two whatever the rules (ROADMAP 4.6); the retry bounds the stage's failure at those
		// odds to the power of the attempts, and a deterministic run stays one.
		auto runWithRetries = [&](int targetOscillation, const std::vector<Solution>& input, bool finalPass)
		{
			std::vector<Solution> pooled;
			const int seed = config.Seed;
			const std::size_t enough = finalPass ? 1 : keepTop;
			for (int attempt = 0; attempt <= passRetries; attempt++)
			{
				if (attempt > 0)
				{
					config.Seed = seed + attempt * 7919;
					std::printf("dr-oscillations: oscillation %d has %llu solution(s), retrying (%d of %d) under seed %d\n",
						targetOscillation, (unsigned long long)pooled.size(), attempt, passRetries, config.Seed);
				}
				std::vector<Solution> found = run(targetOscillation, input, finalPass, attempt);
				pooled.insert(pooled.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
				if (pooled.size() >= enough)
					break;
			}
			config.Seed = seed;
			return pooled;
		};

		config.MaxShots = firstShots;
		auto initial = PipeIn<Scattershot_BitfsDr_Solution>(context.input);
		std::vector<Solution> solutions = runWithRetries(0, initial, maxOscillations <= 1);
		std::vector<int64_t> passSolutions { int64_t(solutions.size()) }; // each pass's yield, for the set
		{
			// The leg's hand-over, for the run's log: how many roots the first oscillation starts from and their spread.
			auto speeds = solutions | std::views::transform([](const Solution& s) { return s.data.fSpd; });
			auto tilts = solutions | std::views::transform([](const Solution& s) { return s.data.xzSum; });
			std::printf("dr-oscillations: the leg found %llu solution(s)", (unsigned long long)solutions.size());
			if (!solutions.empty())
				std::printf(", speed %.2f to %.2f, tilt %.3f to %.3f", std::ranges::min(speeds), std::ranges::max(speeds), std::ranges::min(tilts), std::ranges::max(tilts));
			std::printf("\n");
		}

		config.MaxShots = shots;
		for (int targetOscillation = 1; targetOscillation < maxOscillations; targetOscillation++)
		{
			bool finalPass = targetOscillation == maxOscillations - 1;
			if (finalPass)
			{
				config.MaxSolutions = lastMaxSolutions;
				config.MaxShots = lastShots;
			}

			solutions = runWithRetries(targetOscillation, solutions, finalPass);
			passSolutions.push_back(int64_t(solutions.size()));
			if (solutions.empty())
			{
				std::printf("dr-oscillations: no solutions at target oscillation %d\n", targetOscillation);
				SolutionSet none = ToSet(context, solutions);
				none.passSolutions = passSolutions;
				return none;
			}
			std::size_t found = solutions.size();

			// After the first oscillation the search commits to one direction, the one most of the
			// keepTop fastest solutions took; the other direction needs one oscillation more. (The
			// solutions arrive in block order, an arbitrary one first: committing to that one's
			// direction kept 1 to 3 slow solutions of the minority direction one run in two and
			// the next pass died on them; the maintainer's inconsistent runs, 2026-10-03.)
			if (targetOscillation == 1)
			{
				const int16_t angleA = BitfsDrMetrics::RoughTargetAngleA(quadrant);
				const int16_t angleB = BitfsDrMetrics::RoughTargetAngleB(quadrant);
				std::stable_sort(solutions.begin(), solutions.end(), [](const Solution& x, const Solution& y) { return x.data.fSpd > y.data.fSpd; });
				std::size_t fastest = std::min(solutions.size(), std::size_t(keepTop));
				std::size_t towardA = std::size_t(std::count_if(solutions.begin(), solutions.begin() + fastest,
					[angleA](const Solution& s) { return s.data.roughTargetAngle == angleA; }));
				bool chooseA = towardA * 2 > fastest || (towardA * 2 == fastest && solutions[0].data.roughTargetAngle == angleA);
				const int16_t other = chooseA ? angleB : angleA;
				std::erase_if(solutions, [other](const Solution& s) { return s.data.roughTargetAngle == other; });
				if (chooseA)
					maxOscillations++;
			}

			if (targetOscillation < maxOscillations - 1)
			{
				std::stable_sort(solutions.begin(), solutions.end(), [](const Solution& x, const Solution& y) { return x.data.fSpd > y.data.fSpd; });
				if (solutions.size() > keepTop)
					solutions.resize(keepTop);
			}

			// The hand-over, for the run's log: what the pass found and what the next one starts from.
			std::printf("dr-oscillations: oscillation %d found %llu solution(s), carrying %llu (direction %d):",
				targetOscillation, (unsigned long long)found, (unsigned long long)solutions.size(),
				solutions.empty() ? 0 : int(solutions[0].data.roughTargetAngle));
			for (const Solution& s : solutions)
				std::printf(" [spd %.2f tilt %.3f osc %d]", s.data.fSpd, s.data.xzSum, s.data.currentOscillation);
			std::printf("\n");
		}

		if (requireIncrementParity)
		{
			std::erase_if(solutions, [](const Solution& s)
				{ return std::abs(s.data.incrementFrames[0]) % 2 != std::abs(s.data.incrementFrames[2]) % 2; });
		}

		SolutionSet set = ToSet(context, solutions);
		set.passSolutions = passSolutions;
		for (SolutionRecord& record : set.solutions)
			record.metrics["equilibriumFrame"] = double(equilibriumFrame); // for "input:equilibriumFrame" downstream
		return set;
	}

	// --- osc-final: BitfsOscFinal -------------------------------------------------------------

	SolutionSet RunOscFinal(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a, { "initialFrame", "oscQuadrant", "targetQuadrant", "targetNx", "targetNz" }, where);

		BitfsOscFinalArgs args;
		args.InitialFrame = int64_t(ArgNumber(a, "initialFrame", context.input, double(context.stage.startFrame), where));
		args.OscQuadrant = int(ArgNumber(a, "oscQuadrant", context.input, 4, where));
		args.TargetQuadrant = int(ArgNumber(a, "targetQuadrant", context.input, 1, where));
		args.TargetNx = float(ArgNumber(a, "targetNx", context.input, where));
		args.TargetNz = float(ArgNumber(a, "targetNz", context.input, where));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<BitfsOscSolution>(context.input);
		auto solutions = BitfsOscFinal::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureMetricScript(args)
			.Visualize(context.stage.visualize)
			.Run<BitfsOscFinal>(args);
		return ToSet(context, solutions);
	}

	// --- dr-approach: Scattershot_BitfsDrApproach (dive from the oscillation) --------------------

	SolutionSet RunDrApproach(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a, { "initialFrame", "oscQuadrant", "targetQuadrant", "minXzSum", "targetNx", "targetNz" }, where);

		int64_t initialFrame = int64_t(ArgNumber(a, "initialFrame", context.input, double(context.stage.startFrame), where));
		int oscQuadrant = int(ArgNumber(a, "oscQuadrant", context.input, 4, where));
		int targetQuadrant = int(ArgNumber(a, "targetQuadrant", context.input, 1, where));
		float minXzSum = float(ArgNumber(a, "minXzSum", context.input, where));
		float targetNx = float(ArgNumber(a, "targetNx", context.input, where));
		float targetNz = float(ArgNumber(a, "targetNz", context.input, where));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<Scattershot_BitfsDrApproach_Solution>(context.input);
		auto solutions = Scattershot_BitfsDrApproach::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureMetricScript(initialFrame, oscQuadrant, targetQuadrant, minXzSum, targetNx, targetNz)
			.Visualize(context.stage.visualize)
			.Run<Scattershot_BitfsDrApproach>();
		return ToSet(context, solutions);
	}

	// --- dr-recover: Scattershot_BitfsDrRecover (land the dive, then the C-up trick) -----------

	SolutionSet RunDrRecover(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a, { "initialFrame", "oscQuadrant", "targetQuadrant", "minXzSum", "phase" }, where);

		int64_t initialFrame = int64_t(ArgNumber(a, "initialFrame", context.input, double(context.stage.startFrame), where));
		int oscQuadrant = int(ArgNumber(a, "oscQuadrant", context.input, 4, where));
		int targetQuadrant = int(ArgNumber(a, "targetQuadrant", context.input, 1, where));
		float minXzSum = float(ArgNumber(a, "minXzSum", context.input, where));

		std::string phaseName = ArgString(a, "phase", "attempt-dr", where);
		BitfsDrRecoverMetrics::Phase phase;
		if (phaseName == "attempt-dr")
			phase = BitfsDrRecoverMetrics::Phase::ATTEMPT_DR;
		else if (phaseName == "c-up-trick")
			phase = BitfsDrRecoverMetrics::Phase::C_UP_TRICK;
		else
			ConfigError("\"phase\" in " + where + " must be \"attempt-dr\" or \"c-up-trick\"");

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<Scattershot_BitfsDrRecover_Solution>(context.input);
		auto solutions = Scattershot_BitfsDrRecover::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureMetricScript(initialFrame, oscQuadrant, targetQuadrant, minXzSum)
			.Visualize(context.stage.visualize)
			.Run<Scattershot_BitfsDrRecover>(phase);
		return ToSet(context, solutions);
	}

	// --- pyramid-osc-approach: the original single-threaded experiment ---------------------------

	class PyramidOscApproach : public TopLevelScript<LibSm64>
	{
	public:
		struct Args
		{
			int64_t startFrame = 0;
			int16_t stickYaw = -16384;
			float stickMagnitude = 32;
			float targetXzSum = 0.69f;
			int quadrant = 3;
			bool alwaysBrake = false;
			int16_t roughTargetAngle = 0;
			int maxIdleWait = 1000;
		};

		// What the scripted oscillation built, for the stage's summary line.
		class CustomScriptStatus
		{
		public:
			bool oscillated = false;
			int oscillations = 0;
			float initialXzSum = 0;
			float finalXzSum[2] = { 0, 0 };
			float maxSpeed[2] = { 0, 0 };
			float normal[3] = { 0, 0, 0 };
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		explicit PyramidOscApproach(Args args) : _args(args) {}

		bool validation() override { return true; }

		bool execution() override
		{
			LongLoad(_args.startFrame);

			Camera* camera = *(Camera**)(ReadState("gCamera"));
			MarioState* marioState = *(MarioState**)(ReadState("gMarioState"));
			auto stick = Inputs::GetClosestInputByYawExact(_args.stickYaw, _args.stickMagnitude, camera->yaw);
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

			int waited = 0;
			while (marioState->action != ACT_IDLE)
			{
				if (++waited > _args.maxIdleWait)
					return false;
				AdvanceFrameWrite(Inputs(0, 0, 0));
			}

			auto oscillation = Modify<BitFsPyramidOscillation>(_args.targetXzSum, _args.quadrant, _args.alwaysBrake);
			CustomStatus.oscillated = oscillation.asserted;
			CustomStatus.oscillations = int(oscillation.oscillationMinMaxFrames.size());
			CustomStatus.initialXzSum = oscillation.initialXzSum;
			for (int i = 0; i < 2; i++)
			{
				CustomStatus.finalXzSum[i] = oscillation.finalXzSum[i];
				CustomStatus.maxSpeed[i] = oscillation.maxSpeed[i];
			}
			Object* pyramid = marioState->floor != nullptr ? marioState->floor->object : nullptr;
			if (pyramid != nullptr)
			{
				CustomStatus.normal[0] = pyramid->oTiltingPyramidNormalX;
				CustomStatus.normal[1] = pyramid->oTiltingPyramidNormalY;
				CustomStatus.normal[2] = pyramid->oTiltingPyramidNormalZ;
			}
			Modify<BitFsScApproach>(_args.roughTargetAngle, _args.quadrant, _args.targetXzSum, oscillation);
			return true;
		}

		bool assertion() override { return true; }

	private:
		Args _args;
	};

	SolutionSet RunPyramidOscApproach(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a, { "stickYaw", "stickMagnitude", "targetXzSum", "quadrant", "alwaysBrake", "roughTargetAngle", "maxIdleWait" }, where);

		PyramidOscApproach::Args args;
		args.startFrame = context.stage.startFrame;
		args.stickYaw = int16_t(ArgNumber(a, "stickYaw", context.input, args.stickYaw, where));
		args.stickMagnitude = float(ArgNumber(a, "stickMagnitude", context.input, args.stickMagnitude, where));
		args.targetXzSum = float(ArgNumber(a, "targetXzSum", context.input, args.targetXzSum, where));
		args.quadrant = int(ArgNumber(a, "quadrant", context.input, args.quadrant, where));
		args.alwaysBrake = ArgBool(a, "alwaysBrake", args.alwaysBrake, where);
		args.roughTargetAngle = int16_t(ArgNumber(a, "roughTargetAngle", context.input, args.roughTargetAngle, where));
		args.maxIdleWait = int(ArgNumber(a, "maxIdleWait", context.input, args.maxIdleWait, where));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		M64 m64 = LoadMovie(config);
		auto status = TopLevelScriptBuilder<PyramidOscApproach>::Build(m64).ImportResource(&context.resources[0]).Run(args);
		std::printf("pyramid-osc-approach: oscillation %s, %d oscillations, xz sum %.3f at the start, %.3f and %.3f at the two ends, "
			"max speed %.1f and %.1f, normal at the end (%.9g, %.9g, %.9g); %llu frame advances\n",
			status.oscillated ? "asserted" : "not asserted", status.oscillations, status.initialXzSum, status.finalXzSum[0], status.finalXzSum[1],
			status.maxSpeed[0], status.maxSpeed[1], status.normal[0], status.normal[1], status.normal[2], (unsigned long long)status.nFrameAdvances);

		SolutionSet set;
		set.stage = context.stage.name;
		set.type = context.stage.type;
		set.startFrame = context.stage.startFrame;
		if (status.asserted && !status.m64Diff.frames.empty())
			set.solutions.push_back(SolutionRecord { status.m64Diff, {} });
		return set;
	}

	// --- are-fixer: BitFsAreFixer, one rest with the wanted ARE, on the first resource ---------

	class AreFixerRoot : public TopLevelScript<LibSm64>
	{
	public:
		class CustomScriptStatus
		{
		public:
			ScriptStatus<BitFsAreFixer> fixer;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		AreFixerRoot(int64_t startFrame, const BitFsAreFixer::Args& args) : _startFrame(startFrame), _args(args) {}

		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_startFrame);
			CustomStatus.fixer = Modify<BitFsAreFixer>(_args);
			return true;
		}
		bool assertion() override { return CustomStatus.fixer.asserted; }

	private:
		int64_t _startFrame;
		BitFsAreFixer::Args _args;
	};

	SolutionSet RunAreFixer(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a,
			{ "targetNx", "targetNz", "tolerance", "minNormal", "maxNormal", "farNormal", "quadrant", "minXzSum", "restX", "restZ", "fineFrames", "sticksPerFrame",
				"verifyPerRound", "maxRounds" },
			where);

		BitFsAreFixer::Args args;
		args.targetNx = float(ArgNumber(a, "targetNx", context.input, where));
		args.targetNz = float(ArgNumber(a, "targetNz", context.input, where));
		args.tolerance = int(ArgNumber(a, "tolerance", context.input, args.tolerance, where));
		args.minNormal = float(ArgNumber(a, "minNormal", context.input, args.minNormal, where));
		args.maxNormal = float(ArgNumber(a, "maxNormal", context.input, args.maxNormal, where));
		args.farNormal = float(ArgNumber(a, "farNormal", context.input, args.farNormal, where));
		args.quadrant = int(ArgNumber(a, "quadrant", context.input, 0, where)); // 0: the target's corner
		if (args.quadrant < 0 || args.quadrant > 4)
			ConfigError("\"quadrant\" in " + where + " must be 1 to 4 (the corner: 1 is +x +z, 2 is +x -z, 3 is -x -z, 4 is -x +z) or absent for the target's");
		args.minXzSum = float(ArgNumber(a, "minXzSum", context.input, where)); // stated, never derived (the maintainer, 2026-09-23)
		args.restX = float(ArgNumber(a, "restX", context.input, 0.0, where)); // both 0: the corner's diagonal at the tilt floor's radius
		args.restZ = float(ArgNumber(a, "restZ", context.input, 0.0, where));
		args.fineFrames = int(ArgNumber(a, "fineFrames", context.input, args.fineFrames, where));
		args.sticksPerFrame = int(ArgNumber(a, "sticksPerFrame", context.input, args.sticksPerFrame, where));
		args.verifyPerRound = int(ArgNumber(a, "verifyPerRound", context.input, args.verifyPerRound, where));
		args.maxRounds = int(ArgNumber(a, "maxRounds", context.input, args.maxRounds, where));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		M64 m64 = LoadMovie(config);
		auto status = TopLevelScriptBuilder<AreFixerRoot>::Build(m64).ImportResource(&context.resources[0]).Run(context.stage.startFrame, args);
		const ScriptStatus<BitFsAreFixer>& fixer = status.fixer;
		std::printf("are-fixer: %s in %d rounds; equilibrium frame %lld, rest (%.9g, %.9g), normal (%.9g, %.9g, %.9g), "
			"ARE (%.0f, %.0f), steps (%d, %d); way: %d run frames, dive yaw %d, air %s, %d slide frames, of %d ways; "
			"%llu frame advances, %llu saves, %llu loads\n",
			fixer.solved ? "solved" : "not solved", fixer.rounds, (long long)fixer.equilibriumFrame, fixer.restPos[0], fixer.restPos[2],
			fixer.normal[0], fixer.normal[1], fixer.normal[2], fixer.adjustedRemainderError[0], fixer.adjustedRemainderError[2],
			fixer.incrementFrames[0], fixer.incrementFrames[2], fixer.runFrames, int(fixer.diveYaw),
			fixer.diveAir == 0 ? "back" : fixer.diveAir == 1 ? "neutral" : "at the yaw", fixer.slideFrames, fixer.ways,
			(unsigned long long)fixer.nFrameAdvances, (unsigned long long)fixer.nSaves, (unsigned long long)fixer.nLoads);

		SolutionSet set;
		set.stage = context.stage.name;
		set.type = context.stage.type;
		set.startFrame = context.stage.startFrame;
		if (status.asserted && !status.m64Diff.frames.empty())
			set.solutions.push_back(SolutionRecord { status.m64Diff, Metrics(fixer) });
		return set;
	}

	// --- export: pass the input through (and let the export flag write it) ---------------------

	SolutionSet RunExport(StageContext& context)
	{
		RejectUnknownKeys(context.stage.args, {}, ArgsWhere(context));
		if (!context.input)
			ConfigError("stage \"" + context.stage.name + "\" (export) needs an input");
		SolutionSet set = *context.input;
		set.stage = context.stage.name;
		set.type = context.stage.type;
		return set;
	}

	const std::vector<StageType> g_stageTypes {
		{ "tilt-target", "TiltTargetShot: reach a target pyramid normal (one pass; chain passes through input)", RunTiltTarget },
		{ "are-fixer", "BitFsAreFixer: from a dive onto the platform, a dive recover whose rollout is steered to rest where the pyramid's normal carries the target's ARE", RunAreFixer },
		{ "dr-oscillations", "Scattershot_BitfsDr once per target oscillation, filtering between oscillations", RunDrOscillations },
		{ "osc-final", "BitfsOscFinal: the final oscillation into the target quadrant", RunOscFinal },
		{ "dr-approach", "Scattershot_BitfsDrApproach: dive from the oscillation (was disabled in main.cpp; unverified)", RunDrApproach },
		{ "dr-recover", "Scattershot_BitfsDrRecover: land the dive (\"attempt-dr\") or the C-up trick (\"c-up-trick\"); unverified", RunDrRecover },
		{ "pyramid-osc-approach", "single-threaded BitFsPyramidOscillation + BitFsScApproach from the start frame", RunPyramidOscApproach },
		{ "export", "no search; passes its input through (use with \"export\": true)", RunExport },
	};
}

const std::vector<StageType>& StageTypes()
{
	return g_stageTypes;
}

const StageType* FindStageType(const std::string& name)
{
	for (const StageType& type : g_stageTypes)
	{
		if (name == type.name)
			return &type;
	}
	return nullptr;
}

SolutionSet RunStage(StageContext& context)
{
	const StageType* type = FindStageType(context.stage.type);
	if (!type)
		ConfigError("stage \"" + context.stage.name + "\": unknown type \"" + context.stage.type + "\"");
	return type->run(context);
}

void ExportSolutionSet(const StageContext& context, const SolutionSet& set)
{
	if (set.solutions.empty())
		return;

	fs::path directory = context.pipeline.outputDirectory / "m64" / context.stage.name;
	fs::create_directories(directory);

	std::vector<M64Diff> diffs;
	diffs.reserve(set.solutions.size());
	for (const SolutionRecord& record : set.solutions)
		diffs.push_back(record.m64Diff);

	Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
	M64 m64 = LoadMovie(config);
	TopLevelScriptBuilder<ExportSolutions>::Build(m64).ImportResource(&context.resources[0]).Run(set.startFrame, diffs, directory);
	std::printf("exported %llu movie(s) to %s\n", (unsigned long long)diffs.size(), directory.string().c_str());
}
