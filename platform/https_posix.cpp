#include "https.h"

#include <dlfcn.h>

namespace platform {

namespace {

// libcurl, loaded when needed so that the programs start without it. Option and info
// numbers from curl.h (stable ABI since libcurl 7).
using Curl = void;
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011;
constexpr int kOptTimeout = 13, kOptFollowLocation = 52, kOptNoSignal = 99;
constexpr int kInfoResponseCode = 0x200002;

struct Library {
    Curl* (*easy_init)();
    int (*easy_setopt)(Curl*, int, ...);
    int (*easy_perform)(Curl*);
    int (*easy_getinfo)(Curl*, int, ...);
    void (*easy_cleanup)(Curl*);
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
        if (!l.easy_init || !l.easy_setopt || !l.easy_perform || !l.easy_getinfo || !l.easy_cleanup) return nullptr;
        return &l;
    }();
    return library;
}

size_t append(char* data, size_t size, size_t count, void* body) {
    static_cast<std::string*>(body)->append(data, size * count);
    return size * count;
}

}  // namespace

std::optional<std::string> https_get(const std::string& url, const std::string& user_agent) {
    if (url.rfind("https://", 0) != 0) return {};
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
    l->easy_setopt(c, kOptTimeout, 10L);
    l->easy_setopt(c, kOptFollowLocation, 1L);
    l->easy_setopt(c, kOptNoSignal, 1L);
    long status = 0;
    const bool ok = l->easy_perform(c) == 0 && l->easy_getinfo(c, kInfoResponseCode, &status) == 0 && status == 200;
    l->easy_cleanup(c);
    if (!ok) return {};
    return body;
}

}  // namespace platform
