#pragma once

// The front end (FE_*: menu set-up and labels of the main/bonus/options menus), the UI menu base and panel lookups,
// profile defaults. Menu items and panels are title C++ objects; their methods are called through their vtables
// (thiscall): +0x30 set text by string id, +0x34 set text, +0x6c set label, +0x7c set alpha, +0x140 set wrapped
// text (width, -1.0).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0030bf90 (menu in ESI): "%i / %i" of the collector's-edition category's current item (+0x130 + index * 4 by
// the index +0x22) and its count (38, 15, 15, 3, 9, 11 for indexes 0-5, else 0) into the label +0x3c.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusCESetText(uint8_t* menu);
extern "C" void FE_BonusCESetText_entry(void);

// 0x0030c010 / 0x0030daa0 / 0x0030db00 / 0x0030db60 / 0x0030dcc0 (menu in ESI): the image of the collector's edition art
// / concepts / vehicles / weapons / bonus materials menu freed: nglResetDeviceBindings, the texture (+0x4c / +0xcc /
// +0x94 / +0xb8 / +0x90) released and cleared (the materials menu keeps the pointer), the file (+0x44 / +0xc4 / +0x8c /
// +0xb0 / +0x88) closed unless -1 and then its buffer (the next word) freed; the buffer pointer cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusCEFreeImage(uint8_t* menu);
extern "C" void FE_BonusCEFreeImage_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusConceptFreeImage(uint8_t* menu);
extern "C" void FE_BonusConceptFreeImage_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusVehicleFreeImage(uint8_t* menu);
extern "C" void FE_BonusVehicleFreeImage_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusWeaponFreeImage(uint8_t* menu);
extern "C" void FE_BonusWeaponFreeImage_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusMainFreeImage(uint8_t* menu);
extern "C" void FE_BonusMainFreeImage_entry(void);
// 0x0030c070 (menu in ESI): the six art category item texts (BONUS_ART_DIR .. BONUS_OVERHEAD).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusArtCategoryLabel(uint8_t* menu);
extern "C" void FE_BonusArtCategoryLabel_entry(void);
// 0x0030c130 (menu in ESI): the five movie item texts (BONUS_BRINGING .. BONUS_CE_ARTS), then the selection +0x22
// back to the saved one (+0x40) and +0x3c cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusMovieLabel(uint8_t* menu);
extern "C" void FE_BonusMovieLabel_entry(void);
// 0x0030cd70 / 0x0030d0b0 (play time in EAX: seconds +0x28, minutes +0x2c, hours +0x30, days +0x34): va
// "%s%i:%s%i:%s%i" of hours (days * 24 + hours, at most 999: then the record is set to 41 days 15 hours), minutes,
// seconds, each below 10 with a leading "0". Two identical copies.
const char* XB_REGS1 XB_CDECL FE_FormatPlayTime(int32_t* time);
const char* XB_REGS1 XB_CDECL FE_FormatPlayTime2(int32_t* time);
// 0x0030dfd0 (menu in ESI, ECX = the push ecx slot's value): FEMENU_COP_4X3 / _16X9 by +0x50 into the label +0x3c,
// then the first item's color (vtable +0x84 into a local) onto the label (+0x90).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_AspectOptionLabel(uint8_t* menu, uint32_t ecx);
extern "C" void FE_AspectOptionLabel_entry(void);
// 0x0030e0f0 (key in EDI): the FEMENU_CMD_* display name of the command bound to the key (__stricmp against the
// 18 input commands; key -1 counts as bound to ""), "" for an unbound key; else FEMENU_CMD for Enter (13) / Escape
// (27) or FEMENU_NA.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL FE_CommandDisplayName(int32_t key);
extern "C" void FE_CommandDisplayName_entry(void);
// 0x00312690 (menu in ESI): with the card corrupt (+0x36) FEMENU_OP_CORRUPT on all four slot items; else per slot
// FEMENU_OP_NEW (empty, +0x6c + slot * 12) or its name (+0x64 + slot * 12), then the help bar +0x40:
// FEMENU_OP_HELPBAR when +0x4c is set or the selected slot is empty, else FEMENU_LOAD_HELPBAR_XBOX.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SaveSlotsUpdateText(uint32_t entry, uint8_t* menu, uint32_t edi);
extern "C" void FE_SaveSlotsUpdateText_entry(void);
// 0x00312ad0 (menu in ESI): "%i / %i" of the concept category's current item (+0x8c + index * 4) and its count
// (the 0x48-byte level records at 0x0095f968, entry index + 1; 0 while that level is locked: 0x008dce54 clear and
// 0x008dce44 not above it) into the label +0x3c.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusConceptSetText(uint8_t* menu);
extern "C" void FE_BonusConceptSetText_entry(void);
// 0x00312bd0 (menu in EDI): the 13 bonus level items: shown (+0x20 (0)), hidden again (+0x20 (1)) while the level
// (0x0095f9b0 + i * 0x48; locked as in FE_BonusConceptSetText) has nothing, the item's selection +0x8c clamped up
// to the stored best (0x008dced8) + 1, the text BONUS_BONUS_LEVEL_<i+1> or BONUS_LOCKED when the item is locked
// (+0x24); then 0x00312b40 (the menu), alpha +0x40 = 1.0 and +0x44 cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusLevelLabel(uint8_t* menu);
extern "C" void FE_BonusLevelLabel_entry(void);

