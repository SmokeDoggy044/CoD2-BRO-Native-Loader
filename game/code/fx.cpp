#include "fx.h"

#include <cstdarg>
#include <cstring>

#include "dobj.h"
#include "lanes.h"
#include "ngl.h"
#include "param_set.h"
#include "qcommon.h"
#include "script_string.h"
#include "snd.h"
#include "stream.h"
#include "title_data.h"

namespace game {

FxEffect* XB_CDECL FX_AllocEffect() {
    FxEffect* pool = title_global<FxEffect*>(0x0095f6e4);
    uint32_t& free_count = title_global<uint32_t>(0x00988810);
    if (!pool || !free_count) return nullptr;
    --free_count;
    // Free entries keep the effect's index in their upper half.
    const int16_t index = title_global<int16_t>(0x00988816 + free_count * 4);
    FxEffect* effect = pool + index;
    if (!effect) return nullptr;
    effect->unknown04 = 0;
    effect->unknown08 = 0;
    effect->unknown0c = 0;
    effect->unknown10 = 0;
    effect->unknown16 = 0;
    effect->unknown18 = -1;
    return effect;
}

void XB_CDECL FX_InitEffectPool() {
    FxEffect*& pool = title_global<FxEffect*>(0x0095f6e4);
    if (pool) return;
    uint32_t* block = title_function<uint32_t* XB_CDECL(uint32_t)>(0x00042c30)(4 + 0xf0 * sizeof(FxEffect));  // Heap_Alloc16
    FxEffect* effects = nullptr;
    if (block) {
        block[0] = 0xf0;
        effects = reinterpret_cast<FxEffect*>(block + 1);
        title_function<void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t)>(0x00011240)(  // eh_vector_constructor_iterator
            effects, sizeof(FxEffect), 0xf0, 0x00029650);
    }
    pool = effects;
    uint32_t& free_count = title_global<uint32_t>(0x00988810);
    free_count = 0;
    for (uint32_t i = 0; i < 0xf0; ++i) {
        FxEffect& effect = effects[i];
        effect.unknown00 = i | (i << 16);
        if (static_cast<int32_t>(free_count) < 0xf0) {
            title_global<uint32_t>(0x00988814 + free_count * 4) = effect.unknown00;
            ++free_count;
        }
    }
}

uint8_t* XB_CDECL FX_FindEvictableEffect(const uint8_t* effect) {
    const uint32_t count = title_global<uint32_t>(0x0098880c);
    uint8_t** const first = title_global<uint8_t**>(0x00988804);
    if (!count) return nullptr;
    uint8_t** entry = first + count - 1;
    if (entry == first) return nullptr;
    const int32_t priority = *reinterpret_cast<const int32_t*>(effect + 0x68);
    for (;;) {
        uint8_t* active = *entry;
        const uint8_t* definition = *reinterpret_cast<uint8_t* const*>(active + 4);
        if (definition) {
            const uint32_t flags = *reinterpret_cast<const uint32_t*>(definition + 0x50);
            if (static_cast<int32_t>(flags >> 25) <= priority) return nullptr;
            if (flags & 0x1000000) return active;
        }
        if (entry-- == first) return nullptr;
    }
}

namespace {

uint8_t* address_to_pointer(uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); }

// Slot `slot` of a title object's vtable.
uint32_t vtable_slot(uint32_t object, int slot) {
    return reinterpret_cast<const uint32_t*>(address_to_pointer(*reinterpret_cast<const uint32_t*>(address_to_pointer(object))))[slot];
}

uint32_t effect_order(const uint8_t* effect) {
    const uint8_t* definition = *reinterpret_cast<uint8_t* const*>(effect + 4);
    return definition ? *reinterpret_cast<const uint32_t*>(definition + 0x50) : 0xffffffffu;
}

uint8_t** active_effects() { return title_global<uint8_t**>(0x00988804); }

void swap_effects(int32_t a, int32_t b) {
    uint8_t* t = active_effects()[a];
    active_effects()[a] = active_effects()[b];
    active_effects()[b] = t;
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_FreeEffect_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FX_FreeEffect) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FX_FreeEffect(FxEffect* effect) {
    if (!effect) return;
    title_function<void XB_THISCALL(FxEffect*)>(0x00029560)(effect);  // FX_ReleaseEffectHandle
    uint32_t& free_count = title_global<uint32_t>(0x00988810);
    *reinterpret_cast<uint16_t*>(effect) = static_cast<uint16_t>(free_count);
    if (static_cast<int32_t>(free_count) >= 0xf0) return;
    title_global<uint32_t>(0x00988814 + free_count * 4) = *reinterpret_cast<const uint32_t*>(effect);
    ++free_count;
}

void XB_CDECL FX_SortEffects_r(int32_t lo, int32_t hi) {
    while (hi - lo > 4) {
        const int32_t mid = (lo + hi) / 2;
        if (effect_order(active_effects()[lo]) > effect_order(active_effects()[mid])) swap_effects(lo, mid);
        if (effect_order(active_effects()[lo]) > effect_order(active_effects()[hi])) swap_effects(lo, hi);
        if (effect_order(active_effects()[mid]) > effect_order(active_effects()[hi])) swap_effects(mid, hi);
        swap_effects(mid, hi - 1);
        const uint32_t pivot = effect_order(active_effects()[hi - 1]);
        int32_t i = lo;
        int32_t j = hi - 1;
        for (;;) {
            while (effect_order(active_effects()[++i]) < pivot) {
            }
            while (effect_order(active_effects()[--j]) > pivot) {
            }
            if (j < i) break;
            swap_effects(i, j);
        }
        swap_effects(i, hi - 1);
        FX_SortEffects_r(lo, j);
        lo = i + 1;
    }
}

void XB_CDECL FX_FreeFinishedEffects() {
    uint8_t** const end = title_global<uint8_t**>(0x00988714) + title_global<uint32_t>(0x0098871c);
    bool freed = false;
    for (uint8_t** entry = title_global<uint8_t**>(0x00988714); entry != end; ++entry) {
        uint8_t* light = *entry;
        if (light[0xc]) {
            const float* origin = reinterpret_cast<const float*>(light);
            alignas(16) const float position[4] = {origin[0], origin[1], origin[2], origin[0]};
            const float one = title_global<float>(0x0083eb20);
            alignas(16) const float color[4] = {one, title_global<float>(0x0083eb58), 0.0f, one};
            nglAddPointLight(0x40000000, position, title_global<float>(0x0095f6ec), title_global<float>(0x008db380),
                             color);
            light[0xc] = 0;
        } else {
            *entry = nullptr;
            title_function<void XB_CDECL(void*)>(0x000428d0)(light);  // Z_Free
            freed = true;
        }
    }
    if (!freed) return;
    // std::remove of the null entries, then erase.
    uint8_t** const first = title_global<uint8_t**>(0x00988714);
    uint8_t** const last = first + title_global<uint32_t>(0x0098871c);
    uint8_t** out = first;
    while (out != last && *out) ++out;
    if (out != last) {
        for (uint8_t** in = out + 1; in != last; ++in)
            if (*in) *out++ = *in;
    }
    uint32_t& count = title_global<uint32_t>(0x0098871c);
    if (out != last) count -= static_cast<uint32_t>(last - out);
}

PLATFORM_ASM_ENTRY(FX_IsEffectActive_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FX_IsEffectActive) "addl $8, %esp\n"
                   "ret")

uint8_t XB_CDECL FX_IsEffectActive(const FxIdList* list, uint32_t id) {
    // The first instance with the id is active while flagged (bit 1 of +0x50) or
    // while it still has components.
    for (uint32_t i = 0; i < list->count; ++i) {
        const uint8_t* instance = list->items[i];
        if (*reinterpret_cast<const uint32_t*>(instance + 0x4c) != id) continue;
        if ((*reinterpret_cast<const uint32_t*>(instance + 0x50) >> 1) & 1) return 1;
        return *reinterpret_cast<const uint32_t*>(instance) != 0;
    }
    return 0;
}

void XB_REGS3 XB_CDECL FX_StopEffectById(uint32_t id, uint32_t, FxIdList* list) {
    // Stops every component (slot 3) of the first instance with the id.
    for (uint32_t i = 0; i < list->count; ++i) {
        uint8_t* instance = list->items[i];
        if (*reinterpret_cast<uint32_t*>(instance + 0x4c) != id) continue;
        uint32_t* components = reinterpret_cast<uint32_t*>(instance);
        for (uint32_t c = 0; c < components[0]; ++c) {
            const uint32_t component = components[1 + c];
            title_function<void XB_THISCALL(uint32_t)>(vtable_slot(component, 3))(component);
        }
        return;
    }
}

void XB_STDCALL FX_SetEffectParamById(FxIdList* list, uint32_t id, uint32_t value) {
    // Every instance with the id hands `value` to all its components (slot 5).
    for (uint32_t i = 0; i < list->count; ++i) {
        uint32_t* components = reinterpret_cast<uint32_t*>(list->items[i]);
        if (components[0x4c / 4] != id) continue;
        for (uint32_t c = 0; c < components[0]; ++c) {
            const uint32_t component = components[1 + c];
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(component, 5))(component, value);
        }
    }
}

void XB_STDCALL FX_FreeEffectComponents(uint32_t* components) {
    // {count, component[]}: pooled components (flag bit 3 of +0x10) are released
    // (slot 7, 2.0f) and go back to the free list (up to 0x40), the others are deleted.
    for (uint32_t c = 0; c < components[0]; ++c) {
        const uint32_t component = components[1 + c];
        if ((*reinterpret_cast<const uint32_t*>(address_to_pointer(component) + 0x10) >> 3) & 1) {
            uint8_t* pool = title_global<uint8_t*>(0x0095f5b4);
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(component, 7))(component, 0x40000000);
            int32_t& free_count = *reinterpret_cast<int32_t*>(pool + 0x804);
            if (free_count < 0x40) {
                reinterpret_cast<uint32_t*>(pool + 0x808)[free_count] = component;
                ++free_count;
            }
        } else if (component) {
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(component, 0))(component, 1);  // deleting dtor
        }
    }
}

PLATFORM_ASM_ENTRY(FX_UpdateFadingComponents_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FX_UpdateFadingComponents) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL FX_UpdateFadingComponents(uint8_t* pool, uint32_t delta) {
    // The released components on the pool's free list {count +0x804, +0x808...} that
    // still fade (bit 0 of +0x2a) update (slot 1, delta); once faded below 0.001
    // (+0x1c) they are deleted and the list's last entry takes their place.
    uint32_t& count = *reinterpret_cast<uint32_t*>(pool + 0x804);
    uint32_t* entries = reinterpret_cast<uint32_t*>(pool + 0x808);
    for (uint32_t i = 0; i < count; ++i) {
        if (!(address_to_pointer(entries[i])[0x2a] & 1)) continue;
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(entries[i], 1))(entries[i], delta);
        const uint32_t component = entries[i];
        if (!(address_to_pointer(component)[0x2a] & 1)) continue;
        const float level = *reinterpret_cast<const float*>(address_to_pointer(component) + 0x1c);
        if (!(title_global<float>(0x0083eb4c) > level)) continue;  // comiss/jbe: NaN keeps it
        if (component) title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(component, 0))(component, 1);
        entries[i] = entries[count - 1];
        if (count) --count;
        --i;
    }
}

PLATFORM_ASM_ENTRY(FX_FreeEffectsById_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FX_FreeEffectsById) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL FX_FreeEffectsById(FxIdList* list, uint32_t id) {
    uint32_t* const words = &list->count;
    for (uint32_t i = 0; i < list->count; ++i) {
        uint8_t* item = list->items[i];
        if (*reinterpret_cast<const uint32_t*>(item + 0x4c) != id) continue;
        if (item) {
            FX_FreeEffectComponents(reinterpret_cast<uint32_t*>(item));
            title_function<void XB_THISCALL(uint32_t, void*)>(0x00044910)(title_global<uint32_t>(0x0095f5bc),
                                                                          item);  // SmallHeap_Free
        }
        list->items[i] = reinterpret_cast<uint8_t*>(words[list->count]);
        if (list->count) --list->count;
        --i;
    }
}

void XB_CDECL FX_StopEffect(uint8_t* effect, uint32_t immediate) {
    if (!effect) return;
    if (immediate) *reinterpret_cast<uint16_t*>(effect + 0x4e) |= 4;
    title_function<void XB_THISCALL(uint8_t*)>(0x00094ad0)(effect);
    title_function<void XB_THISCALL(uint32_t, uint8_t*)>(0x0009b770)(title_global<uint32_t>(0x00909994) + 8, effect);
}

PLATFORM_ASM_ENTRY(FX_PlayEffectAt_entry,
                   "pushl %esi\n"
                   "pushl 8(%esp)\n" PLATFORM_ASM_CALL(FX_PlayEffectAt) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL FX_PlayEffectAt(uint32_t effect, const float* origin) {
    alignas(16) float matrix[16];
    title_function<void XB_CDECL(float*)>(0x0001c140)(matrix);  // Matrix_Identity
    // Row 3 = the origin, with x again in its w lane.
    matrix[12] = origin[0];
    matrix[13] = origin[1];
    matrix[14] = origin[2];
    matrix[15] = origin[0];
    return title_function<uint32_t XB_CDECL(uint32_t, float*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0023c040)(
        effect, matrix, 0, 0, 0xffffffff, 0);  // FX_SpawnEffect
}

namespace {

// PtrVector {data, capacity, count}: PtrVector_Resize (0x00013970) takes the vector in
// ESI and the count in EBX, PtrVector_PushBack (0x000138c0) the vector in ESI and a
// pointer to the value on the stack (stdcall).
struct PtrVector {
    uint8_t** data;
    uint32_t capacity;
    uint32_t count;
};

extern "C" PLATFORM_HIDDEN void XB_CDECL PtrVector_Resize_call(PtrVector* vector, uint32_t count);
PLATFORM_ASM_ENTRY(PtrVector_Resize_call,
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "movl 12(%esp), %esi\n"
                   "movl 16(%esp), %ebx\n"
                   "movl $0x00013970, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "ret")

extern "C" PLATFORM_HIDDEN void XB_CDECL PtrVector_PushBack_call(PtrVector* vector, uint8_t* const* value);
PLATFORM_ASM_ENTRY(PtrVector_PushBack_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "pushl 12(%esp)\n"
                   "movl $0x000138c0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

PtrVector& draw_list() { return title_global<PtrVector>(0x00988804); }
PtrVector& effect_list() { return title_global<PtrVector>(0x00988ce8); }
PtrVector& effect_lights() { return title_global<PtrVector>(0x00988714); }

}  // namespace

void XB_CDECL FX_SortEffects() {
    uint8_t** entry = effect_list().data;
    uint8_t** const end = entry + effect_list().count;
    PtrVector_Resize_call(&draw_list(), 0);
    for (; entry != end; ++entry) {
        uint8_t* effect = *entry;
        if (effect && *reinterpret_cast<const uint32_t*>(effect + 4)) PtrVector_PushBack_call(&draw_list(), &effect);
    }
    const uint32_t count = draw_list().count;
    if (count) FX_SortEffects_r(0, static_cast<int32_t>(count - 1));
    uint32_t& flag = title_global<uint32_t>(0x0095f6e8);
    if (flag) flag = 0;
}

void XB_CDECL FX_ClearEffects() {
    uint8_t** const end = effect_list().data + effect_list().count;
    for (uint8_t** entry = effect_list().data; entry != end; ++entry) {
        if (*entry) FX_FreeEffect(reinterpret_cast<FxEffect*>(*entry));
    }
    PtrVector_Resize_call(&effect_list(), 0);
    PtrVector_Resize_call(&draw_list(), 0);
    using Free = void XB_CDECL(void*);
    if (effect_list().data) title_function<Free>(0x000939b0)(effect_list().data);  // Mem_AlignedFree
    effect_list() = PtrVector{};
    if (draw_list().data) title_function<Free>(0x000939b0)(draw_list().data);  // Mem_AlignedFree
    draw_list() = PtrVector{};
}

void XB_CDECL FX_FreeAllEffectLights() {
    uint8_t** const end = effect_lights().data + effect_lights().count;
    for (uint8_t** entry = effect_lights().data; entry != end; ++entry) {
        if (*entry) title_function<void XB_CDECL(void*)>(0x000428d0)(*entry);  // Z_Free
    }
    PtrVector_Resize_call(&effect_lights(), 0);
}

PLATFORM_ASM_ENTRY(FX_FreeAllEffects_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FX_FreeAllEffects) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FX_FreeAllEffects(FxIdList* instances) {
    // Both lists take their last entry into slot 0 while they shrink.
    uint32_t* words = &instances->count;
    while (instances->count) {
        uint8_t* item = instances->items[0];
        if (item) {
            FX_FreeEffectComponents(reinterpret_cast<uint32_t*>(item));
            title_function<void XB_THISCALL(uint32_t, void*)>(0x00044910)(title_global<uint32_t>(0x0095f5bc),
                                                                          item);  // SmallHeap_Free
        }
        instances->items[0] = reinterpret_cast<uint8_t*>(words[instances->count]);
        if (instances->count) --instances->count;
    }
    // A second list at +0x804 of objects deleted through their vtable (slot 0, flag 1).
    auto* objects = reinterpret_cast<FxIdList*>(reinterpret_cast<uint8_t*>(instances) + 0x804);
    words = &objects->count;
    while (objects->count) {
        uint8_t* object = objects->items[0];
        if (object) {
            const uint32_t destroy = vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)), 0);
            title_function<void XB_THISCALL(uint8_t*, uint32_t)>(destroy)(object, 1);
        }
        objects->items[0] = reinterpret_cast<uint8_t*>(words[objects->count]);
        if (objects->count) --objects->count;
    }
    FX_ClearEffects();
}

namespace {

// 0x00284a80 (obj EAX, origin ECX, axis EBX): the placement of the DObj's owner.
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_GetOwnerPlacement_call(uint8_t* obj, float* origin, float* axis);
PLATFORM_ASM_ENTRY(DObj_GetOwnerPlacement_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %eax\n"
                   "movl 12(%esp), %ecx\n"
                   "movl 16(%esp), %ebx\n"
                   "movl $0x00284a80, %edx\n"
                   "call *%edx\n"
                   "popl %ebx\n"
                   "ret")

// CG_DObjEnsureSkeleton (0x0027f860): obj in ESI.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_DObjEnsureSkeleton_call(uint8_t* obj);
PLATFORM_ASM_ENTRY(CG_DObjEnsureSkeleton_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "movl $0x0027f860, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

// DebugDraw_AddLine (0x002b2f20): end point in EAX, the rest cdecl.
extern "C" PLATFORM_HIDDEN void XB_CDECL DebugDraw_AddLine_call(const float* end, const float* start,
                                                               const float* color, uint32_t a, uint32_t b,
                                                               uint32_t c, uint32_t d);
PLATFORM_ASM_ENTRY(DebugDraw_AddLine_call,
                   "movl 4(%esp), %eax\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "movl $0x002b2f20, %ecx\n"
                   "call *%ecx\n"
                   "addl $24, %esp\n"
                   "ret")

}  // namespace

PLATFORM_ASM_ENTRY(FX_GetBoltMatrix_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FX_GetBoltMatrix) "addl $12, %esp\n"
                   "ret")

uint8_t XB_CDECL FX_GetBoltMatrix(float* out, uint32_t handle, int32_t bone) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540) return 0;
    const uint32_t* slot = &title_global<uint32_t>(0x00981418 + index * 8);
    if ((handle >> 12) != slot[1]) return 0;
    uint8_t* obj = reinterpret_cast<uint8_t*>(uintptr_t{slot[0]});
    if (!obj) return 0;
    float origin[3], axis[9];
    DObj_GetOwnerPlacement_call(obj, origin, axis);
    if (bone < 0) {
        std::memcpy(out, origin, sizeof origin);
        std::memcpy(out + 3, axis, sizeof axis);
        return 1;
    }
    if (bone >= obj[0x2f]) return 0;
    CG_DObjEnsureSkeleton_call(obj);
    uint8_t* matrices = *reinterpret_cast<uint8_t**>(obj + 0x20);
    if (matrices) matrices += obj[0x60] * 0x40 + 0x30;
    const float* m = reinterpret_cast<const float*>(matrices + bone * 0x40);
    // The bone's rows times the owner's axes, in the original's order.
    for (int row = 0; row < 2; ++row) {
        const float* r = m + row * 4;
        for (int i = 0; i < 3; ++i)
            out[3 + row * 3 + i] = ((r[2] * axis[6 + i]) + (r[1] * axis[3 + i])) + (r[0] * axis[i]);
    }
    for (int i = 0; i < 3; ++i) out[9 + i] = ((m[8] * axis[i]) + (m[10] * axis[6 + i])) + (m[9] * axis[3 + i]);
    for (int i = 0; i < 3; ++i)
        out[i] = (((m[14] * axis[6 + i]) + (m[13] * axis[3 + i])) + (axis[i] * m[12])) + origin[i];
    // A debug cvar (+0x20 = its integer): the three axes as lines that long.
    const auto cvar_integer = [] { return *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0092e9e0) + 0x20); };
    if (!cvar_integer()) return 1;
    static const uint32_t kColors[] = {0x0082eadc, 0x0082eaec, 0x0082eb0c};
    for (int a = 0; a < 3; ++a) {
        const float length = static_cast<float>(cvar_integer());
        const float end[3] = {out[3 + a * 3] * length + out[0], out[4 + a * 3] * length + out[1],
                              out[5 + a * 3] * length + out[2]};
        DebugDraw_AddLine_call(end, out, reinterpret_cast<const float*>(uintptr_t{kColors[a]}), 1, 0, 0, 0);
    }
    return 1;
}

void XB_CDECL FxMatrix_Multiply(float* out, const float* a, const float* b) {
    // Rows 0-2 from the inputs as they are, stored; row 3 reads the inputs again after
    // that store (as the original: out may be b).
    const auto row = [](const float* r, const float* m) {
        return add(add(mul(lanes(m), r[0]), mul(lanes(m + 4), r[1])), mul(lanes(m + 8), r[2]));
    };
    Lanes rows[3];
    for (int r = 0; r < 3; ++r) rows[r] = row(a + r * 4, b);
    std::memcpy(out, rows, sizeof rows);
    const float* last = a + 12;
    const Lanes moved = add(add(add(lanes(b + 12), mul(lanes(b), last[0])), mul(lanes(b + 4), last[1])),
                            mul(lanes(b + 8), last[2]));
    std::memcpy(out + 12, moved.v, sizeof moved.v);
}

void XB_THISCALL FxEmitter_SetMatrix(uint8_t* emitter, const float* matrix) {
    // +0x50 = the previous position (+0x40, row 3 of the old matrix), or this matrix's
    // when there was none yet (all of x y z 0); then the matrix into +0x10.
    const float* previous = reinterpret_cast<const float*>(emitter + 0x50);
    const bool unset = (previous[0] == 0.0f) & (previous[1] == 0.0f) & (previous[2] == 0.0f);
    std::memcpy(emitter + 0x50, unset ? static_cast<const void*>(matrix + 12) : emitter + 0x40, 16);
    std::memcpy(emitter + 0x10, matrix, 64);
}

void XB_THISCALL FxSystem_SetMatrix(uint8_t* system, const float* matrix) {
    FxMatrix_Multiply(reinterpret_cast<float*>(system), *reinterpret_cast<const float* const*>(system + 0x48), matrix);
    uint8_t* const* emitter = *reinterpret_cast<uint8_t* const* const*>(system + 0x54);
    uint8_t* const* const end = emitter + *reinterpret_cast<const int16_t*>(system + 0x5a);
    for (; emitter != end; ++emitter)
        if (*emitter) FxEmitter_SetMatrix(*emitter, reinterpret_cast<const float*>(system));
}

namespace {

struct FxEmitters {
    uint8_t* const* begin;
    uint8_t* const* end;
};

FxEmitters emitters_of(const uint8_t* system) {
    uint8_t* const* first = *reinterpret_cast<uint8_t* const* const*>(system + 0x54);
    return {first, first + *reinterpret_cast<const int16_t*>(system + 0x5a)};
}

}  // namespace

void XB_THISCALL FxSystem_Stop(uint8_t* system) { *reinterpret_cast<uint32_t*>(system + 0x4c) |= 1; }

uint32_t XB_THISCALL FxSystem_IsFinished(const uint8_t* system) {
    if (*reinterpret_cast<const uint32_t*>(system + 0x4c) & 1) {
        const FxEmitters list = emitters_of(system);
        int32_t live = 0;
        for (uint8_t* const* e = list.begin; e != list.end; ++e)
            if (*e) live += *reinterpret_cast<const int32_t*>(*e + 0xc0);
        if (!live) return 1;
    }
    // Not stopped (or particles left): finished once its definition's key times
    // (+0x48 -> +0x50, 0x3c each, +8) all lie at or before its age (+0x44).
    const uint8_t* definition = *reinterpret_cast<const uint8_t* const*>(system + 0x48);
    const int32_t keys = *reinterpret_cast<const int16_t*>(definition + 0x56);
    const uint8_t* key = *reinterpret_cast<const uint8_t* const*>(definition + 0x50) + 8;
    const float age = *reinterpret_cast<const float*>(system + 0x44);
    for (int32_t i = 0; i < keys; ++i, key += 0x3c)
        if (*reinterpret_cast<const float*>(key) > age) return 0;
    return 1;
}

void XB_THISCALL FxSystem_UpdateSortKey(uint8_t* system) {
    // Low 24 bits: the nearest emitter depth (+0xb8, truncated, clamped 0..0xffffff);
    // bits 25+: |definition +0x68|; bit 24: not bit 1 of +0x4e.
    float nearest = title_global<float>(0x007c5004);
    const FxEmitters list = emitters_of(system);
    for (uint8_t* const* e = list.begin; e != list.end; ++e) {
        if (!*e) continue;
        const float depth = *reinterpret_cast<const float*>(*e + 0xb8);
        if (nearest > depth) nearest = depth;
    }
    int32_t depth = static_cast<int32_t>(nearest);
    if (depth < 0)
        depth = 0;
    else if (depth >= 0xffffff)
        depth = 0xffffff;
    uint32_t& key = *reinterpret_cast<uint32_t*>(system + 0x50);
    key ^= (key ^ static_cast<uint32_t>(depth)) & 0xffffff;
    const int32_t layer = *reinterpret_cast<const int32_t*>(*reinterpret_cast<const uint8_t* const*>(system + 0x48) + 0x68);
    const uint32_t magnitude = static_cast<uint32_t>(layer < 0 ? -layer : layer);
    const uint32_t flags = static_cast<uint32_t>(static_cast<int32_t>(*reinterpret_cast<const int16_t*>(system + 0x4e)));
    key = (((magnitude << 25) | (key & 0xffffff)) & 0xfeffffffu) | (~(flags << 23) & 0x1000000u);
}

float* XB_THISCALL FxBox_Center(const float* box, float* out) {
    const Lanes center = mul(add(lanes(box + 4), lanes(box)), title_global<float>(0x0083eb24));
    std::memcpy(out, center.v, sizeof center.v);
    return out;
}

float XB_THISCALL FxBox_Radius(const float* box) {
    // Half the diagonal: 0.5 * sqrt(dx*dx + dy*dy + dz*dz), the root as RSQRTSS and one
    // Newton step; the packed lanes the result drops are computed too (MXCSR flags).
    const Lanes delta = sub(lanes(box + 4), lanes(box));
    const Lanes squares = mul(delta, delta);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    const float half = title_global<float>(0x0083eb24);
    if (sum == 0.0f || sum != sum) {
        platform_keep(squares.v[1] * half);
        platform_keep(squares.v[2] * half);
        platform_keep(squares.v[3] * half);
        return sum * half;
    }
    const float estimate = platform_rsqrt_estimate(sum);
    const float root = sum * estimate;
    const float step = title_global<float>(0x007c4df0) - root * estimate;
    return ((step * root) * title_global<float>(0x007c4e00)) * half;
}

float* XB_THISCALL FxBox_Sphere(const float* box, float* out) {
    float center[4];
    std::memcpy(center, FxBox_Center(box, center), sizeof center);
    out[3] = FxBox_Radius(box);
    out[0] = center[0];
    out[1] = center[1];
    out[2] = center[2];
    return out;
}

uint32_t XB_THISCALL FxEmitter_Draw(uint8_t* emitter, uint32_t lights) {
    const uint32_t renderer = *reinterpret_cast<const uint32_t*>(emitter);
    if (!renderer) return 3;
    const uint32_t count = *reinterpret_cast<const uint32_t*>(emitter + 0xc0);
    const uint16_t flags = *reinterpret_cast<const uint16_t*>(emitter + 0xae);
    if (!count || !(flags & 1) || ((flags >> 2) & 1)) return 2;
    float* matrix = (flags & 2) ? reinterpret_cast<float*>(emitter + 0x10) : nullptr;
    alignas(16) float sphere[4];
    float* bounds = FxBox_Sphere(reinterpret_cast<const float*>(emitter + 0x60), sphere);
    // The renderer's slot 1: (particle data +0xbc, count, +0x84, bounds, matrix, lights).
    using Draw = uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t, uint8_t*, float*, float*, uint32_t);
    return title_function<Draw>(vtable_slot(renderer, 1))(renderer, *reinterpret_cast<const uint32_t*>(emitter + 0xbc),
                                                         count, emitter + 0x84, bounds, matrix, lights);
}

