#include "g_scr_runtime.h"

#include <cstring>

#include "g_main.h"
#include "lanes.h"
#include "qcommon_core.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t hash_key(const char* name) {
    return title_function<uint32_t XB_CDECL(const char*)>(title_global<uint32_t>(0x009229e4))(name);
}

// The live thread a handle names (low 8 bits index, the rest the generation), or 0.
uint8_t* thread_of(uint32_t handle) {
    const uint32_t index = handle & 0xff;
    if ((handle >> 8) != title_global<uint32_t>(0x008dd22c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x008dd228 + index * 8);
}

}  // namespace

uint32_t* XB_CDECL Scr_HashName(uint32_t* out, const char* name) {
    *out = hash_key(name);
    return out;
}

void XB_CDECL Scr_EndOnString(uint32_t thread, const char* name) {
    title_function<void XB_CDECL(uint32_t, uint32_t)>(title_global<uint32_t>(0x00922188))(thread, hash_key(name));
}

uint32_t XB_CDECL Scr_InThread() { return title_global<uint32_t>(0x008dda2c) != 0; }

uint32_t XB_CDECL Scr_ThreadHandleIsValid(uint32_t handle) { return thread_of(handle) != nullptr; }

uint32_t XB_CDECL Scr_EntityHandleIsValid(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
    return title_global<uint32_t>(0x0097a968 + index * 8) != 0;
}

void XB_CDECL Scr_TerminateThread(uint32_t handle) {
    if (uint8_t* thread = thread_of(handle)) *reinterpret_cast<uint32_t*>(thread + 0x10) |= 0x4c;
}

namespace {

// SmallHeap_Alloc (0x00044850): heap in ECX, size and a flag on the stack, callee pops.
uint32_t* small_heap_alloc(uint32_t heap, uint32_t size) {
    return title_function<uint32_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(
        title_global<uint32_t>(heap), size, 0);
}

// Appends a notify record to the pending list.
void queue_notify(uint32_t* node) {
    node[0] = 0x008dd200;
    uint32_t* tail = title_global<uint32_t*>(0x008dd204);
    node[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tail));
    tail[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    ++title_global<uint32_t>(0x008dd1f8);
    title_global<uint32_t*>(0x008dd204) = node;
}

// G_EntityRunNotifyCallbacks (0x002b6de0): the entity's callback block (+0x234) in ECX, then the entity, the
// notify and the parameter block (ret 0xc).
using NotifyHandler = void XB_THISCALL(uint32_t, uint8_t*, uint32_t, const uint32_t*);

uint32_t entity_number(const uint8_t* ent) { return *reinterpret_cast<const uint32_t*>(ent + 0x224); }

}  // namespace

PLATFORM_ASM_ENTRY(Scr_Notify_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Scr_Notify) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL Scr_Notify(uint8_t* ent, uint32_t notify) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
        node[4] = 0;
    }
    queue_notify(node);
    if (const uint32_t handlers = *reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(handlers, ent, notify, nullptr);
}

// The handler gets the parameter block's address and keeps it in a global (0x008dd1e4) for the
// script thread: the block sits where the original's frame has it (entry ESP - 0x10) and the
// handler runs at the original's ESP. The asm entry keeps the area above the C frame free.
PLATFORM_ASM_ENTRY(G_NotifyWithEntity_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithEntity) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $8")

void XB_CDECL G_NotifyWithEntity(uint32_t entry, uint8_t* ent, uint32_t notify, const uint32_t* other, uint32_t ebx,
                                 uint32_t esi) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x10);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x007f964c;
            argument[3] = *other;
        }
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    }
    queue_notify(node);
    const uint32_t f = title_global<uint32_t>(0x00922eb8);  // the script's undefined float
    const uint32_t block = entry - 0x10;
    uint32_t* parameters = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(block));
    parameters[0] = *other;
    parameters[1] = 0;
    parameters[2] = f;
    parameters[3] = f;
    const uint32_t handler = *reinterpret_cast<const uint32_t*>(ent + 0x234);
    if (!handler) return;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    const TitleRegs regs = {notify, ebx, handler, block, esi, self};
    const uint32_t args[3] = {self, notify, block};
    title_call_at_regs(block, 0x002b6de0, &regs, 3, args);
}

