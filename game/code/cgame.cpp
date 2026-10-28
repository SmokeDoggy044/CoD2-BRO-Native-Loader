#include "cgame.h"

#include <cmath>
#include <cstdarg>
#include <cstring>

#include "bg_misc.h"
#include "cg_ents.h"
#include "cg_view.h"
#include "cl.h"
#include "dobj.h"
#include "fx.h"
#include "g_utils.h"
#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
#include "script_string.h"
#include "snd.h"
#include "title_data.h"
#include "tr_main.h"
#include "xanim.h"

// Calls a title function that takes EAX, ESI and EDI (tr_main.cpp).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

int32_t cg_time() { return title_global<int32_t>(0x0097253c); }
// A reference: a float returned by value passes the x87 stack (i386 ABI), which quiets a
// signaling NaN where the title's MOVSS keeps it (and its later ADDSS raises MXCSR IE).
const float& g(uint32_t address) { return title_global<float>(address); }

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

int32_t XB_REGS3 XB_CDECL CG_SetFade(int32_t duration, uint32_t, int32_t start, int32_t alpha) {
    const float value = static_cast<float>(alpha) * title_global<float>(0x0083eb30);
    title_global<int32_t>(0x009730c0) = start;
    title_global<int32_t>(0x009730c4) = duration;
    title_global<float>(0x009730b8) = value;
    if (start + duration <= cg_time()) title_global<float>(0x009730bc) = value;
    return duration;  // EAX is never written: the caller gets the duration back
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

long double XB_CDECL CG_BobCycleVertical(float cycle, float speed, float cap) {
    return platform_x87_sin_mul(cycle, bob_amplitude(speed, cap));
}

PLATFORM_ASM_ENTRY(CG_BobCycleLateral_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n" PLATFORM_ASM_CALL(CG_BobCycleLateral) "addl $12, %esp\n"
                   "ret")

long double XB_CDECL CG_BobCycleLateral(float cycle, float speed, float cap) {
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
    if (0.0L > platform_x87_sin_mul(roll_cycle, roll_scale)) {  // the unrounded x87 product (FCOMIP)
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
    CG_FillRect(color, 0.0f, 0.0f, 640.0f, 480.0f);
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
    CG_FillRect(color, -10.0f, -10.0f, 650.0f, 490.0f);
}

void XB_CDECL CG_DrawProgressBar(float x, float y, float w, float h, float fraction) {
    const auto fill = CG_FillRect;
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

namespace {

uint32_t float_bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void fill_rect(const float* color, float x, float y, float w, float h) { CG_FillRect(color, x, y, w, h); }

}  // namespace

void XB_CDECL CG_DrawCrosshair() {
    // The held weapon's crosshair (def +0x4bc name; "T..." = a small fixed cross of two
    // rectangles) centred in the refdef at its size (+0x4c4), alpha cg_crosshairAlpha
    // (0x00949590, nothing below 0.01), shader in the weapon's media (0x00976060).
    float color[4] = {g(0x0083eb20), g(0x0083eb20), g(0x0083eb20), 0.0f};
    if (!title_global<uint32_t>(0x009485d4)) return;
    if (title_global<uint32_t>(0x0094c7a4) && title_global<uint32_t>(0x0094bf6c)) return;
    if (title_global<uint32_t>(0x00972548)) return;
    if (!title_global<uint8_t>(0x008dce69)) return;
    const uint8_t* ent = G_EntHandleToEnt(&title_global<uint32_t>(0x009729b8));
    const uint32_t weapon = ent[3];
    if (!weapon) return;
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
    const char* name = *reinterpret_cast<const char* const*>(def + 0x4bc);
    if (!name[0]) return;
    if (name[0] == 'T') {
        color[0] = 0.0f;
        color[1] = 0.0f;
        color[2] = 0.0f;
        color[3] = g(0x0083eba4);
        fill_rect(color, 310.0f, 240.0f, 20.0f, 2.0f);
        fill_rect(color, 319.0f, 242.0f, 2.0f, 8.0f);
        return;
    }
    const float alpha = g(0x00949590);
    color[3] = alpha;
    if (g(0x0083eb94) > alpha) return;
    title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c))(color);
    const float size = static_cast<float>(*reinterpret_cast<const int32_t*>(def + 0x4c4));
    const float ys = g(0x009758bc), xs = g(0x009758b8);
    const float half = g(0x0083eb24);
    const float h = size * ys;
    const float w = size * xs;
    const float y = ((static_cast<float>(title_global<int32_t>(0x00972b9c)) - h) * half + ys * 0.0f) +
                    static_cast<float>(title_global<int32_t>(0x00972b94));
    const float x = ((static_cast<float>(title_global<int32_t>(0x00972b98)) - w) * half + xs * 0.0f) +
                    static_cast<float>(title_global<int32_t>(0x00972b90));
    CG_R_DrawStretchPic(title_global<uint32_t>(0x00976060 + weapon * 0xc4), float_bits(x), float_bits(y), float_bits(w),
                        float_bits(h), 0, 0, 0x3f800000, 0x3f800000);
}

PLATFORM_ASM_ENTRY(CG_DrawBar_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n" PLATFORM_ASM_CALL(CG_DrawBar) "addl $36, %esp\n"
                   "ret")

void XB_CDECL CG_DrawBar(float x, float y, float w, float h, const float* color, float fraction, uint32_t flags,
                         const float* back_color, float* full_color) {
    // A bar filled to `fraction` (flags: 4 vertical, 1 from the far end, 2 centred; 0x10 a
    // background in back_color (else white at 0.25) inset by 2, or 6/12 with 0x20, none with
    // 0x40; 0x100 the fill blended towards full_color; without 8 the alphas times 0x0094b5d8).
    const float one = g(0x0083eb20);
    float back[4] = {one, one, one, g(0x0083ec24)};
    float fill[4] = {color[0], color[1], color[2], color[3]};
    const uint32_t has_back = flags & 0x10;
    if (has_back && back_color) {
        back[0] = back_color[0];
        back[1] = back_color[1];
        back[2] = back_color[2];
        back[3] = back_color[3];
    }
    if (!(flags & 8)) {
        const float alpha = g(0x0094b5d8);
        fill[3] = alpha * fill[3];
        if (full_color) full_color[3] = full_color[3] * alpha;
        back[3] = g(0x0094b5d8) * back[3];
    }
    float blend[4];
    const uint32_t blended = flags & 0x100;
    if (blended) {
        const float rest = one - fraction;
        blend[0] = full_color[0] * fraction + rest * fill[0];
        blend[1] = full_color[1] * fraction + rest * fill[1];
        blend[2] = full_color[2] * fraction + rest * fill[2];
        blend[3] = full_color[3] * fraction + rest * fill[3];
    }
    if (has_back) {
        fill_rect(back, x, y, w, h);
        if (!(flags & 0x40)) {
            if (flags & 0x20) {
                y = y + g(0x0083ecd0);
                h = h - g(0x0083eccc);
            } else {
                x = x + g(0x0083eba8);
                y = y + g(0x0083eba8);
                w = w - g(0x0083100c);
                h = h - g(0x0083100c);
            }
        }
    }
    const float* bar = blended ? blend : fill;
    if (flags & 4) {
        if (flags & 1)
            y = (one - fraction) * h + y;
        else if (flags & 2)
            y = (one - fraction) * h * g(0x0083eb24) + y;
        fill_rect(bar, x, y, w, h * fraction);
    } else {
        if (flags & 1)
            x = (one - fraction) * w + x;
        else if (flags & 2)
            x = (one - fraction) * w * g(0x0083eb24) + x;
        fill_rect(bar, x, y, w * fraction, h);
    }
}

void XB_CDECL CG_DrawFrontBufferFade() {
    // While a front-buffer effect (start 0x00973a4c, duration 0x00973a50, on 0x00973a64)
    // lasts: the last frame as a full-screen quad in the colour 0x008d5114 (clamped to 0..1)
    // with blend mode 0x008d5124; below the fraction 0x008fefc0 left it fades out by 0x008d9d4c
    // (the first colour byte always, all four in modes 5 and 6).
    const int32_t start = title_global<int32_t>(0x00973a4c);
    const int32_t now = title_global<int32_t>(0x0097253c);
    const int32_t elapsed = static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(start));
    if (elapsed < 0) return;
    const int32_t duration = title_global<int32_t>(0x00973a50);
    if (elapsed > duration) return;
    if (!title_global<uint32_t>(0x00973a64)) return;
    const float one = g(0x0083eb20);
    const float left = one - static_cast<float>(elapsed) / static_cast<float>(duration);
    for (uint32_t c = 0x008d5114; c <= 0x008d5120; c += 4) {
        float& v = title_global<float>(c);
        if (v > one)
            v = one;
        else if (0.0f > v)
            v = 0.0f;
    }
    using ext = long double;
    const auto to_byte = [](ext v) { return static_cast<uint8_t>(static_cast<int32_t>(static_cast<int64_t>(v))); };
    const ext full = static_cast<ext>(g(0x0083eb8c));
    uint8_t r = to_byte(static_cast<ext>(g(0x008d5114)) * full);
    uint8_t gr = to_byte(static_cast<ext>(g(0x008d5118)) * full);
    uint8_t b = to_byte(static_cast<ext>(g(0x008d511c)) * full);
    uint8_t a = to_byte(static_cast<ext>(g(0x008d5120)) * full);
    const float threshold = g(0x008fefc0);
    int32_t& mark = title_global<int32_t>(0x0095f644);
    int32_t fade_time;
    if (left > threshold) {
        fade_time = 0;
        mark = 0;
        title_global<float>(0x008d9d4c) = one;
    } else {
        fade_time = mark;
    }
    if (threshold > left && fade_time == 0) {
        fade_time = duration - now + start;
        mark = fade_time;
    }
    if (fade_time)
        title_global<float>(0x008d9d4c) =
            static_cast<float>(duration - now + start) / static_cast<float>(fade_time);
    NglQuad quad;
    nglInitQuad(&quad);
    const ext scale = static_cast<ext>(g(0x008d9d4c));
    r = to_byte(static_cast<ext>(static_cast<int32_t>(r)) * scale);
    const uint32_t mode = title_global<uint32_t>(0x008d5124);
    if (mode == 5 || mode == 6) {
        gr = to_byte(static_cast<ext>(static_cast<int32_t>(gr)) * scale);
        b = to_byte(static_cast<ext>(static_cast<int32_t>(b)) * scale);
        a = to_byte(static_cast<ext>(static_cast<int32_t>(a)) * scale);
    }
    const uint32_t rgba = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(gr) << 16) |
                          (static_cast<uint32_t>(b) << 8) | a;
    nglSetQuadColor(&quad, rgba);
    nglSetQuadBlend(&quad, title_global<uint32_t>(0x008d5124), rgba);
    const float bottom = static_cast<float>(nglGetScreenHeight() + title_global<int32_t>(0x008fefb8));
    const int32_t left_edge = title_global<int32_t>(0x008fefbc);
    const float right = static_cast<float>(nglGetScreenWidth() + left_edge);
    nglSetQuadRect(&quad, static_cast<float>(left_edge), static_cast<float>(title_global<int32_t>(0x008fefb8)), right,
                   bottom);
    nglSetQuadTexture(&quad, nglGetFrontBufferTexture());
    nglListAddQuad(&quad);
}

namespace {

// Cvar_Set2("cg_viewsize", "%i" of cg_viewsize + delta, force).
void set_view_size(int32_t delta) {
    const char* text = title_function<const char* XB_CDECL(const char*, int32_t)>(0x002a93d0)(  // va
        &title_global<char>(0x0082ded4), title_global<int32_t>(0x0094c1ac) + delta);
    title_function<void XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
        &title_global<char>(0x0083428c), text, 1);
}

}  // namespace

void XB_CDECL CG_SizeDown_f() { set_view_size(-10); }

void XB_CDECL CG_SizeUp_f() { set_view_size(10); }

void XB_CDECL CG_CalcVrect() {
    // Q3's CG_CalcVrect: cg_viewsize clamped to 30..100 (the cvar set and updated), the
    // refdef centred at that percentage of the screen (0x0097589c x 0x009758a0); the height
    // percentage times 0.85 while 0x0094ad64 is set; even sizes.
    int32_t size = title_global<int32_t>(0x0094c1ac);
    const auto clamp_to = [](uint32_t text, int32_t value) {
        const uint32_t name = title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(0x0094c1a0) * 0x2c);
        title_function<void XB_CDECL(uint32_t, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
            name, &title_global<char>(text), 1);
        cg_cvar_update(0x0094c1a0);
        return value;
    };
    if (size < 30)
        size = clamp_to(0x00838a60, 30);
    else if (size > 100)
        size = clamp_to(0x00834298, 100);
    int32_t height_size = size;
    if (title_global<uint32_t>(0x0094ad64))
        height_size = static_cast<int32_t>(static_cast<float>(size) * g(0x0083f008));
    const int32_t width = title_global<int32_t>(0x0097589c), height = title_global<int32_t>(0x009758a0);
    const int32_t w = (width * size / 100) & ~1;
    const int32_t h = (height * height_size / 100) & ~1;
    title_global<int32_t>(0x00972b90) = (width - w) / 2;
    title_global<int32_t>(0x00972b9c) = h;
    title_global<int32_t>(0x00972b98) = w;
    title_global<int32_t>(0x00972b94) = (height - h) / 2;
}

PLATFORM_ASM_ENTRY(CG_ExecuteNewServerCommands_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_ExecuteNewServerCommands) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_ExecuteNewServerCommands(int32_t latest) {
    // Q3's CG_ExecuteNewServerCommands: each new server command (CL_GetServerCommand 0x002d59f0,
    // sequence in EAX) through CG_ServerCommand (0x00287ab0).
    int32_t& sequence = title_global<int32_t>(0x00971a94);
    while (sequence < latest) {
        ++sequence;
        if (title_call_regs(0x002d59f0, static_cast<uint32_t>(sequence), 0, 0))
            title_function<void XB_CDECL()>(0x00287ab0)();
    }
}

void XB_CDECL CG_UpdateScreenWhileLoading() {
    // A loading-screen refresh (SCR_UpdateScreen 0x002f1e60, re-entry guard 0x0098ad4c) at
    // most every 100 ms (Sys_Milliseconds; the last one 0x0098ad50), unless 0x00971aa4.
    if (title_global<uint32_t>(0x00971aa4) || title_global<uint32_t>(0x0098ad4c)) return;
    const int32_t now = title_function<int32_t XB_CDECL()>(0x002b1ca0)();  // Sys_Milliseconds
    const int32_t last = title_global<int32_t>(0x0098ad50);
    if (last <= now && last > now - 100) return;
    title_global<int32_t>(0x0098ad50) = now;
    title_global<int32_t>(0x0098ad4c) = 1;
    title_function<void XB_CDECL()>(0x002f1e60)();  // SCR_UpdateScreen
    --title_global<int32_t>(0x0098ad4c);
}

// The original has no frame of its own: its calls run at the entry ESP and it ends in tail
// jumps, and a callee stores a stack address in a global (0x0095e2d0 differed by the C
// frame's depth). The entry moves the C frame below a gap, the C version runs the calls at
// the original's ESP and returns the tail jump's target (0 = return).
PLATFORM_ASM_ENTRY(CG_DrawActive_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_DrawActive) "addl $4, %esp\n"
                   "popl %esp\n"
                   "testl %eax, %eax\n"
                   "jz 1f\n"
                   "jmp *%eax\n"
                   "1:\n"
                   "ret")

uint32_t XB_CDECL CG_DrawActive(uint32_t entry_esp) {
    // Q3's CG_DrawActive: without a snapshot the loading screen; else the refdef (flag 0x10
    // set while 0x009493e4) rendered (import 0x00928524), the shell shock blur and, while
    // 0x008d9d48, CG_Draw2D (0x00285f80).
    if (!title_global<uint32_t>(0x00971aa8)) return 0x00279650;  // CG_UpdateScreenWhileLoading
    uint32_t& flags = title_global<uint32_t>(0x00972be8);
    flags |= 0x10;
    if (!title_global<uint32_t>(0x009493e4)) flags &= ~0x10u;
    const TitleRegs regs{};
    const uint32_t refdef[1] = {0x00972b90};
    title_call_at_regs(entry_esp, title_global<uint32_t>(0x00928524), &regs, 1, refdef);
    title_call_at_regs(entry_esp, 0x00277c50, &regs, 0, nullptr);  // CG_DrawShellShockBlur
    return title_global<uint32_t>(0x008d9d48) ? 0x00285f80 : 0;  // CG_Draw2D
}

void XB_CDECL CG_ParseServerinfo() {
    // cgs.mapname (0x009758c4, 0x80 bytes) = "maps/<mapname>.bsp" from configstring 0.
    const uint8_t* info = title_global<const uint8_t*>(0x009606b8);
    TitleRegs regs{};
    regs.ecx = info ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info + 6)) : 0;
    regs.edi = 0x00835230;  // "mapname"
    const uint32_t value = cg_call_regs(0x002b8f50, &regs, nullptr, 0);  // Info_ValueForKey
    regs = TitleRegs{};
    regs.esi = 0x80;
    regs.edi = 0x009758c4;
    const uint32_t args[2] = {0x00835224, value};  // "maps/%s.bsp"
    cg_call_regs(0x002a9460, &regs, args, 2);  // Com_sprintf
}

void XB_CDECL CG_RegisterCvars() {
    // Q3's CG_RegisterCvars over cgameCvarTable (0x008d9d60, 0x89 entries {vmCvar, name,
    // default, flags}): Cvar_Get, then the vmCvar's handle ((cvar - 0x00940a10) / 0x2c),
    // modification count -1 and Cvar_Update.
    const uint32_t* entry = &title_global<uint32_t>(0x008d9d60);
    for (uint32_t i = 0; i < 0x89; ++i, entry += 4) {
        const uint32_t cvar = title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0029c2d0)(  // Cvar_Get
            entry[1], entry[2], entry[3]);
        int32_t* vm_cvar = reinterpret_cast<int32_t*>(static_cast<uintptr_t>(entry[0]));
        if (!vm_cvar) continue;
        vm_cvar[0] = static_cast<int32_t>(cvar - 0x00940a10) / 0x2c;
        vm_cvar[1] = -1;
        cg_cvar_update(entry[0]);
    }
}

namespace {

// The next character of `p` (advanced): with multi-byte text on (0x0093e750) and language
// cvar 0x0093f7fc in 8..11, a valid lead/trail byte pair counts as one character.
uint32_t next_text_char(const uint8_t*& p) {
    const uint8_t* language = title_global<const uint8_t*>(0x0093f7fc);
    if (title_global<uint32_t>(0x0093e750)) {
        const uint32_t kind = *reinterpret_cast<const uint32_t*>(language + 0x20) - 8;
        const uint32_t hi = p[0], lo = p[1];
        bool pair = false;
        switch (kind) {
        case 0: pair = hi >= 0xb0 && hi <= 0xc8 && lo > 0xa0 && lo < 0xff; break;
        case 1:
            pair = ((hi >= 0xa1 && hi <= 0xc6) || (hi >= 0xc9 && hi <= 0xf9)) &&
                   ((lo >= 0x40 && lo <= 0x7e) || (lo >= 0xa1 && lo <= 0xfe));
            break;
        case 2:
            pair = ((hi >= 0x81 && hi <= 0x9f) || (hi >= 0xe0 && hi <= 0xef)) &&
                   ((lo >= 0x40 && lo <= 0x7e) || (lo >= 0x80 && lo <= 0xfc));
            break;
        case 3: pair = hi >= 0xa1 && hi <= 0xf7 && lo > 0xa0 && lo < 0xff; break;
        default: break;
        }
        if (pair) {
            p += 2;
            return (hi << 8) + lo;
        }
    }
    return *p++;
}

}  // namespace

void XB_REGS1 XB_CDECL CG_CenterPrint(int32_t priority, const char* text, float seconds, int32_t y) {
    // Q3's CG_CenterPrint: unless a higher-priority print (0x00972e80) is still up, the
    // localized text into cg.centerPrint (0x00972c7c, 0x200 bytes) wrapped at a space after
    // 75 characters, its time, duration (0x00972c78, truncated) and y (0x00972c74), and the
    // line count (0x00972e7c; "\n" escapes count too).
    if (title_global<int32_t>(0x00972c70) && priority < title_global<int32_t>(0x00972e80)) return;
    const char* localized = title_function<const char* XB_CDECL(const char*, const char*, uint32_t)>(0x002d03c0)(  // SEH_LocalizeTextMessage
        text, &title_global<char>(0x00839a8c), 0);
    char* print = &title_global<char>(0x00972c7c);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(print, localized, 0x1ff);  // _strncpy
    title_global<int32_t>(0x00972e80) = priority;
    print[0x1ff] = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(print);
    int32_t count = 0;
    bool wrap = false;
    while (*p) {
        const uint32_t c = next_text_char(p);
        if (c == '\n') {
            wrap = false;
            count = 0;
            continue;
        }
        if (++count >= 0x4b) wrap = true;
        if (wrap && c == ' ') {
            const_cast<uint8_t*>(p)[-1] = '\n';
            wrap = false;
            count = 0;
        }
    }
    title_global<int32_t>(0x00972c70) = title_global<int32_t>(0x0097253c);
    title_global<int32_t>(0x00972c78) = static_cast<int32_t>(seconds);
    title_global<int32_t>(0x00972c74) = y;
    int32_t& lines = title_global<int32_t>(0x00972e7c);
    lines = 1;
    p = reinterpret_cast<const uint8_t*>(print);
    while (*p) {
        const uint32_t c = next_text_char(p);
        if (c == '\n') {
            ++lines;
        } else if (c == '\\' && *p == 'n') {
            ++lines;
            ++p;
        }
    }
}

namespace {

// Registers item `index`'s icon shader (0x0096bd10 + index * 0x34, name +0x14) into its
// record (0x00949a98 + index * 8: registered flag, shader) and, for weapon items (+0x24 1),
// the weapon (+0x28; CG_RegisterWeapon 0x002832b0).
void register_item(uint32_t index) {
    uint8_t* record = &title_global<uint8_t>(0x00949a98 + index * 8);
    const uint8_t* item = &title_global<uint8_t>(0x0096bd10 + index * 0x34);
    *reinterpret_cast<uint32_t*>(record) = 0;
    *reinterpret_cast<uint8_t**>(record + 4) = RE_RegisterShader(0, *reinterpret_cast<const char* const*>(item + 0x14));
    if (*reinterpret_cast<const int32_t*>(item + 0x24) == 1)
        title_function<void XB_CDECL(uint32_t)>(0x002832b0)(*reinterpret_cast<const uint32_t*>(item + 0x28));
    record[0] = 1;
}

}  // namespace

void XB_CDECL CG_RegisterItemVisuals(uint32_t index) {
    if (title_global<uint8_t>(0x00949a98 + index * 8)) return;
    register_item(index);
}

void XB_CDECL CG_RegisterItems() {
    // Q3's CG_RegisterItems: the items marked in configstring 8 (one hex digit per 4 items,
    // bit index & 3), 1..135, not registered yet.
    const uint8_t* string = title_global<const uint8_t*>(0x009606d8);
    const char* bits = string ? reinterpret_cast<const char*>(string + 6) : nullptr;
    char buffer[0x100];
    for (uint32_t k = 0;; ++k) {
        buffer[k] = bits[k];
        if (!bits[k]) break;
    }
    for (uint32_t index = 1; index < 0x88; ++index) {
        int32_t digit = buffer[index >> 2];
        digit = digit > '9' ? digit - 0x57 : digit - '0';
        if (!(digit & (1 << (index & 3)))) continue;
        if (title_global<uint8_t>(0x00949a98 + index * 8)) continue;
        register_item(index);
    }
}

void XB_CDECL CG_LoadViewModelAnimTrees() {
    // Each used weapon view model (0x0097606c + (weapon - 1) * 0xc4, used +0x84) registered
    // and its animation tree's root cleared (AnimTree_ClearNode_r); a missing tree is reported.
    for (int32_t weapon = 1; weapon <= title_global<int32_t>(0x0095f4d4); ++weapon) {
        uint8_t* model = &title_global<uint8_t>(0x0097606c + static_cast<uint32_t>(weapon - 1) * 0xc4);
        if (!model[0x84]) continue;
        title_function<void XB_CDECL(int32_t)>(0x002832b0)(weapon);  // CG_RegisterWeapon
        if (!*reinterpret_cast<uint32_t*>(model))
            title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<char>(0x008387c4));  // __break
        title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002c3ae0)(  // AnimTree_ClearNode_r
            **reinterpret_cast<uint32_t**>(model), 0);
    }
}

