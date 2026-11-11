#include "title_objects.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "title_data.h"
#include "title_host.h"
#include "title_xapi.h"

namespace game {

namespace {

TitleHost g_host;
bool g_native = false;

}  // namespace

void set_title_host(const TitleHost& host) { g_host = host; }
const TitleHost& title_host() { return g_host; }
void set_native_platform(bool on) { g_native = on; }
bool native_platform() { return g_native; }

namespace objects {

namespace {

constexpr uint32_t kStatusSuccess = 0;
constexpr uint32_t kStatusUserApc = 0xc0;
constexpr uint32_t kStatusTimeout = 0x102;
constexpr uint32_t kStatusInvalidHandle = 0xc0000008;
constexpr uint32_t kStatusTypeMismatch = 0xc0000024;
constexpr uint32_t kStatusMutantNotOwned = 0xc0000046;

using Clock = std::chrono::steady_clock;

enum class Kind { Event, Mutex, Thread, Payload };

struct Object {
    Kind kind;
    bool manual_reset = false;  // events: a notification event stays signaled
    bool signaled = false;      // events and threads
    // Mutexes: the owner and how often it acquired it (signaled while count is 0).
    std::thread::id owner;
    uint32_t count = 0;
    uint32_t exit_code = 0;  // threads
    std::shared_ptr<Payload> payload;
};

struct Apc {
    uint32_t routine;
    uint32_t args[3];
};

struct State {
    std::mutex lock;
    std::condition_variable changed;
    std::unordered_map<uint32_t, std::shared_ptr<Object>> handles;
    uint32_t next = kFirstHandle;
};

State& state() {
    static State* s = new State;  // never destroyed: title threads may still wait at exit
    return *s;
}

thread_local std::deque<Apc> t_apcs;

uint32_t insert(std::shared_ptr<Object> object) {
    State& s = state();
    std::lock_guard guard(s.lock);
    const uint32_t handle = s.next;
    s.next += 4;
    s.handles.emplace(handle, std::move(object));
    return handle;
}

std::shared_ptr<Object> find(State& s, uint32_t handle) {
    const auto it = s.handles.find(handle);
    return it == s.handles.end() ? nullptr : it->second;
}

bool is_signaled(const Object& o) {
    if (o.kind == Kind::Mutex) return o.count == 0 || o.owner == std::this_thread::get_id();
    return o.signaled;
}

// A satisfied wait: a synchronization event resets, a mutex is taken (once more).
void satisfy(Object& o) {
    if (o.kind == Kind::Event && !o.manual_reset) o.signaled = false;
    if (o.kind == Kind::Mutex) {
        o.owner = std::this_thread::get_id();
        ++o.count;
    }
}

// Runs the calling thread's queued completion routines (outside the lock); true if there were any.
bool deliver_apcs() {
    if (t_apcs.empty()) return false;
    std::deque<Apc> apcs;
    apcs.swap(t_apcs);
    for (const Apc& apc : apcs)
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(apc.routine)(apc.args[0], apc.args[1],
                                                                                   apc.args[2]);
    return true;
}

uint32_t wait_live(const std::vector<std::shared_ptr<Object>>& objects, bool wait_all,
                   std::optional<Clock::time_point> deadline, bool alertable) {
    State& s = state();
    for (;;) {
        if (alertable && deliver_apcs()) return kStatusUserApc;
        std::unique_lock guard(s.lock);
        if (objects.empty()) {
            // A sleep: only the timeout (or a completion routine) ends it.
        } else if (wait_all) {
            if (std::all_of(objects.begin(), objects.end(), [](const auto& o) { return is_signaled(*o); })) {
                for (const auto& o : objects) satisfy(*o);
                return kStatusSuccess;
            }
        } else {
            for (size_t i = 0; i < objects.size(); ++i) {
                if (is_signaled(*objects[i])) {
                    satisfy(*objects[i]);
                    return kStatusSuccess + static_cast<uint32_t>(i);
                }
            }
        }
        if (deadline && Clock::now() >= *deadline) return objects.empty() ? kStatusSuccess : kStatusTimeout;
        // Completion routines are queued by this thread itself (its own I/O), so they are there before the wait;
        // an alertable wait still wakes now and then in case one came from elsewhere.
        if (deadline)
            s.changed.wait_until(guard, alertable ? std::min(*deadline, Clock::now() + std::chrono::milliseconds(10))
                                                  : *deadline);
        else if (alertable)
            s.changed.wait_for(guard, std::chrono::milliseconds(10));
        else
            s.changed.wait(guard);
    }
}

// Record/replay, as the kernel's wait_objects: a timed wait on the main thread ends by another thread's timing (a
// file or sound completion, a loading thread's event), so the recording keeps its outcome; a replay hands back a
// timeout at once, or waits (without the timeout) for the object that was signaled in the recording, at most 10 s.
uint32_t wait_recorded(const std::vector<std::shared_ptr<Object>>& objects, bool wait_all,
                       std::optional<Clock::time_point> deadline, bool alertable) {
    const TitleHost& host = g_host;
    if (!deadline || objects.empty() || !host.replay_on_stream || !host.replay_on_stream())
        return wait_live(objects, wait_all, deadline, alertable);
    uint32_t outcome = 0;
    if (!host.replay_replaying() || !host.replay_recorded('W', &outcome))
        return host.replay_value('W', wait_live(objects, wait_all, deadline, alertable));
    if (outcome == kStatusTimeout) return kStatusTimeout;
    const size_t index = outcome - kStatusSuccess;
    if (outcome == kStatusUserApc || index >= objects.size()) return wait_live(objects, wait_all, deadline, alertable);
    State& s = state();
    const auto limit = Clock::now() + std::chrono::seconds(10);
    {
        std::unique_lock guard(s.lock);
        for (;;) {
            if (wait_all ? std::all_of(objects.begin(), objects.end(), [](const auto& o) { return is_signaled(*o); })
                         : is_signaled(*objects[index])) {
                if (wait_all)
                    for (const auto& o : objects) satisfy(*o);
                else
                    satisfy(*objects[index]);
                return outcome;
            }
            if (Clock::now() >= limit) break;
            s.changed.wait_until(guard, limit);
        }
    }
    std::fprintf(stderr, "[replay] a wait the recording saw end (%08x) did not end within 10 s; waiting live\n",
                 outcome);
    return wait_live(objects, wait_all, deadline, alertable);
}

struct ThreadStart {
    ThreadBody* body;
    uint32_t routine;
    uint32_t context;
    uint32_t handle;
};

// The new thread's first frame (stdcall, called by the host on the title stack): the body, then its handle is
// signaled with the result.
uint32_t XB_STDCALL thread_entry(ThreadStart* start) {
    const ThreadStart copy = *start;
    delete start;
    const uint32_t result = copy.body(copy.routine, copy.context);
    thread_exited(copy.handle, result);
    return result;
}

}  // namespace

bool is_ours(uint32_t handle) { return handle >= kFirstHandle && handle < kFirstHandle + 0x01000000; }

uint32_t create_event(bool manual_reset, bool initial_state) {
    auto o = std::make_shared<Object>();
    o->kind = Kind::Event;
    o->manual_reset = manual_reset;
    o->signaled = initial_state;
    return insert(std::move(o));
}

uint32_t create_payload(std::shared_ptr<Payload> payload) {
    auto o = std::make_shared<Object>();
    o->kind = Kind::Payload;
    o->signaled = true;  // its I/O completes inside the call
    o->payload = std::move(payload);
    return insert(std::move(o));
}

std::shared_ptr<Payload> payload(uint32_t handle) {
    if (!is_ours(handle)) return nullptr;
    State& s = state();
    std::lock_guard guard(s.lock);
    const auto o = find(s, handle);
    return o ? o->payload : nullptr;
}

uint32_t create_mutex(bool initially_owned) {
    auto o = std::make_shared<Object>();
    o->kind = Kind::Mutex;
    if (initially_owned) {
        o->owner = std::this_thread::get_id();
        o->count = 1;
    }
    return insert(std::move(o));
}

uint32_t create_thread(uint32_t stack_size, uint32_t tls_size, ThreadBody* body, uint32_t routine, uint32_t context,
                       uint32_t* thread_id) {
    if (!g_host.start_thread) return 0;
    auto o = std::make_shared<Object>();
    o->kind = Kind::Thread;
    const uint32_t handle = insert(std::move(o));
    auto* start = new ThreadStart{body, routine, context, handle};
    uint32_t id = 0;
    if (!g_host.start_thread(stack_size, tls_size, own_address(&thread_entry),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start)), &id)) {
        delete start;
        close(handle);
        return 0;
    }
    if (thread_id) *thread_id = id;
    return handle;
}

