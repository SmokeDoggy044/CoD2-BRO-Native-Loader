#include "https.h"

#include <dlfcn.h>

#include <algorithm>
#include <string>

namespace platform {

namespace {

// libcurl, loaded when needed so that the programs start without it. Option and info
// numbers from curl.h (stable ABI since libcurl 7).
using Curl = void;
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011;
constexpr int kOptTimeout = 13, kOptFollowLocation = 52, kOptNoSignal = 99, kOptFailOnError = 45;
constexpr int kOptRange = 10007, kOptLowSpeedLimit = 19, kOptLowSpeedTime = 20, kOptConnectTimeout = 78;
constexpr int kInfoResponseCode = 0x200002;

struct Library {
    Curl* (*easy_init)();
    int (*easy_setopt)(Curl*, int, ...);
    int (*easy_perform)(Curl*);
    int (*easy_getinfo)(Curl*, int, ...);
    void (*easy_cleanup)(Curl*);
    const char* (*easy_strerror)(int);
};

const Library* curl() {
    static const Library* library = []() -> const Library* {
        void* handle = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
        if (!handle) return nullptr;
        static Library l;
        l.easy_init = reinterpret_cast<decltype(l.easy_init)>(dlsym(handle, "curl_easy_init"));
        l.easy_setopt = reinterpret_cast<decltype(l.easy_setopt)>(dlsym(handle, "curl_easy_setopt"));
        l.easy_perform = reinterpret_cast<decltype(l.easy_perform)>(dlsym(handle, "curl_easy_perform"));
        l.easy_getinfo = reinterpret_cast<decltype(l.easy_getinfo)>(dlsym(handle, "curl_easy_getinfo"));
        l.easy_cleanup = reinterpret_cast<decltype(l.easy_cleanup)>(dlsym(handle, "curl_easy_cleanup"));
        l.easy_strerror = reinterpret_cast<decltype(l.easy_strerror)>(dlsym(handle, "curl_easy_strerror"));
        if (!l.easy_init || !l.easy_setopt || !l.easy_perform || !l.easy_getinfo || !l.easy_cleanup ||
            !l.easy_strerror)
            return nullptr;
        return &l;
    }();
    return library;
}

size_t append(char* data, size_t size, size_t count, void* body) {
    static_cast<std::string*>(body)->append(data, size * count);
    return size * count;
}

struct Download {
    const Library* l;
    Curl* c;
    uint64_t skip = 0, left = 0;  // bytes to drop (a 200 answer to a range request), bytes still wanted
    bool checked = false, stopped = false;
    const std::function<bool(const char*, size_t)>* sink;
};

size_t receive(char* data, size_t size, size_t count, void* user) {
    auto& d = *static_cast<Download*>(user);
    size_t n = size * count;
    if (!d.checked) {
        d.checked = true;
        long status = 0;
        d.l->easy_getinfo(d.c, kInfoResponseCode, &status);
        if (status == 200) {
            // The whole file: skip to the range. (206 = the range itself.)
        } else if (status != 206) {
            return 0;
        } else {
            d.skip = 0;
        }
    }
    const char* p = data;
    size_t rest = n;
    const size_t drop = static_cast<size_t>(std::min<uint64_t>(d.skip, rest));
    d.skip -= drop, p += drop, rest -= drop;
    const size_t take = static_cast<size_t>(std::min<uint64_t>(d.left, rest));
    if (take && !(*d.sink)(p, take)) {
        d.stopped = true;
        return 0;
    }
    d.left -= take;
    // All wanted: stop a whole-file answer here (curl then reports a write error; the bytes are complete).
    return d.left == 0 && take < rest ? 0 : n;
}

bool loopback_http(const std::string& url) {
    return url.rfind("http://127.0.0.1:", 0) == 0 || url.rfind("http://127.0.0.1/", 0) == 0;
}

}  // namespace

bool https_download(const std::string& url, const std::string& user_agent, uint64_t offset, uint64_t length,
                    const std::function<bool(const char*, size_t)>& sink, std::string& error, long stall_seconds) {
    if (url.rfind("https://", 0) != 0 && !loopback_http(url)) {
        error = "not an https address";
        return false;
    }
    const Library* l = curl();
    if (!l) {
        error = "libcurl not found (install libcurl to download)";
        return false;
    }
    if (!length) return true;
    Curl* c = l->easy_init();
    if (!c) {
        error = "libcurl failed to start";
        return false;
    }
    Download d{l, c, offset, length, false, false, &sink};
    const std::string range = std::to_string(offset) + "-" + std::to_string(offset + length - 1);
    using WriteFunction = size_t (*)(char*, size_t, size_t, void*);
    l->easy_setopt(c, kOptUrl, url.c_str());
    l->easy_setopt(c, kOptUserAgent, user_agent.c_str());
    l->easy_setopt(c, kOptRange, range.c_str());
    l->easy_setopt(c, kOptWriteFunction, static_cast<WriteFunction>(receive));
    l->easy_setopt(c, kOptWriteData, static_cast<void*>(&d));
    l->easy_setopt(c, kOptFollowLocation, 1L);
    l->easy_setopt(c, kOptNoSignal, 1L);
    l->easy_setopt(c, kOptFailOnError, 1L);
    l->easy_setopt(c, kOptConnectTimeout, stall_seconds);
    l->easy_setopt(c, kOptLowSpeedLimit, 1L);
    l->easy_setopt(c, kOptLowSpeedTime, stall_seconds);
    const int result = l->easy_perform(c);
    long status = 0;
    l->easy_getinfo(c, kInfoResponseCode, &status);
    l->easy_cleanup(c);
    if (d.stopped) {
        error = "cancelled";
        return false;
    }
    if (d.left == 0) return true;
    if (status >= 400 || (d.checked && status != 200 && status != 206))
        error = "the server answered " + std::to_string(status);
    else if (result != 0)
        error = l->easy_strerror(result);
    else
        error = "the download ended early";
    return false;
}

std::optional<std::string> https_get(const std::string& url, const std::string& user_agent, long timeout_seconds) {
    if (url.rfind("https://", 0) != 0 && !loopback_http(url)) return {};
    const Library* l = curl();
    if (!l) return {};
    Curl* c = l->easy_init();
    if (!c) return {};
    std::string body;
    using WriteFunction = size_t (*)(char*, size_t, size_t, void*);
    l->easy_setopt(c, kOptUrl, url.c_str());
    l->easy_setopt(c, kOptUserAgent, user_agent.c_str());
    l->easy_setopt(c, kOptWriteFunction, static_cast<WriteFunction>(append));
    l->easy_setopt(c, kOptWriteData, static_cast<void*>(&body));
    l->easy_setopt(c, kOptTimeout, timeout_seconds);
    l->easy_setopt(c, kOptFollowLocation, 1L);
    l->easy_setopt(c, kOptNoSignal, 1L);
    long status = 0;
    const bool ok = l->easy_perform(c) == 0 && l->easy_getinfo(c, kInfoResponseCode, &status) == 0 && status == 200;
    l->easy_cleanup(c);
    if (!ok) return {};
    return body;
}

}  // namespace platform
