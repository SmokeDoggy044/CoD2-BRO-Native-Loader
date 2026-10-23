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

const float* XB_REGS3 XB_CDECL MatrixMultiply43To44(const float* a, float* out, const float* b) {
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
    return a;
}

int32_t XB_CDECL Q_stricmpn(const char* s1, const char* s2, int32_t n) {
    int32_t c1, c2;
    do {
        c1 = *s1++;
        c2 = *s2++;
        if (!n--) return 0;
        if (c1 != c2) {
            if (c1 >= 'A' && c1 <= 'Z') c1 += 0x20;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 0x20;
            if (c1 != c2) return c1 - c2;
        }
    } while (c1);
    return 0;
}

int32_t XB_REGS3 XB_CDECL Q_stricmp(const char* s2, uint32_t, const char* s1) {
    if (!s1 || !s2) return -1;
    return Q_stricmpn(s1, s2, 99999);
}

int32_t XB_REGS3 XB_CDECL Q_stricmpn2(int32_t n, const char* s1, const char* s2) {
    int32_t c1, c2;
    do {
        c1 = *s1++;
        c2 = *s2++;
        if (!n--) return 0;
        if (c1 != c2) {
            if (c1 >= 'a' && c1 <= 'z') c1 -= 0x20;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 0x20;
            if (c1 != c2) return -1;
        }
    } while (c1);
    return 0;
}

namespace {

template <typename T>
T* title_pointer(uint32_t address) {
    return reinterpret_cast<T*>(uintptr_t{address});
}

using Strncpy = char* XB_CDECL(char*, const char*, uint32_t);
using Break = void XB_CDECL(uint32_t, const char*);
using Vsnprintf = int32_t XB_CDECL(char*, uint32_t, const char*, const uint32_t*);
constexpr uint32_t kStrncpy = 0x001eed80;
constexpr uint32_t kBreak = 0x002938f0;
constexpr uint32_t kVsnprintf = 0x001efce2;

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double title_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(uintptr_t{0x0033acda})
                     : "st(1)", "eax", "ecx", "edx", "memory");
    return result;
}

}  // namespace

char* XB_REGS2 XB_CDECL Q_strcat(int32_t size, char* dest, const char* src) {
    const int32_t length = static_cast<int32_t>(std::strlen(dest));
    if (length >= size)
        title_function<Break>(kBreak)(0, title_pointer<const char>(0x0083385c));  // "Q_strcat: already overflowed"
    char* end = dest + length;
    const int32_t left = size - length;
    char* result = title_function<Strncpy>(kStrncpy)(end, src, static_cast<uint32_t>(left - 1));
    end[left - 1] = 0;
    return result;
}

PLATFORM_ASM_ENTRY(Q_strlwr_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Q_strlwr) "addl $4, %esp\n"
                   "ret")

char* XB_CDECL Q_strlwr(char* text) {
    for (char* p = text; *p; ++p)
        *p = static_cast<char>(title_function<int32_t XB_CDECL(int32_t)>(0x001eed48)(*p));  // tolower
    return text;
}

char* XB_REGS2 XB_CDECL COM_SkipPath(uint32_t, const char* path) {
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') last = p + 1;
    char* out = title_pointer<char>(0x0098b1a0);
    char* dst = out;
    while ((*dst++ = *last++) != 0) {
    }
    return out;
}

const char* XB_REGS2 XB_CDECL COM_GetExtension(uint32_t, const char* name) {
    const char* empty = title_pointer<const char>(0x0082dfeb);
    const char* extension = empty;
    for (const char* p = name; *p; ++p) {
        if (*p == '.')
            extension = p + 1;
        else if (*p == '/' || *p == '\\')
            extension = empty;
    }
    return extension;
}

void XB_REGS1 XB_CDECL COM_DefaultExtension(char* path, int32_t max_size, const char* extension) {
    const char* p = path + std::strlen(path) - 1;
    char c = *p;
    if (c != '/') {
        for (;;) {
            if (p == path) break;
            if (c == '.') return;
            c = *--p;
            if (c == '/') break;
        }
    }
    char old_path[0x80];
    title_function<Strncpy>(kStrncpy)(old_path, path, 0x7f);
    old_path[0x7f] = 0;
    const uint32_t args[3] = {0x0082d800, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(old_path)),  // "%s%s"
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(extension))};
    Com_sprintf(path, max_size, args);
}

PLATFORM_ASM_ENTRY(va_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(va) "addl $4, %esp\n"
                   "ret")

char* XB_CDECL va(const uint32_t* args) {
    char* staging = title_pointer<char>(0x0092f148);
    const int32_t length = title_function<Vsnprintf>(kVsnprintf)(
        staging, 0x400, reinterpret_cast<const char*>(uintptr_t{args[0]}), args + 1);
    staging[0x3ff] = 0;
    if (length < 0 || length >= 0x400)
        title_function<Break>(kBreak)(1, title_pointer<const char>(0x00833830));  // "Attempted to overrun string in call to va()"
    int32_t& index = title_global<int32_t>(0x0098ad2c);
    int32_t start = index;
    if (start + length >= 0x3ff) start = 0;
    char* out = title_pointer<char>(0x00927f60) + start;
    std::memmove(out, staging, static_cast<uint32_t>(length + 1));
    index = start + length + 1;
    return out;
}

PLATFORM_ASM_ENTRY(Com_sprintf_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Com_sprintf) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL Com_sprintf(char* dest, int32_t size, const uint32_t* args) {
    const int32_t result = title_function<Vsnprintf>(kVsnprintf)(
        dest, static_cast<uint32_t>(size), reinterpret_cast<const char*>(uintptr_t{args[0]}), args + 1);
    dest[size - 1] = 0;
    return result;
}

int32_t XB_REGS2 XB_CDECL Com_BitCheck(uint32_t, int32_t bit, const uint32_t* array) {
    return (array[bit >> 5] & (1u << (bit & 31))) ? 1 : 0;
}

const char* XB_CDECL Cmd_Argv(uint32_t arg) {
    if (arg >= title_global<uint32_t>(0x00934248)) return title_pointer<const char>(0x0082dfeb);
    return title_global<const char*>(0x0092df90 + arg * 4);
}

int32_t XB_CDECL Cmd_Argc() { return title_global<int32_t>(0x00934248); }

char* XB_REGS1 XB_CDECL Cmd_ArgsFrom(int32_t first) {
    char* out = title_pointer<char>(0x00927b30);
    const int32_t& argc = title_global<int32_t>(0x00934248);
    out[0] = 0;
    const int32_t last = argc - 1;
    for (int32_t i = first; i < argc; ++i) {
        const char* arg = title_global<const char*>(0x0092df90 + i * 4);
        std::memcpy(out + std::strlen(out), arg, std::strlen(arg) + 1);  // strcat
        if (i != last) std::memcpy(out + std::strlen(out), title_pointer<const char>(0x0082d6e4), 2);  // " "
    }
    return out;
}

PLATFORM_ASM_ENTRY(Cmd_ArgsBuffer_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Cmd_ArgsBuffer) "addl $8, %esp\n"
                   "ret")

char* XB_CDECL Cmd_ArgsBuffer(char* buffer, int32_t size) {
    char* result = title_function<Strncpy>(kStrncpy)(buffer, Cmd_ArgsFrom(1), static_cast<uint32_t>(size - 1));
    buffer[size - 1] = 0;
    return result;
}

