#include "cgame.h"

#include <cstdarg>
#include <cstring>

#include "title_data.h"

namespace game {

namespace {

int32_t cg_time() { return title_global<int32_t>(0x0097253c); }

// Registers (import 0x009284f8, type 5) the asset named by the configstring slot at
// `address` when the slot lies in cl_configstrings (0x009606b8, 0x400 slots) and is set and
// not empty; a slot outside or unset reads address 0, as the original does.
void precache_configstring_slot(uint32_t address) {
    const char* name = nullptr;
    if (address >= 0x009606b8 && address < 0x009616b8) {
        const uint8_t* string = title_global<const uint8_t*>(address);
        if (string) name = reinterpret_cast<const char*>(string + 6);
    }
    if (!*name) return;
    title_function<void XB_CDECL(const char*, uint32_t)>(title_global<uint32_t>(0x009284f8))(name, 5);
}

}  // namespace

uint32_t XB_REGS2 XB_CDECL CG_HasWeapon(uint32_t, int32_t weapon) {
    return (title_global<uint32_t>(0x00972940 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f))) != 0;
}

void XB_CDECL CG_ScoresUp_f() {
    if (!title_global<uint32_t>(0x00973a78)) return;
    title_global<uint32_t>(0x00973a78) = 0;
    title_global<int32_t>(0x00973a7c) = cg_time();
}

void XB_CDECL CG_Printf(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
}

PLATFORM_ASM_ENTRY(CG_WeaponSlotFromName_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_WeaponSlotFromName) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL CG_WeaponSlotFromName(const char* name) {
    for (uint32_t slot = 0; slot < 7; ++slot)
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(  // __stricmp
                name, title_global<const char*>(0x008d23a8 + slot * 4)))
            return slot;
    return 0;
}

void XB_CDECL CG_Error(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, text);  // Com_Error
}

void XB_REGS1 XB_CDECL CG_PrecacheConfigStringAsset(int32_t index) {
    precache_configstring_slot(index >= 0 && index < 0x400 ? 0x009606b8 + static_cast<uint32_t>(index) * 4 : 0);
}

void XB_CDECL CG_PrecacheConfigStringAssets() {
    for (uint32_t slot = 0x00961208; slot < 0x00961604; slot += 4) precache_configstring_slot(slot);
}

void XB_CDECL CG_SaveViewModelAnimTrees() {
    // Every weapon's view model (0xc4 bytes from 0x0097606c) that is used (+0x84) must have
    // its tree (+0).
    const int32_t count = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* model = &title_global<uint8_t>(0x0097606c + i * 0xc4);
        if (model[0x84] && !*reinterpret_cast<const uint32_t*>(model))
            title_function<void XB_CDECL(int32_t, uint32_t)>(0x002938f0)(1, 0x008387f8);  // Com_Error
    }
}

char* XB_REGS1 XB_CDECL CG_Argv(uint32_t arg) {
    const char* text = arg < title_global<uint32_t>(0x00934248)  // cmd_argc
                           ? title_global<const char*>(0x0092df90 + arg * 4)  // cmd_argv
                           : reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    char* buffer = &title_global<char>(0x0098b958);  // cg_argvBuffer
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buffer, text, 0xff);  // _strncpy
    buffer[0xff] = 0;
    return buffer;
}

void XB_REGS1 XB_CDECL CG_StretchPicRotate(uint32_t shader, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t s0, uint32_t t0, uint32_t s1, uint32_t t1, uint32_t angle) {
    using Draw = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                               uint32_t);
    title_function<Draw>(title_global<uint32_t>(0x00928538))(x, y, w, h, s0, t0, s1, t1, angle, shader);
}

void XB_CDECL CG_DrawShellShockBlur() {
    if (!title_global<uint32_t>(0x00949354)) return;
    const int32_t start = title_global<int32_t>(0x00973a4c);
    const int32_t duration = title_global<int32_t>(0x00973a50);
    if (start && duration > 0 && start - cg_time() + duration > 0) {
        title_function<void XB_CDECL()>(title_global<uint32_t>(0x00928550))();
        title_global<uint32_t>(0x00973a64) = 1;
        return;
    }
    title_global<uint32_t>(0x00973a64) = 0;
}