// 0x00315210 (menu in ESI, item count in EDI; owner, a2, a3, a4, flags on the stack, ret 0x14): the UI menu base
// constructor - vtable 0x0082a628, a zeroed item array (Heap_Alloc16), the arguments at +0x8/+0xc/+0x10/+0x28 and
// flags without bit 9 at +0x2a, selection -1, +0x33 = the owner's vtable +0x24 result (0 without owner). Returns
// the menu.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIMenu_Init(uint8_t* menu, int32_t count, uint8_t* owner, uint32_t a2,
                                                         uint32_t a3, uint16_t a4, uint32_t flags);
extern "C" void UIMenu_Init_entry(void);
// 0x00315b10 (text in ESI; 10 stack words, ret 0x28): the UI text item base constructor - vtable 0x008298b8, two
// empty ScriptStrings (+0x1c = the text a2, +0x54 = ""), the colours +0x20..+0x4c from 0x00922eb8, kind a1 at
// +0x18, position a3/a4 (+0x2c/+0x30), scale a7 (+0x38/+0x3c/+0x44/+0x48), a10 at +0x50; with a6 == 8 +0x8 =
// a5, else the vtable +0x24 sort key ((a6 * 1000 + a5 clamped to 0..1000) * 0.125); flags +0x5c = a8 | a9 | 4
// (| 1 with the top byte of a10). Returns the text.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIText_Init(uint8_t* text, int32_t kind, const char* string, uint32_t x,
                                                         uint32_t y, int32_t depth, int32_t layer, uint32_t scale,
                                                         uint8_t a8, uint8_t a9, uint32_t a10);
extern "C" void UIText_Init_entry(void);
// 0x00320690 (8 stack words, ret 0x20; the caller's ECX and EDX low bytes become UIText_Init's flag words a9 / a8):
// UIText_Init(text, kind a2, "", a3, a4, a5, a6, a7, EDX, ECX, a8), then the derived vtable 0x0082a450, colour -1,
// alignment -1, the rest cleared and the line height (+0x68, +0x6c) = the height of "!" in the kind's font
// (0x0095f8d4; none for kinds 7/8) at the text's scale (nglGetTextExtent; unsigned to float on the x87).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIText_Construct(uint8_t* text, int32_t kind, uint32_t x, uint32_t y,
                                                              int32_t depth, int32_t layer, uint32_t scale,
                                                              uint32_t a8, uint32_t ecx, uint32_t edx);
extern "C" void UIText_Construct_entry(void);
// 0x003215c0 (name in EBX; panel on the stack, ret 4): the panel's text item (+0xc array, +0x14 count) whose name
// (vtable +0x9c, a ScriptString released after) is `name`, else null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Panel_FindText(const char* name, uint8_t* panel);
extern "C" void Panel_FindText_entry(void);
// 0x00321650 (picture list in EAX: +0 array, +8 count; name on the stack, ret 4): the picture whose name (+0x3c
// ScriptString) is `name`, else the shared default picture (0x00320170 with 0x0095f8d0).
uint8_t* XB_REGS1 XB_STDCALL Panel_FindPicture(uint8_t* list, const char* name);
// 0x00325d60 (menu in EDI; selection on the stack, ret 4): pictures difficulty0-2 of the list +0x34 shown (+0x34
// (1)) for the selection, hidden for the others; then the alphas of +0x3c / +0x40 by the index +0x22 (0: 0 / 1.0,
// 1: 1.0 / 1.0, else 1.0 / 0).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetDifficultyPicture(uint8_t* menu, int32_t selection);
extern "C" void FE_SetDifficultyPicture_entry(void);
// 0x0032ea00 (menu in ESI): deletes every item (vtable +0 (1)), frees the array (Z_Free), stops the menu's sound
// channel when its handle (+0x2c) is still live (table 0x00962860), clears the handle, array and count.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIMenu_FreeItems(uint8_t* menu);
extern "C" void UIMenu_FreeItems_entry(void);

// 0x0032cf50 (profile in ESI): unless 0x0095ee09 is set, the profile's default settings (sensitivities 25, invert
// from the cvar at 0x00929c0c being negative, cg_controllerconfig, volumes ...).
extern "C" PLATFORM_HIDDEN void XB_CDECL Profile_SetDefaults(uint8_t* profile);
extern "C" void Profile_SetDefaults_entry(void);
// 0x0032d660: the "UI_Type" sound alias (S_FindSoundByHash) played at the origin (S_PlaySoundAlias, volumes -1).
void XB_CDECL UI_PlayTypeSound();
// 0x003307a0: from the mission success/fail menu (retry dialog, HandleSelect): with a checkpoint to reload (0x008dd1cf == 1) g_reloading 1 and "reboot",
// returns 1; else the front end's dialog state 4, cl_paused 1 and the front end's vtable +0xc (7), returns 0.
uint8_t XB_CDECL MissionSuccessFailMenu_ReloadCheckpoint();


// UI elements (title C++ objects): +0x04 fade alpha, +0x08 depth, +0x0c fade time, +0x10 flags (bit 4 fading in,
// bit 5 fading out, bits 6-7 alignment).
// 0x000112f0 (vtable slot): the depth +0x08.
void XB_THISCALL UIElement_SetDepth(uint8_t* element, float depth);
// 0x0030fc20 (vtable slot): alignment bits 6-7 of the flags +0x10: 0 -> 0x40, 2 -> 0x80, else none.
void XB_THISCALL UIElement_SetAlign(uint8_t* element, int16_t align);
// 0x00315e70: advances a fade by dt / the fade time +0x0c when vtable slot +0x30 says the element is shown; a fade in
// stops at 1.0 (clears flags 0x30), a fade out at 0 (clears flags 0x34).
void XB_THISCALL UIElement_UpdateFade(uint8_t* element, float dt);

