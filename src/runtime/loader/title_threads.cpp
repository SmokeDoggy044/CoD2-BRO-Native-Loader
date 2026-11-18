#include "title_threads.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
#include <thread>

#include "log.h"
#include "replay.h"
#include "thread_env.h"

namespace loader {

namespace {

std::atomic<uint32_t> g_next_thread_id{1};

// request_title_restart -> wait_for_title_restart.
// A title thread's start: its environment, id and stdcall routine(context).
struct ThreadJob {
    ThreadEnv* env = nullptr;
    uint32_t id = 0, routine = 0, context = 0;
};

struct Restart {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<uint8_t> page;
    bool requested = false;  // the requesting thread is leaving the title
    bool ready = false;      // ... and has left it
    // The thread that asked for the restart stays: on Windows a window belongs to the thread that made it and goes
    // with it when that thread ends (#26, #27: the window vanished, sound played on). That is the main thread, which
    // opened the display; it waits here and runs the restarted title's main thread (id 1) on the same OS thread.
    bool parked = false;
    std::optional<ThreadJob> job;
};
Restart& g_restart = *new Restart;
thread_local bool t_restarting = false;
void (*g_thread_reuse_reset)() = nullptr;

void title_thread_main(ThreadJob job) {
    for (;;) {
        if (!activate_thread_env(job.env)) {
            LOG_ERROR("thread", "thread %u cannot set up FS", job.id);
            return;
        }
        // The first thread is the title's main thread (record/replay follows only its reads).
        if (job.id == 1) xbox::replay::bind_current_thread();
        LOG_DEBUG("thread", "thread %u starts at %08x", job.id, job.routine);
        const uint32_t args[] = {job.context};
        run_on_xbox_stack(job.routine, *job.env, args);
        LOG_DEBUG("thread", "thread %u ended", job.id);
        if (!t_restarting) return;
        // Left for a restart: this thread's state goes back to a fresh thread's, then it waits for the next main
        // thread. Parked and ready are set together, so start_title_thread always finds it.
        t_restarting = false;
        if (g_thread_reuse_reset) g_thread_reuse_reset();
        std::unique_lock lock(g_restart.mutex);
        g_restart.parked = true;
        g_restart.ready = true;
        g_restart.changed.notify_all();
        g_restart.changed.wait(lock, [] { return g_restart.job.has_value(); });
        job = *std::exchange(g_restart.job, std::nullopt);
    }
}

}  // namespace

uint32_t next_title_thread_id() { return g_next_thread_id++; }

void reset_thread_ids() { g_next_thread_id = 1; }

bool start_title_thread(uint32_t stack_size, uint32_t tls_size, uint32_t routine, uint32_t context,
                        uint32_t* thread_id) {
    ThreadEnv* env = create_thread_env(stack_size, tls_size);
    if (!env) return false;
    const uint32_t id = next_title_thread_id();
    if (thread_id) *thread_id = id;
    LOG_DEBUG("thread", "title thread (routine=%08x context=%08x stack=%x tls=%x) -> id %u", routine, context,
              stack_size, tls_size, id);
    const ThreadJob job{env, id, routine, context};
    if (id == 1) {
        std::lock_guard lock(g_restart.mutex);
        if (g_restart.parked) {
            g_restart.parked = false;
            g_restart.job = job;
            g_restart.changed.notify_all();
            LOG_INFO("thread", "restart: the main thread runs on the previous one's thread (its window stays)");
            return true;
        }
    }
    std::thread(title_thread_main, job).detach();
    return true;
}

void set_thread_reuse_reset(void (*reset)()) { g_thread_reuse_reset = reset; }

void request_title_restart(const uint8_t* page, uint32_t size) {
    {
        std::lock_guard lock(g_restart.mutex);
        g_restart.page.assign(page, page + size);
        g_restart.requested = true;
    }
    t_restarting = true;
    exit_xbox_thread();  // back to the thread's main, which parks it for the next main thread
}

std::vector<uint8_t> wait_for_title_restart() {
    std::unique_lock lock(g_restart.mutex);
    g_restart.changed.wait(lock, [] { return g_restart.ready; });
    g_restart.ready = g_restart.requested = false;
    return std::move(g_restart.page);
}

void title_thread_left() {
    if (!t_restarting) return;
    std::lock_guard lock(g_restart.mutex);
    g_restart.ready = true;
    g_restart.changed.notify_all();
}

}  // namespace loader
