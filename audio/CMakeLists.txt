# Our sound device on OpenAL Soft, and the Xbox DirectSound replacement on top (portable).
add_library(audio STATIC
    sound_engine.cpp
    xbox_dsound.cpp
    format.cpp
    output.cpp
    voice.cpp
)
target_include_directories(audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# OpenAL Soft: the system package on Linux (Fedora: openal-soft-devel, plus .i686 for
# the loader), built from source on Windows, where there is no system copy.
if(WIN32)
    include(FetchContent)
    set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
    # Nothing of its own installed: the release installs OpenAL32.dll itself, and the
    # HRTF data is built in.
    foreach(option ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG ALSOFT_INSTALL_HRTF_DATA ALSOFT_INSTALL_AMBDEC_PRESETS)
        set(${option} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(openal_soft
        GIT_REPOSITORY https://github.com/kcat/openal-soft.git
        GIT_TAG 1.24.2)
    FetchContent_MakeAvailable(openal_soft)
    target_link_libraries(audio PUBLIC OpenAL::OpenAL)
else()
    # OpenAL Soft's own config first: it puts <prefix>/include before the system's,
    # so <AL/...> comes from that copy (the sniper release builds a newer one). It
    # has no version file, so no version here.
    find_package(OpenAL CONFIG QUIET)
    if(NOT OpenAL_FOUND)
        find_package(OpenAL 1.22 REQUIRED MODULE)
    endif()
    target_link_libraries(audio PUBLIC OpenAL::OpenAL)
endif()

target_link_libraries(audio PUBLIC xbox platform)

# The output device list alone (the launcher).
add_library(audio_devices STATIC output_devices.cpp)
target_include_directories(audio_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(audio_devices PUBLIC OpenAL::OpenAL)
