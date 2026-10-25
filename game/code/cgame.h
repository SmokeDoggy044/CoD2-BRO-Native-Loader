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
// Returns the duration (the original leaves its EAX argument there).
int32_t XB_REGS3 XB_CDECL CG_SetFade(int32_t duration, uint32_t unused_edx, int32_t start, int32_t alpha);
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
extern "C" PLATFORM_HIDDEN long double XB_CDECL CG_BobCycleVertical(float cycle, float speed, float cap);
extern "C" void CG_BobCycleVertical_entry(void);
// 0x00276d50 (as CG_BobCycleVertical): (sin(cycle * 4 + pi/2) * 0.2 + sin(2 * cycle)) *
// amplitude * 0.75.
extern "C" PLATFORM_HIDDEN long double XB_CDECL CG_BobCycleLateral(float cycle, float speed, float cap);
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

// 0x00279fb0: prints the view origin and yaw as integers ("(%i %i %i) : %i").
void XB_CDECL CG_Viewpos_f();
// 0x002795d0: a zeroed local entity (0x9c bytes) linked in as the newest; without a free one
// the oldest active one is freed first.
uint32_t* XB_CDECL CG_AllocLocalEntity();
// 0x00279530 (entity in EDI): frees the entity once the view is nearer than its cull
// distance (+0x34; origin +0x78), else adds its refEntity (+0x3c) to the scene.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_AddNearCulledLocalEntity(uint32_t* le);
extern "C" void CG_AddNearCulledLocalEntity_entry(void);

// 0x00277700 (element in EAX, cdecl (arg, style)): the element's text (+0) drawn through the
// text call in 0x00928578, aligned in its box by style +0x18 (0 left, 1 centre, 2 right);
// nothing while 0x0095f919 is set.
void XB_REGS1 XB_CDECL CG_DrawHudElemAlignedText(const uint8_t* elem, uint32_t arg, const uint8_t* style);

// 0x0027f040 (hud element in ESI, rect in EBX): the element's material (+0x38) at its size
// (+0x30/+0x34, fading from +0x3c/+0x40 over +0x48 ms from +0x44; 0 = the rect's +0x28),
// aligned vertically in the rect by +0x18, rotated by +0x78 degrees; colour from rect +0x30.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawHudElemMaterial(const uint8_t* he, const uint8_t* rect);
extern "C" void CG_DrawHudElemMaterial_entry(void);

// 0x00278ae0: the gun's bob angles (0x00972fcc lateral, 0x00972fd0 pitch, 0x00972fd4 roll -
// the downward half only) from the bob cycle and speed, scaled per stance and capped,
// damped by 0x009725d8 against the player's +0x644.
void XB_CDECL CG_CalcWeaponBob();

// 0x00279500: Cvar_Update for every vmCvar of cgameCvarTable (0x89 entries).
void XB_CDECL CG_UpdateCvars();
// 0x0027f860 (obj in ESI): the DObj's skeleton when it has none (DObj_CreateSkeleton; a hunk
// block of the same size is allocated first and never used).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DObjEnsureSkeleton(struct DObj* obj);
extern "C" void CG_DObjEnsureSkeleton_entry(void);
// 0x0027cc80 (index in EAX): "" for 0, else the translated hud element string (config
// string 0x293 + index, "hudelem").
const char* XB_REGS1 XB_CDECL CG_GetHudElemText(int32_t index);
// 0x0027af70: +scores: starts the score display (0x00973a78, time 0x00973a7c) unless the
// letterbox is on while 0x0093fcd8 +0x20 is clear.
void XB_CDECL CG_ScoresDown_f();
// 0x00277e70: the whole screen black at alpha 0x00972f70 (when above 0, with a snapshot).
void XB_CDECL CG_DrawScreenBlackout();

// 0x002bc750: the weapon definition in use: the turret's (flags 0x6000, entity handle
// 0x009729b8) or the vehicle's (0x100000) weapon (entity +3), else the player's
// (0x009725d0); 0 when the entity is gone.
uint32_t XB_CDECL CG_GetCurrentWeaponDef();
// 0x002780a0: the red damage flash over the screen (alpha from the time left until
// 0x00972f7c, capped).
void XB_CDECL CG_DrawDamageBlend();

