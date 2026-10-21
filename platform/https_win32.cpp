#include "https.h"

#include <windows.h>
#include <winhttp.h>

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

}  // namespace

std::optional<std::string> https_get(const std::string& url, const std::string& user_agent) {
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return {};
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    Handle session{WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return {};
    WinHttpSetTimeouts(session.h, 5000, 5000, 5000, 5000);
    Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    if (!connection.h) return {};
    Handle request{WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
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
