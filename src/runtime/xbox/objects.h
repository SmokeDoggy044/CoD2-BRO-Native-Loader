#pragma once

// Kernel objects and the handle table. Handles are what the title sees. Behind each
// is a host object; waitable ones also have a body in Xbox-addressable memory that
// starts with a DISPATCHER_HEADER (what ObReferenceObjectByHandle returns and the
// wait functions work on).

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "types.h"

namespace xbox {

using Handle = uint32_t;

constexpr uint32_t kStatusSuccess = 0x00000000;
constexpr uint32_t kStatusInvalidHandle = 0xC0000008;
constexpr uint32_t kStatusInvalidParameter = 0xC000000D;
constexpr uint32_t kStatusNoMemory = 0xC0000017;
constexpr uint32_t kStatusPending = 0x00000103;  // STATUS_PENDING, a thread's exit code while it runs
constexpr uint32_t kStatusTimeout = 0x00000102;
constexpr uint32_t kStatusObjectTypeMismatch = 0xC0000024;
constexpr uint32_t kStatusMutantNotOwned = 0xC0000046;
constexpr uint32_t kStatusUserApc = 0x000000C0;

// The OBJECT_TYPE exports, which ObReferenceObjectByHandle compares against.
namespace object_types {
extern XbObjectType event;
extern XbObjectType mutant;
extern XbObjectType semaphore;
extern XbObjectType timer;
extern XbObjectType file;
extern XbObjectType device;
extern XbObjectType completion;
extern XbObjectType directory;
extern XbObjectType symbolic_link;
extern XbObjectType thread;
}  // namespace object_types

class Object {
public:
    virtual ~Object() = default;
    // The object's body in Xbox memory, or 0 if it has none.
    virtual XbPtr body() const { return 0; }
    // The DISPATCHER_HEADER a wait on the handle waits for (the body itself, or the
    // event inside a FILE_OBJECT), or 0 if it is not waitable.
    virtual XbPtr wait_target() const { return body(); }
    virtual const XbObjectType* type() const = 0;
};

// An object whose body we allocate (events, mutants, files, ...).
class BodyObject : public Object {
public:
    BodyObject(size_t body_size, const XbObjectType& type) : storage_((body_size + 3) / 4), type_(&type) {}
    XbPtr body() const override { return to_xbox_ptr(storage_.data()); }
    const XbObjectType* type() const override { return type_; }
    template <typename T>
    T* as() { return reinterpret_cast<T*>(storage_.data()); }

private:
    std::vector<uint32_t> storage_;  // zeroed, 4-byte aligned
    const XbObjectType* type_;
};

// A thread's body is its KTHREAD, which starts with a DISPATCHER_HEADER that is
// signaled when the thread ends.
class ThreadObject : public Object {
public:
    ThreadObject(uint32_t id, XbPtr kthread) : id_(id), kthread_(kthread) {}

    uint32_t id() const { return id_; }
    XbPtr body() const override { return kthread_; }
    const XbObjectType* type() const override { return &object_types::thread; }

    // Blocks while the thread is suspended (CREATE_SUSPENDED), then returns.
    void wait_until_resumed();
    void set_suspend_count(uint32_t count);
    // Returns the previous count.
    uint32_t resume();

    // Records the exit status and signals the KTHREAD (wakes its waiters).
    void mark_terminated(uint32_t exit_status);
    uint32_t exit_status() const;

private:
    const uint32_t id_;
    const XbPtr kthread_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    uint32_t suspend_count_ = 0;
    uint32_t exit_status_ = kStatusPending;
};

// Returns a new handle (a non-zero multiple of 4, like the Xbox's), or 0 on failure.
Handle insert_handle(std::shared_ptr<Object> object);
std::shared_ptr<Object> lookup_handle(Handle handle);
bool close_handle(Handle handle);

// The object whose body is at `body`, while a handle or a reference
// (ObReferenceObjectByHandle) keeps it alive; null otherwise.
std::shared_ptr<Object> lookup_referenced_body(XbPtr body);

template <typename T>
std::shared_ptr<T> lookup_handle_as(Handle handle) {
    return std::dynamic_pointer_cast<T>(lookup_handle(handle));
}

}  // namespace xbox
