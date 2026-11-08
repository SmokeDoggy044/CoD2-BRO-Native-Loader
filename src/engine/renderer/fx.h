#pragma once

// The effect pool (FX_*): effects of 0x1c bytes, handed out from a free list.

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct FxEffect {
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t unknown08;
    uint32_t unknown0c;
    uint32_t unknown10;
    uint16_t unknown14;
    uint16_t unknown16;
    int32_t unknown18;  // FX_AllocEffect: -1
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(FxEffect) == 0x1c);
#endif

// 0x00029560 (thiscall): stops the effect's system (+0x04, FX_StopEffect, not immediate) and
// clears it; with an id (+0x18 not -1) under that id (0x008bb764) and the particle random
// reset (FxRandom_Reset/Restore).
void XB_THISCALL FX_ReleaseEffectHandle(FxEffect* effect);

// 0x0023b970: takes an effect off the free list and clears it; null when the pool
// is not set up or empty.
FxEffect* XB_CDECL FX_AllocEffect();
// 0x0023b5c0: frees every effect of the effect list (0x00988ce8) with id (+0x18) `id`,
// then stops the particle library's active systems with that id.
void XB_CDECL FX_KillEffectsOnEntity(int32_t id);
// 0x0009f220 (thiscall; ECX unread): whether the particle pool 0x00909f64 has `count`
// free items (+8 - +0xc).
uint32_t XB_THISCALL apsPool_HasRoom(uint32_t unused, int32_t count);
// 0x00094890: whether a particle system (pool 0x00909994) and the definition's elements
// (+0x56) fit.
uint32_t XB_CDECL FX_HasParticleRoom(const uint8_t* definition);
// 0x00096460 (thiscall, stdcall): the value (+8) of the definition's element (0x3c bytes
// each at +0x50).
void XB_THISCALL FxEffectDef_SetElementValue(uint8_t* definition, uint32_t element, float value);
// 0x0023c040: a new effect running `definition` at `matrix`: without a free effect slot,
// room for the particles, or over 80% of the manager's budget, an evictable effect is
// stopped first (none spawned without a slot); the system is created under the current
// effect id (0x009717f0), the owner (+0xc/+0x10/+0x14, flag 1 of +0x16) set when given,
// FLT_MAX element values resolved and the effect added to the effect list (0x00988ce8).
FxEffect* XB_CDECL FX_SpawnEffect(uint8_t* definition, const float* matrix, uint32_t owner_a, uint32_t owner_b,
                                  uint32_t owner_c, uint8_t owner_flag);

// 0x00236b40: walks the active effects (0x00988804, count 0x0098880c) from the
// newest down and returns the first one whose definition (+4) has a priority
// (flags +0x50 >> 25) above `effect`'s (+0x68) and the evictable flag 0x1000000;
// null at the first one with a priority not above it, or with fewer than two
// active effects (the single entry is never looked at, as in the original).
uint8_t* XB_CDECL FX_FindEvictableEffect(const uint8_t* effect);

// 0x00238540: an effect back onto the free list (at most 240 entries): its handle is
// released first (FX_ReleaseEffectHandle), then its first word, with the free count in the low
// half, becomes the entry. ESI = effect (may be null): entered through
// FX_FreeEffect_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_FreeEffect(FxEffect* effect);
extern "C" void FX_FreeEffect_entry(void);

// 0x00238390: quicksort (median of three, down to 5 entries) of the active effects
// 0x00988804[lo..hi] by their definition's flags (+0x50, unsigned; no definition =
// 0xffffffff), recursing on the left part.
void XB_CDECL FX_SortEffects_r(int32_t lo, int32_t hi);

// 0x0023b420: the effect lights of the list 0x00988714 (count 0x0098871c): a lit one
// (+0xc) adds a point light at its position (radius 0x008db380, inner 0x0095f6ec,
// colour 1, 0x0083eb58, 0) and is cleared for the next frame; the rest are freed
// (Z_Free) and removed from the list.
void XB_CDECL FX_FreeFinishedEffects();

// Effect instances by id (sounds, rumble): {count, instance[]}; an instance is
// {component count, component[]} with its id at +0x4c and flags at +0x50.
struct FxIdList {
    uint32_t count;
    uint8_t* items[1];
};

// 0x00299150 (list in ESI, id in EDI): whether the first instance with the id is
// still flagged or has components.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL FX_IsEffectActive(const FxIdList* list, uint32_t id);
extern "C" void FX_IsEffectActive_entry(void);
// 0x00296ab0: stops (component slot 3) the first instance with the id.
void XB_REGS3 XB_CDECL FX_StopEffectById(uint32_t id, uint32_t unused_edx, FxIdList* list);
// 0x00296b00: component slot 5 (value) on every instance with the id.
void XB_STDCALL FX_SetEffectParamById(FxIdList* list, uint32_t id, uint32_t value);
// 0x00296d10: releases pooled components to the free list (0x0095f5b4), deletes the rest.
void XB_STDCALL FX_FreeEffectComponents(uint32_t* components);

// 0x00296c00 (pool in EDI): updates the fading released components, deletes the faded ones.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_UpdateFadingComponents(uint8_t* pool, uint32_t delta);
extern "C" void FX_UpdateFadingComponents_entry(void);

// 0x0029b620 (list in ESI): frees every instance with the id (components, then the
// small heap 0x0095f5bc); the last entry takes its place.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_FreeEffectsById(FxIdList* list, uint32_t id);
extern "C" void FX_FreeEffectsById_entry(void);
// 0x00095210: the effect (flag 4 at +0x4e first when `immediate`) through 0x00094ad0,
// then handed to 0x0009b770 on the effect system (0x00909994) + 8.
void XB_CDECL FX_StopEffect(uint8_t* effect, uint32_t immediate);
// 0x00241200 (origin in ESI): FX_SpawnEffect (0x0023c040) of `effect` with an identity
// matrix moved to the origin; returns FX_SpawnEffect's result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FX_PlayEffectAt(uint32_t effect, const float* origin);
extern "C" void FX_PlayEffectAt_entry(void);

