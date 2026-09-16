#include <doctest/doctest.h>

#include "script_fixtures.hpp"

#include <cstdint>
#include <stdexcept>

// Metric scripts on the mock resource (script_fixtures.hpp): per-frame state computed on
// demand and recursively, in a sandbox that leaves the cursor where it was, typed by the
// metric script the root installs, and following the diff through Modify and Execute.

using namespace tasfw::tests;

// A child script that reads metrics names its metric script once; a metric script names nothing
// (script_fixtures.hpp, RecursiveMetrics reads its own previous frame).
class MetricsReader : public Script<MockResource>
{
public:
	using MetricScript = RecursiveMetrics;

	bool validation() override { return true; }
	bool execution() override
	{
		CustomStatus.sum = GetMetrics(GetCurrentFrame()).sum;
		CustomStatus.exists = MetricsExist(GetCurrentFrame());
		return true;
	}
	bool assertion() override { return true; }

	class CustomScriptStatus
	{
	public:
		uint64_t sum = 0;
		bool exists = false;
	};
	CustomScriptStatus CustomStatus;
};

static_assert(std::is_same_v<MetricScriptOf<RecursiveMetrics>::type, RecursiveMetrics>, "a metric script reads its own state");
static_assert(std::is_same_v<MetricScriptOf<MetricsReader>::type, RecursiveMetrics>, "a child reads the metric script it declares");

TEST_CASE("Metric scripts compute per-frame state, recursively, without moving the cursor")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveMetrics>(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(In(i));

			auto at10 = s.GetMetrics(10);
			CHECK(at10.initialized);
			CHECK(at10.sum == 55); // 0 + 1 + ... + 10

			auto at4 = s.GetMetrics(4);
			CHECK(at4.sum == 10);
			CHECK(s.GetCurrentFrame() == 10);

			// Rewriting frame 6 invalidates metrics after it; they are recomputed.
			s.Load(6);
			s.AdvanceFrameWrite(In(60));
			for (int i = 7; i < 10; i++)
				s.AdvanceFrameWrite(In(i));
			CHECK(s.GetMetrics(10).sum == 55);
		});
}

TEST_CASE("A metrics ahead of the cursor is computed in a sandbox and the cursor stays")
{
	MockResource resource;
	M64 m64;
	for (int i = 0; i < 20; i++)
		m64.frames[i] = In(100 + i);

	RunRoot<RecursiveMetrics>(resource, m64, [&resource](auto& s)
		{
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameRead();
			CHECK(s.GetCurrentFrame() == 5);
			uint64_t checksum = resource.checksum();

			// Frame 12 is seven frames ahead: the metric script advances there in its own sandbox
			// and is reverted; the requesting script does not move and writes nothing.
			const auto& at12 = s.GetMetrics(12);
			CHECK(at12.sum == 78); // 0 + 1 + ... + 12
			CHECK(s.GetCurrentFrame() == 5);
			CHECK(resource.checksum() == checksum);
			CHECK(s.IsDiffEmpty());

			// The frames computed on the way are cached: no further frame advances.
			uint64_t advances = resource.work.frameAdvances;
			CHECK(s.GetMetrics(12).sum == 78);
			CHECK(s.GetMetrics(9).sum == 45);
			CHECK(resource.work.frameAdvances == advances);
		});
}

TEST_CASE("Asking for a metric script type the root does not install throws instead of miscasting")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveMetrics>(resource, m64, [](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			CHECK_THROWS_AS(s.template GetMetrics<OtherMetrics>(1), std::runtime_error);
			CHECK_THROWS_AS(s.template MetricsExist<OtherMetrics>(1), std::runtime_error);
			// The right type still works afterwards.
			CHECK(s.GetMetrics(1).sum == 1);
		});

	// A root without a metric script rejects every metric script type.
	RunRoot(resource, m64, [](auto& s)
		{
			CHECK_THROWS_AS(s.template GetMetrics<OtherMetrics>(0), std::runtime_error);
		});
}

TEST_CASE("A metric script that does not assert leaves a default state that is not recomputed")
{
	MockResource resource;
	M64 m64;
	RunRoot<EvenFramesMetrics>(resource, m64, [&resource](auto& s)
		{
			for (int i = 0; i < 4; i++)
				s.AdvanceFrameWrite(In(i));

			CHECK(s.GetMetrics(2).initialized);
			CHECK(s.GetMetrics(2).frame == 2);
			CHECK_FALSE(s.GetMetrics(3).initialized);
			CHECK(s.MetricsExist(3)); // stored, as a default

			uint64_t advances = resource.work.frameAdvances;
			CHECK_FALSE(s.GetMetrics(3).initialized);
			CHECK(resource.work.frameAdvances == advances); // served from the table
		});
}

TEST_CASE("Metrics follow the diff: kept by Modify, dropped by Execute")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveMetrics>(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 3; i++)
				s.AdvanceFrameWrite(In(i));

			// Execute: the child's frames are reverted, and so are their states.
			s.template Execute<WriteFrames>(4, 10);
			CHECK(s.GetCurrentFrame() == 3);
			CHECK_FALSE(s.MetricsExist(6));

			// Modify: the frames persist and their states are handed to the parent.
			s.template Modify<WriteFrames>(4, 20);
			CHECK(s.GetCurrentFrame() == 7);
			CHECK(s.MetricsExist(6));
			CHECK(s.GetMetrics(7).sum == 28); // 0 + ... + 7

			// A reference into the table stays valid until a write invalidates that frame.
			const auto& at5 = s.GetMetrics(5);
			CHECK(at5.sum == 15);
			CHECK(s.GetMetrics(7).sum == 28); // unrelated lookup
			CHECK(at5.sum == 15);
		});
}

TEST_CASE("GetMetrics names no metric script: a child reads the one it declares under a root that installs it")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveMetrics>(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameWrite(In(i));
			auto status = s.template Execute<MetricsReader>();
			CHECK(status.executed);
			CHECK(status.sum == 15); // 0 + ... + 5 at frame 5
			CHECK(status.exists);
			CHECK(s.GetMetrics(5).sum == 15); // the root, through its own alias
		});
}
