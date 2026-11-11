#pragma once

// SHA-1 (FIPS 180-1), header-only: the kernel's XcSHA* exports and our own save-game signatures share it. The
// context fits the title's A_SHA_CTX (116 bytes in XAPI's signature context), so either side can work in place.

#include <cstdint>
#include <cstring>

namespace platform::sha1 {

struct Context {
    uint32_t h[5];
    uint32_t count_low, count_high;  // message length in bytes
    uint8_t block[64];
};
static_assert(sizeof(Context) <= 116);

inline uint32_t rotl(uint32_t v, int n) { return v << n | v >> (32 - n); }

inline void block(Context& c, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t{p[i * 4]} << 24 | uint32_t{p[i * 4 + 1]} << 16 | uint32_t{p[i * 4 + 2]} << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) f = (b & cc) | (~b & d), k = 0x5A827999;
        else if (i < 40) f = b ^ cc ^ d, k = 0x6ED9EBA1;
        else if (i < 60) f = (b & cc) | (b & d) | (cc & d), k = 0x8F1BBCDC;
        else f = b ^ cc ^ d, k = 0xCA62C1D6;
        const uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d, d = cc, cc = rotl(b, 30), b = a, a = t;
    }
    c.h[0] += a, c.h[1] += b, c.h[2] += cc, c.h[3] += d, c.h[4] += e;
}

inline void init(Context& c) { c = Context{{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, 0, 0, {}}; }

inline void update(Context& c, const uint8_t* data, uint32_t length) {
    uint32_t used = c.count_low % 64;
    const uint32_t old = c.count_low;
    c.count_low += length;
    if (c.count_low < old) ++c.count_high;
    while (length) {
        const uint32_t take = length < 64 - used ? length : 64 - used;
        std::memcpy(c.block + used, data, take);
        used += take, data += take, length -= take;
        if (used == 64) block(c, c.block), used = 0;
    }
}

inline void final(Context& c, uint8_t* digest) {
    const uint64_t bits = (uint64_t{c.count_high} << 32 | c.count_low) * 8;
    static const uint8_t kPad[64] = {0x80};
    const uint32_t used = c.count_low % 64;
    update(c, kPad, used < 56 ? 56 - used : 120 - used);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    update(c, length, 8);
    for (int i = 0; i < 20; ++i) digest[i] = static_cast<uint8_t>(c.h[i / 4] >> (24 - (i % 4) * 8));
}

}  // namespace platform::sha1