// Calls G_NotifyWithEntity at its title address where the original's call puts it (the
// entity number at entry ESP - 4).
PLATFORM_ASM_ENTRY(G_NotifyWithOwnHandle_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithOwnHandle) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_NotifyWithOwnHandle(uint32_t entry, const uint8_t* ent, uint8_t* notified, uint32_t notify,
                                    uint32_t ebx, uint32_t esi) {
    const uint32_t local = entry - 4;
    *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(local)) = entity_number(ent);
    const TitleRegs regs = {notify, ebx, entity_number(ent), local, esi,
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(notified))};
    const uint32_t args[2] = {notify, local};
    title_call_at_regs(local, 0x000265a0, &regs, 2, args);  // G_NotifyWithEntity
}

void XB_CDECL Scr_QueueNotify(uint32_t handle, uint32_t notify) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = handle;
        node[4] = 0;
    }
    queue_notify(node);
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    uint8_t* ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!ent) return;
    if (const uint32_t handlers = *reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(handlers, ent, notify, nullptr);
}

namespace {

// A queued notify carrying one value of the given kind (vtable) - {0, 0, notify, handle,
// value record from heap 0x0095f574}.
void queue_notify_with_value(uint32_t kind, uint32_t handle, uint32_t notify, uint32_t value) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x10);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = kind;
            argument[3] = value;
        }
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = handle;
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    }
    queue_notify(node);
}

}  // namespace

void XB_CDECL Scr_QueueNotifyCallback26(uint32_t handle, uint32_t notify, uint32_t value) {
    queue_notify_with_value(0x008317d8, handle, notify, value);
}

void XB_CDECL Scr_QueueNotifyCallback29(uint32_t handle, uint32_t notify, uint32_t value) {
    queue_notify_with_value(0x008317ec, handle, notify, value);
}

