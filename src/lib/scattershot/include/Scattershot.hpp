#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/Resource.hpp>
#include <tasfw/SharedLib.hpp>
#include <omp.h>
#include <vector>
#include <filesystem>

#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot;

template <class TState>
class StateBin;

class Segment
{
public:
    Segment* parent;
    uint64_t seed;
    uint32_t nReferences;
    uint8_t nFrames;
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

template <class TState>
class StateBin {
public:
    TState state;

    int FindNewHashIndex(int* hashTable, int maxHashes)
    {
        uint64_t hash;
        for (int i = 0; i < 100; i++)
        {
            hash = Scattershot::GetHash(hash);
            int hashIndex = hash % maxHashes;
            
            if (hashTable[hashIndex] == -1)
                return hashIndex;
        }

        //printf("Failed to find new hash index after 100 tries!\n");
        return -1;
    }

    int GetBlockIndex(Block<TState>* blocks, int* hashTable, int maxHashes, int nMin, int nMax)
    {
        uint64_t hash = Scattershot::GetHash(state);
        for (int i = 0; i < 100; i++)
        {
            int blockIndex = hashTable[hash % maxHashes];
            if (blockIndex == -1)
                return nMax;

            if (blockIndex >= nMin && blockIndex < nMax && blocks[blockIndex].stateBin == *this)
                return blockIndex;

            hash = Scattershot::GetHash(hash);
        }

        //printf("Failed to find block from hash after 100 tries!\n");
        return -1; // TODO: Should be nMax?
    }

    bool operator==(const StateBin<TState>& toCompare) const {
        // Compare byte representations
        return (sizeof(*this.state) == sizeof(toCompare.state)) &&
            (std::memcmp(state, &toCompare.state, sizeof(*this.state)) == 0);
    }
};

class Configuration
{
public:
    int StartFrame;
    int SegmentLength;
    int MaxSegments;
    int MaxBlocks;
    int MaxHashes;
    int MaxSharedBlocks;
    int MaxSharedHashes;
    int TotalThreads;
    int MaxSharedSegments;
    int MaxLocalSegments;
    int MaxLightningLength;
    long long MaxShots;
    int SegmentsPerShot;
    int ShotsPerMerge;
    int MergesPerSegmentGC;
    std::vector<std::filesystem::path> ResourcePaths;

    template <class TContainer, typename TElement = typename TContainer::value_type>
        requires std::is_same_v<TElement, std::string>
    void SetResourcePaths(const TContainer& container)
    {
        for (const std::string& item : container)
        {
            ResourcePaths.emplace_back(item);
        }
    }
};

template <class TState>
class GlobalState
{
public:
    Segment** AllSegments;
    Block<TState>* AllBlocks;
    int* AllHashTables;
    int* NBlocks;
    int* NSegments;
    Block<TState>* SharedBlocks;
    int* SharedHashTable;
    const Configuration& config;

    GlobalState(const Configuration& config);

    void MergeState(int mainIteration);
    void MergeBlocks();
    void MergeSegments();
    void SegmentGarbageCollection();
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot
{
public:
    template <derived_from_specialization_of<Resource> TResource>
    static void Run(Configuration configuration)// : config(configuration)
    {
        config = Configuration(configuration);
        gState = GlobalState<TState>(config);
        MultiThread(configuration.TotalThreads, [&]()
            {

            });
    }

    template <typename T>
    static uint64_t GetHash(const T& toHash)
    {
        std::hash<std::byte> byteHasher;
        const std::byte* data = reinterpret_cast<const std::byte*>(toHash);
        uint64_t hashValue = 0;
        for (std::size_t i = 0; i < sizeof(toHash); ++i)
            hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

        return hashValue;
    }

protected:
    

private:
    const Configuration config;
    GlobalState<TState> gState;

    template <typename F>
    void MultiThread(int nThreads, F func)
    {
        omp_set_num_threads(nThreads);
        #pragma omp parallel
        {
            func();
        }
    }

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
};

//Include template method implementations
#include "GlobalState.t.hpp"

#endif