void XB_CDECL CG_InitLocalEntities() {
    // 20 entities of 0x9c bytes (0x00973a90): the active list's sentinel (0x0095f748) empty,
    // all on the free list (next at +4).
    std::memset(&title_global<uint8_t>(0x00973a90), 0, 0x30c * 4);
    title_global<uint32_t>(0x0095f74c) = 0x0095f748;
    title_global<uint32_t>(0x0095f748) = 0x0095f748;
    title_global<uint32_t>(0x0094c9d8) = 0x00973a90;
    for (uint32_t next = 0x00973a94; next < 0x00974628; next += 0x9c) title_global<uint32_t>(next) = next + 0x98;
}

void XB_CDECL CG_StepOffset() {
    const int32_t time = cg_time();
    const int32_t delta = time - title_global<int32_t>(0x00972aec);  // cg.stepTime
    if (delta < 0) title_global<int32_t>(0x00972aec) = time;
    if (delta >= 100) return;
    float& height = title_global<float>(0x00972bb8);
    height = height - static_cast<float>(100 - delta) * title_global<float>(0x00972ae8) * title_global<float>(0x0083eb94);
}

void XB_REGS1 XB_CDECL CG_R_DrawStretchPic(uint32_t shader, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t s0, uint32_t t0, uint32_t s1, uint32_t t1) {
    using Draw = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<Draw>(title_global<uint32_t>(0x00928530))(x, y, w, h, s0, t0, s1, t1, shader);
}

void XB_REGS3 XB_CDECL CG_SetFade(int32_t duration, uint32_t, int32_t start, int32_t alpha) {
    const float value = static_cast<float>(alpha) * title_global<float>(0x0083eb30);
    title_global<int32_t>(0x009730c0) = start;
    title_global<int32_t>(0x009730c4) = duration;
    title_global<float>(0x009730b8) = value;
    if (start + duration <= cg_time()) title_global<float>(0x009730bc) = value;
}

namespace {

// The bob amplitude scale for the player's stance (0x009725f8 against 0x00972970 /
// 0x00972974) times speed, capped at `cap` (the cap when greater; NaN keeps the product).
float bob_amplitude(float speed, float cap) {
    const uint32_t stance = title_global<uint32_t>(0x009725f8);
    const float scale = stance == title_global<uint32_t>(0x00972970)   ? title_global<float>(0x0094aa90)
                        : stance == title_global<uint32_t>(0x00972974) ? title_global<float>(0x00947e80)
                                                                         : title_global<float>(0x0094a6a0);
    const float amplitude = scale * speed;
    return amplitude > cap ? cap : amplitude;
}

void draw_stretch_pic(float x, float y, float w, float h, float s0, float t0, float s1, float t1, uint32_t shader) {
    const float xs = title_global<float>(0x009758b8);  // cgs.screenXScale
    const float ys = title_global<float>(0x009758bc);
    using Draw = void XB_CDECL(float, float, float, float, float, float, float, float, uint32_t);
    title_function<Draw>(title_global<uint32_t>(0x00928530))(xs * x, ys * y, xs * w, ys * h, s0, t0, s1, t1, shader);
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_BobCycleVertical_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n" PLATFORM_ASM_CALL(CG_BobCycleVertical) "addl $12, %esp\n"
                   "ret")

float XB_CDECL CG_BobCycleVertical(float cycle, float speed, float cap) {
    return platform_x87_sin_mul(cycle, bob_amplitude(speed, cap));
}

PLATFORM_ASM_ENTRY(CG_BobCycleLateral_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n" PLATFORM_ASM_CALL(CG_BobCycleLateral) "addl $12, %esp\n"
                   "ret")

float XB_CDECL CG_BobCycleLateral(float cycle, float speed, float cap) {
    return platform_x87_sin_sum_mul(cycle, title_global<float>(0x0083100c), title_global<float>(0x0083ed0c),
                                    title_global<float>(0x0083eca0), bob_amplitude(speed, cap),
                                    title_global<float>(0x0083eb58));
}

void XB_REGS1 XB_CDECL CG_DrawPic(uint32_t shader, float x, float y, float w, float h) {
    draw_stretch_pic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, shader);
}

