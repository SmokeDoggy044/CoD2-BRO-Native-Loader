// Structured exception handling for title code: RtlRaiseException and RtlUnwind.
//
// Titles raise exceptions themselves (RaiseException, and MSVC's C++ throw, which is
// RaiseException(0xE06D7363)); CoD2: BRO's script coroutines throw to unwind. The
// handlers are the title's own (__CxxFrameHandler, _except_handler3), registered in
// the chain at FS:[0]. Kernel thunks run on the title's stack, so the handlers are
// called directly from here; a C++ catch never returns to us (it jumps to its
// continuation and abandons these frames), so nothing here may hold locks or
// objects with destructors across a handler call.

#include "seh.h"

#include <cstdio>
#include <cstring>

#include "compiler.h"
#include "crash.h"
#include "kernel.h"
#include "log.h"
#include "thread_env.h"
#include "types.h"

namespace loader {

namespace {

using xbox::from_xbox_ptr;
using xbox::to_xbox_ptr;

constexpr uint32_t kStatusUnwind = 0xC0000027;
constexpr uint32_t kExceptionNoncontinuable = 0x01;
constexpr uint32_t kExceptionUnwinding = 0x02;
constexpr uint32_t kExceptionExitUnwind = 0x04;
constexpr uint32_t kContextFull = 0x00010007;  // CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS

enum Disposition : uint32_t {
    kContinueExecution = 0, kContinueSearch = 1, kNestedException = 2, kCollidedUnwind = 3,
};

struct XbExceptionRecord {  // EXCEPTION_RECORD
    uint32_t code;
    uint32_t flags;
    xbox::XbPtr record;
    xbox::XbPtr address;
    uint32_t parameter_count;
    uint32_t information[15];
};
static_assert(sizeof(XbExceptionRecord) == 0x50);

struct XbContext {  // CONTEXT (Xbox: FXSAVE-style FLOATING_SAVE_AREA)
    uint32_t flags;
    uint8_t float_save[0x204];
    uint32_t edi, esi, ebx, edx, ecx, eax;
    uint32_t ebp, eip, seg_cs, eflags, esp, seg_ss;
};
static_assert(sizeof(XbContext) == 0x238);

struct Registration {  // EXCEPTION_REGISTRATION_RECORD
    uint32_t next;
    uint32_t handler;
};

using Handler = uint32_t(XB_CDECL*)(XbExceptionRecord* record, Registration* frame, XbContext* context,
                                    uint32_t* dispatcher_context);

// Where the title's part of the chain ends (on Windows the host thread's own
// records follow, which must not see Xbox-format contexts).
uint32_t chain_end() { return current_thread_env()->seh_chain_end; }

// The arguments' addresses give the caller's ESP; -mstackrealign frames still keep the
// saved EBP at the frame address.
uint32_t return_address(void* address) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address)); }
uint32_t caller_ebp(void* frame) { return *static_cast<const uint32_t*>(frame); }

// The context of our caller just after the call returns: what the title would see.
void caller_context(XbContext& context, uint32_t return_address, uint32_t caller_ebp, uint32_t caller_esp) {
    std::memset(&context, 0, sizeof(context));
    context.flags = kContextFull;
    context.eip = return_address;
    context.ebp = caller_ebp;
    context.esp = caller_esp;
}

uint32_t call_handler(const Registration* frame, XbExceptionRecord* record, XbContext* context) {
    uint32_t dispatcher_context = 0;
    auto handler = reinterpret_cast<Handler>(static_cast<uintptr_t>(frame->handler));
    return handler(record, const_cast<Registration*>(frame), context, &dispatcher_context);
}

[[noreturn]] void unhandled(const XbExceptionRecord& record, const XbContext& context) {
    char message[160];
    std::snprintf(message, sizeof(message), "unhandled exception %08x at %08x (flags %x, %u parameters)",
                  record.code, record.address, record.flags, record.parameter_count);
    fatal_with_backtrace(message, context.eip, context.ebp);
}

