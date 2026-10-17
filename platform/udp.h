#pragma once

// A host UDP socket (IPv4), for the Xbox network replacement. Non-blocking, may send
// broadcasts.

#include <cstdint>
#include <span>

namespace platform {

struct UdpEndpoint {
    uint32_t address = 0;  // host byte order (0x7F000001 = 127.0.0.1)
    uint16_t port = 0;
};

class UdpSocket {
public:
    UdpSocket() = default;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket() { close(); }

    // Binds 0.0.0.0:port. False if the port is taken or there is no network stack.
    bool open(uint16_t port);
    void close();
    bool is_open() const { return handle_ != kNone; }

    bool send_to(const UdpEndpoint& to, std::span<const uint8_t> data);
    // Waits up to timeout_ms (0: does not wait) for a datagram. Returns its size (cut
    // to the buffer), or -1 if none came.
    int receive_from(std::span<uint8_t> buffer, UdpEndpoint& from, int timeout_ms);

private:
    static constexpr intptr_t kNone = -1;
    intptr_t handle_ = kNone;
};

}  // namespace platform
