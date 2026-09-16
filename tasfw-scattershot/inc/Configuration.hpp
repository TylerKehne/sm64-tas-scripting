#pragma once
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

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

//Include template method implementations
#include "Configuration.t.hpp"

#endif
