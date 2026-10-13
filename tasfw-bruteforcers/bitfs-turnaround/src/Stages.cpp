#include "Stages.hpp"
#include "ExportSolutions.hpp"
#include "StageArgs.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
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
#include <Scattershot_BitfsScEnding.hpp>
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

	// The normal's names as a tilt-target solution's, so the oscillation stage reads either; the
	// frame it starts from is the fixer's hand-over ("input:handoverFrame").
	std::map<std::string, double> Metrics(const ScriptStatus<BitFsAreFixer>& data)
	{
		std::map<std::string, double> metrics {
			{ "pyraNormX", data.normal[0] }, { "pyraNormY", data.normal[1] }, { "pyraNormZ", data.normal[2] },
			{ "handoverFrame", double(data.handoverFrame) }, { "handoverX", data.handoverPos[0] }, { "handoverZ", data.handoverPos[2] },
			{ "restFrame", double(data.restFrame) }, { "restX", data.restPos[0] }, { "restZ", data.restPos[2] },
			{ "restNormX", data.restNormal[0] }, { "restNormZ", data.restNormal[2] }, { "restErrorX", double(data.restErrorX) },
			{ "rounds", double(data.rounds) }, { "turnRounds", double(data.turnRounds) }, { "framesAfterRest", double(data.framesAfterRest) }
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

	// The ending's solution: what the leave and the squish probe were, so the set of ways
	// through can be read from the solutions file (README.md, "The stages").
	std::map<std::string, double> Metrics(const BitfsScEndingSolution& data)
	{
		return {
			{ "fSpd", data.fSpd }, { "pyraNormX", data.pyraNormX }, { "pyraNormY", data.pyraNormY }, { "pyraNormZ", data.pyraNormZ },
			{ "xzSum", data.xzSum },
			{ "approach", double(data.approach) }, { "leaveAction", double(data.leaveAction) }, { "leaveSpeed", data.leaveSpeed },
			{ "leaveYaw", double(data.leaveYaw) }, { "leaveCUp", double(data.leaveCUp) }, { "quickturn", double(data.quickturn) },
			{ "leaveEdgeDistance", data.leaveEdgeDistance }, { "strainBackFrames", double(data.strainBackFrames) },
			{ "nutLandings", double(data.nutLandings) }, { "departureFrame", double(data.departureFrame) },
			{ "squishFrame", double(data.squishFrame) }, { "probeIndex", double(data.probeIndex) },
			{ "probeDx", data.probeDx }, { "probeDz", data.probeDz }, { "probeSpeed", data.probeSpeed }, { "probeHeight", data.probeHeight },
			{ "probeNormX", data.probeNormalX }, { "probeNormZ", data.probeNormalZ }
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

	// The "bully" argument: the gObjectPool slot of the bully the ending's squish probe moves,
	// one the layout check verifies (BitFsObjects.hpp: 27, the lowest of the level's four, the
	// maintainer's choice); -1 asks for no probe, so no solution is ever found.
	int BullySlot(const json& a, const StageContext& context, const std::string& where)
	{
		int bully = int(ArgNumber(a, "bully", context.input, 27, where));
		if (bully < 0)
			return -1;
		for (const ExpectedObject& object : BitFsExpectedObjects)
		{
			if (object.slot == bully && std::string_view(object.behavior) == "bhvSmallBully")
				return bully;
		}
		ConfigError("\"bully\" in " + where + " must be the slot of a bully the layout check verifies (BitFsObjects.hpp: 27), not " + std::to_string(bully));
	}

	M64 LoadMovie(const Configuration& config)
	{
		M64 m64(config.M64Path);
		if (!m64.load())
			throw std::runtime_error("cannot load movie " + config.M64Path.string());
		return m64;
	}

	// --- the corner: its import from the game, and the replay check of its solutions ----------

	// Plays the game to the frame and exports the corner's level (once; the pointer passed
	// in is reused) and a state for it with the objects named: the one way a script hands
	// out what it reads (ExportSave).
	class CornerImport : public TopLevelScript<Sm64Resource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			std::shared_ptr<const BitfsCornerLevel> level;
			std::optional<ImportedSave<Sm64Mem>> state;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		CornerImport(int64_t frame, BitfsCornerObjects objects, std::shared_ptr<const BitfsCornerLevel> level)
			: _frame(frame), _objects(objects), _level(std::move(level)) { }
		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_frame);
			if (!_level)
				_level = std::make_shared<const BitfsCornerLevel>(ExportSave<BitfsCornerLevel>().state);
			CustomStatus.level = _level;
			CustomStatus.state = ExportSave<Sm64Mem>(*_level, _objects);
			return true;
		}
		bool assertion() override { return CustomStatus.state.has_value(); }

	private:
		int64_t _frame;
		BitfsCornerObjects _objects;
		std::shared_ptr<const BitfsCornerLevel> _level;
	};

	// Replays each solution from the start frame in a sandbox and reads, at its end, what the
	// stages read: the same on either resource, so the two readings can be compared.
	class ReplayProbe : public TopLevelScript<Sm64Resource>
	{
	public:
		struct Probe
		{
			int64_t frame = 0;
			float pos[3] = {};
			float vel[3] = {};
			float forwardVel = 0;
			uint32_t action = 0;
			int16_t faceYaw = 0;
			float floorHeight = 0;
			float normal[3] = {};
			bool operator==(const Probe&) const = default;
		};
		class CustomScriptStatus
		{
		public:
			std::vector<Probe> probes;
		};
		CustomScriptStatus CustomStatus = CustomScriptStatus();

		ReplayProbe(int64_t startFrame, const std::vector<M64Diff>& diffs) : _startFrame(startFrame), _diffs(diffs) { }
		bool validation() override { return true; }
		bool execution() override
		{
			MarioState* marioState = (MarioState*)(ReadState("gMarioStates"));
			Object* objectPool = (Object*)(ReadState("gObjectPool"));
			Object* pyramid = &objectPool[84]; // level-specific index; AGENTS.md, known problems
			LongLoad(_startFrame);
			Save(); // every replay below returns here by a load, not by playing the movie again
			for (const M64Diff& diff : _diffs)
			{
				ExecuteAdhoc([&]()
					{
						Apply(diff);
						Probe probe;
						probe.frame = GetCurrentFrame();
						std::copy(marioState->pos, marioState->pos + 3, probe.pos);
						std::copy(marioState->vel, marioState->vel + 3, probe.vel);
						probe.forwardVel = marioState->forwardVel;
						probe.action = marioState->action;
						probe.faceYaw = marioState->faceAngle[1];
						probe.floorHeight = marioState->floorHeight;
						probe.normal[0] = pyramid->oTiltingPyramidNormalX;
						probe.normal[1] = pyramid->oTiltingPyramidNormalY;
						probe.normal[2] = pyramid->oTiltingPyramidNormalZ;
						CustomStatus.probes.push_back(probe);
						return true;
					});
			}
			return true;
		}
		bool assertion() override { return CustomStatus.probes.size() == _diffs.size(); }

	private:
		int64_t _startFrame;
		const std::vector<M64Diff>& _diffs;
	};

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
			.ImportResourcePerThread([&](auto threadId) { return context.resources[threadId]; })
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
				"firstShots", "shots", "lastShots", "lastMaxSolutions", "keepTop", "passRetries", "handOverFallback", "requireIncrementParity", "minFirstCrossingSpeed", "normalSpecs" },
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
		bool handOverFallback = ArgBool(a, "handOverFallback", true, where);
		float minFirstCrossingSpeed = float(ArgNumber(a, "minFirstCrossingSpeed", context.input, 0.0, where)); // 0: no floor

		const json& specsJson = RequireObject(a, "normalSpecs", where);
		const std::string specsWhere = where + ".normalSpecs";
		RejectUnknownKeys(specsJson,
			{ "onlyMinMajor", "startXzSum", "minXzSum", "legExitSpeed", "legMinClearance", "maxXzSum", "minEndClearance", "minCrossingAxisDiff", "minAxis", "minLavaClearance", "maxRetreat", "handoverXzSum", "minMajor", "maxMajor", "regionsMajor", "minMinor", "maxMinor", "regionsMinor" }, specsWhere);
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
		specs.minEndClearance = float(ArgNumber(specsJson, "minEndClearance", context.input, 0.0, specsWhere));
		specs.minCrossingAxisDiff = float(ArgNumber(specsJson, "minCrossingAxisDiff", context.input, 0.0, specsWhere));
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
				.ImportResourcePerThread([&](auto threadId) { return context.resources[threadId]; })
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

		// Each pass's carried roots go to a file beside the stage's solutions (PassFile), so a
		// run can start at any oscillation from them ("start": "<stage>:<n>"; ROADMAP 4.2).
		std::vector<int64_t> passSolutions; // each pass's yield, for the set
		std::map<int, SolutionSet> carriedSets; // what each pass carried, by pass, as written: for the rewind below
		auto cachePass = [&](int targetOscillation, const std::vector<Solution>& carried)
		{
			SolutionSet set = ToSet(context, carried);
			set.passSolutions = passSolutions;
			for (SolutionRecord& record : set.solutions)
				record.metrics["equilibriumFrame"] = double(equilibriumFrame);
			fs::path file = context.pipeline.PassFile(context.stage.name, targetOscillation);
			fs::create_directories(file.parent_path());
			set.Save(file);
			carriedSets[targetOscillation] = std::move(set);
		};
		// The last pass's exit rules, which a solution must meet to leave the stage: the regime's
		// tilt (minXzSum) and the step parity. The rewind below hands on only such solutions.
		auto meetsExitRules = [&](const SolutionRecord& record)
		{
			auto metric = [&](const char* name)
			{
				auto found = record.metrics.find(name);
				return found == record.metrics.end() ? 0.0 : found->second;
			};
			return metric("xzSum") >= double(specs.minXzSum)
				&& (!requireIncrementParity || std::fmod(std::fabs(metric("incrementFrames0")), 2.0) == std::fmod(std::fabs(metric("incrementFrames2")), 2.0));
		};
		// Each pass's whole pool, before the hand-over trims it, kept by pass and written beside
		// the pass file (<stage>.oscillation<n>.pool.json) so a run started at a later oscillation
		// reads it too: where the rewind takes its solutions, filtered by the exit rules of the run
		// that rewinds, not of the run that wrote the file (the maintainer, 2026-10-12, ROADMAP
		// 4.21: eligibility is the solution's, by the exit tilt; the direction is the pass's).
		std::map<int, SolutionSet> poolSets;
		auto poolFile = [&](int pass) { return context.pipeline.PassFile(context.stage.name, pass).replace_extension(".pool.json"); };
		auto recordPool = [&](int targetOscillation, const std::vector<Solution>& pool)
		{
			SolutionSet set = ToSet(context, pool);
			set.passSolutions = passSolutions;
			for (SolutionRecord& record : set.solutions)
				record.metrics["equilibriumFrame"] = double(equilibriumFrame);
			fs::path file = poolFile(targetOscillation);
			fs::create_directories(file.parent_path());
			set.Save(file);
			poolSets[targetOscillation] = std::move(set);
		};
		// A pass's solutions that meet the exit rules: from its pool, this run's or the file a
		// resumed run's earlier pass wrote, else (a run from before the pool files) from what
		// that pass carried.
		auto exitSet = [&](int pass)
		{
			SolutionSet pool;
			if (auto found = poolSets.find(pass); found != poolSets.end())
				pool = found->second;
			else if (fs::path file = poolFile(pass); fs::exists(file))
				pool = SolutionSet::Load(file);
			else if (auto found = carriedSets.find(pass); found != carriedSets.end())
				pool = found->second;
			else if (fs::path file = context.pipeline.PassFile(context.stage.name, pass); fs::exists(file))
				pool = SolutionSet::Load(file);
			std::erase_if(pool.solutions, [&](const SolutionRecord& record) { return !meetsExitRules(record); });
			return pool;
		};
		// The hand-over, for the run's log: what a pass found and what the next one starts from,
		// by 0.01 by 0.01 cell of the pyramid's normal (one lattice point, the normal stepping
		// 0.01 an axis): how many, their fastest and slowest.
		auto cell = [](const Solution& s) { return std::pair<int, int>(int(std::lround(s.data.pyraNormX * 100.0f)), int(std::lround(s.data.pyraNormZ * 100.0f))); };
		auto logHandOver = [&](int targetOscillation, std::size_t found, const std::vector<Solution>& carried)
		{
			std::map<std::pair<int, int>, std::vector<float>> cells;
			for (const Solution& s : carried)
				cells[cell(s)].push_back(s.data.fSpd);
			std::printf("dr-oscillations: oscillation %d found %llu solution(s), carrying %llu in %llu cell(s) of the normal (direction %d):",
				targetOscillation, (unsigned long long)found, (unsigned long long)carried.size(), (unsigned long long)cells.size(),
				carried.empty() ? 0 : int(carried[0].data.roughTargetAngle));
			for (const auto& [key, speeds] : cells)
				std::printf(" [n (%.2f, %.2f) x%llu spd %.2f..%.2f]", key.first / 100.0, key.second / 100.0, (unsigned long long)speeds.size(),
					*std::max_element(speeds.begin(), speeds.end()), *std::min_element(speeds.begin(), speeds.end()));
			std::printf("\n");
		};

		// The roots a pass hands to the next: the keepTop fastest of its solutions (the
		// maintainer, 2026-10-07; the fastest of each 0.01 cell of the normal was tried on
		// 2026-10-06 and carried the pass's every tilt without a faster crossing, ROADMAP 4.6).
		auto keepFastest = [&](std::vector<Solution>& found)
		{
			std::stable_sort(found.begin(), found.end(), [](const Solution& x, const Solution& y) { return x.data.fSpd > y.data.fSpd; });
			if (found.size() > keepTop)
				found.resize(keepTop);
		};

		const int startOscillation = context.pipeline.startStage == context.stage.name ? context.pipeline.startPass : 0;
		std::vector<Solution> solutions;
		if (startOscillation == 0)
		{
			config.MaxShots = firstShots;
			auto initial = PipeIn<Scattershot_BitfsDr_Solution>(context.input);
			solutions = runWithRetries(0, initial, maxOscillations <= 1);
			passSolutions.push_back(int64_t(solutions.size()));
			{
				// The leg's hand-over, for the run's log: how many roots the first oscillation
				// starts from and their spread. The leg hands on its whole pool, up to the
				// scattershot's maxSolutions; keepTop trims the oscillation passes' output only
				// (the maintainer, 2026-10-12: held to its keepTop fastest, which are one end of the
				// chord, the leg gives the first oscillation nothing, on master's code as on this;
				// docs/performance-changelog.md).
				auto speeds = solutions | std::views::transform([](const Solution& s) { return s.data.fSpd; });
				auto tilts = solutions | std::views::transform([](const Solution& s) { return s.data.xzSum; });
				std::printf("dr-oscillations: the leg found %llu solution(s)", (unsigned long long)solutions.size());
				if (!solutions.empty())
					std::printf(", speed %.2f to %.2f, tilt %.3f to %.3f", std::ranges::min(speeds), std::ranges::max(speeds), std::ranges::min(tilts), std::ranges::max(tilts));
				std::printf("\n");
			}
			cachePass(0, solutions);
			recordPool(0, solutions);
		}
		else
		{
			// From the cache: the roots the pass before carried and the yields so far. The
			// direction the first oscillation committed to is on every root it carried, and when
			// it was the first direction it added an oscillation (the commitment below).
			fs::path file = context.pipeline.PassFile(context.stage.name, startOscillation - 1);
			if (!fs::exists(file))
				ConfigError("\"start\" asks stage \"" + context.stage.name + "\" to begin at oscillation " + std::to_string(startOscillation)
					+ " but " + file.string() + " does not exist; run the stage from an earlier oscillation first");
			SolutionSet cached = SolutionSet::Load(file);
			if (cached.solutions.empty())
				ConfigError(file.string() + " holds no roots to start from");
			solutions = PipeIn<Scattershot_BitfsDr_Solution>(&cached);
			carriedSets[startOscillation - 1] = cached;
			passSolutions = cached.passSolutions;
			if (startOscillation >= 2)
			{
				std::optional<double> direction = cached.Metric("roughTargetAngle");
				if (direction && int16_t(*direction) == BitfsDrMetrics::RoughTargetAngleA(quadrant))
					maxOscillations++;
			}
			if (startOscillation >= maxOscillations)
				ConfigError("\"start\" asks stage \"" + context.stage.name + "\" to begin at oscillation " + std::to_string(startOscillation)
					+ ", past its last pass (maxOscillations " + std::to_string(maxOscillations) + ", the first oscillation's direction counted)");
			std::printf("dr-oscillations: starting at oscillation %d from the %llu root(s) in %s\n",
				startOscillation, (unsigned long long)solutions.size(), file.string().c_str());
		}

		config.MaxShots = shots;
		for (int targetOscillation = std::max(1, startOscillation); targetOscillation < maxOscillations; targetOscillation++)
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
				// The rewind (the maintainer, 2026-10-06, kept on 2026-10-10 over the free oscillations
				// tried between, ROADMAP 4.12; widened on 2026-10-12, ROADMAP 4.21): a pass that yields
				// nothing hands on every earlier solution that meets the last pass's rules, the
				// regime's tilt and the step parity, from the passes whose crossing heads toward the
				// squish-cancel quadrant (the final pass's parity, the passes alternating), each pass's
				// whole pool, not only the roots it carried. Eligibility is the solution's, the
				// direction the pass's, so a set that mixes regime and sub-regime tilts hands on its
				// regime part. A failing middle pass thus ends the stage early instead of ending the
				// run; none at all is the failure it was. The solutions are handed on as the pass
				// wrote them, records with their metrics (a pipe-in keeps only the inputs, and the
				// next stage reads the metrics: the direction, the hand-over frame), from this run
				// or from the pool files of the run a start at a later oscillation resumed.
				SolutionSet fallback;
				std::string fallbackPasses;
				for (int pass = targetOscillation - 1; handOverFallback && pass >= 0; pass--)
				{
					if ((maxOscillations - 1 - pass) % 2 != 0)
						continue;
					SolutionSet candidate = exitSet(pass);
					if (candidate.solutions.empty())
						continue;
					if (fallback.solutions.empty())
					{
						fallback.stage = candidate.stage;
						fallback.type = candidate.type;
						fallback.startFrame = candidate.startFrame;
					}
					fallbackPasses += (fallbackPasses.empty() ? "" : ", ") + std::to_string(pass) + " (" + std::to_string(candidate.solutions.size()) + ")";
					fallback.solutions.insert(fallback.solutions.end(),
						std::make_move_iterator(candidate.solutions.begin()), std::make_move_iterator(candidate.solutions.end()));
				}
				if (fallback.solutions.empty())
				{
					std::printf("dr-oscillations: no solutions at target oscillation %d\n", targetOscillation);
					SolutionSet none = ToSet(context, solutions);
					none.passSolutions = passSolutions;
					return none;
				}
				std::printf("dr-oscillations: no solutions at target oscillation %d; handing on the %llu solution(s) at the exit tilt from oscillation(s) %s, the passes whose crossing heads toward the squish-cancel quadrant\n",
					targetOscillation, (unsigned long long)fallback.solutions.size(), fallbackPasses.c_str());
				fallback.passSolutions = passSolutions;
				return fallback;
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

			recordPool(targetOscillation, solutions);

			// The roots the next pass starts from: the keepTop fastest. The last pass hands on
			// all of its solutions.
			if (targetOscillation < maxOscillations - 1)
				keepFastest(solutions);

			logHandOver(targetOscillation, found, solutions);
			cachePass(targetOscillation, solutions);
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

	// --- sc-ending: Scattershot_BitfsScEnding (the final run, the approach, the leave, the squish test) ---

	SolutionSet RunScEnding(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a, { "initialFrame", "targetQuadrant", "targetEdge", "platform", "targetNx", "targetNz", "edgeSide", "maxLeaveSpeed",
			"bully", "bullySpeed", "probeFrames", "probeWindow", "squishMinSpeed", "squishMinClearance", "pauseBuffer", "moves" }, where);

		BitfsScEndingArgs args;
		args.initialFrame = int64_t(ArgNumber(a, "initialFrame", context.input, double(context.stage.startFrame), where));
		args.targetQuadrant = int(ArgNumber(a, "targetQuadrant", context.input, args.targetQuadrant, where));
		args.platform = PyramidSlot(a, context, where);
		// The target normal, or none (the maintainer, 2026-10-06): without targetNx and targetNz
		// the ending conserves the hand-over's own lattice and a squish cancel at any normal on
		// it counts, the solution recording which (probeNormX, probeNormZ).
		args.targetGiven = a.contains("targetNx") || a.contains("targetNz");
		if (args.targetGiven)
		{
			args.targetNx = float(ArgNumber(a, "targetNx", context.input, where));
			args.targetNz = float(ArgNumber(a, "targetNz", context.input, where));
		}
		args.edgeSide = float(ArgNumber(a, "edgeSide", context.input, args.edgeSide, where));
		args.maxLeaveSpeed = float(ArgNumber(a, "maxLeaveSpeed", context.input, args.maxLeaveSpeed, where));

		// The target edge (the maintainer, 2026-10-06): the axis the squish ledge runs along, "x"
		// or "z" (uscx, uscz; AGENTS.md's glossary), of the target corner's two edges. The squish
		// edge's outward side is then the corner's sign on the other axis, and the oscillation
		// corner is the one across the corner's other edge, the run's; the piped-in solutions must
		// be oriented toward the squish edge, their last crossing's run heading at it.
		{
			const int target = args.targetQuadrant;
			if (target < 1 || target > 4)
				ConfigError("\"targetQuadrant\" in " + where + " must be 1 to 4 (the corner: 1 is +x +z, 2 is +x -z, 3 is -x -z, 4 is -x +z)");
			const int signX = BitfsScEndingMetrics::CornerSignX(target);
			const int signZ = BitfsScEndingMetrics::CornerSignZ(target);
			std::string edge = ArgString(a, "targetEdge", "z", where);
			int oscSignX = signX;
			int oscSignZ = signZ;
			if (edge == "z") { args.squishEdgeX = signX; args.squishEdgeZ = 0; oscSignX = -signX; } // the z-aligned edge faces along x
			else if (edge == "x") { args.squishEdgeX = 0; args.squishEdgeZ = signZ; oscSignZ = -signZ; }
			else
				ConfigError("\"targetEdge\" in " + where + " must be \"x\" or \"z\", the axis the squish ledge runs along (uscx, uscz), not \"" + edge + "\"");
			args.oscQuadrant = oscSignX > 0 ? (oscSignZ > 0 ? 1 : 2) : (oscSignZ > 0 ? 4 : 3);
			edge = std::string(args.squishEdgeX + args.squishEdgeZ > 0 ? "+" : "-") + (args.squishEdgeX != 0 ? "x" : "z") + " (the " + edge + "-aligned edge)";
			// The oscillation solutions end at a crossing, Mario running toward one chord end while
			// the normal, caught up, steps toward the other: their roughTargetAngle is that other end,
			// so his heading is its opposite (the ends differ by a half turn). Measured on the
			// committed run's hand-off: roughTargetAngle -24576, Mario at yaw 6699 toward +x and +z.
			std::optional<double> direction = context.input ? context.input->Metric("roughTargetAngle") : std::nullopt;
			if (direction)
			{
				const int16_t heading = int16_t(int16_t(*direction) + 0x8000);
				const float runX = gSineTable[uint16_t(heading) >> 4];
				const float runZ = gCosineTable[uint16_t(heading) >> 4];
				if (runX * float(args.squishEdgeX) + runZ * float(args.squishEdgeZ) < 0.5f)
					ConfigError("stage \"" + context.stage.name + "\": the input's solutions leave their last crossing heading toward yaw " + std::to_string(int(heading))
						+ ", away from the squish edge " + edge + "; the oscillation stage's last pass must cross with Mario running toward that edge's corner");
			}
		}
		args.bully = BullySlot(a, context, where);
		args.bullySpeed = float(ArgNumber(a, "bullySpeed", context.input, args.bullySpeed, where));
		args.probeFrames = int(ArgNumber(a, "probeFrames", context.input, args.probeFrames, where));
		args.probeWindow = int(ArgNumber(a, "probeWindow", context.input, args.probeWindow, where));
		args.squishMinSpeed = float(ArgNumber(a, "squishMinSpeed", context.input, args.squishMinSpeed, where));
		args.squishMinClearance = float(ArgNumber(a, "squishMinClearance", context.input, args.squishMinClearance, where));
		args.pauseBuffer = ArgBool(a, "pauseBuffer", args.pauseBuffer, where);
		if (a.contains("moves"))
		{
			const json& moves = RequireObject(a, "moves", where);
			const std::string movesWhere = where + ".moves";
			RejectUnknownKeys(moves, { "dive", "brake", "walkOff", "parallel", "quickturn", "cUp", "wait", "strainBack" }, movesWhere);
			args.weightDive = ArgNumber(moves, "dive", context.input, args.weightDive, movesWhere);
			args.weightBrake = ArgNumber(moves, "brake", context.input, args.weightBrake, movesWhere);
			args.weightWalkOff = ArgNumber(moves, "walkOff", context.input, args.weightWalkOff, movesWhere);
			args.weightParallel = ArgNumber(moves, "parallel", context.input, args.weightParallel, movesWhere);
			args.weightQuickturn = ArgNumber(moves, "quickturn", context.input, args.weightQuickturn, movesWhere);
			args.weightCUp = ArgNumber(moves, "cUp", context.input, args.weightCUp, movesWhere);
			args.weightWait = ArgNumber(moves, "wait", context.input, args.weightWait, movesWhere);
			args.weightStrainBack = ArgNumber(moves, "strainBack", context.input, args.weightStrainBack, movesWhere);
		}
		if (args.probeFrames < 1 || args.probeWindow < 0)
			ConfigError("\"probeFrames\" in " + where + " must be at least 1 and \"probeWindow\" at least 0");
		try
		{
			BitfsScEndingMetrics::RoughTargetAngle(args.oscQuadrant, args.targetQuadrant);
		}
		catch (const std::runtime_error&)
		{
			ConfigError("\"targetQuadrant\" in " + where + " must name a corner adjacent to \"oscQuadrant\"");
		}

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<BitfsScEndingSolution>(context.input);
		auto solutions = Scattershot_BitfsScEnding::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureMetricScript(args)
			.Visualize(context.stage.visualize)
			.Run<Scattershot_BitfsScEnding>(args);
		return ToSet(context, solutions);
	}

	// --- pyramid-osc-approach: the original single-threaded experiment ---------------------------

	class PyramidOscApproach : public TopLevelScript<Sm64Resource>
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
		auto status = TopLevelScriptBuilder<PyramidOscApproach>::Build(m64).ImportResource(context.resources[0]).Run(args);
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

	class AreFixerRoot : public TopLevelScript<Sm64Resource>
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
		auto status = TopLevelScriptBuilder<AreFixerRoot>::Build(m64).ImportResource(context.resources[0]).Run(context.stage.startFrame, args);
		const ScriptStatus<BitFsAreFixer>& fixer = status.fixer;
		std::printf("are-fixer: %s in %d landing and %d turn rounds; hand-over frame %lld at (%.9g, %.9g), normal (%.9g, %.9g, %.9g), "
			"ARE (%.0f, %.0f), steps (%d, %d), %d frames after the rest at frame %lld, (%.9g, %.9g), normal (%.9g, %.9g), x error %.0f; "
			"way: %d run frames, dive yaw %d, air %s, %d slide frames, of %d ways, walk %d frame(s) at %d; "
			"%llu frame advances, %llu saves, %llu loads\n",
			fixer.solved ? "solved" : "not solved", fixer.rounds, fixer.turnRounds, (long long)fixer.handoverFrame, fixer.handoverPos[0], fixer.handoverPos[2],
			fixer.normal[0], fixer.normal[1], fixer.normal[2], fixer.adjustedRemainderError[0], fixer.adjustedRemainderError[2],
			fixer.incrementFrames[0], fixer.incrementFrames[2], fixer.framesAfterRest, (long long)fixer.restFrame, fixer.restPos[0], fixer.restPos[2],
			fixer.restNormal[0], fixer.restNormal[2], fixer.restErrorX, fixer.runFrames, int(fixer.diveYaw),
			fixer.diveAir == 0 ? "back" : fixer.diveAir == 1 ? "neutral" : "at the yaw", fixer.slideFrames, fixer.ways, fixer.walkFrames, int(fixer.walkYaw),
			(unsigned long long)fixer.nFrameAdvances, (unsigned long long)fixer.nSaves, (unsigned long long)fixer.nLoads);
		if (!fixer.validated)
			std::printf("are-fixer: not run: %s; the least errors the target admits that step reversibly over the range: %d on x, %d on z (-1: none within %d ULPs)\n",
				fixer.refusal != nullptr ? fixer.refusal : "validation refused", fixer.leastError[0], fixer.leastError[2], BitFsAreFixer::MaxLeastError);

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
		{ "are-fixer", "BitFsAreFixer: from a dive onto the platform, a dive recover whose rest sets the x axis of the pyramid's normal to the target's ARE, then a run and turn that set z, handing Mario over running", RunAreFixer },
		{ "dr-oscillations", "Scattershot_BitfsDr once per target oscillation, filtering between oscillations", RunDrOscillations },
		{ "sc-ending", "Scattershot_BitfsScEnding: the ending from the oscillations' last crossing: the final run, the approach (dive recover or brake), the leave (walk off, parallel, quickturn, C-up, wait) and the squish test", RunScEnding },
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
	TopLevelScriptBuilder<ExportSolutions>::Build(m64).ImportResource(&context.game).Run(set.startFrame, diffs, directory);
	std::printf("exported %llu movie(s) to %s\n", (unsigned long long)diffs.size(), directory.string().c_str());
}

