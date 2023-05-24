#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/SharedLib.hpp>
#include <omp.h>
#include <vector>
#include <filesystem>
#include <unordered_set>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <MovementOption.hpp>
#include <algorithm>

#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
class Scattershot;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>>
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
    int FindSharedHashIndex(Block<TState>* blocks, const int* hashTable, int maxHashes) const;
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
    int PelletLength;
    int MaxSegments;
    int MaxBlocks;
    int MaxHashes;
    int MaxSharedBlocks;
    int MaxSharedHashes;
    int TotalThreads;
    int MaxSharedSegments;
    int MaxLocalSegments;
    long long MaxShots;
    int PelletsPerShot;
    int ShotsPerMerge;
    int MergesPerSegmentGC;
    int StartFromRootEveryNShots;
    int MaxConsecutiveFailedPellets;
    uint32_t CsvSamplePeriod; // Every nth new block per thread will be printed to a CSV. Set to 0 to disable CSV export.
    std::filesystem::path M64Path;
    std::string CsvOutputDirectory;
    std::vector<std::filesystem::path> ResourcePaths;

    template <class TContainer, typename TElement = typename TContainer::value_type>
        requires std::is_same_v<TElement, std::string>
    void SetResourcePaths(const TContainer& container);
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>>
class Scattershot
{
public:
    const Configuration& config;
    friend class ScattershotThread<TState, TResource, TStateTracker>;

    Scattershot(const Configuration& configuration);

    template <derived_from_specialization_of<ScattershotThread> TScattershotThread, class TResourceConfig, typename F>
        requires std::same_as<std::invoke_result_t<F, std::filesystem::path>, TResourceConfig>
    static void Run(const Configuration& configuration, F resourceConfigGenerator)
    {
        auto scattershot = Scattershot(configuration);
        scattershot.OpenCsv();

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

    template <template<class, class, class> class TScattershotThread>
        requires derived_from_specialization_of<ScattershotThread<TState, TResource, TStateTracker>, TScattershotThread>
    static void Run(const Configuration& configuration)
    {
        auto scattershot = Scattershot(configuration);
        scattershot.OpenCsv();

        scattershot.MultiThread(configuration.TotalThreads, [&]()
            {
                int threadId = omp_get_thread_num();
                if (threadId < configuration.ResourcePaths.size())
                {
                    M64 m64 = M64(configuration.M64Path);
                    m64.load();

                    auto resourcePath = configuration.ResourcePaths[threadId];
                    auto status = TScattershotThread<TState, TResource, TStateTracker>
                        ::template MainConfig<TScattershotThread<TState, TResource, TStateTracker>>(m64, resourcePath, scattershot, threadId);
                }
            });
    }

    ~Scattershot();

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

    std::string CsvFileName;
    std::ofstream Csv;
    int CsvSampleFrequency;
    int64_t CsvCounter = 0;
    int64_t CsvRows = -1; // Print this each merge so analysis can be run at the same time w/o dealing with partial rows
    bool CsvEnabled = false;

    uint64_t ScriptCount = 0;
    uint64_t FailedScripts = 0;
    uint64_t RedundantScripts = 0;
    uint64_t NovelScripts = 0;

    void MergeState(int mainIteration, uint64_t rngHash);
    void MergeBlocks(uint64_t rngHash);
    void MergeSegments();
    void SegmentGarbageCollection();

    void OpenCsv();

    template <typename F>
    void MultiThread(int nThreads, F func);
};

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
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
class ScattershotThread : public TopLevelScript<TResource, TStateTracker>
{
public:
    using TopLevelScript<TResource, TStateTracker>::MainConfig;

protected:
    friend class Scattershot<TState, TResource>;

    // Using directives needed for MSVC >:(
    using Script<TResource>::LongLoad;
    using Script<TResource>::ExecuteAdhoc;
    using Script<TResource>::ModifyAdhoc;
    

    const Configuration& config;

    ScattershotThread(Scattershot<TState, TResource, TStateTracker>& scattershot, int id);

    virtual bool validation();
    bool execution();
    virtual bool assertion();

    virtual void SelectMovementOptions() = 0;
    virtual bool ApplyMovement() = 0;
    virtual TState GetStateBin() = 0;
    virtual bool ValidateState() = 0;
    virtual float GetStateFitness() = 0;

    virtual std::string GetCsvLabels();
    virtual std::string GetCsvRow();
    virtual bool ForceAddToCsv();

    uint64_t GetTempRng();

    void AddRandomMovementOption(std::map<MovementOption, double> weightedOptions);
    void AddMovementOption(MovementOption movementOption, double probability = 1.0);
    bool CheckMovementOptions(MovementOption movementOption);
    Inputs RandomInputs(std::map<Buttons, double> buttonProbabilities);

private:
    Scattershot<TState, TResource, TStateTracker>& scattershot;
    Block<TState>* Blocks;
    int* HashTable;
    int Id;
    uint64_t RngHash = 0;
    uint64_t RngHashTemp = 0;
    Block<TState> BaseBlock;
    std::unordered_set<MovementOption> movementOptions;

    short startCourse;
    short startArea;

    // Thread state methods
    uint64_t GetRng();
    void SetRng(uint64_t rngHash);
    void SetTempRng(uint64_t rngHash);
    void InitializeMemory();
    bool SelectBaseBlock(int mainIteration);
    bool ValidateBaseBlock(int shot);
    bool ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin);

    void AddCsvRow(int shot);
    void AddCsvLabels();

    template <typename F>
    void SingleThread(F func);

    template <typename F>
    void ThreadLock(F func);

    bool ValidateCourseAndArea();
    bool ChooseScriptAndApply();
    StateBin<TState> GetStateBinSafe();
    float GetStateFitnessSafe();
    AdhocBaseScriptStatus DecodeBaseBlockDiffAndApply();
    AdhocBaseScriptStatus ExecuteFromBaseBlockAndEncode(int shot);

    template <typename T>
    uint64_t GetHash(const T& toHash) const;
};

//Include template method implementations
#include "Scattershot.t.hpp"
#include "ScattershotThread.t.hpp"

#endif
