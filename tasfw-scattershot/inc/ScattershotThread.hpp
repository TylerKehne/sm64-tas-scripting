#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotThread.hpp is included by Scattershot.hpp, which it completes"
#else
#ifndef SCATTERSHOTTHREAD_H
#define SCATTERSHOTTHREAD_H

// A scattershot script's own moves: a public nested `enum class CustomMoves` in the script class,
// the magic name the way `CustomScriptStatus` is one.
template <class T>
concept HasCustomMoves = requires { typename T::CustomMoves; }
    && std::is_enum_v<typename T::CustomMoves>;

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

    template <typename F>
    static void ThreadLock(const char* /*section*/, F func)
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

    // What a scattershot script implements: its movement options and a move, its state bin, its
    // validation and fitness, and what a solution carries; the CSV hooks have defaults.
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

    // What it calls: the RNG (hard rule 3) and the movement options, the framework's moves and
    // its own.
    uint64_t GetTempRng();

    // The weights are a braced list of {option, weight} pairs, walked in BasicMoves order
    // whatever order the list gives them (the std::map this once took by value walked its keys
    // in that order; a duplicate option keeps its first weight, as the map's insert did), so
    // the draw is the same and nothing is allocated (ROADMAP 3.8). RandomInputs takes its
    // button probabilities the same way.
    void AddRandomMovementOption(std::initializer_list<std::pair<BasicMoves, double>> weightedOptions);
    void AddMovementOption(BasicMoves movementOption, double probability = 1.0);
    bool CheckMovementOptions(BasicMoves movementOption);
    Inputs RandomInputs(std::initializer_list<std::pair<Buttons, double>> buttonProbabilities);

    // The same three calls for a script's own moves, a public nested `enum class CustomMoves` in
    // the script class (HasCustomMoves above). Each takes the script's class from the object it is
    // called on (an explicit object parameter, C++23), so the element type of a braced list
    // is the script's enum before the braces are considered, a foreign enum does not
    // compile, and a script without the enum has only the BasicMoves overloads above.
    // The framework's own input groups (stick magnitude, direction, buttons) stay in
    // BasicMoves, which RandomInputs reads. Defined in-class: constrained member
    // templates are (docs/compilers.md).
    template <class Self> requires HasCustomMoves<Self>
    void AddRandomMovementOption(this Self& self, std::initializer_list<std::pair<typename Self::CustomMoves, double>> weightedOptions)
    {
        ScattershotThread& thread = self;
        thread.DrawOption(weightedOptions, thread.customMoves);
    }

    template <class Self> requires HasCustomMoves<Self>
    void AddMovementOption(this Self& self, typename Self::CustomMoves option, double probability = 1.0)
    {
        ScattershotThread& thread = self;
        thread.AddOption(std::size_t(option), probability, thread.customMoves);
    }

    template <class Self> requires HasCustomMoves<Self>
    bool CheckMovementOptions(this const Self& self, typename Self::CustomMoves option)
    {
        const ScattershotThread& thread = self;
        return OptionSelected(std::size_t(option), thread.customMoves);
    }

    // The lifecycle, the thread's own: one shot per execution.
    virtual bool validation();
    bool execution();
    virtual bool assertion();

private:
    Scattershot<TState, TResource, TStateTracker, TOutputState>& scattershot;
    int Id;
    uint64_t QueueCalls = 0; // tickets taken so far (deterministic mode)
    uint64_t RngHash = 0;
    uint64_t RngHashTemp = 0;
    TState BaseBlockStateBin;
    std::shared_ptr<Segment> BaseBlockTailSegment = nullptr;
    // Set by ValidateBaseBlock on a mismatch so execution() can decode the same block a second
    // time and report whether the two decodes agree with each other (ROADMAP 4.5 diagnosis).
    bool LastValidationFailed = false;
    TState LastDecodedBin;
    M64Diff LastDecodedDiff;

    // The options selected for the current script, one bit per BasicMoves. The enum
    // grows with every scenario and no size is assumed: the vector grows to the largest
    // option a script on this thread ever selects (a handful of times in a run) and is
    // cleared in place per script, so no script allocates for it.
    std::vector<bool> basicMoves;       // BasicMoves, the framework's enum
    std::vector<bool> customMoves;           // the script's own CustomMoves

    static constexpr std::size_t MaxWeightedEntries = 64;
    static constexpr uint64_t SpinBudget = 4096; // pauses before a waiter blocks on the turn (ROADMAP 3.15) // one draw's candidates, not the enum

    short startCourse;
    short startArea;

    // The shot: from the base block through the pellet to the encoded block.
    void Initialize();
    AdhocBaseScriptStatus ExecuteFromBaseBlockAndEncode(int shot);
    AdhocBaseScriptStatus DecodeBaseBlockDiffAndApply();
    bool ChooseScriptAndApply();
    void SelectBaseBlock(int mainIteration);
    bool ValidateBaseBlock(int shot);
    bool ValidateCourseAndArea();
    TState GetStateBinSafe();
    float GetStateFitnessSafe();

    // The RNG.
    uint64_t GetRng();
    void SetRng(uint64_t rngHash);
    void SetTempRng(uint64_t rngHash);
    template <typename T>
    uint64_t GetHash(const T& toHash) const;

    // The turn queue of the deterministic mode.
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

    // The deterministic mode's queue (ROADMAP 3.8): in deterministic mode func runs in this
    // thread's turn (Scattershot::QueueTurn), the same total order per seed on every run
    // whatever the threads' timing; otherwise it runs at once. A thread takes a ticket per
    // call, waits for its turn, and passes the turn on, skipping retired threads; it retires
    // when its shots are done.
    template <typename F>
    void QueueThreadById(bool deterministic, F func);
    uint64_t TakeTicket();
    void WaitForTurn(uint64_t ticket);
    void PassTurn(uint64_t next);
    void RetireFromQueue();

    // The CSV.
    void AddCsvRow(int shot);
    void AddCsvLabels();

    // The draw over movement options.
    void AddOption(std::size_t index, double probability, std::vector<bool>& set);
    static bool OptionSelected(std::size_t index, const std::vector<bool>& set);
    template <class TOption>
    void DrawOption(std::initializer_list<std::pair<TOption, double>> weightedOptions, std::vector<bool>& set);

    // The entries of a braced list in key order, the first of any duplicate key kept: the
    // order and the meaning a std::map built from the same list had. Returns the count.
    template <class TKey>
    static std::size_t SortedByKey(std::initializer_list<std::pair<TKey, double>> list, std::array<std::pair<TKey, double>, MaxWeightedEntries>& out);
};

//Include template method implementations
#include "ScattershotThread.t.hpp"

#endif
#endif
