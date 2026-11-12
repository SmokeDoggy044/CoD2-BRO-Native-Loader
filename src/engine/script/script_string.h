#pragma once

// Strings of the level-script runtime (the code behind the builtin pointer table at
// 0x009221xx that the level sections call). The names are ours: the game has no
// symbols, so they come from what the callers do.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Shared, reference-counted text.
struct ScriptStringRep {
    uint16_t unknown0;  // ScriptStringRep_Init's first argument (capacity?)
    uint16_t length;    // strlen(text)
    uint16_t refs;
    char text[2];       // NUL-terminated, length + 1 bytes
};

struct ScriptString {
    ScriptStringRep* rep;  // null = empty
};

// (key, value) pairs searched by key.
struct ScriptKeyTable {
    uint32_t count;
    uint32_t capacity;  // entries allocated (grows by 8)
    struct Entry {
        uint32_t key;
        uint32_t value;
    }* entries;
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(ScriptStringRep) == 8);
static_assert(offsetof(ScriptStringRep, text) == 6);
static_assert(sizeof(ScriptKeyTable) == 12);
#endif

// 0x0011d540: sets up a new rep with one reference; copies `text` (null: empty
// text, `length` is stored as given).
ScriptStringRep* XB_THISCALL ScriptStringRep_Init(ScriptStringRep* self, uint16_t unknown0,
                                                  uint16_t length, const char* text);

// 0x0011d580: copies `text` ("(null)" for null) to the end of the text and adds
// `length` (the caller's count, not checked against the text) to the length. The
// caller makes sure the allocation is large enough.
// The entry keeps ECX: the original never writes it, and callers store the rep (ECX) after the call.
extern "C" PLATFORM_HIDDEN void XB_THISCALL ScriptStringRep_Append(ScriptStringRep* self, const char* text, uint16_t length);
extern "C" void ScriptStringRep_Append_entry(void);

// 0x0011d5d0: constructor taking a rep without adding a reference.
ScriptString* XB_THISCALL ScriptString_FromRep(ScriptString* self, ScriptStringRep* rep);

// 0x0011d5e0: copy constructor; shares other's text.
ScriptString* XB_THISCALL ScriptString_Copy(ScriptString* self, const ScriptString* other);

// 0x0011d6a0: index of the first `text` at or after `start`, or -1. Like the
// original, an empty string (null rep) is searched at address `start`.
int32_t XB_THISCALL ScriptString_Find(const ScriptString* self, const char* text, uint32_t start);

// 0x0011d700: index of the first `c` at or after `start`, or -1.
int32_t XB_THISCALL ScriptString_FindChar(const ScriptString* self, uint32_t start, char c);

// 0x0011d730: index of the last `text`, or -1. An empty `text` gives the length.
int32_t XB_THISCALL ScriptString_FindLast(const ScriptString* self, const char* text);

// 0x0011d7a0: replaces the character at `index` when it is inside the text.
void XB_THISCALL ScriptString_SetChar(ScriptString* self, uint32_t index, char c);

// 0x0011d7c0: whether `self` holds exactly `text`. False for a null `text`, and
// for the empty string (null rep) even when `text` is "".
bool XB_CDECL ScriptString_Equals(const ScriptString* self, const char* text);

// 0x0011d820: whether both hold the same text. False when either is the empty
// string (null rep), even both.
bool XB_CDECL ScriptString_EqualsString(const ScriptString* a, const ScriptString* b);

// 0x0011db70: the value stored under `key`, or null.
uint32_t* XB_THISCALL ScriptKeyTable_Find(ScriptKeyTable* self, uint32_t key);

// 0x00011200: whether the string has no text (null rep, or an empty text).
uint8_t XB_THISCALL ScriptString_IsEmpty(const ScriptString* self);

// A growable array of script strings.
struct ScrStringArray {
    ScriptString* data;
    uint32_t capacity;
    uint32_t count;
};

// 0x00015be0: appends a new reference to `value`; a full array first grows by 4 (new block,
// strings assigned over, old ones released and freed).
void XB_THISCALL ScrStringArray_PushBack(ScrStringArray* self, const ScriptString* value);
// 0x0001b740 (callee pops 8; neither `self` nor the second count is read): `capacity` empty
// strings from Scr_Alloc.
ScriptString* XB_THISCALL ScrStringArray_AllocData(ScrStringArray* self, uint32_t capacity, uint32_t unused);
// 0x0001b7b0: releases the strings and frees the block (Scr_Free), if any. Leaves the fields.
void XB_THISCALL ScrStringArray_FreeData(ScrStringArray* self);