PLATFORM_ASM_ENTRY(Q_fmod_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n" PLATFORM_ASM_CALL(Q_fmod) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "ret")

float XB_CDECL Q_fmod(float a, float b) {
    const long double difference = static_cast<long double>(a) - b;
    float delta = static_cast<float>(title_ci_fmod(difference, title_global<double>(0x0083ed40)));  // 360.0
    const float full = title_global<float>(0x0083ebcc);                                           // 360
    if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - full;       // 180
    if (title_global<float>(0x0083ebd0) > delta) delta = delta + full;                            // -180
    return delta;
}

uint32_t XB_CDECL Com_Memset32(uint32_t* dest, uint32_t value, uint32_t count) {
    for (int32_t i = static_cast<int32_t>(count & ~7u) - 8; i >= 0; i -= 8)
        for (int32_t j = 0; j < 8; ++j) dest[i + j] = value;
    uint32_t rest = count & 7;
    uint32_t* p = dest + (count & ~7u);
    if (rest >= 4) {
        p[0] = p[1] = p[2] = p[3] = value;
        p += 4;
        rest -= 4;
    }
    if (rest >= 2) {
        p[0] = p[1] = value;
        p += 2;
        rest -= 2;
    }
    if (rest >= 1) p[0] = value;
    return value;
}

uint32_t XB_CDECL Com_Memset(uint8_t* dest, uint32_t fill, uint32_t count) {
    if (count < 8) {
        const uint32_t pattern = (fill & 0xff) * 0x01010101u;
        uint32_t rest = count;
        if (static_cast<int32_t>(rest) >= 4) {
            std::memcpy(dest, &pattern, 4);
            dest += 4;
            rest -= 4;
        }
        if (static_cast<int32_t>(rest) >= 2) {
            std::memcpy(dest, &pattern, 2);
            dest += 2;
            rest -= 2;
        }
        if (rest) *dest = static_cast<uint8_t>(pattern);
        return pattern;
    }
    uint32_t pattern = (fill << 8) | fill;
    pattern |= pattern << 16;
    Com_Memset32(reinterpret_cast<uint32_t*>(dest), pattern, count >> 2);
    const uint32_t rest = count & 3;
    if (!rest) return count;
    uint8_t* tail = dest + (count & ~3u);
    if (rest >= 2) {
        std::memcpy(tail, &pattern, 2);
        if (rest != 2) tail[2] = static_cast<uint8_t>(pattern);
    } else {
        tail[0] = static_cast<uint8_t>(pattern);
    }
    return pattern;
}

PLATFORM_ASM_ENTRY(Cvar_Update_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Cvar_Update) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Cvar_Update(uint8_t* vm_cvar) {
    uint32_t& handle = *reinterpret_cast<uint32_t*>(vm_cvar);
    if (handle >= title_global<uint32_t>(0x009476f8))  // cvar_numIndexes
        title_function<Break>(kBreak)(1, title_pointer<const char>(0x00837ad8));  // "Cvar_Update: handle out of range"
    uint8_t* cv = title_pointer<uint8_t>(0x00940a10 + handle * 0x2c);  // cvar_indexes
    const int32_t modified = *reinterpret_cast<int32_t*>(cv + 0x18);
    if (modified == *reinterpret_cast<int32_t*>(vm_cvar + 4)) return;
    const char* string = *reinterpret_cast<const char**>(cv + 4);
    if (!string) return;
    *reinterpret_cast<int32_t*>(vm_cvar + 4) = modified;
    if (std::strlen(string) + 1 > 0x80)
        title_function<void XB_CDECL(uint32_t, const char*, const char*, uint32_t, uint32_t)>(kBreak)(
            1, title_pointer<const char>(0x00837a98), string, static_cast<uint32_t>(std::strlen(string)), 0x80);
    char* dst = reinterpret_cast<char*>(vm_cvar + 0x10);
    title_function<Strncpy>(kStrncpy)(dst, *reinterpret_cast<const char**>(cv + 4), 0x7f);
    dst[0x7f] = 0;
    std::memcpy(vm_cvar + 8, cv + 0x1c, 4);
    std::memcpy(vm_cvar + 0xc, cv + 0x20, 4);
}

void XB_CDECL Com_BeginParseSession(const char* name) {
    int32_t& depth = title_global<int32_t>(0x00927f54);
    if (depth == 0xf)
        title_function<Break>(kBreak)(0, title_pointer<const char>(0x008338f0));  // "Com_BeginParseSession: session overflow"
    ++depth;
    uint8_t* info = title_pointer<uint8_t>(0x008d3ae0 + depth * 0x11c);
    title_global<uint8_t*>(0x008d4ca0) = info;
    *reinterpret_cast<uint32_t*>(info + 0x80) = 1;
    *reinterpret_cast<uint32_t*>(info + 0x84) = 0;
    *reinterpret_cast<uint32_t*>(info + 0x88) = 1;
    *reinterpret_cast<uint32_t*>(info + 0x8c) = 0;
    char* dst = reinterpret_cast<char*>(info + 0x9c);
    title_function<Strncpy>(kStrncpy)(dst, name, 0x7f);
    dst[0x7f] = 0;
}

void XB_REGS1 XB_CDECL Com_ParseCommandLine(char* command_line) {
    char** lines = title_pointer<char*>(0x0093fcf8);  // com_consoleLines
    int32_t& count = title_global<int32_t>(0x009403e0);  // com_numConsoleLines
    lines[0] = command_line;
    int32_t n = 1;
    count = 1;
    if (!*command_line) return;
    for (char* p = command_line; *p; ++p) {
        if (*p == '+' || *p == '\n') {
            if (n == 0x20) break;
            lines[n++] = p + 1;
            *p = 0;
        }
    }
    count = n;
}

namespace {

int32_t& msg_word(uint8_t* msg, uint32_t offset) { return *reinterpret_cast<int32_t*>(msg + offset); }
uint8_t* msg_data(uint8_t* msg) { return *reinterpret_cast<uint8_t**>(msg + 4); }

}  // namespace

PLATFORM_ASM_ENTRY(MSG_ReadString_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(MSG_ReadString) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL MSG_ReadString(uint8_t* msg) {
    char* out = title_pointer<char>(0x0098b4c8);
    const char* src = reinterpret_cast<const char*>(msg_data(msg) + msg_word(msg, 0x10));
    const int32_t length = static_cast<int32_t>(std::strlen(src));
    if (length >= 0x100 || msg_word(msg, 0x10) + length >= msg_word(msg, 0xc)) {
        out[0] = 0;
        return out;
    }
    std::memcpy(out, src, static_cast<uint32_t>(length) + 1);
    msg_word(msg, 0x10) += static_cast<int32_t>(std::strlen(out)) + 1;
    return out;
}

namespace {

// The four bytes at the read position (readcount advances by 4); false when that passes cursize.
bool msg_read4(uint8_t* msg, uint8_t* bytes) {
    for (int32_t i = 0; i < 4; ++i) bytes[i] = msg_data(msg)[msg_word(msg, 0x10)++];
    return msg_word(msg, 0x10) <= msg_word(msg, 0xc);
}

}  // namespace

PLATFORM_ASM_ENTRY(MSG_ReadFloat_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(MSG_ReadFloat) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL MSG_ReadFloat(uint8_t* msg) {
    uint8_t bytes[4];
    float value;
    if (!msg_read4(msg, bytes)) return title_global<float>(0x0083eb34);  // -1
    std::memcpy(&value, bytes, 4);
    return value;
}

int32_t XB_REGS1 XB_CDECL MSG_ReadLong(uint8_t* msg) {
    uint8_t bytes[4];
    int32_t value;
    if (!msg_read4(msg, bytes)) return -1;
    std::memcpy(&value, bytes, 4);
    return value;
}

void XB_REGS1 XB_CDECL MSG_WriteLong(uint8_t* msg, int32_t value) {
    const uint32_t v = static_cast<uint32_t>(value);
    for (int32_t i = 0; i < 4; ++i) msg_data(msg)[msg_word(msg, 0xc)++] = static_cast<uint8_t>(v >> (i * 8));
}

namespace {

// repz cmpsb's sign: 0, or -1/1 by the first differing byte (unsigned).
int32_t compare_bytes(const uint8_t* a, const uint8_t* b, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

}  // namespace

int32_t XB_REGS2 XB_CDECL NET_CompareAdr(const uint8_t* a, const uint8_t* b) {
    int32_t type_a, type_b;
    std::memcpy(&type_a, a, 4);
    std::memcpy(&type_b, b, 4);
    if (type_a != type_b) return type_a - type_b;
    if (type_a == 2) return 0;
    if (type_a != 4 && type_a != 5) return 0;
    uint16_t port_a, port_b;
    std::memcpy(&port_a, a + 0x12, 2);
    std::memcpy(&port_b, b + 0x12, 2);
    if (port_a != port_b) return static_cast<int32_t>(port_a) - static_cast<int32_t>(port_b);
    return type_a == 4 ? compare_bytes(a + 4, b + 4, 4) : compare_bytes(a + 8, b + 8, 10);
}

PLATFORM_ASM_ENTRY(NET_GetLoopPacket_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NET_GetLoopPacket) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL NET_GetLoopPacket(int32_t sock, uint8_t* msg, uint32_t* from) {
    uint8_t* loop = title_pointer<uint8_t>(0x00928590 + sock * 0x93c);  // loopbacks
    int32_t& get = msg_word(loop, 0x934);
    const int32_t send = msg_word(loop, 0x938);
    if (send - get > 1) get = send - 1;
    if (get >= send) return 0;
    ++get;
    const int32_t length = msg_word(loop, 0x930);
    std::memcpy(msg_data(msg), loop, static_cast<uint32_t>(length));
    msg_word(msg, 0xc) = msg_word(loop, 0x930);
    for (int32_t i = 0; i < 5; ++i) from[i] = 0;
    from[0] = 2;  // NA_LOOPBACK
    return 1;
}

int32_t XB_REGS3 XB_CDECL FS_ReturnPath(uint32_t, uint32_t, const char* name, char* path, int32_t* depth) {
    int32_t new_depth = 0;
    int32_t length = 0;
    int32_t at = 0;
    path[0] = 0;
    for (; name[at]; ++at) {
        if (name[at] == '/' || name[at] == '\\') {
            length = at;
            ++new_depth;
        }
    }
    std::memcpy(path, name, std::strlen(name) + 1);
    path[length] = 0;
    if (length + 1 == at) --new_depth;
    *depth = new_depth;
    return length;
}

PLATFORM_ASM_ENTRY(FS_CreatePath_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FS_CreatePath) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL FS_CreatePath(char* path) {
    using Strstr = char* XB_CDECL(const char*, const char*);
    if (title_function<Strstr>(0x001efb10)(path, title_pointer<const char>(0x008380a4))) return 1;  // ".."
    if (title_function<Strstr>(0x001efb10)(path, title_pointer<const char>(0x008380a0))) return 1;  // "::"
    for (char* p = path + 1; *p; ++p)
        if (*p == '\\') *p = '\\';
    return 0;
}

int32_t XB_REGS1 XB_CDECL FS_HandleForFile(uint32_t flag) {
    const int32_t first = flag ? 3 : 1;
    const int32_t count = flag ? 0 : 2;
    for (int32_t i = 0; i < count; ++i)
        if (!title_global<uint32_t>(0x009477a0 + (first + i) * 0x120)) return first + i;  // fsh[].handleFiles.file
    title_function<Break>(kBreak)(1, title_pointer<const char>(0x008380d4));  // "FS_HandleForFile: none free"
    return -1;
}

int32_t XB_REGS2 XB_CDECL FS_FilenameCompare(const char* s1, const char* s2) {
    int32_t c1, c2;
    do {
        c2 = *s2++;
        c1 = *s1++;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 0x20;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 0x20;
        if (c2 == '\\' || c2 == ':') c2 = '/';
        if (c1 == '\\' || c1 == ':') c1 = '/';
        if (c2 != c1) return -1;
    } while (c2);
    return 0;
}

uint32_t XB_REGS1 XB_CDECL FS_HashFileName(const char* name, int32_t hash_size) {
    uint32_t sum = 0;
    for (int32_t i = 0; name[i]; ++i) {
        int32_t letter = title_function<int32_t XB_CDECL(int32_t)>(0x001eed48)(name[i]);  // tolower
        if (letter == '.') break;
        if (letter == '\\') letter = '/';
        sum += static_cast<uint32_t>(letter) * static_cast<uint32_t>(i + 119);
    }
    const int32_t hash = static_cast<int32_t>(sum);
    return static_cast<uint32_t>((((hash >> 10) ^ hash) >> 10) ^ hash) & static_cast<uint32_t>(hash_size - 1);
}

void XB_REGS1 XB_CDECL Cmd_TokenizeString(const char* text, uint32_t max_tokens) {
    int32_t& argc = title_global<int32_t>(0x00934248);
    const char** argv = title_pointer<const char*>(0x0092df90);
    char* out = title_pointer<char>(0x0092e9e8);  // cmd_tokenized
    argc = 0;
    if (!text) return;
    for (;;) {
        if (--max_tokens == 0) {
            // The last allowed token: the rest of the line as it is.
            if (!*text) return;
            argv[argc++] = out;
            while (*text) *out++ = *text++;
            *out = 0;
            return;
        }
        for (;;) {
            if (!*text) return;
            while (*text <= ' ') {
                if (!*++text) return;
            }
            if (text[0] == '/' && text[1] == '/') return;
            if (text[0] == '/' && text[1] == '*') {
                while (!(text[0] == '*' && text[1] == '/')) {
                    if (!*++text) return;
                }
                text += 2;
                continue;
            }
            break;
        }
        argv[argc++] = out;
        if (*text == '"') {
            ++text;
            while (*text && *text != '"') *out++ = *text++;
            *out++ = 0;
            if (!*text) return;
            ++text;
        } else {
            while (*text > ' ') {
                if (*text == '"') break;
                if (text[0] == '/' && (text[1] == '/' || text[1] == '*')) break;
                *out++ = *text++;
            }
            *out++ = 0;
        }
        if (!*text) return;
        if (*text <= ' ') ++text;
        if (argc == 0x200) return;
    }
}

PLATFORM_ASM_ENTRY(Info_RemoveKey_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Info_RemoveKey) "addl $8, %esp\n"
                   "ret")

namespace {

void info_remove_key(char* info, const char* key, uint32_t message) {
    if (std::strlen(info) >= 0x400) title_function<Break>(kBreak)(1, title_pointer<const char>(message));
    if (title_function<char* XB_CDECL(const char*, int32_t)>(0x001ef5e0)(key, '\\')) return;  // strchr
    char pkey[0x400];
    char value[0x400];
    char* s = info;
    for (;;) {
        char* start = s;
        if (*s == '\\') ++s;
        char* o = pkey;
        while (*s != '\\') {
            if (!*s) return;
            *o++ = *s++;
        }
        ++s;
        *o = 0;
        o = value;
        while (*s != '\\' && *s) *o++ = *s++;
        *o = 0;
        if (!std::strcmp(key, pkey)) {
            char* dst = start;
            while ((*dst++ = *s++) != 0) {
            }
            return;
        }
        if (!*s) return;
    }
}

}  // namespace

void XB_CDECL Info_RemoveKey(char* info, const char* key) {
    info_remove_key(info, key, 0x008337e0);  // "Info_RemoveKey: oversize infostring"
}

PLATFORM_ASM_ENTRY(Info_RemoveKey_Big_entry,
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Info_RemoveKey_Big) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Info_RemoveKey_Big(char* info, const char* key) {
    info_remove_key(info, key, 0x008337b4);
}

uint32_t XB_CDECL Cvar_Set(const char* name, const char* value) {
    return title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(name, value, 1);  // Cvar_Set2
}

namespace {

// Cmd_AddCommand's body on the list at `list`.
void add_command(uint32_t list, const char* name, uint32_t function) {
    using ZMalloc = void* XB_CDECL(uint32_t);
    uint32_t*& head = title_global<uint32_t*>(list);
    for (uint32_t* cmd = head; cmd; cmd = reinterpret_cast<uint32_t*>(uintptr_t{cmd[0]}))
        if (!std::strcmp(name, reinterpret_cast<const char*>(uintptr_t{cmd[1]}))) return;
    uint32_t* cmd = static_cast<uint32_t*>(title_function<ZMalloc>(0x00042c10)(0xc));  // Z_Malloc
    Com_Memset(reinterpret_cast<uint8_t*>(cmd), 0, 0xc);
    const uint32_t size = static_cast<uint32_t>(std::strlen(name)) + 1;
    char* copy = static_cast<char*>(title_function<ZMalloc>(0x00042c10)(size));
    Com_Memset(reinterpret_cast<uint8_t*>(copy), 0, size);
    std::memcpy(copy, name, size);
    cmd[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy));
    cmd[2] = function;
    cmd[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head));
    head = cmd;
}

}  // namespace

void XB_CDECL Cmd_AddCommand(const char* name, uint32_t function) {
    add_command(0x00929b04, name, function);  // cmd_functions
}

void XB_CDECL Cmd_AddServerCommand(const char* name, uint32_t function) {
    Cmd_AddCommand(name, 0x002aaae0);
    add_command(0x009284e4, name, function);
}

void XB_CDECL Cmd_RemoveCommand(const char* name) {
    using ZFree = void XB_CDECL(void*);
    uint32_t* back = title_pointer<uint32_t>(0x00929b04);  // cmd_functions
    for (uint32_t* cmd = reinterpret_cast<uint32_t*>(uintptr_t{*back}); cmd;
         cmd = reinterpret_cast<uint32_t*>(uintptr_t{cmd[0]})) {
        if (!std::strcmp(name, reinterpret_cast<const char*>(uintptr_t{cmd[1]}))) {
            *back = cmd[0];
            if (cmd[1]) title_function<ZFree>(0x000428d0)(reinterpret_cast<void*>(uintptr_t{cmd[1]}));  // Z_Free
            title_function<ZFree>(0x000428d0)(cmd);
            return;
        }
        back = cmd;
    }
}

void XB_CDECL Mem_AlignedFree(void* pointer) {
    using Free = void XB_CDECL(void*);
    --title_global<uint32_t>(0x009098c4);
    if (const uint32_t free = title_global<uint32_t>(0x009098ec)) {
        title_function<Free>(free)(pointer);
        return;
    }
    if (static_cast<int32_t>(reinterpret_cast<uintptr_t>(pointer)) < 0)
        title_function<void XB_STDCALL(void*)>(0x001e76ba)(pointer);  // XPhysicalFree
    else
        title_function<Free>(0x001f113a)(pointer);  // _aligned_free
}

void XB_STDCALL Mem_AlignedFreeCall(void* pointer) { Mem_AlignedFree(pointer); }

void* XB_CDECL Z_MallocDebug(uint32_t size) {
    return title_function<void* XB_CDECL(uint32_t)>(0x00042cb0)(size);  // Z_Malloc
}

void XB_CDECL FS_FreeFile(void* buffer) {
    if (!buffer) title_function<Break>(kBreak)(0, title_pointer<const char>(0x00837f10));  // "FS_FreeFile( NULL )"
    --title_global<int32_t>(0x00940a0c);  // fs_loadStack
    title_function<void XB_CDECL(void*)>(0x000428d0)(buffer);  // Z_Free
}

void XB_CDECL Cvar_Reset_f() {
    if (title_global<int32_t>(0x00934248) != 2) return;  // cmd_argc
    title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
        title_global<const char*>(0x0092df94), nullptr, 0);
}