// UI quads: four base corner points (x, y, z; 12 bytes apart) at +0x00, four vertex alpha scales at +0x30 and the
// screen quad (NglQuad: x, y, u, v, colour per vertex) at +0x40.
// 0x00310400: moves the quad's vertices by (dx, dy).
void XB_REGS1 XB_STDCALL UIQuad_Translate(uint8_t* quad, float dx, float dy);
// 0x00310450: the quad's vertices back to the base corner points.
void XB_REGS1 XB_CDECL UIQuad_ResetPositions(uint8_t* quad);
// 0x00310480: vertex positions from four (x, y, z) points (ECX).
void XB_REGS3 XB_CDECL UIQuad_SetCornerPositions(uint8_t* quad, uint32_t unused_edx, const float* points);
// 0x003104b0: vertex positions from separate x (EAX) and y arrays.
void XB_REGS1 XB_STDCALL UIQuad_SetVertexPositions(const float* xs, uint8_t* quad, const float* ys);
// 0x003104f0: vertex UVs from four (u, v, w) points (ECX).
void XB_REGS3 XB_CDECL UIQuad_SetCornerUVs(uint8_t* quad, uint32_t unused_edx, const float* points);
// 0x00310520: vertex UVs from separate u (EAX) and v (ECX) arrays.
void XB_REGS3 XB_STDCALL UIQuad_SetVertexUVs(const float* us, uint32_t unused_edx, const float* vs,
                                                     uint8_t* quad);
// 0x00310560 (quad in EAX; keeps ECX/EDX, its caller loops on them): places each base point at (x, y) minus its
// offset from the points' bounding-box centre times (sx, sy).
extern "C" PLATFORM_HIDDEN void XB_CDECL UIQuad_PlaceScaled(uint8_t* quad, float x, float y, float sx, float sy);
extern "C" void UIQuad_PlaceScaled_entry(void);
// 0x00310820 (quad in EDI): a wipe of `amount` (above 1: 2 - amount) from the side `mode` (1-4; 0 = nothing) over
// the base size times `scale`; with uv_scale > 0 the UVs follow by amount * uv_scale. Picks the corner order by
// whether vertices 0 and 1 have (almost) the same u.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIQuad_SetWipe(uint8_t* quad, float amount, int32_t mode, float uv_scale,
                                                       float scale);
extern "C" void UIQuad_SetWipe_entry(void);
// 0x00316e20: out (ECX) = a * b / 255 per byte; returns out.
uint8_t* XB_REGS3 XB_CDECL Color_Modulate(uint32_t unused_eax, uint32_t unused_edx, uint8_t* out, uint32_t a,
                                          uint32_t b);
// 0x00317240 (out in EAX, source in ECX; keeps XMM5): out = (max u, max v, 0) over the four vertices of the quad
// at ECX + 8 - the vertex UVs when ECX is a UI quad.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIQuad_GetMaxUV(float* out, const uint8_t* source);
extern "C" void UIQuad_GetMaxUV_entry(void);
// 0x003172f0 (quad in EAX, vertex in ESI): the vertex colour's alpha byte = alpha * 255.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIQuad_SetVertexAlpha(uint8_t* quad, int32_t vertex, float alpha);
extern "C" void UIQuad_SetVertexAlpha_entry(void);
// 0x00317350: the vertex colour's RGB, keeping its alpha.
void XB_REGS3 XB_STDCALL UIQuad_SetVertexColor(int32_t vertex, uint32_t unused_edx, uint8_t* quad, uint32_t color);
// 0x00322bd0: out = (min x, min y, the word at 0x00922eb8) over the four vertices.
void XB_REGS3 XB_CDECL UIQuad_GetVertexMin(float* out, uint32_t unused_edx, const uint8_t* quad);
// 0x00322c70: out = (max x, max y, the word at 0x00922eb8) over the four vertices.
void XB_REGS3 XB_CDECL UIQuad_GetVertexMax(float* out, uint32_t unused_edx, const uint8_t* quad);
// 0x00322d10: UIQuad_SetVertexAlpha with alpha times the vertex's alpha scale +0x30.
void XB_REGS3 XB_STDCALL UIQuad_SetVertexAlphaScaled(uint8_t* quad, uint32_t unused_edx, int32_t vertex,
                                                     float alpha);
// 0x00332b90 (matrix in ECX, z in XMM2): vertex positions = the matrix (rows 16 bytes apart, row 3 = translation)
// times (x, y, z) of each vertex (use_quad) or base point.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIQuad_ApplyMatrix(const float* matrix, float z, uint8_t* quad,
                                                           uint32_t use_quad);
extern "C" void UIQuad_ApplyMatrix_entry(void);

