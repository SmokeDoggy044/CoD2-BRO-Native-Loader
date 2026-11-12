#include "replay.h"
#include "kernel_thunks.h"
#include "seh.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <mutex>
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
#include "vmem.h"
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

// request_title_restart -> wait_for_title_restart.
struct Restart {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<uint8_t> page;
    bool requested = false;  // the requesting thread is leaving the title
    bool ready = false;      // ... and has left it
};
Restart& g_restart = *new Restart;
thread_local bool t_restarting = false;

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
    // The first thread the entry point starts is the title's main thread (record/replay follows only its reads).
    if (start.object->id() == 1) xbox::replay::bind_current_thread();
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
    if (t_restarting) {
        std::lock_guard lock(g_restart.mutex);
        g_restart.ready = true;
        g_restart.changed.notify_all();
    }
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

// Resumes a thread created suspended (the only way a thread gets suspended here).
uint32_t XB_STDCALL NtResumeThread(xbox::Handle handle, uint32_t* previous_suspend_count) {
    const std::shared_ptr<xbox::Object> object = xbox::lookup_handle(handle);
    if (!object) return xbox::kStatusInvalidHandle;
    const auto thread = std::dynamic_pointer_cast<xbox::ThreadObject>(object);
    if (!thread) return xbox::kStatusObjectTypeMismatch;
    const uint32_t previous = thread->resume();
    if (previous_suspend_count) *previous_suspend_count = previous;
    LOG_DEBUG("thread", "NtResumeThread(thread %u): suspend count was %u", thread->id(), previous);
    return xbox::kStatusSuccess;
}

}  // namespace

// A title thread for our C (game::TitleHost::start_thread): the same FS block, stack and TLS block as a kernel
// thread, but no kernel handle - our own object table keeps the thread's handle.
bool start_title_thread(uint32_t stack_size, uint32_t tls_size, uint32_t routine, uint32_t context,
                        uint32_t* thread_id) {
    ThreadEnv* env = create_thread_env(stack_size, tls_size);
    if (!env) return false;
    auto object = std::make_shared<xbox::ThreadObject>(g_next_thread_id++, xbox::to_xbox_ptr(env->kthread));
    if (thread_id) *thread_id = object->id();
    LOG_DEBUG("thread", "title thread (routine=%08x context=%08x stack=%x tls=%x) -> id %u", routine, context,
              stack_size, tls_size, object->id());
    std::thread(xbox_thread_main, ThreadStart{env, object, routine, context, 0}).detach();
    return true;
}

void request_title_restart(const uint8_t* page, uint32_t size) {
    {
        std::lock_guard lock(g_restart.mutex);
        g_restart.page.assign(page, page + size);
        g_restart.requested = true;
    }
    t_restarting = true;
    exit_xbox_thread();  // back to xbox_thread_main, which tells the start thread
}

std::vector<uint8_t> wait_for_title_restart() {
    std::unique_lock lock(g_restart.mutex);
    g_restart.changed.wait(lock, [] { return g_restart.ready; });
    g_restart.ready = g_restart.requested = false;
    return std::move(g_restart.page);
}

void reset_thread_ids() { g_next_thread_id = 1; }

namespace {

const xbox::KernelImpl kLoaderImpls[] = {
    {"NtResumeThread", reinterpret_cast<void*>(&NtResumeThread)},
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

// XB_KERNEL_COUNT=FILE (developer): every function import goes through a stub that counts its calls and keeps
// its last caller; FILE is rewritten every 2 s with "name calls last_caller". Shows which kernel exports our C
// and the remaining original code still use (own engine host, 0.3).
struct CountSlot {
    uint32_t calls;
    uint32_t last_caller;
    uint32_t target;
    const char* name;
};

void install_counting(uint32_t* first, const char* file) {
    size_t n = 0;
    while (first[n]) ++n;
    auto* slots = new CountSlot[n]{};
    constexpr size_t kStub = 3 + 5 + 7 + 6;
    const size_t code_size = (n * kStub + 0xfff) & ~size_t{0xfff};
    auto* code = static_cast<uint8_t*>(platform::vmem::reserve(0, code_size));
    if (!code || !platform::vmem::commit(code, code_size, platform::vmem::Protection::ReadWriteExecute)) return;
    auto put32 = [](uint8_t*& at, uintptr_t value) {
        const uint32_t v = static_cast<uint32_t>(value);
        std::memcpy(at, &v, 4);
        at += 4;
    };
    for (size_t i = 0; i < n; ++i) {
        // The table holds addresses now; the name comes from the implementation below.
        slots[i].target = first[i];
        slots[i].name = "?";
        uint8_t* at = code + i * kStub;
        *at++ = 0x8b; *at++ = 0x04; *at++ = 0x24;                // mov eax, [esp]
        *at++ = 0xa3; put32(at, reinterpret_cast<uintptr_t>(&slots[i].last_caller));   // mov [last], eax
        *at++ = 0xf0; *at++ = 0xff; *at++ = 0x05; put32(at, reinterpret_cast<uintptr_t>(&slots[i].calls));  // lock inc
        *at++ = 0xff; *at++ = 0x25; put32(at, reinterpret_cast<uintptr_t>(&slots[i].target));  // jmp [target]
    }
    for (std::span<const xbox::KernelImpl> impls :
         {std::span<const xbox::KernelImpl>(kLoaderImpls), seh_impls(), xbox::kernel_impls()})
        for (const auto& impl : impls)
            for (size_t i = 0; i < n; ++i)
                if (slots[i].target == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(impl.address)))
                    slots[i].name = impl.name;
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(slots[i].name, "?") != 0)  // data exports and stubs stay as they are
            first[i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(code + i * kStub));
    std::thread([slots, n, path = std::string(file)] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (std::FILE* out = std::fopen(path.c_str(), "w")) {
                for (size_t i = 0; i < n; ++i)
                    if (slots[i].calls)
                        std::fprintf(out, "%s %u %08x\n", slots[i].name, slots[i].calls, slots[i].last_caller);
                std::fclose(out);
            }
        }
    }).detach();
    LOG_INFO("kernel", "counting kernel calls into %s", file);
}

}  // namespace

int32_t initial_image_pages(const XbeImage& image) {
    int32_t pages = static_cast<int32_t>((image.header().size_of_headers + 0xFFF) / 0x1000);
    for (const XbeSectionHeader* section : image.sections())
        if (section->flags & 2) pages += section_pages(*section);  // preloaded
    return pages;
}

void install_kernel_thunks(XbeImage& image, bool host) {
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
        void* target = host ? nullptr : find_impl(exp->name);
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
    if (const char* count = std::getenv("XB_KERNEL_COUNT"); count && *count)
        install_counting(reinterpret_cast<uint32_t*>(uintptr_t{image.kernel_thunk_table()}), count);
}

}  // namespace loader
