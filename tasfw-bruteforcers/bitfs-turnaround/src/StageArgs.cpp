#include "StageArgs.hpp"
#include "SolutionSet.hpp"

#include <stdexcept>

using nlohmann::json;

void ConfigError(const std::string& message)
{
	throw std::runtime_error("config: " + message);
}

void RejectUnknownKeys(const json& object, std::initializer_list<const char*> known, const std::string& where)
{
	if (!object.is_object())
		ConfigError(where + " must be an object");

	for (const auto& item : object.items())
	{
		const std::string& key = item.key();
		if (!key.empty() && key[0] == '_')
			continue;
		bool ok = false;
		for (const char* candidate : known)
		{
			if (key == candidate)
			{
				ok = true;
				break;
			}
		}
		if (!ok)
			ConfigError("unknown key \"" + key + "\" in " + where);
	}
}

// `where` is a string_view rather than a const std::string&: a literal at the call site would
// bind a temporary to the reference, and GCC 13 (-Wdangling-reference) assumes the returned
// reference might point into it (docs/compilers.md).
const json& RequireObject(const json& parent, const char* key, std::string_view where)
{
	if (!parent.contains(key))
		ConfigError(std::string("missing \"") + key + "\" in " + std::string(where));
	const json& value = parent.at(key);
	if (!value.is_object())
		ConfigError(std::string("\"") + key + "\" in " + std::string(where) + " must be an object");
	return value;
}

double ArgNumber(const json& args, const char* key, const SolutionSet* input, const std::string& where)
{
	if (!args.contains(key))
		ConfigError(std::string("missing \"") + key + "\" in " + where);

	const json& value = args.at(key);
	if (value.is_number())
		return value.get<double>();

	if (value.is_string())
	{
		std::string text = value.get<std::string>();
		if (text.rfind("input:", 0) == 0)
		{
			std::string metric = text.substr(6);
			if (!input)
				ConfigError(std::string("\"") + key + "\" in " + where + " refers to " + text + " but the stage has no input");
			std::optional<double> found = input->Metric(metric);
			if (!found)
				ConfigError(std::string("\"") + key + "\" in " + where + ": the input stage \"" + input->stage
					+ "\" has no metric \"" + metric + "\" (or no solutions)");
			return *found;
		}
	}

	ConfigError(std::string("\"") + key + "\" in " + where + " must be a number or \"input:<metric>\"");
}

double ArgNumber(const json& args, const char* key, const SolutionSet* input, double fallback, const std::string& where)
{
	return args.contains(key) ? ArgNumber(args, key, input, where) : fallback;
}

bool ArgBool(const json& args, const char* key, bool fallback, const std::string& where)
{
	if (!args.contains(key))
		return fallback;
	const json& value = args.at(key);
	if (!value.is_boolean())
		ConfigError(std::string("\"") + key + "\" in " + where + " must be true or false");
	return value.get<bool>();
}

std::string ArgString(const json& args, const char* key, const std::string& fallback, const std::string& where)
{
	if (!args.contains(key))
		return fallback;
	const json& value = args.at(key);
	if (!value.is_string())
		ConfigError(std::string("\"") + key + "\" in " + where + " must be a string");
	return value.get<std::string>();
}
