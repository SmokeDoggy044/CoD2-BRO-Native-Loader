#include "title_eh.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "title_crt.h"
#include "title_data.h"
#include "title_xapi.h"

// The frames our code links into FS:[0] and the funclet calls need exact register and stack control: three small
// asm helpers (bottom). Everything here runs between title frames that a catch abandons, so nothing may own
// resources (no destructors, no heap) across a call that can lead to a catch.

namespace game {
struct EhGuardNode;
struct EhCatchNode;
}  // namespace game

extern "C" {
// Calls a funclet (unwind action or catch block) with EBP = the frame's (node + 0xC); returns its EAX.
uint32_t title_eh_call_funclet(uint32_t funclet, uint32_t node);
// The same for a catch block, saving the stack position in `catch_node` first: title_eh_resume returns from this
// call with 0 (a rethrow left the catch block; the catch node's handler did the rest).
uint32_t title_eh_call_catch(uint32_t funclet, uint32_t node, game::EhCatchNode* catch_node);
[[noreturn]] void title_eh_resume(game::EhCatchNode* catch_node);
// Leaves for a catch's continuation: ESP = the frame's saved ESP (node - 4), EBP = node + 0xC.
[[noreturn]] void title_eh_jump(uint32_t continuation, uint32_t node);
}

namespace game {

namespace {

constexpr uint32_t kCxxException = 0xe06d7363;
constexpr uint32_t kCxxMagic = 0x19930520;
constexpr uint32_t kBreakpoint = 0x80000003;
constexpr uint32_t kUnwinding = 0x66;  // EXCEPTION_UNWINDING | EXIT_UNWIND | TARGET_UNWIND | COLLIDED_UNWIND
constexpr uint32_t kContinueSearch = 1;
constexpr uint32_t kRtlUnwindSlot = 0x007b3224;   // the kernel import RtlUnwind (stdcall frame, ip, record, value)

struct EhRecord {  // EXCEPTION_RECORD with a C++ exception's three parameters
    uint32_t code, flags, record, address, count;
    uint32_t magic, object, throw_info;
};

struct EhNode {  // EHRegistrationNode: the function's frame EBP is 0xC above, its ESP saved 4 below
    uint32_t next, handler;
    int32_t state;
};

struct FuncInfo {
    uint32_t magic;
    int32_t max_state;
    uint32_t unwind_map;  // UnwindMapEntry[max_state]
    uint32_t try_count;
    uint32_t try_map;     // TryBlockMapEntry[try_count]
};

struct UnwindEntry {
    int32_t to_state;
    uint32_t action;
};

struct TryEntry {
    int32_t low, high, catch_high, catch_count;
    uint32_t handlers;  // HandlerType[catch_count]
};

struct HandlerType {
    uint32_t adjectives;  // 1 const, 2 volatile, 8 reference, 0x80000000 COM+
    uint32_t type;        // TypeDescriptor, 0 = catch(...)
    int32_t catch_object; // frame offset of the catch object (from EBP)
    uint32_t address;     // the catch funclet
};

struct CatchableType {
    uint32_t properties;  // 1 simple type, 2 by reference only, 4 virtual base
    uint32_t type;
    int32_t mdisp, pdisp, vdisp;
    int32_t size;
    uint32_t copy;        // copy constructor (thiscall), 0 = bitwise
};

struct ThrowInfo {
    uint32_t attributes;  // 1 const, 2 volatile
    uint32_t destructor;
    uint32_t forward_compat;
    uint32_t types;       // CatchableTypeArray {count, CatchableType*[count]}
};

struct FrameInfo {  // the list of exception objects being caught (per thread data +0x84)
    uint32_t object;
    uint32_t next;
};

template <typename T>
T* at(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
template <typename T>
uint32_t address_of(T* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// Per-thread data words (title_crt.h TitleThreadData): the exception being caught and its context, the
// throw-in-progress count, the frame info list.
uint32_t& thread_word(uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(title_thread_data()) + offset);
}
uint32_t& current_exception() { return thread_word(0x78); }
uint32_t& current_context() { return thread_word(0x7c); }
uint32_t& processing_throw() { return thread_word(0x80); }
uint32_t& frame_infos() { return thread_word(0x84); }

[[noreturn]] void eh_fail(const char* what) {
    // VC's terminate() / _inconsistency(): abort.
    std::fprintf(stderr, "C++ exception handling: %s\n", what);
    std::fflush(stderr);
    std::abort();
}

bool is_cxx(const EhRecord* e) { return e->code == kCxxException && e->count == 3 && e->magic == kCxxMagic; }

const char* type_name(uint32_t type) { return at<const char>(type + 8); }

void unwind_nested_frames(uint32_t node, EhRecord* record) {
    // RtlUnwind to `node`: every newer frame's handler runs with the unwinding flag (their destructors) and leaves
    // the chain, which then starts at `node`. Then the flag goes again: the search for this exception continues.
    title_function<void XB_STDCALL(uint32_t, uint32_t, EhRecord*, uint32_t)>(title_global<uint32_t>(kRtlUnwindSlot))(
        node, 0, record, 0);
    record->flags &= ~2u;
}

int32_t frame_unwind_to_state(EhNode* node, const FuncInfo* info, int32_t target) {
    int32_t state = node->state;
    ++processing_throw();
    while (state != target) {
        if (state <= -1 || state >= info->max_state) eh_fail("unwind state out of range");
        const UnwindEntry& entry = at<const UnwindEntry>(info->unwind_map)[state];
        const int32_t next = entry.to_state;
        if (entry.action) {
            node->state = next;
            title_eh_call_funclet(entry.action, address_of(node));
        }
        state = next;
    }
    if (static_cast<int32_t>(processing_throw()) > 0) --processing_throw();
    node->state = state;
    return state;
}

// The try blocks a search at `catch_depth` looks at: inside a catch block (depth > 0) only those nested in it.
const TryEntry* tries_to_check(const FuncInfo* info, int32_t catch_depth, int32_t state, uint32_t* first,
                               uint32_t* end) {
    const auto* map = at<const TryEntry>(info->try_map);
    uint32_t start = info->try_count, last = start, last1 = start;
    while (catch_depth >= 0) {
        if (start == 0xffffffffu) eh_fail("catch depth");
        --start;
        if ((map[start].high < state && state <= map[start].catch_high) || start == 0xffffffffu) {
            --catch_depth;
            last = last1;
            last1 = start;
        }
    }
    ++start;
    *first = start;
    *end = last;
    if (last > info->try_count || start > last) eh_fail("try range");
    return map + start;
}

bool type_match(const HandlerType* handler, const CatchableType* type, const ThrowInfo* thrown) {
    if (!handler->type || !*type_name(handler->type)) return true;  // catch(...)
    if (handler->type != type->type && std::strcmp(type_name(handler->type), type_name(type->type)) != 0)
        return false;
    if ((type->properties & 2) && !(handler->adjectives & 8)) return false;
    if ((thrown->attributes & 1) && !(handler->adjectives & 1)) return false;
    if ((thrown->attributes & 2) && !(handler->adjectives & 2)) return false;
    return true;
}

uint32_t adjust_pointer(uint32_t object, const CatchableType* type) {
    uint32_t result = object + static_cast<uint32_t>(type->mdisp);
    if (type->pdisp >= 0) {
        const uint32_t vbtable = *at<uint32_t>(object + static_cast<uint32_t>(type->pdisp));
        result += *at<uint32_t>(vbtable + static_cast<uint32_t>(type->vdisp)) + static_cast<uint32_t>(type->pdisp);
    }
    return result;
}

void build_catch_object(const EhRecord* record, uint32_t node, const HandlerType* handler, const CatchableType* type) {
    if (!handler->type || !*type_name(handler->type) || (!handler->catch_object && !(handler->adjectives & 0x80000000u)))
        return;
    const uint32_t slot = node + 0xc + static_cast<uint32_t>(handler->catch_object);
    const uint32_t object = record->object;
    if (handler->adjectives & 8) {  // by reference
        *at<uint32_t>(slot) = adjust_pointer(object, type);
    } else if (type->properties & 1) {  // simple type: bitwise, a pointer adjusted
        std::memmove(at<void>(slot), at<void>(object), static_cast<size_t>(type->size));
        if (type->size == 4 && *at<uint32_t>(slot)) *at<uint32_t>(slot) = adjust_pointer(*at<uint32_t>(slot), type);
    } else if (!type->copy) {
        std::memmove(at<void>(slot), at<void>(adjust_pointer(object, type)), static_cast<size_t>(type->size));
    } else if (type->properties & 4) {  // copy constructor with the virtual base flag
        title_function<void XB_THISCALL(uint32_t, uint32_t, int32_t)>(type->copy)(slot, adjust_pointer(object, type), 1);
    } else {
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(type->copy)(slot, adjust_pointer(object, type));
    }
}

void destruct_exception_object(const EhRecord* record) {
    if (!record) return;
    const auto* thrown = at<const ThrowInfo>(record->throw_info);
    if (thrown->destructor) title_function<void XB_THISCALL(uint32_t)>(thrown->destructor)(record->object);
}

bool object_to_be_destroyed(uint32_t object) {  // no other catch still uses it
    for (uint32_t f = frame_infos(); f; f = at<FrameInfo>(f)->next)
        if (at<FrameInfo>(f)->object == object) return false;
    return true;
}

void unlink_frame_info(FrameInfo* frame) {
    if (address_of(frame) == frame_infos()) {
        frame_infos() = frame->next;
        return;
    }
    for (uint32_t f = frame_infos(); at<FrameInfo>(f)->next; f = at<FrameInfo>(f)->next)
        if (at<FrameInfo>(f)->next == address_of(frame)) {
            at<FrameInfo>(f)->next = frame->next;
            return;
        }
    eh_fail("frame info list");
}

uint32_t XB_CDECL internal_handler(EhRecord* record, EhNode* node, uint32_t context, uint32_t dispatcher,
                                   const FuncInfo* info, int32_t catch_depth, uint32_t marker, bool recursive);

}  // namespace

// The catch guard: linked around a catch block, so a throw from inside it searches this function's try blocks
// nested in the catch (one catch depth deeper).
struct EhGuardNode {
    uint32_t next, handler;
    uint32_t cookie;
    const void* info;
    EhNode* node;
    int32_t catch_depth;
};

// CallCatchBlock's own frame (the title's was a __try/__except/__finally): a rethrow out of the catch block ends it
// (the search for the original exception goes on), and an unwind past it (a new exception caught further out) runs
// its clean-up.
struct EhCatchNode {
    uint32_t next, handler;
    uint32_t saved_esp;  // title_eh_call_catch's
    EhRecord* record;
    EhNode* node;
    const FuncInfo* info;
    uint32_t handler_address;
    uint32_t saved_frame_esp;
    uint32_t saved_exception, saved_context;
    FrameInfo frame_info;
    uint32_t continuation;
    uint32_t done;
};

namespace {

uint32_t XB_CDECL catch_guard_handler(EhRecord* record, EhGuardNode* guard, uint32_t context, uint32_t);
uint32_t XB_CDECL catch_node_handler(EhRecord* record, EhCatchNode* catch_node, uint32_t context, uint32_t);

// CallCatchBlock's __finally: the frame's saved ESP and the current exception back, the exception object destroyed
// unless a rethrow passes it on or an enclosing catch still holds it.
void finish_catch(EhCatchNode* c, bool abnormal) {
    if (c->done) return;
    c->done = 1;
    *at<uint32_t>(address_of(c->node) - 4) = c->saved_frame_esp;
    unlink_frame_info(&c->frame_info);
    current_exception() = c->saved_exception;
    current_context() = c->saved_context;
    (void)abnormal;  // only changes what a throwing destructor does (terminate either way)
    if (is_cxx(c->record) && c->continuation && object_to_be_destroyed(c->record->object))
        destruct_exception_object(c->record);
}

uint32_t call_catch_block(EhRecord* record, EhNode* node, uint32_t context, const FuncInfo* info,
                          uint32_t handler_address, int32_t catch_depth) {
    EhCatchNode c{};
    c.handler = own_address(&catch_node_handler);
    c.record = record;
    c.node = node;
    c.info = info;
    c.handler_address = handler_address;
    c.continuation = handler_address;
    c.saved_frame_esp = *at<uint32_t>(address_of(node) - 4);
    c.frame_info.object = record->object;
    c.frame_info.next = frame_infos();
    frame_infos() = address_of(&c.frame_info);
    c.saved_exception = current_exception();
    c.saved_context = current_context();
    current_exception() = address_of(record);
    current_context() = context;
    c.next = platform_seh_head();
    platform_set_seh_head(address_of(&c));

    EhGuardNode guard{};
    guard.handler = own_address(&catch_guard_handler);
    guard.cookie = title_global<uint32_t>(0x008d1a50);
    guard.info = info;
    guard.node = node;
    guard.catch_depth = catch_depth + 1;
    guard.next = platform_seh_head();
    platform_set_seh_head(address_of(&guard));
    const uint32_t continuation = title_eh_call_catch(handler_address, address_of(node), &c);
    if (continuation || !c.done) {  // the catch block returned (0 from title_eh_call_catch: resumed after a rethrow)
        platform_set_seh_head(guard.next);
        c.continuation = continuation;
        finish_catch(&c, false);
    }
    platform_set_seh_head(c.next);
    return c.continuation;
}

void catch_it(EhRecord* record, EhNode* node, uint32_t context, const FuncInfo* info, const HandlerType* handler,
              const CatchableType* type, const TryEntry* entry, int32_t catch_depth, uint32_t marker) {
    if (type) build_catch_object(record, address_of(node), handler, type);
    unwind_nested_frames(marker ? marker : address_of(node), record);
    frame_unwind_to_state(node, info, entry->low);
    node->state = entry->high + 1;
    const uint32_t continuation = call_catch_block(record, node, context, info, handler->address, catch_depth);
    if (continuation) {
        platform_set_seh_head(address_of(node));
        title_eh_jump(continuation, address_of(node));
    }
}

void find_handler_for_foreign_exception(EhRecord* record, EhNode* node, uint32_t context, const FuncInfo* info,
                                        int32_t state, int32_t catch_depth, uint32_t marker) {
    if (record->code == kBreakpoint) return;
    if (thread_word(0x70)) eh_fail("structured exception translator (not ported)");
    uint32_t first, end;
    const TryEntry* entry = tries_to_check(info, catch_depth, state, &first, &end);
    for (; first < end; ++first, ++entry) {
        if (state < entry->low || state > entry->high) continue;
        const HandlerType* last = at<const HandlerType>(entry->handlers) + (entry->catch_count - 1);
        if (last->type && *type_name(last->type)) continue;  // only catch(...) takes a foreign exception
        catch_it(record, node, context, info, last, nullptr, entry, catch_depth, marker);
    }
}

void find_handler(EhRecord* record, EhNode* node, uint32_t context, const FuncInfo* info, bool recursive,
                  int32_t catch_depth, uint32_t marker) {
    const int32_t state = node->state;
    if (state < -1 || state >= info->max_state) eh_fail("state out of range");
    if (record->code == kCxxException && record->count == 3 && record->magic == kCxxMagic && !record->throw_info) {
        // `throw;`: the exception being caught
        if (!current_exception()) return;
        record = at<EhRecord>(current_exception());
        context = current_context();
        if (is_cxx(record) && !record->throw_info) eh_fail("rethrow without an exception");
    }
    if (!is_cxx(record)) {
        if (recursive) eh_fail("foreign exception, recursive");
        find_handler_for_foreign_exception(record, node, context, info, state, catch_depth, marker);
        return;
    }
    uint32_t first, end;
    const TryEntry* entry = tries_to_check(info, catch_depth, state, &first, &end);
    const auto* thrown = at<const ThrowInfo>(record->throw_info);
    for (; first < end; ++first, ++entry) {
        if (entry->low > state || state > entry->high) continue;
        const auto* handler = at<const HandlerType>(entry->handlers);
        bool caught = false;
        for (int32_t h = entry->catch_count; h > 0 && !caught; --h, ++handler) {
            const uint32_t* types = at<const uint32_t>(thrown->types);
            const int32_t count = static_cast<int32_t>(types[0]);
            for (int32_t t = 0; t < count; ++t) {
                const auto* type = at<const CatchableType>(types[1 + t]);
                if (!type_match(handler, type, thrown)) continue;
                catch_it(record, node, context, info, handler, type, entry, catch_depth, marker);
                caught = true;  // the catch block rethrew: on to the next try block
                break;
            }
        }
    }
    if (recursive) destruct_exception_object(record);  // a translated exception (never: no translator)
}

uint32_t XB_CDECL internal_handler(EhRecord* record, EhNode* node, uint32_t context, uint32_t dispatcher,
                                   const FuncInfo* info, int32_t catch_depth, uint32_t marker, bool recursive) {
    (void)dispatcher;
    if ((info->magic & 0x1fffffff) != kCxxMagic) eh_fail("FuncInfo magic");
    if (record->flags & kUnwinding) {
        if (info->max_state && !catch_depth) frame_unwind_to_state(node, info, -1);
    } else if (info->try_count) {
        find_handler(record, node, context, info, recursive, catch_depth, marker);
    }
    return kContinueSearch;
}

uint32_t XB_CDECL catch_guard_handler(EhRecord* record, EhGuardNode* guard, uint32_t context, uint32_t) {
    return internal_handler(record, guard->node, context, 0, static_cast<const FuncInfo*>(guard->info), guard->catch_depth,
                     address_of(guard), false);
}

uint32_t XB_CDECL catch_node_handler(EhRecord* record, EhCatchNode* c, uint32_t, uint32_t) {
    if (record->flags & kUnwinding) {  // a new exception caught further out: this catch block is left for good
        finish_catch(c, true);
        return kContinueSearch;
    }
    if (!(record->code == kCxxException && record->count == 3 && record->magic == kCxxMagic && !record->throw_info))
        return kContinueSearch;
    // A rethrow leaves the catch block: frames newer than this one unwind, the function goes back to the state
    // around the try block's catch, and the catch ends without a continuation (the search for the exception it
    // caught goes on outside).
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(title_global<uint32_t>(kRtlUnwindSlot))(
        address_of(c), 0, 0, 0);
    int32_t state = c->node->state;
    const auto* map = at<const TryEntry>(c->info->try_map);
    for (uint32_t i = 0; i < c->info->try_count; ++i)
        if (state > map[i].high && state <= map[i].catch_high) {
            state = at<const UnwindEntry>(c->info->unwind_map)[map[i].high + 1].to_state;
            break;
        }
    frame_unwind_to_state(c->node, c->info, state);
    c->continuation = 0;
    finish_catch(c, false);
    platform_set_seh_head(c->next);
    title_eh_resume(c);
}

}  // namespace

void XB_STDCALL title_CxxThrowException(void* object, uint32_t throw_info) {
    const uint32_t arguments[3] = {kCxxMagic, address_of(object), throw_info};
    RaiseException(kCxxException, 1, 3,
                                                                                                  arguments);
}

}  // namespace game

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_cxx_frame_handler(void* record, void* node, uint32_t context,
                                                                     uint32_t dispatcher, const void* info) {
    return game::internal_handler(static_cast<game::EhRecord*>(record), static_cast<game::EhNode*>(node), context,
                                  dispatcher, static_cast<const game::FuncInfo*>(info), 0, 0, false);
}

// title_eh_call_funclet(funclet, node) / title_eh_call_catch(funclet, node, catch_node): EBP = node + 0xC for the
// funclet, every callee-saved register kept; the catch variant stores its ESP (after the saves) in catch_node + 8,
// where title_eh_resume returns through with EAX = 0.
PLATFORM_ASM_ENTRY(title_eh_call_funclet,
                   "pushl %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 20(%esp), %eax\n"
                   "movl 24(%esp), %ebp\n"
                   "addl $12, %ebp\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")
PLATFORM_ASM_ENTRY(title_eh_call_catch,
                   "pushl %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 28(%esp), %eax\n"
                   "movl %esp, 8(%eax)\n"
                   "movl 20(%esp), %eax\n"
                   "movl 24(%esp), %ebp\n"
                   "addl $12, %ebp\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")
PLATFORM_ASM_ENTRY(title_eh_resume,
                   "movl 4(%esp), %eax\n"
                   "movl 8(%eax), %esp\n"
                   "xorl %eax, %eax\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")
PLATFORM_ASM_ENTRY(title_eh_jump,
                   "movl 4(%esp), %eax\n"
                   "movl 8(%esp), %ecx\n"
                   "movl -4(%ecx), %esp\n"
                   "leal 12(%ecx), %ebp\n"
                   "jmp *%eax")
// ___CxxFrameHandler: FuncInfo in EAX, then the four handler arguments.
PLATFORM_ASM_ENTRY(title_CxxFrameHandler_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "cld\n"
                   "pushl %eax\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   PLATFORM_ASM_CALL(title_cxx_frame_handler)
                   "addl $20, %esp\n"
                   "popl %ebp\n"
                   "ret")
// __EH_prolog (called first thing by functions with EH, handler stub in EAX): pushes the registration node
// {fs:[0], EAX, state -1} under the caller's saved EBP, links it, sets the caller's EBP to the frame and returns.
PLATFORM_ASM_ENTRY(title_EH_prolog_entry,
                   "pushl $-1\n"
                   "pushl %eax\n"
                   "movl %fs:0, %eax\n"
                   "pushl %eax\n"
                   "movl 12(%esp), %eax\n"
                   "movl %esp, %fs:0\n"
                   "movl %ebp, 12(%esp)\n"
                   "leal 12(%esp), %ebp\n"
                   "pushl %eax\n"
                   "ret")
