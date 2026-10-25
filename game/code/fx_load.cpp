#include "fx_load.h"

#include <cstdarg>
#include <cstring>

#include "fx.h"
#include "lanes.h"
#include "ngl.h"
#include "stream.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

template <typename T>
const T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
}

// The render operations' common head: vtable, kind bits (+4), 0 (+8).
void op_head(uint8_t* self, uint32_t vtable) {
    at<uint32_t>(self, 4) = 0;
    at<uint32_t>(self, 8) = 0;
    at<uint32_t>(self, 0) = vtable;
}

// The effect memory's allocator (0x008bb760, vtable slot 1: size, alignment). Some callers
// clear the allocation context word 0x00909948 around the call.
uint8_t* fx_alloc(uint32_t size, uint32_t alignment) {
    uint8_t* allocator = title_global<uint8_t*>(0x008bb760);
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(allocator, 0)))[1];
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
        title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(method)(allocator, size, alignment)));
}

uint8_t* fx_alloc_no_context(uint32_t size, uint32_t alignment) {
    uint32_t& context = title_global<uint32_t>(0x00909948);
    const uint32_t saved = context;
    context = 0;
    uint8_t* memory = fx_alloc(size, alignment);
    context = saved;
    return memory;
}

// The range objects: two floats (or vectors) ordered min, max per component.
void ordered(float a, float b, float* lo, float* hi) {
    *lo = a;
    *hi = b;
    if (a > b) {
        *lo = b;
        *hi = a;
    }
}

uint8_t* new_range(uint32_t vtable, float a, float b) {
    uint8_t* self = fx_alloc(0xc, 4);
    if (!self) return nullptr;
    at<uint32_t>(self, 0) = vtable;
    ordered(a, b, &at<float>(self, 4), &at<float>(self, 8));
    return self;
}

uint8_t* new_vector_range(uint32_t vtable, const float* a_in, const float* b_in) {
    const float a[3] = {a_in[0], a_in[1], a_in[2]};
    const float b[3] = {b_in[0], b_in[1], b_in[2]};
    uint8_t* self = fx_alloc(0x1c, 4);
    if (!self) return nullptr;
    at<uint32_t>(self, 0) = vtable;
    for (int i = 0; i < 3; ++i) ordered(a[i], b[i], &at<float>(self, 4 + i * 4), &at<float>(self, 0x10 + i * 4));
    return self;
}

// An ARGB colour as four floats b, g, r, a in 0..1 (DIVPS by 255).
void color_to_floats(uint32_t color, float* out) {
    const float k = title_global<float>(0x0083eb8c);
    const Lanes v = {{static_cast<float>(static_cast<int32_t>(color & 0xff)),
                      static_cast<float>(static_cast<int32_t>((color >> 8) & 0xff)),
                      static_cast<float>(static_cast<int32_t>((color >> 16) & 0xff)),
                      static_cast<float>(static_cast<int32_t>(color >> 24))}};
    for (int i = 0; i < 4; ++i) out[i] = v.v[i] / k;
}

// The vector operations 0x000a4ed0..: vtable, xyz of the vector at +0x10, the float at +0x1c.
uint8_t* vector_op(uint8_t* self, uint32_t vtable, const float* v, float w) {
    at<uint32_t>(self, 0) = vtable;
    std::memcpy(self + 0x10, v, 12);
    at<float>(self, 0x1c) = w;
    return self;
}

}  // namespace

void XB_THISCALL FxEffect_SetName(uint8_t* self, const char* name) {
    int32_t i = 0;
    if (name && *name) {
        do {
            if (i >= 0x40) return;
            self[0x6c + i] = static_cast<uint8_t>(name[i]);
        } while (name[++i]);
        if (i >= 0x40) return;
    }
    std::memset(self + 0x6c + i, 0, static_cast<uint32_t>(0x40 - i));
}

void XB_THISCALL FxEffect_SetMatrix(uint8_t* self, const uint32_t* matrix) { std::memcpy(self, matrix, 0x40); }

void XB_THISCALL FxEffect_SetEmitterEntry(uint8_t* self, int32_t index, const uint32_t* name, uint32_t a, uint32_t b,
                                          uint32_t c) {
    uint8_t* entry = at<uint8_t*>(self, 0x58) + index * 0x2c;
    std::memcpy(entry, name, 0x20);
    at<uint32_t>(at<uint8_t*>(self, 0x58) + index * 0x2c, 0x20) = a;
    at<uint32_t>(at<uint8_t*>(self, 0x58) + index * 0x2c, 0x24) = b;
    at<uint32_t>(at<uint8_t*>(self, 0x58) + index * 0x2c, 0x28) = c;
}

uint32_t* XB_CDECL FxVec3_FromWords1(uint32_t* out, const uint32_t* src) {
    out[0] = src[1];
    out[1] = src[2];
    out[2] = src[3];
    return out;
}

uint32_t* XB_CDECL FxVec4_FromWords1(uint32_t* out, const uint32_t* src) {
    const uint32_t v[4] = {src[1], src[2], src[3], src[1]};
    std::memcpy(out, v, 16);
    return out;
}

void XB_THISCALL FxOp_SetColor14(uint8_t* self, uint32_t, uint32_t color, uint32_t, uint32_t) {
    color_to_floats(color, reinterpret_cast<float*>(self + 0x14));
}

void XB_THISCALL FxOp_SetColor24(uint8_t* self, uint32_t, uint32_t color, uint32_t, uint32_t) {
    color_to_floats(color, reinterpret_cast<float*>(self + 0x24));
}

void XB_THISCALL FxParamBlock_SetSlot(uint8_t* self, int32_t index, uint32_t value) {
    at<uint32_t*>(self, 0xc)[index] = value;
}

uint8_t* XB_THISCALL FxElementList_Init(uint8_t* self) {
    at<uint32_t>(self, 0) = 0;
    at<uint16_t>(self, 4) = 0;
    at<uint16_t>(self, 6) = 0;
    at<uint32_t>(self, 8) = 0;
    return self;
}

uint8_t* XB_THISCALL FxOp31_Init(uint8_t* self, const uint32_t* src) {
    op_head(self, 0x007c6724);
    at<uint32_t>(self, 0xc) = src[1];
    at<uint32_t>(self, 0x10) = src[0];
    at<uint32_t>(self, 4) = 0x31;
    return self;
}

uint8_t* XB_THISCALL FxOp1d5_Init(uint8_t* self, const uint8_t* src) {
    at<uint32_t>(self, 0) = 0x007c70a0;
    at<uint32_t>(self, 4) = 0;
    at<uint32_t>(self, 8) = 0;
    std::memcpy(self + 0x10, src + 0x24, 16);
    at<uint32_t>(self, 0x20) = at<uint32_t>(src, 0);
    at<uint32_t>(self, 0x24) = at<uint32_t>(src, 4);
    for (int i = 0; i < 7; ++i) at<uint16_t>(self, 0x28 + i * 2) = at<uint16_t>(src, 8 + i * 4);
    at<uint32_t>(self, 4) = 0x1d5;
    return self;
}

uint8_t* XB_THISCALL FxOp53_Init(uint8_t* self, const uint8_t* src) {
    at<uint32_t>(self, 4) = 0;
    at<uint32_t>(self, 8) = 0;
    at<uint32_t>(self, 0) = 0x007c8490;
    std::memcpy(self + 0x20, src + 0x14, 16);
    at<uint32_t>(self, 0x30) = at<uint32_t>(src, 0);
    at<uint32_t>(self, 0x34) = at<uint32_t>(src, 4);
    at<uint32_t>(self, 0x38) = at<uint32_t>(src, 0x28);
    at<uint32_t>(self, 0x3c) = at<uint32_t>(src, 0x2c);
    std::memcpy(self + 0x10, src + 8, 16);
    at<uint32_t>(self, 4) = at<uint32_t>(src, 0x24) ? 0x853 : 0x53;
    return self;
}

uint8_t* XB_THISCALL FxVertexFormat_Layout(uint8_t* self) {
    uint32_t offset = 0;
    const uint32_t mask = at<uint32_t>(self, 0);
    for (int i = 0; i < 0x1a; ++i) {
        if (mask & (1u << i)) {
            self[8 + i] = static_cast<uint8_t>(offset);
            offset += title_global<uint8_t>(0x008bbbe0 + static_cast<uint32_t>(i));
        } else {
            self[8 + i] = 0;
        }
    }
    at<uint32_t>(self, 4) = (offset + 0xf) & ~0xfu;
    return self;
}

uint8_t* XB_THISCALL FxVectorOp_Init9330(uint8_t* self, const float* v, float w) { return vector_op(self, 0x007c9330, v, w); }
uint8_t* XB_THISCALL FxVectorOp_Init9348(uint8_t* self, const float* v, float w) { return vector_op(self, 0x007c9348, v, w); }
uint8_t* XB_THISCALL FxVectorOp_Init9360(uint8_t* self, const float* v, float w) { return vector_op(self, 0x007c9360, v, w); }
uint8_t* XB_THISCALL FxVectorOp_Init9378(uint8_t* self, const float* v, float w) { return vector_op(self, 0x007c9378, v, w); }

uint8_t* XB_THISCALL FxSegmentOp_Init(uint8_t* self, const float* a, const float* b) {
    at<uint32_t>(self, 0) = 0x007c9390;
    std::memcpy(self + 0x10, a, 16);
    const Lanes d = sub(lanes(b), lanes(a));
    std::memcpy(self + 0x20, d.v, 16);
    return self;
}

uint8_t* XB_STDCALL FxArray16_New(uint32_t capacity, uint32_t count) {
    uint8_t* memory = fx_alloc_no_context(capacity << 4, 4);
    if (!memory) return nullptr;
    for (uint8_t* p = memory; p != memory + (count << 4); p += 0x10) {
        if (p) {
            at<uint32_t>(p, 0) = 3;
            at<uint32_t>(p, 4) = 0;
        }
    }
    return memory;
}

uint8_t* XB_THISCALL FxEffect_Init(uint8_t* self, const char* name) {
    const float* zero = &title_global<float>(0x007c50e0);
    at<float>(self, 0x40) = zero[0];
    at<float>(self, 0x44) = zero[1];
    at<float>(self, 0x48) = zero[2];
    at<float>(self, 0x4c) = title_global<float>(0x0083eb20);
    at<uint32_t>(self, 0x50) = 0;
    at<uint16_t>(self, 0x54) = 0;
    at<uint16_t>(self, 0x56) = 0;
    at<uint32_t>(self, 0x58) = 0;
    at<uint16_t>(self, 0x5c) = 0;
    at<uint16_t>(self, 0x5e) = 0;
    at<uint32_t>(self, 0x64) = 0;
    at<uint32_t>(self, 0x68) = 0x40;
    title_function<void XB_CDECL(uint8_t*)>(0x0001c140)(self);  // Matrix_Identity
    FxEffect_SetName(self, name);
    for (uint32_t offset = 0xac; offset <= 0xc0; offset += 4) at<uint32_t>(self, offset) = 0;
    return self;
}

uint8_t* XB_STDCALL FxEmitterTable_New(uint32_t count) {
    uint8_t* memory = fx_alloc_no_context(count * 0x2c, 4);
    if (!memory) return nullptr;
    for (uint8_t* p = memory; p != memory + count * 0x2c; p += 0x2c) {
        if (p) std::memset(p, 0, 0x2c);
    }
    return memory;
}

uint8_t* XB_CDECL FxRenderer_NewOp31(const uint32_t* const* source) {
    const uint32_t* p = *source;
    const uint32_t words[2] = {p[5], p[1]};
    uint8_t* self = fx_alloc(0x14, 0x10);
    if (!self) return nullptr;
    return FxOp31_Init(self, words);
}

uint8_t* XB_CDECL FxRange_New5ac0(float a, float b) { return new_range(0x007c5ac0, a, b); }
uint8_t* XB_CDECL FxRange_New5b20(float a, float b) { return new_range(0x007c5b20, a, b); }

uint8_t* XB_CDECL FxConst_New(float value) {
    uint8_t* self = fx_alloc(8, 4);
    if (!self) return nullptr;
    at<uint32_t>(self, 0) = 0x007c5af0;
    at<float>(self, 4) = value;
    return self;
}

uint8_t* XB_CDECL FxConstVector_New(const float* v) {
    const float copy[3] = {v[0], v[1], v[2]};
    uint8_t* self = fx_alloc(0x10, 4);
    if (!self) return nullptr;
    at<uint32_t>(self, 0) = 0x007c5b08;
    std::memcpy(self + 4, copy, 12);
    return self;
}

uint8_t* XB_CDECL FxVectorRange_New5ad8(const float* a, const float* b) { return new_vector_range(0x007c5ad8, a, b); }
uint8_t* XB_CDECL FxVectorRange_New5b38(const float* a, const float* b) { return new_vector_range(0x007c5b38, a, b); }

namespace {

// The effect allocator's free (vtable slot 2), with the context word cleared around it.
void fx_free_no_context(uint32_t memory) {
    uint32_t& context = title_global<uint32_t>(0x00909948);
    const uint32_t saved = context;
    uint8_t* allocator = title_global<uint8_t*>(0x008bb760);
    context = 0;
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(allocator, 0)))[2];
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(method)(allocator, memory);
    context = saved;
}

// Growable arrays {data, int16 capacity (+4), int16 count (+6)}: the old block freed and the
// array cleared before the new one is installed.
void replace_block(uint8_t* self, uint8_t* memory, int16_t capacity, int16_t count) {
    if (at<uint32_t>(self, 0)) {
        fx_free_no_context(at<uint32_t>(self, 0));
        at<uint32_t>(self, 0) = 0;
        at<int16_t>(self, 4) = 0;
        at<int16_t>(self, 6) = 0;
    }
    at<uint8_t*>(self, 0) = memory;
    at<int16_t>(self, 6) = count;
    at<int16_t>(self, 4) = capacity;
}

}  // namespace

