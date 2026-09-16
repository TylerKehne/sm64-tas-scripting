#pragma once
#ifndef SCATTERSHOT_H
#error "Scattershot.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <typename F>
void Scattershot<TState, TResource, TStateTracker, TOutputState>::MultiThread(int nThreads, F func)
{
    omp_set_num_threads(nThreads);
    QueueTurn.store(0);
    QueueRetired.assign(size_t(nThreads), 0);
    #pragma omp parallel
    {
        // A thread that finishes retires from the deterministic queue at the end of its
        // execution(), so the others never wait for it.
        func();
    }

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Scattershot<TState, TResource, TStateTracker, TOutputState>::Scattershot(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    : config(config), InputSolutions(inputSolutions)
{
    Blocks.reserve(config.MaxBlocks);
    BlockIndices.reserve(3 * config.MaxBlocks);

    for (int i = 0; i < int(BlockIndices.capacity()); i++)
        BlockIndices.push_back(-1);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool Scattershot<TState, TResource, TStateTracker, TOutputState>::UpsertBlock(
    TState stateBin, bool isSolution, ScattershotSolution<TOutputState> solution, float fitness,
    std::shared_ptr<Segment> parentSegment, uint8_t nScripts, uint64_t segmentSeed, uint16_t pipedDiff1Index)
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
            if (isSolution && int64_t(Solutions.size()) >= config.MaxSolutions)
                return false;

            blockIndex = int(Blocks.size());
            Blocks.emplace_back(std::make_shared<Segment>(parentSegment, segmentSeed, nScripts, pipedDiff1Index), stateBin, fitness);
            BlockIndices[stateBinHash % BlockIndices.size()] = blockIndex;

            if (isSolution)
            {
                #pragma omp critical (solutions)
                {
                    Solutions[blockIndex] = solution;
                }
            }

            return true;
        }

        if (Blocks[blockIndex].stateBin == stateBin && pipedDiff1Index == 0) // False indicates a hash collision or piped-in diff
        {
            // Override fitness check if this block is a new solution
            if ((config.FitnessTieGoesToNewBlock && fitness == Blocks[blockIndex].fitness)
                || fitness > Blocks[blockIndex].fitness
                || (isSolution && !Solutions.contains(blockIndex)))
            {
                // Reject improvements that are not considered solutions if the incumbent is a solution
                if (Solutions.contains(blockIndex) && !isSolution)
                    return false;

                Blocks[blockIndex].fitness = fitness;
                Blocks[blockIndex].tailSegment = std::make_shared<Segment>(parentSegment, segmentSeed, nScripts, pipedDiff1Index);

                if (isSolution && int64_t(Solutions.size()) < config.MaxSolutions)
                {
                    #pragma omp critical (solutions)
                    {
                        Solutions[blockIndex] = solution;
                    }
                }

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
    printf("\nCombined Loops: %llu Blocks: %llu Solutions: %llu\n", (unsigned long long)TotalShots, (unsigned long long)Blocks.size(), (unsigned long long)Solutions.size());

    // Print cumulative script results
    if (ScriptCount != 0)
    {
        #pragma omp critical (scriptcounters)
        {
            int futility = int(double(FailedScripts) / double(ScriptCount) * 100);
            int redundancy = int(double(RedundantScripts) / double(ScriptCount) * 100);
            int discovery = int(double(NovelScripts) / double(ScriptCount) * 100);

            printf("Futility: %d%% Redundancy: %d%% Discovery: %d%%", futility, redundancy, discovery);
            if (ValidationFailures != 0)
                printf(" Validation failures: %llu", (unsigned long long)ValidationFailures);
            printf("\n");
        }
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
        std::cout << "CSV file name: " << CsvFileName << "\n";
    }
    else
        std::cout << "Unable to create CSV file.";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Scattershot<TState, TResource, TStateTracker, TOutputState>::~Scattershot()
{
    Csv.close();
}

#endif
