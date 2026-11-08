#include "levels_templates.h"

#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
uint8_t* ptr(uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); }
uint32_t u32(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The engine's script heap (every copy calls the same two).
uint8_t* scr_alloc(uint32_t size, uint32_t flags, uint32_t alloc) {
    return title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(alloc)(size, flags);
}
void scr_free(uint32_t block, uint32_t free) { title_function<void XB_CDECL(uint32_t)>(free)(block); }
void thiscall0(uint32_t function, uint8_t* self) { title_function<void XB_THISCALL(uint8_t*)>(function)(self); }

// --- Static hash-map singletons: {map pointer global, shared use counter global, ctor, dtor} ---

struct StaticMap {
    uint32_t slot;     // the map pointer
    uint32_t counter;  // shared by the level's singletons: +1 per creation, -1 per release
    uint32_t ctor;     // HashMap constructor (thiscall: traits, unused)
    uint32_t dtor;     // HashMap destructor (thiscall)
};

// Creates the map: Scr_Alloc(0x28, 4), the constructor with `traits` (an address in the caller's
// frame: its byte is the map's traits byte). Stores it (0 when the allocation failed) and counts it.
uint32_t static_map_create(const StaticMap& m, const uint32_t* traits, uint32_t alloc) {
    uint8_t* const map = scr_alloc(0x28, 4, alloc);
    if (map)
        title_function<void XB_THISCALL(uint8_t*, const uint32_t*, const uint32_t*)>(m.ctor)(map, traits, traits);
    title_global<uint32_t>(m.slot) = u32(map);
    ++title_global<int32_t>(m.counter);
    return u32(map);
}

uint32_t static_map_get_or_release(uint8_t release, const StaticMap& m, uint32_t alloc, uint32_t free) {
    const uint32_t map = title_global<uint32_t>(m.slot);
    if (!map) {
        if (release) return 0;
        // The original keeps the new pointer in its argument slot and hands that slot's address over as
        // the traits: the traits byte is the pointer's low byte.
        uint8_t* const block = scr_alloc(0x28, 4, alloc);
        const uint32_t self = u32(block);
        if (block)
            title_function<void XB_THISCALL(uint8_t*, const uint32_t*, const uint32_t*)>(m.ctor)(block, &self, &self);
        title_global<uint32_t>(m.slot) = self;
        ++title_global<int32_t>(m.counter);
        return self;
    }
    if (!release) return map;
    thiscall0(m.dtor, ptr(map));
    scr_free(map, free);
    title_global<uint32_t>(m.slot) = 0;
    --title_global<int32_t>(m.counter);
    return 0;
}

// The constructor's traits point at a never-written local byte (zero in tests).
void static_map_set_string(uint32_t a, uint32_t b, uint32_t* text, const StaticMap& m, uint32_t set,
                           uint32_t alloc, uint32_t copy, uint32_t release) {
    const uint32_t traits = 0;
    uint32_t map = title_global<uint32_t>(m.slot);
    if (!map) map = static_map_create(m, &traits, alloc);
    uint32_t value;
    title_function<void XB_THISCALL(uint32_t*, const uint32_t*)>(copy)(&value, text);  // ScriptString_Copy
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(set)(map, a, b, value);
    title_function<void XB_THISCALL(uint32_t*)>(release)(text);  // ScriptString_Release (by-value argument)
}

// The constructor's traits point at the first argument slot (the out pointer's low byte).
uint32_t static_map_get_string(uint32_t* out, uint32_t a, uint32_t b, const StaticMap& m, uint32_t get,
                               uint32_t alloc) {
    uint32_t map = title_global<uint32_t>(m.slot);
    if (!map) map = static_map_create(m, out, alloc);
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(get)(map, *out, a, b);
    return *out;
}

// --- Lists {?, head, size} and vectors {?, first, last, end} on the script heap ---

// list::clear: the head relinked to itself, size 0, then every node destroyed (`destroy`, thiscall) and freed.
void list_clear_destroy(uint8_t* list, uint32_t destroy, uint32_t free) {
    uint8_t* node = ptr(at<uint32_t>(ptr(at<uint32_t>(list, 4)), 0));
    at<uint32_t>(ptr(at<uint32_t>(list, 4)), 0) = at<uint32_t>(list, 4);
    at<uint32_t>(ptr(at<uint32_t>(list, 4)), 4) = at<uint32_t>(list, 4);
    at<uint32_t>(list, 8) = 0;
    if (u32(node) == at<uint32_t>(list, 4)) return;
    for (;;) {
        uint8_t* const next = ptr(at<uint32_t>(node, 0));
        thiscall0(destroy, node);
        scr_free(u32(node), free);
        node = next;
        if (u32(next) == at<uint32_t>(list, 4)) return;
    }
}

void list_dtor(uint8_t* list, uint32_t clear, uint32_t free) {
    thiscall0(clear, list);
    scr_free(at<uint32_t>(list, 4), free);
    at<uint32_t>(list, 4) = 0;
}

// HashMap {traits, list at +4, vector at +0x10, ...}: the bucket vector freed, the list cleared, its head freed.
void hash_map_dtor(uint8_t* map, uint32_t list_clear, uint32_t free) {
    if (at<uint32_t>(map, 0x14)) scr_free(at<uint32_t>(map, 0x14), free);
    at<uint32_t>(map, 0x14) = 0;
    at<uint32_t>(map, 0x18) = 0;
    at<uint32_t>(map, 0x1c) = 0;
    uint8_t* const list = map + 4;
    thiscall0(list_clear, list);
    scr_free(at<uint32_t>(list, 4), free);
    at<uint32_t>(list, 4) = 0;
}

// --- Dynamic arrays {data, capacity, size} ---

uint8_t* dyn_array_new_copy(const uint8_t* source, uint32_t assign, uint32_t alloc) {
    uint8_t* const array = scr_alloc(0xc, 4, alloc);
    if (!array) return nullptr;
    at<uint32_t>(array, 0) = 0;
    at<uint32_t>(array, 4) = 0;
    at<uint32_t>(array, 8) = 0;
    title_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(assign)(array, source);
    return array;
}

uint32_t* dyn_array_at(uint8_t* array, uint32_t i, uint32_t resize) {
    if (i >= at<uint32_t>(array, 8))
        title_function<void XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(resize)(array, i + 1, i + 4);
    return reinterpret_cast<uint32_t*>(ptr(at<uint32_t>(array, 0))) + i;
}

}  // namespace

