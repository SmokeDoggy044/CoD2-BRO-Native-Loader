// Windows side of the loader's platform layer (os.h) and its entry point, called by
// the stub executable (win32_stub.c) once it holds the Xbox address ranges.

#include "os.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstring>

#include "kernel.h"
#include "log.h"
#include "win32_layout.h"

namespace loader::os {

namespace {

static_assert(xbox::MemoryConfig{}.virtual_base == 0x10000000 && xbox::MemoryConfig{}.virtual_end == 0x50000000 &&
                  xbox::MemoryConfig{}.contiguous_base == 0x80000000,
              "keep COD_BRO_HELD_RANGES in win32_layout.h in step with xbox::MemoryConfig");

struct Range {
    DWORD start, size;
};
constexpr Range kHeldRanges[COD_BRO_HELD_RANGE_COUNT] = COD_BRO_HELD_RANGES;

// The running executable's image range (the stub's, which covers the XBE's).
bool in_executable_image(uint32_t base, uint32_t size) {
    const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;  // already replaced by the XBE
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    const auto start = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image));
    return base >= start && size <= nt->OptionalHeader.SizeOfImage - (base - start);
}

std::wstring widen(const std::string& text) {
    // The C runtime's narrow strings (argv, paths) use the ANSI code page.
    const int length = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), length);
    return wide;
}

std::string narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(length > 0 ? length - 1 : 0, '\0');
    if (length > 1) WideCharToMultiByte(CP_ACP, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

// One argument as CommandLineToArgvW and the C runtime split it again.
void append_quoted(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') ++i, ++backslashes;
        if (i == arg.size()) {
            line.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"')
            line.append(backslashes * 2 + 1, L'\\');
        else
            line.append(backslashes, L'\\');
        line += arg[i];
    }
    line += L'"';
}

}  // namespace

void* map_image(uint32_t base, uint32_t size) {
    void* const want = reinterpret_cast<void*>(uintptr_t{base});
    if (in_executable_image(base, size)) {
        // The stub's own pages: made writable (copy-on-write) and cleared. Its code
        // is not running any more (the stub is waiting for us to return, which we
        // never do).
        DWORD old = 0;
        if (!VirtualProtect(want, size, PAGE_EXECUTE_READWRITE, &old)) {
            LOG_ERROR("loader", "VirtualProtect at %08x: error %lu", base, GetLastError());
            return nullptr;
        }
        std::memset(want, 0, size);
        return want;
    }
    LOG_WARN("loader", "%08x-%08x is outside the stub executable's image; allocating it", base, base + size);
    void* mapped = VirtualAlloc(want, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (mapped != want) {
        LOG_ERROR("loader", "VirtualAlloc at %08x: error %lu", base, GetLastError());
        return nullptr;
    }
    return mapped;
}

void release_reserved_ranges() {
    for (const Range& range : kHeldRanges) {
        void* const start = reinterpret_cast<void*>(uintptr_t{range.start});
        MEMORY_BASIC_INFORMATION info{};
        // Only what the stub reserved (a run without the stub has nothing there).
        if (VirtualQuery(start, &info, sizeof(info)) && info.State == MEM_RESERVE && info.AllocationBase == start)
            VirtualFree(start, 0, MEM_RELEASE);
        else
            LOG_WARN("loader", "%08lx-%08lx was not held by the stub executable", range.start,
                     range.start + range.size);
    }
}

int pass_launch_data(std::span<const uint8_t> page) {
    wchar_t dir[MAX_PATH + 1], path[MAX_PATH + 1];
    if (!GetTempPathW(MAX_PATH + 1, dir) || !GetTempFileNameW(dir, L"cod", 0, path)) return -1;
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    // Deleted when the last handle (the new loader's) is closed.
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return -1;
    DWORD written = 0;
    if (!WriteFile(file, page.data(), static_cast<DWORD>(page.size()), &written, nullptr) || written != page.size()) {
        CloseHandle(file);
        return -1;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(file));
}

std::vector<uint8_t> read_launch_data(int id) {
    HANDLE file = reinterpret_cast<HANDLE>(intptr_t{id});
    std::vector<uint8_t> page(xbox::kLaunchDataPageSize);
    DWORD got = 0;
    const bool ok = SetFilePointer(file, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                    ReadFile(file, page.data(), static_cast<DWORD>(page.size()), &got, nullptr);
    CloseHandle(file);
    if (!ok || got != page.size()) {
        LOG_ERROR("loader", "launch data: read %lu of %zu bytes", got, page.size());
        return {};
    }
    return page;
}

void restart(const std::vector<std::string>& args) {
    wchar_t exe[MAX_PATH + 1];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH + 1);
    if (length == 0 || length > MAX_PATH) {
        LOG_ERROR("loader", "reboot: cannot find the loader executable");
        std::_Exit(1);
    }
    std::wstring line;
    append_quoted(line, exe);
    for (size_t i = 1; i < args.size(); ++i) append_quoted(line, widen(args[i]));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // Inherits the console and the launch data handle.
    if (!CreateProcessW(exe, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        LOG_ERROR("loader", "reboot: CreateProcess failed: error %lu", GetLastError());
        std::_Exit(1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::_Exit(0);
}

}  // namespace loader::os

extern "C" __declspec(dllexport) void cod_bro_loader_main() {
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    for (int i = 0; i < count; ++i) args.push_back(loader::os::narrow(wide[i]));
    LocalFree(wide);
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    // exit, not a return: the stub's code is gone once the XBE is mapped.
    std::exit(loader_main(count, argv.data()));
}
