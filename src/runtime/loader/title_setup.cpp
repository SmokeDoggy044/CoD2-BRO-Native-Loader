// The services our C and the devices get from their host, set up the same way by the loader and the own
// engine host (title_setup.h).

#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "title_crt.h"
#include "scene_view.h"
#include "replay.h"
#include "bindings.h"
#include "compare.h"
#include "crash.h"
#include "diag_summary.h"
#include "functions.h"
#include "gfx_device.h"
#include "enhanced_graphics.h"
#include "gfx_verify.h"
#include "ngl.h"
#include "ngl_gfx.h"
#include "texture_pack.h"
#include "snd.h"
#include "title_xapi.h"
#include "title_host.h"
#include "title_files.h"
#include "title_objects.h"
#include "qcommon.h"
#include "dev_mode.h"
#include "viewer.h"
#include "menu_walk.h"
#include "orbit.h"
#include "snd_env.h"
#include "snd_scene.h"
#include "sound_engine.h"
#include "kernel.h"
#include "civil_time.h"
#include "kernel_memory.h"
#include "title_threads.h"
#include "hooks.h"
#include "host_image.h"
#include "qcommon_fs.h"
#include "text_trap.h"
#include "static_init.h"
#include "title_start.h"
#include "log.h"
#include "os.h"
#include "patches.h"
#include "pc_menu.h"
#include "profiler.h"
#include "release_map.h"
#include "paths.h"
#include "program.h"
#include "script.h"
#include "scr_fiber.h"
#include "settings.h"
#include "shader_dump.h"
#include "kapf.h"
#include "key_prompts.h"
#include "sharp_text.h"
#include "symbols.h"
#include "thread_env.h"
#include "xbe.h"
#include "xbox_d3d_api.h"
#include "test_mode.h"
#include "xbox_dsound.h"
#include "xbox_input.h"
#include "xonline.h"
#include "options.h"
#include "title_setup.h"
#include "title_calls.h"

namespace loader {

// --replay-no-draw with --replay-shots: frames drawn before each picture (2 s: texture caches, fades, motion blur).
constexpr uint32_t kDrawFramesBeforeShot = 120;

// --audio-capture's tables (phase 3.0 tools): per presented frame the capture's sample position and the listener,
// every 6th frame each playing voice. Written on the presenting thread after the frame's other work.
struct AudioCaptureTables {
    std::FILE* frames = nullptr;
    std::FILE* voices = nullptr;
    void (*inner)() = nullptr;  // the frame_presented it wraps
    uint32_t count = 0;         // presented frames (outside a replay)
};
AudioCaptureTables g_capture;

// A mark's <prefix>_sound.txt (phase 3.0 tools): the listener's surroundings and every playing voice - gains as the
// output got them and each occlusion line.
void write_sound_mark(const char* prefix) {
    static const char* const kCategories[] = {"effects", "dialogue", "music", "interface", "ambience", "gunfire", "bullets", "own gunfire"};
    float l[3];
    audio::listener_position(l);
    std::string text;
    game::SndEnv_DescribeForMark(l, text);
    // The listener's facing and, per voice, where it is heard (right = top x front, as the sound placement turns it).
    float front[3], top[3];
    audio::listener_facing(front, top);
    const float right[3] = {top[1] * front[2] - top[2] * front[1], top[2] * front[0] - top[0] * front[2],
                            top[0] * front[1] - top[1] * front[0]};
    const float* camera = reinterpret_cast<const float*>(uintptr_t{0x00972bc0});
    char head[256];
    std::snprintf(head, sizeof(head), "listener front %.2f %.2f %.2f, top %.2f %.2f %.2f; camera forward %.2f %.2f %.2f\n",
                  double(front[0]), double(front[1]), double(front[2]), double(top[0]), double(top[1]), double(top[2]),
                  double(camera[0]), double(camera[1]), double(camera[2]));
    text += head;
    std::vector<audio::VoiceRow> rows = audio::voice_rows();
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.spatial != b.spatial ? a.spatial : a.distance < b.distance;
    });
    char line[512];
    std::snprintf(line, sizeof(line), "\n%zu voices playing (%s sound), spatial nearest first:\n", rows.size(),
                  audio::enhanced() ? "enhanced" : "original");
    text += line;
    for (const audio::VoiceRow& r : rows) {
        std::snprintf(line, sizeof(line),
                      "voice %08x %s%s: at %.0f %.0f %.0f, distance %.0f, distance gain %.3f, volume %.3f, occlusion "
                      "%.2f, out gain %.4f, direct %.2f lf %.2f hf %.2f, send %.2f, level %.4f, heard %.0f deg turned\n",
                      unsigned(r.id), kCategories[size_t(r.category)],
                      r.stereo < 0 ? " (stereo left)" : r.stereo > 0 ? " (stereo right)" : r.spatial ? "" : " (not placed)",
                      double(r.position[0]), double(r.position[1]), double(r.position[2]), double(r.distance),
                      double(r.distance_gain), double(r.volume), double(r.occlusion), double(r.gain),
                      double(r.direct_gain), double(r.direct_gain_lf), double(r.direct_gain_hf), double(r.send_gain),
                      double(r.level), double(r.turn));
        text += line;
        if (r.spatial) {
            const float d[3] = {r.position[0] - l[0], r.position[1] - l[1], r.position[2] - l[2]};
            const float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
            const float z = d[0] * front[0] + d[1] * front[1] + d[2] * front[2];
            std::snprintf(line, sizeof(line), "    direction from the listener %+.0f deg (+ right, 180 behind)\n",
                          double(std::atan2(x, z) * 57.29578f));
            text += line;
            game::SndEnv_DescribeVoiceForMark(l, r.position, text);
        }
    }
    const std::string path = std::string(prefix) + "_sound.txt";
    if (std::FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
        LOG_INFO("audio", "sound at the mark: %s", path.c_str());
    }
}

