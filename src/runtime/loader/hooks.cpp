#include "hooks.h"

#include <cstring>

#include "log.h"
#include "vmem.h"

namespace loader {

void install_jump(uint32_t address, const void* replacement) {
    // The image is mapped writable and executable, so no protection change is needed.
    auto* code = reinterpret_cast<uint8_t*>(uintptr_t{address});
    const auto target = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement));
    const uint32_t relative = target - (address + 5);
    code[0] = 0xE9;  // JMP rel32
    std::memcpy(code + 1, &relative, 4);
    LOG_DEBUG("hooks", "%08x -> %08x", address, target);
}

uint8_t* alloc_code(size_t size) {
    size = (size + platform::vmem::kPageSize - 1) & ~(platform::vmem::kPageSize - 1);
    void* code = platform::vmem::reserve(0, size);
    if (!code) return nullptr;
    if (!platform::vmem::commit(code, size, platform::vmem::Protection::ReadWriteExecute)) {
        platform::vmem::release(code, size);
        return nullptr;
    }
    return static_cast<uint8_t*>(code);
}

}  // namespace loader
