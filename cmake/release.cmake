# The player release (route B, 32-bit Linux): `cmake --install` / `cpack` lay out
#
#   cod_bro_launcher, cod_bro_loader, default_xbe_symbols.txt, codmp_xbe_symbols.txt, *_signatures.txt, README.txt
#   cod_bro.png, cod_bro.desktop, install-linux-icon.sh   Linux desktop icon
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
# DXVK is a prebuilt 32-bit dxvk-native library (no source build). Install both
# names: the executable asks for the SONAME, developers may link against the .so.
if(COD_BRO_DXVK_PREBUILT)
    install(FILES "${COD_BRO_DXVK_PREBUILT_DIR}/linux/lib/libdxvk_d3d9.so.0"
                  "${COD_BRO_DXVK_PREBUILT_DIR}/linux/lib/libdxvk_d3d9.so" DESTINATION lib)
    install(FILES "${COD_BRO_DXVK_PREBUILT_DIR}/LICENSES/DXVK.txt" DESTINATION LICENSES/bundled)
    install(FILES "${COD_BRO_DXVK_PREBUILT_DIR}/LICENSES/libdisplay-info.txt" DESTINATION LICENSES/bundled)
endif()
install(FILES "${PROJECT_SOURCE_DIR}/packaging/mods/README.txt" DESTINATION mods)
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
              ${PROJECT_SOURCE_DIR}/tools/data/codmp_xbe_symbols.txt
              ${PROJECT_SOURCE_DIR}/tools/data/default_xbe_signatures.txt
              ${PROJECT_SOURCE_DIR}/tools/data/codmp_xbe_signatures.txt DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/assets/README.md DESTINATION assets)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/LICENSES/ DESTINATION LICENSES)
# Fonts for sharper text (SIL OFL; their licenses and sources in fonts/).
install(DIRECTORY ${PROJECT_SOURCE_DIR}/fonts/ DESTINATION fonts)
# Linux has no icon embedded in an ELF executable: ship the icon and a desktop
# template, plus a helper that installs an absolute-Exec copy into the user's
# menu. The unpacked archive is relocatable, so a system share/applications
# entry with a relative Exec path would be broken.
configure_file("${PROJECT_SOURCE_DIR}/packaging/cod_bro.desktop.in"
               "${CMAKE_BINARY_DIR}/cod_bro.desktop" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/cod_bro.desktop" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/packaging/icons/cod_bro.png" DESTINATION .)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/packaging/install-linux-icon.sh" DESTINATION .)

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
FetchContent_GetProperties(stb SOURCE_DIR stb_source_dir)
install(FILES "${stb_source_dir}/LICENSE" DESTINATION LICENSES/bundled RENAME stb.txt)
set(imgui_note "Dear ImGui v1.91.9 (built into cod_bro_launcher and cod_bro_loader)\n  license: LICENSES/bundled/Dear-ImGui.txt\n  source:  https://github.com/ocornut/imgui\nstb_truetype, stb_image and stb_image_write (built into the loader's Modding window)\n  license: LICENSES/bundled/stb.txt\n  source:  https://github.com/nothings/stb\nDXVK 2.7.1 (prebuilt 32-bit dxvk-native, lib/libdxvk_d3d9.so.0) and libdisplay-info\n  license: LICENSES/bundled/DXVK.txt, LICENSES/bundled/libdisplay-info.txt\n  source:  https://github.com/doitsujin/dxvk (official Steam Runtime sniper build)\nDiscord Rich Presence: the loader/launcher speak Discord's local IPC themselves; no library.\nFonts in fonts/: SIL Open Font License 1.1, see fonts/README.md and fonts/licenses/\n")
if(COD_BRO_RELEASE_DEPS)
    file(GLOB dep_licenses "${COD_BRO_RELEASE_DEPS}/licenses/*.txt")
    list(FILTER dep_licenses EXCLUDE REGEX "/BUNDLED\\.txt$")
    install(FILES ${dep_licenses} DESTINATION LICENSES/bundled)
    file(READ "${COD_BRO_RELEASE_DEPS}/licenses/BUNDLED.txt" bundled_text)
    file(WRITE "${CMAKE_BINARY_DIR}/BUNDLED.txt" "${bundled_text}${imgui_note}")
else()
    # package|file name|title|license file|source
    # package|name|title|license file|source (package empty: these ship in the tree)
    set(bundled_licenses
        "|SDL3|SDL3 3.2.10 (system/shared library)|${PROJECT_SOURCE_DIR}/LICENSES/SDL3-zlib.txt|https://github.com/libsdl-org/SDL"
        "|OpenAL-Soft|OpenAL Soft (LGPL-2.1-or-later, replaceable)|${PROJECT_SOURCE_DIR}/LICENSES/OpenAL-Soft-LGPL.txt|https://github.com/kcat/openal-soft"
        "|DXVK|DXVK 2.7.1 (prebuilt 32-bit dxvk-native)|${COD_BRO_DXVK_PREBUILT_DIR}/LICENSES/DXVK.txt|https://github.com/doitsujin/dxvk"
        "|libdisplay-info|libdisplay-info (built into DXVK)|${COD_BRO_DXVK_PREBUILT_DIR}/LICENSES/libdisplay-info.txt|https://gitlab.freedesktop.org/emersion/libdisplay-info"
        "|stb-modding|stb_image / stb_image_write (Modding window)|${PROJECT_SOURCE_DIR}/modding/stb/LICENSE|https://github.com/nothings/stb")
    set(bundled_text "Components bundled in this release (libraries in lib/, Dear ImGui, DXVK, stb).\n")
    string(APPEND bundled_text "DXVK is shipped as an official prebuilt binary; it is not compiled from source.\n")
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
