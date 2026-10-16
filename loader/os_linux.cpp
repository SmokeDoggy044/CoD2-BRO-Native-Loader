// Linux side of the loader's platform layer (os.h) and its entry point.

#include "os.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "kernel.h"
#include "log.h"

namespace loader::os {

void* map_image(uint32_t base, uint32_t size) {
    void* const want = reinterpret_cast<void*>(uintptr_t{base});
    void* mapped = mmap(want, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mapped == MAP_FAILED || mapped != want) {
        LOG_ERROR("loader", "mmap at %08x: %s", base, std::strerror(errno));
        return nullptr;
    }
    return mapped;
}

void release_reserved_ranges() {}

int pass_launch_data(std::span<const uint8_t> page) {
    const int fd = memfd_create("cod_bro_launch_data", 0);  // inherited by the new image
    if (fd < 0 || write(fd, page.data(), page.size()) != static_cast<ssize_t>(page.size())) return -1;
    return fd;
}

std::vector<uint8_t> read_launch_data(int fd) {
    std::vector<uint8_t> page(xbox::kLaunchDataPageSize);
    const ssize_t got = pread(fd, page.data(), page.size(), 0);
    close(fd);
    if (got != static_cast<ssize_t>(page.size())) {
        LOG_ERROR("loader", "launch data: read %zd of %zu bytes", got, page.size());
        return {};
    }
    return page;
}

void restart(const std::vector<std::string>& args) {
    std::vector<std::string> copy = args;
    std::vector<char*> argv;
    for (std::string& arg : copy) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execv("/proc/self/exe", argv.data());
    LOG_ERROR("loader", "reboot: exec failed: %s", std::strerror(errno));
    _exit(1);
}

}  // namespace loader::os

int main(int argc, char** argv) { return loader_main(argc, argv); }
