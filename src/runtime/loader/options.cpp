// Option parsing shared by the loader and the own engine host (options.h).

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
#include "dispatcher.h"
#include "snd_env.h"
#include "sound_engine.h"
#include "kernel.h"
#include "civil_time.h"
#include "kernel_memory.h"
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
#include "settings_schema.h"
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

namespace loader {

void print_usage(const char* program) { std::fputs(platform::settings_help(program).c_str(), stdout); }

std::string option_name(std::string arg) {
    if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') arg.insert(0, "-");  // -diag works like --diag
    return arg;
}

namespace {

// A checked value (platform::check_setting's normal form) into Options. The table in src/platform/settings_schema.cpp
// says which settings exist, their values and --help; this says where each goes. Parts of the combined settings
// (fxaa, volume-music, ...) are collected as "key=value" text and parsed once at the end.
struct Combined {
    std::string graphics_features, volume, reverb, enhanced;
};

bool apply_setting(Options& o, Combined& combined, const platform::SettingDef& def, const std::string& v) {
    const std::string_view key = def.key;
    const bool on = v == "on" || v == "true";
    const auto f = [&] { return std::strtof(v.c_str(), nullptr); };
    const auto u = [&] { return static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10)); };
    if (def.part_of && std::string_view(def.part_of) == "keys") {  // key-<action> = "c left-ctrl": replaces its keys
        std::string text;
        std::istringstream in(v);
        for (std::string word; in >> word;) text += std::string(" ") + def.part_key + "=" + word;
        input::parse_bindings(text, o.bindings, &o.bindings_error, true);
    } else if (def.part_of) {
        const std::string_view part_of = def.part_of;
        std::string& text = part_of == "graphics-features" ? combined.graphics_features
                            : part_of == "volume"          ? combined.volume
                            : part_of == "reverb"          ? combined.reverb
                                                           : combined.enhanced;
        text += std::string(" ") + def.part_key + "=" + v;
    } else if (key == "graphics-features") {
        combined.graphics_features += " " + v;
    } else if (key == "volume") {
        combined.volume += " " + v;
    } else if (key == "reverb") {
        combined.reverb += " " + v;
    } else if (key == "enhanced") {
        combined.enhanced += " " + v;
    } else if (key == "assets") {
        o.assets = fs::path(v);
    } else if (key == "xbe") {
        o.xbe = fs::path(v);
    } else if (key == "data") {
        o.data = fs::path(v);
    } else if (key == "symbols") {
        o.symbols = fs::path(v);
    } else if (key == "multiplayer") {
        o.multiplayer = on;
    } else if (key == "language") {
        static constexpr std::pair<std::string_view, uint32_t> kLanguages[] = {
            {"english", 1}, {"german", 3}, {"french", 4}, {"spanish", 5}, {"italian", 6}};
        for (const auto& [name, id] : kLanguages)
            if (v == name) o.language = id;
    } else if (key == "memory") {
        o.memory_mb = u();
    } else if (key == "fullscreen") {
        o.fullscreen = on;
    } else if (key == "windowed") {
        if (on) o.fullscreen = false;
    } else if (key == "scale") {
        o.scale = u();
    } else if (key == "render-scale") {
        o.render_scale = f();
    } else if (key == "aspect") {
        float w = 0, h = 0;
        o.widescreen = v != "4:3";
        if (o.widescreen)
            o.aspect = v == "16:9" || std::sscanf(v.c_str(), "%f:%f", &w, &h) != 2 ? 16.0f / 9.0f : w / h;
    } else if (key == "fov") {
        o.field_of_view = f();
    } else if (key == "brightness") {
        o.brightness = f();
    } else if (key == "fps") {
        o.frame_rate = v == "display" ? 0 : std::atoi(v.c_str());
    } else if (key == "late-frames") {
        o.late_frames_wait = v == "wait";
    } else if (key == "sharp-text") {
        o.sharp_text = on;
    } else if (key == "button-prompts") {
        using Prompts = loader::TitlePatchConfig::ButtonPrompts;
        o.button_prompts = v == "keyboard" ? Prompts::Keyboard : v == "controller" ? Prompts::Controller : Prompts::Auto;
    } else if (key == "renderer") {
        o.renderer = v == "d3d9" ? render::BackendConfig::Api::D3D9 : render::BackendConfig::Api::Vulkan;
    } else if (key == "d3d9") {
        using D3D9 = render::BackendConfig::D3D9;
        o.d3d9 = v == "dxvk" ? D3D9::Dxvk : v == "system" ? D3D9::System : D3D9::Auto;
    } else if (key == "graphics") {
        o.graphics_enhanced = v == "enhanced";
    } else if (key == "anisotropy") {
        o.anisotropy = u();
    } else if (key == "glow") {
        o.glow = on;
    } else if (key == "vk-validation") {
        o.vulkan_validation = on;
    } else if (key == "silent") {
        o.silent = on;
    } else if (key == "audio") {
        o.audio_enhanced = v == "enhanced";
    } else if (key == "speakers") {
        using Speakers = audio::OutputConfig::Speakers;
        static const std::pair<const char*, Speakers> kLayouts[] = {
            {"auto", Speakers::Auto}, {"stereo", Speakers::Stereo},       {"headphones", Speakers::Headphones},
            {"quad", Speakers::Quad}, {"5.1", Speakers::Surround51}, {"7.1", Speakers::Surround71}};
        for (const auto& [name, layout] : kLayouts)
            if (v == name) o.speakers = layout;
    } else if (key == "audio-device") {
        o.audio_device = v;
    } else if (key == "mouse-sensitivity") {
        o.mouse_sensitivity = f();
    } else if (key == "keys") {  // the combined form (command line, recordings): onto what came before
        input::parse_bindings(v, o.bindings, &o.bindings_error, true);
    } else if (key == "aim-assist") {
        o.aim_assist = on;
    } else if (key == "mounted-gun-fix") {
        o.mounted_gun_fix = on;
    } else if (key == "net-port") {
        o.net_port = static_cast<uint16_t>(u());
    } else if (key == "level") {
        o.start_level = std::atoi(v.c_str());
    } else if (key == "exec") {
        if (!o.exec_text.empty()) o.exec_text += ';';
        o.exec_text += v;
    } else if (key == "press") {
        o.press_script = v;
    } else if (key == "profile") {
        o.profile = v;
    } else if (key == "debug") {
        if (on) o.log_level = platform::LogLevel::Debug;
    } else if (key == "trace") {
        if (on) {
            o.log_level = platform::LogLevel::Trace;
            o.trace_kernel = true;
        }
    } else if (key == "any-release") {
        o.any_release = on;
    } else if (key == "pc-patches") {
        o.pc_patches = on;
    } else if (key == "record" || key == "replay") {
        o.replay.mode = key == "record" ? xbox::replay::Mode::Record : xbox::replay::Mode::Replay;
        o.replay.file = fs::path(v);
    } else if (key == "replay-at-switch") {
        o.replay_at_switch = on;
    } else if (key == "replay-hashes") {
        o.replay.hashes_out = fs::path(v);
    } else if (key == "replay-continue") {
        if (on) o.replay.exit_at_end = false;
    } else if (key == "replay-speed") {
        o.replay_speed = std::atof(v.c_str());
    } else if (key == "replay-no-draw") {
        o.replay_no_draw = on;
    } else if (key == "own-main") {
        o.own_main |= on;
    } else if (key == "host") {
        if (on) o.own_main = o.host = true;
    } else if (key == "text-clear") {
        o.text_clear_out = fs::path(v);
    } else if (key == "check-crt") {
        o.check_crt = on;
    } else if (key == "dump-shaders") {
        o.shader_dump_dir = fs::path(v);
    } else if (key == "write-signatures") {
        o.write_signatures = fs::path(v);
    } else if (key == "compare" || key == "compare-all") {
        o.compare |= on;
        o.compare_all |= on && key == "compare-all";
    } else {
        return false;
    }
    return true;
}

