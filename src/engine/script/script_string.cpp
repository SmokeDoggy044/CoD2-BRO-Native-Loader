#include "script_string.h"

#include <cstddef>
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

// thiscall, callee pops 8: ECX saved around the call (the C may clobber it).
PLATFORM_ASM_ENTRY(ScriptStringRep_Append_entry,
                   "pushl %ecx\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n" PLATFORM_ASM_CALL(ScriptStringRep_Append)
                   "popl %ecx\n"
                   "ret $8")

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
    // Integer arithmetic, as the original's `add eax,6`: rep 0xfffffffa wraps to 0, and callers that test the text
    // address for null must see that. `rep->text` lets the optimizer assume a member address is never null (the
    // release build dropped ScriptString_IsEmpty's null test and read address 0).
    return rep ? reinterpret_cast<uintptr_t>(rep) + offsetof(ScriptStringRep, text) : 0;
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

// --- push 4 batch 1: key tables, trims, ConcatCString, NewRepFromCString ---

// The value-type lookup (g_scrValueTypeLookup, 0x00922e70): (key, a, b, c, d) with out slots
// for the per-type value functions; the key tables ask through slot 3 (copy) or slot 5
// (release), ScriptKeyTable_FromPairs through slot 2. Missing: ScriptValue_DefaultCopy
// (returns 0) / ScriptValue_DefaultRelease (does nothing).
using ValueLookup = void XB_CDECL(uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
using ValueFunction = uint32_t XB_CDECL(uint32_t);

static ValueLookup* value_lookup() { return title_global<ValueLookup*>(0x00922e70); }

static ValueFunction* value_function(uint32_t address) { return title_function<ValueFunction>(address); }

// The release function for `key`, asked for through `slot` (a word of the caller's frame).
static ValueFunction* release_for(uint32_t key, uint32_t* slot) {
    if (ValueLookup* lookup = value_lookup()) {
        *slot = 0;
        lookup(key, nullptr, nullptr, nullptr, slot);
        if (*slot) return value_function(*slot);
    }
    return value_function(0x0011db00);  // ScriptValue_DefaultRelease
}

static ValueFunction* copy_for(uint32_t key, uint32_t* slot) {
    if (ValueLookup* lookup = value_lookup()) {
        *slot = 0;
        lookup(key, nullptr, slot, nullptr, nullptr);
        if (*slot) return value_function(*slot);
    }
    return value_function(0x0011daf0);  // ScriptValue_DefaultCopy
}

// The source pointer's argument slot doubles as the lookup's out slot.
ScriptKeyTable* XB_THISCALL ScriptKeyTable_CopyConstruct(ScriptKeyTable* self, const ScriptKeyTable* source) {
    const ScriptKeyTable* const src = source;
    uint32_t* const slot = reinterpret_cast<uint32_t*>(&source);
    self->count = src->count;
    const uint32_t capacity = src->capacity;
    self->capacity = capacity;
    self->entries = reinterpret_cast<ScriptKeyTable::Entry*>(scr_alloc(capacity * 8u, 4));
    for (uint32_t i = 0; i < self->count; ++i) {
        self->entries[i].key = src->entries[i].key;
        ValueFunction* const copy = copy_for(self->entries[i].key, slot);
        const uint32_t value = copy(src->entries[i].value);
        self->entries[i].value = value;
    }
    return self;
}

void XB_THISCALL ScriptKeyTable_Clear(ScriptKeyTable* self) {
    uint32_t slot = 0;
    for (uint32_t i = 0; i < self->count; ++i) {
        ValueFunction* const release = release_for(self->entries[i].key, &slot);
        release(self->entries[i].value);
    }
    title_function<void XB_CDECL(void*)>(0x002f5e10)(self->entries);  // Scr_Free
    self->entries = nullptr;
}

// The key's argument slot doubles as the lookup's out slot. The last entry moves into the gap.
void XB_THISCALL ScriptKeyTable_Remove(ScriptKeyTable* self, uint32_t key) {
    if (!self->count) return;
    ScriptKeyTable::Entry* const entries = self->entries;
    uint32_t i = 0;
    while (entries[i].key != key) {
        if (++i >= self->count) return;
    }
    ValueFunction* const release = release_for(entries[i].key, &key);
    release(self->entries[i].value);
    const uint32_t last = self->count - 1;
    self->count = last;
    if (i >= last) return;
    self->entries[i].key = self->entries[last].key;
    self->entries[i].value = self->entries[self->count].value;
}

// Setting a key of the shared empty table (g_scrEmptyKeyTable) throws (C++ exception, the
// value's argument slot as the object). The key's slot doubles as the lookup's out slot.
uint32_t* XB_THISCALL ScriptKeyTable_Set(ScriptKeyTable* self, uint32_t key, uint32_t value) {
    if (reinterpret_cast<uintptr_t>(self) == 0x00922e94) {
        title_global<uint32_t XB_CDECL (*)()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
        uint8_t* const object = reinterpret_cast<uint8_t*>(&value);
        object[0] = static_cast<uint8_t>(value);
        title_function<void XB_STDCALL(void*, uint32_t)>(0x001f2302)(object, 0x0088ac80);  // _CxxThrowException
    }
    const uint32_t count = self->count;
    if (count) {
        ScriptKeyTable::Entry* const entries = self->entries;
        for (uint32_t i = 0; i < self->count; ++i) {
            if (entries[i].key != key) continue;
            ValueFunction* const release = release_for(entries[i].key, &key);
            release(self->entries[i].value);
            self->entries[i].value = value;
            return &self->entries[i].value;
        }
    }
    if (count == self->capacity) {
        ScriptKeyTable::Entry* const old = self->entries;
        const uint32_t capacity = self->capacity + 8;
        self->capacity = capacity;
        self->entries = reinterpret_cast<ScriptKeyTable::Entry*>(scr_alloc(capacity * 8u, 4));
        for (uint32_t i = 0; i < self->count; ++i) {
            self->entries[i].key = old[i].key;
            self->entries[i].value = old[i].value;
        }
        title_function<void XB_CDECL(void*)>(0x002f5e10)(old);  // Scr_Free
    }
    self->entries[self->count].key = key;
    self->entries[self->count].value = value;
    uint32_t* const result = &self->entries[self->count].value;
    ++self->count;
    return result;
}

// (name, value) pairs: each name hashed (the builtin table's HashKeyNoCase, 0x009229e4), its
// value converted by the type's slot-2 function and set; pairs without one are skipped.
ScriptKeyTable* XB_CDECL ScriptKeyTable_FromPairs(const uint32_t* pairs, int32_t count) {
    ScriptKeyTable* table = reinterpret_cast<ScriptKeyTable*>(scr_alloc(0xc, 4));
    if (table) {
        table->count = 0;
        table->capacity = 0;
        table->entries = nullptr;
    }
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t name = pairs[i * 2];
        const uint32_t value = pairs[i * 2 + 1];
        const uint32_t hash = title_global<uint32_t XB_CDECL (*)(uint32_t)>(0x009229e4)(name);
        uint32_t convert = 0, release = 0;
        value_lookup()(hash, &convert, nullptr, nullptr, &release);
        if (!convert) continue;
        const uint32_t converted = value_function(convert)(value);
        title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, uint32_t)>(0x0011ea10)(table, hash,
                                                                                               converted);  // Set
    }
    return table;
}

