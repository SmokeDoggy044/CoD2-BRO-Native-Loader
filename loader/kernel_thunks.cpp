#include "kernel_thunks.h"
#include "seh.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "compiler.h"
#include "crash.h"
#include "kernel.h"
#include "kernel_memory.h"
#include "log.h"
#include "objects.h"
#include "thread_env.h"
#include "xbe.h"

namespace loader {

namespace {

XbeImage* g_image = nullptr;

// --- Kernel exports that need loader state ----------------------------------------

using xbox::kStatusSuccess;

// Pages a section occupies, charged to the Xbox memory budget while it is loaded.
int32_t section_pages(const XbeSectionHeader& section) {
    const uint32_t start = section.virtual_addr & ~0xFFFu;
    const uint32_t end = (section.virtual_addr + section.virtual_size + 0xFFF) & ~0xFFFu;
    return static_cast<int32_t>((end - start) / 0x1000);
}

uint32_t XB_STDCALL XeLoadSection(XbeSectionHeader* section) {
    if (section->reference_count == 0) xbox::charge_image_pages(section_pages(*section));
    g_image->load_section(*section);
    LOG_INFO("kernel", "XeLoadSection(%s) refs=%u", g_image->section_name(*section).c_str(),
              section->reference_count);
    return kStatusSuccess;
}

uint32_t XB_STDCALL XeUnloadSection(XbeSectionHeader* section) {
    if (section->reference_count == 1) xbox::charge_image_pages(-section_pages(*section));
    g_image->unload_section(*section);
    LOG_DEBUG("kernel", "XeUnloadSection(%s) refs=%u", g_image->section_name(*section).c_str(),
              section->reference_count);
    return kStatusSuccess;
}

// --- Threads -----------------------------------------------------------------------
//
// Every Xbox thread is a host thread with its own ThreadEnv (FS, stack, TLS). The
// Xbox is single-core with a priority scheduler; here threads really run in
// parallel and priorities are ignored, which may matter later.
// TODO: a terminated thread's ThreadEnv and stack are never freed.

thread_local std::shared_ptr<xbox::ThreadObject> t_thread_object;
std::atomic<uint32_t> g_next_thread_id{1};

struct ThreadStart {
    ThreadEnv* env;
    std::shared_ptr<xbox::ThreadObject> object;
    uint32_t start_routine;
    uint32_t start_context;
    uint32_t system_routine;
};

void xbox_thread_main(ThreadStart start) {
    if (!activate_thread_env(start.env)) {
        LOG_ERROR("thread", "thread %u cannot set up FS", start.object->id());
        start.object->mark_terminated(kStatusSuccess);
        return;
    }
    t_thread_object = start.object;
    start.object->wait_until_resumed();
    LOG_DEBUG("thread", "thread %u starts at %08x", start.object->id(),
              start.system_routine ? start.system_routine : start.start_routine);

    // As in the Xbox kernel: a system routine (XAPI's thread startup) receives the
    // start routine and context; without one the start routine is called directly.
    // Both are stdcall.
    XboxThreadEnd end;
    if (start.system_routine) {
        const uint32_t args[] = {start.start_routine, start.start_context};
        end = run_on_xbox_stack(start.system_routine, *start.env, args);
    } else {
        const uint32_t args[] = {start.start_context};
        end = run_on_xbox_stack(start.start_routine, *start.env, args);
    }
    // Returning from the routine terminates the thread with STATUS_SUCCESS.
    if (end == XboxThreadEnd::Returned) start.object->mark_terminated(kStatusSuccess);
    LOG_DEBUG("thread", "thread %u ended, status %08x", start.object->id(), start.object->exit_status());
    t_thread_object.reset();
}

// BOOLEAN arguments arrive in 4-byte stack slots; only the low byte is defined.
uint32_t XB_STDCALL PsCreateSystemThreadEx(xbox::Handle* thread_handle, uint32_t thread_extension_size,
                                           uint32_t kernel_stack_size, uint32_t tls_data_size,
                                           uint32_t* thread_id, uint32_t start_routine, uint32_t start_context,
                                           uint32_t create_suspended, uint32_t debugger_thread,
                                           uint32_t system_routine) {
    (void)debugger_thread;
    if (thread_extension_size)
        LOG_WARN("thread", "ThreadExtensionSize %u is not supported, ignored", thread_extension_size);

    ThreadEnv* env = create_thread_env(kernel_stack_size, tls_data_size);
    if (!env) return xbox::kStatusNoMemory;

    auto object = std::make_shared<xbox::ThreadObject>(g_next_thread_id++, xbox::to_xbox_ptr(env->kthread));
    if (create_suspended & 0xFF) object->set_suspend_count(1);
    const xbox::Handle handle = xbox::insert_handle(object);
    *thread_handle = handle;
    if (thread_id) *thread_id = object->id();
    LOG_DEBUG("thread",
              "PsCreateSystemThreadEx(start=%08x context=%08x system=%08x stack=%x tls=%x%s) -> handle %x, id %u",
              start_routine, start_context, system_routine, kernel_stack_size, tls_data_size,
              (create_suspended & 0xFF) ? " suspended" : "", handle, object->id());

    std::thread(xbox_thread_main, ThreadStart{env, object, start_routine, start_context, system_routine}).detach();
    return kStatusSuccess;
}

uint32_t XB_STDCALL PsCreateSystemThread(xbox::Handle* thread_handle, uint32_t* thread_id, uint32_t start_routine,
                                         uint32_t start_context, uint32_t debugger_thread) {
    return PsCreateSystemThreadEx(thread_handle, 0, 0, 0, thread_id, start_routine, start_context, 0,
                                  debugger_thread, 0);
}

[[noreturn]] void XB_STDCALL PsTerminateSystemThread(uint32_t exit_status) {
    if (t_thread_object) t_thread_object->mark_terminated(exit_status);
    LOG_DEBUG("thread", "PsTerminateSystemThread(%08x)", exit_status);
    exit_xbox_thread();
}

const xbox::KernelImpl kLoaderImpls[] = {
    {"PsCreateSystemThread", reinterpret_cast<void*>(&PsCreateSystemThread)},
    {"PsCreateSystemThreadEx", reinterpret_cast<void*>(&PsCreateSystemThreadEx)},
    {"PsTerminateSystemThread", reinterpret_cast<void*>(&PsTerminateSystemThread)},
    {"XeLoadSection", reinterpret_cast<void*>(&XeLoadSection)},
    {"XeUnloadSection", reinterpret_cast<void*>(&XeUnloadSection)},
};

// --- Stubs for everything else ------------------------------------------------------

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

void* find_impl(const char* name) {
    for (std::span<const xbox::KernelImpl> impls :
         {std::span<const xbox::KernelImpl>(kLoaderImpls), seh_impls(), xbox::kernel_impls()})
        for (const auto& impl : impls)
            if (std::strcmp(impl.name, name) == 0) return impl.address;
    return nullptr;
}

void* zero_block() {
    static std::vector<std::unique_ptr<uint8_t[]>> blocks;
    blocks.push_back(std::make_unique<uint8_t[]>(4096));
    return blocks.back().get();
}

}  // namespace

int32_t initial_image_pages(const XbeImage& image) {
    int32_t pages = static_cast<int32_t>((image.header().size_of_headers + 0xFFF) / 0x1000);
    for (const XbeSectionHeader* section : image.sections())
        if (section->flags & 2) pages += section_pages(*section);  // preloaded
    return pages;
}

void install_kernel_thunks(XbeImage& image) {
    g_image = &image;
    auto* entry = reinterpret_cast<uint32_t*>(uintptr_t{image.kernel_thunk_table()});
    int implemented = 0, zeroed = 0, stubbed = 0;
    std::string missing;
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
        void* target = find_impl(exp->name);
        if (target) {
            ++implemented;
        } else if (exp->kind == xbox::ExportKind::Data) {
            target = zero_block();
            ++zeroed;
            LOG_DEBUG("kernel", "data export %s has no implementation, using zeros", exp->name);
        } else {
            target = reinterpret_cast<void*>(kStubs[ordinal]);
            ++stubbed;
            missing += missing.empty() ? "" : ", ";
            missing += exp->name;
        }
        *entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    }
    LOG_INFO("kernel", "%d imports: %d implemented, %d zero-filled data, %d not yet implemented",
             implemented + zeroed + stubbed, implemented, zeroed, stubbed);
    LOG_DEBUG("kernel", "not yet implemented: %s", missing.c_str());
}

}  // namespace loader
