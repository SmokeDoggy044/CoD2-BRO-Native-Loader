// The effect file loader's helpers (FX_LoadEffectFile's tree, 0x00096000-0x000a7000): the
// effect object, its render operations and particle actions. Names are descriptive.
#pragma once

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00096370 (was FUN_00096370): the effect's name at +0x6c (64 bytes, zero padded; at most 64
// characters, then unterminated).
void XB_THISCALL FxEffect_SetName(uint8_t* self, const char* name);
// 0x00096400: the effect's matrix (+0, 16 words).
void XB_THISCALL FxEffect_SetMatrix(uint8_t* self, const uint32_t* matrix);
// 0x00096500: emitter entry `index` of the table at +0x58 (0x2c bytes): the 32-byte hashed name
// and three words.
void XB_THISCALL FxEffect_SetEmitterEntry(uint8_t* self, int32_t index, const uint32_t* name, uint32_t a, uint32_t b,
                                          uint32_t c);
// 0x00097c90 / 0x000990c0: words 1..3 of src as a vector (the second: 16-byte aligned out, lane
// 3 = word 1). Return out.
uint32_t* XB_CDECL FxVec3_FromWords1(uint32_t* out, const uint32_t* src);
uint32_t* XB_CDECL FxVec4_FromWords1(uint32_t* out, const uint32_t* src);
// 0x00099100 / 0x000991a0 (thiscall, 4 stack words, the second the ARGB colour): the colour as
// floats b, g, r, a / 255 at +0x14 / +0x24.
void XB_THISCALL FxOp_SetColor14(uint8_t* self, uint32_t, uint32_t color, uint32_t, uint32_t);
void XB_THISCALL FxOp_SetColor24(uint8_t* self, uint32_t, uint32_t color, uint32_t, uint32_t);
// 0x0009c350: word `index` of the table at +0xc.
void XB_THISCALL FxParamBlock_SetSlot(uint8_t* self, int32_t index, uint32_t value);
// 0x0009ca70: an empty element list {0, 0, 0, 0}. Returns self.
uint8_t* XB_THISCALL FxElementList_Init(uint8_t* self);
// Render operation constructors (vtable, kind bits at +4): 0x0009ccd0 (0x007c6724, kind 0x31,
// src words 1/0 at +0xc/+0x10), 0x0009e1c0 (0x007c70a0, 0x1d5), 0x0009ef00 (0x007c8490, 0x53 or
// 0x853 with src +0x24 set). Return self.
uint8_t* XB_THISCALL FxOp31_Init(uint8_t* self, const uint32_t* src);
uint8_t* XB_THISCALL FxOp1d5_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOp53_Init(uint8_t* self, const uint8_t* src);
// 0x000a07f0: byte offsets (+8, one per set bit of the mask at +0, sizes from 0x008bbbe0) of a
// 26-attribute vertex format; +4 = the total rounded up to 16. Returns self.
uint8_t* XB_THISCALL FxVertexFormat_Layout(uint8_t* self);
// 0x000a4ed0 / 0x000a4fd0 / 0x000a50e0 / 0x000a51f0: vector operations (vtables 0x007c9330 /
// 48 / 60 / 78): xyz at +0x10, the float at +0x1c. 0x000a52f0 (0x007c9390): a at +0x10, b - a at
// +0x20. Return self.
uint8_t* XB_THISCALL FxVectorOp_Init9330(uint8_t* self, const float* v, float w);
uint8_t* XB_THISCALL FxVectorOp_Init9348(uint8_t* self, const float* v, float w);
uint8_t* XB_THISCALL FxVectorOp_Init9360(uint8_t* self, const float* v, float w);
uint8_t* XB_THISCALL FxVectorOp_Init9378(uint8_t* self, const float* v, float w);
uint8_t* XB_THISCALL FxSegmentOp_Init(uint8_t* self, const float* a, const float* b);

// 0x00029a40 (stdcall): `capacity` 16-byte elements from the effect allocator (context word
// 0x00909948 cleared around the call), the first `count` set to {3, 0}. Null on failure.
uint8_t* XB_STDCALL FxArray16_New(uint32_t capacity, uint32_t count);
// 0x00096c20: the effect object's constructor (name, identity matrix, scale 1 at +0x4c, 0x40
// at +0x68, the rest cleared). Returns self.
uint8_t* XB_THISCALL FxEffect_Init(uint8_t* self, const char* name);
// 0x00096d70 (stdcall): `count` zeroed 0x2c-byte emitter entries (null on failure).
uint8_t* XB_STDCALL FxEmitterTable_New(uint32_t count);
// 0x000985d0: a 0x14-byte FxOp31 from the source record's words 5 and 1 (null on failure).
uint8_t* XB_CDECL FxRenderer_NewOp31(const uint32_t* const* source);
// Parameter objects from the effect allocator (null on failure): 0x0009a2a0 / 0x0009a470 an
// ordered float range (vtables 0x007c5ac0 / 0x007c5b20), 0x0009a3d0 a constant (0x007c5af0),
// 0x0009a400 a constant vector (0x007c5b08), 0x0009a2f0 / 0x0009a4c0 a per-component ordered
// vector range (0x007c5ad8 / 0x007c5b38).
uint8_t* XB_CDECL FxRange_New5ac0(float a, float b);
uint8_t* XB_CDECL FxRange_New5b20(float a, float b);
uint8_t* XB_CDECL FxConst_New(float value);
uint8_t* XB_CDECL FxConstVector_New(const float* v);
uint8_t* XB_CDECL FxVectorRange_New5ad8(const float* a, const float* b);
uint8_t* XB_CDECL FxVectorRange_New5b38(const float* a, const float* b);

}  // namespace game
