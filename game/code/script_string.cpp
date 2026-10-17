#include "script_string.h"

#include <cstring>

namespace game {

ScriptString* XB_THISCALL ScriptString_Copy(ScriptString* self, const ScriptString* other) {
    // Cleared first: copying onto itself leaves it empty, as in the original.
    self->rep = nullptr;
    if (ScriptStringRep* rep = other->rep) {
        self->rep = rep;
        ++rep->refs;
    }
    return self;
}

bool XB_CDECL ScriptString_Equals(const ScriptString* self, const char* text) {
    if (!text) return false;
    const ScriptStringRep* rep = self->rep;
    if (!rep || rep->length != std::strlen(text)) return false;
    for (const char* own = rep->text; *own; ++own, ++text)
        if (*own != *text) return false;
    return true;
}

uint32_t* XB_THISCALL ScriptKeyTable_Find(ScriptKeyTable* self, uint32_t key) {
    for (uint32_t i = 0; i < self->count; ++i)
        if (self->entries[i].key == key) return &self->entries[i].value;
    return nullptr;
}

}  // namespace game
