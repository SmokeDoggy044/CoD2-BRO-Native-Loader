# Decompiled game code (route C): portable C++. Until it is complete, the route-B
# loader runs it in place of the original functions or compares the two
# (functions.h, loader/compare.h).
set(game_code_sources
    functions.cpp
    script_string.cpp
)
# Linux loader builds: a shared library, so that the comparison harness can tell this
# code apart from the loader's (its test processes keep only this library, the C
# runtime and the title's code executable).
if(COD_BRO_BUILD_LOADER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(game_code SHARED ${game_code_sources})
    set_target_properties(game_code PROPERTIES OUTPUT_NAME cod_bro_game
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    add_library(game_code STATIC ${game_code_sources})
endif()
target_include_directories(game_code PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Only platform's headers (compiler.h): its library is not built for a shared object.
target_include_directories(game_code PRIVATE ${PROJECT_SOURCE_DIR}/platform)
target_link_libraries(game_code PRIVATE cod_bro_options)
