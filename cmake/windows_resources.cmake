# Windows version information, manifest and application icon for the port's own
# files (cmake/windows/version.rc.in): product name, version, description. Files without them are
# flagged more often by antivirus heuristics (0.2.0's launcher: Gen:Variant.Babar on VirusTotal).
#   cod_bro_windows_resources(<target> "<description>" PROGRAM|LIBRARY)
function(cod_bro_windows_resources target description kind)
    if(NOT WIN32)
        return()
    endif()
    set(RES_DESCRIPTION "${description}")
    set(RES_NAME "${target}")
    if(kind STREQUAL "PROGRAM")
        set(RES_FILE "${target}.exe")
        set(RES_FILE_TYPE "VFT_APP")
        set(RES_MANIFEST "1 24 \"${PROJECT_SOURCE_DIR}/cmake/windows/program.manifest\"")
    else()
        set(RES_FILE "${target}.dll")
        set(RES_FILE_TYPE "VFT_DLL")
        set(RES_MANIFEST "")
    endif()
    # windres accepts forward slashes in the resource path on both native
    # Windows and the MinGW cross-build.
    file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/packaging/icons/cod_bro.ico" icon_path)
    set(RES_ICON "1 ICON \"${icon_path}\"")
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/windows/version.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
endfunction()
