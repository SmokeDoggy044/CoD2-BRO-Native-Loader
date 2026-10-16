# Install step of release.cmake: finds the oldest glibc the installed binaries run on
# (their newest GLIBC_x.y symbol version) and writes README.txt with it.

set(prefix "$ENV{DESTDIR}${COD_BRO_RELEASE_PREFIX}")
file(GLOB binaries "${prefix}/cod_bro_*" "${prefix}/lib/*.so*")
set(glibc_min "2.0")
foreach(binary ${binaries})
    if(IS_SYMLINK "${binary}" OR NOT binary MATCHES "(cod_bro_[a-z]+|\\.so[.0-9]*)$")
        continue()
    endif()
    execute_process(COMMAND objdump -T "${binary}" OUTPUT_VARIABLE symbols ERROR_QUIET)
    string(REGEX MATCHALL "GLIBC_[0-9]+\\.[0-9]+(\\.[0-9]+)?" versions "${symbols}")
    foreach(version ${versions})
        string(REPLACE "GLIBC_" "" version "${version}")
        if(version VERSION_GREATER glibc_min)
            set(glibc_min "${version}")
        endif()
    endforeach()
endforeach()
message(STATUS "release: needs glibc ${glibc_min} or newer")
set(COD_BRO_README_SYSTEM "Linux")
set(COD_BRO_README_REQUIREMENTS "- Linux on a 64-bit (or 32-bit) x86 PC, glibc ${glibc_min} or newer.
- 32-bit graphics and sound libraries, the same ones 32-bit games need (they come
  with Steam on most distributions): a Vulkan driver (Mesa or NVIDIA, 32-bit),
  the X11 or Wayland client libraries, and PulseAudio or PipeWire client libraries.
- A GPU with Vulkan 1.3 support.

The other libraries are in lib/ (see LICENSES/BUNDLED.txt).")
set(COD_BRO_README_SETTINGS "~/.local/share/cod_bro/settings.ini")
set(COD_BRO_README_HELP "./cod_bro_loader --help")
configure_file("${COD_BRO_README_IN}" "${prefix}/README.txt" @ONLY)
