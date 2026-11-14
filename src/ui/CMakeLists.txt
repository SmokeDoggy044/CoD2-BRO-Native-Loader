# The launcher's and the PC menu's settings widgets, drawn from src/platform/settings_schema (Dear ImGui).
add_library(settings_ui STATIC settings_widgets.cpp)
target_include_directories(settings_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(settings_ui PUBLIC imgui platform)
