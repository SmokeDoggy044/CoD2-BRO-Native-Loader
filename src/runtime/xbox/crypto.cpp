// Kernel crypto services (Xc*): SHA-1, HMAC-SHA1 and RC4. Titles use them for save
// game signatures (XCalculateSignature*) and content keys. The DES, public-key and
// big-number services are for Xbox Live and stay unimplemented until a title needs
// them offline.

#include <cstring>

#include "compiler.h"
#include "kernel_internal.h"
#include "sha1.h"
#include "types.h"

namespace xbox {

namespace {

// --- SHA-1 (FIPS 180-1) ------------------------------------------------------------------

// Lives in the title's A_SHA_CTX (116 bytes in XAPI's signature context); shared with our own C (sha1.h).
using ShaContext = platform::sha1::Context;
void sha_init(ShaContext& c) { platform::sha1::init(c); }
void sha_update(ShaContext& c, const uint8_t* data, uint32_t length) { platform::sha1::update(c, data, length); }
void sha_final(ShaContext& c, uint8_t* digest) { platform::sha1::final(c, digest); }

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
