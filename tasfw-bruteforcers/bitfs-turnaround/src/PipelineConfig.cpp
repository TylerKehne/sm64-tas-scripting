#include "PipelineConfig.hpp"
#include "StageArgs.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	template <class T>
	T Require(const json& object, const char* key, const std::string& where)
	{
		if (!object.contains(key))
			ConfigError(std::string("missing \"") + key + "\" in " + where);
		try
		{
			return object.at(key).get<T>();
		}
		catch (const json::exception& e)
		{
			ConfigError(std::string("\"") + key + "\" in " + where + ": " + e.what());
		}
	}

	template <class T>
	T Optional(const json& object, const char* key, T fallback, const std::string& where)
	{
		return object.contains(key) ? Require<T>(object, key, where) : fallback;
	}
}

Configuration DefaultScattershotConfiguration()
{
	Configuration config {};
	config.StartFrame = -1;
	config.PelletMaxScripts = 20;
	config.PelletMaxFrameDistance = 50;
	config.MaxBlocks = 1000000;
	config.TotalThreads = 1;
	config.MaxShots = 3000;
	config.PelletsPerShot = 200;
	config.ShotsPerUpdate = 10;
	config.StartFromRootEveryNShots = 100;
	config.MaxConsecutiveFailedPellets = 10;
	config.MaxSolutions = 100;
	config.Seed = 6;
	config.FitnessTieGoesToNewBlock = false;
	config.Deterministic = false;
	config.CsvSamplePeriod = 0;
	return config;
}

void ApplyScattershotOverrides(Configuration& config, const json& overrides, const std::string& where)
{
	if (overrides.is_null())
		return;
	RejectUnknownKeys(overrides,
		{ "pelletMaxScripts", "pelletMaxFrameDistance", "maxBlocks", "maxShots", "pelletsPerShot", "shotsPerUpdate",
			"startFromRootEveryNShots", "maxConsecutiveFailedPellets", "maxSolutions", "seed", "fitnessTieGoesToNewBlock",
			"deterministic", "csvSamplePeriod" },
		where);

	auto set = [&](const char* key, auto& field)
	{
		using Field = std::remove_reference_t<decltype(field)>;
		if (overrides.contains(key))
			field = Require<Field>(overrides, key, where);
	};
	set("pelletMaxScripts", config.PelletMaxScripts);
	set("pelletMaxFrameDistance", config.PelletMaxFrameDistance);
	set("maxBlocks", config.MaxBlocks);
	set("maxShots", config.MaxShots);
	set("pelletsPerShot", config.PelletsPerShot);
	set("shotsPerUpdate", config.ShotsPerUpdate);
	set("startFromRootEveryNShots", config.StartFromRootEveryNShots);
	set("maxConsecutiveFailedPellets", config.MaxConsecutiveFailedPellets);
	set("maxSolutions", config.MaxSolutions);
	set("seed", config.Seed);
	set("fitnessTieGoesToNewBlock", config.FitnessTieGoesToNewBlock);
	set("deterministic", config.Deterministic);
	set("csvSamplePeriod", config.CsvSamplePeriod);
}

PipelineConfig PipelineConfig::Load(const fs::path& file)
{
	std::ifstream stream(file);
	if (!stream)
		ConfigError("cannot read " + file.string());
	if (stream.peek() == std::ifstream::traits_type::eof())
		ConfigError(file.string() + " is empty");

	json root;
	try
	{
		// Comments (// and /* */) are allowed and ignored, so the committed config can say
		// what each parameter controls next to it.
		root = json::parse(stream, nullptr, true, true);
	}
	catch (const json::exception& e)
	{
		ConfigError(file.string() + ": " + e.what());
	}
	PipelineConfig pipeline = Parse(root, fs::absolute(file).parent_path());

	// The movie says which game the pipeline runs: {version} in dllPattern, and what the
	// resources are declared to be. A missing movie is reported later (CheckInputsExist).
	if (fs::exists(pipeline.m64))
	{
		M64 movie(pipeline.m64);
		if (movie.load())
			pipeline.countryCode = movie.metadata.countryCode;
	}
	return pipeline;
}

