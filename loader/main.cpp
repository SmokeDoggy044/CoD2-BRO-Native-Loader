// Route-B loader: runs the original default.xbe natively on 32-bit x86 (Linux or
// Windows), with the Xbox kernel and the SDK libraries replaced by our own code.

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
#include "compare.h"
#include "crash.h"
#include "diag_summary.h"
#include "functions.h"
#include "snd.h"
#include "kernel.h"
#include "kernel_memory.h"
#include "kernel_thunks.h"
#include "log.h"
#include "os.h"
#include "patches.h"
#include "pc_menu.h"
#include "profiler.h"
#include "release_map.h"
#include "paths.h"
#include "program.h"
#include "script.h"
#include "settings.h"
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

namespace fs = std::filesystem;

namespace {

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
    float brightness = 1.0f;
    bool sharp_text = true;
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
    bool audio_enhanced = true;  // --audio
    audio::OutputConfig::Speakers speakers = audio::OutputConfig::Speakers::Auto;
    std::string audio_device;    // empty: the system's
    std::string press_script;
    float mouse_sensitivity = 0.11f;
    input::Bindings bindings = input::default_bindings();  // --keys
    std::string bindings_error;  // a word of --keys it did not know (logged; the rest applies)
    bool aim_assist = true;
    int32_t frame_rate = 60;
    bool late_frames_wait = false;
    bool any_release = false;    // start an XBE whose build is not the supported one (testing)
    std::optional<fs::path> write_signatures;  // write the XBE's signatures there and exit
    int32_t start_level = -1;
    std::string exec_text;       // --exec
    std::vector<loader::TitlePatchConfig::ForcedCall> forced_calls;  // --force
    bool multiplayer = false;    // start codmp_xboxf.xbe
    bool compare = false;        // test decompiled functions against the originals
    bool compare_all = false;    // ... settled ones too
    std::optional<bool> pc_patches;  // unset: on, except with --compare
    std::set<std::string> given;     // options named on the command line or in settings.ini
    uint16_t net_port = 3074;    // system link: UDP net_port .. net_port + 3
    std::optional<fs::path> diag_dir;
    uint32_t diag_seconds = 10;
    std::optional<std::string> profile;  // --profile, else XB_PROFILE
    // Set by a title reboot (see reboot_title), not by users.
    std::optional<int> launch_data_fd;
    std::optional<int64_t> session_start_ns;
};

// The command line, repeated when the title reboots into another XBE.
std::vector<std::string> g_args;
fs::path g_xbe;  // the running XBE
std::optional<fs::path> g_diag_dir;
fs::path g_log_path;           // this run's log file
bool g_tell_player = false;    // error boxes (not in scripted runs)

// What went wrong, for a player without a console (a message box on Windows).
void tell_player(const std::string& what) {
    if (!g_tell_player) return;
    loader::os::show_error("Call of Duty 2: Big Red One", what + "\n\nDetails are in the log:\n" + g_log_path.string());
}

// The mapped image, for release_map.h.
loader::ImageBytes image_bytes(const loader::XbeImage& image) {
    loader::ImageBytes bytes;
    bytes.base = image.base();
    bytes.bytes = {reinterpret_cast<const uint8_t*>(uintptr_t{image.base()}), image.size()};
    for (const loader::XbeSectionHeader* section : image.sections())
        bytes.sections.push_back({image.section_name(*section), section->virtual_addr, section->virtual_size});
    return bytes;
}

int startup_failed(const std::string& what) {
    tell_player("The game could not start: " + what + ".");
    return 1;
}