namespace {

uint32_t& wd(uint32_t address) { return title_global<uint32_t>(address); }
uint8_t bt(uint32_t address) { return title_global<uint8_t>(address); }

// CG_RegisterWeapon's frame: `s` is the original's ESP after `sub esp,0x168` and its four pushes. Its locals
// live at the original's addresses (s + 0x13 the shared-tag flag, +0x14 a model handle / temporary, +0x18 the
// weapon def, +0x1c the client weapon record, +0x20 a model handle, later the view-model tree, +0x24 the model
// count, +0x28 the memory context, +0x2c the arms tree, +0x30 the item, +0x34 four 16-byte DObj_Create models
// {model, bone name script string, 0, tree}, +0x78 the arms tree name). Each call runs at `s` (less the words
// still pushed) with the original's registers, and the original's return address is put back where the call
// left ours: the callees (model and anim loads) leave words in the dead stack that later calls read.
struct WeaponFrame {
    uint32_t s;
    uint32_t& at(uint32_t offset) const { return wd(s + offset); }
    uint32_t call(uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count = 0,
                  const uint32_t* args = nullptr, uint32_t outstanding = 0) const {
        const uint32_t top = s - outstanding * 4;
        const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
        wd(top - count * 4 - 4) = ret;
        return result;
    }
};

// The four model entries' bone-name script strings released, the last first (ScriptString_Release, ECX).
void release_bone_names(const WeaponFrame& f, uint32_t ret, TitleRegs r) {
    r.esi = f.s + 0x78;
    for (r.edi = 4; r.edi; --r.edi) {
        r.esi -= 0x10;
        r.ecx = r.esi;
        r.eax = f.call(ret, 0x0011d640, r);
    }
}

// Each entry of an anim-tree table (+4 count, +8 entries of 0x38 bytes) gets its clip (+0x24) looked up
// again: NAL_FindClip's result when it is a generic clip (vtable 0x00903f98), else 0; +0x2c cleared first.
void find_tree_clips(const WeaponFrame& f, TitleRegs& r, uint32_t table, uint32_t ret) {
    r.eax = wd(table + 4);
    r.edi = 0;
    if (!r.eax) return;
    do {
        r.eax = r.edi < wd(table + 4) ? r.edi : 0;
        r.ecx = wd(table + 8);
        r.eax = r.eax * 0x38 + r.ecx;
        r.esi = r.eax;
        wd(r.esi + 0x2c) = 0;
        const uint32_t arg = r.esi;
        r.eax = f.call(ret, 0x0008d950, r, 1, &arg);  // NAL_FindClip
        if (r.eax) {
            r.ecx = wd(r.eax);
            if (r.ecx != wd(0x00903f98)) r.eax = 0;
        }
        wd(r.esi + 0x24) = r.eax;
        r.eax = wd(table + 4);
        ++r.edi;
    } while (r.edi < r.eax);
}

// Table entry `index` (entry 0 when the table is shorter), in EAX and EDI as the original leaves them.
void tree_entry(TitleRegs& r, uint32_t table, uint32_t index) {
    r.ecx = wd(table + 4);
    r.eax = r.ecx > index ? index : 0;
    r.ecx = wd(table + 8);
    r.eax = r.eax * 0x38 + r.ecx;
    r.edi = r.eax;
}

// A new anim tree's root info: goal weight cleared below the root, the root's info (allocated when it has
// none) at weight 1 at once, its parents activated, its time reset and its next notetrack found.
void start_tree(const WeaponFrame& f, TitleRegs& r, uint32_t tree, bool first, const uint32_t (&ret)[5]) {
    r.edi = tree;
    {
        const uint32_t args[3] = {tree, 0, 0};
        r.eax = f.call(ret[0], 0x002c3e60, r, 3, args);  // AnimTree_ClearGoalWeight_r
    }
    const uint16_t index = title_global<uint16_t>(tree + 0x18);
    r.eax = (r.eax & 0xffff0000u) | index;
    wd(0x00928588) = tree;
    if (!index) {
        const uint32_t arg = tree;
        r.eax = 0;
        r.eax = f.call(ret[1], 0x002c3ed0, r, 1, &arg);  // AnimTree_AllocInfo
        r.ecx = r.eax;
    } else {
        r.eax = index;
        r.ecx = 0x00965cf0 + index * 0x30u;
    }
    const uint32_t info = r.ecx;
    const float one = g(0x0083eb20);
    const float zero = 0.0f;
    const float epsilon = g(0x0083eb4c);
    const float difference = one - title_global<float>(info + 0x24);
    std::memcpy(&f.at(0x14), &difference, 4);
    const uint32_t magnitude = f.at(0x14) & 0x7fffffffu;
    if (first)
        r.eax = magnitude;
    else
        r.edx = magnitude;
    f.at(0x14) = magnitude;
    float absolute;
    std::memcpy(&absolute, &magnitude, 4);
    const float blend = absolute * zero;
    title_global<float>(info + 0x20) = one;
    title_global<float>(info + 0x1c) = blend;
    if (epsilon > blend) {
        title_global<float>(info + 0x1c) = zero;
        title_global<float>(info + 0x24) = one;
    } else if (title_global<float>(info + 0x24) == zero) {
        title_global<float>(info + 0x24) = epsilon;
    }
    if (first) r.ebx = 0xffffffffu;
    title_global<float>(info + 0x28) = one;
    title_global<uint16_t>(info + 2) = 0xffff;
    title_global<uint16_t>(info) = 0;
    const uint32_t zeros[2] = {0, 0};
    r.eax = 0;
    r.eax = f.call(ret[2], 0x002cf220, r, 1, zeros);  // AnimTree_ActivateParents
    r.eax = f.call(ret[3], 0x002be5a0, r, 2, zeros, 1);  // AnimTree_ResetTime (ActivateParents' word still pushed)
    r.edx = 0;
    r.eax = f.call(ret[4], 0x002aebf0, r);  // AnimTree_UpdateNoteIndex
}

// __break 1 when a def's anim name (def + field) is set and table entry `index` is a looping anim (+0x34 bit 0;
// from the entry itself when it has +0x20, else from its clip, loaded first).
void check_not_looping(const WeaponFrame& f, TitleRegs& r, uint32_t def, uint32_t table, uint32_t field,
                       uint32_t index, uint32_t ret_load, uint32_t ret_break, bool name_in_edx) {
    const uint32_t name = wd(def + field);
    if (name_in_edx)
        r.edx = name;
    else
        r.eax = name;
    if (!bt(name)) return;
    tree_entry(r, table, index);
    uint32_t looping;
    if (title_global<uint16_t>(r.edi + 0x20)) {
        looping = bt(r.edi + 0x34) & 1u;
    } else {
        r.eax = wd(r.edi + 0x24);
        if (!r.eax) r.eax = f.call(ret_load, 0x002bfb90, r);  // AnimData_EnsureLoaded
        r.edi = wd(r.edi + 0x24);
        looping = r.edi ? bt(r.edi + 0x34) & 1u : 0;
    }
    r.eax = looping;
    if (!looping) return;
    if (name_in_edx)
        r.eax = wd(def + field);
    else
        r.ecx = wd(def + field);
    const uint32_t args[3] = {1, 0x008388d8, wd(def + field)};
    r.eax = f.call(ret_break, 0x002938f0, r, 3, args);  // __break
}

// The view model (def +0x14 set): the DObj from up to four models - def +0x18, def +0x14 on "tag_weapon",
// the hands (+0x1c on +0x24's bone) and the extra model (+0x20 on +0x28's bone) - with the view-model anim
// tree (def +8, the "<name>_arms" tree too when the model shares tag_b models), the anim rates (ms fields
// / clip length) in the record, and the looping checks of the two anims that must not loop. False without the
// anim tree (the original returns then).
bool register_view_model(const WeaponFrame& f, TitleRegs& r, uint32_t def, uint32_t rec) {
    r.eax = wd(def + 0x18);
    r.esi = 0;
    if (!r.eax || !bt(r.eax)) {
        r.edx = wd(def + 0xc);
        const uint32_t args[3] = {1, 0x00838954, r.edx};
        r.eax = f.call(0x002833bc, 0x002938f0, r, 3, args);  // __break
    }
    r.eax = wd(def + 0x30);
    if (!r.eax || !bt(r.eax)) {
        r.eax = wd(def + 0xc);
        const uint32_t args[3] = {1, 0x00838920, r.eax};
        r.eax = f.call(0x002833db, 0x002938f0, r, 3, args);  // __break
    }
    r.ecx = wd(0x0095f638);
    r.edx = wd(r.ecx + 0x34);
    r.ecx = wd(0x0095f6ac);
    {
        const uint32_t args[3] = {r.ecx, f.s + 0x20, r.edx};
        r.eax = wd(def + 0x18);
        r.eax = f.call(0x002833fc, 0x00241f40, r, 3, args);  // R_RegisterModel (name in EAX, ret 0xc)
    }
    r.edx = f.at(0x20);
    r.ecx = f.s + 0x38;
    f.at(0x34) = r.edx;
    {
        const uint32_t arg = 0;
        r.eax = f.call(0x0028340e, 0x0011e0f0, r, 1, &arg);  // ScrCall_sprintf
    }
    r.eax = wd(0x0095f638);
    f.at(0x3c) = 0;
    r.ecx = wd(r.eax + 0x34);
    r.eax = wd(0x0095f6ac);
    r.edx = f.s + 0x20;
    {
        const uint32_t args[3] = {r.eax, r.edx, r.ecx};
        r.eax = wd(def + 0x14);
        r.eax = f.call(0x0028342e, 0x00241f40, r, 3, args);  // R_RegisterModel
    }
    r.ecx = f.at(0x20);
    f.at(0x44) = r.ecx;
    r.ecx = f.s + 0x48;
    {
        const uint32_t arg = 0x00838914;  // "tag_weapon"
        r.eax = f.call(0x00283444, 0x0011dd20, r, 1, &arg);  // ScriptString_AssignCString
    }
    r.eax = f.at(0x44);
    f.at(0x4c) = 0;
    title_global<uint8_t>(f.s + 0x13) = 0;
    if (r.eax) {
        r.esi = wd(r.eax + 0x20);
        r.eax = wd(r.esi + 0x38);
        if (r.eax) {
            r.eax = f.call(0x00283464, 0x00278d80, r);  // CG_ShareWeaponTagBModels
            title_global<uint8_t>(f.s + 0x13) = 1;
        }
    }
    r.esi = def;
    r.eax = wd(def + 0x1c);
    f.at(0x24) = 2;
    if (bt(r.eax)) {
        r.edx = wd(0x0095f638);
        r.ecx = wd(r.edx + 0x34);
        const uint32_t args[4] = {f.s + 0x14, r.eax, r.ecx, 6};
        r.eax = f.s + 0x14;
        f.at(0x24) = 3;
        r.eax = f.call(0x0028349c, 0x00243fb0, r, 4, args);  // RE_RegisterModel
        r.ecx = wd(r.eax);
        f.at(0x54) = r.ecx;
        r.edx = wd(def + 0x24);
        r.ecx = f.s + 0x58;
        const uint32_t arg = r.edx;
        r.eax = f.call(0x002834b2, 0x0011dd20, r, 1, &arg);  // ScriptString_AssignCString
        f.at(0x5c) = 0;
    }
    r.ecx = wd(def + 0x20);
    if (bt(r.ecx)) {
        r.edi = def;
        r.eax = wd(def + 0x1c);
        const uint8_t hands = bt(r.eax);
        r.edx = (r.edx & ~0xffu) | hands;
        f.at(0x24) = f.at(0x24) + 1;
        r.esi = hands ? 3 : 2;
        r.edx = wd(0x0095f638);
        r.eax = wd(r.edx + 0x34);
        const uint32_t args[4] = {f.s + 0x14, r.ecx, r.eax, 6};
        r.eax = f.s + 0x14;
        r.eax = f.call(0x002834f9, 0x00243fb0, r, 4, args);  // RE_RegisterModel
        r.ecx = wd(r.eax);
        r.esi <<= 4;
        f.at(0x34 + r.esi) = r.ecx;
        r.edx = wd(def + 0x28);
        r.ecx = f.s + 0x38 + r.esi;
        const uint32_t arg = r.edx;
        r.eax = f.call(0x00283512, 0x0011dd20, r, 1, &arg);  // ScriptString_AssignCString
        f.at(0x3c + r.esi) = 0;
        r.esi = def;
    }
    r.eax = wd(def + 8);
    r.ecx = wd(0x0095f524);
    uint32_t table;
    {
        const uint32_t arg = r.eax;
        r.eax = wd(r.ecx + 4);
        table = r.eax = f.call(0x00283530, 0x00026050, r, 1, &arg);  // NameTable12_FindFrom1 (table in EAX, ret 4)
    }
    if (!table) {
        // No anim tree: the model entries destroyed (vector_call_iterator, stdcall) and the function returns.
        r.edx = f.s + 0x34;
        const uint32_t args[4] = {f.s + 0x34, 0x10, 4, 0x0001ec00};
        f.call(0x00283549, 0x00011280, r, 4, args);
        return false;
    }
    find_tree_clips(f, r, table, 0x00283580);
    const bool shared = bt(f.s + 0x13) != 0;
    r.eax = (r.eax & ~0xffu) | bt(f.s + 0x13);
    r.esi = def;
    if (shared) {
        r.edx = wd(def + 8);
        const uint32_t args[4] = {f.s + 0x78, 0x0082d800, r.edx, 0x0083890c};  // "%s%s", name, "_arms"
        r.eax = f.s + 0x78;
        r.eax = f.call(0x002835c5, 0x001ee901, r, 4, args);  // _sprintf
        r.edx = wd(0x0095f524);
        r.eax = wd(r.edx + 4);
        r.ecx = f.s + 0x78;
        const uint32_t arg = r.ecx;
        r.ebx = r.eax = f.call(0x002835db, 0x00026050, r, 1, &arg);  // NameTable12_FindFrom1
        find_tree_clips(f, r, r.ebx, 0x00283606);
        r.esi = def;
    }
    {
        const uint32_t args[2] = {0, table};
        r.eax = f.call(0x00283630, 0x002c4a00, r, 2, args);  // AnimTree_Create
        f.at(0x20) = r.eax;
    }
    r.eax = (r.eax & ~0xffu) | bt(f.s + 0x13);
    if (shared) {
        const uint32_t args[2] = {0, r.ebx};
        r.eax = f.call(0x00283647, 0x002c4a00, r, 2, args);  // AnimTree_Create
        f.at(0x2c) = r.eax;
    }
    r.ebx = rec;
    r.edx = 0x3f800000;
    for (uint32_t offset = 4; offset <= 0x64; offset += 4) wd(rec + offset) = 0x3f800000;
    // Anim rates: 1000 x the clip's length (1 without a clip) / the def's time (ms) for that anim, 0 when the
    // time is not positive.
    struct Rate {
        uint32_t field, index, out, ret;
    };
    static const Rate kRates[10] = {
        {0x5e8, 5, 0x18, 0x002836d6},  {0x5ec, 8, 0x24, 0x00283746},  {0x5f0, 9, 0x28, 0x002837ac},
        {0x5f4, 10, 0x2c, 0x00283812}, {0x5fc, 11, 0x30, 0x00283878}, {0x5f0, 12, 0x34, 0x002838de},
        {0x60c, 13, 0x38, 0x00283944}, {0x608, 14, 0x3c, 0x002839aa}, {0x614, 15, 0x40, 0x00283a10},
        {0x610, 16, 0x44, 0x00283a76},
    };
    for (const Rate& rate : kRates) {
        r.eax = wd(def + rate.field);
        float value = 0.0f;
        if (static_cast<int32_t>(r.eax) > 0) {
            tree_entry(r, table, rate.index);
            r.eax = wd(r.edi + 0x24);
            if (!r.eax) r.eax = f.call(rate.ret, 0x002bfb90, r);  // AnimData_EnsureLoaded
            r.edi = wd(r.edi + 0x24);
            value = r.edi ? title_global<float>(r.edi + 0x38) : g(0x0083eb20);
            value = value * g(0x0083eb9c);
            value = value / static_cast<float>(title_global<int32_t>(def + rate.field));
        }
        title_global<float>(rec + rate.out) = value;
    }
    {
        static const uint32_t kRets[5] = {0x00283ab9, 0x00283ad3, 0x00283b69, 0x00283b72, 0x00283b7c};
        const uint32_t tree = f.at(0x20);
        start_tree(f, r, tree, true, kRets);
        f.at(0x40) = tree;
    }
    r.eax = (r.eax & ~0xffu) | bt(f.s + 0x13);
    if (shared) {
        static const uint32_t kRets[5] = {0x00283b9a, 0x00283bb4, 0x00283c48, 0x00283c51, 0x00283c5b};
        const uint32_t tree = f.at(0x2c);
        start_tree(f, r, tree, false, kRets);
        f.at(0x50) = tree;
    }
    check_not_looping(f, r, def, table, 0x88, 0x17, 0x00283c9f, 0x00283cc9, false);
    check_not_looping(f, r, def, table, 0x8c, 0x18, 0x00283d0c, 0x00283d36, true);
    // The DObj: 0xa0 bytes from the memory context (MemContext_Alloc_r, ECX = context, ret 8), else the heap.
    r.eax = f.at(0x28);
    bool heap = true;
    if (r.eax != r.ebx) {
        const int32_t context = static_cast<int32_t>(r.eax);
        r.ecx = context >= 0 && context < 0x63 ? wd(wd(0x0095f638) + static_cast<uint32_t>(context) * 4 + 0x38) : 0;
        const uint32_t args[2] = {0x10, 0xa0};
        r.eax = f.call(0x00283d64, 0x0028c0f0, r, 2, args);  // MemContext_Alloc_r
        heap = !r.eax;
    }
    if (heap) {
        const uint32_t args[2] = {0x10, 0xa0};
        r.eax = f.call(0x00283d74, 0x00042c50, r, 2, args);  // Heap_AllocAligned
    }
    r.esi = r.eax;
    if (r.esi) {
        r.eax = f.at(0x28);
        r.esi = r.eax = f.call(0x00283d86, 0x0023aff0, r);  // DObj_Ctor
    }
    {
        r.edx = f.at(0x24);
        r.eax = f.at(0x20);
        const uint32_t args[2] = {r.edx, r.esi};
        r.ecx = 0;
        r.edx = f.s + 0x34;
        r.eax = f.call(0x00283da1, 0x00245ff0, r, 2, args);  // DObj_Create
    }
    wd(rec) = r.esi;
    r.esi = rec + 0x68;
    r.eax = def;
    r.ecx = wd(def + 0x18);
    {
        const uint32_t args[3] = {rec + 0x68, r.ecx, 0x17};
        r.eax = f.call(0x00283dba, 0x001eed80, r, 3, args, 2);  // _strncpy (DObj_Create's words still pushed)
    }
    r.edi = def;
    title_global<uint8_t>(rec + 0x7f) = 0;
    r.esi = rec;
    return true;
}

// The world models (def +0x57c, +0x580: the second defaults to the first), the item icon and its
// "<icon>_select" version, the def's shaders, and the display, mode and AI overlay names translated
// (cl_languagetranslate 0x00929b4c: Loc_HashKey in the localized strings; "NO STRING" for an entry
// without text), each falling back to the untranslated text (__break 7 with the two dev flags set).
void register_weapon_assets(const WeaponFrame& f, TitleRegs& r, uint32_t def, uint32_t rec) {
    r.eax = wd(def + 0x57c);
    if (bt(r.eax)) {
        r.edx = wd(0x0095f638);
        r.ecx = wd(r.edx + 0x34);
        const uint32_t args[3] = {wd(0x0095f6ac), f.s + 0x14, r.ecx};
        r.ecx = wd(0x0095f6ac);
        r.edx = f.s + 0x14;
        r.eax = f.call(0x00283def, 0x00241f40, r, 3, args);  // R_RegisterModel
        r.edx = f.at(0x14);
        wd(rec + 0x98) = r.edx;
    }
    r.eax = wd(def + 0x580);
    if (bt(r.eax)) {
        r.ecx = wd(0x0095f638);
        r.edx = wd(r.ecx + 0x34);
        const uint32_t args[3] = {wd(0x0095f6ac), f.s + 0x14, r.edx};
        r.ecx = f.s + 0x14;
        r.edx = wd(0x0095f6ac);
        r.eax = f.call(0x00283e1f, 0x00241f40, r, 3, args);  // R_RegisterModel
        r.eax = f.at(0x14);
        wd(rec + 0x9c) = r.eax;
    } else {
        r.ecx = wd(rec + 0x98);
        wd(rec + 0x9c) = r.ecx;
    }
    r.ebx = f.at(0x30);
    r.edx = wd(r.ebx + 0x14);
    r.eax = f.call(0x00283e43, 0x0023ec30, r);  // RE_RegisterShader (name in EDX)
    wd(rec + 0xa0) = r.eax;
    r.edx = wd(r.ebx + 0x14);
    {
        const uint32_t args[2] = {0x008388cc, r.edx};  // "%s_select"
        r.eax = f.call(0x00283e57, 0x002a93d0, r, 2, args);  // va
    }
    r.edx = r.eax;
    r.eax = f.call(0x00283e61, 0x0023ec30, r);  // RE_RegisterShader
    wd(rec + 0xa4) = r.eax;
    r.edx = wd(r.ebx + 0x18);
    r.eax = f.call(0x00283e6f, 0x0023ec30, r);  // RE_RegisterShader
    wd(rec + 0xa8) = r.eax;
    struct Shader {
        uint32_t field, out, ret;
    };
    static const Shader kShaders[5] = {
        {0x4bc, 0xb8, 0x00283e85}, {0x4c0, 0xbc, 0x00283e9b}, {0x634, 0xc0, 0x00283eb1},
        {0x584, 0xac, 0x00283ec7}, {0x58c, 0xb0, 0x00283edd},
    };
    for (const Shader& shader : kShaders) {
        r.edx = wd(def + shader.field);
        if (!bt(r.edx)) continue;
        r.eax = f.call(shader.ret, 0x0023ec30, r);  // RE_RegisterShader
        wd(rec + shader.out) = r.eax;
    }
    struct Text {
        uint32_t field, out, format, ret_hash, ret_find, ret_break;
    };
    static const Text kTexts[3] = {
        {0x0c, 0x8c, 0x00838898, 0x00283f10, 0x00283f18, 0x00283f68},
        {0x90, 0x90, 0x00838868, 0x00283f9e, 0x00283fa6, 0x00283ff9},
        {0x10, 0x94, 0x0083882c, 0x0028402f, 0x00284037, 0x00284081},
    };
    r.ecx = wd(0x00929b4c);
    for (uint32_t k = 0; k < 3; ++k) {
        const Text& text = kTexts[k];
        r.eax = wd(def + text.field);
        bool translate = false;
        if (r.ecx) {
            r.edx = wd(r.ecx + 0x20);
            if (r.edx && bt(r.eax)) {
                const uint8_t second = bt(r.eax + 1);
                if (k < 2)
                    r.edx = (r.edx & ~0xffu) | second;
                else
                    r.ecx = (r.ecx & ~0xffu) | second;
                translate = second != 0;
            }
        }
        if (translate) {
            r.edi = wd(0x0095f5d8);
            r.esi = r.eax;
            r.eax = f.call(text.ret_hash, 0x00024440, r);  // Loc_HashKey (text in ESI)
            const uint32_t arg = r.eax;
            r.eax = r.edi;
            r.eax = f.call(text.ret_find, 0x002984f0, r, 1, &arg);  // Zone_FindByHash (ret 4)
            if (r.eax) {
                r.esi = wd(r.eax + 8);
                if (!r.esi) r.esi = 0x008379b4;  // "NO STRING"
            }
            if (k < 2) r.ecx = wd(0x00929b4c);
            r.edi = def;
            r.eax = r.esi;
            r.esi = rec;
        }
        wd(rec + text.out) = r.eax;
        if (r.eax) continue;
        if (wd(0x00949984) && wd(0x009491a4)) {
            // (The original keeps the cvar in ECX across this __break; it is not reached in the shipped data.)
            const uint32_t args[4] = {7, text.format, wd(def + 8), wd(def + text.field)};
            r.eax = k == 1 ? args[2] : args[3];
            if (k == 0) r.edx = args[2];
            if (k == 1) r.edx = args[3];
            if (k == 2) r.ecx = args[2];
            r.eax = f.call(text.ret_break, 0x002938f0, r, 4, args);  // __break
        }
        if (k == 0)
            r.eax = wd(def + text.field);
        else
            r.edx = wd(def + text.field);
        wd(rec + text.out) = wd(def + text.field);
    }
}

}  // namespace

