#include "Stages.hpp"
#include "ExportSolutions.hpp"
#include "StageArgs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Types.hpp>

#include <BitFSPyramidOscillation.hpp>
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

	void PutVector(std::map<std::string, double>& metrics, const char* prefix, const std::vector<float>& values)
	{
		for (size_t i = 0; i < values.size(); i++)
			metrics[prefix + std::to_string(i)] = values[i];
	}

	void PutVector(std::map<std::string, double>& metrics, const char* prefix, const std::vector<int>& values)
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
			.ConfigureStateTracker(args)
			.Run<TiltTargetShot>(args);
		return ToSet(context, solutions);
	}

	// --- dr-oscillations: Scattershot_BitfsDr once per target oscillation ----------------------

	SolutionSet RunDrOscillations(StageContext& context)
	{
		const std::string where = ArgsWhere(context);
		const json& a = context.stage.args;
		RejectUnknownKeys(a,
			{ "equilibriumFrame", "quadrant", "minOscillationFrames", "targetNx", "targetNz", "maxOscillations", "firstShots",
				"shots", "lastShots", "lastMaxSolutions", "keepTop", "requireIncrementParity", "normalSpecs" },
			where);

		int64_t equilibriumFrame = int64_t(ArgNumber(a, "equilibriumFrame", context.input, where));
		int quadrant = int(ArgNumber(a, "quadrant", context.input, 4, where));
		int minOscillationFrames = int(ArgNumber(a, "minOscillationFrames", context.input, 15, where));
		float targetNx = float(ArgNumber(a, "targetNx", context.input, where));
		float targetNz = float(ArgNumber(a, "targetNz", context.input, where));
		int maxOscillations = int(ArgNumber(a, "maxOscillations", context.input, 5, where));
		long long firstShots = (long long)ArgNumber(a, "firstShots", context.input, 50000, where);
		long long shots = (long long)ArgNumber(a, "shots", context.input, 30000, where);
		long long lastShots = (long long)ArgNumber(a, "lastShots", context.input, 30000, where);
		int lastMaxSolutions = int(ArgNumber(a, "lastMaxSolutions", context.input, 1000000, where));
		size_t keepTop = size_t(ArgNumber(a, "keepTop", context.input, 10, where));
		bool requireIncrementParity = ArgBool(a, "requireIncrementParity", true, where);

		const json& specsJson = RequireObject(a, "normalSpecs", where);
		const std::string specsWhere = where + ".normalSpecs";
		RejectUnknownKeys(specsJson,
			{ "onlyMinMajor", "minXzSum", "minMajor", "maxMajor", "regionsMajor", "minMinor", "maxMinor", "regionsMinor" }, specsWhere);
		NormalSpecsDto specs;
		specs.onlyMinMajor = ArgBool(specsJson, "onlyMinMajor", true, specsWhere);
		specs.minXzSum = float(ArgNumber(specsJson, "minXzSum", context.input, specsWhere));
		specs.minMajor = float(ArgNumber(specsJson, "minMajor", context.input, specsWhere));
		specs.maxMajor = float(ArgNumber(specsJson, "maxMajor", context.input, specsWhere));
		specs.regionsMajor = float(ArgNumber(specsJson, "regionsMajor", context.input, specsWhere));
		specs.minMinor = float(ArgNumber(specsJson, "minMinor", context.input, specsWhere));
		specs.maxMinor = float(ArgNumber(specsJson, "maxMinor", context.input, specsWhere));
		specs.regionsMinor = float(ArgNumber(specsJson, "regionsMinor", context.input, specsWhere));

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		using Solution = ScattershotSolution<Scattershot_BitfsDr_Solution>;

		auto run = [&](int targetOscillation, const std::vector<Solution>& input)
		{
			return Scattershot_BitfsDr::ConfigureScattershot(config)
				.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
				.PipeFrom(input)
				.ConfigureStateTracker(equilibriumFrame, quadrant, specs, minOscillationFrames, targetNx, targetNz)
				.Run<Scattershot_BitfsDr>(targetOscillation, specs);
		};

		config.MaxShots = firstShots;
		auto initial = PipeIn<Scattershot_BitfsDr_Solution>(context.input);
		std::vector<Solution> solutions = run(0, initial);

		config.MaxShots = shots;
		for (int targetOscillation = 1; targetOscillation < maxOscillations; targetOscillation++)
		{
			if (targetOscillation == maxOscillations - 1)
			{
				config.MaxSolutions = lastMaxSolutions;
				config.MaxShots = lastShots;
			}

			solutions = run(targetOscillation, solutions);
			if (solutions.empty())
			{
				std::printf("dr-oscillations: no solutions at target oscillation %d\n", targetOscillation);
				return ToSet(context, solutions);
			}

			// After the first oscillation the search commits to the direction the best solution
			// took; the other direction needs one oscillation more.
			if (targetOscillation == 1)
			{
				if (solutions[0].data.roughTargetAngle != -24576)
				{
					std::erase_if(solutions, [](const Solution& s) { return s.data.roughTargetAngle == -24576; });
				}
				else
				{
					std::erase_if(solutions, [](const Solution& s) { return s.data.roughTargetAngle == 8192; });
					maxOscillations++;
				}
			}

			if (targetOscillation < maxOscillations - 1)
			{
				std::stable_sort(solutions.begin(), solutions.end(), [](const Solution& x, const Solution& y) { return x.data.fSpd > y.data.fSpd; });
				if (solutions.size() > keepTop)
					solutions.resize(keepTop);
			}
		}

		if (requireIncrementParity)
		{
			std::erase_if(solutions, [](const Solution& s)
				{ return std::abs(s.data.incrementFrames[0]) % 2 != std::abs(s.data.incrementFrames[2]) % 2; });
		}

		SolutionSet set = ToSet(context, solutions);
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
			.ConfigureStateTracker(args)
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
			.ConfigureStateTracker(initialFrame, oscQuadrant, targetQuadrant, minXzSum, targetNx, targetNz)
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
		StateTracker_BitfsDrRecover::Phase phase;
		if (phaseName == "attempt-dr")
			phase = StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR;
		else if (phaseName == "c-up-trick")
			phase = StateTracker_BitfsDrRecover::Phase::C_UP_TRICK;
		else
			ConfigError("\"phase\" in " + where + " must be \"attempt-dr\" or \"c-up-trick\"");

		Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
		auto input = PipeIn<Scattershot_BitfsDrRecover_Solution>(context.input);
		auto solutions = Scattershot_BitfsDrRecover::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
			.PipeFrom(input)
			.ConfigureStateTracker(initialFrame, oscQuadrant, targetQuadrant, minXzSum)
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

		explicit PyramidOscApproach(Args args) : _args(args) {}

		bool validation() override { return true; }

		bool execution() override
		{
			LongLoad(_args.startFrame);

			Camera* camera = *(Camera**)(resource->addr("gCamera"));
			MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
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

		SolutionSet set;
		set.stage = context.stage.name;
		set.type = context.stage.type;
		set.startFrame = context.stage.startFrame;
		if (status.asserted && !status.m64Diff.frames.empty())
			set.solutions.push_back(SolutionRecord { status.m64Diff, {} });
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
