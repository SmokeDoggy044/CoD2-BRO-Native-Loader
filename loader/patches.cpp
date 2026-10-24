// Title-specific patches: original functions that cannot run on a PC, replaced or
// turned into no-ops. Addresses are for CoD2: BRO default.xbe (XDK 5849) unless a
// table says codmp_xboxf.xbe (multiplayer).

#include "patches.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iterator>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "compiler.h"
#include "crash.h"
#include "devices.h"
#include "filesystem.h"
#include "float_env.h"
#include "hooks.h"
#include "key_prompts.h"
#include "kernel.h"
#include "log.h"
#include "symbols.h"
#include "xbe.h"
#include "xbox_d3d_api.h"
#include "xbox_dsound.h"
#include "xbox_input.h"
#include "xonline.h"

#include <strings.h>

namespace loader {

namespace {

// XAPI 001ede5f (MP 0015a84e; unnamed in Ghidra), called during startup on retail (no debug
// monitor): finds the kernel's INIT section through its PE header at 0x80010000
// and rewrites GDT entry 1 (code selector 0x08) with SGDT + a direct GDT write, to
// limit code execution. Neither is possible or useful in user mode. With the saved
// descriptor left at zero, XAPI also skips putting it back (at 001ee829).
void XB_CDECL restrict_code_selector() {}

// XAPI XMountUtilityDrive (BOOL format_clean) -> BOOL: picks a cache partition through
// a table in raw disk sectors, formats it as FATX if needed and links Z: to it. The
// cache is a host folder here, so just link it (emptying it when asked to format).
uint32_t XB_STDCALL mount_utility_drive(uint32_t format_clean) {
    return xbox::mount_utility_drive(format_clean & 0xFF) == 0 ? 1 : 0;
}

// Xbox Live sign-in (SP FUN_00221eb0 from the main menu and Xbox Live Options > Sign
// In; MP FUN_0018edd0 from Play Xbox Live; online state in esi, always the object
// the title's online pointer holds): clears the last logon result, sets the state to
// 1 (signing in) and starts the XOnline sign-in UI, which draws its account list with
// XFONT into the frame buffer's tiled view (0xF0000000, not mapped). There is no Xbox
// Live: keep the state at 0, so the menu says "Not signed in".
uint32_t g_online_pointer = 0;  // per title: SP 0x0095f6fc, MP 0x004b2fec

void XB_CDECL start_sign_in() {
    const auto online = *reinterpret_cast<const uint32_t*>(uintptr_t{g_online_pointer});
    if (online) {
        *reinterpret_cast<uint32_t*>(uintptr_t{online + 0x446c}) = 0;  // logon result
        *reinterpret_cast<uint32_t*>(uintptr_t{online + 0x4470}) = 0;
    }
    LOG_INFO("patches", "Xbox Live sign-in: not available");
    render::show_notice({"Xbox Live is not available", "Play System Link instead: it works on your local network."},
                        4);
}

struct Patch {
    uint32_t address;
    const void* replacement;
    const char* what;
};

const Patch kCampaignPatches[] = {
    {0x001ede5f, reinterpret_cast<const void*>(&restrict_code_selector), "XAPI code selector restriction"},
    {0x001e9b66, reinterpret_cast<const void*>(&mount_utility_drive), "XMountUtilityDrive: Z: = cache folder"},
    {0x00221eb0, reinterpret_cast<const void*>(&start_sign_in), "Xbox Live sign-in: not available"},
};

const Patch kMultiplayerPatches[] = {
    {0x0015a84e, reinterpret_cast<const void*>(&restrict_code_selector), "XAPI code selector restriction"},
    {0x0015656c, reinterpret_cast<const void*>(&mount_utility_drive), "XMountUtilityDrive: Z: = cache folder"},
    {0x0018edd0, reinterpret_cast<const void*>(&start_sign_in), "Xbox Live sign-in: not available"},
};

// RDTSC sites in title code (Ghidra: every RDTSC outside the SDK sections) and how
// many bytes from the RDTSC on are moved to the trampoline: RDTSC plus the stores
// of its result after it, which use no relative addressing and are no branch target.
// The title assumes the Xbox CPU's 733 MHz (FUN_002960a0 divides by 733333.3; XAPI
// QueryPerformanceCounter is RDTSC with frequency 733333333); the host's counter
// runs several times faster, and so would the game.
struct RdtscSite {
    uint32_t address;
    uint8_t length;
};
const RdtscSite kCampaignRdtscSites[] = {
    {0x000643ad, 6}, {0x0006441a, 6}, {0x00064584, 6}, {0x0006469e, 6}, {0x00064f2e, 6}, {0x00064f98, 6},
    {0x00064fe8, 6}, {0x00065139, 6}, {0x000651ea, 6}, {0x00065329, 6},
    {0x001ed6ce, 7},  // XAPI QueryPerformanceCounter: mov [ecx],eax; mov [ecx+4],edx
    {0x001fda73, 6}, {0x0020d34a, 6}, {0x0020d37e, 6}, {0x002960ba, 6}, {0x002960e3, 6}, {0x00298185, 6},
    {0x002981ae, 6}, {0x002b1caf, 6}, {0x002b1ce9, 6}, {0x0030ba24, 6}, {0x0030bc51, 6},
    {0x0032d893, 5},  // mov [esp],eax
    {0x0032ebca, 6}, {0x0032ec52, 6}, {0x00331fc5, 6}, {0x0033228a, 6},
};

// codmp_xboxf.xbe: the same engine timing code (all are mov [esp+N],eax after it).
const RdtscSite kMultiplayerRdtscSites[] = {
    {0x0006530d, 6}, {0x0006537a, 6}, {0x000654e4, 6}, {0x000655fe, 6}, {0x00065e8e, 6}, {0x00065ef8, 6},
    {0x00065f48, 6}, {0x00066099, 6}, {0x0006614a, 6}, {0x00066289, 6},
    {0x001544ff, 7},  // XAPI QueryPerformanceCounter
    {0x002074fa, 6}, {0x00207523, 6}, {0x002095d5, 6}, {0x002095fe, 6}, {0x00223aff, 6}, {0x00223b39, 6},
    {0x0027e754, 6}, {0x0027e981, 6}, {0x00298e9a, 6}, {0x00298f22, 6}, {0x0029a925, 6}, {0x0029abea, 6},
};

int64_t XB_CDECL title_rdtsc() { return xbox::cpu_time_stamp_counter(); }

void put_rel32(uint8_t* at, uint8_t opcode, const void* target) {
    const uint32_t from = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at)) + 5;
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)) - from;
    at[0] = opcode;
    std::memcpy(at + 1, &relative, 4);
}

// Each site: JMP to a trampoline that does pushfd; push ecx; call title_rdtsc;
// pop ecx; popfd; <the moved instructions>; jmp back.
void install_rdtsc_patches(std::span<const RdtscSite> sites) {
    constexpr size_t kTrampolineSize = 32;
    auto* pool = alloc_code(sites.size() * kTrampolineSize);
    if (!pool) {
        LOG_ERROR("patches", "no memory for the RDTSC trampolines; the title runs at the host's TSC rate");
        return;
    }
    int installed = 0;
    for (size_t i = 0; i < sites.size(); ++i) {
        const RdtscSite& site = sites[i];
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{site.address});
        if (code[0] != 0x0F || code[1] != 0x31) {
            LOG_WARN("patches", "%08x: not RDTSC; left alone", site.address);
            continue;
        }
        uint8_t* t = pool + i * kTrampolineSize;
        *t++ = 0x9C;  // pushfd
        *t++ = 0x51;  // push ecx
        put_rel32(t, 0xE8, reinterpret_cast<const void*>(&title_rdtsc));
        t += 5;
        *t++ = 0x59;  // pop ecx
        *t++ = 0x9D;  // popfd
        std::memcpy(t, code + 2, site.length - 2u);
        t += site.length - 2;
        put_rel32(t, 0xE9, code + site.length);
        put_rel32(code, 0xE9, pool + i * kTrampolineSize);
        std::memset(code + 5, 0x90, site.length - 5u);  // NOP
        ++installed;
    }
    LOG_INFO("patches", "RDTSC: %d sites run at the Xbox's 733 MHz", installed);
}

// --- Mouse look ---------------------------------------------------------------------
//
// The Xbox build feeds its right stick through Quake 3's mouse path (in_joystick:
// IN_JoyMove posts SE_MOUSE events, CL_MouseMove FUN_002b39e0 applies stick
// acceleration and a 370 deg/s turn limit), so a real mouse cannot go through there.
// Instead CL_CreateCmd's call to CL_MouseMove goes through a thunk that first adds
// the mouse motion to cl.viewangles the way PC CoD does, under the same conditions
// CL_MouseMove checks.

// Client globals per title (MP: the same instructions in the twin functions,
// CL_CreateCmd FUN_0024c130, CL_MouseMove FUN_002259f0, key label FUN_0027fa60).
struct ClientAddresses {
    uint32_t call_mouse_move;  // the call in CL_CreateCmd
    uint32_t mouse_move;       // CL_MouseMove
    uint32_t view_flags;       // player state: 0x4000 = view locked
    uint32_t view_allow_flags; // ... unless one of 0x106000
    uint32_t vehicle;          // non-zero in a vehicle
    uint32_t sensitivity;      // cl.cgameSensitivity
    uint32_t pitch, yaw;       // cl.viewangles
    uint32_t freelook;         // non-zero: pitch always follows the mouse
    uint32_t cl_freelook;      // cvar_t* pointers
    uint32_t m_pitch;
    uint32_t key_bindings;     // keys[] (see remap_keyboard_actions)
};
constexpr ClientAddresses kCampaignClient = {0x002c7ad7, 0x002b39e0, 0x009601ac, 0x00960240, 0x00933fb8,
                                             0x00961774, 0x00961798, 0x0096179c, 0x009341b0, 0x0093f808,
                                             0x00929c0c, 0x0093425c};
constexpr ClientAddresses kMultiplayerClient = {0x0024c177, 0x002259f0, 0x004b3ddc, 0x004b3e84, 0x004834f8,
                                                0x004b53b4, 0x004b53d8, 0x004b53dc, 0x004836f0, 0x0049221c,
                                                0x00479124, 0x004848e4};
ClientAddresses g_client = kCampaignClient;

template <typename T>
T& title(uint32_t address) {
    return *reinterpret_cast<T*>(uintptr_t{address});
}
// A cvar's float / integer value (cvar_t: value at +0x1C, integer at +0x20).
float cvar_value(uint32_t cvar_pointer) { return title<float>(title<uint32_t>(cvar_pointer) + 0x1C); }
int32_t cvar_integer(uint32_t cvar_pointer) { return title<int32_t>(title<uint32_t>(cvar_pointer) + 0x20); }

float g_mouse_degrees_per_count = 0.11f;

