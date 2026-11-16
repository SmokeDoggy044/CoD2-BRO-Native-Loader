#pragma once

// ChaCha20 (RFC 8439: 256-bit key, 96-bit nonce, 32-bit block counter): the texture packs' stored files
// (render::texture_pack, tools/texture_pack/publish.py). Encrypting and decrypting are the same XOR.

#include <cstddef>
#include <cstdint>

namespace platform {

// XORs `size` bytes at `data` with the key stream starting at block `counter`.
void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* data, size_t size);

}  // namespace platform
