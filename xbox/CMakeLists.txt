add_library(xbox STATIC
    crypto.cpp
    dispatcher.cpp
    filesystem.cpp
    kernel_basic.cpp
    objects.cpp
    settings.cpp
    sync.cpp
    kernel_exports.cpp
    kernel_memory.cpp
)
target_include_directories(xbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox PUBLIC platform)

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
        COMMENT "Generating xbox/kernel_exports.inc"
        VERBATIM)
endif()
