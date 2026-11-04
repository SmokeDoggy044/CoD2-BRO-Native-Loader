#include "scr_field.h"

#include <cstring>

#include "bg_misc.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kEmptyKeyTable = 0x00922e94;
constexpr uint32_t kUndefinedBool = 0x00922ed0;
constexpr uint32_t kUndefinedFloat = 0x00922eac;
constexpr uint32_t kUndefinedVectorLane = 0x00922eb8;

// The entity's script object (its field table), or null.
ScriptKeyTable* script_object(uint32_t entity) {
    const uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(entity);  // GScr_GetEntScriptObject
    return reinterpret_cast<ScriptKeyTable*>(static_cast<uintptr_t>(object));
}

ScriptKeyTable* script_object_or_empty(uint32_t entity) {
    ScriptKeyTable* table = script_object(entity);
    return table ? table : &title_global<ScriptKeyTable>(kEmptyKeyTable);
}

// Through the runtime's builtin table (0x00922214), as the original calls it.
bool handle_is_valid(uint32_t entity) {
    return title_function<uint32_t XB_CDECL(uint32_t)>(title_global<uint32_t>(0x00922214))(entity) != 0;
}

uint32_t* key_table_set(ScriptKeyTable* table, uint32_t key, uint32_t value) {
    return title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, uint32_t)>(0x0011ea10)(table, key, value);  // ScriptKeyTable_Set
}

}  // namespace

// The value a missing field starts as: the word at `Undefined`, or 0 for Undefined = 0.
template <uint32_t Undefined>
uint32_t undefined_value() {
    if constexpr (Undefined == 0)
        return 0;
    else
        return title_global<uint32_t>(Undefined);
}

template <uint32_t Hash, uint32_t Undefined>
uint32_t* XB_CDECL ScrField_Slot(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    if (uint32_t* value = ScriptKeyTable_Find(table, Hash)) return value;
    return key_table_set(table, Hash, undefined_value<Undefined>());
}

template <uint32_t Hash, uint32_t Undefined>
bool* XB_CDECL ScrField_SlotIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    const uint32_t undefined = undefined_value<Undefined>();
    *out = (value ? *value : undefined) != undefined;
    return out;
}

template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_BoolSlot(uint32_t entity, uint32_t ecx) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    if (uint32_t* value = ScriptKeyTable_Find(table, Hash)) return value;
    // The original builds the word in its `push ecx` slot and sets only the low byte.
    const uint32_t word = (ecx & 0xffffff00u) | title_global<uint8_t>(kUndefinedBool);
    return key_table_set(table, Hash, word);
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_BoolSlotIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    const uint8_t undefined = title_global<uint8_t>(kUndefinedBool);
    *out = (value ? static_cast<uint8_t>(*value) : undefined) != undefined;
    return out;
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_FloatSlotIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    const float undefined = title_global<float>(kUndefinedFloat);
    float current = undefined;
    if (value) std::memcpy(&current, value, sizeof current);
    *out = !(current == undefined);  // UCOMISS: a NaN counts as defined
    return out;
}

template <uint32_t Hash>
ScriptString* XB_CDECL ScrField_String(uint32_t entity) {
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetString>(0x00458da0)(script_object_or_empty(entity), Hash);  // ScriptKeyTable_GetString
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_StringIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    using GetStringCopy = ScriptString* XB_THISCALL(ScriptKeyTable*, ScriptString*, uint32_t);
    ScriptString copy;
    const ScriptString* got = title_function<GetStringCopy>(0x0052bd10)(table, &copy, Hash);  // ScriptKeyTable_GetStringCopy
    *out = got->rep != nullptr;
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&copy);  // ScriptString_Release
    return out;
}

template <uint32_t Hash, uint32_t Getter>
void* XB_CDECL ScrField_Get(uint32_t entity) {
    return title_function<void* XB_THISCALL(ScriptKeyTable*, uint32_t)>(Getter)(script_object_or_empty(entity), Hash);
}

template <uint32_t Hash, uint32_t Copier>
bool* XB_CDECL ScrField_ArrayIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    uint32_t copy[3];  // data, count, capacity; the copier writes all three
    const uint32_t* got = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t*, uint32_t)>(Copier)(table, copy, Hash);
    *out = got[1] != 0;
    if (copy[0]) title_function<void XB_CDECL(uint32_t)>(0x002f5e10)(copy[0]);  // Scr_Free
    return out;
}

