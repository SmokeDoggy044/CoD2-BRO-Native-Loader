# The Xbox's texture layouts as PC formats (texture_decode.h): the back end's Xbox path and the
# renderer C's textures on our graphics interface (game/code/ngl_gfx.cpp).
add_library(texture_formats STATIC texture_decode.cpp)
set_target_properties(texture_formats PROPERTIES POSITION_INDEPENDENT_CODE ON)  # in the game library too
target_include_directories(texture_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${PROJECT_SOURCE_DIR}/xbox)

# Xbox Direct3D 8 on a D3D9 back end (portable).
add_library(render STATIC
    backend_d3d9.cpp
    d3d9_draw.cpp
    d3d9_overlay.cpp
    d3d9_shader.cpp
    device.cpp
    gfx_d3d9.cpp
    gfx_verify.cpp
    draw.cpp
    nv2a.cpp
    resource.cpp
    shader.cpp
    state.cpp
    texture.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/title_shaders.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/post_shaders.cpp
    d3d9_post.cpp
    graphics_settings.cpp
)
target_include_directories(render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The title's shaders as HLSL of our own (shaders/*.hlsl, tools/shader_hlsl.py), compiled with
# vkd3d-shader (Fedora: vkd3d-compiler) and embedded (title_shaders.h).
find_program(VKD3D_COMPILER vkd3d-compiler)
if(NOT VKD3D_COMPILER)
    message(FATAL_ERROR "vkd3d-compiler not found (Fedora: dnf install vkd3d-compiler; Debian/Ubuntu: vkd3d-compiler)")
endif()
file(GLOB title_shader_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl)
set(title_shader_binaries "")
foreach(hlsl ${title_shader_sources})
    get_filename_component(name ${hlsl} NAME_WE)
    if(name MATCHES "^vs_")
        set(profile vs_3_0)
    else()
        set(profile ps_3_0)
    endif()
    set(binary ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.d3dbc)
    add_custom_command(OUTPUT ${binary}
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
                       COMMAND ${VKD3D_COMPILER} -x hlsl -p ${profile} -b d3dbc -o ${binary} ${hlsl}
                       DEPENDS ${hlsl} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nv2a.hlsli
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders  # for #include
                       VERBATIM)
    list(APPEND title_shader_binaries ${binary})
endforeach()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/title_shaders.cpp
                   COMMAND ${CMAKE_COMMAND} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/title_shaders.cpp
                           "-DINPUTS=${title_shader_binaries}" -P ${PROJECT_SOURCE_DIR}/cmake/embed_shaders.cmake
                   DEPENDS ${title_shader_binaries} ${PROJECT_SOURCE_DIR}/cmake/embed_shaders.cmake
                   VERBATIM)
target_link_libraries(render PUBLIC xbox platform imgui_dx9 texture_formats)

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

# The post passes (enhanced graphics: post/*.hlsl; vs_3_0 for post_vs, ps_3_0 for the rest).
file(GLOB post_shader_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/post/*.hlsl)
set(post_shader_binaries "")
foreach(hlsl ${post_shader_sources})
    get_filename_component(name ${hlsl} NAME_WE)
    if(name STREQUAL "post_vs")
        set(profile vs_3_0)
    else()
        set(profile ps_3_0)
    endif()
    set(binary ${CMAKE_CURRENT_BINARY_DIR}/post/${name}.d3dbc)
    add_custom_command(OUTPUT ${binary}
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/post
                       COMMAND ${VKD3D_COMPILER} -x hlsl -p ${profile} -b d3dbc -o ${binary} ${hlsl}
                       DEPENDS ${hlsl}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/post
                       VERBATIM)
    list(APPEND post_shader_binaries ${binary})
endforeach()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/post_shaders.cpp
                   COMMAND ${CMAKE_COMMAND} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/post_shaders.cpp
                           "-DINPUTS=${post_shader_binaries}" -P ${PROJECT_SOURCE_DIR}/cmake/embed_named_shaders.cmake
                   DEPENDS ${post_shader_binaries} ${PROJECT_SOURCE_DIR}/cmake/embed_named_shaders.cmake
                   VERBATIM)
