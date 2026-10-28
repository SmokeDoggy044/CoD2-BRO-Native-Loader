// The title's zlib 1.1.4 ("inflate 1.1.4 Copyright 1995-2002 Mark Adler", "deflate 1.1.4
// Copyright 1995-2002 Jean-loup Gailly" in the XBE), written from the title's code with the
// upstream source as the guide. Upstream notice:
//
//   zlib.h -- interface of the 'zlib' general purpose compression library
//   version 1.1.4, March 11th, 2002
//
//   Copyright (C) 1995-2002 Jean-loup Gailly and Mark Adler
//
//   This software is provided 'as-is', without any express or implied
//   warranty.  In no event will the authors be held liable for any damages
//   arising from the use of this software.
//
//   Permission is granted to anyone to use this software for any purpose,
//   including commercial applications, and to alter it and redistribute it
//   freely, subject to the following restrictions:
//
//   1. The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software
//      in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
//
//   Jean-loup Gailly        Mark Adler
//   jloup@gzip.org          madler@alumni.caltech.edu
//
// Altered: rewritten in C++ for this port from the compiled title code (structures at the
// title's offsets, callbacks called at title addresses).

#include "unzip.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

constexpr int32_t Z_OK = 0;
constexpr int32_t Z_STREAM_END = 1;
constexpr int32_t Z_STREAM_ERROR = -2;
constexpr int32_t Z_DATA_ERROR = -3;
constexpr int32_t Z_MEM_ERROR = -4;
constexpr int32_t Z_BUF_ERROR = -5;
constexpr int32_t Z_VERSION_ERROR = -6;

constexpr uint32_t kZlibVersion = 0x007baddc;  // "1.1.4"
constexpr uint32_t kZcalloc = 0x0005af00;
constexpr uint32_t kZcfree = 0x0005af20;
constexpr uint32_t kAdler32 = 0x0005ada0;
constexpr uint32_t MANY = 1440;

// ZALLOC / ZFREE through the stream's title callbacks.
void* zalloc(ZStream* z, uint32_t items, uint32_t size) {
    return title_function<void* XB_CDECL(uint32_t, uint32_t, uint32_t)>(z->zalloc)(z->opaque, items, size);
}
void zfree(ZStream* z, void* p) { title_function<void XB_CDECL(uint32_t, void*)>(z->zfree)(z->opaque, p); }
// A check function (adler32) at its title address.
uint32_t call_check(uint32_t fn, uint32_t check, const uint8_t* buf, uint32_t len) {
    return title_function<uint32_t XB_CDECL(uint32_t, const uint8_t*, uint32_t)>(fn)(check, buf, len);
}
// huft_build at its title address (its asm entry reads two stack words it never sets).
int32_t huft_build_call(uint32_t* b, uint32_t* m, uint32_t n, uint32_t s, const uint32_t* d, const uint32_t* e,
                        InflateHuft** t, InflateHuft* hp, uint32_t* hn, uint32_t* v) {
    using HuftBuild = int32_t XB_REGS3 XB_CDECL(uint32_t*, uint32_t, uint32_t*, uint32_t, uint32_t, const uint32_t*,
                                                const uint32_t*, InflateHuft**, InflateHuft*, uint32_t*, uint32_t*);
    return title_function<HuftBuild>(0x0005d190)(m, 0, b, n, s, d, e, t, hp, hn, v);
}

// inflate_mask (the low n bits) in the title's .data: indexed as the original does, so an
// index past its 17 entries (a corrupt exop) reads the same memory.
uint32_t inflate_mask(uint32_t n) { return title_global<uint32_t>(0x008a1d80u + n * 4); }

}  // namespace

int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* /*stream*/) {
    *bl = title_global<uint32_t>(0x008a0c78);  // fixed_bl
    *bd = title_global<uint32_t>(0x008a0c7c);  // fixed_bd
    *tl = &title_global<uint8_t>(0x008a0c80);  // fixed_tl
    *td = &title_global<uint8_t>(0x008a1c80);  // fixed_td
    return 0;
}

int32_t XB_CDECL inflate_fast(uint32_t bl, uint32_t bd, const InflateHuft* tl, const InflateHuft* td,
                              InflateBlocksState* s, ZStream* z) {
    // LOAD
    const uint8_t* p = z->next_in;
    uint32_t n = z->avail_in;
    uint32_t b = s->bitb;
    uint32_t k = s->bitk;
    uint8_t* q = s->write;
    uint32_t m = q < s->read ? static_cast<uint32_t>(s->read - q - 1) : static_cast<uint32_t>(s->end - q);
    const uint32_t ml = inflate_mask(bl);
    const uint32_t md = inflate_mask(bd);

    auto grab = [&](uint32_t bits) {
        while (k < bits) {
            n--;
            b |= static_cast<uint32_t>(*p++) << k;
            k += 8;
        }
    };
    // UNGRAB + UPDATE: give back the whole bytes still in the bit buffer.
    auto finish = [&](int32_t result) {
        uint32_t c = z->avail_in - n;
        if ((k >> 3) < c) c = k >> 3;
        n += c;
        p -= c;
        k -= c << 3;
        s->bitb = b;
        s->bitk = k;
        z->avail_in = n;
        z->total_in += static_cast<uint32_t>(p - z->next_in);
        z->next_in = p;
        s->write = q;
        return result;
    };

    do {
        grab(20);
        const InflateHuft* t = tl + (b & ml);
        uint32_t e = t->exop;
        if (e == 0) {
            b >>= t->bits;
            k -= t->bits;
            *q++ = static_cast<uint8_t>(t->base);
            m--;
            continue;
        }
        for (;;) {
            b >>= t->bits;
            k -= t->bits;
            if (e & 16) {
                // A length, then its distance.
                e &= 15;
                uint32_t c = t->base + (b & inflate_mask(e));
                b >>= e;
                k -= e;
                grab(15);
                t = td + (b & md);
                e = t->exop;
                for (;;) {
                    b >>= t->bits;
                    k -= t->bits;
                    if (e & 16) {
                        e &= 15;
                        grab(e);
                        const uint32_t d = t->base + (b & inflate_mask(e));
                        b >>= e;
                        k -= e;
                        m -= c;
                        const uint8_t* r = q - d;
                        if (r < s->window) {
                            // The match wraps around the window's end.
                            do {
                                r += s->end - s->window;
                            } while (r < s->window);
                            e = static_cast<uint32_t>(s->end - r);
                            if (c > e) {
                                c -= e;
                                do {
                                    *q++ = *r++;
                                } while (--e);
                                r = s->window;
                                do {
                                    *q++ = *r++;
                                } while (--c);
                                break;
                            }
                        }
                        *q++ = *r++;
                        c--;
                        *q++ = *r++;
                        c--;
                        do {
                            *q++ = *r++;
                        } while (--c);
                        break;
                    }
                    if ((e & 64) == 0) {
                        t += t->base;
                        t += b & inflate_mask(e);
                        e = t->exop;
                    } else {
                        z->msg = &title_global<const char>(0x007bbe70);  // "invalid distance code"
                        return finish(-3);                                // Z_DATA_ERROR
                    }
                }
                break;
            }
            if ((e & 64) == 0) {
                t += t->base;
                t += b & inflate_mask(e);
                e = t->exop;
                if (e == 0) {
                    b >>= t->bits;
                    k -= t->bits;
                    *q++ = static_cast<uint8_t>(t->base);
                    m--;
                    break;
                }
            } else if (e & 32) {
                return finish(1);  // Z_STREAM_END
            } else {
                z->msg = &title_global<const char>(0x007bbe88);  // "invalid literal/length code"
                return finish(-3);
            }
        }
    } while (m >= 258 && n >= 10);
    return finish(0);
}


// ---- uncompr.c -----------------------------------------------------------------------

int32_t XB_CDECL uncompress(uint8_t* dest, uint32_t* dest_len, const uint8_t* source, uint32_t source_len) {
    ZStream stream{};
    stream.next_in = source;
    stream.avail_in = source_len;
    stream.next_out = dest;
    stream.avail_out = *dest_len;
    stream.zalloc = 0;
    stream.zfree = 0;
    int32_t err = inflateInit_(&stream, reinterpret_cast<const char*>(uintptr_t{kZlibVersion}), sizeof(ZStream));
    if (err != Z_OK) return err;
    err = inflate(&stream, 4);  // Z_FINISH
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *dest_len = stream.total_out;
    return inflateEnd(&stream);
}

// ---- inflate.c -----------------------------------------------------------------------

int32_t XB_CDECL inflateReset(ZStream* z) {
    if (z == nullptr || z->state == nullptr) return Z_STREAM_ERROR;
    InflateState* state = static_cast<InflateState*>(z->state);
    z->total_in = z->total_out = 0;
    z->msg = nullptr;
    state->mode = state->nowrap ? 7 : 0;  // BLOCKS : METHOD
    inflate_blocks_reset(static_cast<InflateState*>(z->state)->blocks, z, nullptr);
    return Z_OK;
}

int32_t XB_CDECL inflateEnd(ZStream* z) {
    if (z == nullptr || z->state == nullptr || z->zfree == 0) return Z_STREAM_ERROR;
    InflateState* state = static_cast<InflateState*>(z->state);
    if (state->blocks != nullptr) inflate_blocks_free(state->blocks, z);
    zfree(z, z->state);
    z->state = nullptr;
    return Z_OK;
}

int32_t XB_CDECL inflateInit2_(ZStream* z, int32_t w, const char* version, int32_t stream_size) {
    if (version == nullptr || version[0] != '1' || stream_size != static_cast<int32_t>(sizeof(ZStream)))
        return Z_VERSION_ERROR;
    if (z == nullptr) return Z_STREAM_ERROR;
    z->msg = nullptr;
    if (z->zalloc == 0) {
        z->zalloc = kZcalloc;
        z->opaque = 0;
    }
    if (z->zfree == 0) z->zfree = kZcfree;
    z->state = zalloc(z, 1, sizeof(InflateState));
    if (z->state == nullptr) return Z_MEM_ERROR;
    static_cast<InflateState*>(z->state)->blocks = nullptr;
    static_cast<InflateState*>(z->state)->nowrap = 0;
    if (w < 0) {
        w = -w;
        static_cast<InflateState*>(z->state)->nowrap = 1;
    }
    if (w < 8 || w > 15) {
        inflateEnd(z);
        return Z_STREAM_ERROR;
    }
    static_cast<InflateState*>(z->state)->wbits = static_cast<uint32_t>(w);
    InflateBlocksState* blocks = inflate_blocks_new(
        z, static_cast<InflateState*>(z->state)->nowrap ? 0 : kAdler32, uint32_t{1} << w);
    static_cast<InflateState*>(z->state)->blocks = blocks;
    if (static_cast<InflateState*>(z->state)->blocks == nullptr) {
        inflateEnd(z);
        return Z_MEM_ERROR;
    }
    inflateReset(z);
    return Z_OK;
}

