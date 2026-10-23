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

}  // namespace game