ScriptKeyTable* XB_CDECL ScriptKeyTable_Clone(const ScriptKeyTable* source) {
    // The original's C++ exception frame around the allocation has no effect.
    uint32_t* block = scr_alloc(0xc, 4);
    if (!block) return nullptr;
    return title_function<ScriptKeyTable* XB_THISCALL(uint32_t*, const ScriptKeyTable*)>(0x0011e870)(
        block, source);  // ScriptKeyTable_CopyConstruct
}

void XB_CDECL ScriptKeyTable_Delete(ScriptKeyTable* table) {
    if (!table) return;
    title_function<void XB_THISCALL(ScriptKeyTable*)>(0x0011e910)(table);  // ScriptKeyTable_Clear
    title_function<void XB_CDECL(void*)>(0x002f5e10)(table);  // Scr_Free
}

static uint32_t title_strchr(const char* chars, int32_t c) {
    return title_function<uint32_t XB_CDECL(const char*, int32_t)>(0x001ef5e0)(chars, c);  // CRT strchr
}

// Replaces the string with `piece` (made in the caller's argument slot) and destroys the
// temporary: self takes a reference first, then the old rep and the temporary are released.
static void take_piece(ScriptString* self, ScriptString* piece) {
    ScriptStringRep* const fresh = piece->rep;
    ScriptStringRep* const old = self->rep;
    self->rep = fresh;
    if (fresh) ++fresh->refs;
    if (old && drop_ref(old)) scr_free_rep(old);
    if (fresh && drop_ref(fresh)) scr_free_rep(fresh);
}

