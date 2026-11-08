// Synchronization exports: critical sections.
//
// Critical sections work on the title's RTL_CRITICAL_SECTION in place, the way the
// NT/Xbox kernel does: LockCount counts the owner plus waiters (-1 = free), and
// contended threads wait on the embedded synchronization event. Here the event is
// just its SignalState field, waited on with std::atomic_ref (a futex on Linux).

#include <atomic>

#include "compiler.h"
#include "kernel_internal.h"
#include "types.h"

namespace xbox {

namespace {

using Ref = std::atomic_ref<int32_t>;
using PtrRef = std::atomic_ref<XbPtr>;

static_assert(Ref::required_alignment <= 4 && PtrRef::required_alignment <= 4);

// Auto-reset event: wait until signaled, consuming the signal.
void wait_event(XbDispatcherHeader& event) {
    Ref state(event.signal_state);
    for (;;) {
        int32_t current = state.load();
        if (current > 0 && state.compare_exchange_weak(current, 0)) return;
        if (current <= 0) state.wait(current);
    }
}

void signal_event(XbDispatcherHeader& event) {
    Ref state(event.signal_state);
    state.store(1);
    state.notify_one();
}

void XB_STDCALL RtlInitializeCriticalSection(XbCriticalSection* cs) {
    cs->synchronization = {};
    cs->synchronization.type = 1;  // SynchronizationEvent
    cs->synchronization.size = sizeof(XbDispatcherHeader) / 4;
    const XbPtr list_head = to_xbox_ptr(&cs->synchronization.wait_list_flink);
    cs->synchronization.wait_list_flink = list_head;
    cs->synchronization.wait_list_blink = list_head;
    cs->lock_count = -1;
    cs->recursion_count = 0;
    cs->owning_thread = 0;
    KTRACE("RtlInitializeCriticalSection(%08x)", to_xbox_ptr(cs));
}

void XB_STDCALL RtlEnterCriticalSection(XbCriticalSection* cs) {
    const XbPtr thread = current_kthread();
    if (Ref(cs->lock_count).fetch_add(1) != -1) {
        // Held already. Only the owner itself can see its own id here.
        if (PtrRef(cs->owning_thread).load(std::memory_order_relaxed) == thread) {
            ++cs->recursion_count;
            return;
        }
        wait_event(cs->synchronization);
    }
    PtrRef(cs->owning_thread).store(thread, std::memory_order_relaxed);
    cs->recursion_count = 1;
}

void XB_STDCALL RtlLeaveCriticalSection(XbCriticalSection* cs) {
    if (--cs->recursion_count != 0) {
        Ref(cs->lock_count).fetch_sub(1);
        return;
    }
    PtrRef(cs->owning_thread).store(0, std::memory_order_relaxed);
    // Still >= 0 afterwards means someone is waiting: hand the section over.
    if (Ref(cs->lock_count).fetch_sub(1) - 1 >= 0) signal_event(cs->synchronization);
}

const KernelImpl kSyncImpls[] = {
    KIMPL(RtlEnterCriticalSection),
    KIMPL(RtlInitializeCriticalSection),
    KIMPL(RtlLeaveCriticalSection),
};

}  // namespace

std::span<const KernelImpl> sync_kernel_impls() { return kSyncImpls; }

}  // namespace xbox