void XB_THISCALL FxSystem_Draw(uint8_t* system, uint32_t lights) {
    uint16_t& flags = *reinterpret_cast<uint16_t*>(system + 0x4e);
    flags &= 0xfffd;
    const FxEmitters list = emitters_of(system);
    for (uint8_t* const* e = list.begin; e != list.end; ++e)
        if (*e && FxEmitter_Draw(*e, lights) == 1) flags |= 2;
}

void XB_CDECL FxBuffers_Flip() {
    int32_t& index = title_global<int32_t>(0x00909f2c);
    ++index;
    if (index >= 2) index = 0;
    title_global<uint32_t>(0x00909f3c) = title_global<uint32_t>(0x00909f18 + index * 4);
    title_global<uint32_t>(0x00909f38) = 0;
    title_global<uint32_t>(0x00909f48) = 0;
}

namespace {

template <typename T>
T& at(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pointer_at(uint8_t* base, uint32_t offset) { return at<uint8_t*>(base, offset); }

// An action's Apply (vtable slot 0): (begin, end, emitter, system, time, delta); begin
// and end are null for emitter-level actions.
void apply_action(uint32_t action, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t* system, float time,
                  float delta) {
    using Apply = void XB_THISCALL(uint32_t, uint8_t*, uint8_t*, uint8_t*, uint8_t*, float, float);
    title_function<Apply>(vtable_slot(action, 0))(action, begin, end, emitter, system, time, delta);
}

// MINPS/MAXPS lane by lane: the second operand on NaN.
Lanes lanes_min(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes lanes_max(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes splat(float x) { return {{x, x, x, x}}; }

// A particle's position with z in the last lane too (SHUFPS 0x10, 0xa8).
Lanes position_xyzz(const uint8_t* particle) {
    const float* p = reinterpret_cast<const float*>(particle);
    return {{p[0], p[1], p[2], p[2]}};
}

// A system's block (+ptr, two int16 sizes after +size) back to the particle heap, the
// effect id (0x008bb764) and the seeded flag (0x00909948) set aside meanwhile.
void free_system_block(uint8_t* system, uint32_t ptr, uint32_t size) {
    if (!at<uint32_t>(system, ptr)) return;
    const uint32_t id = title_global<uint32_t>(0x008bb764);
    const uint32_t seeded = title_global<uint32_t>(0x00909948);
    const uint32_t heap = title_global<uint32_t>(0x008bb760);
    title_global<uint32_t>(0x008bb764) = 0xffffffffu;
    title_global<uint32_t>(0x00909948) = 1;
    title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(heap, 2))(heap, at<uint32_t>(system, ptr));
    title_global<uint32_t>(0x00909948) = seeded;
    title_global<uint32_t>(0x008bb764) = id;
    at<uint32_t>(system, ptr) = 0;
    at<uint16_t>(system, size) = 0;
    at<uint16_t>(system, size + 2) = 0;
}

uint8_t* emitter_pool() { return title_global<uint8_t*>(0x00909f64) + 8; }

}  // namespace

uint32_t XB_THISCALL FxSystemDef_IsEmitterVisible(const uint8_t* definition, int32_t index) {
    const uint8_t* entries = *reinterpret_cast<const uint8_t* const*>(definition + 0x50);
    return entries[index * 0x3c + 0xc] & 1;
}

void XB_THISCALL FxParamBlock_Set(uint8_t* block, uint32_t index, float value) {
    at<float*>(block, 4)[index] = value;
}

void XB_THISCALL FxSystem_SwapParams(uint8_t* system) {
    uint8_t* entry = pointer_at(system, 0x5c);
    uint8_t* const end = entry + at<int16_t>(system, 0x62) * 0xc;
    for (; entry != end; entry += 0xc) {
        uint8_t* block = pointer_at(entry, 0);
        const uint32_t index = at<uint32_t>(entry, 4);
        const float old = at<float*>(block, 4)[index];
        FxParamBlock_Set(block, index, at<float>(entry, 8));
        at<float>(entry, 8) = old;
    }
}

void XB_THISCALL FxEmitter_FreeParticles(uint8_t* emitter, uint32_t) {
    const uint32_t particles = at<uint32_t>(emitter, 0xbc);
    const uint32_t heap = title_global<uint32_t>(0x008bb760);
    if (particles) title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(heap, 2))(heap, particles);
    const uint32_t count = at<uint32_t>(emitter, 0xc0);
    if (count) title_global<uint32_t>(0x00909ec0) -= count;
}

void XB_THISCALL apsMemoryPool_Free(uint8_t* pool, uint8_t* item) {
    if (!item) return;
    const int32_t index =
        static_cast<int32_t>(item - pointer_at(pool, 0x18)) / at<int32_t>(pool, 0x14);
    uint8_t* slot = pointer_at(pool, 0x1c) + index * 8;
    at<uint16_t>(slot, 2) = at<uint16_t>(pool, 0x20);
    at<uint16_t>(pool, 0x20) = static_cast<uint16_t>(index);
    at<uint32_t>(slot, 4) &= 0xff000000u;
    slot[7] = 0;
    --at<uint32_t>(pool, 4);
}

float XB_THISCALL FxCurve_Evaluate(const uint8_t* curve, float t) {
    const float* keys = reinterpret_cast<const float*>(curve);
    if (!*reinterpret_cast<const uint32_t*>(curve + 0x28)) return title_global<float>(0x0083eafc);
    for (int i = 0; i < 4; ++i)
        if (keys[2 + i * 2] > t) return keys[3 + i * 2];
    return platform_x87_mul(keys[1], keys[0]);
}

uint32_t XB_THISCALL FxEmitter_BeginParticles(const uint8_t* emitter, uint8_t** begin, uint8_t** end, float time) {
    uint8_t* const particles = *reinterpret_cast<uint8_t* const*>(emitter + 0xbc);
    if (!particles) return 0;
    title_global<float>(0x00909ed0) = time;
    *begin = particles;
    title_global<uint8_t*>(0x00909ec8) = particles;
    *end = *begin + *reinterpret_cast<const uint32_t*>(emitter + 0xc0) * *reinterpret_cast<const uint32_t*>(emitter + 0x88);
    // The bounds accumulator: min = FLT_MAX, max = -FLT_MAX (sign flipped with XORPS).
    float* bounds = &title_global<float>(0x00909ee0);
    const float big = title_global<float>(0x007c5000);
    for (int i = 0; i < 4; ++i) {
        bounds[i] = big;
        bounds[4 + i] = -big;
    }
    title_global<uint8_t*>(0x00909ecc) = *end;
    title_global<uint32_t>(0x00909ed4) = 0;
    return 1;
}

void XB_THISCALL FxEmitter_CompactParticles(uint8_t* emitter) {
    const uint32_t killed = title_global<uint32_t>(0x00909ed4);
    if (!killed) return;
    title_global<uint32_t>(0x00909ed4) = 0;
    const uint32_t count = at<uint32_t>(emitter, 0xc0);
    const uint32_t stride = at<uint32_t>(emitter, 0x88);
    at<uint32_t>(emitter, 0xc0) = count - killed;
    title_global<uint32_t>(0x00909ec0) -= killed;
    // The killed particles (0x00909ba0 on, in order) refilled from the end of the array.
    uint8_t* last = pointer_at(emitter, 0xbc) + (count - 1) * stride;
    uint8_t** killed_list = &title_global<uint8_t*>(0x00909b9c);
    for (uint32_t k = killed; k; --k, last -= stride) {
        uint8_t* dst = killed_list[k];
        if (dst != last)
            for (uint32_t i = 0; i < stride; ++i) dst[i] = last[i];
    }
    killed_list[0] = nullptr;
}

void XB_CDECL FxBox_GetCorners(const float* box, float* out) {
    const Lanes low = lanes(box);
    const Lanes high = lanes(box + 4);
    const Lanes extent = sub(high, low);
    const Lanes x = mul(lanes(&title_global<float>(0x007c87b0)), splat(extent.v[0]));
    const Lanes y = mul(lanes(&title_global<float>(0x007c87c0)), splat(extent.v[1]));
    const Lanes z = mul(lanes(&title_global<float>(0x007c87d0)), splat(extent.v[2]));
    const Lanes corners[8] = {low, add(low, x), add(low, y), add(low, z), sub(high, x), sub(high, y), sub(high, z), high};
    std::memcpy(out, corners, sizeof corners);
}

void XB_THISCALL FxEmitter_TransformBounds(uint8_t* emitter) {
    float* box = &at<float>(emitter, 0x60);
    alignas(16) float corners[8][4];
    FxBox_GetCorners(box, &corners[0][0]);
    Lanes low = splat(title_global<float>(0x007c5000));
    Lanes high = neg(low);
    for (const float* corner : corners) {
        alignas(16) float point[4];
        const float* moved = Mat44_TransformPoint(point, &at<float>(emitter, 0x10), corner);
        low = lanes_min(low, lanes(moved));
        high = lanes_max(high, lanes(moved));
    }
    std::memcpy(box, low.v, 16);
    std::memcpy(box + 4, high.v, 16);
}

void XB_THISCALL FxEmitter_EndParticles(uint8_t* emitter) {
    if (!at<uint32_t>(emitter, 0xbc)) return;
    std::memcpy(emitter + 0x60, &title_global<float>(0x00909ee0), 32);
    if (at<uint8_t>(emitter, 0xae) & 2)
        FxEmitter_TransformBounds(emitter);
    FxEmitter_CompactParticles(emitter);
}

void XB_THISCALL FxEmitter_UpdateVisibility(uint8_t* emitter) {
    const uint8_t* scene = title_global<const uint8_t*>(0x0090259c);  // ngl_curScene
    if (!scene) {
        at<uint8_t>(emitter, 0xae) |= 8;
        at<float>(emitter, 0xb8) = 0.0f;
        return;
    }
    const float* box = &at<float>(emitter, 0x60);
    alignas(16) float box_center[4];
    FxBox_Center(box, box_center);
    // SHUFPS leaves {x, y, z, z}.
    alignas(16) float center[4] = {box_center[0], box_center[1], box_center[2], box_center[2]};
    const float radius = FxBox_Radius(box);
    const Lanes offset = sub(lanes(center), lanes(reinterpret_cast<const float*>(scene + 0x250)));
    const Lanes squares = mul(offset, offset);
    const float distance_sq = (squares.v[0] + squares.v[1]) + squares.v[2];
    float distance = distance_sq;
    if (!(distance_sq == 0.0f || distance_sq != distance_sq)) {
        // RSQRTSS and one Newton step.
        const float estimate = platform_rsqrt_estimate(distance_sq);
        const float root = distance_sq * estimate;
        const float step = title_global<float>(0x007c5310) - root * estimate;
        distance = (step * root) * title_global<float>(0x007c5320);
    }
    float depth = distance - radius;
    if (0.0f > depth) depth = 0.0f;
    at<float>(emitter, 0xb8) = depth;
    if (nglSphereInFrustum(center, radius))
        at<uint8_t>(emitter, 0xae) |= 8;
    else
        at<uint8_t>(emitter, 0xae) &= 0xf7;
}

void XB_THISCALL FxEmitter_AdvanceParticles(uint8_t* emitter, uint8_t** begin, uint8_t** end) {
    if (!at<uint32_t>(emitter, 0xbc)) return;
    uint8_t* particle = title_global<uint8_t*>(0x00909ec8);
    uint8_t* const last = title_global<uint8_t*>(0x00909ecc);
    const uint32_t flags = at<uint32_t>(emitter, 0x84);
    const uint32_t stride = at<uint32_t>(emitter, 0x88);
    const uint32_t age = at<uint8_t>(emitter, 0x95);
    Lanes& low = *reinterpret_cast<Lanes*>(&title_global<float>(0x00909ee0));
    Lanes& high = *reinterpret_cast<Lanes*>(&title_global<float>(0x00909ef0));
    // Each particle ages by the pass time and grows the bounds by its extent.
    if (flags & 2) {
        // A sprite: +-size * sqrt(2) around all four position lanes.
        const uint32_t size = at<uint8_t>(emitter, 0x8d);
        const float sqrt2 = title_global<float>(0x0083f1f0);
        for (; particle != last; particle += stride) {
            at<float>(particle, age) += title_global<float>(0x00909ed0);
            const Lanes extent = splat(at<float>(particle, size) * sqrt2);
            const Lanes upper = add(extent, lanes(reinterpret_cast<const float*>(particle)));
            high = lanes_max(high, upper);
            low = lanes_min(low, upper);
            const Lanes lower = sub(lanes(reinterpret_cast<const float*>(particle)), extent);
            low = lanes_min(low, lower);
            high = lanes_max(high, lower);
        }
    } else if (flags & 4) {
        // Two sizes: the larger plus half the smaller.
        const uint32_t size_a = at<uint8_t>(emitter, 0x8e);
        const uint32_t size_b = at<uint8_t>(emitter, 0x93);
        const float half = title_global<float>(0x0083eb24);
        for (; particle != last; particle += stride) {
            at<float>(particle, age) += title_global<float>(0x00909ed0);
            const Lanes position = position_xyzz(particle);
            const float a = at<float>(particle, size_a);
            const float b = at<float>(particle, size_b);
            const Lanes extent = splat(a > b ? b * half + a : a * half + b);
            low = lanes_min(low, sub(position, extent));
            high = lanes_max(high, add(extent, position));
        }
    } else {
        const Lanes extent = splat(title_global<float>(0x0083eb20));
        for (; particle != last; particle += stride) {
            at<float>(particle, age) += title_global<float>(0x00909ed0);
            const Lanes position = position_xyzz(particle);
            const Lanes lower = sub(position, extent);
            const Lanes upper = add(extent, position);
            low = lanes_min(low, lower);
            high = lanes_max(high, upper);
        }
    }
    // One range per pass: the next one is empty.
    *end = nullptr;
    *begin = nullptr;
    title_global<uint8_t*>(0x00909ecc) = nullptr;
    title_global<uint8_t*>(0x00909ec8) = nullptr;
}

void XB_THISCALL FxEmitterList_Remove(uint8_t* list, uint8_t* emitter, uint32_t unused) {
    const int32_t slot = at<int16_t>(emitter, 0xac);
    FxEmitter_FreeParticles(emitter, unused);
    apsMemoryPool_Free(emitter_pool(), emitter);
    at<int16_t>(emitter, 0xac) = -1;
    if (at<int16_t>(list, 0xa) > 1) {
        uint8_t** items = at<uint8_t**>(list, 4);
        items[slot] = items[at<int16_t>(list, 0xa) - 1];
        at<int16_t>(at<uint8_t**>(list, 4)[slot], 0xac) = static_cast<int16_t>(slot);
    }
    if (at<uint16_t>(list, 0xa)) --at<uint16_t>(list, 0xa);
}

uint8_t* XB_THISCALL FxEmitterList_Add(uint8_t* list, uint32_t capacity, uint32_t renderer, uint8_t* block, uint32_t flag) {
    uint8_t* emitter = apsMemoryPool_Alloc(emitter_pool(), 0xd0);
    if (!emitter) return nullptr;
    if (!apsGroup_Create(emitter, capacity, renderer, reinterpret_cast<const uint32_t*>(block), flag)) {
        FxEmitter_FreeParticles(emitter, 0);
        apsMemoryPool_Free(emitter_pool(), emitter);
        return nullptr;
    }
    at<int16_t>(emitter, 0xac) = at<int16_t>(list, 0xa);
    const uint32_t item = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emitter));
    apsPtrVector_PushBack(&at<ApsArray>(list, 4), &item);
    return emitter;
}

void XB_CDECL FxRandom_Reset() { title_global<uint32_t>(0x0090993c) = 0; }

void XB_CDECL FxRandom_Restore() { title_global<uint32_t>(0x0090993c) = title_global<uint32_t>(0x00909f04); }

void XB_THISCALL FxSystem_Destroy(uint8_t* system) {
    uint8_t* const emitters = title_global<uint8_t*>(0x00909f60);
    const uint32_t flag = at<uint8_t>(system, 0x4e) & 4;
    const FxEmitters list = emitters_of(system);
    for (uint8_t* const* it = list.begin; it != list.end; ++it)
        if (*it) FxEmitterList_Remove(emitters, *it, flag);
    free_system_block(system, 0x5c, 0x60);
    free_system_block(system, 0x54, 0x58);
    uint8_t* const definition = pointer_at(system, 0x48);
    if (definition) --at<uint32_t>(definition, 0xb0);
    // The inlined destructor frees both again (already empty).
    free_system_block(system, 0x5c, 0x60);
    free_system_block(system, 0x54, 0x58);
}

void XB_THISCALL FxSystemList_Update(uint8_t* list, float time) {
    uint8_t** const items = reinterpret_cast<uint8_t**>(list + 4);
    int32_t& count = at<int32_t>(list, 0x1e4);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const system = items[i];
        title_global<uint32_t>(0x008bb764) = at<uint32_t>(system, 0x64);
        title_global<uint32_t>(0x00909948) = 1;
        FxRandom_Reset();
        if (!FxSystem_Update(system, time)) {
            FxSystem_Destroy(system);
            apsMemoryPool_Free(title_global<uint8_t*>(0x00909994) + 8, system);
            --count;
            if (i != count) items[i] = items[count];
            --i;
        }
        FxRandom_Restore();
        title_global<uint32_t>(0x008bb764) = 0xffffffffu;
        title_global<uint32_t>(0x00909948) = 0;
    }
}

void XB_CDECL FX_DrawGlobalSystems() {
    FxSystemList_Update(&title_global<uint8_t>(0x009099a8), title_global<float>(0x0095f6e0));
    NglLightSet* const lights = nglCreateLightSet();
    const float ambient = from_bits(0x3ecccccdu);  // 0.4
    nglSetAmbientColor(ambient, ambient, ambient);
    for (int32_t i = 0; i < title_global<int32_t>(0x00909b8c); ++i)
        FxSystem_Draw(title_global<uint8_t*>(0x009099ac + i * 4), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lights)));
}

void XB_THISCALL FxEmitterDef_Update(uint8_t* definition, uint8_t* emitter, uint8_t* system, float age, float delta,
                                     uint32_t stopping) {
    (void)delta;
    FxEmitter_UpdateVisibility(emitter);
    const float max_step = title_global<float>(0x007c6490);
    const uint32_t visible = at<uint8_t>(emitter, 0xae) & 8;
    float step = age - at<float>(emitter, 0xb0);
    float wait = 0.0f;
    if (step > max_step) {
        step = max_step;
    } else if (!visible) {
        // Hidden emitters update every `wait` seconds (a curve over their depth).
        if (at<float>(emitter, 0xb4) > step) return;
        const float curve = FxCurve_Evaluate(&title_global<uint8_t>(0x00909910), at<float>(emitter, 0xb8));
        wait = platform_x87_greater(max_step, curve) ? curve : max_step;
    }
    at<float>(emitter, 0xb4) = wait;
    at<float>(emitter, 0xb0) = age;
    title_global<uint8_t*>(0x00909f10) = definition;
    uint32_t* const first = at<uint32_t*>(definition, 0);
    uint32_t* const last = first + at<int16_t>(definition, 6);
    // Emitter-level actions (+0x14 clear) first, skipped while stopping; the rest run
    // over every particle range.
    uint32_t* action = first;
    for (; action != last; ++action) {
        uint8_t* const object = address_to_pointer(*action);
        if (at<uint32_t>(object, 0x14)) break;
        if (stopping) continue;
        const float* window = at<float*>(object, 4);
        if (age >= window[0] && window[1] >= age) apply_action(*action, nullptr, nullptr, emitter, system, age, step);
    }
    if (action == last) return;
    uint8_t* begin;
    uint8_t* end;
    if (!FxEmitter_BeginParticles(emitter, &begin, &end, step)) return;
    while (begin != end) {
        for (uint32_t* it = action; it != last; ++it) {
            const float* window = at<float*>(address_to_pointer(*it), 4);
            if (age >= window[0] && window[1] >= age) apply_action(*it, begin, end, emitter, system, age, step);
        }
        FxEmitter_AdvanceParticles(emitter, &begin, &end);
    }
    FxEmitter_EndParticles(emitter);
}

void XB_THISCALL FxEmitterDef_Finish(uint8_t* definition, uint8_t* system, uint8_t* emitter) {
    if (!at<uint32_t>(definition, 8)) return;
    uint8_t* begin;
    uint8_t* end;
    if (!FxEmitter_BeginParticles(emitter, &begin, &end, 0.0f)) return;
    while (begin != end) {
        apply_action(at<uint32_t>(definition, 8), begin, end, emitter, system, 0.0f, from_bits(0x460ae38eu));  // 8888.89
        FxEmitter_AdvanceParticles(emitter, &begin, &end);
    }
    FxEmitter_EndParticles(emitter);
}

uint32_t XB_THISCALL FxSystem_Update(uint8_t* system, float time) {
    const float age = time - at<float>(system, 0x40);
    const float delta = age - at<float>(system, 0x44);
    uint32_t result = 1;
    if (0.0f > age || 0.0f > delta) return result;
    if (at<int16_t>(system, 0x62) > 0) FxSystem_SwapParams(system);
    title_global<float>(0x00909998) = time;
    const FxEmitters list = emitters_of(system);
    int32_t index = 0;
    for (uint8_t* const* it = list.begin; it != list.end; ++it, ++index) {
        uint8_t** slot = const_cast<uint8_t**>(it);
        uint8_t* definition = pointer_at(system, 0x48);
        uint8_t* entry = pointer_at(definition, 0x50) + index * 0x3c;
        const float end_time = at<float>(entry, 8);
        uint8_t* emitter = *slot;
        uint8_t* const emitters = title_global<uint8_t*>(0x00909f60);
        if (emitter) {
            uint16_t flags = at<uint16_t>(emitter, 0xae);
            if ((flags >> 2) & 1) {
                FxEmitterList_Remove(emitters, emitter, 0);
                *slot = nullptr;
                result = 0;
                continue;
            }
            const uint32_t renderer = at<uint32_t>(entry, 0x10);
            at<uint32_t>(emitter, 0) = renderer;
            flags &= 0xfffe;
            if (renderer && FxSystemDef_IsEmitterVisible(definition, index)) flags |= 1;
            at<uint16_t>(emitter, 0xae) = flags;
            entry = pointer_at(pointer_at(system, 0x48), 0x50) + index * 0x3c;
            const uint32_t updater = at<uint32_t>(entry, 0x14);
            FxEmitterDef_Update(address_to_pointer(updater), emitter, system, age - at<float>(entry, 4), delta,
                                at<uint32_t>(system, 0x4c) & 1);
            if (age >= end_time) {
                FxEmitterDef_Finish(address_to_pointer(updater), system, emitter);
                at<uint8_t>(emitter, 0xae) |= 4;
            }
            continue;
        }
        const float start = at<float>(entry, 4);
        if (at<uint8_t>(system, 0x4c) & 1) continue;
        if (!(age >= start) || !(end_time > age)) continue;
        emitter = FxEmitterList_Add(emitters, at<uint32_t>(entry, 0), at<uint32_t>(entry, 0x10), entry + 0x18,
                                    (at<uint32_t>(entry, 0xc) >> 1) & 1);
        if (!emitter) continue;
        if (at<uint8_t>(system, 0x4e) & 1) {
            const uint32_t renderer = at<uint32_t>(emitter, 0);
            if (renderer && !title_function<uint32_t XB_THISCALL(uint32_t)>(vtable_slot(renderer, 4))(renderer)) {
                const uint32_t again = at<uint32_t>(emitter, 0);
                if (again) title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(again, 5))(again, system + 0x20);
            }
        }
        FxEmitter_SetMatrix(emitter, reinterpret_cast<const float*>(system));
        const float local_age = age - start;
        *slot = emitter;
        // Bits 1-15 of +0x4c count the spawned emitters.
        uint32_t& state = at<uint32_t>(system, 0x4c);
        state ^= ((((state & 0xffffu) & ~1u) + 2) ^ state) & 0xfffe;
        at<float>(emitter, 0xb0) = local_age;
        const uint32_t updater = at<uint32_t>(pointer_at(pointer_at(system, 0x48), 0x50) + index * 0x3c, 0x14);
        FxEmitterDef_Update(address_to_pointer(updater), emitter, system, local_age, local_age, 0);
    }
    if (at<int16_t>(system, 0x62) > 0) FxSystem_SwapParams(system);
    at<float>(system, 0x44) = age;
    return result;
}

namespace {

float abs_float(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, 4);
    return x;
}

// The series the particle code uses for asin(x), 0 <= x < 0.5.
float asin_series(float x) {
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;
    const float x7 = x5 * x2;
    return ((x7 * title_global<float>(0x0083ee94) + x5 * title_global<float>(0x0083ee98)) +
            x3 * title_global<float>(0x0083ee9c)) +
           x;
}

// asin(x) for x >= 0.5 from s = sqrt((1 - x) / 2) (pi/2 - 2 s - ...).
float asin_upper(float x) {
    const float s = platform_x87_sqrt_abs_sub_mul(title_global<float>(0x0083eb20), x, title_global<float>(0x0083eb24));
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float twice = s * title_global<float>(0x0083eba8);
    const float s5 = s3 * s2;
    const float s7 = s5 * s2;
    return (((s7 * title_global<float>(0x0083ee8c) - s5 * title_global<float>(0x0083ed2c)) -
             s3 * title_global<float>(0x0083ee90)) -
            twice) +
           title_global<float>(0x0083ee88);
}

}  // namespace

void XB_CDECL FxSystem_SetView(const float* view, float scale) {
    title_global<float>(0x00909984) = scale;
    // The view's columns: x scaled into 0x00909970, y into 0x00909960, z into 0x00909950.
    float* const z_column = &title_global<float>(0x00909950);
    float* const y_column = &title_global<float>(0x00909960);
    float* const x_column = &title_global<float>(0x00909970);
    for (int i = 0; i < 4; ++i) {
        z_column[i] = view[i * 4 + 2];
        y_column[i] = view[i * 4 + 1];
        x_column[i] = view[i * 4] * scale;
    }
    // The view's heading from (y_column[2], x_column[2]) by an atan2 series; kept when
    // both are 0.
    const float a = y_column[2];
    const float b = x_column[2];
    if (!(b == 0.0f) || !(a == 0.0f)) {
        const float abs_a = abs_float(a);
        const float abs_b = abs_float(b);
        float angle = 0.0f;
        if (!(platform_x87_add(abs_a, abs_b) == 0.0f)) {
            const float inverse = platform_x87_rcp_hypot(a, b, title_global<float>(0x0083eb20));
            const float half = title_global<float>(0x0083eb24);
            if (abs_b > abs_a) {
                const float x = abs_a * inverse;
                angle = title_global<float>(0x0083ed0c) - (half > x ? asin_series(x) : asin_upper(x));
            } else {
                const float x = abs_b * inverse;
                angle = half > x ? asin_series(x) : asin_upper(x);
            }
            if (0.0f > a) angle = title_global<float>(0x007c4c60) - angle;
            if (0.0f > b) angle = 0.0f - angle;
        }
        title_global<float>(0x00909980) = angle;
    }
    FxBuffers_Flip();
}

namespace {

// The effect pool's free list: count 0x00988810, entries from 0x00988814 (0xf0).
void FX_PushFreeEffect(FxEffect* effect) {
    uint32_t& free_count = title_global<uint32_t>(0x00988810);
    effect->unknown00 = (effect->unknown00 & 0xffff0000u) | (free_count & 0xffff);
    if (static_cast<int32_t>(free_count) < 0xf0) {
        title_global<uint32_t>(0x00988814 + free_count * 4) = effect->unknown00;
        ++free_count;
    }
}

uint8_t* handle_object(uint32_t handle, uint32_t table) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540) return nullptr;
    const uint32_t* slot = &title_global<uint32_t>(table + index * 8);
    if ((handle >> 12) != slot[1]) return nullptr;
    return reinterpret_cast<uint8_t*>(uintptr_t{slot[0]});
}

}  // namespace

