#include "cgame.h"

#include <cstdarg>
#include <cstring>

#include "bg_misc.h"
#include "cg_ents.h"
#include "dobj.h"
#include "qcommon.h"
#include "title_data.h"
#include "tr_main.h"

// Calls a title function that takes EAX, ESI and EDI (tr_main.cpp).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

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

void XB_CDECL CG_Fade_f() {
    // Q3's "fade r g b a seconds": the colour words are parsed and dropped.
    if (title_global<uint32_t>(0x00934248) < 6) return;  // cmd_argc
    const auto atol = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81);
    for (uint32_t arg = 1; arg < 4; ++arg) atol(CG_Argv(arg));
    const int32_t alpha = atol(CG_Argv(4));
    const int32_t duration = atol(CG_Argv(5)) * 1000;
    const int32_t time = cg_time();
    const float value = static_cast<float>(alpha) * title_global<float>(0x0083eb30);
    title_global<int32_t>(0x009730c4) = duration;
    title_global<float>(0x009730b8) = value;
    title_global<int32_t>(0x009730c0) = time;
    if (static_cast<int32_t>(static_cast<uint32_t>(duration) + static_cast<uint32_t>(time)) <= time)
        title_global<float>(0x009730bc) = value;
}

void XB_CDECL CG_DrawFlashFade() {
    const int32_t time = cg_time();
    int32_t& last = title_global<int32_t>(0x0098ad54);
    if (last > time) last = 0;
    const int32_t duration = title_global<int32_t>(0x009730c4);
    const float target = title_global<float>(0x009730b8);
    float& current = title_global<float>(0x009730bc);
    if (title_global<int32_t>(0x009730c0) + duration < time) {
        current = target;
    } else if (!(current == target)) {  // unordered goes on
        const int32_t elapsed = time - last;
        last = time;
        if (elapsed < 500 && elapsed > 0) {
            const float step = static_cast<float>(elapsed) / static_cast<float>(duration);
            if (current > target) {
                current = current - step;
                if (target > current) current = target;
            } else {
                current = step + current;
                if (current > target) current = target;
            }
        }
    }
    if (!platform_x87_greater(current, 0.0f)) return;
    const float color[4] = {0.0f, 0.0f, 0.0f, current};
    const auto set_color = title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c));
    set_color(color);
    draw_stretch_pic(0.0f, 0.0f, title_global<float>(0x0083ec0c), title_global<float>(0x0083ec08), 0.0f, 0.0f, 0.0f,
                     1.0f, title_global<uint32_t>(0x00975c3c));  // 640x480, white
    set_color(nullptr);
}

namespace {

// angle (degrees) * pi * (1 / 180) through FastSinCos.
void sin_cos_degrees(float angle, float* sine, float* cosine) {
    angle = angle * title_global<float>(0x0083ed00);
    angle = angle * title_global<float>(0x0083ecfc);
    FastSinCos(sine, 0, cosine, angle);
}

using DrawQuad = void XB_CDECL(const float*, const float*, uint32_t);

}  // namespace

PLATFORM_ASM_ENTRY(CG_DrawRotatedPic_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n" PLATFORM_ASM_CALL(CG_DrawRotatedPic) "addl $24, %esp\n"
                   "ret")

void XB_CDECL CG_DrawRotatedPic(float x, float y, float w, float h, uint32_t shader, float angle) {
    float sine, cosine;
    sin_cos_degrees(angle, &sine, &cosine);
    const float xs = title_global<float>(0x009758b8);  // cgs.screenXScale
    const float ys = title_global<float>(0x009758bc);
    const float half = title_global<float>(0x0083eb24);
    const float one = title_global<float>(0x0083eb20);
    const float minus_one = title_global<float>(0x0083eb34);
    const float hw = xs * w * half;
    const float cx = xs * x + hw;
    const float hh = ys * h * half;
    const float cy = ys * y + hh;
    // The corners (-1,-1), (1,-1), (1,1), (-1,1) of the half extents, rotated.
    const float ws = hw * sine, wc = hw * cosine, hs = hh * sine, hc = hh * cosine;
    const float left = cx - wc * one, right = wc + cx;
    const float ws_neg = ws * minus_one, hc_neg = hc * minus_one, hs_neg = hs * minus_one;
    const float verts[8] = {left - ws_neg,  (hs_neg + hc_neg) + cy, right - ws_neg, (hs + hc_neg) + cy,
                            right - ws,     (hc + hs) + cy,         left - ws,      (hc + hs_neg) + cy};
    title_function<DrawQuad>(title_global<uint32_t>(0x0092853c))(verts, &title_global<float>(0x00839a3c), shader);
}

