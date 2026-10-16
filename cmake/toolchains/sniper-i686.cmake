# 32-bit x86 release build inside the Steam Runtime 3 "sniper" SDK (Debian 11,
# glibc 2.31), so the release runs on any distribution since about 2021. Used by
# packaging/sniper/build.sh; not meant for a normal host.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)

# GCC 14 for C++20. Its libstdc++ is newer than the system's (GCC 10), so it is
# linked statically everywhere: players' systems need no newer libstdc++.
set(CMAKE_C_COMPILER gcc-14)
set(CMAKE_CXX_COMPILER g++-14)
# -mstackrealign also for the libraries built with this file (SDL3, OpenAL Soft):
# title threads call into them with the Xbox's 4-byte stack alignment, and their SSE
# code assumes 16 (Fedora builds all its i686 packages this way).
set(CMAKE_C_FLAGS_INIT "-m32 -mstackrealign")
set(CMAKE_CXX_FLAGS_INIT "-m32 -mstackrealign")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32 -static-libstdc++ -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32 -static-libstdc++ -static-libgcc")

set(PKG_CONFIG_EXECUTABLE i686-linux-gnu-pkg-config)
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS OFF)
