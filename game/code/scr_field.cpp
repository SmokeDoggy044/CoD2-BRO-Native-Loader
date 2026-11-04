#include "scr_field.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kEmptyKeyTable = 0x00922e94;
constexpr uint32_t kUndefinedBool = 0x00922ed0;
constexpr uint32_t kUndefinedFloat = 0x00922eac;

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
