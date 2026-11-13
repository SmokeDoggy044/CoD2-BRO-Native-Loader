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
#include "kernel_thunks.h"
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

namespace loader {

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
        "                   (default enhanced; original with --pc-patches off): reverb by the\n"
        "                   place around the listener, best resampling (PC menu: live switch)\n"
        "  --speakers auto|stereo|headphones|quad|5.1|7.1  speaker layout (default auto: the\n"
        "                   output's own, HRTF when the game's settings ask for headphones);\n"
        "                   headphones = 3D sound for headphones (HRTF) always\n"
        "  --audio-device NAME  output device (default: the system's; the launcher lists them)\n"
        "  --reverb \"K=V ...\"  enhanced reverb adjustments: amount=DB (-12..12), decay=X\n"
        "                   (0.25..4), brightness=X (0.5..2), reflections=DB (-12..12),\n"
        "                   clutter=A (0..0.3, default 0.07); also in the PC menu\n"
        "  --volume \"K=V ...\"  volume per kind of sound in percent (0..100, default 100):\n"
        "                   effects=P, ambience=P (the levels' background), dialogue=P,\n"
        "                   music=P; on top of the game's own slider,\n"
        "                   original and enhanced sound alike; enhanced only: close_gunfire=DB,\n"
        "                   own_gunfire=DB, close_bullets=DB (0..12, default 0: louder nearby);\n"
        "                   also in the PC menu\n"
        "  --enhanced \"K=0|1 ...\"  enhanced sound's improvements one by one (default all on):\n"
        "                   resampler, voices (more than the Xbox's), reverb, occlusion,\n"
        "                   close_boost (the close_ volumes); also in the PC menu\n"
        "  --graphics original|enhanced  the picture as on the Xbox (at the PC's resolution), or with\n"
        "                   our improvements (default enhanced; original with --pc-patches off;\n"
        "                   PC menu: live switch)\n"
        "  --graphics-features \"K=V ...\"  enhanced graphics' improvements one by one (default\n"
        "                   all on): fxaa, anisotropic (its level: --anisotropy), sharpen with\n"
        "                   sharpness=0..1, vibrance with vibrance_amount=-1..1, texture_detail\n"
        "                   with lod_bias=-2..0 (amounts default 0: no change); also in the PC menu\n"
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
        "                   look-alike fonts (src/text/fonts/ next to the loader; default on)\n"
        "  --button-prompts auto|keyboard|controller  the game's button pictures as keys and\n"
        "                   mouse buttons while the keyboard/mouse is used (auto, default),\n"
        "                   always, or the original controller buttons\n"
        "  --d3d9 WHICH     Windows: auto (default: DXVK on Vulkan, else the system's), dxvk or\n"
        "                   system (Linux always uses DXVK)\n"
        "  --screenshots N DIR  save every Nth frame as DIR/frame_<n>.bmp (debugging)\n"
        "  --press LIST     scripted presses on port 0, e.g. start@20,a@25.5,back@80+4 (seconds after\n"
        "                   start, +S holds the button S seconds;\n"
        "                   buttons: a b x y black white lt rt start back up down left right ls rs)\n"
        "  --record FILE    developer: write every clock and input read of the game's main thread\n"
        "                   and a game-state hash per frame to FILE (until the next reboot)\n"
        "  --replay FILE    developer: play FILE back and report the first frame whose state differs;\n"
        "                   exits at its end (0 = every frame matched, 3 = some differed)\n"
        "  --replay-at-switch  with --record/--replay and --own-main: start at the first level switch (the\n"
        "                   title restarting in this process), not at the start\n"
        "  --replay-hashes FILE  with --replay: this run's own state hashes, for tools/replay_compare.py\n"
        "  --replay-reference FILE DIR  with --replay: compare with another run's hashes (a recording or\n"
        "                   --replay-hashes file); the first differing frame's state goes to DIR\n"
        "  --replay-continue  with --replay: keep playing live after the recording ends\n"
        "  --replay-speed N  with --replay: play N times faster (vertical blank and sound; no sound output)\n"
        "  --replay-no-draw  with --replay: no draws or clears on the GPU (game logic only, faster checks)\n"
        "  --replay-dump N[-M[:S]] DIR  with --record/--replay: game state of frame N (or every [S-th] frame N..M)\n"
        "                   to DIR (tools/replay_diff.py)\n"
        "  --own-main       developer: start the game at our Sys_Main C (own engine host, step 0.3)\n"
        "  --host           developer: --own-main with the original engine code (INT3) and the kernel exports gone\n"
        "  --text-trap LIST OUT  developer: log every original function from LIST (tools/text_trap_list.py)\n"
        "                   the first time it runs, with its caller, to OUT (Linux)\n"
        "  --text-clear OUT developer: original non-level code bytes become INT3 except our hooks; code that\n"
        "                   still runs is logged to OUT with its caller and put back by function (Linux)\n"
        "  --check-crt          our CRT (src/engine/shared/title_crt.h) against the title's on a table, then exit\n"
        "  --replay-shots N,N.. DIR  with --record/--replay: pictures of those frames to DIR (shot_<n>.bmp)\n"
        "  --dump-shaders DIR  every shader program the renderer sets up, for tools/shader_hlsl.py\n"
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
        "                   Controls tab writes them; src/input/bindings.h)\n"
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
        "                   filtering, brightness 1, the original sound and original graphics\n",
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
        } else if (arg == "--record" || arg == "--replay") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a file\n", arg.c_str());
                return std::nullopt;
            }
            options.replay.mode = arg == "--record" ? xbox::replay::Mode::Record : xbox::replay::Mode::Replay;
            options.replay.file = fs::path(argv[++i]);
        } else if (arg == "--replay-at-switch") {
            options.replay_at_switch = true;
        } else if (arg == "--replay-hashes") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--replay-hashes needs a file\n");
                return std::nullopt;
            }
            options.replay.hashes_out = fs::path(argv[++i]);
        } else if (arg == "--replay-reference") {
            if (i + 2 >= argc) {
                std::fprintf(stderr, "--replay-reference needs a file and a folder\n");
                return std::nullopt;
            }
            options.replay.reference = fs::path(argv[++i]);
            options.replay.reference_dump_dir = fs::path(argv[++i]);
        } else if (arg == "--replay-speed") {
            if (i + 1 >= argc || (options.replay_speed = std::atof(argv[++i])) <= 0 || options.replay_speed > 64) {
                std::fprintf(stderr, "--replay-speed needs a factor above 0 and up to 64\n");
                return std::nullopt;
            }
        } else if (arg == "--replay-no-draw") {
            options.replay_no_draw = true;
        } else if (arg == "--replay-continue") {
            options.replay.exit_at_end = false;
        } else if (arg == "--replay-shots") {
            if (i + 2 >= argc) {
                std::fprintf(stderr, "--replay-shots needs frame numbers (1,2,...) and a folder\n");
                return std::nullopt;
            }
            std::string list = argv[++i];
            for (size_t p = 0; p <= list.size();) {
                const size_t comma = std::min(list.find(',', p), list.size());
                const long n = std::atol(list.substr(p, comma - p).c_str());
                if (n < 1) {
                    std::fprintf(stderr, "--replay-shots: frame numbers start at 1 (got \"%s\")\n", list.c_str());
                    return std::nullopt;
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
                return std::nullopt;
            }
            options.replay.dump_dir = fs::path(argv[++i]);
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
        } else if (arg == "--dump-shaders") {
            if (!(options.shader_dump_dir = value())) return std::nullopt;
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
        } else if (arg == "--reverb") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--reverb needs adjustments, e.g. \"amount=-3 decay=1.2\"\n");
                return std::nullopt;
            }
            options.reverb = audio::ReverbTuning::parse(argv[++i]);
        } else if (arg == "--volume") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--volume needs volumes, e.g. \"music=50 effects=80\"\n");
                return std::nullopt;
            }
            options.volumes = audio::CategoryVolumes::parse(argv[++i]);
        } else if (arg == "--enhanced") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--enhanced needs features, e.g. \"reverb=0 occlusion=1\"\n");
                return std::nullopt;
            }
            options.features = audio::EnhancedFeatures::parse(argv[++i]);
        } else if (arg == "--graphics") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "original" && value != "enhanced") {
                std::fprintf(stderr, "--graphics needs original or enhanced\n");
                return std::nullopt;
            }
            options.graphics_enhanced = value == "enhanced";
        } else if (arg == "--graphics-features") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--graphics-features needs features, e.g. \"fxaa=0 sharpness=0.5\"\n");
                return std::nullopt;
            }
            options.graphics_features = render::GraphicsFeatures::parse(argv[++i]);
        } else if (arg == "--audio-device") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--audio-device needs a device name\n");
                return std::nullopt;
            }
            options.audio_device = argv[++i];
        } else if (arg == "--check-crt") {
            options.check_crt = true;
        } else if (arg == "--own-main") {
            options.own_main = true;
        } else if (arg == "--host") {
            options.own_main = options.host = true;
        } else if (arg == "--text-trap") {
            if (i + 2 >= argc) {
                std::fprintf(stderr, "--text-trap needs a list and an output file\n");
                return std::nullopt;
            }
            options.text_trap_list = argv[++i];
            options.text_trap_out = argv[++i];
        } else if (arg == "--text-clear") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--text-clear needs an output file\n");
                return std::nullopt;
            }
            options.text_clear_out = argv[++i];
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

}  // namespace loader