void audio_capture_frame() {
    if (g_capture.inner) g_capture.inner();
    const audio::Output* out = audio::output();
    if (!out) return;
    const uint32_t frame = xbox::replay::active() ? xbox::replay::frame_number() : g_capture.count;
    ++g_capture.count;
    const unsigned long long sample = out->rendered_frames();
    float l[3];
    audio::listener_position(l);
    float front[3], top[3];
    audio::listener_facing(front, top);
    const float* camera = reinterpret_cast<const float*>(uintptr_t{0x00972bc0});  // cgame refdef axis[0] (forward)
    const float* eye = reinterpret_cast<const float*>(uintptr_t{0x00972bb0});     // cgame refdef origin
    std::fprintf(g_capture.frames, "%u,%llu,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f\n", frame, sample,
                 double(l[0]), double(l[1]), double(l[2]), double(front[0]), double(front[1]), double(front[2]),
                 double(camera[0]), double(camera[1]), double(camera[2]), double(eye[0]), double(eye[1]),
                 double(eye[2]), double(top[0]), double(top[1]), double(top[2]));
    if (frame % 6) return;
    static const char* const kCategories[] = {"effects", "dialogue", "music", "interface", "ambience", "gunfire", "bullets", "own_gunfire"};
    for (const audio::VoiceRow& r : audio::voice_rows())
        std::fprintf(g_capture.voices, "%u,%llu,%08x,%s,%d,%.1f,%.1f,%.1f,%.1f,%.4f,%.4f,%.3f,%.5f,%.3f,%.3f,%.3f,%.5f,%.3f,%.1f\n",
                     frame, sample, unsigned(r.id), kCategories[size_t(r.category)], int(r.spatial),
                     double(r.position[0]), double(r.position[1]), double(r.position[2]), double(r.distance),
                     double(r.distance_gain), double(r.volume), double(r.occlusion), double(r.gain),
                     double(r.direct_gain), double(r.direct_gain_hf), double(r.send_gain), double(r.level),
                     double(r.direct_gain_lf), double(r.turn));
    if (frame % 600 == 0) {
        std::fflush(g_capture.frames);
        std::fflush(g_capture.voices);
    }
}

