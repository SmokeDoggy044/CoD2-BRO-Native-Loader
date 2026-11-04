#pragma once

// Field accessors of the level scripts' entity objects. The game has one small
// function per (field, kind) - hundreds of copies that differ only in the field's
// name hash - so each kind is one template here with the hash as its parameter. The
// instances, their names (ScrField_<field> as in the title's symbol list, ours) and
// their kFunctions entries are written by tools/gen_scrfield.py into
// scr_field_instances.h / scr_field_instances.inc (rerun it instead of editing them).
//
// An entity's fields live in its script object's key table (hash -> value word);
// a missing object reads as the shared empty table at 0x00922e94.

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// The field's value word, added as undefined when the table has none: the word at
// `Undefined` (0x00922ec0 ints, 0x00922eb4 handles?, 0x00922eac floats - the same
// word copy), or 0 for Undefined = 0. An entity without a script object uses the
// empty table.
template <uint32_t Hash, uint32_t Undefined>
uint32_t* XB_CDECL ScrField_Slot(uint32_t entity);

// Whether the field holds a value other than undefined. False for an invalid
// handle or an entity without a script object. Returns `out`.
template <uint32_t Hash, uint32_t Undefined>
bool* XB_CDECL ScrField_SlotIsDefined(bool* out, uint32_t entity);

// A bool field (undefined = the byte at 0x00922ed0). The word added for a missing
// field keeps the caller's ECX in its upper three bytes, as the original's stack
// slot does (asm entry).
template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_BoolSlot(uint32_t entity, uint32_t ecx);

// Whether the bool field's low byte differs from the undefined byte.
template <uint32_t Hash>
bool* XB_CDECL ScrField_BoolSlotIsDefined(bool* out, uint32_t entity);

// Whether the float field differs from the undefined float (0x00922eac); a NaN
// counts as defined.
template <uint32_t Hash>
bool* XB_CDECL ScrField_FloatSlotIsDefined(bool* out, uint32_t entity);

// The field's string (added empty when missing; ScriptKeyTable_GetString).
template <uint32_t Hash>
ScriptString* XB_CDECL ScrField_String(uint32_t entity);

// Whether the field holds a non-empty string. False for an invalid handle or an
// entity without a script object. Returns `out`.
template <uint32_t Hash>
bool* XB_CDECL ScrField_StringIsDefined(bool* out, uint32_t entity);

// The field through a ScriptKeyTable getter at `Getter` (thiscall(table, hash), e.g.
// the array getters that add an empty array): its result.
template <uint32_t Hash, uint32_t Getter>
void* XB_CDECL ScrField_Get(uint32_t entity);

// Whether the array field has storage: a copy through `Copier` (thiscall(table,
// out, hash) into a DynArray {data, capacity, size}) with a non-zero capacity,
// freed after. Returns `out`.
template <uint32_t Hash, uint32_t Copier>
bool* XB_CDECL ScrField_ArrayIsDefined(bool* out, uint32_t entity);

// An entity-handle field (starts as 0, ScriptKeyTable_SetValue).
template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_HandleSlot(uint32_t entity);

// Whether the handle field names a live entity (Scr_EntityHandleIsValid; 0 when missing).
template <uint32_t Hash>
bool* XB_CDECL ScrField_HandleSlotIsDefined(bool* out, uint32_t entity);

// A vector field: the table holds a pointer to three floats (added as three
// undefined lanes, 0x00922eb8, when missing). Returns the vector.
template <uint32_t Hash>
float* XB_CDECL ScrField_VectorSlot(uint32_t entity);

// Whether any lane differs from the undefined lane (a NaN counts as defined).
template <uint32_t Hash>
bool* XB_CDECL ScrField_VectorSlotIsDefined(bool* out, uint32_t entity);

// A field that starts as the literal `Value` (ScriptKeyTable_Set).
template <uint32_t Hash, uint32_t Value>
uint32_t* XB_CDECL ScrField_SlotLiteral(uint32_t entity);

// Whether the field (`Missing` when absent) differs from `Unset`.
template <uint32_t Hash, uint32_t Missing, uint32_t Unset>
bool* XB_CDECL ScrField_SlotIsNot(bool* out, uint32_t entity);

// Whether the field is present and neither 0 nor -1.
template <uint32_t Hash>
bool* XB_CDECL ScrField_SlotIsSet(bool* out, uint32_t entity);

// Sets a bool field's low byte to `value` (adding it as undefined first; the added
// word keeps the entity argument's upper bytes, the slot the original builds it in).
template <uint32_t Hash>
uint32_t* XB_CDECL ScrField_BoolSlotSet(uint32_t entity, uint32_t value);

