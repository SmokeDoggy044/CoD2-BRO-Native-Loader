#include "system_info.h"

#include <cpuid.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "log.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#include <fstream>
#endif

#ifndef COD_BRO_VERSION
#define COD_BRO_VERSION "dev"
#endif

namespace platform {

namespace {

std::string trimmed(std::string text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n\"");
    text = text.substr(first, last - first + 1);
    if (!text.empty() && text.front() == '"') text.erase(0, 1);
    return text;
}

uint64_t ram_bytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#else
    struct sysinfo info {};
    return sysinfo(&info) == 0 ? uint64_t{info.totalram} * info.mem_unit : 0;
#endif
}

std::string os_name() {
#ifdef _WIN32
    char text[192];
    // GetVersionEx answers with what the manifest allows; RtlGetVersion tells the truth.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    using WineVersionFn = const char*(CDECL*)();
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr)
        get(&version);
    // Windows 11 still says 10.0; its builds start at 22000.
    const char* name = version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000 ? "Windows 11"
                       : version.dwMajorVersion == 10                                 ? "Windows 10"
                                                                                      : "Windows";
    std::snprintf(text, sizeof(text), "%s (%lu.%lu build %lu)", name, version.dwMajorVersion,
                  version.dwMinorVersion, version.dwBuildNumber);
    std::string result = text;
    if (auto wine = ntdll ? reinterpret_cast<WineVersionFn>(GetProcAddress(ntdll, "wine_get_version")) : nullptr)
        result += std::string(" under Wine ") + wine();
    return result;
#else
    std::string distribution;
    std::ifstream release("/etc/os-release");
    for (std::string line; std::getline(release, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) distribution = trimmed(line.substr(12));
    utsname names{};
    uname(&names);
    if (distribution.empty()) distribution = names.sysname;
    return distribution + ", kernel " + names.release;
#endif
}

}  // namespace

std::string cpu_name() {
    unsigned int words[12] = {};
    unsigned int top = __get_cpuid_max(0x80000000, nullptr);
    if (top < 0x80000004) return {};
    for (unsigned int i = 0; i < 3; ++i)
        __get_cpuid(0x80000002 + i, &words[i * 4], &words[i * 4 + 1], &words[i * 4 + 2], &words[i * 4 + 3]);
    char text[sizeof(words) + 1] = {};
    std::memcpy(text, words, sizeof(words));
    return trimmed(text);
}

void log_system_info() {
    std::string cpu = cpu_name();
    if (cpu.empty()) cpu = "unknown CPU";
    const double ram_gb = static_cast<double>(ram_bytes()) / (1024.0 * 1024.0 * 1024.0);
    LOG_INFO("system", "version %s; %s (%u threads), %.1f GB RAM; %s", COD_BRO_VERSION, cpu.c_str(),
             std::thread::hardware_concurrency(), ram_gb, os_name().c_str());
}

}  // namespace platform
