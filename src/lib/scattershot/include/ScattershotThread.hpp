#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/SharedLib.hpp>
//#include <Scattershot.hpp>
//#include <tasfw/Resource.hpp>
#include <omp.h>
#include <vector>
#include <filesystem>


#ifndef SCATTERSHOT_THREAD_H
#define SCATTERSHOT_THREAD_H

class Segment
{
public:
    Segment* parent;
    uint64_t seed;
    uint32_t nReferences;
    uint8_t nFrames;
    uint8_t depth;
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot;

template <class TState>
class StateBin;

template <class TState>
class Block;

class Configuration;

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
    void ProcessNewBlock(uint64_t prevRngSeed, int nFrames, StateBin<TState> newPos, float newFitness);
    //void PrintStatus(int mainIteration);
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class ScattershotThread : public TopLevelScript<TResource>
{
public:
    enum class MovementOptions { };

    class CustomScriptStatus
    {
    public:
        short startCourse;
        short startArea;
    };
    CustomScriptStatus CustomStatus = CustomScriptStatus();

    Scattershot<TState, TResource>& scattershot;
    const Configuration& config;
    ThreadState<TState, TResource> tState;

    ScattershotThread(Scattershot<TState, TResource>& scattershot, int id) : scattershot(scattershot)
    {
        config = scattershot.config;
        tState = ThreadState(scattershot, id);
    }

    bool validation() { return true; }
    bool assertion() { return true; }
    bool execution()
    {
        LongLoad(config.StartFrame);
        tState.Initialize();

        // Record start course/area for validation (generally scattershot has no cross-level value)
        CustomStatus.startCourse = *(short*)this->resource->addr("gCurrCourseNum");
        CustomStatus.startArea = *(short*)this->resource->addr("gCurrAreaIndex");

        for (int mainIteration = 0; mainIteration <= config.MaxShots; mainIteration++)
        {
            // ALWAYS START WITH A MERGE SO THE SHARED BLOCKS ARE OK.
            if (mainIteration % config.ShotsPerMerge == 0)
                SingleThread([&]() { scattershot.gState.MergeState(); });

            // Pick a block to "fire a scattershot" at
            if (!tState.SelectBaseBlock(mainIteration))
                break;

            ExecuteAdhoc([&]()
                {
                    DecodeAndExecuteBaseBlockDiff();

                    return true;
                });

        }

        return true;
    }

    virtual MovementOptions GetMovementOption(uint64_t rngHash) = 0;
    virtual bool ApplyMovement(MovementOptions movementOption) = 0;

private:
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

    AdhocBaseScriptStatus DecodeAndExecuteBaseBlockDiff()
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
                    for (int f = 0; f < currentSegment->nFrames; f++)
                    {
                        // Execute script and update rng hash
                        MovementOptions movementOption;
                        ExecuteAdhoc([&]()
                            {
                                movementOption = GetMovementOption();
                                return true;
                            });

                        ModifyAdhoc([&]() { return ApplyMovement(movementOption); });
                        inputRngHash = Scattershot<TState, TResource>::GetHash(inputRngHash);
                    }
                }

                return true;
            });
    }
};

//Include template method implementations
#include "ThreadState.t.hpp"

#endif