// UI pictures (vtable 0x0082a2c0, a UI element): +0x14 centre (x, y, z), +0x20 the quads (PtrVector: data,
// capacity, count; 0xa8-byte UI quad blocks, +0xa4 = 1 from the allocators), +0x2c an owned 0x34-byte block, +0x30
// 0 and +0x34 / +0x38 1.0 (reset by UIPicture_ResetTransform), +0x3c the name (ScriptString). Flags bit 0: a
// transform was applied. vtable slots: +0x1c (two words), +0x24 (one word), +0x68 set texture, +0x6c set quad
// flags, +0x78 set alpha, +0x7c set alpha times the vertex alpha scales, +0x88 set corners (xs, ys). World
// pictures (vtable 0x00828df8) add a world position at +0x40 and slot +0x8c (x, y) to place them on screen.
// 0x00011a90 / 0x00011ac0: the bounds' width / height (x87).
long double XB_THISCALL UIPicture_GetWidth(uint8_t* picture);
long double XB_THISCALL UIPicture_GetHeight(uint8_t* picture);
// 0x00310110: corners (x0, y0), (x1, y0), (x0, y1), (x1, y1) through vtable +0x88.
void XB_THISCALL UIPicture_SetRect(uint8_t* picture, float x0, float y0, float x1, float y1);
// 0x003166a0 (picture in EDX, centre x in XMM0): every vertex x and the centre x scaled about the centre by 0.75
// (AL set) or 4/3.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIPicture_ScaleXForAspect(uint32_t widescreen, uint8_t* picture, float cx);
extern "C" void UIPicture_ScaleXForAspect_entry(void);
// 0x003169c0: the depth +0x08 and every quad's z.
void XB_THISCALL UIPicture_SetDepth(uint8_t* picture, float depth);
// 0x00316a10 / 0x00316a50: every quad's flags / texture.
void XB_THISCALL UIPicture_SetQuadFlags(uint8_t* picture, uint32_t flags);
void XB_THISCALL UIPicture_SetTexture(uint8_t* picture, void* texture);
// 0x00316ec0 (picture in EDI): releases the first quad's texture, frees the quads, the block +0x2c, the name and
// the quad array; the vtable back to the UI element's (0x00829244).
extern "C" PLATFORM_HIDDEN void XB_CDECL UIPicture_Dtor(uint8_t* picture);
extern "C" void UIPicture_Dtor_entry(void);
// 0x00316f40: copies the element fields, name, transform, block +0x2c and quads (new copies appended) of `source`;
// the centre from the source's x, y with z 0.
void XB_THISCALL UIPicture_CopyFrom(uint8_t* picture, uint8_t* source);
// 0x00317050 (picture in ESI; name on the stack, ret 4): the picture constructor. Returns the picture.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIPicture_Init(uint8_t* picture, const char* name);
extern "C" void UIPicture_Init_entry(void);
// 0x003216e0 (list in EDI: +0 data, +8 count; flag in BL, centre x in XMM0): UIPicture_ScaleXForAspect on every
// picture of the list.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIMeshList_ScaleXForAspect(uint32_t widescreen, uint8_t* list, float cx);
extern "C" void UIMeshList_ScaleXForAspect_entry(void);
// 0x00321830 (picture in ECX, name in EAX): UIPicture_Init, then the world picture vtable and position 0.
uint8_t* XB_REGS3 XB_CDECL UIWorldPicture_Construct(const char* name, uint32_t unused_edx, uint8_t* picture);
// 0x00321890: every vertex's alpha byte = alpha * 255.
void XB_THISCALL UIPicture_SetAlpha(uint8_t* picture, float alpha);
// 0x00321940: every vertex's RGB, keeping its alpha.
void XB_THISCALL UIPicture_SetColorRGB(uint8_t* picture, uint32_t color);
// 0x0032c190: the world position projected (nglProjectPointCopy) and handed to vtable +0x8c.
void XB_THISCALL UIWorldPicture_UpdateScreenPos(uint8_t* picture);
// 0x0032c200 / 0x0032c430 (picture in EAX; out on the stack, ret 4; keep XMM6/XMM7, SetQuadCorners relies on
// them): out = (min / max x, min / max y, the word at 0x00922eb8) over all quads' vertices. Return out.
extern "C" PLATFORM_HIDDEN float* XB_CDECL UIPicture_GetBoundsMin(uint8_t* picture, float* out);
extern "C" void UIPicture_GetBoundsMin_entry(void);
extern "C" PLATFORM_HIDDEN float* XB_CDECL UIPicture_GetBoundsMax(uint8_t* picture, float* out);
extern "C" void UIPicture_GetBoundsMax_entry(void);
// 0x0032c660: every quad back to its base points; centre = the bounds' centre, +0x30 = 0, +0x34 / +0x38 = 1.0.
void XB_THISCALL UIPicture_ResetTransform(uint8_t* picture);
// 0x0032c770: one quad: its vertices = (xs, ys); several: every vertex mapped from the current bounds onto the
// bounding box of (xs, ys). The centre = that box's centre (z 0).
void XB_THISCALL UIPicture_SetQuadCorners(uint8_t* picture, const float* xs, const float* ys);
// 0x0032cad0: every vertex's alpha byte = alpha * its alpha scale (+0x30) * 255 (x87).
void XB_THISCALL UIPicture_SetVertexScaledAlpha(uint8_t* picture, float alpha);
// 0x0032cba0: vtable +0x1c (a4, a3), then a new quad (0x003173e0: points, colours, unit UVs, the depth) appended;
// with a texture name the localized texture (0x00322b30) through vtable +0x68; the centre = the bounds' centre.
void XB_THISCALL UIPicture_AddQuad(uint8_t* picture, const float* points, const uint32_t* colors, uint32_t a3,
                                   uint32_t a4, const char* texture);
