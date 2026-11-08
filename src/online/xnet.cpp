// Xbox network stack (XNET) replacement: the Winsock subset and XNet address/key
// functions titles use for system link, on the virtual LAN (lan.h). See xonline.h.
//
// XNet addresses: a console's XNADDR holds its virtual address in `ina`. Real XNet
// hands out a separate "secure" IN_ADDR per registered peer (XNetXnAddrToInAddr);
// here a peer's IN_ADDR is simply its virtual address, so the mapping is the
// identity in both directions, and keys only need remembering.

#include "xonline.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <vector>

#include "compiler.h"
#include "lan.h"
#include "log.h"
#include "types.h"

namespace online {

namespace {

using xbox::XbPtr;
using xbox::from_xbox_ptr;

// winsockx.h values.
constexpr int32_t kSocketError = -1;
constexpr uint32_t kInvalidSocket = 0xFFFFFFFF;
constexpr uint32_t kAfInet = 2;
constexpr uint32_t kSockDgram = 2;
constexpr uint32_t kIpProtoUdp = 17;
constexpr uint32_t kIpProtoVdp = 254;
constexpr uint32_t kSolSocket = 0xFFFF;
constexpr uint32_t kSoBroadcast = 0x20;
constexpr uint32_t kFionbio = 0x8004667E;
constexpr uint32_t kWsaEWouldBlock = 10035;
constexpr uint32_t kWsaENotSock = 10038;
constexpr uint32_t kWsaEMsgSize = 10040;
constexpr uint32_t kWsaEProtoNoSupport = 10043;
constexpr uint32_t kWsaEAfNoSupport = 10047;
constexpr uint32_t kWsaEAddrInUse = 10048;
constexpr uint32_t kWsaENetDown = 10050;
constexpr uint32_t kWsaEInval = 10022;
constexpr uint32_t kWsaEFault = 10014;

// XNetGetEthernetLinkStatus flags.
constexpr uint32_t kLinkActive = 0x01;
constexpr uint32_t kLink100Mbps = 0x02;
constexpr uint32_t kLinkFullDuplex = 0x08;
// XNetGetTitleXnAddr result flags.
constexpr uint32_t kXnAddrEthernet = 0x02;
constexpr uint32_t kXnAddrStatic = 0x04;

struct SockAddrIn {  // sockaddr_in
    uint16_t family;
    uint16_t port;     // network byte order
    uint32_t address;  // network byte order
    uint8_t zero[8];
};
static_assert(sizeof(SockAddrIn) == 16);

struct XnAddr {  // XNADDR
    uint32_t ina;
    uint32_t ina_online;
    uint16_t port_online;
    uint8_t enet[6];
    uint8_t online[20];
};
static_assert(sizeof(XnAddr) == 36);

struct XnKid {
    uint8_t bytes[8];
};
struct XnKey {
    uint8_t bytes[16];
};

uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }

struct Socket {
    uint32_t handle;
    int lan_id;
    bool non_blocking;
};

struct State {
    std::mutex mutex;
    VirtualLan lan;
    LanConfig config;
    int startups = 0;  // XNetStartup/WSAStartup minus cleanups
    uint32_t next_handle = 0x100;
    std::vector<Socket> sockets;
    std::vector<XnKid> keys;  // registered session keys, newest last
};

State& state() {
    static State s;
    return s;
}

thread_local uint32_t t_last_error = 0;

int32_t fail(uint32_t error) {
    t_last_error = error;
    return kSocketError;
}

Socket* find_socket(State& s, uint32_t handle) {
    for (Socket& socket : s.sockets)
        if (socket.handle == handle) return &socket;
    return nullptr;
}

void fill_xnaddr(XnAddr& out, uint32_t ina) {
    std::memset(&out, 0, sizeof(out));
    out.ina = ina;
    // A locally administered MAC from the address, so each station has its own.
    const uint8_t mac[6] = {0x02, 0x50, static_cast<uint8_t>(ina), static_cast<uint8_t>(ina >> 8),
                            static_cast<uint8_t>(ina >> 16), static_cast<uint8_t>(ina >> 24)};
    std::memcpy(out.enet, mac, sizeof(mac));
}

void start_network(State& s) {
    if (s.startups++ == 0 && !s.lan.running()) s.lan.start(s.config);
}

void stop_network(State& s) {
    if (s.startups > 0 && --s.startups == 0) {
        s.sockets.clear();
        s.lan.stop();
    }
}

// --- XNet ------------------------------------------------------------------------

// The cable is plugged in: the host's network stands in for the Xbox's port.
uint32_t XB_STDCALL XNetGetEthernetLinkStatus() { return kLinkActive | kLink100Mbps | kLinkFullDuplex; }

int32_t XB_STDCALL XNetStartup(XbPtr /*parameters*/) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    start_network(s);
    return 0;
}