uint32_t XB_THISCALL FxArray16_Reserve(uint8_t* self, int32_t capacity) {
    if (capacity <= at<int16_t>(self, 4)) return 1;
    const int32_t count = at<int16_t>(self, 6);
    uint8_t* memory = FxArray16_New(static_cast<uint32_t>(capacity), static_cast<uint32_t>(count));
    if (!memory) return 0;
    for (int32_t i = 0; i < count; ++i) std::memcpy(memory + i * 16, at<uint8_t*>(self, 0) + i * 16, 16);
    replace_block(self, memory, static_cast<int16_t>(capacity), static_cast<int16_t>(count));
    return 1;
}

uint32_t XB_THISCALL FxArray16_Push(uint8_t* self, const uint32_t* element) {
    const int16_t count = at<int16_t>(self, 6);
    if (count < at<int16_t>(self, 4)) {
        std::memcpy(at<uint8_t*>(self, 0) + count * 16, element, 16);
        ++at<int16_t>(self, 6);
        return 1;
    }
    const int32_t n = count;
    const int32_t capacity = n > 3 ? n + 4 : n + 1;
    uint8_t* memory = FxArray16_New(static_cast<uint32_t>(capacity), static_cast<uint32_t>(n + 1));
    if (!memory) return 0;
    for (int32_t i = 0; i < n; ++i) std::memcpy(memory + i * 16, at<uint8_t*>(self, 0) + i * 16, 16);
    replace_block(self, memory, static_cast<int16_t>(capacity), count);
    std::memcpy(memory + n * 16, element, 16);
    ++at<int16_t>(self, 6);
    return 1;
}

uint32_t XB_THISCALL FxEmitterTable_Resize(uint8_t* self, int32_t count) {
    if (count <= at<int16_t>(self, 4)) {
        const int32_t old = at<int16_t>(self, 6);
        if (count > old) {
            // begin and end once (the elements can overlap self: random inputs)
            uint8_t* const data = at<uint8_t*>(self, 0);
            for (uint8_t* p = data + old * 0x2c, *end = data + count * 0x2c; p != end; p += 0x2c) {
                if (p) std::memset(p, 0, 0x2c);
            }
        }
        at<int16_t>(self, 6) = static_cast<int16_t>(count);
        return 1;
    }
    uint8_t* memory = FxEmitterTable_New(static_cast<uint32_t>(count));
    if (!memory) return 0;
    for (int32_t i = 0; i < at<int16_t>(self, 6); ++i)
        std::memcpy(memory + i * 0x2c, at<uint8_t*>(self, 0) + i * 0x2c, 0x2c);
    replace_block(self, memory, static_cast<int16_t>(count), static_cast<int16_t>(count));
    return 1;
}

uint8_t* XB_THISCALL FxOpdd_Init(uint8_t* self, const uint8_t* src) {
    FxOp53_Init(self, src);
    at<uint32_t>(self, 0) = 0x007c7320;
    at<uint32_t>(self, 4) = at<uint32_t>(src, 0x24) ? 0x8dd : 0xdd;
    return self;
}

uint8_t* XB_THISCALL FxOpGrid_Init(uint8_t* self, const uint8_t* src) {
    FxOp53_Init(self, src);
    at<uint32_t>(self, 0) = 0x007c8220;
    const int32_t columns = at<int32_t>(src, 0x30);
    const int32_t rows = at<int32_t>(src, 0x34);
    at<float>(self, 0x40) = static_cast<float>(columns);
    at<float>(self, 0x44) = static_cast<float>(rows * columns - 1);
    const float one = title_global<float>(0x0083eb20);
    at<float>(self, 0x48) = one / static_cast<float>(columns);
    at<float>(self, 0x4c) = one / static_cast<float>(rows);
    std::memcpy(self + 0x10, src + 8, 16);
    at<uint32_t>(self, 4) = at<uint32_t>(src, 0x24) ? 0x953 : 0x153;
    return self;
}

uint8_t* XB_THISCALL FxVertexFormat_Init(uint8_t* self) {
    at<uint32_t>(self, 0) = 0;
    FxVertexFormat_Layout(self);
    return self;
}

uint8_t* XB_THISCALL FxVertexFormat_Add(uint8_t* self, uint32_t bits) {
    at<uint32_t>(self, 0) |= bits;
    return FxVertexFormat_Layout(self);
}

uint8_t* XB_THISCALL FxPtrArray_InitFill(uint8_t* self, uint32_t count, const uint32_t* value) {
    uint8_t* memory = fx_alloc_no_context(count * 4, 4);
    at<uint8_t*>(self, 0) = memory;
    if (!memory) {
        at<int16_t>(self, 6) = 0;
        at<int16_t>(self, 4) = 0;
        return self;
    }
    at<int16_t>(self, 6) = static_cast<int16_t>(count);
    at<int16_t>(self, 4) = static_cast<int16_t>(count);
    for (int32_t i = 0; i < at<int16_t>(self, 6); ++i) at<uint32_t*>(self, 0)[i] = *value;
    return self;
}

uint8_t* XB_THISCALL FxAction_Init(uint8_t* self, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    at<uint32_t>(self, 0) = 0x007c6220;
    const float zero = 0.0f;
    uint32_t zero_word;
    std::memcpy(&zero_word, &zero, 4);
    FxPtrArray_InitFill(self + 4, a, &zero_word);
    const uint32_t null_word = 0;
    FxPtrArray_InitFill(self + 0xc, b, &null_word);
    at<uint32_t>(self, 0x14) = c;
    at<uint32_t>(self, 0x18) = d;
    at<uint32_t*>(self, 4)[0] = title_global<uint32_t>(0x0083ef2c);  // -FLT_MAX
    at<uint32_t*>(self, 4)[1] = title_global<uint32_t>(0x007c5000);  // FLT_MAX
    return self;
}

void XB_THISCALL FxActionList_CollectFormat(const uint8_t* self, uint8_t* format) {
    uint8_t* const* p = at<uint8_t* const*>(self, 0);
    uint8_t* const* end = p + at<int16_t>(self, 6);
    for (; p != end; ++p) FxVertexFormat_Add(format, at<uint32_t>(*p, 0x18));
}

namespace {

uint8_t* action(uint8_t* self, uint32_t vtable, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    FxAction_Init(self, a, b, c, d);
    at<uint32_t>(self, 0) = vtable;
    return self;
}

}  // namespace

uint8_t* XB_THISCALL FxAction8df0_Init(uint8_t* self) { return action(self, 0x007c8df0, 0x3, 0x10, 0x0, 0x200); }
uint8_t* XB_THISCALL FxAction8e08_Init(uint8_t* self) { return action(self, 0x007c8e08, 0x3, 0x10, 0x0, 0x200); }
uint8_t* XB_THISCALL FxAction8e20_Init(uint8_t* self) { return action(self, 0x007c8e20, 0x3, 0x11, 0x0, 0x200); }
uint8_t* XB_THISCALL FxAction8e38_Init(uint8_t* self) { return action(self, 0x007c8e38, 0x2, 0x0, 0x1, 0x600); }
uint8_t* XB_THISCALL FxAction8e4c_Init(uint8_t* self) { return action(self, 0x007c8e4c, 0x4, 0x0, 0x1, 0x610); }
uint8_t* XB_THISCALL FxAction8e60_Init(uint8_t* self) { return action(self, 0x007c8e60, 0x5, 0x0, 0x1, 0x610); }
uint8_t* XB_THISCALL FxAction8e74_Init(uint8_t* self) { return action(self, 0x007c8e74, 0x4, 0x0, 0x1, 0x1610); }
uint8_t* XB_THISCALL FxAction8e88_Init(uint8_t* self) { return action(self, 0x007c8e88, 0x3, 0x0, 0x1, 0x2); }
uint8_t* XB_THISCALL FxAction8e9c_Init(uint8_t* self) { return action(self, 0x007c8e9c, 0x3, 0x0, 0x1, 0x2); }
uint8_t* XB_THISCALL FxAction8eb0_Init(uint8_t* self) { return action(self, 0x007c8eb0, 0x3, 0x0, 0x1, 0x4); }
uint8_t* XB_THISCALL FxAction8ec4_Init(uint8_t* self) { return action(self, 0x007c8ec4, 0x3, 0x0, 0x1, 0x4); }
uint8_t* XB_THISCALL FxAction8ed8_Init(uint8_t* self) { return action(self, 0x007c8ed8, 0x3, 0x0, 0x1, 0x80); }
uint8_t* XB_THISCALL FxAction8eec_Init(uint8_t* self) { return action(self, 0x007c8eec, 0x3, 0x0, 0x1, 0x80); }
uint8_t* XB_THISCALL FxAction8f00_Init(uint8_t* self) { return action(self, 0x007c8f00, 0x2, 0x0, 0x1, 0x2840); }
uint8_t* XB_THISCALL FxAction8f14_Init(uint8_t* self) { return action(self, 0x007c8f14, 0x2, 0x0, 0x1, 0x4821); }
uint8_t* XB_THISCALL FxAction8f28_Init(uint8_t* self) { return action(self, 0x007c8f28, 0x2, 0x0, 0x1, 0x801); }
uint8_t* XB_THISCALL FxAction8f3c_Init(uint8_t* self) { return action(self, 0x007c8f3c, 0x6, 0x0, 0x1, 0x1); }
uint8_t* XB_THISCALL FxAction8f50_Init(uint8_t* self) { return action(self, 0x007c8f50, 0x6, 0x0, 0x1, 0x800); }
uint8_t* XB_THISCALL FxAction8f64_Init(uint8_t* self) { return action(self, 0x007c8f64, 0xa, 0x0, 0x1, 0x608); }
uint8_t* XB_THISCALL FxAction8f78_Init(uint8_t* self) { return action(self, 0x007c8f78, 0x3, 0x0, 0x1, 0x800); }
uint8_t* XB_THISCALL FxAction8f8c_Init(uint8_t* self) { return action(self, 0x007c8f8c, 0x3, 0x0, 0x1, 0x2000); }
uint8_t* XB_THISCALL FxAction8fa0_Init(uint8_t* self) { return action(self, 0x007c8fa0, 0x3, 0x0, 0x1, 0x4000); }
uint8_t* XB_THISCALL FxAction8fb4_Init(uint8_t* self) { return action(self, 0x007c8fb4, 0x9, 0x0, 0x1, 0x801); }
uint8_t* XB_THISCALL FxAction8fc8_Init(uint8_t* self) { return action(self, 0x007c8fc8, 0x5, 0x0, 0x1, 0x100); }
uint8_t* XB_THISCALL FxAction8fdc_Init(uint8_t* self) { return action(self, 0x007c8fdc, 0x2, 0x0, 0x1, 0x840); }
uint8_t* XB_THISCALL FxAction8ff0_Init(uint8_t* self) { return action(self, 0x007c8ff0, 0x2, 0x0, 0x1, 0x40); }
uint8_t* XB_THISCALL FxAction9004_Init(uint8_t* self) { return action(self, 0x007c9004, 0x2, 0x0, 0x1, 0x40); }
uint8_t* XB_THISCALL FxAction9018_Init(uint8_t* self) { return action(self, 0x007c9018, 0x2, 0x0, 0x1, 0x40); }
uint8_t* XB_THISCALL FxAction902c_Init(uint8_t* self) { return action(self, 0x007c902c, 0x6, 0x0, 0x1, 0x801); }
uint8_t* XB_THISCALL FxAction9040_Init(uint8_t* self) { return action(self, 0x007c9040, 0x9, 0x0, 0x1, 0x801); }
uint8_t* XB_THISCALL FxAction9054_Init(uint8_t* self) { return action(self, 0x007c9054, 0x9, 0x0, 0x1, 0x801); }
uint8_t* XB_THISCALL FxAction9068_Init(uint8_t* self) { return action(self, 0x007c9068, 0xb, 0x0, 0x1, 0xf8800); }
uint8_t* XB_THISCALL FxAction907c_Init(uint8_t* self) { return action(self, 0x007c907c, 0x5, 0x0, 0x1, 0x500001); }
uint8_t* XB_THISCALL FxAction9090_Init(uint8_t* self) { return action(self, 0x007c9090, 0x3, 0x0, 0x1, 0x200001); }
uint8_t* XB_THISCALL FxAction90a4_Init(uint8_t* self) { return action(self, 0x007c90a4, 0x4, 0x0, 0x1, 0x3800041); }

namespace {

uint8_t* derived_op(uint8_t* self, const uint8_t* src, bool grid, uint32_t vtable, uint32_t kind) {
    if (grid)
        FxOpGrid_Init(self, src);
    else
        FxOp53_Init(self, src);
    at<uint32_t>(self, 0) = vtable;
    at<uint32_t>(self, 4) = at<uint32_t>(src, 0x24) ? (kind | 0x800) : kind;
    return self;
}

// The effect allocator's free without the context word (slot 2).
void fx_free(uint32_t memory) {
    uint8_t* allocator = title_global<uint8_t*>(0x008bb760);
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(allocator, 0)))[2];
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(method)(allocator, memory);
}

// A stage's render object (+0x10: virtual destructor slot 0 with 0, then freed) and action list
// (+0x14: apsPtrVector_DeleteAll, then freed).
void destroy_stage_renderer(uint8_t* stage) {
    const uint32_t renderer = at<uint32_t>(stage, 0x10);
    if (!renderer) return;
    const uint32_t destructor = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(renderer))));
    title_function<void XB_THISCALL(uint32_t, uint32_t)>(destructor)(renderer, 0);
}

void destroy_stage_list(uint32_t list) {
    title_function<void XB_THISCALL(uint32_t)>(0x0009ca90)(list);  // apsPtrVector_DeleteAll
}

void init_stage(uint8_t* stage) {
    at<uint32_t>(stage, 0) = 0xa;
    at<float>(stage, 4) = title_global<float>(0x0083eb34);  // -1
    at<float>(stage, 8) = title_global<float>(0x007c5000);  // FLT_MAX
    at<uint32_t>(stage, 0xc) = 1;
    at<uint32_t>(stage, 0x10) = 0;
    at<uint32_t>(stage, 0x14) = 0;
    FxVertexFormat_Init(stage + 0x18);
}

}  // namespace

