# Decompiled game code (route C): portable C++. Until it is complete, the route-B
# loader runs it in place of the original functions or compares the two
# (functions.h, loader/compare.h).
set(game_code_sources
    actor.cpp
    anim_script.cpp
    bg_misc.cpp
    cg_ents.cpp
    cg_view.cpp
    cgame.cpp
    cl.cpp
    cm.cpp
    dobj.cpp
    fe.cpp
    fe_bonus.cpp
    fe_menus.cpp
    fe_options.cpp
    fe_special.cpp
    functions.cpp
    fx.cpp
    fx_cmd.cpp
    fx_load.cpp
    g_scr_math.cpp
    g_scr_runtime.cpp
    g_script.cpp
    g_utils.cpp
    g_vehicle.cpp
    movie.cpp
    movie_vid.cpp
    nal_anim.cpp
    nal_codec.cpp
    nal_entropy.cpp
    nal_math.cpp
    nal_pose.cpp
    ngl.cpp
    ngl_debug.cpp
    param_set.cpp
    qcommon.cpp
    r_init.cpp
    script_string.cpp
    shaders.cpp
    snd.cpp
    stream.cpp
    sv.cpp
    tr_main.cpp
    ui_menu.cpp
    ui_panel.cpp
    unzip.cpp
)
# Linux loader builds: a shared library, so that the comparison harness can tell this
# code apart from the loader's (its test processes keep only this library, the C
# runtime and the title's code executable).
if(COD_BRO_BUILD_LOADER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(game_code SHARED ${game_code_sources})
    set_target_properties(game_code PROPERTIES OUTPUT_NAME cod_bro_game
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    add_library(game_code STATIC ${game_code_sources})
endif()
target_include_directories(game_code PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Only platform's headers (compiler.h): its library is not built for a shared object.
target_include_directories(game_code PRIVATE ${PROJECT_SOURCE_DIR}/platform)
target_link_libraries(game_code PRIVATE cod_bro_options)
# The title does its float math in SSE (single precision); x87 code would round and
# quiet NaNs differently. Pentium III level: SSE only, no SSE2. -frounding-math: the
# title's MXCSR does not round to nearest, so constant expressions (1.0f / 320.0f in
# nglMatrixScreenToClip) must not be folded at build time (optimized builds did).
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86)$")
    target_compile_options(game_code PRIVATE -msse -mfpmath=sse -frounding-math)
endif()
