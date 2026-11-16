#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace platform {

namespace {

constexpr std::array<uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotr(uint32_t x, int n) { return x >> n | x << (32 - n); }

void compress(std::array<uint32_t, 8>& h, const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t{block[i * 4]} << 24 | uint32_t{block[i * 4 + 1]} << 16 | uint32_t{block[i * 4 + 2]} << 8 |
               block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ w[i - 15] >> 3;
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

}  // namespace

void Sha256::update(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    total_ += size;
    if (buffered_) {
        const size_t take = std::min(size, 64 - buffered_);
        std::memcpy(buffer_ + buffered_, bytes, take);
        buffered_ += take, bytes += take, size -= take;
        if (buffered_ < 64) return;
        compress(h_, buffer_);
        buffered_ = 0;
    }
    for (; size >= 64; bytes += 64, size -= 64) compress(h_, bytes);
    std::memcpy(buffer_, bytes, size);
    buffered_ = size;
}

std::array<uint8_t, 32> Sha256::finish() {
    // The rest, 0x80, zeros, then the length in bits (big-endian) in one or two blocks.
    unsigned char tail[128] = {};
    std::memcpy(tail, buffer_, buffered_);
    tail[buffered_] = 0x80;
    const size_t tail_size = buffered_ < 56 ? 64 : 128;
    const uint64_t bits = total_ * 8;
    for (int i = 0; i < 8; ++i) tail[tail_size - 1 - i] = static_cast<unsigned char>(bits >> (i * 8));
    for (size_t i = 0; i < tail_size; i += 64) compress(h_, tail + i);
    std::array<uint8_t, 32> digest;
    for (int i = 0; i < 32; ++i) digest[i] = static_cast<uint8_t>(h_[i / 4] >> (24 - i % 4 * 8));
    return digest;
}

std::string Sha256::finish_hex() {
    const auto digest = finish();
    return to_hex(digest.data(), digest.size());
}

std::string to_hex(const uint8_t* data, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) hex += kHex[data[i] >> 4], hex += kHex[data[i] & 0xF];
    return hex;
}

std::string sha256_hex(std::string_view data) {
    Sha256 h;
    h.update(data);
    return h.finish_hex();
}

}  // namespace platform