void XB_CDECL FX_DrawEffects() {
    FX_FreeFinishedEffects();
    // The frame's step, clamped to 0.05; particles update only for a positive one
    // (NaN included: comiss/jb) while the game is not paused (0x0095f6c0).
    bool update = true;
    float step;
    if (title_global<uint32_t>(0x0095f6c0)) {
        step = 0.0f;
        update = false;
    } else {
        step = title_global<float>(0x008ddaa0);
        const float limit = title_global<float>(0x0083edc8);
        if (step > limit) {
            step = limit;
        } else if (!(0.0f < step) && !(step != step)) {
            update = false;
        }
    }
    float& clock = title_global<float>(0x0095f6e0);
    clock = clock + step;
    alignas(16) float view[16];
    // The -1 is nglGetMatrix's unread third argument, left on the stack for this call.
    FxSystem_SetView(nglGetMatrix(view, 2), -1.0f);
    bool freed = false;
    NglLightSet* lights = nglCreateLightSet();
    nglSetAmbientColor(0.4f, 0.4f, 0.4f);
    FxEffect** const begin = title_global<FxEffect**>(0x00988ce8);
    FxEffect** const end = begin + title_global<uint32_t>(0x00988cf0);
    for (FxEffect** it = begin; it != end; ++it) {
        FxEffect* effect = *it;
        if (!effect || ((effect->unknown16 >> 1) & 1)) continue;
        const uint32_t system = effect->unknown04;
        uint8_t* const system_object = reinterpret_cast<uint8_t*>(uintptr_t{system});
        if (!system || FxSystem_IsFinished(system_object)) {
            *it = nullptr;
            title_function<void XB_THISCALL(FxEffect*)>(0x00029560)(effect);  // FX_ReleaseEffectHandle
            FX_PushFreeEffect(effect);
            freed = true;
            continue;
        }
        uint8_t* bolt_object = handle_object(effect->unknown0c, 0x00981418);
        uint8_t* entity = handle_object(effect->unknown10, 0x0097a968);
        bool placed = bolt_object || entity;
        if (!placed && (effect->unknown16 & 1))
            FxSystem_Stop(system_object);
        if (placed || effect->unknown08) {
            alignas(16) float matrix[16];
            const float* place = nullptr;
            if (effect->unknown08) {
                place = reinterpret_cast<const float*>(uintptr_t{effect->unknown08});
            } else if (static_cast<int16_t>(effect->unknown14) >= 0) {
                float bolt[12];
                if (FX_GetBoltMatrix(bolt, effect->unknown0c, static_cast<int16_t>(effect->unknown14))) {
                    // {origin, axis[3]} -> rows axis0, axis1, axis2, origin ({x y z x} each).
                    for (int row = 0; row < 4; ++row) {
                        const float* src = &bolt[((row + 1) & 3) * 3];
                        matrix[row * 4 + 0] = src[0];
                        matrix[row * 4 + 1] = src[1];
                        matrix[row * 4 + 2] = src[2];
                        matrix[row * 4 + 3] = src[0];
                    }
                    place = matrix;
                }
            } else if (entity) {
                const float* origin = reinterpret_cast<const float*>(entity + 0x140);
                if ((effect->unknown16 >> 2) & 1) {
                    // Unrotated at the entity's origin.
                    static const float kIdentity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                    std::memcpy(matrix, kIdentity, sizeof kIdentity);
                    matrix[12] = origin[0];
                    matrix[13] = origin[1];
                    matrix[14] = origin[2];
                    matrix[15] = origin[0];
                } else {
                    // The entity's angles (+0x150) in radians, lanes {2, 0, 1, 2}.
                    const float* angles = reinterpret_cast<const float*>(entity + 0x150);
                    const float pi = title_global<float>(0x0083ed00);
                    const float half_turn = title_global<float>(0x0083eb60);
                    const float roll = angles[2] * pi / half_turn;
                    alignas(16) const float radians[4] = {roll, angles[0] * pi / half_turn,
                                                          angles[1] * pi / half_turn, roll};
                    title_function<void XB_REGS3 XB_CDECL(const float*, uint32_t, const float*, float*)>(0x0029ff50)(
                        origin, 0, radians, matrix);  // FX_AnglesToMatrix
                }
                place = matrix;
            }
            if (place) FxSystem_SetMatrix(system_object, place);
        }
        if (update) {
            const uint32_t clock_bits = title_global<uint32_t>(0x0095f6e0);
            const int32_t seed = effect->unknown18;
            using Update = void XB_THISCALL(uint32_t, uint32_t);
            if (seed == -1) {
                title_function<Update>(0x00094c60)(system, clock_bits);  // FxSystem_Update
            } else {
                // Repeatable randomness per effect id.
                title_global<int32_t>(0x008bb764) = seed;
                title_global<uint32_t>(0x00909948) = 1;
                title_global<uint32_t>(0x0090993c) = 0;  // FxRandom_Reset (0x0009b9c0)
                title_function<Update>(0x00094c60)(system, clock_bits);
                title_global<uint32_t>(0x0090993c) = title_global<uint32_t>(0x00909f04);  // FxRandom_Restore (0x0009bec0)
                title_global<int32_t>(0x008bb764) = -1;
                title_global<uint32_t>(0x00909948) = 0;
            }
        }
        FxSystem_UpdateSortKey(system_object);
        if (!(title_global<uint8_t>(0x00901e8d) & 8))
            FxSystem_Draw(system_object, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lights)));
    }
    FX_DrawGlobalSystems();
    if (freed) {
        // Drop the freed entries, keeping the order.
        FxEffect** const base = title_global<FxEffect**>(0x00988ce8);
        uint32_t& count = title_global<uint32_t>(0x00988cf0);
        FxEffect** out = base;
        FxEffect** const last = base + count;
        while (out != last && *out) ++out;
        if (out != last) {
            for (FxEffect** in = out + 1; in != last; ++in)
                if (*in) *out++ = *in;
            count = static_cast<uint32_t>(out - base);
        }
    }
    FX_SortEffects();
}

uint32_t XB_REGS1 XB_CDECL FX_PlayBoltedEffect(const float* origin, uint32_t effect, uint32_t entity,
                                               uint32_t owner, uint32_t bone) {
    // {origin, axis0, axis1, axis2}
    float bolt[12] = {origin[0], origin[1], origin[2]};
    if (entity && !FX_GetBoltMatrix(bolt, entity, static_cast<int32_t>(bone))) return 0;
    alignas(16) float matrix[16];
    for (int row = 0; row < 4; ++row) {
        const float* src = &bolt[((row + 1) & 3) * 3];
        matrix[row * 4 + 0] = src[0];
        matrix[row * 4 + 1] = src[1];
        matrix[row * 4 + 2] = src[2];
        matrix[row * 4 + 3] = src[0];
    }
    return title_function<uint32_t XB_CDECL(uint32_t, float*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0023c040)(
        effect, matrix, entity, owner, bone, bone != 0);  // FX_SpawnEffect
}

PLATFORM_ASM_ENTRY(FX_ParseEffectTexture_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FX_ParseEffectTexture) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL FX_ParseEffectTexture(uint32_t* stream, uint32_t* out) {
    // The name buffer; a length past 0x7f is written past it in the original too.
    char name[0x100] = {};
    int32_t length = 0;
    uint8_t hashed[0x20];
    const auto hash_name = [&]() {
        Stream_ReadString(stream, name, &length, 0x7f);
        name[static_cast<uint8_t>(length)] = 0;
        return title_function<char* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
    };
    switch (Stream_ReadInt(stream)) {
    case 0:
        out[0] = 0;
        out[1] = reinterpret_cast<uintptr_t>(nglFindTexture(hash_name()));
        break;
    case 1:
        out[1] = reinterpret_cast<uintptr_t>(nglFindMesh(hash_name()));
        out[0] = 1;
        break;
    case 2:
        out[1] = Stream_ReadUInt(stream);
        out[0] = 2;
        break;
    case 3:
        out[1] = Stream_ReadInt(stream);
        out[0] = 3;
        break;
    case 4: {
        const float value = Stream_ReadFloat(stream);
        std::memcpy(&out[1], &value, 4);
        out[0] = 4;
        break;
    }
    case 5: {
        const float a = Stream_ReadFloat(stream);
        const float b = Stream_ReadFloat(stream);
        const float c = Stream_ReadFloat(stream);
        std::memcpy(&out[3], &c, 4);
        std::memcpy(&out[2], &b, 4);
        out[0] = 5;
        std::memcpy(&out[1], &a, 4);
        break;
    }
    case 8:
        out[1] = Stream_ReadUInt(stream);
        out[0] = 8;
        break;
    default:
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        out[1] = 0;
        out[0] = 2;
        break;
    }
    return out;
}

namespace {

// FX_UpdateEffect (0x0029b8f0): instance in EBX, delta on the stack (stdcall).
// Each callee runs where the original's call puts it (ESP = entry - 16 before the
// push): FX_AddParticleComponents, under FX_SpawnQueuedEffect, stores the address of
// a stack local in 0x00971800, FX_AddSoundComponents one in 0x0096d8f8.
struct EffectsFrame {
    uint32_t top;  // the original's ESP after its four pushes
    uint32_t call(uint32_t function, const TitleRegs& regs, uint32_t arg) const {
        return title_call_at_regs(top, function, &regs, 1, &arg);
    }
    // Our own callees leave other values on the dead stack than the originals: zero
    // what they used, as the harness has it at a test's start.
    void call_ours(uint32_t function, const TitleRegs& regs, uint32_t arg) const {
        call(function, regs, arg);
        constexpr uint32_t kScrub = 16 * 1024;
        std::memset(reinterpret_cast<void*>(uintptr_t{top - 8 - kScrub}), 0, kScrub);
    }
};

uint32_t code_address(void (*function)()) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function));
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_UpdateEffects_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "leal 4(%ebp), %ecx\n"
                   "subl $0x20000, %esp\n"
                   "pushl 8(%ebp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(FX_UpdateEffects_body) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $4")

void XB_CDECL FX_UpdateEffects_body(uint32_t entry, FxIdList* list, uint32_t delta) {
    const EffectsFrame at{entry - 16};
    const uint32_t list_address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
    at.call_ours(code_address(&FX_UpdateFadingComponents_entry), TitleRegs{list_address, 0, 0, 0, 0, list_address},
                 delta);
    uint32_t* const words = &list->count;
    for (uint32_t i = 0; i < list->count; ++i) {
        uint8_t* item = list->items[i];
        const uint32_t item_address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
        at.call(0x0029b8f0, TitleRegs{list->count, item_address, delta, 0, 0, list_address}, delta);  // FX_UpdateEffect
        item = list->items[i];
        if ((*reinterpret_cast<const uint32_t*>(item + 0x50) >> 1) & 1) continue;
        if (*reinterpret_cast<const uint32_t*>(item)) continue;
        if (item) {
            const uint32_t freed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
            at.call_ours(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FX_FreeEffectComponents)),
                         TitleRegs{0, freed, 0, 0, 0, list_address}, freed);
            const uint32_t heap = title_global<uint32_t>(0x0095f5bc);
            at.call(0x00044910, TitleRegs{0, freed, heap, 0, 0, list_address}, freed);  // SmallHeap_Free
        }
        list->items[i] = reinterpret_cast<uint8_t*>(words[list->count]);
        if (list->count) --list->count;
        --i;
    }
    uint32_t& queued = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(list) + 0x910);
    for (uint32_t i = 0; i < queued; ++i) {
        const uint32_t spawn = list_address + 0x920 + i * 0x210;
        at.call(0x002a5810, TitleRegs{queued, spawn, 0, 0, i, list_address}, spawn);  // FX_SpawnQueuedEffect
    }
    queued = 0;
}

namespace {

// The particle heap (0x008bb760): slot 1 Alloc(size, align), slot 2 Free(ptr), slot 3
// Resize(ptr, size, align) in place (nonzero on success).
uint32_t particle_heap() { return title_global<uint32_t>(0x008bb760); }

// A heap call with the effect id (0x008bb764) -1 and the seeded flag (0x00909948) 1
// meanwhile: the effect arrays' storage.
template <typename F>
uint32_t untracked(F call) {
    const uint32_t id = title_global<uint32_t>(0x008bb764);
    const uint32_t seeded = title_global<uint32_t>(0x00909948);
    title_global<uint32_t>(0x008bb764) = 0xffffffffu;
    title_global<uint32_t>(0x00909948) = 1;
    const uint32_t result = call();
    title_global<uint32_t>(0x00909948) = seeded;
    title_global<uint32_t>(0x008bb764) = id;
    return result;
}

void apsLog(const char* format, uint32_t a = 0, uint32_t b = 0) {
    title_function<void XB_CDECL(uint32_t, int32_t, const char*, ...)>(0x0009d000)(  // apsLog_Printf
        title_global<uint32_t>(0x00909908), 1, format, a, b);
}

// The arrays' bodies, one per element size (4: apsPtrArray_*, 12: apsParamArray_*).
template <uint32_t Size>
uint8_t* array_alloc(uint32_t count) {
    const uint32_t heap = particle_heap();
    return reinterpret_cast<uint8_t*>(untracked([&] {
        return title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(vtable_slot(heap, 1))(
            heap, count * Size, 4);
    }));
}

template <uint32_t Size>
uint32_t array_resize(ApsArray* array, uint32_t count) {
    if (!array->data) return 0;
    const uint32_t heap = particle_heap();
    return untracked([&] {
        return title_function<uint32_t XB_THISCALL(uint32_t, uint8_t*, uint32_t, uint32_t)>(vtable_slot(heap, 3))(
            heap, array->data, count * Size, 4);
    });
}

// Moved to new storage of `capacity` (count kept); 0 when the heap is out.
template <uint32_t Size>
uint32_t array_regrow(ApsArray* array, int32_t capacity, int32_t count, int32_t used) {
    uint8_t* data = array_alloc<Size>(capacity);
    if (!data) return 0;
    std::memcpy(data, array->data, static_cast<uint32_t>(count > 0 ? count : 0) * Size);
    apsArray_Free(array);
    array->data = data;
    array->capacity = static_cast<int16_t>(capacity);
    array->count = static_cast<int16_t>(used);
    return 1;
}

template <uint32_t Size>
uint32_t array_reserve(ApsArray* array, int32_t capacity) {
    if (capacity <= array->capacity) return 1;
    const int32_t count = array->count;
    if (array_resize<Size>(array, capacity)) {
        array->capacity = static_cast<int16_t>(capacity);
        return 1;
    }
    return array_regrow<Size>(array, capacity, count, count);
}

template <uint32_t Size>
uint32_t array_push_back(ApsArray* array, const void* item) {
    if (array->count >= array->capacity) {
        const int32_t count = array->count;
        const int32_t capacity = count > 3 ? count + 4 : count + 1;
        if (array_resize<Size>(array, capacity)) {
            array->capacity = static_cast<int16_t>(capacity);
        } else {
            if (!array_regrow<Size>(array, capacity, count, count)) return 0;
            std::memcpy(array->data + count * Size, item, Size);
            ++array->count;
            return 1;
        }
    }
    if (!array->data) return 0;
    std::memcpy(array->data + array->count * Size, item, Size);
    ++array->count;
    return 1;
}

}  // namespace

uint8_t* XB_THISCALL apsMemoryPool_Alloc(uint8_t* pool, uint32_t size) {
    const int32_t index = at<int16_t>(pool, 0x20);
    if (index == -1) {
        apsLog("apsMemory : pool(size=%d, count=%d) is empty", at<uint32_t>(pool, 0x14), at<uint32_t>(pool, 0));
        return nullptr;
    }
    uint8_t* slot = pointer_at(pool, 0x1c) + index * 8;
    at<uint16_t>(pool, 0x20) = at<uint16_t>(slot, 2);
    at<uint32_t>(slot, 4) ^= (at<uint32_t>(slot, 4) ^ size) & 0xffffffu;
    at<uint16_t>(slot, 2) = 0xffff;
    slot[7] = 1;
    const int32_t used = ++at<int32_t>(pool, 4);
    if (used > at<int32_t>(pool, 8)) at<int32_t>(pool, 8) = used;
    if (static_cast<int32_t>(size) < at<int32_t>(pool, 0xc)) at<uint32_t>(pool, 0xc) = size;
    if (at<int32_t>(pool, 0x10) < static_cast<int32_t>(size)) at<uint32_t>(pool, 0x10) = size;
    return pointer_at(pool, 0x18) + at<int32_t>(pool, 0x14) * index;
}

uint32_t XB_THISCALL apsPtrVector_PushBack(ApsArray* array, const uint32_t* item) {
    if (array->count < array->capacity) {
        reinterpret_cast<uint32_t*>(array->data)[array->count] = *item;
        ++array->count;
        return 1;
    }
    const int32_t count = array->count;
    const int32_t capacity = count > 3 ? count + 4 : count + 1;
    const uint32_t heap = particle_heap();
    uint32_t seeded = title_global<uint32_t>(0x00909948);
    title_global<uint32_t>(0x00909948) = 0;
    uint8_t* data = reinterpret_cast<uint8_t*>(title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(
        vtable_slot(heap, 1))(heap, static_cast<uint32_t>(capacity) * 4, 4));
    title_global<uint32_t>(0x00909948) = seeded;
    if (!data) return 0;
    for (int32_t i = 0; i < count; ++i)
        reinterpret_cast<uint32_t*>(data)[i] = reinterpret_cast<const uint32_t*>(array->data)[i];
    if (array->data) {
        seeded = title_global<uint32_t>(0x00909948);
        const uint32_t heap_now = particle_heap();
        title_global<uint32_t>(0x00909948) = 0;
        title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(heap_now, 2))(heap_now, array->data);
        title_global<uint32_t>(0x00909948) = seeded;
        array->data = nullptr;
        array->capacity = 0;
        array->count = 0;
    }
    array->capacity = static_cast<int16_t>(capacity);
    array->count = static_cast<int16_t>(count);
    array->data = data;
    reinterpret_cast<uint32_t*>(data)[count] = *item;
    ++array->count;
    return 1;
}

void XB_THISCALL apsArray_Free(ApsArray* array) {
    if (!array->data) return;
    const uint32_t heap = particle_heap();
    untracked([&] {
        title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(heap, 2))(heap, array->data);
        return 0u;
    });
    array->data = nullptr;
    array->capacity = 0;
    array->count = 0;
}

uint8_t* XB_THISCALL apsPtrArray_Alloc(ApsArray*, uint32_t count, uint32_t) { return array_alloc<4>(count); }
uint32_t XB_THISCALL apsPtrArray_Resize(ApsArray* array, uint32_t count, uint32_t) {
    return array_resize<4>(array, count);
}
uint32_t XB_THISCALL apsPtrArray_Reserve(ApsArray* array, int32_t capacity) { return array_reserve<4>(array, capacity); }
uint32_t XB_THISCALL apsPtrArray_PushBack(ApsArray* array, const uint32_t* item) {
    return array_push_back<4>(array, item);
}
uint8_t* XB_THISCALL apsParamArray_Alloc(ApsArray*, uint32_t count, uint32_t) { return array_alloc<12>(count); }
uint32_t XB_THISCALL apsParamArray_Resize(ApsArray* array, uint32_t count, uint32_t) {
    return array_resize<12>(array, count);
}
uint32_t XB_THISCALL apsParamArray_Reserve(ApsArray* array, int32_t capacity) {
    return array_reserve<12>(array, capacity);
}
uint32_t XB_THISCALL apsParamArray_PushBack(ApsArray* array, const uint32_t* item) {
    return array_push_back<12>(array, item);
}

uint32_t XB_THISCALL apsGroup_Create(uint8_t* emitter, uint32_t capacity, uint32_t renderer, const uint32_t* block,
                                     uint32_t flag) {
    at<uint32_t>(emitter, 0) = renderer;
    at<float>(emitter, 0x80) = 0.0f;
    std::memcpy(emitter + 0x84, block, 9 * 4);
    at<uint32_t>(emitter, 0xa8) = capacity;
    at<uint32_t>(emitter, 0xc0) = 0;
    at<uint32_t>(emitter, 0xc4) = 0;
    at<float>(emitter, 0x58) = 0.0f;
    at<float>(emitter, 0x54) = 0.0f;
    at<float>(emitter, 0x50) = 0.0f;
    // An empty box: min = FLT_MAX, max = -FLT_MAX (sign flipped with XORPS).
    const uint32_t big = title_global<uint32_t>(0x007c5000);
    for (int i = 0; i < 4; ++i) {
        at<uint32_t>(emitter, 0x60 + i * 4) = big;
        at<uint32_t>(emitter, 0x70 + i * 4) = big ^ 0x80000000u;
    }
    const uint32_t size = at<uint32_t>(emitter, 0x88) * at<uint32_t>(emitter, 0xa8);
    at<uint16_t>(emitter, 0xac) = 0xffff;
    at<uint16_t>(emitter, 0xae) = flag ? 3 : 1;
    const uint32_t heap = particle_heap();
    at<uint32_t>(emitter, 0xbc) =
        title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(vtable_slot(heap, 1))(heap, size, 0x80);
    if (!at<uint32_t>(emitter, 0xbc)) {
        apsLog("apsGroup : failed to create particles");
        return 0;
    }
    title_function<void XB_CDECL(float*)>(0x0001c140)(&at<float>(emitter, 0x10));  // Matrix_Identity
    at<float>(emitter, 0xb0) = 0.0f;
    at<float>(emitter, 0xb4) = 0.0f;
    at<float>(emitter, 0xb8) = 0.0f;
    return 1;
}

float* XB_CDECL Mat44_TransformPoint(float* out, const float* matrix, const float* point) {
    const Lanes p = lanes(point);
    const Lanes r = add(add(add(lanes(matrix + 12), mul(lanes(matrix), p.v[0])), mul(lanes(matrix + 4), p.v[1])),
                        mul(lanes(matrix + 8), p.v[2]));
    for (int i = 0; i < 4; ++i) out[i] = r.v[i];
    return out;
}

uint32_t XB_THISCALL apsEffect_Init(uint8_t* system, uint8_t* definition, float time) {
    at<float>(system, 0x40) = time;
    at<uint32_t>(system, 0x4c) &= 0xffff0000u;
    at<uint32_t>(system, 0x64) = title_global<uint32_t>(0x008bb764);
    at<float>(system, 0x44) = 0.0f;
    at<uint8_t*>(system, 0x48) = definition;
    at<uint16_t>(system, 0x4e) = 0;
    title_function<void XB_CDECL(float*)>(0x0001c140)(reinterpret_cast<float*>(system));  // Matrix_Identity
    uint8_t* def = pointer_at(system, 0x48);
    int32_t emitters = at<int16_t>(def, 0x56);
    ApsArray* const emitter_array = &at<ApsArray>(system, 0x54);
    if (emitters == 0) {
        apsLog("apsEffect : effect %s has no elements", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(def + 0x6c)));
    } else if (!apsPtrArray_Reserve(emitter_array, emitters)) {
        ++at<uint32_t>(pointer_at(system, 0x48), 0xb8);
        return 0;
    }
    for (; emitters > 0; --emitters) {
        const uint32_t none = 0;
        apsPtrArray_PushBack(emitter_array, &none);
    }
    const int32_t overrides = at<int16_t>(pointer_at(system, 0x48), 0x5e);
    if (overrides) {
        ApsArray* const params = &at<ApsArray>(system, 0x5c);
        if (!apsParamArray_Reserve(params, overrides)) {
            ++at<uint32_t>(pointer_at(system, 0x48), 0xbc);
            return 0;
        }
        for (int32_t i = 0; i < overrides; ++i) {
            uint8_t* d = pointer_at(system, 0x48);
            uint8_t* entry = pointer_at(d, 0x58) + i * 0x2c;
            uint8_t* element = pointer_at(d, 0x50) + at<uint32_t>(entry, 0x20) * 0x3c;
            uint8_t* block = at<uint8_t**>(pointer_at(element, 0x14), 0)[at<uint32_t>(entry, 0x24)];
            const uint32_t index = at<uint32_t>(entry, 0x28);
            uint32_t record[3] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block)), index,
                                  at<uint32_t*>(block, 4)[index]};
            apsParamArray_PushBack(params, record);
        }
    }
    uint8_t* stats = pointer_at(system, 0x48) + 0xac;
    ++at<uint32_t>(stats, 0);
    ++at<uint32_t>(stats, 0);
    const int32_t live = ++at<int32_t>(stats, 4);
    if (live > at<int32_t>(stats, 8)) at<int32_t>(stats, 8) = live;
    return 1;
}

uint8_t* XB_CDECL apsEffect_Create(uint8_t* definition, float time) {
    uint8_t* system = apsMemoryPool_Alloc(title_global<uint8_t*>(0x00909994) + 8, 0x70);
    if (!system) {
        apsLog("apsEffect : pool is empty");
        return nullptr;
    }
    if (apsEffect_Init(system, definition, time)) return system;
    apsLog("apsEffect : failed to create effect %s", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(definition + 0x6c)));
    FxSystem_Destroy(system);
    return nullptr;
}

uint8_t* XB_THISCALL apsPoolSet_Alloc(ApsArray* set, int32_t size, uint32_t) {
    if (!size) return nullptr;
    uint8_t* const base = reinterpret_cast<uint8_t*>(set);
    if (size > at<int32_t>(base, 0x18)) at<int32_t>(base, 0x18) = size;
    // The pool with exactly this item size and free items, else the closest bigger one.
    uint8_t** pool = reinterpret_cast<uint8_t**>(set->data);
    uint8_t** const end = pool + set->count;
    uint8_t* best = nullptr;
    int32_t slack = 0x40000000;
    for (; pool != end; ++pool) {
        uint8_t* candidate = *pool;
        const int32_t diff = at<int32_t>(candidate, 0x14) - size;
        if (diff == 0) {
            if (best && at<int32_t>(best, 0) != at<int32_t>(best, 4)) continue;
        } else {
            if (diff < 0 || diff >= slack) continue;
            slack = diff;
        }
        best = candidate;
    }
    if (!best) return nullptr;
    uint8_t* item = apsMemoryPool_Alloc(best, static_cast<uint32_t>(size));
    if (!item) {
        ++at<int32_t>(base, 0x14);
        at<int32_t>(base, 0xc) += size;
        return nullptr;
    }
    ++at<int32_t>(base, 0x10);
    at<int32_t>(base, 8) += size;
    return item;
}

uint8_t* XB_CDECL apsAlloc(int32_t size, uint32_t unused) {
    return apsPoolSet_Alloc(title_global<ApsArray*>(0x00909f08), size, unused);
}

void XB_CDECL Mat44_FromQuatTranslation(float* out, const float* quat, const float* translation) {
    const float scale = title_global<float>(0x0083f1f0);  // sqrt 2
    const float one = title_global<float>(0x0083eb20);
    const float a = quat[0] * scale, b = quat[1] * scale, c = quat[2] * scale, d = quat[3] * scale;
    const float aa = a * a, bb = b * b, cc = c * c;
    const float ab = a * b, ac = a * c, bc = c * b;
    const float da = d * a, db = d * b, dc = d * c;
    // The rows' last lanes repeat their first (the original's stack temporaries).
    const float row0_x = one - bb - cc;
    const float row1[4] = {dc + ab, one - aa - cc, bc - da, dc + ab};
    const float row2[4] = {ac - db, da + bc, one - aa - bb, ac - db};
    const float row0[4] = {row0_x, ab - dc, db + ac, row0_x};
    for (int i = 0; i < 4; ++i) out[4 + i] = row1[i];
    for (int i = 0; i < 4; ++i) out[8 + i] = row2[i];
    for (int i = 0; i < 4; ++i) out[i] = row0[i];
    for (int i = 0; i < 4; ++i) out[12 + i] = translation[i];
}

uint8_t* XB_CDECL apsEffect_Spawn(const float* quat, const float* position, uint8_t* definition, float time) {
    uint32_t& count = title_global<uint32_t>(0x00909b8c);
    if (static_cast<int32_t>(count) >= 0x78) return nullptr;
    uint8_t* system = apsEffect_Create(definition, time);
    if (!system) return nullptr;
    if (title_global<uint32_t>(0x009099a8) || count == 0x78) {
        at<uint16_t>(system, 0x4e) |= 4;
        FxSystem_Destroy(system);
        apsMemoryPool_Free(title_global<uint8_t*>(0x00909994) + 8, system);
        return nullptr;
    }
    (&title_global<uint8_t*>(0x009099ac))[count] = system;
    ++count;
    alignas(16) float matrix[16];
    std::memcpy(matrix, system, sizeof(matrix));
    if (quat) {
        Mat44_FromQuatTranslation(matrix, quat, position);
    } else {
        for (int i = 0; i < 4; ++i) matrix[12 + i] = position[i];
    }
    FxSystem_SetMatrix(system, matrix);
    return system;
}

namespace {

// Particle heap calls with the seeded flag (0x00909948) 0 meanwhile: the particle
// library's own arrays.
uint8_t* unseeded_alloc(uint32_t bytes) {
    const uint32_t heap = particle_heap();
    const uint32_t seeded = title_global<uint32_t>(0x00909948);
    title_global<uint32_t>(0x00909948) = 0;
    uint8_t* data = reinterpret_cast<uint8_t*>(
        title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(vtable_slot(heap, 1))(heap, bytes, 4));
    title_global<uint32_t>(0x00909948) = seeded;
    return data;
}

void unseeded_free(ApsArray* array) {
    const uint32_t seeded = title_global<uint32_t>(0x00909948);
    const uint32_t heap = particle_heap();
    title_global<uint32_t>(0x00909948) = 0;
    title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(heap, 2))(heap, array->data);
    title_global<uint32_t>(0x00909948) = seeded;
    array->data = nullptr;
    array->capacity = 0;
    array->count = 0;
}

// What the particle actions spawn at: their effect time (0x00909998).
float spawn_time() { return title_global<float>(0x00909998); }

}  // namespace

uint8_t* XB_CDECL apsMemoryPool_AllocSlots(int32_t count, uint32_t align) {
    const uint32_t heap = particle_heap();
    uint8_t* const slots = reinterpret_cast<uint8_t*>(title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(
        vtable_slot(heap, 1))(heap, static_cast<uint32_t>(count) * 8, align));
    for (int32_t i = 0; i < count; ++i) {
        // The array new's per-element null check.
        if (!(reinterpret_cast<uintptr_t>(slots) + static_cast<uint32_t>(i) * 8)) continue;
        uint8_t* const slot = slots + i * 8;
        at<uint32_t>(slot, 4) &= 0xff000000u;
        at<uint16_t>(slot, 0) = 0xffff;
        at<uint16_t>(slot, 2) = 0xffff;
        slot[7] = 0;
    }
    return slots;
}

