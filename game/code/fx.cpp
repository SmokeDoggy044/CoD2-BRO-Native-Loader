#include "fx.h"

#include "ngl.h"
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

}  // namespace game