uint32_t XB_CDECL StaticHashMap_GetOrRelease_PROLOGUE(uint8_t release) {
    return static_map_get_or_release(release, StaticMap{0x00921cd4, 0x00921ccc, 0x00461fe0, 0x00461db0},
                                     0x002fb3d0, 0x002f5e10);  // Scr_Alloc, Scr_Free
}

void XB_CDECL StaticHashMap_SetString_PROLOGUE(uint32_t a, uint32_t b, uint32_t text) {
    static_map_set_string(a, b, &text, StaticMap{0x00921cd4, 0x00921ccc, 0x00461fe0, 0}, 0x00464a90, 0x002fb3d0,
                          0x0011d5e0, 0x0011d640);  // HashMap_SetString_PROLOGUE, Scr_Alloc, ScriptString_Copy/Release
}

uint32_t XB_CDECL StaticHashMap_GetString_PROLOGUE(uint32_t out, uint32_t a, uint32_t b) {
    return static_map_get_string(&out, a, b, StaticMap{0x00921cd4, 0x00921ccc, 0x00461fe0, 0}, 0x0045e7b0,
                                 0x002fb3d0);  // HashMap_GetStringOrEmpty_PROLOGUE, Scr_Alloc
}

void XB_THISCALL HashMap_Dtor_PROLOGUE(uint8_t* map) {
    hash_map_dtor(map, 0x0045e540, 0x002f5e10);  // std_list_ClearDestroy_PROLOGUE, Scr_Free
}

void XB_THISCALL std_list_ClearDestroy_PROLOGUE(uint8_t* list) {
    list_clear_destroy(list, 0x0045a370, 0x002f5e10);  // ListNode_DestroyValue_PROLOGUE, Scr_Free
}

void XB_THISCALL std_list_Dtor_PROLOGUE(uint8_t* list) {
    list_dtor(list, 0x0045ca00, 0x002f5e10);  // std_list_Clear_2_PROLOGUE, Scr_Free
}

uint32_t XB_THISCALL ListNode_DestroyValue_PROLOGUE(uint8_t* node) {
    return title_function<uint32_t XB_THISCALL(uint8_t*)>(0x0011d640)(node + 0xc);  // ScriptString_Release
}

uint8_t* XB_CDECL DynArray_NewCopy_PROLOGUE(const uint8_t* source) {
    return dyn_array_new_copy(source, 0x0045c270, 0x002fb3d0);  // DynArray_Assign4_PROLOGUE, Scr_Alloc
}

uint32_t* XB_THISCALL DynArray_At_PROLOGUE(uint8_t* array, uint32_t i) {
    return dyn_array_at(array, i, 0x00015a90);  // DynArray_Resize
}

#include "levels_templates_levels.inc"

}  // namespace game