// 0x0011d600 (thiscall, asm entry: EDX kept): shares other's text (one more reference) and drops
// its own (Scr_Free at 0 references). Returns self.
extern "C" PLATFORM_HIDDEN ScriptString* XB_CDECL ScriptString_Assign(ScriptString* self, const ScriptString* other);
extern "C" void ScriptString_Assign_entry(void);
// 0x0011d640 (thiscall, asm entry: ECX and EDX kept): drops a reference (Scr_Free at 0). The
// string keeps its rep pointer. EAX = the rep (Scr_Free's result after a free; no caller reads it).
extern "C" PLATFORM_HIDDEN ScriptStringRep* XB_CDECL ScriptString_Release(ScriptString* self);
extern "C" void ScriptString_Release_entry(void);
// 0x00119a20: whether the string equals the decimal text of `value` (ScriptString_FromInt).
uint8_t XB_CDECL ScriptString_EqualsInt(const ScriptString* self, int32_t value);
// 0x0011a7d0: `text` followed by the decimal `value` (ScriptString_FromCString, AppendInt).
ScriptString* XB_CDECL ScriptString_FromCStringPlusInt(ScriptString* out, const char* text, int32_t value);
// 0x0011b1c0: a copy of `string` followed by the decimal `value`.
ScriptString* XB_CDECL ScriptString_PlusInt(ScriptString* out, const ScriptString* string, int32_t value);
// 0x0011ba80: atol of the text (the builtin table's atol, 0x009229e0; null for the empty string).
int32_t* XB_CDECL ScriptString_ToInt(int32_t* out, const ScriptString* string);
// 0x0011c0c0: the character at `index`, 0 outside the text; AL only (the upper EAX bytes are
// the rep pointer's).
uint32_t XB_THISCALL ScriptString_GetChar(const ScriptString* self, uint32_t index);
// 0x0011a850: releases the string member at +0x20 (tail jump to ScriptString_Release). Its only
// users are C++ exception-unwind actions.
ScriptStringRep* XB_THISCALL ScriptString_ReleaseAt20(uint8_t* self);
// Push 3: the rest of the string runtime. Reps are allocated rounded: (needed + 0x26) & ~0x1f
// bytes with capacity (unknown0) = size - 6, `needed` = reserve + 1 or length + 1. A rep with
// one reference is changed in place when the new text fits, else replaced (old one released).
// 0x0011d5b0: drops a reference; Scr_Free at zero or below.
void XB_THISCALL ScriptStringRep_Release(ScriptStringRep* self);
// 0x0011d660: empties the text in place (one reference or fewer) or lets go of the rep.
void XB_THISCALL ScriptString_Clear(ScriptString* self);
// 0x0011da40: the builtin table's atof (0x009229dc) as float bits in EAX.
uint32_t XB_CDECL ScrCall_ScrIface_atof(const char* text);
// 0x0011dcd0: a new rep holding `length` characters of `text`, room for `reserve` (0: length).
ScriptStringRep* XB_CDECL ScriptString_AllocRep(const char* text, uint32_t length, uint32_t reserve);
// 0x0011dd20 / 0x0011de00: assignment of a C string / one character (built in the argument slot).
ScriptString* XB_THISCALL ScriptString_AssignCString(ScriptString* self, const char* text);
ScriptString* XB_THISCALL ScriptString_AssignChar(ScriptString* self, uint32_t c);
// 0x0011ded0 / 0x0011df90 / 0x0011dff0: constructors from "%d" / "%d" / "%f" (the builtin
// table's sprintf, 0x00922158; the float goes through the x87 as a double).
ScriptString* XB_THISCALL ScriptString_FromInt(ScriptString* self, int32_t value);
ScriptString* XB_THISCALL ScriptString_FromInt2(ScriptString* self, int32_t value);
ScriptString* XB_THISCALL ScriptString_FromFloat(ScriptString* self, float value);
// 0x0011e0f0: assigns the decimal text of `value`.
ScriptString* XB_THISCALL ScrCall_sprintf(ScriptString* self, int32_t value);
// 0x0011e1c0: appends `length` characters (nothing for a null text or zero length).
void XB_THISCALL ScriptString_Append(ScriptString* self, const char* text, uint32_t length);
// 0x0011e290: `count` characters from `start` (clipped) into `out`; empty when start is past
// the end. The source is NUL-cut while copying and restored.
ScriptString* XB_THISCALL ScriptString_Substring(ScriptString* self, ScriptString* out, uint32_t start, uint32_t count);
// 0x0011e340: a + b; shares the other one's rep when either is empty.
ScriptString* XB_CDECL ScriptString_Concat(ScriptString* out, const ScriptString* a, const ScriptString* b);
// 0x0011eb50: constructor from a C string.
ScriptString* XB_THISCALL ScriptString_FromCString(ScriptString* self, const char* text);
// 0x0011eb90 / 0x0011ebc0 / 0x0011ebf0 / 0x0011ec30: append a character, C string, string, "%d".
ScriptString* XB_THISCALL ScriptString_AppendChar(ScriptString* self, uint32_t c);
ScriptString* XB_THISCALL ScriptString_AppendCString(ScriptString* self, const char* text);
ScriptString* XB_THISCALL ScriptString_AppendString(ScriptString* self, const ScriptString* other);
ScriptString* XB_THISCALL ScriptString_AppendInt(ScriptString* self, int32_t value);
// Push 4: key tables (values converted / released through the value-type lookup at
// 0x00922e70, ScriptValue_DefaultCopy / DefaultRelease when it has none), trims, ConcatCString.
// 0x0011e870 (pops 4): copy of `source` with each value copied through its type's function.
ScriptKeyTable* XB_THISCALL ScriptKeyTable_CopyConstruct(ScriptKeyTable* self, const ScriptKeyTable* source);
// 0x0011e910: releases every value, frees the entries (count is left as it was).
void XB_THISCALL ScriptKeyTable_Clear(ScriptKeyTable* self);
// 0x0011e980 (pops 4): releases and removes `key`'s entry; the last entry fills the gap.
void XB_THISCALL ScriptKeyTable_Remove(ScriptKeyTable* self, uint32_t key);
// 0x0011ea10 (pops 8): sets `key` (the old value released), growing by 8 entries; returns the
// value's address. Throws on the shared empty table (0x00922e94).
uint32_t* XB_THISCALL ScriptKeyTable_Set(ScriptKeyTable* self, uint32_t key, uint32_t value);
// 0x0011f060: a new table from `count` (name, value) pairs.
ScriptKeyTable* XB_CDECL ScriptKeyTable_FromPairs(const uint32_t* pairs, int32_t count);
// 0x0011f100 / 0x0011f160: new copy / Clear + Scr_Free (null: nothing).
ScriptKeyTable* XB_CDECL ScriptKeyTable_Clone(const ScriptKeyTable* source);
void XB_CDECL ScriptKeyTable_Delete(ScriptKeyTable* table);
// 0x0011ed20 / 0x0011edf0 (pop 4): strip leading / trailing characters that are in `chars`.
ScriptString* XB_THISCALL ScriptString_TrimLeftChars(ScriptString* self, const char* chars);
ScriptString* XB_THISCALL ScriptString_TrimRightChars(ScriptString* self, const char* chars);
// 0x0011eec0: string + C string into `out`.
ScriptString* XB_CDECL ScriptString_ConcatCString(ScriptString* out, const ScriptString* string, const char* text);
// 0x0011efc0: a new rep holding `text`.
ScriptStringRep* XB_CDECL ScriptString_NewRepFromCString(const char* text);

