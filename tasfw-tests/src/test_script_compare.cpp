#include <doctest/doctest.h>

#include "script_fixtures.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

// The Compare family (ScriptCompareHelper.hpp) on the mock resource. Two things are pinned:
//   * the concepts that describe its callables (parameter generator, comparator, terminator,
//     ad-hoc candidate) hold only for the right signature, and a wrong one makes every
//     overload non-viable at the call site rather than failing inside the instantiation
//     (ROADMAP 3.10);
//   * the runners keep the candidate the comparator prefers, stop at the one the terminator
//     accepts, and revert or apply the way Execute and Modify do.

using namespace tasfw::tests;

namespace
{
	using Status = ScriptStatus<WriteFrames>;
	using OtherStatus = ScriptStatus<FailingScript>;
	using Params = std::tuple<int, int>; // WriteFrames(count, seed)

	struct Reach
	{
		int64_t lastFrame = -1;
	};
	struct OtherReach
	{
		int value = 0;
	};
	using AdhocStatus = AdhocScriptStatus<Reach>;

	// The shapes the family asks for, as plain function objects so that a mismatch is a
	// substitution failure and never an error inside a lambda body.
	struct Generator { bool operator()(int64_t, Params&) const { return false; } };
	struct Comparator { const Status* operator()(const Status* a, const Status*) const { return a; } };
	struct Terminator { bool operator()(const Status*) const { return false; } };
	struct AdhocComparator { const AdhocStatus* operator()(const AdhocStatus* a, const AdhocStatus*) const { return a; } };
	struct AdhocTerminator { bool operator()(const AdhocStatus*) const { return false; } };
	struct AdhocBody { bool operator()(Reach*, int, int) const { return true; } };
	struct AdhocBodyNoParams { bool operator()(Reach*) const { return true; } };

	// One thing wrong each.
	struct GeneratorReturnsInt { int operator()(int64_t, Params&) const { return 0; } };
	struct GeneratorWithoutParams { bool operator()(int64_t) const { return false; } };
	struct ComparatorReturnsBool { bool operator()(const Status*, const Status*) const { return true; } };
	struct ComparatorOneArg { const Status* operator()(const Status* a) const { return a; } };
	struct ComparatorOtherScript { const OtherStatus* operator()(const OtherStatus* a, const OtherStatus*) const { return a; } };
	struct TerminatorReturnsInt { int operator()(const Status*) const { return 0; } };
	struct TerminatorNoArg { bool operator()() const { return false; } };
	struct AdhocComparatorReturnsBool { bool operator()(const AdhocStatus*, const AdhocStatus*) const { return true; } };
	struct AdhocTerminatorReturnsPointer { const AdhocStatus* operator()(const AdhocStatus* a) const { return a; } };
	struct AdhocBodyReturnsInt { int operator()(Reach*, int, int) const { return 0; } };
	struct AdhocBodyMissingParam { bool operator()(Reach*, int) const { return true; } };
	struct AdhocBodyWithoutStatus { bool operator()(int, int) const { return true; } };
}

// The concepts themselves.
static_assert(ScriptParamsGenerator<Generator, Params>);
static_assert(!ScriptParamsGenerator<GeneratorReturnsInt, Params>);
static_assert(!ScriptParamsGenerator<GeneratorWithoutParams, Params>);
static_assert(!ScriptParamsGenerator<Generator, std::tuple<int>>);

static_assert(ScriptComparator<Comparator, WriteFrames>);
static_assert(!ScriptComparator<ComparatorReturnsBool, WriteFrames>);
static_assert(!ScriptComparator<ComparatorOneArg, WriteFrames>);
static_assert(!ScriptComparator<ComparatorOtherScript, WriteFrames>);
static_assert(!ScriptComparator<Comparator, FailingScript>);

static_assert(ScriptTerminator<Terminator, WriteFrames>);
static_assert(!ScriptTerminator<TerminatorReturnsInt, WriteFrames>);
static_assert(!ScriptTerminator<TerminatorNoArg, WriteFrames>);
static_assert(!ScriptTerminator<Terminator, FailingScript>);