template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_HandleSlot(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    if (uint32_t* value = ScriptKeyTable_Find(table, Hash)) return value;
    const uint32_t none = 0;
    using SetValue = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
    return title_function<SetValue>(0x00458e10)(table, Hash, &none);  // ScriptKeyTable_SetValue
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_HandleSlotIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    *out = handle_is_valid(value ? *value : 0);
    return out;
}

template <uint32_t Hash>
float* XB_CDECL ScrField_VectorSlot(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* value = ScriptKeyTable_Find(table, Hash);
    if (!value) {
        const float undefined = title_global<float>(kUndefinedVectorLane);
        const float vector[3] = {undefined, undefined, undefined};
        using SetVector = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const float*);
        value = title_function<SetVector>(0x005b41b0)(table, Hash, vector);  // ScriptKeyTable_SetVector
    }
    return reinterpret_cast<float*>(static_cast<uintptr_t>(*value));
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_VectorSlotIsDefined(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    const float undefined = title_global<float>(kUndefinedVectorLane);
    float v[3] = {undefined, undefined, undefined};
    if (value) std::memcpy(v, reinterpret_cast<const void*>(static_cast<uintptr_t>(*value)), sizeof v);
    // UCOMISS per lane, x first; a NaN lane counts as defined.
    *out = !(v[0] == undefined) || !(v[1] == undefined) || !(v[2] == undefined);
    return out;
}

template <uint32_t Hash, uint32_t Value>
uint32_t* XB_CDECL ScrField_SlotLiteral(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    if (uint32_t* value = ScriptKeyTable_Find(table, Hash)) return value;
    return key_table_set(table, Hash, Value);
}

template <uint32_t Hash, uint32_t Missing, uint32_t Unset>
bool* XB_CDECL ScrField_SlotIsNot(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    *out = (value ? *value : Missing) != Unset;
    return out;
}

template <uint32_t Hash>
bool* XB_CDECL ScrField_SlotIsSet(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    const uint32_t* value = ScriptKeyTable_Find(table, Hash);
    *out = value && *value != 0 && *value != 0xffffffffu;
    return out;
}

template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_BoolSlotSet(uint32_t entity, uint32_t value) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, Hash);
    if (!slot) {
        // Built in the entity argument's slot: its upper bytes stay the handle's.
        const uint32_t word = (entity & 0xffffff00u) | title_global<uint8_t>(kUndefinedBool);
        slot = key_table_set(table, Hash, word);
    }
    *reinterpret_cast<uint8_t*>(slot) = static_cast<uint8_t>(value);
    return slot;
}

template <uint32_t Hash, uint32_t Offset, uint32_t Copier>
void* XB_THISCALL ScrField_CopyOf(uint8_t* self, void* out) {
    using Copy = void* XB_THISCALL(uint8_t*, void*, uint32_t);
    title_function<Copy>(Copier)(self - Offset, out, Hash);
    return out;
}

template <uint32_t Hash, uint32_t Copier, uint32_t Release>
bool* XB_CDECL ScrField_ArrayIsDefinedReleased(bool* out, uint32_t entity) {
    ScriptKeyTable* table = handle_is_valid(entity) ? script_object(entity) : nullptr;
    if (!table) {
        *out = false;
        return out;
    }
    uint32_t copy[3];
    const uint32_t* got = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t*, uint32_t)>(Copier)(table, copy, Hash);
    *out = got[1] != 0;
    title_function<void XB_THISCALL(uint32_t*)>(Release)(copy);
    return out;
}

template <uint32_t Hash, uint32_t Finder>
void XB_CDECL ScrField_Clear52(uint32_t entity) {
    using Find = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    DynArray_Resize52(title_function<Find>(Finder)(script_object_or_empty(entity), Hash), 0, 0);
}

// ---- one-off accessors ----

namespace {

ScriptKeyTable* table_before(uint8_t* self, uint32_t offset) {
    return reinterpret_cast<ScriptKeyTable*>(self - offset);
}

uint32_t* set_value(ScriptKeyTable* table, uint32_t key, const uint32_t* value) {
    using SetValue = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
    return title_function<SetValue>(0x00458e10)(table, key, value);  // ScriptKeyTable_SetValue
}

DynArray* key_table_array(uint32_t entity, uint32_t key) {
    return title_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x0045cd50)(  // ScriptKeyTable_GetArray
        script_object_or_empty(entity), key);
}

