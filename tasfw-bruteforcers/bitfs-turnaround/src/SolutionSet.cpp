#include "SolutionSet.hpp"
#include "StageArgs.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

using nlohmann::json;

json SolutionSet::ToJson() const
{
	json root;
	root["stage"] = stage;
	root["type"] = type;
	root["startFrame"] = startFrame;

	json list = json::array();
	for (const SolutionRecord& solution : solutions)
	{
		json frames = json::array();
		for (const auto& [frame, inputs] : solution.m64Diff.frames)
			frames.push_back(json::array({ frame, inputs.buttons, inputs.stick_x, inputs.stick_y }));

		json metrics = json::object();
		for (const auto& [name, value] : solution.metrics)
		{
			if (std::isfinite(value))
				metrics[name] = value;
			else
				metrics[name] = nullptr;
		}

		list.push_back(json { { "frames", std::move(frames) }, { "metrics", std::move(metrics) } });
	}
	root["solutions"] = std::move(list);
	return root;
}

SolutionSet SolutionSet::FromJson(const json& root)
{
	if (!root.is_object())
		throw std::runtime_error("solution set: top level must be an object");

	SolutionSet set;
	set.stage = root.value("stage", std::string());
	set.type = root.value("type", std::string());
	set.startFrame = root.value("startFrame", int64_t(-1));

	const json& list = root.value("solutions", json::array());
	if (!list.is_array())
		throw std::runtime_error("solution set: \"solutions\" must be an array");

	for (const json& item : list)
	{
		SolutionRecord record;

		// value() returns a copy; keep it alive for the whole loop (items() returns a proxy
		// into its argument and iterating a temporary through it is a use after free).
		const json frames = item.value("frames", json::array());
		for (const json& entry : frames)
		{
			if (!entry.is_array() || entry.size() != 4)
				throw std::runtime_error("solution set: each frame must be [frame, buttons, stickX, stickY]");
			record.m64Diff.frames[entry[0].get<uint64_t>()] =
				Inputs(entry[1].get<uint16_t>(), entry[2].get<int8_t>(), entry[3].get<int8_t>());
		}

		const json metrics = item.value("metrics", json::object());
		for (const auto& metric : metrics.items())
		{
			const json& value = metric.value();
			record.metrics[metric.key()] = value.is_number() ? value.get<double>() : std::numeric_limits<double>::infinity();
		}
		set.solutions.push_back(std::move(record));
	}
	return set;
}

void SolutionSet::Save(const std::filesystem::path& file) const
{
	std::filesystem::create_directories(file.parent_path());
	std::ofstream stream(file);
	if (!stream)
		throw std::runtime_error("cannot write " + file.string());
	stream << ToJson().dump(1, '\t') << '\n';
}

SolutionSet SolutionSet::Load(const std::filesystem::path& file)
{
	std::ifstream stream(file);
	if (!stream)
		throw std::runtime_error("cannot read " + file.string());
	json root;
	stream >> root;
	return FromJson(root);
}

std::optional<double> SolutionSet::Metric(const std::string& name) const
{
	if (solutions.empty())
		return std::nullopt;
	auto found = solutions.front().metrics.find(name);
	if (found == solutions.front().metrics.end())
		return std::nullopt;
	return found->second;
}

void SolutionSet::Select(const json& select, const std::string& where)
{
	if (select.is_null())
		return;
	RejectUnknownKeys(select, { "sortBy", "take" }, where);

	if (select.contains("sortBy"))
	{
		const json& keys = select.at("sortBy");
		if (!keys.is_array())
			ConfigError("\"sortBy\" in " + where + " must be an array of metric names");

		// Sort by the last key first so that earlier keys end up dominating (stable sort).
		for (auto it = keys.rbegin(); it != keys.rend(); ++it)
		{
			if (!it->is_string())
				ConfigError("\"sortBy\" in " + where + " must contain metric names");
			std::string key = it->get<std::string>();
			bool descending = !key.empty() && key[0] == '-';
			if (descending)
				key = key.substr(1);

			auto metricOf = [&](const SolutionRecord& record)
			{
				auto found = record.metrics.find(key);
				if (found == record.metrics.end())
					ConfigError("\"sortBy\" in " + where + ": a solution has no metric \"" + key + "\"");
				return found->second;
			};
			std::stable_sort(solutions.begin(), solutions.end(), [&](const SolutionRecord& a, const SolutionRecord& b)
				{ return descending ? metricOf(a) > metricOf(b) : metricOf(a) < metricOf(b); });
		}
	}

	if (select.contains("take"))
	{
		const json& take = select.at("take");
		if (!take.is_number_integer() || take.get<int64_t>() < 0)
			ConfigError("\"take\" in " + where + " must be a non-negative integer");
		size_t count = size_t(take.get<int64_t>());
		if (solutions.size() > count)
			solutions.resize(count);
	}
}