int32_t XB_STDCALL XNetCleanup() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    stop_network(s);
    return 0;
}

uint32_t XB_STDCALL XNetGetTitleXnAddr(XbPtr xnaddr) {
    State& s = state();
    fill_xnaddr(*from_xbox_ptr<XnAddr>(xnaddr), s.lan.address());
    return kXnAddrEthernet | kXnAddrStatic;
}

int32_t XB_STDCALL XNetRandom(XbPtr buffer, uint32_t size) {
    static std::mutex mutex;
    static std::mt19937 random{std::random_device{}()};
    std::lock_guard lock(mutex);
    auto* bytes = from_xbox_ptr<uint8_t>(buffer);
    for (uint32_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(random());
    return 0;
}

// A new session key: the id's top three bits 000 = system link (XNET_XNKID_SYSTEM_LINK).
int32_t XB_STDCALL XNetCreateKey(XbPtr kid, XbPtr key) {
    XNetRandom(kid, sizeof(XnKid));
    XNetRandom(key, sizeof(XnKey));
    from_xbox_ptr<XnKid>(kid)->bytes[0] &= 0x1F;
    return 0;
}

int32_t XB_STDCALL XNetRegisterKey(XbPtr kid, XbPtr /*key*/) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.keys.push_back(*from_xbox_ptr<XnKid>(kid));
    return 0;
}

int32_t XB_STDCALL XNetUnregisterKey(XbPtr kid) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    const XnKid& k = *from_xbox_ptr<XnKid>(kid);
    std::erase_if(s.keys, [&](const XnKid& e) { return std::memcmp(e.bytes, k.bytes, sizeof(k.bytes)) == 0; });
    return 0;
}

int32_t XB_STDCALL XNetXnAddrToInAddr(XbPtr xnaddr, XbPtr /*kid*/, XbPtr ina) {
    *from_xbox_ptr<uint32_t>(ina) = from_xbox_ptr<XnAddr>(xnaddr)->ina;
    return 0;
}

int32_t XB_STDCALL XNetInAddrToXnAddr(uint32_t ina, XbPtr xnaddr, XbPtr kid) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (xnaddr) fill_xnaddr(*from_xbox_ptr<XnAddr>(xnaddr), ina == kLoopbackAddress ? s.lan.address() : ina);
    if (kid) {
        if (s.keys.empty()) return static_cast<int32_t>(0x80070057);  // E_INVALIDARG: no session
        *from_xbox_ptr<XnKid>(kid) = s.keys.back();
    }
    return 0;
}

int32_t XB_STDCALL XNetUnregisterInAddr(uint32_t /*ina*/) { return 0; }

// XNet's key exchange with a peer before the first packet: nothing to exchange here.
constexpr uint32_t kConnectStatusConnected = 2;  // XNET_CONNECT_STATUS_CONNECTED

int32_t XB_STDCALL XNetConnect(uint32_t /*ina*/) { return 0; }

uint32_t XB_STDCALL XNetGetConnectStatus(uint32_t /*ina*/) { return kConnectStatusConnected; }