void dyn_array_resize(DynArray* array, uint32_t size, uint32_t capacity) {
    DynArray_Resize(reinterpret_cast<uint8_t*>(array), size, capacity);
}

uint8_t* field_at(uint8_t* base, uint32_t offset) { return base + offset; }

}  // namespace

const uint32_t* XB_THISCALL ScrField_hits_Set(uint8_t* self, const uint32_t* value) {
    key_table_set(table_before(self, 0x14), 0x0035aaf3, *value);
    return value;
}

uint32_t* XB_THISCALL ScrField_hits(uint8_t* self) {
    ScriptKeyTable* table = table_before(self, 0x14);
    if (uint32_t* value = ScriptKeyTable_Find(table, 0x0035aaf3)) return value;
    return key_table_set(table, 0x0035aaf3, title_global<uint32_t>(0x00922ec0));
}

uint32_t* XB_THISCALL ScrField_onfire(uint8_t* self) {
    ScriptKeyTable* table = table_before(self, 0x14);
    if (uint32_t* value = ScriptKeyTable_Find(table, 0xd8d2aa25)) return value;
    const uint32_t word = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) & 0xffffff00u) |
                          title_global<uint8_t>(kUndefinedBool);
    return key_table_set(table, 0xd8d2aa25, word);
}

void XB_CDECL ScrField_targets_groupname_2(uint32_t entity, ScriptString value) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    ScriptString* field = title_function<GetString>(0x00458da0)(table, 0x46861906);  // ScriptKeyTable_GetString
    title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(field, &value);  // ScriptString_Assign
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&value);  // ScriptString_Release
}

uint32_t* XB_CDECL ScrField_follow_ent_Set(uint32_t entity, uint32_t value) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, 0xd952b5cf);
    if (!slot) {
        const uint32_t none = 0;
        slot = set_value(table, 0xd952b5cf, &none);
    }
    *slot = value;
    return slot;
}

uint32_t* XB_CDECL ScrField_follow_ent_pos_Set(uint32_t entity, int32_t value) {
    const int32_t clamped = value < 0 ? 0 : value;
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, 0x5cd3d8b8);
    if (!slot) slot = key_table_set(table, 0x5cd3d8b8, title_global<uint32_t>(0x00922ec0));
    *slot = static_cast<uint32_t>(clamped);
    return slot;
}

uint32_t XB_CDECL ScrField_mgturret_2(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, 0x2f68c7a5);
    if (!slot) {
        const uint32_t none = 0;
        slot = set_value(table, 0x2f68c7a5, &none);
    }
    using SetMode = uint32_t XB_CDECL(uint32_t, uint32_t);
    return title_function<SetMode>(title_global<uint32_t>(0x00922584))(*slot, 0xdc26d84c);  // GScr_Turret_SetMode
}

uint32_t XB_CDECL ScrField_he104fd5c_PushBack(uint32_t entity, uint32_t item) {
    DynArray* array = key_table_array(entity, 0xe104fd5c);
    return DynArray_PushBack(reinterpret_cast<uint8_t*>(array), &item);
}

uint32_t* XB_CDECL ScrField_h4ced49f6_PopFront(uint32_t entity) {
    bool defined;  // the original asks and ignores the answer
    ScrField_h4ced49f6_IsDefined(&defined, entity);
    uint32_t i = 0;
    for (uint32_t next = 2;; ++i, ++next) {
        if (static_cast<int32_t>(i) >= static_cast<int32_t>(key_table_array(entity, 0x4ced49f6)->size - 1)) break;
        DynArray* array = key_table_array(entity, 0x4ced49f6);
        if (next - 1 >= array->size) dyn_array_resize(array, next, next + 3);
        const uint32_t* from = &array->data[i + 1];  // taken before the next lookup, as in the original
        array = key_table_array(entity, 0x4ced49f6);
        if (i >= array->size) dyn_array_resize(array, next - 1, next + 2);
        array->data[i] = *from;
    }
    DynArray* array = key_table_array(entity, 0x4ced49f6);
    if (i >= array->size) dyn_array_resize(array, i + 1, i + 4);
    uint32_t* last = &array->data[i];
    *last = title_global<uint32_t>(0x00922e84);
    return last;
}

