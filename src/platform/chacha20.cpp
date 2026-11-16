#include "chacha20.h"

#include <algorithm>
#include <cstring>

namespace platform {

namespace {

uint32_t load32(const uint8_t* p) {
    return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24;
}

uint32_t rotl(uint32_t x, int n) { return x << n | x >> (32 - n); }

void quarter(uint32_t* s, int a, int b, int c, int d) {
    s[a] += s[b], s[d] = rotl(s[d] ^ s[a], 16);
    s[c] += s[d], s[b] = rotl(s[b] ^ s[c], 12);
    s[a] += s[b], s[d] = rotl(s[d] ^ s[a], 8);
    s[c] += s[d], s[b] = rotl(s[b] ^ s[c], 7);
}

}  // namespace

void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* data, size_t size) {
    uint32_t input[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i) input[4 + i] = load32(key + i * 4);
    input[12] = counter;
    for (int i = 0; i < 3; ++i) input[13 + i] = load32(nonce + i * 4);
    while (size) {
        uint32_t s[16];
        std::memcpy(s, input, sizeof(s));
        for (int round = 0; round < 10; ++round) {
            quarter(s, 0, 4, 8, 12), quarter(s, 1, 5, 9, 13), quarter(s, 2, 6, 10, 14), quarter(s, 3, 7, 11, 15);
            quarter(s, 0, 5, 10, 15), quarter(s, 1, 6, 11, 12), quarter(s, 2, 7, 8, 13), quarter(s, 3, 4, 9, 14);
        }
        uint8_t block[64];
        for (int i = 0; i < 16; ++i) {
            const uint32_t v = s[i] + input[i];
            block[i * 4] = static_cast<uint8_t>(v), block[i * 4 + 1] = static_cast<uint8_t>(v >> 8);
            block[i * 4 + 2] = static_cast<uint8_t>(v >> 16), block[i * 4 + 3] = static_cast<uint8_t>(v >> 24);
        }
        const size_t n = std::min<size_t>(size, 64);
        for (size_t i = 0; i < n; ++i) data[i] ^= block[i];
        data += n, size -= n;
        ++input[12];
    }
}

}  // namespace platform
