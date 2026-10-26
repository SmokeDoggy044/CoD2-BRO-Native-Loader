#include "movie_vid.h"

#include "title_data.h"

namespace game {

namespace {

// One 64-bit MMX register as bytes (little-endian lanes: word i = bytes 2i, 2i+1).
struct Mm {
    uint8_t b[8];
};

// The MMX registers of one function, in register order; set_mmx loads the ones it wrote.
struct MmxRegs {
    Mm r[8];
};

Mm mm_load(const uint8_t* p) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = p[i];
    return v;
}
void mm_store(uint8_t* p, const Mm& v) {
    for (int i = 0; i < 8; ++i) p[i] = v.b[i];
}
Mm mm_zero() { return Mm{}; }
// MOVD mm, r32: the dword in the low half, zero above.
Mm mm_movd(uint32_t value) {
    Mm v{};
    for (int i = 0; i < 4; ++i) v.b[i] = static_cast<uint8_t>(value >> (8 * i));
    return v;
}
int16_t word(const Mm& v, int i) { return static_cast<int16_t>(v.b[2 * i] | v.b[2 * i + 1] << 8); }
void set_word(Mm& v, int i, int32_t w) {
    v.b[2 * i] = static_cast<uint8_t>(w);
    v.b[2 * i + 1] = static_cast<uint8_t>(w >> 8);
}
uint8_t sat_u8(int32_t x) { return static_cast<uint8_t>(x < 0 ? 0 : x > 255 ? 255 : x); }
int32_t sat_s16(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }

Mm pavgb(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = static_cast<uint8_t>((a.b[i] + b.b[i] + 1) >> 1);
    return v;
}
Mm pxor(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = a.b[i] ^ b.b[i];
    return v;
}
Mm por(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = a.b[i] | b.b[i];
    return v;
}
Mm pand(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = a.b[i] & b.b[i];
    return v;
}
Mm paddusb(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = sat_u8(a.b[i] + b.b[i]);
    return v;
}
Mm psubusb(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 8; ++i) v.b[i] = sat_u8(a.b[i] - b.b[i]);
    return v;
}
Mm paddsw(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) set_word(v, i, sat_s16(word(a, i) + word(b, i)));
    return v;
}
Mm psubsw(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) set_word(v, i, sat_s16(word(a, i) - word(b, i)));
    return v;
}
// PACKUSWB a, b: a's four words then b's, each saturated to an unsigned byte.
Mm packuswb(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) {
        v.b[i] = sat_u8(word(a, i));
        v.b[4 + i] = sat_u8(word(b, i));
    }
    return v;
}
Mm punpcklbw(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) {
        v.b[2 * i] = a.b[i];
        v.b[2 * i + 1] = b.b[i];
    }
    return v;
}
Mm punpckhbw(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) {
        v.b[2 * i] = a.b[4 + i];
        v.b[2 * i + 1] = b.b[4 + i];
    }
    return v;
}
Mm punpcklwd(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 2; ++i) {
        set_word(v, 2 * i, word(a, i));
        set_word(v, 2 * i + 1, word(b, i));
    }
    return v;
}
Mm punpckhwd(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 2; ++i) {
        set_word(v, 2 * i, word(a, 2 + i));
        set_word(v, 2 * i + 1, word(b, 2 + i));
    }
    return v;
}
Mm punpckldq(const Mm& a, const Mm& b) {
    Mm v;
    for (int i = 0; i < 4; ++i) {
        v.b[i] = a.b[i];
        v.b[4 + i] = b.b[i];
    }
    return v;
}

const Mm& title_mm(uint32_t address) { return title_global<const Mm>(address); }

}  // namespace

// Loads MM0..MM(count-1) from regs (cdecl): the C versions end with the MMX registers
// holding what the original left there.
extern "C" PLATFORM_HIDDEN void movie_vid_set_mmx(const MmxRegs* regs, uint32_t count);
PLATFORM_ASM_ENTRY(movie_vid_set_mmx,
                   "movl 4(%esp), %eax\n"
                   "movl 8(%esp), %ecx\n"
                   "cmpl $8, %ecx\n jb 1f\n movq 56(%eax), %mm7\n"
                   "1: cmpl $7, %ecx\n jb 1f\n movq 48(%eax), %mm6\n"
                   "1: cmpl $6, %ecx\n jb 1f\n movq 40(%eax), %mm5\n"
                   "1: cmpl $5, %ecx\n jb 1f\n movq 32(%eax), %mm4\n"
                   "1: cmpl $4, %ecx\n jb 1f\n movq 24(%eax), %mm3\n"
                   "1: cmpl $3, %ecx\n jb 1f\n movq 16(%eax), %mm2\n"
                   "1: cmpl $2, %ecx\n jb 1f\n movq 8(%eax), %mm1\n"
                   "1: movq (%eax), %mm0\n"
                   "ret")

namespace {

// The half-pel modes of the Mc functions: where the second (and third/fourth) sample of
// each pixel comes from.
enum class Half { None, X, Y, XY };

// One 8-pixel group of a motion-compensated row (the MMX sequence of each Mc variant).
// XY averages four samples as PAVGB(PAVGB(a,b), PAVGB(c,d)) and then subtracts the
// rounding error ((a^b)|(c^d)) & (ab^cd) & 0x01.. (0x008c0200), giving (a+b+c+d+2)>>2.
// m holds the registers used (MM0 = the result, MM1..MM5 as the original leaves them).
void mc_group(MmxRegs& m, uint8_t* dst, const uint8_t* src, int32_t stride, Half half, bool avg) {
    Mm* r = m.r;
    switch (half) {
    case Half::None:
        r[0] = mm_load(src);
        break;
    case Half::X:
        r[0] = pavgb(mm_load(src), mm_load(src + 1));
        break;
    case Half::Y:
        r[0] = pavgb(mm_load(src), mm_load(src + stride));
        break;
    case Half::XY:
        r[0] = mm_load(src);
        r[1] = mm_load(src + 1);
        r[5] = r[0];
        r[2] = mm_load(src + stride);
        r[5] = pxor(r[5], r[1]);
        r[3] = mm_load(src + stride + 1);
        r[4] = r[2];
        r[4] = pxor(r[4], r[3]);
        r[0] = pavgb(r[0], r[1]);
        r[4] = por(r[4], r[5]);
        r[2] = pavgb(r[2], r[3]);
        r[5] = r[0];
        r[5] = pxor(r[5], r[2]);
        r[4] = pand(r[4], r[5]);
        r[4] = pand(r[4], title_mm(0x008c0200));
        r[0] = pavgb(r[0], r[2]);
        r[0] = psubusb(r[0], r[4]);
        break;
    }
    if (avg) r[0] = pavgb(r[0], mm_load(dst));
    mm_store(dst, r[0]);
}

// The 8-wide variants: one register (MM0; MM0..MM5 for XY). The loop tests at the bottom
// (`dec esi; jne`): rows == 0 runs 2^32 times, as the original.
void mc8(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows, Half half, bool avg) {
    MmxRegs m;
    uint32_t n = rows;
    do {
        mc_group(m, dst, src, stride, half, avg);
        src += stride;
        dst += stride;
    } while (--n != 0);
    movie_vid_set_mmx(&m, half == Half::XY ? 6 : 1);
}

// The 16-wide full/X/Y variants keep the two halves in MM0 and MM1 and read all of a row's
// source (and dst for Avg) before storing it.
void mc16(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows, Half half, bool avg) {
    MmxRegs m;
    uint32_t n = rows;
    do {
        Mm a = mm_load(src), b = mm_load(src + 8);
        if (half == Half::X) {
            a = pavgb(a, mm_load(src + 1));
            b = pavgb(b, mm_load(src + 9));
        } else if (half == Half::Y) {
            a = pavgb(a, mm_load(src + stride));
            b = pavgb(b, mm_load(src + stride + 8));
        }
        if (avg) {
            a = pavgb(a, mm_load(dst));
            b = pavgb(b, mm_load(dst + 8));
        }
        mm_store(dst, a);
        mm_store(dst + 8, b);
        m.r[0] = a;
        m.r[1] = b;
        src += stride;
        dst += stride;
    } while (--n != 0);
    movie_vid_set_mmx(&m, 2);
}

// The 16-wide XY variants: two 8-pixel groups one after the other, each stored before the
// next is read.
void mc16xy(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows, bool avg) {
    MmxRegs m;
    uint32_t n = rows;
    do {
        mc_group(m, dst, src, stride, Half::XY, avg);
        mc_group(m, dst + 8, src + 8, stride, Half::XY, avg);
        src += stride;
        dst += stride;
    } while (--n != 0);
    movie_vid_set_mmx(&m, 6);
}

}  // namespace