// A "visualize" block, the top level's defaults or a stage's own: the viewer's parameters
// (Visualization.hpp), checked key by key before the merged block becomes a Visualization.
static void CheckVisualizeKeys(const json& block, const std::string& where)
{
	RejectUnknownKeys(block,
		{ "title", "interpreter", "x", "y", "angle", "speed", "binX", "binY", "binAngle", "binSpeed", "filters", "categorical", "view" }, where);
	if (block.contains("categorical"))
	{
		const json& categorical = block.at("categorical");
		if (!categorical.is_array() || !std::all_of(categorical.begin(), categorical.end(), [](const json& name) { return name.is_string(); }))
			ConfigError("\"categorical\" in " + where + " must be an array of column names (the columns the viewer filters by value)");
	}
	if (block.contains("filters"))
	{
		if (!block.at("filters").is_array())
			ConfigError("\"filters\" in " + where + " must be an array of { column, min, max }");
		for (const json& filter : block.at("filters"))
		{
			if (!filter.is_object())
				ConfigError("\"filters\" in " + where + " must be an array of { column, min, max }");
			RejectUnknownKeys(filter, { "column", "min", "max" }, where + ".filters");
		}
	}
	if (block.contains("view"))
	{
		const json& view = RequireObject(block, "view", where);
		RejectUnknownKeys(view, { "x", "y", "width", "height" }, where + ".view");
		for (const char* key : { "x", "y", "width", "height" })
		{
			if (!view.contains(key) || !view.at(key).is_number())
				ConfigError(std::string("\"view\" in ") + where + " needs a number \"" + key + "\" (a width-by-height window centered on x, y)");
		}
		if (view.at("width").get<double>() <= 0 || view.at("height").get<double>() <= 0)
			ConfigError("\"view\" in " + where + " must have a positive width and height");
	}
}