// 0x00332390: one mesh from the data at *offset (advanced; little-endian): material index (16-byte records:
// +0x00 texture, +0x08 colour, +0x0c use the texture, +0x0d quad flag 2, +0x0e / +0x0f clear: flags 0x40 / 0x80),
// vertex count, per vertex position (3 floats), RGBA colour (modulated by the material's), u, v; 2 skipped bytes,
// index count and 16-bit indices. Triangles sharing an edge with the next (0x003101d0) become one quad, others a
// quad with the third corner twice; corners through the matrix (row 3 = translation; the last z is the depth,
// vtable +0x24), the positions as the base points. Then the material's texture (+0x68, nglAddRef), flags (+0x6c)
// and the centre.
void XB_THISCALL UIPicture_LoadMesh(uint8_t* picture, const uint8_t* materials, const uint8_t* data,
                                    uint32_t* offset, const float* matrix);
// 0x00337460: every quad through the matrix (UIQuad_ApplyMatrix at the depth; from the vertices once flags bit 0
// is set, else from the base points), then vtable +0x7c with alpha (times the fade alpha +0x04 once bit 0 is set);
// sets bit 0.
void XB_THISCALL UIPicture_SetTransform(uint8_t* picture, const float* matrix, float alpha);

// UI texts. The base (UITextBase, vtable 0x008298b8, a UI element): +0x14 an owned block, +0x18 kind (font index,
// ui_fonts 0x0095f8d4; 7 and 8 have none), +0x1c the text (ScriptString), +0x20..+0x4c colours, +0x2c position (x,
// y, z), +0x38 scale (x, y, z), +0x54 the name (ScriptString), +0x5c flags (0x30 horizontal / 0xc0 vertical
// alignment). vtable slots: +0x30 shown, +0x7c set position, +0xc4 line count, +0xc8 / +0xcc get x / y, +0xdc set or
// clear flags (mask, on), +0x100 draw lines. UIText (vtable 0x0082a450) adds +0x68 / +0x6c line height and spacing,
// +0x78 line count, +0x80 the lines (0x20-byte UI text lines, vector-new block), +0x88 / +0x8c the shown range,
// +0x94 scrolled.
struct ScriptString;
// 0x000113f0 / 0x00011430: scale (s, s, 0) / (sx, sy, 0).
void XB_THISCALL UITextBase_SetUniformScale(uint8_t* text, float scale);
void XB_THISCALL UITextBase_SetScale(uint8_t* text, float sx, float sy);
// 0x000114c0: set position (vtable +0x7c) to (get x + dx, get y + dy) (x87 sums).
void XB_THISCALL UITextBase_MoveBy(uint8_t* text, float dx, float dy);
// 0x00011540: position (x, y, 0).
void XB_THISCALL UITextBase_SetPosition(uint8_t* text, float x, float y);
// 0x000115d0 / 0x000115f0: out = a copy of the name +0x54 / text +0x1c. Return out.
ScriptString* XB_THISCALL UITextBase_GetName(uint8_t* text, ScriptString* out);
ScriptString* XB_THISCALL UITextBase_GetText(uint8_t* text, ScriptString* out);
// 0x00011700: frees +0x14, releases the name and text, the UI element vtable; frees the text with flags bit 0.
uint8_t* XB_THISCALL UITextBase_DeletingDtor(uint8_t* text, uint32_t flags);
// 0x0030fba0 / 0x0030fb60: alignment flags 0x30 / 0xc0 cleared, then set to 0x10 / 0x20 (0x40 / 0x80) when given.
void XB_THISCALL UITextBase_SetHorzAlign(uint8_t* text, int32_t align);
void XB_THISCALL UITextBase_SetVertAlign(uint8_t* text, int32_t align);
// 0x000117a0, 0x000117e0, 0x00011820, 0x00011870, 0x00320570: the UITextBase setters, then UIText_LayoutLines.
void XB_THISCALL UIText_SetHorzAlign(uint8_t* text, int32_t align);
void XB_THISCALL UIText_SetVertAlign(uint8_t* text, int32_t align);
void XB_THISCALL UIText_SetUniformScale(uint8_t* text, float scale);
void XB_THISCALL UIText_SetScale(uint8_t* text, float sx, float sy);
void XB_THISCALL UIText_SetPosition(uint8_t* text, float x, float y);
// 0x00320790 (text in ESI): the lines' vector delete (0x00012480), then UITextBase's destructor.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIText_Dtor(uint8_t* text);
extern "C" void UIText_Dtor_entry(void);
// 0x00012460: UIText_Dtor, frees the text with flags bit 0.
uint8_t* XB_THISCALL UIText_DeletingDtor(uint8_t* text, uint32_t flags);
// 0x0030f900: when shown, draw lines (vtable +0x100) of the range +0x88/+0x8c when scrolled, else (0, +0x78).
void XB_THISCALL UIText_Draw(uint8_t* text);
// 0x0030f880: line height and spacing +0x68/+0x6c = spacing, or for -1 the height of "!" at the text's scale.
void XB_THISCALL UIText_SetLineSpacing(uint8_t* text, int32_t spacing);
// 0x00321030: the height of the text at its scale (x87, unsigned).
long double XB_THISCALL UIText_GetLineHeight(uint8_t* text);
// 0x0032ba80: (line count - 1) * spacing + the line height (x87).
long double XB_THISCALL UIText_GetTextHeight(uint8_t* text);
// 0x003158d0 / 0x003158a0: UI text line constructor (kind 8, position 0, text "") / destructor.
uint8_t* XB_THISCALL UITextLine_Construct(uint8_t* line);
void XB_THISCALL UITextLine_Dtor(uint8_t* line);