void XB_CDECL MovieVid_McAvg16(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::None, true);
}
void XB_CDECL MovieVid_McAvg8(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::None, true);
}
void XB_CDECL MovieVid_McPut16(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::None, false);
}
void XB_CDECL MovieVid_McPut8(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::None, false);
}
void XB_CDECL MovieVid_McAvg16X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::X, true);
}
void XB_CDECL MovieVid_McAvg8X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::X, true);
}
void XB_CDECL MovieVid_McPut16X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::X, false);
}
void XB_CDECL MovieVid_McPut8X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::X, false);
}
void XB_CDECL MovieVid_McAvg16Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::Y, true);
}
void XB_CDECL MovieVid_McAvg8Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::Y, true);
}
void XB_CDECL MovieVid_McPut16Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16(dst, src, stride, rows, Half::Y, false);
}
void XB_CDECL MovieVid_McPut8Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::Y, false);
}
void XB_CDECL MovieVid_McAvg16XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16xy(dst, src, stride, rows, true);
}
void XB_CDECL MovieVid_McAvg8XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::XY, true);
}
void XB_CDECL MovieVid_McPut16XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc16xy(dst, src, stride, rows, false);
}
void XB_CDECL MovieVid_McPut8XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows) {
    mc8(dst, src, stride, rows, Half::XY, false);
}

// Rows are written at dst + r * stride; each row's two source halves are read before its
// store.
const int16_t* XB_REGS3 XB_CDECL MovieVid_PutBlock(const int16_t* block, int32_t stride, uint8_t* dst) {
    MmxRegs m;
    const uint8_t* words = reinterpret_cast<const uint8_t*>(block);
    for (int row = 0; row < 8; ++row) {
        m.r[0] = mm_load(words + row * 16 + 8);
        m.r[1] = mm_load(words + row * 16);
        m.r[1] = packuswb(m.r[1], m.r[0]);
        mm_store(dst + row * stride, m.r[1]);
    }
    movie_vid_set_mmx(&m, 2);
    return block;
}

void XB_REGS3 XB_CDECL MovieVid_AddBlock(uint8_t* dst, int32_t stride, const int16_t* block) {
    MmxRegs m;
    const uint8_t* words = reinterpret_cast<const uint8_t*>(block);
    m.r[0] = mm_zero();
    for (int row = 0; row < 8; ++row) {
        uint8_t* p = dst + row * stride;
        m.r[2] = mm_load(words + row * 16 + 8);
        m.r[1] = mm_load(p);
        m.r[3] = paddsw(punpckhbw(m.r[1], m.r[0]), m.r[2]);
        m.r[2] = mm_load(words + row * 16);
        m.r[1] = paddsw(punpcklbw(m.r[1], m.r[0]), m.r[2]);
        m.r[1] = packuswb(m.r[1], m.r[3]);
        mm_store(p, m.r[1]);
    }
    movie_vid_set_mmx(&m, 4);
}

void XB_REGS3 XB_CDECL MovieVid_AddBlockDCOnly(uint8_t* dst, const int16_t* block, int32_t stride) {
    MmxRegs m;
    const int32_t dc = (block[0] + 0x40) >> 7;
    m.r[0] = mm_movd(static_cast<uint32_t>(dc));
    m.r[1] = punpcklwd(m.r[0], m.r[0]);
    m.r[0] = m.r[1];
    m.r[1] = punpckldq(m.r[1], m.r[0]);
    m.r[0] = m.r[1];  // the low word of dc in all four words
    m.r[1] = psubsw(mm_zero(), m.r[0]);
    m.r[0] = packuswb(m.r[0], m.r[0]);  // the positive part as bytes
    m.r[1] = packuswb(m.r[1], m.r[1]);  // the negative part
    for (int row = 0; row < 8; ++row) {
        uint8_t* p = dst + row * stride;
        m.r[2] = psubusb(paddusb(mm_load(p), m.r[0]), m.r[1]);
        mm_store(p, m.r[2]);
    }
    movie_vid_set_mmx(&m, 3);
}

namespace {

// 0x000cfb20: the MMX IDCT, in place (not decompiled yet).
using IdctFn = void XB_CDECL(int16_t* block);

// The tail both Idct wrappers share: PXOR MM0 and 16 MOVQ stores clearing the block.
void clear_block(int16_t* block) {
    MmxRegs m;
    m.r[0] = mm_zero();
    uint8_t* p = reinterpret_cast<uint8_t*>(block);
    for (int i = 0; i < 16; ++i) mm_store(p + 8 * i, m.r[0]);
    movie_vid_set_mmx(&m, 1);
}

}  // namespace

int16_t* XB_CDECL MovieVid_IdctPutBlock(int16_t* block, uint8_t* dst, int32_t stride) {
    title_function<IdctFn>(0x000cfb20)(block);
    MovieVid_PutBlock(block, stride, dst);
    clear_block(block);
    return block;
}

void XB_CDECL MovieVid_IdctAddBlock(int32_t last, int16_t* block, uint8_t* dst, int32_t stride) {
    if (last == 0x81 && (*reinterpret_cast<const uint8_t*>(block) & 0x70) != 0x40) {
        MovieVid_AddBlockDCOnly(dst, block, stride);
    } else {
        title_function<IdctFn>(0x000cfb20)(block);
        MovieVid_AddBlock(dst, stride, block);
    }
    clear_block(block);
}

// ---- Bitstream readers ----
//
// The decoder keeps a 32-bit MSB-first window (+0x14c), a bit count (+0x150: > 0 = room
// for another 16 bits) and a byte pointer (+0x140) into the stream, whose bytes are
// XORed with 0xa6. Coefficient readers keep the three in registers and store them back;
// the others work on the fields in place.

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

