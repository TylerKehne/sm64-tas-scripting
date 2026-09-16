#pragma once

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <array>
#include <atomic>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <cmath>
#include <BinaryStateBin.hpp>
#include <tasfw/Script.hpp>
#include <tasfw/Concepts.hpp>
#include <omp.h>
#include <immintrin.h>
#include <vector>
#include <filesystem>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <BasicMoves.hpp>
#include <algorithm>
#include <functional>
#include <Configuration.hpp>
#include <Segment.hpp>
#include <Block.hpp>
#include <ScattershotSolution.hpp>

#define OMP_STRINGIFY(content) #content
#define OMP_CRITICAL(name) _Pragma(OMP_STRINGIFY(omp critical(name)))

#ifndef SCATTERSHOT_H
#define SCATTERSHOT_H

// The per-byte hash behind every hash the search takes: the block table's state-bin hash
// and the RNG chain (GetRng / GetTempRng advance by hashing their previous value). It is
// FNV-1a over the one byte, which is exactly what MSVC's std::hash<std::byte> computes, so
// every search recorded on Windows and every committed count stays what it was. It is the
// framework's own function because std::hash<std::byte> is not the same function in every
// standard library (libstdc++ hashes a byte to itself), which made the same seed take a
// different search path on Linux than on Windows (docs/compilers.md).
constexpr uint64_t HashByte(std::byte b)
{
    return (14695981039346656037ull ^ uint64_t(b)) * 1099511628211ull;
}

class CriticalRegions
{
public:
    inline static const char* Print = "print";
    inline static const char* Solutions = "solutions";
    inline static const char* TotalShots = "totalshots";
    inline static const char* Blocks = "blocks";
    inline static const char* CsvCounters = "csvcounters";
    inline static const char* CsvExport = "csvexport";
    inline static const char* ScriptCounters = "scriptcounters";
};

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState>
class Scattershot;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState>
class ScattershotThread;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    typename... TMetricScriptParams>
class ScattershotBuilder;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    class TResourceConfig = DefaultResourceConfig,
    typename FResourceConfigGenerator = TResourceConfig(*)(int),
    typename... TMetricScriptParams>
class ScattershotBuilderConfig;

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    typename FResourceImportGenerator = TResource*(*)(int),
    typename... TMetricScriptParams>
class ScattershotBuilderImport;

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript = DefaultMetricScript<TResource>,
    class TOutputState = DefaultState>
class Scattershot
{
public:
    const Configuration& config;

    Scattershot(const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions);
    ~Scattershot();

