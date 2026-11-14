#include "settings_schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "settings.h"

namespace platform {
namespace {

using T = SettingType;
using G = SettingGroup;

bool check_aspect(std::string& value, std::string* error) {
    float w = 0, h = 0;
    char end = 0;
    if (value == "4:3" || value == "16:9") return true;
    if (std::sscanf(value.c_str(), "%f:%f%c", &w, &h, &end) == 2 && h > 0 && w / h >= 16.0f / 9.0f - 0.001f &&
        w / h <= 4.0f)
        return true;
    if (error) *error = "4:3, 16:9 or wider W:H up to 4:1 (e.g. 21:9)";
    return false;
}

bool check_fps(std::string& value, std::string* error) {
    if (value == "display") return true;
    char* end = nullptr;
    const long fps = std::strtol(value.c_str(), &end, 10);
    if (end && *end == 0 && !value.empty() && fps >= 30 && fps <= 1000) return true;
    if (error) *error = "display, 30 (as on the Xbox) or a limit of 31-1000";
    return false;
}

constexpr uint8_t kLiveIni = kLive;
constexpr uint8_t kCli = kCommandLine;
constexpr uint8_t kCliHidden = kCommandLine | kHidden;

// In --help order, by group.
const SettingDef kSettings[] = {
    // --- Game files
    {"assets", "DIR", T::Path, G::Files, 0, 0, 0, nullptr, "",
     "game files (default: COD_BRO_ASSETS, ./assets, or the dev folder)"},
    {"xbe", "FILE", T::Path, G::Files, kCli, 0, 0, nullptr, "", "executable to load (default: <assets>/default.xbe)"},
    {"multiplayer", "", T::Flag, G::Files, kCli, 0, 0, nullptr, "false",
     "start multiplayer (<assets>/codmp_xboxf.xbe)"},
    {"data", "DIR", T::Path, G::Files, 0, 0, 0, nullptr, "",
     "where saves and the cache drive go (default: the user data folder)"},
    {"symbols", "FILE", T::Path, G::Files, kCli, 0, 0, nullptr, "",
     "Ghidra symbol export: SDK replacements and readable backtraces (default: <xbe>_symbols.txt next to the "
     "loader, or tools/data)"},
    {"language", "L", T::Choice, G::Files, 0, 0, 0, "english|german|french|spanish|italian", "english",
     "the console language; the game uses it where its disc has that language"},
    {"memory", "MB", T::Int, G::Files, 0, 16, 1024, nullptr, "64",
     "Xbox memory budget (default 64, like a retail Xbox; devkit: 128)"},

    // --- Display
    {"fullscreen", "", T::Flag, G::Display, 0, 0, 0, nullptr, "false",
     "borderless fullscreen (Alt+Enter switches while playing)"},
    {"windowed", "", T::Flag, G::Display, kCli, 0, 0, nullptr, "false",
     "in a window (default; overrides fullscreen in settings.ini)"},
    {"scale", "N", T::Int, G::Display, 0, 1, 8, nullptr, "2",
     "window size as a multiple of the game's resolution (default 2)"},
    {"render-scale", "X", T::Float, G::Display, 0, 0.5, 8, nullptr, "",
     "render resolution as a multiple of the game's 640x480 (default: the window's or screen's height; above it = "
     "supersampling, e.g. 4)"},
    {"aspect", "A", T::Text, G::Display, 0, 0, 0, nullptr, "16:9",
     "16:9 (default: the game's widescreen mode, wider view and HUD) or 4:3; wider, e.g. 21:9, 32:9 or 3440:1440, "
     "is experimental (wider view, the HUD stays 16:9 in the middle)",
     nullptr, nullptr, check_aspect},
    {"fov", "DEG", T::Float, G::Display, kLiveIni, 50, 120, nullptr, "70",
     "field of view in degrees across a 4:3 picture (50-120, default 70 as the game; campaign); also in the PC "
     "menu (` key while playing)"},
    {"brightness", "X", T::Float, G::Display, 0, 0.5, 2, nullptr, "1",
     "0.5-2.0 (default 1: as on the Xbox; higher = brighter midtones)"},
    {"fps", "display|30|N", T::Text, G::Display, 0, 0, 0, nullptr, "display",
     "frame rate: the display's (default; the display paces each frame), 30 as on the Xbox, or at most N "
     "(31-1000; e.g. 60, or 144 on a 144 Hz display without waiting for it); movies always play at 30",
     nullptr, nullptr, check_fps},
    {"late-frames", "show|wait", T::Choice, G::Display, 0, 0, 0, "show|wait", "show",
     "a frame that missed its vertical blank is shown at once (default) or waits for the next one (as on the "
     "Xbox: 30 fps drops to 20)"},
    {"mods", "on|off", T::Switch, G::Graphics, kCli, 0, 0, nullptr, "on",
     "texture packs and other mods from the mods folder of the data folder (default on; never with --compare)"},
    {"mods-dir", "DIR", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "mods from DIR instead of <data folder>/mods"},
    {"sharp-text", "on|off", T::Switch, G::Display, 0, 0, 0, nullptr, "on",
     "the game's text drawn again at the render resolution from free look-alike fonts (src/text/fonts/ next to "
     "the loader; default on)"},
    {"button-prompts", "auto|keyboard|controller", T::Choice, G::Controls, 0, 0, 0, "auto|keyboard|controller",
     "auto",
     "the game's button pictures as keys and mouse buttons while the keyboard/mouse is used (auto, default), "
     "always, or the original controller buttons"},

    // --- Graphics
    {"renderer", "vulkan|d3d9", T::Choice, G::Graphics, 0, 0, 0, "vulkan|d3d9", "vulkan",
     "the graphics API (default vulkan; falls back to d3d9 without a Vulkan 1.3 device)"},
    {"d3d9", "WHICH", T::Choice, G::Graphics, 0, 0, 0, "auto|dxvk|system", "auto",
     "Windows: auto (default: DXVK on Vulkan, else the system's), dxvk or system (Linux always uses DXVK)"},
    {"graphics", "original|enhanced", T::Choice, G::Graphics, kLiveIni, 0, 0, "original|enhanced", "enhanced",
     "the picture as on the Xbox (at the PC's resolution), or with our improvements (default enhanced; original "
     "with --pc-patches off; PC menu: live switch). Enhanced on Vulkan always draws the 3D picture in 16-bit "
     "colour (HDR), the base for later lighting effects"},
    {"anisotropy", "N", T::Int, G::Graphics, 0, 1, 16, nullptr, "16",
     "anisotropic filtering for mipmapped textures, 1-16 (default 16; 1 = what the game asks for)"},
    {"fxaa", "on|off", T::Switch, G::Graphics, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: anti-aliasing of the 3D picture (FXAA)", "graphics-features", "fxaa"},
    {"anisotropic", "on|off", T::Switch, G::Graphics, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: anisotropic texture filtering (its level: --anisotropy)", "graphics-features", "anisotropic"},
    {"sharpen", "on|off", T::Switch, G::Graphics, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: contrast-adaptive sharpening, by --sharpness", "graphics-features", "sharpen"},
    {"sharpness", "X", T::Float, G::Graphics, kLiveIni, 0, 1, nullptr, "0", "enhanced: sharpening amount, 0..1",
     "graphics-features", "sharpness"},
    {"vibrance", "on|off", T::Switch, G::Graphics, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: stronger colours where they are weak, by --vibrance-amount", "graphics-features", "vibrance"},
    {"vibrance-amount", "X", T::Float, G::Graphics, kLiveIni, -1, 1, nullptr, "0",
     "enhanced: -1 (greyer) .. 1 (0: the game's colours)", "graphics-features", "vibrance_amount"},
    {"texture-detail", "on|off", T::Switch, G::Graphics, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: sharper textures in the distance, by --lod-bias", "graphics-features", "texture_detail"},
    {"lod-bias", "X", T::Float, G::Graphics, kLiveIni, -2, 0, nullptr, "0",
     "enhanced: mipmap bias, -2 (sharpest) .. 0 (the game's)", "graphics-features", "lod_bias"},
    {"glow", "on|off", T::Switch, G::Graphics, 0, 0, 0, nullptr, "off",
     "the glow some levels turn on (mission 4's desert): a blurred copy of the picture added over it (default off, "
     "as all releases showed it; on = drawn)"},
    {"vk-validation", "", T::Flag, G::Graphics, kCli, 0, 0, nullptr, "false",
     "Vulkan: the Khronos validation layer's messages in the log (development)"},

    // --- Sound
    {"silent", "", T::Flag, G::Sound, 0, 0, 0, nullptr, "false",
     "no sound output (audio still runs, into a silent device)"},
    {"audio", "original|enhanced", T::Choice, G::Sound, kLiveIni, 0, 0, "original|enhanced", "enhanced",
     "the sound as on the Xbox, or with our improvements (default enhanced; original with --pc-patches off): "
     "reverb by the place around the listener, best resampling (PC menu: live switch)"},
    {"speakers", "LAYOUT", T::Choice, G::Sound, 0, 0, 0, "auto|stereo|headphones|quad|5.1|7.1", "auto",
     "speaker layout (default auto: the output's own, HRTF when the game's settings ask for headphones); "
     "headphones = 3D sound for headphones (HRTF) always"},
    {"audio-device", "NAME", T::Text, G::Sound, 0, 0, 0, nullptr, "",
     "output device (default: the system's; the launcher lists them)"},
    {"volume-effects", "P", T::Float, G::Sound, kLiveIni, 0, 100, nullptr, "100",
     "volume of gunfire, explosions, footsteps and the menus' sounds in percent, on top of the game's own slider",
     "volume", "effects"},
    {"volume-ambience", "P", T::Float, G::Sound, kLiveIni, 0, 100, nullptr, "100",
     "volume of the levels' background (distant battle, wind, rain, rooms) in percent", "volume", "ambience"},
    {"volume-dialogue", "P", T::Float, G::Sound, kLiveIni, 0, 100, nullptr, "100",
     "volume of orders, chatter and briefings in percent", "volume", "dialogue"},
    {"volume-music", "P", T::Float, G::Sound, kLiveIni, 0, 100, nullptr, "100", "volume of the music in percent",
     "volume", "music"},
    {"close-gunfire", "DB", T::Float, G::Sound, kLiveIni, 0, 12, nullptr, "0",
     "enhanced: gunfire nearby this many dB louder (0..12)", "volume", "close_gunfire"},
    {"own-gunfire", "DB", T::Float, G::Sound, kLiveIni, 0, 12, nullptr, "0",
     "enhanced: the player's own gunfire this many dB louder (0..12)", "volume", "own_gunfire"},
    {"close-bullets", "DB", T::Float, G::Sound, kLiveIni, 0, 12, nullptr, "0",
     "enhanced: bullets passing close this many dB louder (0..12)", "volume", "close_bullets"},
    {"resampler", "on|off", T::Switch, G::Sound, kLiveIni, 0, 0, nullptr, "on", "enhanced: best resampling",
     "enhanced", "resampler"},
    {"more-voices", "on|off", T::Switch, G::Sound, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: more sounds at once than the Xbox's", "enhanced", "voices"},
    {"room-reverb", "on|off", T::Switch, G::Sound, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: reverb by the place around the listener", "enhanced", "reverb"},
    {"occlusion", "on|off", T::Switch, G::Sound, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: sounds behind walls muffled", "enhanced", "occlusion"},
    {"close-boost", "on|off", T::Switch, G::Sound, kLiveIni, 0, 0, nullptr, "on",
     "enhanced: the close-* volumes", "enhanced", "close_boost"},
    {"reverb-amount", "DB", T::Float, G::Sound, kLiveIni, -12, 12, nullptr, "0",
     "enhanced reverb: level in dB (-12..12)", "reverb", "amount"},
    {"reverb-decay", "X", T::Float, G::Sound, kLiveIni, 0.25, 4, nullptr, "1",
     "enhanced reverb: times the decay time (0.25..4)", "reverb", "decay"},
    {"reverb-brightness", "X", T::Float, G::Sound, kLiveIni, 0.5, 2, nullptr, "1",
     "enhanced reverb: times the high-frequency decay (0.5..2)", "reverb", "brightness"},
    {"reverb-reflections", "DB", T::Float, G::Sound, kLiveIni, -12, 12, nullptr, "0",
     "enhanced reverb: early reflections in dB (-12..12)", "reverb", "reflections"},
    {"reverb-clutter", "A", T::Float, G::Sound, kLiveIni, 0, 0.3, nullptr, "0.07",
     "enhanced reverb: absorption of what the probe's rays do not see (0..0.3)", "reverb", "clutter"},

    // --- Controls
    {"mouse-sensitivity", "DEG", T::Float, G::Controls, kLiveIni, 0.01, 10, nullptr, "0.11",
     "degrees the view turns per mouse count (default 0.11)"},
    {"keys", "LIST", T::Text, G::Controls, 0, 0, 0, nullptr, "",
     "keyboard and mouse bindings that differ from the defaults, e.g. \"fire=mouse1 jump=space crouch=left-ctrl "
     "crouch=c\" (src/input/bindings.h); settings.ini keeps them as one key-<action> line each"},
    {"key-move-forward", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "move-forward"},
    {"key-move-back", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "move-back"},
    {"key-move-left", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "move-left"},
    {"key-move-right", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "move-right"},
    {"key-fire", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "fire"},
    {"key-aim", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "aim"},
    {"key-use", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "use"},
    {"key-reload", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "reload"},
    {"key-jump", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "jump"},
    {"key-crouch", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "crouch"},
    {"key-sprint", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "sprint"},
    {"key-melee", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "melee"},
    {"key-grenade", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "grenade"},
    {"key-next-weapon", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "next-weapon"},
    {"key-lean-left", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "lean-left"},
    {"key-lean-right", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "lean-right"},
    {"key-aim-toggle", "KEYS", T::Text, G::Controls, kLive | kHidden, 0, 0, nullptr, "", "", "keys", "aim-toggle"},
    {"aim-assist", "on|off", T::Switch, G::Controls, 0, 0, 0, nullptr, "on",
     "the view following an enemy under the crosshair (default on)"},
    {"mounted-gun-fix", "on|off", T::Switch, G::Controls, 0, 0, 0, nullptr, "on",
     "stops the slow down-left drift of the view on mounted guns and vehicles without input (default on; off = as "
     "on the Xbox)"},

    // --- Network
    {"net-port", "N", T::Int, G::Network, 0, 1024, 65532, nullptr, "3074",
     "system link: UDP ports N to N+3 (default 3074; the same on every PC)"},
    {"check-updates", "", T::Flag, G::Network, kLauncher, 0, 0, nullptr, "true",
     "the launcher looks for a newer release"},

    // --- Testing
    {"level", "N", T::Int, G::Testing, kCli, 0, 13, nullptr, "",
     "start mission N (0-13, 0 = prologue) once the menus are up (testing)"},
    {"exec", "\"CMDS\"", T::Text, G::Testing, kCli, 0, 0, nullptr, "",
     "console commands/cvars, e.g. \"r_showtris 1; god\", run once the level is active (testing; first boot only)"},
    {"press", "LIST", T::Text, G::Testing, kCli, 0, 0, nullptr, "",
     "scripted presses on port 0, e.g. start@20,a@25.5,back@80+4 (seconds after start, +S holds the button S "
     "seconds; buttons: a b x y black white lt rt start back up down left right ls rs)"},
    {"diag", "DIR [S]", T::Custom, G::Testing, kCli, 0, 0, nullptr, "",
     "diagnostics while playing: DIR/log.txt, a screenshot every S seconds (default 10); F12 marks a problem: "
     "screenshot, a note typed while the game is paused (DIR/marks.txt), draw dump in the log; new warnings and "
     "errors are marked by themselves; F11 shows draws that need missing features in magenta; DIR/summary.txt on "
     "exit"},
    {"dev", "", T::Flag, G::Testing, kCli, 0, 0, nullptr, "false",
     "developer mode: an overlay (frame time, GPU memory, texture packs) and keys: F1 overlay on/off, F2 mods on/off, "
     "F3 enhanced graphics on/off, F4 next enhanced feature, F5 that feature on/off, F6 textures under the "
     "crosshair (Vulkan), F7 pause; with --diag, F12 marks note what was switched and picked"},
    {"screenshots", "N DIR", T::Custom, G::Testing, kCli, 0, 0, nullptr, "",
     "save every Nth frame as DIR/frame_<n>.bmp (debugging)"},
    {"profile", "auto[,S]", T::Text, G::Testing, kCli, 0, 0, nullptr, "",
     "each time the frame rate drops under 80% of its aim (24 at 30 fps; at most once a minute, 20 times), log for "
     "S seconds (default 10) where each thread spends its time (START,S: once, from START seconds on; XB_PROFILE "
     "does the same)"},
    {"debug", "", T::Flag, G::Testing, kCli, 0, 0, nullptr, "false", "debug logging"},
    {"trace", "", T::Flag, G::Testing, kCli, 0, 0, nullptr, "false", "log every kernel call (implies --debug)"},
    {"any-release", "", T::Flag, G::Testing, kCli, 0, 0, nullptr, "false",
     "start another release's multiplayer executable even where the port's changes were not all found in it "
     "(testing: expect crashes)"},
    {"pc-patches", "on|off", T::Switch, G::Testing, kCli, 0, 0, nullptr, "on",
     "the PC changes to the game's code: 60 fps pacing, mouse look, aim assist off, widescreen menus and objectives "
     "(default on); off keeps only what the game needs to run on a PC (and --level), and defaults to 4:3, the "
     "game's fonts, render scale 1, no anisotropic filtering, brightness 1, the original sound and original "
     "graphics"},

    // --- Developer
    {"record", "FILE", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "write every clock and input read of the game's main thread and a game-state hash per frame to FILE (until "
     "the next reboot)"},
    {"replay", "FILE", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "play FILE back and report the first frame whose state differs; exits at its end (0 = every frame matched, "
     "3 = some differed)"},
    {"replay-at-switch", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "with --record/--replay and --own-main: start at the first level switch (the title restarting in this "
     "process), not at the start"},
    {"replay-hashes", "FILE", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "with --replay: this run's own state hashes, for tools/replay_compare.py"},
    {"replay-reference", "FILE DIR", T::Custom, G::Developer, kCli, 0, 0, nullptr, "",
     "with --replay: compare with another run's hashes (a recording or --replay-hashes file); the first differing "
     "frame's state goes to DIR"},
    {"replay-continue", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "with --replay: keep playing live after the recording ends"},
    {"replay-speed", "N", T::Float, G::Developer, kCli, 0.001, 64, nullptr, "1",
     "with --replay: play N times faster (vertical blank and sound; no sound output)"},
    {"replay-no-draw", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "with --replay: no draws or clears on the GPU (game logic only, faster checks); with --replay-shots only the "
     "frames just before each picture are drawn"},
    {"replay-dump", "N[-M[:S]] DIR", T::Custom, G::Developer, kCli, 0, 0, nullptr, "",
     "with --record/--replay: game state of frame N (or every [S-th] frame N..M) to DIR (tools/replay_diff.py)"},
    {"replay-shots", "N,N.. DIR", T::Custom, G::Developer, kCli, 0, 0, nullptr, "",
     "with --record/--replay: pictures of those frames to DIR (shot_<n>.bmp)"},
    {"own-main", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "start the game at our Sys_Main C (own engine host, step 0.3)"},
    {"host", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "--own-main with the original engine code (INT3) and the kernel exports gone"},
    {"text-trap", "LIST OUT", T::Custom, G::Developer, kCli, 0, 0, nullptr, "",
     "log every original function from LIST (tools/text_trap_list.py) the first time it runs, with its caller, to "
     "OUT (Linux)"},
    {"text-clear", "OUT", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "original non-level code bytes become INT3 except our hooks; code that still runs is logged to OUT with its "
     "caller and put back by function (Linux)"},
    {"check-crt", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "our CRT (src/engine/shared/title_crt.h) against the title's on a table, then exit"},
    {"dump-shaders", "DIR", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "every shader program the renderer sets up, for tools/shader_hlsl.py"},
    {"write-signatures", "FILE", T::Path, G::Developer, kCli, 0, 0, nullptr, "",
     "write the supported XBE's function signatures, for finding its addresses in other releases (tools/data), and "
     "exit"},
    {"force", "\"F(ARGS)@S*N?W\"", T::Custom, G::Developer, kCli, 0, 0, nullptr, "",
     "with --compare: test N calls of F the title does not make, from S s after the level is active, on frames "
     "where W is nonzero; ARGS: eax= ... ebp= and stack words; values: numbers, [E] reads a title word, E+N (first "
     "boot only); \"scene:F(...)\" calls from R_RenderView with the main scene open (draw-list code)"},
    {"compare", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "test the decompiled functions against the originals while playing (Linux; counts in tools/data/functions.csv, "
     "details in the log); implies --pc-patches off unless that is given, ignores settings.ini; functions with 100 "
     "matched tests and no mismatch or drift are no longer tested"},
    {"compare-all", "", T::Flag, G::Developer, kCli, 0, 0, nullptr, "false",
     "--compare, testing every function (a module's final check)"},
    {"launch-data", "FD", T::Custom, G::Developer, kCliHidden, 0, 0, nullptr, "", "set by a title reboot"},
    {"session-start", "NS", T::Custom, G::Developer, kCliHidden, 0, 0, nullptr, "", "set by a title reboot"},

    // --- Older combined forms, still read (recordings, older settings.ini, scripts); settings.ini gets the parts.
    {"graphics-features", "\"K=V ...\"", T::Text, G::Graphics, kHidden, 0, 0, nullptr, "",
     "older form of fxaa, sharpness, ... in one value"},
    {"volume", "\"K=V ...\"", T::Text, G::Sound, kHidden, 0, 0, nullptr, "",
     "older form of the volume-* and close-* settings"},
    {"reverb", "\"K=V ...\"", T::Text, G::Sound, kHidden, 0, 0, nullptr, "", "older form of the reverb-* settings"},
    {"enhanced", "\"K=0|1 ...\"", T::Text, G::Sound, kHidden, 0, 0, nullptr, "",
     "older form of resampler, more-voices, room-reverb, occlusion, close-boost"},
};

// Launcher and PC menu, in drawing order (per group: the tabs pick groups).
const SettingUi kUi[] = {
    // Game
    {"language", "Language", "Language",
     "The Xbox's language setting. The game can only use languages that are on your disc: the North American "
     "Standard Edition disc is English only, a European disc has its own languages (and may have no English). The "
     "log lists the disc's languages.",
     "English|German|French|Spanish|Italian"},
    // Renderer first: the launcher's Graphics tab starts with it
    {"renderer", "Renderer", "Renderer",
     "Vulkan is the renderer of our own engine (needs a Vulkan 1.3 graphics driver; without one the game uses "
     "Direct3D 9 by itself). Choose Direct3D 9 if the picture or the speed is wrong with Vulkan.",
     "Vulkan|Direct3D 9"},
    {"d3d9", nullptr, "Direct3D 9 through",
     "Automatic uses Vulkan through the included DXVK and falls back to the Direct3D 9 that comes with Windows when "
     "Vulkan is not available. Choose one yourself if the picture or the speed is wrong with the other.",
     "Automatic|Vulkan (DXVK)|Direct3D 9 (Windows)", nullptr, "renderer=d3d9"},
    // Display
    {"aspect", "Window", "Aspect ratio", nullptr},
    {"fullscreen", nullptr, "Fullscreen", "Alt+Enter switches between window and fullscreen while playing."},
    {"scale", nullptr, "Window size", nullptr, nullptr, "x%d", "fullscreen=false"},
    {"render-scale", "Picture", "Render scale", nullptr},
    {"fps", nullptr, "Frame rate", nullptr},
    {"brightness", nullptr, "Brightness",
     "1.00 looks as on the Xbox. Higher brightens the midtones (dark interiors, night missions); black and white "
     "stay as they are.",
     nullptr, "%.2f"},
    {"sharp-text", nullptr, "Sharper text",
     "Draws the game's text again at the render resolution, from free fonts that look like the original ones "
     "(included with the game). Off: the original low-resolution letters."},
    {"fov", "View", "Field of view",
     "Degrees across a 4:3 picture, as the game counts them (default 70); wider pictures see more at the sides. "
     "Campaign only.",
     nullptr, "%.0f"},
    // Graphics
    {"graphics", "Enhanced graphics", "Enhanced graphics",
     "Off: the picture as on the Xbox (at your resolution). On: the improvements below that are switched on. Also "
     "switchable while playing in the PC menu (` key), each one too.",
     nullptr, nullptr, nullptr, "enhanced"},
    {"fxaa", nullptr, "Anti-aliasing (FXAA)", "Smooths jagged edges in the 3D picture. Menus and the HUD are left as "
     "they are.", nullptr, nullptr, "graphics=enhanced"},
    {"anisotropic", nullptr, "Anisotropic filtering",
     "Sharp textures on the ground and walls seen at a slant, at the level below (the level: launcher only).",
     nullptr, nullptr, "graphics=enhanced"},
    {"anisotropy", nullptr, "Filtering level", nullptr, nullptr, nullptr, "anisotropic"},
    {"sharpen", nullptr, "Sharpening",
     "Contrast-adaptive sharpening of the 3D picture: more detail without halos at hard edges.", nullptr, nullptr,
     "graphics=enhanced"},
    {"sharpness", nullptr, "Sharpness", "0 = none (the default).", nullptr, "%.2f", "sharpen"},
    {"vibrance", nullptr, "Colour vibrance",
     "Stronger colours where they are weak (the game's palette is muted); below 0 greyer.", nullptr, nullptr,
     "graphics=enhanced"},
    {"vibrance-amount", nullptr, "Vibrance", "0 = the game's colours (the default).", nullptr, "%+.2f", "vibrance"},
    {"texture-detail", nullptr, "Texture detail",
     "Sharper textures in the distance (finer mipmaps); too far below 0 they shimmer when moving.", nullptr, nullptr,
     "graphics=enhanced"},
    {"lod-bias", nullptr, "Detail bias", "0 = the game's choice (the default); lower is sharper.", nullptr, "%.2f",
     "texture-detail"},
    // Sound
    {"speakers", "Output", "Speakers",
     "Automatic: what your output device has, with 3D sound for headphones when the game's own settings ask for "
     "headphones. Otherwise this layout, whatever the game's settings say. Headphones (3D sound) places sounds "
     "around you, also above and behind (HRTF).",
     "Automatic|Stereo speakers|Headphones (3D sound)|Quad (4.0)|Surround 5.1|Surround 7.1"},
    {"audio-device", nullptr, "Output device", "A device that is gone when the game starts falls back to the system "
     "default."},
    {"silent", nullptr, "Mute sound", nullptr},
    {"volume-effects", "Volume", "Effects volume", "Gunfire, explosions, vehicles, footsteps, the menus' sounds.",
     nullptr, "%.0f%%"},
    {"volume-ambience", nullptr, "Ambience volume", "The levels' background: distant battle, wind, rain, the hum of "
     "a room.", nullptr, "%.0f%%"},
    {"volume-dialogue", nullptr, "Dialogue volume", "Orders, squad chatter, briefings.", nullptr, "%.0f%%"},
    {"volume-music", nullptr, "Music volume",
     "On top of the game's own volume setting, for Original and Enhanced sound alike.", nullptr, "%.0f%%"},
    {"audio", "Enhanced sound", "Enhanced sound",
     "Off: the sound as on the Xbox. On: the improvements below that are switched on. Also switchable while playing "
     "in the PC menu (` key), each improvement too; switch while standing still to compare.",
     nullptr, nullptr, nullptr, "enhanced"},
    {"resampler", nullptr, "Finer resampling", "OpenAL's best resampler (23rd order Sinc) instead of its default.",
     nullptr, nullptr, "audio=enhanced"},
    {"more-voices", nullptr, "More voices",
     "Up to 192 sounds placed in the world at once instead of the Xbox's 36, and effects drop fewer of their sounds "
     "in busy fights.",
     nullptr, nullptr, "audio=enhanced"},
    {"room-reverb", nullptr, "Reverb",
     "Reverb that follows the place you are in: room size, walls, stone or wood, open sky.", nullptr, nullptr,
     "audio=enhanced"},
    {"reverb-amount", nullptr, "Reverb amount", "The reverb's overall level.", nullptr, "%+.1f dB", "room-reverb"},
    {"reverb-decay", nullptr, "Reverb decay",
     "How long the reverb lasts, times what the room's size and materials give.", nullptr, "x%.2f", "room-reverb",
     nullptr, true},
    {"reverb-brightness", nullptr, "Reverb brightness",
     "Below 1 the reverb's highs die away sooner (darker), above 1 later (brighter).", nullptr, "x%.2f",
     "room-reverb", nullptr, true},
    {"reverb-reflections", nullptr, "Early reflections", "The first echoes off nearby walls.", nullptr, "%+.1f dB",
     "room-reverb"},
    {"reverb-clutter", nullptr, "Room clutter",
     "What soaks up sound besides the walls the game knows about (furniture, debris, openings). Lower makes rooms "
     "ring longer. Default 0.07.",
     nullptr, "%.2f", "room-reverb"},
    {"occlusion", nullptr, "Occlusion", "Sounds behind walls and buildings muffled; in view, heard in full.", nullptr,
     nullptr, "audio=enhanced"},
    {"close-boost", nullptr, "Close-range boost",
     "Gunfire and bullets louder near you: full within 15 m, fading to the game's own level at 30 m (your own "
     "gunfire always full). The game mixes them quietly; 0 dB = its level.",
     nullptr, nullptr, "audio=enhanced"},
    {"close-gunfire", nullptr, "Others' gunfire", nullptr, nullptr, "%+.1f dB", "close-boost"},
    {"own-gunfire", nullptr, "Your gunfire", nullptr, nullptr, "%+.1f dB", "close-boost"},
    {"close-bullets", nullptr, "Bullet impacts and flybys", nullptr, nullptr, "%+.1f dB", "close-boost"},
    // Controls
    {"mouse-sensitivity", "Mouse and keyboard", "Mouse sensitivity", "Degrees the view turns per mouse count.",
     nullptr, "%.2f", nullptr, nullptr, true},
    {"aim-assist", nullptr, "Aim assist",
     "The view follows an enemy under the crosshair, as on the Xbox. Applies to the controller and the mouse alike, "
     "in the campaign and in multiplayer."},
    {"button-prompts", "On screen", "Button prompts",
     "The buttons the game shows in its hints and menus: keys and mouse buttons, or the Xbox controller's. They "
     "follow the controller style chosen in the game's options.",
     "Follow the last used device|Keyboard and mouse|Controller"},
    // Network
    {"net-port", "System Link", "System Link port",
     "Use the same port on every PC. A firewall has to let these UDP ports through on the local network (Windows "
     "asks the first time you play)."},
};

bool is_combined(std::string_view key) {
    return key == "graphics-features" || key == "volume" || key == "reverb" || key == "enhanced" || key == "keys";
}

std::vector<std::pair<std::string, std::string>> words(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(text);
    for (std::string word; in >> word;)
        if (const size_t eq = word.find('='); eq != std::string::npos)
            out.emplace_back(word.substr(0, eq), word.substr(eq + 1));
    return out;
}

bool same_value(const SettingDef& def, const std::string& a, const std::string& b) {
    if (def.type == T::Float || def.type == T::Int) return std::strtod(a.c_str(), nullptr) == std::strtod(b.c_str(), nullptr);
    return a == b;
}

void wrap(std::string& out, const std::string& text, size_t indent, size_t width) {
    size_t column = indent;
    std::istringstream in(text);
    bool first = true;
    for (std::string word; in >> word;) {
        if (!first && column + 1 + word.size() > width) {
            out += "\n" + std::string(indent, ' ');
            column = indent;
        } else if (!first) {
            out += ' ';
            ++column;
        }
        out += word;
        column += word.size();
        first = false;
    }
    out += '\n';
}

}  // namespace

std::span<const SettingDef> setting_defs() { return kSettings; }
std::span<const SettingUi> setting_uis() { return kUi; }

const SettingUi* setting_ui(std::string_view key) {
    for (const SettingUi& ui : kUi)
        if (key == ui.key) return &ui;
    return nullptr;
}

const SettingDef* find_setting(std::string_view key) {
    for (const SettingDef& def : kSettings)
        if (key == def.key) return &def;
    return nullptr;
}

const char* setting_group_name(SettingGroup group) {
    switch (group) {
    case G::Files: return "Game";
    case G::Display: return "Display";
    case G::Graphics: return "Graphics";
    case G::Sound: return "Sound";
    case G::Controls: return "Controls";
    case G::Network: return "Network";
    case G::Testing: return "Testing";
    case G::Developer: return "Developer";
    }
    return "";
}

std::optional<std::string> check_setting(const SettingDef& def, std::string_view value_in, std::string* error) {
    std::string value(value_in);
    char buffer[96];
    switch (def.type) {
    case T::Flag:
        if (value == "true" || value == "on" || value == "1") return "true";
        if (value == "false" || value == "off" || value == "0") return "false";
        if (error) *error = "true or false";
        return std::nullopt;
    case T::Switch:
        if (value == "on" || value == "1" || value == "true") return "on";
        if (value == "off" || value == "0" || value == "false") return "off";
        if (error) *error = "on or off";
        return std::nullopt;
    case T::Int: {
        char* end = nullptr;
        const long n = std::strtol(value.c_str(), &end, 10);
        if (!value.empty() && end && *end == 0 && n >= def.min && n <= def.max) return value;
        std::snprintf(buffer, sizeof(buffer), "a whole number (%g-%g)", def.min, def.max);
        if (error) *error = buffer;
        return std::nullopt;
    }
    case T::Float: {
        char* end = nullptr;
        const double x = std::strtod(value.c_str(), &end);
        if (!value.empty() && end && *end == 0 && x >= def.min && x <= def.max) return value;
        std::snprintf(buffer, sizeof(buffer), "a number (%g-%g)", def.min, def.max);
        if (error) *error = buffer;
        return std::nullopt;
    }
    case T::Choice: {
        const std::string_view choices = def.choices;
        for (size_t p = 0; p <= choices.size();) {
            const size_t bar = std::min(choices.find('|', p), choices.size());
            if (choices.substr(p, bar - p) == value) return value;
            p = bar + 1;
        }
        if (error) {
            std::string list(choices);
            const size_t last = list.rfind('|');
            if (last != std::string::npos) list.replace(last, 1, " or ");
            for (char& c : list)
                if (c == '|') c = ',';
            size_t comma = 0;
            while ((comma = list.find(',', comma)) != std::string::npos) list.insert(++comma, " ");
            *error = list;
        }
        return std::nullopt;
    }
    case T::Text:
    case T::Path:
    case T::Custom:
        if (def.check && !def.check(value, error)) return std::nullopt;
        if (value.empty() && def.type == T::Path) {
            if (error) *error = "a file or folder";
            return std::nullopt;
        }
        return value;
    }
    return std::nullopt;
}

std::string settings_help(const char* program) {
    std::string out = std::string("usage: ") + program + " [options]\n"
                      "Every option can also be a line \"name = value\" in settings.ini (the launcher writes it; "
                      "flags: name = true),\nexcept those marked (command line); options given on the command line "
                      "win.\n";
    constexpr size_t kIndent = 19, kWidth = 110;
    for (int g = 0; g <= static_cast<int>(G::Developer); ++g) {
        out += std::string("\n") + setting_group_name(static_cast<G>(g)) + ":\n";
        for (const SettingDef& def : kSettings) {
            if (static_cast<int>(def.group) != g || (def.flags & (kHidden | kLauncher))) continue;
            std::string head = std::string("  --") + def.key + (*def.arg ? std::string(" ") + def.arg : "");
            if (head.size() + 1 > kIndent)
                head += "\n" + std::string(kIndent, ' ');
            else
                head.resize(kIndent, ' ');
            std::string help = def.help;
            if (def.type == T::Choice && std::string_view(def.arg).find('|') == std::string_view::npos) {
                std::string error;
                check_setting(def, "", &error);  // "a, b or c"
                help = error + ": " + help;
            }
            if (def.flags & kCommandLine && def.group != G::Developer && def.group != G::Testing)
                help += " (command line)";
            out += head;
            wrap(out, help, kIndent, kWidth);
        }
    }
    out += "\nOlder combined forms are still read: --graphics-features, --volume, --reverb and --enhanced \"key=value "
           "...\"; --keys \"action=key ...\" is the command-line form of the key-<action> lines.\n";
    return out;
}

bool upgrade_settings(Settings& settings) {
    bool changed = false;
    for (const char* combined : {"graphics-features", "volume", "reverb", "enhanced", "keys"}) {
        const std::optional<std::string> text = settings.get(combined);
        if (!text) continue;
        settings.erase(combined);
        changed = true;
        std::vector<std::string> named;  // parts this line names: a repeat adds a word ("crouch=c crouch=left-ctrl")
        for (auto [part, value] : words(*text)) {
            if (part == "right-stick-click") part = "aim-toggle";  // keys: the toggled aim's name before 0.6d
            for (const SettingDef& def : kSettings) {
                if (!def.part_of || std::string_view(def.part_of) != combined || part != def.part_key) continue;
                const std::optional<std::string> normal = check_setting(def, value);
                if (!normal) break;
                const bool again = std::find(named.begin(), named.end(), def.key) != named.end();
                named.emplace_back(def.key);
                if (again)
                    settings.set(def.key, settings.get(def.key).value_or("") + " " + *normal);
                else if (same_value(def, *normal, def.default_value))
                    settings.erase(def.key);
                else
                    settings.set(def.key, *normal);
            }
        }
    }
    return changed;
}

std::optional<std::string> combined_setting(const Settings& settings, std::string_view combined) {
    std::string text;
    if (auto old = settings.get(std::string(combined))) text = *old;  // not yet upgraded
    for (const SettingDef& def : kSettings) {
        if (!def.part_of || std::string_view(def.part_of) != combined) continue;
        if (auto value = settings.get(def.key)) {
            std::istringstream in(*value);  // several words: the part once per word ("key-crouch = c left-ctrl")
            for (std::string word; in >> word;) {
                if (!text.empty()) text += ' ';
                text += std::string(def.part_key) + "=" + word;
            }
        }
    }
    if (text.empty()) return std::nullopt;
    return text;
}

void set_combined_setting(Settings& settings, std::string_view combined, const std::string& text) {
    settings.erase(std::string(combined));
    for (const SettingDef& def : kSettings)
        if (def.part_of && std::string_view(def.part_of) == combined) settings.erase(def.key);
    if (is_combined(combined)) {
        settings.set(std::string(combined), text);
        upgrade_settings(settings);
    }
}

}  // namespace platform
