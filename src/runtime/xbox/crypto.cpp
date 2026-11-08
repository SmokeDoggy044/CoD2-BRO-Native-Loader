// Kernel crypto services (Xc*): SHA-1, HMAC-SHA1 and RC4. Titles use them for save
// game signatures (XCalculateSignature*) and content keys. The DES, public-key and
// big-number services are for Xbox Live and stay unimplemented until a title needs
// them offline.

#include <cstring>

#include "compiler.h"
#include "kernel_internal.h"
#include "types.h"

namespace xbox {

namespace {

// --- SHA-1 (FIPS 180-1) ------------------------------------------------------------------

// Lives in the title's A_SHA_CTX (116 bytes in XAPI's signature context).
struct ShaContext {
    uint32_t h[5];
    uint32_t count_low, count_high;  // message length in bytes
    uint8_t block[64];
};
static_assert(sizeof(ShaContext) <= 116);

uint32_t rotl(uint32_t v, int n) { return v << n | v >> (32 - n); }

void sha_block(ShaContext& c, const uint8_t* p) {
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

void sha_init(ShaContext& c) {
    c = ShaContext{{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, 0, 0, {}};
}

void sha_update(ShaContext& c, const uint8_t* data, uint32_t length) {
    uint32_t used = c.count_low % 64;
    const uint32_t old = c.count_low;
    c.count_low += length;
    if (c.count_low < old) ++c.count_high;
    while (length) {
        const uint32_t take = length < 64 - used ? length : 64 - used;
        std::memcpy(c.block + used, data, take);
        used += take, data += take, length -= take;
        if (used == 64) sha_block(c, c.block), used = 0;
    }
}

void sha_final(ShaContext& c, uint8_t* digest) {
    const uint64_t bits = (uint64_t{c.count_high} << 32 | c.count_low) * 8;
    static const uint8_t kPad[64] = {0x80};
    const uint32_t used = c.count_low % 64;
    sha_update(c, kPad, used < 56 ? 56 - used : 120 - used);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    sha_update(c, length, 8);
    for (int i = 0; i < 20; ++i) digest[i] = static_cast<uint8_t>(c.h[i / 4] >> (24 - (i % 4) * 8));
}

void XB_STDCALL XcSHAInit(ShaContext* context) { sha_init(*context); }

void XB_STDCALL XcSHAUpdate(ShaContext* context, const uint8_t* data, uint32_t length) {
    sha_update(*context, data, length);
}

void XB_STDCALL XcSHAFinal(ShaContext* context, uint8_t* digest) { sha_final(*context, digest); }

// HMAC-SHA1 of data1 || data2. Like XAPI's own HMAC, keys longer than a block are
// truncated rather than hashed.
void XB_STDCALL XcHMAC(const uint8_t* key, uint32_t key_length, const uint8_t* data1, uint32_t length1,
                       const uint8_t* data2, uint32_t length2, uint8_t* digest) {
    uint8_t pad[64] = {};
    std::memcpy(pad, key, key_length < 64 ? key_length : 64);
    for (auto& b : pad) b ^= 0x36;
    ShaContext c;
    sha_init(c);
    sha_update(c, pad, 64);
    if (data1) sha_update(c, data1, length1);
    if (data2) sha_update(c, data2, length2);
    uint8_t inner[20];
    sha_final(c, inner);
    for (auto& b : pad) b ^= 0x36 ^ 0x5C;
    sha_init(c);
    sha_update(c, pad, 64);
    sha_update(c, inner, 20);
    sha_final(c, digest);
}

// --- RC4 -----------------------------------------------------------------------------------

struct Rc4State {
    uint8_t s[256];
    uint8_t i, j;
};

void XB_STDCALL XcRC4Key(Rc4State* state, uint32_t key_length, const uint8_t* key) {
    for (int n = 0; n < 256; ++n) state->s[n] = static_cast<uint8_t>(n);
    uint8_t j = 0;
    for (int n = 0; n < 256; ++n) {
        j = static_cast<uint8_t>(j + state->s[n] + key[n % key_length]);
        std::swap(state->s[n], state->s[j]);
    }
    state->i = state->j = 0;
}

void XB_STDCALL XcRC4Crypt(Rc4State* state, uint32_t length, uint8_t* data) {
    uint8_t i = state->i, j = state->j;
    for (uint32_t n = 0; n < length; ++n) {
        i = static_cast<uint8_t>(i + 1);
        j = static_cast<uint8_t>(j + state->s[i]);
        std::swap(state->s[i], state->s[j]);
        data[n] ^= state->s[static_cast<uint8_t>(state->s[i] + state->s[j])];
    }
    state->i = i, state->j = j;
}

const KernelImpl kImpls[] = {
    KIMPL(XcHMAC),
    KIMPL(XcRC4Crypt),
    KIMPL(XcRC4Key),
    KIMPL(XcSHAFinal),
    KIMPL(XcSHAInit),
    KIMPL(XcSHAUpdate),
};

}  // namespace

std::span<const KernelImpl> crypto_kernel_impls() { return kImpls; }

}  // namespace xbox
