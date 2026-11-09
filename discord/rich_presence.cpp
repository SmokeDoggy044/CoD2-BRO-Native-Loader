// See rich_presence.h. Discord's local IPC protocol:
//
//   frame = little-endian uint32 opcode, uint32 payload length, JSON payload
//   opcode 0 = handshake {v: 1, client_id: "..."}, from then on opcode 1
//   opcode 1 = commands; SET_ACTIVITY carries the presence we publish
//
// The official discord-rpc library does the same over a socket/pipe; implementing
// it here keeps the loader and launcher free of another dependency (and of any
// network access beyond the user's own machine).

#include "rich_presence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace discord {
namespace {

using namespace std::chrono_literals;

std::mutex g_mutex;
std::condition_variable g_wake;
std::atomic<bool> g_stop{true};
std::atomic<bool> g_active{false};
std::thread g_thread;
std::string g_app_id, g_instance;
StatusProvider g_provider;
uint64_t g_start_time = 0;
uint64_t g_nonce = 0;

uint64_t process_id() {
#if defined(_WIN32)
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

uint64_t now_seconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string json_string(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

std::string nonce() { return std::to_string(++g_nonce) + "-" + std::to_string(process_id()); }

std::string activity_json(const Status& status) {
    std::string state = status.state;
    if (!status.context.empty()) state += (state.empty() ? "" : " - ") + status.context;
    std::string out = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(process_id()) +
                      ",\"activity\":{\"type\":0,\"details\":" + json_string(status.details) +
                      ",\"state\":" + json_string(state) + ",\"timestamps\":{\"start\":" +
                      std::to_string(g_start_time) + "}";
    // An asset only when the application has one: an unknown key would otherwise
    // show as a blank image in Discord.
    if (const char* asset = std::getenv("COD_BRO_DISCORD_ASSET"); asset && *asset) {
        out += ",\"assets\":{\"large_image\":" + json_string(asset) + ",\"large_text\":\"CoD2: BRO\"}";
    }
    out += "}},\"nonce\":" + json_string(nonce()) + "}";
    return out;
}

std::string clear_activity_json() {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(process_id()) +
           ",\"activity\":null},\"nonce\":" + json_string(nonce()) + "}";
}

std::vector<std::string> ipc_paths() {
    std::vector<std::string> dirs;
    if (const char* custom = std::getenv("DISCORD_IPC_PATH"); custom && *custom) dirs.emplace_back(custom);
#if defined(_WIN32)
    (void)dirs;
    std::vector<std::string> paths;
    for (int i = 0; i < 10; ++i) paths.push_back("\\\\?\\pipe\\discord-ipc-" + std::to_string(i));
    return paths;
#else
    const auto add_env = [&](const char* name) {
        if (const char* value = std::getenv(name); value && *value) dirs.emplace_back(value);
    };
    add_env("XDG_RUNTIME_DIR");
    add_env("TMPDIR");
    dirs.emplace_back("/run/user/" + std::to_string(static_cast<unsigned>(getuid())));
    dirs.emplace_back("/tmp");
    std::vector<std::string> extra;
    for (const std::string& dir : dirs) {
        extra.push_back(dir + "/snap.discord");
        extra.push_back(dir + "/snap.discord-canary");
        extra.push_back(dir + "/app/com.discordapp.Discord");
        extra.push_back(dir + "/app/com.discordapp.DiscordCanary");
    }
    dirs.insert(dirs.end(), extra.begin(), extra.end());
    std::vector<std::string> paths;
    for (const std::string& dir : dirs)
        for (int i = 0; i < 10; ++i) paths.push_back(dir + "/discord-ipc-" + std::to_string(i));
    return paths;
#endif
}

// One Discord IPC connection. `read` never blocks for long: the worker polls it
// once a second and treats a timeout as "no reply yet".
class Connection {
public:
    enum class Read { NoData, Frame, Closed };

    bool connect() {
        for (const std::string& path : ipc_paths()) {
#if defined(_WIN32)
            HANDLE pipe = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) continue;
            handle_ = pipe;
#else
            if (path.size() >= sizeof(sockaddr_un::sun_path)) continue;
            int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (socket_fd < 0) continue;
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 150000;
            ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            timeout.tv_sec = 1;
            ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (::connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                fd_ = socket_fd;
                return true;
            }
            ::close(socket_fd);
#endif
            return true;
        }
        return false;
    }

    void close() {
#if defined(_WIN32)
        if (handle_) CloseHandle(handle_);
        handle_ = nullptr;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }

    bool send(uint32_t opcode, const std::string& payload) {
        uint8_t header[8];
        const uint32_t length = static_cast<uint32_t>(payload.size());
        for (uint32_t i = 0; i < 4; ++i) {
            header[i] = static_cast<uint8_t>(opcode >> (8 * i));
            header[4 + i] = static_cast<uint8_t>(length >> (8 * i));
        }
        if (!write_all(header, sizeof(header))) return false;
        return write_all(payload.data(), payload.size());
    }

    Read read(std::string& payload) {
        uint8_t header[8];
#if defined(_WIN32)
        DWORD available = 0;
        if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return Read::Closed;
        if (available < sizeof(header)) return Read::NoData;
        if (!read_all(header, sizeof(header))) return Read::Closed;
#else
        const ssize_t got = ::recv(fd_, header, sizeof(header), MSG_WAITALL);
        if (got == 0) return Read::Closed;
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return Read::NoData;
            return Read::Closed;
        }
        if (static_cast<size_t>(got) < sizeof(header)) return Read::NoData;
#endif
        uint32_t length = 0;
        for (uint32_t i = 0; i < 4; ++i) length |= uint32_t(header[4 + i]) << (8 * i);
        if (length == 0 || length > 1024 * 1024) return Read::Closed;
        payload.resize(length);
        return read_all(payload.data(), length) ? Read::Frame : Read::Closed;
    }

private:
    bool write_all(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        while (size) {
#if defined(_WIN32)
            DWORD written = 0;
            if (!WriteFile(handle_, bytes, static_cast<DWORD>(size), &written, nullptr) || written == 0) return false;
#else
            const ssize_t written = ::send(fd_, bytes, size, MSG_NOSIGNAL);
            if (written <= 0) return false;
#endif
            bytes += written;
            size -= static_cast<size_t>(written);
        }
        return true;
    }

    bool read_all(void* data, size_t size) {
        auto* bytes = static_cast<uint8_t*>(data);
        while (size) {
#if defined(_WIN32)
            DWORD available = 0;
            if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return false;
            if (!available) { Sleep(1); continue; }
            DWORD want = static_cast<DWORD>(std::min<size_t>(size, available));
            DWORD got = 0;
            if (!ReadFile(handle_, bytes, want, &got, nullptr) || got == 0) return false;
#else
            const ssize_t got = ::recv(fd_, bytes, size, 0);
            if (got <= 0) return false;
#endif
            bytes += got;
            size -= static_cast<size_t>(got);
        }
        return true;
    }

#if defined(_WIN32)
    HANDLE handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

bool wait_seconds(int seconds) {
    std::unique_lock lock(g_mutex);
    return !g_wake.wait_for(lock, std::chrono::seconds(seconds), [] { return g_stop.load(); });
}

void worker() {
    Connection connection;
    while (!g_stop.load()) {
        if (!connection.connect()) {
            if (!wait_seconds(5)) break;
            continue;
        }
        if (!connection.send(0, "{\"v\":1,\"client_id\":\"" + g_app_id + "\"}")) {
            connection.close();
            if (!wait_seconds(2)) break;
            continue;
        }
        std::string last_key;
        bool connected = true;
        int since_send = 0;
        while (!g_stop.load() && connected) {
            Status status;
            {
                std::lock_guard lock(g_mutex);
                if (g_provider) status = g_provider();
            }
            const std::string key = status.details + '\n' + status.state + '\n' + status.context + '\n' +
                                    (status.playing ? "1" : "0");
            if (key != last_key || since_send >= 15) {
                const std::string json = status.playing ? activity_json(status) : clear_activity_json();
                if (!connection.send(1, json)) {
                    connected = false;
                    break;
                }
                last_key = key;
                since_send = 0;
            }
            std::string payload;
            switch (connection.read(payload)) {
                case Connection::Read::Closed: connected = false; break;
                case Connection::Read::Frame: break;  // READY / SET_ACTIVITY replies need no work
                case Connection::Read::NoData: break;
            }
            if (!wait_seconds(1)) break;
            ++since_send;
        }
        if (g_stop.load()) (void)connection.send(1, clear_activity_json());
        connection.close();
    }
    g_active.store(false);
}

bool env_disabled() {
    const char* value = std::getenv("COD_BRO_DISCORD_RPC");
    if (!value || !*value) return false;
    return !std::strcmp(value, "0") || !std::strcmp(value, "off") || !std::strcmp(value, "false") ||
           !std::strcmp(value, "no");
}

}  // namespace

bool init(const std::string& app_id, const std::string& instance, StatusProvider provider) {
    if (app_id.empty() || env_disabled()) return false;
    {
        std::lock_guard lock(g_mutex);
        if (g_active.load() && g_thread.joinable()) return true;
        g_app_id = app_id;
        g_instance = instance;
        g_provider = std::move(provider);
        g_start_time = now_seconds();
        g_stop.store(false);
        g_active.store(true);
    }
    try {
        g_thread = std::thread(worker);
    } catch (const std::exception&) {
        g_active.store(false);
        g_stop.store(true);
        return false;
    }
    return true;
}

bool active() { return g_active.load(); }

void shutdown() {
    if (!g_thread.joinable()) return;
    g_stop.store(true);
    g_wake.notify_all();
    g_thread.join();
    {
        std::lock_guard lock(g_mutex);
        g_provider = nullptr;
        g_active.store(false);
    }
}

}  // namespace discord