void XB_CDECL Cvar_SetCheatState() {
    for (uint8_t* var = title_global<uint8_t*>(0x0093fcec); var; var = *reinterpret_cast<uint8_t**>(var + 0x24)) {
        if (!(*reinterpret_cast<uint32_t*>(var + 0x10) & 0x200)) continue;  // CVAR_CHEAT
        const char* reset = *reinterpret_cast<const char**>(var + 8);
        if (!std::strcmp(reset, *reinterpret_cast<const char**>(var + 4))) continue;
        title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
            *reinterpret_cast<const char**>(var), reset, 1);
    }
}

uint32_t XB_CDECL FS_ListFiles(const char* path, const char* extension, int32_t* count) {
    return title_function<uint32_t XB_CDECL(const char*, const char*, const char*, int32_t*)>(0x0029ea80)(  // FS_ListFilteredFiles
        path, extension, nullptr, count);
}

int32_t XB_REGS3 XB_CDECL Com_ModifyMsec(uint32_t, uint32_t, int32_t msec) {
    const int32_t fixed = *reinterpret_cast<int32_t*>(title_global<uint8_t*>(0x0093fcc0) + 0x20);  // com_fixedtime
    const float scale = *reinterpret_cast<float*>(title_global<uint8_t*>(0x0093fce4) + 0x1c);  // com_timescale
    if (fixed)
        msec = fixed;
    else if (scale != 0.0f)
        msec = static_cast<int32_t>(static_cast<float>(msec) * scale);
    if (msec < 1 && scale != 0.0f) msec = 1;
    if (msec > 100) msec = 100;
    if (*reinterpret_cast<int32_t*>(title_global<uint8_t*>(0x0093fcd8) + 0x20)) return 0;
    return msec;
}