static_assert(AdhocScriptComparator<AdhocComparator, Reach>);
static_assert(!AdhocScriptComparator<AdhocComparatorReturnsBool, Reach>);
static_assert(!AdhocScriptComparator<AdhocComparator, OtherReach>);

static_assert(AdhocScriptTerminator<AdhocTerminator, Reach>);
static_assert(!AdhocScriptTerminator<AdhocTerminatorReturnsPointer, Reach>);
static_assert(!AdhocScriptTerminator<AdhocTerminator, OtherReach>);

static_assert(AdhocCompareScript<AdhocBody, Reach, Params>);
static_assert(AdhocCompareScript<AdhocBody, Reach, const Params>); // a container's element, as ExecuteFromTupleAdhoc sees it
static_assert(AdhocCompareScript<AdhocBodyNoParams, Reach, std::tuple<>>);
static_assert(!AdhocCompareScript<AdhocBodyReturnsInt, Reach, Params>);
static_assert(!AdhocCompareScript<AdhocBodyMissingParam, Reach, Params>);
static_assert(!AdhocCompareScript<AdhocBodyWithoutStatus, Reach, Params>);
static_assert(!AdhocCompareScript<AdhocBody, OtherReach, Params>);
static_assert(!AdhocCompareScript<AdhocBody, Reach, int>);

// The script's constructor against the parameter tuple (SharedLib.hpp), which the same
// overloads require.
static_assert(constructible_from_tuple<WriteFrames, Params>);
static_assert(constructible_from_tuple<WriteFrames, const Params>);
static_assert(constructible_from_tuple<FailingScript, std::tuple<>>);
static_assert(!constructible_from_tuple<WriteFrames, std::tuple<int>>);
static_assert(!constructible_from_tuple<WriteFrames, std::tuple<int, int, int>>);
static_assert(!constructible_from_tuple<WriteFrames, std::tuple<int, const char*>>);
static_assert(!constructible_from_tuple<WriteFrames, int>);

// The call site: a wrong callable leaves no viable overload.
namespace
{
	struct NoBody
	{
		template <class T>
		void operator()(T&) const {}
	};
	using Root = TestRoot<NoBody>;

	template <class TList>
	concept CompareTakesList = requires(Root& root, const TList& list, Comparator comparator)
	{
		root.template Compare<WriteFrames>(list, comparator);
	};

	template <class F>
	concept CompareTakesComparator = requires(Root& root, const std::vector<Params>& list, F comparator)
	{
		root.template Compare<WriteFrames>(list, comparator);
	};

	template <class F>
	concept CompareTakesTerminator = requires(Root& root, const std::vector<Params>& list, Comparator comparator, F terminator)
	{
		root.template Compare<WriteFrames>(list, comparator, terminator);
	};

	template <class F>
	concept CompareTakesGenerator = requires(Root& root, F generator, Comparator comparator)
	{
		root.template Compare<WriteFrames, Params>(generator, comparator);
	};

	template <class F>
	concept CompareAdhocTakesBody = requires(Root& root, const std::vector<Params>& list, F body, AdhocComparator comparator)
	{
		root.template CompareAdhoc<Reach>(list, body, comparator);
	};

	template <class F>
	concept CompareAdhocTakesComparator = requires(Root& root, const std::vector<Params>& list, AdhocBody body, F comparator)
	{
		root.template CompareAdhoc<Reach>(list, body, comparator);
	};

	template <class F>
	concept CompareAdhocTakesTerminator = requires(Root& root, F generator, AdhocBody body, AdhocComparator comparator, AdhocTerminator terminator)
	{
		root.template CompareAdhoc<Reach, Params>(generator, body, comparator, terminator);
	};
}

static_assert(CompareTakesList<std::vector<Params>>);
static_assert(!CompareTakesList<std::vector<std::tuple<int>>>);
static_assert(!CompareTakesList<std::vector<std::tuple<int, int, int>>>);

