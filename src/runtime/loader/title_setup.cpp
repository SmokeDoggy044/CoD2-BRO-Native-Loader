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
#include "snd.h"
#include "title_xapi.h"
#include "title_host.h"
#include "title_files.h"
#include "title_objects.h"
#include "snd_env.h"
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
    audio_config.device = options.silent || fast_replay ? audio::OutputConfig::Device::Loopback
                                                         : audio::OutputConfig::Device::Default;
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
    render::set_gfx_state_probe(&game::ngl_gfx_state);  // XB_GFX_VERIFY        // and the renderer C's (once the title made its device)
    render::D3DConfig d3d_config;
    d3d_config.backend.window_scale = options.scale;
    d3d_config.backend.anisotropy = options.anisotropy;
    d3d_config.backend.d3d9 = options.d3d9;
    d3d_config.backend.brightness = options.brightness;
    const bool sharp_text =
        options.sharp_text && text::prepare_sharp_text(assets, platform::executable_dir() / "fonts") > 0;
    const bool key_prompts = options.button_prompts != loader::TitlePatchConfig::ButtonPrompts::Controller &&
                             text::prepare_key_prompts(assets, platform::executable_dir() / "fonts") > 0;
    if (sharp_text || key_prompts)
        d3d_config.backend.replace_texture = [sharp_text, key_prompts](uint64_t content, float render_scale) {
            std::shared_ptr<const render::TextureReplacement> replacement;
            if (key_prompts) replacement = text::key_prompt_replacement(content, render_scale);
            if (!replacement && sharp_text) replacement = text::sharp_text_replacement(content, render_scale);
            return replacement;
        };
    if (key_prompts) d3d_config.backend.replacement_version = &text::key_prompt_version;
    if (options.widescreen) {
        d3d_config.backend.display_aspect = aspect;
        d3d_config.backend.pillarbox_flat_frames = true;  // the menus are 4:3 layouts
        d3d_config.backend.narrow_ui_draw = &loader::in_game_menu_draw;
    }
    d3d_config.backend.pc_menu = &loader::pc_menu;
    d3d_config.backend.pc_menu_shown = &loader::pc_menu_shown;
    // The front-end menus' PC rows (src/engine/client/fe_pc.h); --compare and --pc-patches off: the original menus.
    if (options.pc_patches.value_or(!options.compare)) loader::install_menu_settings();
    d3d_config.backend.menu_font = (platform::executable_dir() / "fonts" / "Jost-500-Medium.ttf").string();
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
        if (options.replay.mode == xbox::replay::Mode::Replay && options.replay_no_draw) render::g_skip_gpu_draws = true;
        if (!options.replay.shot_frames.empty()) d3d_config.frame_screenshot = &xbox::replay::screenshot_path;
    }
    if (options.diag_dir) {
        d3d_config.diag_dir = options.diag_dir->string();
        d3d_config.diag_seconds = options.diag_seconds;
        d3d_config.pause_audio = &audio::set_paused;
        d3d_config.log_audio = [] { audio::log_voices(&game::SndEnv_DescribeOcclusion); };
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