// Every call runs at the original's ESP with its registers and the locals at the original's frame addresses
// (the model entries DObj_Create reads, the arms tree name, the model handles): the entry moves the C frame
// below a gap (the model and anim loads make file calls) and hands over the entry ESP and the registers.
PLATFORM_ASM_ENTRY(CG_RegisterWeapon_entry,
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl 28(%esp)\n" PLATFORM_ASM_CALL(CG_RegisterWeapon) "addl $32, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_RegisterWeapon(uint32_t entry_esp, uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
                                uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's CG_RegisterWeapon (cdecl weapon): once per weapon (client record 0x00975fa8 + weapon * 0xc4, +0x85
    // registered) with a def (bg_weaponDefs 0x0095f4d8): the record cleared, its item (0x0096bd10 + weapon *
    // 0x34, +0x88) registered, the view model when the def has one (+0x80 = -1), then the other assets. The
    // four bone-name script strings of the model entries live across the whole function.
    const WeaponFrame f{entry_esp - 0x178};
    f.at(0xc) = ebx;
    f.at(8) = ebp;
    f.at(4) = esi;
    f.at(0) = edi;
    const uint32_t weapon = wd(entry_esp + 4);
    TitleRegs r{eax, 0, ecx, edx, esi, edi};
    for (uint32_t k = 0; k < 4; ++k) {
        r.esi = f.s + 0x40 + k * 0x10;
        r.edi = 4 - k;
        r.ecx = r.esi - 8;
        f.at(0x34 + k * 0x10) = 0;
        const uint32_t arg = 0;
        r.eax = f.call(0x002832dc, 0x0011d5d0, r, 1, &arg);  // ScriptString_FromRep
        f.at(0x40 + k * 0x10) = 0;
    }
    r.esi = f.s + 0x80;
    r.edi = 0;
    r.eax = wd(0x0095f638);
    r.ecx = wd(r.eax + 0x34);
    f.at(0x2c) = 0;
    f.at(0x28) = r.ecx;
    if (!weapon) {
        release_bone_names(f, 0x0028330d, r);
        return;
    }
    r.edx = wd(0x0095f4d8);
    const uint32_t def = r.ecx = wd(r.edx + weapon * 4);
    const uint32_t rec = r.eax = 0x00975fa8 + weapon * 0xc4;
    const uint8_t registered = bt(rec + 0x85);
    r.edx = (r.edx & ~0xffu) | registered;
    f.at(0x1c) = rec;
    f.at(0x18) = def;
    if (registered || !def) {
        release_bone_names(f, 0x002840ca, r);
        return;
    }
    r.esi = rec;
    for (uint32_t k = 0; k < 0x31; ++k) wd(rec + k * 4) = 0;
    r.ecx = 0;
    r.edi = rec + 0xc4;
    const uint32_t item = r.eax = weapon * 0x34 + 0x0096bd10;
    title_global<uint8_t>(rec + 0x85) = 1;
    f.at(0x30) = item;
    wd(rec + 0x88) = item;
    r.eax = f.call(0x00283380, 0x00285d50, r, 1, &weapon);  // CG_RegisterItemVisuals
    r.edi = def;
    wd(rec + 0x80) = 0xffffffffu;
    r.ecx = wd(def + 0x14);
    r.eax = (r.eax & ~0xffu) | bt(r.ecx);
    if (bt(r.ecx) && !register_view_model(f, r, def, rec)) return;
    register_weapon_assets(f, r, def, rec);
    release_bone_names(f, 0x002840a0, r);
}

void XB_REGS1 XB_CDECL CG_DrawObjectiveList(const float* color, float alpha) {
    // The objectives screen: the "CGAME_MISSIONOBJECTIVES" title underlined (shadow at 40,101
    // in black, line at 39,100 in `color`), then up to the last 6 objectives (0x009730c8,
    // 16 of 0x98 bytes: state +0, text +0x18) with their state's icon (1/4 0x00975c5c, 3
    // 0x00975c60, 5 0x00975c64) and colour, on a dark panel sized to the widest line.
    const float xs = g(0x009758b8), ys = g(0x009758bc);
    const float grey = g(0x0083eba4);
    const float done[4] = {grey, grey, grey, alpha};             // state 1
    const float current[4] = {grey, g(0x0083eb20), grey, alpha};  // state 4
    const float failed[4] = {g(0x0083ec44), g(0x0083066c), g(0x0083066c), alpha};  // state 5
    const float dim[4] = {g(0x0083f064), g(0x0083f064), g(0x0083f064), alpha};      // state 3
    const float shadow[4] = {0.0f, 0.0f, 0.0f, alpha};
    const auto font_height = title_function<int32_t XB_CDECL(uint32_t, float)>(title_global<uint32_t>(0x00928574));
    const auto set_color = title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c));
    using StretchPic = void XB_CDECL(float, float, float, float, float, float, float, float, uint32_t);
    const auto stretch_pic = title_function<StretchPic>(title_global<uint32_t>(0x00928530));
    using DrawText = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, uint32_t, uint32_t, uint32_t);
    const auto draw_text = title_function<DrawText>(title_global<uint32_t>(0x00928578));
    const float small = 0.28f, large = 0.45f;
    const float line_height = static_cast<float>(font_height(0, small));
    const char* title = CG_TranslateString(&title_global<char>(0x00838c8c), &title_global<char>(0x0083521c));
    const float title_width = static_cast<float>(RE_TextWidth(title, 0, large));
    const uint32_t white = title_global<uint32_t>(0x00975c3c);
    set_color(shadow);
    stretch_pic(xs * g(0x0083ec84), ys * g(0x0083f060), title_width * xs, ys, 0.0f, 0.0f, 0.0f, 1.0f, white);
    set_color(nullptr);
    set_color(color);
    stretch_pic(xs * g(0x0083f05c), ys * g(0x0083eb90), title_width * xs, ys, 0.0f, 0.0f, 0.0f, 1.0f,
                title_global<uint32_t>(0x00975c3c));
    set_color(nullptr);
    draw_text(39.0f, 99.0f, 0, large, color, title, 0, 0, 3);
    float y = static_cast<float>(font_height(0, large)) + g(0x0083f058);
    float widest = static_cast<float>(RE_TextWidth(title, 0, large));
    int32_t shown[16];
    int32_t count = 0;
    for (int32_t i = 0; i < 16; ++i) {
        const uint8_t* objective = &title_global<uint8_t>(0x009730c8 + i * 0x98);
        uint32_t icon = 0;
        switch (*reinterpret_cast<const int32_t*>(objective)) {
        case 1:
        case 4: icon = title_global<uint32_t>(0x00975c5c); break;
        case 3: icon = title_global<uint32_t>(0x00975c60); break;
        case 5: icon = title_global<uint32_t>(0x00975c64); break;
        default: continue;
        }
        if (icon && objective[0x18]) shown[count++] = i;
    }
    int32_t first = 0;
    if (count > 6) {
        first = count - 6;
        count = 6;
    }
    const int32_t end = first + count;
    const float four = g(0x0083100c);
    for (int32_t k = first; k < end; ++k) {
        const uint8_t* objective = &title_global<uint8_t>(0x009730c8 + shown[k] * 0x98);
        uint32_t icon;
        const float* line_color;
        switch (*reinterpret_cast<const int32_t*>(objective)) {
        case 1: icon = title_global<uint32_t>(0x00975c5c); line_color = done; break;
        case 2: y = line_height + y + four; continue;
        case 3: icon = title_global<uint32_t>(0x00975c60); line_color = dim; break;
        case 4: icon = title_global<uint32_t>(0x00975c5c); line_color = current; break;
        case 5: icon = title_global<uint32_t>(0x00975c64); line_color = failed; break;
        default: continue;
        }
        const char* text = reinterpret_cast<const char*>(objective + 0x18);
        if (!*text) continue;
        if (icon) {
            const float twelve = g(0x0083eccc);
            const float icon_x = xs * g(0x0083ebc4);
            const float icon_w = xs * twelve;
            const float icon_y = ((y - twelve) - g(0x0083eb20)) * ys;
            const float icon_h = ys * twelve;
            set_color(line_color);
            stretch_pic(static_cast<float>(title_global<int32_t>(0x00972b90)) + icon_x,
                        static_cast<float>(title_global<int32_t>(0x00972b94)) + icon_y, icon_w, icon_h, 0.0f, 0.0f, 1.0f,
                        1.0f, icon);
            set_color(nullptr);
        }
        const char* localized = title_function<const char* XB_CDECL(const char*, const char*, uint32_t)>(0x002d03c0)(  // SEH_LocalizeTextMessage
            text, &title_global<char>(0x00838c7c), 0);
        draw_text(39.0f, y - g(0x0083eba8), 0, small, line_color, localized, 0, 0, 3);
        const float width = static_cast<float>(RE_TextWidth(localized, 0, small));
        if (width > widest) widest = width;
        y = line_height + y + four;
    }
    const float panel[4] = {g(0x0083ebf8), g(0x0083ebf8), g(0x0083ebf8), g(0x0083eb58)};
    const float w = (widest + g(0x0083f05c)) + g(0x0083ebc4);
    const float h = (line_height + four) * static_cast<float>(count) + g(0x0083f054);
    R_DrawFilledRect(panel, 14.0f, 59.0f, w, h, 5.0f);
}

namespace {

const uint8_t* weapon_def(int32_t weapon) { return title_global<const uint8_t* const*>(0x0095f4d8)[weapon]; }

bool owns_weapon(int32_t weapon) {
    return (title_global<uint32_t>(0x00972940 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f))) != 0;
}

// The ammo left for a weapon: clip (+0x5a0 -> 0x009727d0) plus stock (+0x598 -> 0x00972660).
uint32_t weapon_ammo(int32_t weapon) {
    const uint8_t* def = weapon_def(weapon);
    return title_global<uint32_t>(0x009727d0 + *reinterpret_cast<const uint32_t*>(def + 0x5a0) * 4) +
           title_global<uint32_t>(0x00972660 + *reinterpret_cast<const uint32_t*>(def + 0x598) * 4);
}

// Cvar_Set2(the vmCvar's name, value, 1) and Cvar_Update.
void set_vm_cvar(uint32_t vm_cvar, const char* value) {
    title_function<void XB_CDECL(uint32_t, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
        title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(vm_cvar) * 0x2c), value, 1);
    cg_cvar_update(vm_cvar);
}

const char* va_int(int32_t value) {
    return title_function<const char* XB_CDECL(const char*, int32_t)>(0x002a93d0)(&title_global<char>(0x0082ded4), value);
}

// The weapon commands' guard: a snapshot, no flag 0x4000 (0x0097257c) and none of `flags` in
// 0x00972610, and the switch delay (0x0094abb4) passed since 0x0094c23c.
bool weapon_command_allowed(uint32_t flags) {
    if (!title_global<uint32_t>(0x00971aa8)) return false;
    if (title_global<uint32_t>(0x0097257c) & 0x4000) return false;
    if (title_global<uint32_t>(0x00972610) & flags) return false;
    return title_global<int32_t>(0x0097253c) - title_global<int32_t>(0x0094c23c) >= title_global<int32_t>(0x0094abb4);
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_SelectWeapon_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_SelectWeapon) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL CG_SelectWeapon(int32_t weapon) {
    // The weapon change request: its time (vmCvar 0x0094c230), the weapon (0x00947f98) and,
    // unless it is the current weapon's alternate (+0x718), the switch flag (0x0094a3b8 = 1).
    // 0 while 0x0095f91f or when it is already the current weapon (0x00947fa4).
    if (title_global<uint8_t>(0x0095f91f)) return 0;
    set_vm_cvar(0x0094c230, va_int(title_global<int32_t>(0x0097253c)));
    const int32_t current = title_global<int32_t>(0x00947fa4);
    if (current == weapon) return 0;
    const bool alternate = weapon && weapon == *reinterpret_cast<const int32_t*>(weapon_def(current) + 0x718);
    set_vm_cvar(0x00947f98, va_int(weapon));
    if (!alternate) set_vm_cvar(0x0094a3b8, &title_global<char>(0x0082e058));
    return 1;
}

void XB_CDECL CG_CycleWeapon(uint32_t forward, uint32_t need_ammo) {
    // Q3's CG_NextWeapon/CG_PrevWeapon: from the current weapon's inventory slot, the next
    // slot (1..6, wrapping; slot 4 skipped while 0x00929cd0 +0x20) holding a weapon (with ammo
    // when asked); else an owned weapon outside the slots or CG_CycleWeaponSlot. Without a
    // slot: the next owned weapon by index (not in the current one's alternate chain, slotless,
    // with ammo when asked, not slot type 4 while 0x00929cd0 +0x20). The weapon 0 if the
    // current one is no longer owned.
    if (!title_global<uint32_t>(0x00971aa8)) return;
    const int32_t step = forward ? 1 : -1;
    const int32_t stop_slot = forward ? 1 : 6;
    const int32_t stop_weapon = forward ? 1 : title_global<int32_t>(0x0095f4d4);
    const uint8_t* ps = &title_global<uint8_t>(0x00972550);
    int32_t current = title_global<int32_t>(0x00947fa4);
    uint32_t slot = BG_GetWeaponSlot(ps, current, 1);
    if (!slot) slot = BG_ChooseWeaponSlot(0, static_cast<uint32_t>(current), ps);
    if (title_global<uint32_t>(0x009725e0)) {
        FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), title_global<uint32_t>(0x009725e0));
        current = title_global<int32_t>(0x00947fa4);
    }
    if (slot) {
        for (int32_t s = (static_cast<int32_t>(slot) + step + 5) % 6 + 1; s != stop_slot; s = (s + step + 5) % 6 + 1) {
            const int8_t weapon = title_global<int8_t>(0x00972948 + s);
            if (!weapon) continue;
            if (*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00929cd0) + 0x20) && s == 4) continue;
            if (need_ammo && !weapon_ammo(weapon)) continue;
            select_weapon(static_cast<uint32_t>(static_cast<int32_t>(weapon)));
            return;
        }
        if (CG_CycleOwnedWeapon(forward, need_ammo)) return;
        CG_CycleWeaponSlot(forward, need_ammo);
    } else {
        const int32_t base = title_global<int32_t>(0x0095f4d4) + step - 1;
        int32_t weapon = current;
        for (;;) {
            weapon = (base + weapon) % title_global<int32_t>(0x0095f4d4) + 1;
            if (weapon == stop_weapon) {
                if (CG_CycleWeaponSlot(forward, need_ammo)) return;
                CG_CycleOwnedWeapon(forward, need_ammo);
                break;
            }
            if (!owns_weapon(weapon)) continue;
            bool chained = false;
            for (int32_t alt = *reinterpret_cast<const int32_t*>(weapon_def(weapon) + 0x718); alt;
                 alt = *reinterpret_cast<const int32_t*>(weapon_def(alt) + 0x718)) {
                if (alt == title_global<int32_t>(0x00947fa4)) {
                    chained = true;
                    break;
                }
                if (alt == weapon) break;
            }
            if (chained) continue;
            if (BG_GetWeaponSlot(ps, weapon, 1)) continue;
            if (BG_ChooseWeaponSlot(0, static_cast<uint32_t>(weapon), ps)) continue;
            if (need_ammo && !weapon_ammo(weapon)) continue;
            if (*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00929cd0) + 0x20) &&
                *reinterpret_cast<const int32_t*>(weapon_def(weapon) + 0x9c) == 4)
                continue;
            select_weapon(static_cast<uint32_t>(weapon));
            return;
        }
    }
    if (!owns_weapon(title_global<int32_t>(0x00947fa4))) select_weapon(0);
}

void XB_CDECL CG_OutOfAmmoChange() {
    // Q3's CG_OutOfAmmoChange: an owned slot weapon (+0xa0) of the current weapon's slot type
    // (+0x9c, not class 5) with ammo; else the first of slots 1-4 with ammo (slot 4 skipped
    // while 0x00929cd0 +0x20) when the current weapon has a slot; else 0x009725e4 when it has a
    // slot and ammo; else CG_CycleWeapon(1, 1).
    if (!title_global<uint32_t>(0x00971aa8)) return;
    const uint8_t* current = title_global<const uint8_t*>(0x00972acc);
    if (*reinterpret_cast<const uint32_t*>(current + 0xa0)) {
        const int32_t last = title_global<int32_t>(0x0095f4d4);
        for (int32_t weapon = 1; weapon <= last; ++weapon) {
            if (!owns_weapon(weapon)) continue;
            const uint8_t* def = weapon_def(weapon);
            if (!*reinterpret_cast<const uint32_t*>(def + 0xa0)) continue;
            if (*reinterpret_cast<const int32_t*>(def + 0x98) == 5) continue;
            if (*reinterpret_cast<const int32_t*>(def + 0x9c) != *reinterpret_cast<const int32_t*>(current + 0x9c)) continue;
            if (weapon_ammo(weapon)) {
                select_weapon(static_cast<uint32_t>(weapon));
                return;
            }
        }
    }
    const uint8_t* ps = &title_global<uint8_t>(0x00972550);
    if (BG_GetWeaponSlot(ps, *reinterpret_cast<const int32_t*>(current), 1)) {
        for (int32_t s = 1; s <= 4; ++s) {
            const int8_t weapon = title_global<int8_t>(0x00972948 + s);
            if (!weapon) continue;
            if (*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00929cd0) + 0x20) && s == 4) continue;
            if (weapon_ammo(weapon)) {
                select_weapon(static_cast<uint32_t>(static_cast<int32_t>(weapon)));
                return;
            }
        }
    }
    const int32_t fallback = title_global<int32_t>(0x009725e4);
    if (BG_GetWeaponSlot(ps, fallback, 1) && weapon_ammo(fallback)) {
        select_weapon(static_cast<uint32_t>(fallback));
        return;
    }
    CG_CycleWeapon(1, 1);
}

namespace {

// Cvar_SetByVmIndex (0x0029fe40: value in EAX, cdecl vmCvar) of 0x0094c230 to cg.time.
void stamp_weapon_command() {
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(va_int(title_global<int32_t>(0x0097253c))));
    const uint32_t args[1] = {0x0094c230};
    cg_call_regs(0x0029fe40, &regs, args, 1);
}

void cycle_command(uint32_t forward) {
    if (!weapon_command_allowed(0x100000)) return;
    stamp_weapon_command();
    CG_CycleWeapon(forward, 0);
}

}  // namespace

void XB_CDECL CG_PrevWeapon_f() { cycle_command(0); }

void XB_CDECL CG_NextWeapon_f() { cycle_command(1); }

void XB_CDECL CG_WeaponSlot_f() {
    // "weaponslot <name or 1-6>": the weapon in that inventory slot, if owned.
    if (!weapon_command_allowed(0x100000)) return;
    stamp_weapon_command();
    int32_t slot = static_cast<int32_t>(CG_WeaponSlotFromName(CG_Argv(1)));
    if (!slot) slot = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(CG_Argv(1));  // _atol
    if (slot <= 0 || slot >= 7) return;
    const int32_t weapon = title_global<int8_t>(0x00972948 + slot);
    if (weapon && CG_HasWeapon(0, weapon)) select_weapon(static_cast<uint32_t>(weapon));
}

void XB_CDECL CG_AltWeapon_f() {
    // The current weapon's alternate (+0x718) if owned; without one a "no alternate" message.
    if (!weapon_command_allowed(0x106000)) return;
    const int32_t alternate = *reinterpret_cast<const int32_t*>(weapon_def(title_global<int32_t>(0x00947fa4)) + 0x718);
    if (!alternate) {
        title_function<const char* XB_CDECL(const char*, const char*, uint32_t)>(0x002d03c0)(  // SEH_LocalizeTextMessage
            &title_global<char>(0x00838794), &title_global<char>(0x00838b84), 0);
        return;
    }
    if (CG_HasWeapon(0, alternate)) select_weapon(static_cast<uint32_t>(alternate));
}

void XB_REGS1 XB_CDECL CG_ParseObjective(int32_t index) {
    // An objective configstring (index; objective record 0x00972748 + index * 0x98): "state"
    // (+0; becoming 4 stamps +0x10 with cg.time), "str" (+0x18, 0x7f characters), "org"
    // (+4, "%f %f %f") and "ring" (+0x14; a change stamps +0x10). State 0 clears it (+0x10 -1).
    const char* text = nullptr;
    if (index >= 0 && index < 0x400) {
        const uint8_t* string = title_global<const uint8_t*>(0x009606b8 + static_cast<uint32_t>(index) * 4);
        if (string) text = reinterpret_cast<const char*>(string + 6);
    }
    uint8_t* objective = &title_global<uint8_t>(0x00972748 + static_cast<uint32_t>(index) * 0x98);
    int32_t& state = *reinterpret_cast<int32_t*>(objective);
    const auto value_for = [text](uint32_t key) {
        TitleRegs regs{};
        regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
        regs.edi = key;
        return reinterpret_cast<const char*>(static_cast<uintptr_t>(cg_call_regs(0x002b8f50, &regs, nullptr, 0)));  // Info_ValueForKey
    };
    const auto atol = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81);
    bool clear = false;
    if (!*text) {
        state = 0;
        clear = true;
    } else {
        const int32_t old_state = state;
        const char* v = value_for(0x00831b30);  // "state"
        state = *v ? atol(v) : 0;
        if (old_state != 4 && state == 4) *reinterpret_cast<int32_t*>(objective + 0x10) = title_global<int32_t>(0x0097253c);
        clear = state == 0;
    }
    float* origin = reinterpret_cast<float*>(objective + 4);
    if (clear) {
        origin[2] = 0.0f;
        origin[1] = 0.0f;
        origin[0] = 0.0f;
        objective[0x18] = 0;
        *reinterpret_cast<int32_t*>(objective + 0x10) = -1;
        *reinterpret_cast<int32_t*>(objective + 0x14) = 0;
        return;
    }
    const char* v = value_for(0x00831a78);  // "str"
    if (!*v) {
        objective[0x18] = 0;
    } else {
        title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(  // _strncpy
            reinterpret_cast<char*>(objective + 0x18), v, 0x7f);
        objective[0x18 + 0x7f] = 0;
    }
    v = value_for(0x00831a2c);  // "org"
    if (!*v) {
        origin[2] = 0.0f;
        origin[1] = 0.0f;
        origin[0] = 0.0f;
    } else {
        title_function<int32_t XB_CDECL(const char*, const char*, float*, float*, float*)>(0x001f07e1)(  // sscanf
            v, &title_global<char>(0x00838c70), origin, origin + 1, origin + 2);
    }
    const int32_t old_ring = *reinterpret_cast<int32_t*>(objective + 0x14);
    v = value_for(0x00831a24);  // "ring"
    const int32_t ring = *v ? atol(v) : 0;
    *reinterpret_cast<int32_t*>(objective + 0x14) = ring;
    if (old_ring != ring) *reinterpret_cast<int32_t*>(objective + 0x10) = title_global<int32_t>(0x0097253c);
}

int32_t XB_CDECL HudElem_GetTimerValue(const uint8_t* elem) {
    // A timer hud element's value in ms (+0x5c end or start time): types 4/6/8 count down to
    // the end (+999 / +99 / exact, for display rounding), 5/7/9 count up; never negative.
    const int32_t now = title_global<int32_t>(0x0097253c);
    const int32_t time = *reinterpret_cast<const int32_t*>(elem + 0x5c);
    int32_t value;
    switch (*reinterpret_cast<const int32_t*>(elem)) {
    case 4: value = time - now + 0x3e7; break;
    case 6: value = time - now + 0x63; break;
    case 8: value = time - now; break;
    case 5:
    case 7:
    case 9: value = now - time; break;
    default: return 0;
    }
    return value >= 0 ? value : 0;
}

PLATFORM_ASM_ENTRY(HudElem_GetTimerValue_entry,
                   "pushl %ecx\n" PLATFORM_ASM_CALL(HudElem_GetTimerValue) "addl $4, %esp\n"
                   "ret")

namespace {

void update_white_flash_color() {
    if (!title_global<int32_t>(0x00973a78)) return;
    const int32_t start = title_global<int32_t>(0x00973a7c);
    if (!start) return;
    const int32_t t = title_global<int32_t>(0x0097253c) - start;
    if (t >= 100) return;
    const float one = g(0x0083eb20);
    const int32_t left = 100 - t;
    const float f = left < 100 ? static_cast<float>(left) * g(0x0083eb94) : one;
    const float alpha = f * g(0x0094b5d8);
    title_global<float>(0x0098ad70) = one;
    title_global<float>(0x0098ad6c) = one;
    title_global<float>(0x0098ad68) = one;
    title_global<float>(0x0098ad74) = alpha;
}

}  // namespace

void XB_CDECL CG_UpdateWhiteFlashColor() { update_white_flash_color(); }

void XB_CDECL CG_UpdateWhiteFlashColor2() { update_white_flash_color(); }

void XB_CDECL CG_ScreenFade() {
    CG_DrawScreenBlackout();
    const float rate = g(0x00972e88);
    if (rate == 0.0f) return;
    float* current = &title_global<float>(0x00972e8c);
    const float* target = &title_global<float>(0x00972e9c);
    const int32_t left = title_global<int32_t>(0x00972e84) - title_global<int32_t>(0x0097253c);
    if (left <= 0) {
        for (uint32_t i = 0; i < 4; ++i) current[i] = target[i];
        if (current[3] == 0.0f) {
            title_global<float>(0x00972e88) = 0.0f;
            return;
        }
        CG_FillRect(current, 0.0f, 0.0f, 640.0f, 480.0f);
        return;
    }
    const float f = static_cast<float>(left) * rate;
    const float inv = g(0x0083eb20) - f;
    float color[4];
    for (uint32_t i = 0; i < 4; ++i) color[i] = target[i] * inv + current[i] * f;
    if (color[3] == 0.0f) return;
    CG_FillRect(color, 0.0f, 0.0f, g(0x0083ec0c), g(0x0083ec08));  // 640 x 480
}

void XB_CDECL CG_DrawFrontBufferBlend() {
    float& alpha = title_global<float>(0x0095f648);
    float a = alpha - g(0x008ddaa0);
    alpha = a;
    if (0.0f > a) {
        alpha = 0.0f;
        return;
    }
    if (a > g(0x008fefe0)) {
        a = g(0x008fefe0);
        alpha = a;
    }
    if (0.0f >= a) return;
    const float k = g(0x0083eb8c);  // 255
    uint8_t r = static_cast<uint8_t>(platform_x87_mul_ftol(g(0x008fefd8), k));
    uint8_t gr = static_cast<uint8_t>(platform_x87_mul_ftol(g(0x008fefd4), k));
    uint8_t b = static_cast<uint8_t>(platform_x87_mul_ftol(g(0x008fefd0), k));
    const uint8_t base_alpha = static_cast<uint8_t>(platform_x87_mul_ftol(g(0x008fefdc), k));
    const uint8_t al = static_cast<uint8_t>(platform_x87_mul_add_ftol(base_alpha, alpha, 0, false));
    const int32_t mode = title_global<int32_t>(0x008d5124);
    if (mode == 5 || mode == 6) {
        r = static_cast<uint8_t>(platform_x87_mul_add_ftol(r, alpha, 0, false));
        gr = static_cast<uint8_t>(platform_x87_mul_add_ftol(gr, alpha, 0, false));
        b = static_cast<uint8_t>(platform_x87_mul_add_ftol(b, alpha, 0, false));
    }
    NglQuad quad;
    nglInitQuad(&quad);
    const uint32_t color = (static_cast<uint32_t>(al) << 24) | (static_cast<uint32_t>(r) << 16) |
                           (static_cast<uint32_t>(gr) << 8) | b;
    nglSetQuadColor(&quad, color);
    nglSetQuadBlend(&quad, title_global<uint32_t>(0x008fefc4), color);
    const float y1 = static_cast<float>(nglGetScreenHeight()) + g(0x008fefc8);
    const float x1 = static_cast<float>(nglGetScreenWidth()) + g(0x008fefcc);
    nglSetQuadRect(&quad, g(0x008fefcc), g(0x008fefc8), x1, y1);
    nglSetQuadTexture(&quad, nglGetFrontBufferTexture());
    nglListAddQuad(&quad);
}

void XB_CDECL CG_DrawStatusLabel() {
    const uint8_t* label = title_global<const uint8_t*>(0x009606ec);
    if (!label) return;
    const char* name = reinterpret_cast<const char*>(label + 6);
    if (!*name) return;
    if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(name, &title_global<char>(0x00832430)))  // __stricmp "none"
        return;
    const uint8_t* value_text = title_global<const uint8_t*>(0x009606f0);
    float value = static_cast<float>(title_function<double XB_CDECL(const char*)>(0x001ef056)(  // atof
        value_text ? reinterpret_cast<const char*>(value_text + 6) : nullptr));
    const float one = g(0x0083eb20);
    if (value > one)
        value = one;
    else if (0.0f > value)
        value = 0.0f;
    float color[4];
    if (value > 0.5f) {
        color[0] = (one - value) * g(0x0083eba8);
        color[1] = one;
    } else {
        color[0] = one;
        color[1] = value * g(0x0083eba8);
    }
    color[2] = 0.0f;
    color[3] = one;
    using DrawText = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, uint32_t, uint32_t, uint32_t);
    const auto draw_text = title_function<DrawText>(title_global<uint32_t>(0x00928578));
    draw_text(345.0f, 217.0f, 0, 0x1.555556p-2f /* 0x3eaaaaab, 1/3 */, color, name, 0, 0, 3);
    const uint8_t* text = title_global<const uint8_t*>(0x009606f4);
    const char* translated =
        CG_TranslateString(text ? reinterpret_cast<const char*>(text + 6) : nullptr, &title_global<char>(0x0083521c));  // "cgame"
    color[0] = g(0x0083eb20);
    color[1] = color[0];
    color[2] = color[0];
    color[3] = g(0x0083ed28);  // 0.7
    draw_text(345.0f, 235.0f, 0, 0x1.555556p-2f /* 0x3eaaaaab, 1/3 */, color, translated, 0, 0, 3);
}

void XB_CDECL CG_DrawViewDirIcon() {
    const uint32_t flags = title_global<uint32_t>(0x0097257c);
    const float zoom = g(0x009725d8);
    if (!(flags & 0x1000000) || (flags & 0x2000000)) return;
    float x, y;
    CG_ProjectViewDirToScreen(&x, &y);
    const float xs = g(0x009758b8), ys = g(0x009758bc);
    x = x * zoom * xs;
    y = y * zoom * ys;
    const float w = xs * g(0x0083ec84), h = ys * g(0x0083ec84);  // 40
    title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c))(nullptr);  // set colour
    const float half = g(0x0083eb24);
    const float sy = (static_cast<float>(title_global<int32_t>(0x00972b9c)) - h) * half +
                     static_cast<float>(title_global<int32_t>(0x00972b94)) + y;  // refdef height, y
    const float sx = (static_cast<float>(title_global<int32_t>(0x00972b98)) - w) * half +
                     static_cast<float>(title_global<int32_t>(0x00972b90)) + x;  // refdef width, x
    title_function<void XB_CDECL(float, float, float, float, float, float, float, float, uint32_t)>(
        title_global<uint32_t>(0x00928530))(sx, sy, w, h, 0.0f, 0.0f, 1.0f, 1.0f, title_global<uint32_t>(0x00975c48));
}

