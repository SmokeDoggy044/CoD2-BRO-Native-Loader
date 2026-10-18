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

}  // namespace game