uint8_t* XB_THISCALL FxOp1dd_Init(uint8_t* self, const uint8_t* src) { return derived_op(self, src, true, 0x007c75a0, 0x1dd); }
uint8_t* XB_THISCALL FxOp9d5_Init(uint8_t* self, const uint8_t* src) { return derived_op(self, src, true, 0x007c7820, 0x1d5); }
uint8_t* XB_THISCALL FxOpd5_Init(uint8_t* self, const uint8_t* src) { return derived_op(self, src, false, 0x007c7aa0, 0xd5); }
uint8_t* XB_THISCALL FxOp15b_Init(uint8_t* self, const uint8_t* src) { return derived_op(self, src, true, 0x007c7d20, 0x15b); }
uint8_t* XB_THISCALL FxOp5b_Init(uint8_t* self, const uint8_t* src) { return derived_op(self, src, false, 0x007c7fa0, 0x5b); }

void XB_THISCALL FxEffect_ComputeStageSizes(uint8_t* self) {
    uint8_t* stage = at<uint8_t*>(self, 0x50);
    uint8_t* const end = stage + at<int16_t>(self, 0x56) * 0x3c;
    uint32_t total = 0;
    for (; stage != end; stage += 0x3c) {
        if (const uint32_t list = at<uint32_t>(stage, 0x14))
            FxActionList_CollectFormat(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(list)), stage + 0x18);
        if (const uint32_t renderer = at<uint32_t>(stage, 0x10))
            FxVertexFormat_Add(stage + 0x18, reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(renderer))[1]);
        FxVertexFormat_Add(stage + 0x18, 0x600);
        total += at<uint32_t>(stage, 0x1c) * at<uint32_t>(stage, 0);
    }
    at<uint32_t>(self, 0x64) = total;
}

uint8_t* XB_STDCALL FxStageTable_New(uint32_t count) {
    uint8_t* memory = fx_alloc_no_context(count * 0x3c, 4);
    if (!memory) return nullptr;
    for (uint8_t* p = memory; p != memory + count * 0x3c; p += 0x3c) {
        if (p) init_stage(p);
    }
    return memory;
}

uint32_t XB_THISCALL FxStageArray_Resize(uint8_t* self, int32_t count) {
    if (count <= at<int16_t>(self, 4)) {
        const int32_t old = at<int16_t>(self, 6);
        if (count > old) {
            // begin and end once (the elements can overlap self: random inputs)
            uint8_t* const data = at<uint8_t*>(self, 0);
            for (uint8_t* p = data + old * 0x3c, *end = data + count * 0x3c; p != end; p += 0x3c) {
                if (p) init_stage(p);
            }
        }
        at<int16_t>(self, 6) = static_cast<int16_t>(count);
        return 1;
    }
    uint8_t* memory = FxStageTable_New(static_cast<uint32_t>(count));
    if (!memory) return 0;
    for (int32_t i = 0; i < at<int16_t>(self, 6); ++i)
        std::memcpy(memory + i * 0x3c, at<uint8_t*>(self, 0) + i * 0x3c, 0x3c);
    replace_block(self, memory, static_cast<int16_t>(count), static_cast<int16_t>(count));
    return 1;
}

void XB_THISCALL FxEffect_SetStageCount(uint8_t* self, int32_t count) {
    const int32_t old = at<int16_t>(self, 0x56);
    if (count < old && old > 0) {
        for (int32_t i = 0; i < old; ++i) {
            uint8_t* stage = at<uint8_t*>(self, 0x50) + i * 0x3c;
            if (at<uint32_t>(stage, 0x10)) {
                destroy_stage_renderer(stage);
                fx_free(at<uint32_t>(stage, 0x10));
            }
            if (at<uint32_t>(stage, 0x14)) {
                destroy_stage_list(at<uint32_t>(stage, 0x14));
                fx_free(at<uint32_t>(stage, 0x14));
            }
        }
    }
    FxStageArray_Resize(self + 0x50, count);
    if (count > title_global<int32_t>(0x00909b90)) title_global<int32_t>(0x00909b90) = count;
    FxEffect_ComputeStageSizes(self);
}

void XB_THISCALL FxEffect_SetStage(uint8_t* self, int32_t index, uint32_t count, float a, float b, uint32_t renderer,
                                   uint32_t list, uint32_t flag) {
    const int32_t offset = index * 0x3c;
    at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0) = count;
    at<float>(at<uint8_t*>(self, 0x50) + offset, 4) = a;
    at<float>(at<uint8_t*>(self, 0x50) + offset, 8) = b;
    uint32_t& bits = at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0xc);
    bits = (flag ? 2u : 0u) | (bits & ~2u);
    if (at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x10)) {
        destroy_stage_renderer(at<uint8_t*>(self, 0x50) + offset);
        fx_free(at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x10));
    }
    at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x10) = renderer;
    if (at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x14)) {
        destroy_stage_list(at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x14));
        fx_free(at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x14));
    }
    at<uint32_t>(at<uint8_t*>(self, 0x50) + offset, 0x14) = list;
    FxEffect_ComputeStageSizes(self);
}

void XB_THISCALL FxEffect_SetEmitterCount(uint8_t* self, int32_t count) {
    FxEmitterTable_Resize(self + 0x58, count);
    if (count > title_global<int32_t>(0x00909b94)) title_global<int32_t>(0x00909b94) = count;
}

namespace {

// The renderer creators' parameter record from the effect file's parameter list (16-byte
// entries, value in word 1): fixed slots first, then by the extra count: 1 -> +0x24; 2 -> +0x28,
// +0x2c; 3 -> +0x24, +0x28, +0x2c; 4 -> those and the colour (FxOp_SetColor14). Colour default
// 1, 1, 1, 1; everything else 0.
void fill_renderer_record(uint8_t* record, const uint8_t* list, bool grid) {
    const auto* entries = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(list, 0)));
    const auto value = [entries](int i) { return entries[i * 4 + 1]; };
    const float one = title_global<float>(0x0083eb20);
    for (int i = 0; i < 4; ++i) at<float>(record, 0x14 + i * 4) = one;
    at<uint32_t>(record, 0) = value(0);
    at<uint32_t>(record, 4) = value(1);
    int first;
    if (grid) {
        at<uint32_t>(record, 0x30) = value(2);
        at<uint32_t>(record, 0x34) = value(3);
        std::memcpy(record + 8, &entries[4 * 4 + 1], 12);
        first = 5;
    } else {
        std::memcpy(record + 8, &entries[2 * 4 + 1], 12);
        first = 3;
    }
    const int extra = at<int16_t>(list, 6) - first;
    switch (extra) {
    case 1:
        at<uint32_t>(record, 0x24) = value(first);
        break;
    case 2:
        at<uint32_t>(record, 0x28) = value(first);
        at<uint32_t>(record, 0x2c) = value(first + 1);
        break;
    case 3:
    case 4:
        at<uint32_t>(record, 0x24) = value(first);
        at<uint32_t>(record, 0x28) = value(first + 1);
        at<uint32_t>(record, 0x2c) = value(first + 2);
        if (extra == 4) FxOp_SetColor14(record, 0, value(first + 3), 0, 0);
        break;
    default: break;
    }
}

uint8_t* new_renderer(const uint8_t* list, bool grid, uint8_t* (XB_THISCALL* init)(uint8_t*, const uint8_t*)) {
    alignas(16) uint8_t record[0x40] = {};
    fill_renderer_record(record, list, grid);
    uint8_t* self = fx_alloc(grid ? 0x50 : 0x40, 0x10);
    if (!self) return nullptr;
    return init(self, record);
}

}  // namespace

uint8_t* XB_CDECL FxRenderer_NewOp53(const uint8_t* list) { return new_renderer(list, false, &FxOp53_Init); }
uint8_t* XB_CDECL FxRenderer_NewOp5b(const uint8_t* list) { return new_renderer(list, false, &FxOp5b_Init); }
uint8_t* XB_CDECL FxRenderer_NewOpd5(const uint8_t* list) { return new_renderer(list, false, &FxOpd5_Init); }
uint8_t* XB_CDECL FxRenderer_NewOpdd(const uint8_t* list) { return new_renderer(list, false, &FxOpdd_Init); }
uint8_t* XB_CDECL FxRenderer_NewOpGrid(const uint8_t* list) { return new_renderer(list, true, &FxOpGrid_Init); }
uint8_t* XB_CDECL FxRenderer_NewOp15b(const uint8_t* list) { return new_renderer(list, true, &FxOp15b_Init); }
uint8_t* XB_CDECL FxRenderer_NewOp9d5(const uint8_t* list) { return new_renderer(list, true, &FxOp9d5_Init); }
uint8_t* XB_CDECL FxRenderer_NewOp1dd(const uint8_t* list) { return new_renderer(list, true, &FxOp1dd_Init); }

uint8_t* XB_CDECL FxRenderer_NewShrimp(const uint8_t* list) {
    // Nine parameter words (entries 0..8), the colour from entry 9 at +0x24 (default 1, 1, 1, 1).
    const auto* entries = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(list, 0)));
    alignas(16) uint8_t record[0x40] = {};
    const float one = title_global<float>(0x0083eb20);
    for (int i = 0; i < 4; ++i) at<float>(record, 0x24 + i * 4) = one;
    for (int i = 0; i < 9; ++i) at<uint32_t>(record, i * 4) = entries[i * 4 + 1];
    FxOp_SetColor24(record, 0, entries[9 * 4 + 1], 0, 0);
    uint8_t* self = fx_alloc(0x40, 0x10);
    if (!self) return nullptr;
    return FxOp1d5_Init(self, record);
}