// 0x0023c300: the draw list (0x00988804) rebuilt from the effects (0x00988ce8) that
// have a definition (+4), then sorted (FX_SortEffects_r); clears 0x0095f6e8.
void XB_CDECL FX_SortEffects();
// 0x0023b6d0: every effect back to the free list (FX_FreeEffect), then both lists
// emptied and their memory freed.
void XB_CDECL FX_ClearEffects();
// 0x00236aa0: Z_Free of every effect light (0x00988714), the list emptied.
void XB_CDECL FX_FreeAllEffectLights();
// 0x0029b860 (instances in ESI): frees every instance (components, then the small heap
// 0x0095f5bc), deletes every object of the second list at +0x804 (vtable slot 0, 1),
// then FX_ClearEffects.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_FreeAllEffects(FxIdList* instances);
extern "C" void FX_FreeAllEffects_entry(void);
// 0x00095fd0: out = a * b for 4x4 row matrices with an affine last column (rows 0-2
// rotate by b, row 3 moved by b's row 3); packed, all four lanes.
void XB_CDECL FxMatrix_Multiply(float* out, const float* a, const float* b);
// 0x00094800: sets bit 0 of +0x4c (stopping).
void XB_THISCALL FxSystem_Stop(uint8_t* system);
// 0x00094ef0: 1 when a stopping system has no live particles (emitters' +0xc0), or when
// none of its definition's key times (+0x48 -> +0x50, 0x3c each, +8) lies past its age
// (+0x44).
uint32_t XB_THISCALL FxSystem_IsFinished(const uint8_t* system);
// 0x00094fc0: the draw sort key (+0x50): nearest emitter depth (+0xb8) in the low 24
// bits, bit 24 = not bit 1 of +0x4e, |definition +0x68| from bit 25.
void XB_THISCALL FxSystem_UpdateSortKey(uint8_t* system);
// 0x000972c0: an emitter's matrix (+0x10, 64 bytes); +0x50 keeps the previous position
// (+0x40), or takes the new one when x y z were all 0.
void XB_THISCALL FxEmitter_SetMatrix(uint8_t* emitter, const float* matrix);
// 0x00095600: a particle system placed: its matrix (+0) = its local one (+0x48) times
// `matrix`, then every emitter (+0x54, count int16 +0x5a) gets it.
void XB_THISCALL FxSystem_SetMatrix(uint8_t* system, const float* matrix);
// 0x00095990: the centre of a box {min[4], max[4]}: (max + min) * 0.5, packed; returns out.
float* XB_THISCALL FxBox_Center(const float* box, float* out);
// 0x00095d60: half the box's diagonal (RSQRTSS + a Newton step; 0 for an empty box).
float XB_THISCALL FxBox_Radius(const float* box);
// 0x00095e20: {centre, radius} of a box; returns out.
float* XB_THISCALL FxBox_Sphere(const float* box, float* out);
// 0x00095e80: draws an emitter through its renderer (+0, vtable slot 1) with its
// particles (+0xbc, count +0xc0), bounds (+0x60 box) and, with flag 2 of +0xae, its
// matrix (+0x10); 3 without a renderer, 2 when empty, hidden (flag 1 clear) or flag 4.
uint32_t XB_THISCALL FxEmitter_Draw(uint8_t* emitter, uint32_t lights);
// 0x00094f60: every emitter's FxEmitter_Draw; bit 1 of +0x4e = one of them returned 1.
void XB_THISCALL FxSystem_Draw(uint8_t* system, uint32_t lights);
// 0x000965a0: bit 0 of the definition's emitter entry `index` (+0x50, 0x3c each, +0xc):
// the emitter starts visible.
uint32_t XB_THISCALL FxSystemDef_IsEmitterVisible(const uint8_t* definition, int32_t index);
// 0x0009c330: value `index` of a parameter block (+4, floats) set.
void XB_THISCALL FxParamBlock_Set(uint8_t* block, uint32_t index, float value);
// 0x00094990: the system's parameter overrides (+0x5c, {block, index, value} 0xc each,
// count int16 +0x62) swapped with the blocks' values: called before and after an update.
void XB_THISCALL FxSystem_SwapParams(uint8_t* system);
// 0x000971a0: an emitter's particle memory (+0xbc) back to the particle heap (0x008bb760,
// slot 2), its count (+0xc0) off the live total (0x00909ec0); the argument is unread.
void XB_THISCALL FxEmitter_FreeParticles(uint8_t* emitter, uint32_t unused);
// 0x0009b770: an item of a fixed-size pool freed (base +0x18, size +0x14, 8-byte slots
// +0x1c, free list head int16 +0x20, used count +4).
void XB_THISCALL apsMemoryPool_Free(uint8_t* pool, uint8_t* item);
// 0x0009c6e0: a 4-key step curve at `t`: the value (+0xc) of the first key whose time
// (+8, 8 bytes each) lies past t, else +4 * +0 (x87); 0 without keys (+0x28 clear).
float XB_THISCALL FxCurve_Evaluate(const uint8_t* curve, float t);
// 0x00097200: starts a pass over an emitter's particles: begin/end (+0xbc, count +0xc0,
// stride +0x88) out and into 0x00909ec8/cc, the time into 0x00909ed0, the bounds
// accumulator (0x00909ee0) emptied; 0 without particles.
uint32_t XB_THISCALL FxEmitter_BeginParticles(const uint8_t* emitter, uint8_t** begin, uint8_t** end, float time);
// 0x00097580: a particle range advanced: each particle's age (byte offset +0x95) plus the
// pass time (0x00909ed0), the bounds accumulator (0x00909ee0 min, 0x00909ef0 max) grown
// by its extent (+0x84 flag 2: size +0x8d * sqrt 2; flag 4: sizes +0x8e/+0x93; else 1);
// returns the next range, always empty.
void XB_THISCALL FxEmitter_AdvanceParticles(uint8_t* emitter, uint8_t** begin, uint8_t** end);
// 0x00097100: the particles killed in the last pass (0x00909ed4 of them, list from
// 0x00909ba0) refilled from the end of the array; count +0xc0 and 0x00909ec0 lowered.
void XB_THISCALL FxEmitter_CompactParticles(uint8_t* emitter);
// 0x0009f880: the 8 corners of a box {min[4], max[4]} (16 bytes each): min, min + x/y/z
// of the extent, max - x/y/z, max (packed, every lane).
void XB_CDECL FxBox_GetCorners(const float* box, float* out);
// 0x00097910: an emitter's box (+0x60) moved to world space: the min/max of its corners
// through Mat44_TransformPoint with the emitter matrix (+0x10).
void XB_THISCALL FxEmitter_TransformBounds(uint8_t* emitter);
// 0x000979d0: ends a particle pass: the bounds accumulator (0x00909ee0) into +0x60, moved
// to world space (0x00097910) with flag 2 of +0xae, then FxEmitter_CompactParticles.
void XB_THISCALL FxEmitter_EndParticles(uint8_t* emitter);
// 0x000973b0: an emitter's depth (+0xb8: camera distance of its box minus the radius, at
// least 0) and flag 8 of +0xae (box sphere in the frustum; also set without a scene).
void XB_THISCALL FxEmitter_UpdateVisibility(uint8_t* emitter);
// 0x0009f350: an emitter removed from the emitter list (0x00909f60: items +4, count
// int16 +0xa; the last one takes its slot +0xac) and freed to the pool (0x00909f64 +8).
void XB_THISCALL FxEmitterList_Remove(uint8_t* list, uint8_t* emitter, uint32_t unused);
// 0x0009f2d0: a new emitter (0xd0 bytes from the pool, apsGroup_Create) appended to the
// emitter list; 0 when either fails.
uint8_t* XB_THISCALL FxEmitterList_Add(uint8_t* list, uint32_t capacity, uint32_t renderer, uint8_t* block, uint32_t flag);
// 0x0009b9c0 / 0x0009bec0: the particle random index (0x0090993c) to 0 for a seeded
// update, and back (0x00909f04) after it.
void XB_CDECL FxRandom_Reset();
void XB_CDECL FxRandom_Restore();
// 0x00094ad0: a particle system's emitters removed (FxEmitterList_Remove), its parameter
// overrides (+0x5c) and emitter array (+0x54) freed to the particle heap, its
// definition's use count (+0x48 -> +0xb0) lowered.
void XB_THISCALL FxSystem_Destroy(uint8_t* system);
// 0x00095250: every system of a list (items +4, count +0x1e4) updated with its seeded
// random (effect id +0x64); those FxSystem_Update ends are destroyed and freed to the
// pool (0x00909994 +8), the last one taking their place.
void XB_THISCALL FxSystemList_Update(uint8_t* list, float time);
// 0x00236ae0: the global systems (0x009099a8) updated to the effect clock and drawn with
// an ambient-only light set (0.4).
void XB_CDECL FX_DrawGlobalSystems();
// 0x0009c7b0: an emitter definition's update: FxEmitter_UpdateVisibility, the step
// (age - last update +0xb0, at most 0.3; hidden emitters wait +0xb4 from a depth curve),
// then its actions (+0, count int16 +6) inside their time windows (+4 {start, end}):
// emitter-level ones (+0x14 clear, skipped while stopping), the rest over every particle
// range. `delta` is unread.
void XB_THISCALL FxEmitterDef_Update(uint8_t* definition, uint8_t* emitter, uint8_t* system, float age, float delta,
                                     uint32_t stopping);