constexpr uint32_t kBitPtr = 0x140, kBitWindow = 0x14c, kBitCount = 0x150;

// The next two stream bytes, de-obfuscated, as a big-endian word.
uint32_t stream_word(const uint8_t* p) { return (static_cast<uint32_t>(p[0] ^ 0xa6) << 8) | (p[1] ^ 0xa6); }

// A window/count/pointer triple held in locals (the coefficient readers' registers).
struct Bits {
    const uint8_t* ptr;
    uint32_t w;
    int32_t count;
    // NEEDBITS: refill 16 bits when there is room.
    void need() {
        if (count > 0) {
            w |= stream_word(ptr) << (count & 31);
            ptr += 2;
            count -= 16;
        }
    }
};

// NEEDBITS on the decoder's fields in place.
void need_bits(uint8_t* dec) {
    const int32_t count = field<int32_t>(dec, kBitCount);
    if (count > 0) {
        const uint8_t* p = field<const uint8_t*>(dec, kBitPtr);
        const uint32_t word = stream_word(p) << (count & 31);
        field<const uint8_t*>(dec, kBitPtr) = p + 2;
        field<uint32_t>(dec, kBitWindow) |= word;
        field<int32_t>(dec, kBitCount) = count - 16;
    }
}

// A {value, length} (2-byte) or {run, level, length} (3-byte) VLC table entry in .rdata.
const uint8_t* table_entry(uint32_t base, uint32_t index, uint32_t size) {
    return &title_global<const uint8_t>(base + index * size);
}

// Looks a 2-byte {value, length} entry up and drops its length from the window.
const uint8_t* vlc2(uint8_t* dec, uint32_t base, uint32_t index) {
    const uint8_t* e = table_entry(base, index, 2);
    field<uint32_t>(dec, kBitWindow) <<= e[1] & 31;
    field<int32_t>(dec, kBitCount) += e[1];
    return e;
}

// SATURATE on the coefficient scaled by 16: out-of-range values become 0x7ff0 / 0xffff8000.
int32_t saturate(int32_t v) {
    if (static_cast<int16_t>(v) == v) return v;
    return ((v >> 27) & ~15) ^ 0x7ff0;
}

// The codes below 0x04000000 shared by all three coefficient readers (DCT tables for
// 10, 13, 15 and 16 bits; the 16-bit one refills the window first). Adds the run to i.
const uint8_t* long_dct_code(Bits& b, uint32_t table10, int32_t& i) {
    const uint8_t* t;
    if (b.w >= 0x02000000) {
        t = table_entry(table10, (b.w >> 22) - 8, 3);
    } else if (b.w >= 0x00800000) {
        t = table_entry(0x007cedb0, (b.w >> 19) - 16, 3);
    } else if (b.w >= 0x00200000) {
        t = table_entry(0x007ced20, (b.w >> 17) - 16, 3);
    } else {
        const uint32_t word = stream_word(b.ptr) << ((b.count + 16) & 31);
        t = table_entry(0x007cecc0, b.w >> 16, 3);
        b.w = (b.w << 16) | word;
        i += t[0];
        b.ptr += 2;
        return t;
    }
    i += t[0];
    return t;
}

// Intra blocks (B14 or, with the alternative VLC flag, B15 tables): coefficients times
// quant, scaled by 16, stored at scan[i]; the mismatch control flips bit 4 of the last
// coefficient. Returns nothing (EAX = the decoder, unread).
void read_intra(uint8_t* dec, const uint16_t* quant, bool b15) {
    const uint8_t* scan = field<const uint8_t*>(dec, 0x2240);
    Bits b{field<const uint8_t*>(dec, kBitPtr), 0, 0};
    int16_t* block = field<int16_t*>(dec, 0x154);
    int32_t mismatch = ~static_cast<int32_t>(block[0]);
    b.w = field<uint32_t>(dec, kBitWindow);
    b.count = field<int32_t>(dec, kBitCount);
    int32_t i = 0;
    b.need();
    for (;;) {
        const uint8_t* t;
        if (!b15 && b.w >= 0x28000000) {
            t = table_entry(0x007ceec8, (b.w >> 27) - 5, 3);
            i += t[0];
            if (i >= 64) break;
        } else if (b.w >= 0x04000000) {
            t = table_entry(b15 ? 0x007cef90 : 0x007cee58, (b.w >> 24) - 4, 3);
            i += t[0];
            if (i >= 64) {
                // Escape: 6-bit run, 12-bit signed level.
                i += ((b.w >> 20) & 63) - 64;
                if (i >= 64) break;
                const uint8_t j = scan[i];
                b.count += 12;
                b.w <<= 12;
                b.need();
                const int32_t product =
                    static_cast<int32_t>(quant[j] * static_cast<uint32_t>(static_cast<int32_t>(b.w) >> 20));
                const int32_t val = saturate(static_cast<int32_t>(static_cast<uint32_t>(product / 16) << 4));
                block[scan[i]] = static_cast<int16_t>(val);
                mismatch ^= val;
                b.w <<= 12;
                b.count += 12;
                b.need();
                continue;
            }
        } else {
            t = long_dct_code(b, b15 ? 0x007cef74 : 0x007cee40, i);
            if (i >= 64) break;
        }
        const uint8_t j = scan[i];
        b.w <<= t[2] & 31;
        b.count += t[2] + 1;
        const int32_t sign = static_cast<int32_t>(b.w) >> 31;
        int32_t val = static_cast<int32_t>(quant[j] * static_cast<uint32_t>(t[1]));
        val = ((val ^ (static_cast<int32_t>(b.w) >> 27)) & ~15) - static_cast<int32_t>(static_cast<uint32_t>(sign) << 4);
        val = saturate(val);
        block[scan[i]] = static_cast<int16_t>(val);
        mismatch ^= val;
        b.w <<= 1;
        b.need();
    }
    block[63] ^= static_cast<int16_t>(mismatch & 0x10);
    // Drop the end-of-block code (2 bits in B14, 4 in B15).
    const uint32_t eob = b15 ? 4 : 2;
    field<int32_t>(dec, kBitCount) = b.count + static_cast<int32_t>(eob);
    field<const uint8_t*>(dec, kBitPtr) = b.ptr;
    field<uint32_t>(dec, kBitWindow) = b.w << eob;
}

}  // namespace

void XB_THISCALL MovieVid_ReadIntraCoeffs(uint8_t* dec, const uint16_t* quant) { read_intra(dec, quant, false); }

void XB_THISCALL MovieVid_ReadIntraCoeffsAlt(uint8_t* dec, const uint16_t* quant) { read_intra(dec, quant, true); }

