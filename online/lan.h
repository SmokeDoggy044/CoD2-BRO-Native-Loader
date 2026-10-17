#pragma once

// A virtual Xbox LAN for system link, carried over one host UDP socket per running
// game (the way an Xbox tunnels all title traffic through UDP 3074).
//
// The title's sockets are virtual: a protocol (UDP or VDP) and a port on this
// station's virtual address. Datagrams travel inside a host datagram with a small
// header naming both ends, so titles can bind the same fixed ports (discovery 50000)
// in several games on one PC. Broadcasts go to every station port on the LAN
// broadcast address and on loopback; stations learn each other's host endpoints from
// what they receive. Nothing is encrypted: XNet's security layer has no counterpart.

#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

#include "udp.h"

namespace online {

// Addresses are IN_ADDR values as stored in memory (network byte order bytes), ports
// in host byte order.
constexpr uint32_t kBroadcastAddress = 0xFFFFFFFF;
constexpr uint32_t kLoopbackAddress = 0x0100007F;  // 127.0.0.1

struct LanConfig {
    uint16_t base_port = 3074;  // station n uses base_port + n
    int stations = 4;           // games per PC that can see each other
    uint32_t address = 0;       // this station's virtual address (0: a random 10.x.y.z)
};

class VirtualLan {
public:
    bool start(const LanConfig& config);
    void stop();
    bool running() const;
    uint32_t address() const;
    uint16_t host_port() const;

    // Virtual sockets: ids > 0. protocol is the title's (17 UDP, 254 VDP).
    int open(uint8_t protocol);
    void close(int id);
    // port 0: a free one. False if the port is taken (for this protocol).
    bool bind(int id, uint16_t port);
    // Sends from `id` (binding it first if needed). Unknown unicast addresses drop
    // the datagram, as UDP would.
    bool send(int id, uint32_t to_address, uint16_t to_port, std::span<const uint8_t> data);
    // The next datagram for `id`, copied into buffer (cut to its size). Returns the
    // datagram's full size, or -1 if none is waiting (does not block).
    int receive(int id, std::span<uint8_t> buffer, uint32_t& from_address, uint16_t& from_port);
    // Waits up to timeout_ms for something to arrive for any socket.
    void wait(int timeout_ms);

private:
    struct Datagram {
        uint32_t from_address;
        uint16_t from_port;
        std::vector<uint8_t> data;
    };
    struct Socket {
        int id = 0;
        uint8_t protocol = 0;
        uint16_t port = 0;  // 0: not bound
        std::deque<Datagram> queue;
    };
    struct Peer {
        uint32_t address;
        platform::UdpEndpoint endpoint;
    };

    Socket* find(int id);
    bool port_in_use(uint8_t protocol, uint16_t port) const;
    bool bind_locked(Socket& socket, uint16_t port);
    void deliver(uint8_t protocol, uint32_t from_address, uint16_t from_port, uint16_t to_port,
                 std::span<const uint8_t> data);
    void pump(int timeout_ms);
    bool seen_before(uint32_t address, uint32_t sequence);

    mutable std::mutex mutex_;
    LanConfig config_;
    platform::UdpSocket host_;
    uint16_t host_port_ = 0;
    uint32_t address_ = 0;
    uint32_t sequence_ = 0;
    int next_id_ = 1;
    uint16_t next_port_ = 0x8000;
    std::vector<Socket> sockets_;
    std::vector<Peer> peers_;
    std::deque<std::pair<uint32_t, uint32_t>> recent_;  // (address, sequence): broadcast copies
};

}  // namespace online