// Key tables of structs and arrays (script_string.cpp; thiscall, the callee pops its stack arguments).
uint32_t XB_THISCALL ScriptKeyTable_GetStruct1c(ScriptKeyTable* self, uint32_t key);  // 0x0017c080
uint32_t* XB_THISCALL ScriptKeyTable_SetArrayBytes(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x0017c100
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_2(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x0017c200
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_3(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x0017c330
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_4(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x0017c460
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x0017c590
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_5(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x001e5f20
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray_2(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x001e5fa0
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray_3(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x001e6120
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_2(ScriptKeyTable* self, uint32_t key);  // 0x0017c280
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_3(ScriptKeyTable* self, uint32_t key);  // 0x0017c400
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_4(ScriptKeyTable* self, uint32_t key);  // 0x0017c530
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray(ScriptKeyTable* self, uint32_t key);  // 0x0017c660
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArrayBytes(ScriptKeyTable* self, uint32_t key);  // 0x0017c6c0
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_6(ScriptKeyTable* self, uint32_t key);  // 0x001e6070
uint32_t XB_THISCALL ScriptKeyTable_FindOrAdd(ScriptKeyTable* self, uint32_t key);  // 0x001e61f0
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_7(ScriptKeyTable* self, uint32_t key);  // 0x001e6250
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopy_2(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x0017c2e0
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_2(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x0017c3b0
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x0017c4e0
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_3(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x0017c610
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmptyBytes(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x0017c720
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_7(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x001e6020
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_4(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x001e60d0
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_5(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x001e61a0
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_6(ScriptKeyTable* self, uint32_t* out, uint32_t key);  // 0x001e62b0
uint32_t* XB_THISCALL ScriptKeyTable_AddStruct28(ScriptKeyTable* self, uint32_t key, const uint32_t* source);  // 0x001e49b0
uint32_t XB_THISCALL ScriptKeyTable_GetStruct28(ScriptKeyTable* self, uint32_t key);  // 0x001e4a20
uint32_t XB_THISCALL ScrPtrArray_Append(uint32_t* self, const uint32_t* value);  // 0x001e5ec0

// 0x0030f4d0 (EDI = out, the string by value; asm entry).
extern "C" PLATFORM_HIDDEN ScriptString* XB_CDECL ScriptString_ExpandNewlines(ScriptString* out, ScriptString* value);
extern "C" void ScriptString_ExpandNewlines_entry(void);

// 0x0011e800 (gap function, 11-11): the value under `key` compared with `other` by the value type's function (lookup
// slot 3 of g_scrValueTypeLookup; Scr_ReturnFalse without one); AL = 0 when the key is missing.
uint32_t XB_CDECL ScriptKeyTable_ValueEquals(const ScriptKeyTable* table, uint32_t key, uint32_t other);

}  // namespace game
