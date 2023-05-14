add_library(tasfw-scattershot
	"src/Scattershot.cpp"
)
target_include_directories(tasfw-scattershot PUBLIC inc)
target_link_libraries(tasfw-scattershot PUBLIC tasfw-core)

add_library(tasfw::scattershot ALIAS tasfw-scattershot)