// 0x0009c730: the definition's end-of-life action (+8) over every particle range (time 0,
// delta 8888.89).
void XB_THISCALL FxEmitterDef_Finish(uint8_t* definition, uint8_t* system, uint8_t* emitter);
// 0x00094c60: a particle system advanced to `time`: emitters whose definition entry
// (+0x48 -> +0x50, 0x3c each) has started and not ended are spawned, dead ones (flag 4 of
// +0xae) removed, the rest updated (entry +0x14: 0x0009c7b0, at the end time 0x0009c730);
// age +0x44. 0 when an emitter was removed, else 1 (also when time lies before the start).
uint32_t XB_THISCALL FxSystem_Update(uint8_t* system, float time);
// 0x0009d2f0: the particle code's double-buffered block (0x00909f18, index 0x00909f2c)
// flipped: the other one current (0x00909f3c), its fill counters (0x00909f38/48) 0.
void XB_CDECL FxBuffers_Flip();
// 0x000941b0: the particle code's camera from a view matrix: its columns (x times
// `scale`, -1 from FX_DrawEffects) into 0x00909950..0x0090997c, the heading (0x00909980,
// an atan2 series: kept when both inputs are 0), then FxBuffers_Flip.
void XB_CDECL FxSystem_SetView(const float* view, float scale);
// 0x00246810: R_RenderView's effects pass: finished effects freed, the effect clock
// (0x0095f6e0) advanced by the frame step (clamped to 0.05; 0 while paused), the view
// matrix to the particle code, then per live effect (0x00988ce8, count 0x00988cf0): a
// finished particle system releases the effect; otherwise the system is placed (its own
// matrix, a bolt, or the owner entity), updated (seeded per effect id when +0x18 is
// set) and drawn with a fresh light set. Then the global systems, the freed entries
// dropped and the list sorted.
void XB_CDECL FX_DrawEffects();
// 0x002463c0 (out in EDI; AL): the placement {origin, axis[3]} of bone `bone` of the
// DObj in slot `handle` (0x00981418, generation in the upper bits; 0 for a stale one):
// the owner's placement (0x00284a80) for bone < 0, else the bone matrix (skeleton made
// if missing) times it; 0 past the bone count. A debug cvar (0x0092e9e0) draws the axes.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL FX_GetBoltMatrix(float* out, uint32_t handle, int32_t bone);
extern "C" void FX_GetBoltMatrix_entry(void);
// 0x00247190 (origin in EAX): FX_SpawnEffect with the entity's bone matrix
// (FX_GetBoltMatrix, origin preset to `origin`, axes zero) when `entity` is set; 0 when
// the bone is not found. The matrix rows are rotated: axes first, origin last, each
// with its x again in w.
uint32_t XB_REGS1 XB_CDECL FX_PlayBoltedEffect(const float* origin, uint32_t effect, uint32_t entity,
                                               uint32_t owner, uint32_t bone);

// 0x00238570 (stream in EDI, out in ESI): one effect parameter, a type word then its
// value: 0 texture name, 1 mesh name, 2 uint, 3 int, 4 float, 5 three floats, 8 uint;
// other types break into the debugger and give {2, 0}. Returns `out`.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL FX_ParseEffectTexture(uint32_t* stream, uint32_t* out);
extern "C" void FX_ParseEffectTexture_entry(void);

// 0x0023b800: once, the pool of 0xf0 effects (0x0095f6e4; Heap_Alloc16 with a count
// word in front, each built by 0x00029650); every effect's index goes into both halves
// of its first word, which is pushed on the free list (0x00988810).
void XB_CDECL FX_InitEffectPool();

// 0x002a5980 (list in EAX, stdcall): fading components first, then FX_UpdateEffect
// (0x0029b8f0) of every instance; one without flag 2 at +0x50 whose first word is 0 is
// freed (the last entry takes its place). Then FX_SpawnQueuedEffect (0x002a5810) of
// each queued spawn (+0x920, 0x210 each, count +0x910), and the queue emptied.
// Entered through FX_UpdateEffects_entry: every call runs at the original's stack
// position (title_call_at_regs).
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_UpdateEffects_body(uint32_t entry, FxIdList* list, uint32_t delta);
extern "C" void FX_UpdateEffects_entry(void);

// 0x00296c90 (components in ESI): deletes the components of a {count, component[]} list
// whose slot 8 reports them finished, unless flag 1 of their +0x10 keeps them (the last
// takes the place of each).
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_PruneComponents(uint32_t* components);
extern "C" void FX_PruneComponents_entry(void);
// 0x0029b8f0 (effect in EBX, delta on the stack, stdcall): unless flag 2 of +0x50, pruned
// first with flag 1, every component's Update (slot 1, delta); one that then reports done
// (slot 6) ends its entity's sound notify (Entity_EndSoundNotify) and is deleted.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_UpdateEffect(uint32_t* effect, uint32_t delta);
extern "C" void FX_UpdateEffect_entry(void);

// 0x00284a80 (obj EAX, origin ECX, axis EBX): the placement of the DObj's owner (+0x88:
// origin +0x60, angles +0x70 -> forward/left/up); without an owner the identity placement
// at 0x00972fd8.
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_GetOwnerPlacement(const uint8_t* obj, float* origin, float* axis);
extern "C" void DObj_GetOwnerPlacement_entry(void);

// 0x002a5810 (list in EDI, spawn record on the stack, stdcall): finds the list item whose
// +0x4c equals the record's +0x1f8; when the record's entity handle (+0x1f0: index in the low
// 12 bits, generation above; table 0x0097a968, 8 bytes each) is live, the four
// FX_Add*Components run with the squared distance to the view entity (0x0095f534 + 4,
// origin +0x140); none added -> Entity_EndSoundNotify. Clears flag 2 of the item's +0x50.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_SpawnQueuedEffect_body(uint32_t entry, FxIdList* list, uint8_t* spawn);
extern "C" void FX_SpawnQueuedEffect_entry(void);
// 0x002a5940 (list in EAX; called by FXCmd_Alloc): the queue part of FX_UpdateEffects.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_FlushSpawnQueue_body(uint32_t entry, uint8_t* list);
extern "C" void FX_FlushSpawnQueue_entry(void);

// The particle library's growable arrays: storage from the particle heap (0x008bb760).
struct ApsArray {  // 8 bytes
    uint8_t* data;
    int16_t capacity;
    int16_t count;
};
// 0x0009ba90: an item of a fixed-size pool taken from the free list (+0x20), its slot
// marked used with `size`; the pool's used count (+4), peak (+8) and smallest/largest
// size (+0xc/+0x10) updated; null with a log line when the pool is empty.
uint8_t* XB_THISCALL apsMemoryPool_Alloc(uint8_t* pool, uint32_t size);
// 0x0009f570: a pointer appended to an array grown by 4 (by 1 below 4 items) with the
// seeded flag (0x00909948) 0 meanwhile; 0 when the heap is out.
uint32_t XB_THISCALL apsPtrVector_PushBack(ApsArray* array, const uint32_t* item);
// 0x000957d0: an array's storage back to the heap (effect id -1, seeded flag 1 meanwhile)
// and the array emptied.
void XB_THISCALL apsArray_Free(ApsArray* array);
// The effect arrays of pointers (0x00095830 Alloc, 0x00095880 Resize in place,
// 0x00095a90 Reserve, 0x00095b10 PushBack: grows by 4, by 1 below 4 items) and of
// 12-byte parameter overrides (0x000958e0, 0x00095930, 0x00095bc0, 0x00095c70): heap
// calls with effect id -1 and seeded flag 1 meanwhile; the Alloc/Resize count argument
// after the first is unread. 0 when the heap is out (PushBack: also without storage).
uint8_t* XB_THISCALL apsPtrArray_Alloc(ApsArray* array, uint32_t count, uint32_t unused);
uint32_t XB_THISCALL apsPtrArray_Resize(ApsArray* array, uint32_t count, uint32_t unused);
uint32_t XB_THISCALL apsPtrArray_Reserve(ApsArray* array, int32_t capacity);
uint32_t XB_THISCALL apsPtrArray_PushBack(ApsArray* array, const uint32_t* item);
uint8_t* XB_THISCALL apsParamArray_Alloc(ApsArray* array, uint32_t count, uint32_t unused);
uint32_t XB_THISCALL apsParamArray_Resize(ApsArray* array, uint32_t count, uint32_t unused);
uint32_t XB_THISCALL apsParamArray_Reserve(ApsArray* array, int32_t capacity);
uint32_t XB_THISCALL apsParamArray_PushBack(ApsArray* array, const uint32_t* item);
// 0x00097a10: an emitter set up: renderer +0, the 9-word block at +0x84 (stride +0x88),
// capacity +0xa8, empty box, flags +0xae (1, or 3 with `flag`), capacity * stride bytes
// of particles (+0xbc, 128-aligned) and an identity matrix (+0x10); 0 with a log line
// when the heap is out.
uint32_t XB_THISCALL apsGroup_Create(uint8_t* emitter, uint32_t capacity, uint32_t renderer, const uint32_t* block,
                                     uint32_t flag);