    // How a search starts, from a configuration or from imported resources (the builders call
    // these).
    template <std::derived_from<ScattershotThread<TState, TResource, TMetricScript, TOutputState>> TScattershotThread,
        class TResourceConfig, typename F, typename... TParams, typename... TMetricScriptParams>
        requires std::same_as<std::invoke_result_t<F, int>, TResourceConfig>
    static std::vector<ScattershotSolution<TOutputState>> RunConfig(
        const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions, F resourceConfigGenerator,
        std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, TParams&&... params)
    {
        return RunBase<TScattershotThread>(configuration, inputSolutions,
            [&](Scattershot<TState, TResource, TMetricScript, TOutputState>& scattershot, M64& m64, int threadId)
            {
                return std::apply(
                    [&]<typename... Ts>(Ts&&... args) -> ScriptStatus<TScattershotThread>
                    {
                        return TopLevelScriptBuilder<TScattershotThread>::Build(m64)
                            .template ConfigureResource<TResourceConfig>(resourceConfigGenerator(threadId)) // `template`: dependent object (docs/compilers.md)
                            .ConfigureMetricScript(std::forward<Ts>(args)...)
                            .Run(scattershot, std::forward<TParams>(params)...);
                    }, *metricScriptParams);
            });
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TMetricScript, TOutputState>> TScattershotThread,
        typename F, typename... TParams, typename... TMetricScriptParams>
        requires std::same_as<std::invoke_result_t<F, int>, TResource*>
    static std::vector<ScattershotSolution<TOutputState>> RunImport(
        const Configuration& configuration, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions, F resourceImportGenerator,
        std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, TParams&&... params)
    {
        return RunBase<TScattershotThread>(configuration, inputSolutions,
            [&](Scattershot<TState, TResource, TMetricScript, TOutputState>& scattershot, M64& m64, int threadId)
            {
                return std::apply(
                    [&]<typename... Ts>(Ts&&... args) -> ScriptStatus<TScattershotThread>
                    {
                        return TopLevelScriptBuilder<TScattershotThread>::Build(m64)
                            .template ImportResource<TResource>(resourceImportGenerator(threadId)) // `template`: dependent object (docs/compilers.md)
                            .ConfigureMetricScript(std::forward<Ts>(args)...)
                            .Run(scattershot, std::forward<TParams>(params)...);
                    }, *metricScriptParams);
            });
    }

private:
    friend class ScattershotThread<TState, TResource, TMetricScript, TOutputState>;
    friend class PerfAccess; // tasfw-perf benchmarks and tasfw-tests (tasfw/testing/PerfAccess.hpp); see docs/performance.md

    // Global State
    // The deterministic mode's queue (ROADMAP 3.8). Every thread numbers its calls of
    // QueueThreadById; call k of thread i is ticket k * threads + i, and QueueTurn is the
    // ticket being served, so the calls run in the same total order the barriers gave them
    // (round by round, threads in order) but a thread only waits at its own upsert, not for
    // every other thread to finish its script first. Only the holder of the turn writes it.
    // A thread that has no more calls retires under its turn; holders skip retired threads.
    std::atomic<uint64_t> QueueTurn { 0 };
    std::vector<char> QueueRetired;
    std::vector<Block<TState>> Blocks;
    std::vector<int> BlockIndices; // Indexed by state bin hash
    std::map<int, ScattershotSolution<TOutputState>> Solutions;
    const std::vector<ScattershotSolution<TOutputState>>& InputSolutions;

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
    // Shots whose decoded base block did not reproduce the block's state bin (ROADMAP 4.5).
    // A non-zero count means replaying a segment chain is not a pure function of its seeds.
    uint64_t ValidationFailures = 0;

    // The search: the run, its threads, the block table, the status line and the CSV.
    template <std::derived_from<ScattershotThread<TState, TResource, TMetricScript, TOutputState>> TScattershotThread, typename F>
        requires std::same_as<std::invoke_result_t<F, Scattershot<TState, TResource, TMetricScript, TOutputState>&, M64&, int>, ScriptStatus<TScattershotThread>>
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
                if (threadId < int(configuration.ResourcePaths.size()))
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

        // Parsed by scripts/perf.ps1 (Tier D); keep the format if you change it.
        printf("Found %llu solutions in %llu shots, %llu blocks, %llu scripts (%llu base-block validation failures).\n",
            (unsigned long long)scattershot.Solutions.size(), (unsigned long long)scattershot.TotalShots,
            (unsigned long long)scattershot.Blocks.size(), (unsigned long long)scattershot.ScriptCount,
            (unsigned long long)scattershot.ValidationFailures);

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
        int totalSeconds = int(std::chrono::duration_cast<std::chrono::seconds>(finish - start).count());

        int loadPercent = int(double(loadDuration) / double(totalDuration) * 100);
        int savePercent = int(double(saveDuration) / double(totalDuration) * 100);
        int advancePercent = int(double(advanceFrameDuration) / double(totalDuration) * 100);
        int otherPercent = int(double(scriptDuration - (loadDuration + saveDuration + advanceFrameDuration)) / double(totalDuration) * 100);
        int overheadPercent = int(double(totalDuration - scriptDuration) / double(totalDuration) * 100);

        printf("Total time (seconds): %d\n", totalSeconds);
        printf("Load: %d%% Save: %d%% Frame Advance: %d%% Overhead: %d%% Other: %d%%\n", loadPercent, savePercent, advancePercent, overheadPercent, otherPercent);

        return solutions;
    }

    template <typename F>
    void MultiThread(int nThreads, F func);
    bool UpsertBlock(TState stateBin, bool isSolution, ScattershotSolution<TOutputState> solution, float fitness,
        std::shared_ptr<Segment> parentSegment, uint8_t nScripts, uint64_t segmentSeed, uint16_t pipedDiff1Index);

    template <typename T>
    uint64_t GetHash(const T& toHash, bool ignoreFillerBytes)
    {
        const auto* data = reinterpret_cast<const std::byte*>(&toHash);
        uint64_t hashValue = 0;
        for (std::size_t i = 0; i < sizeof(toHash); i++)
        {
            if (ignoreFillerBytes || !FillerBytes.contains(int(i)))
                hashValue ^= HashByte(data[i]) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);
        }

        return hashValue;
    }
    void PrintStatus();
    void OpenCsv();

    static std::unordered_set<int> GetStateBinRuntimeFillerBytes()
    {
        std::unordered_set<int> fillerBytes;

        TState stateBin;
        std::byte* binPtr = reinterpret_cast<std::byte*>(&stateBin);

        // initialize to specific garbage data compatible with all primitives;
        for (int i = 0; i < int(sizeof(TState)); i++)
            binPtr[i] = (std::byte)0x3f;

        // Check which bytes identity depends on
        TState stateBinCopy = stateBin;
        std::byte* binCopyPtr = reinterpret_cast<std::byte*>(&stateBin);
        for (int i = 0; i < int(sizeof(TState)); i++)
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

//Include template method implementations
#include "Scattershot.t.hpp"

// The thread and the builders complete what Scattershot declares (the thread is its friend and
// runs its search, the builders start one), so a search's translation unit has all three; the
// two headers are not included on their own.
#include <ScattershotThread.hpp>
#include <ScattershotBuilder.hpp>

#endif
