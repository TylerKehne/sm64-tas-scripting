#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#ifndef VISUALIZATION_H
#define VISUALIZATION_H

// The run's viewer (ROADMAP 4.4): a program the search launches, detached, when its CSV opens,
// given these parameters and the CSV's path in a JSON file beside the CSV, and told through the
// same file when the run ends. The search never waits for it or reads from it; a run without
// a CSV (CsvSamplePeriod 0) launches nothing. The parameters are what the run knows and the
// viewer cannot guess: the columns it plots, how it bins them, what it keeps. The viewer's own
// settings, its refresh rate and its port, are its own. analysis/visualizer.py is the viewer
// (README.md, "The viewer"); the builders take this through Visualize().
class Visualization
{
public:
    // Keep the rows whose column lies in [min, max]; either bound may be left open.
    struct Filter
    {
        std::string column;
        double min = -std::numeric_limits<double>::infinity();
        double max = std::numeric_limits<double>::infinity();
    };

    // A fixed view: a width-by-height window of the plot's units centered on (x, y), drawn
    // square to its units and never rescaled. Without one the plot follows its data.
    struct View
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
    };

    std::string script;                          // the viewer's path
    std::string interpreter = DefaultInterpreter();
    std::string title;
    std::string x = "MarioZ";                    // the plot's axes and the arrows' direction and length
    std::string y = "MarioX";
    std::string angle = "MarioFYaw";
    std::string speed = "MarioFSpd";
    double binX = 0.1;                           // one arrow per bin, the newest block's
    double binY = 0.1;
    double binAngle = 16;
    double binSpeed = 0.1;
    std::vector<Filter> filters;
    std::optional<View> view;
    bool sampledOnly = true;                     // rows the sample period chose, not the forced ones

    static std::string DefaultInterpreter()
    {
#ifdef _WIN32
        return "pythonw"; // no console window
#else
        return "python3";
#endif
    }

    // The parameters file the viewer reads: this plus the CSV's path, and at the end of the run
    // the same with finished true and the row count. Written whole and renamed into place, so
    // the viewer never reads a partial file. Defined below the JSON conversions it uses.
    void Write(const std::filesystem::path& file, const std::string& csv, bool finished, int64_t rows) const;

    // The command that starts the viewer and returns at once: the platform's detach syntax is
    // the one platform fork here. The viewer gets the null device for its standard handles
    // rather than inheriting the search's: a parent capturing the search's output through a
    // pipe would otherwise wait for the viewer to close.
    std::string Command(const std::filesystem::path& paramsFile) const
    {
        std::string program = interpreter.find(' ') == std::string::npos ? interpreter : "\"" + interpreter + "\"";
#ifdef _WIN32
        return "start \"\" " + program + " \"" + script + "\" \"" + paramsFile.string() + "\" <NUL >NUL 2>&1";
#else
        return "nohup " + program + " '" + script + "' '" + paramsFile.string() + "' </dev/null >/dev/null 2>&1 &";
#endif
    }

    void Launch(const std::filesystem::path& paramsFile) const
    {
        std::string command = Command(paramsFile);
        std::cout << "Viewer: " << command << "\n";
        int result = std::system(command.c_str());
        if (result != 0)
            std::cout << "Viewer launch returned " << result << "; the viewer logs beside its script.\n";
    }
};

inline void to_json(nlohmann::json& json, const Visualization::Filter& filter)
{
    json = nlohmann::json { { "column", filter.column } };
    if (filter.min != -std::numeric_limits<double>::infinity())
        json["min"] = filter.min;
    if (filter.max != std::numeric_limits<double>::infinity())
        json["max"] = filter.max;
}

inline void from_json(const nlohmann::json& json, Visualization::Filter& filter)
{
    filter.column = json.at("column").get<std::string>();
    filter.min = json.value("min", filter.min);
    filter.max = json.value("max", filter.max);
}

inline void to_json(nlohmann::json& json, const Visualization::View& view)
{
    json = nlohmann::json { { "x", view.x }, { "y", view.y }, { "width", view.width }, { "height", view.height } };
}

inline void from_json(const nlohmann::json& json, Visualization::View& view)
{
    view.x = json.at("x").get<double>();
    view.y = json.at("y").get<double>();
    view.width = json.at("width").get<double>();
    view.height = json.at("height").get<double>();
}

inline void to_json(nlohmann::json& json, const Visualization& visualization)
{
    json = nlohmann::json {
        { "script", visualization.script },
        { "interpreter", visualization.interpreter },
        { "title", visualization.title },
        { "x", visualization.x },
        { "y", visualization.y },
        { "angle", visualization.angle },
        { "speed", visualization.speed },
        { "binX", visualization.binX },
        { "binY", visualization.binY },
        { "binAngle", visualization.binAngle },
        { "binSpeed", visualization.binSpeed },
        { "filters", visualization.filters },
        { "sampledOnly", visualization.sampledOnly },
    };
    if (visualization.view)
        json["view"] = *visualization.view;
}

// Absent keys keep the defaults, so a stage's "visualize" block names only what it changes.
inline void from_json(const nlohmann::json& json, Visualization& visualization)
{
    visualization.script = json.value("script", visualization.script);
    visualization.interpreter = json.value("interpreter", visualization.interpreter);
    visualization.title = json.value("title", visualization.title);
    visualization.x = json.value("x", visualization.x);
    visualization.y = json.value("y", visualization.y);
    visualization.angle = json.value("angle", visualization.angle);
    visualization.speed = json.value("speed", visualization.speed);
    visualization.binX = json.value("binX", visualization.binX);
    visualization.binY = json.value("binY", visualization.binY);
    visualization.binAngle = json.value("binAngle", visualization.binAngle);
    visualization.binSpeed = json.value("binSpeed", visualization.binSpeed);
    visualization.filters = json.value("filters", visualization.filters);
    if (json.contains("view"))
        visualization.view = json.at("view").get<Visualization::View>();
    visualization.sampledOnly = json.value("sampledOnly", visualization.sampledOnly);
}

inline void Visualization::Write(const std::filesystem::path& file, const std::string& csv, bool finished, int64_t rows) const
{
    nlohmann::json json = *this;
    json["csv"] = csv;
    json["finished"] = finished;
    json["rows"] = rows;
    std::filesystem::path partial = file;
    partial += ".tmp";
    {
        std::ofstream out(partial);
        out << json.dump(1, '\t') << "\n";
    }
    std::filesystem::rename(partial, file);
}

#endif