// 0x0027abe0: a progress bar: a grey box, then the white bar inset 2 and scaled by fraction.
void XB_CDECL CG_DrawProgressBar(float x, float y, float w, float h, float fraction);
// 0x0027c060 (forward in EAX, need ammo in EBX): selects the next usable weapon slot
// (CG_SelectWeapon); 0 when there is none.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CG_CycleWeaponSlot(uint32_t forward, uint32_t need_ammo);
extern "C" void CG_CycleWeaponSlot_entry(void);

// 0x00278cb0: the cgame part of a save copied into hunk blocks (Z_MallocDebug): per weapon
// from 1 the view model's tree flag (+0x84, set here) and +0x80, then 0x00972f00 (0x60) and
// the camera shakes 0x0097300c (0x90).
void XB_CDECL CG_CopySaveState();
// 0x00276b10: the same blocks written through VM_Call(gvm, 0x14, data, size).
void XB_CDECL CG_WriteSaveState();
// 0x00279dd0: Q3's CG_ConsoleCommand: runs the cg_commands entry (0x0082f420) named by
// the first word (Q_stricmpn); 1 when found.
uint32_t XB_CDECL CG_ConsoleCommand();
// 0x0027acb0: Q3's CG_InitConsoleCommands: Cmd_AddCommand(name, 0) for cg_commands and five
// more names.
void XB_CDECL CG_InitConsoleCommands();

// 0x00284b90: the used hud elements (0x0094cc88, 16 of 0x7c bytes) sorted and drawn.
void XB_CDECL CG_DrawHudElems();

// 0x0027bfb0 (forward in EAX, cdecl need_ammo): the next owned weapon outside the inventory
// slots (from 1 up, or from bg_lastWeaponIndex down) selected; 1 when one was.
uint32_t XB_REGS1 XB_CDECL CG_CycleOwnedWeapon(uint32_t forward, uint32_t need_ammo);
// 0x0027c220: "weapon <name or index>" selects that weapon.
void XB_CDECL CG_Weapon_f();
// 0x00286c20: "cs <index>": CG_ConfigStringModified of that configstring.
void XB_CDECL CG_ConfigStringCommand();
// 0x0027b9f0 (text in EAX, cdecl what): the localized text, or "^1UNLOCALIZED(^7text^1)^7"
// in 0x0098bd20 (the text alone while 0x00949984 is off).
const char* XB_REGS1 XB_CDECL CG_TranslateString(const char* text, const char* what);

// 0x00279be0: the held weapon's crosshair (def +0x4bc, size +0x4c4) centred in the refdef.
void XB_CDECL CG_DrawCrosshair();
// 0x00279900 (flags EBX, back colour ECX, full colour EDX, cdecl (x, y, w, h, colour,
// fraction)): a 640x480 bar filled to the fraction (flags in the definition).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawBar(float x, float y, float w, float h, const float* color, float fraction,
                                                   uint32_t flags, const float* back_color, float* full_color);
extern "C" void CG_DrawBar_entry(void);
// 0x002761b0: the front-buffer effect (shell shock flash): the last frame as a full-screen
// quad in the colour 0x008d5114 with blend mode 0x008d5124, fading out at the end.
void XB_CDECL CG_DrawFrontBufferFade();

// 0x0027bf50 / 0x0027bf80: cg_viewsize - 10 / + 10.
void XB_CDECL CG_SizeDown_f();
void XB_CDECL CG_SizeUp_f();
// 0x0027c3f0: Q3's CG_CalcVrect (cg_viewsize 30..100; height times 0.85 with 0x0094ad64).
void XB_CDECL CG_CalcVrect();
// 0x002884e0 (latest sequence in ESI): Q3's CG_ExecuteNewServerCommands.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ExecuteNewServerCommands(int32_t latest);
extern "C" void CG_ExecuteNewServerCommands_entry(void);

