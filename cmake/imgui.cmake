# Dear ImGui (MIT), fetched at configure time on every platform: the launcher's window
# (SDL renderer back end) and the in-game menu (render/, Direct3D 9 back end).
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(imgui_sdlrenderer STATIC ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_link_libraries(imgui_sdlrenderer PUBLIC imgui)

# The Direct3D 9 back end needs d3d9.h: DXVK's (dxvk-native) on Linux, MinGW's on Windows.
add_library(imgui_dx9 STATIC ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
target_link_libraries(imgui_dx9 PUBLIC imgui)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DXVK_D3D9 REQUIRED IMPORTED_TARGET dxvk-d3d9)
    target_link_libraries(imgui_dx9 PRIVATE PkgConfig::DXVK_D3D9)
endif()