// Walks the handler chain until a handler takes the exception. Returns only when a
// handler says to continue execution; C++ and __try handlers that catch it never
// return here.
void dispatch(XbExceptionRecord* record, XbContext* context) {
    uint32_t frame = xbox_tib().exception_list;
    const uint32_t end = chain_end();
    while (frame != end && frame != 0) {
        const auto* registration = from_xbox_ptr<Registration>(frame);
        const uint32_t next = registration->next;
        switch (call_handler(registration, record, context)) {
            case kContinueExecution:
                if (record->flags & kExceptionNoncontinuable) unhandled(*record, *context);
                return;
            case kContinueSearch:
            case kNestedException:  // simplified: keep searching outwards
                frame = next;
                break;
            default:
                LOG_ERROR("seh", "exception handler at %08x returned an invalid disposition", registration->handler);
                unhandled(*record, *context);
        }
    }
    unhandled(*record, *context);
}

}  // namespace

PLATFORM_NOINLINE void XB_STDCALL RtlRaiseException(xbox::XbPtr record_address) {
    XbExceptionRecord* record = from_xbox_ptr<XbExceptionRecord>(record_address);
    XbContext context;
    // stdcall with one argument: the caller's ESP after return is just past it.
    caller_context(context, return_address(__builtin_return_address(0)), caller_ebp(__builtin_frame_address(0)),
                   to_xbox_ptr(&record_address + 1));
    record->address = context.eip;
    if (xbox::kernel_trace_enabled())
        LOG_TRACE("kernel", "RtlRaiseException(code %08x, flags %x) from %08x", record->code, record->flags,
                  context.eip);
    dispatch(record, &context);
}

// Calls the handlers of every frame newer than `target_frame` with the unwinding
// flag and removes them from the chain. Returns `return_value` (in EAX), which is
// how RtlUnwind continues at TargetIp: callers pass the address right after their
// call.
PLATFORM_NOINLINE uint32_t XB_STDCALL RtlUnwind(xbox::XbPtr target_frame, xbox::XbPtr target_ip,
                                                xbox::XbPtr record_address, uint32_t return_value) {
    XbContext context;
    caller_context(context, return_address(__builtin_return_address(0)), caller_ebp(__builtin_frame_address(0)),
                   to_xbox_ptr(&return_value + 1));
    if (target_ip && target_ip != context.eip)
        LOG_WARN("seh", "RtlUnwind target %08x is not its return address %08x: returning anyway", target_ip,
                 context.eip);
    XbExceptionRecord local{};
    XbExceptionRecord* record = record_address ? from_xbox_ptr<XbExceptionRecord>(record_address) : &local;
    if (!record_address) {
        local.code = kStatusUnwind;
        local.address = context.eip;
    }
    record->flags |= kExceptionUnwinding | (target_frame ? 0 : kExceptionExitUnwind);

    NtTib& t = xbox_tib();
    const uint32_t end = chain_end();
    uint32_t frame;
    while ((frame = t.exception_list) != end && frame != 0 && frame != target_frame) {
        const auto* registration = from_xbox_ptr<Registration>(frame);
        const uint32_t disposition = call_handler(registration, record, &context);
        if (disposition != kContinueSearch && disposition != kCollidedUnwind)
            LOG_WARN("seh", "unwind handler at %08x returned %u", registration->handler, disposition);
        if (t.exception_list == frame) t.exception_list = registration->next;
    }
    return return_value;
}

const xbox::KernelImpl kSehImpls[] = {
    {"RtlRaiseException", reinterpret_cast<void*>(&RtlRaiseException)},
    {"RtlUnwind", reinterpret_cast<void*>(&RtlUnwind)},
};

std::span<const xbox::KernelImpl> seh_impls() { return kSehImpls; }

}  // namespace loader
