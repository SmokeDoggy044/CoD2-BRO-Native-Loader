#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace loader {

// What Xbox code finds through FS. On the Xbox, FS points at the processor control
// region (KPCR). Its first part is an NT_TIB, laid out exactly like on Windows.
struct NtTib {
    uint32_t exception_list;  // SEH chain head, 0xFFFFFFFF = end
    uint32_t stack_base;
    uint32_t stack_limit;
    uint32_t sub_system_tib;
    uint32_t fiber_data;
    uint32_t arbitrary_user_pointer;
    uint32_t self;  // linear address of this NT_TIB
};

struct Kpcr {
    NtTib tib;
    uint32_t self_pcr;  // 0x1C
    uint32_t prcb;      // 0x20: -> prcb_data
    uint8_t irql;       // 0x24
    uint8_t pad[3];
    // KPRCB. Offset 0 (FS:[0x28]) is CurrentThread; the rest is zero until a field
    // turns out to matter (the game reads PRCB+0x24C/+0x250 and FS:[0x58]).
    uint32_t prcb_data[0xC0];
};
static_assert(offsetof(Kpcr, prcb) == 0x20);
static_assert(offsetof(Kpcr, irql) == 0x24);
static_assert(offsetof(Kpcr, prcb_data) == 0x28);

// Offsets into KTHREAD (see xboxkrnl.h).
constexpr size_t kKThreadStackBase = 0x1C;
constexpr size_t kKThreadStackLimit = 0x20;
constexpr size_t kKThreadTlsData = 0x28;
constexpr size_t kKThreadSize = 0x200;

struct ThreadEnv {
    Kpcr* kpcr;
    uint8_t* kthread;
    uint8_t* tls;          // TlsDataSize bytes at the top of the stack, or null
    uint32_t stack_base;   // highest address (exclusive); the TLS block ends here
    uint32_t stack_limit;  // lowest usable address
    uint32_t initial_esp;  // just below the TLS block, 16-byte aligned
    uint32_t depth;        // nesting of run_on_xbox_stack calls on this thread
    void* exit_jump[5];    // __builtin_setjmp buffer used by exit_xbox_thread
    // The title's SEH chain ends at this record: 0xFFFFFFFF in the KPCR (Linux), the
    // host thread's own handlers in the TEB (Windows).
    uint32_t seh_chain_end;
    uint32_t host_stack[2];  // Windows: the TEB's StackBase/StackLimit outside Xbox code
};

// Creates the Xbox-side state for one thread: its stack and, as the Xbox kernel
// does, a zeroed `tls_size`-byte block at the top of that stack (KTHREAD.TlsData).
// Code finds TLS at [StackBase + tls_index * 4] (tls_index is negative), so the
// block has to end exactly at StackBase. XAPI's thread startup fills it in.
// `tls_size` 0 gives TlsData = null, as for the title's initial thread.
ThreadEnv* create_thread_env(uint32_t stack_size, uint32_t tls_size);

// Points FS at `env` for the calling thread. Must be called on every thread that
// runs Xbox code.
bool activate_thread_env(ThreadEnv* env);

// The calling thread's environment, or null for threads that never run Xbox code.
ThreadEnv* current_thread_env();

// The NT_TIB title code reaches through FS: the KPCR's on Linux, the thread's own
// TEB on Windows (same layout; the title only uses its SEH chain and StackBase).
NtTib& xbox_tib();

// Where title code reads KPCR.Irql (FS:[0x24]) on the calling thread, or null; the
// kernel mirrors the IRQL there.
uint8_t* current_irql_location();

class XbeImage;

// Windows: FS is the TEB, whose fields past the NT_TIB differ from the KPCR's, so
// title code that reads those KPCR fields through FS is redirected to TEB TLS slots
// holding the same values. Nothing to do on Linux. Before any title code runs.
bool prepare_fs_access(const XbeImage& image);

// Switches to the thread's Xbox stack and calls `entry` with `args` (pushed as for
// stdcall/cdecl; the stack pointer is restored afterwards either way). Returns when
// `entry` returns or the thread calls exit_xbox_thread(); the result says which.
// Called again while Xbox code runs on this thread (a kernel export calling back into
// the title), it calls `entry` on the current stack instead.
enum class XboxThreadEnd { Returned, Exited };
XboxThreadEnd run_on_xbox_stack(uint32_t entry, ThreadEnv& env, std::span<const uint32_t> args);

// Leaves the Xbox code running on the calling thread: unwinds (without destructors)
// back to its outermost run_on_xbox_stack call. For PsTerminateSystemThread.
[[noreturn]] void exit_xbox_thread();

namespace detail {
// Per platform: point FS at what title code expects for `env` (activate_thread_env).
bool activate_fs(ThreadEnv& env);
// Per platform, around the outermost run_on_xbox_stack call of a thread.
void enter_xbox_stack(ThreadEnv& env);
void leave_xbox_stack(ThreadEnv& env);
}  // namespace detail

}  // namespace loader
