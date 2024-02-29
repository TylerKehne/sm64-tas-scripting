#pragma once

#include <concepts>
#include <stdexcept>
#include <cmath>
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

#define OMP_STRINGIFY(content) #content
#define OMP_CRITICAL(name) _Pragma(OMP_STRINGIFY(omp critical(name)))

#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

class CriticalRegions
{
public:
    inline static const char* Print = "print";
    inline static const char* Solutions = "solutions";
    inline static const char* TotalShots = "totalshots";
    inline static const char* Blocks = "blocks";
    inline static const char* CsvCounters = "csvcounters";
    inline static const char* CsvExport = "csvexport";
    inline static const char* InputSolutions = "inputsolutions";
    inline static const char* ScriptCounters = "scriptcounters";
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
class Scattershot;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
class ScattershotThread;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    typename... TStateTrackerParams>
class ScattershotBuilder;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    class TResourceConfig = DefaultResourceConfig,
    typename FResourceConfigGenerator = TResourceConfig(*)(int),
    typename... TStateTrackerParams>
class ScattershotBuilderConfig;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    typename FResourceImportGenerator = TResource*(*)(int),
    typename... TStateTrackerParams>
class ScattershotBuilderImport;

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
    int Seed;
    bool FitnessTieGoesToNewBlock;
    bool Deterministic;
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
    uint16_t pipedDiff1Index = 0;

    bool operator==(const Segment&) const = default;

    Segment(std::shared_ptr<Segment> parent, uint64_t seed, uint8_t nScripts, uint16_t pipedDiff1Index)
        : parent(parent), seed(seed), nScripts(nScripts), pipedDiff1Index(pipedDiff1Index)
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

template <class TOutputState>
class ScattershotSolution
{
public:
    TOutputState data;
    M64Diff m64Diff;

    ScattershotSolution(TOutputState data, M64Diff m64Diff) : data(data), m64Diff(m64Diff) {}

    ScattershotSolution() = default;
    ScattershotSolution(const ScattershotSolution&) = default;
    ScattershotSolution& operator=(const ScattershotSolution&) = default;
    ScattershotSolution(ScattershotSolution&&) noexcept = default;
    ScattershotSolution& operator=(ScattershotSolution&&) noexcept = default;
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>,
    class TOutputState = DefaultState>
class Scattershot
{
public:
    const Configuration& config;
    friend class ScattershotThread<TState, TResource, TStateTracker, TOutputState>;