void XB_CDECL mouse_look() {
    float dx, dy;
    input::take_mouse_motion(dx, dy);
    if (dx == 0.0f && dy == 0.0f) return;
    // Player state: view locked (0x4000) unless one of 0x106000 allows looking.
    if ((title<uint32_t>(g_client.view_flags) & 0x4000) && !(title<uint32_t>(g_client.view_allow_flags) & 0x106000))
        return;
    if (title<int32_t>(g_client.vehicle) != 0) return;  // in a vehicle: its own controls
    platform::HostFloatEnv float_env;
    // cl.cgameSensitivity: cg.zoomSensitivity, set each frame by CG_DrawActiveFrame
    // (FUN_0028b340); lower while aiming down sights, 0 before the first frame.
    const float scale = g_mouse_degrees_per_count * title<float>(g_client.sensitivity);
    title<float>(g_client.yaw) -= dx * scale;
    if (title<int32_t>(g_client.freelook) != 0 || cvar_integer(g_client.cl_freelook) != 0) {
        const float invert = cvar_value(g_client.m_pitch) < 0.0f ? -1.0f : 1.0f;  // m_pitch sign = invert Y
        title<float>(g_client.pitch) += dy * scale * invert;
    }
}

// pushad; pushfd; call mouse_look; popfd; popad; jmp CL_MouseMove
void install_mouse_look() {
    auto* site = reinterpret_cast<uint8_t*>(uintptr_t{g_client.call_mouse_move});
    uint32_t relative;
    std::memcpy(&relative, site + 1, 4);
    if (site[0] != 0xE8 || g_client.call_mouse_move + 5 + relative != g_client.mouse_move) {
        LOG_WARN("patches", "%08x: not the call to CL_MouseMove; no mouse look", g_client.call_mouse_move);
        return;
    }
    auto* thunk = alloc_code(64);
    if (!thunk) return;
    uint8_t* t = thunk;
    *t++ = 0x60;  // pushad
    *t++ = 0x9C;  // pushfd
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(&mouse_look));
    t += 5;
    *t++ = 0x9D;  // popfd
    *t++ = 0x61;  // popad
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{g_client.mouse_move}));
    put_rel32(site, 0xE8, thunk);
    LOG_INFO("patches", "mouse look: %.3f degrees per count", g_mouse_degrees_per_count);
}

// Keyboard actions follow the player's controller style. The keyboard gives the pad
// buttons of the default layout (config/default.cfg); style_a..l.cfg bind the buttons
// to other commands. The active binding of each key code is at keys[code].binding
// (SP 0x0093425c / MP 0x004848e4 + code * 8, a string or null); the pad's key codes
// come from FUN_002bdf10 (MP FUN_00230d60) and the key name table at 0x008d32e8.
// Both titles use the same style files.
struct PadControl {
    uint16_t digital;     // DigitalButton bit, or 0 for an analog button
    uint8_t analog;       // AnalogButton index when digital == 0
    uint8_t key_code;     // the title's key code for it
    const char* command;  // its command in the default layout
};
constexpr PadControl kPadControls[] = {
    {0, input::kA, 0xd4, "+activate"},             // JOY6 / X
    {0, input::kB, 0xd5, "lowerstance"},           // CIRCLE
    {0, input::kX, 0xd3, "+reload"},               // SQUARE
    {0, input::kY, 0xd6, "+moveup"},               // TRIANGLE
    {0, input::kBlack, 0xd9, "+melee"},            // R2
    {0, input::kWhite, 0xda, "+grenadeattack"},    // L2
    {0, input::kLeftTrigger, 0xd8, "+speed"},      // L1
    {0, input::kRightTrigger, 0xd7, "+attack"},    // R1
    {input::kRightThumb, 0, 0xdb, "toggle cl_run"},  // R3
    {input::kLeftThumb, 0, 0xdc, "+sprint"},       // L3
    {input::kDpadUp, 0, 0x9a, "weapnext"},
    {input::kDpadDown, 0, 0x9b, ""},
    {input::kDpadLeft, 0, 0x9c, "+leanleftswitchnext"},
    {input::kDpadRight, 0, 0x9d, "+leanrightswitchnext"},
};

// Commands that do the same as the default one where a style has no exact match.
const char* similar_command(const char* command) {
    struct Pair { const char* command; const char* similar; };
    static constexpr Pair kSimilar[] = {
        {"+reload", "+activatereload"},   {"+activate", "+activatereload"},
        {"+leanleftswitchnext", "+leanleft"}, {"+leanrightswitchnext", "+leanright"},
        {"weapnext", "weapprev"},
    };
    for (const Pair& pair : kSimilar)
        if (std::strcmp(pair.command, command) == 0) return pair.similar;
    return nullptr;
}

const char* key_binding(uint8_t key_code) {
    const uint32_t binding = title<uint32_t>(g_client.key_bindings + key_code * 8u);
    return binding ? reinterpret_cast<const char*>(uintptr_t{binding}) : "";
}

uint8_t control_value(const input::PadState& state, const PadControl& control) {
    if (control.digital) return (state.buttons & control.digital) ? 0xFF : 0;
    return state.analog_buttons[control.analog];
}

void set_control(input::PadState& state, const PadControl& control, uint8_t value) {
    if (control.digital) {
        if (value) state.buttons |= control.digital;
    } else {
        state.analog_buttons[control.analog] = std::max(state.analog_buttons[control.analog], value);
    }
}

void update_button_prompts();

void remap_keyboard_actions(const input::PadState& default_layout, input::PadState& active_layout) {
    update_button_prompts();  // on the same polls, so the prompts follow the layout
    const bool bound = std::any_of(std::begin(kPadControls), std::end(kPadControls),
                                   [](const PadControl& control) { return *key_binding(control.key_code) != 0; });
    if (!bound) {  // no bindings yet: keep the default layout
        active_layout = default_layout;
        return;
    }
    // Logs the pad's bindings when they change (a style picked in the options).
    static std::string logged;
    std::string bindings;
    for (const PadControl& control : kPadControls) (bindings += key_binding(control.key_code)) += ", ";
    if (bindings != logged) {
        LOG_INFO("input", "pad bindings A B X Y black white LT RT R3 L3 up down left right: %s", bindings.c_str());
        logged = bindings;
    }
    for (const PadControl& from : kPadControls) {
        const uint8_t value = control_value(default_layout, from);
        if (!value) continue;
        // The control bound to the same command now, else to a similar one; nothing
        // if the style has neither (the default control may do something else there).
        const PadControl* to = nullptr;
        for (const char* command : {from.command, similar_command(from.command)}) {
            if (!command || !*command) continue;
            for (const PadControl& candidate : kPadControls)
                if (strcasecmp(key_binding(candidate.key_code), command) == 0) to = &candidate;
            if (to) break;
        }
        if (to) set_control(active_layout, *to, value);
    }
}

// Button prompts: the keys that give each of the title's pad controls in its active
// layout - the inverse of remap_keyboard_actions - shown in place of its button icons
// (text/key_prompts.h) while the keyboard and mouse are in use. In menus A and B are
// Return and Backspace (the menu keys), and the D-pad the arrow keys.
TitlePatchConfig::ButtonPrompts g_button_prompts = TitlePatchConfig::ButtonPrompts::Auto;

// The keyboard's and mouse's control for each of kPadControls in the default layout
// (input/devices.h).
constexpr const char* kDefaultKeys[] = {"F", "C", "R", "Space", "V", "G", "@rmb", "@lmb", "X", "Shift", "1", "", "Q", "E"};
static_assert(std::size(kDefaultKeys) == std::size(kPadControls));

text::PromptIcon prompt_icon(const PadControl& control) {
    using text::PromptIcon;
    switch (control.digital) {
        case input::kDpadUp: return PromptIcon::DpadUp;
        case input::kDpadDown: return PromptIcon::DpadDown;
        case input::kDpadLeft: return PromptIcon::DpadLeft;
        case input::kDpadRight: return PromptIcon::DpadRight;
        case 0: break;
        default: return PromptIcon::Count;  // stick clicks: no icon
    }
    static constexpr PromptIcon kAnalog[] = {PromptIcon::A,     PromptIcon::B,           PromptIcon::X,
                                             PromptIcon::Y,     PromptIcon::Black,       PromptIcon::White,
                                             PromptIcon::LeftTrigger, PromptIcon::RightTrigger};
    return kAnalog[control.analog];
}

void update_button_prompts() {
    using text::PromptIcon;
    const bool keyboard = g_button_prompts == TitlePatchConfig::ButtonPrompts::Keyboard ||
                          (g_button_prompts == TitlePatchConfig::ButtonPrompts::Auto &&
                           input::last_device() == input::Device::Keyboard);
    if (!keyboard) {
        text::set_key_labels(std::nullopt);
        return;
    }
    text::KeyLabels labels;
    auto label = [&](PromptIcon icon) -> std::string& { return labels[static_cast<size_t>(icon)]; };
    const bool bound = std::any_of(std::begin(kPadControls), std::end(kPadControls),
                                   [](const PadControl& control) { return *key_binding(control.key_code) != 0; });
    for (size_t i = 0; i < std::size(kPadControls); ++i) {
        const PadControl& from = kPadControls[i];
        if (!*kDefaultKeys[i]) continue;
        // Where remap_keyboard_actions sends this key's control.
        const PadControl* to = bound ? nullptr : &from;
        for (const char* command : {from.command, similar_command(from.command)}) {
            if (to || !command || !*command) break;
            for (const PadControl& candidate : kPadControls)
                if (strcasecmp(key_binding(candidate.key_code), command) == 0) to = &candidate;
        }
        if (!to) continue;
        const PromptIcon icon = prompt_icon(*to);
        if (icon != PromptIcon::Count && label(icon).empty()) label(icon) = kDefaultKeys[i];
    }
    // The fixed keys (menu keys, Escape, Tab) give their buttons in every layout.
    const bool menu = input::in_menu();
    if (menu || label(PromptIcon::DpadUp).empty()) label(PromptIcon::DpadUp) = "@up";
    if (menu || label(PromptIcon::DpadDown).empty()) label(PromptIcon::DpadDown) = "@down";
    if (menu || label(PromptIcon::DpadLeft).empty()) label(PromptIcon::DpadLeft) = "@left";
    if (menu || label(PromptIcon::DpadRight).empty()) label(PromptIcon::DpadRight) = "@right";
    label(PromptIcon::DpadLeftRight) = menu || label(PromptIcon::DpadLeft) == "@left" ? "@leftright"
                                       : label(PromptIcon::DpadLeft) + "/" + label(PromptIcon::DpadRight);
    label(PromptIcon::DpadUpDown) = "@updown";
    label(PromptIcon::Dpad) = "@arrows";
    label(PromptIcon::Start) = "Esc";
    label(PromptIcon::Back) = "Tab";
    if (menu) {
        label(PromptIcon::A) = "Enter";
        label(PromptIcon::B) = "Bksp";
    }
    text::set_key_labels(labels);
}

// --level N starts mission N without the menus. The client frame FUN_002f1e60 (called
// every frame from the main loop at 0x002f27a4) runs "spmap <name of mission k>" once
// the UI is up (0x00933ee4) when 0x0095f920 holds k >= 0; the difficulty screen
// FUN_0032d800 sets it, and the campaign level index 0x008dce48, on New Game.
constexpr uint32_t kCallClientFrame = 0x002f27a4;
constexpr uint32_t kClientFrame = 0x002f1e60;
constexpr uint32_t kUiStarted = 0x00933ee4;
constexpr uint32_t kLevelToStart = 0x0095f920;
constexpr uint32_t kCampaignLevel = 0x008dce48;