PipelineConfig PipelineConfig::Parse(const json& root, const fs::path& configDirectory)
{
	RejectUnknownKeys(root, { "baseDirectory", "resources", "m64", "outputDirectory", "visualizer", "visualize", "scattershot", "stages", "start" }, "the top level");

	PipelineConfig pipeline;
	pipeline.baseDirectory = root.contains("baseDirectory")
		? fs::path(Require<std::string>(root, "baseDirectory", "the top level"))
		: configDirectory;

	const json& resources = RequireObject(root, "resources", "the top level");
	RejectUnknownKeys(resources, { "dllDirectory", "dllPattern", "threads", "saveMode", "costModel", "savestateBudgetMB" }, "resources");
	pipeline.dllDirectory = pipeline.Resolve(Require<std::string>(resources, "dllDirectory", "resources"));
	pipeline.dllPattern = Optional<std::string>(resources, "dllPattern", "sm64_{version}_{}.dll", "resources");
	pipeline.threads = Optional<int>(resources, "threads", 1, "resources");
	std::string saveMode = Optional<std::string>(resources, "saveMode", LibSm64::SaveModeName(LibSm64SaveMode::Dirty), "resources");
	if (!LibSm64::ParseSaveMode(saveMode, pipeline.saveMode))
		ConfigError("\"saveMode\" in resources must be \"full\", \"fixed\" or \"dirty\", not \"" + saveMode + "\"");
	pipeline.costModel = Optional<bool>(resources, "costModel", true, "resources");
	pipeline.savestateBudgetMB = Optional<int64_t>(resources, "savestateBudgetMB", 8192, "resources");
	if (pipeline.savestateBudgetMB < 1)
		ConfigError("\"savestateBudgetMB\" in resources must be at least 1");
	if (pipeline.threads < 1)
		ConfigError("\"threads\" in resources must be at least 1");
	if (pipeline.threads > 1 && pipeline.dllPattern.find("{}") == std::string::npos)
		ConfigError("\"dllPattern\" in resources needs a {} placeholder when threads > 1 (one DLL copy per thread)");

	pipeline.m64 = pipeline.Resolve(Require<std::string>(root, "m64", "the top level"));
	pipeline.outputDirectory = pipeline.Resolve(Optional<std::string>(root, "outputDirectory", ".", "the top level"));
	if (root.contains("visualizer"))
		pipeline.visualizer = pipeline.Resolve(Require<std::string>(root, "visualizer", "the top level"));
	pipeline.visualizeDefaults = root.value("visualize", json::object());
	if (root.contains("visualize"))
		CheckVisualizeKeys(pipeline.visualizeDefaults, "visualize");

	pipeline.scattershotDefaults = root.value("scattershot", json());
	{
		Configuration probe = DefaultScattershotConfiguration();
		ApplyScattershotOverrides(probe, pipeline.scattershotDefaults, "scattershot");
	}

	if (!root.contains("stages") || !root.at("stages").is_array() || root.at("stages").empty())
		ConfigError("\"stages\" must be a non-empty array");

	std::set<std::string> names;
	for (const json& item : root.at("stages"))
	{
		std::string where = "stages[" + std::to_string(pipeline.stages.size()) + "]";
		RejectUnknownKeys(item, { "name", "type", "startFrame", "m64", "input", "scattershot", "args", "select", "export", "visualize", "resource", "objects", "lead" }, where);

		StageConfig stage;
		stage.name = Require<std::string>(item, "name", where);
		if (stage.name.empty() || stage.name.find_first_of("/\\:*?\"<>|") != std::string::npos)
			ConfigError("stage name \"" + stage.name + "\" is empty or not usable as a file name");
		if (!names.insert(stage.name).second)
			ConfigError("duplicate stage name \"" + stage.name + "\"");
		where = "stages[" + stage.name + "]";

		stage.type = Require<std::string>(item, "type", where);
		stage.startFrame = Require<int64_t>(item, "startFrame", where);
		if (item.contains("m64"))
			stage.m64 = pipeline.Resolve(Require<std::string>(item, "m64", where));
		if (item.contains("input"))
		{
			stage.input = Require<std::string>(item, "input", where);
			if (*stage.input == stage.name || !names.contains(*stage.input))
				ConfigError("\"input\" of stage \"" + stage.name + "\" must name an earlier stage");
		}
		stage.scattershot = item.value("scattershot", json());
		{
			Configuration probe = DefaultScattershotConfiguration();
			ApplyScattershotOverrides(probe, stage.scattershot, where + ".scattershot");
		}
		stage.args = item.value("args", json::object());
		if (!stage.args.is_object())
			ConfigError("\"args\" in " + where + " must be an object");
		stage.select = item.value("select", json());
		stage.exportM64 = Optional<bool>(item, "export", false, where);

		// The resource the stage runs on, and for the corner the objects it has.
		stage.resource = Optional<std::string>(item, "resource", "game", where);
		if (stage.resource != "game" && stage.resource != "corner")
			ConfigError("\"resource\" in " + where + " must be \"game\" or \"corner\"");
		if (stage.resource == "corner")
		{
			if (!item.contains("objects") || !item.at("objects").is_array())
				ConfigError("stage \"" + stage.name + "\" runs on the corner and must list its \"objects\": an array of \"pyramid\", \"otherPyramid\", \"trackPlatform\" and \"bully\" ([] for Mario alone)");
			for (const json& object : item.at("objects"))
			{
				std::string name = object.is_string() ? object.get<std::string>() : "";
				if (name == "pyramid")
					stage.objects.pyramid = true;
				else if (name == "otherPyramid")
					stage.objects.otherPyramid = true;
				else if (name == "trackPlatform")
					stage.objects.trackPlatform = true;
				else if (name == "bully")
					stage.objects.bully = true;
				else
					ConfigError("\"objects\" in " + where + ": \"" + name + "\" is not one of \"pyramid\", \"otherPyramid\", \"trackPlatform\", \"bully\"");
			}
			stage.lead = Optional<int>(item, "lead", 16, where);
			if (stage.lead < 0 || stage.lead > stage.startFrame)
				ConfigError("\"lead\" in " + where + " must be between 0 and the stage's startFrame");
		}
		else if (item.contains("objects") || item.contains("lead"))
			ConfigError("\"objects\" and \"lead\" in " + where + " apply to a stage with \"resource\": \"corner\"");

		// The run's viewer (ROADMAP 4.4): the plot's columns, bins, filters and view; the top
		// level's "visualize" supplies defaults the stage's block overrides key by key, the script
		// is the pipeline's, the title the stage's name unless given. Only a stage with a
		// csvSamplePeriod shows anything.
		if (item.contains("visualize"))
		{
			const json& visualize = RequireObject(item, "visualize", where);
			std::string visualizeWhere = where + ".visualize";
			CheckVisualizeKeys(visualize, visualizeWhere);
			if (!pipeline.visualizer)
				ConfigError("stage \"" + stage.name + "\" has \"visualize\" but the top level has no \"visualizer\" (the viewer's path, analysis/visualizer.py)");

			json merged = pipeline.visualizeDefaults;
			merged.update(visualize);
			Visualization visualization;
			try
			{
				visualization = merged.get<Visualization>();
			}
			catch (const json::exception& e)
			{
				ConfigError(visualizeWhere + ": " + e.what());
			}
			visualization.script = pipeline.visualizer->string();
			if (visualization.title.empty())
				visualization.title = stage.name;
			stage.visualize = std::move(visualization);
		}

		pipeline.stages.push_back(std::move(stage));
	}

	if (root.contains("start"))
		pipeline.SetStart(Require<std::string>(root, "start", "the top level"));
	return pipeline;
}

