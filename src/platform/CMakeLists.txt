add_library(platform STATIC
    log.cpp
    paths.cpp
    program.cpp
    settings.cpp
)
if(WIN32)
    target_sources(platform PRIVATE file_win32.cpp https_win32.cpp udp_win32.cpp vmem_win32.cpp)
    target_link_libraries(platform PUBLIC ws2_32 winhttp)
else()
    target_sources(platform PRIVATE file_posix.cpp https_posix.cpp udp_posix.cpp vmem_posix.cpp)
endif()
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 64-bit file offsets for pread/pwrite in 32-bit builds.
set_source_files_properties(file_posix.cpp PROPERTIES COMPILE_DEFINITIONS _FILE_OFFSET_BITS=64)
# Threads (and dlopen): separate libraries before glibc 2.34, as in the sniper release.
find_package(Threads REQUIRED)
target_link_libraries(platform PUBLIC cod_bro_options Threads::Threads ${CMAKE_DL_LIBS})
