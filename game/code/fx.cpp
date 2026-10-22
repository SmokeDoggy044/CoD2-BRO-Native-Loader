#include "fx.h"

#include <cstring>

#include "lanes.h"
#include "ngl.h"
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

void XB_CDECL FX_PlayEffectAt(uint32_t effect, const float* origin) {
    alignas(16) float matrix[16];
    title_function<void XB_CDECL(float*)>(0x0001c140)(matrix);  // Matrix_Identity
    // Row 3 = the origin, with x again in its w lane.
    matrix[12] = origin[0];
    matrix[13] = origin[1];
    matrix[14] = origin[2];
    matrix[15] = origin[0];
    title_function<uint32_t XB_CDECL(uint32_t, float*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0023c040)(
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

}  // namespace game