namespace {

// 0x000147e0: takes a free slot of the thread handle pool (EAX = 0x008dd208) and writes its
// handle to the word EBX points to.
extern "C" PLATFORM_HIDDEN void XB_CDECL thread_handle_alloc_call(uint32_t* out);
PLATFORM_ASM_ENTRY(thread_handle_alloc_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "movl $0x008dd208, %eax\n"
                   "movl $0x000147e0, %ecx\n"
                   "call *%ecx\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

uint32_t XB_CDECL Scr_GetCurrentThreadHandle() {
    uint8_t* thread = title_global<uint8_t*>(0x008dda2c);
    if (!thread) return 0;
    uint32_t& handle = *reinterpret_cast<uint32_t*>(thread + 0x14);
    if (!handle) {
        uint32_t allocated;
        thread_handle_alloc_call(&allocated);
        const uint32_t index = allocated & 0xff;
        title_global<uint8_t*>(0x008dd228 + index * 8) = thread;
        handle = allocated;
    }
    return handle;
}

PLATFORM_ASM_ENTRY(Scr_TeamFromString_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Scr_TeamFromString) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Scr_TeamFromString(const void* name) {
    static const uint32_t kTeams[3][2] = {{0x00984cd0, 2}, {0x00984cd4, 4}, {0x00984cd8, 8}};
    const auto equals = title_function<uint32_t XB_CDECL(const void*, uint32_t)>(0x0011d820);  // ScriptString_EqualsString
    for (const auto& team : kTeams)
        if (static_cast<uint8_t>(equals(name, team[0]))) return team[1];
    title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    return 0;
}

PLATFORM_ASM_ENTRY(G_SetSoundDoneNotify_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SetSoundDoneNotify) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_SetSoundDoneNotify(uint8_t* ent, uint32_t notify) {
    uint32_t* pending = reinterpret_cast<uint32_t*>(ent + 0x3c4);
    if (ent && pending[0]) {
        Scr_Notify(ent, pending[0]);
        pending[0] = 0;
        pending[1] = 0;
    }
    pending[1] = 0;
    pending[0] = notify;
}

uint32_t* XB_CDECL Scr_Alloc(uint32_t size, uint32_t flags) {
    const uint8_t* small = title_global<const uint8_t*>(0x0095f4a4);
    int32_t last = *reinterpret_cast<const int32_t*>(small + 0x24) - 1;
    if (last <= 0) last = 0;
    const int32_t largest = *reinterpret_cast<const int16_t*>(small + 0x28 + last * 2);
    if (size <= static_cast<uint32_t>(largest)) {
        if (uint32_t* block = small_heap_alloc(0x0095f4a4, size)) return block;
    }
    // Heap_AllocFromHeap (0x00042de0): heap in ECX, callee pops. The original traps (int3) when
    // it fails too.
    return title_function<uint32_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00042de0)(
        title_global<uint32_t>(0x0095f4a0), size, flags);
}

uint32_t* XB_STDCALL Scr_AllocPtrArray(uint32_t count, uint32_t) {
    uint32_t* array = Scr_Alloc(count * 4, 4);
    for (uint32_t* slot = array; slot != array + count; ++slot)
        if (slot) *slot = 0;
    return array;
}

void XB_CDECL Scr_Free(void* block) {
    using HeapCall = uint32_t XB_THISCALL(uint32_t, void*);
    const uint32_t small = title_global<uint32_t>(0x0095f4a4);
    if (static_cast<uint8_t>(title_function<HeapCall>(0x00044960)(small, block)))  // SmallHeap_Owns (AL)
        title_function<HeapCall>(0x00044910)(title_global<uint32_t>(0x0095f4a4), block);  // SmallHeap_Free
    else
        title_function<HeapCall>(0x00042e30)(title_global<uint32_t>(0x0095f4a0), block);  // Heap_FreeIn2
}

uint32_t* XB_CDECL Scr_HashScriptString(uint32_t* out, const uint8_t* const* string) {
    *out = hash_key(*string ? reinterpret_cast<const char*>(*string + 6) : nullptr);
    return out;
}

void XB_CDECL NotifyList_FreeRange(uint32_t* first, uint32_t* next, uint32_t*, uint32_t* end_next, uint32_t) {
    // The original keeps the node it frees one step behind the walk (first is freed first) and
    // keeps the current node in next's argument slot (callee scratch).
    uint32_t* current = first;
    uint32_t* walk = next;
    if (walk == end_next) return;
    do {
        uint32_t* const node = current;
        if (walk) {
            current = walk;
            walk = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(*walk));
        }
        if (node) {
            if (const uint32_t argument = node[4]) {
                const uint32_t vtable = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(argument));
                title_function<void XB_THISCALL(uint32_t, uint32_t)>(
                    *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(vtable)))(argument, 1);
            }
            node[4] = 0;
            SmallHeap_Free(title_global<uint8_t*>(0x0095f56c), reinterpret_cast<uint8_t*>(node));
        }
    } while (walk != end_next);
}

uint8_t* XB_THISCALL EntNotifyBlock_DeletingDtor(uint8_t* self, uint32_t flags) {
    if (uint8_t* const next = *reinterpret_cast<uint8_t**>(self + 0x38)) EntNotifyBlock_DeletingDtor(next, 1);
    if (flags & 1) SmallHeap_Free(title_global<uint8_t*>(0x0095f568), self);
    return self;
}

PLATFORM_ASM_ENTRY(Scr_NotifyWithString_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Scr_NotifyWithString) "addl $12, %esp\n"
                   "ret $8")

void XB_CDECL Scr_NotifyWithString(uint8_t* ent, uint32_t notify, const ScriptString* text) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x10);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x007f963c;
            ScriptString_Copy(reinterpret_cast<ScriptString*>(argument + 3), text);
        }
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
    }
    queue_notify(node);
    if (const uint32_t handlers = *reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(handlers, ent, notify, nullptr);
}