// 0x00279650: a loading-screen refresh at most every 100 ms.
void XB_CDECL CG_UpdateScreenWhileLoading();
// 0x00286bc0: Q3's CG_DrawActive; the C version gets the entry ESP and returns the tail
// jump target for CG_DrawActive_entry.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CG_DrawActive(uint32_t entry_esp);
extern "C" void CG_DrawActive_entry(void);
// 0x0027ada0: Q3's CG_ParseServerinfo (only cgs.mapname "maps/%s.bsp").
void XB_CDECL CG_ParseServerinfo();
// 0x0027bb10: Q3's CG_RegisterCvars.
void XB_CDECL CG_RegisterCvars();

// 0x002800a0 (priority in EAX, cdecl (text, seconds, y)): Q3's CG_CenterPrint with
// multi-byte text and priorities.
void XB_REGS1 XB_CDECL CG_CenterPrint(int32_t priority, const char* text, float seconds, int32_t y);

// 0x00285d50 (cdecl index): Q3's CG_RegisterItemVisuals (icon shader, weapon).
void XB_CDECL CG_RegisterItemVisuals(uint32_t index);
// 0x00286290: Q3's CG_RegisterItems from configstring 8.
void XB_CDECL CG_RegisterItems();
// 0x00285da0: every used weapon view model registered with its animation tree reset.
void XB_CDECL CG_LoadViewModelAnimTrees();

// 0x0027e630 (colour in EAX, cdecl alpha): the objectives list screen.
void XB_REGS1 XB_CDECL CG_DrawObjectiveList(const float* color, float alpha);

// 0x002cde20 (weapon in ESI): requests a weapon change (vmCvars 0x0094c230 time,
// 0x00947f98 weapon, 0x0094a3b8 switch flag unless it is the alternate); 1 when requested.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CG_SelectWeapon(int32_t weapon);
extern "C" void CG_SelectWeapon_entry(void);
// 0x0027dcb0 (cdecl forward, need_ammo): the next/previous weapon.
void XB_CDECL CG_CycleWeapon(uint32_t forward, uint32_t need_ammo);
// 0x0027df30: Q3's CG_OutOfAmmoChange.
void XB_CDECL CG_OutOfAmmoChange();
// 0x0027e0d0 / 0x0027e130: "weapprev" / "weapnext".
void XB_CDECL CG_PrevWeapon_f();
void XB_CDECL CG_NextWeapon_f();
// 0x0027c170: "weaponslot <name or number>".
void XB_CDECL CG_WeaponSlot_f();
// 0x0027e190: "weapalt": the current weapon's alternate.
void XB_CDECL CG_AltWeapon_f();

// 0x0027ade0 (index in EAX): an objective's configstring into its record (0x00972748).
void XB_REGS1 XB_CDECL CG_ParseObjective(int32_t index);

// 0x00275d00 (element in ECX): a timer hud element's value in ms (types 4-9).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL HudElem_GetTimerValue(const uint8_t* elem);
extern "C" void HudElem_GetTimerValue_entry(void);
// 0x0027f1f0 (element EAX, rect EBX): a clock hud element (face and needle).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawHudElemClock(const uint8_t* he, const uint8_t* rect);
extern "C" void CG_DrawHudElemClock_entry(void);

// 0x00281f30: the objectives screen while it is up; 1 when drawn.
uint32_t XB_CDECL CG_DrawObjectives();

// 0x0027c8f0 (name in EAX): scripts/<name>.shock into the shell shock cvars; 0 if missing.
uint32_t XB_REGS1 XB_CDECL CG_ShellShock_LoadParams(const char* name);
// 0x0027c990 (cdecl name): the shell shock cvars saved as scripts/<name>.shock.
uint32_t XB_CDECL CG_ShellShock_SaveParams(const char* name);
// 0x0027d6d0 / 0x0027d730 / 0x0027d780: "cg_shellshock_save", "cg_shellshock_load",
// "cg_shellshock <duration> [file]".
void XB_CDECL CG_ShellShockSave_f();
void XB_CDECL CG_ShellShockLoad_f();
void XB_CDECL CG_ShellShock_f();

