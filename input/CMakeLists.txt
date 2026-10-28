# Xbox input (XPP) replacement (portable): gamepads, keyboard and mouse through SDL3.
# The keyboard and mouse bindings (also the launcher's, so without the Xbox side).
add_library(input_bindings STATIC bindings.cpp)
target_include_directories(input_bindings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(input_bindings PRIVATE SDL3::SDL3)

add_library(input STATIC
    script.cpp
    sdl_devices.cpp
    xbox_input.cpp
)
target_include_directories(input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(input PUBLIC xbox platform input_bindings PRIVATE SDL3::SDL3)
