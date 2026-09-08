// bitfs-turn: runs the BitFS squish-cancel pipeline described by config.json, all stages in
// order or a single one (ROADMAP 1.4). See README.md ("Configuration") for the file format
// and Stages.cpp for what each stage type does.
//
//   bitfs-turn [--config <file>] [--stage <name>] [--list] [--dry-run]
//
// Without --config the file is config.json next to the executable. Every stage writes its
// solutions to <outputDirectory>/solutions/<stage>.json; a stage run alone reads its input
// from the file its input stage wrote last time.
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <BitFsObjects.hpp>
#include <LibSm64.hpp>
#include <tasfw/Script.hpp>

#include "PipelineConfig.hpp"
#include "SelfPath.hpp"
#include "SolutionSet.hpp"
#include "StageArgs.hpp"
#include "Stages.hpp"

namespace fs = std::filesystem;

namespace
{
	struct Options
	{
		fs::path config;
		std::string stage;
		bool list = false;
		bool dryRun = false;
		bool help = false;
	};

	void PrintUsage()
	{
		std::printf(
			"usage: bitfs-turn [--config <file>] [--stage <name>] [--list] [--dry-run]\n"
			"  --config <file>  pipeline config (default: config.json next to the executable)\n"
			"  --stage <name>   run only this stage, reading its input from the solutions file\n"
			"                   its input stage wrote earlier\n"
			"  --list           print the stage types and the configured stages, then exit\n"
			"  --dry-run        resolve paths, check the files exist, load one DLL and print its\n"
			"                   layout report, then exit without searching\n");
	}

	bool ParseOptions(int argc, char** argv, Options& options)
	{
		for (int i = 1; i < argc; i++)
		{
			std::string arg = argv[i];
			auto value = [&]() -> const char*
			{
				if (i + 1 >= argc)
					return nullptr;
				return argv[++i];
			};

			if (arg == "--config")
			{
				const char* v = value();
				if (!v)
					return false;
				options.config = v;
			}
			else if (arg == "--stage")
			{
				const char* v = value();
				if (!v)
					return false;
				options.stage = v;
			}
			else if (arg == "--list")
				options.list = true;
			else if (arg == "--dry-run")
				options.dryRun = true;
			else if (arg == "--help" || arg == "-h")
				options.help = true;
			else
				return false;
		}
		return true;
	}

	void CheckInputsExist(const PipelineConfig& pipeline)
	{
		if (!fs::exists(pipeline.m64))
			ConfigError("movie not found: " + pipeline.m64.string());
		for (const StageConfig& stage : pipeline.stages)
		{
			if (stage.m64 && !fs::exists(*stage.m64))
				ConfigError("movie not found: " + stage.m64->string() + " (stage \"" + stage.name + "\")");
			if (!FindStageType(stage.type))
				ConfigError("stage \"" + stage.name + "\": unknown type \"" + stage.type + "\" (see --list)");
		}
		for (const fs::path& dll : pipeline.DllPaths())
		{
			if (!fs::exists(dll))
				ConfigError("DLL not found: " + dll.string() + " (one copy per thread; docs/libsm64.md)");
		}
	}

	struct ResourceCounters
	{
		unsigned long long frameAdvances = 0;
		unsigned long long saves = 0;
		unsigned long long loads = 0;

		ResourceCounters operator-(const ResourceCounters& other) const
		{
			return { frameAdvances - other.frameAdvances, saves - other.saves, loads - other.loads };
		}
	};

	ResourceCounters CountResourceWork(const std::vector<LibSm64>& resources)
	{
		ResourceCounters total;
		for (const LibSm64& resource : resources)
		{
			total.frameAdvances += resource.nFrameAdvances;
			total.saves += resource.nSaveStates;
			total.loads += resource.nLoadStates;
		}
		return total;
	}

	// --dry-run: play the movie to a stage's start frame and take the in-level layout report
	// there. The power-on report cannot check anything that needs Mario or the level's objects
	// (the slots the scripts hardcode among them), and a TopLevelScript that writes no inputs
	// is rewound when it returns, so the report has to be taken inside execution().
	class DryRunPlay : public TopLevelScript<LibSm64>
	{
	public:
		DryRunPlay(int64_t frame, std::vector<std::string>& report) : _frame(frame), _report(report) {}
		bool validation() override { return true; }
		bool execution() override
		{
			LongLoad(_frame);
			_report = resource->layoutCheckReport();
			return true;
		}
		bool assertion() override { return true; }

	private:
		int64_t _frame;
		std::vector<std::string>& _report;
	};

	std::vector<LibSm64> BuildResources(const PipelineConfig& pipeline, int count)
	{
		std::vector<fs::path> paths = pipeline.DllPaths();
		std::vector<LibSm64> resources;
		resources.reserve(size_t(count));
		for (int i = 0; i < count; i++)
		{
			LibSm64Config config;
			config.dllPath = paths[size_t(i)];
			config.lightweight = pipeline.lightweight;
			config.countryCode = CountryCode::SUPER_MARIO_64_J;
			config.expectedObjects = BitFsExpectedObjects; // the slots the scripts hardcode, verified per thread
			resources.emplace_back(config);
			resources.back().useCostModel = pipeline.costModel;
		}
		return resources;
	}

	void ListStages(const PipelineConfig& pipeline)
	{
		std::printf("stage types:\n");
		for (const StageType& type : StageTypes())
			std::printf("  %-22s %s\n", type.name, type.description);

		std::printf("\nconfigured stages:\n");
		for (const StageConfig& stage : pipeline.stages)
		{
			std::printf("  %-22s %-22s frame %lld%s%s%s\n", stage.name.c_str(), stage.type.c_str(), (long long)stage.startFrame,
				stage.input ? ("  input " + *stage.input).c_str() : "",
				stage.exportM64 ? "  export" : "",
				stage.m64 ? ("  m64 " + stage.m64->filename().string()).c_str() : "");
		}
	}

