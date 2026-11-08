#pragma once

// A host file opened for positioned reads and writes (pread/pwrite style), so one
// handle can serve several threads without a shared file pointer.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>

namespace platform {

class File {
public:
    enum class Access { Read, ReadWrite };

    // Opens an existing file, or creates it when `create` is set (ReadWrite only).
    static std::unique_ptr<File> open(const std::filesystem::path& path, Access access, bool create,
                                      std::error_code& error);
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // Return false on error; `done` is the byte count transferred (short at end of file).
    bool read_at(uint64_t offset, void* buffer, uint32_t size, uint32_t& done, std::error_code& error);
    bool write_at(uint64_t offset, const void* buffer, uint32_t size, uint32_t& done, std::error_code& error);

    std::optional<uint64_t> size() const;
    bool resize(uint64_t size, std::error_code& error);
    bool flush();

private:
    explicit File(intptr_t handle) : handle_(handle) {}
    intptr_t handle_;  // fd on POSIX, HANDLE on Windows
};

}  // namespace platform