// The settings with more than one value or an optional one (SettingType::Custom). False on a bad value (reported).
bool parse_custom(Options& options, const std::string& arg, int argc, char** argv, int& i) {
    if (arg == "--screenshots") {
        if (i + 2 >= argc || (options.screenshot_interval = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1) {
            std::fprintf(stderr, "--screenshots needs a frame interval and a folder\n");
            return false;
        }
        options.screenshot_dir = fs::path(argv[++i]);
    } else if (arg == "--replay-reference") {
        if (i + 2 >= argc) {
            std::fprintf(stderr, "--replay-reference needs a file and a folder\n");
            return false;
        }
        options.replay.reference = fs::path(argv[++i]);
        options.replay.reference_dump_dir = fs::path(argv[++i]);
    } else if (arg == "--replay-shots") {
        if (i + 2 >= argc) {
            std::fprintf(stderr, "--replay-shots needs frame numbers (1,2,...) and a folder\n");
            return false;
        }
        std::string list = argv[++i];
        for (size_t p = 0; p <= list.size();) {
            const size_t comma = std::min(list.find(',', p), list.size());
            const long n = std::atol(list.substr(p, comma - p).c_str());
            if (n < 1) {
                std::fprintf(stderr, "--replay-shots: frame numbers start at 1 (got \"%s\")\n", list.c_str());
                return false;
            }
            options.replay.shot_frames.push_back(static_cast<uint32_t>(n));
            p = comma + 1;
        }
        std::sort(options.replay.shot_frames.begin(), options.replay.shot_frames.end());
        options.replay.shot_dir = fs::path(argv[++i]);
    } else if (arg == "--replay-dump") {
        const char* range = i + 2 < argc ? argv[i + 1] : "";
        const char* dash = std::strchr(range, '-');
        options.replay.dump_last = dash ? static_cast<uint32_t>(std::atoi(dash + 1)) : 0;
        const char* colon = std::strchr(range, ':');
        options.replay.dump_step = colon ? static_cast<uint32_t>(std::atoi(colon + 1)) : 1;
        if (i + 2 >= argc || (options.replay.dump_frame = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1) {
            std::fprintf(stderr, "--replay-dump needs a frame number and a folder\n");
            return false;
        }
        options.replay.dump_dir = fs::path(argv[++i]);
    } else if (arg == "--force") {
        // NAME(ARGS)@SECONDS*COUNT?WHEN; all but NAME optional
        std::string text = i + 1 < argc ? argv[++i] : "";
        loader::TitlePatchConfig::ForcedCall forced;
        if (text.starts_with("scene:")) {
            forced.scene = true;
            text.erase(0, 6);
        }
        if (const size_t when = text.find('?'); when != std::string::npos) {
            forced.when = text.substr(when + 1);
            text.resize(when);
        }
        const size_t open = text.find('('), close = text.rfind(')');
        const size_t at = text.find('@', close == std::string::npos ? 0 : close);
        const size_t times = text.find('*', close == std::string::npos ? 0 : close);
        forced.name = text.substr(0, std::min({open, at, times}));
        if (open != std::string::npos && close != std::string::npos && close > open)
            forced.args = text.substr(open + 1, close - open - 1);
        if (at != std::string::npos) forced.after_ms = static_cast<uint32_t>(std::atof(text.c_str() + at + 1) * 1000);
        if (times != std::string::npos) forced.count = static_cast<uint32_t>(std::atoi(text.c_str() + times + 1));
        if (forced.name.empty() || (open != std::string::npos) != (close != std::string::npos) || !forced.count) {
            std::fprintf(stderr, "--force needs NAME(ARGS)@SECONDS*COUNT, e.g. \"FxPools_Destroy()@20*10\"\n");
            return false;
        }
        options.forced_calls.push_back(forced);
    } else if (arg == "--diag") {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "--diag needs a folder\n");
            return false;
        }
        options.diag_dir = fs::path(argv[++i]);
        if (i + 1 < argc && argv[i + 1][0] != '-') {
            options.diag_seconds = static_cast<uint32_t>(std::atoi(argv[++i]));
            if (options.diag_seconds < 1) {
                std::fprintf(stderr, "--diag needs seconds >= 1\n");
                return false;
            }
        }
    } else if (arg == "--text-trap") {
        if (i + 2 >= argc) {
            std::fprintf(stderr, "--text-trap needs a list and an output file\n");
            return false;
        }
        options.text_trap_list = argv[++i];
        options.text_trap_out = argv[++i];
    } else if (arg == "--launch-data" && i + 1 < argc) {
        options.launch_data_fd = std::atoi(argv[++i]);
    } else if (arg == "--session-start" && i + 1 < argc) {
        options.session_start_ns = std::strtoll(argv[++i], nullptr, 10);
    } else {
        return false;
    }
    return true;
}

}  // namespace