// 0x0030fa60 (vtable): moves (x, y) by the text's width (+0xd0) / height (+0xd4) for the alignment: right (0x20)
// minus all, left (0x10) nothing, else half; bottom 0x80 / top 0x40 likewise. Releases the by-value ScriptString.
void XB_THISCALL UITextBase_ApplyAlignOffset(uint8_t* text, float* x, float* y, uint32_t string);
// 0x003159c0: copies the element fields, the block +0x14 (a new 16-byte copy), kind, text, colours, position, scale,
// name and flags of `source`.
void XB_THISCALL UITextBase_CopyFrom(uint8_t* text, uint8_t* source);
// 0x0030f730: when scrolled (+0x94), +0x90 scroll offset += delta clamped to [-(lines * spacing - margin (+0x84)),
// 0] (with +0x95: [-(lines * spacing + 2 margin), margin]); moves the text by the change (vtable +0x70) and sets the
// shown range +0x88/+0x8c.
void XB_THISCALL UIText_ScrollBy(uint8_t* text, float delta);
// 0x0032b940: frees the lines and allocates `count` new ones (Heap_Alloc16, count at -4); +0x7c = count, +0x78 = 0.
void XB_THISCALL UIText_AllocLines(uint8_t* text, int32_t count);
// 0x0032e180 / 0x0032e250: text tables: selection +0x50 one down (below the row count +0x20) / up, the first shown
// row +0x4c following it (+0x48 rows shown); then the "UI_Highlight" sound. Returns its result | 0xffff (-1 without
// the alias).
uint32_t XB_THISCALL UITextTable_SelectNext(uint8_t* table);
uint32_t XB_THISCALL UITextTable_SelectPrev(uint8_t* table);
// 0x003202e0 (text in ESI): each line's position (+0x04) from the text's position, alignment, the line's width
// (+0x10), the spacing and the height of " "; then the baseline offset +0x70.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIText_LayoutLines(uint8_t* text);
extern "C" void UIText_LayoutLines_entry(void);

// 0x0032bc00 (vtable +0x14; matrix 16-byte aligned, row 3 = translation): position (+0x2c) = translation + the
// current position once flags bit 0 is set, else + the base position +0x20; scale (+0x38) = the matrix diagonal
// times the current scale / the base scale +0x44; then set alpha (vtable +0x88) to alpha (times the fade alpha
// once bit 0 is set).
void XB_THISCALL UITextBase_SetTransform(uint8_t* text, const float* matrix, float alpha);
// 0x0032af10: the same for UIText (y scale from the negated m[5]; base: the matrix scale itself), spacing +0x6c =
// line height * y scale, then UIText_LayoutLines.
void XB_THISCALL UIText_SetTransform(uint8_t* text, const float* matrix, float alpha);

// 0x003207f0 (stdcall; Init tail-jumps here): per '~' button token of the line's text: the token start and the
// width so far (+0x18 records: int16 start, x, end, x after the glyphs), the token replaced by its glyph string
// (0x00315620), the glyphs' width in the button font (ui_fonts[2]) at button_scale (the text's +0x64); the line width +0x10 after the last token.
// Returns the token count (+0x1c / 2).
uint32_t XB_STDCALL UITextLine_LayoutTokens(uint8_t* line, float scale, float button_scale);
// 0x00320f20 (line in ESI, text in EAX; kind, scale, button scale on the stack, ret 0xc): text, kind, position 0, two marks
// (+0x1c) per '~' with a new token block (+0x18), then UITextLine_LayoutTokens; without tokens the width +0x10.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL UITextLine_Init(uint8_t* line, const char* string, int32_t kind,
                                                             float scale, float button_scale);
extern "C" void UITextLine_Init_entry(void);
// 0x00320b00 (word in EAX; kind, scale, button scale on the stack; result in XMM0): the word's width with its
// button tokens (glyphs in the button font at the button scale).
extern "C" PLATFORM_HIDDEN float XB_CDECL UIText_MeasureWord(const char* word, int32_t kind, float scale,
                                                            float button_scale);
extern "C" void UIText_MeasureWord_entry(void);
// 0x00320ce0 (line in ESI; 7 stack words, ret 0x1c): the line's text pieces (nglListAddText) and the button glyphs
// between them in the button font (colour, scale, moved up by `button_drop`).
extern "C" PLATFORM_HIDDEN void XB_CDECL UITextLine_Draw(uint8_t* line, float depth, uint32_t colour,
                                                         uint32_t button_colour, float sx, float sy,
                                                         float button_scale, float button_drop);
