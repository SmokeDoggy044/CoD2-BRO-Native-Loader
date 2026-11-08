#include "file.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace platform {

namespace {

std::error_code last_error() { return std::error_code(static_cast<int>(GetLastError()), std::system_category()); }

HANDLE native(intptr_t handle) { return reinterpret_cast<HANDLE>(handle); }

OVERLAPPED at(uint64_t offset) {
    OVERLAPPED overlapped{};
    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    return overlapped;
}

}  // namespace

std::unique_ptr<File> File::open(const std::filesystem::path& path, Access access, bool create,
                                 std::error_code& error) {
    const DWORD desired = GENERIC_READ | (access == Access::ReadWrite ? GENERIC_WRITE : 0);
    const DWORD disposition = (create && access == Access::ReadWrite) ? OPEN_ALWAYS : OPEN_EXISTING;
    const HANDLE handle = CreateFileW(path.c_str(), desired, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = last_error();
        return nullptr;
    }
    error.clear();
    return std::unique_ptr<File>(new File(reinterpret_cast<intptr_t>(handle)));
}

File::~File() { CloseHandle(native(handle_)); }

bool File::read_at(uint64_t offset, void* buffer, uint32_t size, uint32_t& done, std::error_code& error) {
    OVERLAPPED overlapped = at(offset);
    DWORD read = 0;
    if (!ReadFile(native(handle_), buffer, size, &read, &overlapped) && GetLastError() != ERROR_HANDLE_EOF) {
        error = last_error();
        done = 0;
        return false;
    }
    done = read;
    return true;
}

bool File::write_at(uint64_t offset, const void* buffer, uint32_t size, uint32_t& done, std::error_code& error) {
    OVERLAPPED overlapped = at(offset);
    DWORD written = 0;
    if (!WriteFile(native(handle_), buffer, size, &written, &overlapped)) {
        error = last_error();
        done = 0;
        return false;
    }
    done = written;
    return true;
}

std::optional<uint64_t> File::size() const {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(native(handle_), &size)) return std::nullopt;
    return static_cast<uint64_t>(size.QuadPart);
}

bool File::resize(uint64_t size, std::error_code& error) {
    FILE_END_OF_FILE_INFO info{};
    info.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
    if (SetFileInformationByHandle(native(handle_), FileEndOfFileInfo, &info, sizeof(info))) return true;
    error = last_error();
    return false;
}

bool File::flush() { return FlushFileBuffers(native(handle_)) != 0; }

}  // namespace platform
