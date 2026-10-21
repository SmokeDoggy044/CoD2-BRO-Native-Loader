#include "program.h"

#include <cerrno>
#include <cstring>

#include "log.h"
#include "paths.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace platform {

#if defined(_WIN32)

namespace {

// Quotes one argument the way CommandLineToArgvW splits them again.
std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

}  // namespace

bool start_program(const fs::path& program, const std::vector<std::string>& args) {
    std::wstring command = quote(program.wstring());
    for (const std::string& arg : args) command += L" " + quote(fs::path(arg).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring dir = program.parent_path().wstring();
    if (!CreateProcessW(program.wstring().c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &startup, &process)) {
        LOG_ERROR("process", "cannot start %s (error %lu)", program.string().c_str(), GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

int run_program(const fs::path& program, const std::vector<fs::path>& args) {
    std::wstring command = quote(program.wstring());
    for (const fs::path& arg : args) command += L" " + quote(arg.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.wstring().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        LOG_ERROR("process", "cannot start %s (error %lu)", program.string().c_str(), GetLastError());
        return -1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    const bool ok = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return ok ? static_cast<int>(code) : -1;
}

fs::path sibling_executable(const std::string& name) { return executable_dir() / (name + ".exe"); }

fs::path system_program(const std::string& name) {
    wchar_t dir[MAX_PATH];
    // A 32-bit program on 64-bit Windows sees SysWOW64 as System32; Sysnative is the
    // real one (some programs are only there).
    if (const UINT length = GetSystemWindowsDirectoryW(dir, MAX_PATH); length && length < MAX_PATH) {
        std::error_code ec;
        const fs::path native = fs::path(dir) / "Sysnative" / (name + ".exe");
        if (fs::exists(native, ec)) return native;
    }
    const UINT length = GetSystemDirectoryW(dir, MAX_PATH);
    return (length && length < MAX_PATH ? fs::path(dir) : fs::path(L"C:\\Windows\\System32")) / (name + ".exe");
}

#else

bool start_program(const fs::path& program, const std::vector<std::string>& args) {
    std::vector<std::string> strings = {program.string()};
    strings.insert(strings.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : strings) argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (const int error = posix_spawn(&pid, strings[0].c_str(), nullptr, nullptr, argv.data(), environ)) {
        LOG_ERROR("process", "cannot start %s: %s", strings[0].c_str(), std::strerror(error));
        return false;
    }
    return true;
}

int run_program(const fs::path& program, const std::vector<fs::path>& args) {
    std::vector<std::string> strings = {program.string()};
    for (const fs::path& arg : args) strings.push_back(arg.string());
    std::vector<char*> argv;
    for (std::string& s : strings) argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (const int error = posix_spawn(&pid, strings[0].c_str(), nullptr, nullptr, argv.data(), environ)) {
        LOG_ERROR("process", "cannot start %s: %s", strings[0].c_str(), std::strerror(error));
        return -1;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

fs::path sibling_executable(const std::string& name) { return executable_dir() / name; }

fs::path system_program(const std::string& name) {
    std::error_code ec;
    const fs::path usr = fs::path("/usr/bin") / name;
    return fs::exists(usr, ec) ? usr : fs::path("/bin") / name;
}

#endif

}  // namespace platform
