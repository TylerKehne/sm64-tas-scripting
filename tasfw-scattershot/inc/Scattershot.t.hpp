#pragma once
#ifndef SCATTERSHOT_H
#error "Scattershot.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState>
template <typename T>
uint64_t StateBin<TState>::GetHash(const T& toHash, bool ignoreFillerBytes) const
{
    std::hash<std::byte> byteHasher;
    const auto* data = reinterpret_cast<const std::byte*>(&toHash);
    uint64_t hashValue = 0;
    for (std::size_t i = 0; i < sizeof(toHash); ++i)
    {
        if (ignoreFillerBytes || !StateBin<TState>::FillerBytes.contains(i))
            hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);
    }
   
    return hashValue;
}

template <class TState>
int StateBin<TState>::FindNewHashIndex(const int* hashTable, int maxHashes) const
{
    uint64_t hash = GetHash(state, false);
    for (int i = 0; i < 100; i++)
    {
        int hashIndex = hash % maxHashes;

        if (hashTable[hashIndex] == -1)
            return hashIndex;

        hash = GetHash(hash, true);
    }

    //printf("Failed to find new hash index after 100 tries!\n");
    return -1;
}

template <class TState>
int StateBin<TState>::FindSharedHashIndex(Block<TState>* blocks, const int* hashTable, int maxHashes) const
{
    uint64_t hash = GetHash(state, false);
    for (int i = 0; i < 100; i++)
    {
        int hashIndex = hash % maxHashes;

        if (hashTable[hashIndex] == -1)
            return -1;

        if (blocks[hashTable[hashIndex]].stateBin == *this)
            return hashIndex;

        hash = GetHash(hash, true);
    }

    //printf("Failed to find new hash index after 100 tries!\n");
    return -1;
}

template <class TState>
void StateBin<TState>::print() const
{
    #pragma omp critical
    {
        // cast the instance to a char pointer
        const char* ptr = reinterpret_cast<const char*>(&state);

        // print the bytes in hex format using printf
        for (std::size_t i = 0; i < sizeof(TState); i++)
            printf("%02X ", static_cast<unsigned char>(ptr[i]));

        printf("\n");
    }
}

template <class TContainer, typename TElement>
    requires std::is_same_v<TElement, std::string>
void Configuration::SetResourcePaths(const TContainer& container)
{
    for (const std::string& item : container)
    {
        ResourcePaths.emplace_back(item);
    }
}

