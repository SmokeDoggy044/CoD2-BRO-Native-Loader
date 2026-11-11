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

// Path / vehicle node fields: `node` is a node handle, `Slot` the runtime builtin
// (0x00922xxx) the original calls through.
// *out = Slot(node, hash) (the node's int key).
template <uint32_t Hash, uint32_t Slot>
uint32_t* XB_CDECL ScrNode_GetKey(uint32_t* out, uint32_t node);
// The same on a handle held at `self` (thiscall, ret 4).
template <uint32_t Hash, uint32_t Slot>
uint32_t* XB_THISCALL ScrNode_GetKeyOf(const uint32_t* self, uint32_t* out);
// Slot(*self, hash, *value); returns `value`.
template <uint32_t Hash, uint32_t Slot>
const uint32_t* XB_THISCALL ScrNode_SetKeyOf(const uint32_t* self, const uint32_t* value);
// node != -1 and the int key differs from the undefined int.
template <uint32_t Hash, uint32_t Slot>
bool* XB_CDECL ScrNode_KeyIsDefined(bool* out, uint32_t node);
// *out = *Slot(&tmp, node, hash) (a node-handle key).
template <uint32_t Hash, uint32_t Slot>
uint32_t* XB_CDECL ScrNode_GetNodeKey(uint32_t* out, uint32_t node);
// node != -1 and the node key is not -1.
template <uint32_t Hash, uint32_t Slot>
bool* XB_CDECL ScrNode_NodeKeyIsDefined(bool* out, uint32_t node);
// A built-in node field: defined when the handle passes `Check` (0 also fails when ZeroIsInvalid).
template <uint32_t Check, uint32_t ZeroIsInvalid>
bool* XB_CDECL ScrNode_HandleIsDefined(bool* out, uint32_t node);
// *out = a copy of the string key (Slot(&tmp, *self, hash), tmp released).
template <uint32_t Hash, uint32_t Slot>
ScriptString* XB_THISCALL ScrNode_StringKeyCopy(const uint32_t* self, ScriptString* out);
// The same for a built-in string field (Slot(&tmp, *self)).
template <uint32_t Slot>
ScriptString* XB_THISCALL ScrNode_StringCopy(const uint32_t* self, ScriptString* out);

// The string field through `Getter` (thiscall(&node, out)); returns `out`. Asm entry (SCR_FIELD_ESP_ENTRY):
// (out, node) read from the entry ESP's frame, the getter run at the original's ESP.
template <uint32_t Getter>
ScriptString* XB_CDECL ScrNode_String(uint32_t entry_esp, uint32_t ecx);
// out[0..2] = the vector Slot(&tmp, node) returns.
template <uint32_t Slot>
float* XB_CDECL ScrNode_Vector(float* out, uint32_t node);
// *out = Slot(node, hash) (x87 float result).
template <uint32_t Hash, uint32_t Slot>
float* XB_CDECL ScrNode_FloatKey(float* out, uint32_t node);
// *out = Slot(node) (x87 float result).
template <uint32_t Slot>
float* XB_CDECL ScrNode_Float(float* out, uint32_t node);
// Whether the string Copier(self, &tmp) gives is non-empty (thiscall, self in ECX; asm entry
// SCR_FIELD_ESP_ENTRY: the temporary at the original's frame word, the copier at the original's ESP).
template <uint32_t Copier>
bool XB_CDECL ScrNode_StringNonEmpty(uint32_t entry_esp, uint32_t self);

// Hud element `index` (0x7c-byte records at 0x0094cc88, live while +0 is non-zero):
// setters write the field at `Offset` of a live record and return the record's
// address; getters read it, 0 (floats: 0x0083eafc's) otherwise. The float setters
// are word copies (ScrHudElem_SetWord).
template <uint32_t Offset>
uint32_t XB_CDECL ScrHudElem_SetWord(uint32_t index, uint32_t value);
template <uint32_t Offset>
uint32_t XB_CDECL ScrHudElem_GetWord(uint32_t index);
template <uint32_t Offset>
uint32_t XB_CDECL ScrHudElem_SetByte(uint32_t index, uint32_t value);
template <uint32_t Offset>
uint8_t XB_CDECL ScrHudElem_GetByte(uint32_t index);
template <uint32_t Offset>
float XB_CDECL ScrHudElem_GetFloat(uint32_t index);

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
// 0x002f4e20 (value pointer in EAX, object in ECX; nothing calls it or points to it): v = *value, at least
// 0x0083100c's float (4); the record at object +4 gets v at +0x18 and v * v at +0x1c; returns that record.
uint8_t* XB_REGS3 XB_CDECL ScrField_SetFloatMin4AndSquare(const float* value, uint32_t unused, const uint8_t* object);
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

// 0x00198d80 (out, node): node != -1 and its script_uniquename is non-empty (the title's
// ScrVehNodeField_script_uniquename_IsNonEmpty run at the original's ESP: asm entry).
extern "C" void ScrVehNodeField_script_uniquename_IsDefined_entry(void);
extern "C" PLATFORM_HIDDEN bool* XB_CDECL ScrVehNodeField_script_uniquename_IsDefined_c(uint32_t entry_esp);

// 0x001a4290: the struct field 0x12993a44 (ScriptKeyTable_GetStruct28, 0x001e4a20). That getter adds a
// missing field from a 0x28-byte frame block one word of which (+0x1c) it never sets: the asm entry runs
// both title calls at the original's ESP so the word holds what it holds in the original.
extern "C" void ScrField_h12993a44_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ScrField_h12993a44_c(uint32_t entry_esp, uint32_t entity);

// The script runtime's engine interface (pointers handed to the runtime).
// 0x002f5120: Com_HashKeyNoCase of `text` (the original moves it into EDX and jumps; 5 bytes too short to compare).
uint32_t XB_CDECL ScrIface_HashKeyNoCase(const char* text);
// 0x002f5340: the title CRT's atof (x87 result).
long double XB_CDECL ScrIface_atof(const char* text);
// 0x002f5400: SmallHeap_Free on com_smallHeap (0x00947700).
void XB_CDECL ScrIface_SmallFree(uint8_t* mem);
// 0x002f5420: SmallHeap_Alloc(size) on com_smallHeap.
uint8_t* XB_CDECL ScrIface_Alloc(uint32_t size);
// 0x002f5440: back to the script small heap (0x0095f4a4) when it owns mem, else the script heap (0x0095f4a0).
void XB_CDECL ScrIface_Free(uint8_t* mem);
// 0x002f5470: per character (letters lowered by the CRT's isalpha/tolower) rotate left 5, xor the signed char.
uint32_t XB_CDECL ScrIface_HashStringNoCase(const char* text);
// 0x002f62d0: the script small heap when the size fits its largest pool, else the script heap
// (Heap_AllocFromHeap with `align`); int3 when both fail.
uint8_t* XB_CDECL ScrIface_Malloc(uint32_t size, uint32_t align);

}  // namespace game

#include "scr_field_instances.h"