// 0x00097ba0: out = row 3 + p.x * row 0 + p.y * row 1 + p.z * row 2 (packed, every lane).
float* XB_CDECL Mat44_TransformPoint(float* out, const float* matrix, const float* point);
// 0x00095470: a particle system started from its definition at `time`: identity matrix,
// effect id (0x008bb764) +0x64, one empty emitter slot (+0x54) per element (+0x56), the
// parameter overrides (+0x5c) with each block's current value; the definition's created
// (+0xac, counted twice), live and peak counts (+0xb0/+0xb4) raised. 0 when an array
// can't grow (failure counts +0xb8/+0xbc).
uint32_t XB_THISCALL apsEffect_Init(uint8_t* system, uint8_t* definition, float time);
// 0x00095670: a particle system (0x70 bytes from the pool 0x00909994 + 8) started with
// apsEffect_Init; destroyed again and null when that fails.
uint8_t* XB_CDECL apsEffect_Create(uint8_t* definition, float time);

// 0x0009bba0: `size` bytes from a set of pools ({pools, count}; largest request +0x18):
// the pool with exactly that item size (+0x14) unless it is full and another was found,
// else the one with the least slack; counts and bytes of successes (+0x10/+8) and
// failures (+0x14/+0xc). Null for size 0 or no fitting pool; the 2nd argument is unread.
uint8_t* XB_THISCALL apsPoolSet_Alloc(ApsArray* set, int32_t size, uint32_t unused);
// 0x0009be80: apsPoolSet_Alloc from the global pool set (0x00909f08).
uint8_t* XB_CDECL apsAlloc(int32_t size, uint32_t unused);

// 0x0009fa80: a matrix from a unit quaternion {x, y, z, w} (scaled by sqrt 2 first) and
// a translation (4 floats, row 3); each rotation row's 4th lane repeats its first.
void XB_CDECL Mat44_FromQuatTranslation(float* out, const float* quat, const float* translation);
// 0x000956e0: a particle system created (apsEffect_Create) into the active list
// (0x009099ac, count 0x00909b8c, at most 0x78) and placed with a quaternion (or none:
// identity) and position; destroyed again while 0x009099a8 is set. Null on failure.
uint8_t* XB_CDECL apsEffect_Spawn(const float* quat, const float* position, uint8_t* definition, float time);

// 0x0009c210: `count` free pool slots (8 bytes: index, next = 0xffff, size, used flag)
// from the particle heap, aligned to `align`.
uint8_t* XB_CDECL apsMemoryPool_AllocSlots(int32_t count, uint32_t align);
// 0x0009b9d0: a pool of `count` items of `item_size` bytes at `items`: slots (+0x1c,
// allocated when missing) chained into the free list (+0x20). Returns the slots (null
// for count 0).
uint8_t* XB_THISCALL apsMemoryPool_Init(uint8_t* pool, int32_t count, uint32_t item_size, uint8_t* items);
// 0x0009bf50: a pool of `count` items of `item_size` bytes in one heap block (header and
// slots 128-aligned, then the items) added to a pool set; 0 for no bytes or out of heap.
uint32_t XB_THISCALL apsPoolSet_AddPool(ApsArray* set, int32_t count, int32_t item_size);
// 0x0009c050: apsPoolSet_AddPool to the global pool set (0x00909f08).
uint32_t XB_CDECL apsAddPool(int32_t count, int32_t item_size);
// 0x0009c620: a pointer array set to `count` items (storage grown to exactly that when
// needed; seeded flag 0 around heap calls); 0 when the heap is out.
uint32_t XB_THISCALL apsPtrVector_Resize(ApsArray* array, int32_t count);
// 0x0009cb60: a pointer put first in an array grown by one. With room left the original
// only moves the items up one place (item not stored, count unchanged).
uint32_t XB_THISCALL apsPtrVector_PushFront(ApsArray* array, const uint32_t* item);
// 0x0009cb00: an effect element added to a list ({array, +8 first 'SpDe' element}):
// first when its +0x14 is 0, else last; an 'SpDe' element (vtable slot 1 type) is
// remembered at +8 (when put first, or when none is yet).
void XB_THISCALL apsElementList_Add(uint8_t* list, uint8_t* element);
// 0x0009c9e0: the list's spawn-on-death element's effect (+8 -> +4 -> +8) spawned at `position`/`quat` at
// the effect time (0x00909998); null without one.
uint8_t* XB_THISCALL apsElementList_SpawnDeathEffect(uint8_t* self, const float* quat, const float* position);
// 0x000a1730 (vtable 0x007c907c slot 0): every particle's timer (particle byte offset
// emitter +0xa0; started with config +0x10 while the +0x95 field is 0) counts down by
// `delta`; at 0 the config's effect (+0xc) spawns at the particle (rotation at +0x91
// with emitter flag 0x20) and the timer restarts with the period (+8).
void XB_THISCALL apsActionSpawnTimed_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                           uint8_t* system, float time, float delta);
// 0x000a1850 (vtable 0x007c9090 slot 0): on the death pass (delta 8888.89) the config's
// effect (+8) spawned at each particle.
void XB_THISCALL apsActionSpawnOnDeath_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                             uint8_t* system, float time, float delta);
// 0x00095300 (CG_Shutdown's tail): every system of a list destroyed and freed to the
// pool (0x00909994 +8), the list emptied.
uint32_t XB_THISCALL FxSystemList_DestroyAll(uint8_t* list);

// 0x000971d0: a particle queued for removal (ring of 200 at 0x00909ba0, next 0x00909ed4)
// when the emitter has particle memory (+0xbc); returns the next index (0 without).
uint32_t XB_THISCALL FxEmitter_QueueParticleKill(uint8_t* emitter, uint8_t* particle);
// 0x0009bee0: a pool's separately allocated slots (+0x1c, not the inline ones at +0x24)
// back to the heap; slots and items (+0x18) cleared.
void XB_THISCALL apsMemoryPool_FreeSlots(uint8_t* pool);
// 0x0009bff0: every pool of a set freed (slots, then the block), the set emptied.
uint32_t XB_THISCALL apsPoolSet_Clear(ApsArray* set);