    Scattershot(const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions);

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread,
        class TResourceConfig, typename F, typename... TParams, typename... TStateTrackerParams>
        requires std::same_as<std::invoke_result_t<F, int>, TResourceConfig>
    static std::vector<ScattershotSolution<TOutputState>> RunConfig(
        const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions, F resourceConfigGenerator,
        std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TParams&&... params)
    {
        return RunBase<TScattershotThread>(configuration, inputSolutions,
            [&](Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot, M64& m64, int threadId)
            {
                return std::apply(
                    [&]<typename... Ts>(Ts&&... args) -> ScriptStatus<TScattershotThread>
                    {
                        return TopLevelScriptBuilder<TScattershotThread>::Build(m64)
                            .ConfigureResource<TResourceConfig>(resourceConfigGenerator(threadId))
                            .ConfigureStateTracker(std::forward<Ts>(args)...)
                            .Run(scattershot, std::forward<TParams>(params)...);
                    }, *stateTrackerParams);
            });
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread,
        typename F, typename... TParams, typename... TStateTrackerParams>
        requires std::same_as<std::invoke_result_t<F, int>, TResource*>
    static std::vector<ScattershotSolution<TOutputState>> RunImport(
        const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions, F resourceImportGenerator,
        std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams, TParams&&... params)
    {
        return RunBase<TScattershotThread>(configuration, inputSolutions,
            [&](Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot, M64& m64, int threadId)
            {
                return std::apply(
                    [&]<typename... Ts>(Ts&&... args) -> ScriptStatus<TScattershotThread>
                    {
                        return TopLevelScriptBuilder<TScattershotThread>::Build(m64)
                            .ImportResource<TResource>(resourceImportGenerator(threadId))
                            .ConfigureStateTracker(std::forward<Ts>(args)...)
                            .Run(scattershot, std::forward<TParams>(params)...);
                    }, *stateTrackerParams);
            });
    }

    ~Scattershot();

private:
    // Global State
    std::unordered_set<int> ActiveThreads;
    std::vector<Block<TState>> Blocks;
    std::vector<int> BlockIndices; // Indexed by state bin hash
    std::map<int, ScattershotSolution<TOutputState>> Solutions;
    const std::vector<ScattershotSolution<TOutputState>>& InputSolutions;
    uint16_t InputSolutionsIndex = 0;

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
    bool UpsertBlock(TState stateBin, bool isSolution, ScattershotSolution<TOutputState> solution, float fitness,
        std::shared_ptr<Segment> parentSegment, uint8_t nScripts, uint64_t segmentSeed, uint16_t pipedDiff1Index);

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

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread, typename F>
        requires std::same_as<std::invoke_result_t<F, Scattershot<TState, TResource, TStateTracker, TOutputState>&, M64&, int>, ScriptStatus<TScattershotThread>>
    static std::vector<ScattershotSolution<TOutputState>> RunBase(const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions, F scriptRunner)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto scattershot = Scattershot(configuration, inputSolutions);
        scattershot.OpenCsv();

        std::vector<ScriptStatus<TScattershotThread>> statuses;
        std::vector<uint64_t> totalCycleCounts;
        scattershot.MultiThread(configuration.TotalThreads, [&]()
            {
                int threadId = omp_get_thread_num();
                if (threadId < configuration.ResourcePaths.size())
                {
                    M64 m64 = M64(configuration.M64Path);
                    m64.load();

                    auto startCycles = get_time();
                    ScriptStatus<TScattershotThread> status = scriptRunner(scattershot, m64, threadId);
                    auto finishCycles = get_time();
                    
                    #pragma omp critical
                    {
                        totalCycleCounts.push_back(finishCycles - startCycles);
                        statuses.push_back(std::move(status));
                    }
                }
            });

        printf("Found %d solutions in %d shots.\n", scattershot.Solutions.size(), scattershot.TotalShots);

        std::vector<ScattershotSolution<TOutputState>> solutions;
        solutions.reserve(scattershot.Solutions.size());
        for (auto& pair : scattershot.Solutions)
            solutions.push_back(std::move(pair.second));

        uint64_t loadDuration = 0;
        uint64_t saveDuration = 0;
        uint64_t advanceFrameDuration = 0;
        uint64_t scriptDuration = 0;
        uint64_t totalDuration = 0;

        for (auto& status : statuses)
        {
            loadDuration += status.loadDuration;
            saveDuration += status.saveDuration;
            advanceFrameDuration += status.advanceFrameDuration;
            scriptDuration += status.totalDuration;
        }

        for (auto& cycleCount : totalCycleCounts)
        {
            totalDuration += cycleCount;
        }

        auto finish = std::chrono::high_resolution_clock::now();
        int totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(finish - start).count();

        int loadPercent = double(loadDuration) / double(totalDuration) * 100;
        int savePercent = double(saveDuration) / double(totalDuration) * 100;
        int advancePercent = double(advanceFrameDuration) / double(totalDuration) * 100;
        int otherPercent = double(scriptDuration - (loadDuration + saveDuration + advanceFrameDuration)) / double(totalDuration) * 100;
        int overheadPercent = double(totalDuration - scriptDuration) / double(totalDuration) * 100;

        printf("Total time (seconds): %d\n", totalSeconds);
        printf("Load: %d%% Save: %d%% Frame Advance: %d%% Overhead: %d%% Other: %d%%\n", loadPercent, savePercent, advancePercent, overheadPercent, otherPercent);

        return solutions;
    }

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



template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker = DefaultStateTracker<TResource>,
    class TOutputState = DefaultState>
class ScattershotThread : public TopLevelScript<TResource, TStateTracker>
{
public:
    using TopLevelScript<TResource, TStateTracker>::MainConfig;

    static ScattershotBuilder<TState, TResource, TStateTracker, TOutputState> ConfigureScattershot(const Configuration& config)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState>(config, nullptr);
    }

    /*
    template <typename F>
    static void ThreadLock(F func)
    {
        #pragma omp critical
        {
            func();
        }

        return;
    }
    */

    template <typename F>
    static void ThreadLock(const char* section, F func)
    {
        OMP_CRITICAL(section)
        {
            func();
        }

        return;
    }

protected:
    //friend class Scattershot<TState, TResource, TStateTracker, TOutputState>;

