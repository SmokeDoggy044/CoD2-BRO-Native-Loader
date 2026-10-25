#include "cgame.h"

#include <cstdarg>
#include <cstring>

#include "bg_misc.h"
#include "cg_ents.h"
#include "cg_view.h"
#include "cl.h"
#include "dobj.h"
#include "fx.h"
#include "g_utils.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"
#include "tr_main.h"

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
                   "subl $0x20000, %esp\n"
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
    draw_text(345.0f, 217.0f, 0, 1.0f / 3.0f, color, name, 0, 0, 3);
    const uint8_t* text = title_global<const uint8_t*>(0x009606f4);
    const char* translated =
        CG_TranslateString(text ? reinterpret_cast<const char*>(text + 6) : nullptr, &title_global<char>(0x0083521c));  // "cgame"
    color[0] = g(0x0083eb20);
    color[1] = color[0];
    color[2] = color[0];
    color[3] = g(0x0083ed28);  // 0.7
    draw_text(345.0f, 235.0f, 0, 1.0f / 3.0f, color, translated, 0, 0, 3);
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
        title_function<DrawText>(title_global<uint32_t>(0x00928578))(x, y, 4, 1.0f / 3.0f, color, text, 16.0f, 0, 3);
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

}  // namespace game