namespace {

uint32_t bits_of(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// a - b * c on the x87 (FLD/FMUL/FSUBR), stored as float: no MXCSR flags.
float x87_sub_product(float a, float b, float c) {
    float result;
    __asm__("flds %2\n\tfmuls %3\n\tfsubrs %1\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}

// a - b on the x87 (FLD/FSUB), stored as float.
float x87_sub(float a, float b) {
    float result;
    __asm__("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}

void set_color(const float* color) { title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c))(color); }

// trap_R_DrawStretchPic (import 0x00928530)
void stretch_pic(float x, float y, float w, float h, float s0, float t0, float s1, float t1, uint32_t shader) {
    title_function<void XB_CDECL(float, float, float, float, float, float, float, float, uint32_t)>(
        title_global<uint32_t>(0x00928530))(x, y, w, h, s0, t0, s1, t1, shader);
}

void stretch_pic_eax(uint32_t shader, float x, float y, float w, float h) {
    CG_R_DrawStretchPic(shader, bits_of(x), bits_of(y), bits_of(w), bits_of(h), 0, 0, 0x3f800000, 0x3f800000);
}

float refdef_x() { return static_cast<float>(title_global<int32_t>(0x00972b90)); }
float refdef_y() { return static_cast<float>(title_global<int32_t>(0x00972b94)); }
float refdef_w() { return static_cast<float>(title_global<int32_t>(0x00972b98)); }
float refdef_h() { return static_cast<float>(title_global<int32_t>(0x00972b9c)); }

}  // namespace

PLATFORM_ASM_ENTRY(CG_DrawAdsOverlay_entry,
                   "subl $4, %esp\n"
                   "pushl %esp\n" PLATFORM_ASM_CALL(CG_DrawAdsOverlay) "addl $4, %esp\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_DrawAdsOverlay(float* out) {
    float fraction = 0.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (!CG_GetAdsOverlayFraction(0, 0, &fraction)) {
        if (title_global<uint8_t>(0x0098ad58)) {
            title_global<uint8_t>(0x0095f91f) = 0;
            title_global<uint8_t>(0x0098ad58) = 0;
        }
        *out = 1.0f;
        return;
    }
    color[3] = fraction;
    const uint8_t* cg = title_global<const uint8_t*>(0x00972acc);
    const uint32_t client = *reinterpret_cast<const uint32_t*>(cg);
    float px, py;
    CG_ProjectViewDirToScreen(&px, &py);
    const float xs = g(0x009758b8), ys = g(0x009758bc);
    const float w = *reinterpret_cast<const float*>(cg + 0x63c) * xs;
    const float h = *reinterpret_cast<const float*>(cg + 0x640) * ys;
    const float cx = xs * px + refdef_w() * 0.5f + refdef_x();
    const float cy = ys * py + refdef_h() * 0.5f + refdef_y();
    set_color(color);
    cg = title_global<const uint8_t*>(0x00972acc);
    if (**reinterpret_cast<const char* const*>(cg + 0x634)) {  // the scope overlay, black around it
        const uint32_t shader = title_global<uint32_t>(0x00976068 + client * 0xc4);
        const float x0 = x87_sub_product(cx, w, 0.5f);
        const float y0 = x87_sub_product(cy, h, 0.5f);
        title_global<uint8_t>(0x0098ad58) = 1;
        title_global<uint8_t>(0x0095f91f) = 1;
        stretch_pic(x0, y0, w, h, 0.0f, 0.0f, 1.0f, 1.0f, shader);
        if (x0 > 0.0f) stretch_pic(0.0f, 0.0f, x0, refdef_h(), 0.0f, 0.0f, 0.0f, 1.0f, shader);
        const float x1 = x0 + w;
        const float rw = refdef_w();
        if (rw > x1) stretch_pic(x1, 0.0f, rw - x1, refdef_h(), 0.0f, 0.0f, 0.0f, 1.0f, shader);
        if (y0 > 0.0f) stretch_pic(x0, 0.0f, w, y0, 0.0f, 0.0f, 1.0f, 0.0f, shader);
        const float y1 = y0 + h;
        const float rh = refdef_h();
        if (rh > y1) stretch_pic(x0, y1, w, rh - y1, 0.0f, 0.0f, 1.0f, 0.0f, shader);
    }
    cg = title_global<const uint8_t*>(0x00972acc);
    const int32_t mode = *reinterpret_cast<const int32_t*>(cg + 0x638);
    const auto black = [&color] {
        color[0] = color[1] = color[2] = 0.0f;
        set_color(color);
    };
    if (mode == 1) {  // a reticle shader, size +0x4c4
        const float size = static_cast<float>(*reinterpret_cast<const int32_t*>(cg + 0x4c4));
        const uint32_t shader = title_global<uint32_t>(0x00976060 + client * 0xc4);
        const float w2 = xs * size, h2 = ys * size;
        const float y = (refdef_h() - h2) * 0.5f + ys * py + refdef_y();
        const float x = (refdef_w() - w2) * 0.5f + xs * px + refdef_x();
        stretch_pic(x, y, w2, h2, 0.0f, 0.0f, 1.0f, 1.0f, shader);
    } else if (mode == 2) {  // a vertical line down, two horizontal ones (import)
        black();
        stretch_pic(cx - 1.0f, cy, 3.0f, h * g(0x0083ec18), 0.0f, 0.0f, 1.0f, 1.0f, title_global<uint32_t>(0x00975c40));
        const float length = platform_x87_mul(w, 0.75f);
        const float y = x87_sub(cy, 1.0f);
        stretch_pic(cx - w * g(0x0083ec18), y, length, 3.0f, 0.0f, 0.0f, 1.0f, 1.0f, title_global<uint32_t>(0x00975c44));
        stretch_pic(w * g(0x0083ed2c) + cx, y, length, 3.0f, 0.0f, 0.0f, 1.0f, 1.0f, title_global<uint32_t>(0x00975c44));
    } else if (mode == 3) {  // a full cross
        black();
        stretch_pic_eax(title_global<uint32_t>(0x00975c40), cx - 1.0f, cy - h * g(0x0083ec18), 3.0f, h * g(0x0083f0e8));
        stretch_pic_eax(title_global<uint32_t>(0x00975c44), cx - w * g(0x0083ec18), cy - 1.0f, w * g(0x0083f0e8), 3.0f);
    } else if (mode == 4) {  // as mode 2 through CG_R_DrawStretchPic
        black();
        stretch_pic_eax(title_global<uint32_t>(0x00975c40), cx - 1.0f, cy, 3.0f, h * g(0x0083ec18));
        const float length = platform_x87_mul(w, 0.75f);
        const float y = x87_sub(cy, 1.0f);
        stretch_pic_eax(title_global<uint32_t>(0x00975c44), cx - w * g(0x0083ec18), y, length, 3.0f);
        stretch_pic_eax(title_global<uint32_t>(0x00975c44), w * g(0x0083ed2c) + cx, y, length, 3.0f);
    }
    set_color(nullptr);
    *out = 1.0f - fraction;
}

void XB_CDECL CG_DrawWeaponReticle() {
    float color[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float zoom = g(0x009725d8);
    float scale = 1.0f;
    float offset = 0.0f;
    if (title_global<int32_t>(0x00972548)) return;
    color[3] = g(0x00949590);
    if (*reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0093fcd8) + 0x20)) return;
    if (title_global<uint32_t>(0x0097257c) & 0x1000000) {
        color[0] = g(0x0083f144);
        color[1] = g(0x0083f140);
        color[2] = g(0x0083f13c);
    }
    const uint32_t state = title_global<uint32_t>(0x00972610);
    if (state & 0x6000) {  // in a vehicle
        if (title_global<int32_t>(0x009729b8)) CG_DrawCrosshair();
        return;
    }
    if ((state & 0x100000) && !(state & 0x400000)) {  // a mounted weapon: two lines
        color[0] = color[1] = color[2] = 0.0f;
        color[3] = g(0x0083eba4);
        if (title_global<int32_t>(0x00972a38) == 1) {
            CG_FillRect(color, 310.0f, 240.0f, 20.0f, 2.0f);
            CG_FillRect(color, 319.0f, 242.0f, 2.0f, 8.0f);
            return;
        }
        if (uint8_t* hud = title_global<uint8_t*>(0x0095f8f0)) {  // g_hudManager
            uint8_t* const item = *reinterpret_cast<uint8_t**>(hud + 0x40);
            const auto hidden = title_function<uint8_t XB_THISCALL(uint8_t*)>(
                *reinterpret_cast<const uint32_t*>(*reinterpret_cast<const uint8_t* const*>(item) + 0x10));
            if (hidden(item)) return;
        }
        CG_FillRect(color, 300.0f, 240.0f, 40.0f, 2.0f);
        CG_FillRect(color, 319.0f, 242.0f, 2.0f, 16.0f);
        return;
    }
    const uint8_t* cg = title_global<const uint8_t*>(0x00972acc);
    const uint32_t weapon = *reinterpret_cast<const uint32_t*>(cg);
    if (!weapon) return;
    color[3] = [] {
        float ads;
        CG_DrawAdsOverlay(&ads);
        return ads;
    }() * g(0x00949590);
    if (g(0x0083eb94) > color[3]) return;  // 0.01
    if (!title_global<int32_t>(0x009485d4) || !title_global<uint8_t>(0x008dce69) || title_global<uint8_t>(0x0095f91f))
        return;
    if (title_global<int32_t>(0x009729cc) == 10) return;
    if (title_global<int32_t>(0x0094c7a4) && title_global<int32_t>(0x0094bf6c)) return;
    if (zoom == 1.0f && title_global<int32_t>(0x00949864)) return;
    const int32_t view = title_global<int32_t>(0x009725d4);
    if (view == 10 || view == 11 || view == 5 || view == 14 || view == 1 || view == 2) return;
    cg = title_global<const uint8_t*>(0x00972acc);
    if (*reinterpret_cast<const int32_t*>(cg + 0x98) == 5) return;
    const auto word = [](const uint8_t* base, uint32_t offset) { return *reinterpret_cast<const int32_t*>(base + offset); };
    const auto fl = [](const uint8_t* base, uint32_t offset) -> const float& {
        return *reinterpret_cast<const float*>(base + offset);
    };
    float px, py;
    CG_ProjectViewDirToScreen(&px, &py);
    set_color(color);
    cg = title_global<const uint8_t*>(0x00972acc);
    bool done = false;
    if (!(zoom == 0.0f)) {
        // into the zoom's last part (+0x774 zooming in, else +0x778): the crosshair shrinks and moves down
        const float part = title_global<int32_t>(0x00972a74) ? fl(cg, 0x774) : fl(cg, 0x778);
        float t = zoom - (1.0f - part);
        if (t > 0.0f) {
            t = t / part;
            if (t > 0.0f) {
                scale = 1.0f - t * 0.5f;
                offset = 480.0f / g(0x00972ba4) * fl(cg, 0x770) * t;
            }
        }
        if (!title_global<int32_t>(0x00949864)) {
            if (1.0f > scale) {
                uint32_t shader = title_global<uint32_t>(0x00976060 + weapon * 0xc4);
                if (!shader)
                    shader = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                        RE_RegisterShader(0, &title_global<const char>(0x0094baf0))));
                cg = title_global<const uint8_t*>(0x00972acc);
                if (shader) {
                    const float size = static_cast<float>(word(cg, 0x4c4)) * (1.5f - scale);
                    const float y = (refdef_h() - size) * 0.5f + g(0x009758bc) * py + refdef_y();
                    const float x = (refdef_w() - size) * 0.5f + g(0x009758b8) * px + refdef_x();
                    stretch_pic_eax(shader, x, y, size, size);
                    cg = title_global<const uint8_t*>(0x00972acc);
                }
            }
            done = zoom == 1.0f;
        }
    }
    if (!done) {
        if (!title_global<int32_t>(0x0094b15c)) {
            px = 0.0f;
            py = offset;
        }
        if (**reinterpret_cast<const char* const*>(cg + 0x4bc)) {  // the centre shader
            const float size = static_cast<float>(word(cg, 0x4c4));
            const float y = (refdef_h() - size) * 0.5f + g(0x009758bc) * py + refdef_y();
            const float x = (refdef_w() - size) * 0.5f + g(0x009758b8) * px + refdef_x();
            stretch_pic_eax(title_global<uint32_t>(0x00976060 + weapon * 0xc4), x, y, size, size);
            cg = title_global<const uint8_t*>(0x00972acc);
        }
        if (**reinterpret_cast<const char* const*>(cg + 0x4c0)) {  // the spread ticks
            const float fade = g(0x00972a40) * g(0x0083eb30);  // / 255
            float alpha = (1.0f - fade) * color[3];
            color[3] = alpha;
            if (g(0x0094a7c0) > alpha) color[3] = g(0x0094a7c0);
            set_color(color);
            float spread;
            BG_GetWeaponSpread(&spread, weapon, &title_global<const uint8_t>(0x00972550),
                               word(title_global<const uint8_t*>(0x00971aa8), 4), 0);  // cg.snap->serverTime
            cg = title_global<const uint8_t*>(0x00972acc);
            const float value = ((fl(cg, 0x658) - spread) * (g(0x00972a40) * g(0x0083eb30)) + spread) * scale;
            float sx = 640.0f / g(0x00972ba0) * value;  // fov x
            float sy = 480.0f / g(0x00972ba4) * value;  // fov y
            const float minimum = static_cast<float>(word(cg, 0x4cc));
            if (minimum > sx) sx = minimum;
            if (minimum > sy) sy = minimum;
            const uint32_t shader = title_global<uint32_t>(0x00976064 + weapon * 0xc4);
            // Per tick (only 1 and 3 are drawn): the original's direction tables
            static constexpr struct {
                float ay, by, cy, ax, bx, cx;
            } kTicks[4] = {{}, {0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f}, {}, {0.0f, -0.5f, 0.0f, -1.0f, -1.0f, -1.0f}};
            for (int32_t i = 1; i < 4; i += 2) {
                const auto& tick = kTicks[i];
                const float length = static_cast<float>(word(cg, 0x4c8)) * scale;
                const float c674 = fl(cg, 0x674);
                const float y = (tick.ay * sy + py) * g(0x009758bc) + tick.by * length + tick.cy -
                                tick.ay * c674 * length + refdef_h() * 0.5f + refdef_y();
                const float x = (tick.ax * sx + px) * g(0x009758b8) + tick.bx * length + tick.cx -
                                tick.ax * c674 * length + refdef_w() * 0.5f + refdef_x();
                CG_StretchPicRotate(shader, bits_of(x), bits_of(y), bits_of(length), bits_of(length), 0,
                                    bits_of(static_cast<float>((i >> 1) & 1)), 0x3f800000,
                                    bits_of(static_cast<float>(((i - 2) >> 1) & 1)),
                                    bits_of(static_cast<float>(i & 1) * 90.0f));
                cg = title_global<const uint8_t*>(0x00972acc);
            }
        }
    }
    set_color(nullptr);
}

void XB_CDECL CG_Draw2D() {
    if (title_global<int32_t>(0x00971a88) || title_global<int32_t>(0x00971a8c)) return;
    nglPushScene(1);
    if (title_global<int32_t>(0x00973008) || !title_global<int32_t>(0x00949474)) {
        CG_DrawFlashFade();
        return;
    }
    const auto set = [](uint32_t address) { return title_global<uint8_t>(address) != 0; };
    const auto weapon_hud = [&set] { return set(0x008dce6a) && !set(0x0095f91f) && !set(0x0095f919); };
    const auto snapshot_ok = [] {
        return *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x00971aa8) + 0x34) < 4;  // cg.snap
    };
    const auto call = [](uint32_t address) { title_function<void XB_CDECL()>(address)(); };
    if (weapon_hud()) CG_DrawStatusLabel();
    if (!set(0x0095f919)) {
        CG_ScreenFade();
        CG_DrawDamageBlend();
        CG_DrawDamageDirectionIcons();
    }
    const uint8_t* intro = title_function<const uint8_t* XB_CDECL(const char*, const char*, uint32_t)>(0x0029c2d0)(  // Cvar_Get
        &title_global<char>(0x008344f4), &title_global<char>(0x0082e098), 0);  // "introScreen", "0"
    if (*reinterpret_cast<const int32_t*>(intro + 0x20)) {
        // Over the intro screen the hud elements go on top.
        if (snapshot_ok()) {
            CG_DrawWeaponReticle();
            if (weapon_hud()) CG_DrawViewDirIcon();
        }
        if (snapshot_ok() && title_global<int32_t>(0x00948c94)) CG_DrawHudElems();
    } else {
        if (snapshot_ok() && title_global<int32_t>(0x00948c94)) CG_DrawHudElems();
        if (snapshot_ok()) {
            CG_DrawWeaponReticle();
            if (weapon_hud()) CG_DrawViewDirIcon();
        }
    }
    if (!set(0x0095f919)) {
        CG_DrawFrontBufferBlend();
        CG_DrawFrontBufferFade();
    }
    CG_DrawCheckpointReached();
    if (!CG_DrawObjectives()) CG_DrawCenterString();
    CG_UpdateWhiteFlashColor2();
    CG_UpdateWhiteFlashColor();
    Subtitle_Draw();
    CG_DrawFlashFade();
    nglPopScene();
}

void XB_CDECL HudElem_ClearAll() {
    uint8_t* elems = &title_global<uint8_t>(0x0094cc88);
    for (uint32_t offset = 0; offset < 0x7c0; offset += 0x7c) {
        int32_t& type = *reinterpret_cast<int32_t*>(elems + offset);
        if (type) type = 0;
    }
    std::memset(elems, 0, 0x7c0);
}

void XB_REGS1 XB_CDECL HudElem_SetDefaults(uint8_t* elem) {
    const auto set = [elem](uint32_t offset, uint32_t value) { *reinterpret_cast<uint32_t*>(elem + offset) = value; };
    *reinterpret_cast<float*>(elem + 0xc) = g(0x0083eb20);
    set(0, 1);
    for (uint32_t offset = 4; offset <= 0x18; offset += 4)
        if (offset != 0xc) set(offset, 0);
    set(0x1c, 0xffffffff);
    for (uint32_t offset = 0x20; offset <= 0x48; offset += 4) set(offset, 0);
    set(0x5c, 0);
    set(0x60, 0);
    *reinterpret_cast<float*>(elem + 0x64) = 0.0f;
    set(0x68, 0);
    *reinterpret_cast<float*>(elem + 0x6c) = 0.0f;
}

namespace {

// MSVC's (int)ceil(seconds * 1000) with the product on the x87.
int32_t hud_ms(float seconds) {
    return static_cast<int32_t>(static_cast<int64_t>(std::ceil(platform_x87_mul_double(seconds, g(0x0083eb9c)))));
}

uint8_t* hud_elem(int32_t index) { return &title_global<uint8_t>(0x0094cc88) + index * 0x7c; }

}  // namespace

void XB_REGS1 XB_CDECL HudElem_SetTimerKind(int32_t index, int32_t kind, float seconds) {
    uint8_t* elem = hud_elem(index);
    const int32_t ms = hud_ms(seconds);
    if (ms <= 0 && kind != 5)
        title_function<const char* XB_CDECL(const char*, double)>(0x002a93d0)(  // va
            &title_global<char>(0x008323f4), platform_x87_int_mul_double(ms, g(0x0083eb4c)));  // "time %g should be > 0"
    for (uint32_t offset = 0x30; offset <= 0x48; offset += 4) *reinterpret_cast<int32_t*>(elem + offset) = 0;
    *reinterpret_cast<int32_t*>(elem + 0x60) = 0;
    *reinterpret_cast<int32_t*>(elem + 0x68) = 0;
    *reinterpret_cast<int32_t*>(elem) = kind;
    *reinterpret_cast<float*>(elem + 0x64) = 0.0f;
    *reinterpret_cast<int32_t*>(elem + 0x5c) = title_global<int32_t>(0x0098540c) + ms;  // level.time
}

void XB_REGS1 XB_CDECL HudElem_SetShaderTimer(int32_t index, int32_t kind, const char* name, float seconds,
                                             float period, int32_t w, int32_t h) {
    uint8_t* elem = hud_elem(index);
    const int32_t ms = hud_ms(seconds);
    const int32_t period_ms = hud_ms(period);
    uint8_t* shader = RE_RegisterShader(0, name);
    for (uint32_t offset = 0x3c; offset <= 0x48; offset += 4) *reinterpret_cast<int32_t*>(elem + offset) = 0;
    *reinterpret_cast<int32_t*>(elem + 0x68) = 0;
    *reinterpret_cast<int32_t*>(elem) = kind;
    *reinterpret_cast<uint8_t**>(elem + 0x38) = shader;
    *reinterpret_cast<int32_t*>(elem + 0x60) = period_ms;
    *reinterpret_cast<float*>(elem + 0x64) = 0.0f;
    *reinterpret_cast<int32_t*>(elem + 0x5c) = title_global<int32_t>(0x0098540c) + ms;
    *reinterpret_cast<int32_t*>(elem + 0x30) = w;
    *reinterpret_cast<int32_t*>(elem + 0x34) = h;
}

PLATFORM_ASM_ENTRY(HudElem_SubstituteLabel_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(HudElem_SubstituteLabel) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL HudElem_SubstituteLabel(int32_t size, uint8_t* layout, char* buffer) {
    // The label (+0x10) and text (+0x18) are read through the layout at each character.
    const auto label = [layout] { return *reinterpret_cast<const char* const*>(layout + 0x10); };
    const auto text = [layout] { return *reinterpret_cast<const char* const*>(layout + 0x18); };
    const int32_t n = size - 1;
    int32_t i = 0, j = 0;
    while (i < n) {
        const char c = label()[j];
        if (!c) break;
        if (c == '%' && label()[j + 1] == 's') {
            j += 2;
            break;
        }
        ++i;
        buffer[j++] = c;
    }
    for (int32_t k = 0; i < n; ++k) {
        const char c = text()[k];
        if (!c) break;
        buffer[i++] = c;
    }
    while (i < n) {
        const char c = label()[j];
        if (!c) break;
        buffer[i++] = c;
        ++j;
    }
    buffer[i] = 0;
    float* width = reinterpret_cast<float*>(layout + 0x14);
    const float text_width = *reinterpret_cast<const float*>(layout + 0x1c) + *width;
    *reinterpret_cast<char**>(layout + 0x18) = buffer;
    *reinterpret_cast<float*>(layout + 0x1c) = text_width;
    *reinterpret_cast<const char**>(layout + 0x10) = &title_global<const char>(0x0082dfeb);  // ""
    *width = 0.0f;
    return i;
}

const char* XB_FASTCALL HudElem_FormatTimer(const uint8_t* elem) {
    const int32_t seconds = HudElem_GetTimerValue(elem) / 1000;
    const int32_t hours = seconds / 3600;
    const int32_t rest = seconds - hours * 3600;
    const int32_t minutes = rest / 60;
    const int32_t secs = rest - minutes * 60;
    if (hours)
        return title_function<const char* XB_CDECL(const char*, int32_t, int32_t, int32_t)>(0x002a93d0)(  // va
            &title_global<char>(0x00839968), hours, minutes, secs);  // "%i:%02i:%02i"
    return title_function<const char* XB_CDECL(const char*, int32_t, int32_t)>(0x002a93d0)(  // va
        &title_global<char>(0x00839960), minutes, secs);  // "%i:%02i"
}

const char* XB_FASTCALL HudElem_FormatTenthsTimer(const uint8_t* elem) {
    const int32_t tenths_total = HudElem_GetTimerValue(elem) / 100;
    const int32_t hours = tenths_total / 36000;
    const int32_t rest = tenths_total - hours * 36000;
    const int32_t minutes = rest / 600;
    const int32_t rest2 = rest - minutes * 600;
    const int32_t secs = rest2 / 10;
    const int32_t tenths = rest2 - secs * 10;
    if (hours)
        return title_function<const char* XB_CDECL(const char*, int32_t, int32_t, int32_t, int32_t)>(0x002a93d0)(  // va
            &title_global<char>(0x00839950), hours, minutes, secs, tenths);  // "%i:%02i:%02i.%i"
    return title_function<const char* XB_CDECL(const char*, int32_t, int32_t, int32_t)>(0x002a93d0)(  // va
        &title_global<char>(0x00839944), minutes, secs, tenths);  // "%i:%02i.%i"
}

namespace {

// Hud element types: 1 label text, 2 value, 3 material, 4-7 timers (text); 8/9 clocks.
bool hud_elem_is_text(int32_t type) { return type == 1 || type == 2 || (type >= 4 && type <= 7); }
bool hud_elem_is_sized(int32_t type) { return type == 3 || type == 8 || type == 9; }

// An element size field, or the layout's font height (+0x28) when it is 0.
float hud_elem_size(const uint8_t* elem, uint32_t offset, const uint8_t* layout) {
    const int32_t v = *reinterpret_cast<const int32_t*>(elem + offset);
    return v ? static_cast<float>(v) : *reinterpret_cast<const float*>(layout + 0x28);
}

}  // namespace

PLATFORM_ASM_ENTRY(HudElem_GetWidth_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(HudElem_GetWidth) "addl $8, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL HudElem_GetWidth(const uint8_t* elem, const uint8_t* layout) {
    const int32_t type = *reinterpret_cast<const int32_t*>(elem);
    const float label = *reinterpret_cast<const float*>(layout + 0x14);
    if (hud_elem_is_text(type)) return *reinterpret_cast<const float*>(layout + 0x1c) + label;
    if (!hud_elem_is_sized(type)) return 0.0f;
    const float width = hud_elem_size(elem, 0x30, layout);
    const int32_t fade = *reinterpret_cast<const int32_t*>(elem + 0x48);
    if (fade > 0) {
        const int32_t t = title_global<int32_t>(0x0097253c) - *reinterpret_cast<const int32_t*>(elem + 0x44);
        if (t < fade) {
            const float from = hud_elem_size(elem, 0x3c, layout);
            const float v = g(0x0083eb20) / static_cast<float>(fade) * static_cast<float>(t) * (width - from);
            return label + (v + from);
        }
    }
    return label + width;
}

PLATFORM_ASM_ENTRY(HudElem_GetHeight_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(HudElem_GetHeight) "addl $8, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL HudElem_GetHeight(const uint8_t* elem, const uint8_t* layout) {
    const int32_t type = *reinterpret_cast<const int32_t*>(elem);
    const float font_height = *reinterpret_cast<const float*>(layout + 0x28);
    float height;
    if (hud_elem_is_text(type)) {
        height = font_height;
    } else if (hud_elem_is_sized(type)) {
        height = hud_elem_size(elem, 0x34, layout);
        const int32_t fade = *reinterpret_cast<const int32_t*>(elem + 0x48);
        if (fade > 0) {
            const int32_t t = title_global<int32_t>(0x0097253c) - *reinterpret_cast<const int32_t*>(elem + 0x44);
            if (t < fade) {
                const float from = hud_elem_size(elem, 0x40, layout);
                height = g(0x0083eb20) / static_cast<float>(fade) * static_cast<float>(t) * (height - from) + from;
            }
        }
    } else {
        return 0.0f;
    }
    if (*reinterpret_cast<const uint32_t*>(layout + 0x10) && font_height > height) return font_height;
    return height;
}

PLATFORM_ASM_ENTRY(CG_DrawHudElemText_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_DrawHudElemText) "addl $16, %esp\n"
                   "ret")