int32_t XB_THISCALL MovieVid_ReadNonIntraCoeffs(uint8_t* dec, const uint16_t* quant) {
    Bits b{field<const uint8_t*>(dec, kBitPtr), field<uint32_t>(dec, kBitWindow), field<int32_t>(dec, kBitCount)};
    const uint8_t* scan = field<const uint8_t*>(dec, 0x2240);
    int16_t* block = field<int16_t*>(dec, 0x154);
    int32_t i = -1;
    int32_t mismatch = -1;
    b.need();
    bool first = true;  // the first code uses the DC table (0x007cef20)
    for (;;) {
        const uint8_t* t;
        if (b.w >= 0x28000000) {
            t = table_entry(first ? 0x007cef20 : 0x007ceec8, (b.w >> 27) - 5, 3);
            first = false;
            i += t[0];
            if (i >= 64) break;
        } else {
            first = false;
            if (b.w >= 0x04000000) {
                t = table_entry(0x007cee58, (b.w >> 24) - 4, 3);
                i += t[0];
                if (i >= 64) {
                    // Escape: 6-bit run, 12-bit signed level; (2 * level + sign + 1) * quant / 32.
                    i += ((b.w >> 20) & 63) - 64;
                    if (i >= 64) break;
                    const uint8_t j = scan[i];
                    b.count += 12;
                    b.w <<= 12;
                    b.need();
                    const int32_t level = (static_cast<int32_t>(b.w) >> 31) + (static_cast<int32_t>(b.w) >> 20);
                    const int32_t product = static_cast<int32_t>(static_cast<uint32_t>(level * 2 + 1) * quant[j]);
                    const int32_t val = saturate(static_cast<int32_t>(static_cast<uint32_t>(product / 32) << 4));
                    block[scan[i]] = static_cast<int16_t>(val);
                    mismatch ^= val;
                    b.count += 12;
                    b.w <<= 12;
                    b.need();
                    continue;
                }
            } else {
                t = long_dct_code(b, 0x007cee40, i);
                if (i >= 64) break;
            }
        }
        const uint8_t j = scan[i];
        b.w <<= t[2] & 31;
        b.count += t[2] + 1;
        int32_t val = static_cast<int32_t>((t[1] * 2u + 1) * quant[j]);
        val = (val ^ (static_cast<int32_t>(b.w) >> 26)) >> 1;
        val = (val & ~15) - static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(b.w) >> 31) << 4);
        val = saturate(val);
        block[j] = static_cast<int16_t>(val);
        mismatch ^= val;
        b.w <<= 1;
        b.need();
    }
    block[63] ^= static_cast<int16_t>(mismatch & 0x10);
    field<int32_t>(dec, kBitCount) = b.count + 2;
    field<uint32_t>(dec, kBitWindow) = b.w << 2;
    field<const uint8_t*>(dec, kBitPtr) = b.ptr;
    return i;
}

void XB_THISCALL MovieVid_BuildQuantTables(uint8_t* dec, int32_t index) {
    // +0x2246: quantiser scale type (0 = linear 2q, else the non-linear table 0x008bffd0);
    // +0x2244[index]: the type the tables were built for.
    const uint8_t type = field<uint8_t>(dec, 0x2246);
    if (field<uint8_t>(dec, 0x2244 + index) == type) return;
    field<uint8_t>(dec, 0x2244 + index) = type;
    const uint8_t* matrix = dec + 0x98 + index * 64;
    uint16_t* out = reinterpret_cast<uint16_t*>(dec + 0x160 + index * 0x1000);
    for (int32_t q = 0; q < 32; ++q) {
        const uint32_t scale = field<uint8_t>(dec, 0x2246) ? title_global<uint32_t>(0x008bffd0 + q * 4) : q * 2;
        for (int32_t k = 0; k < 64; ++k) *out++ = static_cast<uint16_t>(matrix[k] * scale);
    }
}

uint32_t XB_FASTCALL MovieVid_ReadMacroblockType(uint8_t* dec) {
    // +0x2164: picture coding type (1 I, 2 P, 3 B); +0x2238: frame_pred_frame_dct.
    // Result: the table's flags, motion type in bits 6-7, DCT type in bit 5, 0x80 when the
    // frame-prediction flag set it.
    uint32_t& w = field<uint32_t>(dec, kBitWindow);
    int32_t& count = field<int32_t>(dec, kBitCount);
    uint32_t type;
    auto read_dct_type = [&] {
        const uint32_t bit = w >> 31;
        w += w;
        type |= bit << 5;
        ++count;
    };
    auto read_motion_type = [&] {
        const uint32_t bits = (w >> 24) & 0xc0;
        w <<= 2;
        type |= bits;
        count += 2;
    };
    switch (field<int32_t>(dec, 0x2164)) {
    case 1:
        type = vlc2(dec, 0x007ce960, w >> 31)[0];
        if (field<int32_t>(dec, 0x2238) == 0) read_dct_type();
        return type;
    case 2:
        type = vlc2(dec, 0x007ce968, w >> 27)[0];
        if (field<int32_t>(dec, 0x2238) != 0) {
            if (type & 8) type |= 0x80;
            return type | 8;
        }
        if (type & 8) read_motion_type();
        if (type & 3) read_dct_type();
        return type | 8;
    case 3:
        type = vlc2(dec, 0x007ce9a8, w >> 26)[0];
        if (field<int32_t>(dec, 0x2238) != 0) return type | 0x80;
        if (!(type & 1)) {
            read_motion_type();
            if (!(type & 3)) return type;
        }
        read_dct_type();
        return type;
    default:
        return 0;
    }
}

uint32_t XB_FASTCALL MovieVid_ReadCodedBlockPattern(uint8_t* dec) {
    need_bits(dec);
    const uint32_t w = field<uint32_t>(dec, kBitWindow);
    const uint8_t* e =
        w >= 0x20000000 ? table_entry(0x007cea80, w >> 25, 2) : table_entry(0x007ceb80, w >> 23, 2);
    field<uint32_t>(dec, kBitWindow) = w << (e[1] & 31);
    field<int32_t>(dec, kBitCount) += e[1];
    return e[0];
}

namespace {

// The DC differential after its size code: `size` bits, negative values coded as the
// one's complement (the top bit clear), shifted by the intra DC precision (+0x2234).
int32_t dc_diff_bits(uint8_t* dec, uint32_t s, uint32_t size) {
    const uint32_t rest = (32 - size) & 31;
    const uint32_t adjust = static_cast<uint32_t>(static_cast<int32_t>(~s) >> 31) >> rest;
    const uint32_t value = s >> rest;
    field<uint32_t>(dec, kBitWindow) = s << (size & 31);
    return static_cast<int32_t>((value - adjust) << (field<uint32_t>(dec, 0x2234) & 31));
}

// Luma and chroma differ in the tables, the zero-size code length (3 / 2 bits) and the
// long code: luma 9 bits from 0x007ce8c0, chroma 10 bits from 0x007ce4c0 with one more
// bit than the table's length.
int32_t read_dc_diff(uint8_t* dec, bool chroma) {
    const uint32_t w = field<uint32_t>(dec, kBitWindow);
    if (w < 0xf8000000) {
        const uint8_t* e = table_entry(chroma ? 0x007cec40 : 0x007cec00, w >> 27, 2);
        const uint32_t size = e[0];
        if (size == 0) {
            const uint32_t len = chroma ? 2 : 3;
            field<int32_t>(dec, kBitCount) += len;
            field<uint32_t>(dec, kBitWindow) = w << len;
            return 0;
        }
        field<int32_t>(dec, kBitCount) += e[1] + size;
        const uint32_t s = field<uint32_t>(dec, kBitWindow) << (e[1] & 31);
        return dc_diff_bits(dec, s, size);
    }
    const uint8_t* e = chroma ? table_entry(0x007ce4c0, w >> 22, 2) : table_entry(0x007ce8c0, w >> 23, 2);
    const uint32_t size = e[0];
    const uint32_t len = e[1] + (chroma ? 1u : 0u);
    field<uint32_t>(dec, kBitWindow) = field<uint32_t>(dec, kBitWindow) << (len & 31);
    const int32_t count = field<int32_t>(dec, kBitCount) + static_cast<int32_t>(len);
    field<int32_t>(dec, kBitCount) = count;
    if (count > 0) {
        const uint8_t* p = field<const uint8_t*>(dec, kBitPtr);
        const uint32_t word = stream_word(p) << (count & 31);
        field<const uint8_t*>(dec, kBitPtr) = p + 2;
        const uint32_t window = field<uint32_t>(dec, kBitWindow) | word;
        const int32_t left = field<int32_t>(dec, kBitCount) - 16;
        field<uint32_t>(dec, kBitWindow) = window;
        field<int32_t>(dec, kBitCount) = left;
    }
    const int32_t diff = dc_diff_bits(dec, field<uint32_t>(dec, kBitWindow), size);
    field<int32_t>(dec, kBitCount) += size;
    return diff;
}

}  // namespace

