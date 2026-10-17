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
    uint16_t unknown0;
    uint16_t length;  // strlen(text)
    uint16_t refs;
    char text[2];     // NUL-terminated, length + 1 bytes
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

// 0x0011d5e0: copy constructor; shares other's text.
ScriptString* XB_THISCALL ScriptString_Copy(ScriptString* self, const ScriptString* other);

// 0x0011d7c0: whether `self` holds exactly `text`. False for a null `text`, and
// for the empty string (null rep) even when `text` is "".
bool XB_CDECL ScriptString_Equals(const ScriptString* self, const char* text);

// 0x0011db70: the value stored under `key`, or null.
uint32_t* XB_THISCALL ScriptKeyTable_Find(ScriptKeyTable* self, uint32_t key);

}  // namespace game
