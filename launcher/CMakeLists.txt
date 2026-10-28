# cod_bro_launcher: settings and game-file setup, then starts the game. Portable; it
# outlives route B (only COD_BRO_GAME_EXECUTABLE changes).

# Dear ImGui: cmake/imgui.cmake.

# The update check and the updater (no UI), also used by the tests.
add_library(launcher_update STATIC update_check.cpp updater.cpp sha256.cpp)
target_include_directories(launcher_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(launcher_update PUBLIC platform)
target_compile_definitions(launcher_update PRIVATE COD_BRO_VERSION="${PROJECT_VERSION}")

add_executable(cod_bro_launcher WIN32
    main.cpp
)
target_link_libraries(cod_bro_launcher PRIVATE imgui_sdlrenderer launcher_update platform input_bindings)
target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_VERSION="${PROJECT_VERSION}"
    COD_BRO_RELEASES="SmokeDoggy044/CoD2-BRO-Native-Loader")
# This platform's release archive (cmake/release*.cmake, packaging/sniper): the updater fetches it.
if(WIN32)
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_PACKAGE="windows.zip")
else()
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_PACKAGE="linux.tar.gz")
endif()
# The program Play starts: the route-B loader while there is one, else the game.
if(COD_BRO_BUILD_LOADER)
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_GAME_EXECUTABLE="cod_bro_loader")
else()
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_GAME_EXECUTABLE="cod_bro")
endif()