int32_t XB_FASTCALL MovieVid_ReadLumaDCDiff(uint8_t* dec) { return read_dc_diff(dec, false); }

int32_t XB_FASTCALL MovieVid_ReadChromaDCDiff(uint8_t* dec) { return read_dc_diff(dec, true); }

void XB_THISCALL MovieVid_DecodeIntraBlock(uint8_t* dec, int32_t cc, uint8_t* dst, int32_t stride) {
    // +0x21ec: DC predictors (Y, Cb, Cr); +0x154: the coefficient block; +0x158: the intra
    // quantiser table; +0x223c: intra VLC format (B15).
    need_bits(dec);
    if (cc == 0) {
        const int32_t diff = MovieVid_ReadLumaDCDiff(dec);
        field<uint16_t>(dec, 0x21ec) = static_cast<uint16_t>(field<uint16_t>(dec, 0x21ec) + diff);
        *field<uint16_t*>(dec, 0x154) = field<uint16_t>(dec, 0x21ec);
    } else {
        const int32_t diff = MovieVid_ReadChromaDCDiff(dec);
        field<uint16_t>(dec, 0x21ec + cc * 2) = static_cast<uint16_t>(field<uint16_t>(dec, 0x21ec + cc * 2) + diff);
        *field<uint16_t*>(dec, 0x154) = field<uint16_t>(dec, 0x21ec + cc * 2);
    }
    if (field<int32_t>(dec, 0x223c) != 0)
        MovieVid_ReadIntraCoeffsAlt(dec, field<const uint16_t*>(dec, 0x158));
    else
        MovieVid_ReadIntraCoeffs(dec, field<const uint16_t*>(dec, 0x158));
    MovieVid_IdctPutBlock(field<int16_t*>(dec, 0x154), dst, stride);
}

// ---- Motion prediction ----
//
// The motion record: ref[3] planes (+0x00), pmv[2][2] (+0x20), f_code[2] (+0x30). Decoder:
// +0x21f4 x offset, +0x2218 y offset, +0x21f8 stride, +0x21fc chroma stride, +0x221c dest
// planes [3], +0x2208 x limit, +0x220c field y limit, +0x2214 frame y limit (16 rows).

namespace {

using McFn = MovieVidMcFn;
// 0x000cea00: get_motion_delta(f_code).
using MotionDeltaFn = int32_t XB_THISCALL(uint8_t* dec, int32_t f_code);

int32_t motion_delta(uint8_t* dec, int32_t f_code) { return title_function<MotionDeltaFn>(0x000cea00)(dec, f_code); }

// bound_motion_vector: wraps into the f_code's range (shift left, arithmetic right).
int32_t bound(int32_t v, int32_t f_code) {
    const uint32_t shift = static_cast<uint32_t>(0x1b - f_code) & 31;
    return static_cast<int32_t>(static_cast<uint32_t>(v) << shift) >> shift;
}

uint8_t* plane(uint8_t* motion, int i) { return field<uint8_t*>(motion, i * 4); }
uint8_t* dest(uint8_t* dec, int i) { return field<uint8_t*>(dec, 0x221c + i * 4); }

// Clamps pos to [0, limit] the way the original does: a position past the limit becomes
// limit, or 0 when negative; the vector follows.
void clamp(int32_t& pos, int32_t& vec, int32_t limit, int32_t base, uint8_t* dec, uint32_t limit_offset) {
    if (pos > limit) {
        pos = pos < 0 ? 0 : field<int32_t>(dec, limit_offset);
        vec = pos - base;
    }
}

// MOTION (frame prediction, 16x16 luma + two 8x8 chroma) through table[0..3] / [4..7].
void motion_frame(uint8_t* dec, uint8_t* motion, McFn* const* table, int32_t mx, int32_t my) {
    const int32_t base_x = field<int32_t>(dec, 0x21f4) * 2;
    const int32_t base_y = field<int32_t>(dec, 0x2218) * 2;
    int32_t pos_x = base_x + mx, pos_y = base_y + my;
    clamp(pos_x, mx, field<int32_t>(dec, 0x2208), base_x, dec, 0x2208);
    clamp(pos_y, my, field<int32_t>(dec, 0x2214), base_y, dec, 0x2214);
    uint32_t half = ((pos_y & 1) << 1) | (pos_x & 1);
    int32_t stride = field<int32_t>(dec, 0x21f8);
    table[half](dest(dec, 0) + field<int32_t>(dec, 0x21f4), plane(motion, 0) + (pos_y >> 1) * stride + (pos_x >> 1),
                stride, 16);
    mx /= 2;
    my /= 2;
    half = ((my & 1) << 1) | (mx & 1);
    int32_t uv = field<int32_t>(dec, 0x21fc);
    const int32_t offset = ((field<int32_t>(dec, 0x2218) + my) >> 1) * uv + ((field<int32_t>(dec, 0x21f4) + mx) >> 1);
    table[4 + half](dest(dec, 1) + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 1) + offset, uv, 8);
    uv = field<int32_t>(dec, 0x21fc);
    table[4 + half](dest(dec, 2) + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 2) + offset, uv, 8);
}

