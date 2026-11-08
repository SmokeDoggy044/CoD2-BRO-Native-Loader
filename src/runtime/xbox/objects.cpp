// Object manager: handle table, thread objects and the exports that work on handles.

#include "objects.h"

#include <cstddef>
#include <unordered_map>

#include "compiler.h"
#include "dispatcher.h"
#include "kernel_internal.h"
#include "log.h"

namespace xbox {

// --- ThreadObject ------------------------------------------------------------------

void ThreadObject::wait_until_resumed() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return suspend_count_ == 0; });
}

void ThreadObject::set_suspend_count(uint32_t count) {
    std::lock_guard lock(mutex_);
    suspend_count_ = count;
}

uint32_t ThreadObject::resume() {
    std::lock_guard lock(mutex_);
    const uint32_t previous = suspend_count_;
    if (suspend_count_ > 0 && --suspend_count_ == 0) changed_.notify_all();
    return previous;
}

void ThreadObject::mark_terminated(uint32_t exit_status) {
    {
        std::lock_guard lock(mutex_);
        exit_status_ = exit_status;
    }
    if (kthread_) signal_object(*from_xbox_ptr<XbDispatcherHeader>(kthread_), 1);
}

uint32_t ThreadObject::exit_status() const {
    std::lock_guard lock(mutex_);
    return exit_status_;
}

// --- Handle table --------------------------------------------------------------------

namespace {

struct HandleTable {
    std::mutex mutex;
    std::unordered_map<Handle, std::shared_ptr<Object>> handles;
    // Bodies handed out by ObReferenceObjectByHandle: kept alive until dereferenced,
    // even if the handle is closed in between.
    std::unordered_map<XbPtr, std::pair<std::shared_ptr<Object>, uint32_t>> references;
};
HandleTable& g_table = immortal<HandleTable>();
std::mutex& g_handles_mutex = g_table.mutex;
auto& g_handles = g_table.handles;
auto& g_references = g_table.references;
Handle g_next_handle = 4;

}  // namespace

Handle insert_handle(std::shared_ptr<Object> object) {
    if (!object) return 0;
    std::lock_guard lock(g_handles_mutex);
    // Handles are never reused, which makes use-after-close bugs visible.
    const Handle handle = g_next_handle;
    g_next_handle += 4;
    g_handles.emplace(handle, std::move(object));
    return handle;
}

std::shared_ptr<Object> lookup_handle(Handle handle) {
    std::lock_guard lock(g_handles_mutex);
    const auto it = g_handles.find(handle);
    return it != g_handles.end() ? it->second : nullptr;
}

std::shared_ptr<Object> lookup_referenced_body(XbPtr body) {
    std::lock_guard lock(g_handles_mutex);
    if (const auto it = g_references.find(body); it != g_references.end()) return it->second.first;
    for (const auto& [handle, object] : g_handles)
        if (object->body() == body) return object;
    return nullptr;
}

bool close_handle(Handle handle) {
    std::shared_ptr<Object> object;  // destroyed after the lock is released
    std::lock_guard lock(g_handles_mutex);
    const auto it = g_handles.find(handle);
    if (it == g_handles.end()) return false;
    object = std::move(it->second);
    g_handles.erase(it);
    return true;
}

// --- Object types ----------------------------------------------------------------------
//
// Only their addresses matter to titles (ObReferenceObjectByHandle's type check).
// default_object is the offset of the header a wait uses.