    // Using directives needed for MSVC >:(
    using Script<TResource>::LongLoad;
    using Script<TResource>::ExecuteAdhoc;
    using Script<TResource>::ModifyAdhoc;

    const Configuration& config;

    ScattershotThread(Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot);

    virtual bool validation();
    bool execution();
    virtual bool assertion();

    virtual void SelectMovementOptions() = 0;
    virtual bool ApplyMovement() = 0;
    virtual TState GetStateBin() = 0;
    virtual bool ValidateState() = 0;
    virtual float GetStateFitness() = 0;

    virtual TOutputState GetSolutionState() { return TOutputState(); }
    virtual bool IsSolution() { return false; };
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
    void Initialize();
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
    static void QueueThreadById(bool deterministic, F func)
    {
        if (!deterministic)
        {
            func();
            return;
        }

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
    typename... TStateTrackerParams>
class ScattershotBuilder
{
public:
    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions)
        : _config(config), _inputSolutions(inputSolutions) // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    {
        _stateTrackerParams = std::make_shared<std::tuple<>>();
    } 

    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : _config(config), _inputSolutions(inputSolutions), _stateTrackerParams(stateTrackerParams)  // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    { }

    template <class TResourceConfig, typename FResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceConfigGenerator, int>, TResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...> ConfigureResourcePerThread(FResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>(_config, callback, _inputSolutions, _stateTrackerParams);
    }

    template <typename FResourceImportGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceImportGenerator, int>, TResource*>)
    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...> ImportResourcePerThread(FResourceImportGenerator callback)
    {
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...>(_config, callback, _inputSolutions, _stateTrackerParams);
    }

    ScattershotBuilder<TState, TResource, TStateTracker, TOutputState> PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(_config, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UStateTrackerParams...> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UStateTrackerParams...>(
            _config, _inputSolutions, std::make_shared(std::make_tuple(std::forward<UStateTrackerParams>(stateTrackerParams)...)));
    }

protected:
    const Configuration& _config;
    const std::vector<ScattershotSolution<TOutputState>>* _inputSolutions;
    std::shared_ptr<std::tuple<TStateTrackerParams...>> _stateTrackerParams = nullptr;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    class TResourceConfig,
    typename FResourceConfigGenerator,
    typename... TStateTrackerParams>
class ScattershotBuilderConfig : public ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_config;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_stateTrackerParams;

    ScattershotBuilderConfig(const Configuration& config, FResourceConfigGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(
            config, inputSolutions, stateTrackerParams), _resourceConfigGenerator(callback) {}

    template <class UResourceConfig, typename GResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<GResourceConfigGenerator, int>, UResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator, TStateTrackerParams...>
        ConfigureResourcePerThread(GResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator, TStateTrackerParams...>(
            _config, callback, _inputSolutions, _stateTrackerParams);
    }

    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>(
            _config, _resourceConfigGenerator, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, UStateTrackerParams...>
        ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
            std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, UStateTrackerParams...>(
            _config, _resourceConfigGenerator, _inputSolutions, tuplePtr);
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TStateTracker, TOutputState>::template RunConfig<TScattershotThread, TResourceConfig>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceConfigGenerator, _stateTrackerParams, std::forward<TParams>(params)...);
    }

private:
    FResourceConfigGenerator _resourceConfigGenerator;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    typename FResourceImportGenerator,
    typename... TStateTrackerParams>
class ScattershotBuilderImport : public ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_config;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_stateTrackerParams;

    ScattershotBuilderImport(const Configuration& config, FResourceImportGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(
            config, inputSolutions, stateTrackerParams), _resourceImportGenerator(callback) {}

    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator>(
            _config, _resourceImportGenerator, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, UStateTrackerParams...>
        ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
            std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, UStateTrackerParams...>(
            _config, _resourceImportGenerator, _inputSolutions, tuplePtr);
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TStateTracker, TOutputState>::template RunImport<TScattershotThread>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceImportGenerator, _stateTrackerParams, std::forward<TParams>(params)...);
    }

private:
    FResourceImportGenerator _resourceImportGenerator;
};

//Include template method implementations
#include "Scattershot.t.hpp"
#include "ScattershotThread.t.hpp"

#endif