PLATFORM_ASM_ENTRY(Com_PushEvent_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Com_PushEvent) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Com_PushEvent(const uint32_t* event) {
    int32_t& head = title_global<int32_t>(0x0095f588);  // com_pushedEventsHead
    int32_t& tail = title_global<int32_t>(0x0095f58c);  // com_pushedEventsTail
    uint32_t* slot = title_pointer<uint32_t>(0x00940400 + (head & 0x3f) * 0x18);  // com_pushedEvents
    if (head - tail >= 0x40) {
        if (!title_global<uint32_t>(0x0098ad44)) title_global<uint32_t>(0x0098ad44) = 1;
        if (slot[5]) title_function<void XB_CDECL(void*)>(0x000428d0)(reinterpret_cast<void*>(uintptr_t{slot[5]}));  // Z_Free
        ++tail;
    } else {
        title_global<uint32_t>(0x0098ad44) = 0;
    }
    for (int32_t i = 0; i < 6; ++i) slot[i] = event[i];
    ++head;
}

void XB_CDECL Cmd_Wait_f() {
    int32_t& wait = title_global<int32_t>(0x00929bfc);  // cmd_wait
    if (title_global<int32_t>(0x00934248) == 2)
        wait = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(title_global<const char*>(0x0092df94));  // atol
    else
        wait = 1;
}

PLATFORM_ASM_ENTRY(NET_AdrToString_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NET_AdrToString) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL NET_AdrToString(const uint8_t* adr) {
    char* out = title_pointer<char>(0x0098b388);
    uint32_t type;
    std::memcpy(&type, adr, 4);
    if (type == 2) {
        const uint32_t args[1] = {0x00833c00};  // "loopback"
        Com_sprintf(out, 0x40, args);
        return out;
    }
    uint32_t port_word;  // the original pushes the dword at +0x12 (two bytes past the record)
    std::memcpy(&port_word, adr + 0x12, 4);
    const uint32_t port = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(
        title_function<uint32_t XB_CDECL(uint32_t)>(title_global<uint32_t>(0x0092f138))(port_word))));  // _BigShort
    if (type == 4) {
        const uint32_t args[6] = {0x00833bf0, adr[4], adr[5], adr[6], adr[7], port};  // "%i.%i.%i.%i:%i"
        Com_sprintf(out, 0x40, args);
        return out;
    }
    uint32_t args[12] = {0x00833bc0};  // "%02x%02x%02x%02x.%02x%02x%02x%02x%02x%02x:%i"
    for (int32_t i = 0; i < 10; ++i) args[1 + i] = adr[8 + i];
    args[11] = port;
    Com_sprintf(out, 0x40, args);
    return out;
}

PLATFORM_ASM_ENTRY(Cvar_InfoString_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Cvar_InfoString) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL Cvar_InfoString(uint32_t bits) {
    char* info = title_pointer<char>(0x0093f818);
    info[0] = 0;
    for (uint8_t* var = title_global<uint8_t*>(0x0093fcec); var; var = *reinterpret_cast<uint8_t**>(var + 0x24)) {
        if (!(*reinterpret_cast<uint32_t*>(var + 0x10) & bits)) continue;
        title_function<void XB_REGS3 XB_CDECL(const char*, uint32_t, const char*, char*)>(0x002aff40)(  // Info_SetValueForKey
            *reinterpret_cast<const char**>(var + 4), 0, *reinterpret_cast<const char**>(var), info);
    }
    return info;
}

PLATFORM_ASM_ENTRY(Cvar_InfoString_Big_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Cvar_InfoString_Big) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL Cvar_InfoString_Big(uint32_t bits) {
    char* info = title_pointer<char>(0x0093fd78);
    info[0] = 0;
    for (uint8_t* var = title_global<uint8_t*>(0x0093fcec); var; var = *reinterpret_cast<uint8_t**>(var + 0x24)) {
        if (!(*reinterpret_cast<uint32_t*>(var + 0x10) & bits)) continue;
        title_function<void XB_REGS1 XB_CDECL(const char*, char*, const char*)>(0x002afdb0)(  // Info_SetValueForKey_Big
            *reinterpret_cast<const char**>(var), info, *reinterpret_cast<const char**>(var + 4));
    }
    return info;
}

int32_t XB_CDECL Cvar_WriteNamedValues(const char* const* names, int32_t count, char* buffer, int32_t size) {
    using Snprintf = int32_t XB_CDECL(char*, int32_t, const char*, const char*, const char*);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* var = title_function<const uint8_t* XB_CDECL(const char*)>(0x00299710)(names[i]);  // Cvar_FindVar
        const char* value = var ? *reinterpret_cast<const char* const*>(var + 4) : title_pointer<const char>(0x0082dfeb);
        const int32_t written = title_function<Snprintf>(0x001eebff)(  // _snprintf
            buffer, size, title_pointer<const char>(0x00834544), names[i], value);  // "%s \"%s\"\n"
        if (written < 0) return 0;
        buffer += written;
        size -= written;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(MSG_WriteString_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(MSG_WriteString) "addl $8, %esp\n"
                   "ret")

void XB_CDECL MSG_WriteString(const char* text, uint8_t* msg) {
    if (!text) {
        msg_data(msg)[msg_word(msg, 0xc)++] = 0;
        return;
    }
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    if (length >= 0x100) {
        msg_data(msg)[msg_word(msg, 0xc)++] = 0;
        return;
    }
    char copy[0x100];
    std::memcpy(copy, text, static_cast<uint32_t>(length));
    copy[length] = 0;
    std::memcpy(msg_data(msg) + msg_word(msg, 0xc), copy, static_cast<uint32_t>(length) + 1);
    msg_word(msg, 0xc) += length + 1;
}

uint8_t* XB_CDECL Cvar_FindVar(const char* name) {
    const int32_t hash = title_function<int32_t XB_REGS1 XB_CDECL(const char*)>(0x00296f60)(name);  // generateHashValue
    for (uint8_t* var = title_global<uint8_t*>(0x009472f8 + hash * 4); var;
         var = *reinterpret_cast<uint8_t**>(var + 0x28)) {
        const char* var_name = *reinterpret_cast<const char**>(var);
        if (name && var_name && !Q_stricmpn(name, var_name, 99999)) return var;
    }
    return nullptr;
}

void XB_CDECL Cvar_SetValue(const char* name, float value) {
    char text[0x20];
    const int32_t integer = static_cast<int32_t>(value);
    if (static_cast<float>(integer) == value) {
        const uint32_t args[2] = {0x0082ded4, static_cast<uint32_t>(integer)};  // "%i"
        Com_sprintf(text, 0x20, args);
    } else {
        const double d = static_cast<double>(value);
        uint32_t args[3] = {0x008325b4};  // "%f"
        std::memcpy(&args[1], &d, 8);
        Com_sprintf(text, 0x20, args);
    }
    title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(name, text, 1);  // Cvar_Set2
}

void XB_REGS1 XB_CDECL Cvar_SetByVmIndex(const char* value, uint8_t* vm_cvar) {
    const uint32_t handle = *reinterpret_cast<uint32_t*>(vm_cvar);
    title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(  // Cvar_Set2
        title_global<const char*>(0x00940a10 + handle * 0x2c), value, 1);
    Cvar_Update(vm_cvar);
}

PLATFORM_ASM_ENTRY(Info_ValueForKey_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Info_ValueForKey) "addl $8, %esp\n"
                   "ret")

