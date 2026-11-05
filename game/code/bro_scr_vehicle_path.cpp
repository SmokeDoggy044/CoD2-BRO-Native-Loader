#include "bro_scr_vehicle_path.h"

#include <cstring>

#include "bro_scr_array.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"

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

uint8_t* bytes(DynArray* array) { return reinterpret_cast<uint8_t*>(array); }
uint32_t* as_words(DynArray* array) { return reinterpret_cast<uint32_t*>(array); }

bool valid(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(ent) != 0; }  // Scr_EntityHandleIsValid
void queue_notify(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify); }
void delete_ent(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922540)(ent); }  // GScr_Delete
int32_t health(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent); }  // ScrEnt_GetHealth

ScriptKeyTable* table_of(uint32_t ent) { return BroScr_ScriptObjectOrEmpty(ent); }

DynArray* field_array(uint32_t ent, uint32_t key) {
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetArray>(0x0045cd50)(table_of(ent), key);  // ScriptKeyTable_GetArray
}

// The array stored under `key` (the value word points to it), set to an empty array when missing
// (ScriptKeyTable_SetArray).
DynArray* array_or_add(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = table_of(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        DynArray empty = {};
        using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, DynArray*);
        value = title_function<SetArray>(0x0045cc80)(table, key, &empty);
    }
    return pointer<DynArray>(*value);
}

// Element `i`, the array grown to i + 1 (capacity i + 4) first when needed.
uint32_t* element(DynArray* array, uint32_t i) { return BroScr_ArrayElement(as_words(array), i); }

const ScriptString* key_string(uint32_t ent, uint32_t key) {
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetString>(0x00458da0)(table_of(ent), key);  // ScriptKeyTable_GetString
}

template <typename F>
bool is_defined(F* test, uint32_t ent) {
    bool defined;
    return *test(&defined, ent);
}

const uint32_t kRiders = 0xe2a39a33;

void set_fire_anim(uint32_t ent, uint32_t anim, uint32_t root) {
    using SetKnob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetKnob>(0x009225e4)(ent, 0x54e3b2b6, anim, root, 0x3f800000, 0, 0x3f800000, 1);  // GScr_SetFlaggedAnimKnobAll
}

bool noteworthy_is(uint32_t ent, const char* text) {
    ScriptString noteworthy;
    const bool equal = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy), text);
    ScriptString_Release(&noteworthy);
    return equal;
}

}  // namespace

void XB_CDECL BroScr_DeleteUndroppedBombs(uint32_t ent) {
    if (!is_defined(ScrField_bomb_IsDefined, ent)) return;
    if (static_cast<int32_t>(field_array(ent, 0x0030b1c2)->size) <= 0) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(field_array(ent, 0x0030b1c2)->size); ++i) {
        if (!valid(*element(field_array(ent, 0x0030b1c2), i))) continue;
        if (!is_defined(ScrField_dropped_IsDefined, *element(field_array(ent, 0x0030b1c2), i))) continue;
        if (*ScrField_dropped(*element(field_array(ent, 0x0030b1c2), i)) != 0) continue;
        using At = uint32_t* XB_THISCALL(void*, uint32_t);
        delete_ent(*title_function<At>(0x0045cb70)(ScrField_bomb(ent), i));  // DynArray_At
    }
}

uint32_t* XB_CDECL BroScr_VehicleFindBlocker(uint32_t* out, uint32_t ent, DynArray* list) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list->size); ++i) {
        if (!valid(*element(list, i))) continue;
        ScriptKeyTable* const table = table_of(ent);
        const uint32_t* range = ScriptKeyTable_Find(table, 0xa22fa6ba);
        if (!range) range = ScriptKeyTable_Set(table, 0xa22fa6ba, title_global<uint32_t>(0x00922eac));
        if (i >= list->size) DynArray_Resize(bytes(list), i + 1, i + 4);
        const uint32_t range_bits = *range;
        uint32_t blocker;
        using Check = uint32_t* XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t);
        const uint32_t found =
            *title_function<Check>(0x0014e020)(&blocker, ent, ent, list->data[i], range_bits);  // BroScr_VehicleCheckBlocker
        if (found) {
            *out = found;
            return out;
        }
    }
    *out = 0;
    return out;
}

