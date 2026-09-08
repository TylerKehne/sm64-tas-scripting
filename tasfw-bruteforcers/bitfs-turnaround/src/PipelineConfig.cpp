#include "PipelineConfig.hpp"
#include "StageArgs.hpp"

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
		stream >> root;
	}
	catch (const json::exception& e)
	{
		ConfigError(file.string() + ": " + e.what());
	}
	return Parse(root, fs::absolute(file).parent_path());
}

PipelineConfig PipelineConfig::Parse(const json& root, const fs::path& configDirectory)
{
	RejectUnknownKeys(root, { "baseDirectory", "resources", "m64", "outputDirectory", "scattershot", "stages" }, "the top level");

	PipelineConfig pipeline;
	pipeline.baseDirectory = root.contains("baseDirectory")
		? fs::path(Require<std::string>(root, "baseDirectory", "the top level"))
		: configDirectory;

	const json& resources = RequireObject(root, "resources", "the top level");
	RejectUnknownKeys(resources, { "dllDirectory", "dllPattern", "threads", "lightweight" }, "resources");
	pipeline.dllDirectory = pipeline.Resolve(Require<std::string>(resources, "dllDirectory", "resources"));
	pipeline.dllPattern = Optional<std::string>(resources, "dllPattern", "sm64_jp_{}.dll", "resources");
	pipeline.threads = Optional<int>(resources, "threads", 1, "resources");
	pipeline.lightweight = Optional<bool>(resources, "lightweight", true, "resources");
	if (pipeline.threads < 1)
		ConfigError("\"threads\" in resources must be at least 1");
	if (pipeline.threads > 1 && pipeline.dllPattern.find("{}") == std::string::npos)
		ConfigError("\"dllPattern\" in resources needs a {} placeholder when threads > 1 (one DLL copy per thread)");

	pipeline.m64 = pipeline.Resolve(Require<std::string>(root, "m64", "the top level"));
	pipeline.outputDirectory = pipeline.Resolve(Optional<std::string>(root, "outputDirectory", ".", "the top level"));

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
		RejectUnknownKeys(item, { "name", "type", "startFrame", "m64", "input", "scattershot", "args", "select", "export" }, where);

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

		pipeline.stages.push_back(std::move(stage));
	}
	return pipeline;
}

fs::path PipelineConfig::Resolve(const fs::path& path) const
{
	return (path.is_absolute() ? path : baseDirectory / path).lexically_normal();
}

std::vector<fs::path> PipelineConfig::DllPaths() const
{
	std::vector<fs::path> paths;
	paths.reserve(size_t(threads));
	for (int i = 0; i < threads; i++)
	{
		std::string name = dllPattern;
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