// The particle action classes (vtables 0x007c8df0-0x007c90a4; slot 1 returns the
// class's four-character type, slot 0 is Apply). The three emitters (SrcA/Brst/RSpn)
// share Apply 0x000a2e40 and add slot 5, the number of particles to emit this frame
// (emitter +0x80 = their accumulator).
// 0x000a0e50 'SrcA': rate (+8) * delta particles, the fraction carried over.
int32_t XB_THISCALL apsActionSource_Count(uint8_t* self, uint8_t* emitter, float delta);
// 0x000a0e90 'Brst': config +8 particles once (accumulator -1 after).
int32_t XB_THISCALL apsActionBurst_Count(uint8_t* self, uint8_t* emitter, float unused);
// 0x000a0ec0 'RSpn': one particle per random interval (+0xc -> +0x40, slot 1) elapsed.
int32_t XB_THISCALL apsActionRandomSpawn_Count(uint8_t* self, uint8_t* emitter, float delta);
// Apply(begin, end, emitter, system, time, delta) of the others (particle fields: fx.cpp):
// 0x000a0970 'Life': particles past their lifetime queued for removal, the element
// list's (0x00909f10) spawn-on-death effect spawned at each first.
void XB_THISCALL apsActionLife_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a0f30 'AlFd': alpha = peak (+8) until lifetime * hold (+0xc), then fading to 0.
void XB_THISCALL apsActionAlphaFade_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a0fe0 'AFIO': alpha fading in to peak (+0xc) until +8, out from +0x10 (age fractions).
void XB_THISCALL apsActionAlphaFadeInOut_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a10a0 'RFIO': the same with each particle's own peak.
void XB_THISCALL apsActionRandomFadeInOut_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a1190 'LnSc' / 0x000a1260 'LScW' / 0x000a1330 'LScH': size / width / height += rate * delta.
void XB_THISCALL apsActionLinearScale_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionLinearScaleWidth_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionLinearScaleHeight_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a11e0 'ExSc' / 0x000a12b0 'EScW' / 0x000a1380 'EScH': times rate ^ delta (series on the x87).
void XB_THISCALL apsActionExpScale_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionExpScaleWidth_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionExpScaleHeight_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a1400 'Move': position += velocity * delta, angle += angular velocity * delta.
void XB_THISCALL apsActionMove_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a1b60 'PsMv': position += velocity * delta.
void XB_THISCALL apsActionPositionMove_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a14d0 'VlDr' / 0x000a1580 'AVDr' / 0x000a1600 'VAVD': velocity / angular velocity /
// angular velocity vector times exp(-rate * delta) (third-order series).
void XB_THISCALL apsActionVelocityDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionAngularDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionVectorAngularDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a16b0 'UVFr': texture frame advanced by rate (+0x10) * delta, wrapped into
// [first (+8), limit (+0xc)).
void XB_THISCALL apsActionUVFrame_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x000a4ac0: atan2(y, x) by the particle code's asin series (0 for 0, 0).
float XB_CDECL apsAtan2(float y, float x);
// 0x000a1fa0 'ATEX' / 0x000a20c0 'ATEY' / 0x000a21e0 'ATEZ': every particle's angle set to the
// screen heading of the emitter matrix's x / y / z row (none when it points at the view).
void XB_THISCALL apsActionAngleToEmitterX_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionAngleToEmitterY_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
void XB_THISCALL apsActionAngleToEmitterZ_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x000a3d80 'PAtr': velocities pulled toward a point (+8, in the emitter's space unless
// emitter flag 2) by strength (+0x14) * delta / distance^2 (distance^2 at least 0.001).
void XB_THISCALL apsActionPointAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a1bc0 'ClSh': colour (+0x8f) shifted from +0xc to +0x1c between the age fractions
// +8 and +0x18, clamped to [0, 1]; outside that window unchanged.
void XB_THISCALL apsActionColorShift_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x000a39f0 'MFxV': positions moved by a fixed velocity (+8) * delta; with +0x14 set the
// velocity is in the emitter's space (times its rotation's transpose).
void XB_THISCALL apsActionMoveFixed_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a3bb0 'Forc': velocities += force (+8) * delta, likewise in the emitter's space with +0x14.
void XB_THISCALL apsActionForce_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x0009f6c0: v (xyz) rotated in place by the unit quaternion q {x, y, z, w}.
void XB_THISCALL Quat_RotateVector(const float* q, float* v);
// 0x000a18f0 'OMov': position += velocity * delta; the rotation quaternion (+0x91)
// advanced by the angular velocity vector (+0x9a, rotated by it) * delta / 2 and renormalised.
void XB_THISCALL apsActionOrientedMove_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x000a1d20 'WPRf': particles behind a plane (normal +8, point +0x14) moving into it
// bounce: velocity -= normal * (1 + bounce (+0x20)) * speed into the plane.
void XB_THISCALL apsActionPlaneBounce_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);


// 0x000a4d30: out = matrix rows 0-2 times v's x, y, z (no translation; all four lanes).
float* XB_CDECL Mat44_TransformVector(float* out, const float* matrix, const float* v);
// 0x0009fc20: a rotation matrix with row 0 = forward (unit), row 2 = normalize(forward x up)
// times 0x00909984, row 1 = normalize(row 2 x forward) (before that scale), row 3 = {0, 0, 0, 1};
// returns out + 12.
float* XB_CDECL Mat44_FromDirection(float* out, const float* forward, const float* up);
// 0x000a0340: the unit quaternion {x, y, z, w} of a rotation matrix (trace or largest-diagonal
// case, square roots on the x87).
float* XB_CDECL Quat_FromMat44(float* out, const float* m);
// 0x000a4460: every particle's rotation quaternion (+0x91) turned to face along its velocity
// (in the emitter's space with emitter flag 2), -z as the up vector.
void XB_CDECL apsOrientToVelocity(uint8_t* begin, uint8_t* end, uint8_t* emitter);
// 0x000a4640 'AngT': with emitter flag 0x20 the rotations face along the velocities
// (apsOrientToVelocity), else each angle (+0x92) is the velocity's view heading like ATEX.
void XB_THISCALL apsActionAngleToVelocity_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);


// 0x000a3f00 'LAtr': velocities pulled toward the line through two points (+8, +0x14; in the
// emitter's space unless emitter flag 2) by strength (+0x20) * delta / distance^2 (at least 0.001).
void XB_THISCALL apsActionLineAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);
// 0x000a41c0 'DLAt': the same, the pull fading from the first point to 0 at the second.
void XB_THISCALL apsActionDecayLineAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);


// 0x000a2300 'KTau': Frenet-frame motion - velocity = tangent * speed (+8); the tangent turns
// by curvature, the binormal by torsion (both * delta, renormalised), normal = tangent x
// binormal; curvature/torsion drift by random rates in [+0xc, +0x10) / [+0x1c, +0x20) and are
// clamped to [+0x14, +0x18] / [+0x24, +0x28].
void XB_THISCALL apsActionFrenetMove_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);


// 0x000a2900: the segment of a point list (3 floats each, ended by a -1 word) `*distance` along
// from *cursor: cursor, the distance into it, its length and both ends ({x, y, z, x}) stored,
// 1; 0 past the end (nothing stored).
uint32_t XB_STDCALL apsPath_FindSegment(uint8_t** cursor, float* distance, float* length, float* start, float* finish);
// 0x000a2aa0 'Traj': particles moved along a path (looked up by id +8 through 0x00909940, speed
// +0xc) and turned to its heading; queued for removal at its end.
void XB_THISCALL apsActionTrajectory_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);


// 0x000a06e0: sin and cos of `angle` by the packed sine polynomial (scene_sine_poly); returns `sine`.
float* XB_CDECL apsSinCos(float angle, float* sine, float* cosine);
// 0x0009fe70: the unit quaternion of a rotation by `angle` about `axis` (renormalised on the x87).
float* XB_THISCALL Quat_FromAxisAngle(float* out, const float* axis, float angle);
// 0x000a00f0: the quaternion of Euler angles: about z, then x, then y.
float* XB_CDECL Quat_FromEuler(float* out, float x_angle, float y_angle, float z_angle);
// 0x00097b60: a box {min, max} (aligned) grown by `amount` on every side.
void XB_THISCALL FxBox_Expand(float* box, float amount);
// 0x00097810: a particle copied into the emitter's next slot (+0xbc, count +0xc0 below +0xa8;
// high-water marks +0xc4 and 0x00909ec0/c4) and its bounds (by size with flag 2, else 1) added
// to the emitter's box (+0x60).
uintptr_t XB_THISCALL FxEmitter_AddParticle(uint8_t* emitter, const uint8_t* particle);


// 0x000a2e40, the Apply of the emitters SrcA/Brst/RSpn: their count (slot 5) of new particles,
// at most up to the emitter's capacity, spread along the emitter's path since the last frame;
// each filled from the 16 generators (self +0xc, vtable slot 1, called in a fixed order) for
// the fields the emitter flags (+0x84) enable, then FxEmitter_AddParticle.
void XB_THISCALL apsActionEmitter_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time, float delta);

// 0x000241d0: the entity of a component's handle (+0xc; G_EntHandleToEnt's table 0x0097a968).
uint8_t* XB_REGS1 XB_CDECL FxComponent_GetEntity(const uint8_t* component);
// 0x000244c0: the DObj of a handle (table 0x00981418, 0x540 slots {object, generation}).
uint8_t* XB_REGS1 XB_CDECL DObjHandle_Resolve(const uint32_t* handle);
// 0x00278e30 (out in EAX, index in ECX): the DObj handle (+0x8c) of weapon model `index`
// (0x00975fa8, 0xc4 each; 0 without a model).
uint32_t* XB_REGS3 XB_CDECL WeaponModel_GetDObjHandle(uint32_t* out, uint32_t unused_edx, uint32_t index);
// 0x0029af30 (out in EAX, component in ECX): the component entity's origin (+0x140, 16 bytes).
uint32_t* XB_REGS3 XB_CDECL FxComponent_GetEntityOrigin(uint32_t* out, uint32_t unused_edx, const uint8_t* component);
// 0x0029b0f0 (out in EAX, component in EDX): the placement's (+0x14) translation row (+0x30),
// else the entity origin {x, y, z, x}.
float* XB_REGS2 XB_CDECL FxComponent_GetPosition(float* out, const uint8_t* component);
// 0x00298ba0 (manager in EDX, scale in XMM0, id on the stack, ret 4): the scale (+0x10) of the
// rumble `id` in either of the manager's lists (+8, +0x18; their last node is never checked).
extern "C" PLATFORM_HIDDEN void XB_CDECL Rumble_SetScale(uint8_t* manager, uint32_t id, float scale);
extern "C" void Rumble_SetScale_entry(void);
// 0x002ae740 (shake in ECX, rotation in XMM0, position on the stack, ret 4): non-zero amplitudes
// stored (rotation +0x18/+0x28, position +0x14/+0x24).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CameraShake_SetAmplitude(uint32_t eax, uint8_t* shake, float rotation,
                                                                  float position);