uint8_t* XB_THISCALL apsMemoryPool_Init(uint8_t* pool, int32_t count, uint32_t item_size, uint8_t* items) {
    at<int32_t>(pool, 0) = count;
    at<uint32_t>(pool, 0x14) = item_size;
    at<uint8_t*>(pool, 0x18) = items;
    if (!count) return nullptr;
    if (!pointer_at(pool, 0x1c)) at<uint8_t*>(pool, 0x1c) = apsMemoryPool_AllocSlots(count, 4);
    for (int32_t i = 0; i < at<int32_t>(pool, 0); ++i) {
        at<uint16_t>(pointer_at(pool, 0x1c) + i * 8, 0) = static_cast<uint16_t>(i);
        at<uint16_t>(pointer_at(pool, 0x1c) + i * 8, 2) = static_cast<uint16_t>(i + 1);
        at<uint32_t>(pointer_at(pool, 0x1c) + i * 8, 4) &= 0xff000000u;
        pointer_at(pool, 0x1c)[i * 8 + 7] = 0;
    }
    uint8_t* const slots = pointer_at(pool, 0x1c);
    at<uint16_t>(slots + at<int32_t>(pool, 0) * 8 - 6, 0) = 0xffff;  // the last slot's next
    at<uint16_t>(pool, 0x20) = 0;
    return slots;
}

uint32_t XB_THISCALL apsPoolSet_AddPool(ApsArray* set, int32_t count, int32_t item_size) {
    const int32_t bytes = count * item_size;
    if (bytes <= 0) return 0;
    const int32_t header = (count * 8 + 0xa3) & ~0x7f;
    const uint32_t heap = particle_heap();
    uint8_t* const pool = reinterpret_cast<uint8_t*>(title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(
        vtable_slot(heap, 1))(heap, static_cast<uint32_t>(((bytes + 0x7f) & ~0x7f) + header), 0x10));
    if (!pool) return 0;
    at<uint32_t>(pool, 0x10) = 0xffffffffu;
    at<uint8_t*>(pool, 0x1c) = pool + 0x24;
    at<uint16_t>(pool, 0x20) = 0xffff;
    at<uint32_t>(pool, 0) = 0;
    at<uint32_t>(pool, 4) = 0;
    at<uint32_t>(pool, 8) = 0;
    at<uint32_t>(pool, 0xc) = 0x7fffffff;
    at<uint32_t>(pool, 0x14) = 0;
    at<uint32_t>(pool, 0x18) = 0;
    apsMemoryPool_Init(pool, count, static_cast<uint32_t>(item_size), pool + header);
    const uint32_t item = reinterpret_cast<uintptr_t>(pool);
    apsPtrVector_PushBack(set, &item);
    return 1;
}

uint32_t XB_CDECL apsAddPool(int32_t count, int32_t item_size) {
    return apsPoolSet_AddPool(title_global<ApsArray*>(0x00909f08), count, item_size);
}

uint32_t XB_THISCALL apsPtrVector_Resize(ApsArray* array, int32_t count) {
    if (count <= array->capacity) {
        array->count = static_cast<int16_t>(count);
        return 1;
    }
    uint8_t* const data = unseeded_alloc(static_cast<uint32_t>(count) * 4);
    if (!data) return 0;
    for (int32_t i = 0; i < array->count; ++i)
        reinterpret_cast<uint32_t*>(data)[i] = reinterpret_cast<const uint32_t*>(array->data)[i];
    if (array->data) unseeded_free(array);
    array->data = data;
    array->count = static_cast<int16_t>(count);
    array->capacity = static_cast<int16_t>(count);
    return 1;
}

uint32_t XB_THISCALL apsPtrVector_PushFront(ApsArray* array, const uint32_t* item) {
    const int16_t count = array->count;
    if (count < array->capacity) {
        // With room left the original only moves the items up one place: the item is not
        // stored and the count stays.
        if (count > 0) {
            for (uint32_t* p = reinterpret_cast<uint32_t*>(array->data) + count;
                 p != reinterpret_cast<uint32_t*>(array->data); --p)
                p[0] = p[-1];
        }
        return 1;
    }
    uint8_t* const data = unseeded_alloc((static_cast<uint32_t>(count) + 1) * 4);
    if (!data) return 0;
    for (int32_t i = 0; i < array->count; ++i)
        reinterpret_cast<uint32_t*>(data)[i + 1] = reinterpret_cast<const uint32_t*>(array->data)[i];
    if (array->data) unseeded_free(array);
    array->capacity = static_cast<int16_t>(count + 1);
    array->count = count;
    array->data = data;
    reinterpret_cast<uint32_t*>(data)[0] = *item;
    ++array->count;
    return 1;
}

void XB_THISCALL apsElementList_Add(uint8_t* list, uint8_t* element) {
    const uint32_t type =
        title_function<uint32_t XB_THISCALL(uint8_t*)>(vtable_slot(reinterpret_cast<uintptr_t>(element), 1))(element);
    uint8_t* const special = type == 0x53704465 ? element : nullptr;  // 'SpDe'
    const uint32_t item = reinterpret_cast<uintptr_t>(element);
    ApsArray* const array = reinterpret_cast<ApsArray*>(list);
    if (!at<uint32_t>(element, 0x14)) {
        apsPtrVector_PushFront(array, &item);
        if (special) at<uint8_t*>(list, 8) = special;
    } else {
        apsPtrVector_PushBack(array, &item);
        if (special && !pointer_at(list, 8)) at<uint8_t*>(list, 8) = special;
    }
}

uint8_t* XB_THISCALL apsElementList_SpawnDeathEffect(uint8_t* self, const float* quat, const float* position) {
    uint8_t* const ref = pointer_at(self, 8);
    if (!ref) return nullptr;
    uint8_t* const definition = pointer_at(pointer_at(ref, 4), 8);
    if (!definition) return nullptr;
    alignas(16) float at_position[4];
    std::memcpy(at_position, position, sizeof(at_position));
    return apsEffect_Spawn(quat, at_position, definition, spawn_time());
}

void XB_THISCALL apsActionSpawnTimed_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                           float, float delta) {
    const float period = at<float>(pointer_at(self, 4), 8);
    uint8_t* const definition = pointer_at(pointer_at(self, 4), 0xc);
    const int32_t stride = at<int32_t>(emitter, 0x88);
    if (!definition) return;
    uint8_t* age = begin + emitter[0x95];
    uint8_t* timer = begin + emitter[0xa0];
    uint8_t* quat = (emitter[0x84] & 0x20) ? begin + emitter[0x91] : nullptr;
    for (uint8_t* particle = begin; particle != end; particle += stride, age += stride, timer += stride) {
        float& t = at<float>(timer, 0);
        if (at<float>(age, 0) == 0.0f) t += at<float>(pointer_at(self, 4), 0x10);
        t = t - delta;
        if (t <= 0.0f) {  // comiss/jb: NaN waits
            t = t + period;
            if (t <= 0.0f) t = period;
            alignas(16) float position[4];
            std::memcpy(position, particle, sizeof(position));
            apsEffect_Spawn(reinterpret_cast<const float*>(quat), position, definition, spawn_time());
        }
        if (quat) quat += stride;
    }
}

void XB_THISCALL apsActionSpawnOnDeath_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                             float, float delta) {
    const int32_t stride = at<int32_t>(emitter, 0x88);
    if (!(delta == title_global<float>(0x007c90bc))) return;  // 8888.89: the death pass
    uint8_t* const definition = pointer_at(pointer_at(self, 4), 8);
    if (!definition) return;
    uint8_t* quat = (emitter[0x84] & 0x20) ? begin + emitter[0x91] : nullptr;
    for (uint8_t* particle = begin; particle != end; particle += stride) {
        alignas(16) float position[4];
        std::memcpy(position, particle, sizeof(position));
        apsEffect_Spawn(reinterpret_cast<const float*>(quat), position, definition, spawn_time());
        if (quat) quat += stride;
    }
}

uint32_t XB_THISCALL FxSystemList_DestroyAll(uint8_t* list) {
    int32_t& count = at<int32_t>(list, 0x1e4);
    int32_t last = count;
    uint8_t** const items = reinterpret_cast<uint8_t**>(list + 4);
    for (int32_t i = 0; i < count; ++i) {
        if (uint8_t* const system = items[i]) {
            at<uint16_t>(system, 0x4e) |= 4;
            FxSystem_Destroy(system);
            apsMemoryPool_Free(title_global<uint8_t*>(0x00909994) + 8, system);
        }
        items[i] = nullptr;
        last = count;
    }
    count = 0;
    return static_cast<uint32_t>(last);
}

// The particle actions. A particle's fields sit at byte offsets the emitter keeps:
// +0x8d size, +0x8e width, +0x90 alpha, +0x91 rotation, +0x92 angle, +0x93 height,
// +0x94 texture frame, +0x95 age, +0x96 lifetime, +0x97 velocity, +0x98 peak alpha,
// +0x99 angular velocity, +0x9a angular velocity vector, +0x9b-0x9f the KTau frame (tangent,
// normal, binormal, curvature, torsion), +0xa0 spawn timer, +0xa3-0xa5 the Traj path (point
// list, speed, distance travelled); stride +0x88.
namespace {

uint32_t field(uint8_t* emitter, uint32_t offset) { return emitter[offset]; }
int32_t stride_of(uint8_t* emitter) { return at<int32_t>(emitter, 0x88); }
uint8_t* action_config(uint8_t* self) { return pointer_at(self, 4); }

// `value` += rate * delta for the float at `offset` of every particle.
void add_rate(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint32_t offset,
              float delta) {
    const float step = at<float>(action_config(self), 8) * delta;
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, offset);
    for (uint8_t* p = begin + field(emitter, offset); p != last; p += stride) at<float>(p, 0) = at<float>(p, 0) + step;
}

// The float at `offset` of every particle times rate ^ delta (a second-order series on the x87).
void mul_exp_rate(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint32_t offset,
                  float delta) {
    const float factor = platform_x87_exp2_series(at<float>(action_config(self), 8), delta,
                                                  title_global<float>(0x0083eb24), title_global<float>(0x0083eb20));
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, offset);
    for (uint8_t* p = begin + field(emitter, offset); p != last; p += stride) at<float>(p, 0) = at<float>(p, 0) * factor;
}

// exp(-rate * delta) as the drags' third-order series.
float drag_factor(uint8_t* self, float delta) {
    const float x = 0.0f - at<float>(action_config(self), 8) * delta;
    const float one = title_global<float>(0x0083eb20);
    return ((x * title_global<float>(0x007c90b8) + title_global<float>(0x0083eb24)) * x + one) * x + one;
}

// The xyz of the vector at `offset` times `factor` (every lane computed, w not stored).
void scale_vectors(uint8_t* begin, uint8_t* end, uint8_t* emitter, uint32_t offset, float factor) {
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, offset);
    for (uint8_t* p = begin + field(emitter, offset); p != last; p += stride) {
        const Lanes scaled = mul(lanes(reinterpret_cast<float*>(p)), factor);
        for (int i = 0; i < 3; ++i) at<float>(p, i * 4) = scaled.v[i];
    }
}

// The xyz of the position += velocity * delta (every lane computed, w kept).
void move_position(uint8_t* p, uint8_t* emitter, float delta) {
    const Lanes step = mul(lanes(reinterpret_cast<float*>(p + field(emitter, 0x97))), delta);
    const float w = at<float>(p, 0xc);
    const Lanes moved = add(lanes(reinterpret_cast<float*>(p)), step);
    for (int i = 0; i < 4; ++i) at<float>(p, i * 4) = moved.v[i];
    at<float>(p, 0xc) = w;
}

}  // namespace

uint32_t XB_THISCALL FxEmitter_QueueParticleKill(uint8_t* emitter, uint8_t* particle) {
    if (!pointer_at(emitter, 0xbc)) return 0;
    uint32_t index = title_global<uint32_t>(0x00909ed4);
    if (index == 200) index = 0;
    (&title_global<uint8_t*>(0x00909ba0))[index] = particle;
    title_global<uint32_t>(0x00909ed4) = ++index;
    return index;
}

void XB_THISCALL apsMemoryPool_FreeSlots(uint8_t* pool) {
    if (uint8_t* const slots = pointer_at(pool, 0x1c)) {
        if (slots != pool + 0x24) {
            const uint32_t heap = particle_heap();
            title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(heap, 2))(heap, slots);
        }
        at<uint8_t*>(pool, 0x1c) = nullptr;
    }
    at<uint8_t*>(pool, 0x18) = nullptr;
}

uint32_t XB_THISCALL apsPoolSet_Clear(ApsArray* set) {
    uint8_t** const pools = reinterpret_cast<uint8_t**>(set->data);
    uint8_t** const last = pools + set->count;
    for (uint8_t** item = pools; item != last; ++item) {
        apsMemoryPool_FreeSlots(*item);
        const uint32_t heap = particle_heap();
        title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(heap, 2))(heap, *item);
    }
    return apsPtrVector_Resize(set, 0);
}

int32_t XB_THISCALL apsActionSource_Count(uint8_t* self, uint8_t* emitter, float delta) {
    const float total = at<float>(action_config(self), 8) * delta + at<float>(emitter, 0x80);
    const int32_t count = static_cast<int32_t>(total);
    at<float>(emitter, 0x80) = total - static_cast<float>(count);
    return count;
}

int32_t XB_THISCALL apsActionBurst_Count(uint8_t* self, uint8_t* emitter, float) {
    if (0.0f > at<float>(emitter, 0x80)) return 0;
    at<float>(emitter, 0x80) = title_global<float>(0x0083eb34);  // -1: done
    return static_cast<int32_t>(at<float>(action_config(self), 8));
}

int32_t XB_THISCALL apsActionRandomSpawn_Count(uint8_t* self, uint8_t* emitter, float delta) {
    uint8_t* const interval = pointer_at(pointer_at(self, 0xc), 0x40);
    float wait = at<float>(emitter, 0x80) - delta;
    int32_t count = 0;
    if (wait <= 0.0f) {
        do {
            ++count;
            float next;
            title_function<void XB_THISCALL(uint8_t*, uint32_t, float*)>(
                vtable_slot(reinterpret_cast<uintptr_t>(interval), 1))(interval, 1, &next);
            wait = next + wait;
        } while (wait <= 0.0f);
    }
    at<float>(emitter, 0x80) = wait;
    return count;
}

void XB_THISCALL apsActionLife_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*, float,
                                     float) {
    const int32_t stride = stride_of(emitter);
    const uint32_t age = field(emitter, 0x95), life = field(emitter, 0x96);
    // Without a rotation the death effect gets the position as its quaternion.
    const uint32_t rotation = (emitter[0x84] & 0x20) ? field(emitter, 0x91) : 0;
    const bool spawns = pointer_at(title_global<uint8_t*>(0x00909f10), 8) != nullptr;
    for (uint8_t* p = begin; p != end; p += stride) {
        if (!(at<float>(p, age) > at<float>(p, life))) continue;
        if (spawns)
            apsElementList_SpawnDeathEffect(title_global<uint8_t*>(0x00909f10), reinterpret_cast<float*>(p + rotation),
                                            reinterpret_cast<float*>(p));
        FxEmitter_QueueParticleKill(emitter, p);
    }
}

void XB_THISCALL apsActionAlphaFade_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                          float, float) {
    const float peak = at<float>(action_config(self), 8);
    const float hold = at<float>(action_config(self), 0xc);
    const float scale = peak / (title_global<float>(0x0083eb20) - hold);
    const int32_t stride = stride_of(emitter);
    const uint32_t age_at = field(emitter, 0x95), life_at = field(emitter, 0x96), alpha_at = field(emitter, 0x90);
    for (uint8_t* p = begin; p != end; p += stride) {
        const float life = at<float>(p, life_at), age = at<float>(p, age_at);
        float alpha = 0.0f;
        if (life * hold > age)
            alpha = peak;
        else if (life > age)
            alpha = (1.0f - age / life) * scale;
        at<float>(p, alpha_at) = alpha;
    }
}

void XB_THISCALL apsActionAlphaFadeInOut_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                               uint8_t*, float, float) {
    const float fade_in = at<float>(action_config(self), 8);
    const float peak = at<float>(action_config(self), 0xc);
    const float fade_out = at<float>(action_config(self), 0x10);
    const float in_scale = peak / fade_in;
    const float out_scale = peak / (title_global<float>(0x0083eb20) - fade_out);
    const int32_t stride = stride_of(emitter);
    const uint32_t age_at = field(emitter, 0x95), life_at = field(emitter, 0x96), alpha_at = field(emitter, 0x90);
    for (uint8_t* p = begin; p != end; p += stride) {
        const float age = at<float>(p, age_at), life = at<float>(p, life_at);
        const float t = age / life;
        float alpha;
        if (fade_in > t)
            alpha = t * in_scale;
        else if (fade_out > t)
            alpha = peak;
        else if (life > age)
            alpha = (title_global<float>(0x0083eb20) - t) * out_scale;
        else
            alpha = 0.0f;
        at<float>(p, alpha_at) = alpha;
    }
}

void XB_THISCALL apsActionRandomFadeInOut_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                                uint8_t*, float, float) {
    const float fade_in = at<float>(action_config(self), 8);
    const float fade_out = at<float>(action_config(self), 0xc);
    const float one = title_global<float>(0x0083eb20);
    const float in_scale = one / fade_in;
    const float out_scale = one / (one - fade_out);
    const int32_t stride = stride_of(emitter);
    const uint32_t age_at = field(emitter, 0x95), life_at = field(emitter, 0x96), peak_at = field(emitter, 0x98),
                   alpha_at = field(emitter, 0x90);
    for (uint8_t* p = begin; p != end; p += stride) {
        const float age = at<float>(p, age_at), life = at<float>(p, life_at), peak = at<float>(p, peak_at);
        const float t = age / life;
        float alpha;
        if (fade_in > t)
            alpha = t * peak * in_scale;
        else if (fade_out > t)
            alpha = peak;
        else if (life > age)
            alpha = (one - t) * peak * out_scale;
        else
            alpha = 0.0f;
        at<float>(p, alpha_at) = alpha;
    }
}

void XB_THISCALL apsActionLinearScale_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                            float, float delta) {
    add_rate(self, begin, end, emitter, 0x8d, delta);
}
void XB_THISCALL apsActionExpScale_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                         float, float delta) {
    mul_exp_rate(self, begin, end, emitter, 0x8d, delta);
}
void XB_THISCALL apsActionLinearScaleWidth_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                                 uint8_t*, float, float delta) {
    add_rate(self, begin, end, emitter, 0x8e, delta);
}
void XB_THISCALL apsActionExpScaleWidth_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                              uint8_t*, float, float delta) {
    mul_exp_rate(self, begin, end, emitter, 0x8e, delta);
}
void XB_THISCALL apsActionLinearScaleHeight_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                                  uint8_t*, float, float delta) {
    add_rate(self, begin, end, emitter, 0x93, delta);
}
void XB_THISCALL apsActionExpScaleHeight_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                               uint8_t*, float, float delta) {
    mul_exp_rate(self, begin, end, emitter, 0x93, delta);
}

void XB_THISCALL apsActionMove_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*, float,
                                     float delta) {
    const int32_t stride = stride_of(emitter);
    const uint32_t angle_at = field(emitter, 0x92), spin_at = field(emitter, 0x99);
    for (uint8_t* p = begin; p < end; p += stride) {
        move_position(p, emitter, delta);
        at<float>(p, angle_at) = at<float>(p, spin_at) * delta + at<float>(p, angle_at);
    }
}

void XB_THISCALL apsActionPositionMove_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                             float, float delta) {
    const int32_t stride = stride_of(emitter);
    for (uint8_t* p = begin; p != end; p += stride) move_position(p, emitter, delta);
}

void XB_THISCALL apsActionVelocityDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                             float, float delta) {
    scale_vectors(begin, end, emitter, 0x97, drag_factor(self, delta));
}

void XB_THISCALL apsActionAngularDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                            float, float delta) {
    const float factor = drag_factor(self, delta);
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, 0x99);
    for (uint8_t* p = begin + field(emitter, 0x99); p != last; p += stride) at<float>(p, 0) = at<float>(p, 0) * factor;
}

void XB_THISCALL apsActionVectorAngularDrag_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                                  uint8_t*, float, float delta) {
    scale_vectors(begin, end, emitter, 0x9a, drag_factor(self, delta));
}

void XB_THISCALL apsActionUVFrame_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                        float, float delta) {
    const float step = at<float>(action_config(self), 0x10) * delta;
    const float first = at<float>(action_config(self), 8);
    const float limit = at<float>(action_config(self), 0xc);
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, 0x94);
    for (uint8_t* p = begin + field(emitter, 0x94); p != last; p += stride) {
        float frame = at<float>(p, 0) + step;
        if (frame >= limit) frame = frame - limit + first;
        if (first > frame) frame = limit - (first - frame);
        at<float>(p, 0) = frame;
    }
}

float XB_CDECL apsAtan2(float y, float x) {
    const float abs_y = abs_float(y);
    const float abs_x = abs_float(x);
    if (platform_x87_add(abs_y, abs_x) == 0.0f) return 0.0f;
    const float inverse = platform_x87_rcp_hypot(y, x, title_global<float>(0x0083eb20));
    const float half = title_global<float>(0x0083eb24);
    float angle;
    if (abs_y > abs_x) {
        const float t = inverse * abs_x;
        angle = title_global<float>(0x0083ed0c) - (half > t ? asin_series(t) : asin_upper(t));
    } else {
        const float t = inverse * abs_y;
        angle = half > t ? asin_series(t) : asin_upper(t);
    }
    if (0.0f > x) angle = title_global<float>(0x007c8cc0) - angle;
    if (0.0f > y) angle = 0.0f - angle;
    return angle;
}

namespace {

// The ATEX/ATEY/ATEZ body: every particle's angle set to the view heading (0x00909980)
// of the emitter matrix's row at `Row` (dots with the view columns 0x00909960/70; kept
// when both are 0), scaled by 0x00909984.
template <uint32_t Row>
void angle_to_emitter_axis(uint8_t* begin, uint8_t* end, uint8_t* emitter) {
    const float offset = title_global<float>(0x00909980) - title_global<float>(0x0083ed0c);
    const float scale = title_global<float>(0x00909984);
    const int32_t stride = stride_of(emitter);
    const Lanes axis = lanes(&at<float>(emitter, Row));
    const Lanes y_dot = mul(axis, lanes(&title_global<float>(0x00909960)));
    const float y = (y_dot.v[0] + y_dot.v[1]) + y_dot.v[2];
    const Lanes x_dot = mul(axis, lanes(&title_global<float>(0x00909970)));
    const float x = (x_dot.v[0] + x_dot.v[1]) + x_dot.v[2];
    if (y == 0.0f && x == 0.0f) return;
    const float angle = platform_x87_add_mul(apsAtan2(y, x), offset, scale);
    for (uint8_t* p = begin + field(emitter, 0x92); p < end; p += stride) at<float>(p, 0) = angle;
}

}  // namespace

void XB_THISCALL apsActionAngleToEmitterX_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                                float, float) {
    angle_to_emitter_axis<0x10>(begin, end, emitter);
}
void XB_THISCALL apsActionAngleToEmitterY_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                                float, float) {
    angle_to_emitter_axis<0x20>(begin, end, emitter);
}
void XB_THISCALL apsActionAngleToEmitterZ_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                                float, float) {
    angle_to_emitter_axis<0x30>(begin, end, emitter);
}

void XB_THISCALL apsActionPointAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                             float, float delta) {
    uint8_t* const config = action_config(self);
    const float strength = at<float>(config, 0x14) * delta;
    // The point's 4th lane repeats x (the original's stack temporary).
    alignas(16) float point[4] = {at<float>(config, 8), at<float>(config, 0xc), at<float>(config, 0x10),
                                  at<float>(config, 8)};
    if (!(emitter[0xae] & 2)) {  // in the emitter's space
        alignas(16) float moved[4];
        std::memcpy(point, Mat44_TransformPoint(moved, &at<float>(emitter, 0x10), point), sizeof(point));
    }
    const float closest = title_global<float>(0x0083eb4c);
    const int32_t stride = stride_of(emitter);
    uint8_t* velocity = begin + field(emitter, 0x97);
    for (uint8_t* p = begin + field(emitter, 0x8c); p < end; p += stride, velocity += stride) {
        const Lanes offset = sub(lanes(reinterpret_cast<float*>(p)), lanes(point));
        const Lanes squares = mul(offset, offset);
        float distance2 = (squares.v[0] + squares.v[1]) + squares.v[2];
        if (closest > distance2) distance2 = closest;
        const float distance = platform_x87_sqrt(distance2);
        const float pull = strength / distance2;
        const Lanes direction = {{offset.v[0] / distance, offset.v[1] / distance, offset.v[2] / distance,
                                  offset.v[3] / distance}};
        const Lanes result = sub(lanes(reinterpret_cast<float*>(velocity)), mul(direction, pull));
        for (int i = 0; i < 3; ++i) at<float>(velocity, i * 4) = result.v[i];
    }
}

void XB_THISCALL apsActionColorShift_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                           float, float) {
    uint8_t* const config = action_config(self);
    // The colours' 4th lanes are never-set stack words (zeroed in test processes).
    const Lanes from = {{at<float>(config, 0xc), at<float>(config, 0x10), at<float>(config, 0x14), 0.0f}};
    const Lanes to = {{at<float>(config, 0x1c), at<float>(config, 0x20), at<float>(config, 0x24), 0.0f}};
    const float start = at<float>(config, 8);
    const float stop = at<float>(config, 0x18);
    const Lanes range = sub(to, from);
    const float scale = title_global<float>(0x0083eb20) / (stop - start);
    const float zero = title_global<float>(0x0083eafc);
    const float one = title_global<float>(0x0083eb20);
    const int32_t stride = stride_of(emitter);
    const uint32_t age_at = field(emitter, 0x95), life_at = field(emitter, 0x96), color_at = field(emitter, 0x8f);
    for (uint8_t* p = begin; p != end; p += stride) {
        const float life = at<float>(p, life_at), age = at<float>(p, age_at);
        const float begins = life * start;
        if (!(age >= begins)) continue;
        if (!(life * stop >= age)) continue;
        const float t = (age - begins) / life * scale;
        const Lanes color = add(from, mul(range, t));
        for (int i = 0; i < 4; ++i) {
            const float low = zero > color.v[i] ? zero : color.v[i];  // MAXPS: NaN passes
            const float clamped = one < low ? one : low;              // MINPS
            if (i < 3) at<float>(p, color_at + i * 4) = clamped;
        }
    }
}

namespace {

// v times the transpose of the emitter matrix's rotation (+0x10, packed: the 4th lane
// takes the translation row).
Lanes rotate_into_emitter(uint8_t* emitter, const Lanes& v) {
    const float* const m = &at<float>(emitter, 0x10);
    const Lanes x_column = {{m[0], m[4], m[8], m[12]}};
    const Lanes y_column = {{m[1], m[5], m[9], m[13]}};
    const Lanes z_column = {{m[2], m[6], m[10], m[14]}};
    return add(add(mul(x_column, v.v[0]), mul(y_column, v.v[1])), mul(z_column, v.v[2]));
}

// The xyz of the vector at `offset` of every particle += step.
void add_step(uint8_t* begin, uint8_t* end, uint8_t* emitter, uint32_t offset, const Lanes& step) {
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + offset;
    for (uint8_t* p = begin + offset; p != last; p += stride)
        for (int i = 0; i < 3; ++i) at<float>(p, i * 4) = step.v[i] + at<float>(p, i * 4);
}

// The config's vector (+8, 4th lane repeating x) and its "in emitter space" flag (+0x14).
Lanes config_vector(uint8_t* config) {
    return {{at<float>(config, 8), at<float>(config, 0xc), at<float>(config, 0x10), at<float>(config, 8)}};
}

}  // namespace

void XB_THISCALL apsActionMoveFixed_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                          float, float delta) {
    uint8_t* const config = action_config(self);
    Lanes velocity = config_vector(config);
    if (at<uint32_t>(config, 0x14)) velocity = rotate_into_emitter(emitter, velocity);
    add_step(begin, end, emitter, 0, mul(velocity, delta));
}

void XB_THISCALL apsActionForce_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*, float,
                                      float delta) {
    uint8_t* const config = action_config(self);
    Lanes step = mul(config_vector(config), delta);
    if (at<uint32_t>(config, 0x14)) step = rotate_into_emitter(emitter, step);
    add_step(begin, end, emitter, field(emitter, 0x97), step);
}

void XB_THISCALL Quat_RotateVector(const float* q, float* v) {
    const float z = 0.0f;  // the vector's w
    const float v0 = v[0], v1 = v[1], v2 = v[2];
    const float nq0 = z - q[0], nq1 = z - q[1], nq2 = z - q[2];
    const float a = (((v1 * q[3]) - (q[0] * v2)) + (q[2] * v0)) + (q[1] * z);
    const float b = (((v0 * q[3]) + (v2 * q[1])) + (q[0] * z)) - (q[2] * v1);
    const float c = (((q[3] * z) - (q[0] * v0)) - (v1 * q[1])) - (q[2] * v2);
    const float d = (((q[0] * v1) + (v2 * q[3])) - (v0 * q[1])) + (q[2] * z);
    v[0] = (((a * nq2) + (q[3] * b)) + (c * nq0)) - (d * nq1);
    v[1] = (((c * nq1) - (nq2 * b)) + (q[3] * a)) + (d * nq0);
    v[2] = (((c * nq2) + (nq1 * b)) - (a * nq0)) + (q[3] * d);
}