const char* XB_CDECL Info_ValueForKey(const char* info, const char* key) {
    const char* empty = title_pointer<const char>(0x0082dfeb);
    if (!info || !key) return empty;
    if (std::strlen(info) >= 0x400)
        title_function<Break>(kBreak)(1, title_pointer<const char>(0x00833808));  // "Info_ValueForKey: oversize infostring"
    uint32_t& index = title_global<uint32_t>(0x0098ad28);
    index ^= 1;
    char pkey[0x2000];
    const char* s = info;
    if (*s == '\\') ++s;
    for (;;) {
        char* o = pkey;
        while (*s != '\\') {
            if (!*s) return empty;
            *o++ = *s++;
        }
        ++s;
        *o = 0;
        char* value = title_pointer<char>(0x00927320 + (index << 10));
        o = value;
        while (*s != '\\' && *s) *o++ = *s++;
        *o = 0;
        if (!Q_stricmpn(key, pkey, 99999)) return value;
        if (!*s) return empty;
        ++s;
    }
}

void XB_REGS3 XB_CDECL Info_SetValueForKey(const char* value, uint32_t, const char* key, char* info) {
    using Strchr = char* XB_CDECL(const char*, int32_t);
    using BreakKv = void XB_CDECL(uint32_t, const char*, const char*, const char*);
    char cleaned[0x400];
    int32_t length = 0;
    for (int32_t i = 0; value[i] && i < 0x3ff; ++i) {
        const char c = value[i];
        if (c == '\\' || c == ';' || c == '"') continue;
        cleaned[length++] = c;
    }
    cleaned[length] = 0;
    if (title_function<Strchr>(0x001ef5e0)(key, '\\'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x00833784), key, value);
    if (title_function<Strchr>(0x001ef5e0)(key, ';'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x0083374c), key, value);
    if (title_function<Strchr>(0x001ef5e0)(key, '"'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x0083371c), key, value);
    Info_RemoveKey(info, key);
    if (!cleaned[0]) return;
    char pair[0x400];
    const uint32_t args[3] = {0x00833714, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)),  // "\\%s\\%s"
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cleaned))};
    Com_sprintf(pair, 0x400, args);
    std::memcpy(info + std::strlen(info), pair, std::strlen(pair) + 1);
}

void XB_REGS3 XB_CDECL FS_BuildOSPath(const char* game, uint32_t, const char* base, const char* qpath, char* ospath,
                                      uint32_t quiet) {
    if (!game || !*game) game = title_pointer<const char>(0x0093fc30);  // fs_gamedir
    const int32_t base_length = static_cast<int32_t>(std::strlen(base));
    int32_t game_length = static_cast<int32_t>(std::strlen(game));
    const int32_t qpath_length = static_cast<int32_t>(std::strlen(qpath));
    if (qpath_length + game_length + base_length + 2 >= 0x80) {
        if (quiet) {
            ospath[0] = 0;
            return;
        }
        title_function<Break>(kBreak)(0, title_pointer<const char>(0x008380a8));  // "FS_BuildOSPath: os path length exceeded"
    }
    std::memcpy(ospath, base, static_cast<uint32_t>(base_length));
    char* separator = ospath + base_length;
    *separator = '/';
    std::memcpy(separator + 1, game, static_cast<uint32_t>(game_length));
    if (game_length)
        ospath[game_length + base_length + 1] = '/';
    else
        game_length = -1;
    std::memcpy(ospath + game_length + base_length + 2, qpath, static_cast<uint32_t>(qpath_length) + 1);
    for (char* p = separator; *p; ++p)
        if (*p == '/' || *p == '\\') *p = '\\';
}

void XB_REGS1 XB_CDECL FS_FreePackList(uint8_t* pack) {
    using ZFree = void XB_CDECL(void*);
    while (pack) {
        uint8_t* next = *reinterpret_cast<uint8_t**>(pack + 0x90);
        title_function<ZFree>(0x000428d0)(*reinterpret_cast<void**>(pack + 0x8c));  // Z_Free
        title_function<ZFree>(0x000428d0)(pack);
        pack = next;
    }
}

void XB_CDECL FS_FreeFileList(char** list) {
    using ZFree = void XB_CDECL(void*);
    if (!list) return;
    for (char** p = list; *p; ++p) title_function<ZFree>(0x000428d0)(*p);  // Z_Free
    title_function<ZFree>(0x000428d0)(list);
}

PLATFORM_ASM_ENTRY(FS_AddFileToList_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FS_AddFileToList) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL FS_AddFileToList(const char* name, char** list, int32_t count) {
    if (count == 0xfff) return count;
    for (int32_t i = 0; i < count; ++i)
        if (name && list[i] && !Q_stricmpn(name, list[i], 99999)) return count;
    const uint32_t size = static_cast<uint32_t>(std::strlen(name)) + 1;
    char* copy = static_cast<char*>(title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(size));  // Z_Malloc
    Com_Memset(reinterpret_cast<uint8_t*>(copy), 0, size);
    std::memcpy(copy, name, size);
    list[count] = copy;
    return count + 1;
}

void XB_REGS1 XB_CDECL Cmd_ExecuteRegisteredCommand(const char* text) {
    title_global<const char*>(0x00927f50) = text;
    Cmd_TokenizeString(text, 0);
    if (!title_global<int32_t>(0x00934248)) return;  // cmd_argc
    uint32_t* prev = title_pointer<uint32_t>(0x009284e4);
    if (!*prev) return;
    for (;;) {
        uint32_t* cmd = reinterpret_cast<uint32_t*>(uintptr_t{*prev});
        const char* argv0 = title_global<const char*>(0x0092df90);
        const char* name = reinterpret_cast<const char*>(uintptr_t{cmd[1]});
        if (argv0 && name && !Q_stricmpn(argv0, name, 99999)) {
            *prev = cmd[0];
            cmd[0] = title_global<uint32_t>(0x009284e4);
            title_global<uint32_t*>(0x009284e4) = cmd;
            if (cmd[2]) title_function<void XB_CDECL()>(cmd[2])();
            return;
        }
        if (!cmd[0]) return;
        prev = cmd;
    }
}

int32_t XB_CDECL Com_AddStartupCommands() {
    int32_t added = 0;
    for (int32_t i = 0; i < title_global<int32_t>(0x009403e0); ++i) {  // com_numConsoleLines
        const char* line = title_global<const char*>(0x0093fcf8 + i * 4);  // com_consoleLines
        if (!line || !*line) continue;
        if (Q_stricmpn(line, title_pointer<const char>(0x00837d70), 3)) added = 1;  // "set"
        // Cbuf_AddText(line) and "\n", inlined.
        line = title_global<const char*>(0x0093fcf8 + i * 4);
        const int32_t length = static_cast<int32_t>(std::strlen(line));
        char* data = title_global<char*>(0x00935328);
        int32_t& cursize = title_global<int32_t>(0x00935330);
        int32_t size = cursize;
        if (size + length < title_global<int32_t>(0x0093532c)) {
            std::memcpy(data + size, line, static_cast<uint32_t>(length));
            size += length;
            cursize = size;
        }
        if (size + 1 < title_global<int32_t>(0x0093532c)) {
            data[size] = title_global<char>(0x0082d288);  // '\n'
            cursize = size + 1;
        }
    }
    return added;
}

void XB_CDECL Cvar_Shutdown() {
    using ZFree = void XB_CDECL(void*);
    for (uint32_t bucket = 0; bucket < 0x400; bucket += 4) {
        uint8_t*& head = title_global<uint8_t*>(0x009472f8 + bucket);  // hashTable
        for (uint8_t* var = head; var; var = *reinterpret_cast<uint8_t**>(var + 0x28)) {
            static const uint32_t kStrings[3] = {0xc, 4, 8};  // latched, string, reset
            for (int32_t i = 0; i < 3; ++i) {
                void*& string = *reinterpret_cast<void**>(var + kStrings[i]);
                if (string) {
                    title_function<ZFree>(0x000428d0)(string);  // Z_Free
                    string = nullptr;
                }
            }
            title_function<ZFree>(0x000428d0)(*reinterpret_cast<void**>(var));
            *reinterpret_cast<void**>(var) = nullptr;
        }
        head = nullptr;
    }
    title_global<uint32_t>(0x009476f8) = 0;  // cvar_numIndexes
    title_global<uint32_t>(0x0093fcec) = 0;  // cvar_vars
    title_global<uint32_t>(0x00940358) = 0;
    title_global<uint32_t>(0x009403ec) = 0;  // cvar_modifiedFlags
}

uint32_t XB_CDECL Swap_Init() {
    title_global<uint32_t>(0x0092f138) = 0x002a97c0;  // _BigShort = ShortSwap
    title_global<uint32_t>(0x0092f134) = 0x002a97b0;  // _LittleShort = ShortNoSwap
    title_global<uint32_t>(0x00933e38) = 0x002a9780;  // _BigLong = LongSwap
    title_global<uint32_t>(0x00927de0) = 0x002a9770;  // _LittleLong = LongNoSwap
    title_global<uint32_t>(0x00929b10) = 0x002a9710;  // _BigLong64 = Long64Swap
    title_global<uint32_t>(0x00929c24) = 0x002a9700;  // _LittleLong64 = Long64NoSwap
    title_global<uint32_t>(0x00934a58) = 0x002a96c0;  // _BigFloat = FloatSwap
    title_global<uint32_t>(0x00929b20) = 0x002a96b0;  // _LittleFloat = FloatNoSwap
    return 1;
}

