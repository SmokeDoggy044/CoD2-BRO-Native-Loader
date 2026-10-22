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
void XB_REGS1 XB_STDCALL FX_UpdateEffects(FxIdList* list, uint32_t delta);

}  // namespace game
