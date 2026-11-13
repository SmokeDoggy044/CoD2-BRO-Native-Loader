#include "kernel_stubs.h"

#include <array>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "compiler.h"
#include "crash.h"
#include "kernel.h"
#include "log.h"
#include "xbe.h"

namespace loader {

namespace {

[[noreturn]] PLATFORM_NOINLINE void unimplemented(uint32_t ordinal, uint32_t return_address, uint32_t caller_ebp) {
    char message[160];
    std::snprintf(message, sizeof(message), "unimplemented kernel export #%u %s", ordinal,
                  xbox::kernel_export_name(ordinal));
    fatal_with_backtrace(message, return_address, caller_ebp);
}

// One distinct stub per ordinal, so the stub knows which export was called. The
// calling convention does not matter because it never returns.
template <uint32_t Ordinal>
[[noreturn]] PLATFORM_NOINLINE void unimplemented_stub() {
    const auto return_address = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const auto caller_ebp = *static_cast<uint32_t*>(__builtin_frame_address(0));
    unimplemented(Ordinal, static_cast<uint32_t>(return_address), caller_ebp);
}

template <size_t... Ordinals>
constexpr auto make_stubs(std::index_sequence<Ordinals...>) {
    return std::array<void (*)(), sizeof...(Ordinals)>{&unimplemented_stub<Ordinals>...};
}

const auto kStubs = make_stubs(std::make_index_sequence<xbox::kMaxKernelOrdinal + 1>{});

}  // namespace

void* kernel_stub(uint32_t ordinal) { return reinterpret_cast<void*>(kStubs[ordinal]); }

void* kernel_zero_block() {
    static std::vector<std::unique_ptr<uint8_t[]>> blocks;
    blocks.push_back(std::make_unique<uint8_t[]>(4096));
    return blocks.back().get();
}

void install_fatal_kernel_thunks(XbeImage& image) {
    auto* entry = reinterpret_cast<uint32_t*>(uintptr_t{image.kernel_thunk_table()});
    int zeroed = 0, stubbed = 0;
    for (; *entry; ++entry) {
        if (!(*entry & 0x80000000)) {
            LOG_WARN("kernel", "thunk %08x imports by name, not supported", *entry);
            continue;
        }
        const uint32_t ordinal = *entry & 0x7FFFFFFF;
        const xbox::KernelExport* exp = xbox::find_kernel_export(ordinal);
        if (!exp) {
            LOG_ERROR("kernel", "import of unknown kernel ordinal %u", ordinal);
            *entry = 0;
            continue;
        }
        void* target;
        if (exp->kind == xbox::ExportKind::Data) {
            target = kernel_zero_block();
            ++zeroed;
        } else {
            target = kernel_stub(ordinal);
            ++stubbed;
        }
        *entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    }
    LOG_INFO("kernel", "%d imports: 0 implemented, %d zero-filled data, %d not yet implemented", zeroed + stubbed,
             zeroed, stubbed);
}

}  // namespace loader