PLATFORM_ASM_ENTRY(CG_DrawRotatedQuadPic_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %esi\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n" PLATFORM_ASM_CALL(CG_DrawRotatedQuadPic) "addl $24, %esp\n"
                   "ret")

void XB_CDECL CG_DrawRotatedQuadPic(float x, float y, const float* st, uint32_t shader, const float* points,
                                    float angle) {
    float sine, cosine;
    sin_cos_degrees(angle, &sine, &cosine);
    const float xs = title_global<float>(0x009758b8);
    const float ys = title_global<float>(0x009758bc);
    const float xsin = xs * sine, ysin = ys * sine, xcos = xs * cosine, ycos = ys * cosine;
    const float ox = xs * x, oy = ys * y;
    // The four points rotated (operand order as the original's, first point apart).
    float verts[8];
    verts[0] = (points[0] * xcos + ox) - xsin * points[1];
    verts[1] = (points[0] * ysin + ycos * points[1]) + oy;
    for (uint32_t i = 2; i < 8; i += 2) {
        verts[i] = (points[i] * xcos + ox) - points[i + 1] * xsin;
        verts[i + 1] = (points[i + 1] * ycos + points[i] * ysin) + oy;
    }
    title_function<DrawQuad>(title_global<uint32_t>(0x0092853c))(verts, st, shader);
}

PLATFORM_ASM_ENTRY(CG_DrawTextScaled_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %eax\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n" PLATFORM_ASM_CALL(CG_DrawTextScaled) "addl $36, %esp\n"
                   "ret")

void XB_CDECL CG_DrawTextScaled(float x, float y, const char* text, float width, uint32_t arg4, uint32_t style,
                                uint32_t screen_coords, const float* color, float scale) {
    const float one = title_global<float>(0x0083eb20);
    const float white[4] = {one, one, one, one};
    y = scale * title_global<float>(0x0083ebc0) + y;  // 0.8
    float text_scale = scale * title_global<float>(0x0083ebbc);  // 1 / 48
    if (!screen_coords) {
        const float inv_x = one / title_global<float>(0x009758b8);
        const float inv_y = one / title_global<float>(0x009758bc);
        x = inv_x * x;
        width = inv_x * width;
        y = inv_y * y;
        text_scale = inv_y * text_scale;
    }
    if (!color) color = white;
    using Draw = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, float, uint32_t, uint32_t);
    title_function<Draw>(title_global<uint32_t>(0x00928578))(x, y, 5, text_scale, color, text, width, arg4,
                                                             style ? 3 : 0);
}

void XB_CDECL CG_DrawCenterString() {
    int32_t& start_time = title_global<int32_t>(0x00972c70);  // cg.centerPrintTime
    if (!start_time) return;
    const int32_t total = static_cast<int32_t>(title_global<float>(0x00949a10) * title_global<float>(0x0083eb9c));
    const int32_t elapsed = cg_time() - start_time;
    if (elapsed >= total) {
        start_time = 0;
        title_global<int32_t>(0x00972e80) = 0;
        return;
    }
    // CG_FadeColor inlined.
    const float one = title_global<float>(0x0083eb20);
    const int32_t left = total - elapsed;
    const float fade = left < 100 ? static_cast<float>(left) * title_global<float>(0x0083eb94) : one;
    float* color = &title_global<float>(0x0098ad68);
    color[2] = one;
    color[1] = one;
    color[0] = one;
    color[3] = title_global<float>(0x0094b5d8) * fade;
    const auto set_color = title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c));
    set_color(color);
    const float scale = static_cast<float>(title_global<int32_t>(0x00972c74)) * title_global<float>(0x0083f004);
    const int32_t height = title_function<int32_t XB_CDECL(uint32_t, float)>(title_global<uint32_t>(0x00928574))(0, scale);
    const float line_height = static_cast<float>(height);
    float y = static_cast<float>(title_global<int32_t>(0x00972c78)) -
              line_height * title_global<float>(0x0083eb24) * static_cast<float>(title_global<int32_t>(0x00972e7c));
    y = y - line_height;
    const float advance = line_height * title_global<float>(0x00830f60);  // 1.2
    using Draw = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, uint32_t, uint32_t, uint32_t);
    const auto draw = title_function<Draw>(title_global<uint32_t>(0x00928578));
    const char* start = &title_global<char>(0x00972c7c);  // cg.centerPrint
    for (;;) {
        // A line ends at the end, a newline or a backslash (strncmp(.., "\\n", 1)); 40 at most.
        char line[0x400];
        int32_t length = 0;
        while (length < 0x28 && start[length] && start[length] != '\n' && start[length] != '\\') {
            line[length] = start[length];
            ++length;
        }
        line[length] = 0;
        const int32_t width = RE_TextWidth(line, 0, scale);
        const float x = (title_global<float>(0x0083ec0c) - static_cast<float>(width)) * title_global<float>(0x0083eb24);
        draw(x, y, 0, scale, color, line, 0, 0, 3);
        y = advance + y;
        // Past the newline, or past "\" and the character after it.
        for (;;) {
            if (!*start) {
                set_color(nullptr);
                return;
            }
            if (*start == '\n') break;
            if (*start++ == '\\') break;
        }
        if (!*start) break;
        ++start;
    }
    set_color(nullptr);
}