static void substring(ScriptString* self, ScriptString* out, uint32_t start, uint32_t count) {
    title_function<ScriptString* XB_THISCALL(ScriptString*, ScriptString*, uint32_t, uint32_t)>(0x0011e290)(
        self, out, start, count);  // ScriptString_Substring
}

// Drops leading characters found in `chars`. The `chars` argument slot holds the temporary.
ScriptString* XB_THISCALL ScriptString_TrimLeftChars(ScriptString* self, const char* chars) {
    const char* const set = chars;
    uint32_t start = 0;
    for (;;) {
        const ScriptStringRep* rep = self->rep;
        if (!rep) break;
        if (start >= rep->length || !title_strchr(set, rep->text[start])) break;
        ++start;
    }
    const uint32_t length = self->rep ? self->rep->length : 0u;
    ScriptString* const piece = reinterpret_cast<ScriptString*>(&chars);
    substring(self, piece, start, length);
    take_piece(self, piece);
    return self;
}

// Drops trailing characters found in `chars`. The `chars` argument slot holds the temporary.
ScriptString* XB_THISCALL ScriptString_TrimRightChars(ScriptString* self, const char* chars) {
    const char* const set = chars;
    uint32_t length = self->rep ? self->rep->length : 0u;
    while (length) {
        const uintptr_t text = text_address(self->rep);
        const char c = *reinterpret_cast<const char*>(text + length - 1);
        if (!title_strchr(set, c)) break;
        --length;
    }
    ScriptString* const piece = reinterpret_cast<ScriptString*>(&chars);
    substring(self, piece, 0, length);
    take_piece(self, piece);
    return self;
}

// string + C string into out; shares the string's rep for a null `text`, plain FromCString for
// an empty string. The new rep is made as a temporary (reference taken by out, then dropped).
ScriptString* XB_CDECL ScriptString_ConcatCString(ScriptString* out, const ScriptString* string, const char* text) {
    if (!text) {
        out->rep = nullptr;
        if (ScriptStringRep* rep = string->rep) {
            out->rep = rep;
            ++rep->refs;
        }
        return out;
    }
    ScriptStringRep* const rep = string->rep;
    if (!rep || !rep->length) {
        title_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(0x0011eb50)(out, text);  // FromCString
        return out;
    }
    const uint32_t length = rep->length;
    uint32_t extra = 0;
    if (*text)
        while (text[++extra]) {
        }
    ScriptStringRep* const fresh = alloc_rep(rep->text, length, length + extra + 1);
    rep_append(fresh, text, extra);
    out->rep = nullptr;
    if (fresh) {
        out->rep = fresh;
        ++fresh->refs;
        if (drop_ref(fresh)) scr_free_rep(fresh);
    }
    return out;
}

// A new rep for `text` with room for exactly its length ((length + 0x27) & ~0x1f bytes).
ScriptStringRep* XB_CDECL ScriptString_NewRepFromCString(const char* text) {
    const uint32_t length = text_length(text);
    const uint32_t size = (length + 0x27) & ~0x1fu;
    uint32_t* block = scr_alloc(size, 4);
    if (!block) return nullptr;
    return title_function<ScriptStringRep* XB_THISCALL(uint32_t*, uint32_t, uint32_t, const char*)>(0x0011d540)(
        block, size - 6, length, text);  // ScriptStringRep_Init
}


