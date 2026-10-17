# The player release (route B, 32-bit Linux): `cmake --install` / `cpack` lay out
#
#   cod_bro_launcher, cod_bro_loader, default_xbe_symbols.txt, codmp_xbe_symbols.txt, README.txt
#   lib/libcod_bro_game.so     the decompiled game code
#   assets/README.md          the player copies the game dump here
#   lib/                      the libraries the executables need beyond glibc and
#                             the system's 32-bit graphics/sound drivers
#   LICENSES/                 the port's notes plus every bundled library's license
#
# Build it with packaging/sniper/build.sh (runs on older distributions; libraries
# from COD_BRO_RELEASE_DEPS) or, for this machine only, the linux-loader-release
# presets with the system's libraries (see the README, Releasing).
set(COD_BRO_RELEASE_DEPS "" CACHE PATH
    "Libraries for the release built by packaging/sniper (with licenses/); empty: the system's")

set(release_programs cod_bro_loader cod_bro_launcher)

# lib/ next to the executables. An old-style RPATH (not RUNPATH) also applies to the
# libraries' own dependencies (OpenAL Soft -> fmt, DXVK -> libdisplay-info).
set_target_properties(${release_programs} PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
foreach(program ${release_programs})
    target_link_options(${program} PRIVATE "LINKER:--disable-new-dtags")
endforeach()

install(TARGETS ${release_programs}
    RUNTIME_DEPENDENCY_SET release_libraries
    RUNTIME DESTINATION .)
# The decompiled code is a shared library in Linux loader builds (game/code).
install(TARGETS game_code LIBRARY DESTINATION lib)
install(RUNTIME_DEPENDENCY_SET release_libraries
    DESTINATION lib
    DIRECTORIES ${COD_BRO_RELEASE_DEPS}/lib /usr/lib/i386-linux-gnu /usr/lib /lib
    # glibc's libraries are the system's (they must match its loader); Vulkan comes
    # with the GPU driver.
    PRE_EXCLUDE_REGEXES
        "^libc\\.so" "^libm\\.so" "^ld-linux" "^libdl\\.so" "^libpthread\\.so" "^librt\\.so"
        "^libresolv\\.so" "^libnsl\\.so" "^libutil\\.so" "^libanl\\.so"
        "^libvulkan\\.so"
    POST_EXCLUDE_REGEXES "^/lib64/" "^/usr/lib64/")

install(FILES ${PROJECT_SOURCE_DIR}/tools/data/default_xbe_symbols.txt
              ${PROJECT_SOURCE_DIR}/tools/data/codmp_xbe_symbols.txt DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/assets/README.md DESTINATION assets)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/LICENSES/ DESTINATION LICENSES)