void XB_CDECL BroScr_SetRiderAnimForTag(uint32_t ent, ScriptString tag) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008cf300), 0);  // GScr_ClearAnim
    if (ScriptString_Equals(&tag, pointer<const char>(0x007fa47c))) {  // "tag_gunner"
        ScriptKeyTable* const table = table_of(ent);
        uint32_t* root = ScriptKeyTable_Find(table, 0x0038b194);
        if (!root) root = ScriptKeyTable_Set(table, 0x0038b194, 0);
        set_fire_anim(ent, title_global<uint32_t>(0x008cd38c), *root);
    } else if (ScriptString_Equals(&tag, pointer<const char>(0x007fc0bc))) {  // "tag_passenger1"
        const uint32_t root = *ScrField_root(ent);
        set_fire_anim(ent, title_global<uint32_t>(0x008cd398), root);
    } else if (ScriptString_Equals(&tag, pointer<const char>(0x007f5c08))) {  // "tag_passenger2"
        const uint32_t root = *ScrField_root(ent);
        set_fire_anim(ent, title_global<uint32_t>(0x008cd3a4), root);
    }
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_DeleteRiders(uint32_t ent, ScriptString name) {
    const uint32_t undefined = title_global<uint32_t>(0x00922ec0);
    uint32_t level_index = undefined;
    uint32_t i = 0;
    for (;; ++i) {
        if (static_cast<int32_t>(i) >= static_cast<int32_t>(array_or_add(ent, kRiders)->size)) break;
        if (!valid(*element(array_or_add(ent, kRiders), i))) continue;
        const uint32_t rider = *element(field_array(ent, kRiders), i);
        if (!ScriptString_EqualsString(key_string(rider, 0xd91e5c5b), &name)) continue;
        // Found: its index in the level's list at +0x4ec (the count when it is not there).
        level_index = 0;
        if (static_cast<int32_t>(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x4f4)) > 0) {
            for (;;) {
                const uint32_t* const mine = element(field_array(ent, kRiders), i);
                DynArray* const list = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4ec);
                if (*element(list, level_index) == *mine) break;
                ++level_index;
                if (static_cast<int32_t>(level_index) >=
                    static_cast<int32_t>(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x4f4)))
                    break;
            }
        }
        break;
    }
    if (i != array_or_add(ent, kRiders)->size) {
        if (level_index != undefined && level_index != *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x4f4)) {
            DynArray* const list = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4ec);
            delete_ent(*element(list, level_index));
            BroScr_ArrayEraseAt(pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x4ec), level_index);
        }
        if (i != undefined && i != 0) BroScr_ArrayEraseAt(as_words(field_array(ent, kRiders)), i);
    }
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_HalftrackAttachPassengers(uint32_t ent, int32_t number) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array_or_add(ent, kRiders)->size); ++i) {
        ScriptString tag;
        ScriptString_FromCStringPlusInt(&tag, pointer<const char>(0x007f5c18), number);  // "tag_passenger"
        const uint32_t rider = *element(array_or_add(ent, kRiders), i);
        const bool here = ScriptString_EqualsString(key_string(rider, 0xd91e5c5b), &tag);
        ScriptString_Release(&tag);
        if (!here) continue;
        queue_notify(*element(field_array(ent, kRiders), i), 0x0d6e32cd);
        BroScr_EntArrayRemove_he104fd5c(ent, *element(field_array(ent, kRiders), i));
        BroScr_ArrayEraseAt(as_words(field_array(ent, kRiders)), i);
        --i;
    }
}

void XB_CDECL BroScr_VehicleFollowPath(uint32_t ent) {
    bool delete_turrets = true;
    {
        ScriptString noteworthy;
        if (ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep) {
            ScriptString again;
            delete_turrets = !ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &again),
                                                  pointer<const char>(0x007f60b4));  // "noturrets"
            ScriptString_Release(&again);
        }
        ScriptString_Release(&noteworthy);
    }
    if (delete_turrets) BroScr_DeleteVehicleTurrets(ent);
    BroScr_DeleteUndroppedBombs(ent);
    DynArray_Resize(bytes(field_array(ent, 0x0030b1c2)), 0, 0);
    if (health(ent) <= 0) {
        ScriptKeyTable* const table = table_of(ent);
        uint32_t* crashed = ScriptKeyTable_Find(table, 0x4af51227);
        if (!crashed) crashed = ScriptKeyTable_Set(table, 0x4af51227, title_global<uint32_t>(0x00922ec0));
        if (*crashed != 0) {
            BroScr_PlaneCrashSound_3(ent);
            delete_ent(ent);
            return;
        }
    }
    if (health(ent) > 0) {
        delete_ent(ent);
        return;
    }
    ScriptString noteworthy;
    const bool has_noteworthy = ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep != nullptr;
    ScriptString_Release(&noteworthy);
    if (has_noteworthy) {
        if (noteworthy_is(ent, pointer<const char>(0x00824d24))) {  // "loop"
            slot<void XB_CDECL(uint32_t, uint32_t*)>(0x0092287c)(ent, ScrField_path(ent));  // GScr_Vehicle_AttachPath
            slot<void XB_CDECL(uint32_t)>(0x00922880)(ent);                                 // GScr_Vehicle_StartPath
            return;
        }
        if (noteworthy_is(ent, pointer<const char>(0x00824d18))) return;  // "no_delete"
    }
    slot<void XB_CDECL(uint32_t)>(0x0092289c)(ent);  // GScr_Vehicle_Kill
}