int32_t XB_CDECL inflateInit_(ZStream* z, const char* version, int32_t stream_size) {
    return inflateInit2_(z, 15, version, stream_size);
}

int32_t XB_CDECL inflate(ZStream* z, int32_t flush) {
    if (z == nullptr || z->state == nullptr || z->next_in == nullptr) return Z_STREAM_ERROR;
    auto state = [z] { return static_cast<InflateState*>(z->state); };
    const int32_t f = flush == 4 ? Z_BUF_ERROR : Z_OK;  // Z_FINISH
    int32_t r = Z_BUF_ERROR;
    uint32_t b;
    auto next_byte = [z] {
        z->avail_in--;
        z->total_in++;
        return static_cast<uint32_t>(*z->next_in++);
    };
#define NEEDBYTE                       \
    {                                  \
        if (z->avail_in == 0) return r; \
        r = f;                         \
    }
    for (;;) switch (state()->mode) {
            case 0:  // METHOD
                NEEDBYTE
                if (((state()->sub0 = next_byte()) & 0xf) != 8) {  // Z_DEFLATED
                    state()->mode = 13;  // BAD
                    z->msg = &title_global<const char>(0x007bae3c);  // "unknown compression method"
                    state()->sub0 = 5;  // marker: can't try inflateSync
                    break;
                }
                if ((state()->sub0 >> 4) + 8 > state()->wbits) {
                    state()->mode = 13;
                    z->msg = &title_global<const char>(0x007bae28);  // "invalid window size"
                    state()->sub0 = 5;
                    break;
                }
                state()->mode = 1;  // FLAG
                [[fallthrough]];
            case 1:  // FLAG
                NEEDBYTE
                b = next_byte();
                if (((state()->sub0 << 8) + b) % 31) {
                    state()->mode = 13;
                    z->msg = &title_global<const char>(0x007bae10);  // "incorrect header check"
                    state()->sub0 = 5;
                    break;
                }
                if (!(b & 0x20)) {  // PRESET_DICT
                    state()->mode = 7;  // BLOCKS
                    break;
                }
                state()->mode = 2;  // DICT4
                [[fallthrough]];
            case 2:  // DICT4
                NEEDBYTE
                state()->sub1 = next_byte() << 24;
                state()->mode = 3;
                [[fallthrough]];
            case 3:  // DICT3
                NEEDBYTE
                state()->sub1 += next_byte() << 16;
                state()->mode = 4;
                [[fallthrough]];
            case 4:  // DICT2
                NEEDBYTE
                state()->sub1 += next_byte() << 8;
                state()->mode = 5;
                [[fallthrough]];
            case 5:  // DICT1
                NEEDBYTE
                state()->sub1 += next_byte();
                z->adler = state()->sub1;
                state()->mode = 6;  // DICT0
                return 2;           // Z_NEED_DICT
            case 6:  // DICT0
                state()->mode = 13;
                z->msg = &title_global<const char>(0x007bade8);  // "need dictionary"
                state()->sub0 = 0;  // marker: can try inflateSync
                return Z_STREAM_ERROR;
            case 7:  // BLOCKS
                r = inflate_blocks(state()->blocks, z, r);
                if (r == Z_DATA_ERROR) {
                    state()->mode = 13;
                    state()->sub0 = 0;
                    break;
                }
                if (r == Z_OK) r = f;
                if (r != Z_STREAM_END) return r;
                r = f;
                inflate_blocks_reset(state()->blocks, z, &state()->sub0);  // check.was
                if (state()->nowrap) {
                    state()->mode = 12;  // DONE
                    break;
                }
                state()->mode = 8;  // CHECK4
                [[fallthrough]];
            case 8:  // CHECK4
                NEEDBYTE
                state()->sub1 = next_byte() << 24;
                state()->mode = 9;
                [[fallthrough]];
            case 9:  // CHECK3
                NEEDBYTE
                state()->sub1 += next_byte() << 16;
                state()->mode = 10;
                [[fallthrough]];
            case 10:  // CHECK2
                NEEDBYTE
                state()->sub1 += next_byte() << 8;
                state()->mode = 11;
                [[fallthrough]];
            case 11:  // CHECK1
                NEEDBYTE
                state()->sub1 += next_byte();
                if (state()->sub0 != state()->sub1) {
                    state()->mode = 13;
                    z->msg = &title_global<const char>(0x007badf8);  // "incorrect data check"
                    state()->sub0 = 5;
                    break;
                }
                state()->mode = 12;
                [[fallthrough]];
            case 12:  // DONE
                return Z_STREAM_END;
            case 13:  // BAD
                return Z_DATA_ERROR;
            default:
                return Z_STREAM_ERROR;
        }
#undef NEEDBYTE
}

// ---- crc32.c / adler32.c / zutil.c ---------------------------------------------------