void XB_CDECL CG_DrawHudElemText(uint8_t* layout, const uint8_t* elem, char* buffer, int32_t size) {
    const auto ei = [elem](uint32_t offset) { return *reinterpret_cast<const int32_t*>(elem + offset); };
    const auto lf = [layout](uint32_t offset) -> float& { return *reinterpret_cast<float*>(layout + offset); };
    const auto ls = [layout](uint32_t offset) -> const char*& { return *reinterpret_cast<const char**>(layout + offset); };
    const char* empty = &title_global<const char>(0x0082dfeb);
    // Font (+0x20), scale (+0x24), font height (+0x28) and fixed character width (+0x2c).
    const float font_scale = *reinterpret_cast<const float*>(elem + 0xc);
    switch (ei(0x10)) {
    case 0: {
        *reinterpret_cast<int32_t*>(layout + 0x20) = 0;
        const float scale = font_scale * g(0x0083ec24);
        lf(0x24) = scale;
        lf(0x28) = static_cast<float>(
            title_function<int32_t XB_CDECL(uint32_t, float)>(title_global<uint32_t>(0x00928574))(0, scale));
        lf(0x2c) = 0.0f;
        break;
    }
    case 1:
        *reinterpret_cast<int32_t*>(layout + 0x20) = 4;
        lf(0x24) = font_scale * g(0x0083ed74);
        lf(0x28) = g(0x0083ebb8);
        lf(0x2c) = g(0x0083ebb8);
        break;
    case 2:
        *reinterpret_cast<int32_t*>(layout + 0x20) = 5;
        lf(0x24) = font_scale * g(0x0083ed74);
        lf(0x28) = g(0x0083ebb8);
        lf(0x2c) = g(0x0083ed78);
        break;
    default: break;
    }
    ls(0x10) = ei(0x2c) ? CG_GetHudElemText(ei(0x2c)) : empty;
    switch (ei(0)) {
    case 1: ls(0x18) = ei(0x68) ? CG_GetHudElemText(ei(0x68)) : empty; break;
    case 2:
        ls(0x18) = title_function<const char* XB_CDECL(const char*, double)>(0x002a93d0)(  // va
            &title_global<char>(0x008324d4), *reinterpret_cast<const float*>(elem + 0x64));  // "%g"
        break;
    case 4:
    case 5: ls(0x18) = HudElem_FormatTimer(elem); break;
    case 6:
    case 7: ls(0x18) = HudElem_FormatTenthsTimer(elem); break;
    default: ls(0x18) = empty; break;
    }
    if (*ls(0x10) && *ls(0x18)) HudElem_SubstituteLabel(size, layout, buffer);
    // Widths: printable characters times the fixed width, else the font's.
    const auto width = [&](const char* text) {
        if (!*text) return 0.0f;
        if (lf(0x2c) != 0.0f) return static_cast<float>(Loc_CountChars(0, text)) * lf(0x2c);
        return static_cast<float>(RE_TextWidth(text, *reinterpret_cast<const uint32_t*>(layout + 0x20), lf(0x24)));
    };
    lf(0x14) = width(ls(0x10));
    lf(0x1c) = width(ls(0x18));
    const float w = HudElem_GetWidth(elem, layout);
    lf(8) = w;
    const float h = HudElem_GetHeight(elem, layout);
    lf(0xc) = h;
    const float one = g(0x0083eb20);
    const float half = g(0x0083eb24);
    // Position (+4/+8), moving from +0x4c/+0x50 over +0x58 ms from +0x54; aligned by +0x14/+0x18.
    const auto position = [&](uint32_t to, uint32_t from, uint32_t align, float extent) {
        const int32_t move = ei(0x58);
        float v;
        int32_t t;
        if (move > 0 && (t = title_global<int32_t>(0x0097253c) - ei(0x54)) < move)
            v = static_cast<float>((ei(to) - ei(from)) * t) * (one / static_cast<float>(move)) +
                static_cast<float>(ei(from));
        else
            v = static_cast<float>(ei(to));
        switch (ei(align)) {
        case 1: v = v - extent * half; break;
        case 2: v = v - extent; break;
        default: break;
        }
        return v;
    };
    lf(0) = position(4, 0x4c, 0x14, w);
    lf(4) = position(8, 0x50, 0x18, h);
    // Colour (+0x30, 0..1) from the bytes +0x1c, fading from +0x20 over +0x28 ms from +0x24.
    const float scale = g(0x0083eb30);
    const int32_t fade = ei(0x28);
    int32_t t;
    if (fade > 0 && (t = title_global<int32_t>(0x0097253c) - ei(0x24)) < fade && t >= 0) {
        const float frac = static_cast<float>(t) / static_cast<float>(fade);
        for (uint32_t i = 0; i < 4; ++i)
            lf(0x30 + i * 4) = (static_cast<float>(elem[0x1c + i] - elem[0x20 + i]) * frac +
                                static_cast<float>(elem[0x20 + i])) * scale;
    } else {
        for (uint32_t i = 0; i < 4; ++i) lf(0x30 + i * 4) = static_cast<float>(elem[0x1c + i]) * scale;
    }
}

PLATFORM_ASM_ENTRY(CG_DrawHudElem_entry,
                   "pushl %ecx\n" PLATFORM_ASM_CALL(CG_DrawHudElem) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_DrawHudElem(const uint8_t* elem) {
    alignas(16) uint8_t layout[0x40] = {};
    char buffer[0x2000];
    CG_DrawHudElemText(layout, elem, buffer, 0x2000);
    const char* label = *reinterpret_cast<const char* const*>(layout + 0x10);
    float& x = *reinterpret_cast<float*>(layout);
    if (*label) {
        CG_DrawHudElemAlignedText(layout, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(label)), elem);
        x = x + *reinterpret_cast<const float*>(layout + 0x14);
    }
    switch (*reinterpret_cast<const int32_t*>(elem)) {
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 7: {
        const char* text = *reinterpret_cast<const char* const*>(layout + 0x18);
        if (*text) CG_DrawHudElemAlignedText(layout, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)), elem);
        break;
    }
    case 3: CG_DrawHudElemMaterial(elem, layout); break;
    case 8:
    case 9: CG_DrawHudElemClock(elem, layout); break;
    default: break;
    }
    const uint8_t* paused = title_function<const uint8_t* XB_CDECL(const char*, const char*, uint32_t)>(0x0029c2d0)(  // Cvar_Get
        &title_global<char>(0x0082ca00), &title_global<char>(0x0082e098), 0);  // "cl_paused", "0"
    if (*reinterpret_cast<const float*>(elem + 0x6c) == g(0x0083ebb4) && !*reinterpret_cast<const int32_t*>(paused + 0x20))
        title_function<void XB_CDECL(uint32_t)>(0x002960a0)(0);
}

int32_t XB_REGS2 XB_CDECL Loc_CountChars(uint32_t, const char* text) {
    if (!text || !*text) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
    int32_t count = 0;
    do {
        const uint32_t c = next_text_char(p);
        if (c == '^') {
            if (*p != '^' && *p >= '0' && *p <= '9') {
                ++p;
                continue;
            }
        } else if (c == '\n' || c == '\r') {
            continue;
        }
        ++count;
    } while (*p);
    return count;
}

PLATFORM_ASM_ENTRY(CG_DrawHudElemClock_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_DrawHudElemClock) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_DrawHudElemClock(const uint8_t* he, const uint8_t* rect) {
    // A clock hud element: its material (+0x38) as the face and "<material name>needle" turned
    // by the timer value (a full turn per +0x60 ms, else per 0x10000 / 1.09227 ms), sized like
    // CG_DrawHudElemMaterial (+0x30/+0x34, fading from +0x3c/+0x40) and aligned vertically in
    // the rect by +0x18; colour rect +0x30.
    const uint8_t* material = *reinterpret_cast<const uint8_t* const*>(he + 0x38);
    if (!material) return;
    char name[0x80];
    const char* source = reinterpret_cast<const char*>(material + 0x58);
    uint32_t n = 0;
    while ((name[n] = source[n]) != 0) ++n;
    const char* suffix = &title_global<char>(0x0083993c);  // "needle"
    for (uint32_t k = 0; k < 7; ++k) name[n + k] = suffix[k];
    const uint32_t needle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RE_RegisterShader(0, name)));
    const float value = static_cast<float>(HudElem_GetTimerValue(he));
    const int32_t period = *reinterpret_cast<const int32_t*>(he + 0x60);
    int32_t turn;
    if (period)
        turn = static_cast<int32_t>(value * g(0x0083ebcc) / static_cast<float>(period) * g(0x0083ebd8)) & 0xffff;
    else
        turn = static_cast<int32_t>(value * g(0x0083f06c)) & 0xffff;
    const float angle = static_cast<float>(turn) * g(0x0083ebd4);
    const float fallback = *reinterpret_cast<const float*>(rect + 0x28);
    const auto size = [he, fallback](uint32_t offset) {
        const int32_t v = *reinterpret_cast<const int32_t*>(he + offset);
        return v ? static_cast<float>(v) : fallback;
    };
    const int32_t fade = *reinterpret_cast<const int32_t*>(he + 0x48);
    const int32_t now = title_global<int32_t>(0x0097253c);
    const float one = g(0x0083eb20);
    float w = size(0x30);
    if (fade > 0 && now - *reinterpret_cast<const int32_t*>(he + 0x44) < fade) {
        const float from = size(0x3c);
        const float t = one / static_cast<float>(fade) * static_cast<float>(now - *reinterpret_cast<const int32_t*>(he + 0x44));
        w = (w - from) * t + from;
    }
    float h = size(0x34);
    if (fade > 0 && now - *reinterpret_cast<const int32_t*>(he + 0x44) < fade) {
        const float from = size(0x40);
        const float t = one / static_cast<float>(fade) * static_cast<float>(now - *reinterpret_cast<const int32_t*>(he + 0x44));
        h = (h - from) * t + from;
    }
    const float* r = reinterpret_cast<const float*>(rect);
    float y;
    switch (*reinterpret_cast<const int32_t*>(he + 0x18)) {
    case 0: y = r[1]; break;
    case 1: y = (r[3] - h) * g(0x0083eb24) + r[1]; break;
    case 2: y = (r[3] + r[1]) - h; break;
    default: y = 0.0f; break;
    }
    const auto set_color = title_function<void XB_CDECL(const void*)>(title_global<uint32_t>(0x0092852c));
    set_color(rect + 0x30);
    const float xs = g(0x009758b8), ys = g(0x009758bc);
    using StretchPic = void XB_CDECL(float, float, float, float, float, float, float, float, const uint8_t*);
    title_function<StretchPic>(title_global<uint32_t>(0x00928530))(xs * r[0], ys * y, xs * w, ys * h, 0.0f, 0.0f, 1.0f, 1.0f,
                                                                    material);
    CG_DrawRotatedPic(r[0], y, w, h, needle, angle);
    set_color(nullptr);
}

uint32_t XB_CDECL CG_DrawObjectives() {
    // The objectives screen while the scores/objectives display is up (0x00973a78), not with
    // 0x0094c7a4 and 0x0094bf6c both set nor in intermission (0x00972574 >= 4): faded in over
    // 100 ms from 0x00973a7c (CG_FadeColor's alpha turned around in place).
    if (title_global<uint32_t>(0x0094c7a4) && title_global<uint32_t>(0x0094bf6c)) return 0;
    if (title_global<int32_t>(0x00972574) >= 4) return 0;
    if (!title_global<uint32_t>(0x00973a78)) return 0;
    float* color = CG_FadeColor(0, title_global<int32_t>(0x00973a7c), 100);
    float alpha = g(0x0083eb20);
    if (color) {
        alpha = alpha - color[3];
        color[3] = alpha;
    } else {
        color = &title_global<float>(0x0082eb9c);
    }
    CG_DrawObjectiveList(color, alpha);
    return 1;
}

namespace {

const char* shock_path(const char* name) {
    return title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
        &title_global<char>(0x00838b30), name);  // "scripts/%s.shock"
}

}  // namespace

// The original advances its text argument slot in place: the entry hands the C version the
// argument block, and it writes the slot as the original does.
PLATFORM_ASM_ENTRY(CG_ShellShock_ParseParams_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_ShellShock_ParseParams) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL CG_ShellShock_ParseParams(uint32_t* args) {
    // (names, count, text, session name): "<cvar> <value>" lines, each name of the list set
    // (Cvar_Set2, forced) to the next token on its line, the rest of the line skipped; 1 when
    // every name was set at least once. The parse session's unget (+0x84; +0x88 = the token
    // at +0 is still valid) is inlined, as is the session's end.
    const char* const* names = reinterpret_cast<const char* const*>(static_cast<uintptr_t>(args[0]));
    const int32_t count = static_cast<int32_t>(args[1]);
    const char*& text = *reinterpret_cast<const char**>(&args[2]);
    uint8_t seen[0x10000];
    std::memset(seen, 0, static_cast<uint32_t>(count));
    int32_t found = 0;
    Com_BeginParseSession(reinterpret_cast<const char*>(static_cast<uintptr_t>(args[3])));
    uint8_t* info = title_global<uint8_t*>(0x008d4ca0);
    auto field = [](uint8_t* p, uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(p + offset); };
    for (;;) {
        if (field(info, 0x84)) {
            field(info, 0x84) = 0;
            text = reinterpret_cast<const char*>(static_cast<uintptr_t>(field(info, 0x98)));
            field(info, 0x80) = field(info, 0x94);
        }
        const char* token = COM_ParseExt(&text, 1);
        if (!*token) break;
        if (count > 0) {
            int32_t i = 0;
            while (title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(token, names[i])) {  // __stricmp
                if (++i >= count) break;
            }
            if (i < count) {
                uint8_t* p = title_global<uint8_t*>(0x008d4ca0);
                char* value;
                if (!field(p, 0x84)) {
                    value = COM_ParseExt(&text, 0);
                } else {
                    field(p, 0x84) = 0;
                    if (field(p, 0x88)) {
                        text = reinterpret_cast<const char*>(static_cast<uintptr_t>(field(p, 0x98)));
                        field(p, 0x80) = field(p, 0x94);
                        value = COM_ParseExt(&text, 0);
                    } else {
                        value = reinterpret_cast<char*>(p);
                    }
                }
                Cvar_Set2(names[i], value, 1);
                if (!seen[i]) {
                    seen[i] = 1;
                    ++found;
                }
            }
            info = title_global<uint8_t*>(0x008d4ca0);
        }
        // The rest of the line skipped (a newline counts a line).
        const char* c = text;
        if (!c) continue;
        int32_t ch = static_cast<signed char>(*c);
        if (ch) {
            for (;;) {
                ++c;
                if (ch == '\n') {
                    ++field(info, 0x80);
                    break;
                }
                ch = static_cast<signed char>(*c);
                if (!ch) break;
            }
        }
        text = c;
    }
    int32_t depth = title_global<int32_t>(0x00927f54);
    if (depth == 0)
        title_function<void XB_CDECL(uint32_t, const char*)>(0x002938f0)(0, &title_global<const char>(0x008338c8));
    --depth;
    title_global<int32_t>(0x00927f54) = depth;
    title_global<uint8_t*>(0x008d4ca0) = &title_global<uint8_t>(0x008d3ae0 + depth * 0x11c);
    return found == count;
}

uint32_t XB_REGS1 XB_CDECL CG_ShellShock_LoadParams(const char* name) {
    // Reads scripts/<name>.shock into the shell shock cvars (0x0029e700 over the 0x1c names
    // at 0x008da5f0) and updates their vmCvars (0x008da660); 0 when the file is missing.
    const char* path = shock_path(name);
    int32_t file;
    const int32_t length = title_function<int32_t XB_CDECL(const char*, int32_t*, int32_t)>(0x002a1b30)(path, &file, 0);  // FS_FOpenFileByMode
    if (length < 0) {
        CG_Printf(&title_global<char>(0x00838b18), path);
        return 0;
    }
    char* text = title_function<char* XB_CDECL(int32_t)>(0x00042c10)(length + 1);  // Z_Malloc
    title_function<void XB_CDECL(void*, int32_t, int32_t)>(0x002971a0)(text, 0, length + 1);  // Com_Memset
    title_function<int32_t XB_CDECL(void*, int32_t, int32_t)>(0x00297af0)(text, length, file);  // FS_Read
    text[length] = 0;
    title_function<void XB_CDECL(int32_t)>(0x00297d70)(file);  // FS_FCloseFile
    const uint32_t result = title_function<uint32_t XB_CDECL(uint32_t, uint32_t, const char*, const char*)>(0x0029e700)(  // CG_ShellShock_ParseParams
        0x008da5f0, 0x1c, text, path);
    title_function<void XB_CDECL(void*)>(0x000428d0)(text);  // Z_Free
    for (uint32_t k = 0; k < 0x70; k += 4) cg_cvar_update(title_global<uint32_t>(0x008da660 + k));
    return result;
}

uint32_t XB_CDECL CG_ShellShock_SaveParams(const char* name) {
    // The shell shock cvars written as scripts/<name>.shock; 0 on failure.
    char text[0x10000];
    if (!title_function<uint32_t XB_CDECL(uint32_t, uint32_t, char*, uint32_t)>(0x0029c4f0)(  // Cvar_WriteNamedValues
            0x008da5f0, 0x1c, text, 0x10000))
        return 0;
    int32_t file;
    if (title_function<int32_t XB_CDECL(const char*, int32_t*, int32_t)>(0x002a1b30)(shock_path(name), &file, 1) < 0) return 0;
    int32_t length = 0;
    while (text[length]) ++length;
    title_function<int32_t XB_CDECL(const void*, int32_t, int32_t)>(0x00297a60)(text, length, file);  // FS_Write
    title_function<void XB_CDECL(int32_t)>(0x00297d70)(file);  // FS_FCloseFile
    return 1;
}

void XB_CDECL CG_ShellShockSave_f() {
    if (title_global<uint32_t>(0x00934248) != 2) {
        CG_Printf(&title_global<char>(0x00839b94));
        return;
    }
    char name[0x80];
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(name, title_global<const char*>(0x0092df94), 0x7f);
    name[0x7f] = 0;
    CG_ShellShock_SaveParams(name);
}

void XB_CDECL CG_ShellShockLoad_f() {
    if (title_global<uint32_t>(0x00934248) != 2) {
        CG_Printf(&title_global<char>(0x00839bb8));
        return;
    }
    char name[0x80];
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(name, title_global<const char*>(0x0092df94), 0x7f);
    name[0x7f] = 0;
    CG_ShellShock_LoadParams(name);
}

void XB_CDECL CG_ShellShock_f() {
    // "cg_shellshock <duration> [file]": a shell shock of `duration` seconds from now with the
    // current (or the file's) parameters (CG_ReadShellShockCvars into 0x00975b44).
    const uint32_t argc = title_global<uint32_t>(0x00934248);
    char text[0x100];
    if (argc == 3) {
        title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(text, title_global<const char*>(0x0092df98), 0xff);
        text[0xff] = 0;
        if (!CG_ShellShock_LoadParams(text)) return;
    } else if (argc != 2) {
        CG_Printf(&title_global<char>(0x00839bdc));
        return;
    }
    const char* arg = title_global<uint32_t>(0x00934248) > 1 ? title_global<const char*>(0x0092df94)
                                                              : reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(text, arg, 0xff);
    text[0xff] = 0;
    using ext = long double;
    const float seconds = static_cast<float>(title_function<ext XB_CDECL(const char*)>(0x001ef056)(text) *  // atof
                                             static_cast<ext>(g(0x0083eb9c)));
    CG_ReadShellShockCvars(&title_global<uint8_t>(0x00975b44));
    title_global<int32_t>(0x00973a70) = title_global<int32_t>(0x0097253c);
    title_global<int32_t>(0x00973a74) =
        static_cast<int32_t>(static_cast<int64_t>(static_cast<ext>(seconds) + static_cast<ext>(g(0x0083eb24))));
}

void XB_CDECL CG_DrawCheckpointReached() {
    // HUD notices: the timed message configstring (0x00975810, green, fading with its timer
    // 0x0095f65c / 3) unless letterboxed, "Quad Speed" while 0x0095bd54 > 190, and
    // "INGAME_CHECKPOINT_REACHED" (localized) while the checkpoint timer (+0x34c of 0x0095f4e0)
    // runs; the timers count down by the frame time 0x008ddaa0.
    using DrawText = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, float, uint32_t, uint32_t);
    const auto draw_text = [](float x, float y, const float* color, const char* text) {
        title_function<DrawText>(title_global<uint32_t>(0x00928578))(x, y, 4, 0x1.555556p-2f /* 0x3eaaaaab, 1/3 */, color, text, 16.0f, 0, 3);
    };
    const uint8_t* letterbox = title_function<const uint8_t* XB_CDECL(const char*, const char*, uint32_t)>(0x0029c2d0)(  // Cvar_Get
        &title_global<char>(0x00835390), &title_global<char>(0x0082e098), 0);
    const float one = g(0x0083eb20);
    float& message_time = title_global<float>(0x0095f65c);
    const float t = message_time;
    if (t > 0.0f && !*reinterpret_cast<const uint32_t*>(letterbox + 0x20)) {
        const float color[4] = {0.0f, one, 0.0f, t / g(0x0083ec9c)};
        message_time = t - g(0x008ddaa0);
        const uint8_t* string = title_global<const uint8_t*>(0x00975810);
        draw_text(30.0f, 314.0f, color, string ? reinterpret_cast<const char*>(string + 6) : nullptr);
    }
    if (title_global<int32_t>(0x0095bd54) > 0xbe) {
        const float white[4] = {one, one, one, one};
        draw_text(500.0f, 64.0f, white, &title_global<char>(0x00839ab8));
    }
    uint8_t* game = title_global<uint8_t*>(0x0095f4e0);
    float& checkpoint = *reinterpret_cast<float*>(game + 0x34c);
    if (checkpoint > 0.0f) {
        const float color[4] = {one, one, 0.0f, checkpoint / g(0x0083ece8)};
        checkpoint = checkpoint - g(0x008ddaa0);
        const char* text = &title_global<char>(0x00839a9c);
        TitleRegs regs{};
        regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
        const uint32_t key = cg_call_regs(0x00024440, &regs, nullptr, 0);  // Loc_HashKey
        regs = TitleRegs{};
        regs.eax = title_global<uint32_t>(0x0095f5d8);
        const uint32_t args[1] = {key};
        const uint8_t* entry = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(cg_call_regs(0x002984f0, &regs, args, 1)));  // Zone_FindByHash
        if (entry) {
            text = *reinterpret_cast<const char* const*>(entry + 8);
            if (!text) text = &title_global<char>(0x008379b4);
        }
        draw_text(50.0f, 64.0f, color, text);
    }
}

void XB_CDECL CG_FreeWeaponModels() {
    // Every weapon view model's DObj (handle +0x8c of the model 0x0097606c + (weapon - 1) *
    // 0xc4, resolved through 0x00981418) freed with its trees, then its memory returned
    // (XModel_Free, then the client's memory context +0x34 or the zone heap).
    const auto resolve = [](const uint8_t* model) -> uint8_t* {
        const uint32_t handle = model ? *reinterpret_cast<const uint32_t*>(model + 0x8c) : 0;
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0098141c + index * 8)) return nullptr;
        return title_global<uint8_t*>(0x00981418 + index * 8);
    };
    for (int32_t weapon = 1; weapon <= title_global<int32_t>(0x0095f4d4); ++weapon) {
        const uint8_t* const* slot = &title_global<const uint8_t*>(0x0097606c + static_cast<uint32_t>(weapon - 1) * 0xc4);
        if (!resolve(*slot)) continue;
        DObj_Free(reinterpret_cast<DObj*>(resolve(*slot)), 1);
        uint8_t* obj = resolve(*slot);
        const int32_t client = *reinterpret_cast<const int32_t*>(resolve(*slot) + 0x34);
        if (!obj) continue;
        title_function<void XB_STDCALL(uint8_t*)>(0x0023cea0)(obj);  // XModel_Free
        if (client != -1) {
            uint32_t context = 0;
            if (client >= 0 && client < 0x63)
                context = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x38 + client * 4);
            if (title_function<uint8_t XB_THISCALL(uint32_t, void*)>(0x0028bfc0)(context, obj)) continue;  // MemContext_Free_r
        }
        title_function<void XB_CDECL(void*)>(0x000428d0)(obj);  // Z_Free
    }
}

namespace {

const char* configstring_text(uint32_t slot) {
    const uint8_t* s = title_global<const uint8_t*>(slot);
    return s ? reinterpret_cast<const char*>(s + 6) : nullptr;
}

}  // namespace

int32_t XB_CDECL CG_ParseInfoValueT() {
    // Configstring 3's info value "t" as an integer (its "n" is looked up and dropped).
    const char* info = configstring_text(0x009606c4);
    const auto value_for = [info](uint32_t key) {
        TitleRegs regs{};
        regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
        regs.esi = regs.ecx;
        regs.edi = key;
        return reinterpret_cast<const char*>(static_cast<uintptr_t>(cg_call_regs(0x002b8f50, &regs, nullptr, 0)));  // Info_ValueForKey
    };
    value_for(0x00838d2c);  // "n"
    return title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(value_for(0x00838d28));  // _atol of "t"
}

void XB_CDECL CG_ParseConfigStringFloat9() {
    // Configstring 9 as a float handed to the import 0x00928520.
    const float value = title_function<float XB_CDECL(const char*)>(0x001ef056)(configstring_text(0x009606dc));  // atof
    title_function<void XB_CDECL(float)>(title_global<uint32_t>(0x00928520))(value);
}

