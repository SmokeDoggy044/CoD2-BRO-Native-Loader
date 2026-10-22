#include "qcommon.h"

#include <cstring>

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

PLATFORM_ASM_ENTRY(FixedStr_BaseName_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FixedStr_BaseName) "addl $12, %esp\n"
                   "ret $4")

char* XB_CDECL FixedStr_BaseName(const char* src, char* dst, uint32_t flag) {
    char stem[0x80];
    int32_t stem_length;
    stem[0x7f] = 0;
    stem[0] = 0;
    title_function<void XB_CDECL(char*, int32_t*, const char*, uint32_t, uint32_t)>(0x00044500)(  // Path_BaseName
        stem, &stem_length, src, static_cast<uint8_t>(src[0x7f]), flag);
    const uint8_t length = static_cast<uint8_t>(stem_length);
    stem[0x7f] = static_cast<char>(length);
    int32_t copied;
    title_function<void XB_CDECL(char*, int32_t*, int32_t, const char*, uint32_t)>(0x00044370)(  // FixedStr_Copy
        dst, &copied, 0x7f, stem, length);
    dst[0x7f] = static_cast<char>(copied);
    return dst;
}

PLATFORM_ASM_ENTRY(AnglesToAxis_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnglesToAxis) "addl $12, %esp\n"
                   "ret")

void XB_CDECL AnglesToAxis(uint32_t* args, float* right, float* up) {
    // args = the original's argument slots {angles, forward}; it keeps two of its sines there.
    const float* angles = reinterpret_cast<const float*>(uintptr_t{args[0]});
    float* forward = reinterpret_cast<float*>(uintptr_t{args[1]});
    const float to_radians = title_global<float>(0x0083edd8);
    float yaw_s, yaw_c, pitch_s, pitch_c, roll_s, roll_c;
    FastSinCos(&yaw_s, 0, &yaw_c, platform_x87_mul(angles[1], to_radians));
    FastSinCos(&pitch_s, 0, &pitch_c, platform_x87_mul(angles[0], to_radians));
    forward[1] = pitch_c * yaw_s;
    forward[0] = pitch_c * yaw_c;
    forward[2] = 0.0f - pitch_s;
    FastSinCos(&roll_s, 0, &roll_c, platform_x87_mul(angles[2], to_radians));
    std::memcpy(&args[0], &pitch_s, 4);
    std::memcpy(&args[1], &roll_s, 4);
    const float minus_one = -1.0f;
    const float rs_ps = roll_s * pitch_s;
    right[0] = roll_c * yaw_s - rs_ps * yaw_c;
    right[1] = roll_c * yaw_c * minus_one - rs_ps * yaw_s;
    right[2] = roll_s * pitch_c * minus_one;
    const float rc_ps = roll_c * pitch_s;
    up[0] = rc_ps * yaw_c + roll_s * yaw_s;
    up[1] = rc_ps * yaw_s - roll_s * yaw_c;
    up[2] = roll_c * pitch_c;
}

void XB_REGS3 XB_CDECL MatrixMultiply43To44(const float* a, float* out, const float* b) {
    // out = a (4x3: rows of 3, translation a[9..11]) applied to b's rotation rows and
    // translation (b rows of 4); sums in the original's order (ssesym.py).
    out[0] = ((b[0] * a[0]) + (b[1] * a[3])) + (a[6] * b[2]);
    out[4] = ((b[5] * a[3]) + (b[4] * a[0])) + (b[6] * a[6]);
    out[8] = ((b[9] * a[3]) + (b[8] * a[0])) + (b[10] * a[6]);
    out[1] = ((b[1] * a[4]) + (a[7] * b[2])) + (a[1] * b[0]);
    out[5] = ((b[6] * a[7]) + (b[5] * a[4])) + (b[4] * a[1]);
    out[9] = ((b[10] * a[7]) + (b[9] * a[4])) + (b[8] * a[1]);
    out[2] = ((b[0] * a[2]) + (b[1] * a[5])) + (a[8] * b[2]);
    out[6] = ((b[6] * a[8]) + (b[5] * a[5])) + (b[4] * a[2]);
    out[10] = ((b[10] * a[8]) + (b[9] * a[5])) + (b[8] * a[2]);
    out[3] = 0.0f;
    out[7] = 0.0f;
    out[11] = 0.0f;
    out[12] = (((b[12] * a[0]) + (a[6] * b[14])) + (b[13] * a[3])) + a[9];
    out[13] = (((a[7] * b[14]) + (a[4] * b[13])) + (a[1] * b[12])) + a[10];
    out[14] = (((b[12] * a[2]) + (b[14] * a[8])) + (b[13] * a[5])) + a[11];
    out[15] = 1.0f;
}

}  // namespace game