int32_t g_start_level = -1;  // pending until the UI is up

void XB_CDECL start_level() {
    if (g_start_level < 0 || title<int32_t>(kUiStarted) == 0) return;
    title<int32_t>(kLevelToStart) = g_start_level;
    title<int32_t>(kCampaignLevel) = g_start_level;
    LOG_INFO("patches", "starting mission %d", g_start_level);
    g_start_level = -1;
}

// pushad; pushfd; call start_level; popfd; popad; jmp FUN_002f1e60
void install_start_level(int32_t level) {
    if (level < 0) return;
    auto* site = reinterpret_cast<uint8_t*>(uintptr_t{kCallClientFrame});
    uint32_t relative;
    std::memcpy(&relative, site + 1, 4);
    if (site[0] != 0xE8 || kCallClientFrame + 5 + relative != kClientFrame) {
        LOG_WARN("patches", "%08x: not the call to the client frame; --level ignored", kCallClientFrame);
        return;
    }
    auto* thunk = alloc_code(64);
    if (!thunk) return;
    uint8_t* t = thunk;
    *t++ = 0x60;  // pushad
    *t++ = 0x9C;  // pushfd
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(&start_level));
    t += 5;
    *t++ = 0x9D;  // popfd
    *t++ = 0x61;  // popad
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{kClientFrame}));
    put_rel32(site, 0xE8, thunk);
    g_start_level = level;
}

// Aim assist (sticky aim): the player movement code's view angle update
// (FUN_002e5530, PM_UpdateViewAngles) calls FUN_002db7d0 every frame, __thiscall with
// the playerState in ecx, caller-cleaned. It finds an enemy under the crosshair
// (bg_stickyAimRender draws its boxes), remembers it at ps+0x500/+0x504 (target)
// and +0x508 (point on it), and turns delta_angles to follow it as it moves. Turned
// off, the function only does what it does when it finds no target:
// mov dword [ecx+0x500], 0; ret.
// Per title: the sticky aim function and its target field in the player state
// (MP FUN_0024c940, found by its callees; the field is +0x510 there).
struct AimAssist {
    uint32_t function;
    uint32_t target_offset;
};
AimAssist g_aim_assist = {0x002db7d0, 0x500};

void disable_aim_assist() {
    const uint32_t kStickyAim = g_aim_assist.function;
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{kStickyAim});
    static constexpr uint8_t kOriginal[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16
    if (std::memcmp(code, kOriginal, sizeof(kOriginal)) != 0) {
        LOG_WARN("patches", "%08x: not the sticky aim function; aim assist stays on", kStickyAim);
        return;
    }
    // mov dword [ecx+target],0; ret
    uint8_t no_target[] = {0xC7, 0x81, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0xC3};
    std::memcpy(no_target + 2, &g_aim_assist.target_offset, 4);
    std::memcpy(code, no_target, sizeof(no_target));
    LOG_INFO("patches", "aim assist off");
}

// --- In-game menus in widescreen ------------------------------------------------------
//
// In 16:9 the game lays out only its HUD for the wider picture (each HUD component by
// hand on cg_widescreen); in-game menus (pause, options, dialogs) keep their 4:3
// layout. The in-game UI's render functions (FUN_003153d0, FUN_00315440,
// FUN_003154b0, one per UI state) draw their screen (the pause menu is +0x2c, drawn
// by FUN_003153d0 while paused) and an open sub-menu (+0x30) through the object's
// vtable (+0x1c); the HUD comes from other code (its own scenes). The
// engine (NGL) queues 2D into scenes and draws them at the end of the frame
// (FUN_00064ec0) with the current scene in current_scene. Scenes come from a
// per-frame bump allocator (frame_allocator), so the scenes the menu creates are
// exactly those allocated while it renders; a screen also draws into the scene the
// caller created for it just before. The renderer narrows draws from these scenes
// (in_game_menu_draw).

// The render calls: screens "mov eax,[ecx]; add esp,0x10; call [eax+0x1c]", sub-menus
// "mov edx,[ecx]; call [edx+0x1c]", and in FUN_003154b0 the tail jump
// "mov ecx,eax; mov eax,[ecx]; jmp [eax+0x1c]".
// MP: FUN_002862d0, FUN_00286340, FUN_002863b0, render method in vtable slot 0x24.
struct MenuRenderSites {
    uint32_t screen_calls[2];
    uint32_t menu_calls[2];
    uint32_t menu_jump;
    uint8_t render_slot;        // vtable offset of the render method
    uint32_t current_scene;
    uint32_t frame_allocator;
    uint32_t engine_frame;      // counts swaps (SP FUN_00064ec0, MP FUN_00065e20)
};
constexpr MenuRenderSites kCampaignMenus = {{0x00315414, 0x00315476}, {0x00315433, 0x00315491}, 0x00315515,
                                            0x1c, 0x0090259c, 0x009025a8, 0x009028e0};
constexpr MenuRenderSites kMultiplayerMenus = {{0x00286314, 0x00286376}, {0x00286333, 0x00286391}, 0x0028640e,
                                               0x24, 0x004520fc, 0x00452108, 0x00452440};
MenuRenderSites g_menus = kCampaignMenus;

struct MenuScenes {
    uint32_t begin = 0, end = 0;  // allocated while the menu rendered
    uint32_t scene = 0;           // a screen's own scene
    uint32_t frame = UINT32_MAX;  // engine_frame then
};
MenuScenes g_menu_scenes;

void XB_CDECL menu_render_begin() {
    const uint32_t allocator = title<uint32_t>(g_menus.frame_allocator);
    g_menu_scenes = {allocator, allocator, 0, title<uint32_t>(g_menus.engine_frame)};
}

void XB_CDECL screen_render_begin() {
    menu_render_begin();
    g_menu_scenes.scene = title<uint32_t>(g_menus.current_scene);
}

void XB_CDECL menu_render_end() { g_menu_scenes.end = title<uint32_t>(g_menus.frame_allocator); }

// pushad; pushfd; call function; popfd; popad
uint8_t* put_preserving_call(uint8_t* t, void (*function)()) {
    *t++ = 0x60, *t++ = 0x9C;
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(function));
    t += 5;
    *t++ = 0x9D, *t++ = 0x61;
    return t;
}

void install_menu_render_hooks() {
    const uint8_t slot = g_menus.render_slot;
    const uint8_t screen_site[8] = {0x8B, 0x01, 0x83, 0xC4, 0x10, 0xFF, 0x50, slot};
    const uint8_t call_site[5] = {0x8B, 0x11, 0xFF, 0x52, slot};
    for (uint32_t address : g_menus.screen_calls)
        if (std::memcmp(reinterpret_cast<void*>(uintptr_t{address}), screen_site, sizeof(screen_site)) != 0) {
            LOG_WARN("patches", "%08x: not a screen render call; in-game menus stay stretched", address);
            return;
        }
    const uint8_t jump_site[7] = {0x8B, 0xC8, 0x8B, 0x01, 0xFF, 0x60, slot};
    for (uint32_t address : g_menus.menu_calls)
        if (std::memcmp(reinterpret_cast<void*>(uintptr_t{address}), call_site, sizeof(call_site)) != 0) {
            LOG_WARN("patches", "%08x: not a menu render call; in-game menus stay stretched", address);
            return;
        }
    auto* jump = reinterpret_cast<uint8_t*>(uintptr_t{g_menus.menu_jump});
    if (std::memcmp(jump, jump_site, sizeof(jump_site)) != 0) {
        LOG_WARN("patches", "%08x: not the menu render jump; in-game menus stay stretched", g_menus.menu_jump);
        return;
    }
    auto* thunks = alloc_code(128);
    if (!thunks) return;
    // Called with the menu in ecx: begin; mov edx,[ecx]; call [edx+slot]; end; ret
    auto put_thunk = [slot](uint8_t* t, void (*begin)()) {
        t = put_preserving_call(t, begin);
        *t++ = 0x8B, *t++ = 0x11, *t++ = 0xFF, *t++ = 0x52, *t++ = slot;
        t = put_preserving_call(t, &menu_render_end);
        *t++ = 0xC3;
        return t;
    };
    uint8_t* const thunk = thunks;
    uint8_t* const screen_thunk = put_thunk(thunk, &menu_render_begin);
    put_thunk(screen_thunk, &screen_render_begin);
    for (uint32_t address : g_menus.menu_calls) put_rel32(reinterpret_cast<uint8_t*>(uintptr_t{address}), 0xE8, thunk);
    for (uint32_t address : g_menus.screen_calls) {  // add esp,0x10; call thunk
        auto* site = reinterpret_cast<uint8_t*>(uintptr_t{address});
        site[0] = 0x83, site[1] = 0xC4, site[2] = 0x10;
        put_rel32(site + 3, 0xE8, screen_thunk);
    }
    // The tail jump: mov ecx,eax; jmp thunk (which returns to FUN_003154b0's caller).
    jump[0] = 0x8B, jump[1] = 0xC8;
    put_rel32(jump + 2, 0xE9, thunk);
}

// The objectives panel (shown while back is held) is cgame 2D laid out for 4:3 too,
// outside the HUD components' widescreen layout: FUN_0027e630, called only from
// FUN_00281f30 with one stack argument (its fade). It draws into the HUD's scene, so
// it gets a child scene of its own, made like the pause screen's in FUN_003153d0, and
// is narrowed around x = 50 of 640, the anchor the HUD's left-hand components use
// (FUN_00311800).
constexpr uint32_t kObjectivesCall = 0x00281f8f;
constexpr uint32_t kObjectivesDraw = 0x0027e630;
constexpr uint32_t kSceneCreate = 0x00062fc0;  // (mode) a child of the current scene, made current
constexpr uint32_t kScenePop = 0x0005e730;     // the parent becomes current
constexpr uint32_t kSceneSetClear = 0x0005e430;  // scene +0x360
constexpr uint32_t kSceneSetFlag384 = 0x0005e490;
constexpr uint32_t kSceneSetFlag385 = 0x0005e4b0;
constexpr float kObjectivesAnchor = 50.0f / 640.0f;
MenuScenes g_objectives_scenes;
bool g_objectives_scene = false;  // made one this call

template <typename... Args>
uint32_t call_title(uint32_t address, Args... args) {
    return reinterpret_cast<uint32_t(XB_CDECL*)(Args...)>(uintptr_t{address})(args...);
}

void XB_CDECL objectives_begin() {
    g_objectives_scene = title<uint32_t>(g_menus.current_scene) != 0 && call_title(kSceneCreate, 0) != 0;
    if (g_objectives_scene) {
        call_title(kSceneSetClear, 0);
        call_title(kSceneSetFlag385, 0);
        call_title(kSceneSetFlag384, 0);
    }
    g_objectives_scenes = {0, 0, g_objectives_scene ? title<uint32_t>(g_menus.current_scene) : 0,
                           title<uint32_t>(g_menus.engine_frame)};
}

