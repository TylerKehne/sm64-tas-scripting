#pragma once
#ifndef SCATTERSHOT_H
#error "GlobalState.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState>
GlobalState<TState>::GlobalState(const Configuration& config) : config(config)
{
    AllBlocks = (Block*)calloc(config.TotalThreads * config.MaxBlocks + config.MaxSharedBlocks, sizeof(Block));
    AllSegments = (Segment**)malloc((config.MaxSharedSegments + config.TotalThreads * config.MaxLocalSegments) * sizeof(Segment*));
    AllHashTables = (int*)calloc(config.TotalThreads * config.MaxHashes + config.MaxSharedHashes, sizeof(int));
    NBlocks = (int*)calloc(config.TotalThreads + 1, sizeof(int));
    NSegments = (int*)calloc(config.TotalThreads + 1, sizeof(int));
    SharedBlocks = AllBlocks + config.TotalThreads * config.MaxBlocks;
    SharedHashTable = AllHashTables + config.TotalThreads * config.MaxHashes;

    // Init shared hash table.
    for (int hashInx = 0; hashInx < config.MaxSharedHashes; hashInx++)
        SharedHashTable[hashInx] = -1;
}

template <class TState>
void GlobalState<TState>::GlobalState::MergeBlocks()
{
    //printer.printfQ("Merging blocks.\n");
    for (int threadId = 0; threadId < config.TotalThreads; threadId++)
    {
        for (int n = 0; n < NBlocks[threadId]; n++)
        {
            const Block<TState>& block = AllBlocks[threadId * config.MaxBlocks + n];
            int blockIndex = block.stateBin.GetBlockIndex(SharedBlocks, SharedHashTable, config.MaxSharedHashes, 0, NBlocks[config.TotalThreads]);
            if (blockIndex < NBlocks[config.TotalThreads])
            {
                if (block.fitness > SharedBlocks[blockIndex].fitness) // changed to >
                    SharedBlocks[blockIndex] = block;

                continue;
            }

            SharedHashTable[block.stateBin.FindNewHashIndex(SharedHashTable, config.MaxSharedHashes)] = NBlocks[config.TotalThreads];
            SharedBlocks[NBlocks[config.TotalThreads]++] = block;
        }
    }

    memset(AllHashTables, 0xFF, config.MaxHashes * config.TotalThreads * sizeof(int)); // Clear all local hash tables.

    for (int threadId = 0; threadId < config.TotalThreads; threadId++)
        NBlocks[threadId] = 0; // Clear all local blocks.
}

template <class TState>
void GlobalState<TState>::MergeSegments()
{
    //printf("Merging segments\n");

    // Get reference counts for each segment. Tried to track this but ran into
    // multi-threading issues, so might as well recompute here.
    for (int threadId = 0; threadId < config.TotalThreads; threadId++)
    {
        for (int segmentIndex = threadId * config.MaxLocalSegments; segmentIndex < threadId * config.MaxLocalSegments + NSegments[threadId]; segmentIndex++)
        {
            //printf("%d %d\n", segInd, numSegs[totThreads]);
            AllSegments[config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads]] = AllSegments[segmentIndex];
            NSegments[config.TotalThreads]++;
            AllSegments[segmentIndex] = 0;
        }

        NSegments[threadId] = 0;
    }
}

template <class TState>
void GlobalState<TState>::SegmentGarbageCollection()
{
    //printf("Segment garbage collection. Start with %d segments\n", NSegments[config.TotalThreads]);

    for (int segmentIndex = config.TotalThreads * config.MaxLocalSegments;
        segmentIndex < config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads];
        segmentIndex++)
    {
        AllSegments[segmentIndex]->refCount = 0;
    }

    for (int segmentIndex = config.TotalThreads * config.MaxLocalSegments;
        segmentIndex < config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads];
        segmentIndex++)
    {
        if (AllSegments[segmentIndex]->parent != 0)
            AllSegments[segmentIndex]->parent->refCount++;
    }

    for (int blockIndex = 0; blockIndex < NBlocks[config.TotalThreads]; blockIndex++)
        SharedBlocks[blockIndex].tailSeg->refCount++;

    for (int segmentIndex = config.TotalThreads * config.MaxLocalSegments;
        segmentIndex < config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads];
        segmentIndex++)
    {
        Segment* currentSegment = AllSegments[segmentIndex];
        if (currentSegment->nReferences == 0)
        {
            //printf("removing a seg\n");
            if (currentSegment->parent != 0) { currentSegment->parent->nReferences -= 1; }
            //printf("moving %d %d\n", segInd, totThreads*maxLocalSegs+numSegs[totThreads]);
            AllSegments[segmentIndex] = AllSegments[config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads] - 1];
            NSegments[config.TotalThreads]--;
            segmentIndex--;
            free(currentSegment);
        }
    }

    //printf("Segment garbage collection finished. Ended with %d segments\n", NSegments[config.TotalThreads]);
}

template <class TState>
void GlobalState<TState>::MergeState(int mainIteration)
{
    // Merge all blocks from all threads and redistribute info.
    MergeBlocks();

    // Handle segments
    MergeSegments();

    if (mainIteration % (config.ShotsPerMerge * config.MergesPerSegmentGC) == 0)
        SegmentGarbageCollection();
}

#endif
