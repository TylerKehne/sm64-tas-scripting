set(CMAKE_EXPORT_COMPILE_COMMANDS true)

cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project(sm64-tas-scripting
  DESCRIPTION "TASing framework for SM64"
  LANGUAGES CXX
)
# TODO: separate library files into a separate target
add_executable(main
  "sm64-tas-scripting/BitFsPyramidOscillation.cpp"
  "sm64-tas-scripting/BitFsPyramidOscillation_Iteration.cpp"
  "sm64-tas-scripting/BitFsPyramidOscillation_RunDownhill.cpp"
  "sm64-tas-scripting/BitFsPyramidOscillation_TurnAroundAndRunDownhill.cpp"
  "sm64-tas-scripting/BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle.cpp"
  "sm64-tas-scripting/BitFsPyramidOscillation_TurnThenRunDownhill.cpp"
  "sm64-tas-scripting/BrakeToIdle.cpp"
  "sm64-tas-scripting/Camera.hpp"
  "sm64-tas-scripting/Game.cpp"
  "sm64-tas-scripting/Game.hpp"
  "sm64-tas-scripting/GetMinimumDownhillWalkingAngle.cpp"
  "sm64-tas-scripting/Inputs.cpp"
  "sm64-tas-scripting/Inputs.hpp"
  "sm64-tas-scripting/main.cpp"
  "sm64-tas-scripting/math.cpp"
  "sm64-tas-scripting/math.hpp"
  "sm64-tas-scripting/ObjectFields.hpp"
  "sm64-tas-scripting/pyramid.cpp"
  "sm64-tas-scripting/pyramid.hpp"
  "sm64-tas-scripting/Script_BitFsPyramidOscillation.hpp"
  "sm64-tas-scripting/Script.cpp"
  "sm64-tas-scripting/Script_General.hpp"
  "sm64-tas-scripting/Script.hpp"
  "sm64-tas-scripting/SharedLib.cpp"
  "sm64-tas-scripting/SharedLib.hpp"
  "sm64-tas-scripting/Sm64.hpp"
  "sm64-tas-scripting/surface.cpp"
  "sm64-tas-scripting/surface.hpp"
  "sm64-tas-scripting/Trig.cpp"
  "sm64-tas-scripting/Trig.hpp"
  "sm64-tas-scripting/TryHackedWalkOutOfBounds.cpp"
  "sm64-tas-scripting/Types.hpp"
  "sm64-tas-scripting/UltraTypes.hpp"
)
target_compile_features(main PUBLIC cxx_std_20)