void XB_REGS1 XB_CDECL Info_SetValueForKey_Big(const char* key, char* info, const char* value) {
    using Strchr = char* XB_CDECL(const char*, int32_t);
    using BreakKv = void XB_CDECL(uint32_t, const char*, const char*, const char*);
    if (std::strlen(info) >= 0x400)
        title_function<Break>(kBreak)(1, title_pointer<const char>(0x008336e8));  // "Info_SetValueForKey: oversize infostring"
    char cleaned[0x400];
    int32_t length = 0;
    for (int32_t i = 0; value[i] && i < 0x3ff; ++i) {
        const char c = value[i];
        if (c == '\\' || c == ';' || c == '"') continue;
        cleaned[length++] = c;
    }
    cleaned[length] = 0;
    if (title_function<Strchr>(0x001ef5e0)(key, '\\'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x00833784), key, value);
    if (title_function<Strchr>(0x001ef5e0)(key, ';'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x0083374c), key, value);
    if (title_function<Strchr>(0x001ef5e0)(key, '"'))
        title_function<BreakKv>(kBreak)(1, title_pointer<const char>(0x0083371c), key, value);
    Info_RemoveKey_Big(info, key);
    if (!cleaned[0]) return;
    char pair[0x400];
    const uint32_t args[3] = {0x00833714, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)),  // "\\%s\\%s"
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cleaned))};
    Com_sprintf(pair, 0x400, args);
    if (std::strlen(pair) + std::strlen(info) > 0x400)
        title_function<void XB_CDECL(uint32_t, const char*, const char*, const char*, const char*)>(kBreak)(
            1, title_pointer<const char>(0x008336a0), key, value, info);  // "Info string length exceeded ..."
    std::memcpy(info + std::strlen(info), pair, std::strlen(pair) + 1);
}

void XB_CDECL Cvar_Init() {
    Cmd_AddCommand(title_pointer<const char>(0x00837a64), 0x0029e4d0);  // "toggle", Cvar_Toggle_f
    Cmd_AddCommand(title_pointer<const char>(0x00837d70), 0x0029e3d0);  // "set", Cvar_Set_f
    Cmd_AddCommand(title_pointer<const char>(0x00837a5c), 0x0029fd20);  // "sets", Cvar_SetS_f
    Cmd_AddCommand(title_pointer<const char>(0x00837a54), 0x0029fce0);  // "seta", Cvar_SetA_f
    Cmd_AddCommand(title_pointer<const char>(0x00837a48), 0x0029e380);  // "setfromcvar"
    Cmd_AddCommand(title_pointer<const char>(0x00837a40), 0x0029fcc0);  // "reset", Cvar_Reset_f
    Cmd_AddCommand(title_pointer<const char>(0x00837d74), 0x0029fc30);  // "cvar_restart", Cvar_Restart_f
}

void XB_CDECL Cmd_Init() {
    Cmd_AddCommand(title_pointer<const char>(0x00833ca8), 0x002aa870);  // "cmdlist", Cmd_List_f
    Cmd_AddCommand(title_pointer<const char>(0x00833ca0), 0x002cae20);  // "exec", Cmd_Exec_f
    Cmd_AddCommand(title_pointer<const char>(0x00833c98), 0x002c0d30);  // "vstr", Cmd_Vstr_f
    Cmd_AddCommand(title_pointer<const char>(0x00833c90), 0x002aaad0);  // "echo", Cmd_Echo_f
    Cmd_AddCommand(title_pointer<const char>(0x00833c88), 0x002aac40);  // "wait", Cmd_Wait_f
}

int32_t XB_REGS1 XB_CDECL generateHashValue(const char* name) {
    if (!name) title_function<Break>(kBreak)(1, title_pointer<const char>(0x00837b5c));  // "null name in generateHashValue"
    uint32_t hash = 0;
    for (int32_t i = 0; name[i]; ++i)
        hash += static_cast<uint32_t>(title_function<int32_t XB_CDECL(int32_t)>(0x001eed48)(name[i])) *  // tolower
                static_cast<uint32_t>(i + 119);
    return static_cast<int32_t>(hash & 0xff);
}

PLATFORM_ASM_ENTRY(CopyString_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CopyString) "addl $4, %esp\n"
                   "ret")

char* XB_CDECL CopyString(const char* text) {
    const uint32_t size = static_cast<uint32_t>(std::strlen(text)) + 1;
    char* copy = static_cast<char*>(title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(size));  // Z_Malloc
    Com_Memset(reinterpret_cast<uint8_t*>(copy), 0, size);
    std::memcpy(copy, text, size);
    return copy;
}

PLATFORM_ASM_ENTRY(SkipWhitespace_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SkipWhitespace) "addl $8, %esp\n"
                   "ret")

const char* XB_CDECL SkipWhitespace(const char* data, int32_t* has_new_lines) {
    while (*data <= ' ') {
        if (!*data) return nullptr;
        if (*data == '\n') {
            ++*reinterpret_cast<int32_t*>(title_global<uint8_t*>(0x008d4ca0) + 0x80);  // lines
            *has_new_lines = 1;
        }
        ++data;
    }
    return data;
}

void XB_REGS1 XB_CDECL Cbuf_InsertText(const char* text) {
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    const int32_t size = title_global<int32_t>(0x00935330);  // cmd_text.cursize
    if (size + length + 1 > title_global<int32_t>(0x0093532c)) return;
    char* data = title_global<char*>(0x00935328);
    for (int32_t i = size - 1; i >= 0; --i) data[i + length + 1] = data[i];
    std::memcpy(data, text, static_cast<uint32_t>(length));
    data[length] = '\n';
    title_global<int32_t>(0x00935330) = size + length + 1;
}

void XB_REGS1 XB_CDECL Cbuf_AddText(const char* text) {
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    const int32_t size = title_global<int32_t>(0x00935330);
    if (size + length >= title_global<int32_t>(0x0093532c)) return;
    std::memcpy(title_global<char*>(0x00935328) + size, text, static_cast<uint32_t>(length));
    title_global<int32_t>(0x00935330) = size + length;
}

namespace {

using CvarSet2 = uint32_t XB_CDECL(const char*, const char*, uint32_t);
constexpr uint32_t kCvarSet2 = 0x0029c030;

const char* cmd_argv_or_empty(uint32_t index) {
    if (title_global<uint32_t>(0x00934248) > index) return title_global<const char*>(0x0092df90 + index * 4);
    return title_pointer<const char>(0x0082dfeb);
}

void set_and_flag(uint32_t flag) {
    if (title_global<int32_t>(0x00934248) != 3) return;
    title_function<void XB_CDECL()>(0x0029e3d0)();  // Cvar_Set_f
    if (uint8_t* var = Cvar_FindVar(cmd_argv_or_empty(1))) *reinterpret_cast<uint32_t*>(var + 0x10) |= flag;
}

}  // namespace

void XB_CDECL Cvar_SetA_f() { set_and_flag(1); }

void XB_CDECL Cvar_SetS_f() { set_and_flag(4); }

void XB_CDECL Cvar_Restart_f() {
    using ZFree = void XB_CDECL(void*);
    uint8_t** prev = title_pointer<uint8_t*>(0x0093fcec);  // cvar_vars
    for (uint8_t* var = *prev; var; var = *prev) {
        const uint32_t flags = *reinterpret_cast<uint32_t*>(var + 0x10);
        if (!(flags & 0x450)) {
            if (flags & 0x80) {
                *prev = *reinterpret_cast<uint8_t**>(var + 0x24);
                static const uint32_t kStrings[4] = {0, 4, 0xc, 8};  // name, string, latched, reset
                for (int32_t i = 0; i < 4; ++i)
                    if (void* string = *reinterpret_cast<void**>(var + kStrings[i]))
                        title_function<ZFree>(0x000428d0)(string);  // Z_Free
                *reinterpret_cast<uint32_t*>(var) = 0;
                continue;
            }
            title_function<CvarSet2>(kCvarSet2)(*reinterpret_cast<const char**>(var),
                                                 *reinterpret_cast<const char**>(var + 8), 1);
        }
        prev = reinterpret_cast<uint8_t**>(var + 0x24);
    }
}

void XB_CDECL Com_StartupVariable(const char* match) {
    for (int32_t i = 0; i < title_global<int32_t>(0x009403e0); ++i) {  // com_numConsoleLines
        Cmd_TokenizeString(title_global<const char*>(0x0093fcf8 + i * 4), 0);
        if (std::strcmp(cmd_argv_or_empty(0), title_pointer<const char>(0x00837d70))) continue;  // "set"
        const char* name = cmd_argv_or_empty(1);
        if (match && std::strcmp(name, match)) continue;
        title_function<CvarSet2>(kCvarSet2)(name, cmd_argv_or_empty(2), 1);
        uint8_t* var = title_function<uint8_t* XB_CDECL(const char*, const char*, uint32_t)>(0x0029c2d0)(  // Cvar_Get
            name, title_pointer<const char>(0x0082dfeb), 0);
        *reinterpret_cast<uint32_t*>(var + 0x10) |= 0x80;  // CVAR_USER_CREATED
    }
}

