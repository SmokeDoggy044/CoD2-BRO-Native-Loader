#pragma once

// Template code the level sections' compiler instantiated once per level (0x00458000+ in the prologue):
// static hash-map singletons, list/vector/dynamic-array helpers, thread-functor plumbing. One member of
// each family is written here over a helper that takes the instance's addresses; tools/near_twins.py
// copies the rest of the family (levels_templates_levels.inc). Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00464490: the level's static hash map at 0x00921cd4 - created on first use (Scr_Alloc 0x28,
// HashMap_Ctor_PROLOGUE_18; counter 0x00921ccc +1), or with `release` destroyed and cleared (counter -1).
// Returns the current map (0 after a release).
uint32_t XB_CDECL StaticHashMap_GetOrRelease_PROLOGUE(uint8_t release);
// 0x00464d00: map[a][b] = text (HashMap_SetString_PROLOGUE with a copy of `text`) on the static map, created
// first when missing; releases its by-value `text`.
void XB_CDECL StaticHashMap_SetString_PROLOGUE(uint32_t a, uint32_t b, uint32_t text);
// 0x00464dc0: *out = map[a][b] or the empty string (HashMap_GetStringOrEmpty_PROLOGUE) on the static map,
// created first when missing. Returns out.
uint32_t XB_CDECL StaticHashMap_GetString_PROLOGUE(uint32_t out, uint32_t a, uint32_t b);
// 0x0045ebe0: hash map destructor - frees the bucket vector (+0x10), clears the node list (+0x4,
// std_list_ClearDestroy_PROLOGUE) and frees its head node.
void XB_THISCALL HashMap_Dtor_PROLOGUE(uint8_t* map);
// 0x0045e540: list clear - unlinks all nodes, destroys each (ListNode_DestroyValue_PROLOGUE) and frees it.
void XB_THISCALL std_list_ClearDestroy_PROLOGUE(uint8_t* list);
// 0x0045e6b0: list destructor - clear (std_list_Clear_2_PROLOGUE), then free the head node.
void XB_THISCALL std_list_Dtor_PROLOGUE(uint8_t* list);
// 0x0045a370: a list node's value destructor: releases the string at +0xc.
uint32_t XB_THISCALL ListNode_DestroyValue_PROLOGUE(uint8_t* node);
// 0x0045cfd0: a new 0xc-byte dynamic array {data, ?, size} assigned from `source` (DynArray_Assign4_PROLOGUE);
// 0 when the allocation fails.
uint8_t* XB_CDECL DynArray_NewCopy_PROLOGUE(const uint8_t* source);
// 0x0045cb70: &array->data[i], resized first (DynArray_Resize(i + 1, i + 4)) when i >= size.
uint32_t* XB_THISCALL DynArray_At_PROLOGUE(uint8_t* array, uint32_t i);

}  // namespace game
