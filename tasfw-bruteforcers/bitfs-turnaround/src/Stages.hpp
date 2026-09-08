#pragma once
// The stage types bitfs-turn can run. A stage takes the pipeline config, its own stage
// entry, the per-thread resources and (optionally) the solutions of an earlier stage, and
// returns its solutions. Stages.cpp holds the implementations and the argument schemas.
#include <string>
#include <vector>

#include <LibSm64.hpp>

#include "PipelineConfig.hpp"
#include "SolutionSet.hpp"

struct StageContext
{
	const PipelineConfig& pipeline;
	const StageConfig& stage;
	std::vector<LibSm64>& resources;
	const SolutionSet* input; // nullptr when the stage has no input
};

struct StageType
{
	const char* name;
	const char* description;
	SolutionSet (*run)(StageContext&);
};

const std::vector<StageType>& StageTypes();
const StageType* FindStageType(const std::string& name);

// Runs the stage named by context.stage.type; throws on an unknown type.
SolutionSet RunStage(StageContext& context);

// Replays every solution on the first resource and writes
// <outputDirectory>/m64/<stage>/<index>_<normal>_<speed>.m64.
void ExportSolutionSet(const StageContext& context, const SolutionSet& set);
