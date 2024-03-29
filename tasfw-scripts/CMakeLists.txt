add_library(tasfw-scripts-general STATIC
	"src/general/GetMinimumDownhillWalkingAngle.cpp"
	"src/general/BrakeToIdle.cpp"
	"src/general/TryHackedWalkOutOfBounds.cpp"
)
target_include_directories(tasfw-scripts-general PUBLIC inc)
target_link_libraries(tasfw-scripts-general PUBLIC tasfw::core tasfw::resources)
add_optimization_flags(tasfw-scripts-general)
add_library(tasfw::scripts_general ALIAS tasfw-scripts-general)

add_library(tasfw-scripts-bitfs-pyramid-osc STATIC
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_TurnThenRunDownhill.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_TurnAroundAndRunDownhill.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_RunDownhill.cpp"
	"src/bitfs-pyramid-osc/BitFsPyramidOscillation_Iteration.cpp"
)
target_link_libraries(tasfw-scripts-bitfs-pyramid-osc PUBLIC tasfw-scripts-general)
add_optimization_flags(tasfw-scripts-bitfs-pyramid-osc)
add_library(tasfw::scripts_bitfs_pyramid_osc ALIAS tasfw-scripts-bitfs-pyramid-osc)

add_library(tasfw-scripts-bitfs-sc-setup STATIC
	"src/bitfs-sc-setup/BitFsScApproach.cpp"
	"src/bitfs-sc-setup/BitFsScApproach_AttemptDr.cpp"
	"src/bitfs-sc-setup/BitFsScApproach_AttemptDr_BF.cpp"
)

add_library(tasfw-scripts-scattershot-bitfs-dr STATIC
	"src/scattershot-bitfsdr/Scattershot_BitfsDr.cpp"
	"src/scattershot-bitfsdr/StateTracker_BitfsDr.cpp"
)

target_link_libraries(tasfw-scripts-bitfs-sc-setup PUBLIC tasfw-scripts-general)
target_link_libraries(tasfw-scripts-bitfs-pyramid-osc PUBLIC tasfw-scripts-general)
target_link_libraries(tasfw-scripts-scattershot-bitfs-dr PUBLIC tasfw-core)
target_link_libraries(tasfw-scripts-scattershot-bitfs-dr PUBLIC tasfw-scattershot)
target_link_libraries(tasfw-scripts-scattershot-bitfs-dr PUBLIC tasfw-scripts-general)
target_link_libraries(tasfw-scripts-scattershot-bitfs-dr PUBLIC tasfw-scripts-bitfs-pyramid-osc)

add_optimization_flags(tasfw-scripts-bitfs-sc-setup)
add_library(tasfw::scripts_bitfs_sc_setup ALIAS tasfw-scripts-bitfs-sc-setup)
