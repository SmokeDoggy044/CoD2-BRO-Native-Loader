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

// Whether the array field has elements: a copy through `Copier` (thiscall(table,
// out[3], hash): data, count, capacity), freed after. Returns `out`.
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

}  // namespace game

#include "scr_field_instances.h"
