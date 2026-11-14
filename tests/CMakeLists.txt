# Tests for src/platform/ and the portable kernel layer (src/runtime/xbox/). Run with `ctest --preset <name>` or
# `ctest --test-dir <build dir>`.
#
# Portable tests (any build).
add_executable(settings_test settings_test.cpp)
target_link_libraries(settings_test PRIVATE platform settings_ui)
add_test(NAME settings_test COMMAND settings_test)

add_executable(reverb_tuning_test reverb_tuning_test.cpp)
target_include_directories(reverb_tuning_test PRIVATE ${PROJECT_SOURCE_DIR}/src/audio)
add_test(NAME reverb_tuning_test COMMAND reverb_tuning_test)

add_executable(category_volumes_test category_volumes_test.cpp)
target_include_directories(category_volumes_test PRIVATE ${PROJECT_SOURCE_DIR}/src/audio)
add_test(NAME category_volumes_test COMMAND category_volumes_test)

add_executable(update_check_test update_check_test.cpp)
target_link_libraries(update_check_test PRIVATE launcher_update)
add_test(NAME update_check_test COMMAND update_check_test)

# Keyboard and mouse bindings (src/input/bindings.h).
add_executable(bindings_test bindings_test.cpp)
target_link_libraries(bindings_test PRIVATE input_bindings SDL3::SDL3)
add_test(NAME bindings_test COMMAND bindings_test)

# Finding the supported build's addresses in another release (src/runtime/loader/release_map.h).
add_executable(release_map_test release_map_test.cpp ${PROJECT_SOURCE_DIR}/src/runtime/loader/release_map.cpp
               ${PROJECT_SOURCE_DIR}/src/runtime/loader/symbols.cpp)
target_include_directories(release_map_test PRIVATE ${PROJECT_SOURCE_DIR}/src/runtime/loader)
target_link_libraries(release_map_test PRIVATE platform)
add_test(NAME release_map_test COMMAND release_map_test)

# The virtual Xbox LAN (system link) over loopback UDP.
add_executable(lan_test lan_test.cpp ${PROJECT_SOURCE_DIR}/src/online/lan.cpp)
target_include_directories(lan_test PRIVATE ${PROJECT_SOURCE_DIR}/src/online)
target_link_libraries(lan_test PRIVATE platform)
add_test(NAME lan_test COMMAND lan_test)
set_tests_properties(lan_test PROPERTIES TIMEOUT 30)

# src/runtime/xbox/ stores the title's pointers as 32-bit values (XbPtr), which is only valid in a
# 32-bit address space, so the tests are built for 32-bit targets only for now.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "src/runtime/xbox/ tests skipped: they need a 32-bit build (XbPtr is 32 bits)")
    return()
endif()

find_package(Threads REQUIRED)

foreach(test audio_test critical_section_test dispatcher_test filesystem_test memory_test time_test crypto_test)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE xbox Threads::Threads)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60)
endforeach()
target_link_libraries(audio_test PRIVATE audio)