extern "C" void UITextLine_Draw_entry(void);
// 0x0032bab0 (vtable +0x100): when shown, lines [first, end) (clamped to the count) with UITextLine_Draw: the colour
// +0x50 (faded with flags bit 3) at the fade alpha, the button colour +0x60, scale, +0x64 / +0x70, the depth.
void XB_THISCALL UIText_DrawLines(uint8_t* text, int32_t first, int32_t end);
// 0x00321160 (vtable +0x3c): when shown and not "", the text at its position moved by the alignment (vtable +0xfc)
// in its colour at the fade alpha.
void XB_THISCALL UITextBase_Draw(uint8_t* text);
// 0x0032b0d0: word-wraps `string` at `width` (separators " -\n\r"; "\f[" words change the kind); with `store` the
// lines go to the line array (up to its capacity +0x7c). Returns the line count.
uint32_t XB_STDCALL UIText_WrapLines(uint8_t* text, const char* string, int32_t width, uint8_t store);
// 0x0032b420 (vtable +0x14c; ScriptString by value): counts the wrapped lines, reallocates the line array, wraps
// into it, the text = line 0's, then UIText_LayoutLines; +0x74 = width.
void XB_THISCALL UIText_SetWrappedText(uint8_t* text, uint32_t string, int32_t width, float a3);
// 0x0031f920: text tables (+0x10 a UI text drawn for every cell): the header (+0x34; texts +0x38.., column offsets
// +0x24..), then rows +0x4c.. (+0x48 shown, +0x20 count; records +0x18: +0x04 text, +0x0c columns, +0x10 texts), the
// selected one (+0x50) highlighted (+0x15, vtable +0x94); the text item's position restored after.
void XB_THISCALL UITextTable_Draw(uint8_t* table);

// Front-end helpers that run every title call at the original's stack position (the set-text methods keep a stack
// buffer's address, the image loaders hand stack buffers on): the entry (fe.cpp FE_FRAME_ENTRY) moves the C frame
// below a gap and hands the C version the entry registers and the entry ESP. They return the original's last EAX.
struct FrameIn {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp;
};
#define FE_FRAME_FUNCTION(name)                                                                               \
    extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL name(const FrameIn* in, uint32_t entry_esp);                   \
    extern "C" void name##_entry(void);

// The bonus text helpers (a 0x20-byte sprintf buffer at the original's frame position).
// 0x0030dbc0 (menu in ESI): BONUS_CHAR_<n>_NAME into the label +0x68, _STATS and _BIO wrapped (300, -1.0) into
// +0x6c / +0x70; n = the index +0x22 + 1.
FE_FRAME_FUNCTION(FE_BonusCharacterSetText)
// 0x0031be80 / 0x0031cc30 (menu in ESI): the current vehicle / weapon (index +0x22, item +0x70 / +0x84 + index * 4)
// BONUS_VEH_/BONUS_WEAP_<n>_<item>_NAME into the label +0x3c, _INFO wrapped (300, -1.0) into +0x44, then "%i /
// %i" of the item and the category's count from the unlock counts 0x00312e30 fills (0x0095f924; weapons 0-9 at
// words 0-9, vehicles 0-5 at words 10-15, others 0) into +0x40.
FE_FRAME_FUNCTION(FE_BonusVehicleSetText)
FE_FRAME_FUNCTION(FE_BonusWeaponSetText)