void XB_THISCALL apsActionOrientedMove_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                             float, float delta) {
    const int32_t stride = stride_of(emitter);
    uint8_t* velocity = begin + field(emitter, 0x97);
    uint8_t* spin = begin + field(emitter, 0x9a);
    uint8_t* rotation = begin + field(emitter, 0x91);
    const float half_delta = delta * title_global<float>(0x0083eb24);
    const float one = title_global<float>(0x0083eb20);
    for (uint8_t* p = begin; p != end; p += stride, velocity += stride, spin += stride, rotation += stride) {
        const Lanes moved = add(lanes(reinterpret_cast<float*>(p)), mul(lanes(reinterpret_cast<float*>(velocity)), delta));
        for (int i = 0; i < 3; ++i) at<float>(p, i * 4) = moved.v[i];
        alignas(16) float w[4];
        std::memcpy(w, spin, sizeof(w));
        float* const q = reinterpret_cast<float*>(rotation);
        Quat_RotateVector(q, w);
        const float z = 0.0f;
        const float e0 = (((q[2] * w[1]) + (q[3] * w[0])) + (q[0] * z)) - (q[1] * w[2]);
        const float e1 = (((q[1] * z) - (q[2] * w[0])) + (q[0] * w[2])) + (q[3] * w[1]);
        const float e2 = (((q[1] * w[0]) + (q[2] * z)) - (q[0] * w[1])) + (q[3] * w[2]);
        const float e3 = (((q[3] * z) - (q[0] * w[0])) - (q[1] * w[1])) - (q[2] * w[2]);
        const float n0 = q[0] + e0 * half_delta;
        const float n1 = q[1] + e1 * half_delta;
        const float n2 = q[2] + e2 * half_delta;
        const float n3 = q[3] + e3 * half_delta;
        const float inverse = platform_x87_rcp_norm4(n0, n3, n1, n2, one);
        q[0] = n0 * inverse;
        q[1] = n1 * inverse;
        q[2] = n2 * inverse;
        q[3] = n3 * inverse;
    }
}

void XB_THISCALL apsActionPlaneBounce_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                            float, float) {
    uint8_t* const config = action_config(self);
    // The normal's 4th lane is a never-set stack word (zeroed in test processes).
    const Lanes normal = {{at<float>(config, 8), at<float>(config, 0xc), at<float>(config, 0x10), 0.0f}};
    const Lanes squares = mul(normal, normal);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    float inverse = 0.0f;   // a zero normal: the constant 0 vector
    float point_w = 0.0f;   // the 4th lane of whichever vector was copied
    if (!(length2 == 0.0f)) {
        const float a = abs_float(length2);
        const float r = platform_rsqrt_estimate(a);
        inverse = ((title_global<float>(0x007c8be0) - r * r * a) * r) * title_global<float>(0x007c8bf0);
        point_w = title_global<float>(0x007c8bec);  // 3.0
    }
    const Lanes unit = mul(normal, inverse);
    const Lanes point = {{at<float>(config, 0x14), at<float>(config, 0x18), at<float>(config, 0x1c), point_w}};
    const Lanes products = mul(neg(unit), point);
    const float offset = (products.v[0] + products.v[1]) + products.v[2];
    const float bounce = at<float>(config, 0x20);
    const float one = title_global<float>(0x0083eb20);
    const int32_t stride = stride_of(emitter);
    uint8_t* position = begin + field(emitter, 0x8c);
    uint8_t* const last = end + field(emitter, 0x97);
    for (uint8_t* velocity = begin + field(emitter, 0x97); velocity != last; velocity += stride, position += stride) {
        const Lanes along = mul(lanes(reinterpret_cast<float*>(position)), unit);
        const float distance = ((along.v[0] + along.v[1]) + along.v[2]) + offset;
        if (!(0.0f > distance)) continue;
        const Lanes v = lanes(reinterpret_cast<float*>(velocity));
        const Lanes toward = mul(v, unit);
        const float speed = (toward.v[0] + toward.v[1]) + toward.v[2];
        if (!(0.0f > speed)) continue;
        const Lanes result = sub(v, mul(unit, (bounce + one) * speed));
        for (int i = 0; i < 3; ++i) at<float>(velocity, i * 4) = result.v[i];
    }
}


namespace {

// The lanes {y, z, x, w} (SHUFPS 0xc9).
Lanes yzxw(const Lanes& a) { return {{a.v[1], a.v[2], a.v[0], a.v[3]}}; }

// cross(a, b) in the title's packed order: (a * b.yzx - a.yzx * b).yzx, every lane computed.
Lanes cross_lanes(const Lanes& a, const Lanes& b) { return yzxw(sub(mul(a, yzxw(b)), mul(yzxw(a), b))); }

// 1 / |v.xyz| as RSQRTSS and one Newton step (3.0 and 0.5 read from `three`/`half`); 0 for a zero
// vector. The squares' unused lanes are computed too (MXCSR flags).
float unit_scale(const Lanes& v, uint32_t three, uint32_t half) {
    const Lanes squares = mul(v, v);
    platform_keep(squares);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    if (sum == 0.0f) return 0.0f;
    const float a = abs_bits(sum);
    const float r = platform_rsqrt_estimate(a);
    return ((title_global<float>(three) - (r * r) * a) * r) * title_global<float>(half);
}

void store_lanes(float* out, const Lanes& v) {
    for (int i = 0; i < 4; ++i) out[i] = v.v[i];
}

}  // namespace

float* XB_CDECL Mat44_TransformVector(float* out, const float* matrix, const float* v) {
    const Lanes r = add(add(mul(lanes(matrix), v[0]), mul(lanes(matrix + 4), v[1])), mul(lanes(matrix + 8), v[2]));
    store_lanes(out, r);
    return out;
}

float* XB_CDECL Mat44_FromDirection(float* out, const float* forward, const float* up) {
    const Lanes a = lanes(forward);
    const Lanes c = cross_lanes(a, lanes(up));
    const Lanes side = mul(c, unit_scale(c, 0x007c8740, 0x007c8750));
    const Lanes d = cross_lanes(side, a);
    const Lanes second = mul(d, unit_scale(d, 0x007c8740, 0x007c8750));
    std::memcpy(out, forward, 16);
    store_lanes(out + 8, mul(side, title_global<float>(0x00909984)));
    store_lanes(out + 4, second);
    std::memcpy(out + 12, &title_global<float>(0x007c88e0), 16);  // {0, 0, 0, 1}
    return out + 12;
}

float* XB_CDECL Quat_FromMat44(float* out, const float* m) {
    const Lanes r0 = lanes(m), r1 = lanes(m + 4), r2 = lanes(m + 8);
    const Lanes trace = add(add(r0, splat(r1.v[1])), splat(r2.v[2]));
    platform_keep(trace);
    const float half = title_global<float>(0x0083eb24);
    const float one = title_global<float>(0x0083eb20);
    if (trace.v[0] > title_global<float>(0x0083eafc)) {
        const float s = platform_x87_add_sqrt(trace.v[0], one);
        const float r = half / s;
        const Lanes a = mul(sub(r2, splat(r1.v[2])), r);
        const Lanes b = mul(sub(splat(r0.v[2]), r2), r);
        const Lanes c = mul(sub(r1, splat(r0.v[1])), r);
        platform_keep(a);
        platform_keep(b);
        platform_keep(c);
        out[0] = a.v[1];
        out[1] = b.v[0];
        out[2] = c.v[0];
        out[3] = platform_x87_mul(s, half);
        return out;
    }
    // The largest diagonal element picks the first component (q = {w, x, y, z} here).
    int i = 0, j = 1, k = 2;
    if (!(m[5] <= m[0])) i = 1, j = 2, k = 0;
    if (m[10] > m[i * 5]) i = 2, j = 0, k = 1;
    const float s = platform_x87_sub_sub_add_sqrt(m[i * 5], m[j * 5], m[k * 5], one);
    float q[4];
    q[1 + i] = platform_x87_mul(s, half);
    const float r = s == 0.0f ? s : half / s;
    q[0] = (m[k * 4 + j] - m[j * 4 + k]) * r;
    q[1 + j] = (m[j * 4 + i] + m[i * 4 + j]) * r;
    q[1 + k] = (m[k * 4 + i] + m[i * 4 + k]) * r;
    out[0] = q[1];
    out[1] = q[2];
    out[2] = q[3];
    out[3] = q[0];
    return out;
}

void XB_CDECL apsOrientToVelocity(uint8_t* begin, uint8_t* end, uint8_t* emitter) {
    const int32_t stride = stride_of(emitter);
    uint8_t* const last = end + field(emitter, 0x97);
    uint8_t* rotation = begin + field(emitter, 0x91);
    for (uint8_t* velocity = begin + field(emitter, 0x97); velocity != last; velocity += stride, rotation += stride) {
        alignas(16) float v[4];
        std::memcpy(v, velocity, sizeof(v));  // w = the next field
        if (emitter[0xae] & 2) {
            alignas(16) float moved[4];
            std::memcpy(v, Mat44_TransformVector(moved, &at<float>(emitter, 0x10), v), sizeof(v));
        }
        const Lanes direction = lanes(v);
        alignas(16) float forward[4], up[4], matrix[16], q[4];
        store_lanes(forward, mul(direction, unit_scale(direction, 0x007c8be0, 0x007c8bf0)));
        store_lanes(up, mul(lanes(&title_global<float>(0x007c8c70)), title_global<float>(0x007c8c40)));  // -z
        Mat44_FromDirection(matrix, forward, up);
        std::memcpy(rotation, Quat_FromMat44(q, matrix), sizeof(q));
    }
}

void XB_THISCALL apsActionAngleToVelocity_Apply(uint8_t*, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                                float, float) {
    if (emitter[0x84] & 0x20) {
        apsOrientToVelocity(begin, end, emitter);
        return;
    }
    const bool local = emitter[0xae] & 2;
    const float offset = title_global<float>(0x00909980) - title_global<float>(0x0083ed0c);
    const float scale = title_global<float>(0x00909984);
    const int32_t stride = stride_of(emitter);
    for (uint8_t* p = begin; p != end; p += stride) {
        alignas(16) float v[4];
        std::memcpy(v, p + field(emitter, 0x97), sizeof(v));
        if (local) {
            alignas(16) float moved[4];
            std::memcpy(v, Mat44_TransformVector(moved, &at<float>(emitter, 0x10), v), sizeof(v));
        }
        const Lanes axis = lanes(v);
        const Lanes y_dot = mul(axis, lanes(&title_global<float>(0x00909960)));
        const float y = (y_dot.v[0] + y_dot.v[1]) + y_dot.v[2];
        const Lanes x_dot = mul(axis, lanes(&title_global<float>(0x00909970)));
        const float x = (x_dot.v[0] + x_dot.v[1]) + x_dot.v[2];
        if (y == 0.0f && x == 0.0f) continue;
        at<float>(p + field(emitter, 0x92), 0) = platform_x87_add_mul(apsAtan2(y, x), offset, scale);
    }
}


namespace {

// The two config points of LAtr/DLAt (+8, +0x14; each 4th lane repeats x), in the emitter's
// space unless emitter flag 2.
void line_points(uint8_t* config, uint8_t* emitter, float* a, float* b) {
    const float a_init[4] = {at<float>(config, 8), at<float>(config, 0xc), at<float>(config, 0x10), at<float>(config, 8)};
    const float b_init[4] = {at<float>(config, 0x14), at<float>(config, 0x18), at<float>(config, 0x1c),
                             at<float>(config, 0x14)};
    std::memcpy(a, a_init, 16);
    std::memcpy(b, b_init, 16);
    if (emitter[0xae] & 2) return;
    alignas(16) float moved[4];
    std::memcpy(a, Mat44_TransformPoint(moved, &at<float>(emitter, 0x10), a), 16);
    std::memcpy(b, Mat44_TransformPoint(moved, &at<float>(emitter, 0x10), b), 16);
}

// velocity -= offset / |offset| * factor (all lanes; xyz stored), |offset|^2 = `distance2`.
void pull_velocity(uint8_t* velocity, const Lanes& offset, float distance2, float factor) {
    const float distance = platform_x87_sqrt(distance2);
    const Lanes direction = {{offset.v[0] / distance, offset.v[1] / distance, offset.v[2] / distance,
                              offset.v[3] / distance}};
    const Lanes result = sub(lanes(reinterpret_cast<float*>(velocity)), mul(direction, factor));
    platform_keep(result);
    for (int i = 0; i < 3; ++i) at<float>(velocity, i * 4) = result.v[i];
}

}  // namespace

void XB_THISCALL apsActionLineAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                            float, float delta) {
    uint8_t* const config = action_config(self);
    const float strength = at<float>(config, 0x20) * delta;
    alignas(16) float a[4], b[4];
    line_points(config, emitter, a, b);
    const Lanes origin = lanes(a);
    const Lanes dir = sub(lanes(b), origin);
    const Lanes unit = mul(dir, unit_scale(dir, 0x007c8be0, 0x007c8bf0));
    const float closest = title_global<float>(0x0083eb4c);
    const int32_t stride = stride_of(emitter);
    uint8_t* velocity = begin + field(emitter, 0x97);
    for (uint8_t* p = begin + field(emitter, 0x8c); p < end; p += stride, velocity += stride) {
        const Lanes position = lanes(reinterpret_cast<float*>(p));
        const Lanes along = mul(sub(position, origin), unit);
        const float projection = (along.v[0] + along.v[1]) + along.v[2];
        const Lanes offset = sub(position, add(origin, mul(unit, projection)));
        const Lanes squares = mul(offset, offset);
        float distance2 = (squares.v[0] + squares.v[1]) + squares.v[2];
        if (closest > distance2) distance2 = closest;
        pull_velocity(velocity, offset, distance2, strength / distance2);
    }
}

void XB_THISCALL apsActionDecayLineAttract_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter,
                                                 uint8_t*, float, float delta) {
    uint8_t* const config = action_config(self);
    const float strength = at<float>(config, 0x20) * delta;
    alignas(16) float a[4], b[4];
    line_points(config, emitter, a, b);
    const Lanes origin = lanes(a);
    const Lanes dir = sub(lanes(b), origin);
    // 1 / length as packed RSQRTPS + one Newton step over {sum, y^2, z^2, w^2} (no abs, no zero test).
    Lanes squares = mul(dir, dir);
    squares.v[0] = (squares.v[0] + squares.v[1]) + squares.v[2];
    Lanes estimate;
    for (int i = 0; i < 4; ++i) estimate.v[i] = platform_rsqrt_estimate(squares.v[i]);
    const Lanes inverse_lanes = mul(mul(sub(lanes(&title_global<float>(0x007c8be0)), mul(mul(estimate, estimate), squares)),
                                        estimate),
                                    lanes(&title_global<float>(0x007c8bf0)));
    platform_keep(inverse_lanes);
    const float inverse = inverse_lanes.v[0];
    const Lanes unit = mul(dir, inverse);
    const float one = title_global<float>(0x0083eb20);
    const float closest = title_global<float>(0x0083eb4c);
    const int32_t stride = stride_of(emitter);
    uint8_t* velocity = begin + field(emitter, 0x97);
    for (uint8_t* p = begin + field(emitter, 0x8c); p < end; p += stride, velocity += stride) {
        const Lanes position = lanes(reinterpret_cast<float*>(p));
        const Lanes along = mul(sub(position, origin), unit);
        const float projection = (along.v[0] + along.v[1]) + along.v[2];
        // The pull fades from 1 at the first point to 0 at the second (clamped fraction).
        const float t = inverse * projection;
        float clamped;
        if (0.0f > t) clamped = 0.0f;
        else if (t > one) clamped = one;
        else clamped = t;
        const float weight = one - clamped;
        const Lanes offset = sub(position, add(origin, mul(unit, projection)));
        const Lanes offset_squares = mul(offset, offset);
        float distance2 = (offset_squares.v[0] + offset_squares.v[1]) + offset_squares.v[2];
        if (closest > distance2) distance2 = closest;
        pull_velocity(velocity, offset, distance2, (weight / distance2) * strength);
    }
}


namespace {

// The particle actions' random number (seed 0x00909f6c): seed = (12345 - seed * 0x3e39b193)
// & 0x7fffffff, its top 23 bits as the mantissa of a float in [1, 2), minus 1.
float action_random(float one) {
    uint32_t& seed = title_global<uint32_t>(0x00909f6c);
    seed = (0x3039u - seed * 0x3e39b193u) & 0x7fffffffu;
    return from_bits((seed >> 8) | 0x3f800000u) - one;
}

Lanes vec(uint8_t* p) { return lanes(reinterpret_cast<float*>(p)); }

void store_xyz(uint8_t* p, const Lanes& v) {
    for (int i = 0; i < 3; ++i) at<float>(p, i * 4) = v.v[i];
}

}  // namespace

void XB_THISCALL apsActionFrenetMove_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                           float, float delta) {
    uint8_t* const config = action_config(self);
    const float speed = at<float>(config, 8);
    const float curvature_min = at<float>(config, 0xc);
    const float curvature_low = at<float>(config, 0x14), curvature_high = at<float>(config, 0x18);
    const float torsion_min = at<float>(config, 0x1c);
    const float torsion_low = at<float>(config, 0x24), torsion_high = at<float>(config, 0x28);
    const float one = title_global<float>(0x0083eb20);
    const int32_t stride = stride_of(emitter);
    uint8_t* velocity = begin + field(emitter, 0x97);
    if (!(velocity < end)) return;
    const float torsion_range = at<float>(config, 0x20) - torsion_min;
    const float curvature_range = at<float>(config, 0x10) - curvature_min;
    uint8_t* tangent = begin + field(emitter, 0x9b);
    uint8_t* normal = begin + field(emitter, 0x9c);
    uint8_t* binormal = begin + field(emitter, 0x9d);
    uint8_t* curvature = begin + field(emitter, 0x9e);
    uint8_t* torsion = begin + field(emitter, 0x9f);
    for (; velocity < end; velocity += stride, tangent += stride, normal += stride, binormal += stride,
                           curvature += stride, torsion += stride) {
        store_xyz(velocity, mul(vec(tangent), speed));
        store_xyz(tangent, add(vec(tangent), mul(mul(vec(normal), at<float>(curvature, 0)), delta)));
        store_xyz(binormal, sub(vec(binormal), mul(mul(vec(normal), at<float>(torsion, 0)), delta)));
        const Lanes t = vec(tangent);
        store_xyz(tangent, mul(t, unit_scale(t, 0x007c8be0, 0x007c8bf0)));
        const Lanes b = vec(binormal);
        store_xyz(binormal, mul(b, unit_scale(b, 0x007c8be0, 0x007c8bf0)));
        store_xyz(normal, cross_lanes(vec(tangent), vec(binormal)));
        // Curvature and torsion drift by a random rate in [min, max) * delta, then are clamped.
        float k = ((action_random(one) * curvature_range) + curvature_min) * delta + at<float>(curvature, 0);
        at<float>(curvature, 0) = k;
        float tau = ((action_random(one) * torsion_range) + torsion_min) * delta + at<float>(torsion, 0);
        at<float>(torsion, 0) = tau;
        if (curvature_low > k) k = curvature_low;
        else if (k > curvature_high) k = curvature_high;
        at<float>(curvature, 0) = k;
        if (torsion_low > tau) tau = torsion_low;
        else if (tau > torsion_high) tau = torsion_high;
        at<float>(torsion, 0) = tau;
    }
}


uint32_t XB_STDCALL apsPath_FindSegment(uint8_t** cursor, float* distance, float* length, float* start,
                                        float* finish) {
    uint8_t* point = *cursor;
    float remaining = *distance;
    if (at<int32_t>(point, 0) == -1) return 0;
    for (;;) {
        if (at<int32_t>(point, 0xc) == -1) return 0;
        // Both points with x repeated in the 4th lane (the original's stack temporaries).
        alignas(16) const float a[4] = {at<float>(point, 0), at<float>(point, 4), at<float>(point, 8),
                                        at<float>(point, 0)};
        alignas(16) const float b[4] = {at<float>(point, 0xc), at<float>(point, 0x10), at<float>(point, 0x14),
                                        at<float>(point, 0xc)};
        const Lanes d = sub(lanes(b), lanes(a));
        const Lanes squares = mul(d, d);
        platform_keep(squares);
        const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
        float segment = sum;  // 0 (or NaN) as it is
        if (!(sum == 0.0f || sum != sum)) {
            // sqrt(sum) as sum * RSQRTSS with one Newton step.
            const float r = platform_rsqrt_estimate(sum);
            const float root = sum * r;
            segment = ((title_global<float>(0x007c8be0) - root * r) * root) * title_global<float>(0x007c8bf0);
        }
        if (segment > remaining) {
            *cursor = point;
            *length = segment;
            *distance = remaining;
            std::memcpy(start, a, 16);
            std::memcpy(finish, b, 16);
            return 1;
        }
        point += 0xc;
        remaining = remaining - segment;
        if (at<int32_t>(point, 0) == -1) return 0;
    }
}

void XB_THISCALL apsActionTrajectory_Apply(uint8_t* self, uint8_t* begin, uint8_t* end, uint8_t* emitter, uint8_t*,
                                           float, float delta) {
    using PathLookup = uint8_t* XB_CDECL(uint32_t);
    PathLookup* const lookup = title_global<PathLookup*>(0x00909940);
    const float scale = title_global<float>(0x00909984);
    uint8_t* const config = action_config(self);
    const uint32_t path_id = at<uint32_t>(config, 8);
    const float path_speed = at<float>(config, 0xc);
    const int32_t stride = stride_of(emitter);
    const float half = title_global<float>(0x0083eb24);
    const float two_pi = title_global<float>(0x0083edac);
    for (uint8_t* p = begin; p != end; p += stride) {
        uint8_t*& path = at<uint8_t*>(p, field(emitter, 0xa3));
        float& speed = at<float>(p, field(emitter, 0xa4));
        float& travelled = at<float>(p, field(emitter, 0xa5));
        uint8_t* cursor = path;
        if (!cursor) {
            if (!lookup) continue;
            cursor = lookup(path_id);
            path = cursor;
            speed = path_speed;
            if (!cursor) continue;
        }
        float remaining = speed * delta + travelled;
        float length = 0.0f;
        alignas(16) float a[4], b[4];
        if (!apsPath_FindSegment(&cursor, &remaining, &length, a, b)) {
            path = nullptr;
            travelled = 0.0f;
            FxEmitter_QueueParticleKill(emitter, p);
            continue;
        }
        path = cursor;
        travelled = remaining;
        Lanes direction = sub(lanes(b), lanes(a));
        const Lanes position = add(lanes(a), mul(direction, remaining / length));
        // The heading of the segment in the xy plane.
        direction.v[2] = 0.0f;
        const Lanes unit = mul(direction, unit_scale(direction, 0x007c8be0, 0x007c8bf0));
        platform_keep(unit);
        const float x = abs_bits(unit.v[0]);
        float angle = platform_x87_greater(half, x) ? asin_series(x) : asin_upper(x);
        if (0.0f > unit.v[0]) angle = 0.0f - angle;
        float heading = title_global<float>(0x0083ed0c) - angle;
        if (0.0f > unit.v[1]) heading = two_pi - heading;
        heading = heading * scale;
        if (0.0f > heading) heading = heading + two_pi;
        at<float>(p, field(emitter, 0x92)) = heading;
        store_xyz(p, position);
    }
}


float* XB_CDECL apsSinCos(float angle, float* sine, float* cosine) {
    // Lanes {angle + 3 pi / 2, angle + 0, ...} through the packed polynomial (the other two lanes
    // repeat these).
    const float offset = title_global<float>(0x0083efe0);
    *sine = scene_sine_poly(angle + offset);
    *cosine = scene_sine_poly(angle + 0.0f);
    return sine;
}

float* XB_THISCALL Quat_FromAxisAngle(float* out, const float* axis, float angle) {
    float sine, cosine;
    apsSinCos(angle * title_global<float>(0x0083eb24), &sine, &cosine);
    const Lanes v = mul(lanes(axis), sine);
    platform_keep(v);
    const float inverse = platform_x87_rcp_norm4(v.v[2], v.v[1], v.v[0], cosine, title_global<float>(0x0083eb20));
    out[0] = platform_x87_mul(v.v[0], inverse);
    out[1] = platform_x87_mul(v.v[1], inverse);
    out[2] = platform_x87_mul(v.v[2], inverse);
    out[3] = platform_x87_mul(inverse, cosine);
    return out;
}

float* XB_CDECL Quat_FromEuler(float* out, float x_angle, float y_angle, float z_angle) {
    const Lanes one = splat(title_global<float>(0x007c8790));
    alignas(16) float axis[4];
    float z[4], y[4], x[4];
    store_lanes(axis, mul(lanes(&title_global<float>(0x007c87d0)), one));
    Quat_FromAxisAngle(z, axis, z_angle);
    store_lanes(axis, mul(lanes(&title_global<float>(0x007c87b0)), one));
    Quat_FromAxisAngle(x, axis, x_angle);
    store_lanes(axis, mul(lanes(&title_global<float>(0x007c87c0)), one));
    Quat_FromAxisAngle(y, axis, y_angle);
    // y * (x * z), in the original's operation order.
    const float a = (((x[1] * z[3]) - (x[2] * z[0])) + (z[1] * x[3])) + (z[2] * x[0]);
    const float b = (((x[2] * z[1]) + (z[3] * x[0])) + (x[3] * z[0])) - (z[2] * x[1]);
    const float c = (((x[3] * z[3]) - (x[0] * z[0])) - (x[1] * z[1])) - (x[2] * z[2]);
    const float d = (((x[2] * z[3]) + (x[1] * z[0])) - (z[1] * x[0])) + (z[2] * x[3]);
    out[0] = (((y[2] * a) + (y[3] * b)) + (c * y[0])) - (y[1] * d);
    out[1] = (((y[1] * c) - (y[2] * b)) + (y[3] * a)) + (d * y[0]);
    out[2] = (((y[2] * c) + (y[1] * b)) - (a * y[0])) + (y[3] * d);
    out[3] = (((y[3] * c) - (b * y[0])) - (y[1] * a)) - (y[2] * d);
    return out;
}

void XB_THISCALL FxBox_Expand(float* box, float amount) {
    const Lanes r = splat(amount);
    store_lanes(box, sub(lanes(box), r));
    store_lanes(box + 4, add(r, lanes(box + 4)));
}

uintptr_t XB_THISCALL FxEmitter_AddParticle(uint8_t* emitter, const uint8_t* particle) {
    uint8_t* const memory = pointer_at(emitter, 0xbc);
    if (!memory) return 0;
    const int32_t count = at<int32_t>(emitter, 0xc0);
    if (count >= at<int32_t>(emitter, 0xa8)) return reinterpret_cast<uintptr_t>(memory);
    const uint32_t stride = at<uint32_t>(emitter, 0x88);
    uint8_t* const slot = memory + stride * static_cast<uint32_t>(count);
    for (uint32_t i = 0; i < stride; ++i) slot[i] = particle[i];
    // The particle's bounds: its position grown by its size (emitter flag 2) or by 1.
    alignas(16) float box[8];
    const Lanes position = position_xyzz(particle);
    store_lanes(box, position);
    store_lanes(box + 4, position);
    FxBox_Expand(box, (emitter[0x84] & 2) ? at<float>(const_cast<uint8_t*>(particle), field(emitter, 0x8d)) : 1.0f);
    store_lanes(&at<float>(emitter, 0x60), lanes_min(lanes(&at<float>(emitter, 0x60)), lanes(box)));
    store_lanes(&at<float>(emitter, 0x70), lanes_max(lanes(&at<float>(emitter, 0x70)), lanes(box + 4)));
    const int32_t added = count + 1;
    at<int32_t>(emitter, 0xc0) = added;
    if (added > at<int32_t>(emitter, 0xc4)) at<int32_t>(emitter, 0xc4) = added;
    const int32_t total = title_global<int32_t>(0x00909ec0) + 1;
    title_global<int32_t>(0x00909ec0) = total;
    if (total > title_global<int32_t>(0x00909ec4)) title_global<int32_t>(0x00909ec4) = total;
    return static_cast<uint32_t>(total);
}


namespace {

// A particle generator (emitter config +0xc, 16 of them): vtable slot 1 fills `count` floats.
// Without one, `fallback` fills them.
float generate1(uint8_t* generator, float* out, float fallback) {
    if (!generator) return fallback;
    using Generate = void XB_THISCALL(uint8_t*, int32_t, float*);
    title_function<Generate>(vtable_slot(reinterpret_cast<uintptr_t>(generator), 1))(generator, 1, out);
    return *out;
}

// Three floats (a 4-float buffer; without a generator `fallback` fills all four).
void generate3(uint8_t* generator, float* out, float fallback) {
    if (generator) {
        using Generate = void XB_THISCALL(uint8_t*, int32_t, float*);
        title_function<Generate>(vtable_slot(reinterpret_cast<uintptr_t>(generator), 1))(generator, 3, out);
        return;
    }
    for (int i = 0; i < 4; ++i) out[i] = fallback;
}

}  // namespace

