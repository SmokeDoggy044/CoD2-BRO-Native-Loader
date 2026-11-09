# DXVK uses official prebuilt binaries fetched by packaging/dxvk/fetch-prebuilt.sh
# (see dxvk/prebuilt/README.md). Nothing is compiled from source: the Linux release uses the 32-bit dxvk-native
# libdxvk_d3d9.so and its MinGW headers from DXVK's official Steam Runtime build,
# the Windows release uses the 32-bit d3d9.dll from the official DXVK release.
#
# This file defines an INTERFACE target `cod_bro_dxvk` for Linux builds. On systems
# that have dxvk-native installed instead, it falls back to pkg-config.

set(COD_BRO_DXVK_PREBUILT_DIR "${PROJECT_SOURCE_DIR}/dxvk/prebuilt" CACHE PATH
    "Directory with the staged official DXVK binaries and headers")

if(NOT WIN32)
    set(dxvk_lib "${COD_BRO_DXVK_PREBUILT_DIR}/linux/lib/libdxvk_d3d9.so")
    set(dxvk_include "${COD_BRO_DXVK_PREBUILT_DIR}/linux/include/dxvk")
    add_library(cod_bro_dxvk INTERFACE)
    if(EXISTS "${dxvk_lib}" AND IS_DIRECTORY "${dxvk_include}")
        target_include_directories(cod_bro_dxvk INTERFACE "${dxvk_include}")
        target_link_libraries(cod_bro_dxvk INTERFACE "${dxvk_lib}")
        set(COD_BRO_DXVK_PREBUILT ON CACHE INTERNAL "Using the bundled DXVK binaries" FORCE)
        message(STATUS "DXVK: prebuilt 32-bit dxvk-native from ${COD_BRO_DXVK_PREBUILT_DIR}")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DXVK_D3D9 REQUIRED IMPORTED_TARGET dxvk-d3d9)
        target_link_libraries(cod_bro_dxvk INTERFACE PkgConfig::DXVK_D3D9)
        set(COD_BRO_DXVK_PREBUILT OFF CACHE INTERNAL "Using the system's dxvk-native" FORCE)
        message(STATUS "DXVK: system dxvk-native (pkg-config)")
    endif()
endif()