int32_t XB_STDCALL XNetInAddrToString(uint32_t ina, XbPtr text, int32_t size) {
    const auto* b = reinterpret_cast<const uint8_t*>(&ina);
    std::snprintf(from_xbox_ptr<char>(text), static_cast<size_t>(size), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return 0;
}

// --- Winsock -----------------------------------------------------------------------

int32_t XB_STDCALL WSAStartup(uint32_t version, XbPtr data) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (data) {
        auto* d = from_xbox_ptr<uint16_t>(data);
        d[0] = static_cast<uint16_t>(version);  // wVersion
        d[1] = 0x0202;                          // wHighVersion
    }
    start_network(s);
    return 0;
}

int32_t XB_STDCALL WSACleanup() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    stop_network(s);
    return 0;
}

uint32_t XB_STDCALL WSAGetLastError() { return t_last_error; }

uint16_t XB_STDCALL htons(uint16_t value) { return swap16(value); }

uint32_t XB_STDCALL inet_addr(XbPtr text) {
    unsigned a, b, c, d;
    char end;
    if (std::sscanf(from_xbox_ptr<const char>(text), "%u.%u.%u.%u%c", &a, &b, &c, &d, &end) != 4 || a > 255 ||
        b > 255 || c > 255 || d > 255)
        return 0xFFFFFFFF;  // INADDR_NONE
    return a | (b << 8) | (c << 16) | (d << 24);
}

uint32_t XB_STDCALL socket(uint32_t family, uint32_t type, uint32_t protocol) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (family != kAfInet) return static_cast<uint32_t>(fail(kWsaEAfNoSupport));
    if (type != kSockDgram || (protocol != 0 && protocol != kIpProtoUdp && protocol != kIpProtoVdp)) {
        LOG_WARN("xnet", "socket(type %u, protocol %u): only UDP/VDP datagrams", type, protocol);
        return static_cast<uint32_t>(fail(kWsaEProtoNoSupport));
    }
    if (!s.lan.running()) return static_cast<uint32_t>(fail(kWsaENetDown));
    const uint32_t handle = s.next_handle++;
    s.sockets.push_back({handle, s.lan.open(static_cast<uint8_t>(protocol ? protocol : kIpProtoUdp)), false});
    LOG_DEBUG("xnet", "socket %x: %s", handle, protocol == kIpProtoVdp ? "VDP" : "UDP");
    return handle;
}

int32_t XB_STDCALL closesocket(uint32_t handle) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    Socket* socket = find_socket(s, handle);
    if (!socket) return fail(kWsaENotSock);
    s.lan.close(socket->lan_id);
    std::erase_if(s.sockets, [handle](const Socket& e) { return e.handle == handle; });
    return 0;
}

// Broadcasts need no permission on the virtual LAN; every other option is accepted
// and ignored (logged once each).
int32_t XB_STDCALL setsockopt(uint32_t handle, uint32_t level, uint32_t name, XbPtr /*value*/, int32_t /*size*/) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (!find_socket(s, handle)) return fail(kWsaENotSock);
    if (!(level == kSolSocket && name == kSoBroadcast)) {
        static std::vector<uint64_t> logged;
        const uint64_t key = (uint64_t{level} << 32) | name;
        if (std::find(logged.begin(), logged.end(), key) == logged.end()) {
            logged.push_back(key);
            LOG_INFO("xnet", "setsockopt level %x option %x: ignored", level, name);
        }
    }
    return 0;
}

int32_t XB_STDCALL ioctlsocket(uint32_t handle, uint32_t command, XbPtr argument) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    Socket* socket = find_socket(s, handle);
    if (!socket) return fail(kWsaENotSock);
    if (command != kFionbio) {
        LOG_WARN("xnet", "ioctlsocket %x: not supported", command);
        return fail(kWsaEInval);
    }
    socket->non_blocking = *from_xbox_ptr<uint32_t>(argument) != 0;
    return 0;
}

