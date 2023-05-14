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
    StateBin(const TState& state) : state(state) {}

    bool operator==(const StateBin<TState>&) const = default;

    template <typename T>
    uint64_t GetHash(const T& toHash, bool ignoreFillerBytes) const;

    int FindNewHashIndex(const int* hashTable, int maxHashes) const;
    int GetBlockIndex(Block<TState>* blocks, int* hashTable, int maxHashes, int nMin, int nMax) const;
    void print() const;

    static std::unordered_set<int> GetStateBinRuntimeFillerBytes()
    {
        std::unordered_set<int> fillerBytes;

        StateBin<TState> stateBin;
        std::byte* binPtr = reinterpret_cast<std::byte*>(&stateBin.state);

        // initialize to specific garbage data compatible with all primitives;
        for (int i = 0; i < sizeof(TState); i++)
            binPtr[i] = (std::byte)0x3f;

        // Check which bytes identity depends on
        StateBin<TState> stateBinCopy = stateBin;
        std::byte* binCopyPtr = reinterpret_cast<std::byte*>(&stateBin.state);
        for (int i = 0; i < sizeof(TState); i++)
        {
            binCopyPtr[i] = (std::byte)0x00;
            if (stateBin == stateBinCopy)
                fillerBytes.insert(i);
            binCopyPtr[i] = (std::byte)0x3f;
        }

        return fillerBytes;
    }

    inline const static std::unordered_set<int> FillerBytes = GetStateBinRuntimeFillerBytes();
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
    void SetResourcePaths(const TContainer& container);
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot
{
public:
    const Configuration& config;
    friend class ScattershotThread<TState, TResource>;

    Scattershot(const Configuration& configuration);

    template <derived_from_specialization_of<ScattershotThread> TScattershotThread, class TResourceConfig, typename F>
        requires std::same_as<std::invoke_result_t<F, std::filesystem::path>, TResourceConfig>
    static void Run(const Configuration& configuration, F resourceConfigGenerator)
    {
        auto scattershot = Scattershot(configuration);
        scattershot.MultiThread(configuration.TotalThreads, [&]()
            {
                int threadId = omp_get_thread_num();
                if (threadId < configuration.ResourcePaths.size())
                {
                    M64 m64 = M64(configuration.M64Path);
                    m64.load();

                    auto status = TScattershotThread::template MainConfig<TScattershotThread>
                        (m64, resourceConfigGenerator(configuration.ResourcePaths[threadId]), scattershot, threadId);
                }
            });
    }

    template <template<class, class> class TScattershotThread>
        requires derived_from_specialization_of<TScattershotThread<TState, TResource>, ScattershotThread>
    static void Run(const Configuration& configuration)
    {
        auto scattershot = Scattershot(configuration);
        scattershot.MultiThread(configuration.TotalThreads, [&]()
            {
                int threadId = omp_get_thread_num();
                if (threadId < configuration.ResourcePaths.size())
                {
                    M64 m64 = M64(configuration.M64Path);
                    m64.load();

                    auto resourcePath = configuration.ResourcePaths[threadId];
                    auto status = TScattershotThread<TState, TResource>::template MainConfig<TScattershotThread<TState, TResource>>(m64, resourcePath, scattershot, threadId);
                }
            });
    }

private:
    // Global State
    Segment** AllSegments;
    Block<TState>* AllBlocks;
    int* AllHashTables;
    int* NBlocks;
    int* NSegments;
    Block<TState>* SharedBlocks;
    int* SharedHashTable;
    std::unordered_set<int> StateBinFillerBytes;

    void MergeState(int mainIteration);
    void MergeBlocks();
    void MergeSegments();
    void SegmentGarbageCollection();

    template <typename F>
    void MultiThread(int nThreads, F func);
};

//Include template method implementations
#include "Scattershot.t.hpp"

#endif