// G_NotifyWithEntity's twin with value kind 0x008317d8 (same frame: the parameter block at entry
// ESP - 0x10, the handler run there).
PLATFORM_ASM_ENTRY(G_NotifyWithEntity_2_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithEntity_2) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $8")

void XB_CDECL G_NotifyWithEntity_2(uint32_t entry, uint8_t* ent, uint32_t notify, const uint32_t* other, uint32_t ebx,
                                   uint32_t esi) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x10);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x008317d8;
            argument[3] = *other;
        }
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    }
    queue_notify(node);
    const uint32_t f = title_global<uint32_t>(0x00922eb8);  // the script's undefined float
    const uint32_t block = entry - 0x10;
    uint32_t* parameters = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(block));
    parameters[1] = 0;
    parameters[2] = f;
    parameters[3] = f;
    parameters[0] = *other;
    const uint32_t handler = *reinterpret_cast<const uint32_t*>(ent + 0x234);
    if (!handler) return;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    const TitleRegs regs = {notify, ebx, handler, block, esi, self};
    const uint32_t args[3] = {self, notify, block};
    title_call_at_regs(block, 0x002b6de0, &regs, 3, args);
}

// The parameter block {other's handle, 0, value as a float, undefined float} sits at entry ESP -
// 0x10, where the handler keeps its address (0x008dd1e4); the handler runs at the original's ESP
// with its registers (EBX = entity, ESI = the queued record, EDI = 0).
PLATFORM_ASM_ENTRY(G_NotifyWithIntEntity_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithIntEntity) "addl $20, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $16")

void XB_CDECL G_NotifyWithIntEntity(uint32_t entry, uint8_t* ent, uint32_t notify, const int32_t* value,
                                    const uint32_t* other) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x14);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x00839c20;
            argument[3] = static_cast<uint32_t>(*value);
            argument[4] = *other;
        }
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    }
    queue_notify(node);
    const uint32_t block = entry - 0x10;
    uint32_t* parameters = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(block));
    parameters[3] = title_global<uint32_t>(0x00922eb8);  // the script's undefined float
    const float as_float = static_cast<float>(*value);
    std::memcpy(&parameters[2], &as_float, 4);
    parameters[1] = 0;
    parameters[0] = *other;
    const uint32_t handler = *reinterpret_cast<const uint32_t*>(ent + 0x234);
    if (!handler) return;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    const TitleRegs regs = {notify, self, handler, block, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)), 0};
    const uint32_t args[3] = {self, notify, block};
    title_call_at_regs(block, 0x002b6de0, &regs, 3, args);
}

PLATFORM_ASM_ENTRY(G_NotifyWithTwoStrings_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_NotifyWithTwoStrings) "addl $16, %esp\n"
                   "ret $12")

void XB_CDECL G_NotifyWithTwoStrings(uint8_t* ent, uint32_t notify, const ScriptString* first,
                                     const ScriptString* second) {
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x14);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x00839c2c;
            ScriptString_Copy(reinterpret_cast<ScriptString*>(argument + 3), first);
            ScriptString_Copy(reinterpret_cast<ScriptString*>(argument + 4), second);
        }
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = entity_number(ent);
    }
    queue_notify(node);
    // The original tail-jumps to the handler with its own argument slots rewritten.
    if (const uint32_t handlers = *reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(handlers, ent, notify, nullptr);
}

// The script wait family (not registered: the waits suspend the script thread - Scr_Suspend*
// save the thread's stack and registers and switch away - so a test process cannot run them).

