#include "script_string.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "title_data.h"

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

static void string_assign(ScriptString* dst, const ScriptString* src) {
    title_function<ScriptString* XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(dst, src);  // ScriptString_Assign
}

static void string_release(ScriptString* string) {
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(string);  // ScriptString_Release
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

uint8_t XB_THISCALL ScriptString_IsEmpty(const ScriptString* self) {
    // The text address (rep + 6) is tested for null as well.
    const uintptr_t text = text_address(self->rep);
    if (!text) return 1;
    return *reinterpret_cast<const char*>(text) == '\0';
}

void XB_THISCALL ScrStringArray_PushBack(ScrStringArray* self, const ScriptString* value) {
    if (self->count >= self->capacity) {
        const uint32_t capacity = self->count + 4;
        ScriptString* data = ScrStringArray_AllocData(self, capacity, self->count + 1);
        for (uint32_t i = 0; i < self->count; ++i) string_assign(&data[i], &self->data[i]);
        ScrStringArray_FreeData(self);
        self->capacity = capacity;
        self->data = data;
    }
    ScriptString* slot = &self->data[self->count];
    ++self->count;
    string_assign(slot, value);
}

ScriptString* XB_THISCALL ScrStringArray_AllocData(ScrStringArray*, uint32_t capacity, uint32_t) {
    ScriptString* data = reinterpret_cast<ScriptString*>(Scr_Alloc(capacity * 4, 4));
    for (ScriptString* slot = data; slot != data + capacity; ++slot)
        if (slot) ScriptString_FromRep(slot, nullptr);
    return data;
}

void XB_THISCALL ScrStringArray_FreeData(ScrStringArray* self) {
    ScriptString* data = self->data;
    if (!data) return;
    for (ScriptString* slot = data, *end = data + self->count; slot != end; ++slot) string_release(slot);
    Scr_Free(self->data);
}

PLATFORM_ASM_ENTRY(ScriptString_Assign_entry,
                   "pushl %edx\n"
                   "pushl 8(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(ScriptString_Assign) "addl $8, %esp\n"
                   "popl %edx\n"
                   "ret $4")

ScriptString* XB_CDECL ScriptString_Assign(ScriptString* self, const ScriptString* other) {
    ScriptStringRep* const rep = other->rep;
    ScriptStringRep* const old = self->rep;
    self->rep = rep;
    if (rep) ++rep->refs;
    if (old && static_cast<int16_t>(--old->refs) <= 0) Scr_Free(old);
    return self;
}

PLATFORM_ASM_ENTRY(ScriptString_Release_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(ScriptString_Release) "addl $4, %esp\n"
                   "popl %ecx\n"
                   "popl %edx\n"
                   "ret")

ScriptStringRep* XB_CDECL ScriptString_Release(ScriptString* self) {
    ScriptStringRep* const rep = self->rep;
    if (rep && static_cast<int16_t>(--rep->refs) <= 0) Scr_Free(rep);
    return rep;
}

uint8_t XB_CDECL ScriptString_EqualsInt(const ScriptString* self, int32_t value) {
    // The original's C++ exception frame around the temporary has no effect.
    ScriptString text;
    title_function<ScriptString* XB_THISCALL(ScriptString*, int32_t)>(0x0011ded0)(&text, value);  // ScriptString_FromInt
    const uint8_t equal = ScriptString_EqualsString(self, &text);
    string_release(&text);
    return equal;
}

// The original's C++ exception frames around the temporaries have no effect.
ScriptString* XB_CDECL ScriptString_FromCStringPlusInt(ScriptString* out, const char* text, int32_t value) {
    ScriptString result{nullptr};
    title_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(0x0011eb50)(&result, text);  // FromCString
    title_function<ScriptString* XB_THISCALL(ScriptString*, int32_t)>(0x0011ec30)(&result, value);  // AppendInt
    ScriptString_Copy(out, &result);
    string_release(&result);
    return out;
}

ScriptString* XB_CDECL ScriptString_PlusInt(ScriptString* out, const ScriptString* string, int32_t value) {
    ScriptString result{nullptr};
    ScriptString_Copy(&result, string);
    title_function<ScriptString* XB_THISCALL(ScriptString*, int32_t)>(0x0011ec30)(&result, value);  // AppendInt
    ScriptString_Copy(out, &result);
    string_release(&result);
    return out;
}

int32_t* XB_CDECL ScriptString_ToInt(int32_t* out, const ScriptString* string) {
    using Atol = int32_t XB_CDECL(const char*);
    const uintptr_t text = text_address(string->rep);
    *out = title_global<Atol*>(0x009229e0)(reinterpret_cast<const char*>(text));
    return out;
}

uint32_t XB_THISCALL ScriptString_GetChar(const ScriptString* self, uint32_t index) {
    const ScriptStringRep* rep = self->rep;
    const uint32_t upper = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rep)) & 0xffffff00u;
    if (!rep || index >= rep->length) return upper;
    return upper | static_cast<uint8_t>(rep->text[index]);
}