	int Run(const Options& options)
	{
		PipelineConfig pipeline = PipelineConfig::Load(options.config);
		if (options.list)
		{
			ListStages(pipeline);
			return 0;
		}

		CheckInputsExist(pipeline);

		if (options.dryRun)
		{
			std::printf("config:  %s\nmovie:   %s\ndlls:    %s (%d threads)\noutput:  %s\n",
				options.config.string().c_str(), pipeline.m64.string().c_str(),
				(pipeline.dllDirectory / pipeline.dllPattern).string().c_str(), pipeline.threads,
				pipeline.outputDirectory.string().c_str());
			ListStages(pipeline);

			std::vector<LibSm64> one = BuildResources(pipeline, 1);
			std::printf("\nlayout checks at power-on (%s):\n", pipeline.DllPaths()[0].filename().string().c_str());
			for (const std::string& line : one[0].layoutCheckReport())
				std::printf("  %s\n", line.c_str());

			// The in-level checks, at the first stage's start frame of its movie.
			if (!pipeline.stages.empty() && pipeline.stages.front().startFrame >= 0)
			{
				const StageConfig& stage = pipeline.stages.front();
				M64 m64(stage.m64.value_or(pipeline.m64));
				if (m64.load() != 1)
				{
					std::fprintf(stderr, "error: could not load movie %s\n", stage.m64.value_or(pipeline.m64).string().c_str());
					return 1;
				}
				std::vector<std::string> report;
				TopLevelScriptBuilder<DryRunPlay>::Build(m64).ImportResource(&one[0]).Run(stage.startFrame, report);
				std::printf("\nlayout checks at frame %lld (stage \"%s\"):\n", (long long)stage.startFrame, stage.name.c_str());
				int failures = 0;
				for (const std::string& line : report)
				{
					std::printf("  %s\n", line.c_str());
					if (line.rfind("FAIL: ", 0) == 0)
						failures++;
				}
				if (failures > 0)
				{
					std::fprintf(stderr, "error: %d layout check(s) failed; a run would stop at start-up with the same message\n", failures);
					return 1;
				}
			}
			return 0;
		}

		// Scattershot writes CSVs and the error.m64 dump straight into the output directory from
		// inside its OpenMP region, where a failed write cannot be reported; make sure it exists.
		fs::create_directories(pipeline.outputDirectory);
		fs::create_directories(pipeline.outputDirectory / "solutions");

		std::vector<LibSm64> resources = BuildResources(pipeline, pipeline.threads);
		std::map<std::string, SolutionSet> produced;

		auto runOne = [&](const StageConfig& stage) -> size_t
		{
			SolutionSet loaded;
			const SolutionSet* input = nullptr;
			if (stage.input)
			{
				auto inMemory = produced.find(*stage.input);
				if (inMemory != produced.end())
					input = &inMemory->second;
				else
				{
					fs::path file = pipeline.SolutionsFile(*stage.input);
					if (!fs::exists(file))
						ConfigError("stage \"" + stage.name + "\" needs the solutions of \"" + *stage.input + "\" but " + file.string() + " does not exist; run that stage first");
					loaded = SolutionSet::Load(file);
					input = &loaded;
					std::printf("input: %llu solution(s) from %s\n", (unsigned long long)loaded.solutions.size(), file.string().c_str());
				}
			}

			std::printf("\n=== stage %s (%s, frame %lld) ===\n", stage.name.c_str(), stage.type.c_str(), (long long)stage.startFrame);
			auto start = std::chrono::steady_clock::now();
			ResourceCounters before = CountResourceWork(resources);

			StageContext context { pipeline, stage, resources, input };
			SolutionSet output = RunStage(context);
			output.Select(stage.select, "stages[" + stage.name + "].select");
			output.Save(pipeline.SolutionsFile(stage.name));
			if (stage.exportM64)
				ExportSolutionSet(context, output);

			double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			ResourceCounters work = CountResourceWork(resources) - before;
			std::printf("=== stage %s: %llu solution(s) in %.1f s, written to %s ===\n", stage.name.c_str(),
				(unsigned long long)output.solutions.size(), seconds, pipeline.SolutionsFile(stage.name).string().c_str());
			// The fixed-workload numbers hard rule 8 asks for (AGENTS.md), summed over threads.
			std::printf("    frame advances %llu, saves %llu, loads %llu\n", work.frameAdvances, work.saves, work.loads);

			size_t count = output.solutions.size();
			produced[stage.name] = std::move(output);
			return count;
		};

		if (!options.stage.empty())
		{
			const StageConfig* stage = pipeline.FindStage(options.stage);
			if (!stage)
				ConfigError("no stage named \"" + options.stage + "\" (see --list)");
			return runOne(*stage) > 0 ? 0 : 1;
		}

		for (const StageConfig& stage : pipeline.stages)
		{
			if (runOne(stage) == 0)
			{
				std::printf("stage %s produced no solutions; stopping\n", stage.name.c_str());
				return 1;
			}
		}
		return 0;
	}
}

int main(int argc, char** argv)
{
	Options options;
	if (!ParseOptions(argc, argv, options))
	{
		PrintUsage();
		return 2;
	}
	if (options.help)
	{
		PrintUsage();
		return 0;
	}
	if (options.config.empty())
		options.config = getPathToSelf().parent_path() / "config.json";

	try
	{
		return Run(options);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "error: %s\n", e.what());
		return 2;
	}
}
