#include "bro_scr_array.h"

#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_field.h"
#include "title_data.h"
#include "levels_twins.h"
#include "qcommon_fs.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* array_bytes(uint32_t* array) { return reinterpret_cast<uint8_t*>(array); }
uint32_t* data_of(const uint32_t* array) { return pointer<uint32_t>(array[0]); }

void free_data(const uint32_t* array) {
    if (array[0]) Scr_Free(data_of(array));
}

bool valid(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(ent) != 0; }  // Scr_EntityHandleIsValid
int32_t health(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent); }      // ScrEnt_GetHealth

// The entity's array field (ScriptKeyTable_GetArray on its script object or the empty table).
uint32_t* field_array(uint32_t ent, uint32_t key) {
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return own_function<GetArray>(&ScriptKeyTable_GetArray)(BroScr_ScriptObjectOrEmpty(ent), key);
}

// A by-value copy of an array (DynArray_Assign4 into a zeroed block).
void copy_array(uint32_t* copy, uint32_t* source) {
    copy[0] = copy[1] = copy[2] = 0;
    own_function<void XB_THISCALL(uint32_t*, uint32_t*)>(&DynArray_Assign4_PROLOGUE)(copy, source);  // DynArray_Assign4_PROLOGUE
}

// GScr_ActorSetFlag2000000 on each element.
void actors_set_flag(uint32_t* array, uint32_t value) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array[2]); ++i)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922790)(*BroScr_ArrayElement(array, i), value);
    free_data(array);
}

}  // namespace

uint32_t* BroScr_ArrayElement(uint32_t* array, uint32_t i) {
    if (i >= array[2]) DynArray_Resize(array_bytes(array), i + 1, i + 4);
    return &data_of(array)[i];
}

void BroScr_ArrayEraseAt(uint32_t* array, uint32_t index) {
    const uint32_t count = array[2];
    if (index >= count) return;
    uint32_t* p = data_of(array);
    uint32_t* const end = p + count;
    if (p == end) return;
    for (; index; --index)
        if (++p == end) return;
    title_function<void XB_THISCALL(uint32_t*, uint32_t*)>(0x004afb70)(array, p);  // DynArray_Erase_ORAN
}

BRO_SCR_ARGS_ENTRY(BroScr_RandomArrayElement)
uint32_t XB_CDECL BroScr_RandomArrayElement(uint32_t* args) {
    uint32_t* const out = pointer<uint32_t>(args[0]);
    uint32_t* const array = &args[1];
    const uint32_t index = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(array[2]);  // Scr_RandomInt
    if (index >= array[2])
        own_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(&DynArray_Resize4)(array, index + 1, index + 4);  // DynArray_Resize4
    *out = data_of(array)[index];
    free_data(array);
    return args[0];
}

BRO_SCR_ARGS_ENTRY(BroScr_ActorArray_ClearFlag2000000)
uint32_t XB_CDECL BroScr_ActorArray_ClearFlag2000000(uint32_t* args) {
    actors_set_flag(args, 0);
    return 0;
}
BRO_SCR_ARGS_ENTRY(BroScr_ActorArray_SetFlag2000000)
uint32_t XB_CDECL BroScr_ActorArray_SetFlag2000000(uint32_t* args) {
    actors_set_flag(args, 1);
    return 0;
}

void XB_CDECL BroScr_EntArrayRemove_he104fd5c(uint32_t ent, uint32_t value) {
    uint32_t i = 0;
    for (;; ++i) {
        if (static_cast<int32_t>(i) >= static_cast<int32_t>(field_array(ent, 0xe104fd5c)[2])) return;
        if (*BroScr_ArrayElement(field_array(ent, 0xe104fd5c), i) == value) break;
    }
    BroScr_ArrayEraseAt(field_array(ent, 0xe104fd5c), i);
}

void XB_CDECL BroScr_NotifyRiders(uint32_t ent) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(field_array(ent, 0xe2a39a33)[2]); ++i) {
        const uint32_t rider = *BroScr_ArrayElement(field_array(ent, 0xe2a39a33), i);
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(rider, 0x78dbe561);  // Scr_QueueNotify
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*BroScr_ArrayElement(field_array(ent, 0xe2a39a33), i));  // GScr_Delete
    }
}

