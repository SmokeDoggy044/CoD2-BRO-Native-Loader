#include "qcommon.h"

#include "lanes.h"
#include "title_data.h"

namespace game {

void* XB_CDECL Mem_AlignedAlloc(uint32_t size, uint32_t alignment, uint32_t flags) {
    using Alloc = void* XB_CDECL(uint32_t, uint32_t, uint32_t);
    ++title_global<uint32_t>(0x009098c4);
    if (const uint32_t allocator = title_global<uint32_t>(0x009098e8))
        return title_function<Alloc>(allocator)(size, alignment, flags);
    if (flags & 0x30000) {
        const uint32_t protect = (flags & 0x20000) ? 0x404 : 0x4;  // PAGE_READWRITE (| WRITECOMBINE)
        return title_function<void* XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x001e763a)(
            size, 0xffffffff, alignment, protect);  // XPhysicalAlloc
    }
    return title_function<void* XB_CDECL(uint32_t, uint32_t)>(0x001f114f)(size, alignment);  // _aligned_malloc
}

uint32_t XB_REGS2 XB_CDECL Com_HashKeyNoCase(uint32_t, const char* text) {
    uint32_t hash = 0;
    if (!text) return 0;
    for (; *text; ++text) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 0x20);
        hash = ((hash << 5) | (hash >> 27)) ^ static_cast<uint32_t>(static_cast<int32_t>(c));
    }
    return hash;
}

void XB_REGS3 XB_CDECL FastSinCos(float* sine, uint32_t, float* cosine, float angle) {
    // Packed: lanes {angle + 3 pi / 2, angle + 0} twice; lanes 2 and 3 repeat 0 and 1.
    const float shifted = angle + from_bits(0x4096cbe2);
    const float plain = angle + 0.0f;
    *sine = scene_sine_poly(shifted);
    *cosine = scene_sine_poly(plain);
}

void XB_CDECL Q_strncpyLen(char* dst, int32_t* length, int32_t max, const char* src) {
    int32_t count = 0;
    if (src && *src) {
        while (count < max) {
            *dst++ = *src;
            ++count;
            if (!*++src) break;
        }
    }
    *dst = 0;
    *length = count;
}

void XB_CDECL Q_strncpySubLen(char* dst, int32_t* length, const char* src, int32_t start, int32_t count,
                              int32_t size) {
    if (start >= size) return;
    int32_t end = size - 1;
    if (end >= start + count) end = start + count;
    int32_t copied = 0;
    for (int32_t i = start; i < end && src[i]; ++i) dst[copied++] = src[i];
    *length = copied;
    dst[copied] = 0;
}

}  // namespace game