ScriptStringRep* XB_THISCALL ScriptString_ReleaseAt20(uint8_t* self) {
    return title_function<ScriptStringRep* XB_THISCALL(uint8_t*)>(0x0011d640)(self + 0x20);  // ScriptString_Release
}

// --- push 3 batch 3: rep allocation, assignment, conversion, append/substring/concat ---

static uint32_t* scr_alloc(uint32_t size, uint32_t flags) {
    return title_function<uint32_t* XB_CDECL(uint32_t, uint32_t)>(0x002fb3d0)(size, flags);  // Scr_Alloc
}

static void scr_free_rep(ScriptStringRep* rep) {
    title_function<void XB_CDECL(ScriptStringRep*)>(0x002f5e10)(rep);  // Scr_Free
}

// The dec/compare of the original's inlined release (signed 16-bit count).
static bool drop_ref(ScriptStringRep* rep) { return static_cast<int16_t>(--rep->refs) <= 0; }

static ScriptStringRep* alloc_rep(const char* text, uint32_t length, uint32_t reserve) {
    return title_function<ScriptStringRep* XB_CDECL(const char*, uint32_t, uint32_t)>(0x0011dcd0)(text, length,
                                                                                                   reserve);  // ScriptString_AllocRep
}

static void rep_append(ScriptStringRep* rep, const char* text, uint32_t length) {
    title_function<void XB_THISCALL(ScriptStringRep*, const char*, uint32_t)>(0x0011d580)(rep, text, length);  // ScriptStringRep_Append
}

// The original's inline strlen: 0 for a null or empty text.
static uint32_t text_length(const char* text) {
    uint32_t length = 0;
    if (text && *text)
        while (text[++length]) {
        }
    return length;
}

static const char* title_text(uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); }

using Sprintf = int XB_CDECL(char*, const char*, ...);
static Sprintf* scr_sprintf() { return title_global<Sprintf*>(0x00922158); }

void XB_THISCALL ScriptStringRep_Release(ScriptStringRep* self) {
    if (drop_ref(self)) scr_free_rep(self);
}

void XB_THISCALL ScriptString_Clear(ScriptString* self) {
    ScriptStringRep* rep = self->rep;
    if (!rep) return;
    if (static_cast<int16_t>(rep->refs) <= 1) {
        rep->text[0] = 0;
        self->rep->length = 0;
        return;
    }
    if (drop_ref(rep)) scr_free_rep(rep);
    self->rep = nullptr;
}

