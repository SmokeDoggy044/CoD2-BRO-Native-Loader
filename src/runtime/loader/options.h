#pragma once

// The loader's and the own engine host's options (command line + settings.ini).

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
#include "bindings.h"
#include "patches.h"
#include "replay.h"
#include "settings.h"
#include "enhanced_graphics.h"
#include "sound_engine.h"
#include "reverb_tuning.h"
#include "xbox_d3d_api.h"
#include "log.h"

namespace loader {

namespace fs = std::filesystem;

struct Options {
    std::optional<fs::path> assets;
    std::optional<fs::path> xbe;
    std::optional<fs::path> symbols;
    std::optional<fs::path> data;
    platform::LogLevel log_level = platform::LogLevel::Info;
    bool trace_kernel = false;
    uint32_t memory_mb = 64;
    uint32_t scale = 2;
    uint32_t anisotropy = 16;
    render::BackendConfig::D3D9 d3d9 = render::BackendConfig::D3D9::Auto;
    render::BackendConfig::Api renderer = render::BackendConfig::Api::Vulkan;  // D3D9 = fallback
    bool vulkan_validation = false;
    float brightness = 1.0f;
    bool sharp_text = true;
    bool mods = true;                  // --mods (phase 1b texture packs)
    std::optional<fs::path> mods_dir;  // --mods-dir
    loader::TitlePatchConfig::ButtonPrompts button_prompts = loader::TitlePatchConfig::ButtonPrompts::Auto;
    float render_scale = 0;  // 0: the window's (fullscreen: the screen's) height
    bool fullscreen = false;
    bool widescreen = true;
    float field_of_view = 0;      // cg_fov; 0: the game's own (70)
    float aspect = 16.0f / 9.0f;  // with widescreen: the picture's shape (wider than 16:9: experimental)
    uint32_t language = 1;  // XC_LANGUAGE
    uint32_t screenshot_interval = 0;
    std::optional<fs::path> screenshot_dir;
    bool silent = false;
    double replay_speed = 1;  // --replay-speed
    bool replay_no_draw = false;  // --replay-no-draw
    bool audio_enhanced = true;  // --audio
    audio::OutputConfig::Speakers speakers = audio::OutputConfig::Speakers::Auto;
    std::string audio_device;    // empty: the system's
    audio::ReverbTuning reverb;  // --reverb
    audio::CategoryVolumes volumes;  // --volume
    audio::EnhancedFeatures features;  // --enhanced
    bool graphics_enhanced = true;               // --graphics
    render::GraphicsFeatures graphics_features;  // --graphics-features
    std::string press_script;
    xbox::replay::Config replay;  // --record / --replay / --replay-dump
    float mouse_sensitivity = 0.11f;
    input::Bindings bindings = input::default_bindings();  // --keys
    std::string bindings_error;  // a word of --keys it did not know (logged; the rest applies)
    bool aim_assist = true;
    bool mounted_gun_fix = true;  // --mounted-gun-fix (loader::TitlePatchConfig::mounted_gun_fix)
    bool glow = false;            // --glow (loader::TitlePatchConfig::glow)
    int32_t frame_rate = 0;  // --fps: 0 = the display's rate (loader::TitlePatchConfig::frame_rate)
    bool late_frames_wait = false;
    bool any_release = false;    // start an XBE whose build is not the supported one (testing)
    std::optional<fs::path> write_signatures;  // write the XBE's signatures there and exit
    int32_t start_level = -1;
    std::string exec_text;       // --exec
    std::vector<loader::TitlePatchConfig::ForcedCall> forced_calls;  // --force
    bool multiplayer = false;    // start codmp_xboxf.xbe
    bool compare = false;        // test decompiled functions against the originals
    bool check_crt = false;      // --check-crt: our CRT vs the title's on a table, then exit
    bool replay_at_switch = false;  // --replay-at-switch: the recording/replay begins at the first in-process restart
    bool own_main = false;       // --own-main: our Sys_Main C instead of the original (own engine host, 0.3)
    bool host = false;           // --host: --own-main without original engine code or kernel (host_image.h)
    std::optional<fs::path> text_trap_list, text_trap_out;  // --text-trap LIST OUT
    std::optional<fs::path> text_clear_out;                 // --text-clear OUT
    bool compare_all = false;    // ... settled ones too
    std::optional<bool> pc_patches;  // unset: on, except with --compare
    std::set<std::string> given;     // options named on the command line or in settings.ini
    uint16_t net_port = 3074;    // system link: UDP net_port .. net_port + 3
    std::optional<fs::path> diag_dir;
    std::optional<fs::path> shader_dump_dir;  // --dump-shaders
    uint32_t diag_seconds = 10;
    bool dev = false;  // --dev: developer overlay and keys
    std::optional<std::string> profile;  // --profile, else XB_PROFILE
    // Set by a title reboot (see reboot_title), not by users.
    std::optional<int> launch_data_fd;
    std::optional<int64_t> session_start_ns;
};

void print_usage(const char* program);
std::string option_name(std::string arg);
std::optional<Options> parse_args(int argc, char** argv);

}  // namespace loader