// thiscall on an object whose field table sits `Offset` bytes before `this`: the
// field copied into `out` through `Copier` (thiscall(table, out, hash)).
template <uint32_t Hash, uint32_t Offset, uint32_t Copier>
void* XB_THISCALL ScrField_CopyOf(uint8_t* self, void* out);

// ScrField_ArrayIsDefined for element types with a destructor (`Release`, thiscall on the copy).
template <uint32_t Hash, uint32_t Copier, uint32_t Release>
bool* XB_CDECL ScrField_ArrayIsDefinedReleased(bool* out, uint32_t entity);

// Empties an array field of 0x34-byte elements found or added through `Finder`
// (DynArray_Resize52 to 0, 0).
template <uint32_t Hash, uint32_t Finder>
void XB_CDECL ScrField_Clear52(uint32_t entity);

// One-off accessors (no template copies).

// 0x0011c0e0 (thiscall, the field table 0x14 before `this`): sets field 0x0035aaf3 to *value.
const uint32_t* XB_THISCALL ScrField_hits_Set(uint8_t* self, const uint32_t* value);
// 0x0011c100: field 0x0035aaf3 of the table 0x14 before `this` (ScrField_Slot on it).
uint32_t* XB_THISCALL ScrField_hits(uint8_t* self);
// 0x0011c130: bool field 0xd8d2aa25 of the table 0x14 before `this`; an added word keeps
// `this`'s upper bytes (the original's push-ecx slot).
uint32_t* XB_THISCALL ScrField_onfire(uint8_t* self);
// 0x0012f990: assigns `value` (taken by value, released here) to string field 0x46861906.
void XB_CDECL ScrField_targets_groupname_2(uint32_t entity, ScriptString value);
// 0x001327e0: handle field 0xd952b5cf (added as 0) set to `value`.
uint32_t* XB_CDECL ScrField_follow_ent_Set(uint32_t entity, uint32_t value);
// 0x00132a00: int field 0x5cd3d8b8 set to `value`, negatives as 0.
uint32_t* XB_CDECL ScrField_follow_ent_pos_Set(uint32_t entity, int32_t value);
// 0x00134e60: handle field 0x2f68c7a5 passed to the turret-mode builtin (slot 0x00922584)
// with mode 0xdc26d84c.
uint32_t XB_CDECL ScrField_mgturret_2(uint32_t entity);
// 0x00149a80: appends `item` to array field 0xe104fd5c.
uint32_t XB_CDECL ScrField_he104fd5c_PushBack(uint32_t entity, uint32_t item);
// 0x0014ebc0: drops the first element of array field 0x4ced49f6 (the rest move down, the
// last slot gets the word at 0x00922e84). Returns the last slot.
uint32_t* XB_CDECL ScrField_h4ced49f6_PopFront(uint32_t entity);
// 0x00156a60: handle field 0xd952b5cf set to `target`, then 0x00151370(entity, arg).
uint32_t XB_CDECL ScrField_follow_ent(uint32_t entity, uint32_t arg, uint32_t target);
// 0x001a1240: int field 0x80658b23 set to 1.
uint32_t* XB_CDECL ScrField_h80658b23_SetTrue(uint32_t entity);
// 0x001b3960: *out = the size of array field 0x8969d6d6.
uint32_t* XB_CDECL ScrField_h8969d6d6_Size(uint32_t* out, uint32_t entity);

// Entity field-table callbacks (base, offset, value pointer): the field at base + offset.
// 0x002f4dd0: *out = the name of weapon index field (bg_weaponDefs[index] + 8) when the def exists.
uint32_t XB_CDECL ScrField_GetWeaponName(uint8_t* base, uint32_t offset, ScriptString* out);
// 0x002f4e00: *out = (int)(field * 0x0083eb4c's float).
int32_t* XB_CDECL ScrField_GetMsAsSeconds(uint8_t* base, uint32_t offset, int32_t* out);
// 0x002f4e60: field = *value clamped to [0, 0x0083eb20's float].
uint8_t* XB_CDECL ScrField_SetFloat01(uint8_t* base, uint32_t offset, const float* value);
// 0x002f4eb0: *out = the byte field.
uint32_t* XB_CDECL ScrField_GetByte(uint8_t* base, uint32_t offset, uint32_t* out);
// 0x002f4ed0: byte field = *value.
uint8_t* XB_CDECL ScrField_SetByte(uint8_t* base, uint32_t offset, const uint8_t* value);
// 0x002f5c10: field = (int)(*value * 0x0083eb9c's float + 0x0083eb24's float).
uint8_t* XB_CDECL ScrField_SetSecondsAsMs(uint8_t* base, uint32_t offset, const int32_t* value);
// 0x002f96c0: field = the weapon index for the name's hash (0 for an empty name).
uint8_t* XB_CDECL ScrField_SetWeaponIndex(uint8_t* base, uint32_t offset, const ScriptString* name);

}  // namespace game

#include "scr_field_instances.h"
