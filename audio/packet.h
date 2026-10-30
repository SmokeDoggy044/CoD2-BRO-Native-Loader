#pragma once

// A stream packet: title-owned sample memory a stream voice plays in order.

#include <cstdint>

namespace audio {

constexpr uint32_t kPacketSuccess = 0x00000000;  // XMP_STATUS_SUCCESS
constexpr uint32_t kPacketPending = 0x8000000A;  // XMP_STATUS_PENDING (E_PENDING)
constexpr uint32_t kPacketFlushed = 0x80004004;  // XMP_STATUS_FLUSHED (E_ABORT)

struct Packet {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t* status = nullptr;     // optional
    uint32_t* completed = nullptr;  // optional: bytes played
    uint32_t completion = 0;        // event handle or callback context (owner's business)
};

}  // namespace audio
