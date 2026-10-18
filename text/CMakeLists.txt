# Sharper game text (PC option): the game's bitmap fonts drawn again from the fonts in
# fonts/ (see sharp_text.h). Portable.

# stb_truetype (public domain / MIT), fetched at configure time.
include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(stb)

add_library(text STATIC
    kapf.cpp
    sharp_text.cpp
)
target_include_directories(text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(text SYSTEM PRIVATE ${stb_SOURCE_DIR})
# Only render's headers (TextureReplacement, the texture hash).
target_include_directories(text PUBLIC ${PROJECT_SOURCE_DIR}/render)
target_link_libraries(text PUBLIC platform)

# fonts/ next to the executables, as in a release.
add_custom_target(fonts ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/fonts ${CMAKE_BINARY_DIR}/bin/fonts
    COMMENT "Copying fonts")
add_dependencies(text fonts)
