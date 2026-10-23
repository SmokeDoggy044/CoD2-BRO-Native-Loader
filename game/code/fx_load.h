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

// 0x00029760 / 0x00029820: growable 16-byte element arrays {data, int16 capacity, int16 count}:
// Reserve(capacity) and Push(element) (growth +1, +4 past 3); old blocks go back to the effect
// allocator. False when the allocation fails.
uint32_t XB_THISCALL FxArray16_Reserve(uint8_t* self, int32_t capacity);
uint32_t XB_THISCALL FxArray16_Push(uint8_t* self, const uint32_t* element);
// 0x00096f00: the emitter table (0x2c-byte entries, same array layout) resized to `count`.
uint32_t XB_THISCALL FxEmitterTable_Resize(uint8_t* self, int32_t count);
// 0x0009e530 / 0x0009ed10: FxOp53 derivatives (vtables 0x007c7320, kind 0xdd/0x8dd; 0x007c8220,
// kind 0x153/0x953 with a columns x rows frame grid at +0x40..+0x4c).
uint8_t* XB_THISCALL FxOpdd_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOpGrid_Init(uint8_t* self, const uint8_t* src);
// 0x000a0840 / 0x000a0860: an empty vertex format / bits added, then laid out. Return self.
uint8_t* XB_THISCALL FxVertexFormat_Init(uint8_t* self);
uint8_t* XB_THISCALL FxVertexFormat_Add(uint8_t* self, uint32_t bits);
// 0x0009c5a0: a word array of `count` copies of *value (effect allocator). Returns self.
uint8_t* XB_THISCALL FxPtrArray_InitFill(uint8_t* self, uint32_t count, const uint32_t* value);
// 0x0009c520: the particle action base (vtable 0x007c6220): `a` floats (0.0, then -FLT_MAX and
// FLT_MAX in the first two) at +4, `b` null words at +0xc, c and d at +0x14/+0x18 (d: the
// vertex attribute bits). Returns self.
uint8_t* XB_THISCALL FxAction_Init(uint8_t* self, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
// 0x0009c9b0: every action's attribute bits (+0x18) added to the vertex format.
void XB_THISCALL FxActionList_CollectFormat(const uint8_t* self, uint8_t* format);
// The particle action constructors apsParseActions calls (FxAction_Init with fixed arguments,
// then the action's vtable).
uint8_t* XB_THISCALL FxAction8df0_Init(uint8_t* self);  // 0x000a08c0
uint8_t* XB_THISCALL FxAction8e08_Init(uint8_t* self);  // 0x000a0910
uint8_t* XB_THISCALL FxAction8e20_Init(uint8_t* self);  // 0x000a0930
uint8_t* XB_THISCALL FxAction8e38_Init(uint8_t* self);  // 0x000a0950
uint8_t* XB_THISCALL FxAction8e4c_Init(uint8_t* self);  // 0x000a0a40
uint8_t* XB_THISCALL FxAction8e60_Init(uint8_t* self);  // 0x000a0a60
uint8_t* XB_THISCALL FxAction8e74_Init(uint8_t* self);  // 0x000a0a80
uint8_t* XB_THISCALL FxAction8e88_Init(uint8_t* self);  // 0x000a0aa0
uint8_t* XB_THISCALL FxAction8e9c_Init(uint8_t* self);  // 0x000a0ac0
uint8_t* XB_THISCALL FxAction8eb0_Init(uint8_t* self);  // 0x000a0ae0
uint8_t* XB_THISCALL FxAction8ec4_Init(uint8_t* self);  // 0x000a0b00
uint8_t* XB_THISCALL FxAction8ed8_Init(uint8_t* self);  // 0x000a0b20
uint8_t* XB_THISCALL FxAction8eec_Init(uint8_t* self);  // 0x000a0b40
uint8_t* XB_THISCALL FxAction8f00_Init(uint8_t* self);  // 0x000a0b60
uint8_t* XB_THISCALL FxAction8f14_Init(uint8_t* self);  // 0x000a0b80
uint8_t* XB_THISCALL FxAction8f28_Init(uint8_t* self);  // 0x000a0ba0
uint8_t* XB_THISCALL FxAction8f3c_Init(uint8_t* self);  // 0x000a0bc0
uint8_t* XB_THISCALL FxAction8f50_Init(uint8_t* self);  // 0x000a0be0
uint8_t* XB_THISCALL FxAction8f64_Init(uint8_t* self);  // 0x000a0c00
uint8_t* XB_THISCALL FxAction8f78_Init(uint8_t* self);  // 0x000a0c20
uint8_t* XB_THISCALL FxAction8f8c_Init(uint8_t* self);  // 0x000a0c40
uint8_t* XB_THISCALL FxAction8fa0_Init(uint8_t* self);  // 0x000a0c60
uint8_t* XB_THISCALL FxAction8fb4_Init(uint8_t* self);  // 0x000a0c80
uint8_t* XB_THISCALL FxAction8fc8_Init(uint8_t* self);  // 0x000a0ca0
uint8_t* XB_THISCALL FxAction8fdc_Init(uint8_t* self);  // 0x000a0cc0
uint8_t* XB_THISCALL FxAction8ff0_Init(uint8_t* self);  // 0x000a0ce0
uint8_t* XB_THISCALL FxAction9004_Init(uint8_t* self);  // 0x000a0d00
uint8_t* XB_THISCALL FxAction9018_Init(uint8_t* self);  // 0x000a0d20
uint8_t* XB_THISCALL FxAction902c_Init(uint8_t* self);  // 0x000a0d40
uint8_t* XB_THISCALL FxAction9040_Init(uint8_t* self);  // 0x000a0d60
uint8_t* XB_THISCALL FxAction9054_Init(uint8_t* self);  // 0x000a0d80
uint8_t* XB_THISCALL FxAction9068_Init(uint8_t* self);  // 0x000a0da0
uint8_t* XB_THISCALL FxAction907c_Init(uint8_t* self);  // 0x000a0dc0
uint8_t* XB_THISCALL FxAction9090_Init(uint8_t* self);  // 0x000a0de0
uint8_t* XB_THISCALL FxAction90a4_Init(uint8_t* self);  // 0x000a0e00

// Derived render operation constructors (FxOpGrid_Init or FxOp53_Init, then vtable and kind;
// +0x800 with src +0x24 set): 0x0009e6c0 (0x007c75a0, 0x1dd), 0x0009e800 (0x007c7820, 0x1d5),
// 0x0009e940 (0x007c7aa0, 0xd5), 0x0009ea80 (0x007c7d20, 0x15b), 0x0009ebc0 (0x007c7fa0, 0x5b).
uint8_t* XB_THISCALL FxOp1dd_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOp9d5_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOpd5_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOp15b_Init(uint8_t* self, const uint8_t* src);
uint8_t* XB_THISCALL FxOp5b_Init(uint8_t* self, const uint8_t* src);
// 0x00096630: each stage's vertex format (actions' bits, the renderer's +4 bits, 0x600) and the
// effect's particle data size (+0x64: sum of format size x stage count).
void XB_THISCALL FxEffect_ComputeStageSizes(uint8_t* self);
// 0x00096cd0 (stdcall) / 0x00096df0: the 0x3c-byte stage table (count 10, -1, FLT_MAX, 1, an
// empty vertex format at +0x18) - new block / resized array.
uint8_t* XB_STDCALL FxStageTable_New(uint32_t count);
uint32_t XB_THISCALL FxStageArray_Resize(uint8_t* self, int32_t count);
// 0x000968d0: stage count; shrinking destroys every stage's renderer and action list first
// (not cleared); high-water mark in 0x00909b90.
void XB_THISCALL FxEffect_SetStageCount(uint8_t* self, int32_t count);
// 0x00096af0 (7 stack words): stage `index`: count, two floats, flag bit 2 at +0xc, renderer
// (+0x10) and action list (+0x14), each replacing (and destroying) the old one.
void XB_THISCALL FxEffect_SetStage(uint8_t* self, int32_t index, uint32_t count, float a, float b, uint32_t renderer,
                                   uint32_t list, uint32_t flag);
// 0x00096bb0: the emitter table resized; high-water mark in 0x00909b94.
void XB_THISCALL FxEffect_SetEmitterCount(uint8_t* self, int32_t count);

// FX_CreateRenderer's creators (cdecl: the parameter list, an FxArray16): a parameter record
// (words 1 of the entries: two words, 3 floats, optional +0x24/+0x28/+0x2c and colour; the grid
// kinds also columns/rows at +0x30/+0x34) handed to the op constructor on a 0x40 / 0x50-byte
// allocation. Null on failure. 0x00099ce0 FxOp53, 0x0009a5a0 FxOp5b, 0x0009a850 FxOpd5,
// 0x0009ab00 FxOpdd, 0x00099f90 FxOpGrid, 0x0009adb0 FxOp15b, 0x0009b0a0 FxOp9d5, 0x0009b390 FxOp1dd.
uint8_t* XB_CDECL FxRenderer_NewOp53(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOp5b(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOpd5(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOpdd(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOpGrid(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOp15b(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOp9d5(const uint8_t* list);
uint8_t* XB_CDECL FxRenderer_NewOp1dd(const uint8_t* list);

// 0x00098650: the "Shrimp" renderer (FxOp1d5): nine parameter words and the colour of entry 9.
uint8_t* XB_CDECL FxRenderer_NewShrimp(const uint8_t* list);
// 0x00098ec0: the renderer object for an effect stage, by four-character id (*id, when set) or
// class name ("BillboardRenderer" ... "ShrimpRenderer"; "_NullRenderer" gives null).
uint8_t* XB_CDECL FX_CreateRenderer(const char* name, const uint32_t* id, const uint8_t* list);

// Small effect system helpers (fx_load.cpp, named from their bodies):
// 0x00094150 returns 0 (stdcall, three words); 0x00094800 sets flag 1 at +0x4c (not hookable);
// 0x00094920 float `value` into parameter `index` (+0x5c table, 12-byte entries, +8);
// 0x00094960 an emitter entry's action parameter (entry -> stage -> action list -> float);
// 0x00096420 / 0x00096430 +0xc0 = 0 / 1; 0x00096960 / 0x00096bd0 append a stage / emitter
// (returns the old count, high-water marks 0x00909b90 / 0x00909b94); 0x00097190 / 0x0009b960
// empty; 0x0009b680.. / 0x0009d5a0 global setters; 0x0009b810 a pool slot's address (null unless
// its flag byte is 1); 0x0009c180 apsPoolSet_Clear of 0x00909f08; 0x0009cc50 three textures at
// +0x70/+0x74/+0x78 (all or none); the render ops' shader setup 0x0009cca0, 0x0009e190 ...
uint32_t XB_STDCALL FxNull_Query(uint32_t, uint32_t, uint32_t);
void XB_THISCALL FxSystem_SetParamFloat(uint8_t* self, int32_t index, float value);
float XB_THISCALL FxSystem_GetEmitterParam(const uint8_t* self, int32_t index);
void XB_THISCALL FxEffect_ClearFlagC0(uint8_t* self);
void XB_THISCALL FxEffect_SetFlagC0(uint8_t* self);
int32_t XB_THISCALL FxEffect_AddStage(uint8_t* self);
int32_t XB_THISCALL FxEffect_AddEmitter(uint8_t* self);
void XB_CDECL FxNop_97190();
void XB_CDECL FxNop_9b960();
void XB_CDECL FxSet_8bb860(uint32_t value);
void XB_CDECL FxSet_8bb864(uint16_t value);
void XB_CDECL FxSet_8bb866(uint16_t value);
void XB_CDECL FxSet_8bb868(uint16_t value);
void XB_CDECL FxSet_8bb990(uint32_t value);
uint32_t XB_THISCALL FxPool_SlotAddress(const uint8_t* self, uint32_t index);
uint32_t XB_CDECL FxPools_Clear();
void XB_THISCALL FxOp_SetTextures(uint8_t* self, uint32_t a, uint32_t b, uint32_t c);
void XB_CDECL FxOp31_InitShaders();
void XB_CDECL FxOp1d5_InitShaders();
void XB_CDECL FxOpdd_InitShaders();
void XB_CDECL FxOp1dd_InitShaders();
void XB_CDECL FxOp9d5_InitShaders();
void XB_CDECL FxOpd5_InitShaders();
void XB_CDECL FxOp15b_InitShaders();
void XB_CDECL FxOp5b_InitShaders();
void XB_CDECL FxOpGrid_InitShaders();
void XB_CDECL FxOp53_InitShaders();

// 0x000a5a80 / 0x000a5f20: the disc / circle domains (centre, radius, unit normal and two unit
// in-plane axes; packed SSE with RSQRTSS + one Newton step). Return self.
uint8_t* XB_THISCALL FxDiscDomain_Init(uint8_t* self, const float* centre, const float* normal, float radius);
uint8_t* XB_THISCALL FxCircleDomain_Init(uint8_t* self, const float* centre, const float* normal, float radius);
// 0x00098820: a particle domain by four-character id or name ("1dBumpDomain" ... "UVAFrame"),
// its parameters from the list's entries (value in word 1). Null for "NULL"/unknown/failure.
uint8_t* XB_CDECL FxDomain_Create(const char* name, const uint32_t* id, const uint8_t* list);

// 0x00097cd0: a particle action by four-character id or class name (35 kinds, each a 0x1c-byte
// FxAction* object). Null for unknown kinds or a failed allocation.
uint8_t* XB_CDECL apsParseActions(const char* name, const uint32_t* id);

// 0x0023e6c0 (stream in EAX; name, zone): an effect file: header, matrix and scale, stages
// (renderer from its parameter list, particle actions with their parameters and domains),
// emitters. Null when an action parameter has an unknown type. Entered through the _entry.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FX_LoadEffectFile(uint32_t* stream, const char* name, uint32_t zone);
extern "C" void FX_LoadEffectFile_entry(void);
// 0x00291140 (size in EAX, data in ECX, zone in EBX; streams, name; callee pops 8): the effect
// loaded from memory and stored in the zone's effect map. Returns EAX as the original leaves it
// (MemStream_Destroy's flag byte over the slot address, or 0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FX_LoadEffectFromStream(int32_t size, const uint8_t* data, uint32_t zone,
                                                                     uint32_t streams, const char* name);
extern "C" void FX_LoadEffectFromStream_entry(void);

// The particle memory pools (fx_load.cpp; slot table +0x1c of 8-byte entries {.., u16 next
// free (+2), 24-bit tag + flag byte (+4)}, slot size +0x14, base +0x18, free head +0x20):
// 0x0009b6e0 Init, 0x0009b710 SetSlotTag (returns the address, 0 when unchanged or too
// large), 0x0009ba60 FreeSlotTable, 0x0009bc50 / 0x0009bce0 Release / SetSlotTag over a pool
// set, 0x0009bea0 Release in the global set (0x00909f08), 0x0009c120 the set destroyed.
uint8_t* XB_THISCALL FxPool_Init(uint8_t* self, uint32_t slots);
uint32_t XB_THISCALL FxPool_SetSlotTag(uint8_t* self, int32_t address, int32_t tag);
void XB_THISCALL FxPool_FreeSlotTable(uint8_t* self);
uint32_t XB_THISCALL FxPools_Release(uint8_t* self, int32_t address);
uint32_t XB_THISCALL FxPools_SetSlotTag(uint8_t* self, int32_t address, int32_t tag);
uint32_t XB_CDECL FxPools_ReleaseGlobal(int32_t address);
void XB_THISCALL FxPools_Destroy(uint8_t* self);
// 0x0009d0e0: the block at +4 freed, 0x00909908 cleared. 0x0009e420: out = 1.5 x the xyz at
// src +0x60, w 1. 0x0009e490: a node onto the scene's opaque (key < 0: -key) or translucent
// (depth) particle list (+0x304/+0x30c, +0x308/+0x310). 0x0009cd00: the particle shader class
// (vtable 0x007c66f4) on the shader list. 0x0009ced0: its name "aeps_simple". 0x0009f660:
// an effect pool's slot table, elements (0xd0 each, empty destructor) and slots released.
void XB_THISCALL FxSystems_FreeBlock(uint8_t* self);
float* XB_CDECL FxVec3_ScaledHalfExtent(float* out, const uint8_t* src);
void XB_THISCALL FxNode_AddToScene(const uint8_t* self, uint8_t* node, float depth);
uint8_t* XB_CDECL FxShaderClass_Create();
uint32_t* XB_STDCALL FxSimpleShader_GetName(uint32_t* out);
void XB_THISCALL FxEffectPool_Release(uint8_t* self);

// 0x0009b830: a pool's statistics (18 words: counts, percentages, bytes, slack of used slots).
void XB_THISCALL FxPool_GetStats(const uint8_t* self, int32_t* stats);
// 0x0009bd40 / 0x0009bd80: every pool's statistics taken (discarded), the set's counters
// (+8..+0x14) cleared; the second for the global set 0x00909f08.
void XB_THISCALL FxPools_ResetStats(uint8_t* self);
void XB_CDECL FxPools_ResetGlobalStats();
// 0x0009c3c0: the action's object list (+0xc) without its null entries.
void XB_THISCALL FxAction_Compact(uint8_t* self);
// 0x0009c470: the action base's destructor (objects at +0xc deleted, both arrays freed).
void XB_THISCALL FxAction_Destroy(uint8_t* self);
// 0x0009ca90: every object of a pointer array deleted (slot 3 with 0, then freed), the array
// freed; +8 cleared.
void XB_THISCALL apsPtrVector_DeleteAll(uint8_t* self);
// 0x0009d000: a log entry {code, printf text} (at most 8 of 0x104 bytes at log[1]).
void XB_CDECL apsLog_Printf(int32_t* log, uint32_t code, const char* format, ...);
// 0x0009f3c0: every live emitter (+4 array, its index at +0xac) freed: particles, pool slot
// (0x00909f64 + 8), the array kept dense. 0x0009f450: then the effect pool and the array freed.
void XB_THISCALL FxEmitters_FreeAll(uint8_t* self);
void XB_THISCALL FxEmitters_Destroy(uint8_t* self);

// 0x0009cf00: view depth (lane z) of the point at +0x50. 0x0009d840: object light selection for
// a sphere (xyz, radius). 0x0009e0c0: out = the vector through the matrix's 3x3 rows, w 0.
float XB_THISCALL FxEffect_ViewDepth(const uint8_t* self);
void XB_CDECL FxLightSet_SelectObjectLights(uint8_t* set, const float* sphere);
float* XB_CDECL FxMatrix_RotateVector(float* out, const float* matrix, const float* v);
// 0x0009d040: the log object (0x00909908): eight empty 0x104-byte entries. Returns self.
uint8_t* XB_THISCALL FxLog_Init(uint8_t* self);
// 0x0009d110: the particle vertex buffers (two, `quads` x 0x60 bytes; 0x00909f18/20) and the
// shared quad index buffer (0x00909f28: 0 1 2 2 1 3 per quad).
void XB_CDECL FxParticleBuffers_Init(int32_t quads);
// 0x0009f4c0: a pointer array's capacity (the old block freed). False on failure.
uint32_t XB_THISCALL apsPtrVector_Reserve(uint8_t* self, int32_t capacity);

// 0x0009ef90 (callee pops 0x1c): a particle node's textures (+0x70..+0x78, all or none),
// sphere (+0x50), matrix (+0x10, identity when null), scale (+0x60) and flags (+0x7c: 2 pre-sorted
// kind 0x800, 1 when the op's slot 4 says so, 8 when the sphere reaches past the scene's far
// distance +0x388). Returns the view depth (ST0).
float XB_THISCALL FxOp_SetupNode(const uint8_t* op, uint8_t* node, uint32_t texture_a, uint32_t texture_b,
                                 const uint32_t* textures, const float* sphere, const uint32_t* matrix,
                                 const uint8_t* scale_source);
// The render ops' AddToList (vtable slot; callee pops 0x18): frustum test of the sphere, a 0x90-byte
// node in frame memory (vtables below), FxOp_SetupNode, FxNode_AddToScene. 1 added, 0 culled, 2 no
// memory. 0x0009e560, 0x0009e6f0, 0x0009e830, 0x0009e970, 0x0009eab0, 0x0009ebf0, 0x0009edc0, 0x0009f0f0.
uint32_t XB_THISCALL FxOpdd_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                      const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOp1dd_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                       const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOp9d5_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                       const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOpd5_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                      const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOp15b_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                       const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOp5b_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                      const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOpGrid_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                        const uint32_t* matrix, const uint8_t* scale_source);
uint32_t XB_THISCALL FxOp53_AddToList(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere,
                                      const uint32_t* matrix, const uint8_t* scale_source);

// 0x0009cd30 / 0x0009e240: FxOp31 / FxOp1d5 AddToList (callee pops 0x18): like the others, the
// first only when the op has a mesh (+0xc; else 2), with a light set (+0x84, default 0x00902e40)
// on the translucent list; the second with the scale at +0x60 on the opaque list (key -1).
// The far test runs on the x87.
uint32_t XB_THISCALL FxOp31_AddToList(const uint8_t* op, uint32_t a0, uint32_t a1, const uint32_t* a2, const float* sphere,
                                      const uint32_t* matrix, uint32_t light_set);
uint32_t XB_THISCALL FxOp1d5_AddToList(const uint8_t* op, uint32_t a0, uint32_t a1, const uint32_t* a2, const float* sphere,
                                       const uint32_t* matrix, const uint8_t* scale_source);

// 0x0009db30 / 0x0009dbb0: vertex shader constant base + 0x60: a distance fade {scale, offset}
// from near/far distances and alphas (range 1 when equal) / a depth range {1/(f-n), -n/(f-n)}
// scaled by 2^24.
void XB_CDECL FxShader_SetFadeConstant(uint32_t base, float near_distance, float far_distance, float near_alpha,
                                       float far_alpha);
void XB_CDECL FxShader_SetDepthRangeConstant(uint32_t base, float near_depth, float far_depth);
// 0x0009dc30: up to four object lights for a particle sphere: directions (negated, through the
// matrix's rotation), colours r[4] g[4] b[4], then the set's ambient.
void XB_CDECL FxNode_ComputeObjectLights(float* directions, float* colors, const float* matrix, uint8_t* set,
                                         const float* sphere);

}  // namespace game
