// Structured exception handling for title code and our C: RtlRaiseException and RtlUnwind (own engine host 0.3;
// moved here from the loader's kernel exports, which now point at these).
//
// The title raises exceptions itself (RaiseException, and MSVC's C++ throw, which is RaiseException(0xE06D7363));
// CoD2: BRO's script coroutines throw to unwind. The handlers are the title's own or ours (__CxxFrameHandler,
// _except_handler3, title_eh.cpp), registered in the chain at FS:[0]. These run on the title's stack, so the
// handlers are called directly from here; a C++ catch never returns to us (it jumps to its continuation and abandons
// these frames), so nothing here may hold locks or objects with destructors across a handler call.

#include "title_seh.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "title_host.h"

namespace game {

namespace {

constexpr uint32_t kStatusUnwind = 0xC0000027;
constexpr uint32_t kExceptionNoncontinuable = 0x01;
constexpr uint32_t kExceptionUnwinding = 0x02;
constexpr uint32_t kExceptionExitUnwind = 0x04;
constexpr uint32_t kContextFull = 0x00010007;  // CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS

enum Disposition : uint32_t {
    kContinueExecution = 0, kContinueSearch = 1, kNestedException = 2, kCollidedUnwind = 3,
};

struct ExceptionRecord {  // EXCEPTION_RECORD
    uint32_t code;
    uint32_t flags;
    uint32_t record;
    uint32_t address;
    uint32_t parameter_count;
    uint32_t information[15];
};
static_assert(sizeof(ExceptionRecord) == 0x50);

struct Context {  // CONTEXT (Xbox: FXSAVE-style FLOATING_SAVE_AREA)
    uint32_t flags;
    uint8_t float_save[0x204];
    uint32_t edi, esi, ebx, edx, ecx, eax;
    uint32_t ebp, eip, seg_cs, eflags, esp, seg_ss;
};
static_assert(sizeof(Context) == 0x238);

struct Registration {  // EXCEPTION_REGISTRATION_RECORD
    uint32_t next;
    uint32_t handler;
};

using Handler = uint32_t(XB_CDECL*)(ExceptionRecord* record, Registration* frame, Context* context,
                                    uint32_t* dispatcher_context);

template <typename T>
T* at(uint32_t address) {
    return reinterpret_cast<T*>(uintptr_t{address});
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Where the title's part of the chain ends (on Windows the host thread's own records follow, which must not see
// Xbox-format contexts).
uint32_t chain_end() {
    const TitleHost& host = title_host();
    return host.seh_chain_end ? host.seh_chain_end() : 0xFFFFFFFFu;
}

// The context of our caller just after the call returns: what the title would see.
void caller_context(Context& context, uint32_t return_address, uint32_t caller_ebp, uint32_t caller_esp) {
    std::memset(&context, 0, sizeof(context));
    context.flags = kContextFull;
    context.eip = return_address;
    context.ebp = caller_ebp;
    context.esp = caller_esp;
}

uint32_t call_handler(const Registration* frame, ExceptionRecord* record, Context* context) {
    uint32_t dispatcher_context = 0;
    auto handler = reinterpret_cast<Handler>(uintptr_t{frame->handler});
    return handler(record, const_cast<Registration*>(frame), context, &dispatcher_context);
}

[[noreturn]] void unhandled(const ExceptionRecord& record, const Context& context) {
    char message[160];
    std::snprintf(message, sizeof(message), "unhandled exception %08x at %08x (flags %x, %u parameters)",
                  record.code, record.address, record.flags, record.parameter_count);
    if (title_host().fatal) title_host().fatal(message, context.eip, context.ebp);
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

// Walks the handler chain until a handler takes the exception. Returns only when a handler says to continue
// execution; C++ and __try handlers that catch it never return here.
void dispatch(ExceptionRecord* record, Context* context) {
    uint32_t frame = platform_seh_head();
    const uint32_t end = chain_end();
    while (frame != end && frame != 0) {
        const auto* registration = at<Registration>(frame);
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
                std::fprintf(stderr, "seh: exception handler at %08x returned an invalid disposition\n",
                             registration->handler);
                unhandled(*record, *context);
        }
    }
    unhandled(*record, *context);
}

}  // namespace

// The arguments' addresses give the caller's ESP; -mstackrealign frames still keep the saved EBP at the frame
// address.
PLATFORM_NOINLINE void XB_STDCALL title_RtlRaiseException(uint32_t record_address) {
    ExceptionRecord* record = at<ExceptionRecord>(record_address);
    Context context;
    // stdcall with one argument: the caller's ESP after return is just past it.
    caller_context(context, address_of(__builtin_return_address(0)),
                   *static_cast<const uint32_t*>(__builtin_frame_address(0)), address_of(&record_address + 1));
    record->address = context.eip;
    dispatch(record, &context);
}

// Calls the handlers of every frame newer than `target_frame` with the unwinding flag and removes them from the
// chain. Returns `return_value` (in EAX), which is how RtlUnwind continues at TargetIp: callers pass the address
// right after their call.
PLATFORM_NOINLINE uint32_t XB_STDCALL title_RtlUnwind(uint32_t target_frame, uint32_t target_ip,
                                                      uint32_t record_address, uint32_t return_value) {
    Context context;
    caller_context(context, address_of(__builtin_return_address(0)),
                   *static_cast<const uint32_t*>(__builtin_frame_address(0)), address_of(&return_value + 1));
    if (target_ip && target_ip != context.eip)
        std::fprintf(stderr, "seh: RtlUnwind target %08x is not its return address %08x: returning anyway\n",
                     target_ip, context.eip);
    ExceptionRecord local{};
    ExceptionRecord* record = record_address ? at<ExceptionRecord>(record_address) : &local;
    if (!record_address) {
        local.code = kStatusUnwind;
        local.address = context.eip;
    }
    record->flags |= kExceptionUnwinding | (target_frame ? 0 : kExceptionExitUnwind);

    const uint32_t end = chain_end();
    uint32_t frame;
    while ((frame = platform_seh_head()) != end && frame != 0 && frame != target_frame) {
        const auto* registration = at<Registration>(frame);
        const uint32_t disposition = call_handler(registration, record, &context);
        if (disposition != kContinueSearch && disposition != kCollidedUnwind)
            std::fprintf(stderr, "seh: unwind handler at %08x returned %u\n", registration->handler, disposition);
        if (platform_seh_head() == frame) platform_set_seh_head(registration->next);
    }
    return return_value;
}

}  // namespace game
