#include "title_threads.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "log.h"
#include "replay.h"
#include "thread_env.h"

namespace loader {

namespace {

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

void title_thread_main(ThreadEnv* env, uint32_t id, uint32_t routine, uint32_t context) {
    if (!activate_thread_env(env)) {
        LOG_ERROR("thread", "thread %u cannot set up FS", id);
        return;
    }
    // The first thread is the title's main thread (record/replay follows only its reads).
    if (id == 1) xbox::replay::bind_current_thread();
    LOG_DEBUG("thread", "thread %u starts at %08x", id, routine);
    const uint32_t args[] = {context};
    run_on_xbox_stack(routine, *env, args);
    LOG_DEBUG("thread", "thread %u ended", id);
    title_thread_left();
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
    std::thread(title_thread_main, env, id, routine, context).detach();
    return true;
}

void request_title_restart(const uint8_t* page, uint32_t size) {
    {
        std::lock_guard lock(g_restart.mutex);
        g_restart.page.assign(page, page + size);
        g_restart.requested = true;
    }
    t_restarting = true;
    exit_xbox_thread();  // back to the thread's main, which calls title_thread_left
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
