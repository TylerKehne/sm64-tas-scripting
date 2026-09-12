#include <doctest/doctest.h>

#include <PipelineConfig.hpp>
#include <SolutionSet.hpp>
#include <StageArgs.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

// The bitfs-turn config schema, solution files and argument helpers (ROADMAP 1.4), without
// the game: paths resolve against the file, unknown keys are rejected, solutions round-trip.

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	json BaseConfig()
	{
		return json::parse(R"({
			"_comment": "comments are keys starting with an underscore",
			"resources": { "dllDirectory": "../res", "threads": 3 },
			"m64": "../res/movie.m64",
			"outputDirectory": "../analysis",
			"scattershot": { "maxShots": 1234, "seed": 9 },
			"stages": [
				{ "name": "first", "type": "tilt-target", "startFrame": 3330,
				  "scattershot": { "seed": 111, "maxSolutions": 1 }, "args": { "targetNx": -0.1 } },
				{ "name": "second", "type": "osc-final", "startFrame": 3400, "input": "first", "m64": "other.m64",
				  "select": { "sortBy": ["-fSpd"], "take": 2 }, "export": true }
			]
		})");
	}

	SolutionRecord Record(uint64_t frame, double fSpd, double nx)
	{
		SolutionRecord record;
		record.m64Diff.frames[frame] = Inputs(uint16_t(frame & 0xFFFF), int8_t(frame % 100), int8_t(-int(frame % 50)));
		record.metrics["fSpd"] = fSpd;
		record.metrics["pyraNormX"] = nx;
		return record;
	}
}

TEST_CASE("Pipeline config resolves paths against its directory and expands the DLL pattern")
{
	PipelineConfig p = PipelineConfig::Parse(BaseConfig(), fs::path("base/cfg"));
	CHECK(p.baseDirectory == fs::path("base/cfg"));
	CHECK(p.dllDirectory.generic_string() == "base/res");
	CHECK(p.m64.generic_string() == "base/res/movie.m64");
	CHECK(p.outputDirectory.generic_string() == "base/analysis");
	CHECK(p.threads == 3);
	CHECK(p.saveMode == LibSm64SaveMode::Dirty);

	auto dlls = p.DllPaths();
	REQUIRE(dlls.size() == 3);
	CHECK(dlls[0].generic_string() == "base/res/sm64_jp_0.dll");
	CHECK(dlls[2].generic_string() == "base/res/sm64_jp_2.dll");

	REQUIRE(p.stages.size() == 2);
	CHECK(p.stages[0].name == "first");
	CHECK_FALSE(p.stages[0].input.has_value());
	CHECK(p.stages[1].input == std::string("first"));
	CHECK(p.stages[1].m64->generic_string() == "base/cfg/other.m64");
	CHECK(p.stages[1].exportM64);
	CHECK(p.FindStage("second") == &p.stages[1]);
	CHECK(p.FindStage("third") == nullptr);
	CHECK(p.SolutionsFile("first").generic_string() == "base/analysis/solutions/first.json");
}

TEST_CASE("baseDirectory in the file overrides the file's location")
{
	json j = BaseConfig();
	j["baseDirectory"] = "elsewhere";
	PipelineConfig p = PipelineConfig::Parse(j, fs::path("base/cfg"));
	CHECK(p.dllDirectory.generic_string() == "res");
	CHECK(p.m64.generic_string() == "res/movie.m64");

	// Absolute paths are kept as they are.
	j["m64"] = fs::absolute("abs.m64").string();
	p = PipelineConfig::Parse(j, fs::path("base/cfg"));
	CHECK(p.m64 == fs::absolute("abs.m64").lexically_normal());
}

TEST_CASE("Scattershot settings layer defaults, pipeline overrides and stage overrides")
{
	PipelineConfig p = PipelineConfig::Parse(BaseConfig(), fs::path("base/cfg"));

	Configuration first = p.ScattershotConfiguration(p.stages[0]);
	CHECK(first.PelletMaxScripts == 20);     // default
	CHECK(first.MaxShots == 1234);           // pipeline
	CHECK(first.Seed == 111);                // stage wins
	CHECK(first.MaxSolutions == 1);
	CHECK(first.StartFrame == 3330);
	CHECK(first.TotalThreads == 3);
	CHECK(first.ResourcePaths.size() == 3);
	CHECK(first.M64Path.generic_string() == "base/res/movie.m64");
	CHECK(first.CsvOutputDirectory.back() == fs::path::preferred_separator);

	Configuration second = p.ScattershotConfiguration(p.stages[1]);
	CHECK(second.Seed == 9);                 // pipeline, no stage override
	CHECK(second.MaxSolutions == 100);       // default
	CHECK(second.StartFrame == 3400);
	CHECK(second.M64Path.generic_string() == "base/cfg/other.m64");
}

