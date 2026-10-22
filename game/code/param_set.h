#pragma once

// Parameter sets (0x00023690-0x00024820): records {type, kind, u16 id, value} checked against
// a definition's parameter list and kept in a fixed buffer. FX_Add*Components fill one on the
// stack from an effect trigger's overrides, the animscript event builders (0x00200450,
// 0x00203280) from anim names.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// The records of a set (0x250 bytes, vtable 0x008376c0).
struct ParamList {
    uint32_t vtable;
    uint32_t count;
    uint8_t* records[64];  // +0x8
    uint8_t slot_of_id[256];  // +0x108
    uint32_t id_present[8];  // +0x208
    uint32_t slot_flag[2];  // +0x228
    uint32_t id_flag[8];  // +0x230
};
static_assert(sizeof(ParamList) == 0x250);

// A set (0x460 bytes, vtable 0x008376b8; slot 1 = 0x0029b9f0).
struct ParamSet {
    uint32_t vtable;
    const uint8_t* definition;  // +4: parameter count +0x1e (u16), entries +0x20, type tables +0x30
    ParamList list;  // +0x8
    uint8_t failed;  // +0x258: an unknown id was added without the flag
    uint8_t buffer[0x203];  // +0x259
    uint32_t used;  // +0x45c
};
static_assert(offsetof(ParamSet, used) == 0x45c && sizeof(ParamSet) == 0x460);

// 0x00023690: a list's vtable set, count and every bitset zeroed (the record pointers and the
// id-to-slot bytes are left as they are).
void XB_REGS1 XB_CDECL ParamList_Init(ParamList* list);
// 0x000237b0: the record pointers in use zeroed, then the count and every bitset.
void XB_REGS1 XB_CDECL ParamList_Clear(ParamList* list);
// 0x00023710 (list in EAX, flag in CL, record in ESI): the record appended; its slot's and id's
// flag bits follow `flag`, its id is marked present.
extern "C" PLATFORM_HIDDEN void XB_CDECL ParamList_Add(ParamList* list, uint32_t flag, uint8_t* record);
extern "C" void ParamList_Add_entry(void);
// 0x00023840 (vtable 0x008376c0 slot 0): cleared, freed with flags & 1.
ParamList* XB_THISCALL ParamList_Delete(ParamList* list, uint32_t flags);
// 0x00023870: the set's buffer and failed flag reset, its list cleared twice (the inlined base
// destructor).
void XB_THISCALL ParamSet_Destruct(ParamSet* set);
// 0x000238b0 (vtable 0x008376b8 slot 0): ParamSet_Destruct, freed with flags & 1.
ParamSet* XB_THISCALL ParamSet_Delete(ParamSet* set, uint32_t flags);
// 0x00024630: a 128-byte value (a name) for parameter `id`. An id the definition does not list
// marks the set failed unless `flag`; otherwise a 0x84-byte record from the buffer (4-aligned)
// goes to ParamList_Add. Nothing happens once the set has failed.
void XB_REGS2 XB_STDCALL ParamSet_AddString(uint32_t unused_eax, ParamSet* set, uint32_t id, const uint32_t* value, uint32_t flag);
// 0x00024730: the same with a 4-byte value (8-byte record).
void XB_REGS2 XB_STDCALL ParamSet_AddValue(uint32_t unused_eax, ParamSet* set, uint32_t id, const uint32_t* value, uint32_t flag);
// 0x00023b20 (source in EAX, set in EDI): the overrides an effect trigger enables (bits 0-11 of
// +0; bit n = parameter n, flag = bit n of +0xc), in the original's order; 6, 7 and 8 are names.
extern "C" PLATFORM_HIDDEN void XB_CDECL ParamSet_AddFxOverrides(const uint8_t* source, ParamSet* set);
extern "C" void ParamSet_AddFxOverrides_entry(void);

// 0x00024940 (choice in EDI: {int16 selection per parameter, definition}; id in BX): the
// selected value (parameter entry +8, 4 bytes each) of parameter `id`; null when the definition
// does not list it or nothing is selected (-1).
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL ParamChoice_GetValue(const uint8_t* choice, uint32_t id);
extern "C" void ParamChoice_GetValue_entry(void);

// 0x00296e50 (out in EAX, node in ECX): a leaf node's choices (+0, count +4) appended to the
// out lists {count, [64]}: every one to list 0, those of the highest priority (+0xa, int16;
// best so far in out[0x82], a higher one empties list 1) to list 1.
void XB_REGS3 XB_CDECL ParamNode_CollectChoices(uint32_t* out, uint32_t unused_edx, const uint8_t* node);
// 0x00299310 (set in EAX, id in EDX, value in ECX): the set's record for `id` tested against a
// node value by the record's operator (byte 1): integers (types 0/6/8) and floats (2/0xb) with
// ==, >, <, >=, <=, != (ops 0, 2, 3, 4, 5, 8); strings (4) with strcmp (op 0) or _stricmp (op
// 9); type 9 with == only. Other types break into the debugger.
uint8_t XB_REGS3 XB_CDECL ParamSet_TestCondition(const ParamSet* set, uint32_t id, const uint32_t* value);
// 0x002994f0 (set in EAX, id in EDX, value in ECX): the node value compared with the set's
// record: -1, 0, 1 (integers 0/6/8/9, floats 2/0xb, strings 4: strcmp or _stricmp by op 9).
int32_t XB_REGS3 XB_CDECL ParamSet_CompareValue(const ParamSet* set, uint32_t id, const uint32_t* value);
// 0x0029b9f0 (vtable 0x008376b8 slot 1): the definition's decision tree (root +0x2c) walked with
// the set's values: a node (+0 children, +4 count, +8 u16 value indices, +0xc parameter; 0x8000
// = leaf) whose parameter the set lacks takes every child; otherwise the children whose value
// matches (binary search for kinds 0/9, ParamSet_TestCondition for the rest) and the valueless
// ones when the parameter is flagged. Leaves feed ParamNode_CollectChoices. Nothing when failed.
void XB_THISCALL ParamSet_Resolve(ParamSet* set, uint32_t* out);

}  // namespace game