extern "C" void CameraShake_SetAmplitude_entry(void);
// 0x002a7f70 (manager in EDX): the first of 5 free shake slots (+0xc, 0x40 each, in use +0x3c)
// cleared and marked in use; null when all are taken.
uint8_t* XB_REGS2 XB_CDECL CameraShake_AllocSlot(uint32_t unused_eax, uint8_t* manager);

// 0x002990d0 (choice in EAX): bits 0-4 = parameters 0xd, 0xe, 0x12, 0x16, 0x20 selected and non-zero.
uint32_t XB_REGS1 XB_CDECL ParamChoice_GetFlags(const uint8_t* choice);
// 0x00298f50: one choice of a list {count, choice*[count]} picked at random by weight (parameter
// 0x21, default 1; the particle code's rand 0x008d5a70, 15 bits); the last one when nothing hits.
const uint8_t* XB_CDECL ParamChoiceList_PickRandom(const uint32_t* list);

// 0x0029b320: a list {count, choice*[64]} reduced to one choice per group (parameter 0x22, a
// name compared with _stricmp): ungrouped choices first, then per group in list order one picked
// by ParamChoiceList_PickRandom. Unchanged when no choice has a group.
void XB_CDECL ParamChoiceList_PickPerGroup(uint32_t* list);

// 0x0029a430 (forward in EDI, right out in ESI, up out in EBX): right = {f.z, -f.x, f.y} made
// perpendicular to forward and normalized (unless zero), up = right x forward.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_AxesFromForward(const float* forward, float* right, float* up);
extern "C" void FX_AxesFromForward_entry(void);
// 0x002969a0: a random value between value / 2^octaves and value * 2^octaves (the particle
// code's rand 0x008d5a70).
// The result is in XMM0.
extern "C" PLATFORM_HIDDEN float XB_CDECL FX_RandomInOctaves(float value, float* octaves);
extern "C" void FX_RandomInOctaves_entry(void);
// 0x002ae780 (type in EAX, slot in ESI, duration on the stack, ret 4): a shake slot from the
// shake type's row (0x008d5138, 0x20 each, int16 type first): two random phases (rand % 16384),
// amplitudes from the row (+8, +0xc), duration = the argument unless 0, else the row's (+4).
extern "C" PLATFORM_HIDDEN void XB_CDECL CameraShake_InitSlot(int32_t type, uint8_t* slot, float duration);
extern "C" void CameraShake_InitSlot_entry(void);

// 0x00023640 (table in ESI, name on the stack, ret 4): the trigger entry (+0x20, 0x34 bytes each,
// count +0x24) whose name (first) equals `name` (_stricmp), or null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FXTriggerTable_Find(const uint8_t* table, const char* name);
extern "C" void FXTriggerTable_Find_entry(void);
// 0x000244f0 (out in ESI; format, ... on the stack): vsprintf into a 32-byte string whose last
// byte holds the length; returns out.
extern "C" PLATFORM_HIDDEN char* XB_CDECL FixedStr32_Format(char* out, const char* format, va_list args);
extern "C" void FixedStr32_Format_entry(void);

// 0x0029e1c0 (list in EDI; zones, prefix, suffix on the stack, ret 12): the "TRIGGERS" entries
// (FXTriggerTable_Find) of the trigger tables named `suffix` in the two global zone sets
// (0x0095f638 +0x2c, +0x34; the second only when it is another table) and, with a prefix, of
// "prefix-suffix" in `zones`, appended to a list {count, entries[16]}.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_CollectTriggers(uint32_t* list, uint32_t zones, const char* prefix, const char* suffix);
extern "C" void FX_CollectTriggers_entry(void);

// 0x002c94d0 (origin in EBX or null; slot, falloff on the stack, ret 8): the slot's distance
// scale (+0x2c) = base (0x00964b80) / max(camera distance * falloff, 2 * base) (Newton-refined
// RSQRTSS), base / (2 * base) without an origin, base when the range is 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL CameraShake_SetDistanceScale(const float* origin, uint8_t* slot, float falloff);
extern "C" void CameraShake_SetDistanceScale_entry(void);
// 0x002c9620: a camera shake of `type` (row of 0x008d5138) started unless the type's cooldown
// (+0x18, set when not -1) since its last start (+0x1c, cg_time / 1000) has not passed: a slot
// (CameraShake_AllocSlot) with the strength (1 for 0 in distance-scaled types, row +2 == 0) and
// duration (row +4 unless given), scaled by the manager's +4 unless the player (0x0095f534 +4)
// lacks flag 0x20 and the manager's +8 is set; CameraShake_InitSlot. Null when not started.
uint8_t* XB_STDCALL CameraShake_Start(uint8_t* manager, int32_t type, const float* origin, float strength, float duration, float cooldown);

// 0x002a22d0 (component in ESI; result in XMM0): 1 inside the component's near range (+0x44),
// fading to 0 at the far range (+0x48), both against the squared distance from the player
// (0x0095f534 +4, origin +0x140) to the component's entity; 1 without an entity or a valid range.
extern "C" PLATFORM_HIDDEN float XB_CDECL FxShakeRumble_GetFalloff(const uint8_t* component);
extern "C" void FxShakeRumble_GetFalloff_entry(void);

struct ScriptString;
// 0x00024110 (record in ESI, fade out in XMM1; 7 stack words, ret 0x1c): a shake/rumble record
// {links, id +8, -start +0xc, +0x10, strength +0x14, hold +0x18, end of fade-out +0x1c, end
// +0x20, fade out +0x24, name +0x28, named +0x2c}; the name is passed by value and released.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ShakeRumbleRecord_Init(uint8_t* record, float fade_out, uint32_t id, float start,
                                                                    float a3, float strength, float hold, float fade_in,
                                                                    ScriptString* name);
extern "C" void ShakeRumbleRecord_Init_entry(void);
// 0x0029b1f0: the two channels of a descriptor (+4, 0x20 each; strength > 0) queued as 0x30-byte
// records (the small heap 0x0095f5c8) on the manager's lists under the manager's next id;
// *out_id = the id, returns out_id.
uint32_t* XB_THISCALL ShakeRumble_Start(const uint8_t* desc, uint8_t* manager, uint32_t* out_id, float a3);

// 0x000146a0 (desc in ESI): a shake/rumble descriptor cleared: both channel names empty,
// strengths and fade-outs 1, the rest 0. Returns desc.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ShakeRumbleDesc_Init(uint8_t* desc);
extern "C" void ShakeRumbleDesc_Init_entry(void);
// 0x00014710 (desc in EAX): both channel names released (second first).
void XB_REGS1 XB_CDECL ShakeRumbleDesc_Destroy(uint8_t* desc);
// 0x002a3570 (vtable 0x008376d0 slot 1): a shake/rumble component's update. After its delay
// (+0x20; age +0x24): once (flag 2 of +0x2a) a camera shake (amplitudes +0x30/+0x34 scaled by
// FxShakeRumble_GetFalloff, duration +0x2c, cooldown +0x38; kept in +0x64) and with +0x59 a
// rumble (strength +0x3c clamped to 0..1, channel 0 on with +0x58; id kept in +0x60); later
// calls rescale both by the falloff.
void XB_THISCALL FxShakeRumbleComponent_Update(uint8_t* component, float delta);