uint32_t XB_CDECL crc32(uint32_t crc, const uint8_t* buf, uint32_t len) {
    if (buf == nullptr) return 0;
    const uint32_t* table = &title_global<const uint32_t>(0x007bae58);  // crc_table
    crc = ~crc;
    while (len >= 8) {
        for (int i = 0; i < 8; i++) crc = table[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
        len -= 8;
    }
    if (len) do {
            crc = table[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
        } while (--len);
    return ~crc;
}

uint32_t XB_CDECL adler32(uint32_t adler, const uint8_t* buf, uint32_t len) {
    constexpr uint32_t BASE = 65521;
    constexpr uint32_t NMAX = 5552;
    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = (adler >> 16) & 0xffff;
    if (buf == nullptr) return 1;
    while (len > 0) {
        int32_t k = static_cast<int32_t>(len < NMAX ? len : NMAX);
        len -= static_cast<uint32_t>(k);
        while (k >= 16) {
            for (int i = 0; i < 16; i++) {
                s1 += buf[i];
                s2 += s1;
            }
            buf += 16;
            k -= 16;
        }
        if (k != 0) do {
                s1 += *buf++;
                s2 += s1;
            } while (--k);
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

void* XB_CDECL zcalloc(uint32_t /*opaque*/, uint32_t items, uint32_t size) {
    return title_function<void* XB_CDECL(uint32_t)>(0x001f0d14)(items * size);  // _malloc
}

// ---- infblock.c ----------------------------------------------------------------------

void XB_CDECL inflate_blocks_reset(InflateBlocksState* s, ZStream* z, uint32_t* c) {
    if (c != nullptr) *c = s->check;
    if (s->mode == 4 || s->mode == 5) zfree(z, reinterpret_cast<void*>(uintptr_t{s->sub[2]}));  // BTREE/DTREE: blens
    if (s->mode == 6) inflate_codes_free(reinterpret_cast<InflateCodesState*>(uintptr_t{s->sub[0]}), z);  // CODES
    s->mode = 0;  // TYPE
    s->bitk = 0;
    s->bitb = 0;
    s->read = s->write = s->window;
    if (s->checkfn != 0) z->adler = s->check = call_check(s->checkfn, 0, nullptr, 0);
}

InflateBlocksState* XB_CDECL inflate_blocks_new(ZStream* z, uint32_t checkfn, uint32_t w) {
    InflateBlocksState* s = static_cast<InflateBlocksState*>(zalloc(z, 1, sizeof(InflateBlocksState)));
    if (s == nullptr) return s;
    s->hufts = static_cast<InflateHuft*>(zalloc(z, sizeof(InflateHuft), MANY));
    if (s->hufts == nullptr) {
        zfree(z, s);
        return nullptr;
    }
    s->window = static_cast<uint8_t*>(zalloc(z, 1, w));
    if (s->window == nullptr) {
        zfree(z, s->hufts);
        zfree(z, s);
        return nullptr;
    }
    s->end = s->window + w;
    s->checkfn = checkfn;
    s->mode = 0;  // TYPE
    inflate_blocks_reset(s, z, nullptr);
    return s;
}

int32_t XB_CDECL inflate_blocks_free(InflateBlocksState* s, ZStream* z) {
    inflate_blocks_reset(s, z, nullptr);
    zfree(z, s->window);
    zfree(z, s->hufts);
    zfree(z, s);
    return Z_OK;
}

// ---- infcodes.c / infutil.c ----------------------------------------------------------

InflateCodesState* XB_CDECL inflate_codes_new(uint32_t bl, uint32_t bd, const InflateHuft* tl,
                                              const InflateHuft* td, ZStream* z) {
    InflateCodesState* c = static_cast<InflateCodesState*>(zalloc(z, 1, sizeof(InflateCodesState)));
    if (c != nullptr) {
        c->mode = 0;  // START
        c->lbits = static_cast<uint8_t>(bl);
        c->dbits = static_cast<uint8_t>(bd);
        c->ltree = tl;
        c->dtree = td;
    }
    return c;
}

void XB_CDECL inflate_codes_free(InflateCodesState* c, ZStream* z) { zfree(z, c); }

int32_t XB_CDECL inflate_flush(InflateBlocksState* s, ZStream* z, int32_t r_in) {
    int32_t r = r_in;
    uint8_t* p = z->next_out;
    uint8_t* q = s->read;

    // As far as the end of the window.
    uint32_t n = static_cast<uint32_t>((q <= s->write ? s->write : s->end) - q);
    if (n > z->avail_out) n = z->avail_out;
    if (n && r == Z_BUF_ERROR) r = Z_OK;
    z->avail_out -= n;
    z->total_out += n;
    if (s->checkfn != 0) z->adler = s->check = call_check(s->checkfn, s->check, q, n);
    platform_rep_movs(p, q, n);
    p += n;
    q += n;

    // The rest from the start of the window.
    if (q == s->end) {
        q = s->window;
        if (s->write == s->end) s->write = s->window;
        n = static_cast<uint32_t>(s->write - q);
        if (n > z->avail_out) n = z->avail_out;
        if (n && r == Z_BUF_ERROR) r = Z_OK;
        z->avail_out -= n;
        z->total_out += n;
        if (s->checkfn != 0) z->adler = s->check = call_check(s->checkfn, s->check, q, n);
        platform_rep_movs(p, q, n);
        p += n;
        q += n;
    }
    z->next_out = p;
    s->read = q;
    return r;
}

// ---- infutil.h's macros, over the locals b, k, p, n, q, m of inflate_blocks/inflate_codes ----

#define UPDBITS \
    {           \
        s->bitb = b; \
        s->bitk = k; \
    }
#define UPDIN                                                      \
    {                                                              \
        z->avail_in = n;                                           \
        z->total_in += static_cast<uint32_t>(p - z->next_in);      \
        z->next_in = p;                                            \
    }
#define UPDOUT \
    { s->write = q; }
#define UPDATE \
    {          \
        UPDBITS UPDIN UPDOUT \
    }
#define LEAVE                           \
    {                                   \
        UPDATE return inflate_flush(s, z, r); \
    }
#define LOADIN            \
    {                     \
        p = z->next_in;   \
        n = z->avail_in;  \
        b = s->bitb;      \
        k = s->bitk;      \
    }
#define NEEDBYTE   \
    {              \
        if (n)     \
            r = Z_OK; \
        else       \
            LEAVE  \
    }
#define NEXTBYTE (n--, *p++)
#define NEEDBITS(j)                                     \
    {                                                   \
        while (k < (j)) {                               \
            NEEDBYTE;                                   \
            b |= static_cast<uint32_t>(NEXTBYTE) << k;  \
            k += 8;                                     \
        }                                               \
    }
#define DUMPBITS(j) \
    {               \
        b >>= (j);  \
        k -= (j);   \
    }
#define WAVAIL static_cast<uint32_t>(q < s->read ? s->read - q - 1 : s->end - q)
#define LOADOUT    \
    {              \
        q = s->write; \
        m = WAVAIL; \
    }
#define WRAP                                          \
    {                                                 \
        if (q == s->end && s->read != s->window) {    \
            q = s->window;                            \
            m = WAVAIL;                               \
        }                                             \
    }
#define FLUSH                      \
    {                              \
        UPDOUT r = inflate_flush(s, z, r); \
        LOADOUT                    \
    }
#define NEEDOUT                \
    {                          \
        if (m == 0) {          \
            WRAP if (m == 0) { \
                FLUSH WRAP if (m == 0) LEAVE \
            }                  \
        }                      \
        r = Z_OK;              \
    }
#define OUTBYTE(a)                       \
    {                                    \
        *q++ = static_cast<uint8_t>(a);  \
        m--;                             \
    }
#define LOAD   \
    {          \
        LOADIN LOADOUT \
    }

// ---- infblock.c ----------------------------------------------------------------------

int32_t XB_CDECL inflate_blocks(InflateBlocksState* s, ZStream* z, int32_t r_in) {
    int32_t r = r_in;
    uint32_t t;
    uint32_t b, k;
    const uint8_t* p;
    uint32_t n;
    uint8_t* q;
    uint32_t m;
    const uint32_t* border = &title_global<const uint32_t>(0x007bb308);  // bit length code order

    LOAD
    for (;;) switch (s->mode) {
            case 0:  // TYPE
                NEEDBITS(3)
                t = b & 7;
                s->last = t & 1;
                switch (t >> 1) {
                    case 0:  // stored
                        DUMPBITS(3)
                        t = k & 7;  // go to byte boundary
                        DUMPBITS(t)
                        s->mode = 1;  // LENS
                        break;
                    case 1: {  // fixed
                        uint32_t bl, bd;
                        void *tl, *td;
                        inflate_trees_fixed(&bl, &bd, &tl, &td, z);
                        s->sub[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(inflate_codes_new(
                            bl, bd, static_cast<const InflateHuft*>(tl), static_cast<const InflateHuft*>(td), z)));
                        if (s->sub[0] == 0) {
                            r = Z_MEM_ERROR;
                            LEAVE
                        }
                        DUMPBITS(3)
                        s->mode = 6;  // CODES
                        break;
                    }
                    case 2:  // dynamic
                        DUMPBITS(3)
                        s->mode = 3;  // TABLE
                        break;
                    case 3:  // illegal
                        DUMPBITS(3)
                        s->mode = 9;  // BAD
                        z->msg = &title_global<const char>(0x007bb3b4);  // "invalid block type"
                        r = Z_DATA_ERROR;
                        LEAVE
                }
                break;
            case 1:  // LENS
                NEEDBITS(32)
                if (((~b >> 16) & 0xffff) != (b & 0xffff)) {
                    s->mode = 9;
                    z->msg = &title_global<const char>(0x007bb394);  // "invalid stored block lengths"
                    r = Z_DATA_ERROR;
                    LEAVE
                }
                s->sub[0] = b & 0xffff;  // left
                b = k = 0;
                s->mode = s->sub[0] ? 2 : (s->last ? 7 : 0);  // STORED : DRY : TYPE
                break;
            case 2:  // STORED
                if (n == 0) LEAVE
                NEEDOUT
                t = s->sub[0];
                if (t > n) t = n;
                if (t > m) t = m;
                platform_rep_movs(q, p, t);
                p += t;
                n -= t;
                q += t;
                m -= t;
                if ((s->sub[0] -= t) != 0) break;
                s->mode = s->last ? 7 : 0;
                break;
            case 3:  // TABLE
                NEEDBITS(14)
                s->sub[0] = t = b & 0x3fff;  // trees.table
                if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29) {
                    s->mode = 9;
                    z->msg = &title_global<const char>(0x007bb370);  // "too many length or distance symbols"
                    r = Z_DATA_ERROR;
                    LEAVE
                }
                t = 258 + (t & 0x1f) + ((t >> 5) & 0x1f);
                s->sub[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zalloc(z, t, 4)));  // trees.blens
                if (s->sub[2] == 0) {
                    r = Z_MEM_ERROR;
                    LEAVE
                }
                DUMPBITS(14)
                s->sub[1] = 0;  // trees.index
                s->mode = 4;    // BTREE
                [[fallthrough]];
            case 4: {  // BTREE
                while (s->sub[1] < 4 + (s->sub[0] >> 10)) {
                    NEEDBITS(3)
                    reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]})[border[s->sub[1]++]] = b & 7;
                    DUMPBITS(3)
                }
                while (s->sub[1] < 19) reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]})[border[s->sub[1]++]] = 0;
                s->sub[3] = 7;  // trees.bb
                t = static_cast<uint32_t>(inflate_trees_bits(reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]}),
                                                             &s->sub[3],
                                                             reinterpret_cast<InflateHuft**>(&s->sub[4]),
                                                             s->hufts, z));
                if (t != Z_OK) {
                    r = static_cast<int32_t>(t);
                    if (r == Z_DATA_ERROR) {
                        zfree(z, reinterpret_cast<void*>(uintptr_t{s->sub[2]}));
                        s->mode = 9;
                    }
                    LEAVE
                }
                s->sub[1] = 0;
                s->mode = 5;  // DTREE
            }
                [[fallthrough]];
            case 5: {  // DTREE
                while (t = s->sub[0], s->sub[1] < 258 + (t & 0x1f) + ((t >> 5) & 0x1f)) {
                    t = s->sub[3];
                    NEEDBITS(t)
                    const InflateHuft* h = reinterpret_cast<const InflateHuft*>(uintptr_t{s->sub[4]}) +
                                           (b & inflate_mask(t));
                    t = h->bits;
                    uint32_t c = h->base;
                    if (c < 16) {
                        DUMPBITS(t)
                        reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]})[s->sub[1]++] = c;
                    } else {  // c == 16..18
                        uint32_t i = c == 18 ? 7 : c - 14;
                        uint32_t j = c == 18 ? 11 : 3;
                        NEEDBITS(t + i)
                        DUMPBITS(t)
                        j += b & inflate_mask(i);
                        DUMPBITS(i)
                        i = s->sub[1];
                        t = s->sub[0];
                        if (i + j > 258 + (t & 0x1f) + ((t >> 5) & 0x1f) || (c == 16 && i < 1)) {
                            zfree(z, reinterpret_cast<void*>(uintptr_t{s->sub[2]}));
                            s->mode = 9;
                            z->msg = &title_global<const char>(0x007bb354);  // "invalid bit length repeat"
                            r = Z_DATA_ERROR;
                            LEAVE
                        }
                        uint32_t* blens = reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]});
                        c = c == 16 ? blens[i - 1] : 0;
                        do {
                            blens[i++] = c;
                        } while (--j);
                        s->sub[1] = i;
                    }
                }
                s->sub[4] = 0;  // trees.tb
                {
                    uint32_t bl = 9, bd = 6;
                    InflateHuft *tl, *td;
                    t = s->sub[0];
                    t = static_cast<uint32_t>(inflate_trees_dynamic(
                        257 + (t & 0x1f), 1 + ((t >> 5) & 0x1f), reinterpret_cast<uint32_t*>(uintptr_t{s->sub[2]}),
                        &bl, &bd, &tl, &td, s->hufts, z));
                    if (t != Z_OK) {
                        if (t == static_cast<uint32_t>(Z_DATA_ERROR)) {
                            zfree(z, reinterpret_cast<void*>(uintptr_t{s->sub[2]}));
                            s->mode = 9;
                        }
                        r = static_cast<int32_t>(t);
                        LEAVE
                    }
                    InflateCodesState* c = inflate_codes_new(bl, bd, tl, td, z);
                    if (c == nullptr) {
                        r = Z_MEM_ERROR;
                        LEAVE
                    }
                    s->sub[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c));  // decode.codes
                }
                zfree(z, reinterpret_cast<void*>(uintptr_t{s->sub[2]}));
                s->mode = 6;  // CODES
            }
                [[fallthrough]];
            case 6:  // CODES
                UPDATE
                if ((r = inflate_codes(s, z, r)) != Z_STREAM_END) return inflate_flush(s, z, r);
                r = Z_OK;
                inflate_codes_free(reinterpret_cast<InflateCodesState*>(uintptr_t{s->sub[0]}), z);
                LOAD
                if (!s->last) {
                    s->mode = 0;
                    break;
                }
                s->mode = 7;  // DRY
                [[fallthrough]];
            case 7:  // DRY
                FLUSH
                if (s->read != s->write) LEAVE
                s->mode = 8;  // DONE
                [[fallthrough]];
            case 8:  // DONE
                r = Z_STREAM_END;
                LEAVE
            case 9:  // BAD
                r = Z_DATA_ERROR;
                LEAVE
            default:
                r = Z_STREAM_ERROR;
                LEAVE
        }
}

// ---- infcodes.c ----------------------------------------------------------------------