void XB_CDECL Cmd_Vstr_f() {
    if (title_global<int32_t>(0x00934248) != 2) return;
    const uint8_t* var = Cvar_FindVar(title_global<const char*>(0x0092df94));
    const char* value = var ? *reinterpret_cast<const char* const*>(var + 4) : title_pointer<const char>(0x0082dfeb);
    const uint32_t args[2] = {0x008325cc, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value))};  // "%s\n"
    Cbuf_InsertText(va(args));
}

PLATFORM_ASM_ENTRY(Com_ParseCommaToken_entry,
                   "pushl %edx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Com_ParseCommaToken) "addl $8, %esp\n"
                   "ret")

char* XB_CDECL Com_ParseCommaToken(const char** data_p, uint32_t allow_line_breaks) {
    char* token = title_global<char*>(0x008d4ca0);
    int32_t length = 0;
    const char* data = *data_p;
    token[0] = 0;
    if (!allow_line_breaks) {
        if (*data == '\r' || *data == '\n') return token;
    } else {
        while (*data == '\r' || *data == '\n') ++data;
    }
    title_global<const char*>(0x0092efec) = title_global<const char*>(0x009355d4);
    title_global<const char*>(0x009355d4) = data;
    for (char c = *data; c; c = *++data) {
        if (c == ',' || c == '\n') {
            if (c != '\n') ++data;
            *data_p = data;
            token[length] = 0;
            return token;
        }
        if (c == '\r') continue;
        if (c == '"') {
            // Quoted: "" is a quote; the closing quote ends the part (no end-of-text check, as the original).
            for (;;) {
                c = *++data;
                if (c == '"') {
                    if (data[1] != '"') break;
                    if (length < 0x7f) token[length++] = '"';
                    ++data;
                    continue;
                }
                if (length < 0x7f) token[length++] = c;
            }
            continue;
        }
        if (length < 0x7f) token[length++] = c;
    }
    *data_p = nullptr;
    token[length] = 0;
    return token;
}

char* XB_CDECL COM_ParseExt(const char** data_p, uint32_t allow_line_breaks) {
    int32_t has_new_lines = 0;
    int32_t length = 0;
    if (!data_p) title_function<Break>(kBreak)(0, title_pointer<const char>(0x0083387c));  // "COM_ParseExt: NULL data_p"
    const char* data = *data_p;
    uint8_t* info = title_global<uint8_t*>(0x008d4ca0);
    char* token = reinterpret_cast<char*>(info);
    token[0] = 0;
    if (!data) {
        *data_p = nullptr;
        return token;
    }
    int32_t& lines = *reinterpret_cast<int32_t*>(info + 0x80);
    *reinterpret_cast<int32_t*>(info + 0x94) = lines;
    *reinterpret_cast<const char**>(info + 0x98) = *data_p;
    if (*reinterpret_cast<uint32_t*>(info + 0x8c)) return Com_ParseCommaToken(data_p, allow_line_breaks);

    char c;
    for (;;) {
        data = SkipWhitespace(data, &has_new_lines);
        if (!data) {
            *data_p = nullptr;
            return token;
        }
        if (has_new_lines && !allow_line_breaks) {
            *data_p = data;
            return token;
        }
        c = *data;
        if (c == '/' && data[1] == '/') {
            while (c && c != '\n') c = *++data;
        } else if (c == '/' && data[1] == '*') {
            while (c) {
                if (c == '*' && data[1] == '/') {
                    data += 2;
                    break;
                }
                if (c == '\n') ++lines;
                c = *++data;
            }
        } else {
            break;
        }
    }

    title_global<const char*>(0x0092efec) = title_global<const char*>(0x009355d4);
    title_global<const char*>(0x009355d4) = data;
    auto append = [&](char ch) {
        if (length < 0x7f) token[length++] = ch;
    };
    auto is_digit = [](char ch) { return ch >= '0' && ch <= '9'; };
    auto is_letter = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };

    if (c == '"') {
        ++data;
        for (;;) {
            c = *data++;
            if (c == '\\' && *data == '"') {
                c = '"';
                ++data;
            } else {
                if (c != '\\' && (c == '"' || !c)) {
                    token[length] = 0;
                    *data_p = data;
                    return token;
                }
                if (*data == '\n') ++lines;
            }
            append(c);
        }
    }
    if (*reinterpret_cast<uint32_t*>(info + 0x88)) {
        do {
            append(c);
            c = *++data;
        } while (c > ' ');
    } else if (is_digit(c) || (*reinterpret_cast<uint32_t*>(info + 0x90) && c == '-' && is_digit(data[1])) ||
               (c == '.' && is_digit(data[1]))) {
        do {
            append(c);
            c = *++data;
        } while (is_digit(c) || c == '.');
        if (c == 'e' || c == 'E') {
            append(c);
            c = *++data;
            if (c == '-' || c == '+') {
                append(c);
                c = *++data;
            }
            do {
                append(c);
                c = *++data;
            } while (is_digit(c));
        }
    } else if (!(*reinterpret_cast<uint32_t*>(info + 0x90) && c == '-') &&
               (is_letter(c) || c == '_' || c == '/' || c == '\\')) {
        do {
            append(c);
            c = *++data;
        } while (is_letter(c) || c == '_' || is_digit(c) || c == '/' || c == '\\' || c == ':' || c == '.');
    } else {
        for (const char* const* punc = title_pointer<const char* const>(0x008d3aa0); *punc; ++punc) {
            const int32_t l = static_cast<int32_t>(std::strlen(*punc));
            int32_t j = 0;
            while (j < l && data[j] == (*punc)[j]) ++j;
            if (j == l) {
                char* out = title_global<char*>(0x008d4ca0);
                std::memcpy(out, *punc, static_cast<uint32_t>(l));
                out[l] = 0;
                *data_p = data + l;
                return out;
            }
        }
        token = title_global<char*>(0x008d4ca0);
        token[0] = *data;
        token[1] = 0;
        *data_p = data + 1;
        return token;
    }
    if (length == 0x80) length = 0;
    token[length] = 0;
    *data_p = data;
    return token;
}

PLATFORM_ASM_ENTRY(MSG_ReadDeltaUsercmd_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(MSG_ReadDeltaUsercmd) "addl $12, %esp\n"
                   "ret")

void XB_CDECL MSG_ReadDeltaUsercmd(const uint8_t* from, uint8_t* msg, uint8_t* to) {
    auto raw_byte = [&]() { return msg_data(msg)[msg_word(msg, 0x10)++]; };
    auto checked_byte = [&]() {
        const uint8_t b = raw_byte();
        return msg_word(msg, 0x10) > msg_word(msg, 0xc) ? uint8_t{0xff} : b;
    };
    auto put = [&](uint32_t offset, const void* value, uint32_t size) { std::memcpy(to + offset, value, size); };
    if (raw_byte()) {
        const uint32_t time = static_cast<uint32_t>(checked_byte()) + *reinterpret_cast<const uint32_t*>(from);
        put(0, &time, 4);
    } else {
        const int32_t time = MSG_ReadLong(msg);
        put(0, &time, 4);
    }
    if (!raw_byte()) {
        static const uint8_t kDwords[8] = {8, 0xc, 0x10, 0x18, 0x1c, 0x20, 0x24, 0x28};
        static const uint8_t kBytes[6] = {0x14, 0x15, 0x16, 4, 5, 6};
        for (int32_t i = 0; i < 3; ++i) put(kDwords[i], from + kDwords[i], 4);
        for (int32_t i = 0; i < 6; ++i) to[kBytes[i]] = from[kBytes[i]];
        for (int32_t i = 3; i < 8; ++i) put(kDwords[i], from + kDwords[i], 4);
        return;
    }
    for (uint32_t offset = 8; offset <= 0x10; offset += 4) {
        const uint8_t low = raw_byte();
        const uint8_t high = raw_byte();
        int16_t angle = static_cast<int16_t>(low | (high << 8));
        if (msg_word(msg, 0x10) > msg_word(msg, 0xc)) angle = -1;
        const int32_t value = angle;
        put(offset, &value, 4);
    }
    to[0x14] = checked_byte();
    to[0x15] = checked_byte();
    to[0x16] = checked_byte();
    to[4] = checked_byte();
    to[5] = checked_byte();
    to[6] = checked_byte();
    for (uint32_t offset = 0x18; offset <= 0x28; offset += 4) {
        // MSG_ReadFloat's bits (not through the x87, which would quiet a signalling NaN).
        uint8_t bytes[4];
        if (!msg_read4(msg, bytes)) std::memcpy(bytes, &title_global<uint32_t>(0x0083eb34), 4);  // -1
        put(offset, bytes, 4);
    }
}