void XB_CDECL objectives_end() {
    if (g_objectives_scene) call_title(kScenePop);
}

void install_objectives_hook() {
    auto* site = reinterpret_cast<uint8_t*>(uintptr_t{kObjectivesCall});
    if (site[0] != 0xE8 || site + 5 + *reinterpret_cast<int32_t*>(site + 1) !=
                               reinterpret_cast<uint8_t*>(uintptr_t{kObjectivesDraw})) {
        LOG_WARN("patches", "%08x: not the objectives draw call; objectives stay stretched", kObjectivesCall);
        return;
    }
    auto* thunk = alloc_code(64);
    if (!thunk) return;
    // begin; push dword [esp+4]; call FUN_0027e630; add esp,4; end; ret
    uint8_t* t = put_preserving_call(thunk, &objectives_begin);
    *t++ = 0xFF, *t++ = 0x74, *t++ = 0x24, *t++ = 0x04;
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(uintptr_t{kObjectivesDraw}));
    t += 5;
    *t++ = 0x83, *t++ = 0xC4, *t++ = 0x04;
    t = put_preserving_call(t, &objectives_end);
    *t++ = 0xC3;
    put_rel32(site, 0xE8, thunk);
}

// --- Multiplayer: a thread-safe game heap ---------------------------------------------
//
// codmp_xboxf.xbe's heap (dlmalloc-style, a "current heap" global at 0x00451648) has
// no lock. While a level loads, MPLevelLoader::run (FUN_00189af0, "Level pump
// thread", priority HIGHEST) pumps the network every 200 ms and allocates while the
// main thread allocates too. On the single-core Xbox the pump only cuts in now and
// then; with real parallelism the free lists got corrupted (crash in free
// FUN_00043fc0 while joining). Every heap entry point now runs under one lock.

// The original function starts with `length` bytes of position-independent
// instructions, which move to a trampoline that continues after them. Returns the
// trampoline (call it to run the original), or null if the bytes differ.
const void* hook_with_original(uint32_t address, std::span<const uint8_t> expected, const void* replacement) {
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{address});
    if (expected.size() < 5 || std::memcmp(code, expected.data(), expected.size()) != 0) {
        LOG_WARN("patches", "%08x: unexpected code; not hooked", address);
        return nullptr;
    }
    uint8_t* trampoline = alloc_code(expected.size() + 5);
    if (!trampoline) return nullptr;
    std::memcpy(trampoline, code, expected.size());
    put_rel32(trampoline + expected.size(), 0xE9, code + expected.size());
    put_rel32(code, 0xE9, replacement);
    std::memset(code + 5, 0xCC, expected.size() - 5);  // never reached: the jump leaves first
    return trampoline;
}

std::recursive_mutex g_heap_mutex;
using HeapAlloc = uint32_t(XB_CDECL*)(uint32_t);
using HeapAlignedAlloc = uint32_t(XB_CDECL*)(uint32_t, uint32_t);
using HeapFree = void(XB_CDECL*)(uint32_t);
using HeapFreeIn = void(XB_CDECL*)(uint32_t, uint32_t);
HeapAlloc g_heap_alloc;
HeapAlignedAlloc g_heap_aligned_alloc;
HeapFree g_heap_free;
HeapFreeIn g_heap_free_in;

uint32_t XB_CDECL locked_heap_alloc(uint32_t size) {
    std::lock_guard lock(g_heap_mutex);
    return g_heap_alloc(size);
}
uint32_t XB_CDECL locked_heap_aligned_alloc(uint32_t alignment, uint32_t size) {
    std::lock_guard lock(g_heap_mutex);
    return g_heap_aligned_alloc(alignment, size);
}
void XB_CDECL locked_heap_free(uint32_t pointer) {
    std::lock_guard lock(g_heap_mutex);
    g_heap_free(pointer);
}
void XB_CDECL locked_heap_free_in(uint32_t heap, uint32_t pointer) {
    std::lock_guard lock(g_heap_mutex);
    g_heap_free_in(heap, pointer);
}

void install_multiplayer_heap_lock() {
    // push ebx; mov ebx,[esp+8]
    g_heap_alloc = reinterpret_cast<HeapAlloc>(
        hook_with_original(0x00043a10, std::to_array<uint8_t>({0x53, 0x8b, 0x5c, 0x24, 0x08}), reinterpret_cast<const void*>(&locked_heap_alloc)));
    // mov eax,[0x00451638]
    g_heap_aligned_alloc = reinterpret_cast<HeapAlignedAlloc>(hook_with_original(
        0x00043850, std::to_array<uint8_t>({0xa1, 0x38, 0x16, 0x45, 0x00}), reinterpret_cast<const void*>(&locked_heap_aligned_alloc)));
    // push esi; mov esi,[esp+8]
    g_heap_free = reinterpret_cast<HeapFree>(
        hook_with_original(0x00043630, std::to_array<uint8_t>({0x56, 0x8b, 0x74, 0x24, 0x08}), reinterpret_cast<const void*>(&locked_heap_free)));
    // mov ecx,[esp+8]; mov eax,[esp+4]
    g_heap_free_in = reinterpret_cast<HeapFreeIn>(hook_with_original(
        0x00043470, std::to_array<uint8_t>({0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x44, 0x24, 0x04}), reinterpret_cast<const void*>(&locked_heap_free_in)));
}

// --- Wider than 16:9 (experimental) ----------------------------------------------------
//
// In widescreen mode (cg_widescreen) the 3D view is 16:9: R_RenderView (0x002473a9) and
// nglSetDefaultSceneParams (0x00062ac0) load the scene aspect "movss xmm0,[16/9]" from
// 0x0083f254. Pointing both at our own value widens the view (the vertical field of
// view stays: Hor+). The culling frustum needs nothing: in widescreen CG_CalcFov sets
// fov_x to 2 atan(16/9) = 121 degrees, wider than a 32:9 view at the game's fov.
// The HUD stays laid out for 16:9 and is narrowed into the middle: CG_DrawActive ends
// in "jmp CG_Draw2D" (0x00286c0c -> 0x00285f80), which goes through a thunk noting
// the current scene (RE_StretchPic adds its quad to it) and the scenes allocated
// meanwhile, like the in-game menus' (in_game_menu_draw). All NGL scenes are
// perspective ones, so the projection does not tell 2D apart.
// MP: 0x003d5240, loaded at 0x000638f0 and 0x001b4b09; jmp 0x001f71ac -> 0x001f67c0.
struct AspectSites {
    uint32_t constant;
    uint32_t loads[2];  // movss xmm0,[constant]
    uint32_t draw_2d_jump, draw_2d;
    uint32_t hud_draw_slot, hud_draw;  // 0: none
};
constexpr AspectSites kCampaignAspect = {0x0083f254, {0x00062ac0, 0x002473a9}, 0x00286c0c, 0x00285f80,
                                         0x0082a5ec, 0x00311940};
constexpr AspectSites kMultiplayerAspect = {0x003d5240, {0x000638f0, 0x001b4b09}, 0x001f71ac, 0x001f67c0, 0, 0};
float g_display_aspect = 16.0f / 9.0f;
float g_scene_aspect = 16.0f / 9.0f;  // the title reads it (32-bit address)
MenuScenes g_hud_scenes[2];  // CG_Draw2D's, the HUD manager's draw's

void scale_fixed_fov();  // Field of view, below

template <int N>
void XB_CDECL hud_render_begin() {
    if (N == 0) scale_fixed_fov();  // CG_Draw2D: cgame is drawing a level
    const uint32_t allocator = title<uint32_t>(g_menus.frame_allocator);
    g_hud_scenes[N] = {allocator, allocator, title<uint32_t>(g_menus.current_scene),
                       title<uint32_t>(g_menus.engine_frame)};
}

template <int N>
void XB_CDECL hud_render_end() {
    g_hud_scenes[N].end = title<uint32_t>(g_menus.frame_allocator);
}

bool is_hud_scene(uint32_t scene, uint32_t frame) {
    for (const MenuScenes& h : g_hud_scenes)
        if (h.frame == frame && (scene == h.scene || (scene >= h.begin && scene < h.end))) return true;
    return false;
}

// CG_DrawActive's "jmp CG_Draw2D" through a thunk (hud_render_begin<0>/end<0>); the
// wide picture and the field of view both use it, so it goes in once.
bool install_draw_2d_hook(const AspectSites& sites) {
    static bool installed = false;
    if (installed) return true;
    auto* jump = reinterpret_cast<uint8_t*>(uintptr_t{sites.draw_2d_jump});
    int32_t relative = 0;
    std::memcpy(&relative, jump + 1, 4);
    if (jump[0] != 0xE9 || sites.draw_2d_jump + 5 + relative != sites.draw_2d) {
        LOG_WARN("patches", "%08x: not the jump to CG_Draw2D", sites.draw_2d_jump);
        return false;
    }
    auto* thunk = alloc_code(32);
    if (!thunk) return false;
    // hud_render_begin; call CG_Draw2D; hud_render_end; ret
    uint8_t* t = put_preserving_call(thunk, &hud_render_begin<0>);
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(uintptr_t{sites.draw_2d}));
    t = put_preserving_call(t + 5, &hud_render_end<0>);
    *t = 0xC3;
    put_rel32(jump, 0xE9, thunk);
    installed = true;
    return true;
}

bool install_hud_scenes(const AspectSites& sites) {
    if (!install_draw_2d_hook(sites)) return false;
    if (!sites.hud_draw_slot) return true;
    // The HUD manager's draw (vtable 0x0082a5d8 slot 0x14, thiscall, no arguments; the
    // in-game UI calls it from FUN_003154b0 before CG_Draw2D) puts the compass, health
    // and the like into a scene of its own.
    auto* slot = reinterpret_cast<uint32_t*>(uintptr_t{sites.hud_draw_slot});
    if (*slot != sites.hud_draw) {
        LOG_WARN("patches", "%08x: not the HUD manager's draw", sites.hud_draw_slot);
        return false;
    }
    auto* draw = alloc_code(32);
    if (!draw) return false;
    // begin; call draw (ecx kept); end; ret
    uint8_t* t = put_preserving_call(draw, &hud_render_begin<1>);
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(uintptr_t{sites.hud_draw}));
    t = put_preserving_call(t + 5, &hud_render_end<1>);
    *t = 0xC3;
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(draw));
    return true;
}

void install_display_aspect(const AspectSites& sites, float aspect) {
    if (aspect <= 0) return;
    g_display_aspect = aspect;
    if (std::fabs(aspect - 16.0f / 9.0f) < 0.001f) return;
    for (uint32_t load : sites.loads) {
        const auto* code = reinterpret_cast<const uint8_t*>(uintptr_t{load});
        uint32_t operand = 0;
        std::memcpy(&operand, code + 4, 4);
        if (code[0] != 0xF3 || code[1] != 0x0F || code[2] != 0x10 || code[3] != 0x05 || operand != sites.constant) {
            LOG_WARN("patches", "%08x: not the scene aspect load; the view stays 16:9 (stretched)", load);
            return;
        }
    }
    if (!install_hud_scenes(sites)) {
        LOG_WARN("patches", "the view stays 16:9 (stretched; the HUD could not be kept at 16:9)");
        return;
    }
    g_scene_aspect = aspect;
    const auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_scene_aspect));
    for (uint32_t load : sites.loads) std::memcpy(reinterpret_cast<uint8_t*>(uintptr_t{load}) + 4, &address, 4);
    LOG_WARN("patches", "experimental: %.3f:1 picture (the view widens, the HUD stays 16:9 in the middle)", aspect);
}

