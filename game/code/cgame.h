#pragma once

// The client game (cgame): Q3's cg_* code - view, weapons, HUD draws, shell shock, fades.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00276900 (weapon in EDX): whether the player holds the weapon (bit set at 0x00972940).
uint32_t XB_REGS2 XB_CDECL CG_HasWeapon(uint32_t unused_eax, int32_t weapon);
// 0x002766a0: the scores display closes (0x00973a78 cleared, its time 0x00973a7c = cg.time).
void XB_CDECL CG_ScoresUp_f();
// 0x00277660: formats into a local buffer and prints nothing (release build).
void XB_CDECL CG_Printf(const char* format, ...);
// 0x002acb40 (name in EDI): the weapon slot (0..6) whose name (0x008d23a8) matches without
// case; 0 when none.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CG_WeaponSlotFromName(const char* name);
extern "C" void CG_WeaponSlotFromName_entry(void);
// 0x00277630: Com_Error (ERR_DROP) with the formatted text.
void XB_CDECL CG_Error(const char* format, ...);
// 0x0027ad40 (index in EAX): registers (import 0x009284f8, type 5) the asset named by the
// configstring, when set and not empty.
void XB_REGS1 XB_CDECL CG_PrecacheConfigStringAsset(int32_t index);
// 0x00234b50 (shader in EAX): trap_R_DrawStretchPic (import 0x00928530).
void XB_REGS1 XB_CDECL CG_R_DrawStretchPic(uint32_t shader, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t s0, uint32_t t0, uint32_t s1, uint32_t t1);
// 0x002765b0 (duration EAX, start ECX): a screen fade to alpha / 255 (0x009730b8; the end
// value 0x009730bc too once start + duration <= cg.time).
void XB_REGS3 XB_CDECL CG_SetFade(int32_t duration, uint32_t unused_edx, int32_t start, int32_t alpha);
// 0x0027b9a0: CG_PrecacheConfigStringAsset of configstrings 0x2d4..0x3d2.
void XB_CDECL CG_PrecacheConfigStringAssets();
// 0x00276b90: Com_Error when a used view model (0x0097606c, 0xc4 each) has no animation tree.
void XB_CDECL CG_SaveViewModelAnimTrees();
// 0x002794b0 (index in EAX): Q3's CG_Argv into cg_argvBuffer (0x0098b958, 0x100 bytes).
char* XB_REGS1 XB_CDECL CG_Argv(uint32_t arg);
// 0x002757d0 (shader in EAX): the rotating trap_R_DrawStretchPic (import 0x00928538).
void XB_REGS1 XB_CDECL CG_StretchPicRotate(uint32_t shader, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t s0, uint32_t t0, uint32_t s1, uint32_t t1, uint32_t angle);
// 0x00277c50: while a shell shock (start 0x00973a4c, duration 0x00973a50) lasts and the blur
// is on (0x00949354), the blur import (0x00928550); its flag 0x00973a64.
void XB_CDECL CG_DrawShellShockBlur();
// 0x00275a30: Q3's CG_InitLocalEntities (20 entities at 0x00973a90).
void XB_CDECL CG_InitLocalEntities();
// 0x00278e50: Q3's step smoothing: the view height (0x00972bb8) lowered by the rest of the
// last step (cg.stepChange 0x00972ae8) over 100 ms.
void XB_CDECL CG_StepOffset();
// 0x00276d00 (cap in XMM1, cdecl, result on the x87): sin(cycle) * min(stance scale * speed,
// cap); the stance scale is 0x0094aa90 / 0x00947e80 / 0x0094a6a0.
extern "C" PLATFORM_HIDDEN float XB_CDECL CG_BobCycleVertical(float cycle, float speed, float cap);
extern "C" void CG_BobCycleVertical_entry(void);
// 0x00276d50 (as CG_BobCycleVertical): (sin(cycle * 4 + pi/2) * 0.2 + sin(2 * cycle)) *
// amplitude * 0.75.
extern "C" PLATFORM_HIDDEN float XB_CDECL CG_BobCycleLateral(float cycle, float speed, float cap);
extern "C" void CG_BobCycleLateral_entry(void);
// 0x002779e0 (shader in EAX): a 640x480 picture scaled to the screen (cgs.screenX/YScale).
void XB_REGS1 XB_CDECL CG_DrawPic(uint32_t shader, float x, float y, float w, float h);
// 0x00277bd0 (colour in EAX; not registered: an indirect CALL in its first 5 bytes): a 640x480 rectangle in the white shader (0x00975c3c), coloured
// (import 0x0092852c, reset after).
void XB_REGS1 XB_CDECL CG_FillRect(const float* color, float x, float y, float w, float h);
// 0x00275e50 (start EDX, total ECX): Q3's CG_FadeColor: white with alpha 0x0094b5d8, fading
// over the last 100 ms (0x0098ad68); null before start or after the end.
float* XB_REGS3 XB_CDECL CG_FadeColor(uint32_t unused_eax, int32_t start, int32_t total);
// 0x002765f0 (out in ECX): with a weapon overlay (+0x634 name set or +0x638), how far the
// zoom (0x009725d8) is into its last part (+0x62c zooming in 0x00972a74, else +0x630) as
// 0..1 into *out; whether that is above 0.01.
uint32_t XB_REGS3 XB_CDECL CG_GetAdsOverlayFraction(uint32_t unused_eax, uint32_t unused_edx, float* out);
// 0x002793f0: Q3's CG_CheckAmmo: cg.lowAmmoWarning (0x00972eb4) 0 with 5 or more "seconds"
// of ammo over the weapons held (first 32 bits only), 1 with some, 2 with none.
void XB_CDECL CG_CheckAmmo();
// 0x00279380: Q3's CG_BuildSolidList over the snapshot's entities (0x0097d370, count
// 0x0097d36c): models other than 0 (0xffffff with flag 2 skipped, type 2 skipped), at most
// 0x400 into cg_solidEntities (0x00974810).
void XB_CDECL CG_BuildSolidList();