namespace {

// repz cmpsb over the name's length including its terminator.
bool same_name(const char* a, uint32_t title_string, uint32_t length) {
    const auto* b = reinterpret_cast<const char*>(static_cast<uintptr_t>(title_string));
    for (uint32_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

}  // namespace

uint8_t* XB_CDECL FX_CreateRenderer(const char* name, const uint32_t* id, const uint8_t* list) {
    // By four-character id when there is one, else by class name.
    if (id && *id) {
        switch (*id) {
        case 0x426c626f: return FxRenderer_NewOp53(list);    // "Blbo" billboard
        case 0x43426c62: return FxRenderer_NewOp5b(list);    // coloured billboard
        case 0x52656374: return FxRenderer_NewOpd5(list);    // "Rect"
        case 0x43526374: return FxRenderer_NewOpdd(list);    // coloured rectangle
        case 0x55564152: return FxRenderer_NewOpGrid(list);  // "UVAR"
        case 0x43555641: return FxRenderer_NewOp15b(list);   // coloured UVA
        case 0x55565265: return FxRenderer_NewOp9d5(list);   // UVA rectangle
        case 0x63555672: return FxRenderer_NewOp1dd(list);   // coloured UVA rectangle
        case 0x534d7368: return FxRenderer_NewOp31(reinterpret_cast<const uint32_t* const*>(list));  // simple mesh
        case 0x53686d70: return FxRenderer_NewShrimp(list);
        default: return nullptr;
        }
    }
    if (same_name(name, 0x007c5d24, 0x12)) return FxRenderer_NewOp53(list);
    if (same_name(name, 0x007c5d0c, 0x17)) return FxRenderer_NewOp5b(list);
    if (same_name(name, 0x007c5cf8, 0x12)) return FxRenderer_NewOpd5(list);
    if (same_name(name, 0x007c5ce0, 0x17)) return FxRenderer_NewOpdd(list);
    if (same_name(name, 0x007c5cd4, 0xc)) return FxRenderer_NewOpGrid(list);
    if (same_name(name, 0x007c5cc0, 0x11)) return FxRenderer_NewOp15b(list);
    if (same_name(name, 0x007c5ca8, 0x15)) return FxRenderer_NewOp9d5(list);
    if (same_name(name, 0x007c5c8c, 0x1a)) return FxRenderer_NewOp1dd(list);
    if (same_name(name, 0x007c5c78, 0x13)) return FxRenderer_NewOp31(reinterpret_cast<const uint32_t* const*>(list));
    if (same_name(name, 0x007c5c68, 0xf)) return FxRenderer_NewShrimp(list);
    return nullptr;  // "_NullRenderer" and unknown names
}

uint32_t XB_STDCALL FxNull_Query(uint32_t, uint32_t, uint32_t) { return 0; }

void XB_THISCALL FxSystem_SetParamFloat(uint8_t* self, int32_t index, float value) {
    at<float>(at<uint8_t*>(self, 0x5c), index * 12 + 8) = value;
}

float XB_THISCALL FxSystem_GetEmitterParam(const uint8_t* self, int32_t index) {
    const uint8_t* effect = at<uint8_t*>(self, 0x48);
    const uint8_t* entry = at<uint8_t*>(effect, 0x58) + index * 0x2c;
    const uint8_t* stage = at<uint8_t*>(effect, 0x50) + at<int32_t>(entry, 0x20) * 0x3c;
    const uint32_t* actions = at<uint32_t*>(at<uint8_t*>(stage, 0x14), 0);
    const uint8_t* action = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(actions[at<int32_t>(entry, 0x24)]));
    return at<float*>(action, 4)[at<int32_t>(entry, 0x28)];
}

void XB_THISCALL FxEffect_ClearFlagC0(uint8_t* self) { at<uint32_t>(self, 0xc0) = 0; }
void XB_THISCALL FxEffect_SetFlagC0(uint8_t* self) { at<uint32_t>(self, 0xc0) = 1; }

int32_t XB_THISCALL FxEffect_AddStage(uint8_t* self) {
    const int32_t old = at<int16_t>(self, 0x56);
    FxStageArray_Resize(self + 0x50, old + 1);
    if (at<int16_t>(self, 0x56) > title_global<int32_t>(0x00909b90)) title_global<int32_t>(0x00909b90) = at<int16_t>(self, 0x56);
    return old;
}

int32_t XB_THISCALL FxEffect_AddEmitter(uint8_t* self) {
    const int32_t old = at<int16_t>(self, 0x5e);
    FxEmitterTable_Resize(self + 0x58, old + 1);
    if (at<int16_t>(self, 0x5e) > title_global<int32_t>(0x00909b94)) title_global<int32_t>(0x00909b94) = at<int16_t>(self, 0x5e);
    return old;
}

void XB_CDECL FxNop_97190() {}
void XB_CDECL FxNop_9b960() {}

void XB_CDECL FxSet_8bb860(uint32_t value) { title_global<uint32_t>(0x008bb860) = value; }
void XB_CDECL FxSet_8bb864(uint16_t value) { title_global<uint16_t>(0x008bb864) = value; }
void XB_CDECL FxSet_8bb866(uint16_t value) { title_global<uint16_t>(0x008bb866) = value; }
void XB_CDECL FxSet_8bb868(uint16_t value) { title_global<uint16_t>(0x008bb868) = value; }
void XB_CDECL FxSet_8bb990(uint32_t value) { title_global<uint32_t>(0x008bb990) = value; }

uint32_t XB_THISCALL FxPool_SlotAddress(const uint8_t* self, uint32_t index) {
    if (at<uint8_t*>(self, 0x1c)[index * 8 + 7] != 1) return 0;
    return at<uint32_t>(self, 0x14) * index + at<uint32_t>(self, 0x18);
}

uint32_t XB_CDECL FxPools_Clear() { return apsPoolSet_Clear(title_global<ApsArray*>(0x00909f08)); }

void XB_THISCALL FxOp_SetTextures(uint8_t* self, uint32_t a, uint32_t b, uint32_t c) {
    if (a && b && c) {
        at<uint32_t>(self, 0x74) = a;
        at<uint32_t>(self, 0x70) = b;
        at<uint32_t>(self, 0x78) = c;
        return;
    }
    at<uint32_t>(self, 0x74) = 0;
    at<uint32_t>(self, 0x70) = 0;
    at<uint32_t>(self, 0x78) = 0;
}

namespace {

void init_op_shaders(uint32_t vertex_shader, uint32_t declaration, uint32_t vertex_program, uint32_t pixel_shader,
                     uint32_t pixel_program) {
    nglVertexShader_Init(&title_global<uint32_t>(vertex_shader), declaration, title_global<uint32_t>(vertex_program));
    nglPixelShader_Init(&title_global<uint32_t>(pixel_shader), title_global<uint32_t>(pixel_program));
}

}  // namespace

void XB_CDECL FxOp31_InitShaders() { init_op_shaders(0x00909f7c, 0x008bbc90, 0x008bbeb4, 0x00909f84, 0x008bbfa8); }
void XB_CDECL FxOp1d5_InitShaders() { init_op_shaders(0x00909f8c, 0x008bbfe0, 0x008bc1c4, 0x00909f94, 0x008bc2b8); }
void XB_CDECL FxOpdd_InitShaders() { init_op_shaders(0x00909fa0, 0x008bbfe0, 0x008bc3d4, 0x00909f9c, 0x008bc4c8); }
void XB_CDECL FxOp1dd_InitShaders() { init_op_shaders(0x00909fb0, 0x008bbfe0, 0x008bc614, 0x00909fac, 0x008bc708); }
void XB_CDECL FxOp9d5_InitShaders() { init_op_shaders(0x00909fbc, 0x008bbfe0, 0x008bc854, 0x00909fc4, 0x008bc948); }
void XB_CDECL FxOpd5_InitShaders() { init_op_shaders(0x00909fd0, 0x008bbfe0, 0x008bca64, 0x00909fcc, 0x008bcb58); }
void XB_CDECL FxOp15b_InitShaders() { init_op_shaders(0x00909fe0, 0x008bbfe0, 0x008bcc74, 0x00909fdc, 0x008bcd68); }
void XB_CDECL FxOp5b_InitShaders() { init_op_shaders(0x00909ff0, 0x008bbfe0, 0x008bce84, 0x00909fec, 0x008bcf78); }
void XB_CDECL FxOpGrid_InitShaders() { init_op_shaders(0x0090a000, 0x008bbfe0, 0x008bd094, 0x00909ffc, 0x008bd188); }
void XB_CDECL FxOp53_InitShaders() { init_op_shaders(0x0090a00c, 0x008bbfe0, 0x008bd274, 0x0090a014, 0x008bd368); }

namespace {

// 1 / sqrt(lane0 of the squared lanes' (x + y) + z) by RSQRTSS and one Newton step on |s|
// (((3 - r r |s|) r) 0.5); 0 when the sum is 0.
float rsqrt_newton(const Lanes& squares) {
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    if (sum == 0.0f) return title_global<float>(0x007c9140);
    const float magnitude = abs_bits(sum);
    const float r = platform_rsqrt_estimate(magnitude);
    return ((title_global<float>(0x007c9100) - (r * r) * magnitude) * r) * title_global<float>(0x007c9110);
}

Lanes normalized(const Lanes& v) {
    const Lanes squares = mul(v, v);
    platform_keep(squares);
    return mul(v, rsqrt_newton(squares));
}

// The disc/circle domains (vtables 0x007c93a8 / 0x007c93c0): centre and radius, the normal
// and two in-plane axes at +0x20 / +0x30.
uint8_t* plane_domain(uint8_t* self, uint32_t vtable, const float* centre, const float* normal, float radius) {
    at<uint32_t>(self, 0) = vtable;
    std::memcpy(self + 0x10, centre, 12);
    at<float>(self, 0x1c) = radius;
    const Lanes n = normalized(lanes(normal));
    static const float kSwapXZ[4] = {-1.0f, 0.0f, 1.0f, 0.0f};  // 0x007c92b0
    static const float kSwapXY[4] = {1.0f, -1.0f, 0.0f, 0.0f};  // 0x007c92c0
    Lanes u = mul(Lanes{{n.v[2], n.v[1], n.v[0], n.v[3]}}, lanes(kSwapXZ));
    std::memcpy(self + 0x20, u.v, 16);
    const Lanes u_squares = mul(u, u);
    platform_keep(u_squares);
    if (title_global<float>(0x0083eca0) > (u_squares.v[0] + u_squares.v[1]) + u_squares.v[2]) {
        u = mul(Lanes{{n.v[1], n.v[0], n.v[2], n.v[3]}}, lanes(kSwapXY));
        std::memcpy(self + 0x20, u.v, 16);
    }
    u = normalized(lanes(reinterpret_cast<const float*>(self + 0x20)));
    std::memcpy(self + 0x20, u.v, 16);
    const Lanes a = mul(Lanes{{n.v[1], n.v[2], n.v[0], n.v[3]}}, u);
    const Lanes b = mul(n, Lanes{{u.v[1], u.v[2], u.v[0], u.v[3]}});
    const Lanes c = sub(b, a);
    const Lanes v = {{c.v[1], c.v[2], c.v[0], c.v[3]}};
    std::memcpy(self + 0x30, v.v, 16);
    const Lanes w = normalized(lanes(reinterpret_cast<const float*>(self + 0x30)));
    std::memcpy(self + 0x30, w.v, 16);
    return self;
}

}  // namespace

uint8_t* XB_THISCALL FxDiscDomain_Init(uint8_t* self, const float* centre, const float* normal, float radius) {
    return plane_domain(self, 0x007c93a8, centre, normal, radius);
}

uint8_t* XB_THISCALL FxCircleDomain_Init(uint8_t* self, const float* centre, const float* normal, float radius) {
    return plane_domain(self, 0x007c93c0, centre, normal, radius);
}

namespace {

enum class Domain { None, Line, Box3, Box1, Bump1, Bump3, Point1, Point3, Rgb, Circle, Disc, SphereSurface, Sphere,
                    ZHemisphere, YHemisphere, FrameRange, Frame };

Domain domain_by_id(uint32_t id) {
    switch (id) {
    case 0x4c696e65: return Domain::Line;           // "Line"
    case 0x33644275: return Domain::Bump3;          // "3dBu"
    case 0x31644278: return Domain::Box1;           // "1dBx"
    case 0x31644275: return Domain::Bump1;          // "1dBu"
    case 0x31645074: return Domain::Point1;         // "1dPt"
    case 0x4372636c: return Domain::Circle;         // "Crcl"
    case 0x33644278: return Domain::Box3;           // "3dBx"
    case 0x33645074: return Domain::Point3;         // "3dPt"
    case 0x44697363: return Domain::Disc;           // "Disc"
    case 0x55764652: return Domain::FrameRange;     // "UvFR"
    case 0x53705375: return Domain::SphereSurface;  // "SpSu"
    case 0x52676242: return Domain::Rgb;            // "RgbB"
    case 0x53706865: return Domain::Sphere;         // "Sphe"
    case 0x5576466d: return Domain::Frame;          // "UvFm"
    case 0x59485370: return Domain::YHemisphere;    // "YHSp"
    case 0x5a485370: return Domain::ZHemisphere;    // "ZHSp"
    default: return Domain::None;                   // includes "NULL"
    }
}

Domain domain_by_name(const char* name) {
    static const struct {
        uint32_t text, length;
        Domain domain;
    } kNames[] = {
        {0x007c5c4c, 0xc, Domain::None},       {0x007c5c40, 0xc, Domain::Box1},
        {0x007c5c34, 0xc, Domain::Box3},       {0x007c5c24, 0xd, Domain::Rgb},
        {0x007c5c14, 0xe, Domain::FrameRange}, {0x007c5c04, 0xe, Domain::Point1},
        {0x007c5bf4, 0xe, Domain::Point3},     {0x007c5be8, 0x9, Domain::Frame},
        {0x007c5bdc, 0xb, Domain::Disc},       {0x007c5bcc, 0xd, Domain::Circle},
        {0x007c5bbc, 0xd, Domain::Sphere},     {0x007c5ba8, 0x12, Domain::YHemisphere},
        {0x007c5b94, 0x12, Domain::ZHemisphere}, {0x007c5b80, 0x14, Domain::SphereSurface},
        {0x007c5b70, 0xd, Domain::Line},       {0x007c5b60, 0xd, Domain::Bump1},
        {0x007c5b50, 0xd, Domain::Bump3},
    };
    for (const auto& entry : kNames) {
        if (same_name(name, entry.text, entry.length)) return entry.domain;
    }
    return Domain::None;
}

}  // namespace

uint8_t* XB_CDECL FxDomain_Create(const char* name, const uint32_t* id, const uint8_t* list) {
    const Domain domain = (id && *id) ? domain_by_id(*id) : domain_by_name(name);
    const auto* e = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(list, 0)));
    const auto value = [e](int i) {
        float f;
        std::memcpy(&f, &e[i * 4 + 1], 4);
        return f;
    };
    alignas(16) uint32_t t0[4];
    alignas(16) uint32_t t1[4];
    switch (domain) {
    case Domain::None: return nullptr;
    case Domain::Bump1: return FxRange_New5b20(value(0), value(1));
    case Domain::Box1: return FxRange_New5ac0(value(0), value(1));
    case Domain::Point1: return FxConst_New(value(0));
    case Domain::Bump3:
        std::memcpy(t1, &e[4 + 1], 12);
        std::memcpy(t0, &e[1], 12);
        return FxVectorRange_New5b38(reinterpret_cast<const float*>(t0), reinterpret_cast<const float*>(t1));
    case Domain::Point3: return FxConstVector_New(reinterpret_cast<const float*>(FxVec3_FromWords1(t0, e)));
    case Domain::Box3:
    case Domain::Rgb: {
        const uint32_t* b = FxVec3_FromWords1(t1, e + 4);
        const uint32_t* a = FxVec3_FromWords1(t0, e);
        return FxVectorRange_New5ad8(reinterpret_cast<const float*>(a), reinterpret_cast<const float*>(b));
    }
    case Domain::Circle:
    case Domain::Disc: {
        uint8_t* self = fx_alloc(0x40, 0x10);
        if (!self) return nullptr;
        const float radius = value(2);
        const uint32_t* normal = FxVec4_FromWords1(t1, e + 4);
        const uint32_t* centre = FxVec4_FromWords1(t0, e);
        return domain == Domain::Circle
                   ? FxCircleDomain_Init(self, reinterpret_cast<const float*>(centre), reinterpret_cast<const float*>(normal), radius)
                   : FxDiscDomain_Init(self, reinterpret_cast<const float*>(centre), reinterpret_cast<const float*>(normal), radius);
    }
    case Domain::Line: {
        uint8_t* self = fx_alloc(0x30, 0x10);
        if (!self) return nullptr;
        const uint32_t b[4] = {e[5], e[6], e[7], e[5]};
        const uint32_t a[4] = {e[1], e[2], e[3], e[1]};
        std::memcpy(t1, b, 16);
        std::memcpy(t0, a, 16);
        return FxSegmentOp_Init(self, reinterpret_cast<const float*>(t0), reinterpret_cast<const float*>(t1));
    }
    case Domain::SphereSurface:
    case Domain::Sphere:
    case Domain::ZHemisphere:
    case Domain::YHemisphere: {
        uint8_t* self = fx_alloc(0x20, 0x10);
        if (!self) return nullptr;
        const float radius = value(1);
        const float* centre = reinterpret_cast<const float*>(FxVec4_FromWords1(t0, e));
        if (domain == Domain::SphereSurface) return FxVectorOp_Init9378(self, centre, radius);
        if (domain == Domain::Sphere) return FxVectorOp_Init9330(self, centre, radius);
        if (domain == Domain::ZHemisphere) return FxVectorOp_Init9360(self, centre, radius);
        return FxVectorOp_Init9348(self, centre, radius);
    }
    case Domain::FrameRange: {
        const float a = static_cast<float>(static_cast<int32_t>(e[1]));
        const float b = static_cast<float>(static_cast<int32_t>(e[5]));
        uint8_t* self = fx_alloc(0xc, 4);
        if (!self) return nullptr;
        at<uint32_t>(self, 0) = 0x007c5ac0;
        ordered(a, b, &at<float>(self, 4), &at<float>(self, 8));
        return self;
    }
    case Domain::Frame: {
        const float a = static_cast<float>(static_cast<int32_t>(e[1]));
        uint8_t* self = fx_alloc(8, 4);
        if (!self) return nullptr;
        at<uint32_t>(self, 0) = 0x007c5af0;
        at<float>(self, 4) = a;
        return self;
    }
    }
    return nullptr;
}

