// Virtual Xbox LAN. See lan.h.

#include "lan.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "log.h"

namespace online {

namespace {

// Host datagram: header, then the title's datagram.
constexpr uint32_t kMagic = 0x4E4C4258;  // "XBLN"
struct Header {
    uint32_t magic;
    uint32_t sequence;
    uint32_t from_address;
    uint32_t to_address;
    uint16_t from_port;
    uint16_t to_port;
    uint8_t protocol;
};
constexpr size_t kHeaderSize = 21;
constexpr size_t kMaxDatagram = 4096;
constexpr size_t kMaxQueued = 256;  // per socket; older datagrams are dropped

void put_header(uint8_t* out, const Header& h) {
    std::memcpy(out, &h.magic, 4);
    std::memcpy(out + 4, &h.sequence, 4);
    std::memcpy(out + 8, &h.from_address, 4);
    std::memcpy(out + 12, &h.to_address, 4);
    std::memcpy(out + 16, &h.from_port, 2);
    std::memcpy(out + 18, &h.to_port, 2);
    out[20] = h.protocol;
}

Header get_header(const uint8_t* in) {
    Header h;
    std::memcpy(&h.magic, in, 4);
    std::memcpy(&h.sequence, in + 4, 4);
    std::memcpy(&h.from_address, in + 8, 4);
    std::memcpy(&h.to_address, in + 12, 4);
    std::memcpy(&h.from_port, in + 16, 2);
    std::memcpy(&h.to_port, in + 18, 2);
    h.protocol = in[20];
    return h;
}

uint32_t random_address() {
    std::random_device random;
    const uint32_t bits = random();
    const uint8_t bytes[4] = {10, static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8),
                              static_cast<uint8_t>(1 + (bits >> 16) % 254)};
    uint32_t address;
    std::memcpy(&address, bytes, 4);
    return address;
}

}  // namespace

bool VirtualLan::start(const LanConfig& config) {
    std::lock_guard lock(mutex_);
    if (host_.is_open()) return true;
    config_ = config;
    for (int n = 0; n < config.stations; ++n) {
        const auto port = static_cast<uint16_t>(config.base_port + n);
        if (host_.open(port)) {
            host_port_ = port;
            break;
        }
    }
    if (!host_.is_open()) {
        LOG_WARN("lan", "no free UDP port in %u-%u: no system link", config.base_port,
                 config.base_port + config.stations - 1);
        return false;
    }
    address_ = config.address ? config.address : random_address();
    sequence_ = std::random_device{}();
    const auto* a = reinterpret_cast<const uint8_t*>(&address_);
    LOG_INFO("lan", "system link on UDP port %u, virtual address %u.%u.%u.%u", host_port_, a[0], a[1], a[2], a[3]);
    return true;
}

void VirtualLan::stop() {
    std::lock_guard lock(mutex_);
    host_.close();
    sockets_.clear();
    peers_.clear();
    recent_.clear();
}

bool VirtualLan::running() const {
    std::lock_guard lock(mutex_);
    return host_.is_open();
}

uint32_t VirtualLan::address() const {
    std::lock_guard lock(mutex_);
    return address_;
}

uint16_t VirtualLan::host_port() const {
    std::lock_guard lock(mutex_);
    return host_port_;
}

int VirtualLan::open(uint8_t protocol) {
    std::lock_guard lock(mutex_);
    Socket socket;
    socket.id = next_id_++;
    socket.protocol = protocol;
    sockets_.push_back(std::move(socket));
    return sockets_.back().id;
}

void VirtualLan::close(int id) {
    std::lock_guard lock(mutex_);
    std::erase_if(sockets_, [id](const Socket& s) { return s.id == id; });
}

VirtualLan::Socket* VirtualLan::find(int id) {
    for (Socket& s : sockets_)
        if (s.id == id) return &s;
    return nullptr;
}

bool VirtualLan::port_in_use(uint8_t protocol, uint16_t port) const {
    return std::any_of(sockets_.begin(), sockets_.end(),
                       [&](const Socket& s) { return s.protocol == protocol && s.port == port; });
}

bool VirtualLan::bind_locked(Socket& socket, uint16_t port) {
    if (port == 0) {
        do port = next_port_ = static_cast<uint16_t>(next_port_ == 0xFFFF ? 0x8000 : next_port_ + 1);
        while (port_in_use(socket.protocol, port));
    } else if (port_in_use(socket.protocol, port)) {
        return false;
    }
    socket.port = port;
    return true;
}