// --- Frame rate -------------------------------------------------------------------
//
// The title paces itself on the vertical blank: its callback (0x000643e0) counts
// vblanks, and the end of each frame (FUN_00064ec0) busy-waits until the number in
// 0x008a2cb0 has passed since the last frame. FUN_000645c0(mode), cdecl, sets that
// number: 0 = none, 1, 2; modes 3/4 = 1/2 and also set 0x00902878 (a late frame does
// not wait for the next vblank). Every caller (startup twice, the movie player) asks
// for mode 2 = 30 fps. Game time comes from the clock (Com_Frame: msec from
// Com_EventLoop), so a shorter frame only means more frames, not a faster game.
//
// Movies (.xbv) are the exception: the decoder's next-frame method (vtable 0x007cf390
// slot 2, 0x000ce210, thiscall) shows one movie frame per game frame. So each frame's
// wait is chosen where it is set up - FUN_00064580 (a D3D callback inserted by the
// frame end) and 0x000649c0 both jump to 0x00064370 - with 2 vblanks while a movie
// frame was decoded in the last few frames.
//
// The wait itself is a spin on the pending flag (0x0090288c, cleared by the vblank
// callback): "cmp byte [flag],bl; jne $-6" right before D3DDevice_Swap. It is replaced
// by a call that sleeps until the next vertical blank, so the title thread leaves the
// core to the graphics driver's thread (issue #8: a dual-core laptop at 20 fps).
//
// A frame that missed its vertical blank waits for the next one with modes 1/2 (on the
// Xbox too): at 30 fps a 34 ms frame is shown at 50 ms, so a PC slightly too slow for
// 30 drops to 20. Unless late_frames_wait, the "late frames do not wait" flag of modes
// 3/4 is set as well: a late frame is shown at once and the next one counts its
// vertical blanks from there (between 20 and 30 fps instead of 20). Frames on time are
// paced as before.
// MP: the same code (FUN_00065520, 0x000652d0, decoder vtable 0x0038e830, spin
// 0x00066123 on 0x004523ec).
struct FramePacing {
    uint32_t set_frame_interval;
    uint32_t frame_vblanks;
    uint32_t late_no_wait;
    uint32_t wait_setup;
    uint32_t wait_jumps[2];
    uint32_t movie_next_frame_slot;
    uint32_t movie_next_frame;
    uint32_t frame_wait_spin;
    uint32_t frame_pending;
};
constexpr FramePacing kCampaignPacing = {0x000645c0, 0x008a2cb0, 0x00902878, 0x00064370,
                                         {0x000645ad, 0x000649d8}, 0x007cf398, 0x000ce210,
                                         0x000651c3, 0x0090288c};
constexpr FramePacing kMultiplayerPacing = {0x00065520, 0x003e8850, 0x004523d8, 0x000652d0,
                                            {0x0006550d, 0x00065938}, 0x0038e838, 0x000cf050,
                                            0x00066123, 0x004523ec};
FramePacing g_pacing = kCampaignPacing;
uint32_t g_frame_vblanks = 2;      // per frame outside movies
bool g_frame_wait = true;          // false: the title asked for mode 0 (no wait)
bool g_late_frames_wait = false;   // TitlePatchConfig::late_frames_wait
uint32_t g_movie_frame = 0;        // engine_frame at the last movie frame
bool g_movie_seen = false;
bool g_movie_pacing = false;

void XB_CDECL set_frame_interval(uint32_t mode) {
    if (mode > 4) return;
    g_frame_wait = mode != 0;
    title<uint32_t>(g_pacing.late_no_wait) = mode >= 3 || !g_late_frames_wait ? 1 : 0;
    title<uint32_t>(g_pacing.frame_vblanks) = g_frame_wait ? g_frame_vblanks : 0;
}

void XB_CDECL movie_frame() {
    g_movie_frame = title<uint32_t>(g_menus.engine_frame);
    g_movie_seen = true;
}

void XB_CDECL choose_frame_wait() {
    if (!g_frame_wait || g_frame_vblanks == 2) return;
    const bool movie = g_movie_seen && title<uint32_t>(g_menus.engine_frame) - g_movie_frame <= 2;
    if (movie != g_movie_pacing) {
        g_movie_pacing = movie;
        render::frame_rate_changed(movie ? 30 : 60 / g_frame_vblanks);
        LOG_INFO("patches", movie ? "movie: 30 fps while it plays" : "movie over: %u vertical blanks per frame again",
                 g_frame_vblanks);
    }
    title<uint32_t>(g_pacing.frame_vblanks) = movie ? 2 : g_frame_vblanks;
}

// --- Field of view ----------------------------------------------------------------------
//
// cg_fov (vmCvar 0x0094c3e0, value +8; default 70, horizontal degrees at 4:3: the game
// widens it Hor+) is set through Cvar_SetValue (0x0029fdd0, cdecl (name, float); it
// forces the change) on the title's thread at the frame end, again if a level start
// put the default back. In widescreen CG_CalcFov sets the culling fov_x to
// 2 atan(16/9) from the constants 9 (0x0083eef8) and 16 (0x0083ebb8), loaded at
// 0x00284727 and 0x00284735 ("movss xmm0,[9]" / "movss xmm0,[16]"): those loads read
// our pair instead, which stays 1 : 16/9 (the same 121 degrees) until a wide picture
// with a high fov needs more (this fov_x also places the damage direction icons).
// Campaign only for now.
struct FovSites {
    uint32_t vm_value;
    uint32_t set_value;
    uint32_t current_weapon_def;  // CG_GetCurrentWeaponDef, cdecl, eax
    uint32_t gun_fov_load;        // R_RenderView, the gun scene's "mov ecx,[ebx+0x128]" (viewParms fov_y)
    uint32_t refdef_fov_y;        // cg.refdef fov_y (CG_CalcFov)
    uint32_t zoom;                // cg zoom fraction, 0 (hip) .. 1 (aiming down sights)
    uint32_t cull_loads[2];  // movss xmm0,[a]; movss xmm0,[b]
    uint32_t cull_constants[2];
};
constexpr FovSites kCampaignFov = {0x0094c3e8, 0x0029fdd0, 0x002bc750, 0x002475de, 0x00972ba4, 0x009725d8,
                                   {0x00284727, 0x00284735}, {0x0083eef8, 0x0083ebb8}};
constexpr uint8_t kGunFovLoad[6] = {0x8B, 0x8B, 0x28, 0x01, 0x00, 0x00};  // mov ecx,[ebx+0x128]
constexpr uint32_t kWeaponFixedFov = 0x848;  // weapon def: a fov of its own (tanks, mounted guns), else 0
constexpr float kGameFov = 70.0f;           // cg_fov's default
const FovSites* g_fov = nullptr;
std::atomic<float> g_fov_wanted{0.0f};  // 0: the game's own
uint32_t g_fov_set_frame = 0;
float g_cull[2] = {1.0f, 16.0f / 9.0f};  // the title reads them (32-bit addresses)
float g_gun_fov_y = 0.0f;                 // the title reads it while the gun's own fov is on
bool g_gun_fov_ok = false;                // the gun scene's fov load is where expected
bool g_gun_fov_on = false;                // ... and reads g_gun_fov_y

void install_field_of_view(const FovSites* sites, const AspectSites& aspect_sites, float degrees) {
    if (!sites || !install_draw_2d_hook(aspect_sites)) return;
    for (int i = 0; i < 2; ++i) {
        const auto* code = reinterpret_cast<const uint8_t*>(uintptr_t{sites->cull_loads[i]});
        uint32_t operand = 0;
        std::memcpy(&operand, code + 4, 4);
        if (code[0] != 0xF3 || code[1] != 0x0F || code[2] != 0x10 || code[3] != 0x05 ||
            operand != sites->cull_constants[i]) {
            LOG_WARN("patches", "%08x: not the widescreen culling fov; no field of view setting", sites->cull_loads[i]);
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        const auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_cull[i]));
        std::memcpy(reinterpret_cast<uint8_t*>(uintptr_t{sites->cull_loads[i]}) + 4, &address, 4);
    }
    g_gun_fov_ok = std::memcmp(reinterpret_cast<const void*>(uintptr_t{sites->gun_fov_load}), kGunFovLoad, 6) == 0;
    if (!g_gun_fov_ok)
        LOG_WARN("patches", "%08x: not the gun scene's fov; the gun widens with the view", sites->gun_fov_load);
    g_fov = sites;
    if (degrees > 0) {
        g_fov_wanted = degrees;
        LOG_INFO("patches", "field of view %.0f", static_cast<double>(degrees));
    }
}

void set_gun_fov(float wanted);  // below

// On the title's thread at the frame end.
void apply_field_of_view() {
    if (!g_fov) return;
    const float current = title<float>(g_fov->vm_value);
    const float wanted = g_fov_wanted.load();
    const uint32_t frame = title<uint32_t>(g_menus.engine_frame);
    // cgame is up (its vmCvar has a value) and has had a few frames to pick up the last set.
    if (wanted > 0 && current > 0 && current != wanted && frame - g_fov_set_frame > 10) {
        static const char kName[] = "cg_fov";
        call_title(g_fov->set_value, kName, wanted);
        g_fov_set_frame = frame;
    }
    set_gun_fov(wanted);
    // The culling fov_x: wide enough for this picture at this fov, plus a margin.
    constexpr float kPi = 3.14159265f;
    const float fov = std::max(current, wanted);
    const float half_width = g_display_aspect * 0.75f * std::tan(fov * kPi / 360.0f) * 1.05f;
    g_cull[1] = std::max(16.0f / 9.0f, half_width);
}

// CG_GetViewFov returns the weapon def's own fov (+0x848) instead of cg_fov where it has
// one: a tank's view (55) or a mounted gun's. That fov is widened by as much as cg_fov
// is (in tan(fov / 2)), on the title's thread while cgame draws a level (CG_Draw2D),
// keeping the original to come back to; zoomed sights (+0x624) stay as they are.
struct ScaledFov {
    uint32_t def;
    float original, written;
};
std::vector<ScaledFov> g_scaled_fovs;

float widen_fov(float fov, float wanted) {
    constexpr float kHalfRadians = 3.14159265f / 360.0f;
    return std::atan(std::tan(fov * kHalfRadians) * std::tan(wanted * kHalfRadians) /
                     std::tan(kGameFov * kHalfRadians)) / kHalfRadians;
}

