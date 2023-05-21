#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotThread.t.hpp should only be included by Scattershot.hpp"
#else
#include <Scattershot.hpp>

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
ScattershotThread<TState, TResource, TStateTracker>::ScattershotThread(Scattershot<TState, TResource, TStateTracker>& scattershot, int id)
    : scattershot(scattershot), config(scattershot.config)
{
    Id = id;
    Blocks = scattershot.AllBlocks + Id * config.MaxBlocks;
    HashTable = scattershot.AllHashTables + Id * config.MaxHashes;
    SetRng((uint64_t)(Id + 173) * 5786766484692217813);
    
    //printf("Thread %d\n", Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::validation() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::execution()
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
            SingleThread([&]() { scattershot.MergeState(shot); });

        // Pick a block to "fire a shot" at
        if (!SelectBaseBlock(shot))
            break;

        auto status = ExecuteAdhoc([&]()
            {
                DecodeBaseBlockDiffAndApply();

                if (!ValidateBaseBlock())
                    return false;

                this->Save();
                for (int segment = 0; segment < config.PelletsPerShot; segment++)
                    ExecuteFromBaseBlockAndEncode(shot);

                return true;
            });

        //printf("%d %d %d %d\n", status.nLoads, status.nSaves, status.nFrameAdvances, status.executionDuration);
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
std::string ScattershotThread<TState, TResource, TStateTracker>::GetCsvLabels()
{
    return "";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::ForceAddToCsv()
{
    return false;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::AddCsvLabels()
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
    std::derived_from<Script<TResource>> TStateTracker>
std::string ScattershotThread<TState, TResource, TStateTracker>::GetCsvRow()
{
    return "";
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::assertion() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::InitializeMemory()
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
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::SelectBaseBlock(int mainIteration)
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
                //printf("Chosen block tailseg null!\n");
                continue;
            }

            if (scattershot.SharedBlocks[sharedBlockIndex].tailSegment->depth == 0)
            {
                //printf("Chosen block tailseg depth 0!\n");
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
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::ValidateBaseBlock()
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

        BaseBlock.stateBin.print();
        currentStateBin.print();
        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin)
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
        if (!bestlocalBlock)
            HashTable[newStateBin.FindNewHashIndex(HashTable, config.MaxHashes)] = scattershot.NBlocks[Id];

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
        Blocks[blockIndexLocal] = newBlock;

        if (bestlocalBlock)
            Blocks[blockIndexLocal] = newBlock;
        else
            Blocks[scattershot.NBlocks[Id]++] = newBlock;

        return true;
    }

    return false;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::AddCsvRow(int shot)
{
    bool sampled = false;
    ThreadLock([&]()
        {
            if (scattershot.CsvRows == -1)
                return;

            if (scattershot.CsvCounter++ % config.CsvSamplePeriod == 0)
                sampled = true;
        });
            
    if (!sampled && !ExecuteAdhoc([&]() { return ForceAddToCsv(); }).executed)
        return;

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

            scattershot.Csv << shot << "," << this->GetCurrentFrame() << "," << sampled << "," << row << "\n";
            scattershot.CsvRows++;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
template <typename F>
void ScattershotThread<TState, TResource, TStateTracker>::SingleThread(F func)
{
    #pragma omp barrier
    {
        if (omp_get_thread_num() == 0)
            func();
    }
    #pragma omp barrier

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
template <typename F>
void ScattershotThread<TState, TResource, TStateTracker>::ThreadLock(F func)
{
    #pragma omp critical
    {
        func();
    }

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::ValidateCourseAndArea()
{
    return startCourse == *(short*)this->resource->addr("gCurrCourseNum")
        && startArea == *(short*)this->resource->addr("gCurrAreaIndex");
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::ChooseScriptAndApply()
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
    std::derived_from<Script<TResource>> TStateTracker>
StateBin<TState> ScattershotThread<TState, TResource, TStateTracker>::GetStateBinSafe()
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
    std::derived_from<Script<TResource>> TStateTracker>
float ScattershotThread<TState, TResource, TStateTracker>::GetStateFitnessSafe()
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
    std::derived_from<Script<TResource>> TStateTracker>
AdhocBaseScriptStatus ScattershotThread<TState, TResource, TStateTracker>::DecodeBaseBlockDiffAndApply()
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
    std::derived_from<Script<TResource>> TStateTracker>
AdhocBaseScriptStatus ScattershotThread<TState, TResource, TStateTracker>::ExecuteFromBaseBlockAndEncode(int shot)
{
    return ExecuteAdhoc([&]()
        {
            StateBin<TState> prevStateBin = GetStateBinSafe();
            uint64_t baseRngHash = RngHash;

            for (int n = 0; n < config.PelletLength; n++)
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
                if (newStateBin != prevStateBin && newStateBin != BaseBlock.stateBin && ProcessNewBlock(baseRngHash, n, newStateBin))
                {
                    ThreadLock([&]() { scattershot.NovelScripts++; });
                    AddCsvRow(shot);
                }
                else
                    ThreadLock([&]() { scattershot.RedundantScripts++; });

                prevStateBin = newStateBin;
            }

            return true;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
uint64_t ScattershotThread<TState, TResource, TStateTracker>::GetRng()
{
    uint64_t rngHashPrev = RngHash;
    RngHash = GetHash(RngHash);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::SetRng(uint64_t rngHash)
{
    RngHash = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
uint64_t ScattershotThread<TState, TResource, TStateTracker>::GetTempRng()
{
    uint64_t rngHashPrev = RngHashTemp;
    RngHashTemp = GetHash(RngHashTemp);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::SetTempRng(uint64_t rngHash)
{
    RngHashTemp = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
template <typename T>
uint64_t ScattershotThread<TState, TResource, TStateTracker>::GetHash(const T& toHash) const
{
    std::hash<std::byte> byteHasher;
    const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
    uint64_t hashValue = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

    return hashValue;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
void ScattershotThread<TState, TResource, TStateTracker>::AddRandomMovementOption(std::map<MovementOption, double> weightedOptions)
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
    std::derived_from<Script<TResource>> TStateTracker>
bool ScattershotThread<TState, TResource, TStateTracker>::CheckMovementOptions(MovementOption movementOption)
{
    return movementOptions.contains(movementOption);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker>
Inputs ScattershotThread<TState, TResource, TStateTracker>::RandomInputs(std::map<Buttons, double> buttonProbabilities)
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