int32_t XB_CDECL inflate_codes(InflateBlocksState* s, ZStream* z, int32_t r_in) {
    int32_t r = r_in;
    uint32_t j;
    const InflateHuft* t;
    uint32_t e;
    uint32_t b, k;
    const uint8_t* p;
    uint32_t n;
    uint8_t* q;
    uint32_t m;
    uint8_t* f;
    InflateCodesState* c = reinterpret_cast<InflateCodesState*>(uintptr_t{s->sub[0]});

    LOAD
    for (;;) switch (c->mode) {
            case 0:  // START
                if (m >= 258 && n >= 10) {
                    UPDATE
                    r = inflate_fast(c->lbits, c->dbits, c->ltree, c->dtree, s, z);
                    LOAD
                    if (r != Z_OK) {
                        c->mode = r == Z_STREAM_END ? 7 : 9;  // WASH : BADCODE
                        break;
                    }
                }
                c->sub1 = c->lbits;  // code.need
                c->sub0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c->ltree));  // code.tree
                c->mode = 1;  // LEN
                [[fallthrough]];
            case 1:  // LEN
                j = c->sub1;
                NEEDBITS(j)
                t = reinterpret_cast<const InflateHuft*>(uintptr_t{c->sub0}) + (b & inflate_mask(j));
                DUMPBITS(t->bits)
                e = t->exop;
                if (e == 0) {  // literal
                    c->sub0 = t->base;
                    c->mode = 6;  // LIT
                    break;
                }
                if (e & 16) {  // length
                    c->sub0 = e & 15;  // copy.get
                    c->len = t->base;
                    c->mode = 2;  // LENEXT
                    break;
                }
                if ((e & 64) == 0) {  // next table
                    c->sub1 = e;
                    c->sub0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t + t->base));
                    break;
                }
                if (e & 32) {  // end of block
                    c->mode = 7;  // WASH
                    break;
                }
                c->mode = 9;  // BADCODE
                z->msg = &title_global<const char>(0x007bbe88);  // "invalid literal/length code"
                r = Z_DATA_ERROR;
                LEAVE
            case 2:  // LENEXT
                j = c->sub0;
                NEEDBITS(j)
                c->len += b & inflate_mask(j);
                DUMPBITS(j)
                c->sub1 = c->dbits;
                c->sub0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c->dtree));
                c->mode = 3;  // DIST
                [[fallthrough]];
            case 3:  // DIST
                j = c->sub1;
                NEEDBITS(j)
                t = reinterpret_cast<const InflateHuft*>(uintptr_t{c->sub0}) + (b & inflate_mask(j));
                DUMPBITS(t->bits)
                e = t->exop;
                if (e & 16) {  // distance
                    c->sub0 = e & 15;     // copy.get
                    c->sub1 = t->base;    // copy.dist
                    c->mode = 4;          // DISTEXT
                    break;
                }
                if ((e & 64) == 0) {  // next table
                    c->sub1 = e;
                    c->sub0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t + t->base));
                    break;
                }
                c->mode = 9;
                z->msg = &title_global<const char>(0x007bbe70);  // "invalid distance code"
                r = Z_DATA_ERROR;
                LEAVE
            case 4:  // DISTEXT
                j = c->sub0;
                NEEDBITS(j)
                c->sub1 += b & inflate_mask(j);
                DUMPBITS(j)
                c->mode = 5;  // COPY
                [[fallthrough]];
            case 5:  // COPY
                f = q - c->sub1;
                while (f < s->window) f += s->end - s->window;  // modulo window size
                while (c->len) {
                    NEEDOUT
                    OUTBYTE(*f++)
                    if (f == s->end) f = s->window;
                    c->len--;
                }
                c->mode = 0;
                break;
            case 6:  // LIT
                NEEDOUT
                OUTBYTE(c->sub0)
                c->mode = 0;
                break;
            case 7:  // WASH
                if (k > 7) {  // return unused byte, if any
                    k -= 8;
                    n++;
                    p--;
                }
                FLUSH
                if (s->read != s->write) LEAVE
                c->mode = 8;  // END
                [[fallthrough]];
            case 8:  // END
                r = Z_STREAM_END;
                LEAVE
            case 9:  // BADCODE
                r = Z_DATA_ERROR;
                LEAVE
            default:
                r = Z_STREAM_ERROR;
                LEAVE
        }
}

#undef UPDBITS
#undef UPDIN
#undef UPDOUT
#undef UPDATE
#undef LEAVE
#undef LOADIN
#undef NEEDBYTE
#undef NEXTBYTE
#undef NEEDBITS
#undef DUMPBITS
#undef WAVAIL
#undef LOADOUT
#undef WRAP
#undef FLUSH
#undef NEEDOUT
#undef OUTBYTE
#undef LOAD

// ---- inftrees.c ----------------------------------------------------------------------

PLATFORM_ASM_ENTRY(huft_build_entry,
                   // The original never writes two words of its frame: r's padding (the word at
                   // entry - 0xd8, whose low half r's exop and bits overwrite) and the one at
                   // entry - 0xd4 (r.base before the first code). The C version gets both and
                   // leaves them as the original would: r's last value, the other word untouched.
                   "pushl -0xd4(%esp)\n"  // entry - 0xd4
                   "pushl -0xd4(%esp)\n"  // entry - 0xd8
                   "movl %esp, %edx\n"
                   "pushl %edx\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(huft_build) "addl $44, %esp\n"
                   "popl %edx\n"
                   "movl %edx, -0xd4(%esp)\n"
                   "popl %edx\n"
                   "movl %edx, -0xd4(%esp)\n"
                   "ret")

int32_t XB_CDECL huft_build(uint32_t* b, uint32_t* m, uint32_t n_in, uint32_t s, const uint32_t* d,
                            const uint32_t* e, InflateHuft** t, InflateHuft* hp, uint32_t* hn, uint32_t* v,
                            uint32_t* frame) {
    constexpr uint32_t BMAX = 15;
    uint32_t n = n_in;
    uint32_t a;
    uint32_t c[BMAX + 1];
    uint32_t f;
    int32_t g;
    int32_t h;
    uint32_t i;
    uint32_t j;
    int32_t k;
    int32_t l;
    uint32_t mask;
    uint32_t* p;
    InflateHuft* q;
    InflateHuft r;
    InflateHuft* u[BMAX];
    int32_t w;
    uint32_t x[BMAX + 1];
    uint32_t* xp;
    int32_t y;
    uint32_t z;

    // r's first word and its base before the first code are stack words the original never
    // sets; the first word is written back as r's last value (the original keeps r there).
    std::memcpy(&r, &frame[0], 4);
    r.base = frame[1];
    auto done = [&](int32_t result) {
        std::memcpy(&frame[0], &r, 4);
        return result;
    };

    // Generate counts for each bit length.
    for (i = 0; i <= BMAX; i++) c[i] = 0;
    p = b;
    i = n;
    do {
        c[*p++]++;  // assume all entries <= BMAX
    } while (--i);
    if (c[0] == n) {  // null input--all zero length codes
        *t = nullptr;
        *m = 0;
        return done(Z_OK);
    }

    // Find minimum and maximum length, bound *m by those.
    l = static_cast<int32_t>(*m);
    for (j = 1; j <= BMAX; j++)
        if (c[j]) break;
    k = static_cast<int32_t>(j);  // minimum code length
    if (static_cast<uint32_t>(l) < j) l = static_cast<int32_t>(j);
    for (i = BMAX; i; i--)
        if (c[i]) break;
    g = static_cast<int32_t>(i);  // maximum code length
    if (static_cast<uint32_t>(l) > i) l = static_cast<int32_t>(i);
    *m = static_cast<uint32_t>(l);

    // Adjust last length count to fill out codes, if needed.
    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= static_cast<int32_t>(c[j])) < 0) return done(Z_DATA_ERROR);
    if ((y -= static_cast<int32_t>(c[i])) < 0) return done(Z_DATA_ERROR);
    c[i] += static_cast<uint32_t>(y);

    // Generate starting offsets into the value table for each length.
    x[1] = j = 0;
    p = c + 1;
    xp = x + 2;
    while (--i) *xp++ = (j += *p++);  // i == g from above

    // Make a table of values in order of bit lengths.
    p = b;
    i = 0;
    do {
        if ((j = *p++) != 0) v[x[j]++] = i;
    } while (++i < n);
    n = x[g];  // length of v

    // Generate the Huffman codes and for each, make the table entries.
    x[0] = i = 0;
    p = v;
    h = -1;
    w = -l;
    u[0] = nullptr;
    q = nullptr;
    z = 0;
    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            // Make tables up to the required level.
            while (k > w + l) {
                h++;
                w += l;
                z = static_cast<uint32_t>(g - w);
                z = z > static_cast<uint32_t>(l) ? static_cast<uint32_t>(l) : z;
                if ((f = 1u << (j = static_cast<uint32_t>(k - w))) > a + 1) {
                    f -= a + 1;
                    xp = c + k;
                    if (j < z)
                        while (++j < z) {
                            if ((f <<= 1) <= *++xp) break;
                            f -= *xp;
                        }
                }
                z = 1u << j;
                if (*hn + z > MANY) return done(Z_DATA_ERROR);
                u[h] = q = hp + *hn;
                *hn += z;
                if (h) {
                    x[h] = i;
                    r.bits = static_cast<uint8_t>(l);
                    r.exop = static_cast<uint8_t>(j);
                    j = i >> (w - l);
                    r.base = static_cast<uint32_t>(q - u[h - 1] - j);
                    u[h - 1][j] = r;
                } else {
                    *t = q;
                }
            }

            // Set up the table entry in r.
            r.bits = static_cast<uint8_t>(k - w);
            if (p >= v + n) {
                r.exop = 128 + 64;  // out of values--invalid code
            } else if (*p < s) {
                r.exop = static_cast<uint8_t>(*p < 256 ? 0 : 32 + 64);  // 256 is end-of-block
                r.base = *p++;
            } else {
                r.exop = static_cast<uint8_t>(e[*p - s] + 16 + 64);
                r.base = d[*p++ - s];
            }

            // Fill code-like entries with r.
            f = 1u << (k - w);
            for (j = i >> w; j < z; j += f) q[j] = r;

            // Backwards increment the k-bit code i.
            for (j = 1u << (k - 1); i & j; j >>= 1) i ^= j;
            i ^= j;

            // Back up over finished tables.
            mask = (1u << w) - 1;
            while ((i & mask) != x[h]) {
                h--;
                w -= l;
                mask = (1u << w) - 1;
            }
        }
    }
    return done(y != 0 && g != 1 ? Z_BUF_ERROR : Z_OK);
}

int32_t XB_CDECL inflate_trees_bits(uint32_t* c, uint32_t* bb, InflateHuft** tb, InflateHuft* hp, ZStream* z) {
    uint32_t hn = 0;
    uint32_t* v = static_cast<uint32_t*>(zalloc(z, 19, 4));
    if (v == nullptr) return Z_MEM_ERROR;
    int32_t r = huft_build_call(c, bb, 19, 19, nullptr, nullptr, tb, hp, &hn, v);
    if (r == Z_DATA_ERROR) {
        z->msg = &title_global<const char>(0x007bc0ec);  // "oversubscribed dynamic bit lengths tree"
    } else if (r == Z_BUF_ERROR || *bb == 0) {
        z->msg = &title_global<const char>(0x007bc0c8);  // "incomplete dynamic bit lengths tree"
        r = Z_DATA_ERROR;
    }
    zfree(z, v);
    return r;
}

