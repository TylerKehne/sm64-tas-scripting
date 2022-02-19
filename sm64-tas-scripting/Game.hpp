#pragma once

#include <string.h>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "Inputs.hpp"
#include "SharedLib.hpp"

#ifndef GAME_H
  #define GAME_H

using namespace std;

// structs like this can be aggregate-initialized
// like SegVal {".data", 0xDEADBEEF, 12345678};
struct SegVal {
  string name;
  void* address;
  size_t length;
  
  static SegVal fromSectionData(const std::string& name, SectionInfo info) {
    return {name, info.address, info.length};
  }
};

class Game;

class Slot {
public:
  std::vector<uint8_t> buf1;
  std::vector<uint8_t> buf2;
  Game* game = NULL;

  Slot() {}

  ~Slot();
};

class Game {
public:
  uint64_t _totalFrameAdvanceTime = 0;
  uint64_t _totalLoadStateTime    = 0;
  uint64_t _totalSaveStateTime    = 0;
  uint64_t nFrameAdvances         = 0;
  uint64_t nLoadStates            = 0;
  uint64_t nSaveStates            = 0;

  SharedLib dll;
  std::vector<SegVal> segment;
  Slot startSave = Slot();

  Game(const char* dll_path) : dll(dll_path) {
    // constructor of SharedLib will throw if it can't load
    void* processID = dll.get("sm64_init");

    // Macro evalutes to nothing on Linux and __stdcall on Windows
    // looks cleaner
    using pICFUNC = int(TAS_FW_STDCALL*)();

    pICFUNC sm64_init;
    sm64_init = pICFUNC(processID);

    sm64_init();
    
    auto sects = dll.readSections();
    segment = std::vector<SegVal> {
      SegVal {".data", sects[".data"].address, sects[".data"].length},
      SegVal {".bss", sects[".bss"].address, sects[".bss"].length},
    };

    save_state(&startSave);
  }

  void advance_frame();
  bool save_state(Slot* slot);
  void load_state(Slot* slot);
  intptr_t addr(const char* symbol);
  uint32_t getCurrentFrame();
  bool shouldSave(uint64_t framesSinceLastSave);

private:
  friend class Slot;

  const int64_t _saveMemLimit = 1024 * 1024 * 1024;  // 1 GB
  int64_t _currentSaveMem     = 0;
};

#endif