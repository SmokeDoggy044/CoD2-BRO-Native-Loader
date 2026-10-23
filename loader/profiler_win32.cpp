// Sampling profiler, Windows side (profiler_os.h): each millisecond, every other
// thread that used CPU time since the last tick (QueryThreadCycleTime) is suspended
// for a GetThreadContext and resumed; nothing is allocated while one is suspended.
// A thread that ran only part of the millisecond may be caught in a wait already, so
// waits (ntdll/win32u/kernel32 functions) can take a few percent of a busy thread.
// Wine has no QueryThreadCycleTime (ERROR_CALL_NOT_IMPLEMENTED): there every thread is
// sampled on every tick, waiting or not.

#include "profiler_os.h"

#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>

#include "log.h"

namespace loader::profiler_os {

namespace {

struct Watched {
    HANDLE handle = nullptr;
    ULONG64 cycles = 0;
    bool seen = false;  // in the latest snapshot
};

// Names read while sampling, so threads that exited before the report keep theirs.
std::map<uint32_t, std::string> g_names;

std::string read_thread_name(HANDLE thread) {
    // Windows 10 1607+; older systems have no thread names.
    using GetDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto get_description = reinterpret_cast<GetDescription>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    if (!get_description) return {};
    PWSTR wide = nullptr;
    if (FAILED(get_description(thread, &wide)) || !wide) return {};
    char name[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name) - 1, nullptr, nullptr);
    LocalFree(wide);
    return name;
}

// Opens the process's new threads and closes those that are gone.
void refresh_threads(std::map<DWORD, Watched>& watched, DWORD self) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    for (auto& [tid, w] : watched) w.seen = false;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
        Watched& w = watched[entry.th32ThreadID];
        w.seen = true;
        if (w.handle) continue;
        w.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                              entry.th32ThreadID);
        if (!w.handle) continue;
        QueryThreadCycleTime(w.handle, &w.cycles);
        g_names[entry.th32ThreadID] = read_thread_name(w.handle);
    }
    CloseHandle(snapshot);
    for (auto it = watched.begin(); it != watched.end();) {
        if (it->second.seen && it->second.handle) {
            ++it;
            continue;
        }
        if (it->second.handle) CloseHandle(it->second.handle);
        it = watched.erase(it);
    }
}

}  // namespace

size_t sample(double seconds, std::vector<Sample>& samples) {
    using Clock = std::chrono::steady_clock;
    timeBeginPeriod(1);  // 1 ms sleeps (the default timer tick is 15.6 ms)
    const DWORD self = GetCurrentThreadId();
    std::map<DWORD, Watched> watched;
    auto refresh = Clock::now();
    size_t ticks = 0;
    bool no_cycle_time = false;
    const auto end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    while (Clock::now() < end) {
        if (Clock::now() >= refresh) {
            refresh_threads(watched, self);
            refresh = Clock::now() + std::chrono::milliseconds(100);
        }
        for (auto& [tid, w] : watched) {
            ULONG64 cycles = 0;
            if (QueryThreadCycleTime(w.handle, &cycles)) {
                if (cycles == w.cycles) continue;
                w.cycles = cycles;
            } else {
                no_cycle_time = true;
            }
            if (SuspendThread(w.handle) == static_cast<DWORD>(-1)) continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const BOOL got = GetThreadContext(w.handle, &context);
            ResumeThread(w.handle);
            if (got) samples.push_back({static_cast<uint32_t>(context.Eip), static_cast<uint32_t>(tid)});
        }
        ++ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    timeEndPeriod(1);
    if (no_cycle_time)
        LOG_WARN("profile", "no thread CPU times here (Wine?): waiting threads were sampled too, so busy "
                 "is not CPU use and waits are counted");
    for (auto& [tid, w] : watched) CloseHandle(w.handle);
    return ticks;
}

std::string thread_name(uint32_t tid) {
    const auto it = g_names.find(tid);
    return it != g_names.end() ? it->second : std::string{};
}

// The module and the offset in it (tools/resolve_profile.py names our own from the
// unstripped build), with the nearest exported function below the address for other
// modules: system DLLs ship no symbols, and most of their code is not exported, so
// that name is only a hint. Our DLL exports only its entry point: no hint.
std::string host_place(uint32_t eip) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(uintptr_t{eip}), &module)) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%08x (no module)", eip);
        return buffer;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* file = std::strrchr(path, '\\');
    file = file ? file + 1 : path;
    const auto* base = reinterpret_cast<const uint8_t*>(module);
    const uint32_t rva = eip - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(module));
    char text[MAX_PATH + 160];
    std::snprintf(text, sizeof(text), "%s +0x%x", file, rva);
    std::string result = text;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    HMODULE own = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&host_place), &own);
    if (module == own || !dir.VirtualAddress || !dir.Size) return result;
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
    const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    const char* best = nullptr;
    DWORD best_rva = 0;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const DWORD f = functions[ordinals[i]];
        if (f >= dir.VirtualAddress && f < dir.VirtualAddress + dir.Size) continue;  // forwarded
        if (f <= rva && f >= best_rva) best = reinterpret_cast<const char*>(base + names[i]), best_rva = f;
    }
    if (best) {
        std::snprintf(text, sizeof(text), " (%s+0x%x)", best, static_cast<unsigned>(rva - best_rva));
        result += text;
    }
    return result;
}

}  // namespace loader::profiler_os