PLATFORM_ASM_ENTRY(CG_ShareWeaponTagBModels_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_ShareWeaponTagBModels) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL CG_ShareWeaponTagBModels(uint8_t* model) {
    // Tags (+0x10 count, +0x14 12-byte entries, name first) and their models (+0x28 count,
    // +0x2c): every "tag_b*" tag without a model gets the last "tag_b" tag's. Indexes past a
    // vector's count read element 0.
    const auto word = [model](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(model + offset); };
    const auto tag_name = [&](uint32_t i) {
        const uint32_t index = i < word(0x10) ? i : 0;
        return *reinterpret_cast<const char* const*>(word(0x14) + index * 12);
    };
    const auto model_slot = [&](uint32_t i) -> uint32_t& {
        const uint32_t index = i < word(0x28) ? i : 0;
        return reinterpret_cast<uint32_t*>(word(0x2c))[index];
    };
    const char* const tag_b = reinterpret_cast<const char*>(uintptr_t{0x0083898c});
    uint32_t shared = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(word(0x10)); ++i)
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(tag_name(i), tag_b))  // __stricmp
            shared = model_slot(i);
    if (!shared) return static_cast<int32_t>(word(0x10));
    for (int32_t i = 0; i < static_cast<int32_t>(word(0x10)); ++i)
        if (!title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001f1486)(tag_name(i), tag_b, 5) &&
            !model_slot(i))
            model_slot(i) = shared;
    return static_cast<int32_t>(word(0x10));
}

void XB_CDECL CG_Viewpos_f() {
    const float* origin = &title_global<float>(0x00972bb0);
    CG_Printf(&title_global<char>(0x00839c0c), static_cast<int32_t>(origin[0]), static_cast<int32_t>(origin[1]),
              static_cast<int32_t>(origin[2]), static_cast<int32_t>(title_global<float>(0x00972bf4)));
}

namespace {

// Local entities: {prev, next, ...} 0x9c bytes; the active list's sentinel is
// 0x0095f748 {prev = oldest, next = newest}, the free list 0x0094c9d8 (next at +4).
uint32_t*& free_local_entities() { return title_global<uint32_t*>(0x0094c9d8); }

void free_local_entity(uint32_t* le) {
    if (!le[0]) CG_Error(&title_global<char>(0x00839910));
    reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[0]))[1] = le[1];
    reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[1]))[0] = le[0];
    le[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(free_local_entities()));
    free_local_entities() = le;
}

}  // namespace

uint32_t* XB_CDECL CG_AllocLocalEntity() {
    uint32_t* sentinel = &title_global<uint32_t>(0x0095f748);
    if (!free_local_entities()) {
        // None free: the oldest active one is reused.
        free_local_entity(reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(sentinel[0])));
    }
    uint32_t* le = free_local_entities();
    free_local_entities() = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[1]));
    std::memset(le, 0, 0x9c);
    le[1] = sentinel[1];
    le[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentinel));
    reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(sentinel[1]))[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(le));
    sentinel[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(le));
    return le;
}

PLATFORM_ASM_ENTRY(CG_AddNearCulledLocalEntity_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_AddNearCulledLocalEntity) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_AddNearCulledLocalEntity(uint32_t* le) {
    // Freed once the view comes nearer than its cull distance (+0x34), else drawn.
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(le);
    const float* origin = reinterpret_cast<const float*>(bytes + 0x78);
    const float* view = &title_global<float>(0x00972bb0);
    if (platform_x87_greater_than_dist3(*reinterpret_cast<const float*>(bytes + 0x34), origin[0], view[0], origin[1], view[1],
                                        origin[2], view[2])) {
        free_local_entity(le);
        return;
    }
    R_AddRefEntityToScene(reinterpret_cast<uint8_t*>(le) + 0x3c, -1);
}

