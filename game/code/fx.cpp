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

}  // namespace game