uint32_t XB_CDECL ScrField_follow_ent(uint32_t entity, uint32_t arg, uint32_t target) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, 0xd952b5cf);
    if (!slot) {
        const uint32_t none = 0;
        slot = set_value(table, 0xd952b5cf, &none);
    }
    *slot = target;
    return title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00151370)(entity, arg);
}

uint32_t* XB_CDECL ScrField_h80658b23_SetTrue(uint32_t entity) {
    ScriptKeyTable* table = script_object_or_empty(entity);
    uint32_t* slot = ScriptKeyTable_Find(table, 0x80658b23);
    if (!slot) slot = key_table_set(table, 0x80658b23, title_global<uint32_t>(0x00922ec0));
    *slot = 1;
    return slot;
}

uint32_t* XB_CDECL ScrField_h8969d6d6_Size(uint32_t* out, uint32_t entity) {
    *out = key_table_array(entity, 0x8969d6d6)->size;
    return out;
}

uint32_t XB_CDECL ScrField_GetWeaponName(uint8_t* base, uint32_t offset, ScriptString* out) {
    const uint32_t index = *reinterpret_cast<const uint32_t*>(field_at(base, offset));
    const uint32_t def = title_global<uint32_t*>(0x0095f4d8)[index];  // bg_weaponDefs
    if (!def) return 0;
    const uint32_t name = title_global<uint32_t>(def + 8);
    using Assign = uint32_t XB_THISCALL(ScriptString*, uint32_t);
    return title_function<Assign>(0x0011dd20)(out, name);  // ScriptString_AssignCString
}

int32_t* XB_CDECL ScrField_GetMsAsSeconds(uint8_t* base, uint32_t offset, int32_t* out) {
    const float value = static_cast<float>(*reinterpret_cast<const int32_t*>(field_at(base, offset))) *
                        title_global<float>(0x0083eb4c);
    *out = static_cast<int32_t>(value);
    return out;
}

uint8_t* XB_CDECL ScrField_SetFloat01(uint8_t* base, uint32_t offset, const float* value) {
    float v = *value;
    const float one = title_global<float>(0x0083eb20);
    if (v > one)
        v = one;
    else if (0.0f > v)
        v = 0.0f;
    std::memcpy(field_at(base, offset), &v, sizeof v);
    return base;
}

uint32_t* XB_CDECL ScrField_GetByte(uint8_t* base, uint32_t offset, uint32_t* out) {
    *out = *field_at(base, offset);
    return out;
}

uint8_t* XB_CDECL ScrField_SetByte(uint8_t* base, uint32_t offset, const uint8_t* value) {
    *field_at(base, offset) = *value;
    return base;
}

uint8_t* XB_CDECL ScrField_SetSecondsAsMs(uint8_t* base, uint32_t offset, const int32_t* value) {
    const float ms = static_cast<float>(*value) * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24);
    const int32_t whole = static_cast<int32_t>(ms);
    std::memcpy(field_at(base, offset), &whole, sizeof whole);
    return base;
}

uint8_t* XB_CDECL ScrField_SetWeaponIndex(uint8_t* base, uint32_t offset, const ScriptString* name) {
    const uintptr_t rep = reinterpret_cast<uintptr_t>(name->rep);
    uint32_t index = 0;
    if (rep && rep + 6 && *reinterpret_cast<const char*>(rep + 6)) {
        const uint32_t hash = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(rep + 6));
        index = static_cast<uint8_t>(BG_FindWeaponIndexForHash(hash));
    }
    std::memcpy(field_at(base, offset), &index, sizeof index);
    return base;
}

}  // namespace game

// Instances taking the caller's ECX (ScrField_BoolSlot): an asm entry hands it on.
#define SCR_FIELD_ECX_ENTRY(name, instance)                                                         \
    namespace game {                                                                                \
    extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL name##_c(uint32_t entity, uint32_t ecx) {        \
        return instance(entity, ecx);                                                               \
    }                                                                                               \
    }                                                                                               \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl %ecx\n"                                                 \
                                     "pushl 8(%esp)\n" PLATFORM_ASM_CALL(name##_c) "addl $8, %esp\n" \
                                     "ret")

// The instances (tools/gen_scrfield.py).
#include "scr_field_instances.inc"
