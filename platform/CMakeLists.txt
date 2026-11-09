add_library(platform STATIC
    display_backends.cpp
    log.cpp
    paths.cpp
    program.cpp
    settings.cpp
    window_icon.cpp
)
if(WIN32)
    target_sources(platform PRIVATE file_win32.cpp https_win32.cpp udp_win32.cpp vmem_win32.cpp)
    target_link_libraries(platform PUBLIC ws2_32 winhttp)
else()
    target_sources(platform PRIVATE file_posix.cpp https_posix.cpp udp_posix.cpp vmem_posix.cpp)
endif()
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 64-bit file offsets for pread/pwrite in 32-bit builds.
set_source_files_properties(file_posix.cpp PROPERTIES COMPILE_DEFINITIONS _FILE_OFFSET_BITS=64)

# SDL3 (window, controllers). The X11 and Wayland client libraries are linked in
# explicitly on Linux: SDL3 can open a window on either, and the preferences in
# display_backends.cpp choose the session's native one with the other as fallback.
find_package(Threads REQUIRED)
target_link_libraries(platform PUBLIC cod_bro_options Threads::Threads ${CMAKE_DL_LIBS} SDL3::SDL3)

option(COD_BRO_LINK_X11_WAYLAND "Link the X11 and Wayland client libraries (Linux)" ON)
if(NOT WIN32 AND COD_BRO_LINK_X11_WAYLAND)
    find_package(X11 QUIET)
    find_package(PkgConfig QUIET)
    if(X11_FOUND AND TARGET X11::X11)
        target_link_libraries(platform PUBLIC X11::X11)
        message(STATUS "Display: X11 linked (${X11_X11_LIB})")
    endif()
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
        pkg_check_modules(XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
        if(TARGET PkgConfig::WAYLAND_CLIENT)
            target_link_libraries(platform PUBLIC PkgConfig::WAYLAND_CLIENT)
            message(STATUS "Display: Wayland linked (wayland-client)")
        endif()
        if(TARGET PkgConfig::XKBCOMMON)
            target_link_libraries(platform PUBLIC PkgConfig::XKBCOMMON)
        endif()
    endif()
    if(NOT X11_FOUND AND NOT TARGET PkgConfig::WAYLAND_CLIENT)
        message(WARNING "Display: neither X11 nor Wayland development libraries found; SDL3 will load them at run time")
    endif()
endif()
