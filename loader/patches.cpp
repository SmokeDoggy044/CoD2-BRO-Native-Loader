// Title-specific patches: original functions that cannot run on a PC, replaced or
// turned into no-ops. Addresses are for CoD2: BRO default.xbe (XDK 5849).

#include "patches.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "compiler.h"
#include "crash.h"
#include "devices.h"
#include "filesystem.h"
#include "float_env.h"
#include "hooks.h"
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

// XAPI 001ede5f (unnamed in Ghidra), called during startup on retail (no debug
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

// Console command load_mp_exe (FUN_002cf920): shuts the game down and reboots into
// codmp_xboxf.xbe. The Multiplayer and Xbox Live menus run it; multiplayer is not
// ported, so the player stays in the menu and gets a notice instead.
void XB_CDECL load_mp_exe() {
    LOG_INFO("patches", "load_mp_exe: multiplayer is not available");
    render::show_notice({"Multiplayer is not available", "This port plays the single-player campaign only."}, 4);
}

// Xbox Live sign-in (FUN_00221eb0, from the main menu and Xbox Live Options > Sign In;
// online state in esi, always the object at 0x0095f6fc): clears the last logon result,
// sets the state to 1 (signing in) and starts the XOnline sign-in UI, which draws its
// account list with XFONT into the frame buffer's tiled view (0xF0000000, not mapped).
// There is no Xbox Live: keep the state at 0, so the menu says "Not signed in".
constexpr uint32_t kOnlineState = 0x0095f6fc;

void XB_CDECL start_sign_in() {
    const auto online = *reinterpret_cast<const uint32_t*>(uintptr_t{kOnlineState});
    if (online) {
        *reinterpret_cast<uint32_t*>(uintptr_t{online + 0x446c}) = 0;  // logon result
        *reinterpret_cast<uint32_t*>(uintptr_t{online + 0x4470}) = 0;
    }
    LOG_INFO("patches", "Xbox Live sign-in: not available");
    render::show_notice({"Xbox Live is not available", "This port plays the single-player campaign only."}, 4);
}

struct Patch {
    uint32_t address;
    const void* replacement;
    const char* what;
};