void scale_fixed_fov() {
    if (!g_fov) return;
    const float wanted = g_fov_wanted.load();
    if (wanted <= 0) return;
    const uint32_t def = call_title(g_fov->current_weapon_def);
    if (!def) return;
    float& fixed = title<float>(def + kWeaponFixedFov);
    auto it = std::find_if(g_scaled_fovs.begin(), g_scaled_fovs.end(), [def](const ScaledFov& s) { return s.def == def; });
    if (it == g_scaled_fovs.end()) {
        if (fixed <= 0) return;
        g_scaled_fovs.push_back({def, fixed, fixed});
        it = g_scaled_fovs.end() - 1;
    } else if (fixed != it->written) {
        it->original = fixed;  // a new level's def at the same place
    }
    if (it->original <= 0) return;
    const float target = wanted == kGameFov ? it->original : widen_fov(it->original, wanted);
    if (fixed != target) fixed = it->written = target;
}

// The gun (view model) has a scene of its own in R_RenderView, set up with the view's
// fov_y; at a wider fov it shrinks and shows what the game's 70 keeps off screen (the
// front hand under the rifle). Off the default, that scene's fov load
// "mov ecx,[ebx+0x128]" becomes "mov ecx,[g_gun_fov_y]": at the hip the view's fov_y
// narrowed back by the fov's factor (in tan(fov / 2)), aiming down sights the view's own
// (zoomed sights are not widened), blended by the zoom fraction. It follows the view a
// frame late (computed at the frame end), so at the default the original load is back.
void set_gun_fov(float wanted) {
    if (!g_gun_fov_ok) return;
    const bool on = wanted > 0 && wanted != kGameFov;
    if (on) {
        constexpr float kHalfRadians = 3.14159265f / 360.0f;
        const float factor = std::tan(wanted * kHalfRadians) / std::tan(kGameFov * kHalfRadians);
        const float zoom = std::clamp(title<float>(g_fov->zoom), 0.0f, 1.0f);
        const float view = std::tan(title<float>(g_fov->refdef_fov_y) * kHalfRadians);
        g_gun_fov_y = std::atan(view / factor * (1.0f - zoom) + view * zoom) / kHalfRadians;
    }
    if (on == g_gun_fov_on) return;
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{g_fov->gun_fov_load});
    if (on) {
        const auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_gun_fov_y));
        code[0] = 0x8B, code[1] = 0x0D;  // mov ecx,[address]
        std::memcpy(code + 2, &address, 4);
    } else {
        std::memcpy(code, kGunFovLoad, 6);
    }
    g_gun_fov_on = on;
}

void XB_CDECL wait_frame_start() {
    apply_field_of_view();
    const auto& pending = *reinterpret_cast<volatile uint8_t*>(uintptr_t{g_pacing.frame_pending});
    for (;;) {
        const uint32_t seen = render::vertical_blanks_done();
        if (!pending) return;
        render::wait_vertical_blank_after(seen, 50);
    }
}

void install_frame_wait() {
    auto* site = reinterpret_cast<uint8_t*>(uintptr_t{g_pacing.frame_wait_spin});
    uint8_t spin[8] = {0x38, 0x1D, 0, 0, 0, 0, 0x75, 0xF8};  // cmp [pending],bl; jne $-6
    std::memcpy(spin + 2, &g_pacing.frame_pending, 4);
    if (std::memcmp(site, spin, sizeof(spin)) != 0) {
        LOG_WARN("patches", "%08x: not the frame end's wait; it spins", g_pacing.frame_wait_spin);
        return;
    }
    auto* thunk = alloc_code(16);
    if (!thunk) return;
    *put_preserving_call(thunk, &wait_frame_start) = 0xC3;
    put_rel32(site, 0xE8, thunk);
    std::memset(site + 5, 0x90, 3);
}

void install_frame_rate(int32_t frame_rate, bool late_frames_wait) {
    g_frame_vblanks = frame_rate == 60 ? 1 : 2;
    g_late_frames_wait = late_frames_wait;
    install_frame_wait();
    install_jump(g_pacing.set_frame_interval, reinterpret_cast<const void*>(&set_frame_interval));
    // MP sets the interval only in its intro movie player (FUN_0029ab90, mode 2, never
    // reset); before that its static 1 applies. Start at the chosen rate either way.
    title<uint32_t>(g_pacing.frame_vblanks) = g_frame_vblanks;
    title<uint32_t>(g_pacing.late_no_wait) = late_frames_wait ? 0 : 1;
    LOG_INFO("patches", "%d fps (%u vertical blanks per frame)%s", frame_rate, g_frame_vblanks,
             late_frames_wait ? "; late frames wait for the next one" : "");
    if (g_frame_vblanks == 2) return;

    auto* slot = reinterpret_cast<uint32_t*>(uintptr_t{g_pacing.movie_next_frame_slot});
    for (uint32_t address : g_pacing.wait_jumps) {
        auto* site = reinterpret_cast<uint8_t*>(uintptr_t{address});
        if (site[0] != 0xE9 || site + 5 + *reinterpret_cast<int32_t*>(site + 1) !=
                                   reinterpret_cast<uint8_t*>(uintptr_t{g_pacing.wait_setup})) {
            LOG_WARN("patches", "%08x: not the frame wait jump; movies play too fast", address);
            return;
        }
    }
    if (*slot != g_pacing.movie_next_frame) {
        LOG_WARN("patches", "%08x: not the movie decoder's next-frame slot; movies play too fast", g_pacing.movie_next_frame_slot);
        return;
    }
    auto* thunks = alloc_code(64);
    if (!thunks) return;
    // choose_frame_wait; jmp 0x00064370
    uint8_t* t = put_preserving_call(thunks, &choose_frame_wait);
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{g_pacing.wait_setup}));
    for (uint32_t address : g_pacing.wait_jumps) put_rel32(reinterpret_cast<uint8_t*>(uintptr_t{address}), 0xE9, thunks);
    // movie_frame; jmp 0x000ce210 (ecx = the decoder, kept by the preserving call)
    uint8_t* const movie_thunk = t + 5;
    t = put_preserving_call(movie_thunk, &movie_frame);
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{g_pacing.movie_next_frame}));
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(movie_thunk));
}

// --- Menus with the mouse --------------------------------------------------------------
//
// The UI is run by managers - front end 0x0095f8f8 (null in a level), in-game 0x0095f8fc
// (pause menu; the HUD is a screen without items), dialogs 0x0095f900 - each with
// screens (+4 array, +0xc the active one) that are menus of items. Every frame each
// manager polls the pad (FUN_0031fc10, vtable +0x14, ecx = manager) and sends the first
// new press to its active screen (D-pad: move the selection, A: activate it).
// Screen: +4 items, +0x22 selected index (int16), +0x26 item count (int16), +0x30 set
// while it is locked (animating), +0x31 plays the move sound; vtable +0x10 the move
// sound, +0x88 select(index, 1) (thiscall). Item: +0x8/+0xa/+0xc/+0xe the neighbour
// up/down/left/right (int16, -1 none), +0x10 its text widget, +0x16 disabled.
// Text widget (vtable 0x008298b8, draw FUN_00321160): +0x10 bit 2 visible, +0x2c x and
// +0x30 y in 640x480, +0x5c alignment (0x10 left, 0x20 right, else centred; 0x40 top,
// 0x80 bottom, else centred; FUN_0030fa60); vtable +0xd0 width, +0xd4 height.
//
// The poll goes through a thunk that lets the mouse point while the manager's active
// screen has items on screen (their widgets were drawn): hovering selects an item, a
// left click selects it and taps A, a right click taps B, the wheel taps up/down.
// Pickers that step left/right through pictures (difficulty: arrows around a chevron;
// their items' text is not what is shown) have no boxes: a click left or right of the
// middle taps left/right, in the middle A, and the wheel steps left/right.
// Selecting waits while the screen is locked (+0x30). Dialogs are drawn over the other
// screens and take the mouse while theirs are shown. The front end keeps the cursor on
// screens without items too (the title screen), where a click taps A, and so does the
// in-game manager's screen after a level loads ("Press A to start the game", vtable
// 0x008295a0, in-game screen 5) once it is ready (+0x38; its A handler 0x00335b10
// checks the same).
struct MenuPointerSites {
    uint32_t screen_poll;
    std::array<uint8_t, 6> poll_prologue;
    uint32_t text_draw;
    std::array<uint8_t, 8> draw_prologue;
    uint8_t draw_prologue_length;
    uint32_t text_widget_vtable;
    uint32_t front_end, dialogs;   // manager pointers
    uint8_t sound_slot, select_slot;  // screen vtable
    uint32_t press_start_vtable;      // the screen after loading a level (0: none)
};
// MP: the same classes with longer screen and item vtables (FUN_0028d080, FUN_0028e5d0).
constexpr MenuPointerSites kCampaignMenuPointer = {
    0x0031fc10, {0x53, 0x55, 0x56, 0x57, 0x8b, 0xe9},  // push ebx; push ebp; push esi; push edi; mov ebp,ecx
    0x00321160, {0x83, 0xec, 0x10, 0x53, 0x55}, 5,      // sub esp,0x10; push ebx; push ebp
    0x008298b8, 0x0095f8f8, 0x0095f900, 0x10, 0x88, 0x008295a0};
constexpr MenuPointerSites kMultiplayerMenuPointer = {
    0x0028d080, {0x53, 0x55, 0x56, 0x57, 0x8b, 0xe9},
    0x0028e5d0, {0x81, 0xec, 0x20, 0x01, 0x00, 0x00, 0x53, 0x55}, 8,  // sub esp,0x120; push ebx; push ebp
    0x003bc8f8, 0x004b31f8, 0x004b3200, 0x18, 0x98, 0};
MenuPointerSites g_menu_pointer = kCampaignMenuPointer;

std::mutex g_drawn_mutex;
std::unordered_map<uint32_t, uint32_t> g_text_drawn;  // text widget -> engine frame of its last draw
uint32_t g_dialog_frame = 0;                          // engine frame a dialog's items were last shown
bool g_dialog_shown = false;

uint32_t engine_frame() { return title<uint32_t>(g_menus.engine_frame); }

void XB_CDECL text_drawn(uint32_t widget) {
    if (!(title<uint8_t>(widget + 0x10) & 4)) return;  // hidden: the draw does nothing
    const uint32_t frame = engine_frame();
    std::lock_guard lock(g_drawn_mutex);
    if (g_text_drawn.size() > 1024) std::erase_if(g_text_drawn, [frame](const auto& e) { return frame - e.second > 2; });
    g_text_drawn[widget] = frame;
}

bool drawn_lately(uint32_t widget, uint32_t frame) {
    std::lock_guard lock(g_drawn_mutex);
    const auto it = g_text_drawn.find(widget);
    return it != g_text_drawn.end() && frame - it->second <= 2;  // the poll runs before this frame's draws
}

struct ItemBox {
    int16_t index;
    float left, top, right, bottom;
    uint32_t widget = 0;
};

