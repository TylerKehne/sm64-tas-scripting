#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/SharedLib.hpp>
#include <omp.h>
#include <vector>
#include <filesystem>
#include <map>
#include <unordered_set>

#ifndef SCATTERSHOT_THREAD_H
#define SCATTERSHOT_THREAD_H

enum class MovementOptions
{
    // Joystick mag
    MAX_MAGNITUDE,
    ZERO_MAGNITUDE,
    SAME_MAGNITUDE,
    RANDOM_MAGNITUDE,

    // Input angle
    MATCH_FACING_YAW,
    ANTI_FACING_YAW,
    SAME_YAW,
    RANDOM_YAW,

    // Buttons
    SAME_BUTTONS,
    NO_BUTTONS,
    RANDOM_BUTTONS,

    // Scripts
    NO_SCRIPT,
    PBD,
    RUN_DOWNHILL,
    REWIND,
    TURN_UPHILL
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot;

template <class TState>
class StateBin;

class Configuration;

class Segment
{
public:
    Segment* parent;
    uint64_t seed;
    uint32_t nReferences;
    uint8_t nScripts;
    uint8_t depth;
};

template <class TState>
class Block
{
public:
    float fitness;
    Segment* tailSegment;
    StateBin<TState> stateBin;
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class ScattershotThread : public TopLevelScript<TResource>
{
protected:
    friend class Scattershot<TState, TResource>;

    // Using directives needed for MSVC >:(
    using Script<TResource>::LongLoad;
    using Script<TResource>::ExecuteAdhoc;
    using Script<TResource>::ModifyAdhoc;
    using TopLevelScript<TResource>::MainConfig;

    Scattershot<TState, TResource>& scattershot;
    const Configuration& config;

    ScattershotThread(Scattershot<TState, TResource>& scattershot, int id);

    virtual bool validation();
    bool execution();
    virtual bool assertion();

    MovementOptions ChooseMovementOption(uint64_t rngHash, std::map<MovementOptions, double> weightedOptions);

    virtual std::unordered_set<MovementOptions> GetMovementOptions(uint64_t rngHash) = 0;
    virtual bool ApplyMovement(std::unordered_set<MovementOptions> movementOptions, uint64_t rngHash) = 0;
    virtual StateBin<TState> GetStateBin() = 0;
    virtual bool ValidateBlock() = 0;
    virtual float GetStateFitness() = 0;

    

private:
    Block<TState>* Blocks;
    int* HashTable;
    int Id;
    uint64_t RngHash;
    Block<TState> BaseBlock;

    short startCourse;
    short startArea;

    // Thread state methods
    uint64_t UpdateRngHash();
    void InitializeMemory(StateBin<TState> initTruncPos);
    bool SelectBaseBlock(int mainIteration);
    bool ValidateBaseBlock(const StateBin<TState>& baseBlockStateBin) const;
    void ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin, float newFitness);

    template <typename F>
    void SingleThread(F func);

    bool ValidateCourseAndArea();
    uint64_t ChooseScriptAndApply(uint64_t rngHash);
    StateBin<TState> GetStateBinSafe();
    float GetStateFitnessSafe();
    AdhocBaseScriptStatus DecodeBaseBlockDiffAndApply();
    AdhocBaseScriptStatus ExecuteFromBaseBlockAndEncode();
    
    static Scattershot<TState, TResource> CreateScattershot(const Configuration& configuration)
    {
        return Scattershot<TState, TResource>(configuration);
    }
};

//Include template method implementations
#include "ScattershotThread.t.hpp"

#endif