void XB_THISCALL apsActionEmitter_Apply(uint8_t* self, uint8_t*, uint8_t*, uint8_t* emitter, uint8_t*, float,
                                        float delta) {
    using Count = int32_t XB_THISCALL(uint8_t*, uint8_t*, float);
    int32_t n = title_function<Count>(vtable_slot(reinterpret_cast<uintptr_t>(self), 5))(self, emitter, delta);
    if (n <= 0) return;
    const int32_t count = at<int32_t>(emitter, 0xc0);
    const int32_t capacity = at<int32_t>(emitter, 0xa8);
    if (!(count + n < capacity)) n = capacity - count;
    if (n <= 0) return;
    uint8_t** const g = reinterpret_cast<uint8_t**>(pointer_at(self, 0xc));
    uint8_t* gen[16];
    for (int i = 0; i < 16; ++i) gen[i] = g[i];
    const uint32_t stride = at<uint32_t>(emitter, 0x88);
    alignas(16) uint8_t particle[0x140];
    for (uint32_t i = 0; i < stride; ++i) particle[i] = 0;
    alignas(16) float matrix[16];
    std::memcpy(matrix, &at<float>(emitter, 0x10), sizeof(matrix));
    // The emitter's path since the last frame (+0x50 -> matrix row 3), split into n steps.
    Lanes position = lanes(matrix + 12);
    Lanes step = lanes(&title_global<float>(0x007c8c00));
    if (n > 1) {
        step = mul(sub(position, lanes(&at<float>(emitter, 0x50))), title_global<float>(0x0083eb20) / static_cast<float>(n));
        position = add(lanes(&at<float>(emitter, 0x50)), step);
    }
    const uint32_t flags = at<uint32_t>(emitter, 0x84);
    const bool world = emitter[0xae] & 2;
    const float one = title_global<float>(0x0083eb20);
    for (int32_t left = n; left != 0; --left) {
        store_lanes(matrix + 12, position);
        position = add(position, step);
        // A generator fills three lanes; the 4th is 0 as in the original's (zeroed) stack slot.
        alignas(16) float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        alignas(16) float moved[4];
        float scalar;
        generate3(gen[0], v, 0.0f);
        if (!world) std::memcpy(v, Mat44_TransformPoint(moved, matrix, v), sizeof(v));
        store_xyz(particle, lanes(v));
        const float size = generate1(gen[1], &scalar, one);
        if (flags & 2) at<float>(particle, field(emitter, 0x8d)) = size;
        const float width = generate1(gen[2], &scalar, one);
        if (flags & 4) at<float>(particle, field(emitter, 0x8e)) = width;
        if (flags & 8) {
            alignas(16) float colour[4];
            generate3(gen[3], colour, one);
            store_xyz(particle + field(emitter, 0x8f), lanes(colour));
        }
        if (flags & 0x10) at<float>(particle, field(emitter, 0x90)) = generate1(gen[4], &scalar, one);
        if (flags & 0x20) {
            float* const rotation = &at<float>(particle, field(emitter, 0x91));
            if (gen[5]) {
                float euler[4];
                generate3(gen[5], euler, 0.0f);
                float q[4];
                std::memcpy(rotation, Quat_FromEuler(q, euler[0], euler[1], euler[2]), sizeof(q));
            } else {
                rotation[0] = 0.0f;
                rotation[1] = 0.0f;
                rotation[2] = 0.0f;
                rotation[3] = one;
            }
        }
        const float angle = generate1(gen[6], &scalar, one);
        if (flags & 0x40) at<float>(particle, field(emitter, 0x92)) = angle;
        const float height = generate1(gen[7], &scalar, one);
        if (flags & 0x80) at<float>(particle, field(emitter, 0x93)) = height;
        const float frame = generate1(gen[8], &scalar, 0.0f);
        if (flags & 0x100) at<float>(particle, field(emitter, 0x94)) = frame;
        at<float>(particle, field(emitter, 0x95)) = 0.0f;
        at<float>(particle, field(emitter, 0x96)) = generate1(gen[9], &scalar, one);
        const float peak = generate1(gen[10], &scalar, one);
        if (flags & 0x1000) at<float>(particle, field(emitter, 0x98)) = peak;
        if (flags & 0x800) {
            alignas(16) float velocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            generate3(gen[11], velocity, 0.0f);
            if (!world) std::memcpy(velocity, Mat44_TransformVector(moved, matrix, velocity), sizeof(velocity));
            store_xyz(particle + field(emitter, 0x97), lanes(velocity));
            if (flags & 0x8000) {
                // The KTau frame: tangent along the velocity, a normal across it, their cross.
                const Lanes direction = lanes(velocity);
                const Lanes tangent = mul(direction, unit_scale(direction, 0x007c8be0, 0x007c8bf0));
                const Lanes negated = neg(tangent);
                const Lanes across = tangent.v[2] == 0.0f
                                         ? Lanes{{tangent.v[1], negated.v[0], 0.0f, tangent.v[1]}}
                                         : Lanes{{tangent.v[2], 0.0f, negated.v[0], tangent.v[2]}};
                const Lanes normal = mul(across, unit_scale(across, 0x007c8be0, 0x007c8bf0));
                store_xyz(particle + field(emitter, 0x9b), tangent);
                store_xyz(particle + field(emitter, 0x9c), normal);
                store_xyz(particle + field(emitter, 0x9d), cross_lanes(tangent, normal));
            }
        }
        if (flags & 0x4000) {
            alignas(16) float spin[4];
            generate3(gen[13], spin, 0.0f);
            if (!world) std::memcpy(spin, Mat44_TransformVector(moved, matrix, spin), sizeof(spin));
            store_xyz(particle + field(emitter, 0x9a), lanes(spin));
        }
        const float angular = generate1(gen[12], &scalar, 0.0f);
        if (flags & 0x2000) at<float>(particle, field(emitter, 0x99)) = angular;
        const float curvature = generate1(gen[14], &scalar, 0.0f);
        if (flags & 0x40000) at<float>(particle, field(emitter, 0x9e)) = curvature;
        const float torsion = generate1(gen[15], &scalar, 0.0f);
        if (flags & 0x80000) at<float>(particle, field(emitter, 0x9f)) = torsion;
        if (flags & 0x800000) at<float>(particle, field(emitter, 0xa3)) = 0.0f;
        if (flags & 0x2000000) at<float>(particle, field(emitter, 0xa5)) = 0.0f;
        if (flags & 0x100000) at<float>(particle, field(emitter, 0xa0)) = 0.0f;
        FxEmitter_AddParticle(emitter, particle);
    }
}

// The FX component helpers (FX_Add*Components and the component Updates).

uint8_t* XB_REGS1 XB_CDECL FxComponent_GetEntity(const uint8_t* component) {
    return handle_object(*reinterpret_cast<const uint32_t*>(component + 0xc), 0x0097a968);
}

uint8_t* XB_REGS1 XB_CDECL DObjHandle_Resolve(const uint32_t* handle) {
    return handle_object(*handle, 0x00981418);
}

void XB_REGS3 XB_CDECL WeaponModel_GetDObjHandle(uint32_t* out, uint32_t, uint32_t index) {
    const uint8_t* model = title_global<const uint8_t*>(0x00975fa8 + index * 0xc4);
    *out = model ? *reinterpret_cast<const uint32_t*>(model + 0x8c) : 0;
}

void XB_REGS3 XB_CDECL FxComponent_GetEntityOrigin(uint32_t* out, uint32_t, const uint8_t* component) {
    const uint8_t* entity = FxComponent_GetEntity(component);
    std::memcpy(out, entity + 0x140, 16);
}

void XB_REGS2 XB_CDECL FxComponent_GetPosition(float* out, const uint8_t* component) {
    if (const uint8_t* placement = *reinterpret_cast<uint8_t* const*>(component + 0x14)) {
        std::memcpy(out, placement + 0x30, 16);
        return;
    }
    const float* origin = reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140);
    out[0] = origin[0];
    out[1] = origin[1];
    out[2] = origin[2];
    out[3] = origin[0];
}

PLATFORM_ASM_ENTRY(Rumble_SetScale_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Rumble_SetScale) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL Rumble_SetScale(uint8_t* manager, uint32_t id, float scale) {
    for (int list = 0; list < 2; ++list) {
        uint32_t* head = reinterpret_cast<uint32_t*>(manager + 8 + list * 0x10);
        uint32_t* node = reinterpret_cast<uint32_t*>(uintptr_t{*head});
        uint32_t* next = node ? reinterpret_cast<uint32_t*>(uintptr_t{node[0]}) : nullptr;
        if (node == head + 1 || !next) continue;
        for (;;) {
            if (node[2] == id) {
                std::memcpy(&node[4], &scale, 4);
                break;
            }
            node = next;
            next = reinterpret_cast<uint32_t*>(uintptr_t{next[0]});
            if (!next) break;
        }
    }
}

PLATFORM_ASM_ENTRY(CameraShake_SetAmplitude_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(CameraShake_SetAmplitude) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL CameraShake_SetAmplitude(uint8_t* shake, float rotation, float position) {
    if (rotation != 0.0f) {
        std::memcpy(shake + 0x18, &rotation, 4);
        std::memcpy(shake + 0x28, &rotation, 4);
    }
    if (position != 0.0f) {
        std::memcpy(shake + 0x14, &position, 4);
        std::memcpy(shake + 0x24, &position, 4);
    }
}

uint8_t* XB_REGS2 XB_CDECL CameraShake_AllocSlot(uint32_t, uint8_t* manager) {
    int32_t i = 0;
    while (*reinterpret_cast<const uint32_t*>(manager + 0x48 + i * 0x40) != 0) {
        if (++i >= 5) return nullptr;
    }
    uint8_t* slot = manager + 0xc + i * 0x40;
    if (!slot) return nullptr;
    std::memset(slot, 0, 0x40);
    *reinterpret_cast<uint32_t*>(slot + 0x3c) = 1;
    return slot;
}

uint32_t XB_REGS1 XB_CDECL ParamChoice_GetFlags(const uint8_t* choice) {
    static const uint16_t kIds[] = {0xd, 0xe, 0x12, 0x16, 0x20};
    uint32_t flags = 0;
    for (uint32_t i = 0; i < 5; ++i) {
        const uint32_t* value = ParamChoice_GetValue(choice, kIds[i]);
        if (value && *value) flags |= 1u << i;
    }
    return flags;
}

const uint8_t* XB_CDECL ParamChoiceList_PickRandom(const uint32_t* list) {
    // Weights (parameter 0x21, default 1) of the first 32 choices; the original reads past them
    // into its other locals for longer lists (none seen).
    float weights[32];
    int32_t stored = 0;
    float total = 0.0f;
    for (uint32_t i = 0; i < list[0]; ++i) {
        const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{list[1 + i]});
        const uint32_t* value = ParamChoice_GetValue(choice, 0x21);
        float weight = 1.0f;
        if (value) std::memcpy(&weight, value, 4);
        if (stored < 32) weights[stored++] = weight;
        total = weight + total;
    }
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    const float random = platform_x87_unsigned_to_float(seed >> 17);
    const float pick = platform_x87_mul_div_add(total, random, 32768.0f, 0.0f);  // (total - 0) * r / 32768 + 0
    const uint32_t last = list[0] - 1;
    float sum = 0.0f;
    for (uint32_t i = 0; i < last; ++i) {
        if (pick >= sum && weights[i] + sum > pick)
            return reinterpret_cast<const uint8_t*>(uintptr_t{list[1 + i]});
        sum = weights[i] + sum;
    }
    return reinterpret_cast<const uint8_t*>(uintptr_t{list[list[0]]});
}

namespace {

// Parameter 0x22 of a choice: its group name, or null.
const char* choice_group(const uint8_t* choice) {
    const uint32_t* value = ParamChoice_GetValue(choice, 0x22);
    return value ? reinterpret_cast<const char*>(uintptr_t{*value}) : nullptr;
}

}  // namespace

void XB_CDECL ParamChoiceList_PickPerGroup(uint32_t* list) {
    if (list[0] == 0) return;
    uint32_t i = 0;
    for (;;) {
        const char* group = choice_group(reinterpret_cast<const uint8_t*>(uintptr_t{list[1 + i]}));
        if (group && *group) break;
        if (++i >= list[0]) return;
    }
    uint32_t all[0x41];
    std::memcpy(all, list, sizeof(all));
    list[0] = 0;
    uint32_t done[2] = {0, 0};
    for (uint32_t j = 0; j < all[0]; ++j) {
        const char* group = choice_group(reinterpret_cast<const uint8_t*>(uintptr_t{all[1 + j]}));
        if (group && *group) continue;
        done[j >> 5] |= 1u << (j & 0x1f);
        if (static_cast<int32_t>(list[0]) < 0x40) list[1 + list[0]++] = all[1 + j];
    }
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    for (uint32_t j = 0; j < all[0]; ++j) {
        if (done[j >> 5] & (1u << (j & 0x1f))) continue;
        const char* group = choice_group(reinterpret_cast<const uint8_t*>(uintptr_t{all[1 + j]}));
        uint32_t members[0x41];
        members[0] = 0;
        for (uint32_t k = 0; k < all[0]; ++k) {
            if (done[k >> 5] & (1u << (k & 0x1f))) continue;
            const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{all[1 + k]});
            const char* name = choice_group(choice);
            if (!name || title_function<Stricmp>(0x001f1443)(name, group) != 0) continue;
            done[k >> 5] |= 1u << (k & 0x1f);
            if (static_cast<int32_t>(members[0]) < 0x40) members[1 + members[0]++] = all[1 + k];
        }
        const uint8_t* picked = ParamChoiceList_PickRandom(members);
        if (static_cast<int32_t>(list[0]) < 0x40) list[1 + list[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(picked));
    }
}

namespace {

// The title CRT's __CIsqrt / __CIpow (x87 intrinsics: argument(s) in ST, result in ST),
// called where the original calls them so error cases behave alike.
float title_ci_unary(uint32_t function, float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{function})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

float title_ci_sqrt(float x) { return title_ci_unary(0x0033acf4, x); }
float title_ci_sin(float x) { return title_ci_unary(0x0033ae60, x); }
float title_ci_cos(float x) { return title_ci_unary(0x0033adb0, x); }

float title_ci_pow(double base, float exponent) {
    float result;
    __asm__ volatile("fldl %1\n\tflds %2\n\tcall *%3\n\tfstps %0"
                     : "=m"(result)
                     : "m"(base), "m"(exponent), "r"(uintptr_t{0x001f1a6c})
                     : "eax", "ecx", "edx", "st", "st(1)", "memory");
    return result;
}

uint32_t particle_rand15() {
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    return seed >> 17;
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_AxesFromForward_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FX_AxesFromForward) "addl $12, %esp\n"
                   "ret")

void XB_CDECL FX_AxesFromForward(const float* forward, float* right, float* up) {
    right[1] = 0.0f - forward[0];
    std::memcpy(&right[2], &forward[1], 4);
    std::memcpy(&right[0], &forward[2], 4);
    const float d = 0.0f - (forward[0] * right[0] + forward[2] * right[2] + right[1] * forward[1]);
    right[0] = forward[0] * d + right[0];
    right[1] = forward[1] * d + right[1];
    right[2] = forward[2] * d + right[2];
    const float length = title_ci_sqrt(right[2] * right[2] + right[0] * right[0] + right[1] * right[1]);
    if (length != 0.0f) {
        const float scale = 1.0f / length;
        right[0] = right[0] * scale;
        right[1] = right[1] * scale;
        right[2] = right[2] * scale;
    }
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - forward[2] * right[0];
    up[2] = forward[1] * right[0] - right[1] * forward[0];
}

PLATFORM_ASM_ENTRY(FX_RandomInOctaves_entry,
                   "leal 8(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 8(%esp)\n" PLATFORM_ASM_CALL(FX_RandomInOctaves) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "ret")

float XB_CDECL FX_RandomInOctaves(float value, float* octaves) {
    // The original keeps pow's result and then the random number in its `octaves` slot.
    const float factor = title_ci_pow(2.0, *octaves);
    const float high = factor * value;
    const float low = value / factor;
    const float random = platform_x87_unsigned_to_float(particle_rand15());
    *octaves = random;
    return (high - low) * random / 32768.0f + low;
}

PLATFORM_ASM_ENTRY(CameraShake_InitSlot_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CameraShake_InitSlot) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL CameraShake_InitSlot(int32_t type, uint8_t* slot, float duration) {
    const uint8_t* def = &title_global<uint8_t>(0x008d5138);
    while (*reinterpret_cast<const int16_t*>(def) != type) def += 0x20;
    using Rand = int32_t XB_CDECL();
    auto put = [slot](uint32_t offset, const void* value) { std::memcpy(slot + offset, value, 4); };
    const float phase0 = static_cast<float>(title_function<Rand>(0x001efcc0)() % 0x4000);
    put(0xc, &phase0);
    *reinterpret_cast<uint32_t*>(slot + 0x10) = 2;
    put(0x18, def + 0xc);
    put(0x14, def + 8);
    const float phase1 = static_cast<float>(title_function<Rand>(0x001efcc0)() % 0x4000);
    put(0x1c, &phase1);
    *reinterpret_cast<uint32_t*>(slot + 0x20) = 2;
    put(0x24, def + 8);
    put(0x28, def + 0xc);
    if (duration != 0.0f)
        put(0x30, &duration);
    else
        put(0x30, def + 4);
}

PLATFORM_ASM_ENTRY(FXTriggerTable_Find_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FXTriggerTable_Find) "addl $8, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL FXTriggerTable_Find(const uint8_t* table, const char* name) {
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(table + 0x24); ++i) {
        uint8_t* entry = *reinterpret_cast<uint8_t* const*>(table + 0x20) + i * 0x34;
        if (title_function<Stricmp>(0x001f1443)(reinterpret_cast<const char*>(entry), name) == 0) return entry;
    }
    return nullptr;
}

PLATFORM_ASM_ENTRY(FixedStr32_Format_entry,
                   "leal 8(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FixedStr32_Format) "addl $12, %esp\n"
                   "ret")

char* XB_CDECL FixedStr32_Format(char* out, const char* format, va_list args) {
    out[0x1f] = 0;
    out[0] = 0;
    title_function<int XB_CDECL(char*, const char*, va_list)>(0x001eea95)(out, format, args);  // _vsprintf
    uint32_t length = 0;
    while (out[length]) ++length;
    out[0x1f] = static_cast<char>(length);
    return out;
}

namespace {

char* fixed_str32_format(char* out, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FixedStr32_Format(out, format, args);
    va_end(args);
    return out;
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_CollectTriggers_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FX_CollectTriggers) "addl $16, %esp\n"
                   "ret $12")

void XB_CDECL FX_CollectTriggers(uint32_t* list, uint32_t zones, const char* prefix, const char* suffix) {
    // ZoneStringMap_Lookup (EDX zone set; asset maps, out, key, a word it never reads, out index).
    // The original's out is its own `suffix` argument slot, and the unread word that slot with
    // its low byte cleared.
    using Lookup = void XB_REGS2 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, const char*, uint32_t, uint32_t*);
    const auto lookup = [](uint32_t zone_set, uint32_t* slot, const char* key) {
        title_function<Lookup>(0x0002bf80)(0, zone_set, title_global<uint32_t>(0x0095f5a8), slot, key,
                                           *slot & ~0xffu, nullptr);
    };
    const auto add = [list](uint32_t table) {
        if (!table) return;
        uint8_t* triggers = FXTriggerTable_Find(reinterpret_cast<const uint8_t*>(uintptr_t{table}), "TRIGGERS");
        if (triggers && static_cast<int32_t>(list[0]) < 0x10)
            list[1 + list[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(triggers));
    };
    const uint8_t* zone_sets = title_global<const uint8_t*>(0x0095f638);
    uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(suffix));
    lookup(*reinterpret_cast<const uint32_t*>(zone_sets + 0x2c), &slot, suffix);
    const uint32_t first = slot;
    add(first);
    lookup(*reinterpret_cast<const uint32_t*>(title_global<const uint8_t*>(0x0095f638) + 0x34), &slot, suffix);
    if (slot != first) add(slot);
    if (prefix) {
        char name[32];
        fixed_str32_format(name, "%s-%s", prefix, suffix);
        lookup(zones, &slot, name);
        add(slot);
    }
}

PLATFORM_ASM_ENTRY(CameraShake_SetDistanceScale_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CameraShake_SetDistanceScale) "addl $12, %esp\n"
                   "ret $8")

void XB_CDECL CameraShake_SetDistanceScale(const float* origin, uint8_t* slot, float falloff) {
    const float base = title_global<float>(0x00964b80);
    float range;
    if (!origin) {
        range = base * 2.0f;
    } else {
        const float* camera = nglGetViewMatrix() + 12;
        const float dx = camera[0] - origin[0], dy = camera[1] - origin[1], dz = camera[2] - origin[2];
        const float dw = camera[3] - origin[3];
        platform_keep(dw * dw);  // MULPS squares every lane
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        float distance = distance_sq;
        if (distance_sq != 0.0f && distance_sq == distance_sq) {
            const float estimate = platform_rsqrt_estimate(distance_sq);
            const float x = distance_sq * estimate;
            distance = (3.0f - x * estimate) * x * 0.5f;
        }
        range = distance * falloff;
        const float minimum = base * 2.0f;
        if (minimum > range) range = minimum;
    }
    float scale = 1.0f;
    if (range != 0.0f) scale = 1.0f / range;
    std::memcpy(slot + 0x2c, &scale, 4);
    const float scaled = base * scale;
    std::memcpy(slot + 0x2c, &scaled, 4);
}

uint8_t* XB_STDCALL CameraShake_Start(uint8_t* manager, int32_t type, const float* origin, float strength, float duration, float cooldown) {
    uint8_t* row = &title_global<uint8_t>(0x008d5138);
    while (*reinterpret_cast<const int16_t*>(row) != type) row += 0x20;
    float* row_f = reinterpret_cast<float*>(row);
    if (cooldown != -1.0f) row_f[6] = cooldown;
    const float now = static_cast<float>(title_global<int32_t>(0x0097253c)) * 0.001f;  // cg_time
    if (row_f[6] > now - row_f[7]) return nullptr;
    row_f[7] = now;
    // The original keeps the duration in its `type` argument slot.
    float length = row_f[1];
    if (duration != 0.0f) length = duration;
    uint8_t* slot;
    if (*reinterpret_cast<const int16_t*>(row + 2) == 0) {
        const float scale = strength == 0.0f ? 1.0f : strength;
        slot = CameraShake_AllocSlot(0, manager);
        if (!slot) return nullptr;
        *reinterpret_cast<int32_t*>(slot) = type;
        std::memcpy(slot + 4, &scale, 4);
        *reinterpret_cast<uint32_t*>(slot + 8) = 0;
        CameraShake_SetDistanceScale(origin, slot, row_f[4]);
    } else {
        slot = CameraShake_AllocSlot(0, manager);
        *reinterpret_cast<int32_t*>(slot) = type;
        std::memcpy(slot + 4, &strength, 4);
        *reinterpret_cast<uint32_t*>(slot + 8) = 0;
    }
    float factor = reinterpret_cast<const float*>(manager)[1];
    const uint8_t* player = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f534) + 4);
    if (player && !(*(*reinterpret_cast<uint8_t* const*>(player + 0x23c) + 0x2c) & 0x20) &&
        *reinterpret_cast<const uint32_t*>(manager + 8) != 0)
        factor = 1.0f;
    float* slot_f = reinterpret_cast<float*>(slot);
    slot_f[1] = slot_f[1] * factor;
    slot_f[13] = slot_f[13] * factor;
    CameraShake_InitSlot(type, slot, length);
    return slot;
}

PLATFORM_ASM_ENTRY(FxShakeRumble_GetFalloff_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FxShakeRumble_GetFalloff) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL FxShakeRumble_GetFalloff(const uint8_t* component) {
    const float* range = reinterpret_cast<const float*>(component + 0x44);  // near, far (squared)
    if (!FxComponent_GetEntity(component) || !(range[0] > 0.0f) || !(range[1] > range[0])) return 1.0f;
    alignas(16) uint32_t origin_bits[4];
    FxComponent_GetEntityOrigin(origin_bits, 0, component);
    float origin[4];
    std::memcpy(origin, origin_bits, sizeof(origin));
    const uint8_t* player = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f534) + 4);
    const float* eye = reinterpret_cast<const float*>(player + 0x140);
    const float dx = eye[0] - origin[0], dy = eye[1] - origin[1], dz = eye[2] - origin[2];
    const float dw = eye[3] - origin[3];
    platform_keep(dw * dw);  // MULPS squares every lane
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    if (range[0] > distance_sq) return 1.0f;
    if (!(range[1] > distance_sq)) return 0.0f;
    return 1.0f - (distance_sq - range[0]) / (range[1] - range[0]);
}

PLATFORM_ASM_ENTRY(ShakeRumbleRecord_Init_entry,
                   "leal 28(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(ShakeRumbleRecord_Init) "addl $36, %esp\n"
                   "ret $28")

uint8_t* XB_CDECL ShakeRumbleRecord_Init(uint8_t* record, float fade_out, uint32_t id, float start, float a3, float strength,
                                         float hold, float fade_in, ScriptString* name) {
    float* f = reinterpret_cast<float*>(record);
    uint32_t* w = reinterpret_cast<uint32_t*>(record);
    f[3] = 0.0f - start;
    f[4] = a3;
    f[5] = strength;
    f[6] = hold;
    const float end_hold = hold + fade_out;
    f[7] = end_hold;
    w[0] = 0;
    w[1] = 0;
    w[2] = id;
    f[8] = end_hold + fade_in;
    f[9] = fade_out;
    ScriptString* own = reinterpret_cast<ScriptString*>(record + 0x28);
    ScriptString_Copy(own, name);
    const ScriptStringRep* rep = own->rep;
    record[0x2c] = rep && rep->text[0] ? 1 : 0;  // the original also tests rep + 6 for null
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(name);  // ScriptString_Release
    return record;
}

uint32_t* XB_THISCALL ShakeRumble_Start(const uint8_t* desc, uint8_t* manager, uint32_t* out_id, float a3) {
    const uint32_t id = *reinterpret_cast<uint32_t*>(manager);
    *reinterpret_cast<uint32_t*>(manager) = id + 1;
    for (int channel = 0; channel < 2; ++channel) {
        const uint8_t* c = desc + 4 + channel * 0x20;  // {start, strength, hold, fade out, fade in, name}
        const float* cf = reinterpret_cast<const float*>(c);
        if (!(cf[1] > 0.0f)) continue;
        uint8_t* record = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(
            title_global<uint32_t>(0x0095f5c8), 0x30, 0);  // SmallHeap_Alloc
        if (record) {
            ScriptString name{nullptr};
            ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(c + 0x14));
            ShakeRumbleRecord_Init(record, cf[3], id, cf[0], a3, cf[1], cf[2], cf[4], &name);
        }
        // Appended to the channel's list (count +4, end node +0xc {next, prev}).
        uint8_t* list = manager + 0x10 + channel * 0x10;
        uint32_t** last = reinterpret_cast<uint32_t**>(list);
        uint32_t* node = reinterpret_cast<uint32_t*>(record);
        node[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list - 4));
        node[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*last));
        (*last)[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(record));
        *last = node;
        ++*reinterpret_cast<uint32_t*>(list - 0xc);
    }
    *out_id = id;
    return out_id;
}

PLATFORM_ASM_ENTRY(ShakeRumbleDesc_Init_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ShakeRumbleDesc_Init) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL ShakeRumbleDesc_Init(uint8_t* desc) {
    for (int channel = 0; channel < 2; ++channel)
        title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x0011d5d0)(desc + 0x18 + channel * 0x20, 0);  // ScriptString_FromRep
    float* f = reinterpret_cast<float*>(desc);
    desc[0] = 0;
    f[1] = 0.0f;
    f[2] = 1.0f;
    f[3] = 0.0f;
    f[4] = 1.0f;
    f[5] = 0.0f;
    desc[0x1c] = 0;
    desc[0x20] = 0;
    f[9] = 0.0f;
    f[10] = 1.0f;
    f[11] = 0.0f;
    f[12] = 1.0f;
    f[13] = 0.0f;
    desc[0x3c] = 0;
    return desc;
}

void XB_REGS1 XB_CDECL ShakeRumbleDesc_Destroy(uint8_t* desc) {
    for (int channel = 1; channel >= 0; --channel)
        title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(desc + 0x18 + channel * 0x20);  // ScriptString_Release
}