ImportedCorner ImportCorner(LibSm64& game, const PipelineConfig& pipeline, const StageConfig& stage, std::shared_ptr<const BitfsCornerLevel> level)
{
	Configuration config = pipeline.ScattershotConfiguration(stage);
	M64 m64 = LoadMovie(config);
	auto status = TopLevelScriptBuilder<CornerImport>::Build(m64).ImportResource(&game).Run(stage.ImportFrame(), stage.objects, std::move(level));
	if (!status.asserted)
		throw std::runtime_error("stage \"" + stage.name + "\": the corner could not be imported from the game at frame " + std::to_string(stage.ImportFrame()));
	return ImportedCorner { status.level, *status.state };
}

size_t VerifyOnGame(const StageContext& context, SolutionSet& set)
{
	if (set.solutions.empty())
		return 0;
	std::vector<M64Diff> diffs;
	diffs.reserve(set.solutions.size());
	for (const SolutionRecord& record : set.solutions)
		diffs.push_back(record.m64Diff);

	Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
	M64 m64 = LoadMovie(config);
	auto onGame = TopLevelScriptBuilder<ReplayProbe>::Build(m64).ImportResource(&context.game).Run(set.startFrame, diffs);
	auto onCorner = TopLevelScriptBuilder<ReplayProbe>::Build(m64).ImportResource(context.resources[0]).Run(set.startFrame, diffs);
	if (!onGame.asserted || !onCorner.asserted)
		throw std::runtime_error("stage \"" + context.stage.name + "\": the replay check read " + std::to_string(onGame.probes.size()) + " solution(s) on the game and "
			+ std::to_string(onCorner.probes.size()) + " on the corner, of " + std::to_string(diffs.size()));

	size_t dropped = 0;
	std::vector<SolutionRecord> kept;
	kept.reserve(set.solutions.size());
	for (size_t i = 0; i < diffs.size(); i++)
	{
		const ReplayProbe::Probe& g = onGame.probes[i];
		const ReplayProbe::Probe& c = onCorner.probes[i];
		if (g == c)
		{
			kept.push_back(std::move(set.solutions[i]));
			continue;
		}
		dropped++;
		std::printf("    solution %zu differs on the game at frame %lld: the corner has Mario at (%g, %g, %g), speed %g, action 0x%X, facing %d, the normal (%g, %g, %g); "
					"the game has (%g, %g, %g), speed %g, action 0x%X, facing %d, (%g, %g, %g)\n",
			i, (long long)g.frame, c.pos[0], c.pos[1], c.pos[2], c.forwardVel, c.action, c.faceYaw, c.normal[0], c.normal[1], c.normal[2],
			g.pos[0], g.pos[1], g.pos[2], g.forwardVel, g.action, g.faceYaw, g.normal[0], g.normal[1], g.normal[2]);
	}
	set.solutions = std::move(kept);
	return dropped;
}
