#pragma once

#include <BinaryStateBin.hpp>
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
#include <functional>

#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
class Scattershot;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
class ScattershotThread;

class Segment;

template <class TState>
class Block;

class Configuration
{
public:
    int StartFrame;
    int PelletMaxScripts;
    int PelletMaxFrameDistance;
    int MaxBlocks;
    int TotalThreads;
    long long MaxShots;
    int PelletsPerShot;
    int ShotsPerUpdate;
    int StartFromRootEveryNShots;
    int MaxConsecutiveFailedPellets;
    int MaxSolutions;
    uint32_t CsvSamplePeriod; // Every nth new block per thread will be printed to a CSV. Set to 0 to disable CSV export.
    std::filesystem::path M64Path;
    std::string CsvOutputDirectory;
    std::vector<std::filesystem::path> ResourcePaths;

    template <class TContainer, typename TElement = typename TContainer::value_type>
        requires std::is_same_v<TElement, std::string>
    void SetResourcePaths(const TContainer& container);
};

class Segment
{
public:
    std::shared_ptr<Segment> parent;
    uint64_t seed;
    uint8_t nScripts;
    uint8_t depth;

    bool operator==(const Segment&) const = default;

    Segment(std::shared_ptr<Segment> parent, uint64_t seed, uint8_t nScripts) : parent(parent), seed(seed), nScripts(nScripts)
    {
        if (parent == nullptr)
            depth = 0;
        else
            depth = parent->depth + 1;
    }
};

template <class TState>
class Block
{
public:
    std::shared_ptr<Segment> tailSegment;
    TState stateBin;
    float fitness;
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>,
    class TOutputState = DefaultState>
class Scattershot
{
public:
    const Configuration& config;
    friend class ScattershotThread<TState, TResource, TStateTracker, TOutputState>;

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

                    auto status = TopLevelScriptBuilder<TScattershotThread>::Build(m64)
                        .ConfigureResource<TResourceConfig>(resourceConfigGenerator(configuration.ResourcePaths[threadId]))
                        .Main(scattershot, threadId);
                }
            });
    }

    template <template<class, class, class> class TScattershotThread, class TResourceConfig, typename F>
        requires (derived_from_specialization_of<ScattershotThread<TState, TResource, TStateTracker, TOutputState>, TScattershotThread>
    && std::same_as<std::invoke_result_t<F, std::filesystem::path>, TResourceConfig>)
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

                    auto status = TopLevelScriptBuilder<TScattershotThread<TState, TResource, TStateTracker, TOutputState>>::Build(m64)
                        .ConfigureResource<TResourceConfig>(resourceConfigGenerator(configuration.ResourcePaths[threadId]))
                        .Main(scattershot, threadId);
                }
            });
    }

    ~Scattershot();

