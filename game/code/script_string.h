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
    uint32_t unknown4;
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
void XB_THISCALL ScriptStringRep_Append(ScriptStringRep* self, const char* text, uint16_t length);

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

}  // namespace game
