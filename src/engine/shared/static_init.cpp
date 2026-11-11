#include "static_init.h"

#include <vector>

namespace game {

namespace {
std::vector<uint32_t>& destructors() {
    static std::vector<uint32_t> list;
    return list;
}
}  // namespace

uint32_t static_init_atexit(uint32_t destructor) {
    destructors().push_back(destructor);
    return 0;
}

void XB_CDECL run_static_initializers() {
    for (size_t i = 0; i < kStaticInitializerCount; ++i) {
        const StaticInitializer& entry = kStaticInitializers[i];
        if (entry.code)
            entry.code();
        else
            reinterpret_cast<void(XB_CDECL*)()>(static_cast<uintptr_t>(entry.address))();
    }
}

void XB_CDECL title_cinit() {
    const auto call = [](uint32_t address) { reinterpret_cast<void(XB_CDECL*)()>(static_cast<uintptr_t>(address))(); };
    if (const uint32_t fp_init = *reinterpret_cast<const uint32_t*>(uintptr_t{0x008d1378})) call(fp_init);
    for (uint32_t at = 0x008978d0; at < 0x008978ec; at += 4) {
        const uint32_t entry = *reinterpret_cast<const uint32_t*>(uintptr_t{at});
        if (entry && entry != 0xffffffffu) call(entry);
    }
    run_static_initializers();
}

}  // namespace game
