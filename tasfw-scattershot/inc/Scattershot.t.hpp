#pragma once
#ifndef SCATTERSHOT_H
#error "Scattershot.t.hpp should only be included by Scattershot.hpp"
#else

/*
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
*/

template <class TContainer, typename TElement>
    requires std::is_same_v<TElement, std::string>
void Configuration::SetResourcePaths(const TContainer& container)
{
    for (const std::string& item : container)
    {
        ResourcePaths.emplace_back(item);
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <typename F>
void Scattershot<TState, TResource, TStateTracker, TOutputState>::MultiThread(int nThreads, F func)
{
    omp_set_num_threads(nThreads);
    #pragma omp parallel
    {
        int threadId = omp_get_thread_num();
        #pragma omp critical
        {
            ActiveThreads.insert(threadId);
        }

        func();

        // Since threads can exit at different times, this ensures the remaining threads will not get stuck at barrier directives
        while (!ActiveThreads.empty())
        {
            #pragma omp critical
            {
                ActiveThreads.erase(threadId);
            }

            #pragma omp barrier
            continue;
        }
    }

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Scattershot<TState, TResource, TStateTracker, TOutputState>::Scattershot(const Configuration& config) : config(config)
{
    Blocks.reserve(config.MaxBlocks);
    BlockIndices.reserve(3 * config.MaxBlocks);

    for (int i = 0; i < BlockIndices.capacity(); i++)
        BlockIndices.push_back(-1);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool Scattershot<TState, TResource, TStateTracker, TOutputState>::UpsertBlock(
    TState stateBin, bool isSolution, ScattershotSolution<TOutputState> solution, float fitness, std::shared_ptr<Segment> parentSegment, uint8_t nScripts, uint64_t segmentSeed)
{
    if (Blocks.size() == Blocks.capacity())
        throw std::runtime_error("Block cap reached");

    uint64_t stateBinHash = GetHash(stateBin, false);
    while (true)
    {
        int blockIndex = BlockIndices[stateBinHash % BlockIndices.size()];
        // Check for hash collision
        if (blockIndex == -1) 
        {
            if (isSolution && Solutions.size() >= config.MaxSolutions)
                return false;

            blockIndex = Blocks.size();
            Blocks.emplace_back(std::make_shared<Segment>(parentSegment, segmentSeed, nScripts), stateBin, fitness);
            BlockIndices[stateBinHash % BlockIndices.size()] = blockIndex;

            if (isSolution)
                Solutions[blockIndex] = solution;

            return true;
        }

        if (Blocks[blockIndex].stateBin == stateBin) // False indicates a hash collision
        {
            if (fitness > Blocks[blockIndex].fitness)
            {
                // Reject improvements that are not considered solutions if the incumbent is a solution
                if (Solutions.contains(blockIndex) && !isSolution)
                    return false;

                Blocks[blockIndex].fitness = fitness;
                Blocks[blockIndex].tailSegment = std::make_shared<Segment>(parentSegment, segmentSeed, nScripts);

                if (isSolution)
                    Solutions[blockIndex] = solution;

                return true;
            }

            return false;
        }

        stateBinHash = GetHash(stateBinHash, true); // Re-hash due to collision
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void Scattershot<TState, TResource, TStateTracker, TOutputState>::PrintStatus()
{
    printf("\nCombined Loops: %d Blocks: %d Solutions: %d\n", TotalShots, Blocks.size(), Solutions.size());

    // Print cumulative script results
    if (ScriptCount != 0)
    {
        int futility = double(FailedScripts) / double(ScriptCount) * 100;
        int redundancy = double(RedundantScripts) / double(ScriptCount) * 100;
        int discovery = double(NovelScripts) / double(ScriptCount) * 100;

        printf("Futility: %d%% Redundancy: %d%% Discovery: %d%%\n", futility, redundancy, discovery);
    }

    if (CsvRows != -1)
    {
        Csv.flush();
        printf("CSV rows: %u\n", (unsigned int)CsvRows);
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void Scattershot<TState, TResource, TStateTracker, TOutputState>::OpenCsv()
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
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Scattershot<TState, TResource, TStateTracker, TOutputState>::~Scattershot()
{
    Csv.close();
}

#endif
