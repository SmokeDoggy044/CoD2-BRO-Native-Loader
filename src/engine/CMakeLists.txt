# Decompiled game code (route C): portable C++. Until it is complete, the route-B
# loader runs it in place of the original functions or compares the two
# (functions.h, src/runtime/loader/compare.h).
set(game_code_sources
    game/actor.cpp
    xanim/anim_script.cpp
    game/bg_misc.cpp
    levelscript/bro_scr_actor.cpp
    levelscript/bro_scr_ai.cpp
    levelscript/bro_scr_ai_anim.cpp
    levelscript/bro_scr_ai_look.cpp
    levelscript/bro_scr_ai_weapons.cpp
    levelscript/bro_scr_anim.cpp
    levelscript/bro_scr_anim_handles.cpp
    levelscript/bro_scr_array.cpp
    levelscript/bro_scr_arrays.cpp
    levelscript/bro_scr_banim_handles.cpp
    levelscript/bro_scr_brofunc.cpp
    levelscript/bro_scr_cover_turret.cpp
    levelscript/bro_scr_death_watch.cpp
    levelscript/bro_scr_defaults.cpp
    levelscript/bro_scr_drone.cpp
    levelscript/bro_scr_drones.cpp
    levelscript/bro_scr_exploder.cpp
    levelscript/bro_scr_flak.cpp
    levelscript/bro_scr_fx.cpp
    levelscript/bro_scr_gibs.cpp
    levelscript/bro_scr_level.cpp
    levelscript/bro_scr_level_info.cpp
    levelscript/bro_scr_level_threads.cpp
    levelscript/bro_scr_level_threads_2.cpp
    levelscript/bro_scr_math.cpp
    levelscript/bro_scr_mission.cpp
    levelscript/bro_scr_mortar.cpp
    levelscript/bro_scr_nodes_spawner.cpp
    levelscript/bro_scr_notetrack.cpp
    levelscript/bro_scr_squad.cpp
    levelscript/bro_scr_stubs.cpp
    levelscript/bro_scr_tank_init.cpp
    levelscript/bro_scr_util.cpp
    levelscript/bro_scr_vehicle.cpp
    levelscript/bro_scr_vehicle_health.cpp
    levelscript/bro_scr_fx_threads.cpp
    levelscript/bro_scr_battle_threads.cpp
    levelscript/bro_scr_chain_threads.cpp
    levelscript/bro_scr_pose_threads.cpp
    levelscript/bro_scr_ai_anim_threads.cpp
    levelscript/bro_scr_notify_threads.cpp
    levelscript/bro_scr_anim_threads.cpp
    levelscript/bro_scr_trigger_threads.cpp
    levelscript/bro_scr_traverse.cpp
    levelscript/bro_scr_note_handlers.cpp
    levelscript/bro_scr_entity_threads.cpp
    levelscript/bro_scr_anim_reach.cpp
    levelscript/bro_scr_drone_threads.cpp
    levelscript/bro_scr_path_mover.cpp
    levelscript/bro_scr_halftrack_crew.cpp
    levelscript/bro_scr_stuka_bomb.cpp
    levelscript/bro_scr_actor_threads.cpp
    levelscript/bro_scr_mortar_loops.cpp
    levelscript/bro_scr_friendly_threads.cpp
    levelscript/bro_scr_anim_scenes.cpp
    levelscript/bro_scr_vehicle_path.cpp
    levelscript/bro_scr_vehicle_riders.cpp
    levelscript/bro_scr_flak88.cpp
    levelscript/bro_scr_wad.cpp
    levelscript/bro_scr_grenade.cpp
    levelscript/bro_scr_hud_fade.cpp
    levelscript/bro_scr_treefall.cpp
    levelscript/bro_scr_thread_misc.cpp
    levelscript/bro_scr_wait_misc.cpp
    levelscript/bro_scr_wait_notify.cpp
    levelscript/bro_scr_wallhop.cpp
    levelscript/bro_scr_world.cpp
    cgame/cg_ents.cpp
    cgame/cg_hud.cpp
    cgame/cg_view.cpp
    cgame/cgame.cpp
    client/cl.cpp
    collision/cm.cpp
    renderer/dobj.cpp
    client/fe.cpp
    client/fe_bonus.cpp
    client/fe_menus.cpp
    client/fe_options.cpp
    client/fe_profile.cpp
    client/fe_setup.cpp
    client/fe_setup_bonus.cpp
    client/fe_setup_special.cpp
    client/fe_special.cpp
    shared/functions.cpp
    shared/title_crt.cpp
    shared/static_init.cpp
    shared/static_init_generated.cpp
    renderer/fx.cpp
    renderer/fx_cmd.cpp
    renderer/fx_load.cpp
    game/g_client.cpp
    game/g_combat.cpp
    game/g_main.cpp
    game/g_mover.cpp
    game/g_pathnode.cpp
    script/g_scr_builtins.cpp
    script/g_scr_builtins2.cpp
    game/g_scr_imports.cpp
    game/g_scr_math.cpp
    game/g_scr_runtime.cpp
    game/g_script.cpp
    game/g_spawn.cpp
    game/g_static.cpp
    game/g_utils.cpp
    game/g_vehicle.cpp
    levels/levels_prologue_a.cpp
    levels/levels_prologue_b.cpp
    levels/levels_prologue_bodies_a.cpp
    levels/levels_prologue_events.cpp
    levels/levels_templates.cpp
    client/movie.cpp
    client/movie_vid.cpp
    xanim/nal_anim.cpp
    xanim/nal_codec.cpp
    xanim/nal_entropy.cpp
    xanim/nal_math.cpp
    xanim/nal_pose.cpp
    renderer/ngl.cpp
    renderer/ngl_debug.cpp
    renderer/ngl_gfx.cpp
    qcommon/param_set.cpp
    game/phys.cpp
    game/pmove.cpp
    qcommon/qcommon.cpp
    qcommon/qcommon_core.cpp
    qcommon/qcommon_fs.cpp
    renderer/r_init.cpp
    renderer/r_static_init.cpp
    game/ragdoll.cpp
    script/scr_access.cpp
    script/scr_class.cpp
    script/scr_field.cpp
    script/script_string.cpp
    renderer/shaders.cpp
    renderer/xgraphics.cpp
    sound/snd.cpp
    sound/snd_env.cpp
    script/scr_fiber.cpp
    shared/stack_guard.cpp
    streaming/stream.cpp
    server/sv.cpp
    renderer/tr_main.cpp
    client/ui_menu.cpp
    client/ui_panel.cpp
    zlib/unzip.cpp
    xanim/xanim.cpp
    zlib/zlib.cpp
)
# Linux loader builds: a shared library, so that the comparison harness can tell this
# code apart from the loader's (its test processes keep only this library, the C
# runtime and the title's code executable).
if(COD_BRO_BUILD_LOADER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(game_code SHARED ${game_code_sources})
    set_target_properties(game_code PROPERTIES OUTPUT_NAME cod_bro_game
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    # Calls to its own functions stay in the library: at -O0 template helpers (std::atomic's
    # memory_order operators, initializer_list) are weak out-of-line copies, and without this they
    # bind to the loader's copies - host code to the harness ("called host code").
    target_link_options(game_code PRIVATE -Wl,-Bsymbolic-functions)
    # Script thread stacks (scr_fiber.cpp): platform's page allocator, built into this library.
    target_sources(game_code PRIVATE ${PROJECT_SOURCE_DIR}/src/platform/vmem_posix.cpp)
else()
    add_library(game_code STATIC ${game_code_sources})
    target_link_libraries(game_code PRIVATE platform)
endif()
# One folder per module; the sources include each other's headers by file name.
target_include_directories(game_code PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cgame
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/collision
    ${CMAKE_CURRENT_SOURCE_DIR}/game
    ${CMAKE_CURRENT_SOURCE_DIR}/levels
    ${CMAKE_CURRENT_SOURCE_DIR}/levelscript
    ${CMAKE_CURRENT_SOURCE_DIR}/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/script
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/streaming
    ${CMAKE_CURRENT_SOURCE_DIR}/xanim
    ${CMAKE_CURRENT_SOURCE_DIR}/zlib
)
# Only platform's headers (compiler.h): its library is not built for a shared object.
target_include_directories(game_code PRIVATE ${PROJECT_SOURCE_DIR}/src/platform)
# Only declarations from src/audio/ (sound_device.h, format.h, packet.h): the loader hands over the device.
target_include_directories(game_code PRIVATE ${PROJECT_SOURCE_DIR}/src/audio)
# From src/render/: declarations (gfx_device.h; the loader says where the device comes from) and the
# texture formats library (texture_decode.h).
target_include_directories(game_code PRIVATE ${PROJECT_SOURCE_DIR}/src/render)
target_link_libraries(game_code PRIVATE cod_bro_options texture_formats)
# The title does its float math in SSE (single precision); x87 code would round and
# quiet NaNs differently. Pentium III level: SSE only, no SSE2. -frounding-math: the
# title's MXCSR does not round to nearest, so constant expressions (1.0f / 320.0f in
# nglMatrixScreenToClip) must not be folded at build time (optimized builds did).
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86)$")
    target_compile_options(game_code PRIVATE -msse -mfpmath=sse -frounding-math)
endif()
