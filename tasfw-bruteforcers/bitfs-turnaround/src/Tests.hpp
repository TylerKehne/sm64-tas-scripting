#pragma once
// The executable's own tests: `bitfs-turn --test [doctest arguments]`. They run the game
// through the pipeline's config (the DLL, the movie and the stages it names), so they are
// optional: neither CI nor the framework's suite (tasfw-tests) runs them.
#include <filesystem>

int RunTests(const std::filesystem::path& config, int argc, char** argv);
