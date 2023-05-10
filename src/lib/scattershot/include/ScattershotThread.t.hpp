#pragma once
#ifndef SCATTERSHOT_THREAD_H
#error "ThreadState.t.hpp should only be included by Scattershot.hpp"
#else

template <class TState, derived_from_specialization_of<Resource> TResource>
ScattershotThread<TState, TResource>::ScattershotThread(Scattershot<TState, TResource>& scattershot, int id)
    : scattershot(scattershot), config(scattershot.config)
{
    Id = id;
    Blocks = scattershot.AllBlocks + Id * config.MaxBlocks;
    HashTable = scattershot.AllHashTables + Id * config.MaxHashes;
    SetRng((uint64_t)(Id + 173) * 5786766484692217813);
    
    //printf("Thread %d\n", Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::validation() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::execution()
{
    LongLoad(config.StartFrame);
    InitializeMemory();

    // Record start course/area for validation (generally scattershot has no cross-level value)
    startCourse = *(short*)this->resource->addr("gCurrCourseNum");
    startArea = *(short*)this->resource->addr("gCurrAreaIndex");

    for (int shot = 0; shot <= config.MaxShots; shot++)
    {
        // ALWAYS START WITH A MERGE SO THE SHARED BLOCKS ARE OK.
        if (shot % config.ShotsPerMerge == 0)
            SingleThread([&]() { scattershot.MergeState(shot); });

        // Pick a block to "fire a scattershot" at
        if (!SelectBaseBlock(shot))
            break;

        auto status = ExecuteAdhoc([&]()
            {
                DecodeBaseBlockDiffAndApply();

                if (!ValidateBaseBlock())
                    return false;

                this->Save();
                for (int segment = 0; segment < config.SegmentsPerShot; segment++)
                    ExecuteFromBaseBlockAndEncode();

                return true;
            });

        //printf("%d %d %d %d\n", status.nLoads, status.nSaves, status.nFrameAdvances, status.executionDuration);
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::assertion() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::InitializeMemory()
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

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::SelectBaseBlock(int mainIteration)
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

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::ValidateBaseBlock()
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

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::ProcessNewBlock(uint64_t baseRngHash, int nScripts, StateBin<TState> newStateBin)
{
    Block<TState> newBlock;

    // Create and add block to list.
    if (scattershot.NBlocks[Id] == config.MaxBlocks)
    {
        //printf("Max local blocks reached!\n");
    }
    else {
        //UPDATED FOR SEGMENTS STRUCT
        newBlock = BaseBlock;
        newBlock.stateBin = newStateBin;
        newBlock.fitness = GetStateFitnessSafe();
        int blockIndexLocal = newStateBin.GetBlockIndex(Blocks, HashTable, config.MaxHashes, 0, scattershot.NBlocks[Id]);
        int blockIndex = newStateBin.GetBlockIndex(
            scattershot.SharedBlocks, scattershot.SharedHashTable, config.MaxSharedHashes, 0, scattershot.NBlocks[config.TotalThreads]);

        bool bestlocalBlock = blockIndexLocal < scattershot.NBlocks[Id]
            && newBlock.fitness >= Blocks[blockIndexLocal].fitness;
        bool bestSharedBlockOrNew = !(blockIndex < scattershot.NBlocks[config.TotalThreads]
            && newBlock.fitness < scattershot.SharedBlocks[blockIndex].fitness);

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
        }
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource>
template <typename F>
void ScattershotThread<TState, TResource>::SingleThread(F func)
{
    #pragma omp barrier
    {
        if (omp_get_thread_num() == 0)
            func();
    }
    #pragma omp barrier

    return;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::ValidateCourseAndArea()
{
    return startCourse == *(short*)this->resource->addr("gCurrCourseNum")
        && startArea == *(short*)this->resource->addr("gCurrAreaIndex");
}

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::ChooseScriptAndApply()
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

template <class TState, derived_from_specialization_of<Resource> TResource>
StateBin<TState> ScattershotThread<TState, TResource>::GetStateBinSafe()
{
    StateBin<TState> stateBin;
    ExecuteAdhoc([&]()
        {
            stateBin = StateBin<TState>(GetStateBin());
            return true;
        });

    return stateBin;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
float ScattershotThread<TState, TResource>::GetStateFitnessSafe()
{
    float fitness;
    ExecuteAdhoc([&]()
        {
            fitness = GetStateFitness();
            return true;
        });

    return fitness;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus ScattershotThread<TState, TResource>::DecodeBaseBlockDiffAndApply()
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
    this->Load(postScriptFrame);
    return status;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus ScattershotThread<TState, TResource>::ExecuteFromBaseBlockAndEncode()
{
    return ExecuteAdhoc([&]()
        {
            StateBin<TState> prevStateBin = GetStateBinSafe();
            uint64_t baseRngHash = RngHash;

            for (int n = 0; n < config.SegmentLength; n++)
            {
                SetTempRng(RngHash);
                bool updated = ChooseScriptAndApply();
                SetRng(RngHashTemp);

                if (!updated || !ValidateCourseAndArea() || !ExecuteAdhoc([&]() { return ValidateState(); }).executed)
                    break;

                auto newStateBin = GetStateBinSafe();
                if (newStateBin != prevStateBin && newStateBin != BaseBlock.stateBin)
                {
                    // Create and add block to list.
                    ProcessNewBlock(baseRngHash, n, newStateBin);
                    prevStateBin = newStateBin; // TODO: Why this here?
                }
            }

            return true;
        });
}

template <class TState, derived_from_specialization_of<Resource> TResource>
uint64_t ScattershotThread<TState, TResource>::GetRng()
{
    uint64_t rngHashPrev = RngHash;
    RngHash = GetHash(RngHash);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::SetRng(uint64_t rngHash)
{
    RngHash = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
uint64_t ScattershotThread<TState, TResource>::GetTempRng()
{
    uint64_t rngHashPrev = RngHashTemp;
    RngHashTemp = GetHash(RngHashTemp);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::SetTempRng(uint64_t rngHash)
{
    RngHashTemp = rngHash;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
template <typename T>
uint64_t ScattershotThread<TState, TResource>::GetHash(const T& toHash) const
{
    std::hash<std::byte> byteHasher;
    const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
    uint64_t hashValue = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        hashValue ^= static_cast<uint64_t>(byteHasher(data[i])) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

    return hashValue;
}

template <class TState, derived_from_specialization_of<Resource> TResource>
void ScattershotThread<TState, TResource>::AddRandomMovementOption(std::map<MovementOption, double> weightedOptions)
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

template <class TState, derived_from_specialization_of<Resource> TResource>
bool ScattershotThread<TState, TResource>::CheckMovementOptions(MovementOption movementOption)
{
    return movementOptions.contains(movementOption);
}

template <class TState, derived_from_specialization_of<Resource> TResource>
Inputs ScattershotThread<TState, TResource>::RandomInputs(std::map<Buttons, double> buttonProbabilities)
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
