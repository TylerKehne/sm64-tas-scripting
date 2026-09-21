#include <doctest/doctest.h>

#include <Visualization.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

// The run's viewer (ROADMAP 4.4), without a viewer: what the search writes for it and how it
// launches it. The viewer itself is analysis/visualizer.py, which CI renders a CSV with.

using nlohmann::json;
namespace fs = std::filesystem;

TEST_CASE("Visualization round-trips through JSON and absent keys keep their defaults")
{
	Visualization v;
	v.script = "analysis/visualizer.py";
	v.title = "tilt-x";
	v.binX = 5;
	v.filters.push_back(Visualization::Filter { "NormalDistance", 0, 100 });
	v.filters.push_back(Visualization::Filter { "Phase" }); // both bounds open

	json j = v;
	CHECK(j["x"] == "MarioZ");
	CHECK(j["y"] == "MarioX");
	CHECK(j["binX"] == 5);
	CHECK(j["binAngle"] == 16);
	CHECK(j["sampledOnly"] == true);
	CHECK(j["filters"][0]["min"] == 0);
	CHECK(j["filters"][0]["max"] == 100);
	CHECK(!j["filters"][1].contains("min")); // JSON has no infinity; an open bound is an absent key
	CHECK(!j["filters"][1].contains("max"));

	Visualization back = j.get<Visualization>();
	CHECK(back.script == v.script);
	CHECK(back.title == v.title);
	CHECK(back.interpreter == Visualization::DefaultInterpreter());
	CHECK(back.binX == 5);
	CHECK(back.binY == 0.1);
	REQUIRE(back.filters.size() == 2);
	CHECK(back.filters[0].column == "NormalDistance");
	CHECK(back.filters[0].max == 100);
	CHECK(std::isinf(back.filters[1].min));
	CHECK(std::isinf(back.filters[1].max));

	Visualization partial = json::parse(R"({ "y": "MarioY", "filters": [{ "column": "MarioYVel", "max": 0 }] })").get<Visualization>();
	CHECK(partial.x == "MarioZ");
	CHECK(partial.y == "MarioY");
	CHECK(partial.speed == "MarioFSpd");
	REQUIRE(partial.filters.size() == 1);
	CHECK(std::isinf(partial.filters[0].min));
	CHECK(partial.filters[0].max == 0);
	CHECK_THROWS(json::parse(R"({ "filters": [{ "min": 0 }] })").get<Visualization>()); // a filter names its column

	// A fixed view is optional: absent, the key is absent and the plot follows its data.
	CHECK(!j.contains("view"));
	CHECK(!back.view.has_value());
	v.view = Visualization::View { -715, -1945, 900, 900 };
	json withView = v;
	CHECK(withView["view"]["x"] == -715);
	CHECK(withView["view"]["height"] == 900);
	Visualization viewed = withView.get<Visualization>();
	REQUIRE(viewed.view.has_value());
	CHECK(viewed.view->y == -1945);
	CHECK(viewed.view->width == 900);
	CHECK_THROWS(json::parse(R"({ "view": { "x": 0, "y": 0, "width": 900 } })").get<Visualization>()); // a view needs all four
}

TEST_CASE("The parameters file carries the CSV's path and, at the end, the run's row count")
{
	fs::path dir = fs::temp_directory_path() / "tasfw-test-visualization";
	fs::create_directories(dir);
	fs::path file = dir / "csv_1.visualizer.json";

	Visualization v;
	v.script = "analysis/visualizer.py";
	v.title = "osc-final";
	v.Write(file, "out/csv_1.csv", false, 0);
	json started = json::parse(std::ifstream(file));
	CHECK(started["csv"] == "out/csv_1.csv");
	CHECK(started["finished"] == false);
	CHECK(started["rows"] == 0);
	CHECK(started["title"] == "osc-final");
	CHECK(started["angle"] == "MarioFYaw");

	v.Write(file, "out/csv_1.csv", true, 42);
	json finished = json::parse(std::ifstream(file));
	CHECK(finished["finished"] == true);
	CHECK(finished["rows"] == 42);
	CHECK(!fs::exists(dir / "csv_1.visualizer.json.tmp")); // written whole, renamed into place

	fs::remove_all(dir);
}

TEST_CASE("The launch command detaches the viewer and quotes its paths")
{
	Visualization v;
	v.interpreter = "py";
	v.script = "C:/a b/visualizer.py";
	std::string command = v.Command(fs::path("out/csv_1.visualizer.json"));
#ifdef _WIN32
	CHECK(command.starts_with("start \"\" py "));
	CHECK(command.ends_with("<NUL >NUL 2>&1")); // not the search's handles: a pipe on them would wait for the viewer
#else
	CHECK(command.starts_with("nohup py "));
	CHECK(command.ends_with("</dev/null >/dev/null 2>&1 &"));
#endif
	CHECK(command.find("a b/visualizer.py") != std::string::npos);
	CHECK(command.find("csv_1.visualizer.json") != std::string::npos);
	CHECK(command.find(" a b/") == std::string::npos); // the path with a space is quoted

	v.interpreter = "C:/Program Files/Python/pythonw.exe";
	CHECK(v.Command(fs::path("p.json")).find("\"C:/Program Files/Python/pythonw.exe\"") != std::string::npos);
}