// 0x0027bd90: the timed message, "Quad Speed" and "checkpoint reached" notices.
void XB_CDECL CG_DrawCheckpointReached();

// 0x0027c290: every weapon view model's DObj freed (shutdown).
void XB_CDECL CG_FreeWeaponModels();

// 0x0027af30: configstring 3's info value "t" (atol).
int32_t XB_CDECL CG_ParseInfoValueT();
// 0x0027ad70: configstring 9 (atof) to the import 0x00928520.
void XB_CDECL CG_ParseConfigStringFloat9();
// 0x0027abb0: configstring 11 (atof) into 0x009730a4.
void XB_CDECL CG_ParseConfigStringFloat11();

// 0x002764c0 and 0x00276530 (two copies of one body): for 100 ms after the flash start
// (0x00973a7c, while 0x00973a78 is set) the flash colour 0x0098ad68 is white with alpha
// fading from 0x0094b5d8.
void XB_CDECL CG_UpdateWhiteFlashColor();
void XB_CDECL CG_UpdateWhiteFlashColor2();
// 0x00277ed0 (starts with a call, cannot be hooked): the screen blackout, then the scripted
// fade - colour 0x00972e8c blending to 0x00972e9c until 0x00972e84 at rate 0x00972e88 - as a
// full-screen rectangle.
void XB_CDECL CG_ScreenFade();
// 0x00277ca0: the front-buffer blend - the last frame as a full-screen quad (offset
// 0x008fefcc/0x008fefc8, blend mode 0x008fefc4) in the colour 0x008fefd0, its alpha
// 0x0095f648 (at most 0x008fefe0) falling by 0x008ddaa0 each frame; blend modes 5/6
// (0x008d5124) scale the colour by it too.
void XB_CDECL CG_DrawFrontBufferBlend();
// 0x0027d180: a status label at (345, 217) - the name at 0x009606ec +6 (nothing for "none"),
// coloured from red through yellow to green by the value at 0x009606f0 +6 (0..1) - and under
// it at y 235 the translated text at 0x009606f4 +6 (white, alpha 0.7), small font.
void XB_CDECL CG_DrawStatusLabel();
// 0x002824f0: with player flag 0x1000000 (not 0x2000000), the icon 0x00975c48 (40x40) at the
// screen position of the view direction (CG_ProjectViewDirToScreen times the zoom 0x009725d8),
// centred in the refdef.
void XB_CDECL CG_DrawViewDirIcon();
// 0x00282620 (result in XMM0): with an ADS overlay (CG_GetAdsOverlayFraction), the weapon's scope picture
// (+0x634 set: shader 0x00976068 per client, +0x63c x +0x640 at the view direction, the refdef outside it filled
// with its edge texels; flags 0x0098ad58/0x0095f91f set) and its reticle by +0x638: 1 shader 0x00976060 (size
// +0x4c4), 2/4 a line down and two sideways (0x00975c40 / 0x00975c44; x87 for w * 0.75 and cy - 1), 3 a full
// cross, in black with the fraction as alpha. Returns 1 - fraction (1.0 without overlay, the flags cleared).
// The C version writes the result to `out`.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawAdsOverlay(float* out);
extern "C" void CG_DrawAdsOverlay_entry(void);
// 0x00285260: the crosshair: vehicles (0x00972610 & 0x6000) CG_DrawCrosshair; a mounted weapon two black
// CG_FillRect lines; else, alpha = CG_DrawAdsOverlay * 0x00949590 and enough of it, outside the excluded views
// (0x009725d4) - while zooming a shrinking shader 0x00976060 (RE_RegisterShader of 0x0094baf0 when unset), the
// centre shader (+0x4bc) and the two spread ticks (+0x4c0: shader 0x00976064 through CG_StretchPicRotate, spread
// BG_GetWeaponSpread blended to +0x658 by 0x00972a40 / 255, scaled by the fov, at least +0x4cc).
void XB_CDECL CG_DrawWeaponReticle();
// 0x00285f80: the 2D pass of a frame - fades, damage, hud elements, crosshair and weapon hud,
// checkpoint, objectives or the centre print, subtitles - in a pushed 2D scene.
void XB_CDECL CG_Draw2D();
// 0x0024ad30: clears the 16 hud elements at 0x0094cc88 (0x7c bytes each; each type first
// when set, then all of it).
void XB_CDECL HudElem_ClearAll();
// 0x002f6080 (element in EAX): a hud element's defaults - type 1, font scale 1, sort 0, alpha
// colour 0xffffffff at +0x1c, the rest 0.
void XB_REGS1 XB_CDECL HudElem_SetDefaults(uint8_t* elem);
// 0x002fb500 (element index in EAX): makes element `index` a timer of `kind` (4-7) ending or
// starting at level.time + ceil(seconds * 1000) ms; formats a "time %g should be > 0" message
// (unused) when that is not positive, except for kind 5.
void XB_REGS1 XB_CDECL HudElem_SetTimerKind(int32_t index, int32_t kind, float seconds);
// 0x00301d30 (element index in EAX): makes element `index` a clock (kind 8/9) of the shader
// `name`, size w x h, timer at level.time + ceil(seconds * 1000), turn period ceil(period * 1000).
void XB_REGS1 XB_CDECL HudElem_SetShaderTimer(int32_t index, int32_t kind, const char* name, float seconds,
                                             float period, int32_t w, int32_t h);
