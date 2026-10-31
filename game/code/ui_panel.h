#pragma once

// UI panels: the element sets a loading screen, menu or dialog draws, loaded from a panel file (a node tree of
// pictures and texts, each node with its own transform). Title C++ objects whose elements are called through their
// vtables (thiscall).
//
// Panel: +0x00 pictures (PtrVector: data, capacity, count), +0x0c texts (PtrVector), +0x18 the picture index
// Panel_Draw skips, +0x1c texts hidden, +0x1d the file name (64 characters). Element vtable slots: +0x00 deleting
// destructor (flags), +0x18 set alignment (int16), +0x1c set depth and layer (depth, layer), +0x28 the depth (x87),
// +0x3c draw (texts), +0x44 draw (pictures) / hidden (texts).
// Panel file data (little-endian): a node count (int32), then the nodes: type byte (0x91 picture, 0x94 text), the
// name (int16 length + characters, made a C string in place), the node transform (four x, y, z rows), alignment byte,
// child count (int32) and the children; then the picture or text record of the node itself.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "title_data.h"

// Calls a title function with all six registers from `regs` and `count` stack words (args[0] pushed last); ESP is
// restored after, whether the callee cleans its arguments or not. Returns EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ui_call_regs(uint32_t function, const game::TitleRegs* regs,
                                                         uint32_t count, const uint32_t* args);

namespace game {

// 0x00310010 (panel in ESI): draws (+0x44) every picture but the one at +0x18; unless +0x1c, every text not hidden
// (+0x44) is drawn (+0x3c).
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_Draw(uint8_t* panel);
extern "C" void Panel_Draw_entry(void);
// 0x00310070 (panel in EDI; layer on the stack, ret 4): every picture and text set (+0x1c) to its own depth
// (+0x28) in the layer.
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_SetLayer(uint8_t* panel, uint32_t layer);
extern "C" void Panel_SetLayer_entry(void);
// 0x003101d0 (first triangle in EDI; cdecl): the corners of triangles a and b (16-bit indices, 3 per triangle) as a
// quad: out[0] = a's corner b lacks, out[1..] the shared ones (in a's order), out[3] = b's corner a lacks. Returns
// the shared corner count.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Panel_TrianglePairToQuad(int32_t a, const int16_t* indices, int32_t b,
                                                                     int32_t* out);
extern "C" void Panel_TrianglePairToQuad_entry(void);
struct ScriptString;
// 0x00316580 (offset in ESI, data in EDI; string on the stack): appends the int16-length string at *offset.
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_ReadString(uint32_t* offset, const uint8_t* data,
                                                         ScriptString* string);
extern "C" void Panel_ReadString_entry(void);
// 0x00316480 (offset in ECX, data in EAX, out in EDI): three little-endian floats at *offset (bits copied).
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_ReadVec3(uint32_t* offset, const uint8_t* data, float* out);
extern "C" void Panel_ReadVec3_entry(void);
// 0x00316530 (offset in EAX, data in ECX): the int16-length name at *offset moved two bytes down over its length
// and terminated (two zero bytes) in place; returns it.
char* XB_REGS3 XB_CDECL Panel_ReadCString(uint32_t* offset, uint32_t unused_edx, uint8_t* data);
// 0x00321430 (offset in ESI; out, data on the stack): four rows of three floats (Panel_ReadVec3), each stored as
// (x, y, z, x).
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_ReadTransform(uint32_t* offset, float* out, const uint8_t* data);
extern "C" void Panel_ReadTransform_entry(void);
// 0x0032c0b0 (data in EAX, offset in EDI; record on the stack, ret 4): a material record: +8 the colour (bytes 2, 1,
// 0, 3), +0xc textured; if so +4 the name (Panel_ReadCString), +0 its texture (UI_FindLocalizedTexture) and three
// flag bytes +0xd..+0xf.
extern "C" PLATFORM_HIDDEN void XB_CDECL Panel_ReadMaterial(uint8_t* data, uint32_t* offset, uint8_t* record);
extern "C" void Panel_ReadMaterial_entry(void);
// 0x00316620 (panel in EAX): deletes (vtable +0 (1)) every picture and text, both vectors resized to 0.
void XB_REGS1 XB_CDECL Panel_FreeElements(uint8_t* panel);
// The inline panel delete of the destructors (0x000121d0 without the flags): Panel_FreeElements, the vector
// arrays freed (Mem_AlignedFree, +0x0c then +0x00, each cleared), the panel freed (Z_Free).
void Panel_Delete(uint8_t* panel);

// 0x000121d0 (panel in ESI; flags on the stack, ret 4): Panel_Delete's steps, the panel freed only with flags bit
// 0. Returns the panel.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Panel_DeletingDtor(uint8_t* panel, uint32_t flags);
extern "C" void Panel_DeletingDtor_entry(void);
// 0x00320170 (the UI root in EDI): the shared "default_PQ" picture +0x34, created on first use with one 100 x 100
// quad (colour 0xff80ff80, no texture). Returns it.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Panel_GetDefaultPicture(uint8_t* root);
extern "C" void Panel_GetDefaultPicture_entry(void);
// 0x0032bd00 (offset in EAX; ret 0x14): a text node: font name, colour (RGBA bytes), vertical alignment byte (bits
// 0-1 horizontal, 2-3 vertical), wrap width, scale, mode (1: a plain UI text base, else a UIText with the mode
// +0x13c), +0x8c value, position and size (x, y, z); the position moved by the size for the alignment and by the
// transform's translation. Appended to the panel's texts.
void XB_REGS1 XB_STDCALL Panel_CreateText(uint32_t* offset, uint8_t* panel, const uint8_t* data,
                                          const float* matrix, const char* name, uint32_t align);
// 0x0032fbd0 (offset in EAX; ret 0x14): a picture node: the material records (16 bytes, 0x0032c0b0 reads each),
// then the meshes, one picture each (UIPicture_LoadMesh through +0x40), appended to the panel's pictures; the
// materials' textures released after.
void XB_REGS1 XB_STDCALL Panel_CreatePicture(uint32_t* offset, uint8_t* panel, const uint8_t* data,
                                             const float* matrix, const char* name, uint32_t align);
// 0x003370d0: one node and its children (recursive); the node's transform times the parent's.
void XB_THISCALL Panel_LoadNode_r(uint8_t* panel, const uint8_t* data, uint32_t* offset, const float* matrix);
// 0x00337380 (data in ESI, panel in EDI; name on the stack, ret 4): the name into +0x1d, then the top nodes under
// the identity transform. False without data.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL Panel_Load(const uint8_t* data, uint8_t* panel, const char* name);
extern "C" void Panel_Load_entry(void);

}  // namespace game