TEST_CASE("Unknown keys and bad references are rejected with the location in the message")
{
	auto parse = [](json j) { return PipelineConfig::Parse(j, fs::path("base/cfg")); };

	json j = BaseConfig();
	j["scattershot"]["maxShotz"] = 1;
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("maxShotz"), std::runtime_error);

	j = BaseConfig();
	j["stages"][0]["scattershot"]["deterministc"] = true;
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("stages[first].scattershot"), std::runtime_error);

	j = BaseConfig();
	j["stages"][1]["input"] = "third";
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("earlier stage"), std::runtime_error);

	j = BaseConfig();
	j["stages"][1]["name"] = "first";
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("duplicate"), std::runtime_error);

	j = BaseConfig();
	j["resources"]["threads"] = 0;
	CHECK_THROWS_AS(parse(j), std::runtime_error);

	j = BaseConfig();
	j["resources"]["dllPattern"] = "single.dll";
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("{}"), std::runtime_error);

	j = BaseConfig();
	j.erase("m64");
	CHECK_THROWS_WITH_AS(parse(j), doctest::Contains("m64"), std::runtime_error);

	j = BaseConfig();
	j["stages"] = json::array();
	CHECK_THROWS_AS(parse(j), std::runtime_error);

	// Underscore keys are comments anywhere.
	j = BaseConfig();
	j["stages"][0]["args"]["_why"] = "because";
	j["resources"]["_note"] = 1;
	CHECK_NOTHROW(parse(j));
}

TEST_CASE("Solution sets round-trip through JSON, including non-finite metrics")
{
	SolutionSet set;
	set.stage = "dr";
	set.type = "dr-oscillations";
	set.startFrame = 3330;
	set.solutions.push_back(Record(3340, 30.5, -0.2));
	set.solutions.push_back(Record(3341, 12.0, std::numeric_limits<double>::infinity()));
	set.solutions[1].m64Diff.frames[3350] = Inputs(0x8000, -128, 127);

	json j = set.ToJson();
	CHECK(j["solutions"][1]["metrics"]["pyraNormX"].is_null());

	SolutionSet back = SolutionSet::FromJson(json::parse(j.dump()));
	CHECK(back.stage == "dr");
	CHECK(back.type == "dr-oscillations");
	CHECK(back.startFrame == 3330);
	REQUIRE(back.solutions.size() == 2);
	CHECK(back.solutions[0].m64Diff.frames == set.solutions[0].m64Diff.frames);
	CHECK(back.solutions[1].m64Diff.frames == set.solutions[1].m64Diff.frames);
	CHECK(back.solutions[1].m64Diff.frames.at(3350) == Inputs(0x8000, -128, 127));
	CHECK(back.solutions[0].metrics.at("fSpd") == 30.5);
	CHECK(std::isinf(back.solutions[1].metrics.at("pyraNormX")));

	CHECK(set.Metric("fSpd") == 30.5);
	CHECK_FALSE(set.Metric("missing").has_value());
	CHECK_FALSE(SolutionSet().Metric("fSpd").has_value());
}

TEST_CASE("Select sorts by metrics in order and keeps the first n")
{
	SolutionSet set;
	set.solutions.push_back(Record(1, 10, 0.5));
	set.solutions.push_back(Record(2, 30, 0.1));
	set.solutions.push_back(Record(3, 30, 0.0));
	set.solutions.push_back(Record(4, 20, 0.9));

	SolutionSet sorted = set;
	sorted.Select(json::parse(R"({"sortBy": ["-fSpd", "pyraNormX"], "take": 3})"), "test");
	REQUIRE(sorted.solutions.size() == 3);
	CHECK(sorted.solutions[0].m64Diff.frames.begin()->first == 3); // fSpd 30, nx 0.0
	CHECK(sorted.solutions[1].m64Diff.frames.begin()->first == 2); // fSpd 30, nx 0.1
	CHECK(sorted.solutions[2].m64Diff.frames.begin()->first == 4); // fSpd 20

	SolutionSet untouched = set;
	untouched.Select(json(), "test");
	CHECK(untouched.solutions.size() == 4);

	SolutionSet bad = set;
	CHECK_THROWS_WITH_AS(bad.Select(json::parse(R"({"sortBy": ["nope"]})"), "test"), doctest::Contains("nope"), std::runtime_error);
	CHECK_THROWS_AS(bad.Select(json::parse(R"({"takes": 1})"), "test"), std::runtime_error);
}

TEST_CASE("Stage arguments come from the JSON or from the input's first solution")
{
	SolutionSet input;
	input.stage = "tilt";
	input.solutions.push_back(Record(5, 42.0, -0.25));

	json args = json::parse(R"({"a": 7, "b": "input:fSpd", "c": "input:nope", "d": "seven", "flag": true, "name": "z"})");
	CHECK(ArgNumber(args, "a", &input, "t") == 7);
	CHECK(ArgNumber(args, "b", &input, "t") == 42.0);
	CHECK(ArgNumber(args, "missing", &input, 1.5, "t") == 1.5);
	CHECK_THROWS_WITH_AS(ArgNumber(args, "c", &input, "t"), doctest::Contains("nope"), std::runtime_error);
	CHECK_THROWS_WITH_AS(ArgNumber(args, "b", nullptr, "t"), doctest::Contains("no input"), std::runtime_error);
	CHECK_THROWS_AS(ArgNumber(args, "d", &input, "t"), std::runtime_error);
	CHECK_THROWS_WITH_AS(ArgNumber(args, "missing", &input, "t"), doctest::Contains("missing"), std::runtime_error);
	CHECK(ArgBool(args, "flag", false, "t"));
	CHECK_FALSE(ArgBool(args, "other", false, "t"));
	CHECK_THROWS_AS(ArgBool(args, "a", false, "t"), std::runtime_error);
	CHECK(ArgString(args, "name", "x", "t") == "z");
	CHECK(ArgString(args, "other", "x", "t") == "x");
	CHECK_THROWS_WITH_AS(RejectUnknownKeys(args, { "a", "b", "c", "d", "flag" }, "t"), doctest::Contains("name"), std::runtime_error);
}
