# Tests for platform/ and the portable kernel layer (xbox/). Run with `ctest --preset <name>` or
# `ctest --test-dir <build dir>`.
#
# Portable tests (any build).
add_executable(settings_test settings_test.cpp)
target_link_libraries(settings_test PRIVATE platform)
add_test(NAME settings_test COMMAND settings_test)

# The virtual Xbox LAN (system link) over loopback UDP.
add_executable(lan_test lan_test.cpp ${PROJECT_SOURCE_DIR}/online/lan.cpp)
target_include_directories(lan_test PRIVATE ${PROJECT_SOURCE_DIR}/online)
target_link_libraries(lan_test PRIVATE platform)
add_test(NAME lan_test COMMAND lan_test)
set_tests_properties(lan_test PROPERTIES TIMEOUT 30)

# xbox/ stores the title's pointers as 32-bit values (XbPtr), which is only valid in a
# 32-bit address space, so the tests are built for 32-bit targets only for now.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "xbox/ tests skipped: they need a 32-bit build (XbPtr is 32 bits)")
    return()
endif()

find_package(Threads REQUIRED)

foreach(test audio_test critical_section_test dispatcher_test filesystem_test memory_test time_test crypto_test)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE xbox Threads::Threads)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60)
endforeach()
target_link_libraries(audio_test PRIVATE audio)

# Header-only parts of render/ (render itself needs D3D9).
add_executable(frame_pacing_test frame_pacing_test.cpp)
target_include_directories(frame_pacing_test PRIVATE ${PROJECT_SOURCE_DIR}/render)
target_link_libraries(frame_pacing_test PRIVATE xbox)
add_test(NAME frame_pacing_test COMMAND frame_pacing_test)