int32_t XB_CDECL inflate_trees_dynamic(uint32_t nl, uint32_t nd, uint32_t* c, uint32_t* bl, uint32_t* bd,
                                       InflateHuft** tl, InflateHuft** td, InflateHuft* hp, ZStream* z) {
    uint32_t hn = 0;
    uint32_t* v = static_cast<uint32_t*>(zalloc(z, 288, 4));
    if (v == nullptr) return Z_MEM_ERROR;

    // Literal/length tree.
    int32_t r = huft_build_call(c, bl, nl, 257, &title_global<const uint32_t>(0x007bbed8),  // cplens
                                &title_global<const uint32_t>(0x007bbf58), tl, hp, &hn, v);  // cplext
    if (r != Z_OK || *bl == 0) {
        if (r == Z_DATA_ERROR) {
            z->msg = &title_global<const char>(0x007bc134);  // "oversubscribed literal/length tree"
        } else if (r != Z_MEM_ERROR) {
            z->msg = &title_global<const char>(0x007bc114);  // "incomplete literal/length tree"
            r = Z_DATA_ERROR;
        }
        zfree(z, v);
        return r;
    }

    // Distance tree.
    r = huft_build_call(c + nl, bd, nd, 0, &title_global<const uint32_t>(0x007bbfd8),  // cpdist
                        &title_global<const uint32_t>(0x007bc050), td, hp, &hn, v);  // cpdext
    if (r != Z_OK || (*bd == 0 && nl > 257)) {
        if (r == Z_DATA_ERROR) {
            z->msg = &title_global<const char>(0x007bc198);  // "oversubscribed distance tree"
        } else if (r == Z_BUF_ERROR) {
            z->msg = &title_global<const char>(0x007bc17c);  // "incomplete distance tree"
            r = Z_DATA_ERROR;
        } else if (r != Z_MEM_ERROR) {
            z->msg = &title_global<const char>(0x007bc158);  // "empty distance tree with lengths"
            r = Z_DATA_ERROR;
        }
        zfree(z, v);
        return r;
    }
    zfree(z, v);
    return Z_OK;
}

// ---- deflate.c -----------------------------------------------------------------------

namespace {

constexpr int32_t INIT_STATE = 42;
constexpr int32_t BUSY_STATE = 113;
constexpr int32_t FINISH_STATE = 666;
constexpr uint32_t MIN_MATCH = 3;
constexpr uint32_t MAX_MATCH = 258;
constexpr uint32_t MIN_LOOKAHEAD = MAX_MATCH + MIN_MATCH + 1;
constexpr uint32_t TOO_FAR = 4096;
constexpr int32_t need_more = 0, block_done = 1, finish_started = 2, finish_done = 3;

// z_errmsg[2 - err]
const char* err_msg(int32_t err) { return title_global<const char*>(0x008a0c14u + static_cast<uint32_t>(2 - err) * 4); }

uint32_t max_dist(const DeflateState* s) { return s->w_size - MIN_LOOKAHEAD; }

void put_byte(DeflateState* s, uint8_t c) { s->pending_buf[s->pending++] = c; }

void update_hash(DeflateState* s, uint32_t& h, uint32_t c) { h = ((h << s->hash_shift) ^ c) & s->hash_mask; }

// INSERT_STRING: returns the previous head of the hash chain.
uint32_t insert_string(DeflateState* s, uint32_t str) {
    update_hash(s, s->ins_h, s->window[str + (MIN_MATCH - 1)]);
    const uint32_t match_head = s->head[s->ins_h];
    s->prev[str & s->w_mask] = static_cast<uint16_t>(match_head);
    s->head[s->ins_h] = static_cast<uint16_t>(str);
    return match_head;
}

void clear_hash(DeflateState* s) {
    s->head[s->hash_size - 1] = 0;
    std::memset(s->head, 0, (s->hash_size - 1) * sizeof(uint16_t));
}

// _length_code[256] and _dist_code[512] (trees.h).
uint8_t length_code(uint32_t len) { return title_global<const uint8_t>(0x007bbc80u + len); }
uint8_t d_code(uint32_t dist) {
    return dist < 256 ? title_global<const uint8_t>(0x007bba80u + dist)
                      : title_global<const uint8_t>(0x007bba80u + 256 + (dist >> 7));
}

// _tr_tally_lit / _tr_tally_dist: true when the literal buffer is full.
bool tally_lit(DeflateState* s, uint8_t cc) {
    s->d_buf[s->last_lit] = 0;
    s->l_buf[s->last_lit++] = cc;
    s->dyn_ltree[cc].fc++;
    return s->last_lit == s->lit_bufsize - 1;
}
bool tally_dist(DeflateState* s, uint32_t distance, uint32_t length) {
    const uint8_t len = static_cast<uint8_t>(length);
    uint16_t dist = static_cast<uint16_t>(distance);
    s->d_buf[s->last_lit] = dist;
    s->l_buf[s->last_lit++] = len;
    dist--;
    s->dyn_ltree[length_code(len) + 256 + 1].fc++;
    s->dyn_dtree[d_code(dist)].fc++;
    return s->last_lit == s->lit_bufsize - 1;
}

// FLUSH_BLOCK_ONLY
void flush_block_only(DeflateState* s, int32_t eof) {
    _tr_flush_block(s, s->block_start >= 0 ? &s->window[static_cast<uint32_t>(s->block_start)] : nullptr,
                    static_cast<uint32_t>(static_cast<int32_t>(s->strstart) - s->block_start), eof);
    s->block_start = static_cast<int32_t>(s->strstart);
    flush_pending(s->strm);
}

}  // namespace

int32_t XB_CDECL compress2(uint8_t* dest, uint32_t* dest_len, const uint8_t* source, uint32_t source_len,
                           int32_t level) {
    ZStream stream{};
    stream.next_in = source;
    stream.avail_in = source_len;
    stream.next_out = dest;
    stream.avail_out = *dest_len;
    stream.zalloc = 0;
    stream.zfree = 0;
    stream.opaque = 0;
    int32_t err =
        deflateInit_(&stream, level, reinterpret_cast<const char*>(uintptr_t{kZlibVersion}), sizeof(ZStream));
    if (err != Z_OK) return err;
    err = deflate(&stream, 4);  // Z_FINISH
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *dest_len = stream.total_out;
    return deflateEnd(&stream);
}

DeflateState* XB_REGS3 XB_CDECL putShortMSB(DeflateState* s, uint32_t /*edx_unused*/, uint32_t b) {
    put_byte(s, static_cast<uint8_t>(b >> 8));
    put_byte(s, static_cast<uint8_t>(b & 0xff));
    return s;
}

void XB_REGS1 XB_CDECL flush_pending(ZStream* strm) {
    DeflateState* state = static_cast<DeflateState*>(strm->state);
    uint32_t len = static_cast<uint32_t>(state->pending);
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;
    platform_rep_movs(strm->next_out, state->pending_out, len);
    strm->next_out += len;
    static_cast<DeflateState*>(strm->state)->pending_out += len;
    strm->total_out += len;
    strm->avail_out -= len;
    static_cast<DeflateState*>(strm->state)->pending -= static_cast<int32_t>(len);
    state = static_cast<DeflateState*>(strm->state);
    if (state->pending == 0) state->pending_out = state->pending_buf;
}

int32_t XB_CDECL deflate(ZStream* strm, int32_t flush) {
    if (strm == nullptr || strm->state == nullptr || flush > 4 || flush < 0) return Z_STREAM_ERROR;
    DeflateState* s = static_cast<DeflateState*>(strm->state);
    if (strm->next_out == nullptr || (strm->next_in == nullptr && strm->avail_in != 0) ||
        (s->status == FINISH_STATE && flush != 4)) {
        strm->msg = err_msg(Z_STREAM_ERROR);
        return Z_STREAM_ERROR;
    }
    if (strm->avail_out == 0) {
        strm->msg = err_msg(Z_BUF_ERROR);
        return Z_BUF_ERROR;
    }
    s->strm = strm;
    const int32_t old_flush = s->last_flush;
    s->last_flush = flush;

    // The zlib header.
    if (s->status == INIT_STATE) {
        uint32_t header = (8 + ((s->w_bits - 8) << 4)) << 8;
        uint32_t level_flags = static_cast<uint32_t>((s->level - 1) >> 1);
        if (level_flags > 3) level_flags = 3;
        header |= level_flags << 6;
        if (s->strstart != 0) header |= 0x20;  // PRESET_DICT
        header += 31 - (header % 31);
        s->status = BUSY_STATE;
        putShortMSB(s, 0, header);
        if (s->strstart != 0) {
            putShortMSB(s, 0, strm->adler >> 16);
            putShortMSB(s, 0, strm->adler & 0xffff);
        }
        strm->adler = 1;
    }

    // Flush as much pending output as possible.
    if (s->pending != 0) {
        flush_pending(strm);
        if (strm->avail_out == 0) {
            s->last_flush = -1;
            return Z_OK;
        }
    } else if (strm->avail_in == 0 && flush <= old_flush && flush != 4) {
        strm->msg = err_msg(Z_BUF_ERROR);
        return Z_BUF_ERROR;
    }
    if (s->status == FINISH_STATE && strm->avail_in != 0) {
        strm->msg = err_msg(Z_BUF_ERROR);
        return Z_BUF_ERROR;
    }

    // Start a new block or continue the current one.
    if (strm->avail_in != 0 || s->lookahead != 0 || (flush != 0 && s->status != FINISH_STATE)) {
        const uint32_t func = title_global<uint32_t>(0x007bb298u + static_cast<uint32_t>(s->level) * 12);
        const int32_t bstate = title_function<int32_t XB_CDECL(DeflateState*, int32_t)>(func)(s, flush);
        if (bstate == finish_started || bstate == finish_done) s->status = FINISH_STATE;
        if (bstate == need_more || bstate == finish_started) {
            if (strm->avail_out == 0) s->last_flush = -1;
            return Z_OK;
        }
        if (bstate == block_done) {
            if (flush == 1) {  // Z_PARTIAL_FLUSH
                _tr_align(s);
            } else {
                _tr_stored_block(s, nullptr, 0, 0);
                if (flush == 3) clear_hash(s);  // Z_FULL_FLUSH: forget history
            }
            flush_pending(strm);
            if (strm->avail_out == 0) {
                s->last_flush = -1;
                return Z_OK;
            }
        }
    }
    if (flush != 4) return Z_OK;
    if (s->noheader) return Z_STREAM_END;

    // The zlib trailer (adler32).
    putShortMSB(s, 0, strm->adler >> 16);
    putShortMSB(s, 0, strm->adler & 0xffff);
    flush_pending(strm);
    s->noheader = -1;  // write the trailer only once
    return s->pending != 0 ? Z_OK : Z_STREAM_END;
}

int32_t XB_CDECL deflateEnd(ZStream* strm) {
    if (strm == nullptr || strm->state == nullptr) return Z_STREAM_ERROR;
    const int32_t status = static_cast<DeflateState*>(strm->state)->status;
    if (status != INIT_STATE && status != BUSY_STATE && status != FINISH_STATE) return Z_STREAM_ERROR;
    if (static_cast<DeflateState*>(strm->state)->pending_buf)
        zfree(strm, static_cast<DeflateState*>(strm->state)->pending_buf);
    if (static_cast<DeflateState*>(strm->state)->head) zfree(strm, static_cast<DeflateState*>(strm->state)->head);
    if (static_cast<DeflateState*>(strm->state)->prev) zfree(strm, static_cast<DeflateState*>(strm->state)->prev);
    if (static_cast<DeflateState*>(strm->state)->window)
        zfree(strm, static_cast<DeflateState*>(strm->state)->window);
    zfree(strm, strm->state);
    strm->state = nullptr;
    return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK;
}

