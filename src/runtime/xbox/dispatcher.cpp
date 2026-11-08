// Dispatcher objects, waits, timers, DPCs and IRQL. See dispatcher.h.

#include "dispatcher.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

#include "compiler.h"
#include "kernel_internal.h"
#include "log.h"
#include "objects.h"

namespace xbox {

namespace {

using Clock = std::chrono::steady_clock;

using TimerQueue = std::multimap<Clock::time_point, XbKTimer*>;

struct UserApc {
    XbPtr routine;  // VOID NTAPI (*)(argument1, argument2, argument3)
    uint32_t arguments[3];
};

struct DispatcherState {
    // The dispatcher database lock: guards every object's dispatcher state, the
    // timer queue and the DPC queue. Never held while title code runs.
    std::mutex lock;
    std::condition_variable signaled;     // an object was signaled
    std::condition_variable kernel_wake;  // timer or DPC queue changed
    TimerQueue timer_queue;
    std::unordered_map<XbKTimer*, TimerQueue::iterator> timer_index;
    std::deque<XbKDpc*> dpc_queue;
    bool kernel_thread_started = false;
    std::mutex dispatch_level;  // DISPATCH_LEVEL exclusion (see dispatcher.h)
    // User-mode APCs waiting for their thread (by KTHREAD) to wait alertably.
    std::unordered_map<XbPtr, std::deque<UserApc>> user_apcs;
};
DispatcherState& g_state = immortal<DispatcherState>();

std::mutex& g_lock = g_state.lock;
std::condition_variable& g_signaled = g_state.signaled;
std::condition_variable& g_kernel_wake = g_state.kernel_wake;
TimerQueue& g_timer_queue = g_state.timer_queue;
auto& g_timer_index = g_state.timer_index;
auto& g_dpc_queue = g_state.dpc_queue;
bool& g_kernel_thread_started = g_state.kernel_thread_started;
std::mutex& g_dispatch_level = g_state.dispatch_level;
auto& g_user_apcs = g_state.user_apcs;
thread_local uint8_t t_irql = kPassiveLevel;  // for threads without a KPCR

// --- Time ---------------------------------------------------------------------------

// Xbox times are in 100ns units: negative = relative, positive = absolute system time.
Clock::time_point deadline_from(int64_t xbox_time) {
    constexpr int64_t kMaxUnits = 10'000'000LL * 60 * 60 * 24 * 365;  // a year
    const int64_t units = xbox_time < 0 ? -xbox_time : xbox_time - current_system_time();
    return Clock::now() + std::chrono::nanoseconds(std::clamp<int64_t>(units, 0, kMaxUnits) * 100);
}

void link_empty_list(XbPtr& flink, XbPtr& blink) {
    flink = blink = to_xbox_ptr(&flink);
}

void init_header(XbDispatcherHeader& header, uint8_t type, size_t size, int32_t signal_state) {
    header = {};
    header.type = type;
    header.size = static_cast<uint8_t>(size / 4);
    header.signal_state = signal_state;
    link_empty_list(header.wait_list_flink, header.wait_list_blink);
}

// --- Waiting (g_lock held) ------------------------------------------------------------

bool is_signaled(const XbDispatcherHeader& header, XbPtr thread) {
    if (header.type == kXbMutant)
        return header.signal_state > 0 || reinterpret_cast<const XbKMutant&>(header).owner_thread == thread;
    return header.signal_state > 0;
}

// Side effects of a successful wait, as in NT's KiWaitSatisfy*.
void satisfy(XbDispatcherHeader& header, XbPtr thread) {
    switch (header.type) {
        case kXbEventSynchronization:
        case kXbTimerSynchronization:
            header.signal_state = 0;
            break;
        case kXbSemaphore:
            --header.signal_state;
            break;
        case kXbMutant:
            if (--header.signal_state == 0) reinterpret_cast<XbKMutant&>(header).owner_thread = thread;
            break;
        default:
            break;
    }
}

// User-mode APCs run when their thread waits alertably in user mode, as in NT. The
// wait then ends with STATUS_USER_APC instead of waiting for its objects.
bool delivers_apcs(uint32_t wait_mode, uint32_t alertable) { return (alertable & 0xFF) && wait_mode == 1; }

// Runs the calling thread's queued user APCs (g_lock held on entry and exit, released
// while they run). Returns whether there were any.
bool deliver_user_apcs(std::unique_lock<std::mutex>& lock, XbPtr thread) {
    const auto it = g_user_apcs.find(thread);
    if (it == g_user_apcs.end() || it->second.empty()) return false;
    std::deque<UserApc> apcs = std::move(it->second);
    g_user_apcs.erase(it);
    lock.unlock();
    for (const UserApc& apc : apcs) {
        if (auto call_guest = kernel_config().call_guest)
            call_guest(apc.routine, apc.arguments, 3);
        else
            LOG_ERROR("kernel", "APC %08x cannot run: no call_guest", apc.routine);
    }
    lock.lock();
    return true;
}

// Waits for `objects` (none = just sleep until the timeout).
uint32_t wait_objects(std::span<XbDispatcherHeader* const> objects, bool wait_all, const int64_t* timeout,
                      bool alertable = false) {
    std::optional<Clock::time_point> deadline;
    if (timeout) deadline = deadline_from(*timeout);
    const XbPtr thread = current_kthread();
    std::unique_lock lock(g_lock);
    for (;;) {
        if (alertable && deliver_user_apcs(lock, thread)) return kStatusUserApc;
        if (objects.empty()) {
            // A sleep: only the timeout (or an APC) ends it.
        } else if (wait_all) {
            if (std::all_of(objects.begin(), objects.end(), [&](auto* o) { return is_signaled(*o, thread); })) {
                for (XbDispatcherHeader* o : objects) satisfy(*o, thread);
                return kStatusSuccess;
            }
        } else {
            for (size_t i = 0; i < objects.size(); ++i) {
                if (is_signaled(*objects[i], thread)) {
                    satisfy(*objects[i], thread);
                    return kStatusSuccess + static_cast<uint32_t>(i);  // STATUS_WAIT_0 + i
                }
            }
        }
        if (deadline && Clock::now() >= *deadline) return objects.empty() ? kStatusSuccess : kStatusTimeout;
        if (deadline)
            g_signaled.wait_until(lock, *deadline);
        else
            g_signaled.wait(lock);
    }
}

int32_t set_event(XbDispatcherHeader& event) {
    std::lock_guard lock(g_lock);
    const int32_t previous = event.signal_state;
    event.signal_state = 1;
    g_signaled.notify_all();
    return previous;
}

// --- IRQL -------------------------------------------------------------------------------

uint8_t& irql_ref() {
    if (auto current_irql = kernel_config().current_irql)
        if (uint8_t* irql = current_irql()) return *irql;
    return t_irql;
}

uint8_t raise_irql(uint8_t new_irql) {
    uint8_t& irql = irql_ref();
    const uint8_t old = irql;
    if (old < kDispatchLevel && new_irql >= kDispatchLevel) g_dispatch_level.lock();
    irql = new_irql;
    return old;
}

void lower_irql(uint8_t new_irql) {
    uint8_t& irql = irql_ref();
    if (irql >= kDispatchLevel && new_irql < kDispatchLevel) g_dispatch_level.unlock();
    irql = new_irql;
}

// --- Timers and DPCs (g_lock held) ---------------------------------------------------

void kernel_thread_main();

void ensure_kernel_thread() {
    if (g_kernel_thread_started) return;
    g_kernel_thread_started = true;
    std::thread(kernel_thread_main).detach();
}

bool remove_timer(XbKTimer& timer) {
    timer.header.inserted = 0;
    const auto it = g_timer_index.find(&timer);
    if (it == g_timer_index.end()) return false;
    g_timer_queue.erase(it->second);
    g_timer_index.erase(it);
    return true;
}

void insert_timer(XbKTimer& timer, Clock::time_point when) {
    g_timer_index[&timer] = g_timer_queue.emplace(when, &timer);
    timer.header.inserted = 1;
    ensure_kernel_thread();
    g_kernel_wake.notify_one();
}

bool queue_dpc(XbKDpc& dpc, XbPtr argument1, XbPtr argument2) {
    if (dpc.inserted) return false;
    dpc.inserted = 1;
    dpc.system_argument1 = argument1;
    dpc.system_argument2 = argument2;
    g_dpc_queue.push_back(&dpc);
    ensure_kernel_thread();
    g_kernel_wake.notify_one();
    return true;
}

void expire_timers(Clock::time_point now) {
    while (!g_timer_queue.empty() && g_timer_queue.begin()->first <= now) {
        const auto [when, timer] = *g_timer_queue.begin();
        remove_timer(*timer);
        timer->header.signal_state = 1;
        g_signaled.notify_all();
        if (timer->dpc) {
            const auto time = static_cast<uint64_t>(current_system_time());
            queue_dpc(*from_xbox_ptr<XbKDpc>(timer->dpc), static_cast<uint32_t>(time), static_cast<uint32_t>(time >> 32));
        }
        if (timer->period > 0) {
            const auto period = std::chrono::milliseconds(timer->period);
            insert_timer(*timer, std::max(when + period, now));
        }
    }
}

// Runs expired timers and queued DPCs. Like the Xbox's clock interrupt and DPC
// processing, all of it happens at DISPATCH_LEVEL: a thread that raised its IRQL
// never sees a timer expire or a queued DPC leave the queue until it lowers again.
// Lock order: dispatch level, then g_lock.
void kernel_thread_main() {
    if (auto attach = kernel_config().attach_kernel_thread) attach();
    for (;;) {
        {
            std::unique_lock lock(g_lock);
            if (g_dpc_queue.empty()) {
                if (g_timer_queue.empty())
                    g_kernel_wake.wait(lock);
                else
                    g_kernel_wake.wait_until(lock, g_timer_queue.begin()->first);
            }
        }
        const uint8_t old_irql = raise_irql(kDispatchLevel);
        std::unique_lock lock(g_lock);
        expire_timers(Clock::now());
        while (!g_dpc_queue.empty()) {
            XbKDpc* dpc = g_dpc_queue.front();
            g_dpc_queue.pop_front();
            dpc->inserted = 0;
            const uint32_t routine = dpc->deferred_routine;
            const uint32_t args[4] = {to_xbox_ptr(dpc), dpc->deferred_context, dpc->system_argument1,
                                      dpc->system_argument2};
            lock.unlock();
            if (auto call_guest = kernel_config().call_guest)
                call_guest(routine, args, 4);
            else
                LOG_ERROR("kernel", "DPC %08x cannot run: no call_guest", routine);
            lock.lock();
        }
        lock.unlock();
        lower_irql(old_irql);
    }
}

// --- Exports: IRQL ---------------------------------------------------------------------

// The IRQL arguments are UCHARs; fastcall passes them in ECX.
uint32_t XB_FASTCALL KfRaiseIrql(uint32_t new_irql) { return raise_irql(static_cast<uint8_t>(new_irql)); }

void XB_FASTCALL KfLowerIrql(uint32_t new_irql) { lower_irql(static_cast<uint8_t>(new_irql)); }

uint32_t XB_STDCALL KeRaiseIrqlToDpcLevel() { return raise_irql(kDispatchLevel); }

// --- Exports: DPCs and timers ------------------------------------------------------------

void XB_STDCALL KeInitializeDpc(XbKDpc* dpc, XbPtr deferred_routine, XbPtr deferred_context) {
    *dpc = {};
    dpc->type = kXbDpc;
    dpc->deferred_routine = deferred_routine;
    dpc->deferred_context = deferred_context;
    KTRACE("KeInitializeDpc(%08x, routine=%08x, context=%08x)", to_xbox_ptr(dpc), deferred_routine, deferred_context);
}

uint32_t XB_STDCALL KeInsertQueueDpc(XbKDpc* dpc, XbPtr argument1, XbPtr argument2) {
    std::lock_guard lock(g_lock);
    return queue_dpc(*dpc, argument1, argument2);
}

uint32_t XB_STDCALL KeRemoveQueueDpc(XbKDpc* dpc) {
    std::lock_guard lock(g_lock);
    if (!dpc->inserted) return 0;
    g_dpc_queue.erase(std::find(g_dpc_queue.begin(), g_dpc_queue.end(), dpc));
    dpc->inserted = 0;
    return 1;
}

void XB_STDCALL KeInitializeTimerEx(XbKTimer* timer, uint32_t timer_type) {
    std::lock_guard lock(g_lock);
    remove_timer(*timer);  // in case a set timer is initialized again
    *timer = {};
    init_header(timer->header, static_cast<uint8_t>(kXbTimerNotification + (timer_type & 0xFF)), sizeof(XbKTimer), 0);
    link_empty_list(timer->list_flink, timer->list_blink);
    KTRACE("KeInitializeTimerEx(%08x, %u)", to_xbox_ptr(timer), timer_type & 0xFF);
}

uint32_t XB_STDCALL KeSetTimerEx(XbKTimer* timer, int64_t due_time, int32_t period, XbKDpc* dpc) {
    std::lock_guard lock(g_lock);
    const bool was_set = remove_timer(*timer);
    timer->header.signal_state = 0;
    timer->due_time = static_cast<uint64_t>(due_time);
    timer->dpc = to_xbox_ptr(dpc);
    timer->period = period;
    insert_timer(*timer, deadline_from(due_time));
    KTRACE("KeSetTimerEx(%08x, due=%lld, period=%d, dpc=%08x)", to_xbox_ptr(timer), static_cast<long long>(due_time),
           period, to_xbox_ptr(dpc));
    return was_set;
}

uint32_t XB_STDCALL KeSetTimer(XbKTimer* timer, int64_t due_time, XbKDpc* dpc) {
    return KeSetTimerEx(timer, due_time, 0, dpc);
}

uint32_t XB_STDCALL KeCancelTimer(XbKTimer* timer) {
    std::lock_guard lock(g_lock);
    return remove_timer(*timer);
}

// --- Exports: waits ------------------------------------------------------------------------

uint32_t XB_STDCALL KeWaitForSingleObject(XbDispatcherHeader* object, uint32_t wait_reason, uint32_t wait_mode,
                                          uint32_t alertable, int64_t* timeout) {
    (void)wait_reason;
    XbDispatcherHeader* const objects[] = {object};
    return wait_objects(objects, false, timeout, delivers_apcs(wait_mode, alertable));
}

uint32_t XB_STDCALL KeWaitForMultipleObjects(uint32_t count, XbPtr* objects, uint32_t wait_type, uint32_t wait_reason,
                                             uint32_t wait_mode, uint32_t alertable, int64_t* timeout,
                                             XbPtr wait_block_array) {
    (void)wait_reason, (void)wait_block_array;
    std::vector<XbDispatcherHeader*> headers(count);
    for (uint32_t i = 0; i < count; ++i) headers[i] = from_xbox_ptr<XbDispatcherHeader>(objects[i]);
    return wait_objects(headers, wait_type == 0 /* WaitAll */, timeout, delivers_apcs(wait_mode, alertable));
}

uint32_t XB_STDCALL NtWaitForSingleObjectEx(Handle handle, uint32_t wait_mode, uint32_t alertable, int64_t* timeout) {
    const std::shared_ptr<Object> object = lookup_handle(handle);  // keeps the body alive while waiting
    if (!object) return kStatusInvalidHandle;
    if (!object->wait_target()) {
        LOG_WARN("kernel", "wait on handle %x, which is not waitable", handle);
        return kStatusObjectTypeMismatch;
    }
    XbDispatcherHeader* const objects[] = {from_xbox_ptr<XbDispatcherHeader>(object->wait_target())};
    return wait_objects(objects, false, timeout, delivers_apcs(wait_mode, alertable));
}

uint32_t XB_STDCALL NtWaitForSingleObject(Handle handle, uint32_t alertable, int64_t* timeout) {
    return NtWaitForSingleObjectEx(handle, 1 /* UserMode */, alertable, timeout);
}

// Returns STATUS_SUCCESS after the interval, or STATUS_USER_APC if APCs ran first.
uint32_t XB_STDCALL KeDelayExecutionThread(uint32_t wait_mode, uint32_t alertable, int64_t* interval) {
    return wait_objects({}, false, interval, delivers_apcs(wait_mode, alertable));
}

// The APC routine XAPI's ReadFileEx/WriteFileEx pass to NtReadFile/NtWriteFile, with
// the caller's completion routine as context. Calls
// completion(error, bytes transferred, OVERLAPPED*); the OVERLAPPED starts with the
// IO_STATUS_BLOCK.
void XB_STDCALL NtUserIoApcDispatcher(XbPtr apc_context, XbIoStatusBlock* io_status, uint32_t reserved) {
    (void)reserved;
    const uint32_t error = (io_status->status & 0x80000000) ? nt_status_to_dos_error(io_status->status) : 0;
    const uint32_t args[3] = {error, io_status->information, to_xbox_ptr(io_status)};
    if (auto call_guest = kernel_config().call_guest) call_guest(apc_context, args, 3);
}

uint32_t XB_STDCALL NtYieldExecution() {
    std::this_thread::yield();
    return kStatusSuccess;
}

// --- Exports: events and mutants -----------------------------------------------------------

void warn_if_named(const XbObjectAttributes* attributes, const char* what) {
    if (attributes && attributes->object_name)
        LOG_WARN("kernel", "named %s requested; names are not supported yet, creating an unnamed one", what);
}

int32_t XB_STDCALL KeSetEvent(XbKEvent* event, int32_t increment, uint32_t wait) {
    (void)increment, (void)wait;
    return set_event(event->header);
}

uint32_t XB_STDCALL NtCreateEvent(Handle* event_handle, XbObjectAttributes* attributes, uint32_t event_type,
                                  uint32_t initial_state) {
    event_type &= 0xFF;
    if (event_type != kXbEventNotification && event_type != kXbEventSynchronization) return kStatusInvalidParameter;
    warn_if_named(attributes, "event");
    auto object = std::make_shared<BodyObject>(sizeof(XbKEvent), object_types::event);
    init_header(object->as<XbKEvent>()->header, static_cast<uint8_t>(event_type), sizeof(XbKEvent),
                (initial_state & 0xFF) ? 1 : 0);
    *event_handle = insert_handle(object);
    KTRACE("NtCreateEvent(type=%u, initial=%u) -> %x", event_type, initial_state & 0xFF, *event_handle);
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtSetEvent(Handle handle, int32_t* previous_state) {
    const std::shared_ptr<Object> object = lookup_handle(handle);
    if (!object) return kStatusInvalidHandle;
    auto* header = from_xbox_ptr<XbDispatcherHeader>(object->body());
    if (!header || header->type > kXbEventSynchronization) return kStatusObjectTypeMismatch;
    const int32_t previous = set_event(*header);
    if (previous_state) *previous_state = previous;
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtCreateMutant(Handle* mutant_handle, XbObjectAttributes* attributes, uint32_t initial_owner) {
    warn_if_named(attributes, "mutant");
    const bool owned = initial_owner & 0xFF;
    auto object = std::make_shared<BodyObject>(sizeof(XbKMutant), object_types::mutant);
    auto* mutant = object->as<XbKMutant>();
    init_header(mutant->header, kXbMutant, sizeof(XbKMutant), owned ? 0 : 1);
    link_empty_list(mutant->list_flink, mutant->list_blink);
    mutant->owner_thread = owned ? current_kthread() : 0;
    *mutant_handle = insert_handle(object);
    KTRACE("NtCreateMutant(owned=%d) -> %x", owned, *mutant_handle);
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtReleaseMutant(Handle handle, int32_t* previous_count) {
    const std::shared_ptr<Object> object = lookup_handle(handle);
    if (!object) return kStatusInvalidHandle;
    auto* mutant = from_xbox_ptr<XbKMutant>(object->body());
    if (!mutant || mutant->header.type != kXbMutant) return kStatusObjectTypeMismatch;
    std::lock_guard lock(g_lock);
    if (mutant->owner_thread != current_kthread()) return kStatusMutantNotOwned;
    const int32_t previous = mutant->header.signal_state++;
    if (mutant->header.signal_state == 1) {
        mutant->owner_thread = 0;
        g_signaled.notify_all();
    }
    if (previous_count) *previous_count = previous;
    return kStatusSuccess;
}

const KernelImpl kDispatcherImpls[] = {
    KIMPL(KeCancelTimer),
    KIMPL(KeDelayExecutionThread),
    KIMPL(KeInitializeDpc),
    KIMPL(KeInitializeTimerEx),
    KIMPL(KeInsertQueueDpc),
    KIMPL(KeRaiseIrqlToDpcLevel),
    KIMPL(KeRemoveQueueDpc),
    KIMPL(KeSetEvent),
    KIMPL(KeSetTimer),
    KIMPL(KeSetTimerEx),
    KIMPL(KeWaitForMultipleObjects),
    KIMPL(KeWaitForSingleObject),
    KIMPL(KfLowerIrql),
    KIMPL(KfRaiseIrql),
    KIMPL(NtCreateEvent),
    KIMPL(NtCreateMutant),
    KIMPL(NtReleaseMutant),
    KIMPL(NtSetEvent),
    KIMPL(NtUserIoApcDispatcher),
    KIMPL(NtWaitForSingleObject),
    KIMPL(NtWaitForSingleObjectEx),
    KIMPL(NtYieldExecution),
};

}  // namespace

void set_event_handle(Handle event, int32_t state) {
    if (!event) return;
    const std::shared_ptr<Object> object = lookup_handle(event);
    if (object && object->body()) signal_object(*from_xbox_ptr<XbDispatcherHeader>(object->body()), state);
}

void queue_user_apc(XbPtr thread, XbPtr routine, uint32_t argument1, uint32_t argument2, uint32_t argument3) {
    std::lock_guard lock(g_lock);
    g_user_apcs[thread].push_back({routine, {argument1, argument2, argument3}});
    g_signaled.notify_all();  // an alertable waiter may be the target
}

void signal_object(XbDispatcherHeader& header, int32_t signal_state) {
    std::lock_guard lock(g_lock);
    header.signal_state = signal_state;
    g_signaled.notify_all();
}

std::span<const KernelImpl> dispatcher_kernel_impls() { return kDispatcherImpls; }

uint8_t raise_irql_to(uint8_t new_irql) { return raise_irql(new_irql); }
void lower_irql_to(uint8_t old_irql) { lower_irql(old_irql); }

}  // namespace xbox
