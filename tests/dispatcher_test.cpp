// Waits, events, mutants, timers, DPCs and IRQL (src/runtime/xbox/dispatcher.cpp), called through
// the export table the way the title calls them.
//
// Timing checks have strict lower bounds (firing early is a bug) and loose upper
// bounds (a busy machine may be late).

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "dispatcher.h"
#include "test_support.h"
#include "types.h"

using namespace xbox;
using namespace std::chrono;

namespace {

constexpr int64_t kMs = -10000;  // relative 100ns units per millisecond

using KeSetEventFn = int32_t(XB_STDCALL*)(XbKEvent*, int32_t, uint32_t);
using KeWaitFn = uint32_t(XB_STDCALL*)(void*, uint32_t, uint32_t, uint32_t, int64_t*);
using KeWaitMultipleFn = uint32_t(XB_STDCALL*)(uint32_t, XbPtr*, uint32_t, uint32_t, uint32_t, uint32_t, int64_t*,
                                               XbPtr);
using KeInitializeDpcFn = void(XB_STDCALL*)(XbKDpc*, XbPtr, XbPtr);
using KeInitializeTimerExFn = void(XB_STDCALL*)(XbKTimer*, uint32_t);
using KeSetTimerExFn = uint32_t(XB_STDCALL*)(XbKTimer*, int64_t, int32_t, XbKDpc*);
using KeCancelTimerFn = uint32_t(XB_STDCALL*)(XbKTimer*);
using KeInsertQueueDpcFn = uint32_t(XB_STDCALL*)(XbKDpc*, XbPtr, XbPtr);
using KeRaiseIrqlToDpcLevelFn = uint32_t(XB_STDCALL*)();
using KfLowerIrqlFn = void(XB_FASTCALL*)(uint32_t);
using KeQuerySystemTimeFn = void(XB_STDCALL*)(int64_t*);
using NtCreateMutantFn = uint32_t(XB_STDCALL*)(uint32_t*, XbObjectAttributes*, uint32_t);
using NtReleaseMutantFn = uint32_t(XB_STDCALL*)(uint32_t, int32_t*);
using NtWaitFn = uint32_t(XB_STDCALL*)(uint32_t, uint32_t, int64_t*);
using NtCreateEventFn = uint32_t(XB_STDCALL*)(uint32_t*, XbObjectAttributes*, uint32_t, uint32_t);
using NtSetEventFn = uint32_t(XB_STDCALL*)(uint32_t, int32_t*);
using KeDelayFn = uint32_t(XB_STDCALL*)(uint32_t, uint32_t, int64_t*);

KeSetEventFn KeSetEvent;
KeWaitFn KeWait;

XbKEvent g_dpc_event;
std::atomic<int> g_dpc_count{0};
std::atomic<bool> g_dispatch_level_held{false};
std::atomic<int> g_dpc_while_held{0};
std::atomic<uint8_t> g_dpc_irql{0};

// DPC routine: (dpc, context, argument1, argument2). Context 1 = also set g_dpc_event.
void dpc_routine(const uint32_t* args, uint32_t) {
    if (g_dispatch_level_held) ++g_dpc_while_held;
    g_dpc_irql = test::t_irql;
    ++g_dpc_count;
    if (args[1] == 1) KeSetEvent(&g_dpc_event, 0, 0);
}

std::atomic<int> g_apc_runs{0};
std::atomic<uint32_t> g_apc_thread{0};
uint32_t g_apc_args[3];

void apc_routine(const uint32_t* args, uint32_t) {
    std::copy(args, args + 3, g_apc_args);
    g_apc_thread = test::t_kthread;
    ++g_apc_runs;
}

void make_event(XbKEvent& event, uint8_t type) {
    event = {};
    event.header.type = type;
    event.header.size = 4;
}

long long elapsed_ms(steady_clock::time_point start) {
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
}

}  // namespace

