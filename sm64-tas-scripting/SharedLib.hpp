#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
  #include <windows.h>
  
  #define TAS_FW_STDCALL __stdcall
#elif defined(__linux__)
  #define TAS_FW_STDCALL
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
  
  // Reads out a list of sections.
  // Do cache the results, as this WILL re-read the file each time it's run.
  std::unordered_map<std::string, SectionInfo> readSections();
};