void print_usage(const char* program) {
    std::printf(
        "usage: %s [options]\n"
        "  --assets DIR     game files (default: COD_BRO_ASSETS, ./assets, or the dev folder)\n"
        "  --xbe FILE       executable to load (default: <assets>/default.xbe)\n"
        "  --multiplayer    start multiplayer (<assets>/codmp_xboxf.xbe)\n"
        "  --net-port N     system link: UDP ports N to N+3 (default 3074; the same on every PC)\n"
        "  --symbols FILE   Ghidra symbol export: SDK replacements and readable backtraces\n"
        "                   (default: <xbe>_symbols.txt next to the loader, or tools/data)\n"
        "  --data DIR       where saves and the cache drive go (default: the user data folder)\n"
        "  --debug          debug logging\n"
        "  --trace          log every kernel call (implies --debug)\n"
        "  --silent         no sound output (audio still runs, into a silent device)\n"
        "  --audio original|enhanced  the sound as on the Xbox, or with our improvements\n"
        "                   (default enhanced; original with --pc-patches off): best resampling\n"
        "  --speakers auto|stereo|headphones|quad|5.1|7.1  speaker layout (default auto: the\n"
        "                   output's own, HRTF when the game's settings ask for headphones);\n"
        "                   headphones = 3D sound for headphones (HRTF) always\n"
        "  --audio-device NAME  output device (default: the system's; the launcher lists them)\n"
        "  --memory MB      Xbox memory budget (default 64, like a retail Xbox; devkit: 128)\n"
        "  --fullscreen     borderless fullscreen (Alt+Enter switches while playing)\n"
        "  --windowed       in a window (default; overrides fullscreen in settings.ini)\n"
        "  --scale N        window size as a multiple of the game's resolution (default 2)\n"
        "  --render-scale X render resolution as a multiple of the game's 640x480 (default: the\n"
        "                   window's or screen's height; above it = supersampling, e.g. 4)\n"
        "  --aspect A       16:9 (default: the game's widescreen mode, wider view and HUD) or 4:3;\n"
        "                   wider, e.g. 21:9, 32:9 or 3440:1440, is experimental (wider view,\n"
        "                   the HUD stays 16:9 in the middle)\n"
        "  --anisotropy N   anisotropic filtering for mipmapped textures, 1-16 (default 16;\n"
        "                   1 = what the game asks for)\n"
        "  --brightness X   0.5-2.0 (default 1: as on the Xbox; higher = brighter midtones)\n"
        "  --language L     english (default), german, french, spanish or italian: the console\n"
        "                   language; the game uses it where its disc has that language\n"
        "  --sharp-text on|off  the game's text drawn again at the render resolution from free\n"
        "                   look-alike fonts (fonts/ next to the loader; default on)\n"
        "  --button-prompts auto|keyboard|controller  the game's button pictures as keys and\n"
        "                   mouse buttons while the keyboard/mouse is used (auto, default),\n"
        "                   always, or the original controller buttons\n"
        "  --d3d9 WHICH     Windows: auto (default: DXVK on Vulkan, else the system's), dxvk or\n"
        "                   system (Linux always uses DXVK)\n"
        "  --screenshots N DIR  save every Nth frame as DIR/frame_<n>.bmp (debugging)\n"
        "  --press LIST     scripted presses on port 0, e.g. start@20,a@25.5,back@80+4 (seconds after\n"
        "                   start, +S holds the button S seconds;\n"
        "                   buttons: a b x y black white lt rt start back up down left right ls rs)\n"
        "  --diag DIR [S]   diagnostics while playing: DIR/log.txt, a screenshot every S seconds\n"
        "                   (default 10); F12 marks a problem: screenshot, a note typed while the\n"
        "                   game is paused (DIR/marks.txt), draw dump in the log; new warnings\n"
        "                   and errors are marked by themselves; F11 shows draws that need\n"
        "                   missing features in magenta; DIR/summary.txt on exit\n"
        "  --profile auto[,S]  each time the frame rate drops under 80%% of its aim (24 at 30 fps;\n"
        "                   at most once a minute, 20 times), log for S seconds (default 10)\n"
        "                   where each thread spends its time (START,S: once, from START\n"
        "                   seconds on; XB_PROFILE does the same)\n"
        "  --mouse-sensitivity DEG  degrees the view turns per mouse count (default 0.11)\n"
        "  --keys LIST      keyboard and mouse bindings that differ from the defaults, e.g.\n"
        "                   \"fire=mouse1 jump=space crouch=left-ctrl crouch=c\" (the launcher's\n"
        "                   Controls tab writes them; input/bindings.h)\n"
        "  --aim-assist on|off  the view following an enemy under the crosshair (default on)\n"
        "  --fps 60|30      frame rate (default 60; 30 as on the Xbox; movies always play at 30)\n"
        "  --fov DEG        field of view in degrees across a 4:3 picture (50-120, default 70\n"
        "                   as the game; campaign); also in the PC menu (` key while playing)\n"
        "  --late-frames show|wait  a frame that missed its vertical blank is shown at once\n"
        "                   (default) or waits for the next one (as on the Xbox: 30 fps drops to 20)\n"
        "  --any-release    start another release's multiplayer executable even where the\n"
        "                   port's changes were not all found in it (testing: expect crashes)\n"
        "  --write-signatures FILE  write the supported XBE's function signatures, for\n"
        "                   finding its addresses in other releases (tools/data), and exit\n"
        "  --level N        start mission N (0-13, 0 = prologue) once the menus are up (testing)\n"
        "  --exec \"CMDS\"    console commands/cvars, e.g. \"r_showtris 1; god\", run once the\n"
        "                   level is active (testing; first boot only)\n"
        "  --force \"F(ARGS)@S*N?W\"  with --compare: test N calls of F the title does not make,\n"
        "                   from S s after the level is active, on frames where W is nonzero;\n"
        "                   ARGS: eax= ... ebp= and stack words; values: numbers, [E] reads a\n"
        "                   title word, E+N (first boot only); \"scene:F(...)\" calls from\n"
        "                   R_RenderView with the main scene open (draw-list code)\n"
        "  --compare        test the decompiled functions against the originals while playing\n"
        "                   (Linux; counts in tools/data/functions.csv, details in the log);\n"
        "                   implies --pc-patches off unless that is given, ignores settings.ini;\n"
        "                   functions with 100 matched tests and no mismatch or drift are no\n"
        "                   longer tested\n"
        "  --compare-all    --compare, testing every function (a module's final check)\n"
        "  --pc-patches on|off  the PC changes to the game's code: 60 fps pacing, mouse look,\n"
        "                   aim assist off, widescreen menus and objectives (default on); off\n"
        "                   keeps only what the game needs to run on a PC (and --level), and\n"
        "                   defaults to 4:3, the game's fonts, render scale 1, no anisotropic\n"
        "                   filtering, brightness 1 and the original sound\n",
        program);
}

std::string option_name(std::string arg) {
    if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') arg.insert(0, "-");  // -diag works like --diag
    return arg;
}