fs::path PipelineConfig::Resolve(const fs::path& path) const
{
	return (path.is_absolute() ? path : baseDirectory / path).lexically_normal();
}

std::string PipelineConfig::ResolvedDllPattern() const
{
	std::string pattern = dllPattern;
	size_t version = pattern.find("{version}");
	if (version != std::string::npos)
		pattern.replace(version, 9, LibSm64::VersionName(countryCode));
	return pattern;
}

std::vector<fs::path> PipelineConfig::DllPaths() const
{
	std::vector<fs::path> paths;
	paths.reserve(size_t(threads));
	std::string pattern = ResolvedDllPattern();
	for (int i = 0; i < threads; i++)
	{
		std::string name = pattern;
		size_t placeholder = name.find("{}");
		if (placeholder != std::string::npos)
			name.replace(placeholder, 2, std::to_string(i));
		paths.push_back(dllDirectory / name);
	}
	return paths;
}

const StageConfig* PipelineConfig::FindStage(const std::string& name) const
{
	for (const StageConfig& stage : stages)
	{
		if (stage.name == name)
			return &stage;
	}
	return nullptr;
}

fs::path PipelineConfig::SolutionsFile(const std::string& stageName) const
{
	return outputDirectory / "solutions" / (stageName + ".json");
}

fs::path PipelineConfig::PassFile(const std::string& stageName, int pass) const
{
	return outputDirectory / "solutions" / (stageName + ".oscillation" + std::to_string(pass) + ".json");
}

void PipelineConfig::SetStart(const std::string& spec)
{
	std::string name = spec;
	int pass = 0;
	size_t colon = spec.find(':');
	if (colon != std::string::npos)
	{
		name = spec.substr(0, colon);
		std::string digits = spec.substr(colon + 1);
		if (digits.empty() || digits.size() > 6 || digits.find_first_not_of("0123456789") != std::string::npos)
			ConfigError("\"start\" \"" + spec + "\" must be a stage name, or <stage>:<oscillation> with a whole number");
		pass = std::stoi(digits);
	}
	const StageConfig* stage = FindStage(name);
	if (!stage)
		ConfigError("\"start\" names no configured stage: \"" + name + "\" (see --list)");
	if (pass > 0 && stage->type != "dr-oscillations")
		ConfigError("\"start\" \"" + spec + "\": only a dr-oscillations stage has oscillation passes to start at");
	startStage = name;
	startPass = pass;
}

Configuration PipelineConfig::ScattershotConfiguration(const StageConfig& stage) const
{
	Configuration config = DefaultScattershotConfiguration();
	ApplyScattershotOverrides(config, scattershotDefaults, "scattershot");
	ApplyScattershotOverrides(config, stage.scattershot, "stages[" + stage.name + "].scattershot");
	config.StartFrame = int(stage.startFrame);
	config.TotalThreads = threads;
	config.M64Path = stage.m64 ? *stage.m64 : m64;
	config.CsvOutputDirectory = (outputDirectory / "").string(); // Scattershot appends the file name
	config.ResourcePaths = DllPaths();
	return config;
}
