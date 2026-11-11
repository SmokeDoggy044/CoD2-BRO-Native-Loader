#pragma once

// The title's waitable objects without a kernel (own engine host step 5c): events, mutexes and threads in a handle
// table of our own, waits on host condition variables, and each thread's queue of completion routines (what the
// Xbox runs as user APCs at an alertable wait). Used by our XAPI C (title_xapi.cpp) when native_platform() is on.
//
// Results are the kernel's NTSTATUS values, so the XAPI wrappers above keep their error handling: STATUS_WAIT_0 + i,
// STATUS_TIMEOUT (0x102), STATUS_USER_APC (0xc0), STATUS_INVALID_HANDLE (0xc0000008),
// STATUS_MUTANT_NOT_OWNED (0xc0000046), STATUS_OBJECT_TYPE_MISMATCH (0xc0000024).
//
// Record/replay: a timed wait on the main thread keeps its outcome in the recording ('W'), exactly where the
// kernel's did, so recordings made with the loader replay on these objects.

#include <cstddef>
#include <cstdint>

namespace game::objects {

// Our handles: 0x7e000000 + 4n, a range the loader's kernel never hands out (it counts up from 4).
constexpr uint32_t kFirstHandle = 0x7e000000;
bool is_ours(uint32_t handle);

uint32_t create_event(bool manual_reset, bool initial_state);
uint32_t create_mutex(bool initially_owned);

// A thread running `body(routine, context)` (XAPI's start-up around the title's routine); its handle is signaled
// with the result when the body returns. 0 when the host cannot start it.
using ThreadBody = uint32_t(uint32_t routine, uint32_t context);
uint32_t create_thread(uint32_t stack_size, uint32_t tls_size, ThreadBody* body, uint32_t routine, uint32_t context,
                       uint32_t* thread_id);
// The calling thread's routine returned: signals its handle (from the thread's own entry).
void thread_exited(uint32_t handle, uint32_t exit_code);

uint32_t set_event(uint32_t handle, bool state);
uint32_t release_mutex(uint32_t handle);
uint32_t close(uint32_t handle);

// `timeout_ms` null = no timeout. Without handles (count 0) this is a sleep: STATUS_SUCCESS at the timeout.
uint32_t wait(const uint32_t* handles, size_t count, bool wait_all, const uint32_t* timeout_ms, bool alertable);

// A completion routine for the calling thread: runs as routine(a1, a2, a3) (stdcall) at its next alertable wait.
void queue_apc(uint32_t routine, uint32_t a1, uint32_t a2, uint32_t a3);

}  // namespace game::objects
