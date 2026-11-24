# 32-bit Windows build on a Linux host with MinGW-w64 GCC; tests and the game run
# under Wine. Fedora: sudo dnf install mingw32-gcc-c++ mingw32-winpthreads wine
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(triple i686-w64-mingw32)
set(CMAKE_C_COMPILER ${triple}-gcc)
set(CMAKE_CXX_COMPILER ${triple}-g++)
set(CMAKE_RC_COMPILER ${triple}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${triple}/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# GCC assumes plain operator new returns 16-byte aligned memory, but msvcrt's malloc
# gives only 8 on 32-bit Windows: make types aligned above 8 use the aligned new
# (OpenAL Soft's SSE stores into new'd objects crashed half the time without it).
set(CMAKE_CXX_FLAGS_INIT "-faligned-new=8")

# libgcc and libstdc++ linked into every executable and DLL; winpthreads stays its own DLL
# (libwinpthread-1.dll, shipped beside the programs): linked in, its thread code put
# SuspendThread/GetThreadContext/SetThreadContext/IsDebuggerPresent into the import table of
# every file of ours - calls antivirus heuristics weigh as an injector's. OpenAL Soft stays a
# DLL as well (LGPL).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

set(CMAKE_CROSSCOMPILING_EMULATOR wine)