void XB_CDECL CG_ParseConfigStringFloat11() {
    // Configstring 11 as a float into 0x009730a4.
    title_global<float>(0x009730a4) = title_function<float XB_CDECL(const char*)>(0x001ef056)(configstring_text(0x009606e4));  // atof
}

// --- Push batch 2 ---

namespace {

// The rumble system's two lists of playing rumbles (head +8 + 16 * motor, end sentinel 4 bytes
// on, count 4 bytes before), each node {+0 next, +4 previous, +8 id, +0x28 script string}.
uint32_t& word_at(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

}  // namespace

uint8_t XB_REGS2 XB_STDCALL Rumble_IsActive(uint32_t, uint8_t* rumble, uint32_t id) {
    for (uint32_t motor = 0; motor < 2; ++motor) {
        const uint32_t list = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rumble)) + 8 + motor * 0x10;
        uint32_t node = word_at(list);
        uint32_t next = node ? word_at(node) : 0;
        if (node == list + 4 || !next) continue;
        do {
            if (word_at(node + 8) == id) return 1;
            node = next;
            next = word_at(next);
        } while (next);
    }
    return 0;
}

void XB_REGS1 XB_STDCALL Rumble_Stop(uint8_t* rumble, uint32_t id) {
    // The first rumble with that id in each list is unlinked, its script string released and the
    // node returned to the small heap (0x0095f5c8).
    for (uint32_t motor = 0; motor < 2; ++motor) {
        const uint32_t list = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rumble)) + 8 + motor * 0x10;
        uint32_t node = word_at(list);
        uint32_t next = node ? word_at(node) : 0;
        if (node == list + 4 || !next) continue;
        for (;;) {
            if (word_at(node + 8) == id) {
                word_at(word_at(node) + 4) = word_at(node + 4);
                word_at(word_at(node + 4)) = word_at(node);
                word_at(node) = 0;
                word_at(node + 4) = 0;
                --word_at(list - 4);
                title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(node + 0x28);  // ScriptString_Release
                title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x00044910)(title_global<uint32_t>(0x0095f5c8),
                                                                                 node);  // SmallHeap_Free
                break;
            }
            if (!next) break;
            node = next;
            next = word_at(next);
            if (!next) break;
        }
    }
}

uint32_t XB_CDECL CG_GetGrenadeAmmoCount() {
    // The ammo (clip + stock) of every owned weapon of class 5 (grenades, def +0x98).
    uint32_t total = 0;
    for (int32_t weapon = 1; weapon <= title_global<int32_t>(0x0095f4d4); ++weapon) {
        if (!owns_weapon(weapon)) continue;
        const uint8_t* def = weapon_def(weapon);
        if (*reinterpret_cast<const int32_t*>(def + 0x98) != 5) continue;
        total += title_global<uint32_t>(0x009727d0 + *reinterpret_cast<const uint32_t*>(def + 0x5a0) * 4) +
                 title_global<uint32_t>(0x00972660 + *reinterpret_cast<const uint32_t*>(def + 0x598) * 4);
    }
    return total;
}

void XB_CDECL CG_FxRestart_f() {
    title_function<void XB_CDECL()>(0x0023b800)();  // FX_InitEffectPool
    title_function<void XB_CDECL()>(0x0023b6d0)();  // FX_ClearEffects
    title_function<void XB_CDECL()>(0x00094170)();  // FX_ResetParticleSystem
    title_function<void XB_CDECL()>(0x00236aa0)();  // FX_FreeAllEffectLights
}

void XB_REGS3 XB_CDECL CG_ItemPickup(uint32_t, uint32_t, int32_t item) {
    // Q3's CG_ItemPickup: the pickup (0x00972ecc) and its times (0x00972ed0 / 0x00972ed4); a
    // weapon item (bg_itemlist 0x0096bd10 + item * 0x34: type +0x24 == 1, tag +0x28) is requested
    // through the vmCvars 0x0094c230 (time) and 0x00947f98 (weapon) unless one is pending (its
    // integer 0x00947fa4).
    const uint8_t* entry = &title_global<uint8_t>(0x0096bd10 + static_cast<uint32_t>(item) * 0x34);
    title_global<int32_t>(0x00972ecc) = item;
    const int32_t time = cg_time();
    const int32_t weapon = *reinterpret_cast<const int32_t*>(entry + 0x28);
    title_global<int32_t>(0x00972ed0) = time;
    title_global<int32_t>(0x00972ed4) = time;
    if (*reinterpret_cast<const int32_t*>(entry + 0x24) != 1 || title_global<int32_t>(0x00947fa4)) return;
    set_vm_cvar(0x0094c230, va_int(time));
    set_vm_cvar(0x00947f98, va_int(weapon));
}

uint32_t XB_REGS3 XB_STDCALL FxTransform_Init(uint32_t, const uint32_t* quat, const uint32_t* origin, uint32_t* out,
                                              uint32_t id) {
    // An effect transform block: the rotation (EDX) at +0, the position (ECX) at +0x10, +0x20 = id;
    // returns the block.
    for (uint32_t i = 0; i < 4; ++i) out[i] = quat[i];
    for (uint32_t i = 0; i < 4; ++i) out[4 + i] = origin[i];
    out[8] = id;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
}

namespace {

// CG_DrawWhiteStringFont5 / 4: white text with the given alpha, 1/3 scale, 14 units lower.
void draw_white_string(const char* text, float x, float y, float alpha, uint32_t font, float width, uint32_t style) {
    using DrawText = void XB_CDECL(float, float, uint32_t, float, const float*, const char*, float, uint32_t, uint32_t);
    const float one = g(0x0083eb20);
    const float color[4] = {one, one, one, alpha};
    const float top = (y + g(0x0083ebb8)) - g(0x0083eba8);  // + 16 - 2
    title_function<DrawText>(title_global<uint32_t>(0x00928578))(x, top, font, 0x1.555556p-2f /* 1/3 */, color, text,
                                                                 width, 0, style);
}

}  // namespace

void XB_REGS1 XB_CDECL CG_DrawWhiteStringFont5(const char* text, float x, float y, float alpha) {
    draw_white_string(text, x, y, alpha, 5, 8.0f, 0);
}

void XB_REGS1 XB_CDECL CG_DrawWhiteStringFont4(const char* text, float x, float y, float alpha) {
    draw_white_string(text, x, y, alpha, 4, 16.0f, 3);
}

uint8_t XB_THISCALL CG_ViewWeaponAnimsInactive(const uint8_t* tree) {
    // Whether view-weapon anims 9, 10, 12 and 11 (the tree's anim-info indices +0x2a, +0x2c,
    // +0x30, +0x2e; 0 = none) all have zero weight (anim info +0x14; NaN counts as weight).
    static const uint32_t kOffsets[4] = {0x2a, 0x2c, 0x30, 0x2e};
    for (uint32_t k = 0; k < 4; ++k) {
        const uint16_t index = *reinterpret_cast<const uint16_t*>(tree + kOffsets[k]);
        if (!index) continue;
        if (!(title_global<float>(0x00965d14 + index * 0x30u) == 0.0f)) return 0;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(CG_ViewWeaponAnimsFinished_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_ViewWeaponAnimsFinished) "addl $8, %esp\n"
                   "ret")

uint8_t XB_CDECL CG_ViewWeaponAnimsFinished(const uint8_t* tree, uint8_t* weapon) {
    // 0 (and the weapon's +0x80 = -1) while one of anims 4..22 still runs (an anim info whose time
    // +0 did not pass its previous time +4, is not at 1, and whose frame +8 is not past +0xa) or
    // the ADS fraction 0x009725d8 is strictly between 0.01 and 0.99; else 1.
    const float one = g(0x0083eb20);
    for (uint32_t i = 4; i < 0x17; ++i) {
        const uint16_t index = *reinterpret_cast<const uint16_t*>(tree + 0x18 + i * 2);
        if (!index) continue;
        const uint8_t* info = &title_global<uint8_t>(0x00965d00 + index * 0x30u);
        if (*reinterpret_cast<const float*>(info + 4) > *reinterpret_cast<const float*>(info)) continue;
        if (*reinterpret_cast<const float*>(info) == one) continue;
        if (*reinterpret_cast<const int16_t*>(info + 8) <= *reinterpret_cast<const int16_t*>(info + 0xa)) {
            *reinterpret_cast<int32_t*>(weapon + 0x80) = -1;
            return 0;
        }
    }
    const float ads = g(0x009725d8);
    if (ads > g(0x0083eb94) && g(0x0083ed08) > ads) {
        *reinterpret_cast<int32_t*>(weapon + 0x80) = -1;
        return 0;
    }
    return 1;
}

namespace {

uint8_t* view_anim_info(uint32_t info) { return &title_global<uint8_t>(0x00965cf0 + info * 0x30); }
float& info_f(uint8_t* info, uint32_t offset) { return *reinterpret_cast<float*>(info + offset); }

// The inlined goal-weight setter: goal (+0x20) reached over |goal - weight| x blend (+0x1c;
// at once under 0.001, else a weight of 0 starts at goal x 0.001), the rate (+0x28), no
// notetrack (+2 = -1) and +0 cleared.
void view_anim_set_goal(uint8_t* info, float goal, float blend, float rate) {
    const float r = abs_bits(goal - info_f(info, 0x24)) * blend;
    info_f(info, 0x20) = goal;
    info_f(info, 0x1c) = r;
    if (g(0x0083eb4c) > r) {
        info_f(info, 0x1c) = 0.0f;
        info_f(info, 0x24) = goal;
    } else if (info_f(info, 0x24) == 0.0f) {
        info_f(info, 0x24) = goal * g(0x0083eb4c);
    }
    info_f(info, 0x28) = rate;
    *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
    *reinterpret_cast<uint16_t*>(info) = 0;
}

// Node `node` of `tree` to `goal` over `blend` (allocated when missing and `create`), then
// its parents (blend time `parents_bits`), its time (restart unless `keep`) and notetrack.
void view_anim_node(uint8_t* tree, uint32_t node, float goal, float blend, float rate, bool create,
                    uint32_t parents_bits, uint32_t keep) {
    const uint16_t index = *reinterpret_cast<const uint16_t*>(tree + 0x18 + node * 2);
    title_global<uint8_t*>(0x00928588) = tree;
    uint8_t* info = nullptr;
    if (index)
        info = view_anim_info(index);
    else if (create)
        info = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(AnimTree_AllocInfo(node, tree)));
    if (info) view_anim_set_goal(info, goal, blend, rate);
    AnimTree_ActivateParents(node, parents_bits);
    AnimTree_ResetTime(node, keep);
    AnimTree_UpdateNoteIndex(0, node);
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_PlayViewWeaponAnim_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_PlayViewWeaponAnim) "addl $16, %esp\n"
                   "ret")

void XB_CDECL CG_PlayViewWeaponAnim(uint32_t weapon, uint32_t anim, uint8_t* obj, uint32_t blend_bits) {
    // On each anim tree of the view weapon's object (+0x2e trees at +0): view-weapon anim
    // `anim` (1..24) to weight 1 at once, the others to 0 over `blend`, each at the weapon's
    // rate (0x00975fa8 + weapon * 0xc4, +4 per anim). While aiming, the fire anims 9-11 also
    // kick anim 24 (to 1, then to 0 over 0.3), clear 23 and blend `anim` in again over 0.3.
    // The checks for node 0 (goal 1, rate 1) can never fire and are left out.
    const uint8_t* const rates = &title_global<uint8_t>(0x00975fa8 + weapon * 0xc4);
    auto rate_of = [rates](uint32_t node) { return *reinterpret_cast<const float*>(rates + 4 + node * 4); };
    float blend;
    std::memcpy(&blend, &blend_bits, 4);
    const float one = g(0x0083eb20);
    for (uint32_t t = 0; t < obj[0x2e]; ++t) {
        uint8_t* const tree = reinterpret_cast<uint8_t* const*>(obj)[t];
        if (!tree) continue;
        for (uint32_t i = 1; i < 0x19; ++i) {
            if (i == anim)
                view_anim_node(tree, i, one, 0.0f, rate_of(i), true, 0, 1);
            else
                view_anim_node(tree, i, 0.0f, blend, rate_of(i), false, blend_bits, 0);
        }
        if (!(g(0x009725d8) > 0.0f)) continue;
        if (anim != 10 && anim != 9 && anim != 11) continue;
        const float kick = g(0x0083066c);  // 0.3
        view_anim_node(tree, 0x18, one, 0.0f, rate_of(0x18), true, 0, 1);
        view_anim_node(tree, 0x18, 0.0f, kick, rate_of(0x18), false, 0x3e99999au, 0);
        view_anim_node(tree, 0x17, 0.0f, 0.0f, rate_of(0x17), false, 0, 1);
        view_anim_node(tree, anim, 0.0f, 0.0f, rate_of(anim), false, 0, 1);
        view_anim_node(tree, anim, one, kick, rate_of(anim), true, 0x3e99999au, 1);
    }
}

namespace {

// An anim info's time (+0x10, +0x14) set to t, its frames (+0x18, +0x1a) to 0.
void view_anim_set_time(uint16_t index, float t) {
    uint8_t* const info = view_anim_info(index);
    info_f(info, 0x10) = t;
    *reinterpret_cast<uint16_t*>(info + 0x18) = 0;
    info_f(info, 0x14) = t;
    *reinterpret_cast<uint16_t*>(info + 0x1a) = 0;
}

uint32_t float_word(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_UpdateViewWeaponAdsAnims_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_UpdateViewWeaponAdsAnims) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_UpdateViewWeaponAdsAnims(uint8_t* tree, const uint8_t* rec) {
    // The ADS in/out anims 23 and 24 of a view-weapon tree (EAX; rec = the weapon's anim
    // record, rates at +4 per anim): while the ADS fraction 0x009725d8 is between 0 and 1 their
    // times follow it (23 at the fraction, 24 at 1 - it, when they have weight); at 0 or 1 both
    // go to 0 at once. Then, aiming in with the first of anims 9, 10, 12 that has weight (12
    // without a weight test) at full weight and under 0.4 s left (length x (1 - time) x rate),
    // that anim fades out over 0.3 / rate and 24 restarts at full weight at 1 - the fraction.
    const float aim = g(0x009725d8);
    const float one = g(0x0083eb20);
    auto rate_of = [rec](uint32_t node) { return *reinterpret_cast<const float*>(rec + 4 + node * 4); };
    auto word = [tree](uint32_t node) { return *reinterpret_cast<const uint16_t*>(tree + 0x18 + node * 2); };
    auto weight = [](uint16_t index) -> const float& { return info_f(view_anim_info(index), 0x24); };
    if (!(aim == 0.0f) && !(aim == one)) {
        const uint16_t in = word(0x17);
        if (in && !(weight(in) == 0.0f)) view_anim_set_time(in, aim);
        const uint16_t out = word(0x18);
        if (out && !(weight(out) == 0.0f)) view_anim_set_time(out, one - aim);
    } else {
        view_anim_node(tree, 0x17, 0.0f, 0.0f, rate_of(0x17), false, 0, 1);
        view_anim_node(tree, 0x18, 0.0f, 0.0f, rate_of(0x18), false, 0, 1);
    }
    const float aim2 = g(0x009725d8);
    float w = 0.0f;
    uint32_t anim = 9;
    const uint16_t i9 = word(9);
    if (i9 && !(weight(i9) == 0.0f)) {
        w = weight(i9);
    } else {
        anim = 0xa;
        const uint16_t i10 = word(0xa);
        if (i10 && !(weight(i10) == 0.0f)) {
            w = weight(i10);
        } else {
            anim = 0xc;
            const uint16_t i12 = word(0xc);
            w = i12 ? weight(i12) : 0.0f;
        }
    }
    if (!(aim2 > 0.0f)) return;
    if (!(w == g(0x0083eb20))) return;
    float t = 0.0f;
    if (const uint16_t index = word(anim)) t = info_f(view_anim_info(index), 0x10);
    const uint8_t* const anims = *reinterpret_cast<uint8_t* const*>(tree + 8);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(anims + 4);
    uint8_t* const node = *reinterpret_cast<uint8_t* const*>(anims + 8) + (anim >= count ? 0 : anim) * 0x38;
    if (!*reinterpret_cast<const uint32_t*>(node + 0x24)) AnimData_EnsureLoaded(node);
    const uint8_t* const clip = *reinterpret_cast<uint8_t* const*>(node + 0x24);
    const float length = clip ? *reinterpret_cast<const float*>(clip + 0x38) : g(0x0083eb20);
    const float left = (length - length * t) * rate_of(anim);
    if (!(g(0x0083ec44) > left)) return;
    const float fade = static_cast<float>(static_cast<long double>(rate_of(anim)) * static_cast<long double>(g(0x0083066c)));
    const uint32_t fade_bits = float_word(fade);
    const uint32_t rate_bits = float_word(rate_of(anim));
    AnimTree_SetGoalWeightClamped(tree, 0, anim, 0, fade_bits, rate_bits, 0, 0, 0);
    AnimTree_SetGoalWeightClamped(tree, 0, 0x18, 0, 0, float_word(rate_of(0x18)), 0, 0, 1);
    AnimTree_SetGoalWeightClamped(tree, 0, 0x18, 0x3f800000u, fade_bits, float_word(rate_of(0x18)), 0, 0, 1);
    AnimTree_SetGoalWeightClamped(tree, 0, 0x17, 0, 0, float_word(rate_of(0x17)), 0, 0, 1);
    const uint16_t out = word(0x18);
    const float rest = g(0x0083eb20) - g(0x009725d8);
    if (out) view_anim_set_time(out, rest);
}

void XB_CDECL CG_UpdateViewWeaponAnim(uint8_t* ps, uint8_t* rec) {
    // The view weapon's anims (rec = 0x00975fa8 + weapon * 0xc4: +0 object, +0x80 the anim
    // last started): the ADS anims, then on a new weapon anim (ps +0x4ec, flag 0x200 masked) the
    // matching view anim (CG_PlayViewWeaponAnim; 23/24 blend in over 0.3 once anims 9-12 rest,
    // idle (0/23) waits for the running ones and picks 20 / 3 / 1 / 2 by weapon type, sprint flag
    // and clip; 2 alternates 17 and 4 for type-3 non-ADS weapons). Unknown anims play 1.
    uint8_t* const obj = *reinterpret_cast<uint8_t**>(rec);
    if (!obj) return;
    uint8_t* const tree = *reinterpret_cast<uint8_t**>(obj);
    const int32_t weapon = *reinterpret_cast<const int32_t*>(ps + 0x80);
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[weapon];
    const int32_t state = *reinterpret_cast<const int32_t*>(ps + 0x84);
    uint32_t sprint = 0;
    if ((state == 5 && *reinterpret_cast<const int32_t*>(ps + 0x34) - *reinterpret_cast<const int32_t*>(def + 0x80c) > 0) ||
        state == 0xe)
        sprint = 0;
    else if (ps[0x2c] & 0x20)
        sprint = 1;
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    regs.esi = regs.eax;
    regs.edi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(def));
    regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
    const uint32_t ads_args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rec))};
    cg_call_regs(0x00280ed0, &regs, ads_args, 1);  // CG_UpdateViewWeaponAdsAnims
    const int32_t current = *reinterpret_cast<const int32_t*>(ps + 0x4ec);
    if (current == *reinterpret_cast<const int32_t*>(rec + 0x80)) return;
    static const uint8_t kAnims[24] = {0,    1,    0,   6,   7,    0x11, 0x12, 0x13, 8,    0xe, 0xd,  9,
                                       0xa,  0xb,  0xc, 0x10, 0xf,  5,    1,    0x15, 0x16, 0,   0,    0};
    const uint32_t kick = 0x3e99999au;  // 0.3
    const uint32_t sel = static_cast<uint32_t>(current) & ~0x200u;
    uint32_t anim = 1;
    uint32_t blend = 0;
    switch (sel > 0x17 ? 1 : sel) {
    case 0:
    case 0x17: {
        if (!CG_ViewWeaponAnimsFinished(tree, rec)) return;
        const int32_t type = *reinterpret_cast<const int32_t*>(def + 0x98);
        if ((type == 3 || type == 8) && (ps[0x2c] & 0x20)) {
            anim = 0x14;
        } else if (sprint) {
            anim = 3;
        } else {
            const uint32_t clip = *reinterpret_cast<const uint32_t*>(
                title_global<uint8_t* const*>(0x0095f4d8)[*reinterpret_cast<const int32_t*>(ps + 0x80)] + 0x5a0);
            anim = *reinterpret_cast<const uint32_t*>(ps + 0x280 + clip * 4) ? 1 : 2;
        }
        break;
    }
    case 2:
        if (!*reinterpret_cast<const uint32_t*>(def + 0x6dc) && *reinterpret_cast<const int32_t*>(def + 0x9c) == 3) {
            CG_PlayViewWeaponAnim(static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(ps + 0x80)),
                                  title_global<uint8_t>(0x0098ad48) ? 0x11 : 4, obj, 0);
            title_global<uint8_t>(0x0098ad48) = title_global<uint8_t>(0x0098ad48) == 0;
            *reinterpret_cast<int32_t*>(rec + 0x80) = *reinterpret_cast<const int32_t*>(ps + 0x4ec);
            return;
        }
        anim = 4;
        break;
    case 0x15:
    case 0x16:
        if (!CG_ViewWeaponAnimsInactive(tree)) return;
        anim = sel == 0x15 ? 0x17 : 0x18;
        blend = kick;
        break;
    default:
        anim = kAnims[sel];
        break;
    }
    CG_PlayViewWeaponAnim(static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(ps + 0x80)), anim, obj, blend);
    *reinterpret_cast<int32_t*>(rec + 0x80) = *reinterpret_cast<const int32_t*>(ps + 0x4ec);
}

void XB_CDECL CG_Shutdown() {
    // The voice volume scale back to 0, the weapon models freed, the loaded string table
    // (0x0095f59c when 0x0095f5a0 is 2) freed, then every FX system destroyed (tail jump).
    title_function<void XB_CDECL(uint32_t)>(0x000c5390)(0);  // Snd_SetVoiceVolumeScale
    CG_FreeWeaponModels();
    if (title_global<int32_t>(0x0095f5a0) == 2) {
        title_function<void XB_CDECL(void*)>(0x000428d0)(title_global<void*>(0x0095f59c));  // Z_Free
        title_global<uint32_t>(0x0095f59c) = 0;
        title_global<uint32_t>(0x0095f5a0) = 0;
    }
    FxSystemList_DestroyAll(&title_global<uint8_t>(0x009099a8));
}

void XB_CDECL CG_RegisterGraphics() {
    // Q3's CG_RegisterGraphics: the effect pools reset, seven shaders, the weapon tables
    // (0x00949a98, 0x800 bytes; 0x00975fa8, 0x4670 bytes) cleared, the items, then the inline
    // models of configstrings 0x21.. (until an empty one, at most 0x7f) into 0x00975948, with
    // loading-screen refreshes between.
    FX_InitEffectPool();
    FX_ClearEffects();
    FX_ResetParticleSystem();
    FX_FreeAllEffectLights();
    auto refresh = []() { title_function<void XB_CDECL()>(0x002f1e60)(); };  // SCR_UpdateScreen
    refresh();
    static const uint32_t kShaders[7][2] = {{0x00838dac, 0x00975c4c}, {0x00838d9c, 0x00975c58}, {0x00838d80, 0x00975c5c},
                                           {0x00838d64, 0x00975c60}, {0x00838d4c, 0x00975c64}, {0x00838d3c, 0x00975c50},
                                           {0x00838d30, 0x00975c54}};
    for (uint32_t k = 0; k < 7; ++k)
        title_global<uint8_t*>(kShaders[k][1]) = RE_RegisterShader(0, &title_global<const char>(kShaders[k][0]));
    refresh();
    for (uint32_t k = 0; k < 0x200; ++k) title_global<uint32_t>(0x00949a98 + k * 4) = 0;
    for (uint32_t k = 0; k < 0x119c; ++k) title_global<uint32_t>(0x00975fa8 + k * 4) = 0;
    refresh();
    CG_RegisterItems();
    refresh();
    refresh();
    uint32_t handle = 0;
    for (int32_t i = 0x21; i - 0x20 < 0x80; ++i) {
        const char* name = nullptr;
        if (i >= 0 && i < 0x400) {
            const uint8_t* const string = title_global<const uint8_t*>(0x009606b8 + static_cast<uint32_t>(i) * 4);
            if (string) name = reinterpret_cast<const char*>(string + 6);
        }
        if (!*name) break;
        R_RegisterModel(name, title_global<uint32_t>(0x0095f6ac), &handle,
                        *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x34));
        title_global<uint32_t>(0x00975948 + static_cast<uint32_t>(i - 0x21) * 4) = handle;
    }
    refresh();
}

void XB_REGS1 XB_CDECL CG_ClearState(uint32_t restart) {
    // Q3's CG_Init part that clears cg (0x00971a80, 0x2010 bytes; +0x24 = `restart`): the
    // server-command hashes, on a restart the weapons again (BG_InitWeapons; a loaded string
    // table marked unloaded), the local entities, objectives 16..31, the gravity / time scale
    // configstrings (import 0x00928520 and 0x009730a4), all sounds stopped unless the music
    // channel (0x009600f4) is still playing, the serverinfo read, cg_thirdPerson 0, land and step
    // times -1 and the vehicle turn matrix marked invalid (0x00972c40).
    CG_InitServerCommandHashes();
    if (restart) {
        if (title_global<int32_t>(0x0095f5a0) == 2) title_global<int32_t>(0x0095f5a0) = 0;
        title_function<void XB_CDECL()>(0x002cdf00)();  // BG_InitWeapons
    }
    for (uint32_t k = 0; k < 0x804; ++k) title_global<uint32_t>(0x00971a80 + k * 4) = 0;
    title_global<uint32_t>(0x00971aa4) = restart;
    title_global<uint32_t>(0x00973a68) = 0;
    title_global<uint32_t>(0x00973a6c) = 0;
    CG_InitLocalEntities();
    for (int32_t i = 0x10; i < 0x20; ++i) CG_ParseObjective(i);
    using AtofFn = float XB_CDECL(const char*);
    auto configstring = [](uint32_t address) -> const char* {
        const uint8_t* const string = title_global<const uint8_t*>(address);
        return string ? reinterpret_cast<const char*>(string + 6) : nullptr;
    };
    const float first = title_function<AtofFn>(0x001ef056)(configstring(0x009606dc));
    uint32_t first_bits;
    std::memcpy(&first_bits, &first, 4);
    title_function<void XB_CDECL(uint32_t)>(title_global<uint32_t>(0x00928520))(first_bits);
    title_global<float>(0x009730a4) = title_function<AtofFn>(0x001ef056)(configstring(0x009606e4));
    const uint32_t handle = title_global<uint32_t>(0x009600f4);
    const uint32_t index = handle & 0xfff;
    const bool playing = index < 0x200 && (handle >> 12) == title_global<uint32_t>(0x00962864 + index * 8) &&
                         title_global<uint32_t>(0x00962860 + index * 8);
    if (!playing) {
        TitleRegs regs{};
        regs.ebx = title_global<uint32_t>(0x0095f4c4);
        cg_call_regs(0x002c8d60, &regs, nullptr, 0);  // S_StopAllSounds
        title_function<void XB_STDCALL(uint32_t)>(0x002c8c20)(title_global<uint32_t>(0x0095f4c4));  // S_StopAllChannels
    }
    const char* const info = configstring(0x009606c4);
    TitleRegs regs{};
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
    regs.edi = 0x00838d2c;
    cg_call_regs(0x002b8f50, &regs, nullptr, 0);  // Info_ValueForKey
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
    regs.edi = 0x00838d28;
    const uint32_t value = cg_call_regs(0x002b8f50, &regs, nullptr, 0);  // Info_ValueForKey
    title_function<int32_t XB_CDECL(uint32_t)>(0x001eeb81)(value);  // _atol (result unused)
    const uint32_t name = title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(0x00948388) * 0x2c);
    title_function<uint8_t* XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0029c030)(name, 0x0082e098, 1);  // Cvar_Set2(.., "0")
    cg_cvar_update(0x00948388);
    title_global<int32_t>(0x00972af4) = -1;
    title_global<int32_t>(0x00972aec) = -1;
    title_global<uint32_t>(0x00972c40) = 1;
}

