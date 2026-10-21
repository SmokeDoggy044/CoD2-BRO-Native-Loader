#include "unzip.h"

#include "title_data.h"

namespace game {

namespace {

// inflate_mask: the low n bits.
const uint32_t kInflateMask[17] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff,
};

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
    const uint32_t ml = kInflateMask[bl];
    const uint32_t md = kInflateMask[bd];

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
                uint32_t c = t->base + (b & kInflateMask[e]);
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
                        const uint32_t d = t->base + (b & kInflateMask[e]);
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
                        t += b & kInflateMask[e];
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
                t += b & kInflateMask[e];
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

int32_t XB_REGS2 XB_CDECL strcmpcasenosensitive_internal(const char* name1, const char* name2) {
    // minizip's, with the characters compared as signed and the result from name2's
    // side: -1 when name2 sorts first.
    for (;;) {
        int8_t c2 = static_cast<int8_t>(*name2++);
        int8_t c1 = static_cast<int8_t>(*name1++);
        if (c2 >= 'a' && c2 <= 'z') c2 = static_cast<int8_t>(c2 - 0x20);
        if (c1 >= 'a' && c1 <= 'z') c1 = static_cast<int8_t>(c1 - 0x20);
        if (c2 == 0) return c1 != 0 ? -1 : 0;
        if (c1 == 0) return 1;
        if (c2 < c1) return -1;
        if (c2 > c1) return 1;
    }
}

}  // namespace game
