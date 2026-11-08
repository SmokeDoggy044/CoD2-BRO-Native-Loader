#include "udp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace platform {

bool UdpSocket::open(uint16_t port) {
    close();
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return false;
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK) != 0) {
        ::close(fd);
        return false;
    }
    handle_ = fd;
    return true;
}

void UdpSocket::close() {
    if (handle_ == kNone) return;
    ::close(static_cast<int>(handle_));
    handle_ = kNone;
}

bool UdpSocket::send_to(const UdpEndpoint& to, std::span<const uint8_t> data) {
    if (handle_ == kNone) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(to.port);
    address.sin_addr.s_addr = htonl(to.address);
    return ::sendto(static_cast<int>(handle_), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)) == static_cast<ssize_t>(data.size());
}

int UdpSocket::receive_from(std::span<uint8_t> buffer, UdpEndpoint& from, int timeout_ms) {
    if (handle_ == kNone) return -1;
    const int fd = static_cast<int>(handle_);
    for (;;) {
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        const ssize_t size =
            ::recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&address), &length);
        if (size >= 0) {
            from = {ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
            return static_cast<int>(size);
        }
        // ECONNREFUSED: an earlier send's ICMP error (Linux reports it here); try again.
        if (errno == EINTR || errno == ECONNREFUSED) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (timeout_ms <= 0) return -1;
        pollfd wait{fd, POLLIN, 0};
        if (::poll(&wait, 1, timeout_ms) <= 0) return -1;
        timeout_ms = 0;
    }
}

}  // namespace platform