void XB_REGS1 XB_CDECL CG_DrawHudElemAlignedText(const uint8_t* elem, uint32_t arg, const uint8_t* style) {
    if (title_global<uint8_t>(0x0095f919)) return;
    // Aligned in the element's box (x +4, width +0xc) by the text width (+0x28).
    const float x = *reinterpret_cast<const float*>(elem + 4);
    const float box = *reinterpret_cast<const float*>(elem + 0xc);
    const float width = *reinterpret_cast<const float*>(elem + 0x28);
    float offset;
    switch (*reinterpret_cast<const int32_t*>(style + 0x18)) {
    case 0: offset = x; break;
    case 1: offset = (box - width) * title_global<float>(0x0083eb24) + x; break;
    case 2: offset = (box + x) - width; break;
    default: offset = 0.0f; break;
    }
    const uint32_t* e = reinterpret_cast<const uint32_t*>(elem);
    title_function<void XB_CDECL(uint32_t, float, uint32_t, uint32_t, const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t)>(
        title_global<uint32_t>(0x00928578))(e[0], width + offset, e[8], e[9], elem + 0x30, arg, e[11], 0, 3);
}

PLATFORM_ASM_ENTRY(CG_DrawHudElemMaterial_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_DrawHudElemMaterial) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_DrawHudElemMaterial(const uint8_t* he, const uint8_t* rect) {
    if (*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0093fcd8) + 0x20)) return;
    const uint32_t material = *reinterpret_cast<const uint32_t*>(he + 0x38);
    if (!material) return;
    const float fallback = *reinterpret_cast<const float*>(rect + 0x28);
    const auto size = [&](uint32_t offset) {
        const int32_t v = *reinterpret_cast<const int32_t*>(he + offset);
        return v ? static_cast<float>(v) : fallback;
    };
    // Width (+0x30) and height (+0x34), scaled from +0x3c/+0x40 over the fade (+0x44 start, +0x48 ms).
    const int32_t now = title_global<int32_t>(0x0097253c);
    const int32_t fade = *reinterpret_cast<const int32_t*>(he + 0x48);
    const int32_t start = *reinterpret_cast<const int32_t*>(he + 0x44);
    const float one = title_global<float>(0x0083eb20);
    const bool fading = fade > 0 && now - start < fade;
    float width = size(0x30);
    if (fading) {
        const float from = size(0x3c);
        width = one / static_cast<float>(fade) * static_cast<float>(now - start) * (width - from) + from;
    }
    float height = size(0x34);
    if (fading) {
        const float from = size(0x40);
        height = one / static_cast<float>(fade) * static_cast<float>(now - start) * (height - from) + from;
    }
    // Vertical alignment in the rect (y +4, height +0xc) by style +0x18.
    const float y = *reinterpret_cast<const float*>(rect + 4);
    const float box = *reinterpret_cast<const float*>(rect + 0xc);
    float top;
    switch (*reinterpret_cast<const int32_t*>(he + 0x18)) {
    case 0: top = y; break;
    case 1: top = (box - height) * title_global<float>(0x0083eb24) + y; break;
    case 2: top = (box + y) - height; break;
    default: top = 0.0f; break;
    }
    const auto set_color = title_function<void XB_CDECL(const void*)>(title_global<uint32_t>(0x0092852c));
    set_color(rect + 0x30);
    const float x = *reinterpret_cast<const float*>(rect);
    const float angle = *reinterpret_cast<const float*>(he + 0x78);
    if (angle == title_global<float>(0x0083eafc))
        CG_DrawPic(material, x, top, width, height);
    else
        CG_DrawRotatedPic(x, top, width, height, material, angle);
    set_color(nullptr);
}