// 0x00299280 (zones in EAX: 99 tables at +4; key on the stack, ret 4): the first zone table
// whose map (+8, UintMap_Find; lists +0x14, count +0x10) has `key`: its list's next item
// round-robin ({count, items, position}; the only item for one), 0 for an empty list.
uint32_t XB_REGS1 XB_STDCALL ZoneAliasList_NextVariant(const uint32_t* zones, uint32_t key);

// 0x0029ff50 (translation in EAX or null = {0, 0, 0, 1}, angles in ECX (radians), out on the
// stack): a 4x4 rotation from the angles (the title CRT's __CIsin/__CIcos) with the translation
// as row 3; each row's 4th lane repeats its first.
void XB_REGS3 XB_CDECL FX_AnglesToMatrix(const float* translation, uint32_t unused_edx, const float* angles, float* out);

// 0x0023d5a0 (axis[3] in EAX, origin in ECX, effect on the stack): FX_SpawnEffect with the
// matrix {axis rows, origin} (4th lanes repeat the first), no entity, bone -1.
uint32_t XB_REGS3 XB_CDECL FX_PlayEffectWithAxis(const float* axis, uint32_t unused_edx, const float* origin, uint32_t effect);
// 0x0023e620 (direction in EAX; effect, origin on the stack): FX_PlayEffectWithAxis with the
// axes FX_AxesFromForward builds around the direction (forward = axis 2).
uint32_t XB_REGS1 XB_CDECL FX_PlayEffectAlongDir(const float* direction, uint32_t effect, const float* origin);

// 0x002a46d0 (vtable 0x008375e0 slot 1): a particle component's update. Once past its delay
// (+0x20; age +0x24) it spawns its effect from the parameters FX_AddParticleComponents left in
// globals (0x009717f4-0x00971838): bolted to a tag of the entity's or its weapon model's DObj,
// at a matrix (+0x14), at the entity's angles or an identity (flags 0x80/0x200), along a
// placement's direction (0x40, 0x100), or at the entity origin; the effect in +0x2c.
void XB_THISCALL FxParticleComponent_Update(uint8_t* component, float delta);

// 0x002a4bc0 (stdcall; the first argument unread): for each "LIGHT.FX" trigger of the source's
// zones (FX_CollectTriggers) a ParamSet with the source's overrides, resolved (vtable slot 1)
// and reduced per group; every choice with a texture (parameter 0x17) becomes a light component
// (0x30 bytes, vtable 0x008376fc; delay = parameter 0x10, flags = ParamChoice_GetFlags) with a
// light (Z_Malloc 0x10, placed by 0x002462b0, added to 0x00988714), appended to out {count,
// [16]}. Returns the number of components made.
uint32_t XB_STDCALL FX_AddLightComponents(uint32_t unused, const uint8_t* source, uint32_t* out);

// 0x002a3820 (stdcall; the first argument unread): FX_AddLightComponents' pattern for the
// "SHAKEANDRUMBLE.FX" triggers: every choice becomes a shake/rumble component (0x68 bytes,
// vtable 0x008376d0) from parameters 0x10 (delay), 0x32-0x39, 0x3c (clamped to >= 0.01),
// 0x3a/0x3b (bytes), 0x4/0x5 (ranges, squared), 0x17, then its first Update (delta 0).
uint32_t XB_STDCALL FX_AddShakeRumbleComponents(uint32_t unused, const uint8_t* source, uint32_t* out);

// 0x002a4ec0 (stdcall, 4 words; the first unread): FX_AddLightComponents' pattern for the
// "PARTICLE.FX" triggers, skipped (flag 2 of out +0x50 cleared) for modes 1-3 beyond
// 0x008fef64 squared. Choices (list 0, per group) come from per-weapon-hand caches
// (bg_weaponDefs +0x2b4) or per-trigger caches keyed by the source's name (0x0096d970, 0x00971910;
// 0x008d5a78 entries) when 0x008d5110 is set. Each choice's effect file (parameter 0x11) is
// looked up (Stream_FindEffect of its base name) and the spawn parameters go to the globals
// 0x009717f0-0x00971838 that FxParticleComponent_Update reads; 0x00971800 = the file name's
// stack address (the C version writes the original's: entry ESP - 4, 8-aligned, - 0x770).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FX_AddParticleComponents(uint32_t entry, uint32_t unused, const uint8_t* source,
                                                                      uint32_t* out, float distance);
extern "C" void FX_AddParticleComponents_entry(void);

// 0x0029d7c0 (vtable 0x00837674 slot 1, thiscall + delta, ret 4): a sound component's update.
// Once past its delay (+0x20; age +0x24) and with a live entity: the first time (flag 2 of
// +0x2a) the alias (+0x68; not with volume 0) plays at the placement (+0x30, flag 0x40), the
// entity origin or 0x0082e290 - S_PlaySoundAlias2 with flag 0x10, else S_PlaySoundAlias after
// FX_LimitSoundComponents (+0x5e instances), retried without the loop flag (8) when no voice
// came; the pitch +0x58 is FX_RandomInOctaves(+0x44, +0x48) unless +0x44 is -1. A dead looping
// sound (flag 0x20) restarts within +0x50 of the listener. Then the timer +0x54 (vtable slot 7),
// the end notifies (0x0096d928, the reference's +0x2c), the fade (flag 1: +0x1c over +0x18
// scales +0x40) and the placement's position. The asm entry passes the caller's EBX: its
// upper half goes into a word S_PlaySoundAlias gets.
extern "C" PLATFORM_HIDDEN void XB_CDECL FxSoundComponent_Update(uint8_t* component, float delta, uint32_t ebx);
extern "C" void FxSoundComponent_Update_entry(void);

// 0x0029df60 (component in ESI, returned): a sound component (0x70 bytes, vtable 0x00837674)
// from the spawn parameters FX_AddSoundComponents leaves in 0x0096d8e8-0x0096d92c (floats
// +0x40-+0x54, placement copy +0x30 with flag 0x40, the sound S_FindSoundByHash of the name
// 0x0096d8f8 in +0x68, its name hash in +4, also given to the entity's +0x3c8 when +0x3c4 is
// set and +0x3c8 not), then its first Update (FxSoundComponent_Update 0x0029d7c0, delta 0).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FxSoundComponent_Create(uint8_t* component);
extern "C" void FxSoundComponent_Create_entry(void);

// 0x0029eea0 (stdcall, 4 words; the first unread): FX_AddParticleComponents' pattern for the
// "SOUND.FX" triggers (caches 0x0096f8a8 / 0x00971840, weapon hands at bg_weaponDefs +0xac;
// only single-choice lists are cached): skipped for modes 0-3 beyond 0x008fef68 squared and for
// modes 1-3 with more than 15 busy voices. A source with its own aliased sound (flags 1 and
// 0x40, +0x1b4 == 3; ZoneAliasList_NextVariant of its name's hash) makes one component and no
// trigger is read; otherwise only the first trigger with choices makes components, one per
// choice with a sound name (parameter 0x19, base name; 0x0096d8f8 = its stack address in the
// original: entry ESP - 4, 8-aligned, - 0x6b0). Clears flag 2 of source +0x20c after the
// triggers. Returns the number of components.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FX_AddSoundComponents(uint32_t entry, uint32_t unused, uint8_t* source,
                                                                   uint32_t* out, float distance);
extern "C" void FX_AddSoundComponents_entry(void);

// 0x0029b680 (stdcall: lists {count, list*}, name hash, entity, limit): counts the sound
// components (flag 4 of +0x2a) of `entity` with the name hash; at the limit the oldest one (age
// +0x28; the first of equals) is stopped (vtable slot 4), destroyed and replaced by its list's
// last item. Returns the count.
uint32_t XB_STDCALL FX_LimitSoundComponents(uint32_t* lists, uint32_t hash, const uint8_t* entity, int32_t limit);
// 0x0029b7f0 (entity in EAX, may be null): a pending sound notify (+0x3c4) is sent
// (Scr_Notify) and +0x3c4/+0x3c8 cleared.
void XB_REGS1 XB_CDECL Entity_EndSoundNotify(uint8_t* entity);