namespace {

using ActionInit = uint8_t* XB_THISCALL (*)(uint8_t*);

ActionInit action_by_id(uint32_t id) {
    switch (id) {
    case 0x4146494f: return &FxAction8e60_Init;  // "AFIO"
    case 0x41544558: return &FxAction8ff0_Init;  // "ATEX"
    case 0x41544559: return &FxAction9004_Init;  // "ATEY"
    case 0x4154455a: return &FxAction9018_Init;  // "ATEZ"
    case 0x41564472: return &FxAction8f8c_Init;  // "AVDr"
    case 0x416c4664: return &FxAction8e4c_Init;  // "AlFd"
    case 0x416e6754: return &FxAction8fdc_Init;  // "AngT"
    case 0x42727374: return &FxAction8e08_Init;  // "Brst"
    case 0x436c5368: return &FxAction8f64_Init;  // "ClSh"
    case 0x444c4174: return &FxAction9054_Init;  // "DLAt"
    case 0x45536348: return &FxAction8eec_Init;  // "EScH"
    case 0x45536357: return &FxAction8ec4_Init;  // "EScW"
    case 0x45785363: return &FxAction8e9c_Init;  // "ExSc"
    case 0x466f7263: return &FxAction8f50_Init;  // "Forc"
    case 0x4b546175: return &FxAction9068_Init;  // "KTau"
    case 0x4c417472: return &FxAction9040_Init;  // "LAtr"
    case 0x4c536348: return &FxAction8ed8_Init;  // "LScH"
    case 0x4c536357: return &FxAction8eb0_Init;  // "LScW"
    case 0x4c696665: return &FxAction8e38_Init;  // "Life"
    case 0x4c6e5363: return &FxAction8e88_Init;  // "LnSc"
    case 0x4d467856: return &FxAction8f3c_Init;  // "MFxV"
    case 0x4d6f7665: return &FxAction8f00_Init;  // "Move"
    case 0x4f4d6f76: return &FxAction8f14_Init;  // "OMov"
    case 0x50417472: return &FxAction902c_Init;  // "PAtr"
    case 0x50734d76: return &FxAction8f28_Init;  // "PsMv"
    case 0x5246494f: return &FxAction8e74_Init;  // "RFIO"
    case 0x5253706e: return &FxAction8e20_Init;  // "RSpn"
    case 0x53704163: return &FxAction907c_Init;  // "SpAc"
    case 0x53704465: return &FxAction9090_Init;  // "SpDe"
    case 0x53726341: return &FxAction8df0_Init;  // "SrcA"
    case 0x5472616a: return &FxAction90a4_Init;  // "Traj"
    case 0x55564672: return &FxAction8fc8_Init;  // "UVFr"
    case 0x56415644: return &FxAction8fa0_Init;  // "VAVD"
    case 0x566c4472: return &FxAction8f78_Init;  // "VlDr"
    case 0x57505266: return &FxAction8fb4_Init;  // "WPRf"
    default: return nullptr;
    }
}

ActionInit action_by_name(const char* name) {
    static const struct {
        uint32_t text, length;
        ActionInit init;
    } kNames[] = {
        {0x007c5ab0, 13, &FxAction8df0_Init},
        {0x007c5aa4, 12, &FxAction8e08_Init},
        {0x007c5a94, 15, &FxAction8e38_Init},
        {0x007c5a84, 16, &FxAction8e4c_Init},
        {0x007c5a6c, 21, &FxAction8e60_Init},
        {0x007c5a50, 27, &FxAction8e74_Init},
        {0x007c5a3c, 18, &FxAction8e88_Init},
        {0x007c5a24, 23, &FxAction8e9c_Init},
        {0x007c5a0c, 23, &FxAction8eb0_Init},
        {0x007c59f0, 28, &FxAction8ec4_Init},
        {0x007c59d8, 24, &FxAction8ed8_Init},
        {0x007c59b8, 29, &FxAction8eec_Init},
        {0x007c59a4, 19, &FxAction8f28_Init},
        {0x007c5990, 17, &FxAction8f14_Init},
        {0x007c5984, 11, &FxAction8f00_Init},
        {0x007c5968, 26, &FxAction8f3c_Init},
        {0x007c595c, 12, &FxAction8f50_Init},
        {0x007c5948, 17, &FxAction8f64_Init},
        {0x007c5934, 19, &FxAction8f78_Init},
        {0x007c5918, 26, &FxAction8f8c_Init},
        {0x007c58f8, 32, &FxAction8fa0_Init},
        {0x007c58dc, 27, &FxAction8fb4_Init},
        {0x007c58c8, 19, &FxAction8fc8_Init},
        {0x007c58ac, 25, &FxAction8fdc_Init},
        {0x007c5890, 25, &FxAction8ff0_Init},
        {0x007c5874, 25, &FxAction9004_Init},
        {0x007c5858, 25, &FxAction9018_Init},
        {0x007c5840, 21, &FxAction902c_Init},
        {0x007c582c, 20, &FxAction9040_Init},
        {0x007c5810, 25, &FxAction9054_Init},
        {0x007c5800, 15, &FxAction9068_Init},
        {0x007c57f4, 12, &FxAction907c_Init},
        {0x007c57e0, 19, &FxAction9090_Init},
        {0x007c57cc, 17, &FxAction90a4_Init},
        {0x007c57b8, 18, &FxAction8e20_Init},
    };
    for (const auto& entry : kNames) {
        if (same_name(name, entry.text, entry.length)) return entry.init;
    }
    return nullptr;
}

}  // namespace

uint8_t* XB_CDECL apsParseActions(const char* name, const uint32_t* id) {
    // A particle action by four-character id (when set) or class name: a 0x1c-byte object from
    // the effect allocator handed to its constructor. Null for unknown kinds or failure.
    const ActionInit init = (id && *id) ? action_by_id(*id) : action_by_name(name);
    if (!init) return nullptr;
    uint8_t* self = fx_alloc(0x1c, 4);
    if (!self) return nullptr;
    return init(self);
}

namespace {

// The effect file's name strings: ReadString (at most 0x7f), its length byte stored at the
// buffer's end (+0x7f) as the original does, then terminated.
void read_name(uint32_t* stream, char* buffer) {
    int32_t length = 0;
    buffer[0x7f] = 0;
    buffer[0] = 0;
    Stream_ReadString(stream, buffer, &length, 0x7f);
    buffer[0x7f] = static_cast<char>(length);
    buffer[static_cast<uint8_t>(length)] = 0;
}

void hash_string(uint32_t* out, const char* text) {
    title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(out, text);  // HashStr_Init
}

void free_list(uint8_t* list) {
    if (!at<uint32_t>(list, 0)) return;
    fx_free_no_context(at<uint32_t>(list, 0));
    at<uint32_t>(list, 0) = 0;
    at<int16_t>(list, 4) = 0;
    at<int16_t>(list, 6) = 0;
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_LoadEffectFile_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FX_LoadEffectFile) "addl $12, %esp\n"
                   "ret")

uint8_t* XB_CDECL FX_LoadEffectFile(uint32_t* stream, const char* name, uint32_t zone) {
    using HeapAlloc = uint8_t* XB_CDECL(uint32_t, uint32_t);
    uint8_t* memory = title_function<HeapAlloc>(0x00042c60)(0x10, 0xd0);  // Heap_AllocAligned
    uint8_t* effect = memory ? FxEffect_Init(memory, name) : nullptr;
    Stream_ReadInt(stream);
    const float version = Stream_ReadFloat(stream);
    float matrix[16];
    for (float& value : matrix) value = Stream_ReadFloat(stream);
    FxEffect_SetMatrix(effect, reinterpret_cast<const uint32_t*>(matrix));
    const float a = Stream_ReadFloat(stream);
    const float b = Stream_ReadFloat(stream);
    const float c = Stream_ReadFloat(stream);
    at<float>(effect, 0x4c) = Stream_ReadFloat(stream);
    at<float>(effect, 0x40) = a;
    at<float>(effect, 0x44) = b;
    at<float>(effect, 0x48) = c;
    int32_t particle_limit = 0x40;
    if (version >= title_global<float>(0x0083f000)) {  // 1.02
        particle_limit = static_cast<int32_t>(Stream_ReadInt(stream));
        at<int32_t>(effect, 0x68) = particle_limit < 0 ? -particle_limit : particle_limit;
    }
    const int32_t stages = static_cast<int32_t>(Stream_ReadInt(stream));
    FxEffect_SetStageCount(effect, stages);
    // The parameter buffers keep what earlier parameters left in their unused words (the
    // original's stack slots; zero at the start of a test).
    alignas(16) uint32_t renderer_parameter[4] = {};
    alignas(16) uint32_t domain_parameter[4] = {};
    for (int32_t stage = 0; stage < stages; ++stage) {
        const uint32_t count = Stream_ReadInt(stream);
        const float rate = Stream_ReadFloat(stream);
        const float life = Stream_ReadFloat(stream);
        const uint32_t flag = Stream_ReadInt(stream);
        uint32_t renderer_id = Stream_ReadUInt(stream);
        Stream_ReadFloat(stream);
        uint8_t list[8] = {};
        const int32_t parameters = static_cast<int32_t>(Stream_ReadInt(stream));
        FxArray16_Reserve(list, parameters);
        for (int32_t i = 0; i < parameters; ++i) {
            FX_ParseEffectTexture(stream, renderer_parameter);
            FxArray16_Push(list, renderer_parameter);
        }
        uint8_t* renderer = FX_CreateRenderer(nullptr, &renderer_id, list);
        at<int32_t>(renderer, 8) = particle_limit;
        free_list(list);
        uint8_t* memory_list = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0xc);  // Z_Malloc
        uint8_t* elements = memory_list ? FxElementList_Init(memory_list) : nullptr;
        const uint32_t actions = Stream_ReadInt(stream);
        for (uint32_t k = 0; k < actions; ++k) {
            uint32_t action_id = Stream_ReadUInt(stream);
            Stream_ReadFloat(stream);
            uint8_t* action = apsParseActions(nullptr, &action_id);
            const int32_t values = static_cast<int32_t>(Stream_ReadInt(stream));
            for (int32_t j = 0; j < values; ++j) {
                const uint32_t type = Stream_ReadInt(stream);
                if (type == 0) {
                    FxParamBlock_Set(action, static_cast<uint32_t>(j), Stream_ReadFloat(stream));
                } else if (type == 1) {
                    char text[0x80];
                    read_name(stream, text);
                    uint32_t hashed[8];
                    hash_string(hashed, text);
                    const uint32_t found = Stream_FindEffect(hashed, title_global<uint32_t>(0x0095f630), zone, 0, 0);
                    float bits;
                    std::memcpy(&bits, &found, 4);
                    FxParamBlock_Set(action, static_cast<uint32_t>(j), bits);
                } else {
                    return nullptr;
                }
            }
            const int32_t domains = static_cast<int32_t>(Stream_ReadInt(stream));
            for (int32_t r = 0; r < domains; ++r) {
                uint32_t domain_id = Stream_ReadUInt(stream);
                Stream_ReadFloat(stream);
                uint8_t domain_list[8] = {};
                const int32_t n = static_cast<int32_t>(Stream_ReadInt(stream));
                for (int32_t i = 0; i < n; ++i) {
                    FX_ParseEffectTexture(stream, domain_parameter);
                    FxArray16_Push(domain_list, domain_parameter);
                }
                uint8_t* domain = FxDomain_Create(nullptr, &domain_id, domain_list);
                FxParamBlock_SetSlot(action, r, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(domain)));
                free_list(domain_list);
            }
            apsElementList_Add(elements, action);
        }
        FxEffect_SetStage(effect, stage, count, rate, life, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(renderer)),
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(elements)), flag != 0);
    }
    const int32_t emitters = static_cast<int32_t>(Stream_ReadInt(stream));
    FxEffect_SetEmitterCount(effect, emitters);
    for (int32_t i = 0; i < emitters; ++i) {
        char text[0x80];
        read_name(stream, text);
        const uint32_t x = Stream_ReadInt(stream);
        const uint32_t y = Stream_ReadInt(stream);
        const uint32_t z = Stream_ReadInt(stream);
        uint32_t hashed[8];
        hash_string(hashed, text);
        FxEffect_SetEmitterEntry(effect, i, hashed, x, y, z);
    }
    FxEffect_ComputeStageSizes(effect);
    return effect;
}

PLATFORM_ASM_ENTRY(FX_LoadEffectFromStream_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FX_LoadEffectFromStream) "addl $20, %esp\n"
                   "ret $8")

uint32_t XB_CDECL FX_LoadEffectFromStream(int32_t size, const uint8_t* data, uint32_t zone, uint32_t streams,
                                          const char* name) {
    MemStream stream;
    MemStream_Init(&stream, data, size);
    uint8_t* effect = FX_LoadEffectFile(reinterpret_cast<uint32_t*>(&stream), name, zone);
    // EAX at the end: what MemStream_Destroy leaves - its flag byte (0 for this stream) in AL
    // over the last value: 0, or the effect slot's address.
    uint32_t result = 0;
    if (effect) {
        uint32_t hashed[8];
        Stream_HashFileName(name, hashed);
        const auto* zone_map = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(streams + 0x5c + zone * 4))));
        const uint32_t slot = ZoneEffectMap_Lookup(hashed[0], reinterpret_cast<const char*>(hashed + 1), zone_map);
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(slot)) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(effect));
        result = slot;
    }
    MemStream_Destroy(&stream);
    return (result & ~0xffu) | stream.flag;
}

uint8_t* XB_THISCALL FxPool_Init(uint8_t* self, uint32_t slots) {
    at<uint32_t>(self, 0) = 0;
    at<uint32_t>(self, 4) = 0;
    at<uint32_t>(self, 8) = 0;
    at<uint32_t>(self, 0x14) = 0;
    at<uint32_t>(self, 0x18) = 0;
    at<uint32_t>(self, 0xc) = 0x7fffffff;
    at<uint32_t>(self, 0x10) = 0xffffffff;
    at<uint32_t>(self, 0x1c) = slots;
    at<uint16_t>(self, 0x20) = 0xffff;
    return self;
}