namespace {

// After a wait: a thread being torn down (g_scrThreadAborting) throws a one-byte object (the low
// byte of the wait's first argument; type info 0x0088ac80) to unwind the script function.
void wait_done(uint32_t first_argument) {
    if (!title_global<uint8_t>(0x00922164)) return;
    uint8_t object = static_cast<uint8_t>(first_argument);
    title_function<void XB_STDCALL(void*, uint32_t)>(0x001f2302)(&object, 0x0088ac80);  // _CxxThrowException
}

void suspend(uint32_t function) { title_function<void XB_CDECL()>(function)(); }

// The wait-till record (0x00922130 notify, 0x00922134 object, 0x00922e6c, 0x00922120 /
// 0x0092212c / 0x00922e60 arguments, 0x00922e5c flag, 0x00922138 timeout), then
// Scr_SuspendWaitTill.
void wait_till(uint32_t object, uint32_t notify, uint32_t a, uint32_t b, uint32_t c, uint8_t flag, float timeout) {
    title_global<uint32_t>(0x00922134) = object;
    title_global<uint32_t>(0x00922e6c) = 0;
    title_global<uint32_t>(0x00922130) = notify;
    title_global<uint32_t>(0x00922120) = a;
    title_global<uint32_t>(0x0092212c) = b;
    title_global<uint32_t>(0x00922e60) = c;
    title_global<uint8_t>(0x00922e5c) = flag;
    title_global<float>(0x00922138) = timeout;
    suspend(0x0011d9d0);  // Scr_SuspendWaitTill
}

}  // namespace

uint8_t XB_CDECL Scr_ReturnFalse() { return 0; }

void XB_CDECL Scr_WaitExact(float seconds) {
    uint32_t bits;
    std::memcpy(&bits, &seconds, 4);
    title_global<uint32_t>(0x00922124) = bits;  // g_scrWaitTime
    suspend(0x0011d950);                        // Scr_SuspendWaitTime
    wait_done(bits);
}

void XB_CDECL Scr_Wait(float seconds) {
    // A random time between seconds and 5% more (Scr_RandomFloatRange returns on the x87).
    const float longest = seconds * from_bits(0x3f866666);
    title_global<float>(0x00922124) =
        title_function<float XB_CDECL(float, float)>(title_global<uint32_t>(0x00922238))(seconds, longest);
    suspend(0x0011d950);  // Scr_SuspendWaitTime
    uint32_t bits;
    std::memcpy(&bits, &seconds, 4);
    wait_done(bits);
}

void XB_CDECL Scr_WaitFrames(uint32_t frames) {
    title_global<uint32_t>(0x00922128) = frames;
    suspend(0x0011d990);  // Scr_SuspendWaitFrames
    wait_done(frames);
}

void XB_CDECL Scr_WaitTill(uint32_t object, uint32_t notify) {
    if (!object) return;
    wait_till(object, notify, 0, 0, 0, 0, 0.0f);
    wait_done(object);
}

void XB_CDECL Scr_WaitTillTimeout(uint32_t object, uint32_t notify, float timeout) {
    if (!object) return;
    wait_till(object, notify, 0, 0, 0, 0, timeout);
    wait_done(object);
}

void XB_CDECL Scr_WaitTillArgs(uint32_t object, uint32_t notify, uint32_t a, uint32_t b, uint32_t c) {
    wait_till(object, notify, a, b, c, 1, 0.0f);
    wait_done(object);
}

void XB_CDECL Scr_WaitTillArgsNoFlag(uint32_t object, uint32_t notify, uint32_t a, uint32_t b, uint32_t c) {
    wait_till(object, notify, a, b, c, 0, 0.0f);
    wait_done(object);
}

void XB_CDECL Scr_WaitTillString(uint32_t object, const char* name) {
    Scr_WaitTill(object, hash_key(name));
}

void XB_CDECL Scr_WaitOnObject(uint32_t object) {
    title_global<uint32_t>(0x00922e6c) = object;
    title_global<uint32_t>(0x00922130) = 1;
    suspend(0x0011d9d0);  // Scr_SuspendWaitTill
    wait_done(object);
}

void XB_CDECL Scr_WaitOnObjectNoFlag(uint32_t object) {
    title_global<uint32_t>(0x00922e6c) = object;
    title_global<uint32_t>(0x00922130) = 0;
    suspend(0x0011d9d0);  // Scr_SuspendWaitTill
    wait_done(object);
}

