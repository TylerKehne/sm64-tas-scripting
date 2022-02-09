#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__linux__)
#endif

struct SectionInfo {
  void* address;
  size_t length;
};

class SharedLib {
  std::string libFileName;
#if defined(_WIN32)
  HMODULE handle;
#elif defined(__linux__)
  void* handle;
#endif
public:
  SharedLib(const char* fileName);
  ~SharedLib();
  
  void* get(const char* symbol);
  std::unordered_map<std::string, SectionInfo> readSections();
};