void XB_CDECL CG_CalcWeaponBob() {
    // The gun's bob angles 0x00972fcc/fd0/fd4 from the bob cycle (0x00973a80) and speed
    // (0x00973a84), each scale capped by 0x0094ba58, per stance (0x009725f8).
    const float cap = title_global<float>(0x0094ba58);
    const float cycle = platform_x87_mul_add_add(title_global<float>(0x0094bc98), title_global<float>(0x0083ed00),
                                                 title_global<float>(0x00973a80), title_global<float>(0x0083edac));
    const float speed = platform_x87_mul(title_global<float>(0x0094b308), title_global<float>(0x00973a84));
    title_global<float>(0x00972fcc) = -CG_BobCycleLateral(cycle, speed, cap);
    const int32_t stance = title_global<int32_t>(0x009725f8);
    const auto stance_scale = [stance]() {
        if (stance == title_global<int32_t>(0x00972970)) return title_global<float>(0x0094aa90);
        if (stance == title_global<int32_t>(0x00972974)) return title_global<float>(0x00947e80);
        return title_global<float>(0x0094a6a0);
    };
    float pitch_scale = stance_scale() * speed;
    if (pitch_scale > cap) pitch_scale = cap;
    const float roll_cycle = cycle - title_global<float>(0x0083ee4c);
    const float roll_speed = title_global<float>(0x0094a450) * speed;
    title_global<float>(0x00972fd0) = -platform_x87_sin_mul(cycle, pitch_scale);
    float roll_scale = stance_scale() * roll_speed;
    if (roll_scale > cap) roll_scale = cap;
    // Only the downward half of the roll cycle.
    const float sine = platform_x87_sin(roll_cycle);
    float roll = 0.0f;
    if (platform_x87_greater(0.0f, platform_x87_sin_mul(roll_cycle, roll_scale))) {
        float scale = stance_scale() * roll_speed;
        if (scale > cap) scale = cap;
        roll = sine * scale;
    }
    title_global<float>(0x00972fd4) = roll;
    const float damp = title_global<float>(0x009725d8);
    if (!(damp == 0.0f)) {
        const float one = title_global<float>(0x0083eb20);
        const uint8_t* player = title_global<uint8_t*>(0x00972acc);
        const float f = one - (one - *reinterpret_cast<const float*>(player + 0x644)) * damp;
        title_global<float>(0x00972fcc) = title_global<float>(0x00972fcc) * f;
        title_global<float>(0x00972fd0) = title_global<float>(0x00972fd0) * f;
        title_global<float>(0x00972fd4) = roll * f;
    }
}

namespace {

// Cvar_Update (0x00296ec0) takes the vmCvar in EBX.
extern "C" PLATFORM_HIDDEN void XB_CDECL cg_cvar_update(uint32_t vm_cvar);
PLATFORM_ASM_ENTRY(cg_cvar_update,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "movl $0x00296ec0, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

void XB_CDECL CG_UpdateCvars() {
    // The 0x89 entries of cgameCvarTable (0x008d9d60, 16 bytes: vmCvar first).
    const uint32_t* entry = &title_global<uint32_t>(0x008d9d60);
    for (uint32_t i = 0; i < 0x89; ++i, entry += 4)
        if (entry[0]) cg_cvar_update(entry[0]);
}

PLATFORM_ASM_ENTRY(CG_DObjEnsureSkeleton_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_DObjEnsureSkeleton) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_DObjEnsureSkeleton(DObj* obj) {
    if (obj->bone_matrices) return;
    // The block from the hunk is handed on but DObj_CreateSkeleton never reads it.
    title_function<void* XB_CDECL(uint32_t, const char*, const char*, uint32_t)>(0x00042c20)(  // Z_MallocDebug
        obj->bone_count * 0x40u + 0x30, &title_global<char>(0x0083534c), &title_global<char>(0x00835354), 0x21c);
    DObj_CreateSkeleton(obj);
}

const char* XB_REGS1 XB_CDECL CG_GetHudElemText(int32_t index) {
    if (!index) return &title_global<char>(0x0082dfeb);
    const auto translate = title_function<const char* XB_REGS1 XB_CDECL(const char*, const char*)>(0x0027b9f0);
    const int32_t slot = index + 0x293;  // the hud element strings in the config strings
    const char* text = nullptr;
    if (slot >= 0 && slot < 0x400) text = title_global<const char*>(0x009606b8 + slot * 4);
    return translate(text ? text + 6 : nullptr, &title_global<char>(0x00838cfc));
}

void XB_CDECL CG_ScoresDown_f() {
    const uint8_t* letterbox = title_function<uint8_t* XB_CDECL(const char*, const char*, uint32_t)>(0x0029c2d0)(  // Cvar_Get
        &title_global<char>(0x00835390), &title_global<char>(0x0082e098), 0);
    if (!(*reinterpret_cast<const float*>(letterbox + 0x1c) == title_global<float>(0x0083eafc)) &&
        !*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0093fcd8) + 0x20))
        return;
    if (title_global<uint32_t>(0x00973a78)) return;
    title_global<uint32_t>(0x00973a78) = 1;
    title_global<int32_t>(0x00973a7c) = title_global<int32_t>(0x0097253c);
}

void XB_CDECL CG_DrawScreenBlackout() {
    const float alpha = title_global<float>(0x00972f70);
    if (platform_x87_greater_equal(0.0f, alpha) || !title_global<uint32_t>(0x00971aa8)) return;
    const float color[4] = {0.0f, 0.0f, 0.0f, alpha};
    title_function<void XB_REGS1 XB_CDECL(const float*, float, float, float, float)>(0x00277bd0)(  // CG_FillRect
        color, 0.0f, 0.0f, 640.0f, 480.0f);
}