int main() {
    test::init_kernel();
    KeSetEvent = test::find_export<KeSetEventFn>("KeSetEvent");
    KeWait = test::find_export<KeWaitFn>("KeWaitForSingleObject");
    const auto KeWaitMultiple = test::find_export<KeWaitMultipleFn>("KeWaitForMultipleObjects");
    const auto KeInitializeDpc = test::find_export<KeInitializeDpcFn>("KeInitializeDpc");
    const auto KeInitializeTimerEx = test::find_export<KeInitializeTimerExFn>("KeInitializeTimerEx");
    const auto KeSetTimerEx = test::find_export<KeSetTimerExFn>("KeSetTimerEx");
    const auto KeCancelTimer = test::find_export<KeCancelTimerFn>("KeCancelTimer");
    const auto KeInsertQueueDpc = test::find_export<KeInsertQueueDpcFn>("KeInsertQueueDpc");
    const auto KeRaiseIrqlToDpcLevel = test::find_export<KeRaiseIrqlToDpcLevelFn>("KeRaiseIrqlToDpcLevel");
    const auto KfLowerIrql = test::find_export<KfLowerIrqlFn>("KfLowerIrql");
    const auto KeQuerySystemTime = test::find_export<KeQuerySystemTimeFn>("KeQuerySystemTime");
    const auto NtCreateMutant = test::find_export<NtCreateMutantFn>("NtCreateMutant");
    const auto NtReleaseMutant = test::find_export<NtReleaseMutantFn>("NtReleaseMutant");
    const auto NtWait = test::find_export<NtWaitFn>("NtWaitForSingleObject");
    const auto NtCreateEvent = test::find_export<NtCreateEventFn>("NtCreateEvent");
    const auto NtSetEvent = test::find_export<NtSetEventFn>("NtSetEvent");
    const auto KeDelayExecutionThread = test::find_export<KeDelayFn>("KeDelayExecutionThread");
    const XbPtr dpc_routine_id = test::register_guest(1, dpc_routine);
    const XbPtr apc_routine_id = test::register_guest(2, apc_routine);
    int64_t zero = 0;
    int64_t one_second = 1000 * kMs;

    // Timeouts: a relative timeout waits at least that long; zero only polls.
    {
        XbKEvent event;
        make_event(event, kXbEventSynchronization);
        int64_t timeout = 30 * kMs;
        const auto start = steady_clock::now();
        CHECK(KeWait(&event, 0, 0, 0, &timeout) == kStatusTimeout);
        const long long ms = elapsed_ms(start);
        CHECK(ms >= 29 && ms < 1000, "took %lld ms", ms);
        CHECK(KeWait(&event, 0, 0, 0, &zero) == kStatusTimeout);
    }

    // Synchronization events: ping-pong between two threads, auto-reset.
    {
        XbKEvent ping, pong;
        make_event(ping, kXbEventSynchronization);
        make_event(pong, kXbEventSynchronization);
        std::thread partner([&] {
            test::attach_thread();
            for (int i = 0; i < 20000; ++i) {
                KeWait(&ping, 0, 0, 0, nullptr);
                KeSetEvent(&pong, 0, 0);
            }
        });
        for (int i = 0; i < 20000; ++i) {
            KeSetEvent(&ping, 0, 0);
            KeWait(&pong, 0, 0, 0, nullptr);
        }
        partner.join();
        CHECK(ping.header.signal_state == 0 && pong.header.signal_state == 0);
    }

    // One-shot relative timer with a DPC; a notification timer stays signaled.
    XbKTimer timer;
    XbKDpc event_dpc;
    {
        make_event(g_dpc_event, kXbEventSynchronization);
        KeInitializeTimerEx(&timer, 0);
        KeInitializeDpc(&event_dpc, dpc_routine_id, 1);
        const auto start = steady_clock::now();
        CHECK(KeSetTimerEx(&timer, 50 * kMs, 0, &event_dpc) == 0, "timer was not set before");
        CHECK(KeWait(&g_dpc_event, 0, 0, 0, &one_second) == 0, "DPC did not run");
        const long long ms = elapsed_ms(start);
        CHECK(ms >= 49 && ms < 1000, "fired after %lld ms", ms);
        CHECK(g_dpc_irql == 2, "DPC ran at IRQL %u", unsigned{g_dpc_irql});
        CHECK(timer.header.signal_state == 1 && timer.header.inserted == 0);
        CHECK(KeWait(&timer, 0, 0, 0, &zero) == 0 && timer.header.signal_state == 1);
    }

    // Absolute due time (system time + 40 ms) on a synchronization timer.
    {
        XbKTimer absolute;
        KeInitializeTimerEx(&absolute, 1);
        int64_t now = 0;
        KeQuerySystemTime(&now);
        const auto start = steady_clock::now();
        KeSetTimerEx(&absolute, now + 40 * 10000, 0, nullptr);
        CHECK(KeWait(&absolute, 0, 0, 0, &one_second) == 0);
        const long long ms = elapsed_ms(start);
        CHECK(ms >= 35 && ms < 1000, "fired after %lld ms", ms);
        CHECK(absolute.header.signal_state == 0, "synchronization timer did not reset");
    }

    // Periodic timer: fires repeatedly, never more often than its period, stops on cancel.
    {
        XbKDpc counting_dpc;
        KeInitializeDpc(&counting_dpc, dpc_routine_id, 0);
        g_dpc_count = 0;
        KeSetTimerEx(&timer, 10 * kMs, 10, &counting_dpc);
        CHECK(test::eventually([] { return g_dpc_count >= 5; }), "periodic timer fired %d times", g_dpc_count.load());
        const auto start = steady_clock::now();
        const int before = g_dpc_count;
        std::this_thread::sleep_for(milliseconds(100));
        const int fired = g_dpc_count - before;
        CHECK(fired <= elapsed_ms(start) / 10 + 2, "fired %d times in %lld ms", fired, elapsed_ms(start));
        CHECK(KeCancelTimer(&timer) == 1, "cancel of a set timer");
        const int after_cancel = g_dpc_count;
        std::this_thread::sleep_for(milliseconds(50));
        CHECK(g_dpc_count == after_cancel, "fired after cancel");
        CHECK(KeCancelTimer(&timer) == 0, "cancel of an unset timer");
    }

    // DISPATCH_LEVEL holds off DPCs until the IRQL is lowered.
    {
        XbKDpc dpc;
        KeInitializeDpc(&dpc, dpc_routine_id, 0);
        g_dpc_count = 0;
        const uint32_t old_irql = KeRaiseIrqlToDpcLevel();
        CHECK(old_irql == 0 && test::t_irql == 2);
        g_dispatch_level_held = true;
        CHECK(KeInsertQueueDpc(&dpc, 0, 0) == 1);
        CHECK(KeInsertQueueDpc(&dpc, 0, 0) == 0, "queued twice");
        std::this_thread::sleep_for(milliseconds(30));
        CHECK(g_dpc_count == 0, "DPC ran at DISPATCH_LEVEL");
        g_dispatch_level_held = false;
        KfLowerIrql(old_irql);
        CHECK(test::t_irql == 0);
        CHECK(test::eventually([] { return g_dpc_count == 1; }), "DPC did not run after lowering");
        CHECK(g_dpc_while_held == 0);
    }

    // Mutant through handles: exclusion, recursion, release by a non-owner.
    {
        uint32_t mutant = 0;
        CHECK(NtCreateMutant(&mutant, nullptr, 0) == 0);
        long counter = 0;
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([&] {
                test::attach_thread();
                for (int i = 0; i < 5000; ++i) {
                    NtWait(mutant, 0, nullptr);
                    NtWait(mutant, 0, nullptr);
                    const long value = counter;
                    counter = value + 1;
                    NtReleaseMutant(mutant, nullptr);
                    NtReleaseMutant(mutant, nullptr);
                }
            });
        for (auto& worker : workers) worker.join();
        CHECK(counter == 20000, "counter %ld", counter);
        CHECK(NtReleaseMutant(mutant, nullptr) == 0xC0000046, "release by a non-owner");
    }

    // Wait-any returns the signaled index; wait-all needs every object and satisfies all.
    {
        XbKEvent notification, synchronization;
        make_event(notification, kXbEventNotification);
        make_event(synchronization, kXbEventSynchronization);
        XbPtr objects[2] = {to_xbox_ptr(&notification), to_xbox_ptr(&synchronization)};
        KeSetEvent(&synchronization, 0, 0);
        CHECK(KeWaitMultiple(2, objects, 1 /* WaitAny */, 0, 0, 0, &zero, 0) == 1);
        CHECK(KeWaitMultiple(2, objects, 0 /* WaitAll */, 0, 0, 0, &zero, 0) == kStatusTimeout);
        KeSetEvent(&notification, 0, 0);
        KeSetEvent(&synchronization, 0, 0);
        CHECK(KeWaitMultiple(2, objects, 0 /* WaitAll */, 0, 0, 0, &zero, 0) == 0);
        CHECK(notification.header.signal_state == 1 && synchronization.header.signal_state == 0);
    }

    // Events through handles.
    {
        uint32_t event = 0;
        CHECK(NtCreateEvent(&event, nullptr, kXbEventSynchronization, 0) == 0);
        int32_t previous = 7;
        CHECK(NtSetEvent(event, &previous) == 0 && previous == 0);
        CHECK(NtWait(event, 0, &zero) == 0);
        CHECK(NtWait(event, 0, &zero) == kStatusTimeout, "synchronization event did not reset");
        CHECK(NtWait(0x12345678, 0, &zero) == 0xC0000008, "invalid handle");
    }

    // User APCs run only in alertable user-mode waits, on their own thread; the wait
    // then returns STATUS_USER_APC. Sleeping without APCs returns STATUS_SUCCESS.
    {
        constexpr uint32_t kUserApc = 0xC0;
        int64_t short_sleep = 5 * kMs;
        queue_user_apc(test::t_kthread, apc_routine_id, 7, 8, 9);
        CHECK(KeDelayExecutionThread(0 /* KernelMode */, 1, &short_sleep) == 0 && g_apc_runs == 0,
              "kernel-mode waits do not run user APCs");
        CHECK(KeDelayExecutionThread(1, 0, &short_sleep) == 0 && g_apc_runs == 0, "non-alertable waits do not");
        const auto start = steady_clock::now();
        CHECK(KeDelayExecutionThread(1, 1, &one_second) == kUserApc);
        CHECK(elapsed_ms(start) < 500, "the APC ended the sleep early");
        CHECK(g_apc_runs == 1 && g_apc_args[0] == 7 && g_apc_args[1] == 8 && g_apc_args[2] == 9);
        CHECK(KeDelayExecutionThread(1, 1, &short_sleep) == 0, "queue is empty again");

        // An APC queued to a thread blocked in an alertable wait wakes it up.
        XbKEvent never;
        make_event(never, kXbEventNotification);
        std::atomic<uint32_t> waiter_thread{0};
        std::atomic<uint32_t> result{0};
        std::thread waiter([&] {
            test::attach_thread();
            waiter_thread = test::t_kthread;
            int64_t timeout = 2000 * kMs;
            result = KeWait(&never, 0, 1, 1, &timeout);
        });
        CHECK(test::eventually([&] { return waiter_thread != 0; }));
        std::this_thread::sleep_for(milliseconds(20));
        queue_user_apc(waiter_thread, apc_routine_id, 1, 2, 3);
        waiter.join();
        CHECK(result == kUserApc && g_apc_runs == 2 && g_apc_thread == waiter_thread, "result %x", result.load());
    }

    return test::finish("dispatcher_test");
}