std::optional<Options> parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = option_name(argv[i]);
        options.given.insert(arg);
        auto value = [&]() -> std::optional<fs::path> {
            if (i + 1 < argc) return fs::path(argv[++i]);
            std::fprintf(stderr, "%s needs a value\n", arg.c_str());
            return std::nullopt;
        };
        if (arg == "--assets") {
            if (!(options.assets = value())) return std::nullopt;
        } else if (arg == "--xbe") {
            if (!(options.xbe = value())) return std::nullopt;
        } else if (arg == "--data") {
            if (!(options.data = value())) return std::nullopt;
        } else if (arg == "--symbols") {
            if (!(options.symbols = value())) return std::nullopt;
        } else if (arg == "--memory") {
            if (i + 1 >= argc || (options.memory_mb = static_cast<uint32_t>(std::atoi(argv[++i]))) < 16 ||
                options.memory_mb > 1024) {
                std::fprintf(stderr, "--memory needs a size in MB (16-1024)\n");
                return std::nullopt;
            }
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--windowed") {
            options.fullscreen = false;
        } else if (arg == "--scale") {
            if (i + 1 >= argc || (options.scale = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1 ||
                options.scale > 8) {
                std::fprintf(stderr, "--scale needs a number (1-8)\n");
                return std::nullopt;
            }
        } else if (arg == "--render-scale") {
            if (i + 1 >= argc || (options.render_scale = std::strtof(argv[++i], nullptr)) < 0.5f ||
                options.render_scale > 8.0f) {
                std::fprintf(stderr, "--render-scale needs a number (0.5-8)\n");
                return std::nullopt;
            }
        } else if (arg == "--aspect") {
            const std::string aspect = i + 1 < argc ? argv[++i] : "";
            float w = 0, h = 0;
            char end = 0;
            if (aspect == "4:3") {
                options.widescreen = false;
            } else if (std::sscanf(aspect.c_str(), "%f:%f%c", &w, &h, &end) == 2 && h > 0 &&
                       w / h >= 16.0f / 9.0f - 0.001f && w / h <= 4.0f) {
                options.widescreen = true;
                options.aspect = aspect == "16:9" ? 16.0f / 9.0f : w / h;
            } else {
                std::fprintf(stderr, "--aspect needs 4:3, 16:9 or wider W:H up to 4:1 (e.g. 21:9)\n");
                return std::nullopt;
            }
        } else if (arg == "--anisotropy") {
            if (i + 1 >= argc || (options.anisotropy = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1 ||
                options.anisotropy > 16) {
                std::fprintf(stderr, "--anisotropy needs a number (1-16)\n");
                return std::nullopt;
            }
        } else if (arg == "--screenshots") {
            if (i + 2 >= argc || (options.screenshot_interval = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1) {
                std::fprintf(stderr, "--screenshots needs a frame interval and a folder\n");
                return std::nullopt;
            }
            options.screenshot_dir = fs::path(argv[++i]);
        } else if (arg == "--press") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--press needs a list of button@seconds\n");
                return std::nullopt;
            }
            options.press_script = argv[++i];
        } else if (arg == "--mouse-sensitivity") {
            if (i + 1 >= argc || (options.mouse_sensitivity = std::strtof(argv[++i], nullptr)) <= 0.0f ||
                options.mouse_sensitivity > 10.0f) {
                std::fprintf(stderr, "--mouse-sensitivity needs degrees per count (0-10)\n");
                return std::nullopt;
            }
        } else if (arg == "--aim-assist") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "on" && value != "off") {
                std::fprintf(stderr, "--aim-assist needs on or off\n");
                return std::nullopt;
            }
            options.aim_assist = value == "on";
        } else if (arg == "--language") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            static constexpr std::pair<std::string_view, uint32_t> kLanguages[] = {
                {"english", 1}, {"german", 3}, {"french", 4}, {"spanish", 5}, {"italian", 6}};
            const auto* found = std::find_if(std::begin(kLanguages), std::end(kLanguages),
                                             [&](const auto& l) { return l.first == value; });
            if (found == std::end(kLanguages)) {
                std::fprintf(stderr, "--language needs english, german, french, spanish or italian\n");
                return std::nullopt;
            }
            options.language = found->second;
        } else if (arg == "--keys") {
            options.bindings_error.clear();
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--keys needs a list of action=key\n");
                return std::nullopt;
            }
            input::parse_bindings(argv[++i], options.bindings, &options.bindings_error);
        } else if (arg == "--button-prompts") {
            using Prompts = loader::TitlePatchConfig::ButtonPrompts;
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value == "auto") {
                options.button_prompts = Prompts::Auto;
            } else if (value == "keyboard") {
                options.button_prompts = Prompts::Keyboard;
            } else if (value == "controller") {
                options.button_prompts = Prompts::Controller;
            } else {
                std::fprintf(stderr, "--button-prompts needs auto, keyboard or controller\n");
                return std::nullopt;
            }
        } else if (arg == "--sharp-text") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "on" && value != "off") {
                std::fprintf(stderr, "--sharp-text needs on or off\n");
                return std::nullopt;
            }
            options.sharp_text = value == "on";
        } else if (arg == "--multiplayer") {
            options.multiplayer = true;
        } else if (arg == "--net-port") {
            const long port = i + 1 < argc ? std::strtol(argv[++i], nullptr, 10) : 0;
            if (port < 1024 || port > 65532) {
                std::fprintf(stderr, "--net-port needs a UDP port (1024-65532)\n");
                return std::nullopt;
            }
            options.net_port = static_cast<uint16_t>(port);
        } else if (arg == "--fps") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "30" && value != "60") {
                std::fprintf(stderr, "--fps needs 30 or 60\n");
                return std::nullopt;
            }
            options.frame_rate = std::stoi(value);
        } else if (arg == "--fov") {
            if (i + 1 >= argc || (options.field_of_view = std::strtof(argv[++i], nullptr)) < 50.0f ||
                options.field_of_view > 120.0f) {
                std::fprintf(stderr, "--fov needs degrees across a 4:3 picture (50-120; the game's own is 70)\n");
                return std::nullopt;
            }
        } else if (arg == "--any-release") {
            options.any_release = true;
        } else if (arg == "--write-signatures") {
            if (!(options.write_signatures = value())) return std::nullopt;
        } else if (arg == "--late-frames") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "show" && value != "wait") {
                std::fprintf(stderr, "--late-frames needs show or wait\n");
                return std::nullopt;
            }
            options.late_frames_wait = value == "wait";
        } else if (arg == "--brightness") {
            options.brightness = i + 1 < argc ? std::strtof(argv[++i], nullptr) : 0.0f;
            if (!(options.brightness >= 0.5f && options.brightness <= 2.0f)) {
                std::fprintf(stderr, "--brightness needs a number (0.5-2.0)\n");
                return std::nullopt;
            }
        } else if (arg == "--d3d9") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value == "auto")
                options.d3d9 = render::BackendConfig::D3D9::Auto;
            else if (value == "dxvk")
                options.d3d9 = render::BackendConfig::D3D9::Dxvk;
            else if (value == "system")
                options.d3d9 = render::BackendConfig::D3D9::System;
            else {
                std::fprintf(stderr, "--d3d9 needs auto, dxvk or system\n");
                return std::nullopt;
            }
        } else if (arg == "--level") {
            if (i + 1 >= argc || (options.start_level = std::atoi(argv[++i])) < 0 || options.start_level > 13) {
                std::fprintf(stderr, "--level needs a mission number (0-13)\n");
                return std::nullopt;
            }
        } else if (arg == "--exec") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--exec needs console commands\n");
                return std::nullopt;
            }
            if (!options.exec_text.empty()) options.exec_text += ';';
            options.exec_text += argv[++i];
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
                return std::nullopt;
            }
            options.forced_calls.push_back(forced);
        } else if (arg == "--diag") {
            if (!(options.diag_dir = value())) return std::nullopt;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.diag_seconds = static_cast<uint32_t>(std::atoi(argv[++i]));
                if (options.diag_seconds < 1) {
                    std::fprintf(stderr, "--diag needs seconds >= 1\n");
                    return std::nullopt;
                }
            }
        } else if (arg == "--profile") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--profile needs auto[,SECONDS] or START,SECONDS\n");
                return std::nullopt;
            }
            options.profile = argv[++i];
        } else if (arg == "--launch-data" && i + 1 < argc) {
            options.launch_data_fd = std::atoi(argv[++i]);
        } else if (arg == "--session-start" && i + 1 < argc) {
            options.session_start_ns = std::strtoll(argv[++i], nullptr, 10);
        } else if (arg == "--silent") {
            options.silent = true;
        } else if (arg == "--audio") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "original" && value != "enhanced") {
                std::fprintf(stderr, "--audio needs original or enhanced\n");
                return std::nullopt;
            }
            options.audio_enhanced = value == "enhanced";
        } else if (arg == "--speakers") {
            using Speakers = audio::OutputConfig::Speakers;
            static const std::pair<const char*, Speakers> kLayouts[] = {
                {"auto", Speakers::Auto}, {"stereo", Speakers::Stereo},       {"headphones", Speakers::Headphones},
                {"quad", Speakers::Quad}, {"5.1", Speakers::Surround51}, {"7.1", Speakers::Surround71}};
            const std::string value = i + 1 < argc ? argv[++i] : "";
            const auto* layout = std::find_if(std::begin(kLayouts), std::end(kLayouts),
                                              [&](const auto& l) { return value == l.first; });
            if (layout == std::end(kLayouts)) {
                std::fprintf(stderr, "--speakers needs auto, stereo, headphones, quad, 5.1 or 7.1\n");
                return std::nullopt;
            }
            options.speakers = layout->second;
        } else if (arg == "--audio-device") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--audio-device needs a device name\n");
                return std::nullopt;
            }
            options.audio_device = argv[++i];
        } else if (arg == "--compare" || arg == "--compare-all") {
            options.compare = true;
            options.compare_all |= arg == "--compare-all";
        } else if (arg == "--pc-patches") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "on" && value != "off") {
                std::fprintf(stderr, "--pc-patches needs on or off\n");
                return std::nullopt;
            }
            options.pc_patches = value == "on";
        } else if (arg == "--debug") {
            options.log_level = platform::LogLevel::Debug;
        } else if (arg == "--trace") {
            options.log_level = platform::LogLevel::Trace;
            options.trace_kernel = true;
        } else {
            print_usage(argv[0]);
            return std::nullopt;
        }
    }
    return options;
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