void XB_THISCALL FxShakeRumbleComponent_Update(uint8_t* component, float delta) {
    float* f = reinterpret_cast<float*>(component);
    f[9] = delta + f[9];  // +0x24 age
    if (f[8] > f[9]) return;  // +0x20 start delay
    if (component[0x2a] & 2) {
        const float falloff = FxShakeRumble_GetFalloff(component);
        if (uint8_t* shake = *reinterpret_cast<uint8_t* const*>(component + 0x64)) {
            const float rotation = f[13] * falloff;
            const float position = f[12] * falloff;
            if (rotation != 0.0f) {
                std::memcpy(shake + 0x18, &rotation, 4);
                std::memcpy(shake + 0x28, &rotation, 4);
            }
            if (position != 0.0f) {
                std::memcpy(shake + 0x14, &position, 4);
                std::memcpy(shake + 0x24, &position, 4);
            }
        }
        if (uint8_t* manager = title_global<uint8_t*>(0x0095f5c4))
            Rumble_SetScale(manager, *reinterpret_cast<const uint32_t*>(component + 0x60), falloff);
        return;
    }
    component[0x2a] |= 2;
    const float falloff = FxShakeRumble_GetFalloff(component);  // kept in the `delta` slot by the original
    if (f[12] > 0.0f && f[13] > 0.0f && falloff > 0.0f) {
        uint8_t* shake = CameraShake_Start(&title_global<uint8_t>(0x00964b80), 1, nullptr, 1.0f, f[11], f[14]);
        *reinterpret_cast<uint8_t**>(component + 0x64) = shake;
        if (shake) CameraShake_SetAmplitude(shake, f[13] * falloff, f[12] * falloff);
    }
    if (!component[0x59]) return;
    alignas(4) uint8_t desc[0x40];
    ShakeRumbleDesc_Init(desc);
    float* d = reinterpret_cast<float*>(desc);
    d[2] = component[0x58] ? 1.0f : 0.0f;
    d[4] = f[19];
    float strength = f[15];
    if (0.0f > strength)
        strength = 0.0f;
    else if (strength > 1.0f)
        strength = 1.0f;
    d[10] = strength;
    d[12] = f[19];
    d[11] = f[20];
    d[13] = f[21];
    if (uint8_t* manager = title_global<uint8_t*>(0x0095f5c4)) {
        uint32_t id;  // the original's `delta` slot again
        *reinterpret_cast<uint32_t*>(component + 0x60) = *ShakeRumble_Start(desc, manager, &id, falloff);
    }
    ShakeRumbleDesc_Destroy(desc);
}

uint32_t XB_REGS1 XB_STDCALL ZoneAliasList_NextVariant(const uint32_t* zones, uint32_t key) {
    for (int32_t i = 0; i < 0x63; ++i) {
        const uint8_t* table = reinterpret_cast<const uint8_t*>(uintptr_t{zones[1 + i]});
        if (!table) continue;
        const uint32_t* found = UintMap_Find(reinterpret_cast<const uint32_t*>(table + 8), &key);
        if (!found) continue;
        uint32_t index = *found;
        if (index >= *reinterpret_cast<const uint32_t*>(table + 0x10)) index = 0;
        uint32_t* list = reinterpret_cast<uint32_t*>(uintptr_t{(*reinterpret_cast<uint32_t* const*>(table + 0x14))[index]});
        if (!list) continue;
        // {count, items, round-robin position}
        const uint32_t count = list[0];
        const uint32_t* items = reinterpret_cast<const uint32_t*>(uintptr_t{list[1]});
        if (count == 0) return 0;
        if (count == 1) return items[0];
        uint32_t next = (list[2] + 1) % count;
        list[2] = next;
        if (next >= count) next = 0;
        return items[next];
    }
    return 0;
}

void XB_REGS3 XB_CDECL FX_AnglesToMatrix(const float* translation, uint32_t, const float* angles, float* out) {
    const float s0 = title_ci_sin(angles[0]), c0 = title_ci_cos(angles[0]);
    const float s1 = title_ci_sin(angles[1]), c1 = title_ci_cos(angles[1]);
    const float s2 = title_ci_sin(angles[2]), c2 = title_ci_cos(angles[2]);
    const float* t = translation ? translation : &title_global<float>(0x0082e290);  // {0, 0, 0, 1}
    const float c1c0 = c1 * c0;
    const float a = s2 * s1;
    const float a_c0 = a * c0 - c2 * s0;
    const float b = c2 * s1;
    const float b_c0 = b * c0 + s2 * s0;
    const float a_s0 = a * s0;
    const float c1s0 = c1 * s0;
    const float b_s0 = b * s0 - s2 * c0;
    const float c2c1 = c2 * c1;
    const float s2c1 = s2 * c1;
    const float a_s0_c2c0 = a_s0 + c2 * c0;
    const float minus_s1 = 0.0f - s1;
    const float m[16] = {c2c1, s2c1, minus_s1, c2c1, b_s0, a_s0_c2c0, c1s0, b_s0,
                         b_c0, a_c0, c1c0, b_c0, t[0], t[1], t[2], t[3]};
    std::memcpy(out, m, sizeof(m));
}

uint32_t XB_REGS3 XB_CDECL FX_PlayEffectWithAxis(const float* axis, uint32_t, const float* origin, uint32_t effect) {
    alignas(16) float matrix[16];
    for (int row = 0; row < 4; ++row) {
        const float* src = row < 3 ? &axis[row * 3] : origin;
        matrix[row * 4 + 0] = src[0];
        matrix[row * 4 + 1] = src[1];
        matrix[row * 4 + 2] = src[2];
        matrix[row * 4 + 3] = src[0];
    }
    return title_function<uint32_t XB_CDECL(uint32_t, float*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0023c040)(
        effect, matrix, 0, 0, 0xffffffff, 0);  // FX_SpawnEffect
}

uint32_t XB_REGS1 XB_CDECL FX_PlayEffectAlongDir(const float* direction, uint32_t effect, const float* origin) {
    float axis[9];  // right, up, forward
    axis[6] = direction[0];
    axis[7] = direction[1];
    axis[8] = direction[2];
    FX_AxesFromForward(&axis[6], &axis[0], &axis[3]);
    return FX_PlayEffectWithAxis(axis, 0, origin, effect);
}

void XB_THISCALL FxParticleComponent_Update(uint8_t* component, float delta) {
    // The spawn parameters FX_AddParticleComponents left in globals: 0x009717f4 entity handle,
    // 0x00971804 placement {origin, +0x10 direction} or null, 0x0097180c tag name, 0x00971828
    // tag index (-1: by name), 0x0097182c/0x00971838 flags, 0x00971830 effect, 0x00971834.
    using SpawnEffect = uint32_t XB_CDECL(uint32_t, const float*, uint32_t, uint32_t, uint32_t, uint32_t);
    const auto spawn = title_function<SpawnEffect>(0x0023c040);
    const auto entity_of = [](uint32_t address) {
        return title_function<uint8_t* XB_REGS1 XB_CDECL(uint32_t)>(0x00013860)(address);  // G_EntHandleToEnt
    };
    float* f = reinterpret_cast<float*>(component);
    uint16_t& flags = *reinterpret_cast<uint16_t*>(component + 0x2a);
    uint32_t& result = *reinterpret_cast<uint32_t*>(component + 0x2c);
    f[9] = delta + f[9];  // +0x24 age
    if (f[8] > f[9]) return;  // +0x20 start delay
    if (!(component[0x2a] & 2)) {
        const uint8_t* placement = title_global<const uint8_t*>(0x00971804);
        const uint32_t effect = title_global<uint32_t>(0x00971830);
        if (placement)
            component[0x2a] |= 0x40;
        else
            flags |= 0x80;
        if (title_global<uint8_t>(0x0097182c)) {
            component[0x2b] |= 1;
            flags |= 0x80;
            component[0x2a] &= ~0x40;
        }
        if (title_global<uint8_t>(0x00971838)) component[0x2b] |= 2;
        const uint32_t handle = *reinterpret_cast<const uint32_t*>(component + 0xc);
        uint32_t slot = 0;  // the entity, later the weapon model's DObj handle (one local)
        if (const uint8_t* e = handle_object(handle, 0x0097a968)) slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e));
        const uint8_t* entity = reinterpret_cast<const uint8_t*>(uintptr_t{slot});
        const DObj* obj = *reinterpret_cast<DObj* const*>(entity + 0x22c);
        uint32_t owner = obj ? *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(obj) + 0x8c) : 0;
        int32_t tag = title_global<int32_t>(0x00971828);
        bool bolt = tag != -1;
        if (!bolt) {
            const uint32_t hash = Com_HashKeyNoCase(0, &title_global<char>(0x0097180c));
            if (obj) {
                tag = DObj_GetTagIndex(obj, hash);
                bolt = tag != -1;
            }
            if (!bolt && entity[3]) {
                WeaponModel_GetDObjHandle(&slot, 0, entity[3]);
                if (const DObj* weapon = reinterpret_cast<const DObj*>(DObjHandle_Resolve(&slot))) {
                    tag = DObj_GetTagIndex(weapon, hash);
                    if (tag > 0) {
                        owner = slot;
                        bolt = true;
                    }
                }
            }
        }
        if (bolt) {
            result = FX_PlayBoltedEffect(reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140), effect,
                                         owner, handle, static_cast<uint32_t>(tag));
        } else if (const float* matrix = *reinterpret_cast<float* const*>(component + 0x14)) {
            result = spawn(effect, matrix, owner, handle, 0xffffffff, 1);
        } else if (flags & 0x80) {
            alignas(16) float m[16];
            bool attached = false;
            if (flags & 0x200) {
                const uint8_t* source = entity_of(0x009717f4);
                const float scale = title_global<float>(0x0082e140);
                for (int i = 0; i < 12; ++i) m[i] = (&title_global<float>(0x0082e160))[i] * scale;
                const float* origin = reinterpret_cast<const float*>(source + 0x140);
                m[12] = origin[0];
                m[13] = origin[1];
                m[14] = origin[2];
                m[15] = origin[0];
                attached = true;
            } else {
                const float pi = title_global<float>(0x0083ed00), half_turn = title_global<float>(0x0083eb60);
                const float yaw = reinterpret_cast<const float*>(entity_of(0x009717f4) + 0x154)[0] * pi / half_turn;
                const float pitch = reinterpret_cast<const float*>(entity_of(0x009717f4) + 0x150)[0] * pi / half_turn;
                const float roll = reinterpret_cast<const float*>(entity_of(0x009717f4) + 0x158)[0] * pi / half_turn;
                alignas(16) const float angles[4] = {roll, pitch, yaw, roll};
                FX_AnglesToMatrix(reinterpret_cast<const float*>(entity_of(0x009717f4) + 0x140), 0, angles, m);
            }
            result = spawn(effect, m, owner, handle, 0xffffffff, 1);
            if (result && attached) reinterpret_cast<uint8_t*>(uintptr_t{result})[0x16] |= 4;
            flags |= 2;
            goto finish;
        } else if (flags & 0x40) {
            const float* p = reinterpret_cast<const float*>(placement);
            alignas(16) const float origin[4] = {p[0], p[1], p[2], p[0]};
            const float direction[3] = {p[4], p[5], p[6]};
            result = FX_PlayEffectAlongDir(direction, effect, origin);
            if (result) *reinterpret_cast<uint16_t*>(*reinterpret_cast<uint8_t* const*>(uintptr_t{result} + 4) + 0x4e) |= 1;
            flags |= 2;
            goto finish;
        } else if (flags & 0x100) {
            alignas(16) float origin[4];
            std::memcpy(origin, FxComponent_GetEntity(component) + 0x140, 16);
            const float* p = reinterpret_cast<const float*>(placement);
            const float direction[3] = {p[4], p[5], p[6]};
            result = FX_PlayEffectAlongDir(direction, effect, origin);
        } else {
            result = FX_PlayEffectAt(effect, reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140));
        }
        flags |= 2;
    finish:
        if (result && title_global<uint32_t>(0x00971834)) *reinterpret_cast<uint16_t*>(uintptr_t{result} + 0x16) |= 2;
    }
    if (component[0x2a] & 1) f[7] = f[7] - delta;  // +0x1c
}

namespace {

// A ParamSet on the stack as FX_Add*Components build it: definition = the trigger entry.
void param_set_open(ParamSet* set, uint32_t trigger) {
    set->vtable = 0x008376b8;
    set->definition = reinterpret_cast<const uint8_t*>(uintptr_t{trigger});
    ParamList_Init(&set->list);
    set->failed = 0;
    set->used = 0;
}

// ParamSet_Destruct inlined (the original's order).
void param_set_close(ParamSet* set) {
    set->vtable = 0x008376b8;
    set->used = 0;
    ParamList_Clear(&set->list);
    set->failed = 0;
    set->list.vtable = 0x008376c0;
    ParamList_Clear(&set->list);
}

// The set's choices (vtable slot 1, 0x0029b9f0): three lists {count, choice*[64]}, the second
// the one the callers use.
void param_set_resolve(ParamSet* set, uint32_t* lists) {
    lists[0] = 0;
    lists[0x41] = 0;
    lists[0x82] = 0;
    title_function<void XB_THISCALL(ParamSet*, uint32_t*)>(vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(set)), 1))(
        set, lists);
}

float choice_float(const uint8_t* choice, uint32_t id) {
    const uint32_t* value = ParamChoice_GetValue(choice, id);
    float result = 0.0f;
    if (value) std::memcpy(&result, value, 4);
    return result;
}

}  // namespace

uint32_t XB_STDCALL FX_AddLightComponents(uint32_t, const uint8_t* source, uint32_t* out) {
    uint32_t added = 0;
    uint32_t triggers[17];
    triggers[0] = 0;
    FX_CollectTriggers(triggers, *reinterpret_cast<const uint32_t*>(source + 0x1f4), nullptr, "LIGHT.FX");
    for (uint32_t t = 0; t < triggers[0]; ++t) {
        ParamSet set;
        param_set_open(&set, triggers[1 + t]);
        ParamSet_AddFxOverrides(source, &set);
        uint32_t lists[0x83];
        param_set_resolve(&set, lists);
        uint32_t* choices = &lists[0x41];
        ParamChoiceList_PickPerGroup(choices);
        for (uint32_t i = 0; i < choices[0]; ++i) {
            const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{choices[1 + i]});
            const uint32_t* texture_value = ParamChoice_GetValue(choice, 0x17);
            uint32_t light_slot = texture_value ? *texture_value : 0;  // later the light
            const uint32_t flags = ParamChoice_GetFlags(choice);
            const float delay = choice_float(choice, 0x10);
            if (!light_slot) continue;
            uint8_t* component = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(
                title_global<uint32_t>(0x0095f5cc), 0x30, 0);  // SmallHeap_Alloc
            if (component) {
                const uint32_t entity = *reinterpret_cast<const uint32_t*>(source + 0x1f0);
                uint32_t* w = reinterpret_cast<uint32_t*>(component);
                w[3] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                std::memcpy(component + 0x20, &delay, 4);
                w[2] = *reinterpret_cast<const uint32_t*>(source + 0x1f4);
                w[3] = entity;
                w[4] = flags;
                w[5] = 0;
                const float zero = 0.0f;
                std::memcpy(component + 0x24, &zero, 4);
                *reinterpret_cast<uint16_t*>(component + 0x28) = 0;
                w[1] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                w[0] = 0x008376fc;
                uint8_t* light = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0x10);  // Z_Malloc
                if (light) light[0xc] = 0;
                light_slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(light));
                title_function<void XB_STDCALL(uint8_t*, uint32_t)>(0x002462b0)(light, entity);
                PtrVector_PushBack_call(&effect_lights(), reinterpret_cast<uint8_t* const*>(&light_slot));
                w[11] = light_slot;
            }
            if (static_cast<int32_t>(out[0]) < 0x10) out[1 + out[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component));
            ++added;
        }
        param_set_close(&set);
    }
    return added;
}

namespace {

// ParamChoice_GetValue for byte-sized values (index without the * 4).
const uint8_t* choice_byte(const uint8_t* choice, uint16_t id) {
    const uint8_t* definition = *reinterpret_cast<const uint8_t* const*>(choice + 4);
    const uint32_t count = *reinterpret_cast<const uint16_t*>(definition + 0x1e);
    uint8_t* const* entries = *reinterpret_cast<uint8_t* const* const*>(definition + 0x20);
    int32_t i = -1;
    for (uint32_t j = 0; j < count; ++j) {
        if (*reinterpret_cast<const uint16_t*>(entries[j]) == id) {
            i = static_cast<int16_t>(j);
            break;
        }
    }
    if (i == -1) return nullptr;
    const int32_t selected = (*reinterpret_cast<const int16_t* const*>(choice))[i];
    if (selected == -1) return nullptr;
    return *reinterpret_cast<uint8_t* const*>(entries[static_cast<uint16_t>(i)] + 8) + static_cast<uint16_t>(selected);
}

float choice_float_or(const uint8_t* choice, uint16_t id, float fallback) {
    const uint32_t* value = ParamChoice_GetValue(choice, id);
    if (!value) return fallback;
    float result;
    std::memcpy(&result, value, 4);
    return result;
}

}  // namespace

uint32_t XB_STDCALL FX_AddShakeRumbleComponents(uint32_t, const uint8_t* source, uint32_t* out) {
    uint32_t added = 0;
    uint32_t triggers[17];
    triggers[0] = 0;
    FX_CollectTriggers(triggers, *reinterpret_cast<const uint32_t*>(source + 0x1f4), nullptr, "SHAKEANDRUMBLE.FX");
    for (uint32_t t = 0; t < triggers[0]; ++t) {
        ParamSet set;
        param_set_open(&set, triggers[1 + t]);
        ParamSet_AddFxOverrides(source, &set);
        uint32_t lists[0x83];
        param_set_resolve(&set, lists);
        uint32_t* choices = &lists[0x41];
        ParamChoiceList_PickPerGroup(choices);
        for (uint32_t i = 0; i < choices[0]; ++i) {
            const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{choices[1 + i]});
            const uint32_t* name_value = ParamChoice_GetValue(choice, 0xf);
            uint8_t hashed[0x20];
            title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x00011060)(  // HashStr_Init (result unused)
                hashed, name_value ? *name_value : title_global<uint32_t>(0x008d5a74));
            const float delay = choice_float_or(choice, 0x10, 0.0f);
            const uint32_t flags = ParamChoice_GetFlags(choice);
            const float p32 = choice_float_or(choice, 0x32, 1.0f);
            const float p33 = choice_float_or(choice, 0x33, 40.0f);
            const float p34 = choice_float_or(choice, 0x34, title_global<float>(0x0083ebf8));  // 0.1
            const float p35 = choice_float_or(choice, 0x35, 1.0f);
            const float p36 = choice_float_or(choice, 0x36, 0.0f);
            const float p37 = choice_float_or(choice, 0x37, 0.0f);
            const float near_range = choice_float_or(choice, 0x4, 0.0f);
            const float far_range = choice_float_or(choice, 0x5, 0.0f);
            float p38 = choice_float_or(choice, 0x38, 0.0f);
            float p39 = choice_float_or(choice, 0x39, 0.0f);
            const float p3c = choice_float_or(choice, 0x3c, p32);
            float p3c_clamped = p3c;
            const uint8_t* b3a = choice_byte(choice, 0x3a);
            const uint8_t channel0 = b3a ? *b3a : 0;
            const uint8_t* b3b = choice_byte(choice, 0x3b);
            const uint8_t rumble = b3b ? *b3b : 1;
            if (p3c == p32) {
                p39 = 0.0f;
                p38 = 0.0f;
            }
            const float minimum = title_global<float>(0x0083eb94);  // 0.01
            if (minimum > p3c) p3c_clamped = minimum;
            const uint32_t* name17 = ParamChoice_GetValue(choice, 0x17);
            const uint32_t rumble_name = name17 ? *name17 : 0;
            uint8_t* component = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(
                title_global<uint32_t>(0x0095f5cc), 0x68, 0);  // SmallHeap_Alloc
            if (component) {
                uint32_t* w = reinterpret_cast<uint32_t*>(component);
                float* f = reinterpret_cast<float*>(component);
                w[3] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                f[8] = delay;
                f[9] = 0.0f;
                f[11] = p32;
                f[12] = p33;
                f[13] = p34;
                f[14] = p35;
                f[15] = p36;
                f[16] = p37;
                f[17] = near_range * near_range;
                f[19] = p3c_clamped;
                w[3] = *reinterpret_cast<const uint32_t*>(source + 0x1f0);
                f[20] = p38;
                w[2] = *reinterpret_cast<const uint32_t*>(source + 0x1f4);
                w[4] = flags;
                w[5] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x28) = 0;
                w[1] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                w[0] = 0x008376d0;
                f[18] = far_range * far_range;
                f[21] = p39;
                component[0x58] = channel0;
                component[0x59] = rumble;
                w[23] = rumble_name;
                w[24] = 0;
                w[25] = 0;
                FxShakeRumbleComponent_Update(component, 0.0f);
            }
            if (static_cast<int32_t>(out[0]) < 0x10) out[1 + out[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component));
            ++added;
        }
        param_set_close(&set);
    }
    return added;
}

namespace {

// Stream_FindEffect (0x00290150): hashed name in EBX; (streams, zone, 0, a flag it never reads).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindEffect_call(const uint8_t* hashed, uint32_t streams, uint32_t zone,
                                                                    uint32_t unused);
PLATFORM_ASM_ENTRY(Stream_FindEffect_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 20(%esp)\n"
                   "pushl $0\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "movl $0x00290150, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

bool script_string_set(const ScriptString* string) {
    const ScriptStringRep* rep = string->rep;
    return rep && rep->text[0];  // the original also tests rep + 6 for null
}

void copy_choice_list(uint32_t* dst, const uint32_t* src) { std::memcpy(dst, src, 0x41 * 4); }

}  // namespace

PLATFORM_ASM_ENTRY(FX_AddParticleComponents_entry,
                   "movl %esp, %eax\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FX_AddParticleComponents) "addl $20, %esp\n"
                   "ret $16")

uint32_t XB_CDECL FX_AddParticleComponents(uint32_t entry, uint32_t, const uint8_t* source, uint32_t* out, float distance) {
    const int32_t mode = *reinterpret_cast<const int32_t*>(source + 0x208);
    if (mode == 1 || mode == 2 || mode == 3) {
        const float range = title_global<float>(0x008fef64);
        if (distance > range * range) {
            out[0x14] &= ~2u;
            return 0;
        }
    }
    const ScriptString* source_name = reinterpret_cast<const ScriptString*>(source + 0x1fc);
    const int32_t cache_size = title_global<int32_t>(0x008d5a78);
    uint32_t added = 0;
    uint32_t triggers[17];
    triggers[0] = 0;
    FX_CollectTriggers(triggers, *reinterpret_cast<const uint32_t*>(source + 0x1f4), nullptr, "PARTICLE.FX");
    for (uint32_t t = 0; t < triggers[0]; ++t) {
        // Per-trigger caches: A {names[15], lists[15] +0x3c, uses u16[15] +0xf78, next +0xf98},
        // B (names without choices) {names[15], uses u16[15] +0x3c, next +0x5c}.
        uint8_t* cache_a = &title_global<uint8_t>(0x0096d970 + t * 0xf9c);
        uint8_t* cache_b = &title_global<uint8_t>(0x00971910 + t * 0x60);
        ScriptString* names_a = reinterpret_cast<ScriptString*>(cache_a);
        ScriptString* names_b = reinterpret_cast<ScriptString*>(cache_b);
        uint16_t* uses_a = reinterpret_cast<uint16_t*>(cache_a + 0xf78);
        uint16_t* uses_b = reinterpret_cast<uint16_t*>(cache_b + 0x3c);
        int32_t& next_a = *reinterpret_cast<int32_t*>(cache_a + 0xf98);
        int32_t& next_b = *reinterpret_cast<int32_t*>(cache_b + 0x5c);
        ParamSet set;
        param_set_open(&set, triggers[1 + t]);
        ParamSet_AddFxOverrides(source, &set);
        uint32_t lists[0x83];
        lists[0] = lists[0x41] = lists[0x82] = 0;
        uint32_t choices[0x41];
        const auto resolve = [&] {
            title_function<void XB_THISCALL(ParamSet*, uint32_t*)>(
                vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&set)), 1))(&set, lists);
        };
        bool have_choices = false;
        bool skip = false;
        if (title_global<uint32_t>(0x008d5110)) {
            const uint16_t hand = *reinterpret_cast<const uint16_t*>(source + 0x20e);
            const uint8_t* entity = nullptr;
            if (hand != 0xffff) entity = handle_object(*reinterpret_cast<const uint32_t*>(source + 0x1f0), 0x0097a968);
            if (hand != 0xffff && entity[3]) {
                const uint8_t* weapon = title_global<const uint8_t* const*>(0x0095f4d8)[entity[3]];  // bg_weaponDefs
                uint32_t* cached = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(weapon) + 0x2b4 + static_cast<int16_t>(hand) * 0x104);
                if (weapon && cached[0] == 0) {
                    resolve();
                    copy_choice_list(choices, lists);
                    ParamChoiceList_PickPerGroup(choices);
                    copy_choice_list(reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(weapon) + 0x2b4 +
                                                                 *reinterpret_cast<const int16_t*>(source + 0x20e) * 0x104),
                                     choices);
                } else {
                    copy_choice_list(choices, cached);
                }
                have_choices = true;
            } else if (script_string_set(source_name)) {
                for (int32_t i = 0; i < cache_size; ++i) {
                    if (ScriptString_EqualsString(&names_b[i], source_name)) {
                        ++uses_b[i];
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    for (int32_t i = 0; i < cache_size; ++i) {
                        if (ScriptString_EqualsString(&names_a[i], source_name)) {
                            ++uses_a[i];
                            copy_choice_list(choices, reinterpret_cast<uint32_t*>(cache_a + 0x3c + i * 0x104));
                            have_choices = true;
                            break;
                        }
                    }
                }
            }
        }
        using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
        const auto assign = title_function<Assign>(0x0011d600);  // ScriptString_Assign
        if (!skip && !have_choices) {
            resolve();
            if (lists[0] == 0) {
                if (title_global<uint32_t>(0x008d5110) && script_string_set(source_name)) {
                    const int32_t last = next_b ? next_b : cache_size - 1;
                    if (!ScriptString_EqualsString(&names_b[last], source_name)) {
                        assign(&names_b[next_b], source_name);
                        uses_b[next_b] = 0;
                        if (++next_b == cache_size) next_b = 0;
                    }
                }
                skip = true;
            } else {
                copy_choice_list(choices, lists);
                ParamChoiceList_PickPerGroup(choices);
                if (title_global<uint32_t>(0x008d5110) && script_string_set(source_name)) {
                    assign(&names_a[next_a], source_name);
                    copy_choice_list(reinterpret_cast<uint32_t*>(cache_a + 0x3c + next_a * 0x104), choices);
                    uses_a[next_a] = 0;
                    if (++next_a == cache_size) next_a = 0;
                }
                have_choices = true;
            }
        }
        if (have_choices) {
            for (int32_t i = 0; i < static_cast<int32_t>(choices[0]); ++i) {
                const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{choices[1 + i]});
                const uint32_t* file_value = ParamChoice_GetValue(choice, 0x11);
                char file[0x80];  // FixedStr: length in the last byte
                int32_t length = 0;
                Q_strncpyLen(file, &length, 0x7f, reinterpret_cast<const char*>(uintptr_t{file_value ? *file_value : 0}));
                file[0x7f] = static_cast<char>(length);
                uint32_t text_length = 0;
                while (file[text_length]) ++text_length;
                if (!text_length) continue;
                title_global<uint32_t>(0x009717f0) = *reinterpret_cast<const uint32_t*>(source + 0x1f4);
                title_global<uint32_t>(0x009717f4) = *reinterpret_cast<const uint32_t*>(source + 0x1f0);
                // The original keeps `file` on its stack: its address there.
                title_global<uint32_t>(0x00971800) = ((entry - 4) & ~7u) - 0x770;
                char stem[0x80];
                int32_t stem_length = 0;
                stem[0x7f] = 0;
                stem[0] = 0;
                title_function<void XB_CDECL(char*, int32_t*, const char*, uint32_t, uint32_t)>(0x00044500)(  // Path_BaseName
                    stem, &stem_length, file, static_cast<uint8_t>(file[0x7f]), 1);
                stem[0x7f] = static_cast<char>(stem_length);
                char name[0x80];
                int32_t name_length = 0;
                title_function<void XB_CDECL(char*, int32_t*, int32_t, const char*, uint32_t)>(0x00044370)(  // FixedStr_Copy
                    name, &name_length, 0x7f, stem, static_cast<uint8_t>(stem_length));
                name[0x7f] = static_cast<char>(name_length);
                uint8_t hashed[0x20];
                title_function<void XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
                title_global<uint32_t>(0x00971830) = Stream_FindEffect_call(
                    hashed, title_global<uint32_t>(0x0095f630), *reinterpret_cast<const uint32_t*>(title_global<const uint8_t*>(0x0095f638) + 0x34), 0);
                title_global<uint32_t>(0x00971804) =
                    source[0x20c] & 4 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source + 0x1c0)) : 0;
                const float delay = choice_float(choice, 0x10);
                title_global<float>(0x009717fc) = delay;
                title_global<uint32_t>(0x009717f8) = ParamChoice_GetFlags(choice);
                title_global<uint8_t>(0x0097182c) = source[0x20c] & 1;
                const uint32_t* tag_value = ParamChoice_GetValue(choice, 0x17);
                uint8_t tag[0x20];
                title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x00011060)(  // HashStr_Init
                    tag, tag_value ? *tag_value : title_global<uint32_t>(0x008d5a74));
                std::memcpy(&title_global<uint8_t>(0x00971808), tag, 0x20);
                title_global<uint32_t>(0x00971828) = *reinterpret_cast<const uint32_t*>(source + 0x204);
                title_global<uint32_t>(0x00971834) = (source[0x20c] >> 1) & 1;
                const uint8_t* attach = choice_byte(choice, 0x18);
                title_global<uint8_t>(0x00971838) = attach ? *attach : 0;
                uint8_t* component = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(
                    title_global<uint32_t>(0x0095f5cc), 0x30, 0);  // SmallHeap_Alloc
                if (!component) continue;
                uint32_t* w = reinterpret_cast<uint32_t*>(component);
                w[3] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                std::memcpy(component + 0x20, &title_global<float>(0x009717fc), 4);
                w[3] = title_global<uint32_t>(0x009717f4);
                w[2] = title_global<uint32_t>(0x009717f0);
                w[4] = title_global<uint32_t>(0x009717f8);
                w[5] = 0;
                const float zero = 0.0f;
                std::memcpy(component + 0x24, &zero, 4);
                *reinterpret_cast<uint16_t*>(component + 0x28) = 0;
                w[1] = 0;
                *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
                w[0] = 0x008375e0;
                // The file name's case-insensitive hash (rotate left 5, xor).
                uint32_t hash = 0;
                for (const char* c = file; *c; ++c) {
                    int8_t ch = *c;
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<int8_t>(ch + 0x20);
                    hash = ((hash << 5) | (hash >> 27)) ^ static_cast<uint32_t>(static_cast<int32_t>(ch));
                }
                w[1] = hash;
                w[11] = 0;
                FxParticleComponent_Update(component, 0.0f);
                if (static_cast<int32_t>(out[0]) < 0x10) out[1 + out[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component));
                ++added;
            }
        }
        param_set_close(&set);
    }
    return added;
}

