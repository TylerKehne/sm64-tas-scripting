#pragma once
#ifndef SCATTERSHOT_THREAD_H
#error "ThreadState.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState, derived_from_specialization_of<Resource> TResource>
ScattershotThread<TState, TResource>::ScattershotThread(Scattershot<TState, TResource>& scattershot, int id)
    : scattershot(scattershot), config(scattershot.config)
{
    Id = id;
    Blocks = scattershot.AllBlocks + Id * config.MaxBlocks;
    HashTable = scattershot.AllHashTables + Id * config.MaxHashes;
    RngHash = (uint64_t)(Id + 173) * 5786766484692217813;
    
    //printf("Thread %d\n", Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::validation() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::execution()
{
    LongLoad(config.StartFrame);
    InitializeMemory(GetStateBinSafe());

    // Record start course/area for validation (generally scattershot has no cross-level value)
    startCourse = *(short*)this->resource->addr("gCurrCourseNum");
    startArea = *(short*)this->resource->addr("gCurrAreaIndex");

    for (int shot = 0; shot <= config.MaxShots; shot++)
    {
        // ALWAYS START WITH A MERGE SO THE SHARED BLOCKS ARE OK.
        if (shot % config.ShotsPerMerge == 0)
            SingleThread([&]() { scattershot.MergeState(shot); });

        // Pick a block to "fire a scattershot" at
        if (!SelectBaseBlock(shot))
            break;

        auto status = ExecuteAdhoc([&]()
            {
                DecodeBaseBlockDiffAndApply();

                if (!ValidateBaseBlock(GetStateBinSafe()))
                    return false;

                this->Save();
                for (int segment = 0; segment < config.SegmentsPerShot; segment++)
                    ExecuteFromBaseBlockAndEncode();

                return true;
            });

        //printf("%d %d %d %d\n", status.nLoads, status.nSaves, status.nFrameAdvances, status.executionDuration);
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::assertion() { return true; }

/*
template <class TState, derived_from_specialization_of<Resource> TResource>
static Scattershot<TState, TResource> CreateScattershot(const Configuration& configuration)
{
    return Scattershot<TState, TResource>(configuration);
}
*/
template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::InitializeMemory(StateBin<TState> initStateBin)
{
    // Initial block
    Blocks[0].stateBin = initStateBin; //CHEAT TODO NOTE
    Blocks[0].tailSegment = (Segment*)malloc(sizeof(Segment)); //Instantiate root segment
    Blocks[0].tailSegment->nScripts = 0;
    Blocks[0].tailSegment->parent = NULL;
    Blocks[0].tailSegment->nReferences = 0;
    Blocks[0].tailSegment->depth = 1;

    // Init local hash table.
    for (int hashIndex = 0; hashIndex < config.MaxHashes; hashIndex++)
        HashTable[hashIndex] = -1;

    HashTable[Blocks[0].stateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = 0;

    // Synchronize global state
    scattershot.AllSegments[scattershot.NSegments[Id] + Id * config.MaxLocalSegments] = Blocks[0].tailSegment;
    scattershot.NSegments[Id]++;
    scattershot.NBlocks[Id]++;

    //LoopTimeStamp = omp_get_wtime();
}

template <class TState, derived_from_specialization_of<Resource> TResource>
uint64_t ScattershotThread<TState, TResource>::UpdateRngHash()
{
    return RngHash = Scattershot<TState, TResource>::GetHash(&RngHash);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::SelectBaseBlock(int mainIteration)
{
    int sharedBlockIndex = scattershot.NBlocks[config.TotalThreads];
    if (mainIteration % config.StartFromRootEveryNShots == 0)
        sharedBlockIndex = 0;
    else
    {
        int weighted = UpdateRngHash() % 5;
        for (int attempt = 0; attempt < 100000; attempt++) {
            sharedBlockIndex = UpdateRngHash() % scattershot.NBlocks[config.TotalThreads];

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment == 0)
            {
                //printf("Chosen block tailseg null!\n");
                continue;
            }

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment->depth == 0)
            {
                //printf("Chosen block tailseg depth 0!\n");
                continue;
            }

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment->depth < config.MaxSegments)
                break;
        }
        if (sharedBlockIndex == scattershot.NBlocks[config.TotalThreads])
        {
            //printf("Could not find block!\n");
            return false;
        }
    }

    BaseBlock = scattershot.SharedBlocks[sharedBlockIndex];
    //if (BaseBlock.tailSeg->depth > config.MaxSegments + 2) { printf("BaseBlock depth above max!\n"); }
    //if (BaseBlock.tailSeg->depth == 0) { printf("BaseBlock depth is zero!\n"); }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::ValidateBaseBlock(const StateBin<TState>& baseBlockStateBin) const
{
    if (BaseBlock.stateBin != baseBlockStateBin) {
        //printf("ORIG %d %d %d %ld AND BLOCK %d %d %d %ld NOT EQUAL\n",
        //    baseBlockStateBin.x, baseBlockStateBin.y, baseBlockStateBin.z, baseBlockStateBin.s,
        //    BaseBlock.pos.x, BaseBlock.pos.y, BaseBlock.pos.z, BaseBlock.pos.s);

        Segment* currentSegmentDebug = BaseBlock.tailSegment;
        while (currentSegmentDebug != 0) {  //inefficient but probably doesn't matter
            if (currentSegmentDebug->parent == 0)
                //printf("Parent is null!");
            //if (currentSegmentDebug->parent->depth + 1 != currentSegmentDebug->depth) { printf("Depths wrong"); }
                currentSegmentDebug = currentSegmentDebug->parent;
        }

        BaseBlock.stateBin.print();
        baseBlockStateBin.print();
        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin, float newFitness)
{
    Block<TState> newBlock;

    // Create and add block to list.
    if (scattershot.NBlocks[Id] == config.MaxBlocks)
    {
        //printf("Max local blocks reached!\n");
    }
    else {
        //UPDATED FOR SEGMENTS STRUCT
        newBlock = BaseBlock;
        newBlock.stateBin = newStateBin;
        newBlock.fitness = newFitness;
        int blockIndexLocal = newStateBin.GetBlockIndex(Blocks, HashTable, config.MaxHashes, 0, scattershot.NBlocks[Id]);
        int blockIndex = newStateBin.GetBlockIndex(
            scattershot.SharedBlocks, scattershot.SharedHashTable, config.MaxSharedHashes, 0, scattershot.NBlocks[config.TotalThreads]);

        bool bestlocalBlock = blockIndexLocal < scattershot.NBlocks[Id]
            && newBlock.fitness >= Blocks[blockIndexLocal].fitness;
        bool bestSharedBlockOrNew = !(blockIndex < scattershot.NBlocks[config.TotalThreads]
            && newBlock.fitness < scattershot.SharedBlocks[blockIndex].fitness);

        if (bestlocalBlock || bestSharedBlockOrNew)
        {
            if (!bestlocalBlock)
                HashTable[newStateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = scattershot.NBlocks[Id];

            // Create new segment
            Segment* newSegment = (Segment*)malloc(sizeof(Segment));
            newSegment->parent = BaseBlock.tailSegment;
            newSegment->nReferences = bestlocalBlock ? 0 : 1;
            newSegment->nScripts = nScripts + 1;
            newSegment->seed = baseRngHash;
            newSegment->depth = BaseBlock.tailSegment->depth + 1;
            //if (newSegment->depth == 0) { printf("newSeg depth is 0!\n"); }
            //if (BaseBlock.tailSegment->depth == 0) { printf("origBlock tailSeg depth is 0!\n"); }
            newBlock.tailSegment = newSegment;
            scattershot.AllSegments[Id * config.MaxLocalSegments + scattershot.NSegments[Id]] = newSegment;
            scattershot.NSegments[Id] += 1;
            Blocks[blockIndexLocal] = newBlock;

            if (bestlocalBlock)
                Blocks[blockIndexLocal] = newBlock;
            else
                Blocks[scattershot.NBlocks[Id]++] = newBlock;
        }
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource>
template <typename F>
void ScattershotThread<TState, TResource>::SingleThread(F func)
{
    #pragma omp barrier
    {
        if (omp_get_thread_num() == 0)
            func();
    }
    #pragma omp barrier

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::ValidateCourseAndArea()
{
    return startCourse == *(short*)this->resource->addr("gCurrCourseNum")
        && startArea == *(short*)this->resource->addr("gCurrAreaIndex");
}

template <class TState, derived_from_specialization_of<Resource> TResource>
uint64_t ScattershotThread<TState, TResource>::ChooseScriptAndApply(uint64_t rngHash)
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

template <class TState, derived_from_specialization_of<Resource> TResource>
StateBin<TState> ScattershotThread<TState, TResource>::GetStateBinSafe()
{
    StateBin<TState> stateBin;
    ExecuteAdhoc([&]()
        {
            stateBin = GetStateBin();
            return true;
        });

    return stateBin;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
float ScattershotThread<TState, TResource>::GetStateFitnessSafe()
{
    float fitness;
    ExecuteAdhoc([&]()
        {
            fitness = GetStateFitness();
            return true;
        });

    return fitness;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus ScattershotThread<TState, TResource>::DecodeBaseBlockDiffAndApply()
{
    return ModifyAdhoc([&]()
        {
            //if (tState.BaseBlock.tailSeg == 0) printf("origBlock has null tailSeg");

            Segment* tailSegment = BaseBlock.tailSegment;
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

template <class TState, derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus ScattershotThread<TState, TResource>::ExecuteFromBaseBlockAndEncode()
{
    return ExecuteAdhoc([&]()
        {
            StateBin<TState> prevStateBin = GetStateBinSafe();
            uint64_t baseRngHash = RngHash;

            for (int n = 0; n < config.SegmentLength; n++)
            {
                RngHash = ChooseScriptAndApply(RngHash);
                if (!ValidateCourseAndArea() || !ExecuteAdhoc([&]() { return ValidateBlock(); }).executed)
                    break;

                auto newStateBin = GetStateBinSafe();
                if (newStateBin != prevStateBin && newStateBin != BaseBlock.stateBin)
                {
                    // Create and add block to list.
                    ProcessNewBlock(baseRngHash, n, newStateBin, GetStateFitnessSafe());
                    prevStateBin = newStateBin; // TODO: Why this here?
                }
            }

            return true;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource>
MovementOptions ScattershotThread<TState, TResource>::ChooseMovementOption(uint64_t rngHash, std::map<MovementOptions, double> weightedOptions)
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

#endif
