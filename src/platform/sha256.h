#pragma once

// SHA-256 (FIPS 180-4): release downloads against SHA256SUMS.txt, texture pack files against their index, and the
// texture pack keys (render::texture_pack).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace platform {

class Sha256 {
public:
    void update(const void* data, size_t size);
    void update(std::string_view data) { update(data.data(), data.size()); }
    // The digest; the object is spent afterwards.
    std::array<uint8_t, 32> finish();
    std::string finish_hex();

private:
    std::array<uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buffer_[64] = {};
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

// The digest of `data` as 64 lowercase hex digits (sha256sum's form).
std::string sha256_hex(std::string_view data);

std::string to_hex(const uint8_t* data, size_t size);

}  // namespace platform
