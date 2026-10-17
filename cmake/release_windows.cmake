# The player release for Windows (route B, 32-bit, cross-built with MinGW-w64 by the
# windows-mingw-release presets): `cmake --install` / `cpack` lay out
#
#   cod_bro_launcher.exe, cod_bro_loader.exe (stub) + cod_bro_loader_core.dll,
#   OpenAL32.dll, default_xbe_symbols.txt, codmp_xbe_symbols.txt, README.txt
#   dxvk/d3d9.dll             DXVK (Direct3D 9 on Vulkan); the game falls back to the
#                             system's d3d9.dll without it or without Vulkan
#   assets/README.md          the player copies the game dump here
#   LICENSES/                 the port's notes plus every bundled component's license
#
# The GCC runtime and SDL3 are linked in; OpenAL Soft stays a DLL (LGPL: replaceable).
# DXVK comes from packaging/windows-dxvk/build.sh (run it once before the release).

install(TARGETS cod_bro_loader cod_bro_loader_core cod_bro_launcher OpenAL RUNTIME DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/tools/data/default_xbe_symbols.txt
              ${PROJECT_SOURCE_DIR}/tools/data/codmp_xbe_symbols.txt DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/assets/README.md DESTINATION assets)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/LICENSES/ DESTINATION LICENSES)

set(dxvk_dir "${PROJECT_SOURCE_DIR}/build/windows-dxvk/out")
if(NOT EXISTS "${dxvk_dir}/d3d9.dll")
    message(FATAL_ERROR "release: no DXVK for Windows; run packaging/windows-dxvk/build.sh first")
endif()
file(STRINGS "${dxvk_dir}/VERSION" dxvk_version)
install(FILES "${dxvk_dir}/d3d9.dll" DESTINATION dxvk)

set(COD_BRO_README_SYSTEM "Windows")
set(COD_BRO_README_REQUIREMENTS "- 64-bit Windows 10 or 11 (the game needs memory above 2 GB, which a 32-bit
  program only gets on 64-bit Windows).
- A graphics driver with Vulkan 1.3 (the included DXVK, dxvk\\d3d9.dll), or any GPU
  through the Direct3D 9 that comes with Windows. The game picks one by itself
  (Graphics in the launcher); nothing needs to be installed.

If the game does not start, a message names the log file to send:
%APPDATA%\\cod_bro\\log.txt.

OpenAL32.dll and DXVK are included (see LICENSES/BUNDLED.txt).")
set(COD_BRO_README_SETTINGS "%APPDATA%\\cod_bro\\settings.ini")
set(COD_BRO_README_HELP "cod_bro_loader --help (from a console)")
configure_file(${PROJECT_SOURCE_DIR}/packaging/README.txt.in ${CMAKE_BINARY_DIR}/README.txt @ONLY NEWLINE_STYLE CRLF)
install(FILES ${CMAKE_BINARY_DIR}/README.txt DESTINATION .)

# License texts of the bundled components: from the fetched sources and the Fedora
# MinGW packages the release is built with.
FetchContent_GetProperties(imgui SOURCE_DIR imgui_dir)
FetchContent_GetProperties(sdl3 SOURCE_DIR sdl3_dir)
FetchContent_GetProperties(openal_soft SOURCE_DIR openal_dir)
file(GLOB openal_fmt_license "${openal_dir}/fmt-*/LICENSE")
# file|name|title|source
set(bundled_licenses
    "${sdl3_dir}/LICENSE.txt|SDL3|SDL3 3.2.10 (built into cod_bro_loader_core.dll and cod_bro_launcher.exe)|https://github.com/libsdl-org/SDL"
    "${imgui_dir}/LICENSE.txt|Dear-ImGui|Dear ImGui v1.91.9 (built into cod_bro_launcher.exe)|https://github.com/ocornut/imgui"
    "${openal_dir}/COPYING|OpenAL-Soft|OpenAL Soft 1.24.2 (OpenAL32.dll, LGPL: can be replaced with any compatible build)|https://github.com/kcat/openal-soft"
    "${openal_dir}/BSD-3Clause|OpenAL-Soft-BSD-3-Clause|OpenAL Soft, BSD-3-Clause parts|"
    "${openal_dir}/LICENSE-pffft|OpenAL-Soft-pffft|OpenAL Soft, pffft|"
    "${openal_fmt_license}|fmt|fmt (built into OpenAL32.dll)|https://github.com/fmtlib/fmt"
    "${dxvk_dir}/licenses/DXVK.txt|DXVK|DXVK ${dxvk_version} (dxvk/d3d9.dll, built with -mstackrealign)|https://github.com/doitsujin/dxvk"
    "${dxvk_dir}/licenses/libdisplay-info.txt|libdisplay-info|libdisplay-info (built into dxvk/d3d9.dll)|https://gitlab.freedesktop.org/emersion/libdisplay-info"
    "/usr/share/licenses/mingw32-libgcc/COPYING3|GCC-runtime-GPL-3.0|GCC runtime (libgcc and libstdc++, linked in): GPL-3.0|https://gcc.gnu.org"
    "/usr/share/licenses/mingw32-libgcc/COPYING.RUNTIME|GCC-runtime-exception|GCC runtime: runtime library exception|"
    "/usr/share/licenses/mingw32-winpthreads/COPYING|mingw-w64-winpthreads|mingw-w64 winpthreads (linked in)|https://www.mingw-w64.org"
    "/usr/share/licenses/mingw32-crt/COPYING|mingw-w64-runtime|mingw-w64 runtime (startup code, linked in)|https://www.mingw-w64.org")
set(bundled_text "Components bundled in this release (libraries linked into the programs,\nOpenAL32.dll and dxvk/d3d9.dll).\n\n")
foreach(entry ${bundled_licenses})
    string(REPLACE "|" ";" fields "${entry}|")  # the trailing | keeps an empty source
    list(GET fields 0 file)
    list(GET fields 1 name)
    list(GET fields 2 title)
    list(GET fields 3 url)
    if(NOT EXISTS "${file}")
        message(WARNING "release: license file ${file} not found")
        continue()
    endif()
    install(FILES "${file}" DESTINATION LICENSES/bundled RENAME "${name}.txt")
    string(APPEND bundled_text "${title}\n  license: LICENSES/bundled/${name}.txt\n")
    if(url)
        string(APPEND bundled_text "  source:  ${url}\n")
    endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/BUNDLED.txt" "${bundled_text}")
install(FILES "${CMAKE_BINARY_DIR}/BUNDLED.txt" DESTINATION LICENSES)

# cpack: cod_bro-<version>-windows.zip with one top folder.
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME cod_bro)
set(CPACK_PACKAGE_FILE_NAME "cod_bro-${PROJECT_VERSION}-windows")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
include(CPack)
