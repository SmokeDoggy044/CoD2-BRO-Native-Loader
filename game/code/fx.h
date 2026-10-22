#pragma once

// The effect pool (FX_*): effects of 0x1c bytes, handed out from a free list.

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

// 0x0023b970: takes an effect off the free list and clears it; null when the pool
// is not set up or empty.
FxEffect* XB_CDECL FX_AllocEffect();

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
// matrix moved to the origin.
extern "C" PLATFORM_HIDDEN void XB_CDECL FX_PlayEffectAt(uint32_t effect, const float* origin);
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

}  // namespace game
