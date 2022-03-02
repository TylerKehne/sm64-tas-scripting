# BitFS turnaround script. Scripts can likely be moved to /lib, given that they will be chained together?

add_executable(bitfs-turn
	"src/BitFsConfig.cpp"
	"src/BitFsConfig.hpp"
	"src/BitFsPyramidOscillation.cpp"
	"src/BitFsPyramidOscillation_Iteration.cpp"
	"src/BitFsPyramidOscillation_RunDownhill.cpp"
	"src/BitFsPyramidOscillation_TurnAroundAndRunDownhill.cpp"
	"src/BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle.cpp"
	"src/BitFsPyramidOscillation_TurnThenRunDownhill.cpp"
	"src/BrakeToIdle.cpp"
	"src/GetMinimumDownhillWalkingAngle.cpp"
	"src/main.cpp"
	"src/Script_BitFsPyramidOscillation.hpp"
	"src/ScriptDefs.hpp"
	"src/Script_General.hpp"
	"src/TryHackedWalkOutOfBounds.cpp"
)
target_compile_definitions(bitfs-turn PRIVATE cxx_std_20)
target_link_libraries(bitfs-turn PRIVATE tasfw::core tasfw::decomp nlohmann_json::nlohmann_json)

include(MatchArchFlag)
match_arch_and_ipo(bitfs-turn)