bool VirtualLan::bind(int id, uint16_t port) {
    std::lock_guard lock(mutex_);
    Socket* socket = find(id);
    return socket && bind_locked(*socket, port);
}

void VirtualLan::deliver(uint8_t protocol, uint32_t from_address, uint16_t from_port, uint16_t to_port,
                         std::span<const uint8_t> data) {
    for (Socket& s : sockets_) {
        if (s.protocol != protocol || s.port != to_port) continue;
        if (s.queue.size() >= kMaxQueued) s.queue.pop_front();
        s.queue.push_back({from_address, from_port, {data.begin(), data.end()}});
        return;
    }
}

bool VirtualLan::send(int id, uint32_t to_address, uint16_t to_port, std::span<const uint8_t> data) {
    std::lock_guard lock(mutex_);
    Socket* socket = find(id);
    if (!socket || !host_.is_open() || data.size() > kMaxDatagram) return false;
    if (socket->port == 0 && !bind_locked(*socket, 0)) return false;
    if (to_address == kLoopbackAddress || to_address == address_) {
        deliver(socket->protocol, to_address, socket->port, to_port, data);
        return true;
    }
    uint8_t packet[kHeaderSize + kMaxDatagram];
    put_header(packet, {kMagic, ++sequence_, address_, to_address, socket->port, to_port, socket->protocol});
    std::memcpy(packet + kHeaderSize, data.data(), data.size());
    const std::span<const uint8_t> bytes(packet, kHeaderSize + data.size());
    if (to_address == kBroadcastAddress) {
        for (int n = 0; n < config_.stations; ++n) {
            const auto port = static_cast<uint16_t>(config_.base_port + n);
            host_.send_to({0xFFFFFFFF, port}, bytes);
            if (port != host_port_) host_.send_to({0x7F000001, port}, bytes);
        }
        return true;
    }
    for (const Peer& peer : peers_)
        if (peer.address == to_address) return host_.send_to(peer.endpoint, bytes);
    return true;  // nobody there: lost, as on a real network
}

bool VirtualLan::seen_before(uint32_t address, uint32_t sequence) {
    const auto key = std::make_pair(address, sequence);
    if (std::find(recent_.begin(), recent_.end(), key) != recent_.end()) return true;
    if (recent_.size() >= 64) recent_.pop_front();
    recent_.push_back(key);
    return false;
}

void VirtualLan::pump(int timeout_ms) {
    uint8_t packet[kHeaderSize + kMaxDatagram];
    platform::UdpEndpoint from;
    for (int size; (size = host_.receive_from(packet, from, timeout_ms)) >= 0; timeout_ms = 0) {
        if (static_cast<size_t>(size) < kHeaderSize) continue;
        const Header h = get_header(packet);
        if (h.magic != kMagic || h.from_address == address_) continue;  // not ours, or our own broadcast
        if (h.to_address != kBroadcastAddress && h.to_address != address_) continue;
        if (seen_before(h.from_address, h.sequence)) continue;  // a broadcast's second copy
        auto peer = std::find_if(peers_.begin(), peers_.end(), [&](const Peer& p) { return p.address == h.from_address; });
        if (peer == peers_.end())
            peers_.push_back({h.from_address, from});
        else
            peer->endpoint = from;
        deliver(h.protocol, h.from_address, h.from_port, h.to_port,
                std::span<const uint8_t>(packet + kHeaderSize, static_cast<size_t>(size) - kHeaderSize));
    }
}

int VirtualLan::receive(int id, std::span<uint8_t> buffer, uint32_t& from_address, uint16_t& from_port) {
    std::lock_guard lock(mutex_);
    pump(0);
    Socket* socket = find(id);
    if (!socket || socket->queue.empty()) return -1;
    const Datagram& d = socket->queue.front();
    std::memcpy(buffer.data(), d.data.data(), std::min(buffer.size(), d.data.size()));
    from_address = d.from_address;
    from_port = d.from_port;
    const int size = static_cast<int>(d.data.size());
    socket->queue.pop_front();
    return size;
}

void VirtualLan::wait(int timeout_ms) {
    std::lock_guard lock(mutex_);
    for (const Socket& s : sockets_)
        if (!s.queue.empty()) return;
    pump(timeout_ms);
}

}  // namespace online