static_assert(CompareTakesComparator<Comparator>);
static_assert(!CompareTakesComparator<ComparatorReturnsBool>);
static_assert(!CompareTakesComparator<ComparatorOneArg>);
static_assert(!CompareTakesComparator<ComparatorOtherScript>);

static_assert(CompareTakesTerminator<Terminator>);
static_assert(!CompareTakesTerminator<TerminatorReturnsInt>);
static_assert(!CompareTakesTerminator<TerminatorNoArg>);

static_assert(CompareTakesGenerator<Generator>);
static_assert(!CompareTakesGenerator<GeneratorReturnsInt>);
static_assert(!CompareTakesGenerator<GeneratorWithoutParams>);

static_assert(CompareAdhocTakesBody<AdhocBody>);
static_assert(!CompareAdhocTakesBody<AdhocBodyReturnsInt>);
static_assert(!CompareAdhocTakesBody<AdhocBodyMissingParam>);
static_assert(!CompareAdhocTakesBody<AdhocBodyWithoutStatus>);

static_assert(CompareAdhocTakesComparator<AdhocComparator>);
static_assert(!CompareAdhocTakesComparator<AdhocComparatorReturnsBool>);

static_assert(CompareAdhocTakesTerminator<Generator>);
static_assert(!CompareAdhocTakesTerminator<GeneratorReturnsInt>);

// The runners, written the way the BitFS scripts write them: generic lambdas.

namespace
{
	// Prefers the candidate that reached the later frame.
	constexpr auto furthest = [](auto incumbent, auto challenger)
	{
		return challenger->lastFrame > incumbent->lastFrame ? challenger : incumbent;
	};
}

TEST_CASE("Compare runs every candidate, keeps the one the comparator prefers and reverts")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			std::vector<Params> candidates { { 2, 10 }, { 5, 20 }, { 3, 30 } };
			int compared = 0;
			auto status = s.template Compare<WriteFrames>(candidates,
				[&](auto incumbent, auto challenger) //comparator
				{
					compared++;
					return furthest(incumbent, challenger);
				});

			REQUIRE(status.asserted);
			CHECK(status.lastFrame == 4);
			CHECK(compared == 2);
			REQUIRE(status.m64Diff.frames.size() == 5);
			for (int i = 0; i < 5; i++)
				CHECK(status.m64Diff.frames.at(i) == In(20 + i));

			// Execute semantics: nothing of any candidate stays in the parent.
			CHECK(s.GetCurrentFrame() == 0);
			CHECK(s.IsDiffEmpty());
			CHECK(resource.checksum() == 0);
		});
}

TEST_CASE("A terminator ends the comparison at the first candidate it accepts")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			std::vector<Params> candidates { { 2, 10 }, { 5, 20 }, { 7, 30 } };
			int compared = 0;
			auto status = s.template Compare<WriteFrames>(candidates,
				[&](auto incumbent, auto challenger) //comparator
				{
					compared++;
					return furthest(incumbent, challenger);
				},
				[](auto candidate) { return candidate->lastFrame >= 3; }); //terminator

			REQUIRE(status.asserted);
			CHECK(status.lastFrame == 4); // the second candidate; the third, further, never ran
			CHECK(compared == 0);         // the terminator is asked before the comparator
			CHECK(s.GetCurrentFrame() == 0);
			CHECK(s.IsDiffEmpty());
		});
}

TEST_CASE("ModifyCompare applies the winner's frames to the parent")
{
	std::vector<Params> candidates;
	SUBCASE("the winner in the middle") { candidates = { { 2, 10 }, { 5, 20 }, { 3, 30 } }; }
	SUBCASE("the winner last") { candidates = { { 2, 10 }, { 3, 30 }, { 5, 20 } }; }

	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			auto status = s.template ModifyCompare<WriteFrames>(candidates, furthest);

			REQUIRE(status.asserted);
			CHECK(status.lastFrame == 4);
			CHECK(s.GetCurrentFrame() == 5);
			M64Diff diff = s.GetDiff();
			REQUIRE(diff.frames.size() == 5);
			for (int i = 0; i < 5; i++)
				CHECK(diff.frames.at(i) == In(20 + i));
			CHECK(resource.state().buttons == In(24).buttons);
		});
}

