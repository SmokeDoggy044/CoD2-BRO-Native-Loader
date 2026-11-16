#include "https.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string>
#include <vector>

namespace platform {

namespace {

std::wstring widen(const std::string& text) {
    std::wstring wide(text.size(), L'\0');
    wide.resize(MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                    static_cast<int>(wide.size())));
    return wide;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::string last_error_text(const char* what) {
    return std::string(what) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

}  // namespace

bool https_download(const std::string& url, const std::string& user_agent, uint64_t offset, uint64_t length,
                    const std::function<bool(const char*, size_t)>& sink, std::string& error, long stall_seconds) {
    if (!length) return true;
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
        error = "not a web address";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    // Plain http only for developer test servers on this machine.
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && host != L"127.0.0.1") {
        error = "not an https address";
        return false;
    }
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle session{WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        error = last_error_text("WinHttpOpen");
        return false;
    }
    const int timeout_ms = static_cast<int>(std::min(stall_seconds, 300L) * 1000);
    WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    if (!connection.h) {
        error = last_error_text("Connecting");
        return false;
    }
    Handle request{WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    const std::wstring range = L"Range: bytes=" + std::to_wstring(offset) + L"-" + std::to_wstring(offset + length - 1);
    if (!request.h ||
        !WinHttpSendRequest(request.h, range.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = last_error_text("The request");
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        (status != 200 && status != 206)) {
        error = "the server answered " + std::to_string(status);
        return false;
    }
    uint64_t skip = status == 200 ? offset : 0, left = length;
    std::vector<char> chunk(1 << 16);
    while (left) {
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
            error = last_error_text("Reading");
            return false;
        }
        if (!read) {
            error = "the download ended early";
            return false;
        }
        const char* p = chunk.data();
        DWORD rest = read;
        const DWORD drop = static_cast<DWORD>(std::min<uint64_t>(skip, rest));
        skip -= drop, p += drop, rest -= drop;
        const DWORD take = static_cast<DWORD>(std::min<uint64_t>(left, rest));
        if (take && !sink(p, take)) {
            error = "cancelled";
            return false;
        }
        left -= take;
    }
    return true;
}

std::optional<std::string> https_get(const std::string& url, const std::string& user_agent, long timeout_seconds) {
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) return {};
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && host != L"127.0.0.1") return {};  // plain http: developer test servers only
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    Handle session{WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return {};
    const int timeout_ms = static_cast<int>(std::min(timeout_seconds, 60L) * 1000);
    WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    if (!connection.h) return {};
    Handle request{WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
        return {};
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) return {};
        if (!available) break;
        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) return {};
        body.append(chunk.data(), read);
    }
    return body;
}

}  // namespace platform
