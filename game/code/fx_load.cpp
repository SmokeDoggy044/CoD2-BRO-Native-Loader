#include "fx_load.h"

#include <cstring>

#include "lanes.h"
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

}  // namespace game