uint32_t current_kthread() {
    const loader::ThreadEnv* env = loader::current_thread_env();
    return env ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(env->kthread)) : 0;
}

void attach_kernel_thread() {
    loader::ThreadEnv* env = loader::create_thread_env(0, 0);
    if (!env || !loader::activate_thread_env(env)) LOG_ERROR("loader", "cannot prepare a kernel thread");
}

void call_guest(uint32_t function, const uint32_t* args, uint32_t count) {
    loader::ThreadEnv* env = loader::current_thread_env();
    if (!env) {
        LOG_ERROR("loader", "call to %08x from a thread without an Xbox environment", function);
        return;
    }
    loader::run_on_xbox_stack(function, *env, std::span(args, count));
}

void configure_title_services(const Options& options, const fs::path& assets, const TitleInfo& title, float aspect,
                              void (*device_failed)()) {
    xbox::set_title_calls({&call_guest, &attach_kernel_thread});
    online::configure_network({options.net_port});
    audio::OutputConfig audio_config;
    // A faster replay has to consume its sound faster too (the title waits for its packets): silent, at that speed.
    const bool fast_replay = options.replay.mode == xbox::replay::Mode::Replay && options.replay_speed != 1;
    audio_config.device = options.silent || fast_replay || options.audio_capture
                              ? audio::OutputConfig::Device::Loopback
                              : audio::OutputConfig::Device::Default;
    if (options.audio_capture) audio_config.capture_path = options.audio_capture->string();
    if (fast_replay) audio_config.speed = options.replay_speed;
    audio_config.device_name = options.audio_device;
    audio_config.speakers = options.speakers;
    render::set_graphics(options.graphics_enhanced, options.graphics_features);
    if (const std::string changed = options.graphics_features.format(); options.graphics_enhanced)
        LOG_INFO("render", "enhanced graphics%s%s", changed.empty() ? "" : ": ", changed.c_str());
    else
        LOG_INFO("render", "original graphics");
    audio_config.enhanced = options.audio_enhanced;
    audio_config.features = options.features;
    if (const std::string off = options.features.format(); !off.empty())
        LOG_INFO("audio", "enhanced sound without: %s", off.c_str());
    audio::configure(audio_config);
    game::SndEnv_SetTuning(options.reverb);
    if (const std::string reverb = options.reverb.format(); !reverb.empty())
        LOG_INFO("audio", "reverb adjustments: %s", reverb.c_str());
    audio::set_category_volumes(options.volumes);
    if (const std::string volumes = options.volumes.format(); !volumes.empty())
        LOG_INFO("audio", "volumes: %s", volumes.c_str());
    game::set_sound_device(&audio::sound_device());  // the decompiled sound code's device
    if (options.shader_dump_dir) loader::start_shader_dump(*options.shader_dump_dir);
    game::set_title_clock(&loader::title_rdtsc);
    game::set_title_fs_map(&loader::title_fs_offset);  // our XAPI QueryPerformanceCounter's clock
    {
        // What our C asks of its host (title_host.h); under --own-main it uses its own objects, not the kernel's.
        game::TitleHost host;
        host.start_thread = &loader::start_title_thread;
        host.replay_on_stream = [] { return xbox::replay::on_stream(); };
        host.replay_replaying = [] { return xbox::replay::replaying(); };
        host.replay_recorded = [](char tag, uint32_t* value) { return xbox::replay::recorded(tag, *value); };
        host.replay_value = [](char tag, uint32_t real) { return xbox::replay::value(tag, real); };
        host.system_time = [] { return platform::system_time_now(); };
        host.allocate_contiguous = &xbox::title_allocate_contiguous;
        host.free_contiguous = &xbox::title_free_contiguous;
        host.protect = &xbox::title_protect;
        host.protection = &xbox::title_protection;
        host.memory_statistics = [](uint32_t* total, uint32_t* available, uint32_t* reserved) {
            const xbox::TitleMemoryStatistics statistics = xbox::title_memory_statistics();
            *total = statistics.total_pages;
            *available = statistics.available_pages;
            *reserved = statistics.reserved_bytes;
        };
        host.reserve = &xbox::title_reserve;
        host.relaunch = [](const uint8_t* page) { loader::request_title_restart(page, xbox::kLaunchDataPageSize); };
        host.launch_data_page = &xbox::launch_data_page_word();
        host.language = options.language;
        host.widescreen = options.widescreen;
        host.seh_chain_end = [] {
            const loader::ThreadEnv* env = loader::current_thread_env();
            return env ? env->seh_chain_end : 0xFFFFFFFFu;
        };
        host.fatal = &loader::fatal_with_backtrace;
        game::set_title_host(host);
        // The title's folders for our native files (--own-main; --check-crt compares them with the kernel's).
        game::files::set_folders(assets, options.data.value_or(platform::user_data_dir()));
        if (options.own_main) {
            game::set_native_platform(true);
            // Our native files (the loader replaces this with the kernel's table, which forwards them).
            audio::set_title_file_lookup([](uint32_t handle) { return game::files::host_file(handle); });
            xbox::set_event_signal([](uint32_t handle) { game::objects::set_event(handle, true); });
        }
    }
    game::set_gfx_device(&render::gfx_device, &render::open_gfx_device);
    if (!options.compare) {
        game::renderer_pc_hooks().smooth_models = [] { return render::active_graphics_features().smooth_models; };
        game::renderer_pc_hooks().original_shadows = [] {
            return render::active_graphics_features().original_shadows;
        };
        if (options.renderer == render::BackendConfig::Api::Vulkan) {
            game::renderer_pc_hooks().sun_shadows = [] { return render::active_graphics_features().sun_shadows; };
            game::renderer_pc_hooks().set_scene_view = [](const render::SceneView& view) {
                render::set_scene_view(view);
            };
            game::renderer_pc_hooks().lighting_features = [] {
                const render::GraphicsFeatures f = render::active_graphics_features();
                return (f.pixel_lighting ? game::kLightingPixel : 0u) | (f.normal_maps ? game::kLightingNormalMaps : 0u) |
                       (f.specular ? game::kLightingSpecular : 0u) |
                       (f.world_lights ? game::kLightingWorldLights : 0u) | (f.cavity ? game::kLightingCavity : 0u);
            };
        }
    }
    render::set_gfx_state_probe(&game::ngl_gfx_state);  // XB_GFX_VERIFY        // and the renderer C's (once the title made its device)
    render::D3DConfig d3d_config;
    d3d_config.backend.window_scale = options.scale;
    d3d_config.backend.anisotropy = options.anisotropy;
    // Native pacing at the display's rate (patches.cpp install_frame_rate): present waits for the display. Not in
    // replays (they run at the recording's pace or faster, often several at once in windows that may be covered).
    d3d_config.backend.wait_for_display = title.id == loader::TitleId::Campaign && options.frame_rate == 0 &&
                                          options.pc_patches.value_or(!options.compare) &&
                                          options.replay.mode != xbox::replay::Mode::Replay;
    d3d_config.backend.d3d9 = options.d3d9;
    d3d_config.backend.api = options.renderer;
    // Multiplayer still draws on the Xbox D3D path (its renderer is the original code, not our C through
    // GfxDevice), which only the Direct3D 9 back end has: Vulkan would show the background and nothing else.
    if (title.id != loader::TitleId::Campaign && options.renderer == render::BackendConfig::Api::Vulkan) {
        d3d_config.backend.api = render::BackendConfig::Api::D3D9;
        LOG_INFO("render", "multiplayer: Direct3D 9 (its renderer is not ours yet; Vulkan draws only ours)");
    }
    d3d_config.backend.vulkan_validation = options.vulkan_validation;
    d3d_config.backend.brightness = options.brightness;
    const bool sharp_text =
        options.sharp_text && text::prepare_sharp_text(assets, platform::executable_dir() / "fonts") > 0;
    const bool key_prompts = options.button_prompts != loader::TitlePatchConfig::ButtonPrompts::Controller &&
                             text::prepare_key_prompts(assets, platform::executable_dir() / "fonts") > 0;
    // Texture packs (phase 1b): outside the Enhanced switch, never in --compare runs.
    const bool packs =
        options.mods && !options.compare &&
        !render::texture_pack::open(options.mods_dir.value_or(options.data.value_or(platform::user_data_dir()) / "mods"),
                                    options.mod_list)
             .empty();
    if (sharp_text || key_prompts || packs)
        d3d_config.backend.replace_texture = [sharp_text, key_prompts, packs](uint64_t content, float render_scale,
                                                                            std::span<const uint8_t> original) {
            std::shared_ptr<const render::TextureReplacement> replacement;
            if (key_prompts) replacement = text::key_prompt_replacement(content, render_scale);
            if (!replacement && sharp_text) replacement = text::sharp_text_replacement(content, render_scale);
            if (!replacement && packs) replacement = render::texture_pack::replacement(content, original);
            return replacement;
        };
    if (key_prompts && packs)
        d3d_config.backend.replacement_version = [](uint64_t content) {
            return (text::key_prompt_version(content) * 0x9e3779b1u) ^ render::texture_pack::version(content);
        };
    else if (key_prompts)
        d3d_config.backend.replacement_version = &text::key_prompt_version;
    else if (packs)
        d3d_config.backend.replacement_version = &render::texture_pack::version;
    if (options.widescreen) {
        d3d_config.backend.display_aspect = aspect;
        d3d_config.backend.pillarbox_flat_frames = true;  // the menus are 4:3 layouts
        d3d_config.backend.narrow_ui_draw = &loader::in_game_menu_draw;
    }
    // The ` overlay (Dear ImGui) is a developer tool since phase 4.5: players set everything in the game's own
    // menus (Options, the pause menu's Settings).
    if (options.dev) {
        d3d_config.backend.pc_menu = &loader::pc_menu;
        d3d_config.backend.pc_menu_shown = &loader::pc_menu_shown;
        d3d_config.backend.pc_menu_event = &loader::pc_menu_event;
        loader::set_pc_menu_notice(true);
    }
    // The front-end menus' PC rows (src/engine/client/fe_pc.h); --compare and --pc-patches off: the original menus.
    if (options.pc_patches.value_or(!options.compare)) {
        loader::install_menu_settings();
        loader::install_menu_pages(options.mods_dir.value_or(options.data.value_or(platform::user_data_dir()) / "mods"),
                                   options.replay.mode == xbox::replay::Mode::Off);
    }
    d3d_config.backend.menu_font = (platform::executable_dir() / "fonts" / "Jost-500-Medium.ttf").string();
    d3d_config.backend.pipeline_cache_path =
        (options.data.value_or(platform::user_data_dir()) / "cache" / "vulkan_pipelines.bin").string();
    d3d_config.backend.fullscreen = options.fullscreen;
    d3d_config.backend.render_scale = options.render_scale;  // 0: the back end picks
    d3d_config.screenshot_interval = options.screenshot_interval;
    if (options.check_crt) {
        // On the title's main thread at its first frame: the title's CRT is initialized by then.
        d3d_config.frame_presented = [] {
            std::string report;
            const int32_t differences = game::check_title_crt(report) + game::check_title_xapi(report);
            if (!report.empty()) LOG_WARN("crt", "\n%s", report.c_str());
            LOG_INFO("crt", "check: %d difference(s)", differences);
            std::fflush(nullptr);
            std::_Exit(differences ? 3 : 0);
        };
    } else if (options.replay.mode != xbox::replay::Mode::Off) {
        d3d_config.frame_presented = &xbox::replay::frame;
        if (options.replay.mode == xbox::replay::Mode::Replay) d3d_config.vblank_speed = options.replay_speed;
        if (options.replay.mode == xbox::replay::Mode::Replay && options.replay_no_draw) {
            render::g_skip_gpu_draws = true;
            if (!options.replay.shot_frames.empty()) {
                // Pictures wanted: draw only the frames just before each (caches and fades settle), the rest no-draw.
                render::g_skip_keeps_uploads = true;
                d3d_config.frame_presented = [] {
                    xbox::replay::frame();
                    render::g_skip_gpu_draws = xbox::replay::frames_to_next_shot() > kDrawFramesBeforeShot;
                };
            }
        }
        if (!options.replay.shot_frames.empty()) d3d_config.frame_screenshot = &xbox::replay::screenshot_path;
    }
    if (!options.menu_walk.empty() && !loader::load_menu_walk(options.menu_walk, options.menu_walk_dir)) {
        std::fflush(nullptr);
        std::_Exit(2);  // an unattended run without its script would only sit in the menus
    }
    if (!options.menu_walk.empty()) {
        static void (*inner)() = nullptr;
        inner = d3d_config.frame_presented;
        d3d_config.frame_presented = [] {
            if (inner) inner();
            loader::menu_walk_frame();
        };
        d3d_config.frame_screenshot = &loader::menu_walk_screenshot;
    }
    if (options.audio_capture) {
        const std::string base = options.audio_capture->string();
        g_capture.frames = std::fopen((base + ".frames.csv").c_str(), "w");
        g_capture.voices = std::fopen((base + ".voices.csv").c_str(), "w");
        if (g_capture.frames && g_capture.voices) {
            std::fputs("frame,sample,listener_x,listener_y,listener_z,front_x,front_y,front_z,camera_x,camera_y,camera_z,eye_x,eye_y,"
                       "eye_z,top_x,top_y,top_z\n",
                       g_capture.frames);
            std::fputs("frame,sample,voice,category,spatial,x,y,z,distance,distance_gain,volume,occlusion,gain,direct,"
                       "direct_hf,send,level,direct_lf,turn\n", g_capture.voices);
            g_capture.inner = d3d_config.frame_presented;
            d3d_config.frame_presented = &audio_capture_frame;
        } else {
            LOG_ERROR("audio", "cannot write the capture tables beside %s", base.c_str());
        }
    }
    render::dev::on_debug_view([](uint32_t view) {
        game::renderer_pc_hooks().debug_view = view;
        if (view != render::dev::kViewSound) render::dev::set_shapes({});
    });
    game::SndScene_SetLog([](const char* line) { LOG_INFO("audio", "%s", line); });
    game::renderer_pc_hooks().set_overlay_shapes = [](std::vector<render::dev::Shape>&& shapes) {
        render::dev::set_shapes(std::move(shapes));
    };
    if (!options.debug_view.empty()) render::dev::set_debug_view(options.debug_view.c_str());
    if (options.diag_dir) {
        d3d_config.diag_dir = options.diag_dir->string();
        d3d_config.diag_seconds = options.diag_seconds;
        d3d_config.pause_audio = &audio::set_paused;
        d3d_config.log_audio = [](const char* prefix) {
            audio::log_voices(&game::SndEnv_DescribeOcclusion);
            write_sound_mark(prefix);
        };
    }
    if (options.dev && !options.compare) {
        d3d_config.dev = true;
        if (!options.viewer_models.empty()) {
            loader::set_viewer_models(options.viewer_models);
            render::dev::set_extra(&loader::viewer_frame);
        }
        if (!options.orbit_file.empty() && loader::load_orbit(options.orbit_file))
            render::dev::set_extra(&loader::orbit_frame);
        // The game's own pause (cl_paused, as its pause menu): the world stops, frames are still drawn.
        d3d_config.dev_pause = [](bool paused) {
            game::Cvar_Set("cl_paused", paused ? "1" : "0");
            audio::set_paused(paused);
        };
    }
    if (options.screenshot_dir) {
        fs::create_directories(*options.screenshot_dir);
        d3d_config.screenshot_dir = options.screenshot_dir->string();
    }
    d3d_config.device_pointer = loader::title_address(title.d3d_device_pointer, "(D3D device pointer)");
    d3d_config.render_state = loader::title_address(title.d3d_render_state, "(D3D render states)");
    d3d_config.texture_state = loader::title_address(title.d3d_texture_state, "(D3D texture states)");
    d3d_config.device_failed = device_failed;
    render::configure(d3d_config);
    input::configure({loader::title_address(title.gamepad_type, "(gamepad device type)")});
}

}  // namespace loader
