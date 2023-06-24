#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotThread.t.hpp should only be included by Scattershot.hpp"
#else
#include <Scattershot.hpp>

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ScattershotThread(Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot)
    : scattershot(scattershot), config(scattershot.config)
{
    Id = omp_get_thread_num();
    SetRng((uint64_t)(Id + 173) * 5786766484692217813);
    
    //printf("Thread %d\n", Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::validation() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::execution()
{
    Initialize();

    uint64_t totalShots = 0;
    for (int shot = 0; totalShots <= config.MaxShots; shot++)
    {
        // Pick a block to "fire a shot" at
        ThreadLock(CriticalRegions::Blocks, [&]() { SelectBaseBlock(shot); });

        auto status = ExecuteAdhoc([&]()
            {
                DecodeBaseBlockDiffAndApply();

                if (!ValidateBaseBlock(shot))
                    return false;

                this->Save();
                int consecutiveFailedPellets = 0;
                for (int segment = 0; segment < config.PelletsPerShot && consecutiveFailedPellets < config.MaxConsecutiveFailedPellets; segment++)
                {
                    if (ExecuteFromBaseBlockAndEncode(shot).executed)
                        consecutiveFailedPellets = 0;
                    else
                        consecutiveFailedPellets++;
                }
                    
                return true;
            });

        size_t nSolutions = 0;
        ThreadLock(CriticalRegions::Print, [&]()
            {
                ThreadLock(CriticalRegions::Solutions, [&]() { nSolutions = scattershot.Solutions.size(); });
                ThreadLock(CriticalRegions::TotalShots, [&]() { totalShots = ++scattershot.TotalShots; });

                // Periodically print progress to console
                if (totalShots % config.ShotsPerUpdate == 0)
                    scattershot.PrintStatus();
            });

        //printf("%d %d %d %d\n", status.nLoads, status.nSaves, status.nFrameAdvances, status.executionDuration);

        if (config.MaxSolutions > 0 && nSolutions >= config.MaxSolutions)
            return true;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::Initialize()
{
    LongLoad(config.StartFrame);

    // Load piped-in diffs as root blocks
    if (!scattershot.InputSolutions.empty())
    {
        bool finishedProcessingDiffs = false;
        uint16_t inputSolutionsIndex = 0;
        std::shared_ptr<Segment> rootSegment = std::make_shared<Segment>(nullptr, 0, RngHash, 0);
        while (true)
        {
            ThreadLock(CriticalRegions::InputSolutions, [&]()
                {
                    inputSolutionsIndex = scattershot.InputSolutionsIndex++;
                    finishedProcessingDiffs = inputSolutionsIndex >= scattershot.InputSolutions.size() || inputSolutionsIndex >= 65534;
                    if (finishedProcessingDiffs)
                        return;
                });

            // Execute diff and save block
            ExecuteAdhoc([&]()
                {
                    if (finishedProcessingDiffs)
                    {
                        QueueThreadById(config.Deterministic, [&]() {});
                        return true;
                    }

                    this->Apply(scattershot.InputSolutions[inputSolutionsIndex].m64Diff);
                    QueueThreadById(config.Deterministic, [&]()
                        {
                            ThreadLock(CriticalRegions::Blocks, [&]()
                                {
                                    scattershot.UpsertBlock(GetStateBinSafe(), false, ScattershotSolution<TOutputState>(),
                                    GetStateFitnessSafe(), rootSegment, 1, GetRng(), inputSolutionsIndex + 1);
                                });
                        });

                    return true;
                });

            if (finishedProcessingDiffs)
                break;
        }
    }

    SingleThread([&]()
        {
            // Initialize root block if no diffs are piped in
            if (scattershot.InputSolutions.empty())
                scattershot.UpsertBlock(GetStateBinSafe(), false, ScattershotSolution<TOutputState>(), GetStateFitnessSafe(), nullptr, 0, RngHash, 0);

            AddCsvLabels();
            scattershot.PrintStatus();
        });

    // Record start course/area for validation (generally scattershot has no cross-level value)
    startCourse = *(short*)this->resource->addr("gCurrCourseNum");
    startArea = *(short*)this->resource->addr("gCurrAreaIndex");
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
std::string ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetCsvLabels()
{
    return "";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ForceAddToCsv()
{
    return false;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddCsvLabels()
{
    ExecuteAdhoc([&]()
        {
            std::string labels = "Shot,Frame,Sampled," + GetCsvLabels();
            if (labels == "" || config.CsvSamplePeriod == 0)
                return false;

            scattershot.Csv << labels << "\n";
            scattershot.CsvRows = 0;
            AddCsvRow(0);

            return true;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
std::string ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetCsvRow()
{
    return "";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::assertion() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::SelectBaseBlock(int mainIteration)
{
    int blockIndex = -1;
    if (mainIteration % config.StartFromRootEveryNShots == 0)
    {
        if (scattershot.InputSolutions.empty())
            blockIndex = 0;
        else
            blockIndex = GetRng() % scattershot.InputSolutions.size();
    }
    else
    {
        while (true)
        {
            blockIndex = GetRng() % scattershot.Blocks.size();

            // Don't explore beyond known solutions
            bool isSolution;
            ThreadLock(CriticalRegions::Solutions, [&]() { isSolution = scattershot.Solutions.contains(blockIndex); });
            if (!isSolution)
                break;
        }
    }

    BaseBlockStateBin = scattershot.Blocks[blockIndex].stateBin;
    BaseBlockTailSegment = scattershot.Blocks[blockIndex].tailSegment;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ValidateBaseBlock(int shot)
{
    TState currentStateBin = GetStateBinSafe();
    if (BaseBlockStateBin != currentStateBin) {
        this->ExportM64("C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\analysis\\error.m64");
        cout << Id << " " << shot << "\n";
        //BaseBlockStateBin.print();
        //currentStateBin.print();
        cout << scattershot.GetHash(BaseBlockStateBin, false) << "\n";
        cout << scattershot.GetHash(currentStateBin, false) << "\n";
        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddCsvRow(int shot)
{
    bool sampled = false;
    ThreadLock(CriticalRegions::CsvCounters, [&]()
        {
            if (scattershot.CsvEnabled == false || scattershot.CsvRows == -1)
                return;

            if (scattershot.CsvCounter++ % config.CsvSamplePeriod == 0)
                sampled = true;
        });
    
    // Check if we should force an export for the current state
    if (!sampled && !ExecuteAdhoc([&]() { return ForceAddToCsv(); }).executed)
        return;

    // Get CSV row and validate cell count
    std::string row;
    bool rowValidated = ExecuteAdhoc([&]()
        {
            auto labels = GetCsvLabels();
            row = GetCsvRow();

            // Validate column count is the same
            int labelsColumns = std::count(labels.begin(), labels.end(), ',');
            int rowColumns = std::count(row.begin(), row.end(), ',');

            return labelsColumns == rowColumns;
        }).executed;

    ThreadLock(CriticalRegions::CsvExport, [&]()
        {
            if (!rowValidated)
            {
                ThreadLock(CriticalRegions::Print, [&]() { cout << "Unable to add row to CSV. Labels/Row have different column counts.\n"; });
                return;
            }

            // CSV row export retry loop
            auto failedRowPos = scattershot.Csv.tellp();
            for (int i = 0; i < 5; i++)
            {
                if (i > 0)
                {
                    ThreadLock(CriticalRegions::Print, [&]()
                        {
                            ThreadLock(CriticalRegions::CsvCounters, [&]() { cout << "Retrying CSV row " << scattershot.CsvRows << ".\n"; });
                        });
                }

                scattershot.Csv << shot << "," << this->GetCurrentFrame() << "," << sampled << "," << row << "\n";
                if (!scattershot.Csv.fail())
                {
                    ThreadLock(CriticalRegions::CsvCounters, [&]() { scattershot.CsvRows++; });
                    return;
                }
                else {
                    ThreadLock(CriticalRegions::Print, [&]() { cout << "Error writing to CSV row " << scattershot.CsvRows << ": " << scattershot.Csv.rdstate() << "\n"; });
                    scattershot.Csv.close();
                    scattershot.Csv = std::ofstream(scattershot.CsvFileName);
                    scattershot.Csv.seekp(failedRowPos);
                }
            }

            ThreadLock(CriticalRegions::Print, [&]() { cout << "Exceeded CSV export retry count. Disabling CSV export.\n"; });
            scattershot.CsvEnabled = false;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ValidateCourseAndArea()
{
    return startCourse == *(short*)this->resource->addr("gCurrCourseNum")
        && startArea == *(short*)this->resource->addr("gCurrAreaIndex");
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ChooseScriptAndApply()
{
    movementOptions = std::unordered_set<MovementOption>();

    ExecuteAdhoc([&]()
        {
            SelectMovementOptions();
            return true;
        });

    // Execute script and update rng hash
    auto status = ModifyAdhoc([&]() { return ApplyMovement(); });
    return status.executed && !status.m64Diff.frames.empty();
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
TState ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetStateBinSafe()
{
    TState stateBin;
    ExecuteAdhoc([&]()
        {
            stateBin = TState(GetStateBin());
            return true;
        });

    return stateBin;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
float ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetStateFitnessSafe()
{
    float fitness;
    ExecuteAdhoc([&]()
        {
            fitness = GetStateFitness();
            return true;
        });

    return fitness;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
AdhocBaseScriptStatus ScattershotThread<TState, TResource, TStateTracker, TOutputState>::DecodeBaseBlockDiffAndApply()
{
    int64_t postScriptFrame = -1;
    auto status = ModifyAdhoc([&]()
        {
            std::shared_ptr<Segment> tailSegment = BaseBlockTailSegment;
            std::vector<std::shared_ptr<Segment>> segments(tailSegment->depth);
            for (auto currentSegment = tailSegment; currentSegment->depth > 0; currentSegment = currentSegment->parent)
                segments[currentSegment->depth - 1] = currentSegment;

            for (auto& currentSegment : segments)
            {
                SetTempRng(currentSegment->seed);
                for (int script = 0; script < currentSegment->nScripts; script++)
                {
                    if (currentSegment->pipedDiff1Index > 0)
                        this->Apply(scattershot.InputSolutions[currentSegment->pipedDiff1Index - 1].m64Diff);
                    else
                        ChooseScriptAndApply();

                    // This is here so the queued upserts don't block on the entire decoding
                    QueueThreadById(config.Deterministic, [&]() {});
                }
                
            }

            postScriptFrame = this->GetCurrentFrame();
            return true;
        });

    // Needed to sync with original execution (block is saved after individual script diff is applied).
    // Note that this often does nothing. It does not hurt performance unless it rewinds.
    // TODO: Consider changing TASFW Modify methods to persist frame cursor so this isn't necessary
    this->Load(postScriptFrame);
    return status;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
AdhocBaseScriptStatus ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ExecuteFromBaseBlockAndEncode(int shot)
{
    return ExecuteAdhoc([&]()
        {
            TState prevStateBin = BaseBlockStateBin;
            uint64_t baseRngHash = RngHash;
            bool anyNovelScripts = false; // Mark pellet as failed if 0 scripts were successful

            uint64_t baseFrame = this->GetCurrentFrame();
            for (int n = 0; n < config.PelletMaxScripts && abs(int64_t(this->GetCurrentFrame() - baseFrame)) < config.PelletMaxFrameDistance; n++)
            {
                // Apply next script
                SetTempRng(RngHash);
                bool updated = ChooseScriptAndApply();
                SetRng(RngHashTemp);

                // Validation
                bool validated = updated && ValidateCourseAndArea() && ExecuteAdhoc([&]() { return ValidateState(); }).executed;

                // Create and add block to list if it is new.
                bool novelScript = false;
                auto newStateBin = validated ? GetStateBinSafe() : TState();
                float fitness = validated ? GetStateFitness() : 0.f;
                bool isSolution = validated ? ExecuteAdhoc([&]() { return IsSolution(); }).executed : false;
                ScattershotSolution<TOutputState> solution = isSolution ? ScattershotSolution<TOutputState>(GetSolutionState(), this->GetInputs(config.StartFrame, this->GetCurrentFrame() - 1))
                    : ScattershotSolution<TOutputState>();
                QueueThreadById(config.Deterministic, [&]()
                    {
                        ThreadLock(CriticalRegions::Blocks, [&]()
                            {
                                if (validated && newStateBin != prevStateBin && newStateBin != BaseBlockStateBin)
                                    novelScript = scattershot.UpsertBlock(newStateBin, isSolution, solution, fitness, BaseBlockTailSegment, n + 1, baseRngHash, 0);
                            });
                    });

                // Update script result count
                ThreadLock(CriticalRegions::ScriptCounters, [&]()
                    {
                        scattershot.ScriptCount++;

                        if (!validated)
                            scattershot.FailedScripts++;
                        else if (novelScript)
                            scattershot.NovelScripts++;
                        else
                            scattershot.RedundantScripts++;
                    });

                if (!validated)
                    break;

                if (novelScript)
                {
                    anyNovelScripts |= true;
                    AddCsvRow(shot);
                }

                prevStateBin = newStateBin;
            }

            return anyNovelScripts;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
uint64_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetRng()
{
    uint64_t rngHashPrev = RngHash;
    RngHash = GetHash(RngHash);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::SetRng(uint64_t rngHash)
{
    RngHash = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
uint64_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetTempRng()
{
    uint64_t rngHashPrev = RngHashTemp;
    RngHashTemp = GetHash(RngHashTemp);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::SetTempRng(uint64_t rngHash)
{
    RngHashTemp = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <typename T>
uint64_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetHash(const T& toHash) const
{
    std::hash<std::byte> byteHasher;
    const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
    uint64_t hashValue = 0;
    for (std::size_t i = 0; i < sizeof(T); i++)
        hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

    return hashValue;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddRandomMovementOption(std::map<MovementOption, double> weightedOptions)
{
    if (weightedOptions.empty())
        return;

    double maxRng = 65536.0;

    double totalWeight = 0;
    for (const auto& pair : weightedOptions)
    {
        if (pair.second > 0)
            totalWeight += pair.second;
    }

    if (totalWeight == 0)
        return;

    double rng = GetTempRng() % (int)maxRng;
    double rngRangeMin = 0;
    for (const auto& pair : weightedOptions)
    {
        if (pair.second <= 0)
            continue;

        double rngRangeMax = rngRangeMin + pair.second * maxRng / totalWeight;
        if (rng >= rngRangeMin && rng < rngRangeMax)
        {
            movementOptions.insert(pair.first);
            return;
        }

        rngRangeMin = rngRangeMax;
    }

    movementOptions.insert(weightedOptions.rbegin()->first);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddMovementOption(MovementOption movementOption, double probability)
{
    if (probability <= 0.0)
        return;

    if (probability >= 1.0 || GetTempRng() % 65536 <= int(probability / 65535.0))
        movementOptions.insert(movementOption);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::CheckMovementOptions(MovementOption movementOption)
{
    return movementOptions.contains(movementOption);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Inputs ScattershotThread<TState, TResource, TStateTracker, TOutputState>::RandomInputs(std::map<Buttons, double> buttonProbabilities)
{
    Inputs inputs;

    ExecuteAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(this->resource->addr("gMarioState"));
            Camera* camera = *(Camera**)(this->resource->addr("gCamera"));

            // stick mag
            float intendedMag = 0;
            if (CheckMovementOptions(MovementOption::MAX_MAGNITUDE))
                intendedMag = 32.0f;
            else if (CheckMovementOptions(MovementOption::ZERO_MAGNITUDE))
                intendedMag = 0;
            else if (CheckMovementOptions(MovementOption::SAME_MAGNITUDE))
                intendedMag = marioState->intendedMag;
            else if (CheckMovementOptions(MovementOption::RANDOM_MAGNITUDE))
                intendedMag = (GetTempRng() % 1024) / 32.0f;

            // Intended yaw
            int16_t intendedYaw = 0;
            if (CheckMovementOptions(MovementOption::MATCH_FACING_YAW))
                intendedYaw = marioState->faceAngle[1];
            else if (CheckMovementOptions(MovementOption::ANTI_FACING_YAW))
                intendedYaw = marioState->faceAngle[1] + 0x8000;
            else if (CheckMovementOptions(MovementOption::SAME_YAW))
                intendedYaw = marioState->intendedYaw;
            else if (CheckMovementOptions(MovementOption::RANDOM_YAW))
                intendedYaw = GetTempRng();

            // Buttons
            uint16_t buttons = 0;
            if (CheckMovementOptions(MovementOption::SAME_BUTTONS))
                buttons = this->GetInputs(this->GetCurrentFrame() - 1).buttons;
            else if (CheckMovementOptions(MovementOption::NO_BUTTONS))
                buttons = 0;
            else if (CheckMovementOptions(MovementOption::RANDOM_BUTTONS))
            {
                for (const auto& pair : buttonProbabilities)
                {
                    if (pair.second <= 0)
                        continue;

                    if (pair.second >= 1.0)
                    {
                        buttons |= pair.first;
                        continue;
                    }

                    if (double(GetTempRng()) / double(0xFFFFFFFFFFFFFFFFull) <= pair.second)
                        buttons |= pair.first;
                }
            }

            // Calculate and execute input
            auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
            inputs = Inputs(buttons, stick.first, stick.second);
            return true;
        });

    return inputs;
}

#endif
