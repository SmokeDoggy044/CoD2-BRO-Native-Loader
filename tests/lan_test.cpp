// Virtual Xbox LAN (online/lan.h): two stations in one process find each other the
// way system link does - a broadcast to a fixed port, a unicast answer to the sender.

#include <cstdio>
#include <cstring>
#include <string_view>

#include "lan.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

constexpr uint8_t kUdp = 17, kVdp = 254;

// Polls until a datagram arrives (the host socket delivers asynchronously).
int receive(online::VirtualLan& lan, int socket, uint8_t* buffer, size_t size, uint32_t& from, uint16_t& port) {
    for (int i = 0; i < 100; ++i) {
        const int n = lan.receive(socket, {buffer, size}, from, port);
        if (n >= 0) return n;
        lan.wait(10);
    }
    return -1;
}

std::span<const uint8_t> bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

}  // namespace

int main() {
    // An unusual base port, so a running game does not interfere.
    const online::LanConfig config{23074, 4, 0};
    online::VirtualLan host, client;
    CHECK(host.start(config));
    CHECK(client.start(config));
    CHECK(host.host_port() != client.host_port());
    CHECK(host.address() != client.address());

    // Discovery: the host listens on 50000; the client broadcasts from any port.
    const int listener = host.open(kUdp);
    CHECK(host.bind(listener, 50000));
    CHECK(!host.bind(host.open(kUdp), 50000));  // taken
    CHECK(host.bind(host.open(kVdp), 50000));   // another protocol: free
    const int searcher = client.open(kUdp);
    CHECK(client.send(searcher, online::kBroadcastAddress, 50000, bytes("search")));

    uint8_t buffer[64];
    uint32_t from = 0;
    uint16_t port = 0;
    int n = receive(host, listener, buffer, sizeof(buffer), from, port);
    CHECK(n == 6 && std::memcmp(buffer, "search", 6) == 0);
    CHECK(from == client.address());
    CHECK(port >= 0x8000);  // the searcher's automatic port
    // The broadcast's loopback and LAN copies arrive once.
    CHECK(host.receive(listener, buffer, from, port) < 0);

    // The answer goes straight back to the sender's address and port.
    CHECK(host.send(listener, from, port, bytes("game here")));
    uint32_t answer_from = 0;
    uint16_t answer_port = 0;
    n = receive(client, searcher, buffer, sizeof(buffer), answer_from, answer_port);
    CHECK(n == 9 && std::memcmp(buffer, "game here", 9) == 0);
    CHECK(answer_from == host.address() && answer_port == 50000);

    // A datagram larger than the buffer: cut, full size returned.
    CHECK(host.send(listener, from, port, bytes("0123456789")));
    n = receive(client, searcher, buffer, 4, answer_from, answer_port);
    CHECK(n == 10 && std::memcmp(buffer, "0123", 4) == 0);

    // Loopback stays inside the station.
    const int self = host.open(kUdp);
    CHECK(host.bind(self, 1000));
    CHECK(host.send(listener, online::kLoopbackAddress, 1000, bytes("me")));
    CHECK(host.receive(listener, buffer, from, port) < 0);
    CHECK(host.receive(self, buffer, from, port) == 2 && from == online::kLoopbackAddress && port == 50000);

    host.stop();
    client.stop();
    if (g_failures) std::printf("%d failure(s)\n", g_failures);
    else std::printf("lan_test passed\n");
    return g_failures ? 1 : 0;
}