uint32_t XB_THISCALL FxPool_SetSlotTag(uint8_t* self, int32_t address, int32_t tag) {
    const int32_t stride = at<int32_t>(self, 0x14);
    if (tag > stride) return 0;
    const int32_t index = (address - at<int32_t>(self, 0x18)) / stride;
    uint32_t* entry = reinterpret_cast<uint32_t*>(at<uint8_t*>(self, 0x1c) + index * 8);
    const uint32_t word = entry[1];
    if (tag == static_cast<int32_t>(word << 8) >> 8) return 0;
    entry[1] = ((word ^ static_cast<uint32_t>(tag)) & 0xffffff) ^ word;
    return static_cast<uint32_t>(address);
}

void XB_THISCALL FxPool_FreeSlotTable(uint8_t* self) {
    const uint32_t table = at<uint32_t>(self, 0x1c);
    if (table) {
        if (table != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self + 0x24))) fx_free(table);
        at<uint32_t>(self, 0x1c) = 0;
    }
    at<uint32_t>(self, 0x18) = 0;
}

namespace {

// The pool (of a pool set) whose slot range holds `address`.
uint8_t* pool_of(const uint8_t* set, int32_t address) {
    uint8_t* const* p = at<uint8_t* const*>(set, 0);
    uint8_t* const* const end = p + at<int16_t>(set, 6);
    for (; p != end; ++p) {
        uint8_t* pool = *p;
        const int32_t base = at<int32_t>(pool, 0x18);
        if (!base || address - base < 0) continue;
        if ((at<int32_t>(pool, 0) - 1) * at<int32_t>(pool, 0x14) - address + base >= 0) return pool;
    }
    return nullptr;
}

}  // namespace

uint32_t XB_THISCALL FxPools_Release(uint8_t* self, int32_t address) {
    if (!address) return 0;
    uint8_t* pool = pool_of(self, address);
    if (!pool) return 0;
    const int32_t index = (address - at<int32_t>(pool, 0x18)) / at<int32_t>(pool, 0x14);
    uint8_t* entry = at<uint8_t*>(pool, 0x1c) + index * 8;
    at<uint16_t>(entry, 2) = at<uint16_t>(pool, 0x20);
    at<uint16_t>(pool, 0x20) = static_cast<uint16_t>(index);
    at<uint32_t>(entry, 4) &= 0xff000000;
    entry[7] = 0;
    --at<int32_t>(pool, 4);
    return 1;
}

uint32_t XB_THISCALL FxPools_SetSlotTag(uint8_t* self, int32_t address, int32_t tag) {
    if (!address) return 0;
    uint8_t* pool = pool_of(self, address);
    if (!pool) return 0;
    return FxPool_SetSlotTag(pool, address, tag);
}

uint32_t XB_CDECL FxPools_ReleaseGlobal(int32_t address) { return FxPools_Release(title_global<uint8_t*>(0x00909f08), address); }

void XB_THISCALL FxPools_Destroy(uint8_t* self) {
    apsPoolSet_Clear(reinterpret_cast<ApsArray*>(self));
    if (at<uint32_t>(self, 0)) {
        fx_free_no_context(at<uint32_t>(self, 0));
        at<uint32_t>(self, 0) = 0;
        at<int16_t>(self, 4) = 0;
        at<int16_t>(self, 6) = 0;
    }
    title_global<uint32_t>(0x00909f08) = 0;
}

void XB_THISCALL FxSystems_FreeBlock(uint8_t* self) {
    if (at<uint32_t>(self, 4)) {
        fx_free(at<uint32_t>(self, 4));
        at<uint32_t>(self, 4) = 0;
    }
    title_global<uint32_t>(0x00909908) = 0;
}

float* XB_CDECL FxVec3_ScaledHalfExtent(float* out, const uint8_t* src) {
    const float k = title_global<float>(0x0083eaf4);  // 1.5
    const Lanes v = mul(lanes(reinterpret_cast<const float*>(src + 0x60)), k);
    platform_keep(v);
    out[0] = v.v[0];
    out[1] = v.v[1];
    out[2] = v.v[2];
    out[3] = title_global<float>(0x0083eb20);
    return out;
}

void XB_THISCALL FxNode_AddToScene(const uint8_t* self, uint8_t* node, float depth) {
    uint8_t* scene = reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c));
    const int32_t key = at<int32_t>(self, 8);
    if (key < 0) {
        at<int32_t>(node, 8) = -key;
        at<uint32_t>(node, 4) = at<uint32_t>(scene, 0x304);
        at<uint8_t*>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x304) = node;
        ++at<uint32_t>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x30c);
        return;
    }
    at<float>(node, 8) = depth;
    at<uint32_t>(node, 4) = at<uint32_t>(scene, 0x308);
    at<uint8_t*>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x308) = node;
    ++at<uint32_t>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x310);
}

uint8_t* XB_CDECL FxShaderClass_Create() {
    uint8_t* self = fx_alloc(0xc, 4);
    if (!self) return nullptr;
    at<uint32_t>(self, 0) = 0x007b3ec0;
    at<uint32_t>(self, 4) = title_global<uint32_t>(0x00909904);
    title_global<uint8_t*>(0x00909904) = self;
    at<uint32_t>(self, 0) = 0x007c66f4;
    return self;
}

uint32_t* XB_STDCALL FxSimpleShader_GetName(uint32_t* out) {
    return title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(out, "aeps_simple");
}

void XB_THISCALL FxEffectPool_Release(uint8_t* self) {
    uint8_t* slots = self + 8;
    FxPool_FreeSlotTable(slots);
    const uint32_t effects = at<uint32_t>(self, 0);
    for (int32_t n = at<int32_t>(self, 4); n > 0; --n) {
    }  // each element's destructor (0x00097190) is empty
    fx_free(effects);
    apsMemoryPool_FreeSlots(slots);
    title_global<uint32_t>(0x00909f64) = 0;
}

void XB_THISCALL FxPool_GetStats(const uint8_t* self, int32_t* stats) {
    std::memset(stats, 0, 0x12 * 4);
    const int32_t capacity = at<int32_t>(self, 0);
    if (!capacity) return;
    const int32_t used = at<int32_t>(self, 4);
    stats[0] = capacity;
    stats[1] = used;
    stats[11] = used * 100 / capacity;
    stats[2] = capacity - used;
    stats[12] = (capacity - used) * 100 / capacity;
    stats[3] = at<int32_t>(self, 8);
    stats[13] = stats[3] * 100 / capacity;
    stats[4] = at<int32_t>(self, 0x14);
    stats[5] = at<int32_t>(self, 0xc) == 0x7fffffff ? 0 : at<int32_t>(self, 0xc);
    stats[6] = at<int32_t>(self, 0x10) == -1 ? 0 : at<int32_t>(self, 0x10);
    const int32_t used_bytes = at<int32_t>(self, 0x14) * used;
    stats[8] = used_bytes;
    const int32_t total = at<int32_t>(self, 0x14) * capacity;
    stats[7] = total;
    stats[14] = used_bytes * 100 / total;
    stats[15] = stats[9] * 100 / total;
    stats[9] = total - used_bytes;
    int32_t slack = 0;
    const uint8_t* entry = at<uint8_t*>(self, 0x1c) + 4;
    for (int32_t i = 0; i < at<int32_t>(self, 0); ++i, entry += 8) {
        if (entry[3] == 1) slack += at<int32_t>(self, 0x14) - (static_cast<int32_t>(at<uint32_t>(entry, 0) << 8) >> 8);
    }
    stats[10] = slack;
    stats[16] = used_bytes ? slack * 100 / used_bytes : 0;
    stats[17] = slack * 100 / stats[7];
}

void XB_THISCALL FxPools_ResetStats(uint8_t* self) {
    uint8_t* const* p = at<uint8_t* const*>(self, 0);
    uint8_t* const* const end = p + at<int16_t>(self, 6);
    int32_t stats[0x12];
    for (; p != end; ++p) FxPool_GetStats(*p, stats);
    at<uint32_t>(self, 0x14) = 0;
    at<uint32_t>(self, 0xc) = 0;
    at<uint32_t>(self, 0x10) = 0;
    at<uint32_t>(self, 8) = 0;
}

void XB_CDECL FxPools_ResetGlobalStats() { FxPools_ResetStats(title_global<uint8_t*>(0x00909f08)); }

void XB_THISCALL FxAction_Compact(uint8_t* self) {
    ApsArray* list = reinterpret_cast<ApsArray*>(self + 0xc);
    uint8_t kept_storage[8] = {};
    ApsArray* kept = reinterpret_cast<ApsArray*>(kept_storage);
    const uint32_t* p = at<uint32_t*>(self, 0xc);
    const uint32_t* const end = p + at<int16_t>(self, 0x12);
    for (; p != end; ++p) {
        if (*p) apsPtrVector_PushBack(kept, p);
    }
    apsPtrVector_Resize(list, 0);
    const uint32_t* q = at<uint32_t*>(kept_storage, 0);
    const uint32_t* const kept_end = q + at<int16_t>(kept_storage, 6);
    for (; q != kept_end; ++q) {
        if (*q) apsPtrVector_PushBack(list, q);
    }
    if (at<uint32_t>(kept_storage, 0)) fx_free_no_context(at<uint32_t>(kept_storage, 0));
}

void XB_THISCALL FxAction_Destroy(uint8_t* self) {
    at<uint32_t>(self, 0) = 0x007c6220;
    uint32_t* p = at<uint32_t*>(self, 0xc);
    uint32_t* const end = p + at<int16_t>(self, 0x12);
    for (; p != end; ++p) {
        const uint32_t object = *p;
        if (!object) continue;
        const uint32_t destructor = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object))));
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(destructor)(object, 0);
        fx_free(object);
    }
    if (at<uint32_t>(self, 0xc)) {
        fx_free_no_context(at<uint32_t>(self, 0xc));
        at<uint32_t>(self, 0xc) = 0;
        at<int16_t>(self, 0x10) = 0;
        at<int16_t>(self, 0x12) = 0;
    }
    if (at<uint32_t>(self, 4)) {
        fx_free_no_context(at<uint32_t>(self, 4));
        at<uint32_t>(self, 4) = 0;
        at<int16_t>(self, 8) = 0;
        at<int16_t>(self, 0xa) = 0;
    }
}

void XB_THISCALL apsPtrVector_DeleteAll(uint8_t* self) {
    uint32_t* p = at<uint32_t*>(self, 0);
    uint32_t* const end = p + at<int16_t>(self, 6);
    for (; p != end; ++p) {
        const uint32_t object = *p;
        if (!object) continue;
        const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object))))[3];
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(method)(object, 0);
        fx_free(object);
    }
    at<uint32_t>(self, 8) = 0;
    if (at<uint32_t>(self, 0)) {
        fx_free_no_context(at<uint32_t>(self, 0));
        at<uint32_t>(self, 0) = 0;
        at<int16_t>(self, 4) = 0;
        at<int16_t>(self, 6) = 0;
    }
}

void XB_CDECL apsLog_Printf(int32_t* log, uint32_t code, const char* format, ...) {
    if (log[0] >= 8) return;
    const int32_t index = log[0]++;
    uint8_t* entry = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(log[1])) + index * 0x104;
    at<uint32_t>(entry, 0) = code;
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(
        reinterpret_cast<char*>(static_cast<uintptr_t>(log[1])) + index * 0x104 + 4, format, args);  // _vsprintf
    va_end(args);
}

void XB_THISCALL FxEmitters_FreeAll(uint8_t* self) {
    uint8_t** p = at<uint8_t**>(self, 4);
    uint8_t** const end = p + at<int16_t>(self, 0xa);
    for (; p != end; ++p) {
        uint8_t* emitter = *p;
        const int16_t index = at<int16_t>(emitter, 0xac);
        FxEmitter_FreeParticles(emitter, 0);
        apsMemoryPool_Free(title_global<uint8_t*>(0x00909f64) + 8, emitter);
        at<uint16_t>(emitter, 0xac) = 0xffff;
        if (at<int16_t>(self, 0xa) > 1) {
            uint8_t** data = at<uint8_t**>(self, 4);
            data[index] = data[at<int16_t>(self, 0xa) - 1];
            at<int16_t>(at<uint8_t**>(self, 4)[index], 0xac) = index;
        }
        if (at<uint16_t>(self, 0xa)) --at<uint16_t>(self, 0xa);
    }
}

void XB_THISCALL FxEmitters_Destroy(uint8_t* self) {
    FxEmitters_FreeAll(self);
    if (title_global<uint32_t>(0x00909f64)) {
        FxEffectPool_Release(title_global<uint8_t*>(0x00909f64));
        fx_free(title_global<uint32_t>(0x00909f64));
    }
    if (at<uint32_t>(self, 4)) {
        fx_free_no_context(at<uint32_t>(self, 4));
        at<uint32_t>(self, 4) = 0;
        at<int16_t>(self, 8) = 0;
        at<int16_t>(self, 0xa) = 0;
    }
    title_global<uint32_t>(0x00909f60) = 0;
}

float XB_THISCALL FxEffect_ViewDepth(const uint8_t* self) {
    const float* p = reinterpret_cast<const float*>(self + 0x50);
    const float* v = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(title_global<uint32_t>(0x0090259c)) + 0x150);
    const Lanes view = add(add(add(lanes(v + 12), mul(lanes(v), p[0])), mul(lanes(v + 4), p[1])), mul(lanes(v + 8), p[2]));
    platform_keep(view);
    return view.v[2];
}

