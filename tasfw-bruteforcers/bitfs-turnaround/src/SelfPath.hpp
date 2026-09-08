#pragma once
#include <filesystem>

// Absolute path of the running executable; the default config.json lives next to it.
const std::filesystem::path& getPathToSelf();
