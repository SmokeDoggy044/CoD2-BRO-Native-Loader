# cod_bro_launcher: settings and game-file setup, then starts the game. Portable; it
# outlives route B (only COD_BRO_GAME_EXECUTABLE changes).

# Dear ImGui: cmake/imgui.cmake.

# The update check and the updater (no UI), also used by the tests.
add_library(launcher_update STATIC update_check.cpp updater.cpp texture_packs.cpp game_files.cpp)
target_include_directories(launcher_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(launcher_update PUBLIC platform)
target_compile_definitions(launcher_update PRIVATE COD_BRO_VERSION="${PROJECT_VERSION}")

add_executable(cod_bro_launcher WIN32
    main.cpp
    front_picture.cpp
)
cod_bro_windows_resources(cod_bro_launcher "Call of Duty 2: Big Red One native loader - launcher" PROGRAM)
target_link_libraries(cod_bro_launcher PRIVATE imgui_sdlrenderer launcher_update platform settings_ui input_bindings audio_devices text)
# Only declarations from src/render/ (enhanced_graphics.h: the settings).
target_include_directories(cod_bro_launcher PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_VERSION="${PROJECT_VERSION}"
    COD_BRO_RELEASES="SmokeDoggy044/CoD2-BRO-Native-Loader")
# This platform's release archive (cmake/release*.cmake, packaging/sniper): the updater fetches it.
if(WIN32)
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_PACKAGE="windows.zip")
else()
    target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_PACKAGE="linux.tar.gz")
endif()
# The program Play starts: our own engine. The multiplayer still runs on the route-B loader (its renderer is the
# original's), so Multiplayer starts that.
target_compile_definitions(cod_bro_launcher PRIVATE COD_BRO_GAME_EXECUTABLE="cod_bro"
    COD_BRO_MULTIPLAYER_EXECUTABLE="cod_bro_loader")

# The known editions' file lists (tools/game_file_tables.py) next to the launcher, as in a release.
add_custom_target(game_file_table ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/game_files.txt ${CMAKE_BINARY_DIR}/bin/game_files.txt
    COMMENT "Copying game_files.txt")
add_dependencies(cod_bro_launcher game_file_table)
