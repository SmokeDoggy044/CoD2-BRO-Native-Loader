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
                   // entry - 0xd8, upper half) and r.base before the first code (entry - 0xd4).
                   "pushl -0xd4(%esp)\n"
                   "pushl -0xd4(%esp)\n"  // entry - 0xd8 after the first push
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(huft_build) "addl $48, %esp\n"
                   "ret")

int32_t XB_CDECL huft_build(uint32_t* b, uint32_t* m, uint32_t n_in, uint32_t s, const uint32_t* d,
                            const uint32_t* e, InflateHuft** t, InflateHuft* hp, uint32_t* hn, uint32_t* v,
                            uint32_t stale_pad, uint32_t stale_base) {
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

    // r's padding and its base before the first code are stack words the original never sets.
    r.exop = 0;
    r.bits = 0;
    r.pad[0] = static_cast<uint8_t>(stale_pad >> 16);
    r.pad[1] = static_cast<uint8_t>(stale_pad >> 24);
    r.base = stale_base;

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
        return Z_OK;
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
        if ((y -= static_cast<int32_t>(c[j])) < 0) return Z_DATA_ERROR;
    if ((y -= static_cast<int32_t>(c[i])) < 0) return Z_DATA_ERROR;
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
                if (*hn + z > MANY) return Z_DATA_ERROR;
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
    return y != 0 && g != 1 ? Z_BUF_ERROR : Z_OK;
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

}  // namespace game
