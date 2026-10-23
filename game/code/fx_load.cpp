#include "fx_load.h"

#include <cstring>

#include "fx.h"
#include "lanes.h"
#include "ngl.h"
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
            for (uint8_t* p = at<uint8_t*>(self, 0) + old * 0x2c; p != at<uint8_t*>(self, 0) + count * 0x2c; p += 0x2c) {
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
            for (uint8_t* p = at<uint8_t*>(self, 0) + old * 0x3c; p != at<uint8_t*>(self, 0) + count * 0x3c; p += 0x3c) {
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

}  // namespace game
