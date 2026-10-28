# Xbox Direct3D 8 on a D3D9 back end (portable).
add_library(render STATIC
    backend_d3d9.cpp
    d3d9_draw.cpp
    d3d9_overlay.cpp
    d3d9_shader.cpp
    device.cpp
    draw.cpp
    nv2a.cpp
    resource.cpp
    shader.cpp
    state.cpp
    texture.cpp
    texture_decode.cpp
)
target_include_directories(render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render PUBLIC xbox platform imgui_dx9)

# D3D9 from DXVK (dxvk-native) on Linux. Fedora: dxvk-native-devel, plus its .i686
# package for the loader. On Windows nothing is linked: the back end loads DXVK's
# d3d9.dll (release: dxvk/) or the system's at run time. SDL3 comes from the
# top-level CMakeLists.txt.
if(WIN32)
    target_link_libraries(render PRIVATE SDL3::SDL3)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DXVK_D3D9 REQUIRED IMPORTED_TARGET dxvk-d3d9)
    target_link_libraries(render PRIVATE SDL3::SDL3 PkgConfig::DXVK_D3D9)
endif()
