#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/SharedLib.hpp>
#include <ScattershotThread.hpp>
#include <omp.h>
#include <vector>
#include <filesystem>


#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot;

template <class TState, derived_from_specialization_of<Resource> TResource>
class ScattershotThread;

template <class TState>
class StateBin;

class Segment;

template <class TState>
class Block;

template <class TState>
class StateBin {
public:
    TState state;

    StateBin() = default;
    StateBin(TState state) : state(state) {}

    /*
    StateBin(const StateBin<TState>& toCopy)
    {
        state = toCopy.state;
    }

    template <typename... T>
    StateBin(T&&... args)
    {
        state = TState(std::forward<T>(args)...);
    }
    */

    template <typename T>
    uint64_t GetHash(const T& toHash) const
    {
        std::hash<std::byte> byteHasher;
        const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
        uint64_t hashValue = 0;
        for (std::size_t i = 0; i < sizeof(toHash); ++i)
            hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

        return hashValue;
    }

    int FindNewHashIndex(int* hashTable, int maxHashes) const
    {
        uint64_t hash;
        for (int i = 0; i < 100; i++)
        {
            hash = GetHash(hash);
            int hashIndex = hash % maxHashes;
            
            if (hashTable[hashIndex] == -1)
                return hashIndex;
        }

        //printf("Failed to find new hash index after 100 tries!\n");
        return -1;
    }

    int GetBlockIndex(Block<TState>* blocks, int* hashTable, int maxHashes, int nMin, int nMax) const
    {
        uint64_t hash = GetHash(state);
        for (int i = 0; i < 100; i++)
        {
            int blockIndex = hashTable[hash % maxHashes];
            if (blockIndex == -1)
                return nMax;

            if (blockIndex >= nMin && blockIndex < nMax && blocks[blockIndex].stateBin == *this)
                return blockIndex;

            hash = GetHash(hash);
        }

        //printf("Failed to find block from hash after 100 tries!\n");
        return -1; // TODO: Should be nMax?
    }

    bool operator==(const StateBin<TState>& toCompare) const {
        // Compare byte representations
        return std::memcmp(&state, &toCompare.state, sizeof(TState)) == 0;
    }

    bool operator!=(const StateBin<TState>& toCompare) const
    {
        return !(*this == toCompare);
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
    int StartFromRootEveryNShots;
    std::filesystem::path M64Path;
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
    const Configuration& config;
    GlobalState<TState> gState;
    friend class ScattershotThread<TState, TResource>;

    Scattershot(const Configuration& configuration) : config(configuration), gState(configuration)
    {
        //gState = GlobalState<TState>(config);
    }

    template <derived_from_specialization_of<ScattershotThread> TScattershotThread>
    static void Run(const Configuration& configuration)
    {
        auto scattershot = TScattershotThread::CreateScattershot(configuration);
        scattershot.MultiThread(configuration.TotalThreads, [&]()
            {
                int threadId = omp_get_thread_num();
                if (threadId < configuration.ResourcePaths.size())
                {
                    M64 m64 = M64(configuration.M64Path);
                    m64.load();

                    auto resourcePath = configuration.ResourcePaths[threadId];
                    auto status = TScattershotThread::template MainConfig<TScattershotThread>(m64, resourcePath, scattershot, threadId);
                }
            });
    }

    template <typename T>
    static uint64_t GetHash(const T& toHash)
    {
        std::hash<std::byte> byteHasher;
        const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
        uint64_t hashValue = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

        return hashValue;
    }

protected:
    

private:

    template <typename F>
    void MultiThread(int nThreads, F func)
    {
        omp_set_num_threads(nThreads);
        #pragma omp parallel
        {
            func();
        }
    }
};

//Include template method implementations
#include "GlobalState.t.hpp"

#endif