uint32_t XB_CDECL CG_GetCurrentWeaponDef() {
    const uint32_t flags = title_global<uint32_t>(0x00972610);  // the player state's flags
    const uint32_t* weapon_defs = title_global<uint32_t*>(0x0095f4d8);
    const uint8_t* ent;
    if (flags & 0x6000) {
        // Using a turret: the entity of the handle in 0x009729b8 (index, generation << 12).
        const uint32_t handle = title_global<uint32_t>(0x009729b8);
        const uint32_t index = handle & 0xfff;
        if (static_cast<int32_t>(index) >= 0x540) return 0;
        if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    } else if (flags & 0x100000) {
        const uint8_t* vehicle = title_global<uint8_t*>(0x0095f534);
        const uint32_t owner = *reinterpret_cast<const uint32_t*>(vehicle + 4);
        if (!owner) return 0;
        ent = title_function<uint8_t* XB_REGS1 XB_CDECL(uint32_t)>(0x00013860)(owner + 0x1a0);  // G_EntHandleToEnt
    } else {
        return weapon_defs[title_global<uint32_t>(0x009725d0)];
    }
    if (!ent) return 0;
    return weapon_defs[ent[3]];
}

void XB_CDECL CG_DrawDamageBlend() {
    if (!title_global<uint32_t>(0x00971aa8)) return;  // cg_snap
    const int32_t end = title_global<int32_t>(0x00972f7c);
    const int32_t now = title_global<int32_t>(0x0097253c);
    if (end <= now) return;
    const float cap = title_global<float>(0x0083ece8);
    const float fade = platform_x87_abs_int_mul_mul(end - now, title_global<float>(0x0083ec1c), title_global<float>(0x00972f80));
    const float level = platform_x87_greater(fade, cap) ? cap : fade;
    const float red = title_global<float>(0x0083eca0);
    const float color[4] = {red, 0.0f, 0.0f, level * red * title_global<float>(0x0083ed28)};
    title_function<void XB_REGS1 XB_CDECL(const float*, float, float, float, float)>(0x00277bd0)(  // CG_FillRect
        color, -10.0f, -10.0f, 650.0f, 490.0f);
}

void XB_CDECL CG_DrawProgressBar(float x, float y, float w, float h, float fraction) {
    const auto fill = title_function<void XB_REGS1 XB_CDECL(const float*, float, float, float, float)>(0x00277bd0);  // CG_FillRect
    const float one = title_global<float>(0x0083eb20), half = title_global<float>(0x0083eb24);
    const float alpha = title_global<float>(0x0083066c);
    const float back[4] = {half, half, half, alpha};
    const float bar[4] = {one, one, one, alpha};
    fill(back, x, y, w, h);
    const float border = title_global<float>(0x0083100c), inset = title_global<float>(0x0083eba8);
    fill(bar, x + inset, y + inset, (w - border) * fraction, h - border);
}

PLATFORM_ASM_ENTRY(CG_CycleWeaponSlot_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_CycleWeaponSlot) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL CG_CycleWeaponSlot(uint32_t forward, uint32_t need_ammo) {
    // Weapon slots 1-6 (bytes at 0x00972948), up or down from the end; slot 4 is skipped
    // while 0x00929cd0 +0x20 is set. Selects the first usable one.
    const int32_t step = forward ? 1 : -1;
    const uint32_t* weapon_defs = title_global<uint32_t*>(0x0095f4d8);
    for (int32_t slot = forward ? 1 : 6;; ) {
        const int8_t weapon = title_global<int8_t>(0x00972948 + slot);
        bool usable = false;
        if (weapon && !(*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00929cd0) + 0x20) && slot == 4)) {
            if (!need_ammo) {
                usable = true;
            } else {
                const uint8_t* def = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(weapon_defs[weapon]));
                const uint32_t ammo = title_global<uint32_t>(0x009727d0 + *reinterpret_cast<const uint32_t*>(def + 0x5a0) * 4) +
                                      title_global<uint32_t>(0x00972660 + *reinterpret_cast<const uint32_t*>(def + 0x598) * 4);
                usable = ammo != 0;
            }
        }
        if (usable) {
            title_call_regs(0x002cde20, 0, static_cast<uint32_t>(static_cast<int32_t>(weapon)), 0);  // CG_SelectWeapon
            return 1;
        }
        slot += step;
        if (slot == 0 || slot == 7) return 0;
    }
}

