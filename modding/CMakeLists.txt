# The F4 "Modding" window: PNG texture extraction/replacement, WAV sound
# extraction/replacement, and Xbox model/animation export + same-length override
# patching over the game's .cod packs and loose assets.
add_library(modding STATIC
    animation.cpp
    audio_bank.cpp
    audio_codec.cpp
    csv.cpp
    image.cpp
    modding.cpp
    pack.cpp
)
target_include_directories(modding PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# render/ is needed only for its header-only TextureReplacement / hash_bytes;
# imgui for the window; platform for the user data folder and logging.
target_include_directories(modding PRIVATE ${PROJECT_SOURCE_DIR}/render ${CMAKE_CURRENT_SOURCE_DIR}/stb)
target_link_libraries(modding PUBLIC platform imgui)
# The vendored stb headers (modding/stb) are public domain / MIT.