// A text widget's box in 640x480, with a little around the letters. False if it has
// no text.
bool widget_box(uint32_t widget, ItemBox& box) {
    using FloatMethod = float(XB_FASTCALL*)(uint32_t);
    const float width = reinterpret_cast<FloatMethod>(uintptr_t{title<uint32_t>(g_menu_pointer.text_widget_vtable + 0xd0)})(widget);
    const float height = reinterpret_cast<FloatMethod>(uintptr_t{title<uint32_t>(g_menu_pointer.text_widget_vtable + 0xd4)})(widget);
    if (!(width > 0.0f && height > 0.0f)) return false;
    const uint8_t align = title<uint8_t>(widget + 0x5c);
    float left = title<float>(widget + 0x2c), top = title<float>(widget + 0x30);
    left -= (align & 0x20) ? width : (align & 0x10) ? 0.0f : width / 2;
    top -= (align & 0x80) ? height : (align & 0x40) ? 0.0f : height / 2;
    constexpr float kMargin = 3.0f;
    box.left = left - kMargin, box.top = top - kMargin, box.right = left + width + kMargin, box.bottom = top + height + kMargin;
    box.widget = widget;
    return true;
}

// The active screen's enabled items that are on screen, with their text's box.
std::vector<ItemBox> shown_items(uint32_t menu) {
    std::vector<ItemBox> boxes;
    const uint32_t frame = engine_frame();
    const int16_t count = title<int16_t>(menu + 0x26);
    const uint32_t items = title<uint32_t>(menu + 4);
    for (int16_t i = 0; items && i < count; ++i) {
        const uint32_t item = title<uint32_t>(items + i * 4u);
        if (!item || title<uint8_t>(item + 0x16)) continue;
        const uint32_t widget = title<uint32_t>(item + 0x10);
        if (!widget || title<uint32_t>(widget) != g_menu_pointer.text_widget_vtable || !drawn_lately(widget, frame)) continue;
        ItemBox box{i, 0, 0, 0, 0};
        if (widget_box(widget, box)) boxes.push_back(box);
    }
    return boxes;
}

// Option rows ("Invert Aim  < off >", sliders) show their value right of the label,
// changed with left/right. The x of the value's middle: the middle of the texts drawn
// on the item's line right of its label (the value and, in the front end, its arrows),
// else that of the other rows whose labels start in the same column (sliders have no
// text), else for a label in the left half halfway to the screen's right edge (a
// lone slider); negative for none.
float value_middle(const std::vector<ItemBox>& items, const ItemBox& row) {
    std::vector<uint32_t> drawn;
    const uint32_t frame = engine_frame();
    {
        std::lock_guard lock(g_drawn_mutex);
        for (const auto& [widget, at] : g_text_drawn)
            if (frame - at <= 2) drawn.push_back(widget);
    }
    float own = -1, others = 0;
    int other_count = 0;
    for (const ItemBox& item : items) {
        if (item.index != row.index && std::abs(item.left - row.left) >= 4.0f) continue;  // another column
        const float middle = (item.top + item.bottom) / 2;
        float left = 1e9f, right = -1e9f;
        for (uint32_t widget : drawn) {
            ItemBox text{};
            if (title<uint32_t>(widget) != g_menu_pointer.text_widget_vtable || !widget_box(widget, text)) continue;
            const float text_middle = (text.top + text.bottom) / 2;
            if (std::abs(text_middle - middle) > (item.bottom - item.top) / 2 || text.left < item.right) continue;
            left = std::min(left, text.left);
            right = std::max(right, text.right);
        }
        if (right < left) continue;
        if (item.index == row.index)
            own = (left + right) / 2;
        else
            others += (left + right) / 2, ++other_count;
    }
    if (own >= 0) return own;
    if (other_count) return others / other_count;
    constexpr float kScreenMiddle = 320.0f, kScreenRight = 640.0f;
    if (row.right > kScreenMiddle) return -1.0f;  // a menu item, not an option's label
    return (row.right + kScreenRight) / 2;
}

// Items linked only left/right: a picker.
bool horizontal(uint32_t menu) {
    bool sideways = false;
    const uint32_t items = title<uint32_t>(menu + 4);
    for (int16_t i = 0; items && i < title<int16_t>(menu + 0x26); ++i) {
        const uint32_t item = title<uint32_t>(items + i * 4u);
        if (!item) continue;
        if (title<int16_t>(item + 0x8) >= 0 || title<int16_t>(item + 0xa) >= 0) return false;
        sideways = sideways || title<int16_t>(item + 0xc) >= 0 || title<int16_t>(item + 0xe) >= 0;
    }
    return sideways;
}

void tap_button(input::AnalogButton button) {
    input::PadState pad;
    pad.analog_buttons[button] = 0xFF;
    input::tap(pad);
}

void tap_digital(uint16_t button) {
    input::PadState pad;
    pad.buttons = button;
    input::tap(pad);
}

void XB_CDECL menu_pointer(uint32_t manager) {
    const uint32_t screens = title<uint32_t>(manager + 4);
    const int32_t active = title<int32_t>(manager + 0xc);
    const uint32_t menu = screens && active >= 0 ? title<uint32_t>(screens + active * 4u) : 0;
    const std::vector<ItemBox> items = menu ? shown_items(menu) : std::vector<ItemBox>{};
    const uint32_t frame = engine_frame();
    if (g_menu_pointer.dialogs && manager == title<uint32_t>(g_menu_pointer.dialogs)) {
        if (items.empty()) return;
        g_dialog_frame = frame;
        g_dialog_shown = true;
    } else if (g_dialog_shown && frame - g_dialog_frame <= 2) {
        return;  // a dialog over this screen has the mouse
    } else if (items.empty() && (!g_menu_pointer.front_end || manager != title<uint32_t>(g_menu_pointer.front_end)) &&
               !(menu && g_menu_pointer.press_start_vtable && title<uint32_t>(menu) == g_menu_pointer.press_start_vtable &&
                 title<uint8_t>(menu + 0x38))) {
        return;  // in a level without a menu
    }

    input::set_menu_pointer();
    input::PointerState pointer = input::take_pointer();
    float x = 0, y = 0;
    if (!pointer.inside || !render::window_to_screen(pointer.x, pointer.y, x, y)) return;
    // A click that could not select its item yet (the screen was locked) waits a little.
    static int held_clicks = 0;
    static uint32_t held_since = 0;
    const bool was_held = held_clicks && frame - held_since <= 30;
    if (was_held) pointer.left_clicks += held_clicks;
    held_clicks = 0;
    // Text boxes of close lines overlap: the nearest line's.
    const ItemBox* hit = nullptr;
    const auto nearer = [&](const ItemBox& box) {
        return !hit || std::abs(y - (box.top + box.bottom) / 2) < std::abs(y - (hit->top + hit->bottom) / 2);
    };
    for (const ItemBox& box : items)
        if (x >= box.left && x < box.right && y >= box.top && y < box.bottom && nearer(box)) hit = &box;
    // Right of a label on its line: the row's value (arrows, a slider).
    float value_x = -1;
    if (!hit) {
        for (const ItemBox& box : items)
            if (x >= box.right && y >= box.top && y < box.bottom && nearer(box)) hit = &box;
        if (hit && (value_x = value_middle(items, *hit)) < 0) hit = nullptr;
    }
    const bool on_item = hit != nullptr;
    if (on_item && title<uint8_t>(menu + 0x30) == 0 && (pointer.moved || pointer.left_clicks) && hit->index != title<int16_t>(menu + 0x22)) {
        using Method = void(XB_FASTCALL*)(uint32_t);
        using Select = void(XB_FASTCALL*)(uint32_t, uint32_t, int32_t, int32_t);  // thiscall (index, 1)
        const uint32_t vtable = title<uint32_t>(menu);
        if (title<uint8_t>(menu + 0x31))
            reinterpret_cast<Method>(uintptr_t{title<uint32_t>(vtable + g_menu_pointer.sound_slot)})(menu);
        reinterpret_cast<Select>(uintptr_t{title<uint32_t>(vtable + g_menu_pointer.select_slot)})(menu, 0, hit->index, 1);
    }
    if (pointer.left_clicks && on_item && hit->index != title<int16_t>(menu + 0x22)) {
        if (!was_held) held_since = frame;
        held_clicks = pointer.left_clicks;
        LOG_DEBUG("input", "menu click on item %d held (selected %d, locked %d)", hit->index,
                  title<int16_t>(menu + 0x22), title<uint8_t>(menu + 0x30));
        pointer.left_clicks = 0;
    }
    if (pointer.left_clicks && value_x >= 0)
        LOG_DEBUG("input", "menu click at %.0f,%.0f: on a value (its middle at x %.0f)", x, y, value_x);
    else if (pointer.left_clicks)
        LOG_DEBUG("input", "menu click at %.0f,%.0f: %s", x, y,
                  on_item ? "on an item" : items.empty() ? "screen without items" : "beside the items");
    if (pointer.left_clicks && value_x >= 0) {  // left or right of the value's middle steps it
        constexpr float kValueMiddle = 10.0f;
        tap_digital(x < value_x - kValueMiddle ? input::kDpadLeft : input::kDpadRight);
        return;
    }
    const bool no_items = !menu || title<int16_t>(menu + 0x26) <= 0;
    const bool picker = menu && horizontal(menu);
    constexpr float kPickerMiddle = 64.0f;  // half the middle's width, around x = 320
    if (pointer.left_clicks && picker && items.empty() && std::abs(x - 320.0f) > kPickerMiddle)
        tap_digital(x < 320.0f ? input::kDpadLeft : input::kDpadRight);
    else if (pointer.left_clicks && (on_item || no_items || (picker && items.empty())))
        tap_button(input::kA);
    else if (pointer.right_clicks)
        tap_button(input::kB);
    else if (pointer.wheel && picker)
        tap_digital(pointer.wheel > 0 ? input::kDpadLeft : input::kDpadRight);
    else if (pointer.wheel)
        tap_digital(pointer.wheel > 0 ? input::kDpadUp : input::kDpadDown);
}

// pushad; pushfd; push ecx; call function; add esp,4; popfd; popad; jmp original
uint8_t* put_hook_thunk(uint8_t* t, void (XB_CDECL* function)(uint32_t), uint32_t address,
                        std::span<const uint8_t> prologue) {
    uint8_t* const thunk = t;
    *t++ = 0x60, *t++ = 0x9C, *t++ = 0x51;
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(function));
    t += 5;
    *t++ = 0x83, *t++ = 0xC4, *t++ = 0x04, *t++ = 0x9D, *t++ = 0x61;
    const void* original = hook_with_original(address, prologue, thunk);
    if (!original) return nullptr;
    put_rel32(t, 0xE9, original);
    return t + 5;
}

void install_menu_pointer() {
    const MenuPointerSites& sites = g_menu_pointer;
    auto* thunks = alloc_code(64);
    if (!thunks) return;
    uint8_t* t = put_hook_thunk(thunks, &text_drawn, sites.text_draw,
                                std::span(sites.draw_prologue.data(), sites.draw_prologue_length));
    if (!t || !put_hook_thunk(t, &menu_pointer, sites.screen_poll, sites.poll_prologue))
        LOG_WARN("patches", "no mouse in the menus");
}

}  // namespace

void set_field_of_view(float degrees) { g_fov_wanted = degrees; }
float field_of_view() { return g_fov_wanted.load(); }
float game_field_of_view() { return g_fov ? title<float>(g_fov->vm_value) : 0.0f; }
bool field_of_view_supported() { return g_fov != nullptr; }

