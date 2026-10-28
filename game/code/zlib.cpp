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
int32_t inflate(ZStream* z, int32_t flush) {
    return title_function<int32_t XB_CDECL(ZStream*, int32_t)>(0x00058020)(z, flush);
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

}  // namespace game