// HalReturnToFirmware(QuickReboot): the Xbox reboots into `xbe` with the launch data
// page kept in memory. Here the loader replaces itself (exec) with the same command
// line for `xbe`, passing the page in a file and the session start time (log and
// --diag times continue). Scripted presses, --level and --exec are for the first boot only.
[[noreturn]] void reboot_title(const fs::path& xbe, std::span<const uint8_t> launch_data_page) {
    // Patches and symbols exist for the disc's two titles only.
    const bool same_title = fs::path(xbe).filename() == g_xbe.filename();
    if (!loader::find_title(xbe.filename().string())) {
        LOG_ERROR("loader", "reboot: %s is not supported", xbe.filename().string().c_str());
        std::_Exit(1);
    }
    if (g_diag_dir) {
        // To repeat this boot: --launch-data 3 3<launch_data.bin
        if (std::FILE* file = std::fopen((*g_diag_dir / "launch_data.bin").string().c_str(), "wb")) {
            std::fwrite(launch_data_page.data(), 1, launch_data_page.size(), file);
            std::fclose(file);
        }
    }
    loader::compare::flush();
    const int fd = loader::os::pass_launch_data(launch_data_page);
    if (fd < 0) {
        LOG_ERROR("loader", "reboot: cannot pass the launch data");
        std::_Exit(1);
    }
    std::vector<std::string> args = {g_args[0]};
    for (size_t i = 1; i < g_args.size(); ++i) {
        const std::string name = option_name(g_args[i]);
        if (name == "--xbe" || name == "--launch-data" || name == "--session-start" || name == "--press" ||
            name == "--level" || name == "--exec" || name == "--force" || name == "--multiplayer" || (name == "--symbols" && !same_title)) {
            ++i;  // dropped with its value
            continue;
        }
        args.push_back(g_args[i]);
    }
    const auto start = platform::session_start().time_since_epoch();
    args.insert(args.end(), {"--xbe", xbe.string(), "--launch-data", std::to_string(fd), "--session-start",
                             std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(start).count())});
    LOG_INFO("loader", "reboot: starting %s", xbe.string().c_str());
    loader::os::restart(args);
}

