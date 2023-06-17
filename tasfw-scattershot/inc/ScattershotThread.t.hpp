#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotThread.t.hpp should only be included by Scattershot.hpp"
#else
#include <Scattershot.hpp>

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ScattershotThread(Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot, int id)
    : scattershot(scattershot), config(scattershot.config)
{
    Id = id;
    Blocks = scattershot.AllBlocks + Id * config.MaxBlocks;
    HashTable = scattershot.AllHashTables + Id * config.MaxHashes;
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
    LongLoad(config.StartFrame);
    InitializeMemory();

    // Add CSV columns if present
    SingleThread([&]() { AddCsvLabels(); });

    // Record start course/area for validation (generally scattershot has no cross-level value)
    startCourse = *(short*)this->resource->addr("gCurrCourseNum");
    startArea = *(short*)this->resource->addr("gCurrAreaIndex");

    for (int shot = 0; shot <= config.MaxShots; shot++)
    {
        // ALWAYS START WITH A MERGE SO THE SHARED BLOCKS ARE OK.
        if (shot % config.ShotsPerMerge == 0)
            SingleThread([&]() { scattershot.MergeState(shot, GetRng()); });

        // Pick a block to "fire a shot" at
        if (!SelectBaseBlock(shot))
            break;

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

        //printf("%d %d %d %d\n", status.nLoads, status.nSaves, status.nFrameAdvances, status.executionDuration);
    }

    return true;
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
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::InitializeMemory()
{
    // Initial block
    Blocks[0].stateBin = GetStateBinSafe(); //CHEAT TODO NOTE
    Blocks[0].tailSegment = (Segment*)malloc(sizeof(Segment)); //Instantiate root segment
    Blocks[0].tailSegment->nScripts = 0;
    Blocks[0].tailSegment->parent = NULL;
    Blocks[0].tailSegment->nReferences = 0;
    Blocks[0].tailSegment->depth = 1;

    // Init local hash table.
    for (int hashIndex = 0; hashIndex < config.MaxHashes; hashIndex++)
        HashTable[hashIndex] = -1;

    HashTable[Blocks[0].stateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = 0;

    // Synchronize global state
    scattershot.AllSegments[scattershot.NSegments[Id] + Id * config.MaxLocalSegments] = Blocks[0].tailSegment;
    scattershot.NSegments[Id]++;
    scattershot.NBlocks[Id]++;

    //LoopTimeStamp = omp_get_wtime();
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::SelectBaseBlock(int mainIteration)
{
    int sharedBlockIndex = scattershot.NBlocks[config.TotalThreads];
    if (mainIteration % config.StartFromRootEveryNShots == 0)
        sharedBlockIndex = 0;
    else
    {
        for (int attempt = 0; attempt < 100000; attempt++) {
            sharedBlockIndex = GetRng() % scattershot.NBlocks[config.TotalThreads];

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment == 0)
            {
                printf("Chosen block tailseg null!\n");
                continue;
            }

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment->depth == 0)
            {
                printf("Chosen block tailseg depth 0!\n");
                continue;
            }

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment->depth < config.MaxSegments)
                break;
        }
        if (sharedBlockIndex == scattershot.NBlocks[config.TotalThreads])
        {
            //printf("Could not find block!\n");
            return false;
        }
    }

    BaseBlock = scattershot.SharedBlocks[sharedBlockIndex];
    //if (BaseBlock.tailSeg->depth > config.MaxSegments + 2) { printf("BaseBlock depth above max!\n"); }
    //if (BaseBlock.tailSeg->depth == 0) { printf("BaseBlock depth is zero!\n"); }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ValidateBaseBlock(int shot)
{
    StateBin<TState> currentStateBin = GetStateBinSafe();
    if (BaseBlock.stateBin != currentStateBin) {
        Segment* currentSegmentDebug = BaseBlock.tailSegment;
        while (currentSegmentDebug != 0) {  //inefficient but probably doesn't matter
            if (currentSegmentDebug->parent == 0)
                //printf("Parent is null!");
            //if (currentSegmentDebug->parent->depth + 1 != currentSegmentDebug->depth) { printf("Depths wrong"); }
                currentSegmentDebug = currentSegmentDebug->parent;
        }

        this->ExportM64("C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\analysis\\error.m64");
        cout << Id << " " << shot << "\n";
        BaseBlock.stateBin.print();
        currentStateBin.print();
        cout << BaseBlock.stateBin.GetHash(BaseBlock.stateBin.state, false) << "\n";
        cout << currentStateBin.GetHash(currentStateBin.state, false) << "\n";
        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin)
{
    Block<TState> newBlock;

    // Create and add block to list.
    if (scattershot.NBlocks[Id] == config.MaxBlocks)
    {
        //printf("Max local blocks reached!\n");
        return false;
    }

    //UPDATED FOR SEGMENTS STRUCT
    newBlock = BaseBlock;
    newBlock.stateBin = newStateBin;
    newBlock.fitness = GetStateFitnessSafe();
    int blockIndexLocal = newStateBin.GetBlockIndex(Blocks, HashTable, config.MaxHashes, 0, scattershot.NBlocks[Id]);
    int blockIndex = newStateBin.GetBlockIndex(
        scattershot.SharedBlocks, scattershot.SharedHashTable, config.MaxSharedHashes, 0, scattershot.NBlocks[config.TotalThreads]);

    bool bestlocalBlock = blockIndexLocal < scattershot.NBlocks[Id]
        && newBlock.fitness > Blocks[blockIndexLocal].fitness;
    bool bestSharedBlockOrNew = !(blockIndex < scattershot.NBlocks[config.TotalThreads]
        && newBlock.fitness <= scattershot.SharedBlocks[blockIndex].fitness);

    if (bestlocalBlock || bestSharedBlockOrNew)
    {
        // Create new segment
        Segment* newSegment = (Segment*)malloc(sizeof(Segment));
        newSegment->parent = BaseBlock.tailSegment;
        newSegment->nReferences = bestlocalBlock ? 0 : 1;
        newSegment->nScripts = nScripts + 1;
        newSegment->seed = baseRngHash;
        newSegment->depth = BaseBlock.tailSegment->depth + 1;
        //if (newSegment->depth == 0) { printf("newSeg depth is 0!\n"); }
        //if (BaseBlock.tailSegment->depth == 0) { printf("origBlock tailSeg depth is 0!\n"); }
        newBlock.tailSegment = newSegment;
        scattershot.AllSegments[Id * config.MaxLocalSegments + scattershot.NSegments[Id]] = newSegment;
        scattershot.NSegments[Id] += 1;
        
        if (bestlocalBlock)
            Blocks[blockIndexLocal] = newBlock;
        else
        {
            HashTable[newStateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = scattershot.NBlocks[Id];
            Blocks[scattershot.NBlocks[Id]++] = newBlock;
        }

        return true;
    }

    return false;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddCsvRow(int shot)
{
    bool sampled = false;
    ThreadLock([&]()
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

    ThreadLock([&]()
        {
            if (!rowValidated)
            {
                cout << "Unable to add row to CSV. Labels/Row have different column counts.\n";
                return;
            }

            // CSV row export retry loop
            auto failedRowPos = scattershot.Csv.tellp();
            for (int i = 0; i < 5; i++)
            {
                if (i > 0)
                    cout << "Retrying CSV row " << scattershot.CsvRows << ".\n";

                scattershot.Csv << shot << "," << this->GetCurrentFrame() << "," << sampled << "," << row << "\n";
                if (!scattershot.Csv.fail())
                {
                    scattershot.CsvRows++;
                    return;
                }
                else {
                    cout << "Error writing to CSV row " << scattershot.CsvRows << ": " << scattershot.Csv.rdstate() << "\n";
                    scattershot.Csv.close();
                    scattershot.Csv = std::ofstream(scattershot.CsvFileName);
                    scattershot.Csv.seekp(failedRowPos);
                }
            }

            cout << "Exceeded CSV export retry count. Disabling CSV export.\n";
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
StateBin<TState> ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetStateBinSafe()
{
    StateBin<TState> stateBin;
    ExecuteAdhoc([&]()
        {
            stateBin = StateBin<TState>(GetStateBin());
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
            //if (tState.BaseBlock.tailSeg == 0) printf("origBlock has null tailSeg");

            Segment* tailSegment = BaseBlock.tailSegment;
            Segment* currentSegment;
            int tailSegmentDepth = tailSegment->depth;
            for (int i = 1; i <= tailSegmentDepth; i++) {
                currentSegment = tailSegment;
                while (currentSegment->depth != i) // inefficient but probably doesn't matter
                {
                    //if (currentSegment->parent == 0) printf("Parent is null!");
                    //if (currentSegment->parent->depth + 1 != currentSegment->depth) { printf("Depths wrong"); }
                    currentSegment = currentSegment->parent;
                }

                SetTempRng(currentSegment->seed);
                for (int script = 0; script < currentSegment->nScripts; script++)
                    ChooseScriptAndApply();
            }

            postScriptFrame = this->GetCurrentFrame();
            return true;
        });

    // Needed to sync with original execution (block is saved after individual script diff is applied)
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
            StateBin<TState> prevStateBin = GetStateBinSafe();
            uint64_t baseRngHash = RngHash;
            bool anyNovelScripts = false; // Mark pellet as failed if 0 scripts were successful

            uint64_t baseFrame = this->GetCurrentFrame();
            for (int n = 0; n < config.PelletMaxScripts && abs(int64_t(this->GetCurrentFrame() - baseFrame)) < config.PelletMaxFrameDistance; n++)
            {
                // Apply next script
                SetTempRng(RngHash);
                bool updated = ChooseScriptAndApply();
                SetRng(RngHashTemp);

                ThreadLock([&]() { scattershot.ScriptCount++; });

                // Validation
                if (!updated || !ValidateCourseAndArea() || !ExecuteAdhoc([&]() { return ValidateState(); }).executed)
                {
                    ThreadLock([&]() { scattershot.FailedScripts++; });
                    break;
                }  

                // Create and add block to list if it is new.
                auto newStateBin = GetStateBinSafe();
                /*
                if (newStateBin.GetHash(newStateBin.state, false) == 15818986094471996600ull)
                {
                    this->ExportM64("C:\\Users\\Tyler\\Documents\\repos\\sm64_tas_scripting\\analysis\\error0.m64");
                    cout << Id << " " << shot << "\n";
                    newStateBin.print();
                }
                */

                if (newStateBin != prevStateBin && newStateBin != BaseBlock.stateBin && ProcessNewBlock(baseRngHash, n, newStateBin))
                {
                    anyNovelScripts |= true;
                    ThreadLock([&]() { scattershot.NovelScripts++; });
                    AddCsvRow(shot);
                }
                else
                    ThreadLock([&]() { scattershot.RedundantScripts++; });

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
    for (std::size_t i = 0; i < sizeof(T); ++i)
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
