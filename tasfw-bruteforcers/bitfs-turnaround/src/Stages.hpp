#pragma once
// The stage types bitfs-turn can run. A stage takes the pipeline config, its own stage
// entry, the per-thread resources and (optionally) the solutions of an earlier stage, and
// returns its solutions. Stages.cpp holds the implementations and the argument schemas.
#include <memory>
#include <string>
#include <vector>

#include <BitfsCorner.hpp>
#include <LibSm64.hpp>
#include <Sm64Resource.hpp>

#include "PipelineConfig.hpp"
#include "SolutionSet.hpp"

struct StageContext
{
	const PipelineConfig& pipeline;
	const StageConfig& stage;
	std::vector<Sm64Resource*>& resources; // the stage's, one per thread: the game's DLL copies, or corners imported from the game (stage.resource)
	LibSm64& game;                         // the game itself: a corner stage's solutions are replayed on it before they count, and the movies are exported from it
	const SolutionSet* input;              // nullptr when the stage has no input
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

// Replays every solution on the game and writes
// <outputDirectory>/m64/<stage>/<index>_<normal>_<speed>.m64.
void ExportSolutionSet(const StageContext& context, const SolutionSet& set);

// What a stage on the corner starts from: the corner's level, read from the game once per
// run (pass the pointer back in for the next stage), and a state with the stage's objects,
// both exported by a script that plays the game to the stage's start frame. Every thread's
// corner loads a copy of the state (main.cpp).
struct ImportedCorner
{
	std::shared_ptr<const BitfsCornerLevel> level;
	ImportedSave<Sm64Mem> state;
};
ImportedCorner ImportCorner(LibSm64& game, const PipelineConfig& pipeline, const StageConfig& stage, std::shared_ptr<const BitfsCornerLevel> level);

// A corner stage's solutions replayed on the game and on a corner, and what the stages
// read compared at the end of each: Mario's position, speed, action, facing and floor
// height, and the pyramid's normal. A solution that differs is dropped, with a line saying
// which and how. Returns the number dropped.
size_t VerifyOnGame(const StageContext& context, SolutionSet& set);
