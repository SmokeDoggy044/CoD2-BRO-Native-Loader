# 32-bit x86 build on a 64-bit Linux host (needed for the route-B loader).
# Fedora: sudo dnf install glibc-devel.i686 libstdc++-devel.i686
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")

# Look for 32-bit libraries (SDL etc. later) in lib/, not lib64/.
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS OFF)
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB32_PATHS ON)
# Debian multiarch puts 32-bit .pc files in /usr/lib/i386-linux-gnu/pkgconfig;
# Fedora uses /usr/lib/pkgconfig for its .i686 packages. Include both, plus the
# prebuilt DXVK pkgconfig directory when the sniper recipe provides one.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig:$ENV{PKG_CONFIG_LIBDIR}")