// --- Key tables of structs and arrays. The arrays are {data, capacity, count} (12 bytes) and
// one instance per element type exists for each shape below; `Assign` is that type's copy
// (thiscall(dst, src), dst zeroed first). The originals' C++ exception frames have no effect.
namespace {

using KeyTableAssign = void XB_THISCALL(uint32_t* dst, const uint32_t* source);

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A copy of `source` in a new block (null if the allocation fails) set under `key`; returns
// the value's slot.
template <uint32_t Assign>
uint32_t* key_table_set_array(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    uint32_t* block = scr_alloc(0xc, 4);
    if (block) {
        block[0] = 0;
        block[1] = 0;
        block[2] = 0;
        title_function<KeyTableAssign>(Assign)(block, source);
    }
    return ScriptKeyTable_Set(self, key, address_of(block));
}

// The array under `key`; a new empty one is set (through `Setter`) when there is none.
template <uint32_t Setter>
uint32_t key_table_get_or_add_array(ScriptKeyTable* self, uint32_t key) {
    uint32_t* slot = ScriptKeyTable_Find(self, key);
    if (!slot) {
        uint32_t empty[3] = {0, 0, 0};
        slot = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*)>(Setter)(self, key,
                                                                                                     empty);
    }
    return *slot;
}

// `out` = a copy of the array under `key`, or empty.
template <uint32_t Assign>
uint32_t* key_table_get_array_copy(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    const uint32_t* slot = ScriptKeyTable_Find(self, key);
    if (!slot) {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        return out;
    }
    const uint32_t array = *slot;
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    title_function<KeyTableAssign>(Assign)(out, reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(array)));
    return out;
}

}  // namespace

// A zeroed 0x1c-byte struct is added when the key has none (null when the allocation fails).
uint32_t XB_THISCALL ScriptKeyTable_GetStruct1c(ScriptKeyTable* self, uint32_t key) {
    if (const uint32_t* slot = ScriptKeyTable_Find(self, key)) return *slot;
    uint32_t* block = scr_alloc(0x1c, 4);
    if (block) {
        for (int i = 0; i < 7; ++i) block[i] = 0;  // REP MOVSD of a zeroed local
    }
    return *ScriptKeyTable_Set(self, key, address_of(block));
}

uint32_t* XB_THISCALL ScriptKeyTable_SetArrayBytes(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0017bb40>(self, key, source);  // DynArray_AssignBytes
}
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_2(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045c740>(self, key, source);
}
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_3(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045c7c0>(self, key, source);
}
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_4(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045c860>(self, key, source);
}
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045c8e0>(self, key, source);
}
uint32_t* XB_THISCALL ScriptKeyTable_SetArray_5(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0017ba30>(self, key, source);  // DynArray_Assign4
}
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray_2(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045d5f0>(self, key, source);
}
uint32_t* XB_THISCALL ScriptKeyTable_SetNewArray_3(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    return key_table_set_array<0x0045c6b0>(self, key, source);
}

uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_2(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x0017c200>(self, key);  // ScriptKeyTable_SetArray_2
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_3(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x0017c330>(self, key);  // ScriptKeyTable_SetArray_3
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_4(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x0017c460>(self, key);  // ScriptKeyTable_SetArray_4
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x0017c590>(self, key);  // ScriptKeyTable_SetNewArray
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArrayBytes(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x0017c100>(self, key);  // ScriptKeyTable_SetArrayBytes
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_6(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x001e5fa0>(self, key);  // ScriptKeyTable_SetNewArray_2
}
uint32_t XB_THISCALL ScriptKeyTable_FindOrAdd(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x001e6120>(self, key);  // ScriptKeyTable_SetNewArray_3
}
uint32_t XB_THISCALL ScriptKeyTable_GetOrAddArray_7(ScriptKeyTable* self, uint32_t key) {
    return key_table_get_or_add_array<0x001e5f20>(self, key);  // ScriptKeyTable_SetArray_5
}

uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopy_2(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c410>(self, out, key);  // DynArray_Assign12
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_2(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c7c0>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c860>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_3(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c8e0>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmptyBytes(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0017bb40>(self, out, key);  // DynArray_AssignBytes
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_7(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045d5f0>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_4(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c740>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_5(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0045c6b0>(self, out, key);
}
uint32_t* XB_THISCALL ScriptKeyTable_GetArrayCopyOrEmpty_6(ScriptKeyTable* self, uint32_t* out, uint32_t key) {
    return key_table_get_array_copy<0x0017ba30>(self, out, key);  // DynArray_Assign4
}

// A copy (0x00458f60: nine words and a script string at +0x20) of the 0x28-byte struct in a new
// block (null if the allocation fails) set under `key`; returns the value's slot.
uint32_t* XB_THISCALL ScriptKeyTable_AddStruct28(ScriptKeyTable* self, uint32_t key, const uint32_t* source) {
    uint32_t* block = scr_alloc(0x28, 4);
    if (block) title_function<KeyTableAssign>(0x00458f60)(block, source);
    return ScriptKeyTable_Set(self, key, address_of(block));
}

// The struct under `key`; a default one is added when there is none: word 0 from 0x00922eb4,
// six words from 0x00922eb8, an empty string at +0x20. Words +0x1c and +0x24 are never set in
// the original's local (zero here, as in the test processes' dead stack).
uint32_t XB_THISCALL ScriptKeyTable_GetStruct28(ScriptKeyTable* self, uint32_t key) {
    if (const uint32_t* slot = ScriptKeyTable_Find(self, key)) return *slot;
    uint32_t value[10] = {};
    value[0] = title_global<uint32_t>(0x00922eb4);
    for (int i = 1; i <= 6; ++i) value[i] = title_global<uint32_t>(0x00922eb8);
    ScriptString* const name = reinterpret_cast<ScriptString*>(&value[8]);
    ScriptString_FromRep(name, nullptr);
    uint32_t* const slot = ScriptKeyTable_AddStruct28(self, key, value);
    string_release(name);
    return *slot;
}

// Appends a pointer ({data, capacity, count}); a full array grows to count + 4 (the old data
// copied, then freed). Returns the old count.
namespace {

// `alloc` (stdcall, capacity and count): Scr_AllocPtrArray here, a level copy of it in the level copies.
uint32_t scr_ptr_array_append(uint32_t* self, const uint32_t* value, uint32_t alloc) {
    if (self[2] >= self[1]) {
        const uint32_t capacity = self[2] + 4;
        uint32_t* data = title_function<uint32_t* XB_STDCALL(uint32_t, uint32_t)>(alloc)(capacity, self[2] + 1);
        for (uint32_t i = 0; i < self[2]; ++i) data[i] = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(self[0]))[i];
        if (self[0]) Scr_Free(reinterpret_cast<void*>(static_cast<uintptr_t>(self[0])));
        self[0] = address_of(data);
        self[1] = capacity;
    }
    const uint32_t count = self[2];
    reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(self[0]))[count] = *value;
    ++self[2];
    return count;
}

}  // namespace

uint32_t XB_THISCALL ScrPtrArray_Append(uint32_t* self, const uint32_t* value) {
    return scr_ptr_array_append(self, value, 0x0011f660);  // Scr_AllocPtrArray
}


// EDI = out, the string by value (asm entry: its argument slot): each "\n" escape (0x0082d278)
// after index 0 becomes ' ' + newline, then the result is copied to out and the argument released.
extern "C" PLATFORM_HIDDEN ScriptString* XB_CDECL ScriptString_ExpandNewlines(ScriptString* out, ScriptString* value) {
    const char* const escape = reinterpret_cast<const char*>(uintptr_t{0x0082d278});
    for (int32_t at = ScriptString_Find(value, escape, 0); at > 0;
         at = ScriptString_Find(value, escape, static_cast<uint32_t>(at + 2))) {
        ScriptString_SetChar(value, static_cast<uint32_t>(at), ' ');
        ScriptString_SetChar(value, static_cast<uint32_t>(at + 1), '\n');
    }
    ScriptString_Copy(out, value);
    string_release(value);
    return out;
}

}  // namespace game

PLATFORM_ASM_ENTRY(ScriptString_ExpandNewlines_entry,
                   "leal 4(%esp), %eax\n pushl %eax\n pushl %edi\n" PLATFORM_ASM_CALL(ScriptString_ExpandNewlines)
                   "addl $8, %esp\n ret")

namespace game {

#include "script_string_levels.inc"

}  // namespace game
