#pragma once
#ifndef SCATTERSHOT_THREAD_H
#error "ThreadState.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState, derived_from_specialization_of<Resource> TResource>
ThreadState<TState, TResource>::ThreadState(Scattershot<TState, TResource>& scattershot, int id) : scattershot(scattershot)
{
    Id = id;
    config = scattershot.config;
    Blocks = scattershot.gState.AllBlocks + Id * config.MaxBlocks;
    HashTable = scattershot.gState.AllHashTables + Id * config.MaxHashes;
    RngHash = (uint64_t)(Id + 173) * 5786766484692217813;
    
    //printf("Thread %d\n", Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ThreadState<TState, TResource>::Initialize(StateBin<TState> initStateBin)
{
    // Initial block
    Blocks[0].stateBin = initStateBin; //CHEAT TODO NOTE
    Blocks[0].tailSegment = (Segment*)malloc(sizeof(Segment)); //Instantiate root segment
    Blocks[0].tailSegment->numFrames = 0;
    Blocks[0].tailSegment->parent = NULL;
    Blocks[0].tailSegment->nReferences = 0;
    Blocks[0].tailSegment->depth = 1;

    // Init local hash table.
    for (int hashIndex = 0; hashIndex < config.MaxHashes; hashIndex++)
        HashTable[hashIndex] = -1;

    HashTable[Blocks[0].stateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = 0;

    // Synchronize global state
    scattershot.gState.AllSegments[scattershot.gState.NSegments[Id] + Id * config.MaxLocalSegments] = Blocks[0].tailSegment;
    scattershot.gState.NSegments[Id]++;
    scattershot.gState.NBlocks[Id]++;

    //LoopTimeStamp = omp_get_wtime();
}

template <class TState, derived_from_specialization_of<Resource> TResource>
uint64_t ThreadState<TState, TResource>::UpdateRngHash()
{
    return RngHash = Scattershot<TState, TResource>::GetHash(&RngHash);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ThreadState<TState, TResource>::SelectBaseBlock(int mainIteration)
{
    int sharedBlockIndex = scattershot.gState.NBlocks[config.TotalThreads];
    if (mainIteration % 15 == 0)
        sharedBlockIndex = 0;
    else
    {
        int weighted = UpdateRngHash() % 5;
        for (int attempt = 0; attempt < 100000; attempt++) {
            sharedBlockIndex = UpdateRngHash() % scattershot.gState.NBlocks[config.TotalThreads];

            if (scattershot.gState.SharedBlocks[sharedBlockIndex].tailSegment == 0)
            {
                //printf("Chosen block tailseg null!\n");
                continue;
            }

            if (scattershot.gState.SharedBlocks[sharedBlockIndex].tailSegment->depth == 0)
            {
                //printf("Chosen block tailseg depth 0!\n");
                continue;
            }

            if (scattershot.gState.SharedBlocks[sharedBlockIndex].tailSegment->depth < config.MaxSegments)
                break;
        }
        if (sharedBlockIndex == scattershot.gState.NBlocks[config.TotalThreads])
        {
            //printf("Could not find block!\n");
            return false;
        }
    }

    BaseBlock = scattershot.gState.SharedBlocks[sharedBlockIndex];
    //if (BaseBlock.tailSeg->depth > config.MaxSegments + 2) { printf("BaseBlock depth above max!\n"); }
    //if (BaseBlock.tailSeg->depth == 0) { printf("BaseBlock depth is zero!\n"); }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ThreadState<TState, TResource>::ValidateBaseBlock(StateBin<TState> baseBlockStateBin)
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

        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ThreadState<TState, TResource>::ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin, float newFitness)
{
    Block newBlock;

    // Create and add block to list.
    if (scattershot.gState.NBlocks[Id] == config.MaxBlocks)
    {
        //printf("Max local blocks reached!\n");
    }
    else {
        //UPDATED FOR SEGMENTS STRUCT
        newBlock = BaseBlock;
        newBlock.stateBin = newStateBin;
        newBlock.fitness = newFitness;
        int blockIndexLocal = newStateBin.GetBlockIndex(Blocks, HashTable, config.MaxHashes, 0, scattershot.gState.NBlocks[Id]);
        int blockIndex = newStateBin.GetBlockIndex(
            scattershot.gState.SharedBlocks, scattershot.gState.SharedHashTable, config.MaxSharedHashes, 0, scattershot.gState.NBlocks[config.TotalThreads]);

        bool bestlocalBlock = blockIndexLocal < scattershot.gState.NBlocks[Id]
            && newBlock.fitness >= Blocks[blockIndexLocal].fitness;
        bool bestSharedBlockOrNew = !(blockIndex < scattershot.gState.NBlocks[config.TotalThreads]
            && newBlock.fitness < scattershot.gState.SharedBlocks[blockIndex].fitness);

        if (bestlocalBlock || bestSharedBlockOrNew)
        {
            if (!bestlocalBlock)
                HashTable[newStateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = scattershot.gState.NBlocks[Id];

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
            scattershot.gState.AllSegments[Id * config.MaxLocalSegments + scattershot.gState.NSegments[Id]] = newSegment;
            scattershot.gState.NSegments[Id] += 1;
            Blocks[blockIndexLocal] = newBlock;

            if (bestlocalBlock)
                Blocks[blockIndexLocal] = newBlock;
            else
                Blocks[scattershot.gState.NBlocks[Id]++] = newBlock;
        }
    }
}

#endif