// The languages the game's text is on the disc in (Global.<code>.stb in the packs), for
// the log: the title picks its language from the console's and the disc's region (a
// European disc has no English text), so the language setting only chooses among these.
void log_disc_languages(const fs::path& assets, uint32_t language) {
    std::string codes;
    for (const char* name : {"sp/global.cod", "mp/global.cod"}) {
        const auto pack = text::KapfPack::open(assets / name);
        if (!pack) continue;
        for (const auto& entry : pack->entries()) {
            const std::string_view file = entry.name;
            if (!file.starts_with("Global.") || !file.ends_with(".stb") || file.size() <= 11) continue;
            const std::string code(file.substr(7, file.size() - 11));
            if (codes.find(code) == std::string::npos) codes += (codes.empty() ? "" : " ") + code;
        }
        break;
    }
    LOG_INFO("loader", "console language %u; text languages on the disc: %s", language,
             codes.empty() ? "unknown" : codes.c_str());
}

xbox::KernelConfig kernel_config(const Options& options, const fs::path& assets, const fs::path& xbe) {
    static std::string image_file_name;
    image_file_name = "\\Device\\CdRom0\\" + xbe.filename().string();
    xbox::KernelConfig config{};
    config.image_file_name = image_file_name.c_str();
    config.trace_calls = options.trace_kernel;
    config.current_thread = current_kthread;
    config.current_irql = loader::current_irql_location;
    config.attach_kernel_thread = attach_kernel_thread;
    config.call_guest = call_guest;
    config.memory.physical_bytes = options.memory_mb * 1024 * 1024;
    config.widescreen = options.widescreen;
    config.language = options.language;
    config.cdrom_root = assets;
    config.data_root = options.data.value_or(platform::user_data_dir());
    if (options.launch_data_fd) config.launch_data_page = loader::os::read_launch_data(*options.launch_data_fd);
    config.reboot = reboot_title;
    return config;
}

// Without game files the player gets the launcher, which says where they go. Not when
// the folder was named on the command line or in COD_BRO_ASSETS (development).
bool start_launcher_for_assets(int argc, char** argv) {
    if (const char* env = std::getenv("COD_BRO_ASSETS"); env && *env) return false;
    for (int i = 1; i < argc; ++i)
        if (option_name(argv[i]) == "--assets") return false;
    const fs::path launcher = platform::sibling_executable("cod_bro_launcher");
    if (!fs::exists(launcher)) return false;
    LOG_INFO("loader", "no game files: starting %s", launcher.string().c_str());
    return platform::start_program(launcher);
}

}  // namespace