template <class TState>
int StateBin<TState>::GetBlockIndex(Block<TState>* blocks, int* hashTable, int maxHashes, int nMin, int nMax) const
{
    uint64_t hash = GetHash(state, false);
    for (int i = 0; i < 100; i++)
    {
        int blockIndex = hashTable[hash % maxHashes];
        if (blockIndex == -1)
            return nMax;

        if (blockIndex >= nMin && blockIndex < nMax && blocks[blockIndex].stateBin == *this)
            return blockIndex;

        hash = GetHash(hash, true);
    }

    //printf("Failed to find block from hash after 100 tries!\n");
    return -1; // TODO: Should be nMax?
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
template <typename F>
void Scattershot<TState, TResource, TStateTracker>::MultiThread(int nThreads, F func)
{
    omp_set_num_threads(nThreads);
    #pragma omp parallel
    {
        func();
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
Scattershot<TState, TResource, TStateTracker>::Scattershot(const Configuration& config) : config(config)
{
    AllBlocks = (Block<TState>*)calloc(config.TotalThreads * config.MaxBlocks + config.MaxSharedBlocks, sizeof(Block<TState>));
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

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void Scattershot<TState, TResource, TStateTracker>::MergeBlocks(uint64_t rngHash)
{
    //printer.printfQ("Merging blocks.\n");
    for (int threadId = 0; threadId < config.TotalThreads; threadId++)
    {
        //printf("Thread blocks: %d\n", NBlocks[threadId]);
        //int newBlocks = 0;
        for (int n = 0; n < NBlocks[threadId]; n++)
        {
            const Block<TState>& block = AllBlocks[threadId * config.MaxBlocks + n];
            int blockIndex = block.stateBin.GetBlockIndex(SharedBlocks, SharedHashTable, config.MaxSharedHashes, 0, NBlocks[config.TotalThreads]);
            if (blockIndex < NBlocks[config.TotalThreads])
            {
                if (block.fitness > SharedBlocks[blockIndex].fitness) // changed to >
                    SharedBlocks[blockIndex] = block;

                // This block was previously generated by a script that was marked as novel,
                // but it turned out to be redundant.
                NovelScripts--;
                RedundantScripts++;

                continue;
            }
            /*
            else if (config.BlockCannibalismRate > 0 && NBlocks[config.TotalThreads] >= 2) // Occasionally cannibalize old blocks with new ones, to equalize distibution of blocks throughout state space
            {
                rngHash = block.stateBin.GetHash(rngHash, true);

                if (config.BlockCannibalismRate >= 1.0 || config.BlockCannibalismRate >= double(rngHash) / double(0xFFFFFFFFFFFFFFFFull))
                {
                    rngHash = block.stateBin.GetHash(rngHash, true);
                    int blockToEraseIndex = (rngHash % ((NBlocks[config.TotalThreads]) - 1)) + 1;
                    const Block<TState>& blockToErase = SharedBlocks[blockToEraseIndex];
                    uint64_t blockToEraseHash = blockToErase.stateBin.FindSharedHashIndex(SharedBlocks, SharedHashTable, config.MaxSharedHashes);

                    SharedHashTable[blockToEraseHash] = -1; // mark old block as erased
                    SharedBlocks[blockToEraseIndex] = block; // replace with new block
                    SharedHashTable[block.stateBin.FindNewHashIndex(SharedHashTable, config.MaxSharedHashes)] = blockToEraseIndex;

                    continue;
                }
            }
            */
            SharedHashTable[block.stateBin.FindNewHashIndex(SharedHashTable, config.MaxSharedHashes)] = NBlocks[config.TotalThreads];
            SharedBlocks[NBlocks[config.TotalThreads]++] = block;
            //newBlocks++;
        }

        //printf("New blocks: %d\n", newBlocks);
    }

    memset(AllHashTables, 0xFF, config.MaxHashes * config.TotalThreads * sizeof(int)); // Clear all local hash tables.

    for (int threadId = 0; threadId < config.TotalThreads; threadId++)
        NBlocks[threadId] = 0; // Clear all local blocks.
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void Scattershot<TState, TResource, TStateTracker>::MergeSegments()
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

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void Scattershot<TState, TResource, TStateTracker>::SegmentGarbageCollection()
{
    //printf("Segment garbage collection. Start with %d segments\n", NSegments[config.TotalThreads]);

    for (int segmentIndex = config.TotalThreads * config.MaxLocalSegments;
        segmentIndex < config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads];
        segmentIndex++)
    {
        AllSegments[segmentIndex]->nReferences = 0;
    }

    for (int segmentIndex = config.TotalThreads * config.MaxLocalSegments;
        segmentIndex < config.TotalThreads * config.MaxLocalSegments + NSegments[config.TotalThreads];
        segmentIndex++)
    {
        if (AllSegments[segmentIndex]->parent != 0)
            AllSegments[segmentIndex]->parent->nReferences++;
    }

    for (int blockIndex = 0; blockIndex < NBlocks[config.TotalThreads]; blockIndex++)
        SharedBlocks[blockIndex].tailSegment->nReferences++;

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

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void Scattershot<TState, TResource, TStateTracker>::MergeState(int mainIteration, uint64_t rngHash)
{
    // Merge all blocks from all threads and redistribute info.
    MergeBlocks(rngHash);

    // Handle segments
    MergeSegments();

    if (mainIteration % (config.ShotsPerMerge * config.MergesPerSegmentGC) == 0)
        SegmentGarbageCollection();

    #pragma omp critical
    {
        printf("\nThread ALL Loop %d blocks %d\n", mainIteration, NBlocks[config.TotalThreads]);

        // Print cumulative script results
        if (ScriptCount != 0)
        {
            int futility = double(FailedScripts) / double(ScriptCount) * 100;
            int redundancy = double(RedundantScripts) / double(ScriptCount) * 100;
            int discovery = double(NovelScripts) / double(ScriptCount) * 100;

            printf("Futility: %d%% Redundancy: %d%% Discovery: %d%%\n", futility, redundancy, discovery);
        }

        if (CsvRows != -1)
            printf("CSV rows: %d\n", CsvRows);
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void Scattershot<TState, TResource, TStateTracker>::OpenCsv()
{
    if (config.CsvSamplePeriod == 0)
        return;

    auto now = std::chrono::system_clock::now();
    long long startTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    CsvFileName = config.CsvOutputDirectory + "csv_" + std::to_string(startTime) + ".csv";
    Csv = std::ofstream(CsvFileName);

    if (!Csv.fail())
    {
        CsvEnabled = true;
        cout << "CSV file name: " << CsvFileName << "\n";
    }
    else
        cout << "Unable to create CSV file.";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
Scattershot<TState, TResource, TStateTracker>::~Scattershot()
{
    Csv.close();
}

#endif
