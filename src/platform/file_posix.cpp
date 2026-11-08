#include "file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

namespace platform {

namespace {

std::error_code last_error() { return std::error_code(errno, std::generic_category()); }

}  // namespace

std::unique_ptr<File> File::open(const std::filesystem::path& path, Access access, bool create,
                                 std::error_code& error) {
    int flags = (access == Access::ReadWrite ? O_RDWR : O_RDONLY) | O_CLOEXEC;
    if (create && access == Access::ReadWrite) flags |= O_CREAT;
    const int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0) {
        error = last_error();
        return nullptr;
    }
    error.clear();
    return std::unique_ptr<File>(new File(fd));
}

File::~File() { ::close(static_cast<int>(handle_)); }

bool File::read_at(uint64_t offset, void* buffer, uint32_t size, uint32_t& done, std::error_code& error) {
    done = 0;
    while (done < size) {
        const ssize_t n = ::pread(static_cast<int>(handle_), static_cast<char*>(buffer) + done, size - done,
                                  static_cast<off_t>(offset + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            error = last_error();
            return false;
        }
        if (n == 0) break;  // end of file
        done += static_cast<uint32_t>(n);
    }
    return true;
}

bool File::write_at(uint64_t offset, const void* buffer, uint32_t size, uint32_t& done, std::error_code& error) {
    done = 0;
    while (done < size) {
        const ssize_t n = ::pwrite(static_cast<int>(handle_), static_cast<const char*>(buffer) + done, size - done,
                                   static_cast<off_t>(offset + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            error = last_error();
            return false;
        }
        done += static_cast<uint32_t>(n);
    }
    return true;
}

std::optional<uint64_t> File::size() const {
    struct stat st {};
    if (::fstat(static_cast<int>(handle_), &st) != 0) return std::nullopt;
    return static_cast<uint64_t>(st.st_size);
}

bool File::resize(uint64_t size, std::error_code& error) {
    if (::ftruncate(static_cast<int>(handle_), static_cast<off_t>(size)) == 0) return true;
    error = last_error();
    return false;
}

bool File::flush() { return ::fsync(static_cast<int>(handle_)) == 0; }

}  // namespace platform