void thread_exited(uint32_t handle, uint32_t exit_code) {
    State& s = state();
    std::lock_guard guard(s.lock);
    // The handle may be closed already; the object lives on only through it.
    if (const auto o = find(s, handle)) {
        o->exit_code = exit_code;
        o->signaled = true;
    }
    s.changed.notify_all();
}

uint32_t set_event(uint32_t handle, bool signaled) {
    State& s = state();
    std::lock_guard guard(s.lock);
    const auto o = find(s, handle);
    if (!o) return kStatusInvalidHandle;
    if (o->kind != Kind::Event) return kStatusTypeMismatch;
    o->signaled = signaled;
    if (signaled) s.changed.notify_all();
    return kStatusSuccess;
}

uint32_t release_mutex(uint32_t handle) {
    State& s = state();
    std::lock_guard guard(s.lock);
    const auto o = find(s, handle);
    if (!o) return kStatusInvalidHandle;
    if (o->kind != Kind::Mutex) return kStatusTypeMismatch;
    if (o->count == 0 || o->owner != std::this_thread::get_id()) return kStatusMutantNotOwned;
    if (--o->count == 0) {
        o->owner = {};
        s.changed.notify_all();
    }
    return kStatusSuccess;
}

uint32_t close(uint32_t handle) {
    State& s = state();
    std::lock_guard guard(s.lock);
    return s.handles.erase(handle) ? kStatusSuccess : kStatusInvalidHandle;
}