const Patch kPatches[] = {
    {0x001ede5f, reinterpret_cast<const void*>(&restrict_code_selector), "XAPI code selector restriction"},
    {0x001e9b66, reinterpret_cast<const void*>(&mount_utility_drive), "XMountUtilityDrive: Z: = cache folder"},
    {0x002cf920, reinterpret_cast<const void*>(&load_mp_exe), "load_mp_exe: no multiplayer reboot"},
    {0x00221eb0, reinterpret_cast<const void*>(&start_sign_in), "Xbox Live sign-in: not available"},
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
const RdtscSite kRdtscSites[] = {
    {0x000643ad, 6}, {0x0006441a, 6}, {0x00064584, 6}, {0x0006469e, 6}, {0x00064f2e, 6}, {0x00064f98, 6},
    {0x00064fe8, 6}, {0x00065139, 6}, {0x000651ea, 6}, {0x00065329, 6},
    {0x001ed6ce, 7},  // XAPI QueryPerformanceCounter: mov [ecx],eax; mov [ecx+4],edx
    {0x001fda73, 6}, {0x0020d34a, 6}, {0x0020d37e, 6}, {0x002960ba, 6}, {0x002960e3, 6}, {0x00298185, 6},
    {0x002981ae, 6}, {0x002b1caf, 6}, {0x002b1ce9, 6}, {0x0030ba24, 6}, {0x0030bc51, 6},
    {0x0032d893, 5},  // mov [esp],eax
    {0x0032ebca, 6}, {0x0032ec52, 6}, {0x00331fc5, 6}, {0x0033228a, 6},
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
void install_rdtsc_patches() {
    constexpr size_t kTrampolineSize = 32;
    auto* pool = alloc_code(std::size(kRdtscSites) * kTrampolineSize);
    if (!pool) {
        LOG_ERROR("patches", "no memory for the RDTSC trampolines; the title runs at the host's TSC rate");
        return;
    }
    int installed = 0;
    for (size_t i = 0; i < std::size(kRdtscSites); ++i) {
        const RdtscSite& site = kRdtscSites[i];
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

constexpr uint32_t kCallMouseMove = 0x002c7ad7;  // in CL_CreateCmd FUN_002c7a90
constexpr uint32_t kMouseMove = 0x002b39e0;

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
    if ((title<uint32_t>(0x009601ac) & 0x4000) && !(title<uint32_t>(0x00960240) & 0x106000)) return;
    if (title<int32_t>(0x00933fb8) != 0) return;  // in a vehicle: its own controls
    platform::HostFloatEnv float_env;
    // cl.cgameSensitivity: cg.zoomSensitivity, set each frame by CG_DrawActiveFrame
    // (FUN_0028b340); lower while aiming down sights, 0 before the first frame.
    const float scale = g_mouse_degrees_per_count * title<float>(0x00961774);
    title<float>(0x0096179c) -= dx * scale;  // cl.viewangles[YAW]
    if (title<int32_t>(0x009341b0) != 0 || cvar_integer(0x0093f808) != 0) {  // cl_freelook
        const float invert = cvar_value(0x00929c0c) < 0.0f ? -1.0f : 1.0f;  // m_pitch sign = invert Y
        title<float>(0x00961798) += dy * scale * invert;  // cl.viewangles[PITCH]
    }
}

// pushad; pushfd; call mouse_look; popfd; popad; jmp CL_MouseMove
void install_mouse_look() {
    auto* site = reinterpret_cast<uint8_t*>(uintptr_t{kCallMouseMove});
    uint32_t relative;
    std::memcpy(&relative, site + 1, 4);
    if (site[0] != 0xE8 || kCallMouseMove + 5 + relative != kMouseMove) {
        LOG_WARN("patches", "%08x: not the call to CL_MouseMove; no mouse look", kCallMouseMove);
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
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{kMouseMove}));
    put_rel32(site, 0xE8, thunk);
    LOG_INFO("patches", "mouse look: %.3f degrees per count", g_mouse_degrees_per_count);
}

// Keyboard actions follow the player's controller style. The keyboard gives the pad
// buttons of the default layout (config/default.cfg); style_a..l.cfg bind the buttons
// to other commands. The active binding of each key code is at keys[code].binding
// (0x0093425c + code * 8, a string or null); the pad's key codes come from
// FUN_002bdf10 and the key name table at 0x008d32e8.
constexpr uint32_t kKeyBindings = 0x0093425c;

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
    const uint32_t binding = title<uint32_t>(kKeyBindings + key_code * 8u);
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

void remap_keyboard_actions(const input::PadState& default_layout, input::PadState& active_layout) {
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
constexpr uint32_t kStickyAim = 0x002db7d0;

void disable_aim_assist() {
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{kStickyAim});
    static constexpr uint8_t kOriginal[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16
    if (std::memcmp(code, kOriginal, sizeof(kOriginal)) != 0) {
        LOG_WARN("patches", "%08x: not the sticky aim function; aim assist stays on", kStickyAim);
        return;
    }
    static constexpr uint8_t kNoTarget[] = {0xC7, 0x81, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3};
    std::memcpy(code, kNoTarget, sizeof(kNoTarget));
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
// (FUN_00064ec0) with the current scene in kCurrentScene. Scenes come from a
// per-frame bump allocator (kFrameAllocator), so the scenes the menu creates are
// exactly those allocated while it renders; a screen also draws into the scene the
// caller created for it just before. The renderer narrows draws from these scenes
// (in_game_menu_draw).

// The render calls: screens "mov eax,[ecx]; add esp,0x10; call [eax+0x1c]", sub-menus
// "mov edx,[ecx]; call [edx+0x1c]", and in FUN_003154b0 the tail jump
// "mov ecx,eax; mov eax,[ecx]; jmp [eax+0x1c]".
constexpr uint32_t kScreenRenderCalls[] = {0x00315414, 0x00315476};
constexpr uint32_t kMenuRenderCalls[] = {0x00315433, 0x00315491};
constexpr uint32_t kMenuRenderJump = 0x00315515;
constexpr uint32_t kCurrentScene = 0x0090259c;
constexpr uint32_t kFrameAllocator = 0x009025a8;
constexpr uint32_t kEngineFrame = 0x009028e0;  // counts swaps (FUN_00064ec0)

struct MenuScenes {
    uint32_t begin = 0, end = 0;  // allocated while the menu rendered
    uint32_t scene = 0;           // a screen's own scene
    uint32_t frame = UINT32_MAX;  // kEngineFrame then
};
MenuScenes g_menu_scenes;

void XB_CDECL menu_render_begin() {
    const uint32_t allocator = title<uint32_t>(kFrameAllocator);
    g_menu_scenes = {allocator, allocator, 0, title<uint32_t>(kEngineFrame)};
}

void XB_CDECL screen_render_begin() {
    menu_render_begin();
    g_menu_scenes.scene = title<uint32_t>(kCurrentScene);
}

void XB_CDECL menu_render_end() { g_menu_scenes.end = title<uint32_t>(kFrameAllocator); }

// pushad; pushfd; call function; popfd; popad
uint8_t* put_preserving_call(uint8_t* t, void (*function)()) {
    *t++ = 0x60, *t++ = 0x9C;
    put_rel32(t, 0xE8, reinterpret_cast<const void*>(function));
    t += 5;
    *t++ = 0x9D, *t++ = 0x61;
    return t;
}

void install_menu_render_hooks() {
    const uint8_t screen_site[8] = {0x8B, 0x01, 0x83, 0xC4, 0x10, 0xFF, 0x50, 0x1C};
    const uint8_t call_site[5] = {0x8B, 0x11, 0xFF, 0x52, 0x1C};
    for (uint32_t address : kScreenRenderCalls)
        if (std::memcmp(reinterpret_cast<void*>(uintptr_t{address}), screen_site, sizeof(screen_site)) != 0) {
            LOG_WARN("patches", "%08x: not a screen render call; in-game menus stay stretched", address);
            return;
        }
    const uint8_t jump_site[7] = {0x8B, 0xC8, 0x8B, 0x01, 0xFF, 0x60, 0x1C};
    for (uint32_t address : kMenuRenderCalls)
        if (std::memcmp(reinterpret_cast<void*>(uintptr_t{address}), call_site, sizeof(call_site)) != 0) {
            LOG_WARN("patches", "%08x: not a menu render call; in-game menus stay stretched", address);
            return;
        }
    auto* jump = reinterpret_cast<uint8_t*>(uintptr_t{kMenuRenderJump});
    if (std::memcmp(jump, jump_site, sizeof(jump_site)) != 0) {
        LOG_WARN("patches", "%08x: not the menu render jump; in-game menus stay stretched", kMenuRenderJump);
        return;
    }
    auto* thunks = alloc_code(128);
    if (!thunks) return;
    // Called with the menu in ecx: begin; mov edx,[ecx]; call [edx+0x1c]; end; ret
    auto put_thunk = [](uint8_t* t, void (*begin)()) {
        t = put_preserving_call(t, begin);
        *t++ = 0x8B, *t++ = 0x11, *t++ = 0xFF, *t++ = 0x52, *t++ = 0x1C;
        t = put_preserving_call(t, &menu_render_end);
        *t++ = 0xC3;
        return t;
    };
    uint8_t* const thunk = thunks;
    uint8_t* const screen_thunk = put_thunk(thunk, &menu_render_begin);
    put_thunk(screen_thunk, &screen_render_begin);
    for (uint32_t address : kMenuRenderCalls) put_rel32(reinterpret_cast<uint8_t*>(uintptr_t{address}), 0xE8, thunk);
    for (uint32_t address : kScreenRenderCalls) {  // add esp,0x10; call thunk
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
    g_objectives_scene = title<uint32_t>(kCurrentScene) != 0 && call_title(kSceneCreate, 0) != 0;
    if (g_objectives_scene) {
        call_title(kSceneSetClear, 0);
        call_title(kSceneSetFlag385, 0);
        call_title(kSceneSetFlag384, 0);
    }
    g_objectives_scenes = {0, 0, g_objectives_scene ? title<uint32_t>(kCurrentScene) : 0,
                           title<uint32_t>(kEngineFrame)};
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
constexpr uint32_t kSetFrameInterval = 0x000645c0;
constexpr uint32_t kFrameVBlanks = 0x008a2cb0;
constexpr uint32_t kFrameLateNoWait = 0x00902878;
constexpr uint32_t kFrameWaitSetup = 0x00064370;
constexpr uint32_t kFrameWaitJumps[] = {0x000645ad, 0x000649d8};
constexpr uint32_t kMovieNextFrameSlot = 0x007cf398;
constexpr uint32_t kMovieNextFrame = 0x000ce210;
uint32_t g_frame_vblanks = 2;      // per frame outside movies
bool g_frame_wait = true;          // false: the title asked for mode 0 (no wait)
uint32_t g_movie_frame = 0;        // kEngineFrame at the last movie frame
bool g_movie_seen = false;
bool g_movie_pacing = false;

void XB_CDECL set_frame_interval(uint32_t mode) {
    if (mode > 4) return;
    g_frame_wait = mode != 0;
    title<uint32_t>(kFrameLateNoWait) = mode >= 3 ? 1 : 0;
    title<uint32_t>(kFrameVBlanks) = g_frame_wait ? g_frame_vblanks : 0;
}

void XB_CDECL movie_frame() {
    g_movie_frame = title<uint32_t>(kEngineFrame);
    g_movie_seen = true;
}

void XB_CDECL choose_frame_wait() {
    if (!g_frame_wait || g_frame_vblanks == 2) return;
    const bool movie = g_movie_seen && title<uint32_t>(kEngineFrame) - g_movie_frame <= 2;
    if (movie != g_movie_pacing) {
        g_movie_pacing = movie;
        LOG_INFO("patches", movie ? "movie: 30 fps while it plays" : "movie over: %u vertical blanks per frame again",
                 g_frame_vblanks);
    }
    title<uint32_t>(kFrameVBlanks) = movie ? 2 : g_frame_vblanks;
}

void install_frame_rate(int32_t frame_rate) {
    g_frame_vblanks = frame_rate == 60 ? 1 : 2;
    install_jump(kSetFrameInterval, reinterpret_cast<const void*>(&set_frame_interval));
    LOG_INFO("patches", "%d fps (%u vertical blanks per frame)", frame_rate, g_frame_vblanks);
    if (g_frame_vblanks == 2) return;

    auto* slot = reinterpret_cast<uint32_t*>(uintptr_t{kMovieNextFrameSlot});
    for (uint32_t address : kFrameWaitJumps) {
        auto* site = reinterpret_cast<uint8_t*>(uintptr_t{address});
        if (site[0] != 0xE9 || site + 5 + *reinterpret_cast<int32_t*>(site + 1) !=
                                   reinterpret_cast<uint8_t*>(uintptr_t{kFrameWaitSetup})) {
            LOG_WARN("patches", "%08x: not the frame wait jump; movies play too fast", address);
            return;
        }
    }
    if (*slot != kMovieNextFrame) {
        LOG_WARN("patches", "%08x: not the movie decoder's next-frame slot; movies play too fast", kMovieNextFrameSlot);
        return;
    }
    auto* thunks = alloc_code(64);
    if (!thunks) return;
    // choose_frame_wait; jmp 0x00064370
    uint8_t* t = put_preserving_call(thunks, &choose_frame_wait);
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{kFrameWaitSetup}));
    for (uint32_t address : kFrameWaitJumps) put_rel32(reinterpret_cast<uint8_t*>(uintptr_t{address}), 0xE9, thunks);
    // movie_frame; jmp 0x000ce210 (ecx = the decoder, kept by the preserving call)
    uint8_t* const movie_thunk = t + 5;
    t = put_preserving_call(movie_thunk, &movie_frame);
    put_rel32(t, 0xE9, reinterpret_cast<const void*>(uintptr_t{kMovieNextFrame}));
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(movie_thunk));
}

}  // namespace

float in_game_menu_draw() {
    const uint32_t scene = title<uint32_t>(kCurrentScene);
    const MenuScenes& m = g_menu_scenes;
    const MenuScenes& o = g_objectives_scenes;
    const uint32_t frame = title<uint32_t>(kEngineFrame);
    if (m.frame == frame && (scene == m.scene || (scene >= m.begin && scene < m.end))) return 0.5f;
    if (o.frame == frame && o.scene != 0 && scene == o.scene) return kObjectivesAnchor;
    return -1.0f;
}

namespace {

struct SdkLibrary {
    const char* name;
    const char* section;  // XBE section holding the original library
    std::span<const xbox::KernelImpl> functions;
};

// SDK functions XbSymbolDatabase leaves unnamed in this title, by their XDK name.
struct SdkAlias {
    uint32_t address;
    const char* name;
};
const SdkAlias kSdkAliases[] = {
    {0x003e6210, "D3DDevice_BlockOnFence"},  // BlockOnTime(fence, 4)
    {0x003e6ea0, "D3DDevice_LoadVertexShaderProgram"},
    {0x003e5f20, "D3DDevice_GetTexture2"},
    {0x003e6200, "D3DDevice_BlockUntilIdle"},
    {0x003e6950, "D3DDevice_GetScreenSpaceOffset"},
    {0x00379ebb, "XOnlineSilentLogon"},  // GetUsers, first account, Logon; 0x8015108x errors
};

// SDK functions that are safe to run as they are (no hardware access), left untrapped.
const uint32_t kSdkKeepOriginal[] = {
    0x003ebb20,  // D3D: returns 1 (called before D3D_SetPushBufferSize)
    0x00396f5f,  // XONLINE: a static initializer (_cinit)
};

// The same, by SDK name (any title): thin wrappers around XAPI.
const char* const kSdkKeepOriginalNames[] = {
    "D3D_AllocContiguousMemory",  // XMemAlloc with D3D's attributes
    "D3D_FreeContiguousMemory",   // XMemFree
    "D3DDevice_GetCreationParameters",  // constants plus a flag from the device block
};

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

bool install_sdk_replacements(const XbeImage& image, const SymbolTable& symbols) {
    g_symbols = &symbols;
    const SdkLibrary libraries[] = {
        {"DirectSound", "DSOUND", audio::dsound_functions()},
        {"Direct3D", "D3D", render::d3d_functions()},
        {"Input", "XPP", input::xpp_functions()},
        {"Xbox Live", "XONLINE", online::xonline_functions()},
        {"Network", "XNET", {}},
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
        std::vector<uint32_t> keep(std::begin(kSdkKeepOriginal), std::end(kSdkKeepOriginal));
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
            for (const SdkAlias& alias : kSdkAliases)
                if (std::strcmp(alias.name, function.name) == 0) addresses.push_back(alias.address);
            if (addresses.empty()) {
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

// XAPI XLaunchNewImageA calls the first non-null entry of a launch hook table
// (0x00897908-0x00897910, static data) instead of its own path. XONLINE puts
// FUN_0037da38 there: the same launch plus Xbox Live dashboard handling and cleanup.
// Without it XAPI writes the LaunchDataPage and calls HalReturnToFirmware itself.
constexpr uint32_t kLaunchHookTable = 0x00897908;
constexpr uint32_t kLaunchHookTableEnd = 0x00897910;

void install_title_patches(const TitlePatchConfig& config) {
    g_mouse_degrees_per_count = config.mouse_degrees_per_count;
    for (const Patch& patch : kPatches) {
        install_jump(patch.address, patch.replacement);
        LOG_DEBUG("patches", "%08x: %s", patch.address, patch.what);
    }
    std::memset(reinterpret_cast<void*>(uintptr_t{kLaunchHookTable}), 0, kLaunchHookTableEnd - kLaunchHookTable);
    install_rdtsc_patches();
    install_mouse_look();
    install_start_level(config.start_level);
    if (!config.aim_assist) disable_aim_assist();
    install_frame_rate(config.frame_rate);
    install_menu_render_hooks();
    install_objectives_hook();
    input::set_action_remap(&remap_keyboard_actions);
}

}  // namespace loader