void XB_REGS2 XB_CDECL lm_init(uint32_t /*eax_unused*/, DeflateState* s) {
    s->window_size = 2 * s->w_size;
    clear_hash(s);
    // configuration_table[level]: good_length, max_lazy, nice_length, max_chain, func.
    const uint32_t config = 0x007bb290u + static_cast<uint32_t>(s->level) * 12;
    s->max_lazy_match = title_global<const uint16_t>(config + 2);
    s->good_match = title_global<const uint16_t>(config + 0);
    s->nice_match = title_global<const uint16_t>(config + 4);
    s->max_chain_length = title_global<const uint16_t>(config + 6);
    s->strstart = 0;
    s->block_start = 0;
    s->lookahead = 0;
    s->match_length = s->prev_length = MIN_MATCH - 1;
    s->match_available = 0;
    s->ins_h = 0;
}

PLATFORM_ASM_ENTRY(longest_match_entry,
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(longest_match) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL longest_match(DeflateState* s, uint32_t cur_match) {
    uint32_t chain_length = s->max_chain_length;
    uint8_t* scan = s->window + s->strstart;
    uint8_t* match;
    int32_t len;
    int32_t best_len = static_cast<int32_t>(s->prev_length);
    int32_t nice_match = s->nice_match;
    const uint32_t limit = s->strstart > max_dist(s) ? s->strstart - max_dist(s) : 0;
    const uint16_t* prev = s->prev;
    const uint32_t wmask = s->w_mask;
    uint8_t* strend = s->window + s->strstart + MAX_MATCH;
    uint8_t scan_end1 = scan[best_len - 1];
    uint8_t scan_end = scan[best_len];

    if (s->prev_length >= s->good_match) chain_length >>= 2;
    if (static_cast<uint32_t>(nice_match) > s->lookahead) nice_match = static_cast<int32_t>(s->lookahead);

    do {
        match = s->window + cur_match;
        if (match[best_len] != scan_end || match[best_len - 1] != scan_end1 || *match != *scan ||
            *++match != scan[1])
            continue;
        scan += 2, match++;
        do {
        } while (*++scan == *++match && *++scan == *++match && *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match && *++scan == *++match && *++scan == *++match &&
                 scan < strend);
        len = static_cast<int32_t>(MAX_MATCH) - static_cast<int32_t>(strend - scan);
        scan = strend - MAX_MATCH;
        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match) break;
            scan_end1 = scan[best_len - 1];
            scan_end = scan[best_len];
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length != 0);

    if (static_cast<uint32_t>(best_len) <= s->lookahead) return static_cast<uint32_t>(best_len);
    return s->lookahead;
}

PLATFORM_ASM_ENTRY(fill_window_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(fill_window) "addl $4, %esp\n"
                   "ret")

void XB_CDECL fill_window(DeflateState* s) {
    uint32_t n, m;
    uint16_t* p;
    uint32_t more;
    const uint32_t wsize = s->w_size;
    do {
        more = s->window_size - s->lookahead - s->strstart;
        if (more == 0 && s->strstart == 0 && s->lookahead == 0) {
            more = wsize;
        } else if (more == 0xffffffffu) {
            more--;
        } else if (s->strstart >= wsize + max_dist(s)) {
            platform_rep_movs(s->window, s->window + wsize, wsize);
            s->match_start -= wsize;
            s->strstart -= wsize;
            s->block_start -= static_cast<int32_t>(wsize);
            n = s->hash_size;
            p = &s->head[n];
            do {
                m = *--p;
                *p = static_cast<uint16_t>(m >= wsize ? m - wsize : 0);
            } while (--n);
            n = wsize;
            p = &s->prev[n];
            do {
                m = *--p;
                *p = static_cast<uint16_t>(m >= wsize ? m - wsize : 0);
            } while (--n);
            more += wsize;
        }
        if (s->strm->avail_in == 0) return;

        // read_buf
        ZStream* strm = s->strm;
        uint8_t* buf = s->window + s->strstart + s->lookahead;
        uint32_t len = strm->avail_in;
        if (len > more) len = more;
        if (len != 0) {
            strm->avail_in -= len;
            if (!static_cast<DeflateState*>(strm->state)->noheader) strm->adler = adler32(strm->adler, strm->next_in, len);
            platform_rep_movs(buf, strm->next_in, len);
            strm->next_in += len;
            strm->total_in += len;
        }
        n = len;

        s->lookahead += n;
        if (s->lookahead >= MIN_MATCH) {
            s->ins_h = s->window[s->strstart];
            update_hash(s, s->ins_h, s->window[s->strstart + 1]);
        }
    } while (s->lookahead < MIN_LOOKAHEAD && s->strm->avail_in != 0);
}

int32_t XB_CDECL deflate_stored(DeflateState* s, int32_t flush) {
    uint32_t max_block_size = 0xffff;
    if (max_block_size > s->pending_buf_size - 5) max_block_size = s->pending_buf_size - 5;
    for (;;) {
        if (s->lookahead <= 1) {
            fill_window(s);
            if (s->lookahead == 0 && flush == 0) return need_more;
            if (s->lookahead == 0) break;  // flush the current block
        }
        s->strstart += s->lookahead;
        s->lookahead = 0;
        const uint32_t max_start = static_cast<uint32_t>(s->block_start) + max_block_size;
        if (s->strstart == 0 || s->strstart >= max_start) {
            s->lookahead = s->strstart - max_start;
            s->strstart = max_start;
            flush_block_only(s, 0);
            if (s->strm->avail_out == 0) return need_more;
        }
        if (s->strstart - static_cast<uint32_t>(s->block_start) >= max_dist(s)) {
            flush_block_only(s, 0);
            if (s->strm->avail_out == 0) return need_more;
        }
    }
    flush_block_only(s, flush == 4);
    if (s->strm->avail_out == 0) return flush == 4 ? finish_started : need_more;
    return flush == 4 ? finish_done : block_done;
}

int32_t XB_CDECL deflate_fast(DeflateState* s, int32_t flush) {
    uint32_t hash_head = 0;
    bool bflush;
    for (;;) {
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == 0) return need_more;
            if (s->lookahead == 0) break;
        }
        if (s->lookahead >= MIN_MATCH) hash_head = insert_string(s, s->strstart);
        if (hash_head != 0 && s->strstart - hash_head <= max_dist(s)) {
            if (s->strategy != 2) s->match_length = longest_match(s, hash_head);  // Z_HUFFMAN_ONLY
        }
        if (s->match_length >= MIN_MATCH) {
            bflush = tally_dist(s, s->strstart - s->match_start, s->match_length - MIN_MATCH);
            s->lookahead -= s->match_length;
            if (s->match_length <= s->max_lazy_match && s->lookahead >= MIN_MATCH) {
                s->match_length--;
                do {
                    s->strstart++;
                    hash_head = insert_string(s, s->strstart);
                } while (--s->match_length != 0);
                s->strstart++;
            } else {
                s->strstart += s->match_length;
                s->match_length = 0;
                s->ins_h = s->window[s->strstart];
                update_hash(s, s->ins_h, s->window[s->strstart + 1]);
            }
        } else {
            bflush = tally_lit(s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush) {
            flush_block_only(s, 0);
            if (s->strm->avail_out == 0) return need_more;
        }
    }
    flush_block_only(s, flush == 4);
    if (s->strm->avail_out == 0) return flush == 4 ? finish_started : need_more;
    return flush == 4 ? finish_done : block_done;
}

int32_t XB_CDECL deflate_slow(DeflateState* s, int32_t flush) {
    uint32_t hash_head = 0;
    bool bflush;
    for (;;) {
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == 0) return need_more;
            if (s->lookahead == 0) break;
        }
        if (s->lookahead >= MIN_MATCH) hash_head = insert_string(s, s->strstart);

        s->prev_length = s->match_length, s->prev_match = s->match_start;
        s->match_length = MIN_MATCH - 1;
        if (hash_head != 0 && s->prev_length < s->max_lazy_match && s->strstart - hash_head <= max_dist(s)) {
            if (s->strategy != 2) s->match_length = longest_match(s, hash_head);
            if (s->match_length <= 5 &&
                (s->strategy == 1 || (s->match_length == MIN_MATCH && s->strstart - s->match_start > TOO_FAR)))
                s->match_length = MIN_MATCH - 1;  // Z_FILTERED, or a short far match
        }
        if (s->prev_length >= MIN_MATCH && s->match_length <= s->prev_length) {
            const uint32_t max_insert = s->strstart + s->lookahead - MIN_MATCH;
            bflush = tally_dist(s, s->strstart - 1 - s->prev_match, s->prev_length - MIN_MATCH);
            s->lookahead -= s->prev_length - 1;
            s->prev_length -= 2;
            do {
                if (++s->strstart <= max_insert) hash_head = insert_string(s, s->strstart);
            } while (--s->prev_length != 0);
            s->match_available = 0;
            s->match_length = MIN_MATCH - 1;
            s->strstart++;
            if (bflush) {
                flush_block_only(s, 0);
                if (s->strm->avail_out == 0) return need_more;
            }
        } else if (s->match_available) {
            bflush = tally_lit(s, s->window[s->strstart - 1]);
            if (bflush) flush_block_only(s, 0);
            s->strstart++;
            s->lookahead--;
            if (s->strm->avail_out == 0) return need_more;
        } else {
            s->match_available = 1;
            s->strstart++;
            s->lookahead--;
        }
    }
    if (s->match_available) {
        tally_lit(s, s->window[s->strstart - 1]);
        s->match_available = 0;
    }
    flush_block_only(s, flush == 4);
    if (s->strm->avail_out == 0) return flush == 4 ? finish_started : need_more;
    return flush == 4 ? finish_done : block_done;
}

int32_t XB_CDECL deflateReset(ZStream* strm) {
    if (strm == nullptr || strm->state == nullptr || strm->zalloc == 0 || strm->zfree == 0) return Z_STREAM_ERROR;
    DeflateState* s = static_cast<DeflateState*>(strm->state);
    strm->total_in = strm->total_out = 0;
    strm->msg = nullptr;
    strm->data_type = 2;  // Z_UNKNOWN
    s->pending = 0;
    s->pending_out = s->pending_buf;
    if (s->noheader < 0) s->noheader = 0;  // was set to -1 by deflate(..., Z_FINISH)
    s->status = s->noheader ? BUSY_STATE : INIT_STATE;
    strm->adler = 1;
    s->last_flush = 0;
    _tr_init(s);
    lm_init(0, s);
    return Z_OK;
}

