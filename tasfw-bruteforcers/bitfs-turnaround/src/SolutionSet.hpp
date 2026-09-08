#pragma once
// What a stage produces: one input diff per solution plus named numbers describing it. A
// set is saved as <outputDirectory>/solutions/<stage>.json, so a later stage can be run on
// its own (`bitfs-turn --stage <name>`) against an earlier run's result, and so the numbers a
// later stage needs from an earlier one ("input:<metric>" arguments) travel with the file.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <tasfw/Inputs.hpp>

struct SolutionRecord
{
	M64Diff m64Diff;
	std::map<std::string, double> metrics;
};

struct SolutionSet
{
	std::string stage;
	std::string type;
	int64_t startFrame = -1;
	std::vector<SolutionRecord> solutions;

	// {"stage", "type", "startFrame", "solutions": [{"frames": [[frame, buttons, x, y], ...],
	// "metrics": {name: number}}]}. Infinite and NaN metrics are written as null and read back
	// as +infinity (JSON has no representation for them).
	nlohmann::json ToJson() const;
	static SolutionSet FromJson(const nlohmann::json& json);
	void Save(const std::filesystem::path& file) const;
	static SolutionSet Load(const std::filesystem::path& file);

	// The first solution's metric, if there is a first solution and it has that metric.
	std::optional<double> Metric(const std::string& name) const;

	// {"sortBy": ["-fSpd", "pyraNormX"], "take": 10}: stable sort by each key in order, later
	// keys breaking ties ("-" prefix for descending), then keep the first `take`. A metric
	// missing from any solution is an error.
	void Select(const nlohmann::json& select, const std::string& where);
};