private:
    // Global State
    std::vector<Block<TState>> Blocks;
    std::vector<int> BlockIndices; // Indexed by state bin hash

    std::string CsvFileName;
    std::ofstream Csv;
    int CsvSampleFrequency;
    int64_t CsvCounter = 0;
    int64_t CsvRows = -1; // Print this each merge so analysis can be run at the same time w/o dealing with partial rows
    bool CsvEnabled = false;

    uint64_t TotalShots = 0;
    uint64_t ScriptCount = 0;
    uint64_t FailedScripts = 0;
    uint64_t RedundantScripts = 0;
    uint64_t NovelScripts = 0;

    void PrintStatus();
    bool UpsertBlock(TState stateBin, float fitness, std::shared_ptr<Segment> parentSegment, uint8_t nScripts, uint64_t segmentSeed);

    template <typename T>
    uint64_t GetHash(const T& toHash, bool ignoreFillerBytes)
    {
        std::hash<std::byte> byteHasher;
        const auto* data = reinterpret_cast<const std::byte*>(&toHash);
        uint64_t hashValue = 0;
        for (std::size_t i = 0; i < sizeof(toHash); i++)
        {
            if (ignoreFillerBytes || !FillerBytes.contains(i))
                hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);
        }

        return hashValue;
    }

    void OpenCsv();

    template <typename F>
    void MultiThread(int nThreads, F func);

    static std::unordered_set<int> GetStateBinRuntimeFillerBytes()
    {
        std::unordered_set<int> fillerBytes;

        TState stateBin;
        std::byte* binPtr = reinterpret_cast<std::byte*>(&stateBin);

        // initialize to specific garbage data compatible with all primitives;
        for (int i = 0; i < sizeof(TState); i++)
            binPtr[i] = (std::byte)0x3f;

        // Check which bytes identity depends on
        TState stateBinCopy = stateBin;
        std::byte* binCopyPtr = reinterpret_cast<std::byte*>(&stateBin);
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

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    class TResourceConfig = DefaultResourceConfig,
    typename FResourceConfigGenerator = TResourceConfig(*)(std::filesystem::path)>
class ScattershotBuilder;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>,
    class TOutputState = DefaultState>
class ScattershotThread : public TopLevelScript<TResource, TStateTracker>
{
public:
    using TopLevelScript<TResource, TStateTracker>::MainConfig;

    static ScattershotBuilder<TState, TResource, TStateTracker, TOutputState> ConfigureScattershot(const Configuration& config)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState>(config);
    }

protected:
    //friend class Scattershot<TState, TResource, TStateTracker, TOutputState>;

    // Using directives needed for MSVC >:(
    using Script<TResource>::LongLoad;
    using Script<TResource>::ExecuteAdhoc;
    using Script<TResource>::ModifyAdhoc;

    const Configuration& config;

    ScattershotThread(Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot, int id);

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
    Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot;
    int Id;
    uint64_t RngHash = 0;
    uint64_t RngHashTemp = 0;
    TState BaseBlockStateBin;
    std::shared_ptr<Segment> BaseBlockTailSegment = nullptr;
    std::unordered_set<MovementOption> movementOptions;

    short startCourse;
    short startArea;

    // Thread state methods
    uint64_t GetRng();
    void SetRng(uint64_t rngHash);
    void SetTempRng(uint64_t rngHash);
    void SelectBaseBlock(int mainIteration);
    bool ValidateBaseBlock(int shot);

    void AddCsvRow(int shot);
    void AddCsvLabels();

    template <typename F>
    static void SingleThread(F func)
    {
        #pragma omp barrier
        {
            if (omp_get_thread_num() == 0)
                func();
        }
        #pragma omp barrier

        return;
    }

    template <typename F>
    static void ThreadLock(F func)
    {
        #pragma omp critical
        {
            func();
        }

        return;
    }

    template <typename F>
    static void QueueThreadById(F func)
    {
        #pragma omp barrier

        int nThreads = omp_get_num_threads();
        for (int i = 0; i < nThreads; i++)
        {
            if (omp_get_thread_num() == i)
                func();

            #pragma omp barrier
            continue;
        }

        return;
    }

    bool ValidateCourseAndArea();
    bool ChooseScriptAndApply();
    TState GetStateBinSafe();
    float GetStateFitnessSafe();
    AdhocBaseScriptStatus DecodeBaseBlockDiffAndApply();
    AdhocBaseScriptStatus ExecuteFromBaseBlockAndEncode(int shot);

    template <typename T>
    uint64_t GetHash(const T& toHash) const;
};

//void DefaultResourceConfigGenerator() {}

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    class TResourceConfig,
    typename FResourceConfigGenerator>
class ScattershotBuilder
{
public:
    ScattershotBuilder(const Configuration& config) : _config(config) {} // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    ScattershotBuilder(const Configuration& config, FResourceConfigGenerator callback) : _config(config), _resourceConfigGenerator(callback) {}

    template <class UResourceConfig, typename GResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<GResourceConfigGenerator, std::filesystem::path>, UResourceConfig>)
    ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator> ConfigureResourcePerPath(GResourceConfigGenerator callback)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator>(_config, callback);
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread>
    void Run()
    {
        Scattershot<TState, TResource, TStateTracker, TOutputState>::Run<TScattershotThread, TResourceConfig>(_config, _resourceConfigGenerator);
    }

private:
    const Configuration& _config;
    FResourceConfigGenerator _resourceConfigGenerator;
};

//Include template method implementations
#include "Scattershot.t.hpp"
#include "ScattershotThread.t.hpp"

#endif
