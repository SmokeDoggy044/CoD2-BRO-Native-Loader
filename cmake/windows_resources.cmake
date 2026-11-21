# Windows version information for the port's own files (cmake/windows/version.rc.in): product
# name, version, description - and for programs an "asInvoker" manifest, and for the launcher the icon
# (cmake/windows/program.ico, drawn from program.svg: PNG-compressed images, 16-256 px). Files without them are
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
        # The icon only on the launcher: in the 5 KB starters its compressed images were 82 % of the file (entropy
        # 3.8 -> 7.1 bits a byte, a packed program's look) and VirusTotal detections went from 6 to 19-21 (1.0.10 test).
        if(target STREQUAL "cod_bro_launcher")
            set(RES_ICON "1 ICON \"${PROJECT_SOURCE_DIR}/cmake/windows/program.ico\"")
        else()
            set(RES_ICON "")
        endif()
    else()
        set(RES_FILE "${target}.dll")
        set(RES_FILE_TYPE "VFT_DLL")
        set(RES_MANIFEST "")
        set(RES_ICON "")
    endif()
    if(kind STREQUAL "PROGRAM")
        # Terminal-server aware, as Microsoft's linker marks every program by default (MinGW does not):
        # no change at run time, one difference less from the programs antivirus models are trained on.
        target_link_options(${target} PRIVATE -Wl,--tsaware)
    endif()
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/windows/version.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
endfunction()