# Header-only parts of src/render/ (render itself needs D3D9).
add_executable(frame_pacing_test frame_pacing_test.cpp)
target_include_directories(frame_pacing_test PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_link_libraries(frame_pacing_test PRIVATE xbox)
add_test(NAME frame_pacing_test COMMAND frame_pacing_test)

# The D3D9 shader translator (token assembly only; no D3D9 needed).
add_executable(d3d9_shader_test d3d9_shader_test.cpp ${PROJECT_SOURCE_DIR}/src/render/d3d9_shader.cpp)
target_include_directories(d3d9_shader_test PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_link_libraries(d3d9_shader_test PRIVATE xbox)
add_test(NAME d3d9_shader_test COMMAND d3d9_shader_test)

# Our graphics interface on a real D3D9 device (hidden window; skipped without one).
add_executable(gfx_device_test gfx_device_test.cpp)
target_link_libraries(gfx_device_test PRIVATE render xbox test_shaders)
add_test(NAME gfx_device_test COMMAND gfx_device_test)
set_tests_properties(gfx_device_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# Enhanced graphics' settings text (header only).
add_executable(graphics_features_test graphics_features_test.cpp)
target_include_directories(graphics_features_test PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_link_libraries(graphics_features_test PRIVATE xbox)
add_test(NAME graphics_features_test COMMAND graphics_features_test)

# Xbox texture layouts as PC formats (YUV, swizzled).
add_executable(texture_decode_test texture_decode_test.cpp)
target_include_directories(texture_decode_test PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_link_libraries(texture_decode_test PRIVATE texture_formats xbox)
add_test(NAME texture_decode_test COMMAND texture_decode_test)

# The graphics tests' own shaders (tests/shaders/*.hlsl: vs_3_0 for *_vs, else ps_3_0) as D3D9 tokens and
# SPIR-V, like the title's (src/render/CMakeLists.txt); test_shaders.h finds them by name.
file(GLOB test_shader_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl)
set(test_shader_binaries "")
foreach(hlsl ${test_shader_sources})
    get_filename_component(name ${hlsl} NAME_WE)
    if(name MATCHES "_vs$")
        set(profile vs_3_0)
    else()
        set(profile ps_3_0)
    endif()
    set(binary ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.d3dbc)
    add_custom_command(OUTPUT ${binary} ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
                       COMMAND ${VKD3D_COMPILER} -x hlsl -p ${profile} -b d3dbc -o ${binary} ${hlsl}
                       COMMAND ${VKD3D_COMPILER} -x d3dbc -b spirv-binary -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv ${binary}
                       DEPENDS ${hlsl} ${PROJECT_SOURCE_DIR}/src/render/shaders/nv2a.hlsli
                       WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/render/shaders  # #include "nv2a.hlsli"
                       VERBATIM)
    list(APPEND test_shader_binaries ${binary})
endforeach()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_shaders.cpp
                   COMMAND ${CMAKE_COMMAND} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test_shaders.cpp
                           "-DINPUTS=${test_shader_binaries}" -DHEADER=test_shaders.h -DFUNCTION=test_shader
                           -P ${PROJECT_SOURCE_DIR}/cmake/embed_named_shaders.cmake
                   DEPENDS ${test_shader_binaries} ${PROJECT_SOURCE_DIR}/cmake/embed_named_shaders.cmake
                   VERBATIM)
add_library(test_shaders STATIC ${CMAKE_CURRENT_BINARY_DIR}/test_shaders.cpp)
target_include_directories(test_shaders PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_shaders PUBLIC render)

# The translated title shaders against the Xbox path's translation (XB_SHADER_DUMP=a --dump-shaders
# directory; skipped without one).
add_executable(title_shader_test title_shader_test.cpp)
target_link_libraries(title_shader_test PRIVATE render xbox)
add_test(NAME title_shader_test COMMAND title_shader_test)
set_tests_properties(title_shader_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The title and post shaders' SPIR-V with the Vulkan back end's fixed layout (vk/spirv_layout.h), then
# through spirv-val when it is installed (Fedora: spirv-tools).
add_executable(vk_shader_layout_test vk_shader_layout_test.cpp)
target_include_directories(vk_shader_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/src/render/vk)
target_link_libraries(vk_shader_layout_test PRIVATE render xbox)
add_test(NAME vk_shader_layout_test COMMAND vk_shader_layout_test)
find_program(SPIRV_VAL spirv-val)
if(SPIRV_VAL AND NOT WIN32)
    add_test(NAME vk_shader_layout_spirv_val
             COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:vk_shader_layout_test> -DSPIRV_VAL=${SPIRV_VAL}
                     -DDIR=${CMAKE_CURRENT_BINARY_DIR}/vk_shader_layout -P ${PROJECT_SOURCE_DIR}/cmake/spirv_val_dir.cmake)
endif()

# The NAL entropy mode decoders against the title's own code (runs default.xbe's bytes;
# skipped without the game files).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(nal_entropy_test nal_entropy_test.cpp ${PROJECT_SOURCE_DIR}/src/engine/xanim/nal_entropy.cpp)
    target_include_directories(nal_entropy_test PRIVATE $<TARGET_PROPERTY:game_code,INTERFACE_INCLUDE_DIRECTORIES> ${PROJECT_SOURCE_DIR}/src/platform)
    target_compile_definitions(nal_entropy_test PRIVATE
        COD_BRO_DEFAULT_XBE="${PROJECT_SOURCE_DIR}/../unpacked/default.xbe")
    target_link_libraries(nal_entropy_test PRIVATE cod_bro_options)
    add_test(NAME nal_entropy_test COMMAND nal_entropy_test)
    set_tests_properties(nal_entropy_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# The comparison harness (Linux loader builds): its own copy of the harness and a
# shared library standing in for the decompiled code.
if(COD_BRO_BUILD_LOADER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(compare_test_code SHARED compare_test_code.cpp)
    target_include_directories(compare_test_code PRIVATE ${PROJECT_SOURCE_DIR}/src/platform)
    target_link_libraries(compare_test_code PRIVATE cod_bro_options)
    add_executable(compare_test compare_test.cpp ${PROJECT_SOURCE_DIR}/src/runtime/loader/compare_linux.cpp
                   ${PROJECT_SOURCE_DIR}/src/runtime/loader/compare_common.cpp
                   ${PROJECT_SOURCE_DIR}/src/runtime/loader/hooks.cpp ${PROJECT_SOURCE_DIR}/src/runtime/loader/release_map.cpp
                   ${PROJECT_SOURCE_DIR}/src/runtime/loader/symbols.cpp)
    target_include_directories(compare_test PRIVATE ${PROJECT_SOURCE_DIR}/src/runtime/loader
                               $<TARGET_PROPERTY:game_code,INTERFACE_INCLUDE_DIRECTORIES>
                               ${PROJECT_SOURCE_DIR}/src/render)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/runtime/loader/compare_linux.cpp TARGET_DIRECTORY compare_test
        PROPERTIES COMPILE_OPTIONS "-fno-stack-protector;$<$<CXX_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>")
    target_link_libraries(compare_test PRIVATE compare_test_code xbox Threads::Threads)
    add_test(NAME compare_test COMMAND compare_test)
    set_tests_properties(compare_test PROPERTIES TIMEOUT 60)

    # Decompiled functions the campaign never runs, against default.xbe's code mapped at its
    # addresses (tools/leafscan.py --emit writes leaf_functions.inc; skipped without the game
    # files).
    add_executable(leaf_test leaf_test.cpp)
    target_include_directories(leaf_test PRIVATE ${PROJECT_SOURCE_DIR}/src/platform)
    target_compile_definitions(leaf_test PRIVATE
        COD_BRO_DEFAULT_XBE="${PROJECT_SOURCE_DIR}/../unpacked/default.xbe"
        COD_BRO_LEAF_KNOWN="${PROJECT_SOURCE_DIR}/tools/data/leaf_known.csv")
    target_link_libraries(leaf_test PRIVATE game_code cod_bro_options)
    add_test(NAME leaf_test COMMAND leaf_test)
    set_tests_properties(leaf_test PROPERTIES TIMEOUT 3600 SKIP_RETURN_CODE 77)
endif()
