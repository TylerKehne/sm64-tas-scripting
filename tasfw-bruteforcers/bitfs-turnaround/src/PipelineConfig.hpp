#pragma once
// The BitFS pipeline as declared in config.json: game resources, source movie, output
// location, scattershot defaults, and an ordered list of named stages. README.md
// ("Configuration") documents the schema. Relative paths resolve against the directory of
// the config file, unless the file carries "baseDirectory" (the build writes one into the
// copy it places next to the executable, so the committed file's relative paths keep
// working from the build tree).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <Scattershot.hpp>

struct StageConfig
{
	std::string name;
	std::string type;
	int64_t startFrame = -1;
	std::optional<std::filesystem::path> m64; // overrides the pipeline's movie for this stage
	std::optional<std::string> input;         // an earlier stage whose solutions feed this one
	nlohmann::json scattershot;               // overrides of the pipeline's scattershot defaults
	nlohmann::json args;                      // stage-type-specific; see Stages.cpp
	nlohmann::json select;                    // SolutionSet::Select, applied to the output
	bool exportM64 = false;                   // write one .m64 per solution
};

struct PipelineConfig
{
	std::filesystem::path baseDirectory;
	std::filesystem::path dllDirectory;
	std::string dllPattern = "sm64_jp_{}.dll";
	int threads = 1;
	bool lightweight = true;
	bool costModel = true; // Resource::useCostModel; false makes runs timing-independent (diagnosis)
	std::filesystem::path m64;
	std::filesystem::path outputDirectory;
	nlohmann::json scattershotDefaults;
	std::vector<StageConfig> stages;

	static PipelineConfig Load(const std::filesystem::path& file);
	static PipelineConfig Parse(const nlohmann::json& root, const std::filesystem::path& configDirectory);

	std::filesystem::path Resolve(const std::filesystem::path& path) const;
	std::vector<std::filesystem::path> DllPaths() const; // one per thread
	const StageConfig* FindStage(const std::string& name) const;
	std::filesystem::path SolutionsFile(const std::string& stageName) const;

	// Defaults, then the pipeline's overrides, then the stage's; threads, movie, output and
	// DLL paths from the pipeline; the start frame from the stage.
	Configuration ScattershotConfiguration(const StageConfig& stage) const;
};

// Every field set (Configuration itself has no default member initializers).
Configuration DefaultScattershotConfiguration();

// Applies the known keys of `overrides` (an object, or null for none); unknown keys are errors.
void ApplyScattershotOverrides(Configuration& config, const nlohmann::json& overrides, const std::string& where);
