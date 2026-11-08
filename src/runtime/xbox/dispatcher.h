#pragma once

// The dispatcher: waits on and signals dispatcher objects (events, mutants, timers,
// threads), runs timers and DPCs, and models IRQL.
//
// All object state lives in the title's own structs (DISPATCHER_HEADER and the rest),
// because XDK code initializes events inline and reads these fields directly. One
// global lock + condition variable stands in for the Xbox's dispatcher database.
//
// IRQL: the Xbox is single-core, so code at DISPATCH_LEVEL or above cannot be
// interrupted by DPCs or other threads, and libraries rely on that for exclusion.
// Here a kernel thread expires timers and runs DPCs while holding a global lock, and
// raising to DISPATCH_LEVEL takes the same lock.

#include "objects.h"
#include "types.h"

namespace xbox {

constexpr uint8_t kPassiveLevel = 0;
constexpr uint8_t kDispatchLevel = 2;

// Sets SignalState and wakes waiters.
void signal_object(XbDispatcherHeader& header, int32_t signal_state);

// Sets (state 1) or resets (0) the event behind `handle`; ignores 0 and non-events.
void set_event_handle(Handle event, int32_t state = 1);

// Queues a user-mode APC to `thread` (a KTHREAD address). It runs on that thread
// during its next alertable user-mode wait, which then returns STATUS_USER_APC.
void queue_user_apc(XbPtr thread, XbPtr routine, uint32_t argument1, uint32_t argument2, uint32_t argument3);

// Raises the calling thread's IRQL (e.g. to kDispatchLevel, which excludes DPCs and
// threads that raised theirs) and returns the old level, like KfRaiseIrql. For host
// code calling title callbacks that the Xbox runs at DPC level (vertical blank).
uint8_t raise_irql_to(uint8_t new_irql);
void lower_irql_to(uint8_t old_irql);

}  // namespace xbox