// The title callees run at the original's ESP with its registers (a 2-byte dead-stack word
// differed from a C frame): the entry moves the C frame below a gap and hands over the entry
// ESP, EAX (snap), EBX, ESI and EDI.
PLATFORM_ASM_ENTRY(CG_SetInitialSnapshot_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_SetInitialSnapshot) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_SetInitialSnapshot(uint32_t entry_esp, uint8_t* snap, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // Q3's CG_SetInitialSnapshot (snap in EAX): cg.snap, the time from it, the entities' +0x418
    // cleared (list 0x0097d370, 0x0097d36c of them). On the first one of a level (0x0094a974) the
    // fade (0x009730b8..) reset and the loading screen closed: the chapter's front-end page
    // (g_currentChapter <= 0, <= 5, <= 8, else; the three later ones started with 2.0) told to
    // continue, and when the front end is in state 6 its loading panel hidden, the cvars and
    // command 0x0082bc74 run, the free sound channels stopped and the rest resumed, the key
    // catcher 2 cleared and the keys released (tail jump to Key_ClearStates).
    (void)ebx;
    auto field_ptr = [](uint8_t* object, uint32_t offset) { return *reinterpret_cast<uint8_t**>(object + offset); };
    auto vslot = [](uint8_t* object, uint32_t offset) {
        return *reinterpret_cast<uint32_t*>(*reinterpret_cast<uint8_t**>(object) + offset);
    };
    auto addr = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    TitleRegs regs{};
    regs.ebx = 1;
    regs.esi = esi;
    regs.edi = edi;
    const int32_t count = title_global<int32_t>(0x0097d36c);
    title_global<uint8_t*>(0x00971aa8) = snap;
    title_global<uint32_t>(0x00971aa4) = 1;
    title_global<uint32_t>(0x00971a84) = *reinterpret_cast<const uint32_t*>(snap + 0x8c);
    const int32_t time = *reinterpret_cast<const int32_t*>(snap + 4);
    title_global<int32_t>(0x0097253c) = time;
    title_global<int32_t>(0x00972540) = time;
    for (uint32_t p = 0x0097d370; p != 0x0097d370 + static_cast<uint32_t>(count) * 4; p += 4)
        if (uint8_t* const ent = title_global<uint8_t*>(p)) *reinterpret_cast<uint32_t*>(ent + 0x418) = 0;
    if (!title_global<uint32_t>(0x0094a974)) return;
    uint32_t top = entry_esp - 8;  // after push ebx, push esi
    title_global<float>(0x009730b8) = g(0x0083eb20);
    title_global<int32_t>(0x009730c0) = title_global<int32_t>(0x0097253c);
    title_global<uint32_t>(0x009730c4) = 0;
    title_global<float>(0x009730bc) = g(0x0083eb20);
    const uint32_t set1[3] = {0x0082ca00, 0x0082e058, 1};
    title_call_at_regs(top, 0x0029c030, &regs, 3, set1);  // Cvar_Set2
    const int32_t chapter = title_global<int32_t>(0x008dce48);
    title_global<uint32_t>(0x0092bdf4) = 2;
    title_global<uint8_t>(0x008d50dd) = 1;
    uint8_t* const pages = field_ptr(title_global<uint8_t*>(0x0095f8fc), 4);
    uint8_t* page;
    uint32_t text_slot;
    const uint32_t zero[1] = {0};
    if (chapter <= 0) {
        page = field_ptr(pages, 0x14);
        regs.esi = addr(page);
        page[0x38] = 1;
        text_slot = 0x58;
    } else {
        uint32_t start;
        if (chapter <= 5) {
            page = field_ptr(pages, 0xc);
            start = 0x003134e0;
            text_slot = 0x124;
        } else if (chapter <= 8) {
            page = field_ptr(pages, 0x10);
            start = 0x00313240;
            text_slot = 0xa8;
        } else {
            page = field_ptr(pages, 8);
            start = 0x00313790;
            text_slot = 0x10c;
        }
        regs.esi = addr(page);
        regs.eax = addr(page);
        page[0x38] = 1;
        const uint32_t two[1] = {0x40000000u};  // 2.0
        title_call_at_regs(top, start, &regs, 1, two);  // stdcall (page in EAX, 2.0)
    }
    if (title_global<uint8_t>(0x0093fcd0)) {
        regs.ecx = addr(page);
        title_call_at_regs(top, vslot(page, 0x60), &regs, 1, zero);
    }
    uint8_t* const text = field_ptr(page, text_slot);
    regs.ecx = addr(text);
    const uint32_t message[1] = {0x0082bcd4};
    title_call_at_regs(top, vslot(text, 0x6c), &regs, 1, message);
    uint8_t* const fe = title_global<uint8_t*>(0x0095f8fc);
    if (*reinterpret_cast<const int32_t*>(fe + 0xc) != 6) return;
    uint8_t* const panel = field_ptr(field_ptr(fe, 4), 0x18);
    uint8_t* const element = *reinterpret_cast<uint8_t**>(field_ptr(panel, 4));
    regs.esi = addr(panel);
    panel[0x5c] = 1;
    top -= 4;  // push edi
    regs.ecx = addr(element);
    const uint32_t empty[1] = {0x0082dfeb};  // ""
    title_call_at_regs(top, vslot(element, 0x34), &regs, 1, empty);
    title_global<uint32_t>(0x008d9d48) = 1;
    panel[0x54] = 0;
    const uint32_t set2[3] = {0x0082ca00, 0x0082e098, 1};  // "0"
    title_call_at_regs(top, 0x0029c030, &regs, 3, set2);  // Cvar_Set2
    regs.eax = 0x0082bc74;
    title_call_at_regs(top - 0xc, 0x002aabb0, &regs, 0, nullptr);  // Cbuf_AddText
    const uint32_t set3[3] = {0x0082bc68, 0x0082e098, 1};
    title_call_at_regs(top - 0xc, 0x0029c030, &regs, 3, set3);  // Cvar_Set2
    title_global<uint8_t>(0x0095f919) = 0;
    uint32_t channel = title_global<uint32_t>(0x0095f4c4);
    for (uint32_t left = 0x200; left; --left, channel += 0x30) {
        regs.esi = left;
        regs.edi = channel;
        if (title_global<uint8_t>(channel + 8)) continue;
        title_call_at_regs(top, 0x002c3400, &regs, 0, nullptr);  // S_StopChannel
    }
    regs.eax = title_global<uint32_t>(0x0095f4c4);
    regs.esi = 0;
    regs.edi = channel;
    title_call_at_regs(top, 0x002b6ba0, &regs, 0, nullptr);  // S_ResumeAllChannels
    title_global<uint32_t>(0x0092bdf4) &= ~2u;
    regs.ebx = ebx;
    regs.esi = esi;
    regs.edi = edi;
    regs.eax = title_global<uint32_t>(0x0092bdf4);
    title_call_at_regs(entry_esp, 0x002d57c0, &regs, 0, nullptr);  // Key_ClearStates (tail jump)
}

// The map checksum's local is the original's `push ecx` slot, which the map import may leave
// unwritten: the entry hands the C version the entry ECX.
PLATFORM_ASM_ENTRY(CG_Init_entry,
                   "pushl %ecx\n" PLATFORM_ASM_CALL(CG_Init) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_Init(uint32_t ecx) {
    // Q3's CG_Init: cgs (0x00975818), the weapon tables cleared; the white / soft-line /
    // friendly-fire shaders, cvars and console commands; glconfig (0x0092beb8, 0xa0 bytes) and the
    // 640x480 screen scales; the game name checked ("cod-sp"), the map name, weapons, the map
    // loaded (imports 0x00928514 / 0x009284fc, checksum 0x00964cd0), the graphics and configstring
    // assets, then the state cleared - with loading-screen refreshes between.
    for (uint32_t k = 0; k < 0x1e3; ++k) title_global<uint32_t>(0x00975818 + k * 4) = 0;
    for (uint32_t k = 0; k < 0x119c; ++k) title_global<uint32_t>(0x00975fa8 + k * 4) = 0;
    for (uint32_t k = 0; k < 0x200; ++k) title_global<uint32_t>(0x00949a98 + k * 4) = 0;
    static const uint32_t kShaders[4][2] = {
        {0x00838cf4, 0x00975c3c}, {0x00838ce8, 0x00975c40}, {0x00838cdc, 0x00975c44}, {0x00838ccc, 0x00975c48}};
    for (uint32_t k = 0; k < 4; ++k)
        title_global<uint8_t*>(kShaders[k][1]) = RE_RegisterShader(0, &title_global<const char>(kShaders[k][0]));
    CG_RegisterCvars();
    CG_InitConsoleCommands();
    const uint8_t* const cs2 = title_global<const uint8_t*>(0x009606c0);
    for (uint32_t k = 0; k < 0x28; ++k) title_global<uint32_t>(0x00975818 + k * 4) = title_global<uint32_t>(0x0092beb8 + k * 4);
    title_global<float>(0x009758b8) = static_cast<float>(title_global<int32_t>(0x0097589c)) * g(0x0083ec14);
    title_global<float>(0x009758bc) = static_cast<float>(title_global<int32_t>(0x009758a0)) * g(0x0083ec10);
    const char* const game = cs2 ? reinterpret_cast<const char*>(cs2 + 6) : nullptr;
    if (std::strcmp(game, &title_global<const char>(0x0083366c)))  // "cod-sp"
        CG_Error(&title_global<const char>(0x00838ca8), &title_global<const char>(0x0083366c), game);
    CG_ParseServerinfo();
    title_function<void XB_CDECL()>(0x002cdf00)();  // BG_InitWeapons
    auto refresh = []() { title_function<void XB_CDECL()>(0x002f1e60)(); };  // SCR_UpdateScreen
    refresh();
    refresh();
    for (uint32_t k = 0; k < 0x18; ++k) title_global<uint32_t>(0x00972b90 + k * 4) = 0;
    title_function<void XB_CDECL()>(title_global<uint32_t>(0x00928514))();
    refresh();
    uint32_t checksum = ecx;
    title_function<void XB_CDECL(uint32_t, uint32_t*)>(title_global<uint32_t>(0x009284fc))(0x009758c4, &checksum);
    title_global<uint32_t>(0x00964cd0) = checksum;
    refresh();
    refresh();
    refresh();
    CG_RegisterGraphics();
    refresh();
    refresh();
    refresh();
    CG_PrecacheConfigStringAssets();
    refresh();
    refresh();
    CG_ClearState(0);
}

// Every call runs at the original's ESP (entry - 4, after its push esi) with its registers:
// the per-frame call reaches CG_DrawActiveFrame's whole subtree. The entry moves the C frame
// below a gap and hands over the entry ESP, EBX, EDI, ECX and EDX.
PLATFORM_ASM_ENTRY(CG_vmMain_entry,
                   "movl %edx, %eax\n"
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %eax\n"
                   "pushl %ecx\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_vmMain) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

int32_t XB_CDECL CG_vmMain(uint32_t entry_esp, uint32_t ebx, uint32_t edi, uint32_t ecx, uint32_t edx) {
    // Q3's vmMain for the cgame (cdecl command, arguments a1..a6): 0 init, 1 shutdown, 2 console
    // command (its result), 3 draw frame (a1 in ECX, a2 in EAX, a4..a6 on the stack), 8 whether
    // model a1 is registered (0x00975944), 9 a DObj skeleton, 11 a camera shake (origin a3,
    // scale *a1, duration a2, radius a4), 12 returns 1, 13-16 save/load of the view-model trees
    // and save state, 19 the view origin into a1, 20 a restart, 21 configstring a1 changed, 22
    // the time set; 4, 5, 7, 17, 18, 23, 24 do nothing; others are an error (-1).
    auto arg = [entry_esp](uint32_t k) { return title_global<uint32_t>(entry_esp + 4 + 4 * k); };
    const int32_t command = static_cast<int32_t>(arg(0));
    const uint32_t top = entry_esp - 4;
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.edi = edi;
    regs.ecx = ecx;
    regs.edx = edx;
    regs.esi = 0;
    regs.eax = static_cast<uint32_t>(command);
    auto call = [&](uint32_t function, uint32_t count, const uint32_t* args) {
        return title_call_at_regs(top, function, &regs, count, args);
    };
    switch (command) {
    case 0:
        call(0x0028b7c0, 0, nullptr);  // CG_Init
        return 0;
    case 1:
        call(0x0027cc30, 0, nullptr);  // CG_Shutdown
        return 0;
    case 2:
        return static_cast<int32_t>(call(0x00279dd0, 0, nullptr));  // CG_ConsoleCommand
    case 3: {
        regs.eax = arg(2);
        regs.ecx = arg(1);
        regs.edx = arg(4);
        const uint32_t args[3] = {arg(4), arg(5), arg(6)};
        call(0x0028b340, 3, args);  // CG_DrawActiveFrame
        return 0;
    }
    case 4:
    case 5:
    case 7:
    case 17:
    case 18:
    case 23:
    case 24:
        return 0;
    case 8:
        return title_global<uint32_t>(0x00975944 + arg(1) * 4) != 0;
    case 9: {
        regs.eax = arg(2);
        regs.ecx = arg(1);
        regs.edx = arg(3);
        const uint32_t args[3] = {arg(1), arg(2), arg(3)};
        call(0x0027f8e0, 3, args);  // CL_DObjCreateSkeleton
        return 0;
    }
    case 11: {
        // The scale goes in XMM0: through the C version's argument.
        const float radius = static_cast<float>(static_cast<int32_t>(arg(4)));
        float scale;
        std::memcpy(&scale, reinterpret_cast<const void*>(static_cast<uintptr_t>(arg(1))), 4);
        CG_StartShakeCamera(reinterpret_cast<const float*>(static_cast<uintptr_t>(arg(3))), scale,
                            static_cast<int32_t>(arg(2)), radius);
        return 0;
    }
    case 12:
        return 1;
    case 13:
        call(0x00276b90, 0, nullptr);  // CG_SaveViewModelAnimTrees
        return 0;
    case 14:
        call(0x00285da0, 0, nullptr);  // CG_LoadViewModelAnimTrees
        return 0;
    case 15:
        call(0x00278cb0, 0, nullptr);  // CG_CopySaveState
        return 0;
    case 16:
        call(0x00276b10, 0, nullptr);  // CG_WriteSaveState
        return 0;
    case 19: {
        uint32_t* const out = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(arg(1)));
        out[0] = title_global<uint32_t>(0x00972bb0);
        out[1] = title_global<uint32_t>(0x00972bb4);
        out[2] = title_global<uint32_t>(0x00972bb8);
        return 0;
    }
    case 20:
        regs.eax = 1;
        call(0x0027cad0, 0, nullptr);  // CG_ClearState
        return 0;
    case 21:
        regs.eax = arg(1);
        call(0x002867a0, 0, nullptr);  // CG_ConfigStringModified
        return 0;
    case 22:
        title_global<uint32_t>(0x0097253c) = arg(1);
        title_global<uint32_t>(0x00972540) = arg(1);
        return 0;
    default: {
        const uint32_t args[2] = {0x00838dbc, static_cast<uint32_t>(command)};
        call(0x00277630, 2, args);  // CG_Error
        return -1;
    }
    }
}

// The calls run at the original's ESP with its registers and the two model infos at the
// original's frame address (DObj_Create's subtree reads stack words its callees left): the entry
// moves the C frame below a gap and hands over the entry ESP. The original keeps the DObj
// handle in its first argument slot; the C version writes it there too.
PLATFORM_ASM_ENTRY(CG_CreateViewWeaponDObj_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_CreateViewWeaponDObj) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_CreateViewWeaponDObj(uint32_t entry_esp) {
    // (cdecl weapon, hands name): the view weapon's DObj (weapon record 0x00975fa8 + weapon x 0xc4,
    // +0) rebuilt from the hands model (def +0x18) and the gun model (def +0x14) hung from
    // "tag_weapon", reusing the old DObj's first tree; the hands name kept at +0x68 (0x17
    // characters). Nothing for weapon 0, without a DObj or without a gun model name.
    uint32_t& weapon_slot = title_global<uint32_t>(entry_esp + 4);
    const uint32_t weapon = weapon_slot;
    const uint32_t hands = title_global<uint32_t>(entry_esp + 8);
    const uint32_t frame = entry_esp - 0x3c;
    auto at = [frame](uint32_t offset) { return frame + offset; };
    TitleRegs regs{};
    regs.ebx = 0;
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t info = at(0x18 + i * 0x10);
        title_global<uint32_t>(info) = 0;
        regs.ecx = info + 4;
        regs.esi = info + 0xc;
        regs.edi = 2 - i;
        const uint32_t zero[1] = {0};
        title_call_at_regs(frame, 0x0011d5d0, &regs, 1, zero);  // ScriptString_FromRep
        title_global<uint32_t>(info + 0xc) = 0;
    }
    regs.edi = 0;
    if (!weapon) {
        for (uint32_t i = 2; i; --i) {
            regs.esi = at(0x18 + (i - 1) * 0x10 + 4);
            regs.ecx = regs.esi;
            regs.edi = i;
            title_call_at_regs(frame, 0x0011d640, &regs, 0, nullptr);  // ScriptString_Release
        }
        return;
    }
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[weapon];
    const uint32_t rec = 0x00975fa8 + weapon * 0xc4;
    regs.esi = rec;
    const uint32_t old = title_global<uint32_t>(rec);
    const uint32_t destroy[4] = {at(0x18), 0x10, 2, 0x0001ec00};
    if (!old || !**reinterpret_cast<const char* const*>(def + 0x14)) {
        title_call_at_regs(frame, 0x00011280, &regs, 4, destroy);  // vector_call_iterator (destructors)
        return;
    }
    title_global<uint32_t>(at(0x14)) = title_global<uint32_t>(old);
    weapon_slot = title_global<uint32_t>(rec) ? title_global<uint32_t>(title_global<uint32_t>(rec) + 0x8c) : 0;
    regs.eax = entry_esp + 4;
    const uint32_t obj = title_call_at_regs(frame - 4, 0x000244c0, &regs, 0, nullptr);  // DObjHandle_Resolve
    regs.edi = obj;
    const uint32_t no_trees[1] = {0};
    title_call_at_regs(frame, 0x00240ce0, &regs, 1, no_trees);  // DObj_Free
    const uint32_t ctx = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x34);
    regs.eax = *reinterpret_cast<const uint32_t*>(def + 0x18);
    const uint32_t model_args[3] = {title_global<uint32_t>(0x0095f6ac), at(0x10), ctx};
    title_call_at_regs(frame, 0x00241f40, &regs, 3, model_args);  // R_RegisterModel (hands)
    title_global<uint32_t>(at(0x18)) = title_global<uint32_t>(at(0x10));
    regs.ecx = at(0x1c);
    const uint32_t zero[1] = {0};
    title_call_at_regs(frame, 0x0011e0f0, &regs, 1, zero);  // ScrCall_sprintf
    title_global<uint32_t>(at(0x20)) = 0;
    regs.eax = *reinterpret_cast<const uint32_t*>(def + 0x14);
    const uint32_t model_args2[3] = {title_global<uint32_t>(0x0095f6ac), at(0x10),
                                     *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x34)};
    title_call_at_regs(frame, 0x00241f40, &regs, 3, model_args2);  // R_RegisterModel (gun)
    title_global<uint32_t>(at(0x28)) = title_global<uint32_t>(at(0x10));
    regs.ecx = at(0x2c);
    const uint32_t tag[1] = {0x00838914};  // "tag_weapon"
    title_call_at_regs(frame, 0x0011dd20, &regs, 1, tag);  // ScriptString_AssignCString
    title_global<uint32_t>(at(0x30)) = 0;
    regs.eax = entry_esp + 4;
    const uint32_t target = title_call_at_regs(frame, 0x000244c0, &regs, 0, nullptr);  // DObjHandle_Resolve
    regs.eax = title_global<uint32_t>(at(0x14));
    regs.ecx = 0;
    regs.edx = at(0x18);
    const uint32_t create_args[2] = {2, target};
    title_call_at_regs(frame, 0x00245ff0, &regs, 2, create_args);  // DObj_Create
    regs.eax = entry_esp + 4;
    title_global<uint32_t>(rec) = title_call_at_regs(frame, 0x000244c0, &regs, 0, nullptr);  // DObjHandle_Resolve
    regs.esi = rec + 0x68;
    const uint32_t copy_args[3] = {rec + 0x68, hands, 0x17};
    title_call_at_regs(frame, 0x001eed80, &regs, 3, copy_args);  // _strncpy
    title_global<uint8_t>(rec + 0x68 + 0x17) = 0;
    title_call_at_regs(frame, 0x00011280, &regs, 4, destroy);  // vector_call_iterator (destructors)
}

void XB_REGS1 XB_CDECL CG_ConfigStringModified(int32_t index) {
    // Q3's CG_ConfigStringModified (index in EAX): 8 the items, 3 / 0 / 9 / 11 their parsers,
    // 0x20..0x9f the inline models (0x009758c4 + index x 4), 0x230/0x231 the two shell shock
    // presets (loaded and read into 0x00975b44 + n x 0x7c), 0x10..0x1f objectives, 0x2d3..0x3d2
    // the precached assets; 12 is copied and scanned as four floats that nothing keeps.
    const char* text = nullptr;
    if (index >= 0 && index < 0x400) {
        const uint8_t* const string = title_global<const uint8_t*>(0x009606b8 + static_cast<uint32_t>(index) * 4);
        if (string) text = reinterpret_cast<const char*>(string + 6);
    }
    if (index == 8) {
        CG_RegisterItems();
        return;
    }
    if (index == 3) {
        CG_ParseInfoValueT();
        return;
    }
    if (index == 0) {
        CG_ParseServerinfo();
        return;
    }
    if (index == 9) {
        CG_ParseConfigStringFloat9();
        return;
    }
    if (index >= 0x20 && index < 0xa0) {
        uint32_t handle = 0;
        const uint32_t ctx = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x34);
        const uint32_t out = RE_RegisterModel(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&handle)), text, ctx);
        title_global<uint32_t>(0x009758c4 + static_cast<uint32_t>(index) * 4) = title_global<uint32_t>(out);
        return;
    }
    if (index >= 0xe0 && index < 0x130) return;
    if (index >= 0x230 && index < 0x232) {
        if (!*text) return;
        if (!CG_ShellShock_LoadParams(text)) return;
        CG_ReadShellShockCvars(&title_global<uint8_t>(0x00975b44 + static_cast<uint32_t>(index - 0x230) * 0x7c));
        return;
    }
    if (index >= 0x10 && index < 0x20) {
        CG_ParseObjective(index);
        return;
    }
    if (index >= 0x272 && index < 0x273) return;
    if (index >= 0x2d3 && index < 0x3d3) {
        CG_PrecacheConfigStringAsset(index);
        return;
    }
    if (index == 0xb) {
        CG_ParseConfigStringFloat11();
        return;
    }
    if (index != 0xc) return;
    const uint8_t* const string = title_global<const uint8_t*>(0x009606e8);
    const char* source = string ? reinterpret_cast<const char*>(string + 6) : nullptr;
    char buffer[0x90];
    char* dest = buffer;
    char c;
    do {
        c = *source++;
        *dest++ = c;
    } while (c);
    float values[4];
    title_function<int32_t XB_CDECL(const char*, const char*, ...)>(0x001f07e1)(
        buffer, &title_global<const char>(0x00838c64), &values[1], &values[2], &values[3], &values[0]);  // sscanf
}

