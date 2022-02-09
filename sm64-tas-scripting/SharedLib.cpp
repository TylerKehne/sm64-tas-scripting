#include "SharedLib.hpp"

#include <codecvt>
#include <errhandlingapi.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <libloaderapi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <winnt.h>


#if defined(_WIN32)
#include <windows.h>

SharedLib::SharedLib(const char *fileName)
    : libFileName(fileName), handle([fileName]() -> HMODULE {
        // Strings are UTF-8, so we need to convert to UTF-16 for proper Win32
        // handling
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wideName = converter.from_bytes(fileName);
        HMODULE res = LoadLibraryW(wideName.c_str());
        if (res == nullptr) {
          DWORD lastError = GetLastError();
          throw std::system_error(lastError, std::system_category());
        }
        return res;
      }()) {}
SharedLib::~SharedLib() {
  bool good = FreeLibrary(handle);
  if (!good) {
    DWORD lastError = GetLastError();
    std::cerr << "FreeLibrary error: " << std::system_error(lastError, std::system_category()).what() << '\n';
    std::cerr << "terminating...\n";
    std::terminate();
  }
}
void* SharedLib::get(const char* symbol) {
  FARPROC res = GetProcAddress(handle, symbol);
  if (res == nullptr) {
    DWORD lastError = GetLastError();
    throw std::system_error(lastError, std::system_category());
  }
  
  return reinterpret_cast<void*>(res);
}
std::unordered_map<std::string, SectionInfo> SharedLib::readSections() {
  using std::ios_base;
  
  std::unordered_map<std::string, SectionInfo> sectionMap;
  std::ifstream file(libFileName);
  
  {
    file.seekg(0x40, ios_base::beg);
    IMAGE_FILE_HEADER header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
  }
}
#elif defined(__linux__)

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

SharedLib::SharedLib(const char *fileName)
    : libFileName(fileName), handle([fileName]() -> void * {
        void *res = dlopen(fileName, RTLD_NOW);
        if (res == nullptr) {
          throw std::runtime_error(dlerror());
        }
        return res;
      }()) {}
SharedLib::~SharedLib() {
  int res = dlclose(handle);
  if (res != 0) {
    std::cerr << "dlclose error: " << dlerror() << '\n';
    std::cerr << "terminating...\n";
    std::terminate();
  }
}
void *SharedLib::get(const char *symbol) {
  dlerror();
  void *res = dlsym(handle, symbol);
  char *err = dlerror();
  if (err != nullptr) {
    throw std::runtime_error(err);
  }
  return res;
}
std::unordered_map<std::string, SectionInfo> SharedLib::readSections() {
  using std::ios_base;

  std::unordered_map<std::string, SectionInfo> sectionMap;

  intptr_t baseAddress;
  {
    link_map *linkMap;
    int returnCode = dlinfo(handle, RTLD_DI_LINKMAP, &linkMap);
    if (returnCode == -1) {
      throw std::runtime_error(dlerror());
    }
    baseAddress = linkMap->l_addr;
  }

  std::ifstream file(libFileName);
  std::unique_ptr<Elf64_Shdr[]> sections;
  std::unique_ptr<char[]> strTable;
  uint16_t numSections;
  {
    // read header
    Elf64_Ehdr header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.e_shoff == 0) {
      throw std::runtime_error("No section table");
    }
    numSections = header.e_shnum;
    // read section headers
    file.seekg(header.e_shoff, ios_base::beg);
    sections = std::make_unique<Elf64_Shdr[]>(header.e_shnum);
    file.read(reinterpret_cast<char *>(sections.get()),
              header.e_shnum * sizeof(Elf64_Shdr));

    const Elf64_Shdr &strtab_sect = sections[header.e_shstrndx];

    file.seekg(strtab_sect.sh_offset, ios_base::beg);
    strTable = std::make_unique<char[]>(strtab_sect.sh_size);
    file.read(strTable.get(), strtab_sect.sh_size);
  }

  for (uint16_t i = 0; i < numSections; i++) {
    const auto &sect = sections[i];
    sectionMap[&strTable[sect.sh_name]] = SectionInfo{
        reinterpret_cast<void *>(baseAddress + sect.sh_addr), sect.sh_size};
  }
  return sectionMap;
}
#endif