// 0x00094170: 0x0009bd80, then a tail jump to 0x000950b0 (CALL in the first 5 bytes: not
// hookable).
uint32_t XB_CDECL FX_ResetParticleSystem();

// 0x000240b0 (system in ESI): runs a new system `time` seconds ahead - its start (+0x40)
// moved back by `time`, then FxSystem_Update in 0.3 s steps until `time` is covered.
extern "C" PLATFORM_HIDDEN void XB_CDECL FxSystem_Prewarm(uint8_t* system, float time);
extern "C" void FxSystem_Prewarm_entry(void);
// 0x00024200 (EAX): 1 when the component waits for a delay (flag bit 0 of +0x2a) whose
// remaining time (+0x1c) is under 0.001.
uint32_t XB_REGS1 XB_CDECL FxComponent_DelayDone(const uint8_t* component);
// 0x00024320, 0x00024350, 0x000243a0: the sound / particle / shake-rumble components'
// deleting destructors (bit 0 of flags frees to the small-block heap 0x0095f5cc). The
// particle one stops its system, the shake-rumble one its rumble (+0x60) and shake (+0x64).
uint8_t* XB_THISCALL FxSoundComponent_DeletingDtor(uint8_t* component, uint32_t flags);
uint8_t* XB_THISCALL FxParticleComponent_DeletingDtor(uint8_t* component, uint32_t flags);
uint8_t* XB_THISCALL FxShakeRumbleComponent_DeletingDtor(uint8_t* component, uint32_t flags);
// 0x00094610 (cdecl; Sys_Main passes 0, 0x2000000, 4000): the particle library's start -
// view reset, log, particle buffers for `quads`, every render op's shaders, the heap size,
// the settings block 0x00909910..; returns 1.
uint32_t XB_CDECL FX_InitSystem(uint32_t unused, uint32_t heap_size, int32_t quads);
// 0x00094750: the effect view (0x00909950, 14 floats) back to its default axes.
void XB_THISCALL FxView_Reset(float* view);
// 0x00094810: the index of the definition's (+0x48) emitter named `name`, -1 if none.
int32_t XB_THISCALL FxSystem_FindEmitterByName(const uint8_t* system, const char* name);
// 0x000949e0: the box (min, max: 8 floats) around the system's emitters that have
// particles (+0xc0), from each one's box at +0x60/+0x70. Returns the emitter list's end.
uint8_t* const* XB_THISCALL FxSystem_GetBounds(const uint8_t* system, float* out);
// 0x00095060: FxSystem_GetBounds of every system in the list (+4, count +0x1e4) into a
// scratch box (the results are dropped).
void XB_THISCALL FxSystemList_UpdateBounds(const uint8_t* list);
// 0x000951c0: the system pool (fx_systemPool 0x00909994, 0x2c bytes): `count` systems
// (+0, +4) behind a memory pool at +8.
void XB_CDECL FxSystemPool_Init(int32_t count);
// 0x00095f20: `count` empty systems (0x70 bytes each) from the particle heap.
uint8_t* XB_CDECL FxSystem_AllocArray(int32_t count, uint32_t alignment);
// 0x00097340: an emitter's origin (+0x40) = `origin`; the previous one (+0x50) = the old
// origin, or the new one while the previous is still 0. Returns origin's 4th word.
uint32_t XB_THISCALL FxEmitter_SetOrigin(uint8_t* emitter, const uint32_t* origin);

// 0x00236ba0 (block EDI, memory context ESI): the effect allocator's free - the global
// effect pools first (FxPools_ReleaseGlobal), then the context's heap (0x0095f638 +0x38
// table, 99 entries; -1 skips it), else the main heap 0x0095f6d8.
extern "C" PLATFORM_HIDDEN void XB_CDECL FxMem_Free(int32_t context, uint32_t block);
extern "C" void FxMem_Free_entry(void);
// 0x00236bf0 (size ESI, alignment EDI, memory context EBX): apsAlloc, then the context's heap,
// else the main heap.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FxMem_Alloc(int32_t context, uint32_t size, uint32_t alignment);
extern "C" void FxMem_Alloc_entry(void);
// 0x002462b0: an entity's (handle in the low 12 bits, 0x540 slots at 0x0097a968) "tag_flash"
// origin (the view's own entity: its weapon model, 0x00975fa8 + 0xc4 per weapon; others:
// their model +0x22c in world space), else its origin +0x140; out +0xc = 1.
void XB_STDCALL FX_GetEntityTagOrigin(float* out, uint32_t handle);
// 0x00293410 (effect in EBX: count +0, components +4): 1 when every component's slot 10
// (finished) says so.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL FX_EffectComponentsFinished(const uint8_t* effect);
extern "C" void FX_EffectComponentsFinished_entry(void);
// 0x0029ae90 (vtable slot 4): stops the rumble (+0x60, Rumble_Stop) and detaches the shake
// (+0x64: its +0x3c cleared).
void XB_THISCALL FxShakeRumbleComponent_Stop(uint8_t* component);
// 0x0029b010 (vtable slot 9): parent +0x14; a live sound handle's (+0x64) channel gets it at
// +0x28 and its voice the parent's origin (+0x30).
void XB_THISCALL FxSoundComponent_SetParent(uint8_t* component, const uint8_t* parent);
// 0x0029ed60 (vtable slot 3): replays a live handle's voice in state 3 (paused) and restarts
// its subtitle (+0x60).
void XB_THISCALL FxSoundComponent_Resume(uint8_t* component);
// 0x0029ee30 (component in ESI): stops a live handle's channel (S_StopChannel), then the
// base destructor.
extern "C" PLATFORM_HIDDEN void XB_CDECL FxSoundComponent_Dtor(uint8_t* component);
extern "C" void FxSoundComponent_Dtor_entry(void);

// 0x00094ab0 (thiscall, one stack word it never reads, ret 4): FxSystem_GetBounds into a
// scratch box nothing reads; returns its result. No references (dead).
uint8_t* const* XB_THISCALL FxSystem_UpdateBounds(const uint8_t* system, uint32_t unused);
// 0x000950b0 (FX_ResetParticleSystem's tail jump): every pooled system's definition (+0x48)
// gets flag +0xc0 cleared and set again, then FxSystem_GetBounds of every pooled and every
// active system (0x009099ac, count 0x00909b8c) into a scratch box. Returns the active count.
uint32_t XB_CDECL FxSystemPool_Refresh();
// 0x00095f80 (thiscall): the system pool's teardown, FxSystemPool_Init's reverse - slot
// table freed, each of the `+4` systems (array +0, 0x70 bytes each) destroyed, the array
// given back to the particle heap (slot 2), the memory pool's slots freed, fx_systemPool
// (0x00909994) cleared. Only caller 0x00095640, reached from 0x0009c190: no references (dead).
void XB_THISCALL FxSystemPool_Destroy(uint8_t* pool);
// 0x0029b0b0 (vtable 0x00837624 slot 6, the component base's "done"; called by the slot 6 of
// vtable 0x008376fc, 0x0029d620): 1 in AL when the owner entity handle (+0xc, table
// 0x0097a968) no longer resolves and flag 1 of +0x10 does not keep the component.
uint32_t XB_THISCALL FxComponent_OwnerGone(const uint8_t* component);

// 0x0029d620 (vtable 0x008376fc slot 6, 0x00837714; thiscall): 1 in AL when the component's
// definition (+0x2c) has no byte +0xc, 0 when it does not follow an owner (flag 2 of +0x2a),
// otherwise FxComponent_OwnerGone.
uint32_t XB_THISCALL FxComponentB_IsOwnerGone(const uint8_t* component);

// 0x0023b9c0 (surface in ECX, plane out in EBX): the plane of a surface's first triangle -
// kind (+0) >= 0x14: vertices +0x3c (12 bytes each) indexed by the words +0x44 (origin), +0x48,
// +0x46; kind 2: the three points of +0xc at +0, +0x40, +0x20 - through PlaneFromPoints;
// no surface or another kind: five zero words with 1.0 in the first. No reference (dead).
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_SurfacePlane(const uint8_t* surface, float* plane);
extern "C" void FX_SurfacePlane_entry(void);

}  // namespace game