// 0x00279e50: Q3's CG_Fade_f ("fade r g b a seconds", 6 words): the CG_SetFade of alpha a
// over the seconds from cg.time; the colour words are parsed and dropped.
void XB_CDECL CG_Fade_f();
// 0x00278150: the screen fade (0x009730b8 target, 0x009730bc current, start 0x009730c0,
// duration 0x009730c4) stepped towards its target by the time since the last call
// (0x0098ad54; steps of 500 ms or more skip), drawn as a black full-screen rectangle.
void XB_CDECL CG_DrawFlashFade();
// 0x0027cf40 (angle in XMM0, degrees): a 640x480 picture rotated about its centre (import
// 0x0092853c with the texture coordinates at 0x00839a3c).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawRotatedPic(float x, float y, float w, float h, uint32_t shader,
                                                            float angle);
extern "C" void CG_DrawRotatedPic_entry(void);
// 0x0027cdc0 (points in ESI, angle in XMM0): four 640x480 offsets rotated and placed at x, y
// (import 0x0092853c).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawRotatedQuadPic(float x, float y, const float* st, uint32_t shader,
                                                                const float* points, float angle);
extern "C" void CG_DrawRotatedQuadPic_entry(void);
// 0x00276060 (style flag EAX, screen_coords EDX, colour ECX, scale XMM1): text in font 5
// (import 0x00928578) at scale / 48, lowered by 0.8 * scale; 640x480 positions are divided
// by the screen scale unless screen_coords; white without a colour; style 3 when set.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawTextScaled(float x, float y, const char* text, float width,
                                                            uint32_t arg4, uint32_t style, uint32_t screen_coords,
                                                            const float* color, float scale);
extern "C" void CG_DrawTextScaled_entry(void);
// 0x0027d450: Q3's CG_DrawCenterString: cg.centerPrint (0x00972c7c) line by line (newline
// or "\n", 40 characters at most), centred, fading over cg_centertime (0x00949a10).
void XB_CDECL CG_DrawCenterString();
// 0x00278d80 (model in ESI): every "tag_b*" tag without a model gets the last "tag_b" tag's;
// returns the tag count.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CG_ShareWeaponTagBModels(uint8_t* model);
extern "C" void CG_ShareWeaponTagBModels_entry(void);

}  // namespace game