void XB_CDECL BroScr_ArrayAddUnique(uint32_t* array, uint32_t value) {
    if (array[1] != 0 && static_cast<int32_t>(array[2]) > 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array[2]); ++i)
            if (*BroScr_ArrayElement(array, i) == value) return;
    }
    uint32_t index = array[2];
    if (index == 1 && !valid(data_of(array)[0])) index = 0;
    *BroScr_ArrayElement(array, index) = value;
}

void XB_CDECL BroScr_QueueAdd_h4ced49f6(uint32_t owner, uint32_t ent) {
    valid(ent);
    if (health(ent) > 0) BroScr_ArrayAddUnique(field_array(owner, 0x4ced49f6), ent);
}

void XB_CDECL BroScr_QueueRemove_h4ced49f6(uint32_t owner, uint32_t ent) {
    valid(ent);
    if (health(ent) <= 0) return;
    uint32_t args[5];
    copy_array(&args[2], field_array(owner, 0x4ced49f6));
    args[1] = ent;
    args[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(field_array(owner, 0x4ced49f6)));
    BroScr_ArrayRemoveValue(args);
}

uint8_t* XB_CDECL BroScr_RotateQueue_h4ced49f6(uint8_t* out, uint32_t ent) {
    bool defined;
    ScrField_h4ced49f6_IsDefined(&defined, ent);
    if (!defined) {
        *out = 0;
        return out;
    }
    const uint8_t* level = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0));
    if (valid(*reinterpret_cast<const uint32_t*>(level + 0x6ec))) {
        const uint32_t first = *BroScr_ArrayElement(field_array(ent, 0x4ced49f6), 0);
        level = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0));
        if (first == *reinterpret_cast<const uint32_t*>(level + 0x6ec)) {
            uint32_t args[5];
            copy_array(&args[2], field_array(ent, 0x4ced49f6));
            args[1] = *BroScr_ArrayElement(field_array(ent, 0x4ced49f6), 0);
            args[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(field_array(ent, 0x4ced49f6)));
            BroScr_ArrayRotateFirstToBack(args);
        }
    }
    *out = 1;
    return out;
}

BRO_SCR_ARGS_ENTRY(BroScr_ArrayRemoveValue)
uint32_t XB_CDECL BroScr_ArrayRemoveValue(uint32_t* args) {
    uint32_t* const list = pointer<uint32_t>(args[0]);
    uint32_t* const result = &args[2];
    *BroScr_ArrayElement(result, 0) = args[1];
    if (list[1] != 0 && static_cast<int32_t>(list[2]) > 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list[2]); ++i) {
            if (args[1] == *BroScr_ArrayElement(list, i)) continue;
            const uint32_t* const element = BroScr_ArrayElement(list, i);
            const uint32_t n = result[2];
            DynArray_Resize(reinterpret_cast<uint8_t*>(result), n + 1, n + 4);
            data_of(result)[n] = *element;
        }
    }
    free_data(result);
    return 0;
}

BRO_SCR_ARGS_ENTRY(BroScr_ArrayRotateFirstToBack)
uint32_t XB_CDECL BroScr_ArrayRotateFirstToBack(uint32_t* args) {
    uint32_t* const list = pointer<uint32_t>(args[0]);
    uint32_t* const result = &args[2];
    if (static_cast<int32_t>(list[2]) > 1) {
        uint32_t out = 0;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list[2]); ++i) {
            if (i == 0) continue;
            const uint32_t* const element = BroScr_ArrayElement(list, i);
            uint32_t* const dst = BroScr_ArrayElement(result, out);
            *dst = *element;
            ++out;
        }
        const uint32_t* const first = BroScr_ArrayElement(list, 0);
        *BroScr_ArrayElement(result, out) = *first;
    } else {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list[2]); ++i) {
            const uint32_t* const element = BroScr_ArrayElement(list, i);
            *BroScr_ArrayElement(result, i) = *element;
        }
    }
    free_data(result);
    return 0;
}

}  // namespace game
