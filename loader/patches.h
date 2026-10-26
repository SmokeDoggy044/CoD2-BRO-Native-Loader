#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compare.h"

namespace loader {

// The disc's two titles: default.xbe (campaign) and codmp_xboxf.xbe (multiplayer).
// Both are XDK 5849 builds of the same engine, at different addresses.
enum class TitleId { Campaign, Multiplayer };

struct TitleInfo {
    TitleId id;
    const char* xbe_name;
    const char* symbol_file;  // Ghidra export (next to the loader, or tools/data)
    uint32_t d3d_device_pointer;  // D3D__pDevice (D3D is LTCG)
    uint32_t d3d_render_state;    // D3D__RenderState
    uint32_t d3d_texture_state;   // D3D_g_DeferredTextureState
    uint32_t gamepad_type;        // XPP g_DeviceType_Gamepad (XDEVICE_TYPE_GAMEPAD)
};

// The title with this XBE file name, or null.
const TitleInfo* find_title(std::string_view xbe_name);

struct TitlePatchConfig {
    float mouse_degrees_per_count = 0.11f;  // mouse look (PC CoD: sensitivity 5 * m_yaw 0.022)
    int32_t start_level = -1;               // >= 0: start this mission once the menus are up (testing)
    bool aim_assist = true;                 // false: no sticky aim (the view following a target)
    int32_t frame_rate = 60;                // 60 or 30 (as on the Xbox): frames per second
    // The game's button icons: keycaps while the keyboard/mouse is used (Auto), always
    // (Keyboard) or never (Controller). See text/key_prompts.h.
    enum class ButtonPrompts { Auto, Keyboard, Controller };
    ButtonPrompts button_prompts = ButtonPrompts::Auto;
    // false: only the patches the game needs to run here (privileged code, RDTSC,
    // the MP heap lock, --level); none of the PC features above, so the game's code
    // runs as on the Xbox (for --compare: a patched function cannot be compared).
    bool pc_features = true;
    // Console commands and cvars ("r_fullbright 1; give all") queued once the level
    // is active (testing, campaign only).
    std::string exec_text;
    // Calls the title does not make, tested once the level is active (--force, needs
    // --compare; campaign only): compare::force_call at the client frame's call site.
    struct ForcedCall {
        std::string name, args;
        std::string when;       // only while this is nonzero (compare::force_call)
        uint32_t after_ms = 0;  // after the level became active
        uint32_t count = 1;     // tests, at most one per frame
        uint32_t started = 0;
    };
    std::vector<ForcedCall> forced_calls;
};

// Redirects the title's functions that cannot run on a PC (privileged instructions,
// direct kernel/hardware access) to replacements, and adds PC features (mouse look).
// Call after the image is mapped. `config` applies to both titles, except
// start_level, exec_text and forced_calls (campaign only).
void install_title_patches(const TitleInfo& title, const TitlePatchConfig& config);

// While the engine draws a scene an in-game menu or the objectives panel created this
// frame (UI laid out for 4:3), the x to narrow it around as a fraction of the width;
// otherwise -1 (render::BackendConfig::narrow_ui_draw).
float in_game_menu_draw();

class SymbolTable;
class XbeImage;

// Redirects the SDK libraries the port replaces (DirectSound, Direct3D), found by name
// in the Ghidra symbols. Every other function in those libraries' sections traps with
// its name when called. Returns false if a library is present but nothing was found.
bool install_sdk_replacements(const XbeImage& image, const SymbolTable& symbols, const TitleInfo& title);

// The Direct3D replacements that --compare's test processes record instead of calling
// (state setters and draws: the title reads nothing back from them).
std::vector<compare::SdkCall> recorded_sdk_calls();
// The pages of the RDTSC trampolines (begin, end; 0, 0 before install_title_patches).
std::pair<uint32_t, uint32_t> rdtsc_trampolines();

}  // namespace loader