// 0x00275b60 (layout in EDI, buffer size in EDX, buffer on the stack): the label's first "%s"
// replaced by the text, into the buffer, which becomes the text (its width += the label's);
// the label becomes "" with width 0. Returns the length.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL HudElem_SubstituteLabel(int32_t size, uint8_t* layout, char* buffer);
extern "C" void HudElem_SubstituteLabel_entry(void);
// 0x002778e0 (element in ECX): the timer value as "h:mm:ss" or "m:ss" (va).
const char* XB_FASTCALL HudElem_FormatTimer(const uint8_t* elem);
// 0x00277850 (element in ECX): the timer value with tenths, "h:mm:ss.t" or "m:ss.t" (va).
const char* XB_FASTCALL HudElem_FormatTenthsTimer(const uint8_t* elem);
// 0x00279750 (element in EAX, layout in ESI; float in XMM0): the element's width - label +
// text for the text types, else its size +0x30 (fading from +0x3c) plus the label.
extern "C" PLATFORM_HIDDEN float XB_CDECL HudElem_GetWidth(const uint8_t* elem, const uint8_t* layout);
extern "C" void HudElem_GetWidth_entry(void);
// 0x002796a0 (element in EAX, layout in ESI; float in XMM0): the element's height - the font
// height for the text types, else its size +0x34 (fading from +0x40); at least the font height.
extern "C" PLATFORM_HIDDEN float XB_CDECL HudElem_GetHeight(const uint8_t* elem, const uint8_t* layout);
extern "C" void HudElem_GetHeight_entry(void);
// 0x0027f460 (layout in EAX, element in EBX; text buffer and its size on the stack): a hud
// element's text layout - font, label and text with their widths, size, position after the
// move fade and alignment, colour after the colour fade.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawHudElemText(uint8_t* layout, const uint8_t* elem, char* buffer,
                                                            int32_t size);
extern "C" void CG_DrawHudElemText_entry(void);
// 0x002820f0 (element in ECX): one hud element - its text layout (CG_DrawHudElemText), the
// label, then the text, material or clock; an element with sort -777 (+0x6c) while not paused
// also updates the loading-screen animation (0x002960a0).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DrawHudElem(const uint8_t* elem);
extern "C" void CG_DrawHudElem_entry(void);
// 0x002c0120 (text in EDX): the printable characters - multi-byte pairs once, colour codes
// "^0".."^9" and line breaks not counted.
int32_t XB_REGS2 XB_CDECL Loc_CountChars(uint32_t unused, const char* text);

}  // namespace game