render::UiNarrowing in_game_menu_draw() {
    const uint32_t scene = title<uint32_t>(g_menus.current_scene);
    const MenuScenes& m = g_menu_scenes;
    const MenuScenes& o = g_objectives_scenes;
    const uint32_t frame = title<uint32_t>(g_menus.engine_frame);
    const float menu = 4.0f / 3.0f / g_display_aspect;
    const float hud = std::min(1.0f, 16.0f / 9.0f / g_display_aspect);  // 16:9 layout into the picture
    if (m.frame == frame && (scene == m.scene || (scene >= m.begin && scene < m.end))) return {0.5f, menu};
    if (o.frame == frame && o.scene != 0 && scene == o.scene) {
        // 4:3 around the HUD's left anchor within the 16:9 layout (k1), that layout
        // around the centre (k2): one narrowing by k1 k2 around the point both keep.
        const float k1 = 0.75f, k2 = hud, a = kObjectivesAnchor;
        return {(0.5f * (1.0f - k2) + k2 * a * (1.0f - k1)) / (1.0f - k1 * k2), k1 * k2};
    }
    if (hud < 1.0f && is_hud_scene(scene, frame)) return {0.5f, hud};
    return {};
}

namespace {

struct SdkLibrary {
    const char* name;
    const char* section;  // XBE section holding the original library
    std::span<const xbox::KernelImpl> functions;
    bool partial = false;  // titles link only the functions they use: missing ones are normal
};

// SDK functions XbSymbolDatabase leaves unnamed in this title, by their XDK name.
struct SdkAlias {
    uint32_t address;
    const char* name;
};
const SdkAlias kCampaignSdkAliases[] = {
    {0x003e6210, "D3DDevice_BlockOnFence"},  // BlockOnTime(fence, 4)
    {0x003e6ea0, "D3DDevice_LoadVertexShaderProgram"},
    {0x003e5f20, "D3DDevice_GetTexture2"},
    {0x003e6200, "D3DDevice_BlockUntilIdle"},
    {0x003e6950, "D3DDevice_GetScreenSpaceOffset"},
    {0x00379ebb, "XOnlineSilentLogon"},  // GetUsers, first account, Logon; 0x8015108x errors
};
// The same functions in codmp_xboxf.xbe (identical code, scripts/match_hashes.py).
const SdkAlias kMultiplayerSdkAliases[] = {
    {0x00318250, "D3DDevice_BlockOnFence"},
    {0x00318ee0, "D3DDevice_LoadVertexShaderProgram"},
    {0x00317f60, "D3DDevice_GetTexture2"},
    {0x00318240, "D3DDevice_BlockUntilIdle"},
    {0x00318990, "D3DDevice_GetScreenSpaceOffset"},
};

// SDK functions that are safe to run as they are (no hardware access), left untrapped.
const uint32_t kCampaignSdkKeepOriginal[] = {
    0x003ebb20,  // D3D: returns 1 (called before D3D_SetPushBufferSize)
    0x00396f5f,  // XONLINE: a static initializer (_cinit)
};
const uint32_t kMultiplayerSdkKeepOriginal[] = {
    0x0031db60,  // D3D: returns 1
    0x002db076,  // XONLINE: the static initializer
};

// The same, by SDK name (any title): thin wrappers around XAPI.
const char* const kSdkKeepOriginalNames[] = {
    "D3D_AllocContiguousMemory",  // XMemAlloc with D3D's attributes
    "D3D_FreeContiguousMemory",   // XMemFree
    "D3DDevice_GetCreationParameters",  // constants plus a flag from the device block
};

// XAPI XLaunchNewImageA calls the first non-null entry of a launch hook table
// (static data) instead of its own path. XONLINE puts its launch there (SP
// FUN_0037da38 at 0x0089790c): the same plus Xbox Live dashboard handling and
// cleanup. With the table cleared, XAPI writes the LaunchDataPage and calls
// HalReturnToFirmware itself.
struct TitleData {
    TitleInfo info;
    std::span<const Patch> patches;
    std::span<const RdtscSite> rdtsc_sites;
    std::span<const SdkAlias> sdk_aliases;
    std::span<const uint32_t> sdk_keep_original;
    uint32_t launch_hook_table, launch_hook_table_end;
    uint32_t online_pointer;  // the title's Xbox Live state object (see start_sign_in)
};

const TitleData kTitles[] = {
    {{TitleId::Campaign, "default.xbe", "default_xbe_symbols.txt", 0x003f8318, 0x003f8328, 0x003f5c68, 0x007a9f78,
      0x433a25fa, 0x00990d80},
     kCampaignPatches, kCampaignRdtscSites, kCampaignSdkAliases, kCampaignSdkKeepOriginal, 0x00897908, 0x00897910,
     0x0095f6fc},
    {{TitleId::Multiplayer, "codmp_xboxf.xbe", "codmp_xbe_symbols.txt", 0x0032a358, 0x0032a368, 0x00327ca8,
      0x003693d8, 0x433a2612, 0x00503a80},
     kMultiplayerPatches, kMultiplayerRdtscSites, kMultiplayerSdkAliases, kMultiplayerSdkKeepOriginal, 0x003dd49c,
     0x003dd4a4, 0x004b2fec},
};

const TitleData& title_data(const TitleInfo& info) {
    for (const TitleData& data : kTitles)
        if (&data.info == &info) return data;
    std::abort();  // only find_title hands out TitleInfo
}

const SymbolTable* g_symbols = nullptr;

// Every function in a replaced library's section that we do not replace starts with
// "call sdk_trap", so reaching original SDK code (it would touch hardware) stops with
// its name. The return address is the trapped function's entry + 5; the caller's
// return address is the next stack slot.
[[noreturn]] PLATFORM_NOINLINE void XB_CDECL sdk_trap() {
    const auto* frame = static_cast<uint32_t*>(__builtin_frame_address(0));
    const uint32_t function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0))) - 5;
    const uint32_t caller = frame[2];  // [ebp+8]: above our return address
    char message[200];
    std::snprintf(message, sizeof(message), "unimplemented SDK function %s",
                  g_symbols->describe(function).c_str());
    fatal_with_backtrace(message, caller, frame[0]);
}

void install_call(uint32_t address, const void* target) {
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{address});
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)) - (address + 5);
    code[0] = 0xE8;  // CALL rel32
    std::memcpy(code + 1, &relative, 4);
}

}  // namespace

const TitleInfo* find_title(std::string_view xbe_name) {
    for (const TitleData& data : kTitles)
        if (strcasecmp(std::string(xbe_name).c_str(), data.info.xbe_name) == 0) return &data.info;
    return nullptr;
}

bool install_sdk_replacements(const XbeImage& image, const SymbolTable& symbols, const TitleInfo& title) {
    const TitleData& data = title_data(title);
    g_symbols = &symbols;
    const SdkLibrary libraries[] = {
        {"DirectSound", "DSOUND", audio::dsound_functions()},
        {"Direct3D", "D3D", render::d3d_functions()},
        {"Input", "XPP", input::xpp_functions()},
        {"Xbox Live", "XONLINE", online::xonline_functions()},
        {"Network", "XNET", online::xnet_functions(), true},
    };
    bool ok = true;
    for (const SdkLibrary& library : libraries) {
        const XbeSectionHeader* section = nullptr;
        for (const XbeSectionHeader* s : image.sections())
            if (image.section_name(*s) == library.section) section = s;
        if (!section) continue;  // the title does not use this library

        // Trap first; replacements then overwrite the entries they own. Functions
        // shorter than a CALL are left alone (they would clobber their neighbour).
        const std::vector<uint32_t> entries =
            symbols.in_range(section->virtual_addr, section->virtual_addr + section->virtual_size);
        std::vector<uint32_t> keep(data.sdk_keep_original.begin(), data.sdk_keep_original.end());
        for (const char* name : kSdkKeepOriginalNames)
            for (uint32_t address : symbols.find(name)) keep.push_back(address);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (std::find(keep.begin(), keep.end(), entries[i]) != keep.end()) continue;
            const uint32_t next = i + 1 < entries.size() ? entries[i + 1] : section->virtual_addr + section->virtual_size;
            if (next - entries[i] >= 5) install_call(entries[i], reinterpret_cast<const void*>(&sdk_trap));
        }

        int installed = 0, missing = 0;
        for (const xbox::KernelImpl& function : library.functions) {
            std::vector<uint32_t> addresses = symbols.find(function.name);
            for (const SdkAlias& alias : data.sdk_aliases)
                if (std::strcmp(alias.name, function.name) == 0) addresses.push_back(alias.address);
            if (addresses.empty()) {
                if (library.partial)
                    LOG_DEBUG("patches", "%s: %s not linked into this title", library.name, function.name);
                else
                    LOG_WARN("patches", "%s: %s not found in the symbols; not replaced", library.name, function.name);
                ++missing;
                continue;
            }
            for (uint32_t address : addresses) install_jump(address, function.address);
            ++installed;
        }
        LOG_INFO("patches", "%s: %d functions replaced, %d not found, %zu others trapped", library.name, installed,
                 missing, entries.size() - installed);
        if (installed == 0 && !library.functions.empty()) ok = false;
    }
    return ok;
}

void install_title_patches(const TitleInfo& title, const TitlePatchConfig& config) {
    const TitleData& data = title_data(title);
    g_online_pointer = data.online_pointer;
    for (const Patch& patch : data.patches) {
        install_jump(patch.address, patch.replacement);
        LOG_DEBUG("patches", "%08x: %s", patch.address, patch.what);
    }
    std::memset(reinterpret_cast<void*>(uintptr_t{data.launch_hook_table}), 0,
                data.launch_hook_table_end - data.launch_hook_table);
    install_rdtsc_patches(data.rdtsc_sites);
    g_mouse_degrees_per_count = config.mouse_degrees_per_count;
    g_client = title.id == TitleId::Campaign ? kCampaignClient : kMultiplayerClient;
    install_mouse_look();
    input::set_action_remap(&remap_keyboard_actions);
    g_button_prompts = config.button_prompts;
    const bool campaign = title.id == TitleId::Campaign;
    g_aim_assist = campaign ? AimAssist{0x002db7d0, 0x500} : AimAssist{0x0024c940, 0x510};
    g_menus = campaign ? kCampaignMenus : kMultiplayerMenus;
    g_pacing = campaign ? kCampaignPacing : kMultiplayerPacing;
    if (!config.aim_assist) disable_aim_assist();
    install_frame_rate(config.frame_rate, config.late_frames_wait);
    install_display_aspect(campaign ? kCampaignAspect : kMultiplayerAspect, config.display_aspect);
    install_field_of_view(campaign ? &kCampaignFov : nullptr, kCampaignAspect, config.field_of_view);
    install_menu_render_hooks();
    g_menu_pointer = campaign ? kCampaignMenuPointer : kMultiplayerMenuPointer;
    install_menu_pointer();
    if (title.id == TitleId::Multiplayer) install_multiplayer_heap_lock();
    if (!campaign) return;  // the objectives panel and --level are campaign-only

    install_start_level(config.start_level);
    install_objectives_hook();
}

}  // namespace loader