// 0x0011b830: Scr_WaitTillString with the script string's text (null for the empty string).
void XB_CDECL Scr_WaitTillScriptString(uint32_t object, const ScriptString* name) {
    const char* const text = name->rep ? name->rep->text : nullptr;
    Scr_WaitTillString(object, text);
}

namespace {

// GScr_GetNode / GScr_GetVehicleNode (interface slots 0x009221cc / 0x009221d0): up to `max` node
// handles matching (key, value) into `out`; returns the count.
using NodeQuery = int32_t XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t);
using ArrayCall = void XB_THISCALL(uint32_t*, uint32_t*);

// Shared copies in every level section at fixed addresses: DynArray_ReserveInt0 (0x0045d480) and
// DynArray_ReserveIntNeg1 (0x0045c4a0) (thiscall, two zero arguments).
void reserve_none(uint32_t function, uint32_t* array) {
    title_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(function)(array, 0, 0);
}

// Appends the nodes; a -1 handle goes in as 0 (ScrPtrArray_Append, 0x001e5ec0).
void append_nodes(uint32_t* array, const uint32_t* nodes, int32_t count) {
    for (int32_t i = 0; i < count; ++i) {
        uint32_t node = nodes[i];
        if (node == 0xffffffff) node = 0;
        title_function<ArrayCall>(0x001e5ec0)(array, &node);
    }
}

}  // namespace

void XB_CDECL Scr_GetNodeArray(uint32_t key, uint32_t value, uint32_t* array) {
    reserve_none(0x0045d480, array);
    uint32_t nodes[0x400];
    const int32_t count = title_function<NodeQuery>(title_global<uint32_t>(0x009221cc))(key, value, nodes, 0x400);
    PtrArray_Reserve(array, array[2] + count);
    append_nodes(array, nodes, count);
}

void XB_CDECL Scr_GetNodeArrayNoKey(uint32_t* array) {
    reserve_none(0x0045d480, array);
    ScriptString empty = {nullptr};
    uint32_t nodes[0x400];
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&empty));
    const int32_t count = title_function<NodeQuery>(title_global<uint32_t>(0x009221cc))(key, key, nodes, 0x400);
    if (count > 0) {
        PtrArray_Reserve(array, array[2] + count);
        append_nodes(array, nodes, count);
    }
    // The local string's destructor (inlined): one reference less, freed at zero.
    if (ScriptStringRep* const rep = empty.rep) {
        --rep->refs;
        if (static_cast<int16_t>(rep->refs) <= 0) Scr_Free(rep);
    }
}

void XB_CDECL Scr_GetVehicleNodeArray(uint32_t key, uint32_t value, uint32_t* array) {
    reserve_none(0x0045c4a0, array);
    uint32_t nodes[0x400];
    const int32_t count = title_function<NodeQuery>(title_global<uint32_t>(0x009221d0))(key, value, nodes, 0x400);
    if (count <= 0) return;
    PtrArray_Reserve2(array, array[2] + count);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t node = nodes[i];
        title_function<ArrayCall>(0x0045c550)(array, &node);  // vehicle node array append
    }
}

// The entity wait lists: records {next, prev, entity handle, wait count +0xc, waits (head +0x10,
// end node +0x14/+0x18), pending (head +0x20, end node +0x24/+0x28)} on the list 0x008dda8c (end
// node 0x008dda90, last 0x008dda94, count 0x008dda88).
uint32_t* XB_REGS3 XB_CDECL Scr_LinkEntityWaitList(uint32_t* list, uint32_t unused_edx, const uint8_t* ent) {
    (void)unused_edx;
    list[0] = 0;
    list[1] = 0;
    list[2] = *reinterpret_cast<const uint32_t*>(ent + 0x224);
    list[5] = 0;
    list[3] = 0;
    list[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 5));
    list[6] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 4));
    list[9] = 0;
    list[7] = 0;
    list[8] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 9));
    list[10] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 8));
    list[0] = 0x008dda90;
    list[1] = title_global<uint32_t>(0x008dda94);
    *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x008dda94))) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
    const uint32_t count = title_global<uint32_t>(0x008dda88) + 1;
    title_global<uint32_t>(0x008dda94) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
    title_global<uint32_t>(0x008dda88) = count;
    return list;
}

