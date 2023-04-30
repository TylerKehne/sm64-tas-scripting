#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/SharedLib.hpp>
//#include <Scattershot.hpp>
//#include <tasfw/Resource.hpp>
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
    PBDR,
    RUN_DOWNHILL,
    REWIND
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



    int DiffFrameCount()
    {
        int nFrames = 0;
        Segment* currentSegment = tailSegment;
        //if (tailSeg->depth == 0) { printf("tailSeg depth is 0!\n"); }
        while (currentSegment != 0)
        {
            //if (curSeg->depth == 0) { printf("curSeg depth is 0!\n"); }
            nFrames += currentSegment->nFrames;
            currentSegment = currentSegment->parent;
        }

        return nFrames;
    }
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class ThreadState
{
public:
    Block<TState>* Blocks;
    int* HashTable;
    int Id;
    uint64_t RngHash;
    Scattershot<TState, TResource>& scattershot;
    const Configuration& config;

    Block<TState> BaseBlock;
    StateBin<TState> BaseStateBin;

    /*
    double LoadTime = 0;
    double BlockTime = 0;
    double RunTime = 0;
    double LoopTimeStamp = 0;
    */

    ThreadState(Scattershot<TState, TResource>& scattershot, int id);
    uint64_t UpdateRngHash();
    void Initialize(StateBin<TState> initTruncPos);
    bool SelectBaseBlock(int mainIteration);
    bool ValidateBaseBlock(StateBin<TState> baseBlockStateBin);
    void ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin, float newFitness);
    //void PrintStatus(int mainIteration);
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class ScattershotThread : public TopLevelScript<TResource>
{
public:
    using Script<TResource>::LongLoad;
    using Script<TResource>::ExecuteAdhoc;
    using Script<TResource>::ModifyAdhoc;

    Scattershot<TState, TResource>& scattershot;
    const Configuration& config;
    ThreadState<TState, TResource> tState;

    ScattershotThread(Scattershot<TState, TResource>& scattershot, int id)
        : scattershot(scattershot), config(scattershot.config), tState(scattershot, id)
    {
        //config = scattershot.config;
        //tState = ThreadState(scattershot, id);
    }


    //template <class TState, derived_from_specialization_of<Resource> TResource>
    static Scattershot<TState, TResource> CreateScattershot(const Configuration& configuration)
    {
        return Scattershot<TState, TResource>(configuration);
    }

    MovementOptions ChooseMovementOption(uint64_t rngHash, std::map<MovementOptions, double> weightedOptions)
    {
        if (weightedOptions.empty())
            throw std::runtime_error("No movement options provided.");

        double maxRng = 10000.0;

        double totalWeight = 0;
        for (const auto& pair : weightedOptions)
            totalWeight += pair.second;

        double rng = rngHash % 10000;
        double rngRangeMin = 0;
        for (const auto& pair : weightedOptions)
        {
            double rngRangeMax = rngRangeMin + pair.second * maxRng / totalWeight;
            if (rng >= rngRangeMin && rng < rngRangeMax)
                return pair.first;

            rngRangeMin = rngRangeMax;
        }

        return weightedOptions.rbegin()->first;
    }

    bool validation() { return true; }

    bool assertion() { return true; }

    bool execution()
    {
        LongLoad(config.StartFrame);
        tState.Initialize(GetStateBinSafe());

        // Record start course/area for validation (generally scattershot has no cross-level value)
        startCourse = *(short*)this->resource->addr("gCurrCourseNum");
        startArea = *(short*)this->resource->addr("gCurrAreaIndex");

        for (int shot = 0; shot <= config.MaxShots; shot++)
        {
            // ALWAYS START WITH A MERGE SO THE SHARED BLOCKS ARE OK.
            if (shot % config.ShotsPerMerge == 0)
                SingleThread([&]() { scattershot.gState.MergeState(shot); });

            // Pick a block to "fire a scattershot" at
            if (!tState.SelectBaseBlock(shot))
                break;

            ExecuteAdhoc([&]()
                {
                    DecodeBaseBlockDiffAndApply();

                    if (tState.ValidateBaseBlock(GetStateBinSafe()))
                        return false;

                    for (int segment = 0; segment < config.SegmentsPerShot; segment++)
                        ExecuteFromBaseBlockAndEncode();

                    return true;
                });
        }

        return true;
    }

    virtual std::unordered_set<MovementOptions> GetMovementOptions(uint64_t rngHash) = 0;
    virtual bool ApplyMovement(std::unordered_set<MovementOptions> movementOptions, uint64_t rngHash) = 0;
    virtual StateBin<TState> GetStateBin() = 0;
    virtual bool ValidateBlock() = 0;
    virtual float GetStateFitness() = 0;

private:
    short startCourse;
    short startArea;

    template <typename F>
    void SingleThread(F func)
    {
        #pragma omp barrier
        {
            if (omp_get_thread_num() == 0)
                func();
        }
        #pragma omp barrier

        return;
    }

    bool ValidateCourseAndArea()
    {
        return startCourse == *(short*)this->resource->addr("gCurrCourseNum")
            && startArea == *(short*)this->resource->addr("gCurrAreaIndex");
    }

    uint64_t ChooseScriptAndApply(uint64_t rngHash)
    {
        std::unordered_set<MovementOptions> movementOptions;
        ExecuteAdhoc([&]()
            {
                movementOptions = GetMovementOptions(rngHash);
                return true;
            });

        // Execute script and update rng hash
        rngHash = Scattershot<TState, TResource>::GetHash(rngHash);
        ModifyAdhoc([&]() { return ApplyMovement(movementOptions, rngHash); });
        return rngHash = Scattershot<TState, TResource>::GetHash(rngHash);
    }

    StateBin<TState> GetStateBinSafe()
    {
        StateBin<TState> stateBin;
        ExecuteAdhoc([&]()
            {
                stateBin = GetStateBin();
                return true;
            });

        return stateBin;
    }

    float GetStateFitnessSafe()
    {
        float fitness;
        ExecuteAdhoc([&]()
            {
                fitness = GetStateFitness();
                return true;
            });

        return fitness;
    }

    AdhocBaseScriptStatus DecodeBaseBlockDiffAndApply()
    {
        return ModifyAdhoc([&]()
            {
                //if (tState.BaseBlock.tailSeg == 0) printf("origBlock has null tailSeg");

                Segment* tailSegment = tState.BaseBlock.tailSegment;
                Segment* currentSegment;
                int tailSegmentDepth = tailSegment->depth;
                for (int i = 1; i <= tailSegmentDepth; i++) {
                    currentSegment = tailSegment;
                    while (currentSegment->depth != i) // inefficient but probably doesn't matter
                    {  
                        //if (currentSegment->parent == 0) printf("Parent is null!");
                        //if (currentSegment->parent->depth + 1 != currentSegment->depth) { printf("Depths wrong"); }
                        currentSegment = currentSegment->parent;
                    }

                    uint64_t inputRngHash = currentSegment->seed;
                    for (int script = 0; script < currentSegment->nScripts; script++)
                        inputRngHash = ChooseScriptAndApply(inputRngHash);
                }

                return true;
            });
    }

    AdhocBaseScriptStatus ExecuteFromBaseBlockAndEncode()
    {
        return ExecuteAdhoc([&]()
            {
                StateBin<TState> prevStateBin = GetStateBinSafe();
                uint64_t baseRngHash = tState.RngHash;

                for (int n = 0; n < config.SegmentLength; n++)
                {
                    tState.RngHash = ChooseScriptAndApply(tState.RngHash);
                    if (!ValidateCourseAndArea() || !ExecuteAdhoc([&]() { return ValidateBlock(); }).executed)
                        break;

                    auto newStateBin = GetStateBinSafe();
                    if (newStateBin != prevStateBin && newStateBin != tState.BaseBlock.stateBin)
                    {
                        // Create and add block to list.
                        tState.ProcessNewBlock(baseRngHash, n, newStateBin, GetStateFitnessSafe());
                        prevStateBin = newStateBin; // TODO: Why this here?
                    }
                }

                return true;
            });
    }
};

//Include template method implementations
#include "ThreadState.t.hpp"

#endif