void XB_CDECL CG_ServerCommand() {
    // Q3's CG_ServerCommand: argv 0 (into cg_argvBuffer, 0xff characters) matched by hash
    // against the server-command hashes (0x0094c508..): configstring, localized messages (one
    // printed, the objective message), voice volume scale, fade (alpha argv1 x 255, start argv2,
    // duration argv3), centre print, voice attenuation cleared, 0x00971a88 set, a console command
    // ("%s" of argv1; Cbuf_Execute when empty), music played / stopped. Others are reported as
    // unknown - also after the music stop - unless one of three ignored hashes.
    using LocalizeFn = const char* XB_CDECL(const char*, uint32_t, uint32_t);
    LocalizeFn* const localize = title_function<LocalizeFn>(0x002d03c0);  // SEH_LocalizeTextMessage
    using AtofFn = float XB_CDECL(const char*);
    using AtolFn = int32_t XB_CDECL(const char*);
    const char* const first = title_global<uint32_t>(0x00934248) ? title_global<const char*>(0x0092df90)
                                                                  : &title_global<const char>(0x0082dfeb);
    char* const buffer = &title_global<char>(0x0098b958);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buffer, first, 0xff);  // _strncpy
    buffer[0xff] = 0;
    if (!buffer[0]) return;
    const uint32_t hash = Com_HashKeyNoCase(0, buffer);
    auto is = [hash](uint32_t address) { return hash == title_global<uint32_t>(address); };
    if (is(0x0094c50c)) {
        CG_ConfigStringCommand();
        return;
    }
    if (is(0x0094c518)) {
        localize(CG_Argv(1), 0x00838ba4, 0);
        return;
    }
    if (is(0x0094c510)) {
        const char* const text = localize(CG_Argv(1), 0x00838b94, 0);
        CG_Printf(&title_global<const char>(0x008325cc), text);  // "%s\n"
        return;
    }
    if (is(0x0094c53c)) {
        const float scale = title_function<AtofFn>(0x001ef056)(CG_Argv(1));
        uint32_t bits;
        std::memcpy(&bits, &scale, 4);
        title_function<void XB_CDECL(uint32_t)>(0x000c5390)(bits);  // Snd_SetVoiceVolumeScale
        return;
    }
    if (is(0x0094c540)) {
        const int32_t duration = title_function<AtolFn>(0x001eeb81)(CG_Argv(3));
        const int32_t start = title_function<AtolFn>(0x001eeb81)(CG_Argv(2));
        const float alpha = title_function<AtofFn>(0x001ef056)(CG_Argv(1));
        const int32_t alpha255 = static_cast<int32_t>(
            static_cast<int64_t>(static_cast<long double>(alpha) * static_cast<long double>(g(0x0083eb8c))));
        CG_SetFade(duration, 0, start, alpha255);
        return;
    }
    if (is(0x0094c508)) {
        CG_CenterPrint(0, CG_Argv(1), 360.0f, 8);
        return;
    }
    if (is(0x0094c514)) {
        localize(CG_Argv(1), 0x00838b84, 0);
        return;
    }
    if (is(0x0094c51c) || is(0x0094c520)) {
        CG_SetObjectiveMessage(localize(CG_Argv(1), 0x00838b84, 0));
        return;
    }
    if (is(0x0094c524) || is(0x0094c528)) {
        S_ClearVoiceAttenuation();
        return;
    }
    if (is(0x0094c52c)) {
        title_global<uint32_t>(0x00971a88) = 1;
        return;
    }
    if (is(0x0094c530)) {
        const char* const command =
            title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(&title_global<const char>(0x00838b68), CG_Argv(1));  // va
        if (command && std::strlen(command)) {
            TitleRegs regs{};
            regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command));
            cg_call_regs(0x002c6140, &regs, nullptr, 0);  // Cmd_ExecuteString
            return;
        }
        title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute
        return;
    }
    if (is(0x0094c534)) {
        TitleRegs regs{};
        regs.eax = title_global<uint32_t>(0x0095f4cc);
        regs.esi = regs.eax;
        regs.edi = hash;
        const uint32_t args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(CG_Argv(1)))};
        cg_call_regs(0x002ce520, &regs, args, 1);  // Music_Play
        return;
    }
    if (is(0x0094c538)) {
        TitleRegs regs{};
        regs.esi = title_global<uint32_t>(0x0095f4cc);
        regs.edi = hash;
        cg_call_regs(0x002c8bc0, &regs, nullptr, 0);  // S_StopSoundHandle
    }
    if (is(0x0094c548) || is(0x0094c54c) || is(0x0094c550)) return;
    CG_Printf(&title_global<const char>(0x00838b44), buffer);  // "Unknown client game command: %s\n"
}

// Every call runs at the original's ESP with its registers and the listener vectors at the
// original's frame addresses (the whole frame's subtree is under it; cl.cpp calls it at the
// original's ESP for the same reason): the entry moves the C frame below a gap and hands over
// the entry ESP and EAX, ECX, ESI, EDI.
PLATFORM_ASM_ENTRY(CG_DrawActiveFrame_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_DrawActiveFrame) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_DrawActiveFrame(uint32_t entry_esp, int32_t stereo, int32_t server_time, uint32_t esi, uint32_t edi) {
    // Q3's CG_DrawActiveFrame (stereo view in EAX, time in ECX; cdecl cube-map face, size,
    // demo flag): the frame time (not for view 2; never negative), cvars, the map's frame import,
    // snapshots; unless the level is loading (0x0094a974): the frame count, prediction, the shell
    // shock of the snapshot (+0x504 preset, +0x508 start, +0x50c duration; else the scripted one
    // 0x00973a70/74), third person, the view values, camera shake and tilt (not for a cube map),
    // the sky portal, the view weapon (0x008da6d4), the client's view state for the renderer
    // (0x00961768..: weapon, zoom sensitivity x shell-shock scale, gun angles), the drawing, and
    // the sound listener at the view origin facing forward with up = -axis[2].
    auto arg = [entry_esp](uint32_t k) { return title_global<uint32_t>(entry_esp + 4 + 4 * k); };
    const uint32_t top = (((entry_esp - 4) & ~15u) - 0x58) - 8;
    auto at = [top](uint32_t offset) -> float& { return title_global<float>(top + offset); };
    auto word = [top](uint32_t offset) -> uint32_t& { return title_global<uint32_t>(top + offset); };
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(stereo);
    regs.ecx = static_cast<uint32_t>(server_time);
    regs.esi = esi;
    regs.edi = edi;
    auto call = [&](uint32_t function) { return title_call_at_regs(top, function, &regs, 0, nullptr); };
    if (stereo != 2) title_global<int32_t>(0x00972540) = title_global<int32_t>(0x0097253c);
    title_global<uint32_t>(0x00971a8c) = arg(0);
    title_global<int32_t>(0x0097253c) = server_time;
    title_global<uint32_t>(0x00971a90) = arg(1);
    if (stereo != 2) {
        const int32_t frametime = server_time - title_global<int32_t>(0x00972540);
        title_global<int32_t>(0x00972534) = frametime;
        if (frametime < 0) {
            title_global<int32_t>(0x00972534) = 0;
            title_global<int32_t>(0x00972540) = server_time;
        }
    }
    title_global<uint32_t>(0x00972538) = arg(2);
    regs.ecx = arg(2);
    regs.edx = arg(1);
    call(0x00279500);  // CG_UpdateCvars
    call(title_global<uint32_t>(0x00928514));
    call(0x0028b220);  // CG_ProcessSnapshots
    if (title_global<uint32_t>(0x0094a974)) return;
    ++title_global<int32_t>(0x00971a80);
    call(0x0028acc0);  // CG_PredictPlayerState
    const uint8_t* const snap = title_global<const uint8_t*>(0x00971aa8);
    const int32_t preset = *reinterpret_cast<const int32_t*>(snap + 0x504);
    uint32_t params, start, duration;
    if (!preset) {
        params = 0x00975b44;
        start = title_global<uint32_t>(0x00973a70);
        duration = title_global<uint32_t>(0x00973a74);
    } else {
        params = 0x00975b44 + static_cast<uint32_t>(preset) * 0x7c;
        start = *reinterpret_cast<const uint32_t*>(snap + 0x508);
        duration = *reinterpret_cast<const uint32_t*>(snap + 0x50c);
    }
    title_global<uint32_t>(0x00973a48) = params;
    title_global<uint32_t>(0x00973a4c) = start;
    title_global<uint32_t>(0x00973a50) = duration;
    regs.eax = start;
    regs.edx = start;
    regs.ecx = duration;
    regs.esi = duration;
    const uint32_t shock_args[1] = {params};
    title_call_at_regs(top, 0x0027e540, &regs, 1, shock_args);  // CG_UpdateShellShock
    title_global<uint32_t>(0x00972548) = title_global<uint32_t>(0x00948394);
    call(0x002880a0);  // CG_CalcViewValues
    if (!title_global<uint32_t>(0x00971a8c)) {
        call(0x0027fc40);  // CG_ShakeCamera
        regs.esi = 0x00972bd8;
        regs.edi = top + 0x14;
        const uint32_t axis_args[2] = {0x00972bf0, 0x00972bc0};
        title_call_at_regs(top, 0x002a2af0, &regs, 2, axis_args);  // AnglesToAxis
        const float r1 = 0.0f - at(0x18);
        const float r2 = 0.0f - at(0x1c);
        title_global<float>(0x00972bcc) = 0.0f - at(0x14);
        title_global<float>(0x00972bd0) = r1;
        title_global<float>(0x00972bd4) = r2;
        call(0x00278fa0);  // CG_TiltViewAxis
    }
    call(0x0027afd0);  // CG_DrawSkyBoxPortal
    if (title_global<uint32_t>(0x008da6d4)) {
        const uint32_t ps[1] = {0x00972550};
        title_call_at_regs(top, 0x00289540, &regs, 1, ps);  // CG_AddViewWeapon
    }
    title_global<int32_t>(0x00972be4) = title_global<int32_t>(0x0097253c);
    const float shock = g(0x00973a58);
    float zoom = g(0x00972c38);
    if (!(shock == g(0x0083eafc))) zoom = shock * zoom;
    title_global<float>(0x00961774) = zoom;
    for (uint32_t k = 0; k < 5; ++k) title_global<uint32_t>(0x00961784 + k * 4) = title_global<uint32_t>(0x00972fa0 + k * 4);
    title_global<uint32_t>(0x00961768) = title_global<uint32_t>(0x00947fa4);
    title_global<uint32_t>(0x0096176c) = 0;
    call(0x00286bc0);  // CG_DrawActive
    // Three 4-lane temporaries (w = x) through +0x30: origin to +0x20, forward to +0x40, up to +0x50.
    auto lanes = [&](uint32_t x, uint32_t y, uint32_t z, uint32_t dst) {
        word(0x3c) = x;
        word(0x30) = x;
        word(0x38) = z;
        word(0x34) = y;
        for (uint32_t k = 0; k < 16; k += 4) word(dst + k) = word(0x30 + k);
    };
    lanes(title_global<uint32_t>(0x00972bb0), title_global<uint32_t>(0x00972bb4), title_global<uint32_t>(0x00972bb8), 0x20);
    lanes(title_global<uint32_t>(0x00972bc0), title_global<uint32_t>(0x00972bc4), title_global<uint32_t>(0x00972bc8), 0x40);
    const float u2 = 0.0f - g(0x00972be0);
    const float u0 = 0.0f - g(0x00972bd8);
    const float u1 = 0.0f - g(0x00972bdc);
    uint32_t b0, b1, b2;
    std::memcpy(&b0, &u0, 4);
    std::memcpy(&b1, &u1, 4);
    std::memcpy(&b2, &u2, 4);
    lanes(b0, b1, b2, 0x50);
    for (uint32_t k = 0; k < 12; k += 4) word(0x14 + k) = word(0x20 + k);
    for (uint32_t k = 0; k < 12; k += 4) word(0x30 + k) = word(0x40 + k);
    for (uint32_t k = 0; k < 12; k += 4) word(0x20 + k) = word(0x50 + k);
    regs.edx = top + 0x14;
    const uint32_t origin[1] = {top + 0x14};
    title_call_at_regs(top, 0x000c65c0, &regs, 1, origin);  // Snd_SetListenerOrigin
    regs.eax = top + 0x20;
    regs.ecx = top + 0x30;
    const uint32_t orientation[2] = {top + 0x30, top + 0x20};
    title_call_at_regs(top - 4, 0x000c6640, &regs, 2, orientation);  // Snd_SetListenerOrientation
}

namespace {

// The title CRT's __CIsqrt of an x87 value, stored as float.
float ci_sqrt_ext(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

// Every call runs at the original's ESP with its registers and the locals (latest command,
// old player state parts, mover deltas) at the original's frame addresses: the movement code
// under it is depth-sensitive like CG_DrawActiveFrame's. The entry moves the C frame below a gap
// and hands over the entry ESP and EBX/ESI/EDI.
PLATFORM_ASM_ENTRY(CG_PredictPlayerState_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_PredictPlayerState) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_PredictPlayerState(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // Q3's CG_PredictPlayerState: the predicted player state (0x00972550) from the snapshot the
    // first time; with cg_nopredict (0x0094b66c) interpolated. Else pmove (0x009746d0..) set up
    // (traces CG_Trace / CG_PointContents, mask by view state), the next snapshot's state taken
    // (0x00972544 its time; interpolated in view states 1 and 5), pmove_msec kept in 8..33, and
    // the last 64 commands newer than the state replayed (0x002e5530; pmove_fixed rounds the
    // time, a loading level keeps only some buttons): on the first one the mover adjustment and
    // the prediction error (0x00972ad8, decayed over cg_errorDecay) when it moved over 0.1. After
    // any move the mover adjustment again, damage feedback, ammo and the player-state events.
    const uint32_t top = (((entry_esp - 4) & ~15u) - 0xb4) - 0xc;
    auto at = [top](uint32_t offset) -> float& { return title_global<float>(top + offset); };
    auto word = [top](uint32_t offset) -> uint32_t& { return title_global<uint32_t>(top + offset); };
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.esi = esi;
    regs.edi = edi;
    auto call = [&](uint32_t function, uint32_t count, const uint32_t* args) {
        return title_call_at_regs(top, function, &regs, count, args);
    };
    auto weapon_def = []() {
        title_global<uint32_t>(0x00972acc) =
            title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + title_global<uint32_t>(0x009725d0) * 4);
    };
    if (!title_global<uint32_t>(0x00972ad0)) {
        const uint32_t from = title_global<uint32_t>(0x00971aa8) + 0x10;
        title_global<uint32_t>(0x00972ad0) = 1;
        for (uint32_t k = 0; k < 0x148; ++k) title_global<uint32_t>(0x00972550 + k * 4) = title_global<uint32_t>(from + k * 4);
        regs.esi = from + 0x520;
        regs.edi = 0x00972a70;
        weapon_def();
    }
    if (title_global<uint32_t>(0x0094b66c)) {
        const uint32_t one[1] = {1};
        call(0x00287d60, 1, one);  // CG_InterpolatePlayerState
        return;
    }
    const int32_t state = title_global<int32_t>(0x00972574);
    title_global<uint32_t>(0x00974800) = 0x00286c60;  // CG_Trace
    title_global<uint32_t>(0x00974804) = 0x00286c60;
    title_global<uint32_t>(0x00974808) = 0x00286c60;
    const int32_t current = title_global<int32_t>(0x009622ac);
    title_global<uint32_t>(0x009746d0) = 0x00972550;
    title_global<uint32_t>(0x0097480c) = 0x00281fa0;  // CG_PointContents
    word(0x1c) = static_cast<uint32_t>(current);
    title_global<uint32_t>(0x0097472c) = ((static_cast<uint32_t>(state < 4) - 1u) & 0xfdff4000u) + 0x0381c011u;
    regs.ebx = static_cast<uint32_t>(current);
    regs.eax = static_cast<uint32_t>(current);
    regs.edx = top + 0x94;
    if (!call(0x002ac630, 0, nullptr)) return;  // CL_GetUserCmd (the latest command)
    word(0x30) = title_global<uint32_t>(0x00972570);
    const uint32_t old_flags = title_global<uint32_t>(0x00972610);
    for (uint32_t k = 0; k < 0xf; ++k) word(0x58 + k * 4) = title_global<uint32_t>(0x00972614 + k * 4);
    const uint32_t next = title_global<uint32_t>(0x00971aac) + 0x10;
    word(0x20) = title_global<uint32_t>(0x00972550);
    word(0x24) = title_global<uint32_t>(0x00972554);
    const uint32_t old_z = title_global<uint32_t>(0x00972558);
    for (uint32_t k = 0; k < 0x148; ++k) title_global<uint32_t>(0x00972550 + k * 4) = title_global<uint32_t>(next + k * 4);
    regs.esi = next + 0x520;
    regs.edi = 0x00972a70;
    title_global<uint32_t>(0x00972544) = title_global<uint32_t>(title_global<uint32_t>(0x00971aac) + 4);
    word(0x28) = old_z;
    word(0x2c) = old_flags;
    const int32_t view_state = title_global<int32_t>(0x00972574);
    if (view_state == 1 || view_state == 5) {
        const uint32_t zero[1] = {0};
        call(0x00287d60, 1, zero);  // CG_InterpolatePlayerState
    }
    weapon_def();
    const int32_t msec = title_global<int32_t>(0x00948ae4);
    if (msec < 8) {
        const uint32_t args[3] = {title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(0x00948ad8) * 0x2c), 0x00839454, 1};
        call(0x0029c030, 3, args);  // Cvar_Set2(pmove_msec, "8")
        regs.ebx = 0x00948ad8;
        call(0x00296ec0, 0, nullptr);  // Cvar_Update
        regs.ebx = static_cast<uint32_t>(current);
    } else if (msec > 0x21) {
        regs.eax = 0x00838ca4;  // "33"
        const uint32_t vm[1] = {0x00948ad8};
        call(0x0029fe40, 1, vm);  // Cvar_SetByVmIndex
    }
    title_global<uint32_t>(0x009747f8) = title_global<uint32_t>(0x0094be4c);
    title_global<uint32_t>(0x009747fc) = title_global<uint32_t>(0x00948ae4);
    word(0xc) = 0;
    int32_t number = current - 63;
    if (number > static_cast<int32_t>(word(0x1c))) return;
    do {
        regs.ebx = static_cast<uint32_t>(number);
        if (number > title_global<int32_t>(0x009622ac)) {
            const uint32_t args[4] = {1, 0x00835374, static_cast<uint32_t>(number), title_global<uint32_t>(0x009622ac)};
            call(0x002938f0, 4, args);  // __break
        }
        if (number > title_global<int32_t>(0x009622ac) - 64 && number > 0) {
            const uint32_t from = 0x009617ac + (static_cast<uint32_t>(number) & 0x3f) * 0x2c;
            for (uint32_t k = 0; k < 0xb; ++k) title_global<uint32_t>(0x009746d4 + k * 4) = title_global<uint32_t>(from + k * 4);
            regs.esi = from + 0x2c;
            regs.edi = 0x00974700;
            const uint32_t pm_args[3] = {0x009746d4, 0x00974700, 0x00286c60};
            if (title_global<uint32_t>(0x009747f8)) {
                regs.eax = title_global<uint32_t>(0x009746d0);
                call(0x002e5530, 3, pm_args);  // PM_UpdateViewAngles
            }
            const int32_t time = title_global<int32_t>(0x009746d4);
            if (time > title_global<int32_t>(0x00972570) && time <= static_cast<int32_t>(word(0x94))) {
                regs.eax = static_cast<uint32_t>(number - 1);
                regs.edx = 0x00974700;
                if (call(0x002ac630, 0, nullptr)) {  // CL_GetUserCmd (the command before)
                    if (title_global<uint32_t>(0x00972570) == word(0x30)) {
                        regs.esi = top + 0x40;
                        const uint32_t mover[5] = {0x00972550, title_global<uint32_t>(0x009725b0), title_global<uint32_t>(0x00972544),
                                                   title_global<uint32_t>(0x00972540), top + 0x34};
                        call(0x0027fac0, 5, mover);  // CG_AdjustPositionForMover
                        using ext = long double;
                        at(0x10) = static_cast<float>(static_cast<ext>(at(0x20)) - static_cast<ext>(at(0x40)));
                        const int32_t yaw = static_cast<int32_t>(at(0x38) * g(0x0083ebd8)) & 0xffff;
                        title_global<int32_t>(0x009725a8) += yaw;
                        at(0x14) = static_cast<float>(static_cast<ext>(at(0x24)) - static_cast<ext>(at(0x44)));
                        at(0x18) = static_cast<float>(static_cast<ext>(at(0x28)) - static_cast<ext>(at(0x48)));
                        const float length = ci_sqrt_ext(
                            (static_cast<ext>(at(0x18)) * static_cast<ext>(at(0x18)) + static_cast<ext>(at(0x14)) * static_cast<ext>(at(0x14))) +
                            static_cast<ext>(at(0x10)) * static_cast<ext>(at(0x10)));
                        at(0xc) = length;
                        if (platform_x87_greater(length, g(0x0083ebf8))) {
                            float e0, e1, e2;
                            if (title_global<uint32_t>(0x0094b9cc)) {
                                const float decay = g(0x0094b9c8);
                                float f = (decay - static_cast<float>(title_global<int32_t>(0x0097253c) - title_global<int32_t>(0x00972ad4))) / decay;
                                if (0.0f > f) f = 0.0f;
                                e0 = g(0x00972ad8) * f;
                                e1 = g(0x00972adc) * f;
                                e2 = g(0x00972ae0) * f;
                            } else {
                                e0 = e1 = e2 = 0.0f;
                            }
                            title_global<float>(0x00972ad8) = e0 + at(0x10);
                            title_global<float>(0x00972adc) = e1 + at(0x14);
                            title_global<float>(0x00972ae0) = e2 + at(0x18);
                            title_global<int32_t>(0x00972ad4) = title_global<int32_t>(0x00972540);
                        }
                    }
                    int32_t command_time = title_global<int32_t>(0x009746d4);
                    if (title_global<uint32_t>(0x009747f8)) {
                        const int32_t step = title_global<int32_t>(0x00948ae4);
                        command_time = (command_time + step - 1) / step * step;
                        title_global<int32_t>(0x009746d4) = command_time;
                    }
                    if (title_global<uint32_t>(0x0094a974)) {
                        title_global<uint8_t>(0x009746d9) &= 0xc0;
                        title_global<uint32_t>(0x009746dc) = title_global<uint32_t>(0x00974708);
                        title_global<uint32_t>(0x009746e4) = title_global<uint32_t>(0x00974710);
                        const int32_t ps_time = title_global<int32_t>(0x00972570);
                        title_global<uint8_t>(0x009746d8) = 0;
                        title_global<uint8_t>(0x009746e8) = 0;
                        title_global<uint8_t>(0x009746e9) = 0;
                        title_global<uint8_t>(0x009746ea) = 0;
                        title_global<uint32_t>(0x009746e0) = title_global<uint32_t>(0x0097470c);
                        if (command_time - ps_time > 1) title_global<int32_t>(0x009746d4) = ps_time + 1;
                    }
                    regs.eax = title_global<uint32_t>(0x009746d0);
                    call(0x002e5530, 3, pm_args);  // PM_UpdateViewAngles (the move)
                    word(0xc) = 1;
                }
            }
        }
        ++number;
    } while (number <= static_cast<int32_t>(word(0x1c)));
    regs.ebx = static_cast<uint32_t>(number);
    if (!word(0xc)) return;
    regs.esi = 0x00972550;
    const uint32_t mover[5] = {0x00972550, title_global<uint32_t>(0x009725b0), title_global<uint32_t>(0x00972544),
                               title_global<uint32_t>(0x0097253c), top + 0x34};
    call(0x0027fac0, 5, mover);  // CG_AdjustPositionForMover
    const uint32_t flags = title_global<uint32_t>(0x00972610);
    regs.esi = flags;
    if (title_global<uint32_t>(0x0097263c) != word(0x80) && title_global<uint32_t>(0x00972648)) {
        regs.eax = title_global<uint32_t>(0x00972640);
        regs.ecx = title_global<uint32_t>(0x00972644);
        const uint32_t damage[1] = {title_global<uint32_t>(0x00972648)};
        call(0x0027eb80, 1, damage);  // CG_DamageFeedback
    }
    call(0x002793f0, 0, nullptr);  // CG_CheckAmmo
    regs.eax = flags;
    regs.edx = word(0x2c);
    regs.ecx = top + 0x58;
    regs.edi = 0x00972614;
    call(0x0028a440, 0, nullptr);  // CG_CheckPlayerstateEvents
    weapon_def();
}

void XB_CDECL AtExit_ReleaseScriptString_975810() {
    title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(0x00975810);  // ScriptString_Release
}

int32_t XB_CDECL HudElem_Alloc() {
    // The first free script HUD element (16 x 0x7c at 0x0094cc88, +0 in use) reset to its
    // defaults (scale +0xc = 1, +0x1c = -1, the rest 0); -1 when all are used.
    int32_t index = 0;
    for (uint32_t offset = 0; offset < 0x7c0; offset += 0x7c, ++index) {
        if (title_global<uint32_t>(0x0094cc88 + offset)) continue;
        uint8_t* elem = &title_global<uint8_t>(0x0094cc88 + static_cast<uint32_t>(index) * 0x7c);
        auto word = [elem](uint32_t at) -> uint32_t& { return *reinterpret_cast<uint32_t*>(elem + at); };
        *reinterpret_cast<float*>(elem + 0xc) = g(0x0083eb20);
        word(0) = 1;
        word(4) = 0;
        word(8) = 0;
        word(0x10) = 0;
        word(0x14) = 0;
        word(0x18) = 0;
        word(0x1c) = 0xffffffffu;
        for (uint32_t at = 0x20; at <= 0x2c; at += 4) word(at) = 0;
        *reinterpret_cast<float*>(elem + 0x6c) = 0.0f;
        for (uint32_t at = 0x30; at <= 0x48; at += 4) word(at) = 0;
        word(0x5c) = 0;
        word(0x60) = 0;
        *reinterpret_cast<float*>(elem + 0x64) = 0.0f;
        word(0x68) = 0;
        return index;
    }
    return -1;
}

void XB_CDECL CG_InitServerCommandHashes() {
    // The case-blind hashes of the 21 server commands CG_ServerCommand dispatches on (0x0094c500):
    // startCam, stopCam, cp, cs, print, gm, gmb, object_update, object_complete, opendeadscreen,
    // openvictoryscreen, clientLevelShot, saveshot, mu_play, mu_stop, snd_fade, scr_fade, fog, ls,
    // popupopen, popupclose.
    static const uint32_t kNames[21] = {0x00838c58, 0x00838c50, 0x00838c4c, 0x008352e8, 0x00838c44, 0x008325c0,
                                        0x008325bc, 0x00838c34, 0x00838c24, 0x00838c14, 0x00838c00, 0x008352d8,
                                        0x00838bf4, 0x00838bec, 0x00838be4, 0x00838bd8, 0x00838bcc, 0x00838bc8,
                                        0x00838bc4, 0x00838bb8, 0x00831d88};
    for (uint32_t i = 0; i < 21; ++i) title_global<uint32_t>(0x0094c500 + i * 4) = 0;
    for (uint32_t i = 0; i < 21; ++i)
        title_global<uint32_t>(0x0094c500 + i * 4) =
            Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{kNames[i]}));
}

PLATFORM_ASM_ENTRY(CG_SetObjectiveMessage_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_SetObjectiveMessage) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_SetObjectiveMessage(const char* text) {
    // The objective message (ScriptString 0x00975810) shown for 3 s (0x0095f65c); the same text while
    // it shows does not restart it.
    float& timer = title_global<float>(0x0095f65c);
    if (!(0.0f >= timer) && ScriptString_Equals(&title_global<ScriptString>(0x00975810), text)) return;
    timer = g(0x0083ec9c);  // 3
    title_function<void XB_THISCALL(ScriptString*, const char*)>(0x0011dd20)(&title_global<ScriptString>(0x00975810),
                                                                            text);  // ScriptString_AssignCString
}

}  // namespace game