PLATFORM_ASM_ENTRY(Scr_EntityWaitList_FreeWaits_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Scr_EntityWaitList_FreeWaits) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Scr_EntityWaitList_FreeWaits(uint32_t* list) {
    // EBX list: every wait record (+0x10 list up to its end node +0x14) unlinked, the wait count
    // (+0xc) lowered, its value (+0x10) through its deleting destructor (vtable slot 0, flag 1) and
    // the record back to heap 0x0095f56c. 1 when both the wait and pending lists are empty.
    auto ptr = [](uint32_t value) { return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(value)); };
    const uint32_t end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 5));
    uint32_t node = list[4];
    uint32_t next = node ? *ptr(node) : 0;
    if (node != end && next) {
        do {
            const uint32_t current = node;
            if (next) {
                node = next;
                next = *ptr(next);
            }
            uint32_t* const c = ptr(current);
            ptr(c[0])[1] = c[1];
            ptr(c[1])[0] = c[0];
            --list[3];
            if (const uint32_t value = c[4]) {
                title_function<void XB_THISCALL(uint32_t, uint32_t)>(*ptr(*ptr(value)))(value, 1);
            }
            c[4] = 0;
            SmallHeap_Free(title_global<uint8_t*>(0x0095f56c), reinterpret_cast<uint8_t*>(c));
        } while (next);
    }
    if (list[4] != end) return 0;
    return list[8] == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 9)) ? 1 : 0;
}

void XB_REGS1 XB_CDECL Scr_UnlinkEntityWaitList(uint32_t* list) {
    // EAX list: off the global list (count 0x008dda88 lowered), its waits freed (NotifyList_FreeRange
    // over +0x10 up to the end node), the wait list emptied and the count cleared; the entity
    // (handle +8) forgets it (+0x230).
    auto ptr = [](uint32_t value) { return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(value)); };
    ptr(list[0])[1] = list[1];
    ptr(list[1])[0] = list[0];
    title_global<uint32_t>(0x008dda88) = title_global<uint32_t>(0x008dda88) - 1;
    uint32_t* const end = list + 5;
    uint32_t* first = ptr(list[4]);
    uint32_t* second = first ? ptr(*first) : nullptr;
    if (first == end) {
        second = nullptr;
        first = nullptr;
    }
    NotifyList_FreeRange(first, second, end, nullptr, 0);
    list[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end));
    list[6] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 4));
    list[3] = 0;
    const uint32_t handle = list[2];
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    if (!title_global<uint32_t>(0x0097a968 + index * 8)) return;
    *ptr(title_global<uint32_t>(0x0097a968 + index * 8) + 0x230) = 0;
}

void XB_CDECL Scr_PruneEntityWaitLists() {
    // Each entity wait list (0x008dda8c up to the end node 0x008dda90) gets its waits freed; empty
    // ones are unlinked and go back to heap 0x0095f570.
    auto ptr = [](uint32_t value) { return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(value)); };
    uint32_t list = title_global<uint32_t>(0x008dda8c);
    uint32_t next = list ? *ptr(list) : 0;
    if (list == 0x008dda90 || !next) return;
    do {
        const uint32_t current = list;
        if (next) {
            list = next;
            next = *ptr(next);
        }
        const TitleRegs regs{0, current, 0, 0, next, list};
        if ((g_ent_call(0x0029ac20, &regs, 0, nullptr) & 0xff) && current) {  // Scr_EntityWaitList_FreeWaits
            Scr_UnlinkEntityWaitList(ptr(current));
            SmallHeap_Free(title_global<uint8_t*>(0x0095f570), reinterpret_cast<uint8_t*>(ptr(current)));
        }
    } while (next);
}

}  // namespace game