std::optional<Options> parse_args(int argc, char** argv) {
    Options options;
    Combined combined;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = option_name(argv[i]);
        options.given.insert(arg);
        const platform::SettingDef* def = arg.starts_with("--") ? platform::find_setting(arg.substr(2)) : nullptr;
        if (!def) {
            print_usage(argv[0]);
            return std::nullopt;
        }
        if (def->type == platform::SettingType::Custom) {
            if (!parse_custom(options, arg, argc, argv, i)) return std::nullopt;
            continue;
        }
        std::string error;
        std::optional<std::string> value;
        if (def->type == platform::SettingType::Flag)
            value = "true";
        else if (i + 1 < argc)
            value = platform::check_setting(*def, argv[++i], &error);
        else
            error = def->arg;
        if (!value) {
            std::fprintf(stderr, "%s needs %s\n", arg.c_str(), error.c_str());
            return std::nullopt;
        }
        if (!apply_setting(options, combined, *def, *value)) {
            std::fprintf(stderr, "%s is not an option of the game\n", arg.c_str());
            return std::nullopt;
        }
    }
    // The parts in the order given: a later value wins over an earlier one (settings.ini, then the command line).
    if (!combined.graphics_features.empty())
        options.graphics_features = render::GraphicsFeatures::parse(combined.graphics_features);
    if (!combined.volume.empty()) options.volumes = audio::CategoryVolumes::parse(combined.volume);
    if (!combined.reverb.empty()) options.reverb = audio::ReverbTuning::parse(combined.reverb);
    if (!combined.enhanced.empty()) options.features = audio::EnhancedFeatures::parse(combined.enhanced);
    return options;
}

}  // namespace loader