namespace {

// Z_MallocDebug(size, "hunk", file, 0x88): the save-state copy's blocks.
void* save_block(uint32_t size) {
    return title_function<void* XB_CDECL(uint32_t, const char*, const char*, uint32_t)>(0x00042c20)(
        size, &title_global<char>(0x0083534c), &title_global<char>(0x00835354), 0x88);
}

// VM_Call(gvm, 0x14, data, size): writes a block of the save state.
void write_save_block(uint32_t data, uint32_t size) {
    TitleRegs regs{};
    regs.edx = title_global<uint32_t>(0x0095f50c);  // gvm
    const uint32_t args[3] = {0x14, data, size};
    cg_call_regs(0x002a84e0, &regs, args, 3);
}

}  // namespace

void XB_CDECL CG_CopySaveState() {
    // Per weapon from 1 (view models 0x0097606c, 0xc4 each): whether its tree has a first
    // word (byte at +0x84) and the word at +0x80; then 0x00972f00 (0x60 bytes) and the camera
    // shakes 0x0097300c (0x90 bytes).
    for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {
        uint8_t* model = &title_global<uint8_t>(0x0097606c + static_cast<uint32_t>(i - 1) * 0xc4);
        const uint32_t* tree = *reinterpret_cast<uint32_t**>(model);
        model[0x84] = tree && *tree ? 1 : 0;
        *static_cast<uint8_t*>(save_block(1)) = model[0x84];
        *static_cast<uint32_t*>(save_block(4)) = *reinterpret_cast<uint32_t*>(model + 0x80);
    }
    std::memcpy(save_block(0x60), &title_global<uint8_t>(0x00972f00), 0x60);
    std::memcpy(save_block(0x90), &title_global<uint8_t>(0x0097300c), 0x90);
}

void XB_CDECL CG_WriteSaveState() {
    for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {
        const uint32_t word = 0x009760ec + static_cast<uint32_t>(i - 1) * 0xc4;
        write_save_block(word + 4, 1);
        write_save_block(word, 4);
    }
    write_save_block(0x00972f00, 0x60);
    write_save_block(0x0097300c, 0x90);
}

uint32_t XB_CDECL CG_ConsoleCommand() {
    // Q3's CG_ConsoleCommand: the first word against cg_commands (0x0082f420, {name, function}).
    const char* text = title_global<uint32_t>(0x00934248)  // cmd_argc
                           ? title_global<const char*>(0x0092df90)  // cmd_argv[0]
                           : reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    char* buffer = &title_global<char>(0x0098b958);  // cg_argvBuffer
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buffer, text, 0xff);  // _strncpy
    buffer[0xff] = 0;
    const uint32_t* commands = &title_global<uint32_t>(0x0082f420);
    for (uint32_t i = 0; commands[i * 2]; ++i) {
        if (!title_function<int32_t XB_CDECL(const char*, const char*, int32_t)>(0x000441c0)(  // Q_stricmpn
                buffer, reinterpret_cast<const char*>(static_cast<uintptr_t>(commands[i * 2])), 99999)) {
            title_function<void XB_CDECL()>(commands[i * 2 + 1])();
            return 1;
        }
    }
    return 0;
}

void XB_CDECL CG_InitConsoleCommands() {
    const auto add = [](uint32_t name) {
        title_function<void XB_CDECL(const char*, uint32_t)>(0x002ba5c0)(  // Cmd_AddCommand
            reinterpret_cast<const char*>(static_cast<uintptr_t>(name)), 0);
    };
    for (const uint32_t* command = &title_global<uint32_t>(0x0082f420); *command; command += 2) add(*command);
    // "startCamera", "stopCamera", "setCameraOrigin", "levelshot", "stats"
    add(0x00839af4);
    add(0x00839ae8);
    add(0x00839ad8);
    add(0x00839acc);
    add(0x00839ac4);
}

void XB_CDECL CG_DrawHudElems() {
    // The used hud elements (16 of 0x7c bytes from 0x0094cc88), sorted (qsort with
    // 0x00275ab0), each drawn by CG_DrawHudElem (0x002820f0, element in ECX).
    uint32_t elems[16];
    int32_t count = 0;
    for (uint32_t elem = 0x0094cc88; elem < 0x0094d448; elem += 0x7c)
        if (title_global<uint32_t>(elem)) elems[count++] = elem;
    title_function<void XB_CDECL(void*, uint32_t, uint32_t, uint32_t)>(0x001ef880)(elems, static_cast<uint32_t>(count), 4,
                                                                                 0x00275ab0);  // _qsort
    for (int32_t i = 0; i < count; ++i) {
        TitleRegs regs{};
        regs.ecx = elems[i];
        regs.esi = static_cast<uint32_t>(count);
        regs.edi = static_cast<uint32_t>(i);
        cg_call_regs(0x002820f0, &regs, nullptr, 0);
    }
}