int loader_main(int argc, char** argv) {
    // settings.ini first, so the command line overrides it. A reboot re-reads it.
    // Not with --compare: the comparison runs the game as on the Xbox, whatever the
    // player chose in the launcher.
    bool comparing = false;
    for (int i = 1; i < argc; ++i)
        comparing |= option_name(argv[i]) == "--compare" || option_name(argv[i]) == "--compare-all";
    const platform::Settings settings = comparing ? platform::Settings{} : platform::load_settings();
    std::vector<std::string> merged = settings.as_args();
    merged.insert(merged.begin(), argv[0]);
    merged.insert(merged.end(), argv + 1, argv + argc);
    std::vector<char*> merged_argv;
    for (std::string& arg : merged) merged_argv.push_back(arg.data());
    std::optional<Options> parsed = parse_args(static_cast<int>(merged_argv.size()), merged_argv.data());
    if (!parsed) {
        if (!settings.empty())
            std::fprintf(stderr, "(options include %s)\n", platform::settings_path().string().c_str());
        return 2;
    }
    if (!parsed->pc_patches.value_or(!parsed->compare)) {
        // PC features off: the presentation is the Xbox's too (4:3 without the EEPROM
        // widescreen flag, the game's own fonts and button icons, 640x480, its texture filtering), unless
        // an option asks for one of them explicitly.
        const auto unless_given = [&](const char* name, auto& field, auto value) {
            if (!parsed->given.count(name)) field = value;
        };
        unless_given("--aspect", parsed->widescreen, false);
        unless_given("--sharp-text", parsed->sharp_text, false);
        unless_given("--button-prompts", parsed->button_prompts, loader::TitlePatchConfig::ButtonPrompts::Controller);
        unless_given("--render-scale", parsed->render_scale, 1.0f);
        unless_given("--anisotropy", parsed->anisotropy, uint32_t{1});
        unless_given("--brightness", parsed->brightness, 1.0f);
        unless_given("--audio", parsed->audio_enhanced, false);
    }
    const std::optional<Options>& options = parsed;
    g_args.assign(argv, argv + argc);
    g_diag_dir = options->diag_dir;
    if (options->session_start_ns)
        platform::set_session_start(std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(*options->session_start_ns))));
    platform::log_set_level(options->log_level);
    g_tell_player = options->press_script.empty() && !options->screenshot_dir;
    if (options->diag_dir) {
        fs::create_directories(*options->diag_dir);
        g_log_path = *options->diag_dir / "log.txt";
        if (!platform::log_set_file(g_log_path.string().c_str())) {
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
            return 1;
        }
        LOG_INFO("diag", "diagnostics in %s: a screenshot every %u s, F12 marks a problem",
                 options->diag_dir->string().c_str(), options->diag_seconds);
        // summary.txt when the process ends (not on a reboot: the session goes on).
        std::atexit([] { loader::write_diag_summary(*g_diag_dir); });
    } else {
        // Always a log, so that a player without a console can send it: one run per
        // file (a reboot continues it), in the user data folder.
        std::error_code ec;
        fs::create_directories(platform::user_data_dir(), ec);
        g_log_path = platform::user_data_dir() / "log.txt";
        if (!options->launch_data_fd) fs::remove(g_log_path, ec);
        constexpr long kPlayerLogLimit = 8l << 20;
        if (!platform::log_set_file(g_log_path.string().c_str(), kPlayerLogLimit))
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
    }
    loader::set_fatal_exit_hook([](bool error) {
        loader::compare::flush();
        if (g_diag_dir) loader::write_diag_summary(*g_diag_dir);
        if (error) tell_player("The game stopped after an error.");
    });
    // An uncaught C++ exception (e.g. std::thread failing to start one) would end the
    // process without a word.
    std::set_terminate([] {
        const char* what = "unknown";
        if (const std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& e) {
                what = e.what();
            } catch (...) {
            }
        }
        LOG_ERROR("fatal", "uncaught C++ exception: %s", what);
        loader::run_fatal_exit_hook(true);
        std::_Exit(3);
    });

    const auto assets = platform::resolve_asset_root(options->assets);
    if (!assets) {
        if (!start_launcher_for_assets(argc, argv))
            return startup_failed("no game files (copy your game dump into the assets folder)");
        return 1;
    }
    LOG_INFO("loader", "assets: %s", assets->string().c_str());

    static loader::XbeImage image;
    const fs::path xbe = options->xbe.value_or(*assets / (options->multiplayer ? "codmp_xboxf.xbe" : "default.xbe"));
    const loader::TitleInfo* title = loader::find_title(xbe.filename().string());
    if (!title) return startup_failed(xbe.filename().string() + " is not one of the game's executables");
    // Wider than 16:9 keeps the HUD at 16:9 through the campaign's HUD manager, which
    // multiplayer does not have (patches.cpp, "Wider than 16:9").
    float aspect = options->aspect;
    if (title->id == loader::TitleId::Multiplayer && aspect > 16.0f / 9.0f + 0.001f) {
        LOG_WARN("loader", "multiplayer: pictures wider than 16:9 are campaign only for now; 16:9");
        aspect = 16.0f / 9.0f;
    }
    if (!image.load(xbe)) return startup_failed("cannot load " + xbe.string());
    // A release has the data files next to the loader; builds use the source tree's.
    auto data_file = [](const char* name) {
        const fs::path next_to_loader = platform::executable_dir() / name;
        return fs::exists(next_to_loader) ? next_to_loader : fs::path(COD_BRO_SYMBOLS_DIR) / name;
    };
    const bool other_build = image.code_fingerprint() != title->code_fingerprint;
    bool release_map_ready = false;
    if (other_build) {
        LOG_WARN("loader", "%s is another build (time stamp %08x, code fingerprint %016llx) than the supported one "
                 "(%016llx); its sections:", xbe.filename().string().c_str(), image.header().time_date,
                 static_cast<unsigned long long>(image.code_fingerprint()),
                 static_cast<unsigned long long>(title->code_fingerprint));
        image.log_sections();
        // The port's addresses are the supported build's; find where they are in this one.
        loader::ReleaseSignatures signatures;
        const fs::path signature_path = data_file(title->signature_file);
        if (signatures.load(signature_path)) {
            loader::ReleaseMap map;
            map.build(signatures, image_bytes(image));
            loader::set_release_map(map);
            release_map_ready = true;
        } else {
            LOG_ERROR("loader", "cannot read %s; the port's changes cannot be placed in this build",
                      signature_path.string().c_str());
        }
        // Without the map, another release's campaign ran on 0.1.8 (issue #9: the
        // French/Spanish/Italian disc), but its multiplayer crashed.
        if (!release_map_ready && title->id == loader::TitleId::Multiplayer && !options->any_release)
            return startup_failed(std::string(title->signature_file) + " is missing or damaged");
    }
    g_xbe = xbe;

    static loader::SymbolTable symbols;
    const fs::path symbol_path = options->symbols ? *options->symbols : data_file(title->symbol_file);
    if (symbols.load(symbol_path))
        LOG_INFO("loader", "%zu symbols from %s", symbols.size(), symbol_path.string().c_str());
    else
        LOG_WARN("loader", "no symbols at %s; backtraces will show addresses only", symbol_path.string().c_str());
    if (options->write_signatures) {
        if (other_build) return startup_failed("--write-signatures needs the supported build");
        const loader::ReleaseSignatures signatures =
            loader::make_signatures(image_bytes(image), symbols.in_range(0, 0xFFFFFFFF));
        if (!signatures.save(*options->write_signatures))
            return startup_failed("cannot write " + options->write_signatures->string());
        LOG_INFO("loader", "%zu function signatures and %zu data references written to %s",
                 signatures.functions.size(), signatures.data.size(), options->write_signatures->string().c_str());
        return 0;
    }
    if (release_map_ready) {
        loader::release_map().log_summary(&symbols);
        // Names and the SDK replacements follow the functions to where they are here.
        const size_t dropped = symbols.relocate([](uint32_t address) { return loader::release_map().find(address); });
        if (dropped) LOG_INFO("release", "%zu symbols are in changed code and were left out", dropped);
    }

    loader::install_crash_handler(image, symbols);
    loader::start_profiler(options->profile ? options->profile->c_str() : std::getenv("XB_PROFILE"),
                         static_cast<uint32_t>(options->frame_rate), image, symbols);
    loader::os::release_reserved_ranges();
    log_disc_languages(*assets, options->language);
    if (!xbox::kernel_init(kernel_config(*options, *assets, xbe))) return startup_failed("cannot set up the Xbox memory");
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::install_kernel_thunks(image);
    if (!options->bindings_error.empty())
        LOG_WARN("input", "keys setting: %s (ignored; the rest applies)", options->bindings_error.c_str());
    if (const std::string keys = input::format_bindings(options->bindings); !keys.empty())
        LOG_INFO("input", "keys: %s", keys.c_str());
    input::set_bindings(options->bindings);
    loader::install_title_patches(*title, {options->mouse_sensitivity, options->start_level, options->aim_assist,
                                           options->frame_rate, options->late_frames_wait,
                                           options->widescreen ? aspect : 0.0f, options->field_of_view,
                                           options->button_prompts, options->pc_patches.value_or(!options->compare),
                                           options->exec_text, options->forced_calls});
    if (!loader::prepare_fs_access(image)) return startup_failed("cannot prepare the game's code");
    online::configure_network({options->net_port});
    audio::OutputConfig audio_config;
    audio_config.device = options->silent ? audio::OutputConfig::Device::Loopback : audio::OutputConfig::Device::Default;
    audio_config.device_name = options->audio_device;
    audio_config.speakers = options->speakers;
    audio_config.enhanced = options->audio_enhanced;
    audio::configure(audio_config);
    game::set_sound_device(&audio::sound_device());  // the decompiled sound code's device
    render::D3DConfig d3d_config;
    d3d_config.backend.window_scale = options->scale;
    d3d_config.backend.anisotropy = options->anisotropy;
    d3d_config.backend.d3d9 = options->d3d9;
    d3d_config.backend.brightness = options->brightness;
    const bool sharp_text =
        options->sharp_text && text::prepare_sharp_text(*assets, platform::executable_dir() / "fonts") > 0;
    const bool key_prompts = options->button_prompts != loader::TitlePatchConfig::ButtonPrompts::Controller &&
                             text::prepare_key_prompts(*assets, platform::executable_dir() / "fonts") > 0;
    if (sharp_text || key_prompts)
        d3d_config.backend.replace_texture = [sharp_text, key_prompts](uint64_t content, float render_scale) {
            std::shared_ptr<const render::TextureReplacement> replacement;
            if (key_prompts) replacement = text::key_prompt_replacement(content, render_scale);
            if (!replacement && sharp_text) replacement = text::sharp_text_replacement(content, render_scale);
            return replacement;
        };
    if (key_prompts) d3d_config.backend.replacement_version = &text::key_prompt_version;
    if (options->widescreen) {
        d3d_config.backend.display_aspect = aspect;
        d3d_config.backend.pillarbox_flat_frames = true;  // the menus are 4:3 layouts
        d3d_config.backend.narrow_ui_draw = &loader::in_game_menu_draw;
    }
    d3d_config.backend.pc_menu = &loader::pc_menu;
    d3d_config.backend.pc_menu_shown = &loader::pc_menu_shown;
    d3d_config.backend.menu_font = (platform::executable_dir() / "fonts" / "Jost-500-Medium.ttf").string();
    d3d_config.backend.fullscreen = options->fullscreen;
    d3d_config.backend.render_scale = options->render_scale;  // 0: the back end picks
    d3d_config.screenshot_interval = options->screenshot_interval;
    if (options->diag_dir) {
        d3d_config.diag_dir = options->diag_dir->string();
        d3d_config.diag_seconds = options->diag_seconds;
        d3d_config.pause_audio = &audio::set_paused;
    }
    if (options->screenshot_dir) {
        fs::create_directories(*options->screenshot_dir);
        d3d_config.screenshot_dir = options->screenshot_dir->string();
    }
    d3d_config.device_pointer = loader::title_address(title->d3d_device_pointer, "(D3D device pointer)");
    d3d_config.render_state = loader::title_address(title->d3d_render_state, "(D3D render states)");
    d3d_config.texture_state = loader::title_address(title->d3d_texture_state, "(D3D texture states)");
    d3d_config.device_failed = [] {
        loader::run_fatal_exit_hook(false);
        tell_player(
            "The game could not create a graphics device.\n\nUpdate the graphics driver. If that does not help, "
            "choose another Graphics setting in the launcher.");
        std::_Exit(1);
    };
    render::configure(d3d_config);
    input::configure({loader::title_address(title->gamepad_type, "(gamepad device type)")});
    if (!options->press_script.empty() && !input::set_script(options->press_script)) return 1;
    if (!loader::install_sdk_replacements(image, symbols, *title)) {
        LOG_ERROR("loader", "SDK replacements need the symbol file (%s)", symbol_path.string().c_str());
        return startup_failed(std::string(title->symbol_file) + " is missing or damaged");
    }
    // After the title patches and SDK replacements: they must not hook the same code.
    loader::compare::Config compare;
    compare.enabled = options->compare;
    compare.multiplayer = title->id == loader::TitleId::Multiplayer;
    compare.image_begin = image.base();
    compare.image_end = image.base() + image.size();
    compare.stack_limit = [] {
        const loader::ThreadEnv* env = loader::current_thread_env();
        return env ? env->stack_limit : 0u;
    };
    compare.describe = loader::describe_address;
    if (options->compare_all) compare.enough_matches = 0;
    if (options->compare) compare.sdk_calls = loader::recorded_sdk_calls();
    if (options->compare) compare.host_code.push_back(loader::rdtsc_trampolines());
    if (options->compare) compare.host_code.push_back(loader::scene_force_thunk());
    if (options->compare)
        compare.results = fs::path(COD_BRO_SYMBOLS_DIR) / (compare.multiplayer ? "functions_mp.csv" : "functions.csv");
    // Test processes run the DirectSound replacements without output (audio/test_mode.h).
    if (options->compare)
        audio::set_test_mode(loader::compare::in_test_flag(), [](audio::TestLock which) {
            loader::compare::host_lock_held(static_cast<uint32_t>(which));
        });
    compare.before_fork = audio::hold_for_fork;
    compare.after_fork = audio::release_after_fork;
    loader::compare::install(game::functions(), compare);
    std::atexit([] { loader::compare::flush(); });
    if (const size_t missed = loader::missed_title_addresses()) {
        LOG_WARN("loader", "%zu of the port's changes were not found in this release (see above)", missed);
        // The campaign started anyway before there was a map (0.1.8 on the issue #9 disc).
        if (title->id == loader::TitleId::Multiplayer && !options->any_release)
            return startup_failed(xbe.filename().string() +
                                  " is from another release of the game, and some of the places the port changes "
                                  "were not found in it (the log lists them; please attach the log and mention "
                                  "your disc's region and languages in a report)");
    } else if (release_map_ready) {
        LOG_INFO("loader", "all of the port's changes were found in this release");
    }

    // The title's initial thread. Like the Xbox's, it has no TLS block (TlsData is
    // null); XAPI only creates TLS for the threads it starts itself.
    loader::ThreadEnv* env = loader::create_thread_env(image.header().pe_stack_commit, 0);
    if (!env || !loader::activate_thread_env(env)) return startup_failed("cannot prepare the game's main thread");

    LOG_INFO("loader", "starting title at %08x", image.entry_point());
    // Four zero dwords stand in for arguments in case the entry point reads any.
    const uint32_t args[4] = {};
    loader::run_on_xbox_stack(image.entry_point(), *env, args);
    // The entry point only starts the title's main thread and returns. Park this
    // thread; the process lives on until the title quits. It must not exit: once the
    // initial thread is gone, Linux no longer resolves /proc/self/exe, which DXVK
    // and SDL read (and on Windows the stub's code it would return to is gone).
    LOG_INFO("loader", "title entry point returned");
    for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
}
