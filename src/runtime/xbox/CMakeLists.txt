# xbox_host: what the own engine host (cod_bro) takes from here - the title's memory, replay, the title calls and
# clocks, the export-name table for its fatal kernel stubs. No kernel export runs in it.
add_library(xbox_host STATIC
    kernel_exports.cpp
    kernel_memory.cpp
    kernel_shared.cpp
    replay.cpp
    title_calls.cpp
)
target_include_directories(xbox_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox_host PUBLIC platform)

# xbox: the kernel itself (the loader only).
add_library(xbox STATIC
    crypto.cpp
    dispatcher.cpp
    filesystem.cpp
    kernel_basic.cpp
    objects.cpp
    settings.cpp
    sync.cpp
)
target_link_libraries(xbox PUBLIC xbox_host)

# Regenerate kernel_exports.inc after changing the ordinal list or xboxkrnl.h:
#   cmake --build <dir> --target gen_kernel_exports
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(gen_kernel_exports
        COMMAND Python3::Interpreter
            ${PROJECT_SOURCE_DIR}/tools/gen_kernel_exports.py
            ${PROJECT_SOURCE_DIR}/tools/data/xboxkrnl_ordinals.txt
            ${PROJECT_SOURCE_DIR}/third_party/xbox-includes/include/xboxkrnl.h
            ${CMAKE_CURRENT_SOURCE_DIR}/kernel_exports.inc
        COMMENT "Generating src/runtime/xbox/kernel_exports.inc"
        VERBATIM)
endif()