namespace {

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t bits_of(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The follow path (24-byte elements: point, offset point; ScriptKeyTable_GetOrAddArray_3).
DynArray* follow_path(uint32_t ent) {
    return reinterpret_cast<DynArray*>(ScriptKeyTable_GetOrAddArray_3(table_of(ent), 0xaeae4e0b));
}

uint32_t* field_slot(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = table_of(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    return ScriptKeyTable_Set(table, key, missing);
}

}  // namespace

uint32_t* XB_CDECL BroScr_FollowUpdateCurrentPos(uint32_t* out, uint32_t ent) {
    const uint32_t undefined = title_global<uint32_t>(0x00922ec0);
    uint32_t start = *field_slot(ent, 0x9aec9c1a, undefined);
    if (start == undefined) start = 0;
    float best = title_global<float>(0x00824c78);  // 1e10
    uint32_t best_index = start;
    for (uint32_t i = start;; ++i) {
        if (static_cast<int32_t>(i) >= static_cast<int32_t>(follow_path(ent)->size)) {
            *out = best_index;
            return out;
        }
        DynArray* const path = follow_path(ent);
        if (i >= path->size) DynArray_Resize24(path, i + 1, i + 4);
        const uint32_t* const point = &path->data[i * 6];
        const uint32_t followed = *BroScr_FieldHandleSlot(ent, 0xd952b5cf);
        uint32_t buffer[3];
        const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, followed);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        const float distance = slot<float XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(origin, point);  // Scr_Distance
        if (!(best > distance)) continue;
        best = distance;
        best_index = i;
        const int32_t lag = static_cast<int32_t>(*field_slot(ent, 0x96f9883f, undefined));
        if (!(static_cast<float>(lag) > distance)) continue;
        if (i == start) {
            *out = i;
        } else if (i == 0) {
            *out = 0;
        } else {
            *out = i - 1;
        }
        return out;
    }
}

void XB_CDECL BroScr_FollowPathOffsetPoint(uint32_t ent, int32_t index) {
    ScriptKeyTable* const table = table_of(ent);
    uint32_t* enabled = ScriptKeyTable_Find(table, 0x217d7784);
    // The original builds the added word in `ent`'s argument slot: its upper bytes stay.
    if (!enabled) enabled = ScriptKeyTable_Set(table, 0x217d7784, (ent & 0xffffff00u) | title_global<uint8_t>(0x00922ed0));
    if (!*reinterpret_cast<const uint8_t*>(enabled)) return;
    if (index <= 0) return;
    if (static_cast<uint32_t>(index) == follow_path(ent)->size - 1) return;
    const uint32_t* const before = DynArray_At24(follow_path(ent), static_cast<uint32_t>(index - 1));
    const float bx = as_float(before[0]), by = as_float(before[1]);
    const uint32_t* const after = DynArray_At24(follow_path(ent), static_cast<uint32_t>(index + 1));
    const float dx = as_float(after[0]) - bx;
    const float dy = as_float(after[1]) - by;
    const uint32_t side[3] = {bits_of(0.0f - dy), bits_of(dx), bits_of(0.0f)};
    uint32_t normal_buffer[3];
    const uint32_t* const n = ScrCall_Scr_VectorNormalize(normal_buffer, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(side)));
    const float nx = as_float(n[0]), ny = as_float(n[1]), nz = as_float(n[2]);
    const float r = static_cast<float>(slot<long double XB_CDECL(uint32_t, uint32_t)>(0x00922238)(0xc2000000, 0x42000000));  // Scr_RandomFloatRange(-32, 32)
    const float ox = platform_mulss(r, nx);
    const float oy = platform_mulss(ny, r);
    const float oz = platform_mulss(nz, r);
    const uint32_t* const point = DynArray_At24(follow_path(ent), static_cast<uint32_t>(index));
    const float pz = platform_addss(as_float(point[2]), oz);
    const float py = platform_addss(as_float(point[1]), oy);
    const float px = platform_addss(as_float(point[0]), ox);
    uint32_t* const offset = DynArray_At24(follow_path(ent), static_cast<uint32_t>(index)) + 3;
    offset[0] = bits_of(px);
    offset[1] = bits_of(py);
    offset[2] = bits_of(pz);
}

}  // namespace game
