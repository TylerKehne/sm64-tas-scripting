#pragma once
// Small JSON helpers shared by the pipeline config and the stage argument parsers. Every
// error is a std::runtime_error whose message starts with "config: " and names the offending
// key and where it was found.
#include <initializer_list>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

struct SolutionSet;

[[noreturn]] void ConfigError(const std::string& message);

// Keys starting with '_' are comments and always allowed.
void RejectUnknownKeys(const nlohmann::json& object, std::initializer_list<const char*> known, const std::string& where);

// `parent[key]`, which must exist and be an object.
const nlohmann::json& RequireObject(const nlohmann::json& parent, const char* key, std::string_view where);

// Numeric stage arguments are a JSON number, or the string "input:<metric>", which takes the
// metric from the first solution of the stage's input set.
double ArgNumber(const nlohmann::json& args, const char* key, const SolutionSet* input, const std::string& where);
double ArgNumber(const nlohmann::json& args, const char* key, const SolutionSet* input, double fallback, const std::string& where);
bool ArgBool(const nlohmann::json& args, const char* key, bool fallback, const std::string& where);
std::string ArgString(const nlohmann::json& args, const char* key, const std::string& fallback, const std::string& where);