void XB_REGS3 XB_CDECL MSG_WriteDeltaUsercmd(const uint8_t* from, const uint8_t* to, uint8_t* msg) {
    auto write_byte = [&](uint8_t b) { msg_data(msg)[msg_word(msg, 0xc)++] = b; };
    auto write_bytes = [&](const uint8_t* p, int32_t count) {
        for (int32_t i = 0; i < count; ++i) write_byte(p[i]);
    };
    auto word = [](const uint8_t* p, uint32_t offset) {
        int32_t v;
        std::memcpy(&v, p + offset, 4);
        return v;
    };
    auto real = [](const uint8_t* p, uint32_t offset) {
        float v;
        std::memcpy(&v, p + offset, 4);
        return v;
    };
    if (word(to, 0) - word(from, 0) < 0x100) {
        write_byte(1);
        write_byte(static_cast<uint8_t>(to[0] - from[0]));
    } else {
        write_byte(0);
        write_bytes(to, 4);
    }
    static const uint8_t kBytes[6] = {0x14, 0x15, 0x16, 4, 5, 6};
    bool same = word(from, 8) == word(to, 8) && word(from, 0xc) == word(to, 0xc) && word(from, 0x10) == word(to, 0x10);
    for (int32_t i = 0; same && i < 6; ++i) same = from[kBytes[i]] == to[kBytes[i]];
    for (uint32_t offset = 0x18; same && offset <= 0x28; offset += 4) same = real(from, offset) == real(to, offset);
    if (same) {
        write_byte(0);
        return;
    }
    write_byte(1);
    for (uint32_t offset = 8; offset <= 0x10; offset += 4) write_bytes(to + offset, 2);
    for (int32_t i = 0; i < 6; ++i) write_byte(to[kBytes[i]]);
    for (uint32_t offset = 0x18; offset <= 0x28; offset += 4) write_bytes(to + offset, 4);
}

namespace {

template <typename T>
T& cvar_field(uint8_t* var, uint32_t offset) {
    return *reinterpret_cast<T*>(var + offset);
}

using ZFreeFn = void XB_CDECL(void*);
constexpr uint32_t kZFree = 0x000428d0;

// The title CRT's atof (double in ST0, stored as a float) and atol.
float title_atof(const char* text) { return title_function<float XB_CDECL(const char*)>(0x001ef056)(text); }
int32_t title_atol(const char* text) { return title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(text); }

void check_cvar_name(const char* name) {
    using Strchr = char* XB_CDECL(const char*, int32_t);
    if (name && !title_function<Strchr>(0x001ef5e0)(name, '\\') && !title_function<Strchr>(0x001ef5e0)(name, '"') &&
        !title_function<Strchr>(0x001ef5e0)(name, ';'))
        return;
    const uint32_t args[2] = {0x00837b20, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name))};  // "invalid cvar name string: %s"
    title_function<void XB_CDECL(uint32_t, const char*)>(kBreak)(0, va(args));
}

bool cheats_off() {
    const uint8_t* cheats = title_global<uint8_t*>(0x00940358);  // sv_cheats
    return !*reinterpret_cast<const int32_t*>(cheats + 0x20);
}

}  // namespace

uint8_t* XB_CDECL Cvar_Set2(const char* name, char* value, uint32_t force) {
    const uint32_t print_args[3] = {0x00837afc, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)),  // "      cvar set %s %s\n"
                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value))};
    va(print_args);
    for (uint32_t i = 0; i < std::strlen(value); ++i)
        if (value[i] == '\\') value[i] = '/';
    check_cvar_name(name);
    uint8_t* var = Cvar_FindVar(name);
    if (!var) {
        if (!value) return nullptr;
        return Cvar_Get(name, value, force ? 0 : 0x80);  // CVAR_USER_CREATED unless forced
    }
    if (!value) value = cvar_field<char*>(var, 8);
    if (!std::strcmp(value, cvar_field<const char*>(var, 4))) {
        if ((cvar_field<uint32_t>(var, 0x10) & 0x20) && cvar_field<void*>(var, 0xc)) {
            title_function<ZFreeFn>(kZFree)(cvar_field<void*>(var, 0xc));
            cvar_field<void*>(var, 0xc) = nullptr;
        }
        return var;
    }
    const uint32_t flags = cvar_field<uint32_t>(var, 0x10);
    title_global<uint32_t>(0x009403ec) |= flags;  // cvar_modifiedFlags
    if (!force) {
        if (flags & 0x40) return var;  // CVAR_ROM
        if (flags & 0x10) return var;  // CVAR_INIT
        if ((flags & 0x200) && cheats_off()) return var;
        if (flags & 0x20) {  // CVAR_LATCH
            if (char* latched = cvar_field<char*>(var, 0xc)) {
                if (!std::strcmp(value, latched)) return var;
                title_function<ZFreeFn>(kZFree)(latched);
            } else if (!std::strcmp(value, cvar_field<const char*>(var, 4))) {
                return var;
            }
            cvar_field<char*>(var, 0xc) = CopyString(value);
            cvar_field<uint32_t>(var, 0x14) = 1;
            return var;
        }
    } else if (cvar_field<void*>(var, 0xc)) {
        title_function<ZFreeFn>(kZFree)(cvar_field<void*>(var, 0xc));
        cvar_field<void*>(var, 0xc) = nullptr;
    }
    if (!std::strcmp(value, cvar_field<const char*>(var, 4))) return var;
    cvar_field<uint32_t>(var, 0x14) = 1;
    ++cvar_field<int32_t>(var, 0x18);
    title_function<ZFreeFn>(kZFree)(cvar_field<void*>(var, 4));
    cvar_field<char*>(var, 4) = CopyString(value);
    cvar_field<float>(var, 0x1c) = title_atof(cvar_field<const char*>(var, 4));
    cvar_field<int32_t>(var, 0x20) = title_atol(cvar_field<const char*>(var, 4));
    return var;
}

uint8_t* XB_CDECL Cvar_Get(const char* name, const char* value, uint32_t flags) {
    using Set2 = uint8_t* XB_CDECL(const char*, const char*, uint32_t);
    if (!name || !value)
        title_function<Break>(kBreak)(0, title_pointer<const char>(0x00837b40));  // "Cvar_Get: NULL parameter"
    if (name) check_cvar_name(name);
    uint8_t* var = Cvar_FindVar(name);
    if (var) {
        if ((cvar_field<uint32_t>(var, 0x10) & 0x1080) && !(flags & 0x1080) && (value[0] || (flags & 0x200))) {
            cvar_field<uint32_t>(var, 0x10) &= ~0x1080u;
            title_function<ZFreeFn>(kZFree)(cvar_field<void*>(var, 8));
            cvar_field<char*>(var, 8) = CopyString(value);
            title_global<uint32_t>(0x009403ec) |= flags;
        }
        cvar_field<uint32_t>(var, 0x10) |= flags;
        if (!cvar_field<const char*>(var, 8)[0]) {
            title_function<ZFreeFn>(kZFree)(cvar_field<void*>(var, 8));
            cvar_field<char*>(var, 8) = CopyString(value);
        }
        if (char* latched = cvar_field<char*>(var, 0xc)) {
            cvar_field<char*>(var, 0xc) = nullptr;
            title_function<Set2>(0x0029c030)(name, latched, 1);  // Cvar_Set2
            title_function<ZFreeFn>(kZFree)(latched);
        }
        if (flags & 0x40) title_function<Set2>(0x0029c030)(name, value, 1);
        if ((cvar_field<uint32_t>(var, 0x10) & 0x200) && title_global<uint8_t*>(0x00940358) && cheats_off())
            title_function<Set2>(0x0029c030)(name, value, 1);
        return var;
    }
    int32_t& count = title_global<int32_t>(0x009476f8);  // cvar_numIndexes
    if (count >= 0x262) title_function<Break>(kBreak)(0, title_pointer<const char>(0x00837b14));  // "MAX_CVARS"
    var = title_pointer<uint8_t>(0x00940a10 + count * 0x2c);  // cvar_indexes
    ++count;
    cvar_field<char*>(var, 0) = CopyString(name);
    cvar_field<char*>(var, 4) = CopyString(value);
    cvar_field<uint32_t>(var, 0x14) = 1;
    cvar_field<uint32_t>(var, 0x18) = 1;
    cvar_field<float>(var, 0x1c) = title_atof(cvar_field<const char*>(var, 4));
    cvar_field<int32_t>(var, 0x20) = title_atol(cvar_field<const char*>(var, 4));
    cvar_field<char*>(var, 8) = CopyString(value);
    uint8_t** prev = title_pointer<uint8_t*>(0x0093fcec);  // cvar_vars
    while (*prev) {
        if (title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(  // _stricmp
                cvar_field<const char*>(var, 0), cvar_field<const char*>(*prev, 0)) < 0)
            break;
        prev = &cvar_field<uint8_t*>(*prev, 0x24);
    }
    cvar_field<uint8_t*>(var, 0x24) = *prev;
    *prev = var;
    cvar_field<uint32_t>(var, 0x10) = flags;
    const int32_t hash = generateHashValue(name);
    cvar_field<uint8_t*>(var, 0x28) = title_global<uint8_t*>(0x009472f8 + hash * 4);  // hashTable
    title_global<uint8_t*>(0x009472f8 + hash * 4) = var;
    return var;
}

}  // namespace game