TEST_CASE("The generator form asks for one parameter tuple per iteration")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			int asked = 0;
			auto status = s.template Compare<WriteFrames, Params>(
				[&](auto iteration, auto& params) //paramsGenerator
				{
					asked++;
					if (iteration >= 3)
						return false;

					params = Params(int(iteration) + 1, 10 * (int(iteration) + 1));
					return true;
				},
				furthest);

			REQUIRE(status.asserted);
			CHECK(status.lastFrame == 2);
			CHECK(asked == 4);
			CHECK(status.m64Diff.frames.at(0) == In(30));
			CHECK(s.GetCurrentFrame() == 0);
			CHECK(s.IsDiffEmpty());
		});
}

TEST_CASE("CompareAdhoc hands each candidate its status and the tuple's elements")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			std::vector<Params> candidates { { 2, 10 }, { 5, 20 }, { 7, 30 } };
			auto status = s.template CompareAdhoc<Reach>(candidates,
				[&](auto reach, int count, int seed) //script
				{
					for (int i = 0; i < count; i++)
						s.AdvanceFrameWrite(In(seed + i));
					reach->lastFrame = s.GetCurrentFrame() - 1;
					return count != 7; // the furthest candidate rejects itself
				},
				furthest);

			REQUIRE(status.executed);
			CHECK(status.lastFrame == 4);
			REQUIRE(status.m64Diff.frames.size() == 5);
			CHECK(status.m64Diff.frames.at(4) == In(24));
			CHECK(s.GetCurrentFrame() == 0);
			CHECK(s.IsDiffEmpty());
		});
}

TEST_CASE("ModifyCompareAdhoc keeps the winner's frames")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			std::vector<Params> candidates { { 2, 10 }, { 5, 20 }, { 7, 30 } };
			auto status = s.template ModifyCompareAdhoc<Reach>(candidates,
				[&](auto reach, int count, int seed) //script
				{
					for (int i = 0; i < count; i++)
						s.AdvanceFrameWrite(In(seed + i));
					reach->lastFrame = s.GetCurrentFrame() - 1;
					return count != 7;
				},
				furthest);

			REQUIRE(status.executed);
			CHECK(status.lastFrame == 4);
			CHECK(s.GetCurrentFrame() == 5);
			M64Diff diff = s.GetDiff();
			REQUIRE(diff.frames.size() == 5);
			for (int i = 0; i < 5; i++)
				CHECK(diff.frames.at(i) == In(20 + i));
		});
}

TEST_CASE("DynamicModifyCompareAdhoc mutates between candidates and applies the winner on its mutations")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			std::vector<Params> candidates { { 2, 10 }, { 5, 20 }, { 3, 30 } };
			int mutations = 0;
			auto status = s.template DynamicModifyCompareAdhoc<Reach>(candidates,
				[&](auto reach, int count, int seed) //script
				{
					for (int i = 0; i < count; i++)
						s.AdvanceFrameWrite(In(seed + i));
					reach->lastFrame = s.GetCurrentFrame() - 1;
					return true;
				},
				[&]() //mutator: one frame forward before every candidate but the first
				{
					s.AdvanceFrameWrite(In(90 + mutations));
					mutations++;
					return true;
				},
				furthest);

			REQUIRE(status.executed);
			CHECK(mutations == 2);
			CHECK(status.nMutations == 1);           // the winner ran after the first mutation
			CHECK(status.substatus.lastFrame == 5);  // frames 1..5, after that mutation's frame 0

			// The parent holds the winner's mutations and then the winner: the second
			// mutation, made for the third candidate, is gone.
			CHECK(s.GetCurrentFrame() == 6);
			M64Diff diff = s.GetDiff();
			REQUIRE(diff.frames.size() == 6);
			CHECK(diff.frames.at(0) == In(90));
			for (int i = 1; i <= 5; i++)
				CHECK(diff.frames.at(i) == In(20 + i - 1));
			CHECK(resource.state().buttons == In(24).buttons);
		});
}