// MOTION_FIELD: one field (8 luma rows at twice the stride, 4 chroma rows) from source
// rows `pos_y op` (op: & ~1, or | 1 for dual prime's opposite parity) plus src_field.
void motion_field(uint8_t* dec, uint8_t* motion, McFn* const* table, int32_t mx, int32_t my, int32_t src_field,
                  int32_t dest_field, bool odd_rows) {
    const int32_t base_x = field<int32_t>(dec, 0x21f4) * 2;
    int32_t pos_x = base_x + mx, pos_y = field<int32_t>(dec, 0x2218) + my;
    clamp(pos_x, mx, field<int32_t>(dec, 0x2208), base_x, dec, 0x2208);
    if (pos_y > field<int32_t>(dec, 0x220c)) {
        pos_y = pos_y < 0 ? 0 : field<int32_t>(dec, 0x220c);
        my = pos_y - field<int32_t>(dec, 0x2218);
    }
    uint32_t half = ((pos_y & 1) << 1) | (pos_x & 1);
    const int32_t stride = field<int32_t>(dec, 0x21f8);
    const int32_t row_y = odd_rows ? (pos_y | 1) : (pos_y & ~1);
    table[half](dest(dec, 0) + dest_field * stride + field<int32_t>(dec, 0x21f4),
                plane(motion, 0) + (row_y + src_field) * stride + (pos_x >> 1), stride * 2, 8);
    mx /= 2;
    my /= 2;
    half = ((my & 1) << 1) | (mx & 1);
    int32_t uv = field<int32_t>(dec, 0x21fc);
    const int32_t row_c = odd_rows ? (my | 1) : (my & ~1);
    const int32_t offset =
        ((field<int32_t>(dec, 0x2218) >> 1) + row_c + src_field) * uv + ((field<int32_t>(dec, 0x21f4) + mx) >> 1);
    table[4 + half](dest(dec, 1) + dest_field * uv + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 1) + offset,
                    uv * 2, 4);
    uv = field<int32_t>(dec, 0x21fc);
    table[4 + half](dest(dec, 2) + dest_field * uv + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 2) + offset,
                    uv * 2, 4);
}

// One bit off the window (the field select).
uint32_t read_bit(uint8_t* dec) {
    const uint32_t w = field<uint32_t>(dec, kBitWindow);
    field<int32_t>(dec, kBitCount) += 1;
    field<uint32_t>(dec, kBitWindow) = w + w;
    return w >> 31;
}

// get_dmv: a 2-byte {int8 value, length} entry indexed by the top two bits.
int32_t read_dmv(uint8_t* dec) {
    return static_cast<int8_t>(vlc2(dec, 0x007cea98, field<uint32_t>(dec, kBitWindow) >> 30)[0]);
}

McFn* const* mc_table(uint32_t offset) { return &title_global<McFn* const>(0x008c0208 + offset); }

}  // namespace

void XB_THISCALL MovieVid_PredictFrameMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table) {
    need_bits(dec);
    int32_t delta = motion_delta(dec, field<int32_t>(motion, 0x30));
    const int32_t mx = bound(field<int32_t>(motion, 0x20) + delta, field<int32_t>(motion, 0x30));
    field<int32_t>(motion, 0x20) = mx;
    field<int32_t>(motion, 0x28) = mx;
    need_bits(dec);
    delta = motion_delta(dec, field<int32_t>(motion, 0x34));
    const int32_t my = bound(field<int32_t>(motion, 0x24) + delta, field<int32_t>(motion, 0x34));
    field<int32_t>(motion, 0x24) = my;
    field<int32_t>(motion, 0x2c) = my;
    motion_frame(dec, motion, table, mx, my);
}

void XB_THISCALL MovieVid_PredictFieldMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table) {
    for (int32_t f = 0; f < 2; ++f) {
        need_bits(dec);
        const int32_t src_field = static_cast<int32_t>(read_bit(dec));
        const int32_t delta_x = motion_delta(dec, field<int32_t>(motion, 0x30));
        const int32_t mx = bound(field<int32_t>(motion, 0x20 + f * 8) + delta_x, field<int32_t>(motion, 0x30));
        field<int32_t>(motion, 0x20 + f * 8) = mx;
        need_bits(dec);
        const int32_t delta = motion_delta(dec, field<int32_t>(motion, 0x34));
        const int32_t my = (field<int32_t>(motion, 0x24 + f * 8) >> 1) + delta;
        field<int32_t>(motion, 0x24 + f * 8) = my * 2;
        motion_field(dec, motion, table, mx, my, src_field, f, false);
    }
}

void XB_THISCALL MovieVid_PredictDualPrime(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* /*table*/) {
    need_bits(dec);
    int32_t delta = motion_delta(dec, field<int32_t>(motion, 0x30));
    int32_t mx = bound(field<int32_t>(motion, 0x20) + delta, field<int32_t>(motion, 0x30));
    field<int32_t>(motion, 0x20) = mx;
    field<int32_t>(motion, 0x28) = mx;
    need_bits(dec);
    const int32_t dmv_x = read_dmv(dec);
    delta = motion_delta(dec, field<int32_t>(motion, 0x34));
    int32_t my = (field<int32_t>(motion, 0x24) >> 1) + delta;
    field<int32_t>(motion, 0x24) = my * 2;
    field<int32_t>(motion, 0x2c) = my * 2;
    const int32_t dmv_y = read_dmv(dec);
    const int32_t gx = mx > 0, gy = my > 0;
    // The opposite-parity predictions (put): top field from 3/2 of the vector, rows | 1;
    // bottom field from 1/2, rows & ~1.
    motion_field(dec, motion, mc_table(0x00), ((gx + mx * 3) >> 1) + dmv_x, ((gy + my * 3) >> 1) + dmv_y - 1, 0, 0,
                 true);
    motion_field(dec, motion, mc_table(0x00), ((mx + gx) >> 1) + dmv_x, ((gy + my) >> 1) + dmv_y + 1, 0, 1, false);
    // The same-parity prediction averaged into both fields.
    const int32_t base_x = field<int32_t>(dec, 0x21f4) * 2;
    int32_t pos_x = base_x + mx, pos_y = field<int32_t>(dec, 0x2218) + my;
    clamp(pos_x, mx, field<int32_t>(dec, 0x2208), base_x, dec, 0x2208);
    if (pos_y > field<int32_t>(dec, 0x220c)) {
        pos_y = pos_y < 0 ? 0 : field<int32_t>(dec, 0x220c);
        my = pos_y - field<int32_t>(dec, 0x2218);
    }
    const uint32_t half = ((pos_y & 1) << 1) | (pos_x & 1);
    int32_t stride = field<int32_t>(dec, 0x21f8);
    const int32_t offset = (pos_y & ~1) * stride + (pos_x >> 1);
    mc_table(0x20)[half](dest(dec, 0) + field<int32_t>(dec, 0x21f4), plane(motion, 0) + offset, stride * 2, 8);
    stride = field<int32_t>(dec, 0x21f8);
    mc_table(0x20)[half](dest(dec, 0) + stride + field<int32_t>(dec, 0x21f4), plane(motion, 0) + stride + offset,
                         stride * 2, 8);
    mx /= 2;
    my /= 2;
    const uint32_t chalf = ((my & 1) << 1) | (mx & 1);
    int32_t uv = field<int32_t>(dec, 0x21fc);
    const int32_t coffset =
        ((field<int32_t>(dec, 0x2218) >> 1) + (my & ~1)) * uv + ((mx + field<int32_t>(dec, 0x21f4)) >> 1);
    for (int p = 1; p <= 2; ++p) {
        if (p == 2) uv = field<int32_t>(dec, 0x21fc);
        mc_table(0x30)[chalf](dest(dec, p) + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, p) + coffset, uv * 2, 4);
        uv = field<int32_t>(dec, 0x21fc);
        mc_table(0x30)[chalf](dest(dec, p) + uv + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, p) + uv + coffset,
                              uv * 2, 4);
    }
}

void XB_THISCALL MovieVid_PredictLastMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table) {
    motion_frame(dec, motion, table, field<int32_t>(motion, 0x20), field<int32_t>(motion, 0x24));
}