// The bonus image loaders (menu in EAX): the old image freed (inline, or FE_Bonus*FreeImage), the texts
// (FE_Bonus*SetText), the .dds file of the current item opened (handle, aligned buffer, texture fields), read whole
// into a 32 KB-aligned buffer (Mem_AlignedAlloc, 0x80, 0x30000) and loaded as a texture onto the image quad.
// 0x0031d5b0: characters, bonus\char\char<n>.dds ("texchar"; +0x74 file, +0x78 buffer, +0x7c texture, quad +0x80).
FE_FRAME_FUNCTION(FE_BonusCharacterLoadImage)
// 0x0031a900: the collector's-edition hub: bonus_ce\main\ce_<n>.dds, row 2 ce_3_<+0x48 + 1>.dds (+0x5c.., quad +0x68).
FE_FRAME_FUNCTION(FE_BonusCEMainLoadImage)
// 0x0031d9d0: the bonus materials hub: bonus\main\tex<n>.dds; row 1 bonus\main\mov<item>.dds, its first items by
// language (+0x88.., quad +0x94).
FE_FRAME_FUNCTION(FE_BonusMainLoadImage)
// 0x0031cd50 / 0x0031bf70 / 0x0031b730: weapons / vehicles / concept art: bonus\weapon<f>\wp<n>_<item>[f].dds etc.,
// the folder f from the category n (and the vehicle item); full screen (+0xac / +0x88 / +0xc0) picks the "f" file,
// the "...Full" texture name and the full-screen quad.
FE_FRAME_FUNCTION(FE_BonusWeaponLoadImage)
FE_FRAME_FUNCTION(FE_BonusVehicleLoadImage)
FE_FRAME_FUNCTION(FE_BonusConceptLoadImage)
// 0x0031a490: collector's-edition art: bonus_ce\<folder><f>\<prefix><group>[_<item>][f].dds from the category +0x22
// and the current item's group / index (0x0030be60 / 0x0030bd30, menu in EDX); +0x44.., quads +0x50 / +0xb4.
FE_FRAME_FUNCTION(FE_BonusCELoadImage)
// The bonus label helpers: the unlock counts (Profile_SumMissionStats of 0x0095f924) hide the locked rows (+0x20),
// the row texts, BONUS_LOCKED on the locked ones (+0x24), then the current texts.
// 0x0031d880 (menu in EAX): characters (rows up to the characters reached, count word 16), then 0x0031d6b0;
// +0x3c = 1.0, +0x40 cleared.
FE_FRAME_FUNCTION(FE_BonusCharacterLabel)
// 0x0031c5d0 (menu in ESI) / 0x0031d250 (menu in EAX): vehicle (counts 10-15) / weapon (counts 0-9) categories; the
// current items raised to the stored bests (0x00312d60, EAX = out) + 1 while unlocked beyond them; then 0x0031c0e0
// (stdcall menu) / 0x0031ceb0 (EDI = menu); +0x48 = 1.0, +0x4c cleared.
FE_FRAME_FUNCTION(FE_BonusVehicleCategoryLabel)
FE_FRAME_FUNCTION(FE_BonusWeaponCategoryLabel)
// 0x0031de10 (menu in EAX): the bonus materials rows: row 1's movie count +0x78 (by the chapters reached, one or two
// fewer by language), +0x61 = more movies than the stored best (0x008dcf0c), rows 2-5 hidden before chapter 2; the
// texts, then 0x0031db10 (EDI = menu); the selection back to +0x80.
FE_FRAME_FUNCTION(FE_BonusMenuLabel)
// 0x00333060 (menu in EDI; movie name on the stack, ret 4): plays a bonus movie: the music handle stopped, the old
// movie and image freed, CL_PlayMovie (ESI = name), then +0x44 = 5 and FE_BonusCEMainLoadImage.
FE_FRAME_FUNCTION(SpecialMaterialsMenu_PlayMovie)
// 0x003330f0 (index in EAX, menu in ECX): the collector's-edition movies: rows 0-3 play Bringing / TheMen / the
// multiplayer map (+0x48: MPKass, MPMaub, MPTroina) / Behind; row 4 opens dialog screen 0x13. Nothing while a movie
// plays (+0x3c).
FE_FRAME_FUNCTION(SpecialMaterialsMenu_HandleSelect)
// 0x00338710 (slot in EAX; menu on the stack, ret 4): with room on the card (MemDlg_CheckFreeSpace2) the slot into
// 0x0095f480 +0x11ec; an empty slot asks MEM_XBOX_OVERWRITE_CONFIRM (NO selected); else (mode 3: the default
// profile applied to 0x008dce30 and CL_ApplyProfileSettings) ProfileMenu_ShowSaveWarning.
FE_FRAME_FUNCTION(FE_ProfileSelect)
// 0x0031ff30 (object in EAX): its +0x28 deleted, the chapter's theatre picture (textures\<language>\africa / sicily
// / europe.dds) drawn full screen for two frames, then released.
FE_FRAME_FUNCTION(FE_LoadTheatreBackground)
// 0x00335d60 (menu in EAX): the style letter (+0x44), UpdateLabelColors, CL_ExecControllerConfig, each command's key
// name (FE_CommandDisplayName) and the fixed PAUSE / OBJECTIVES / MOVE / LOOK labels; all 18 labels black.
FE_FRAME_FUNCTION(FE_ControlsUpdateText)
// 0x003367a0 (menu in ECX; pad on the stack, ret 4; main menu vtable 0x0082906c slot): the sign-in text (0x0095f478)
// by the Live state (signing in / signed in as <gamertag> / the sign-in error); the first time the start-up movies
// (legal, ATVI, Treyarch, intro) and the music; else the selected item and the backdrop shaders.
FE_FRAME_FUNCTION(MainMenu_Update)
// 0x0031af60 (vtable slot, menu in ECX): the loading screen: the quotes / chapters around +0x190, the panel, the
// picture +0x18c at (320, 240), the help bar, the movie frame and the menu.
FE_FRAME_FUNCTION(ChapterMenu_Draw)
// 0x003346e0 (menu on the stack, ret 4): the save device and free space checked at start-up; the dialogs closed.
FE_FRAME_FUNCTION(FE_CheckStartupStorage)
// 0x003175b0 (owner in EDI): two 22 x 22 cross pictures (normal_cross: ironcrosswhite at +0x08, ace_cross:
// ironcrossgold at +0x0c).
FE_FRAME_FUNCTION(FE_LiberatorCreateCrossIcons)
// 0x0032d890: fades the music channel out over 0x008d2090 seconds (step 0x008d208c) with a black quad, then stops
// all sounds.
FE_FRAME_FUNCTION(FE_FadeOut)
// 0x003393f0 (name, data): a new panel (Z_Malloc 0x60), Panel_Load, FE_SetupPanel (EAX = panel, EDX = name).
FE_FRAME_FUNCTION(FE_LoadPanel)
// 0x00337050 (owner in ESI): the front-end menu (+0x28, its first screen) and the dialog manager (+0x30).
FE_FRAME_FUNCTION(FE_CreateMenus)
// 0x00338d70 (vtable slot, menu in ECX): difficulty pictures hidden, the selection restored, the propaganda movie
// opened once, row 1 selected.
FE_FRAME_FUNCTION(FE_DifficultyMenu_Init)
// 0x00331340 (menu in ESI): the control and sound settings to the cvars / globals, the controller style config.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_ApplyControlSettings(uint8_t* menu);
extern "C" void FE_ApplyControlSettings_entry(void);
// 0x0030d140 / 0x0030cdf0 (seconds in EAX, at most 359999; ret 0x10): days, hours, minutes and seconds into the
// four out words. Two identical copies.
int32_t* XB_REGS1 XB_STDCALL FE_SplitPlayTime(int32_t seconds, int32_t* out_seconds, int32_t* out_minutes,
                                              int32_t* out_hours, int32_t* out_days);
int32_t* XB_REGS1 XB_STDCALL FE_SplitPlayTime2(int32_t seconds, int32_t* out_seconds, int32_t* out_minutes,
                                               int32_t* out_hours, int32_t* out_days);

}  // namespace game