namespace {

// CG_SelectWeapon (0x002cde20, weapon in ESI).
void select_weapon(uint32_t weapon) { title_call_regs(0x002cde20, 0, weapon, 0); }

}  // namespace

uint32_t XB_REGS1 XB_CDECL CG_CycleOwnedWeapon(uint32_t forward, uint32_t need_ammo) {
    // Weapons 1.. up (or bg_lastWeaponIndex.. down) the player owns (bits 0x00972940) that
    // are in no inventory slot and get none (BG_GetWeaponSlot / BG_ChooseWeaponSlot): the
    // first one (with ammo when asked) is selected. Without ammo the original tries the
    // same weapon again forever.
    const uint8_t* ps = &title_global<uint8_t>(0x00972550);
    const int32_t step = forward ? 1 : -1;
    int32_t weapon = forward ? 1 : title_global<int32_t>(0x0095f4d4);
    for (;;) {
        if ((title_global<uint32_t>(0x00972940 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f))) &&
            !BG_GetWeaponSlot(ps, weapon, 1) && !BG_ChooseWeaponSlot(0, static_cast<uint32_t>(weapon), ps)) {
            if (need_ammo) {
                const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
                if (title_global<uint32_t>(0x009727d0 + *reinterpret_cast<const uint32_t*>(def + 0x5a0) * 4) +
                        title_global<uint32_t>(0x00972660 + *reinterpret_cast<const uint32_t*>(def + 0x598) * 4) ==
                    0)
                    continue;
            }
            select_weapon(static_cast<uint32_t>(weapon));
            return 1;
        }
        weapon += step;
        if (weapon == 0 || weapon == title_global<int32_t>(0x0095f4d4)) return 0;
    }
}

void XB_CDECL CG_Weapon_f() {
    // "weapon <name or index>": selects it unless there is no snapshot or flag 0x4000
    // (0x0097257c) / 0x100000 (0x00972610) blocks it.
    if (!title_global<uint32_t>(0x00971aa8)) return;
    if (title_global<uint32_t>(0x0097257c) & 0x4000) return;
    if (title_global<uint32_t>(0x00972610) & 0x100000) return;
    const char* name = CG_Argv(1);
    uint32_t weapon = 0;
    if (name && *name) weapon = BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, name)) & 0xff;
    if (!weapon) {
        weapon = static_cast<uint32_t>(
            title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(CG_Argv(1)));  // _atol
        if (!weapon) return;
    }
    select_weapon(weapon);
}

void XB_CDECL CG_ConfigStringCommand() {
    // "cs <index>": CG_ConfigStringModified (0x002867a0, index in EAX) of the second word.
    const char* text = title_global<uint32_t>(0x00934248) > 1  // cmd_argc
                           ? title_global<const char*>(0x0092df94)  // cmd_argv[1]
                           : reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    char* buffer = &title_global<char>(0x0098b958);  // cg_argvBuffer
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buffer, text, 0xff);  // _strncpy
    buffer[0xff] = 0;
    const int32_t index = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(buffer);  // _atol
    title_call_regs(0x002867a0, static_cast<uint32_t>(index), 0, 0);
}

const char* XB_REGS1 XB_CDECL CG_TranslateString(const char* text, const char* what) {
    // The localized text (Loc_GetString 0x0029d5f0, text in ESI) while localization is on
    // (0x00929b4c +0x20) and the text has two or more characters; without a translation
    // "^1UNLOCALIZED(^7text^1)^7" (0x0098bd20; the text alone with 0x00949984 off).
    const uint8_t* loc = title_global<const uint8_t*>(0x00929b4c);
    const char* result = text;
    if (loc && *reinterpret_cast<const uint32_t*>(loc + 0x20) && text[0] && text[1])
        result = reinterpret_cast<const char*>(
            static_cast<uintptr_t>(title_call_regs(0x0029d5f0, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)), 0)));
    if (result) return result;
    char* buffer = &title_global<char>(0x0098bd20);
    if (!title_global<uint32_t>(0x00949984)) {
        std::strcpy(buffer, text);
        return buffer;
    }
    if (title_global<uint32_t>(0x009491a4))
        title_function<void XB_CDECL(int32_t, const char*, const char*, const char*)>(0x002938f0)(  // __break
            7, &title_global<char>(0x00838d04), what, text);
    std::memcpy(buffer, &title_global<char>(0x00833404), 17);
    std::strcat(buffer, text);
    std::strcat(buffer, &title_global<char>(0x008333fc));
    return buffer;
}

}  // namespace game