namespace {

// Scr_Notify (0x002b75c0): entity in ESI, the notify on the stack (ret 4).
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_Notify_call(uint8_t* entity, uint32_t notify);
PLATFORM_ASM_ENTRY(Scr_Notify_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "pushl 12(%esp)\n"
                   "movl $0x002b75c0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

// S_PlaySoundAlias (0x002c30a0) / S_PlaySoundAlias2 (0x002c3120): alias in EAX, reference table
// in ECX, out + 9 words (ret 0x28).
using PlayAlias = uint32_t* XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t, uint32_t,
                                                 const float*, const float*, uint32_t, uint32_t, uint32_t, uint32_t);

uint8_t* sound_handle_resolve(uint32_t* handle) {
    return title_function<uint8_t* XB_REGS1 XB_CDECL(uint32_t*)>(0x00013890)(handle);  // SoundHandle_Resolve
}

}  // namespace

PLATFORM_ASM_ENTRY(FxSoundComponent_Update_entry,
                   "pushl %ebx\n"
                   "pushl 8(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(FxSoundComponent_Update) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL FxSoundComponent_Update(uint8_t* component, float delta, uint32_t ebx) {
    float* f = reinterpret_cast<float*>(component);
    uint32_t* w = reinterpret_cast<uint32_t*>(component);
    uint16_t& flags = *reinterpret_cast<uint16_t*>(component + 0x2a);
    const float age = delta + f[9];
    ++*reinterpret_cast<uint16_t*>(component + 0x28);
    f[9] = age;
    if (f[8] > age) return;
    if (!handle_object(w[3], 0x0097a968)) return;
    uint32_t* handle = &w[25];  // +0x64
    const uint32_t table = title_global<uint32_t>(0x0095f4c4);
    const auto play = title_function<PlayAlias>(0x002c30a0);   // S_PlaySoundAlias
    const auto play2 = title_function<PlayAlias>(0x002c3120);  // S_PlaySoundAlias2
    // The original pushes EBX with the flag (bit 3 of +0x2a) in its low byte over the high byte
    // of +0x2a and its caller's upper half.
    const auto flag_word = [&](uint16_t bits) { return (ebx & 0xffff0000u) | (bits & 0xff00u) | ((bits >> 3) & 1); };
    const float* zero_vector = &title_global<float>(0x0082e110);
    float pos[4];
    float zero[4];
    uint32_t out = 0;
    const auto notify_entity = [&](uint32_t notify) {
        Scr_Notify_call(FxComponent_GetEntity(component), notify);
    };
    const auto fail = [&] {
        flags |= 2;
        Entity_EndSoundNotify(FxComponent_GetEntity(component));
        if (const uint32_t notify = title_global<uint32_t>(0x0096d928)) notify_entity(notify);
    };
    // A looping sound whose voice ended restarts while the listener is within range (+0x50).
    if ((flags & 0x20) && (flags & 2) && !sound_handle_resolve(handle)) {
        const uint16_t bits = flags;
        const uint8_t* listener = *reinterpret_cast<const uint8_t* const*>(title_global<const uint8_t*>(0x0095f534) + 4);
        const float* here = reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140);
        const float* there = reinterpret_cast<const float*>(listener + 0x140);
        const float dx = here[0] - there[0];
        const float dy = here[1] - there[1];
        const float dz = here[2] - there[2];
        const float distance2 = dz * dz + dy * dy + dx * dx;
        const float range = f[20];
        if (range * range > distance2) {
            const bool placed = (bits >> 6) & 1;
            float position[4];
            const float* source = &f[12];  // +0x30
            if (!placed) {
                FxComponent_GetPosition(position, component);
                source = position;
            }
            std::memcpy(pos, source, 0x10);
            std::memcpy(zero, zero_vector, 0x10);
            out = 0;
            *handle = *play(w[26], 0, table, &out, placed ? 0 : w[3], flag_word(bits), 0, pos, zero, w[16], w[22], w[19], w[20]);
        }
    }
    if (!(flags & 2)) {
        float value = f[17];  // +0x44
        if (!(value == title_global<float>(0x0083eb34))) {
            float octaves = f[18];
            value = FX_RandomInOctaves(value, &octaves);
        }
        f[22] = value;  // +0x58
        const uint32_t alias = w[26];
        if (alias == 0xffffffffu) return fail();
        const float volume = title_function<float XB_CDECL(uint32_t, uint32_t, float)>(0x000c59b0)(alias, 0, 1.0f);  // SndVoice_GetParam
        if (volume == title_global<float>(0x0083eafc)) return fail();
        const uint16_t bits = flags;
        const float* source;
        if (bits & 0x40) {
            source = &f[12];
        } else if (FxComponent_GetEntity(component)) {
            source = reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140);
        } else {
            std::memcpy(zero, &title_global<float>(0x0082e290), 0x10);
            source = zero;
        }
        std::memcpy(pos, source, 0x10);
        std::memcpy(zero, zero_vector, 0x10);
        if (bits & 0x10) {
            *handle = *play2(alias, 0, table, &out, w[3], flag_word(bits), 0, pos, zero, w[16], w[22], w[19], w[20]);
            if (!sound_handle_resolve(handle)) return fail();
            if (*reinterpret_cast<const uint32_t*>(sound_handle_resolve(handle)) == 0xffffffffu && (flags & 8)) {
                *handle = *play2(w[26], 0, table, &out, w[3], 0, 0, pos, zero, w[16], w[22], w[19], w[20]);
                goto check_voice;
            }
        } else {
            FX_LimitSoundComponents(title_global<uint32_t*>(0x0095f5b4), w[1],
                                    title_function<uint8_t* XB_REGS1 XB_CDECL(uint32_t*)>(0x00013860)(&w[3]),  // G_EntHandleToEnt
                                    *reinterpret_cast<const uint16_t*>(component + 0x5e));
            const uint16_t bits2 = flags;
            const bool placed = (bits2 >> 6) & 1;
            const float* source2;
            if (placed) {
                source2 = &f[12];
            } else if (FxComponent_GetEntity(component)) {
                source2 = reinterpret_cast<const float*>(FxComponent_GetEntity(component) + 0x140);
            } else {
                std::memcpy(zero, &title_global<float>(0x0082e290), 0x10);
                source2 = zero;
            }
            std::memcpy(pos, source2, 0x10);
            const uint32_t entity = placed ? 0 : w[3];
            std::memcpy(zero, zero_vector, 0x10);
            *handle = *play(w[26], 0, table, &out, entity, flag_word(bits2), 0, pos, zero, w[16], w[22], w[19], w[20]);
            if (!sound_handle_resolve(handle)) return fail();
            if (*reinterpret_cast<const uint32_t*>(sound_handle_resolve(handle)) == 0xffffffffu && (flags & 8)) {
                *handle = *play(w[26], 0, table, &out, entity, 0, 0, pos, zero, w[16], w[22], w[19], w[20]);
                goto check_voice;
            }
        }
        goto keep_notify;
    check_voice:
        if (*reinterpret_cast<const uint32_t*>(sound_handle_resolve(handle)) == 0xffffffffu) {
            Entity_EndSoundNotify(FxComponent_GetEntity(component));
            const uint32_t notify = title_global<uint32_t>(0x0096d928);
            if (!notify) goto started;
            notify_entity(notify);
        }
    keep_notify:
        if (const uint32_t notify = title_global<uint32_t>(0x0096d928)) {
            *reinterpret_cast<uint32_t*>(sound_handle_resolve(handle) + 0x2c) = notify;
        }
    started:
        if (SoundRef_IsBusy(sound_handle_resolve(handle))) flags |= 0x20;
        f[20] = SoundRef_GetParam1a(sound_handle_resolve(handle));
        if (sound_handle_resolve(handle)) {
            const float scale = *reinterpret_cast<const float*>(title_global<const uint8_t*>(0x0095f4c4) + 0x6018);
            const float volume_scale = SoundRef_GetParam0(sound_handle_resolve(handle)) * scale;
            SoundRef_SetVolumeScale(sound_handle_resolve(handle), volume_scale);
        }
        flags |= 2;
    }
    // A timer (+0x54): when it runs out, vtable slot 7 with 2.0.
    {
        float timer = f[21];
        if (timer >= 0.0f) {
            timer -= delta;
            f[21] = timer;
            if (0.0f > timer) {
                f[21] = title_global<float>(0x0083eb34);
                title_function<void XB_THISCALL(uint8_t*, float)>(
                    vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component)), 7))(component, 2.0f);
            }
        }
    }
    if (!sound_handle_resolve(handle)) return;
    if (SoundRef_IsDone(sound_handle_resolve(handle))) return;
    if (SoundRef_IsState3(sound_handle_resolve(handle))) {
        Entity_EndSoundNotify(FxComponent_GetEntity(component));
        if (*reinterpret_cast<const uint32_t*>(sound_handle_resolve(handle) + 0x2c)) {
            notify_entity(*reinterpret_cast<const uint32_t*>(sound_handle_resolve(handle) + 0x2c));
        }
    }
    // A fade (flag 1): +0x1c counts down over +0x18, scaling the volume +0x40.
    if (flags & 1) {
        const float left = f[7] - delta;
        f[7] = left;
        if (sound_handle_resolve(handle)) {
            float fraction = left / f[6];
            if (0.0f > fraction) {
                fraction = 0.0f;
            } else if (fraction > title_global<float>(0x0083eb20)) {
                fraction = title_global<float>(0x0083eb20);
            }
            SoundRef_SetVolumeScale(sound_handle_resolve(handle), fraction * f[16]);
        }
    }
    if (const uint8_t* placement = reinterpret_cast<const uint8_t*>(uintptr_t{w[5]})) {
        if (sound_handle_resolve(handle)) SoundRef_SetSource(placement, 0, sound_handle_resolve(handle));
    }
}

namespace {

// FxSoundComponent_Create with the sound name passed in (FX_AddSoundComponents' own buffer;
// 0x0096d8f8 then holds the original's stack address of it).
uint8_t* sound_component_create(uint8_t* component, uint32_t name) {
    uint32_t* w = reinterpret_cast<uint32_t*>(component);
    float* f = reinterpret_cast<float*>(component);
    w[3] = 0;
    *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
    f[8] = title_global<float>(0x0096d8f4);  // delay
    w[2] = title_global<uint32_t>(0x0096d8e8);
    w[3] = title_global<uint32_t>(0x0096d8ec);
    w[4] = title_global<uint32_t>(0x0096d8f0);
    w[5] = 0;
    f[9] = 0.0f;
    *reinterpret_cast<uint16_t*>(component + 0x28) = 0;
    w[1] = 0;
    *reinterpret_cast<uint16_t*>(component + 0x2a) = 0;
    w[0] = 0x00837674;
    w[25] = 0;
    f[16] = title_global<float>(0x0096d904);
    f[17] = title_global<float>(0x0096d908);
    f[18] = title_global<float>(0x0096d90c);
    f[19] = title_global<float>(0x0096d910);
    f[20] = title_global<float>(0x0096d914);
    f[21] = title_global<float>(0x0096d900);
    *reinterpret_cast<uint16_t*>(component + 0x5c) = 0;
    w[24] = title_global<uint32_t>(0x0096d92c);
    if (const uint8_t* placement = title_global<const uint8_t*>(0x0096d8fc)) {
        component[0x2a] |= 0x40;
        std::memcpy(component + 0x30, placement, 0x10);
    }
    if (title_global<uint32_t>(0x0096d91c)) component[0x2a] |= 0x10;
    if (title_global<uint8_t>(0x0096d920)) component[0x2a] |= 8;
    component[0x2a] |= 4;
    *reinterpret_cast<uint16_t*>(component + 0x5e) = static_cast<uint16_t>(title_global<uint32_t>(0x0096d918));
    w[1] = title_global<uint32_t>(0x0096d924);
    w[26] = title_function<uint32_t XB_CDECL(uint32_t)>(0x000c5810)(name);  // S_FindSoundByHash
    uint8_t* entity = handle_object(title_global<uint32_t>(0x0096d8ec), 0x0097a968);
    uint32_t* sound_ids = reinterpret_cast<uint32_t*>(entity + 0x3c4);
    if (sound_ids[0] && !sound_ids[1]) sound_ids[1] = w[1];
    FxSoundComponent_Update(component, 0.0f, 0);  // the original's EBX is 0 here
    return component;
}

}  // namespace

PLATFORM_ASM_ENTRY(FxSoundComponent_Create_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FxSoundComponent_Create) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL FxSoundComponent_Create(uint8_t* component) {
    return sound_component_create(component, title_global<uint32_t>(0x0096d8f8));
}

PLATFORM_ASM_ENTRY(FX_AddSoundComponents_entry,
                   "movl %esp, %eax\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FX_AddSoundComponents) "addl $20, %esp\n"
                   "ret $16")

uint32_t XB_CDECL FX_AddSoundComponents(uint32_t entry, uint32_t, uint8_t* source, uint32_t* out, float distance) {
    const int32_t mode = *reinterpret_cast<const int32_t*>(source + 0x208);
    if (mode == 0 || mode == 1 || mode == 2 || mode == 3) {
        const float range = title_global<float>(0x008fef68);
        if (distance > range * range) {
            out[0x14] &= ~2u;
            return 0;
        }
    }
    const int32_t busy = Snd_CountBusyVoices();
    const int32_t mode2 = *reinterpret_cast<const int32_t*>(source + 0x208);
    if ((mode2 == 1 || mode2 == 2 || mode2 == 3) && busy > 0xf) {
        out[0x14] &= ~2u;
        return 0;
    }
    using Alloc = uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t);
    const auto alloc = title_function<Alloc>(0x00044850);  // SmallHeap_Alloc
    const auto append = [out](uint8_t* component) {
        if (static_cast<int32_t>(out[0]) < 0x10) out[1 + out[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component));
    };
    // An alias of the source's own sound name: one component, no triggers.
    const uint32_t flags0 = *reinterpret_cast<const uint32_t*>(source);
    if ((flags0 & 1) && *reinterpret_cast<const int32_t*>(source + 0x1b4) == 3 && ((flags0 >> 6) & 1) &&
        !title_global<uint8_t>(0x0095f5b0)) {
        const uint32_t key = title_function<uint32_t XB_REGS1 XB_CDECL(const uint8_t*)>(0x0001d830)(source + 0x134);  // HashStr_Hash
        const uint32_t alias = ZoneAliasList_NextVariant(title_global<const uint32_t*>(0x0095f5ac), key);
        if (alias) {
            title_global<float>(0x0096d904) = title_global<float>(0x0083eb34);  // -1
            title_global<uint32_t>(0x0096d8e8) = *reinterpret_cast<const uint32_t*>(source + 0x1f4);
            title_global<uint32_t>(0x0096d8ec) = *reinterpret_cast<const uint32_t*>(source + 0x1f0);
            title_global<uint32_t>(0x0096d8f8) = alias;
            title_global<uint32_t>(0x0096d91c) = (source[0x20c] >> 1) & 1;
            title_global<uint32_t>(0x0096d924) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{alias}));
            title_global<uint32_t>(0x0096d8fc) =
                source[0x20c] & 4 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source + 0x1c0)) : 0;
            const float minus_one = title_global<float>(0x0083eb34);
            title_global<uint32_t>(0x0096d918) = 2;
            title_global<float>(0x0096d908) = minus_one;
            title_global<float>(0x0096d90c) = 0.0f;
            title_global<float>(0x0096d910) = minus_one;
            title_global<float>(0x0096d914) = minus_one;
            title_global<float>(0x0096d8f4) = 0.0f;
            title_global<float>(0x0096d900) = minus_one;
            title_global<uint32_t>(0x0096d8f0) = 0;
            title_global<uint8_t>(0x0096d920) = (source[0x20c] >> 3) & 1;
            title_global<uint32_t>(0x0096d928) = *reinterpret_cast<const uint32_t*>(source + 0x200);
            if (*reinterpret_cast<const int32_t*>(source + 0x208) == 7) title_global<uint8_t>(0x0096d920) = 1;
            uint8_t* component = alloc(title_global<uint32_t>(0x0095f5cc), 0x70, 0);
            if (!component) return 0;
            component = sound_component_create(component, alias);
            if (!component) return 0;
            append(component);
            return 1;
        }
    }
    const ScriptString* source_name = reinterpret_cast<const ScriptString*>(source + 0x1fc);
    const int32_t cache_size = title_global<int32_t>(0x008d5a78);
    uint32_t added = 0;
    uint32_t triggers[17];
    triggers[0] = 0;
    FX_CollectTriggers(triggers, *reinterpret_cast<const uint32_t*>(source + 0x1f4), nullptr, "SOUND.FX");
    bool done = false;  // only the first trigger with choices makes components
    for (uint32_t t = 0; t < triggers[0]; ++t) {
        if (done) continue;
        // Per-trigger caches as in FX_AddParticleComponents: A at 0x0096f8a8, B at 0x00971840.
        uint8_t* cache_a = &title_global<uint8_t>(0x0096f8a8 + t * 0xf9c);
        uint8_t* cache_b = &title_global<uint8_t>(0x00971840 + t * 0x60);
        ScriptString* names_a = reinterpret_cast<ScriptString*>(cache_a);
        ScriptString* names_b = reinterpret_cast<ScriptString*>(cache_b);
        uint16_t* uses_a = reinterpret_cast<uint16_t*>(cache_a + 0xf78);
        uint16_t* uses_b = reinterpret_cast<uint16_t*>(cache_b + 0x3c);
        int32_t& next_a = *reinterpret_cast<int32_t*>(cache_a + 0xf98);
        int32_t& next_b = *reinterpret_cast<int32_t*>(cache_b + 0x5c);
        ParamSet set;
        param_set_open(&set, triggers[1 + t]);
        ParamSet_AddFxOverrides(source, &set);
        uint32_t lists[0x83];
        lists[0] = lists[0x41] = lists[0x82] = 0;
        uint32_t choices[0x41];
        const auto resolve = [&] {
            title_function<void XB_THISCALL(ParamSet*, uint32_t*)>(
                vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&set)), 1))(&set, lists);
        };
        bool have_choices = false;
        bool skip = false;
        if (title_global<uint32_t>(0x008d5110)) {
            const uint16_t hand = *reinterpret_cast<const uint16_t*>(source + 0x20e);
            const uint8_t* entity = nullptr;
            if (hand != 0xffff) entity = handle_object(*reinterpret_cast<const uint32_t*>(source + 0x1f0), 0x0097a968);
            if (hand != 0xffff && entity[3]) {
                const uint8_t* weapon = title_global<const uint8_t* const*>(0x0095f4d8)[entity[3]];  // bg_weaponDefs
                uint32_t* cached = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(weapon) + 0xac + static_cast<int16_t>(hand) * 0x104);
                if (weapon && cached[0] == 0) {
                    resolve();
                    copy_choice_list(choices, lists);
                    ParamChoiceList_PickPerGroup(choices);
                    copy_choice_list(reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(weapon) + 0xac +
                                                                 *reinterpret_cast<const int16_t*>(source + 0x20e) * 0x104),
                                     choices);
                } else {
                    copy_choice_list(choices, cached);
                }
                have_choices = true;
            } else if (script_string_set(source_name)) {
                for (int32_t i = 0; i < cache_size; ++i) {
                    if (ScriptString_EqualsString(&names_b[i], source_name)) {
                        ++uses_b[i];
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    for (int32_t i = 0; i < cache_size; ++i) {
                        if (ScriptString_EqualsString(&names_a[i], source_name)) {
                            ++uses_a[i];
                            copy_choice_list(choices, reinterpret_cast<uint32_t*>(cache_a + 0x3c + i * 0x104));
                            have_choices = true;
                            break;
                        }
                    }
                }
            }
        }
        using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
        const auto assign = title_function<Assign>(0x0011d600);  // ScriptString_Assign
        if (!skip && !have_choices) {
            resolve();
            if (lists[0] == 0) {
                if (title_global<uint32_t>(0x008d5110) && script_string_set(source_name)) {
                    const int32_t last = next_b ? next_b : cache_size - 1;
                    if (ScriptString_EqualsString(&names_b[last], source_name)) {
                        ParamSet_Destruct(&set);
                        continue;
                    }
                    assign(&names_b[next_b], source_name);
                    uses_b[next_b] = 0;
                    if (++next_b == cache_size) next_b = 0;
                }
                skip = true;
            } else {
                copy_choice_list(choices, lists);
                const uint32_t count = choices[0];
                ParamChoiceList_PickPerGroup(choices);
                // Only single-choice lists are cached here.
                if (title_global<uint32_t>(0x008d5110) && count == 1 && script_string_set(source_name)) {
                    assign(&names_a[next_a], source_name);
                    copy_choice_list(reinterpret_cast<uint32_t*>(cache_a + 0x3c + next_a * 0x104), choices);
                    uses_a[next_a] = 0;
                    if (++next_a == cache_size) next_a = 0;
                }
                have_choices = true;
            }
        }
        if (have_choices) {
            for (uint32_t i = 0; i < choices[0]; ++i) {
                done = true;
                const uint8_t* choice = reinterpret_cast<const uint8_t*>(uintptr_t{choices[1 + i]});
                const uint32_t* file_value = ParamChoice_GetValue(choice, 0x19);
                char file[0x40];  // FixedStr: length in the last byte
                int32_t length = 0;
                Q_strncpyLen(file, &length, 0x3f, reinterpret_cast<const char*>(uintptr_t{file_value ? *file_value : 0}));
                file[0x3f] = static_cast<char>(length);
                if (!file[0]) continue;
                title_global<float>(0x0096d904) = choice_float_or(choice, 0x1c, title_global<float>(0x0083eb34));
                title_global<uint32_t>(0x0096d8e8) = *reinterpret_cast<const uint32_t*>(source + 0x1f4);
                title_global<uint32_t>(0x0096d8ec) = *reinterpret_cast<const uint32_t*>(source + 0x1f0);
                title_global<uint32_t>(0x0096d91c) = (source[0x20c] >> 1) & 1;
                using BaseName = void XB_CDECL(char*, int32_t*, const char*, uint32_t, uint32_t);
                using Copy = void XB_CDECL(char*, int32_t*, int32_t, const char*, uint32_t);
                const auto base_name = title_function<BaseName>(0x00044500);  // Path_BaseName
                const auto fixed_copy = title_function<Copy>(0x00044370);     // FixedStr_Copy
                // The sound name (its stack address goes to 0x0096d8f8) and, the same again,
                // the name hashed into 0x0096d924.
                char stem[0x40];
                int32_t stem_length = 0;
                stem[0x3f] = 0;
                stem[0] = 0;
                base_name(stem, &stem_length, file, static_cast<uint8_t>(length), 1);
                stem[0x3f] = static_cast<char>(stem_length);
                char name[0x40];
                int32_t name_length = 0;
                fixed_copy(name, &name_length, 0x3f, stem, static_cast<uint8_t>(stem_length));
                name[0x3f] = static_cast<char>(name_length);
                title_global<uint32_t>(0x0096d8f8) = ((entry - 4) & ~7u) - 0x6b0;
                char stem2[0x40];
                int32_t stem2_length = 0;
                stem2[0x3f] = 0;
                stem2[0] = 0;
                base_name(stem2, &stem2_length, file, static_cast<uint8_t>(file[0x3f]), 1);
                stem2[0x3f] = static_cast<char>(stem2_length);
                char name2[0x40];
                int32_t name2_length = 0;
                fixed_copy(name2, &name2_length, 0x3f, stem2, static_cast<uint8_t>(stem2_length));
                name2[0x3f] = static_cast<char>(name2_length);
                uint32_t hash = 0;
                for (const char* c = name2; *c; ++c) {
                    int8_t ch = *c;
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<int8_t>(ch + 0x20);
                    hash = ((hash << 5) | (hash >> 27)) ^ static_cast<uint32_t>(static_cast<int32_t>(ch));
                }
                title_global<uint32_t>(0x0096d924) = hash;
                title_global<uint32_t>(0x0096d8fc) =
                    source[0x20c] & 4 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source + 0x1c0)) : 0;
                const uint32_t* channel = ParamChoice_GetValue(choice, 0x26);
                title_global<uint32_t>(0x0096d918) = channel ? *channel : 2;
                const float minus_one = title_global<float>(0x0083eb34);
                title_global<float>(0x0096d908) = choice_float_or(choice, 0x1a, minus_one);
                title_global<float>(0x0096d90c) = choice_float_or(choice, 0x1b, 0.0f);
                title_global<float>(0x0096d910) = choice_float_or(choice, 0x1d, minus_one);
                title_global<float>(0x0096d914) = choice_float_or(choice, 0x1e, minus_one);
                title_global<float>(0x0096d8f4) = choice_float_or(choice, 0x10, 0.0f);
                title_global<float>(0x0096d900) = choice_float_or(choice, 0x23, minus_one);
                title_global<uint32_t>(0x0096d8f0) = ParamChoice_GetFlags(choice);
                title_global<uint8_t>(0x0096d920) = (source[0x20c] >> 3) & 1;
                title_global<uint32_t>(0x0096d928) = *reinterpret_cast<const uint32_t*>(source + 0x200);
                const uint32_t* text_value = ParamChoice_GetValue(choice, 0x27);
                const uint32_t text = text_value ? *text_value : title_global<uint32_t>(0x008d5a74);
                title_global<uint32_t>(0x0096d92c) = *reinterpret_cast<const char*>(uintptr_t{text}) ? text : 0;
                const int32_t mode3 = *reinterpret_cast<const int32_t*>(source + 0x208);
                if (mode3 == 4 || mode3 == 6 || mode3 == 7) title_global<uint8_t>(0x0096d920) = 1;
                uint8_t* component = alloc(title_global<uint32_t>(0x0095f5cc), 0x70, 0);
                if (!component) continue;
                component = sound_component_create(component, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
                if (!component) continue;
                append(component);
                ++added;
            }
        }
        param_set_close(&set);
    }
    source[0x20c] &= ~2;
    return added;
}

uint32_t XB_STDCALL FX_LimitSoundComponents(uint32_t* lists, uint32_t hash, const uint8_t* entity, int32_t limit) {
    int32_t count = 0;
    bool full = false;
    uint8_t* oldest = nullptr;
    int32_t oldest_age = 0;
    uint32_t oldest_list = 0;
    uint32_t oldest_index = 0;
    for (uint32_t l = 0; l < lists[0]; ++l) {
        const uint32_t* list = reinterpret_cast<const uint32_t*>(uintptr_t{lists[1 + l]});
        for (uint32_t i = 0; i < list[0]; ++i) {
            uint8_t* component = reinterpret_cast<uint8_t*>(uintptr_t{list[1 + i]});
            if (!component || !(component[0x2a] & 4)) continue;
            if (handle_object(*reinterpret_cast<const uint32_t*>(component + 0xc), 0x0097a968) != entity) continue;
            if (*reinterpret_cast<const uint32_t*>(component + 4) != hash) continue;
            if (++count >= limit) full = true;
            const int32_t age = *reinterpret_cast<const int16_t*>(component + 0x28);
            if (oldest && age <= oldest_age) continue;
            oldest = component;
            oldest_age = age;
            oldest_list = l;
            oldest_index = i;
        }
    }
    if (full) {
        title_function<void XB_THISCALL(uint8_t*)>(vtable_slot(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(oldest)), 4))(oldest);
        uint32_t* list = reinterpret_cast<uint32_t*>(uintptr_t{lists[1 + oldest_list]});
        if (const uint32_t victim = list[1 + oldest_index]) {
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(victim, 0))(victim, 1);  // deleting destructor
        }
        list = reinterpret_cast<uint32_t*>(uintptr_t{lists[1 + oldest_list]});
        list[1 + oldest_index] = list[list[0]];
        if (list[0]) --list[0];
    }
    return static_cast<uint32_t>(count);
}

void XB_REGS1 XB_CDECL Entity_EndSoundNotify(uint8_t* entity) {
    if (!entity) return;
    uint32_t* notify = reinterpret_cast<uint32_t*>(entity + 0x3c4);
    if (!notify[0]) return;
    Scr_Notify_call(entity, notify[0]);
    notify[0] = 0;
    notify[1] = 0;
}

}  // namespace game