# Oldest glibc that runs the release: the newest GLIBC_ symbol version the shipped
# binaries use, found after install (see release_glibc.cmake). The README gets it
# from there too, so it is written at install time.
install(CODE "set(COD_BRO_RELEASE_PREFIX \"\${CMAKE_INSTALL_PREFIX}\")
              set(PROJECT_VERSION \"${PROJECT_VERSION}\")
              set(COD_BRO_README_IN \"${PROJECT_SOURCE_DIR}/packaging/README.txt.in\")")
install(SCRIPT ${PROJECT_SOURCE_DIR}/cmake/release_glibc.cmake)

# License texts of the bundled libraries. Sniper builds: written by
# packaging/sniper/inside.sh next to the libraries. System builds: from the Fedora
# packages they come from (so they match the shipped versions).
FetchContent_GetProperties(imgui SOURCE_DIR imgui_source_dir)
install(FILES "${imgui_source_dir}/LICENSE.txt" DESTINATION LICENSES/bundled RENAME Dear-ImGui.txt)
set(imgui_note "Dear ImGui v1.91.9 (built into cod_bro_launcher)\n  license: LICENSES/bundled/Dear-ImGui.txt\n  source:  https://github.com/ocornut/imgui\n")
if(COD_BRO_RELEASE_DEPS)
    file(GLOB dep_licenses "${COD_BRO_RELEASE_DEPS}/licenses/*.txt")
    list(FILTER dep_licenses EXCLUDE REGEX "/BUNDLED\\.txt$")
    install(FILES ${dep_licenses} DESTINATION LICENSES/bundled)
    file(READ "${COD_BRO_RELEASE_DEPS}/licenses/BUNDLED.txt" bundled_text)
    file(WRITE "${CMAKE_BINARY_DIR}/BUNDLED.txt" "${bundled_text}${imgui_note}")
else()
    # package|file name|title|license file|source
    set(bundled_licenses
        "SDL3|SDL3|SDL3|/usr/share/licenses/SDL3/LICENSE.txt|https://github.com/libsdl-org/SDL"
        "dxvk-native|DXVK|DXVK (dxvk-native)|/usr/share/licenses/dxvk-native/LICENSE|https://github.com/doitsujin/dxvk"
        "openal-soft|OpenAL-Soft|OpenAL Soft|/usr/share/licenses/openal-soft/COPYING|https://github.com/kcat/openal-soft"
        "openal-soft|OpenAL-Soft-BSD-3-Clause|OpenAL Soft, BSD-3-Clause parts|/usr/share/licenses/openal-soft/BSD-3Clause|"
        "openal-soft|OpenAL-Soft-pffft|OpenAL Soft, pffft|/usr/share/licenses/openal-soft/LICENSE-pffft|"
        "fmt|fmt|fmt|/usr/share/licenses/fmt/LICENSE|https://github.com/fmtlib/fmt"
        "libdisplay-info|libdisplay-info|libdisplay-info|/usr/share/licenses/libdisplay-info/LICENSE|https://gitlab.freedesktop.org/emersion/libdisplay-info"
        "libstdc++|GCC-runtime-GPL-3.0|GCC runtime (libstdc++, libgcc_s): GPL-3.0|/usr/share/licenses/libgcc/COPYING3|https://gcc.gnu.org"
        "libstdc++|GCC-runtime-exception|GCC runtime (libstdc++, libgcc_s): runtime library exception|/usr/share/licenses/libgcc/COPYING.RUNTIME|")
    set(bundled_text "Libraries bundled in lib/ (and Dear ImGui, built into cod_bro_launcher).\n")
    string(APPEND bundled_text "Versions are the Fedora packages the release was built with; their sources\n")
    string(APPEND bundled_text "are at the upstream links and https://src.fedoraproject.org/rpms/<package>.\n")
    string(APPEND bundled_text "OpenAL Soft is LGPL: lib/libopenal.so.1 can be replaced with any compatible build.\n\n")
    foreach(entry ${bundled_licenses})
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 package)
        list(GET fields 1 name)
        list(GET fields 2 title)
        list(GET fields 3 file)
        list(GET fields 4 url)
        if(NOT EXISTS "${file}")
            message(WARNING "release: license file ${file} not found")
            continue()
        endif()
        install(FILES "${file}" DESTINATION LICENSES/bundled RENAME "${name}.txt")
        set(version "")
        if(package)
            execute_process(COMMAND rpm -q --qf "%{VERSION}-%{RELEASE}" "${package}.i686"
                OUTPUT_VARIABLE version ERROR_QUIET RESULT_VARIABLE rpm_failed)
            if(rpm_failed)
                set(version "")
            endif()
        endif()
        string(APPEND bundled_text "${title}")
        if(version)
            string(APPEND bundled_text ", package ${package} ${version}")
        endif()
        string(APPEND bundled_text "\n  license: LICENSES/bundled/${name}.txt\n")
        if(url)
            string(APPEND bundled_text "  source:  ${url}\n")
        endif()
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/BUNDLED.txt" "${bundled_text}${imgui_note}")
endif()
install(FILES "${CMAKE_BINARY_DIR}/BUNDLED.txt" DESTINATION LICENSES)

# cpack: cod_bro-<version>-linux.tar.gz with one top folder.
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME cod_bro)
set(CPACK_PACKAGE_FILE_NAME "cod_bro-${PROJECT_VERSION}-linux")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
include(CPack)