int32_t XB_STDCALL bind(uint32_t handle, XbPtr name, int32_t size) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    Socket* socket = find_socket(s, handle);
    if (!socket) return fail(kWsaENotSock);
    if (size < static_cast<int32_t>(sizeof(SockAddrIn))) return fail(kWsaEFault);
    const uint16_t port = swap16(from_xbox_ptr<SockAddrIn>(name)->port);
    if (!s.lan.bind(socket->lan_id, port)) return fail(kWsaEAddrInUse);
    LOG_DEBUG("xnet", "bind %x: port %u", handle, port);
    return 0;
}

int32_t XB_STDCALL sendto(uint32_t handle, XbPtr buffer, int32_t length, uint32_t /*flags*/, XbPtr to,
                          int32_t to_size) {
    State& s = state();
    int lan_id;
    {
        std::lock_guard lock(s.mutex);
        Socket* socket = find_socket(s, handle);
        if (!socket) return fail(kWsaENotSock);
        lan_id = socket->lan_id;
    }
    if (length < 0 || to_size < static_cast<int32_t>(sizeof(SockAddrIn))) return fail(kWsaEFault);
    const SockAddrIn& address = *from_xbox_ptr<SockAddrIn>(to);
    const std::span data(from_xbox_ptr<const uint8_t>(buffer), static_cast<size_t>(length));
    if (!s.lan.send(lan_id, address.address, swap16(address.port), data)) return fail(kWsaEMsgSize);
    return length;
}

int32_t XB_STDCALL recvfrom(uint32_t handle, XbPtr buffer, int32_t length, uint32_t /*flags*/, XbPtr from,
                            XbPtr from_size) {
    State& s = state();
    int lan_id;
    bool non_blocking;
    {
        std::lock_guard lock(s.mutex);
        Socket* socket = find_socket(s, handle);
        if (!socket) return fail(kWsaENotSock);
        lan_id = socket->lan_id;
        non_blocking = socket->non_blocking;
    }
    const std::span out(from_xbox_ptr<uint8_t>(buffer), static_cast<size_t>(length < 0 ? 0 : length));
    uint32_t address = 0;
    uint16_t port = 0;
    int size;
    while ((size = s.lan.receive(lan_id, out, address, port)) < 0) {
        if (non_blocking) return fail(kWsaEWouldBlock);
        s.lan.wait(20);
    }
    if (from && from_size && *from_xbox_ptr<int32_t>(from_size) >= static_cast<int32_t>(sizeof(SockAddrIn))) {
        SockAddrIn& f = *from_xbox_ptr<SockAddrIn>(from);
        f = {};
        f.family = kAfInet;
        f.port = swap16(port);
        f.address = address;
        *from_xbox_ptr<int32_t>(from_size) = sizeof(SockAddrIn);
    }
    if (size > length) return fail(kWsaEMsgSize);
    return size;
}

#define XNET(name) {#name, reinterpret_cast<void*>(&name)}

const xbox::KernelImpl kFunctions[] = {
    XNET(XNetCleanup),
    XNET(XNetConnect),
    XNET(XNetCreateKey),
    XNET(XNetGetConnectStatus),
    XNET(XNetGetEthernetLinkStatus),
    XNET(XNetGetTitleXnAddr),
    XNET(XNetInAddrToString),
    XNET(XNetInAddrToXnAddr),
    XNET(XNetRandom),
    XNET(XNetRegisterKey),
    XNET(XNetStartup),
    XNET(XNetUnregisterInAddr),
    XNET(XNetUnregisterKey),
    XNET(XNetXnAddrToInAddr),
    XNET(WSACleanup),
    XNET(WSAGetLastError),
    XNET(WSAStartup),
    XNET(bind),
    XNET(closesocket),
    XNET(htons),
    XNET(inet_addr),
    XNET(ioctlsocket),
    XNET(recvfrom),
    XNET(sendto),
    XNET(setsockopt),
    XNET(socket),
};

}  // namespace

void configure_network(const LanConfig& config) { state().config = config; }

std::span<const xbox::KernelImpl> xnet_functions() { return kFunctions; }

}  // namespace online
