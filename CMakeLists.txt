cmake_minimum_required(VERSION 3.24)
project(cod_bro_port VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The game's files (disc contents). Builds use the release layout: every executable
# goes to <build>/bin/, and <build>/bin/assets links to this folder.
set(COD_BRO_GAME_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../unpacked" CACHE PATH
    "Game files, linked as <build>/bin/assets")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config ${CMAKE_CONFIGURATION_TYPES})  # multi-config generators: no per-config subfolder
    string(TOUPPER "${config}" config)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

file(REAL_PATH "${COD_BRO_GAME_FILES}" game_files)
set(assets_link "${CMAKE_BINARY_DIR}/bin/assets")
if(IS_SYMLINK "${assets_link}")
    file(READ_SYMLINK "${assets_link}" current_target)
    if(NOT current_target STREQUAL game_files)
        file(REMOVE "${assets_link}")
    endif()
endif()
if(NOT EXISTS "${assets_link}")
    if(NOT EXISTS "${game_files}")
        message(WARNING "COD_BRO_GAME_FILES (${game_files}) does not exist; no assets link created")
    elseif(CMAKE_HOST_WIN32)
        # A junction needs no special rights, unlike a symbolic link.
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        file(TO_NATIVE_PATH "${assets_link}" native_link)
        file(TO_NATIVE_PATH "${game_files}" native_target)
        execute_process(COMMAND cmd /c mklink /J "${native_link}" "${native_target}" OUTPUT_QUIET)
    else()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        file(CREATE_LINK "${game_files}" "${assets_link}" SYMBOLIC)
    endif()
endif()

set(COD_BRO_IS_X86_32 OFF)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86)$")
    set(COD_BRO_IS_X86_32 ON)
endif()

set(COD_BRO_LOADER_DEFAULT OFF)
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32) AND COD_BRO_IS_X86_32)
    set(COD_BRO_LOADER_DEFAULT ON)
endif()
option(COD_BRO_BUILD_LOADER "Build the route-B XBE loader (32-bit x86 Linux or Windows)" ${COD_BRO_LOADER_DEFAULT})

# Settings shared by every target.
add_library(cod_bro_options INTERFACE)
if(MSVC)
    target_compile_options(cod_bro_options INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(cod_bro_options INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    # Xbox structures are read through other types all over; MSVC never assumes
    # strict aliasing, so optimized GCC/Clang builds must not either.
    target_compile_options(cod_bro_options INTERFACE -Wall -Wextra -fno-omit-frame-pointer -fno-strict-aliasing)
    if(MINGW)
        # MinGW's own printf family: msvcrt's knows no %zu or %lld.
        target_compile_definitions(cod_bro_options INTERFACE __USE_MINGW_ANSI_STDIO=1)
    endif()
    if(COD_BRO_IS_X86_32)
        # Xbox code calls into ours with only 4-byte stack alignment.
        target_compile_options(cod_bro_options INTERFACE -mstackrealign)
    endif()
endif()

option(COD_BRO_BUILD_TESTS "Build the tests" ON)

# SDL3 (window, gamepads, keyboard): fetched and linked statically on Windows, from
# the system on Linux (Fedora: SDL3-devel, plus SDL3-devel.i686 for the loader).
if(WIN32)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10)
    FetchContent_MakeAvailable(sdl3)
else()
    find_package(SDL3 REQUIRED CONFIG)
endif()

add_subdirectory(platform)
add_subdirectory(xbox)
add_subdirectory(audio)
add_subdirectory(render)
add_subdirectory(input)
add_subdirectory(online)
add_subdirectory(game)
add_subdirectory(launcher)

if(COD_BRO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COD_BRO_BUILD_LOADER)
    if(NOT ((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32) AND COD_BRO_IS_X86_32))
        message(FATAL_ERROR "The loader needs a 32-bit x86 build; use the linux-loader or windows-mingw preset.")
    endif()
    add_subdirectory(loader)
    # The player release: cmake --install / cpack
    if(WIN32)
        include(cmake/release_windows.cmake)
    else()
        include(cmake/release.cmake)
    endif()
endif()