namespace object_types {
constexpr uint32_t tag(const char (&text)[5]) {
    return uint32_t(uint8_t(text[0])) | uint32_t(uint8_t(text[1])) << 8 | uint32_t(uint8_t(text[2])) << 16 |
           uint32_t(uint8_t(text[3])) << 24;
}
XbObjectType event{0, 0, 0, 0, 0, 0, tag("Even")};
XbObjectType mutant{0, 0, 0, 0, 0, 0, tag("Muta")};
XbObjectType semaphore{0, 0, 0, 0, 0, 0, tag("Sema")};
XbObjectType timer{0, 0, 0, 0, 0, 0, tag("Time")};
XbObjectType file{0, 0, 0, 0, 0, offsetof(XbFileObject, event), tag("File")};
XbObjectType device{0, 0, 0, 0, 0, 0, tag("Devi")};
XbObjectType completion{0, 0, 0, 0, 0, 0, tag("Comp")};
XbObjectType directory{0, 0, 0, 0, 0, 0, tag("Dire")};
XbObjectType symbolic_link{0, 0, 0, 0, 0, 0, tag("Symb")};
XbObjectType thread{0, 0, 0, 0, 0, 0, tag("Thre")};
}  // namespace object_types

// --- Exports -------------------------------------------------------------------------

namespace {

constexpr Handle kCurrentThread = 0xFFFFFFFE;  // NtCurrentThread()

uint32_t XB_STDCALL NtClose(Handle handle) {
    const bool closed = close_handle(handle);
    KTRACE("NtClose(%x)%s", handle, closed ? "" : " -> invalid handle");
    return closed ? kStatusSuccess : kStatusInvalidHandle;
}

// Returns the object's body and keeps it alive until ObfDereferenceObject.
uint32_t XB_STDCALL ObReferenceObjectByHandle(Handle handle, const XbObjectType* object_type, XbPtr* returned_object) {
    if (handle == kCurrentThread) {
        if (object_type && object_type != &object_types::thread) return kStatusObjectTypeMismatch;
        *returned_object = current_kthread();  // lives as long as the thread; no reference kept
        return kStatusSuccess;
    }
    std::lock_guard lock(g_handles_mutex);
    const auto it = g_handles.find(handle);
    if (it == g_handles.end()) return kStatusInvalidHandle;
    const std::shared_ptr<Object>& object = it->second;
    if ((object_type && object->type() != object_type) || !object->body()) return kStatusObjectTypeMismatch;
    auto& reference = g_references[object->body()];
    reference.first = object;
    ++reference.second;
    *returned_object = object->body();
    KTRACE("ObReferenceObjectByHandle(%x) -> %08x", handle, *returned_object);
    return kStatusSuccess;
}

void XB_FASTCALL ObfReferenceObject(XbPtr body) {
    std::lock_guard lock(g_handles_mutex);
    if (const auto it = g_references.find(body); it != g_references.end()) {
        ++it->second.second;
        return;
    }
    for (const auto& [handle, object] : g_handles) {
        if (object->body() == body) {
            g_references[body] = {object, 1};
            return;
        }
    }
    LOG_DEBUG("kernel", "ObfReferenceObject(%08x): not a known object", body);
}

void XB_FASTCALL ObfDereferenceObject(XbPtr body) {
    std::shared_ptr<Object> last;  // destroyed after the lock is released
    std::lock_guard lock(g_handles_mutex);
    const auto it = g_references.find(body);
    if (it == g_references.end()) return;  // e.g. the current thread, which is not counted
    if (--it->second.second == 0) {
        last = std::move(it->second.first);
        g_references.erase(it);
    }
}

const KernelImpl kObjectImpls[] = {
    KIMPL(NtClose),
    KIMPL(ObReferenceObjectByHandle),
    KIMPL(ObfDereferenceObject),
    KIMPL(ObfReferenceObject),
    {"ExEventObjectType", &object_types::event},
    {"ExMutantObjectType", &object_types::mutant},
    {"ExSemaphoreObjectType", &object_types::semaphore},
    {"ExTimerObjectType", &object_types::timer},
    {"IoCompletionObjectType", &object_types::completion},
    {"IoDeviceObjectType", &object_types::device},
    {"IoFileObjectType", &object_types::file},
    {"ObDirectoryObjectType", &object_types::directory},
    {"ObSymbolicLinkObjectType", &object_types::symbolic_link},
    {"PsThreadObjectType", &object_types::thread},
};

}  // namespace

std::span<const KernelImpl> object_kernel_impls() { return kObjectImpls; }

}  // namespace xbox
