#include "script_string.h"

#include <cstring>

namespace game {

ScriptStringRep* XB_THISCALL ScriptStringRep_Init(ScriptStringRep* self, uint16_t unknown0,
                                                  uint16_t length, const char* text) {
    self->unknown0 = unknown0;
    self->refs = 1;
    self->length = length;
    if (!text) {
        self->text[0] = '\0';
        return self;
    }
    char* out = self->text;
    while ((*out++ = *text++) != '\0') {
    }
    return self;
}

void XB_THISCALL ScriptStringRep_Append(ScriptStringRep* self, const char* text, uint16_t length) {
    if (!text) text = "(null)";
    char* out = self->text + self->length;
    while ((*out++ = *text++) != '\0') {
    }
    self->length = static_cast<uint16_t>(self->length + length);
}

ScriptString* XB_THISCALL ScriptString_FromRep(ScriptString* self, ScriptStringRep* rep) {
    self->rep = rep;
    return self;
}

ScriptString* XB_THISCALL ScriptString_Copy(ScriptString* self, const ScriptString* other) {
    // Cleared first: copying onto itself leaves it empty, as in the original.
    self->rep = nullptr;
    if (ScriptStringRep* rep = other->rep) {
        self->rep = rep;
        ++rep->refs;
    }
    return self;
}

// The text's address as the original computes it: 0 for the empty string (kept as
// an integer: arithmetic on a null pointer is undefined in C++).
static uintptr_t text_address(const ScriptStringRep* rep) {
    return rep ? reinterpret_cast<uintptr_t>(rep->text) : 0;
}

int32_t XB_THISCALL ScriptString_Find(const ScriptString* self, const char* text, uint32_t start) {
    const ScriptStringRep* rep = self->rep;
    if (start > (rep ? rep->length : 0u)) return -1;
    const char* found = std::strstr(reinterpret_cast<const char*>(text_address(rep) + start), text);
    if (!found) return -1;
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(found) - text_address(self->rep));
}

int32_t XB_THISCALL ScriptString_FindChar(const ScriptString* self, uint32_t start, char c) {
    const ScriptStringRep* rep = self->rep;
    for (; rep && start < rep->length; ++start)
        if (rep->text[start] == c) return static_cast<int32_t>(start);
    return -1;
}

int32_t XB_THISCALL ScriptString_FindLast(const ScriptString* self, const char* text) {
    const int32_t count = static_cast<int32_t>(std::strlen(text));
    const ScriptStringRep* rep = self->rep;
    // With no rep only an empty `text` gets here (at = 0), which compares nothing.
    for (int32_t at = (rep ? rep->length : 0) - count; at >= 0; --at) {
        int32_t i = 0;
        while (i < count && rep->text[at + i] == text[i]) ++i;
        if (i == count) return at;
    }
    return -1;
}

void XB_THISCALL ScriptString_SetChar(ScriptString* self, uint32_t index, char c) {
    if (ScriptStringRep* rep = self->rep; rep && index < rep->length) rep->text[index] = c;
}

bool XB_CDECL ScriptString_Equals(const ScriptString* self, const char* text) {
    if (!text) return false;
    const ScriptStringRep* rep = self->rep;
    if (!rep || rep->length != std::strlen(text)) return false;
    for (const char* own = rep->text; *own; ++own, ++text)
        if (*own != *text) return false;
    return true;
}

bool XB_CDECL ScriptString_EqualsString(const ScriptString* a, const ScriptString* b) {
    const ScriptStringRep* left = a->rep;
    const ScriptStringRep* right = b->rep;
    if ((left ? left->length : 0) != (right ? right->length : 0) || !left || !right) return false;
    // Compared up to the end of a's text only; the lengths are already equal.
    const char* own = left->text;
    const char* other = right->text;
    for (; *own; ++own, ++other)
        if (*own != *other) return false;
    return true;
}

uint32_t* XB_THISCALL ScriptKeyTable_Find(ScriptKeyTable* self, uint32_t key) {
    for (uint32_t i = 0; i < self->count; ++i)
        if (self->entries[i].key == key) return &self->entries[i].value;
    return nullptr;
}

}  // namespace game