void XB_THISCALL MovieVid_PredictZeroMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table) {
    field<int32_t>(motion, 0x24) = 0;
    field<int32_t>(motion, 0x20) = 0;
    field<int32_t>(motion, 0x2c) = 0;
    field<int32_t>(motion, 0x28) = 0;
    const int32_t stride = field<int32_t>(dec, 0x21f8);
    table[0](dest(dec, 0) + field<int32_t>(dec, 0x21f4),
             plane(motion, 0) + field<int32_t>(dec, 0x2218) * stride + field<int32_t>(dec, 0x21f4), stride, 16);
    int32_t uv = field<int32_t>(dec, 0x21fc);
    const int32_t offset = (field<int32_t>(dec, 0x2218) >> 1) * uv + (field<int32_t>(dec, 0x21f4) >> 1);
    table[4](dest(dec, 1) + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 1) + offset, uv, 8);
    uv = field<int32_t>(dec, 0x21fc);
    table[4](dest(dec, 2) + (field<int32_t>(dec, 0x21f4) >> 1), plane(motion, 2) + offset, uv, 8);
}

// ---- Slice decoding and output ----

// EMMS (the frame code ends its MMX use here).
extern "C" PLATFORM_HIDDEN void movie_vid_emms();
PLATFORM_ASM_ENTRY(movie_vid_emms, "emms\nret")

// Calls the title's cdecl fn(a, b, c) and stores MM0..MM7 as it leaves them (the YUV
// converter 0x000d0860 returns its pixels in MM0..MM2).
extern "C" PLATFORM_HIDDEN void movie_vid_call_mmx3(uint32_t fn, const void* a, const void* b, const void* c,
                                                    void* regs);
PLATFORM_ASM_ENTRY(movie_vid_call_mmx3,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "movl 16(%esp), %eax\n"
                   "call *%eax\n"
                   "addl $12, %esp\n"
                   "movl 20(%esp), %eax\n"
                   "movq %mm0, (%eax)\n movq %mm1, 8(%eax)\n movq %mm2, 16(%eax)\n movq %mm3, 24(%eax)\n"
                   "movq %mm4, 32(%eax)\n movq %mm5, 40(%eax)\n movq %mm6, 48(%eax)\n movq %mm7, 56(%eax)\n"
                   "ret")

namespace {

// The macroblock_address_increment VLC: escapes (0x21 each) until a table code; the
// first-macroblock variant reads one bit fewer of the window for each test. Returns false
// on an invalid code (the caller's exit differs).
bool read_mba_increment(uint8_t* dec, int32_t& mba, bool first) {
    const uint32_t limit = first ? 0x08000000 : 0x10000000;
    const uint32_t small = first ? 0x01800000 : 0x03000000;
    const uint32_t mask = first ? 0xfff00000 : 0xffe00000;
    const uint32_t escape = first ? 0x00800000 : 0x01000000;
    const uint8_t* e;
    for (;;) {
        if (field<uint32_t>(dec, kBitWindow) >= limit) {
            const uint32_t w = field<uint32_t>(dec, kBitWindow);
            e = table_entry(0x007cf280, w >> (first ? 26 : 27), 2);
            break;
        }
        const uint32_t w = field<uint32_t>(dec, kBitWindow);
        if (w >= small) {
            e = table_entry(0x007cf290, w >> (first ? 20 : 21), 2);
            break;
        }
        if ((w & mask) != escape) return false;
        mba += 0x21;
        field<uint32_t>(dec, kBitWindow) = w << 11;
        field<int32_t>(dec, kBitCount) += 11;
        need_bits(dec);
    }
    // The slice's first increment has one more (marker) bit than the table's length.
    const uint32_t len = e[1] + (first ? 1u : 0u);
    field<uint32_t>(dec, kBitWindow) <<= len & 31;
    field<int32_t>(dec, kBitCount) += len;
    mba += e[0];
    return true;
}

void reset_dc_pred(uint8_t* dec) {
    field<uint16_t>(dec, 0x21f0) = 0x4000;
    field<uint16_t>(dec, 0x21ee) = 0x4000;
    field<uint16_t>(dec, 0x21ec) = 0x4000;
}

void clear_pmv(uint8_t* dec) {
    static const uint16_t kOrder[8] = {0x218c, 0x2188, 0x2194, 0x2190, 0x21c4, 0x21c0, 0x21cc, 0x21c8};
    for (int i = 0; i < 8; ++i) field<int32_t>(dec, kOrder[i]) = 0;
}

// quantiser_scale_code: selects the scaled intra (+0x158) and non-intra (+0x15c) tables.
void set_quant_tables(uint8_t* dec, uint32_t code) {
    uint8_t* base = dec + code * 128;
    field<uint8_t*>(dec, 0x158) = base + 0x160;
    field<uint8_t*>(dec, 0x15c) = base + 0x1160;
}

// NEXT_MACROBLOCK: false when the picture is done (the caller finishes).
bool next_macroblock(uint8_t* dec) {
    const int32_t offset = field<int32_t>(dec, 0x21f4) + 16;
    const int32_t width = field<int32_t>(dec, 4);
    field<int32_t>(dec, 0x21f4) = offset;
    if (offset == width) {
        if (field<int32_t>(dec, 0x2218) >= field<int32_t>(dec, 0x220c)) return false;
        field<int32_t>(dec, 0x21f4) = 0;
    }
    return true;
}

using PredictFn = void XB_THISCALL(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);

// Forward (put) and/or backward (avg after a forward one, else put) prediction.
void predict(uint8_t* dec, uint32_t type, PredictFn* fn) {
    const uint32_t forward = type & 8;
    if (forward) fn(dec, dec + 0x2168, mc_table(0x00));
    if (type & 4) fn(dec, dec + 0x21a0, mc_table(forward != 0 ? 0x20 : 0x00));
}

}  // namespace