void XB_REGS1 XB_CDECL CG_FillRect(const float* color, float x, float y, float w, float h) {
    const auto set_color = title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c));
    set_color(color);
    draw_stretch_pic(x, y, w, h, 0.0f, 0.0f, 0.0f, 1.0f, title_global<uint32_t>(0x00975c3c));  // white
    set_color(nullptr);
}

float* XB_REGS3 XB_CDECL CG_FadeColor(uint32_t, int32_t start, int32_t total) {
    if (!start) return nullptr;
    const int32_t elapsed = cg_time() - start;
    if (elapsed >= total) return nullptr;
    const float one = title_global<float>(0x0083eb20);
    const int32_t left = total - elapsed;
    const float fade = left < 100 ? static_cast<float>(left) * title_global<float>(0x0083eb94) : one;
    float* color = &title_global<float>(0x0098ad68);
    color[2] = one;
    color[1] = one;
    color[0] = one;
    color[3] = title_global<float>(0x0094b5d8) * fade;
    return color;
}

uint32_t XB_REGS3 XB_CDECL CG_GetAdsOverlayFraction(uint32_t, uint32_t, float* out) {
    float fraction = title_global<float>(0x009725d8);
    *out = 0.0f;
    const auto weapon = [] { return title_global<const uint8_t*>(0x00972acc); };
    const auto field = [](const uint8_t* def, uint32_t offset) { return *reinterpret_cast<const float*>(def + offset); };
    if (!**reinterpret_cast<const char* const*>(weapon() + 0x634) && !*reinterpret_cast<const uint32_t*>(weapon() + 0x638))
        return 0;
    if (fraction == 0.0f) return 0;
    // The overlay fades in over the last part of the zoom (in +0x62c, out +0x630).
    const uint32_t time_offset = title_global<uint32_t>(0x00972a74) ? 0x62c : 0x630;
    fraction = fraction - (title_global<float>(0x0083eb20) - field(weapon(), time_offset));
    *out = fraction;
    if (fraction > 0.0f) *out = fraction / field(weapon(), time_offset);
    return !(title_global<float>(0x0083eb94) >= *out);
}

void XB_CDECL CG_CheckAmmo() {
    // The weapons word pair the original tests also takes two stack words, one of them its
    // return address: never all zero, so the check always runs.
    const uint8_t* snap = title_global<const uint8_t*>(0x00971aa8);
    const uint32_t weapons = *reinterpret_cast<const uint32_t*>(snap + 0x400);
    const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    int32_t total = 0;
    for (int32_t i = 1; i < last; ++i) {
        if (!(weapons & (1u << (i & 0x1f)))) continue;
        const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[i];  // bg_weaponDefs
        const int32_t ammo_index = *reinterpret_cast<const int32_t*>(def + 0x598);
        total += *reinterpret_cast<const int32_t*>(snap + 0x120 + ammo_index * 4) * 1000;
        if (total >= 5000) {
            title_global<int32_t>(0x00972eb4) = 0;  // cg.lowAmmoWarning
            return;
        }
    }
    title_global<int32_t>(0x00972eb4) = total ? 1 : 2;
}

void XB_CDECL CG_BuildSolidList() {
    const uint32_t count = title_global<uint32_t>(0x0097d36c);
    int32_t& solid_count = title_global<int32_t>(0x0094af98);  // cg_numSolidEntities
    solid_count = 0;
    if (!count) return;
    int32_t found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* entity = title_global<const uint8_t*>(0x0097d370 + i * 4);
        if (!entity) continue;
        const uint32_t model = *reinterpret_cast<const uint32_t*>(entity + 0xac);
        if (model == 0xffffff && (entity[0x10] & 2)) continue;
        if (entity[0] == 2 || !model || found >= 0x400) continue;
        title_global<uint32_t>(0x00974810 + found * 4) = *reinterpret_cast<const uint32_t*>(entity + 0x224);
        ++found;
    }
    solid_count = found;
}

}  // namespace game