void XB_CDECL FxLightSet_SelectObjectLights(uint8_t* set, const float* sphere) {
    // Like nglSelectMeshLightSet: the set's list for category (0x008bb993) - 1, directional
    // lights (type 1) always, point lights (type 0) whose radius plus the sphere's reaches it.
    at<uint8_t*>(set, 0x20) = set;
    const int32_t list = static_cast<int8_t>(title_global<uint8_t>(0x008bb993)) - 1;
    const Lanes centre = {{sphere[0], sphere[1], sphere[2], 0.0f}};
    for (uint8_t* light = at<uint8_t**>(set, 0)[list]; light != set; light = at<uint8_t**>(light, 0)[list]) {
        const uint32_t type = at<uint32_t>(light, 0x28);
        bool add_light = false;
        if (type == 0) {
            const float* data = at<float*>(light, 0x2c);
            const float radius = data[9] + sphere[3];
            const Lanes d = sub(lanes(data), centre);
            const Lanes sq = mul(d, d);
            platform_keep(sq);
            const float distance = (sq.v[0] + sq.v[1]) + sq.v[2];
            add_light = radius * radius >= distance;
        } else if (type == 1) {
            add_light = true;
        }
        if (add_light) {
            at<uint8_t*>(light, 0x20) = at<uint8_t*>(set, 0x20);
            at<uint8_t*>(set, 0x20) = light;
        }
    }
}

float* XB_CDECL FxMatrix_RotateVector(float* out, const float* matrix, const float* v) {
    const Lanes sum = add(add(mul(lanes(matrix), v[0]), mul(lanes(matrix + 4), v[1])), mul(lanes(matrix + 8), v[2]));
    platform_keep(sum);
    const float result[4] = {sum.v[0], sum.v[1], sum.v[2], title_global<float>(0x007c6c70)};
    std::memcpy(out, result, 16);
    return out;
}

uint8_t* XB_THISCALL FxLog_Init(uint8_t* self) {
    title_global<uint8_t*>(0x00909908) = self;
    at<uint32_t>(self, 0) = 0;
    uint8_t* entries = fx_alloc(0x820, 4);
    for (int i = 0; i < 8; ++i) {
        uint8_t* entry = entries + i * 0x104;
        if (entry) {
            at<uint32_t>(entry, 0) = 0;
            entry[4] = 0;
        }
    }
    at<uint8_t*>(self, 4) = entries;
    return self;
}

void XB_CDECL FxParticleBuffers_Init(int32_t quads) {
    const uint32_t vertex_bytes = static_cast<uint32_t>(quads) * 0x60;
    title_global<int32_t>(0x00909f34) = quads;
    for (int i = 0; i < 2; ++i) {
        auto* buffer = reinterpret_cast<uint32_t*>(fx_alloc(0xc, 4));
        if (buffer) buffer[0] = 0;
        title_global<uint32_t*>(0x00909f20 + i * 4) = buffer;
        nglCreateVertexBuffer(buffer, vertex_bytes, 0x208);
        title_global<uint32_t>(0x00909f18 + i * 4) = title_global<uint32_t*>(0x00909f20 + i * 4)[0];
    }
    auto* indices = reinterpret_cast<uint8_t**>(fx_alloc(4, 4));
    if (indices) indices[0] = nullptr;
    title_global<uint8_t**>(0x00909f28) = indices;
    const uint32_t index_bytes = static_cast<uint32_t>(quads) * 0xc;
    nglIndexBuffer_Alloc(indices, index_bytes);
    auto* out = reinterpret_cast<uint16_t*>(nglIndexBuffer_Lock(title_global<uint8_t**>(0x00909f28), 0, index_bytes));
    for (int32_t k = 0, base = 0; k < quads; ++k, base += 4) {
        *out++ = static_cast<uint16_t>(base);
        *out++ = static_cast<uint16_t>(base + 1);
        *out++ = static_cast<uint16_t>(base + 2);
        *out++ = static_cast<uint16_t>(base + 2);
        *out++ = static_cast<uint16_t>(base + 1);
        *out++ = static_cast<uint16_t>(base + 3);
    }
    nglIndexBuffer_Unlock();
}

uint32_t XB_THISCALL apsPtrVector_Reserve(uint8_t* self, int32_t capacity) {
    if (capacity <= at<int16_t>(self, 4)) return 1;
    const int32_t count = at<int16_t>(self, 6);
    auto* memory = reinterpret_cast<uint32_t*>(fx_alloc_no_context(static_cast<uint32_t>(capacity) << 2, 4));
    if (!memory) return 0;
    for (int32_t i = 0; i < count; ++i) memory[i] = at<uint32_t*>(self, 0)[i];
    replace_block(self, reinterpret_cast<uint8_t*>(memory), static_cast<int16_t>(capacity), static_cast<int16_t>(count));
    return 1;
}

float XB_THISCALL FxOp_SetupNode(const uint8_t* op, uint8_t* node, uint32_t texture_a, uint32_t texture_b,
                                 const uint32_t* textures, const float* sphere, const uint32_t* matrix,
                                 const uint8_t* scale_source) {
    const uint32_t texture_c = textures[1];
    if (texture_b && texture_a && texture_c) {
        at<uint32_t>(node, 0x74) = texture_b;
        at<uint32_t>(node, 0x70) = texture_a;
        at<uint32_t>(node, 0x78) = texture_c;
    } else {
        at<uint32_t>(node, 0x74) = 0;
        at<uint32_t>(node, 0x70) = 0;
        at<uint32_t>(node, 0x78) = 0;
    }
    std::memcpy(node + 0x50, sphere, 16);
    if (matrix)
        std::memcpy(node + 0x10, matrix, 0x40);
    else
        title_function<void XB_CDECL(uint8_t*)>(0x0001c140)(node + 0x10);  // Matrix_Identity
    if (scale_source && at<int32_t>(op, 0x34) == 3) {
        alignas(16) float extent[4];
        FxVec3_ScaledHalfExtent(extent, scale_source);
        const Lanes scaled = mul(lanes(extent), lanes(reinterpret_cast<const float*>(op + 0x20)));
        std::memcpy(node + 0x60, scaled.v, 16);
    } else {
        std::memcpy(node + 0x60, op + 0x20, 16);
    }
    uint32_t flags = 0;
    if (at<uint32_t>(op, 4) & 0x800) {
        flags = 2;
    } else {
        const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(op, 0)))[4];
        if (title_function<uint32_t XB_THISCALL(const uint8_t*)>(method)(op)) flags = 1;
    }
    const float depth = FxEffect_ViewDepth(node);
    const int32_t kind = at<int32_t>(op, 0x34);
    if (kind <= 0 || kind > 2) {
        const uint8_t* scene = reinterpret_cast<const uint8_t*>(title_global<uint32_t>(0x0090259c));
        if (sphere[3] + depth > at<float>(scene, 0x388)) flags |= 8;
    }
    at<uint32_t>(node, 0x7c) |= flags;
    return depth;
}

namespace {

uint32_t op_add_to_list(const uint8_t* op, uint32_t node_vtable, uint32_t a1, uint32_t a2, const uint32_t* a3,
                        const float* sphere, const uint32_t* matrix, const uint8_t* scale_source) {
    alignas(16) float centre[4];
    std::memcpy(centre, sphere, 16);
    if (!nglSphereInFrustum(centre, sphere[3])) return 0;
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    const uintptr_t block = (arena + 0xf) & ~uintptr_t{0xf};
    arena = block + 0x90;
    if (!block) return 2;
    uint8_t* node = reinterpret_cast<uint8_t*>(block);
    at<uint32_t>(node, 0x7c) = 0;
    at<uint32_t>(node, 0) = node_vtable;
    at<const uint8_t*>(node, 0x80) = op;
    const float depth = FxOp_SetupNode(op, node, a1, a2, a3, sphere, matrix, scale_source);
    FxNode_AddToScene(op, node, depth);
    return 1;
}

}  // namespace

#define FX_OP_ADD_TO_LIST(name, vtable)                                                                         \
    uint32_t XB_THISCALL name(const uint8_t* op, uint32_t a1, uint32_t a2, const uint32_t* a3, const float* sphere, \
                              const uint32_t* matrix, const uint8_t* scale_source) {                            \
        return op_add_to_list(op, vtable, a1, a2, a3, sphere, matrix, scale_source);                           \
    }
FX_OP_ADD_TO_LIST(FxOpdd_AddToList, 0x007c7344)
FX_OP_ADD_TO_LIST(FxOp1dd_AddToList, 0x007c75c4)
FX_OP_ADD_TO_LIST(FxOp9d5_AddToList, 0x007c783c)
FX_OP_ADD_TO_LIST(FxOpd5_AddToList, 0x007c7abc)
FX_OP_ADD_TO_LIST(FxOp15b_AddToList, 0x007c7d3c)
FX_OP_ADD_TO_LIST(FxOp5b_AddToList, 0x007c7fbc)
FX_OP_ADD_TO_LIST(FxOpGrid_AddToList, 0x007c75bc)
FX_OP_ADD_TO_LIST(FxOp53_AddToList, 0x007c733c)
#undef FX_OP_ADD_TO_LIST

namespace {

// The shared head of FxOp31/FxOp1d5's AddToList: frustum test and node; null when culled (*result
// 0) or out of frame memory (*result 2).
uint8_t* op_node(const uint8_t* op, uint32_t node_vtable, uint32_t a0, uint32_t a1, const uint32_t* a2,
                 const float* sphere, const uint32_t* matrix, uint32_t* result) {
    alignas(16) float centre[4];
    std::memcpy(centre, sphere, 16);
    if (!nglSphereInFrustum(centre, sphere[3])) {
        *result = 0;
        return nullptr;
    }
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    const uintptr_t block = (arena + 0xf) & ~uintptr_t{0xf};
    arena = block + 0x90;
    if (!block) {
        *result = 2;
        return nullptr;
    }
    uint8_t* node = reinterpret_cast<uint8_t*>(block);
    at<uint32_t>(node, 0x7c) = 0;
    at<uint32_t>(node, 0) = node_vtable;
    at<const uint8_t*>(node, 0x80) = op;
    FxOp_SetTextures(node, a1, a0, a2[1]);
    std::memcpy(node + 0x50, sphere, 16);
    if (matrix)
        std::memcpy(node + 0x10, matrix, 0x40);
    else
        title_function<void XB_CDECL(uint8_t*)>(0x0001c140)(node + 0x10);  // Matrix_Identity
    return node;
}

uint32_t op_sort_flags(const uint8_t* op) {
    if (at<uint32_t>(op, 4) & 0x800) return 2;
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(op, 0)))[4];
    return title_function<uint32_t XB_THISCALL(const uint8_t*)>(method)(op) ? 1 : 0;
}

// x87: depth + radius beyond the scene's far distance (+0x388).
bool beyond_far(float depth, const float* sphere) {
    const uint8_t* scene = reinterpret_cast<const uint8_t*>(title_global<uint32_t>(0x0090259c));
    return platform_x87_greater(platform_x87_add(depth, sphere[3]), at<float>(scene, 0x388));
}

}  // namespace

uint32_t XB_THISCALL FxOp31_AddToList(const uint8_t* op, uint32_t a0, uint32_t a1, const uint32_t* a2, const float* sphere,
                                      const uint32_t* matrix, uint32_t light_set) {
    if (!at<uint32_t>(op, 0xc)) return 2;
    uint32_t result = 0;
    uint8_t* node = op_node(op, 0x007c6740, a0, a1, a2, sphere, matrix, &result);
    if (!node) return result;
    at<uint32_t>(node, 0x84) = light_set ? light_set : title_global<uint32_t>(0x00902e40);
    uint32_t flags = op_sort_flags(op);
    const float depth = FxEffect_ViewDepth(node);
    if (beyond_far(depth, sphere)) flags |= 8;
    at<float>(node, 8) = depth;
    at<uint32_t>(node, 0x7c) |= flags;
    uint8_t* scene = reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c));
    at<uint32_t>(node, 4) = at<uint32_t>(scene, 0x308);
    at<uint8_t*>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x308) = node;
    ++at<uint32_t>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x310);
    return 1;
}

uint32_t XB_THISCALL FxOp1d5_AddToList(const uint8_t* op, uint32_t a0, uint32_t a1, const uint32_t* a2, const float* sphere,
                                       const uint32_t* matrix, const uint8_t* scale_source) {
    uint32_t result = 0;
    uint8_t* node = op_node(op, 0x007c70bc, a0, a1, a2, sphere, matrix, &result);
    if (!node) return result;
    if (scale_source && at<int32_t>(op, 0x24) == 3) {
        alignas(16) float extent[4];
        FxVec3_ScaledHalfExtent(extent, scale_source);
        const Lanes scaled = mul(lanes(extent), lanes(reinterpret_cast<const float*>(op + 0x10)));
        std::memcpy(node + 0x60, scaled.v, 16);
    } else {
        std::memcpy(node + 0x60, op + 0x10, 16);
    }
    uint32_t flags = op_sort_flags(op);
    const float depth = FxEffect_ViewDepth(node);
    if (beyond_far(depth, sphere)) flags |= 8;
    at<uint32_t>(node, 8) = 0xffffffff;
    at<uint32_t>(node, 0x7c) |= flags;
    uint8_t* scene = reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c));
    at<uint32_t>(node, 4) = at<uint32_t>(scene, 0x304);
    at<uint8_t*>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x304) = node;
    ++at<uint32_t>(reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x0090259c)), 0x30c);
    return 1;
}

namespace {

void set_constant(uint32_t base, const float* v) {
    title_function<void XB_FASTCALL(uint32_t, const float*)>(0x003e7440)(base + 0x60, v);  // SetVertexShaderConstant1Fast
}

}  // namespace

void XB_CDECL FxShader_SetFadeConstant(uint32_t base, float near_distance, float far_distance, float near_alpha,
                                       float far_alpha) {
    float range = far_distance - near_distance;
    const float one = title_global<float>(0x0083eb20);
    const float a = one - near_alpha;
    const float b = one - far_alpha;
    if (range == 0.0f) range = one;
    const float scale = (b - a) / range;
    alignas(16) const float v[4] = {scale, a - scale * near_distance, 0.0f, 0.0f};
    set_constant(base, v);
}