int32_t XB_CDECL deflateInit2_(ZStream* strm, int32_t level_in, int32_t method, int32_t window_bits_in,
                               int32_t mem_level, int32_t strategy, const char* version, int32_t stream_size) {
    int32_t level = level_in;
    int32_t window_bits = window_bits_in;
    int32_t noheader = 0;
    const char* my_version = title_global<const char*>(0x008a0c10);  // ZLIB_VERSION
    if (version == nullptr || version[0] != my_version[0] || stream_size != static_cast<int32_t>(sizeof(ZStream)))
        return Z_VERSION_ERROR;
    if (strm == nullptr) return Z_STREAM_ERROR;
    strm->msg = nullptr;
    if (strm->zalloc == 0) {
        strm->zalloc = kZcalloc;
        strm->opaque = 0;
    }
    if (strm->zfree == 0) strm->zfree = kZcfree;
    if (level == -1) level = 6;  // Z_DEFAULT_COMPRESSION
    if (window_bits < 0) {      // suppress the zlib header
        noheader = 1;
        window_bits = -window_bits;
    }
    if (mem_level < 1 || mem_level > 9 || method != 8 || window_bits < 9 || window_bits > 15 || level < 0 ||
        level > 9 || strategy < 0 || strategy > 2)
        return Z_STREAM_ERROR;

    DeflateState* s = static_cast<DeflateState*>(zalloc(strm, 1, sizeof(DeflateState)));
    if (s == nullptr) return Z_MEM_ERROR;
    strm->state = s;
    s->strm = strm;
    s->noheader = noheader;
    s->w_bits = static_cast<uint32_t>(window_bits);
    s->w_size = 1u << s->w_bits;
    s->w_mask = s->w_size - 1;
    s->hash_bits = static_cast<uint32_t>(mem_level) + 7;
    s->hash_size = 1u << s->hash_bits;
    s->hash_mask = s->hash_size - 1;
    s->hash_shift = (s->hash_bits + MIN_MATCH - 1) / MIN_MATCH;
    s->window = static_cast<uint8_t*>(zalloc(strm, s->w_size, 2));
    s->prev = static_cast<uint16_t*>(zalloc(strm, s->w_size, 2));
    s->head = static_cast<uint16_t*>(zalloc(strm, s->hash_size, 2));
    s->lit_bufsize = 1u << (mem_level + 6);
    uint16_t* overlay = static_cast<uint16_t*>(zalloc(strm, s->lit_bufsize, 4));
    s->pending_buf = reinterpret_cast<uint8_t*>(overlay);
    s->pending_buf_size = s->lit_bufsize * 4;
    if (s->window == nullptr || s->prev == nullptr || s->head == nullptr || s->pending_buf == nullptr) {
        strm->msg = err_msg(Z_MEM_ERROR);
        deflateEnd(strm);
        return Z_MEM_ERROR;
    }
    s->d_buf = overlay + s->lit_bufsize / sizeof(uint16_t);
    s->l_buf = s->pending_buf + 3 * s->lit_bufsize;
    s->level = level;
    s->strategy = strategy;
    s->method = static_cast<uint8_t>(method);
    return deflateReset(strm);
}

int32_t XB_CDECL deflateInit_(ZStream* strm, int32_t level, const char* version, int32_t stream_size) {
    return deflateInit2_(strm, level, 8, 15, 8, 0, version, stream_size);  // Z_DEFLATED, MAX_WBITS, DEF_MEM_LEVEL
}

// ---- trees.c -------------------------------------------------------------------------

namespace {

constexpr int32_t L_CODES = 286;
constexpr int32_t D_CODES = 30;
constexpr int32_t BL_CODES = 19;
constexpr int32_t HEAP_SIZE = 2 * L_CODES + 1;
constexpr int32_t MAX_BITS = 15;
constexpr int32_t END_BLOCK = 256;
constexpr int32_t REP_3_6 = 16, REPZ_3_10 = 17, REPZ_11_138 = 18;
constexpr int32_t SMALLEST = 1;

const CtData* static_ltree() { return &title_global<const CtData>(0x007bb588); }
const CtData* static_dtree() { return &title_global<const CtData>(0x007bba08); }
uint8_t bl_order(int32_t i) { return title_global<const uint8_t>(0x007bb574u + static_cast<uint32_t>(i)); }
int32_t extra_lbits(uint32_t i) { return title_global<const int32_t>(0x007bb438u + i * 4); }
int32_t extra_dbits(uint32_t i) { return title_global<const int32_t>(0x007bb4b0u + i * 4); }
int32_t base_length(uint32_t i) { return title_global<const int32_t>(0x007bbd80u + i * 4); }
int32_t base_dist(uint32_t i) { return title_global<const int32_t>(0x007bbdf8u + i * 4); }

void put_short(DeflateState* s, uint32_t w) {
    put_byte(s, static_cast<uint8_t>(w & 0xff));
    put_byte(s, static_cast<uint8_t>(static_cast<uint16_t>(w) >> 8));
}

void send_bits(DeflateState* s, int32_t value, int32_t length) {
    if (s->bi_valid > 16 - length) {
        const int32_t val = value;
        s->bi_buf = static_cast<uint16_t>(s->bi_buf | (val << s->bi_valid));
        put_short(s, s->bi_buf);
        s->bi_buf = static_cast<uint16_t>(static_cast<uint16_t>(val) >> (16 - s->bi_valid));
        s->bi_valid += length - 16;
    } else {
        s->bi_buf = static_cast<uint16_t>(s->bi_buf | (value << s->bi_valid));
        s->bi_valid += length;
    }
}

void send_code(DeflateState* s, int32_t c, const CtData* tree) { send_bits(s, tree[c].fc, tree[c].dl); }

bool smaller(const CtData* tree, int32_t n, int32_t m, const uint8_t* depth) {
    return tree[n].fc < tree[m].fc || (tree[n].fc == tree[m].fc && depth[n] <= depth[m]);
}

}  // namespace

void XB_REGS2 XB_CDECL init_block(uint32_t /*eax_unused*/, DeflateState* s) {
    for (int32_t n = 0; n < L_CODES; n++) s->dyn_ltree[n].fc = 0;
    for (int32_t n = 0; n < D_CODES; n++) s->dyn_dtree[n].fc = 0;
    for (int32_t n = 0; n < BL_CODES; n++) s->bl_tree[n].fc = 0;
    s->dyn_ltree[END_BLOCK].fc = 1;
    s->opt_len = s->static_len = 0;
    s->last_lit = s->matches = 0;
}

void XB_CDECL _tr_init(DeflateState* s) {
    s->l_desc.dyn_tree = s->dyn_ltree;
    s->l_desc.stat_desc = &title_global<const StaticTreeDesc>(0x008a0c3c);  // static_l_desc
    s->d_desc.dyn_tree = s->dyn_dtree;
    s->d_desc.stat_desc = &title_global<const StaticTreeDesc>(0x008a0c50);  // static_d_desc
    s->bl_desc.dyn_tree = s->bl_tree;
    s->bl_desc.stat_desc = &title_global<const StaticTreeDesc>(0x008a0c64);  // static_bl_desc
    s->bi_buf = 0;
    s->bi_valid = 0;
    s->last_eob_len = 8;  // enough lookahead for inflate
    init_block(0, s);
}

PLATFORM_ASM_ENTRY(pqdownheap_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(pqdownheap) "addl $12, %esp\n"
                   "ret")

DeflateState* XB_CDECL pqdownheap(DeflateState* s, CtData* tree, int32_t k_in) {
    int32_t k = k_in;
    const int32_t v = s->heap[k];
    int32_t j = k << 1;  // left son of k
    while (j <= s->heap_len) {
        if (j < s->heap_len && smaller(tree, s->heap[j + 1], s->heap[j], s->depth)) j++;
        if (smaller(tree, v, s->heap[j], s->depth)) break;
        s->heap[k] = s->heap[j];
        k = j;
        j <<= 1;
    }
    s->heap[k] = v;
    return s;
}

DeflateState* XB_REGS3 XB_CDECL gen_bitlen(DeflateState* s, uint32_t /*edx_unused*/, TreeDesc* desc) {
    CtData* tree = desc->dyn_tree;
    const int32_t max_code = desc->max_code;
    const CtData* stree = desc->stat_desc->static_tree;
    const int32_t* extra = desc->stat_desc->extra_bits;
    const int32_t base = desc->stat_desc->extra_base;
    const int32_t max_length = desc->stat_desc->max_length;
    int32_t h, n, m, bits, xbits;
    uint16_t f;
    int32_t overflow = 0;

    for (bits = 0; bits <= MAX_BITS; bits++) s->bl_count[bits] = 0;
    tree[s->heap[s->heap_max]].dl = 0;  // root of the heap

    for (h = s->heap_max + 1; h < HEAP_SIZE; h++) {
        n = s->heap[h];
        bits = tree[tree[n].dl].dl + 1;
        if (bits > max_length) bits = max_length, overflow++;
        tree[n].dl = static_cast<uint16_t>(bits);
        if (n > max_code) continue;  // not a leaf node
        s->bl_count[bits]++;
        xbits = 0;
        if (n >= base) xbits = extra[n - base];
        f = tree[n].fc;
        s->opt_len += static_cast<uint32_t>(f) * static_cast<uint32_t>(bits + xbits);
        if (stree) s->static_len += static_cast<uint32_t>(f) * static_cast<uint32_t>(stree[n].dl + xbits);
    }
    if (overflow == 0) return s;

    // Find the first bit length which could increase.
    do {
        bits = max_length - 1;
        while (s->bl_count[bits] == 0) bits--;
        s->bl_count[bits]--;
        s->bl_count[bits + 1] += 2;
        s->bl_count[max_length]--;
        overflow -= 2;
    } while (overflow > 0);

    // Recompute all bit lengths, scanning in increasing frequency.
    for (bits = max_length; bits != 0; bits--) {
        n = s->bl_count[bits];
        while (n != 0) {
            m = s->heap[--h];
            if (m > max_code) continue;
            if (tree[m].dl != static_cast<uint32_t>(bits)) {
                s->opt_len += static_cast<uint32_t>((bits - static_cast<int32_t>(tree[m].dl)) *
                                                    static_cast<int32_t>(tree[m].fc));
                tree[m].dl = static_cast<uint16_t>(bits);
            }
            n--;
        }
    }
    return s;
}

PLATFORM_ASM_ENTRY(gen_codes_entry,
                   "pushl %edx\n"
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(gen_codes) "addl $12, %esp\n"
                   "ret")

void XB_CDECL gen_codes(CtData* tree, int32_t max_code, const uint16_t* bl_count) {
    uint16_t next_code[MAX_BITS + 1];
    uint16_t code = 0;
    for (int32_t bits = 1; bits <= MAX_BITS; bits++)
        next_code[bits] = code = static_cast<uint16_t>((code + bl_count[bits - 1]) << 1);
    for (int32_t n = 0; n <= max_code; n++) {
        int32_t len = tree[n].dl;
        if (len == 0) continue;
        // bi_reverse(next_code[len]++, len)
        uint32_t c = next_code[len]++;
        uint32_t res = 0;
        do {
            res |= c & 1;
            c >>= 1, res <<= 1;
        } while (--len > 0);
        tree[n].fc = static_cast<uint16_t>(res >> 1);
    }
}

PLATFORM_ASM_ENTRY(build_tree_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(build_tree) "addl $8, %esp\n"
                   "ret")