uint32_t XB_CDECL ScrCall_ScrIface_atof(const char* text) {
    // The builtin's double result (ST0) stored as a float (FSTP DWORD), returned as bits.
    const float value = title_global<float XB_CDECL (*)(const char*)>(0x009229dc)(text);
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

ScriptStringRep* XB_CDECL ScriptString_AllocRep(const char* text, uint32_t length, uint32_t reserve) {
    const uint32_t needed = reserve ? reserve + 1 : length + 1;
    const uint32_t size = (needed + 0x26) & ~0x1fu;
    uint32_t* block = scr_alloc(size, 4);
    if (!block) return nullptr;
    return title_function<ScriptStringRep* XB_THISCALL(uint32_t*, uint32_t, uint32_t, const char*)>(0x0011d540)(
        block, size - 6, length, text);  // ScriptStringRep_Init
}

ScriptString* XB_THISCALL ScriptString_AssignCString(ScriptString* self, const char* text) {
    if (ScriptStringRep* rep = self->rep) {
        if (rep->refs == 1) {
            const uint32_t length = text_length(text);
            if (length + 1 < rep->unknown0) {
                // In place, counting the length up per character (re-read through self).
                char* dst = rep->text;
                rep->length = 0;
                const char* src = text;
                char c = *src;
                *dst = c;
                while (c) {
                    ScriptStringRep* const current = self->rep;
                    ++dst;
                    ++src;
                    ++current->length;
                    c = *src;
                    *dst = c;
                }
                return self;
            }
            ScriptStringRep* const fresh = alloc_rep(text, length, length);
            ScriptStringRep* const old = self->rep;
            if (drop_ref(old)) scr_free_rep(old);
            self->rep = fresh;
            return self;
        }
        if (drop_ref(rep)) scr_free_rep(rep);
    }
    self->rep = alloc_rep(text, text_length(text), 0);
    return self;
}

// The new text {c, 0} is built in the argument slot.
ScriptString* XB_THISCALL ScriptString_AssignChar(ScriptString* self, uint32_t c) {
    if (ScriptStringRep* rep = self->rep) {
        if (rep->refs == 1) {
            rep->text[0] = static_cast<char>(c);
            self->rep->text[1] = 0;
            self->rep->length = 1;
            return self;
        }
        if (drop_ref(rep)) scr_free_rep(rep);
    }
    char* const slot = reinterpret_cast<char*>(&c);
    slot[0] = static_cast<char>(c);
    slot[1] = 0;
    self->rep = alloc_rep(slot, 1, 0);
    return self;
}

static ScriptString* from_buffer(ScriptString* self, const char* buffer) {
    uint32_t length = 0;
    if (*buffer)
        while (buffer[++length]) {
        }
    self->rep = alloc_rep(buffer, length, 0);
    return self;
}

ScriptString* XB_THISCALL ScriptString_FromInt(ScriptString* self, int32_t value) {
    char buffer[16];
    scr_sprintf()(buffer, title_text(0x008325b8), value);  // "%d"
    return from_buffer(self, buffer);
}

ScriptString* XB_THISCALL ScriptString_FromInt2(ScriptString* self, int32_t value) {
    char buffer[16];
    scr_sprintf()(buffer, title_text(0x008325b8), value);  // "%d"
    return from_buffer(self, buffer);
}

ScriptString* XB_THISCALL ScriptString_FromFloat(ScriptString* self, float value) {
    char buffer[0x80];
    const double wide = static_cast<double>(platform_x87_ld(value));  // FLD DWORD / FSTP QWORD
    scr_sprintf()(buffer, title_text(0x008325b4), wide);  // "%f"
    return from_buffer(self, buffer);
}

ScriptString* XB_THISCALL ScrCall_sprintf(ScriptString* self, int32_t value) {
    char buffer[16];
    scr_sprintf()(buffer, title_text(0x008325b8), value);  // "%d"
    title_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(0x0011dd20)(self, buffer);  // AssignCString
    return self;
}

void XB_THISCALL ScriptString_Append(ScriptString* self, const char* text, uint32_t length) {
    if (!text || !length) return;
    ScriptStringRep* rep = self->rep;
    if (!rep) {
        self->rep = alloc_rep(text, length, 0);
        return;
    }
    const uint32_t old_length = rep->length;
    const uint32_t total = old_length + length;
    if (total + 1 >= rep->unknown0) {
        ScriptStringRep* const fresh = alloc_rep(rep->text, old_length, total);
        rep_append(fresh, text, length);
        ScriptStringRep* const old = self->rep;
        if (drop_ref(old)) scr_free_rep(old);
        self->rep = fresh;
        return;
    }
    if (rep->refs == 1) {
        rep_append(rep, text, length);
        return;
    }
    ScriptStringRep* const fresh = alloc_rep(rep->text, old_length, total);
    rep_append(fresh, text, length);
    title_function<void XB_THISCALL(ScriptStringRep*)>(0x0011d5b0)(self->rep);  // ScriptStringRep_Release
    self->rep = fresh;
}

// The text is cut at the end with a NUL while the new rep copies it; the byte it replaced
// waits in the `start` argument slot.
ScriptString* XB_THISCALL ScriptString_Substring(ScriptString* self, ScriptString* out, uint32_t start, uint32_t count) {
    ScriptStringRep* rep = self->rep;
    if (!rep || start >= rep->length || !count) {
        out->rep = nullptr;
        return out;
    }
    if (start + count > rep->length) count = rep->length - start;
    const uint32_t end = start + count;
    const char saved = rep->text[end];
    rep->text[end] = 0;
    rep = self->rep;
    const uint32_t size = (count + 0x27) & ~0x1fu;
    const uint32_t first = start;
    reinterpret_cast<char*>(&start)[0] = saved;
    const char* const text = rep->text + first;
    uint32_t* block = scr_alloc(size, 4);
    ScriptStringRep* fresh = nullptr;
    if (block)
        fresh = title_function<ScriptStringRep* XB_THISCALL(uint32_t*, uint32_t, uint32_t, const char*)>(0x0011d540)(
            block, size - 6, count, text);  // ScriptStringRep_Init
    self->rep->text[end] = reinterpret_cast<const char*>(&start)[0];
    out->rep = fresh;
    return out;
}

ScriptString* XB_CDECL ScriptString_Concat(ScriptString* out, const ScriptString* a, const ScriptString* b) {
    ScriptStringRep* const brep = b->rep;
    const uint32_t blen = brep ? brep->length : 0;
    if (!brep || !blen) {
        out->rep = nullptr;
        if (ScriptStringRep* rep = a->rep) {
            out->rep = rep;
            ++rep->refs;
        }
        return out;
    }
    ScriptStringRep* const arep = a->rep;
    if (!arep || !arep->length) {
        out->rep = nullptr;
        if (ScriptStringRep* rep = b->rep) {
            out->rep = rep;
            ++rep->refs;
        }
        return out;
    }
    const uint32_t alen = arep->length;
    ScriptStringRep* const fresh = alloc_rep(arep->text, alen, blen + alen);
    ScriptStringRep* const tail = b->rep;
    const uint32_t tail_length = tail ? tail->length : 0;
    rep_append(fresh, tail ? tail->text : nullptr, tail_length);
    out->rep = fresh;
    return out;
}

static void append(ScriptString* self, const char* text, uint32_t length) {
    title_function<void XB_THISCALL(ScriptString*, const char*, uint32_t)>(0x0011e1c0)(self, text, length);  // ScriptString_Append
}

ScriptString* XB_THISCALL ScriptString_FromCString(ScriptString* self, const char* text) {
    self->rep = alloc_rep(text, text_length(text), 0);
    return self;
}

// {c, 0} built in the argument slot.
ScriptString* XB_THISCALL ScriptString_AppendChar(ScriptString* self, uint32_t c) {
    char* const slot = reinterpret_cast<char*>(&c);
    slot[0] = static_cast<char>(c);
    slot[1] = 0;
    append(self, slot, 1);
    return self;
}

ScriptString* XB_THISCALL ScriptString_AppendCString(ScriptString* self, const char* text) {
    append(self, text, text_length(text));
    return self;
}

ScriptString* XB_THISCALL ScriptString_AppendString(ScriptString* self, const ScriptString* other) {
    const ScriptStringRep* rep = other->rep;
    append(self, rep ? rep->text : nullptr, rep ? rep->length : 0u);
    return self;
}

ScriptString* XB_THISCALL ScriptString_AppendInt(ScriptString* self, int32_t value) {
    char buffer[16];
    scr_sprintf()(buffer, title_text(0x008325b8), value);  // "%d"
    uint32_t length = 0;
    if (*buffer)
        while (buffer[++length]) {
        }
    append(self, buffer, length);
    return self;
}

}  // namespace game
