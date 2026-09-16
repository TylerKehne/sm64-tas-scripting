#pragma once
#ifndef SCATTERSHOTTHREAD_H
#error "ScattershotThread.t.hpp should only be included by ScattershotThread.hpp"
#else
#include <sm64/Camera.hpp>

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ScattershotThread(Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot)
    : config(scattershot.config), scattershot(scattershot)
{
    Id = omp_get_thread_num();
    SetRng((uint64_t)(Id + config.Seed + 173) * 5786766484692217813);
    
    //printf("Thread %d\n", Id);
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
std::string ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetCsvRow()
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
uint64_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::GetTempRng()
{
    uint64_t rngHashPrev = RngHashTemp;
    RngHashTemp = GetHash(RngHashTemp);
    return rngHashPrev;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddRandomMovementOption(std::initializer_list<std::pair<BasicMoves, double>> weightedOptions)
{
    DrawOption(weightedOptions, basicMoves);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddMovementOption(BasicMoves movementOption, double probability)
{
    AddOption(std::size_t(movementOption), probability, basicMoves);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::CheckMovementOptions(BasicMoves movementOption)
{
    return OptionSelected(std::size_t(movementOption), basicMoves);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
Inputs ScattershotThread<TState, TResource, TStateTracker, TOutputState>::RandomInputs(std::initializer_list<std::pair<Buttons, double>> buttonProbabilities)
{
    std::array<std::pair<Buttons, double>, MaxWeightedEntries> probabilities;
    std::size_t count = SortedByKey(buttonProbabilities, probabilities);
    Inputs inputs;

    ExecuteAdhoc([&]()
        {
            MarioState* marioState = *(MarioState**)(this->ReadState("gMarioState"));
            Camera* camera = *(Camera**)(this->ReadState("gCamera"));

            // stick mag
            float intendedMag = 0;
            if (CheckMovementOptions(BasicMoves::MAX_MAGNITUDE))
                intendedMag = 32.0f;
            else if (CheckMovementOptions(BasicMoves::ZERO_MAGNITUDE))
                intendedMag = 0;
            else if (CheckMovementOptions(BasicMoves::SAME_MAGNITUDE))
                intendedMag = marioState->intendedMag;
            else if (CheckMovementOptions(BasicMoves::RANDOM_MAGNITUDE))
                intendedMag = (GetTempRng() % 1024) / 32.0f;

            // Intended yaw
            int16_t intendedYaw = 0;
            if (CheckMovementOptions(BasicMoves::MATCH_FACING_YAW))
                intendedYaw = marioState->faceAngle[1];
            else if (CheckMovementOptions(BasicMoves::ANTI_FACING_YAW))
                intendedYaw = marioState->faceAngle[1] + 0x8000;
            else if (CheckMovementOptions(BasicMoves::SAME_YAW))
                intendedYaw = marioState->intendedYaw;
            else if (CheckMovementOptions(BasicMoves::RANDOM_YAW))
                intendedYaw = int16_t(GetTempRng());

            // Buttons
            uint16_t buttons = 0;
            if (CheckMovementOptions(BasicMoves::SAME_BUTTONS))
                buttons = this->GetInputs(this->GetCurrentFrame() - 1).buttons;
            else if (CheckMovementOptions(BasicMoves::NO_BUTTONS))
                buttons = 0;
            else if (CheckMovementOptions(BasicMoves::RANDOM_BUTTONS))
            {
                for (std::size_t i = 0; i < count; i++)
                {
                    const auto& pair = probabilities[i];
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
    for (int shot = 0; totalShots <= uint64_t(config.MaxShots); shot++)
    {
        // Pick a block to "fire a shot" at. In deterministic mode this is a turn of the queue
        // like an upsert, so the table it reads is the same on every run.
        QueueThreadById(config.Deterministic, [&]()
            {
                #pragma omp critical (blocks)
                {
                    SelectBaseBlock(shot);
                }
            });

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

        // Diagnosis of a validation failure (ROADMAP 4.5): decode the same block again from the
        // same reverted state. If the second decode matches the first, decoding is deterministic
        // and the disagreement is between encoding and decoding; if it does not, state survives
        // a load. The first frame where the two decoded input sequences differ is printed.
        if (LastValidationFailed)
        {
            LastValidationFailed = false;
            ExecuteAdhoc([&]()
                {
                    DecodeBaseBlockDiffAndApply();
                    TState again = GetStateBinSafe();
                    M64Diff diff = this->GetTotalDiff();

                    #pragma omp critical (print)
                    {
                        std::cout << "  re-decode: " << (again == LastDecodedBin
                            ? "same bin as the first decode, so decoding is deterministic and the recording disagrees with it"
                            : "different bin from the first decode, so state survives a load") << "\n";

                        std::set<uint64_t> frames;
                        for (const auto& pair : LastDecodedDiff.frames) frames.insert(pair.first);
                        for (const auto& pair : diff.frames) frames.insert(pair.first);
                        bool reported = false;
                        for (uint64_t frame : frames)
                        {
                            auto a = LastDecodedDiff.frames.find(frame);
                            auto b = diff.frames.find(frame);
                            bool same = a != LastDecodedDiff.frames.end() && b != diff.frames.end() && a->second == b->second;
                            if (same)
                                continue;
                            auto show = [](auto it, auto end)
                            {
                                if (it == end)
                                    return std::string("(none)");
                                return std::to_string(it->second.buttons) + "/" + std::to_string(it->second.stick_x) + "/" + std::to_string(it->second.stick_y);
                            };
                            std::cout << "  first differing decoded input at frame " << frame << ": first " << show(a, LastDecodedDiff.frames.end())
                                << ", second " << show(b, diff.frames.end()) << " (diffs span " << *frames.begin() << ".." << *frames.rbegin() << ")\n";
                            reported = true;
                            break;
                        }
                        if (!reported)
                            std::cout << "  the two decoded input sequences are identical (" << frames.size() << " frames)\n";
                    }
                    return false;
                });
        }

        size_t nSolutions = 0;
        bool maxShotsReached = false;
        // The shot count and the solution count decide when this thread stops: read in a
        // turn of the queue for the same reason.
        QueueThreadById(config.Deterministic, [&]()
            {
                #pragma omp critical (print)
                {
                    //const char* x = "solutions";
                    #pragma omp critical (solutions)
                    {
                        nSolutions = scattershot.Solutions.size();
                    }

                    //ThreadLock(x, [&]() { nSolutions = scattershot.Solutions.size(); });
                    //ThreadLock(CriticalRegions::Solutions, [&]() { nSolutions = scattershot.Solutions.size(); });
                    #pragma omp critical (totalshots)
                    {
                        totalShots = ++scattershot.TotalShots;
                        if (totalShots + omp_get_num_threads() - 1 >= uint64_t(config.MaxShots))
                            maxShotsReached = true;
                    }

                    // Periodically print progress to console
                    if (totalShots % config.ShotsPerUpdate == 0)
                        scattershot.PrintStatus();
                }
            });

        if (maxShotsReached || (config.MaxSolutions > 0 && nSolutions >= size_t(config.MaxSolutions)))
            break;
    }

    if (config.Deterministic)
        RetireFromQueue();
    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::assertion() { return true; }

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::Initialize()
{
    LongLoad(config.StartFrame);

    // Load piped-in diffs as root blocks, in rounds every thread takes part in: in round r
    // thread i applies input r * threads + i when there is one and makes its queue call
    // either way, so every thread makes the same number of calls and the tickets of 3.8 stay
    // paired (ROADMAP 3.14: a shared index handed the inputs out in timing order and left
    // the threads with different call counts, which paired later calls across threads and
    // could hang). The block's piped-diff index is 16 bits, hence the cap.
    if (!scattershot.InputSolutions.empty())
    {
        // (parent, seed, nScripts, pipedDiff1Index). Root segments are never decoded, so the
        // values are informational; the old call passed RngHash as nScripts (truncated to 8 bits).
        std::shared_ptr<Segment> rootSegment = std::make_shared<Segment>(nullptr, RngHash, uint8_t(0), uint16_t(0));
        uint64_t threads = uint64_t(omp_get_num_threads());
        uint64_t inputs = std::min<uint64_t>(scattershot.InputSolutions.size(), 65534);
        uint64_t rounds = (inputs + threads - 1) / threads;
        for (uint64_t round = 0; round < rounds; round++)
        {
            uint64_t index = round * threads + uint64_t(Id);
            ExecuteAdhoc([&]()
                {
                    if (index >= inputs)
                    {
                        QueueThreadById(config.Deterministic, [&]() {});
                        return true;
                    }

                    this->Apply(scattershot.InputSolutions[size_t(index)].m64Diff);
                    QueueThreadById(config.Deterministic, [&]()
                        {
                            #pragma omp critical (blocks)
                            {
                                scattershot.UpsertBlock(GetStateBinSafe(), false, ScattershotSolution<TOutputState>(),
                                    GetStateFitnessSafe(), rootSegment, 1, GetRng(), uint16_t(index + 1));
                            }
                        });

                    return true;
                });
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
    startCourse = *(short*)this->ReadState("gCurrCourseNum");
    startArea = *(short*)this->ReadState("gCurrAreaIndex");
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
                float fitness = validated ? GetStateFitnessSafe() : 0.f;
                bool isSolution = validated ? ExecuteAdhoc([&]() { return IsSolution(); }).executed : false;
                ScattershotSolution<TOutputState> solution = isSolution ? ScattershotSolution<TOutputState>(GetSolutionState(), this->GetTotalDiff())
                    : ScattershotSolution<TOutputState>();
                QueueThreadById(config.Deterministic, [&]()
                    {
                        #pragma omp critical (blocks)
                        {
                            //if (validated && newStateBin != prevStateBin && newStateBin != BaseBlockStateBin)
                            if (validated)
                                novelScript = scattershot.UpsertBlock(newStateBin, isSolution, solution, fitness, BaseBlockTailSegment, n + 1, baseRngHash, 0);
                        }
                            });

                // Update script result count
                #pragma omp critical (scriptcounters)
                {
                    scattershot.ScriptCount++;

                    if (!validated)
                        scattershot.FailedScripts++;
                    else if (novelScript)
                        scattershot.NovelScripts++;
                    else
                        scattershot.RedundantScripts++;
                }

                if (!validated)
                    break;

                if (novelScript)
                {
                    anyNovelScripts |= true;
                    AddCsvRow(shot);
                }

                if (isSolution)
                    break;

                prevStateBin = newStateBin;
            }

            return anyNovelScripts;
        });
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

    // Modify leaves the cursor after the end of the child's diff by design (ARCHITECTURE.md,
    // "Script hierarchy"); a block is keyed by the frame the script stopped on, so go back to
    // it. This often does nothing and only costs anything when it rewinds.
    this->Load(postScriptFrame);
    return status;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ChooseScriptAndApply()
{
    std::fill(basicMoves.begin(), basicMoves.end(), false);
    std::fill(customMoves.begin(), customMoves.end(), false);

    ExecuteAdhoc([&]()
        {
            SelectMovementOptions();
            return true;
        });

    // Execute script and update rng hash
    int64_t postScriptFrame = -1;
    auto status = ModifyAdhoc([&]()
        {
            bool success = ApplyMovement();
            postScriptFrame = this->GetCurrentFrame();
            return success;
        });

    // Modify leaves the cursor after the end of the child's diff by design (ARCHITECTURE.md,
    // "Script hierarchy"); a block is keyed by the frame the script stopped on, so go back to
    // it. This often does nothing and only costs anything when it rewinds.
    if (status.executed)
        this->Load(postScriptFrame);

    return status.executed;
}

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
            blockIndex = int(GetRng() % scattershot.InputSolutions.size());
    }
    else
    {
        while (true)
        {
            blockIndex = int(GetRng() % scattershot.Blocks.size());

            // Don't explore beyond known solutions
            bool isSolution;
            #pragma omp critical (solutions)
            {
                isSolution = scattershot.Solutions.contains(blockIndex);
            }
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
    LastValidationFailed = BaseBlockStateBin != currentStateBin;
    if (LastValidationFailed) {
        LastDecodedBin = currentStateBin;
        LastDecodedDiff = this->GetTotalDiff();

        #pragma omp critical (scriptcounters)
        {
            scattershot.ValidationFailures++;
        }

        // Dumped next to the CSVs for post-mortem; the path comes from the configuration (AGENTS.md hard rule 5).
        // A root base block has nothing applied, so there may be no inputs to dump.
        if (!LastDecodedDiff.frames.empty())
            this->ExportM64(std::filesystem::path(scattershot.config.CsvOutputDirectory) / "error.m64", LastDecodedDiff.frames.rbegin()->first + 1);

        #pragma omp critical (print)
        {
            std::cout << "base-block validation failed: thread " << Id << " shot " << shot << " frame " << this->GetCurrentFrame()
                << " chain depth " << int(BaseBlockTailSegment->depth) << "\n";
            if constexpr (requires { BaseBlockStateBin.bytes; })
            {
                auto hex = [](const TState& bin)
                {
                    std::string text;
                    char buffer[4];
                    for (auto byte : bin.bytes)
                    {
                        std::snprintf(buffer, sizeof buffer, "%02x", unsigned(byte));
                        text += buffer;
                    }
                    return text;
                };
                std::cout << "  expected " << hex(BaseBlockStateBin) << "\n  decoded  " << hex(currentStateBin) << "\n";
            }
            else
            {
                std::cout << "  expected hash " << scattershot.GetHash(BaseBlockStateBin, false)
                    << "\n  decoded hash  " << scattershot.GetHash(currentStateBin, false) << "\n";
            }
        }
        return false;
    }

    return true;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::ValidateCourseAndArea()
{
    return startCourse == *(short*)this->ReadState("gCurrCourseNum")
        && startArea == *(short*)this->ReadState("gCurrAreaIndex");
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
    const std::byte* data = reinterpret_cast<const std::byte*>(&toHash);
    uint64_t hashValue = 0;
    for (std::size_t i = 0; i < sizeof(T); i++)
        hashValue ^= HashByte(data[i]) + 0x9e3779b97f4a7c15ull + (hashValue << 6) + (hashValue >> 2);

    return hashValue;
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <typename F>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::QueueThreadById(bool deterministic, F func)
{
    if (!deterministic)
    {
        func();
        return;
    }

    uint64_t ticket = TakeTicket();
    WaitForTurn(ticket);
    func();
    PassTurn(ticket + 1);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
uint64_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::TakeTicket()
{
    // Call k of thread i is ticket k * threads + i: round by round, threads in order, the
    // order the barriers gave the calls.
    return QueueCalls++ * uint64_t(omp_get_num_threads()) + uint64_t(Id);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::WaitForTurn(uint64_t ticket)
{
    // A bounded spin, then a wait on the turn itself. The handoff is on the critical path
    // of every script, so the spin covers a turn of typical length and a wake through the
    // kernel is paid only past it; a waiter past it blocks instead of burning its core
    // (ROADMAP 3.15: libomp's barriers slept where the spin did not).
    uint64_t spins = 0;
    for (uint64_t turn; (turn = scattershot.QueueTurn.load(std::memory_order_acquire)) != ticket;)
    {
        if (spins++ < SpinBudget)
            _mm_pause();
        else
            scattershot.QueueTurn.wait(turn, std::memory_order_acquire);
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::PassTurn(uint64_t next)
{
    // Only the holder of the turn writes it. Retired threads' tickets are skipped; the loop
    // is bounded so that once every thread has retired the turn just moves on.
    uint64_t threads = uint64_t(omp_get_num_threads());
    for (uint64_t skipped = 0; skipped < threads && scattershot.QueueRetired[size_t(next % threads)]; skipped++)
        next++;
    scattershot.QueueTurn.store(next, std::memory_order_release);
    scattershot.QueueTurn.notify_all();
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::RetireFromQueue()
{
    // Taken under this thread's own turn, so no holder is passing to it at that moment and
    // every later pass skips it.
    uint64_t ticket = TakeTicket();
    WaitForTurn(ticket);
    scattershot.QueueRetired[size_t(Id)] = 1;
    PassTurn(ticket + 1);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddCsvRow(int shot)
{
    bool sampled = false;
    #pragma omp critical (csvcounters)
    {
        sampled = scattershot.CsvEnabled == true && scattershot.CsvRows != -1 && scattershot.CsvCounter++ % config.CsvSamplePeriod == 0;
    }
    
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
            int labelsColumns = int(std::count(labels.begin(), labels.end(), ','));
            int rowColumns = int(std::count(row.begin(), row.end(), ','));

            return labelsColumns == rowColumns;
        }).executed;

    if (!rowValidated)
    {
        #pragma omp critical (print)
        {
            std::cout << "Unable to add row to CSV. Labels/Row have different column counts.\n";
        }

        return;
    }

    int retries = 0;
    #pragma omp critical (csvexport)
    {
        // CSV row export retry loop
        auto failedRowPos = scattershot.Csv.tellp();
        for (int retries = 0; retries < 5; retries++)
        {
            if (retries > 0)
            {
                #pragma omp critical (print)
                {
                    #pragma omp critical (csvcounters)
                    {
                        std::cout << "Retrying CSV row " << scattershot.CsvRows << ".\n";
                    }
                }
            }

            scattershot.Csv << shot << "," << this->GetCurrentFrame() << "," << sampled << "," << row << "\n";
            if (!scattershot.Csv.fail())
            {
                #pragma omp critical (csvcounters)
                {
                    scattershot.CsvRows++;
                }

                break;
            }
            else
            {
                #pragma omp critical (print)
                {
                    std::cout << "Error writing to CSV row " << scattershot.CsvRows << ": " << scattershot.Csv.rdstate() << "\n";
                }

                scattershot.Csv.close();
                scattershot.Csv = std::ofstream(scattershot.CsvFileName);
                scattershot.Csv.seekp(failedRowPos);
            }
        }

        if (retries == 5)
        {
            #pragma omp critical (print)
            {
                std::cout << "Exceeded CSV export retry count. Disabling CSV export.\n";
            }

            scattershot.CsvEnabled = false;
        }
    }
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
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::AddOption(std::size_t index, double probability, std::vector<bool>& set)
{
    if (probability <= 0.0)
        return;

    if (probability >= 1.0 || GetTempRng() % 65536 <= uint64_t(int(probability / 65535.0)))
    {
        if (index >= set.size())
            set.resize(index + 1, false);
        set[index] = true;
    }
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
bool ScattershotThread<TState, TResource, TStateTracker, TOutputState>::OptionSelected(std::size_t index, const std::vector<bool>& set)
{
    return index < set.size() && set[index];
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <class TOption>
void ScattershotThread<TState, TResource, TStateTracker, TOutputState>::DrawOption(std::initializer_list<std::pair<TOption, double>> weightedOptions, std::vector<bool>& set)
{
    std::array<std::pair<TOption, double>, MaxWeightedEntries> options;
    std::size_t count = SortedByKey(weightedOptions, options);
    if (count == 0)
        return;

    double maxRng = 65536.0;

    double totalWeight = 0;
    for (std::size_t i = 0; i < count; i++)
    {
        if (options[i].second > 0)
            totalWeight += options[i].second;
    }

    if (totalWeight == 0)
        return;

    double rng = double(GetTempRng() % (int)maxRng);
    double rngRangeMin = 0;
    for (std::size_t i = 0; i < count; i++)
    {
        if (options[i].second <= 0)
            continue;

        double rngRangeMax = rngRangeMin + options[i].second * maxRng / totalWeight;
        if (rng >= rngRangeMin && rng < rngRangeMax)
        {
            AddOption(std::size_t(options[i].first), 1.0, set);
            return;
        }

        rngRangeMin = rngRangeMax;
    }

    AddOption(std::size_t(options[count - 1].first), 1.0, set);
}

template <class TState, derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState>
template <class TKey>
std::size_t ScattershotThread<TState, TResource, TStateTracker, TOutputState>::SortedByKey(
    std::initializer_list<std::pair<TKey, double>> list, std::array<std::pair<TKey, double>, MaxWeightedEntries>& out)
{
    std::size_t count = 0;
    for (const auto& entry : list)
    {
        bool duplicate = false;
        for (std::size_t j = 0; j < count && !duplicate; j++)
            duplicate = out[j].first == entry.first;
        if (duplicate)
            continue;
        if (count == MaxWeightedEntries)
            throw std::length_error("more weighted entries than a scattershot script can hold");
        std::size_t i = count++;
        for (; i > 0 && entry.first < out[i - 1].first; i--)
            out[i] = out[i - 1];
        out[i] = entry;
    }
    return count;
}

#endif