void XB_CDECL build_tree(DeflateState* s, TreeDesc* desc) {
    CtData* tree = desc->dyn_tree;
    const CtData* stree = desc->stat_desc->static_tree;
    const int32_t elems = desc->stat_desc->elems;
    int32_t n, m;
    int32_t max_code = -1;
    int32_t node;

    s->heap_len = 0, s->heap_max = HEAP_SIZE;
    for (n = 0; n < elems; n++) {
        if (tree[n].fc != 0) {
            s->heap[++(s->heap_len)] = max_code = n;
            s->depth[n] = 0;
        } else {
            tree[n].dl = 0;
        }
    }
    // Force at least two codes of non zero frequency.
    while (s->heap_len < 2) {
        node = s->heap[++(s->heap_len)] = (max_code < 2 ? ++max_code : 0);
        tree[node].fc = 1;
        s->depth[node] = 0;
        s->opt_len--;
        if (stree) s->static_len -= stree[node].dl;
    }
    desc->max_code = max_code;

    for (n = s->heap_len / 2; n >= 1; n--) pqdownheap(s, tree, n);

    node = elems;
    do {
        // pqremove
        n = s->heap[SMALLEST];
        s->heap[SMALLEST] = s->heap[s->heap_len--];
        pqdownheap(s, tree, SMALLEST);
        m = s->heap[SMALLEST];
        s->heap[--(s->heap_max)] = n;
        s->heap[--(s->heap_max)] = m;
        tree[node].fc = static_cast<uint16_t>(tree[n].fc + tree[m].fc);
        s->depth[node] = static_cast<uint8_t>((s->depth[n] >= s->depth[m] ? s->depth[n] : s->depth[m]) + 1);
        tree[n].dl = tree[m].dl = static_cast<uint16_t>(node);
        s->heap[SMALLEST] = node++;
        pqdownheap(s, tree, SMALLEST);
    } while (s->heap_len >= 2);
    s->heap[--(s->heap_max)] = s->heap[SMALLEST];

    gen_bitlen(s, 0, desc);
    gen_codes(tree, max_code, s->bl_count);
}

void XB_REGS3 XB_CDECL scan_tree(CtData* tree, uint32_t /*edx_unused*/, int32_t max_code, DeflateState* s) {
    int32_t prevlen = -1;
    int32_t curlen;
    int32_t nextlen = tree[0].dl;
    int32_t count = 0;
    int32_t max_count = 7;
    int32_t min_count = 4;
    if (nextlen == 0) max_count = 138, min_count = 3;
    tree[max_code + 1].dl = 0xffff;  // guard
    for (int32_t n = 0; n <= max_code; n++) {
        curlen = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            s->bl_tree[curlen].fc = static_cast<uint16_t>(s->bl_tree[curlen].fc + count);
        } else if (curlen != 0) {
            if (curlen != prevlen) s->bl_tree[curlen].fc++;
            s->bl_tree[REP_3_6].fc++;
        } else if (count <= 10) {
            s->bl_tree[REPZ_3_10].fc++;
        } else {
            s->bl_tree[REPZ_11_138].fc++;
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) {
            max_count = 138, min_count = 3;
        } else if (curlen == nextlen) {
            max_count = 6, min_count = 3;
        } else {
            max_count = 7, min_count = 4;
        }
    }
}

DeflateState* XB_REGS3 XB_CDECL send_tree(DeflateState* s, uint32_t /*edx_unused*/, CtData* tree,
                                          int32_t max_code) {
    int32_t prevlen = -1;
    int32_t curlen;
    int32_t nextlen = tree[0].dl;
    int32_t count = 0;
    int32_t max_count = 7;
    int32_t min_count = 4;
    if (nextlen == 0) max_count = 138, min_count = 3;
    for (int32_t n = 0; n <= max_code; n++) {
        curlen = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            do {
                send_code(s, curlen, s->bl_tree);
            } while (--count != 0);
        } else if (curlen != 0) {
            if (curlen != prevlen) {
                send_code(s, curlen, s->bl_tree);
                count--;
            }
            send_code(s, REP_3_6, s->bl_tree);
            send_bits(s, count - 3, 2);
        } else if (count <= 10) {
            send_code(s, REPZ_3_10, s->bl_tree);
            send_bits(s, count - 3, 3);
        } else {
            send_code(s, REPZ_11_138, s->bl_tree);
            send_bits(s, count - 11, 7);
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) {
            max_count = 138, min_count = 3;
        } else if (curlen == nextlen) {
            max_count = 6, min_count = 3;
        } else {
            max_count = 7, min_count = 4;
        }
    }
    return s;
}

int32_t XB_REGS1 XB_CDECL build_bl_tree(DeflateState* s) {
    scan_tree(s->dyn_ltree, 0, s->l_desc.max_code, s);
    scan_tree(s->dyn_dtree, 0, s->d_desc.max_code, s);
    build_tree(s, &s->bl_desc);
    int32_t max_blindex;
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--)
        if (s->bl_tree[bl_order(max_blindex)].dl != 0) break;
    s->opt_len += static_cast<uint32_t>(3 * (max_blindex + 1) + 5 + 5 + 4);
    return max_blindex;
}

DeflateState* XB_REGS1 XB_CDECL send_all_trees(DeflateState* s, int32_t lcodes, int32_t dcodes, int32_t blcodes) {
    send_bits(s, lcodes - 257, 5);
    send_bits(s, dcodes - 1, 5);
    send_bits(s, blcodes - 4, 4);
    for (int32_t rank = 0; rank < blcodes; rank++) send_bits(s, s->bl_tree[bl_order(rank)].dl, 3);
    send_tree(s, 0, s->dyn_ltree, lcodes - 1);
    send_tree(s, 0, s->dyn_dtree, dcodes - 1);
    return s;
}

DeflateState* XB_CDECL _tr_stored_block(DeflateState* s, const uint8_t* buf, uint32_t stored_len, int32_t eof) {
    send_bits(s, (0 << 1) + eof, 3);  // STORED_BLOCK
    return copy_block(s, buf, stored_len, 1);
}

void XB_CDECL _tr_align(DeflateState* s) {
    send_bits(s, 1 << 1, 3);  // STATIC_TREES
    send_code(s, END_BLOCK, static_ltree());
    bi_flush(s);
    // Of the 10 bits for the empty block, at least 9 must be sent before the next block's EOB.
    if (1 + s->last_eob_len + 10 - s->bi_valid < 9) {
        send_bits(s, 1 << 1, 3);
        send_code(s, END_BLOCK, static_ltree());
        bi_flush(s);
    }
    s->last_eob_len = 7;
}

void XB_CDECL _tr_flush_block(DeflateState* s, const uint8_t* buf, uint32_t stored_len, int32_t eof) {
    uint32_t opt_lenb, static_lenb;
    int32_t max_blindex = 0;
    if (s->level > 0) {
        if (s->data_type == 2) set_data_type(s);  // Z_UNKNOWN
        build_tree(s, &s->l_desc);
        build_tree(s, &s->d_desc);
        max_blindex = build_bl_tree(s);
        opt_lenb = (s->opt_len + 3 + 7) >> 3;
        static_lenb = (s->static_len + 3 + 7) >> 3;
        if (static_lenb <= opt_lenb) opt_lenb = static_lenb;
    } else {
        opt_lenb = static_lenb = stored_len + 5;  // force a stored block
    }
    if (stored_len + 4 <= opt_lenb && buf != nullptr) {
        _tr_stored_block(s, buf, stored_len, eof);
    } else if (static_lenb == opt_lenb) {
        send_bits(s, (1 << 1) + eof, 3);  // STATIC_TREES
        compress_block(s, static_ltree(), static_dtree());
    } else {
        send_bits(s, (2 << 1) + eof, 3);  // DYN_TREES
        send_all_trees(s, s->l_desc.max_code + 1, s->d_desc.max_code + 1, max_blindex + 1);
        compress_block(s, s->dyn_ltree, s->dyn_dtree);
    }
    init_block(0, s);
    if (eof) bi_windup(s);
}

DeflateState* XB_REGS1 XB_CDECL compress_block(DeflateState* s, const CtData* ltree, const CtData* dtree) {
    uint32_t dist;
    int32_t lc;
    uint32_t lx = 0;
    uint32_t code;
    int32_t extra;
    if (s->last_lit != 0) do {
            dist = s->d_buf[lx];
            lc = s->l_buf[lx++];
            if (dist == 0) {
                send_code(s, lc, ltree);  // literal byte
            } else {
                // A match: the length code, its extra bits, then the distance.
                code = length_code(static_cast<uint32_t>(lc));
                send_code(s, static_cast<int32_t>(code) + 256 + 1, ltree);
                extra = extra_lbits(code);
                if (extra != 0) {
                    lc -= base_length(code);
                    send_bits(s, lc, extra);
                }
                dist--;
                code = d_code(dist);
                send_code(s, static_cast<int32_t>(code), dtree);
                extra = extra_dbits(code);
                if (extra != 0) {
                    dist -= static_cast<uint32_t>(base_dist(code));
                    send_bits(s, static_cast<int32_t>(dist), extra);
                }
            }
        } while (lx < s->last_lit);
    send_code(s, END_BLOCK, ltree);
    s->last_eob_len = ltree[END_BLOCK].dl;
    return s;
}

void XB_FASTCALL set_data_type(DeflateState* s) {
    int32_t n = 0;
    uint32_t ascii_freq = 0;
    uint32_t bin_freq = 0;
    while (n < 7) bin_freq += s->dyn_ltree[n++].fc;
    while (n < 128) ascii_freq += s->dyn_ltree[n++].fc;
    while (n < 256) bin_freq += s->dyn_ltree[n++].fc;
    s->data_type = static_cast<uint8_t>(bin_freq > (ascii_freq >> 2) ? 0 : 1);  // Z_BINARY : Z_ASCII
}

DeflateState* XB_REGS1 XB_CDECL bi_flush(DeflateState* s) {
    if (s->bi_valid == 16) {
        put_short(s, s->bi_buf);
        s->bi_buf = 0;
        s->bi_valid = 0;
    } else if (s->bi_valid >= 8) {
        put_byte(s, static_cast<uint8_t>(s->bi_buf));
        s->bi_buf >>= 8;
        s->bi_valid -= 8;
    }
    return s;
}

DeflateState* XB_REGS1 XB_CDECL bi_windup(DeflateState* s) {
    if (s->bi_valid > 8) {
        put_short(s, s->bi_buf);
    } else if (s->bi_valid > 0) {
        put_byte(s, static_cast<uint8_t>(s->bi_buf));
    }
    s->bi_buf = 0;
    s->bi_valid = 0;
    return s;
}

DeflateState* XB_REGS3 XB_CDECL copy_block(DeflateState* s, const uint8_t* buf, uint32_t len, int32_t header) {
    bi_windup(s);
    s->last_eob_len = 8;
    if (header) {
        put_short(s, static_cast<uint16_t>(len));
        put_short(s, static_cast<uint16_t>(~len));
    }
    while (len--) put_byte(s, *buf++);
    return s;
}

}  // namespace game
