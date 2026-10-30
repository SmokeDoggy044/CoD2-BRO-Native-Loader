# Tests for platform/ and the portable kernel layer (xbox/). Run with `ctest --preset <name>` or
# `ctest --test-dir <build dir>`.
#
# Portable tests (any build).
add_executable(settings_test settings_test.cpp)
target_link_libraries(settings_test PRIVATE platform)
add_test(NAME settings_test COMMAND settings_test)

add_executable(reverb_tuning_test reverb_tuning_test.cpp)
target_include_directories(reverb_tuning_test PRIVATE ${PROJECT_SOURCE_DIR}/audio)
add_test(NAME reverb_tuning_test COMMAND reverb_tuning_test)

add_executable(category_volumes_test category_volumes_test.cpp)
target_include_directories(category_volumes_test PRIVATE ${PROJECT_SOURCE_DIR}/audio)
add_test(NAME category_volumes_test COMMAND category_volumes_test)

add_executable(update_check_test update_check_test.cpp)
target_link_libraries(update_check_test PRIVATE launcher_update)
add_test(NAME update_check_test COMMAND update_check_test)

# Keyboard and mouse bindings (input/bindings.h).
add_executable(bindings_test bindings_test.cpp)
target_link_libraries(bindings_test PRIVATE input_bindings SDL3::SDL3)
add_test(NAME bindings_test COMMAND bindings_test)

# Finding the supported build's addresses in another release (loader/release_map.h).
add_executable(release_map_test release_map_test.cpp ${PROJECT_SOURCE_DIR}/loader/release_map.cpp
               ${PROJECT_SOURCE_DIR}/loader/symbols.cpp)
target_include_directories(release_map_test PRIVATE ${PROJECT_SOURCE_DIR}/loader)
target_link_libraries(release_map_test PRIVATE platform)
add_test(NAME release_map_test COMMAND release_map_test)

# The virtual Xbox LAN (system link) over loopback UDP.
add_executable(lan_test lan_test.cpp ${PROJECT_SOURCE_DIR}/online/lan.cpp)
target_include_directories(lan_test PRIVATE ${PROJECT_SOURCE_DIR}/online)
target_link_libraries(lan_test PRIVATE platform)
add_test(NAME lan_test COMMAND lan_test)
set_tests_properties(lan_test PROPERTIES TIMEOUT 30)

# xbox/ stores the title's pointers as 32-bit values (XbPtr), which is only valid in a
# 32-bit address space, so the tests are built for 32-bit targets only for now.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "xbox/ tests skipped: they need a 32-bit build (XbPtr is 32 bits)")
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

# Header-only parts of render/ (render itself needs D3D9).
add_executable(frame_pacing_test frame_pacing_test.cpp)
target_include_directories(frame_pacing_test PRIVATE ${PROJECT_SOURCE_DIR}/render)
target_link_libraries(frame_pacing_test PRIVATE xbox)
add_test(NAME frame_pacing_test COMMAND frame_pacing_test)

# The D3D9 shader translator (token assembly only; no D3D9 needed).
add_executable(d3d9_shader_test d3d9_shader_test.cpp ${PROJECT_SOURCE_DIR}/render/d3d9_shader.cpp)
target_include_directories(d3d9_shader_test PRIVATE ${PROJECT_SOURCE_DIR}/render)
target_link_libraries(d3d9_shader_test PRIVATE xbox)
add_test(NAME d3d9_shader_test COMMAND d3d9_shader_test)

# Our graphics interface on a real D3D9 device (hidden window; skipped without one).
add_executable(gfx_device_test gfx_device_test.cpp)
target_link_libraries(gfx_device_test PRIVATE render xbox)
add_test(NAME gfx_device_test COMMAND gfx_device_test)
set_tests_properties(gfx_device_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# The translated title shaders against the Xbox path's translation (XB_SHADER_DUMP=a --dump-shaders
# directory; skipped without one).
add_executable(title_shader_test title_shader_test.cpp)
target_link_libraries(title_shader_test PRIVATE render xbox)
add_test(NAME title_shader_test COMMAND title_shader_test)
set_tests_properties(title_shader_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The NAL entropy mode decoders against the title's own code (runs default.xbe's bytes;
# skipped without the game files).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(nal_entropy_test nal_entropy_test.cpp ${PROJECT_SOURCE_DIR}/game/code/nal_entropy.cpp)
    target_include_directories(nal_entropy_test PRIVATE ${PROJECT_SOURCE_DIR}/game/code ${PROJECT_SOURCE_DIR}/platform)
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
    target_include_directories(compare_test_code PRIVATE ${PROJECT_SOURCE_DIR}/platform)
    target_link_libraries(compare_test_code PRIVATE cod_bro_options)
    add_executable(compare_test compare_test.cpp ${PROJECT_SOURCE_DIR}/loader/compare_linux.cpp
                   ${PROJECT_SOURCE_DIR}/loader/compare_common.cpp
                   ${PROJECT_SOURCE_DIR}/loader/hooks.cpp ${PROJECT_SOURCE_DIR}/loader/release_map.cpp
                   ${PROJECT_SOURCE_DIR}/loader/symbols.cpp)
    target_include_directories(compare_test PRIVATE ${PROJECT_SOURCE_DIR}/loader ${PROJECT_SOURCE_DIR}/game/code
                               ${PROJECT_SOURCE_DIR}/render)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/loader/compare_linux.cpp TARGET_DIRECTORY compare_test
        PROPERTIES COMPILE_OPTIONS "-fno-stack-protector;$<$<CXX_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>")
    target_link_libraries(compare_test PRIVATE compare_test_code xbox Threads::Threads)
    add_test(NAME compare_test COMMAND compare_test)
    set_tests_properties(compare_test PROPERTIES TIMEOUT 60)

    # Decompiled functions the campaign never runs, against default.xbe's code mapped at its
    # addresses (tools/leafscan.py --emit writes leaf_functions.inc; skipped without the game
    # files).
    add_executable(leaf_test leaf_test.cpp)
    target_include_directories(leaf_test PRIVATE ${PROJECT_SOURCE_DIR}/platform)
    target_compile_definitions(leaf_test PRIVATE
        COD_BRO_DEFAULT_XBE="${PROJECT_SOURCE_DIR}/../unpacked/default.xbe")
    target_link_libraries(leaf_test PRIVATE game_code cod_bro_options)
    add_test(NAME leaf_test COMMAND leaf_test)
    set_tests_properties(leaf_test PROPERTIES TIMEOUT 3600 SKIP_RETURN_CODE 77)
endif()