uint32_t wait(const uint32_t* handles, size_t count, bool wait_all, const uint32_t* timeout_ms, bool alertable) {
    std::vector<std::shared_ptr<Object>> objects;
    {
        State& s = state();
        std::lock_guard guard(s.lock);
        for (size_t i = 0; i < count; ++i) {
            auto o = find(s, handles[i]);
            if (!o) return kStatusInvalidHandle;
            objects.push_back(std::move(o));
        }
    }
    std::optional<Clock::time_point> deadline;
    if (timeout_ms) deadline = Clock::now() + std::chrono::milliseconds(*timeout_ms);
    return wait_recorded(objects, wait_all, deadline, alertable);
}

void queue_apc(uint32_t routine, uint32_t a1, uint32_t a2, uint32_t a3) { t_apcs.push_back({routine, {a1, a2, a3}}); }

void reset() {
    State& s = state();
    std::unordered_map<uint32_t, std::shared_ptr<Object>> closed;
    {
        std::lock_guard lock(s.lock);
        closed.swap(s.handles);
        s.next = kFirstHandle;
    }
    t_apcs.clear();
}

// --- Critical sections ------------------------------------------------------------------------------------------

namespace {

struct CriticalSection {  // RTL_CRITICAL_SECTION
    uint8_t type, absolute, size, inserted;  // DISPATCHER_HEADER of a synchronization event
    int32_t signal_state;
    uint32_t wait_list_flink, wait_list_blink;
    int32_t lock_count;  // -1 = free
    int32_t recursion_count;
    uint32_t owning_thread;  // KTHREAD
};
static_assert(sizeof(CriticalSection) == 0x1c);

CriticalSection& section_at(uint32_t section) { return *reinterpret_cast<CriticalSection*>(uintptr_t{section}); }

}  // namespace

void initialize_critical_section(uint32_t section) {
    CriticalSection& cs = section_at(section);
    cs.type = 1;  // SynchronizationEvent
    cs.absolute = 0;
    cs.size = 4;  // the header's size in dwords
    cs.inserted = 0;
    cs.signal_state = 0;
    cs.wait_list_flink = section + 8;  // an empty wait list points at itself
    cs.wait_list_blink = section + 8;
    cs.lock_count = -1;
    cs.recursion_count = 0;
    cs.owning_thread = 0;
}

void enter_critical_section(uint32_t section) {
    CriticalSection& cs = section_at(section);
    const uint32_t thread = title_fs_word(0x28);
    if (std::atomic_ref<int32_t>(cs.lock_count).fetch_add(1) != -1) {
        // Held already; only the owner itself can see its own id here.
        if (std::atomic_ref<uint32_t>(cs.owning_thread).load(std::memory_order_relaxed) == thread) {
            ++cs.recursion_count;
            return;
        }
        std::atomic_ref<int32_t> state(cs.signal_state);  // auto-reset: take the signal
        for (;;) {
            int32_t current = state.load();
            if (current > 0 && state.compare_exchange_weak(current, 0)) break;
            if (current <= 0) state.wait(current);
        }
    }
    std::atomic_ref<uint32_t>(cs.owning_thread).store(thread, std::memory_order_relaxed);
    cs.recursion_count = 1;
}

void leave_critical_section(uint32_t section) {
    CriticalSection& cs = section_at(section);
    if (--cs.recursion_count != 0) {
        std::atomic_ref<int32_t>(cs.lock_count).fetch_sub(1);
        return;
    }
    std::atomic_ref<uint32_t>(cs.owning_thread).store(0, std::memory_order_relaxed);
    // Still >= 0 afterwards: someone waits - hand the section over.
    if (std::atomic_ref<int32_t>(cs.lock_count).fetch_sub(1) - 1 >= 0) {
        std::atomic_ref<int32_t> state(cs.signal_state);
        state.store(1);
        state.notify_one();
    }
}

}  // namespace objects

}  // namespace game
