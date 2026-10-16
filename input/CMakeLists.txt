# Xbox input (XPP) replacement (portable): gamepads, keyboard and mouse through SDL3.
add_library(input STATIC
    script.cpp
    sdl_devices.cpp
    xbox_input.cpp
)
target_include_directories(input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(input PUBLIC xbox platform PRIVATE SDL3::SDL3)