uint32_t XB_FASTCALL MovieVid_DecodeMacroblocks(uint8_t* dec) {
    // slice_init: the window from four raw bytes, quantiser_scale_code, extra slice info.
    const uint8_t* p = field<const uint8_t*>(dec, kBitPtr);
    uint32_t w = static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
                 static_cast<uint32_t>(p[2]) << 8 | p[3];
    field<const uint8_t*>(dec, kBitPtr) = p + 4;
    field<uint32_t>(dec, kBitWindow) = w;
    reset_dc_pred(dec);
    w ^= 0xa6a6a6a6;
    const uint32_t code = w >> 27;
    w <<= 5;
    clear_pmv(dec);
    field<uint32_t>(dec, kBitWindow) = w;
    field<int32_t>(dec, kBitCount) = -11;
    set_quant_tables(dec, code);
    while (static_cast<int32_t>(field<uint32_t>(dec, kBitWindow)) < 0) {
        field<int32_t>(dec, kBitCount) += 9;
        field<uint32_t>(dec, kBitWindow) <<= 9;
        need_bits(dec);
    }
    int32_t mba = 0;
    if (!read_mba_increment(dec, mba, true)) return 1;
    // Position: +4 width; wrap the start offset into rows.
    const int32_t width = field<int32_t>(dec, 4);
    field<int32_t>(dec, 0x21f4) = mba << 4;
    if (static_cast<int32_t>(static_cast<uint32_t>(mba << 4) - static_cast<uint32_t>(width)) >= 0) {
        do {
            field<int32_t>(dec, 0x21f4) -= width;
            field<int32_t>(dec, 0x2218) += 16;
        } while (static_cast<int32_t>(static_cast<uint32_t>(field<int32_t>(dec, 0x21f4)) -
                                      static_cast<uint32_t>(field<int32_t>(dec, 4))) >= 0);
    }
    if (field<int32_t>(dec, 0x2218) > field<int32_t>(dec, 0x220c)) return 1;

    for (;;) {
        need_bits(dec);
        const uint32_t type = MovieVid_ReadMacroblockType(dec);
        if (type & 0x10) {
            const uint32_t window = field<uint32_t>(dec, kBitWindow);
            field<uint32_t>(dec, kBitWindow) = window << 5;
            field<int32_t>(dec, kBitCount) += 5;
            set_quant_tables(dec, window >> 27);
        }
        if (type & 1) {
            // Intra: four luma blocks (field DCT: rows interleaved), then Cb and Cr.
            clear_pmv(dec);
            int32_t stride, next_row;
            if (type & 0x20) {
                next_row = field<int32_t>(dec, 0x21f8);
                stride = next_row * 2;
            } else {
                stride = field<int32_t>(dec, 0x21f8);
                next_row = stride * 8;
            }
            const int32_t offset = field<int32_t>(dec, 0x21f4);
            uint8_t* y = dest(dec, 0) + offset;
            MovieVid_DecodeIntraBlock(dec, 0, y, stride);
            MovieVid_DecodeIntraBlock(dec, 0, y + 8, stride);
            y += next_row;
            MovieVid_DecodeIntraBlock(dec, 0, y, stride);
            MovieVid_DecodeIntraBlock(dec, 0, y + 8, stride);
            const int32_t half = offset >> 1;
            const int32_t uv = field<int32_t>(dec, 0x21fc);
            MovieVid_DecodeIntraBlock(dec, 1, dest(dec, 1) + half, uv);
            const int32_t uv2 = field<int32_t>(dec, 0x21fc);
            MovieVid_DecodeIntraBlock(dec, 2, dest(dec, 2) + half, uv2);
        } else {
            // The motion type (bits 6-7, 0x80 = frame prediction forced): the jump table
            // at 0x000ce054.
            switch (static_cast<uint32_t>(static_cast<int32_t>(type) >> 6)) {
            case 0: predict(dec, type, &MovieVid_PredictZeroMotion); break;
            case 1: predict(dec, type, &MovieVid_PredictFieldMotion); break;
            case 2: predict(dec, type, &MovieVid_PredictFrameMotion); break;
            case 3: predict(dec, type, &MovieVid_PredictDualPrime); break;
            case 4: predict(dec, type, &MovieVid_PredictLastMotion); break;
            default: break;
            }
            if (type & 2) {
                int32_t stride, next_row;
                if (type & 0x20) {
                    next_row = field<int32_t>(dec, 0x21f8);
                    stride = next_row * 2;
                } else {
                    stride = field<int32_t>(dec, 0x21f8);
                    next_row = stride * 8;
                }
                const uint32_t cbp = MovieVid_ReadCodedBlockPattern(dec);
                const int32_t offset = field<int32_t>(dec, 0x21f4);
                uint8_t* y = dest(dec, 0) + offset;
                auto block = [&](uint8_t* dst, int32_t block_stride) {
                    const uint16_t* quant = field<const uint16_t*>(dec, 0x15c);
                    const int32_t last = MovieVid_ReadNonIntraCoeffs(dec, quant);
                    MovieVid_IdctAddBlock(last, field<int16_t*>(dec, 0x154), dst, block_stride);
                };
                if (cbp & 1) block(y, stride);
                if (cbp & 2) block(y + 8, stride);
                if (cbp & 4) block(y + next_row, stride);
                if (cbp & 8) block(y + next_row + 8, stride);
                if (cbp & 0x10) {
                    uint8_t* dst = dest(dec, 1) + (offset >> 1);
                    block(dst, field<int32_t>(dec, 0x21fc));
                }
                if (cbp & 0x20) {
                    uint8_t* dst = dest(dec, 2) + (offset >> 1);
                    block(dst, field<int32_t>(dec, 0x21fc));
                }
            }
            reset_dc_pred(dec);
        }
        if (!next_macroblock(dec)) break;
        need_bits(dec);
        int32_t skip = 0;
        if (!read_mba_increment(dec, skip, false)) {
            movie_vid_emms();
            return 0;
        }
        if (skip == 0) continue;
        // Skipped macroblocks: P pictures predict them with a zero vector, B pictures with
        // the previous macroblock's vectors.
        reset_dc_pred(dec);
        bool done = false;
        if (field<int32_t>(dec, 0x2164) == 2) {
            do {
                MovieVid_PredictZeroMotion(dec, dec + 0x2168, mc_table(0x00));
                if (!next_macroblock(dec)) {
                    done = true;
                    break;
                }
            } while (--skip != 0);
        } else {
            const uint32_t backward = type & 4, forward = type & 8;
            do {
                if (forward) MovieVid_PredictLastMotion(dec, dec + 0x2168, mc_table(0x00));
                if (backward) MovieVid_PredictLastMotion(dec, dec + 0x21a0, mc_table(forward != 0 ? 0x20 : 0x00));
                if (!next_macroblock(dec)) {
                    done = true;
                    break;
                }
            } while (--skip != 0);
        }
        if (done) break;
    }
    // The picture is complete.
    movie_vid_emms();
    field<int32_t>(dec, 0x2230) = 1;
    return 0;
}

void XB_CDECL MovieVid_ConvertMacroblockRow(uint8_t* const* planes, uint8_t* dst, int32_t width) {
    // 16 rows of YUV 4:2:0 to 32-bit pixels, 8 at a time (0x000d0860 converts them into
    // MM0..MM2); each chroma row serves two luma rows.
    const uint8_t* u = planes[1];
    const uint8_t* v = planes[2];
    const uint8_t* y = planes[0];
    uint8_t* out = dst;
    const int32_t half = width >> 1;
    const int32_t groups = width >> 3;
    MmxRegs m;
    auto convert_row = [&] {
        for (int32_t n = groups; n != 0; --n) {
            movie_vid_call_mmx3(0x000d0860, y, u, v, &m);
            const Mm zero = mm_zero();
            const Mm lo = punpcklbw(m.r[0], zero), hi = punpckhbw(m.r[0], zero);
            mm_store(out, punpcklwd(punpcklbw(m.r[2], m.r[1]), lo));
            mm_store(out + 8, punpckhwd(punpcklbw(m.r[2], m.r[1]), lo));
            mm_store(out + 16, punpcklwd(punpckhbw(m.r[2], m.r[1]), hi));
            mm_store(out + 24, punpckhwd(punpckhbw(m.r[2], m.r[1]), hi));
            out += 32;
            y += 8;
            u += 4;
            v += 4;
        }
    };
    for (int row = 0; row < 8; ++row) {
        convert_row();
        u -= half;
        v -= half;
        convert_row();
    }
    movie_vid_emms();
}

}  // namespace game
