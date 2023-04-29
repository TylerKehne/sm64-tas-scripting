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

            /*
            uint64_t s = gState.SharedBlocks[origInx].pos.s;
            int normInfo = s % 900;
            float xNorm = (float)((int)normInfo / 30);
            float zNorm = (float)(normInfo % 30);
            float approxXZSum = fabs((xNorm - 15) / 15) + fabs((zNorm - 15) / 15) + .01;
            bool validXzSum = ((float)(Utils::xoro_r(&RngSeed) % 50) / 100 < approxXZSum * approxXZSum;
            */

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

#endif
