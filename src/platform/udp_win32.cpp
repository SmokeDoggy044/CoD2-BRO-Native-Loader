#include "udp.h"

#include <winsock2.h>

namespace platform {

namespace {

bool winsock_ready() {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

}  // namespace

bool UdpSocket::open(uint16_t port) {
    close();
    if (!winsock_ready()) return false;
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    const BOOL on = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
    // Without this, an ICMP port unreachable from an earlier send fails later receives.
    DWORD no_reset = FALSE, unused = 0;
    constexpr DWORD kSioUdpConnreset = 0x9800000C;  // SIO_UDP_CONNRESET
    ::WSAIoctl(s, kSioUdpConnreset, &no_reset, sizeof(no_reset), nullptr, 0, &unused, nullptr, nullptr);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    u_long non_blocking = 1;
    if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::ioctlsocket(s, FIONBIO, &non_blocking) != 0) {
        ::closesocket(s);
        return false;
    }
    handle_ = static_cast<intptr_t>(s);
    return true;
}

void UdpSocket::close() {
    if (handle_ == kNone) return;
    ::closesocket(static_cast<SOCKET>(handle_));
    handle_ = kNone;
}

bool UdpSocket::send_to(const UdpEndpoint& to, std::span<const uint8_t> data) {
    if (handle_ == kNone) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(to.port);
    address.sin_addr.s_addr = htonl(to.address);
    return ::sendto(static_cast<SOCKET>(handle_), reinterpret_cast<const char*>(data.data()),
                    static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)) == static_cast<int>(data.size());
}

int UdpSocket::receive_from(std::span<uint8_t> buffer, UdpEndpoint& from, int timeout_ms) {
    if (handle_ == kNone) return -1;
    const SOCKET s = static_cast<SOCKET>(handle_);
    for (;;) {
        sockaddr_in address{};
        int length = sizeof(address);
        const int size = ::recvfrom(s, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                    reinterpret_cast<sockaddr*>(&address), &length);
        const int error = size < 0 ? WSAGetLastError() : 0;
        if (size >= 0 || error == WSAEMSGSIZE) {  // WSAEMSGSIZE: cut to the buffer
            from = {ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
            return size >= 0 ? size : static_cast<int>(buffer.size());
        }
        if (error == WSAECONNRESET) continue;
        if (error != WSAEWOULDBLOCK || timeout_ms <= 0) return -1;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval wait{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (::select(0, &readable, nullptr, nullptr, &wait) <= 0) return -1;
        timeout_ms = 0;
    }
}

}  // namespace platform