void XB_CDECL FxShader_SetDepthRangeConstant(uint32_t base, float near_depth, float far_depth) {
    const float k = title_global<float>(0x0083f2a0);  // 2^24
    const float n = near_depth * k;
    float range = far_depth * k - n;
    const float zero = 0.0f;
    float one = title_global<float>(0x0083eb20);
    if (zero == range) range = one;
    const float inverse = one / range;
    alignas(16) const float v[4] = {inverse, zero - inverse * n, zero, zero};
    set_constant(base, v);
}

void XB_CDECL FxNode_ComputeObjectLights(float* directions, float* colors, const float* matrix, uint8_t* set,
                                         const float* sphere) {
    FxLightSet_SelectObjectLights(set, sphere);
    alignas(16) const float position[4] = {sphere[0], sphere[1], sphere[2], 0.0f};
    int count = 0;
    float* direction = directions;
    for (uint8_t* light = at<uint8_t*>(set, 0x20); light && light != set;) {
        alignas(16) float evaluated[8];
        const float* data = nglEvalLight(evaluated, reinterpret_cast<const NglLight*>(light), position);
        alignas(16) float rotated[4];
        const float* r = FxMatrix_RotateVector(rotated, matrix, data);
        for (int i = 0; i < 4; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &r[i], 4);
            bits ^= 0x80000000u;
            std::memcpy(&direction[i], &bits, 4);
        }
        colors[count] = data[4];
        colors[4 + count] = data[5];
        colors[8 + count] = data[6];
        ++count;
        direction += 4;
        if (count == 4) break;
        light = at<uint8_t*>(light, 0x20);
    }
    for (; count < 4; ++count) {
        std::memset(directions + count * 4, 0, 16);
        colors[count] = 0.0f;
        colors[4 + count] = 0.0f;
        colors[8 + count] = 0.0f;
    }
    std::memcpy(colors + 12, set + 0x60, 16);
}

void XB_CDECL FxCamera_BillboardAxes(float* forward_out, float* right_out, float* up_out) {
    // The camera's forward (scene +0x260), a horizontal right axis (y, -x) and up = right x forward,
    // both normalized (RSQRTSS + Newton); right scaled by the billboard size 0x00909984.
    const float* camera = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(title_global<uint32_t>(0x0090259c)) + 0x260);
    const Lanes f = lanes(camera);
    static const float kRight[4] = {1.0f, -1.0f, 0.0f, 0.0f};  // 0x007c6de0
    const Lanes right = normalized(mul(Lanes{{f.v[1], f.v[0], f.v[2], f.v[3]}}, lanes(kRight)));
    const Lanes a = mul(Lanes{{right.v[1], right.v[2], right.v[0], right.v[3]}}, f);
    const Lanes b = mul(right, Lanes{{f.v[1], f.v[2], f.v[0], f.v[3]}});
    const Lanes c = sub(b, a);
    const Lanes up = normalized(Lanes{{c.v[1], c.v[2], c.v[0], c.v[3]}});
    std::memcpy(forward_out, f.v, 16);
    const Lanes scaled = mul(right, title_global<float>(0x00909984));
    std::memcpy(right_out, scaled.v, 16);
    std::memcpy(up_out, up.v, 16);
}

uint8_t* XB_THISCALL FxSystem_InitBounds(uint8_t* self) {
    const float big = title_global<float>(0x007c5000);  // FLT_MAX
    for (int i = 0; i < 4; ++i) at<float>(self, 0x60 + i * 4) = big;
    for (int i = 0; i < 4; ++i) at<uint32_t>(self, 0x70 + i * 4) = at<uint32_t>(self, 0x60 + i * 4) ^ 0x80000000u;
    FxVertexFormat_Init(self + 0x84);
    return self;
}

uint8_t* XB_CDECL FxSystems_New(int32_t count, uint32_t alignment) {
    uint8_t* memory = fx_alloc(static_cast<uint32_t>(count) * 0xd0, alignment);
    uint8_t* p = memory;
    for (int32_t i = 0; i < count; ++i, p += 0xd0) {
        if (p) FxSystem_InitBounds(p);
    }
    return memory;
}

uint8_t* XB_THISCALL FxEmitters_Init(uint8_t* self, int32_t count) {
    title_global<uint8_t*>(0x00909f60) = self;
    at<uint32_t>(self, 4) = 0;
    at<int16_t>(self, 8) = 0;
    at<int16_t>(self, 0xa) = 0;
    at<int32_t>(self, 0) = count;
    apsPtrVector_Reserve(self + 4, count);
    uint8_t* pool = fx_alloc(0x2c, 4);
    if (pool) {
        title_global<uint8_t*>(0x00909f64) = pool;
        FxPool_Init(pool + 8, 0);
        at<int32_t>(pool, 4) = count;
        uint8_t* items = FxSystems_New(count, 0x10);
        at<uint8_t*>(pool, 0) = items;
        apsMemoryPool_Init(pool + 8, count, 0xd0, items);
    }
    return self;
}

float* XB_CDECL FxMatrix_RigidInverse(float* out, const float* m) {
    // Rows: the transposed 3x3 (lane 3 of each repeats row 2's element), then -(T0 x + T1 y + T2 z)
    // of the translation (all lanes, packed).
    const Lanes t0 = {{m[0], m[4], m[8], m[8]}};
    const Lanes t1 = {{m[1], m[5], m[9], m[9]}};
    const Lanes t2 = {{m[2], m[6], m[10], m[10]}};
    const Lanes sum = add(add(mul(t0, m[12]), mul(t1, m[13])), mul(t2, m[14]));
    const Lanes translation = neg(sum);
    float result[16];
    std::memcpy(result, t0.v, 16);
    std::memcpy(result + 4, t1.v, 16);
    std::memcpy(result + 8, t2.v, 16);
    std::memcpy(result + 12, translation.v, 16);
    std::memcpy(out, result, 64);
    return out;
}

void XB_CDECL FxAxis_Frame(const float* direction, float* direction_out, float* right_out, float* up_out) {
    // A frame around a direction: the normalized direction, a right axis perpendicular to it and
    // to a reference (y, or the rotated direction when |z| > 0.98), and up = right x direction,
    // both normalized; right scaled by the billboard size 0x00909984. Packed SSE throughout.
    const Lanes n = normalized(lanes(direction));
    Lanes reference;
    if (abs_bits(n.v[2]) > title_global<float>(0x0083f148)) {  // 0.98
        reference = Lanes{{n.v[1], n.v[2], n.v[0], n.v[0]}};
    } else {
        static const float kUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};  // 0x007c6cb0
        reference = mul(lanes(kUp), title_global<float>(0x007c6c80));
    }
    const Lanes shuffled_n = {{n.v[1], n.v[2], n.v[0], n.v[3]}};
    const Lanes a = mul(shuffled_n, reference);
    const Lanes b = mul(n, Lanes{{reference.v[1], reference.v[2], reference.v[0], reference.v[3]}});
    const Lanes c = sub(b, a);
    const Lanes right = normalized(Lanes{{c.v[1], c.v[2], c.v[0], c.v[3]}});
    const Lanes a2 = mul(Lanes{{right.v[1], right.v[2], right.v[0], right.v[3]}}, n);
    const Lanes b2 = mul(right, shuffled_n);
    const Lanes c2 = sub(b2, a2);
    const Lanes up = normalized(Lanes{{c2.v[1], c2.v[2], c2.v[0], c2.v[3]}});
    std::memcpy(direction_out, n.v, 16);
    const Lanes scaled = mul(right, title_global<float>(0x00909984));
    std::memcpy(right_out, scaled.v, 16);
    std::memcpy(up_out, up.v, 16);
}

void XB_THISCALL FxEffectStage_CollectFormat(uint8_t* stage) {
    if (const uint8_t* actions = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(at<uint32_t>(stage, 0x14))))
        FxActionList_CollectFormat(actions, stage + 0x18);
    if (const uint32_t renderer = at<uint32_t>(stage, 0x10))
        FxVertexFormat_Add(stage + 0x18, at<uint32_t>(reinterpret_cast<const void*>(static_cast<uintptr_t>(renderer)), 4));
    FxVertexFormat_Add(stage + 0x18, 0x600);
}

int32_t XB_THISCALL FxSystemDef_FindEmitterByHash(const uint8_t* definition, const uint32_t* hash) {
    const int32_t count = at<int16_t>(definition, 0x5e);
    const uint32_t* entry = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at<uint32_t>(definition, 0x58)));
    for (int32_t i = 0; i < count; ++i, entry += 0x2c / 4) {
        int32_t word = 0;
        while (word < 8 && entry[word] == hash[word]) ++word;
        if (word == 8) return i;
    }
    return -1;
}

uint32_t XB_THISCALL FxEffectDef_CanReuseStages(const uint8_t* self, const uint8_t* other) {
    if (other == self) return 1;
    // Each of `other`'s stages takes the smallest free stage of `self` that holds as many
    // particles (+0x1c * +0). The original keeps the sizes in an alloca'd array and marks
    // a taken stage -1; the sizes are recomputed here (nothing writes the stages meanwhile).
    const int32_t count = at<int16_t>(self, 0x56);
    const uint8_t* stages = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(at<uint32_t>(self, 0x50)));
    uint32_t taken[0x8000 / 32] = {};
    auto size_of = [](const uint8_t* stage) {
        return static_cast<int32_t>(at<uint32_t>(stage, 0x1c) * at<uint32_t>(stage, 0));
    };
    auto available = [&](int32_t j) {
        return (taken[j >> 5] >> (j & 31)) & 1 ? -1 : size_of(stages + j * 0x3c);
    };
    const uint8_t* stage = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(at<uint32_t>(other, 0x50)));
    const uint8_t* const end = stage + at<int16_t>(other, 0x56) * 0x3c;
    for (; stage != end; stage += 0x3c) {
        const int32_t need = size_of(stage);
        int32_t best_index = -1;
        int32_t best = 0x40000000;
        for (int32_t j = 0; j < count; ++j) {
            const int32_t size = available(j);
            if (size >= need && size < best) {
                best = size;
                best_index = j;
            }
        }
        if (best_index < 0) return 0;
        taken[best_index >> 5] |= 1u << (best_index & 31);
    }
    return 1;
}

void XB_THISCALL FxEffectStage_Release(uint8_t* stage) {
    if (const uint32_t renderer = at<uint32_t>(stage, 0x10)) {
        const uint32_t dtor = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
            at<uint32_t>(reinterpret_cast<const void*>(static_cast<uintptr_t>(renderer)), 0)))[0];
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(dtor)(renderer, 0);
        fx_free(at<uint32_t>(stage, 0x10));
    }
    if (const uint32_t actions = at<uint32_t>(stage, 0x14)) {
        apsPtrVector_DeleteAll(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(actions)));
        fx_free(at<uint32_t>(stage, 0x14));
    }
}

void XB_THISCALL FxEffectDef_Release(uint8_t* definition) {
    const int32_t count = at<int16_t>(definition, 0x56);
    if (count > 0) {
        int32_t left = count;
        uint32_t offset = 0;
        do {
            FxEffectStage_Release(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(at<uint32_t>(definition, 0x50))) +
                                  offset);
            offset += 0x3c;
        } while (--left != 0);
    }
    // The emitter table (+0x58, sizes +0x5c/+0x5e), then the stage table (+0x50, +0x54/+0x56).
    static const uint32_t kTables[2] = {0x58, 0x50};
    for (uint32_t table : kTables) {
        if (!at<uint32_t>(definition, table)) continue;
        fx_free_no_context(at<uint32_t>(definition, table));
        at<uint32_t>(definition, table) = 0;
        at<int16_t>(definition, table + 4) = 0;
        at<int16_t>(definition, table + 6) = 0;
    }
}

void XB_THISCALL FxRange_SampleN(const uint8_t* range, int32_t count, float* out) {
    if (count <= 0) return;
    const float* bounds = reinterpret_cast<const float*>(range + 4);
    do {
        *out++ = static_cast<float>(FxRandom_Range(bounds[0], bounds[1]));
        ++bounds;
    } while (--count != 0);
}

long double XB_CDECL FxRandom_Range(float low, float high) {
    uint32_t& seed = title_global<uint32_t>(0x00909f6c);  // fx_randSeed
    seed = (0x3039u - seed * 0x3e39b193u) & 0x7fffffffu;
    const uint32_t bits = (seed >> 8) | 0x3f800000u;
    float unit;
    std::memcpy(&unit, &bits, 4);
    // On the x87: (unit - 1) * (high - low) + low.
    const long double fraction = static_cast<long double>(unit) - title_global<float>(0x0083eb20);
    const long double span = static_cast<long double>(high) - low;
    return fraction * span + low;
}

// The ranges' and constants' Within (vtable slots): at most 1 (scalar) or 3 (vector)
// components, each tested against value[0] - the value pointer never advances.
uint32_t XB_THISCALL FxRange_Within(const uint8_t* range, int32_t count, const float* value) {
    if (count > 1) count = 1;
    for (int32_t i = 0; i < count; ++i) {
        if (at<float>(range, 4 + i * 4) > *value) return 0;
        if (*value > at<float>(range, 8 + i * 4)) return 0;
    }
    return 1;
}

uint32_t XB_THISCALL FxConst_Within(const uint8_t* constant, int32_t count, const float* value) {
    if (count > 1) count = 1;
    for (int32_t i = 0; i < count; ++i)
        if (!(*value == at<float>(constant, 4 + i * 4))) return 0;
    return 1;
}

uint32_t XB_THISCALL FxConstVector_Within(const uint8_t* constant, int32_t count, const float* value) {
    if (count > 3) count = 3;
    for (int32_t i = 0; i < count; ++i)
        if (!(*value == at<float>(constant, 4 + i * 4))) return 0;
    return 1;
}

uint32_t XB_THISCALL FxVectorRange_Within(const uint8_t* range, int32_t count, const float* value) {
    if (count > 3) count = 3;
    for (int32_t i = 0; i < count; ++i) {
        if (at<float>(range, 4 + i * 4) > *value) return 0;
        if (*value > at<float>(range, 0x10 + i * 4)) return 0;
    }
    return 1;
}

}  // namespace game
