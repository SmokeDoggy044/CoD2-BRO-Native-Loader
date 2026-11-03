#include "g_scr_runtime.h"

#include <cstring>

#include "actor.h"
#include "dobj.h"
#include "g_main.h"
#include "ngl_debug.h"
#include "g_script.h"
#include "g_utils.h"
#include "qcommon_fs.h"
#include "sv.h"
#include "lanes.h"
#include "qcommon.h"
#include "xanim.h"
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

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }
uint8_t* ptr_at(uint8_t* base, uint32_t offset) { return reinterpret_cast<uint8_t*>(uintptr_t{word_at(base, offset)}); }
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

void XB_CDECL Scr_NopRefSetter() {}

void XB_CDECL Scr_NopFieldSetter() {}

uint8_t* XB_REGS3 XB_STDCALL Scr_InitWaitRecord4(uint8_t* record, uint32_t, uint32_t thread, uint32_t owner,
                                                 uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t flag) {
    // A wait record (vtable 0x008317f8) on four values; +0x28 bit n set for each value n that is 0.
    word_at(record, 4) = 0;
    word_at(record, 8) = 0;
    word_at(record, 0x10) = thread;
    word_at(record, 0x14) = owner;
    record[0xc] = 0;
    word_at(record, 0) = 0x008317f8;
    word_at(record, 0x18) = 0;
    word_at(record, 0x1c) = 0;
    word_at(record, 0x20) = 0;
    word_at(record, 0x24) = 0;
    record[0x2c] = static_cast<uint8_t>(flag);
    word_at(record, 0x28) = 0;
    word_at(record, 0x18) = a;
    word_at(record, 0x1c) = b;
    word_at(record, 0x20) = c;
    word_at(record, 0x24) = d;
    if (!word_at(record, 0x18)) word_at(record, 0x28) |= 1;
    if (!word_at(record, 0x1c)) word_at(record, 0x28) |= 2;
    if (!word_at(record, 0x20)) word_at(record, 0x28) |= 4;
    if (!word_at(record, 0x24)) word_at(record, 0x28) |= 8;
    return record;
}

void XB_REGS3 XB_CDECL Scr_RestoreThreadStack(const uint32_t* saved, uint32_t, uint8_t* dst) {
    // The saved stack: a chain of 0xfc-byte chunks (next chunk at +0xfc), copied in words.
    const uint8_t* chunk = reinterpret_cast<const uint8_t*>(uintptr_t{saved[0]});
    int32_t remaining = static_cast<int32_t>(saved[1]);
    ++title_global<uint32_t>(0x0098ad18);
    while (remaining) {
        const int32_t size = remaining < 0xfc ? remaining : 0xfc;
        if (size > 0) {
            for (uint32_t i = 0, words = ((static_cast<uint32_t>(size) - 1) >> 2) + 1; i < words; ++i)
                reinterpret_cast<uint32_t*>(dst)[i] = reinterpret_cast<const uint32_t*>(chunk)[i];
        }
        remaining -= size;
        dst += size;
        if (!remaining) break;
        chunk = reinterpret_cast<const uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(chunk + 0xfc)});
    }
}

namespace {

// Scr_Notify on the level entity (0x0095f534 +8) with "<prefix><name>".
void notify_level_prefixed(uint32_t prefix, const char* name) {
    using StringFrom = void XB_THISCALL(uint32_t*, const char*);
    uint32_t text;
    title_function<StringFrom>(0x0011eb50)(&text, reinterpret_cast<const char*>(uintptr_t{prefix}));  // ScriptString_FromCString
    title_function<StringFrom>(0x0011ebc0)(&text, name);  // ScriptString_AppendCString
    uint8_t* const level = reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(
        title_global<uint8_t*>(0x0095f534) + 8)});
    if (level) Scr_Notify(level, Com_HashKeyNoCase(0, text ? reinterpret_cast<const char*>(uintptr_t{text + 6}) : nullptr));
    title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&text);  // ScriptString_Release
}

}  // namespace

void XB_CDECL Scr_NotifyUnloaded(const char* name) { notify_level_prefixed(0x00832338, name); }  // "unloaded_"

void XB_CDECL Scr_NotifyLoaded(const char* name) { notify_level_prefixed(0x00832344, name); }  // "loaded_"

void XB_CDECL Scr_QueueNotifyWith2Args(uint32_t handle, uint32_t notify, uint32_t value, uint32_t value2) {
    // A value record of kind 0x00831810 {kind, 0, 0, value (float), value2}.
    uint32_t* node = small_heap_alloc(0x0095f56c, 0x14);
    if (node) {
        uint32_t* argument = small_heap_alloc(0x0095f574, 0x14);
        if (argument) {
            argument[1] = 0;
            argument[2] = 0;
            argument[0] = 0x00831810;
            argument[3] = value;
            argument[4] = value2;
        }
        node[0] = 0;
        node[1] = 0;
        node[2] = notify;
        node[3] = handle;
        node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    }
    queue_notify(node);
}

void XB_CDECL Scr_QueueNotifyWithString(uint32_t handle, uint32_t notify, const ScriptString* text) {
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
        node[3] = handle;
    }
    queue_notify(node);
}

uint32_t XB_CDECL Scr_SaveThreadLocals(uint32_t data) {
    // ScrThread_AppendData (0x002f6670; EAX data, ECX the current thread's +0x28, its +0x2c on the stack, ret 4).
    uint8_t* const thread = title_global<uint8_t*>(0x008dda2c);
    const TitleRegs regs{data, 0, word_at(thread, 0x28), 0, 0, 0};
    const uint32_t arg = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread + 0x2c));
    return g_ent_call(0x002f6670, &regs, 1, &arg);
}

void XB_STDCALL Scr_AbortAllThreads(uint8_t* scheduler) {
    // Every thread record in the list at +4 (sentinel +8) gets flags 0x40 | 8 (+0x10), then the pending
    // notify range at +0x24 (sentinel +0x28) is freed and the list emptied.
    uint32_t* node = reinterpret_cast<uint32_t*>(uintptr_t{word_at(scheduler, 4)});
    uint32_t* next = node ? reinterpret_cast<uint32_t*>(uintptr_t{*node}) : nullptr;
    if (node != reinterpret_cast<uint32_t*>(scheduler + 8) && next) {
        do {
            uint32_t* const record = node;
            if (next) {
                node = next;
                next = reinterpret_cast<uint32_t*>(uintptr_t{*next});
            }
            record[4] |= 0x40;
            record[4] |= 8;
        } while (next);
    }
    uint32_t* first = reinterpret_cast<uint32_t*>(uintptr_t{word_at(scheduler, 0x24)});
    uint32_t* const end = reinterpret_cast<uint32_t*>(scheduler + 0x28);
    uint32_t* first_next = first ? reinterpret_cast<uint32_t*>(uintptr_t{*first}) : nullptr;
    if (first == end) {
        first_next = nullptr;
        first = nullptr;
    }
    // The tag word is the argument slot with its low byte cleared (the original builds it there).
    NotifyList_FreeRange(first, first_next, end, nullptr,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scheduler)) & 0xffffff00u);
    word_at(scheduler, 0x24) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end));
    word_at(scheduler, 0x2c) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scheduler + 0x24));
    word_at(scheduler, 0x20) = 0;
}

PLATFORM_ASM_ENTRY(Scr_HasEndOnRecord_entry,
                   "pushl %esi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Scr_HasEndOnRecord) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Scr_HasEndOnRecord(uint8_t* thread, uint32_t value) {
    // A record of kind 3 (+0xc) on this value (+0x14) in the thread's list (+0x1c, sentinel +0x20).
    const uint32_t* node = reinterpret_cast<const uint32_t*>(uintptr_t{word_at(thread, 0x1c)});
    const uint32_t* next = node ? reinterpret_cast<const uint32_t*>(uintptr_t{*node}) : nullptr;
    if (node == reinterpret_cast<const uint32_t*>(thread + 0x20) || !next) return 0;
    do {
        if (node[3] == 3 && node[5] == value) return 1;
        node = next;
        next = reinterpret_cast<const uint32_t*>(uintptr_t{*next});
    } while (next);
    return 0;
}

PLATFORM_ASM_ENTRY(Scr_UpdateThreadWaits_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Scr_UpdateThreadWaits) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Scr_UpdateThreadWaits(uint8_t* thread) {
    uint32_t& flags = word_at(thread, 0x10);
    const uint32_t handle = word_at(thread, 8);
    const uint32_t index = handle & 0xfff;
    const bool owner_alive = index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8) &&
                             title_global<uint32_t>(0x0097a968 + index * 8);
    if (!owner_alive || ((flags >> 6) & 1)) {
        // The owner entity is gone (or the thread ended): ended (0x40 | 8), and runnable (4) unless bit 0.
        const uint32_t ended = flags | 0x40;
        const uint32_t marked = ended | 8;
        flags = ended;
        flags = marked;
        if (!(marked & 1)) flags = marked | 4;
        return;
    }
    // Each wait record (list +0x1c, sentinel +0x20; the link at record +4) is polled through its vtable +4:
    // 2 = done (runnable, wait bit 0x10 cleared), 3 = ended too; records with +0xc set are unlinked and deleted.
    uint32_t* node = reinterpret_cast<uint32_t*>(uintptr_t{word_at(thread, 0x1c)});
    uint32_t* next = node ? reinterpret_cast<uint32_t*>(uintptr_t{*node}) : nullptr;
    if (node == reinterpret_cast<uint32_t*>(thread + 0x20) || !next) return;
    do {
        uint32_t* const link = node;
        if (next) {
            node = next;
            next = reinterpret_cast<uint32_t*>(uintptr_t{*next});
        }
        uint8_t* const record = reinterpret_cast<uint8_t*>(link) - 4;
        const uint32_t result =
            title_function<uint32_t XB_THISCALL(uint8_t*, uint8_t*)>(word_at(ptr_at(record, 0), 4))(record, thread);
        if (result == 2 || result == 3) {
            if (result == 3) {
                const uint32_t ended = flags | 8;
                flags = ended;
                flags = ended | 0x40;
            }
            const uint32_t runnable = flags | 4;
            flags = runnable;
            flags = runnable & ~0x10u;
        }
        if (record[0xc]) {
            uint8_t* const after = ptr_at(record, 4);
            uint8_t* const before = ptr_at(record, 8);
            word_at(after, 4) = address_of(before);
            word_at(ptr_at(record, 8), 0) = word_at(record, 4);
            --word_at(thread, 0x18);
            title_function<void XB_THISCALL(uint8_t*, uint32_t)>(word_at(ptr_at(record, 0), 0))(record, 1);
        }
    } while (next);
}

void XB_CDECL Scr_DestroyThreadContents(uint8_t* thread, uint32_t ecx) {
    // The wait records (+0x1c, sentinel +0x20); the tag word is the entry's ECX with its low byte cleared (the
    // original builds it in its ECX slot).
    uint32_t* first = reinterpret_cast<uint32_t*>(uintptr_t{word_at(thread, 0x1c)});
    uint32_t* first_next = first ? reinterpret_cast<uint32_t*>(uintptr_t{*first}) : nullptr;
    uint32_t* const end = reinterpret_cast<uint32_t*>(thread + 0x20);
    if (first == end) {
        first_next = nullptr;
        first = nullptr;
    }
    title_function<void XB_CDECL(uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t)>(0x0001b580)(
        first, first_next, end, nullptr, ecx & 0xffffff00u);  // ScrWaitList_DeleteRange
    word_at(thread, 0x1c) = address_of(end);
    word_at(thread, 0x24) = address_of(thread + 0x1c);
    word_at(thread, 0x18) = 0;
    // The thread's context object (+0xc, deleted without freeing, then back to the small heap), its handle, and
    // the saved-stack chunks (+0x2c, next at +0xfc).
    uint8_t* const context = ptr_at(thread, 0xc);
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(word_at(ptr_at(context, 0), 0))(context, 0);
    title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x00044910)(title_global<uint32_t>(0x00947700),
                                                                    word_at(thread, 0xc));  // SmallHeap_Free
    if (const uint32_t handle = word_at(thread, 0x14)) HandleTable256_Free(&title_global<uint8_t>(0x008dd208), handle);
    while (const uint32_t chunk = word_at(thread, 0x2c)) {
        word_at(thread, 0x2c) = *reinterpret_cast<const uint32_t*>(uintptr_t{chunk + 0xfc});
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x00044910)(title_global<uint32_t>(0x0095f490),
                                                                        chunk);  // SmallHeap_Free
    }
}

PLATFORM_ASM_ENTRY(Scr_DestroyThreadContents_entry,
                   "pushl %ecx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Scr_DestroyThreadContents) "addl $8, %esp\n"
                   "ret")

void XB_REGS3 XB_CDECL Scr_FreeThread(uint8_t* scheduler, uint32_t, uint8_t* thread) {
    if (!thread) return;
    if (word_at(scheduler, 0x854) == address_of(thread)) word_at(scheduler, 0x854) = 0;
    const bool notified = (word_at(thread, 0x10) >> 11) & 1;
    word_at(ptr_at(thread, 0), 4) = word_at(thread, 4);
    word_at(ptr_at(thread, 4), 0) = word_at(thread, 0);
    // Threads started by a notify (flag 0x800) are counted at +0x10, the others at +0.
    if (notified)
        --word_at(scheduler, 0x10);
    else
        --word_at(scheduler, 0);
    Scr_DestroyThreadContents(thread, word_at(thread, 0));
    title_function<void XB_THISCALL(uint32_t, uint8_t*)>(0x00044910)(title_global<uint32_t>(0x0095f488),
                                                                    thread);  // SmallHeap_Free
}

namespace {

// A new thread record (0x34 bytes, heap 0x0095f488): {0, 0, a, b, flags 1, handle 0, wait list (+0x1c, sentinel
// +0x20), count 0, ...}, appended to the list at `list` (tail list + 4, count at `count`); with `with_handle` it
// gets a script thread handle (table 0x008dd208, slots 0x008dd228). Returns the handle.
uint8_t* new_thread_record(uint32_t a, uint32_t b) {
    uint32_t* const block = small_heap_alloc(0x0095f488, 0x34);
    if (!block) return nullptr;
    uint8_t* const thread = reinterpret_cast<uint8_t*>(block);
    word_at(thread, 0) = 0;
    word_at(thread, 4) = 0;
    word_at(thread, 8) = a;
    word_at(thread, 0xc) = b;
    word_at(thread, 0x10) = 0;
    word_at(thread, 0x14) = 0;
    word_at(thread, 0x1c) = address_of(thread + 0x20);
    word_at(thread, 0x20) = 0;
    word_at(thread, 0x18) = 0;
    word_at(thread, 0x24) = address_of(thread + 0x1c);
    word_at(thread, 0x2c) = 0;
    word_at(thread, 0x30) = 0;
    word_at(thread, 0x10) |= 1;
    return thread;
}

uint32_t give_thread_handle(uint8_t* thread, uint32_t with_handle) {
    if (static_cast<uint8_t>(with_handle)) {
        uint32_t handle = with_handle;
        thread_handle_alloc_call(&handle);
        if ((handle & 0xff) < 0x100) title_global<uint32_t>(0x008dd228 + (handle & 0xff) * 8) = address_of(thread);
        word_at(thread, 0x14) = handle;
    }
    return word_at(thread, 0x14);
}

}  // namespace

uint32_t XB_CDECL Scr_BeginThread(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t b, uint32_t with_handle) {
    // A thread started by a notify: list 0x008dd1f0 (count 0x008dd1e8), flag 0x800, the current new thread
    // (0x008dda30).
    uint8_t* const thread = new_thread_record(a, b);
    word_at(thread, 0) = 0x008dd1f0;
    uint8_t* const tail = title_global<uint8_t*>(0x008dd1f4);
    word_at(thread, 4) = address_of(tail);
    word_at(tail, 0) = address_of(thread);
    const uint32_t count = title_global<uint32_t>(0x008dd1e8) + 1;
    title_global<uint8_t*>(0x008dd1f4) = thread;
    title_global<uint32_t>(0x008dd1e8) = count;
    title_global<uint8_t*>(0x008dda30) = thread;
    word_at(thread, 0x10) |= 0x800;
    return give_thread_handle(thread, with_handle);
}

uint32_t XB_CDECL Scr_CreateThread(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t b, uint32_t with_handle) {
    // A thread on the run list 0x008dd1e0 (count 0x008dd1d8).
    uint8_t* const thread = new_thread_record(a, b);
    word_at(thread, 0) = 0x008dd1e0;
    uint8_t* const tail = title_global<uint8_t*>(0x008dd1e4);
    word_at(thread, 4) = address_of(tail);
    word_at(tail, 0) = address_of(thread);
    const uint32_t count = title_global<uint32_t>(0x008dd1d8) + 1;
    title_global<uint8_t*>(0x008dd1e4) = thread;
    title_global<uint32_t>(0x008dd1d8) = count;
    return give_thread_handle(thread, with_handle);
}

void XB_STDCALL Scr_MovePendingEntityWaits(uint8_t* scheduler) {
    // Every pending entity wait (list +0x24, sentinel +0x28, count +0x20) moves to its entity's wait list
    // (+0x230, created on first use: Scr_LinkEntityWaitList); waits on gone entities are deleted.
    uint32_t* node = reinterpret_cast<uint32_t*>(uintptr_t{word_at(scheduler, 0x24)});
    uint32_t* next = node ? reinterpret_cast<uint32_t*>(uintptr_t{*node}) : nullptr;
    if (node == reinterpret_cast<uint32_t*>(scheduler + 0x28) || !next) return;
    uint32_t* saved = nullptr;
    bool first = true;
    do {
        if (!first) node = saved;
        first = false;
        if (next) {
            saved = next;
            next = reinterpret_cast<uint32_t*>(uintptr_t{*next});
        }
        uint8_t* const wait = reinterpret_cast<uint8_t*>(node);
        word_at(ptr_at(wait, 0), 4) = word_at(wait, 4);
        word_at(ptr_at(wait, 4), 0) = word_at(wait, 0);
        --word_at(scheduler, 0x20);
        const uint32_t handle = word_at(wait, 0xc);
        const uint32_t index = handle & 0xfff;
        uint8_t* ent = nullptr;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            ent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (ent) {
            if (!word_at(ent, 0x230)) {
                uint32_t* const block = small_heap_alloc(0x0095f570, 0x2c);
                word_at(ent, 0x230) = block ? address_of(Scr_LinkEntityWaitList(block, 0, ent)) : 0;
            }
            uint8_t* const list = ptr_at(ent, 0x230) + 0xc;
            word_at(wait, 0) = address_of(list + 8);
            uint8_t* const tail = ptr_at(list, 0xc);
            word_at(wait, 4) = address_of(tail);
            word_at(tail, 0) = address_of(wait);
            const uint32_t count = word_at(list, 0) + 1;
            word_at(list, 0xc) = address_of(wait);
            word_at(list, 0) = count;
        } else {
            if (uint8_t* const value = ptr_at(wait, 0x10))
                title_function<void XB_THISCALL(uint8_t*, uint32_t)>(word_at(ptr_at(value, 0), 0))(value, 1);
            word_at(wait, 0x10) = 0;
            title_function<void XB_THISCALL(uint32_t, uint8_t*)>(0x00044910)(title_global<uint32_t>(0x0095f56c),
                                                                            wait);  // SmallHeap_Free
        }
    } while (next);
}

void XB_CDECL G_SetupAnimTrees(uint32_t callback, uint32_t arg) {
    // AnimTree_Setup for trees 1..g_numAnimTrees-1, the count kept in 0x0092716c, then the game import's +0x85c
    // and (tail call with these arguments) +0x864.
    int32_t i = 1;
    for (; i < static_cast<int32_t>(title_global<uint32_t>(0x00927098)); ++i)
        AnimTree_Setup(static_cast<uint32_t>(i), arg);  // the entry index in EBX
    title_global<uint32_t>(0x0092716c) = title_global<uint32_t>(0x00927098);
    uint8_t* const import = title_global<uint8_t*>(0x0095f49c);
    TitleRegs regs{address_of(import), static_cast<uint32_t>(i), 0, 0, 0, 0};
    regs.eax = g_ent_call(word_at(import, 0x85c), &regs, 0, nullptr);
    regs.ecx = title_global<uint32_t>(0x0095f49c);
    const uint32_t args[2] = {callback, arg};
    g_ent_call(word_at(reinterpret_cast<uint8_t*>(uintptr_t{regs.ecx}), 0x864), &regs, 2, args);
}

uint8_t* XB_REGS2 XB_STDCALL Scr_InitWaitRecord(uint32_t kind, uint32_t value, uint8_t* record, uint32_t handle) {
    // {vtable 0x00831800, 0, 0, done 0, kind, entity handle, value, death link}; an end-on (kind 3) on the death
    // notify (0x0095fdfc) of a live entity also links a thread reference (heap 0x0095f498, ScrObject_GetThreadHandle)
    // into the entity's wait list (+0x230, created on first use).
    word_at(record, 4) = 0;
    word_at(record, 8) = 0;
    record[0xc] = 0;
    word_at(record, 0x10) = kind;
    word_at(record, 0) = 0x00831800;
    word_at(record, 0x14) = handle;
    word_at(record, 0x18) = value;
    word_at(record, 0x1c) = 0;
    if (kind != 3 || value != title_global<uint32_t>(0x0095fdfc)) return record;
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return record;
    uint8_t* const ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!ent) return record;
    uint8_t* list = ptr_at(ent, 0x230);
    if (!list) {
        uint32_t* const block = small_heap_alloc(0x0095f570, 0x2c);
        list = block ? reinterpret_cast<uint8_t*>(Scr_LinkEntityWaitList(block, 0, ent)) : nullptr;
        word_at(ent, 0x230) = address_of(list);
    }
    uint32_t* const block = small_heap_alloc(0x0095f498, 0xc);
    uint8_t* reference = nullptr;
    if (block) {
        const TitleRegs regs{0, 0, 0, 0, address_of(block), title_global<uint32_t>(0x008dda2c)};
        reference = reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(0x002fdf40, &regs, 0, nullptr)});  // ScrObject_GetThreadHandle
    }
    word_at(record, 0x1c) = address_of(reference);
    word_at(reference, 0) = address_of(list + 0x24);
    uint8_t* const tail = ptr_at(list, 0x28);
    word_at(reference, 4) = address_of(tail);
    word_at(tail, 0) = address_of(reference);
    word_at(list, 0x28) = address_of(reference);
    ++word_at(list, 0x1c);
    return record;
}

namespace {

// Appends a wait record's link (+4) to a thread's wait list (+0x20 sentinel, tail +0x24, count +0x18).
void link_wait(uint8_t* thread, uint8_t* link) {
    word_at(link, 0) = address_of(thread + 0x20);
    uint8_t* const tail = ptr_at(thread, 0x24);
    word_at(link, 4) = address_of(tail);
    word_at(tail, 0) = address_of(link);
    word_at(thread, 0x24) = address_of(link);
    ++word_at(thread, 0x18);
}

}  // namespace

void XB_CDECL Scr_EndOn(uint32_t handle, uint32_t notify) {
    // The current thread (0x008dda2c) ends when the live entity gets the notify (one record per notify).
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    if (!title_global<uint32_t>(0x0097a968 + index * 8)) return;
    const uint32_t main_thread = word_at(title_global<uint8_t*>(0x0095f49c), 0x898);
    uint8_t* const thread = title_global<uint8_t*>(0x008dda2c);
    if (word_at(thread, 0x14) == main_thread) title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    if (Scr_HasEndOnRecord(thread, notify)) return;
    uint32_t* const block = small_heap_alloc(0x0095f494, 0x20);
    uint8_t* const record = block ? Scr_InitWaitRecord(3, notify, reinterpret_cast<uint8_t*>(block), handle) : nullptr;
    link_wait(thread, record + 4);
}

uint32_t XB_CDECL Scr_BeginThreadEndOn(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t handle, uint32_t notify,
                                       uint32_t b) {
    // A new thread waiting (flags 2 | 0x10 | 0x200) on an entity notify (wait record kind 2), on the run list.
    uint8_t* const thread = new_thread_record(a, b);
    uint32_t* const block = small_heap_alloc(0x0095f494, 0x20);
    uint8_t* const record = block ? Scr_InitWaitRecord(2, notify, reinterpret_cast<uint8_t*>(block), handle) : nullptr;
    const uint32_t waiting = word_at(thread, 0x10) | 2;
    word_at(thread, 0x10) = waiting;
    word_at(thread, 0x10) = waiting | 0x10;
    word_at(thread, 0x10) = waiting | 0x10 | 0x200;
    link_wait(thread, record + 4);
    word_at(thread, 0) = 0x008dd1e0;
    uint8_t* const tail = title_global<uint8_t*>(0x008dd1e4);
    word_at(thread, 4) = address_of(tail);
    word_at(tail, 0) = address_of(thread);
    const uint32_t count = title_global<uint32_t>(0x008dd1d8) + 1;
    title_global<uint8_t*>(0x008dd1e4) = thread;
    title_global<uint32_t>(0x008dd1d8) = count;
    return 0;
}

namespace {

uint32_t& sched_word(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }
uint8_t* sched_ptr(uint32_t value) { return reinterpret_cast<uint8_t*>(uintptr_t{value}); }

// Scr_ResumeThread (0x002fe100: thread in EAX, scheduler in ESI) where the original calls it (top = entry - 0x11c):
// it copies the thread's saved stack back to where it was saved (below the scheduler's frame) and runs the thread
// until it waits. The original's return address is written back into the dead stack.
void resume_thread(uint32_t top, uint32_t ret, uint8_t* scheduler, uint8_t* thread) {
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread));
    regs.esi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scheduler));
    regs.edi = regs.eax;
    title_call_at_regs(top, 0x002fe100, &regs, 0, nullptr);
    *reinterpret_cast<uint32_t*>(uintptr_t{top - 4}) = ret;
}

// The title clock (RDTSC at a patched site) through XAPI QueryPerformanceCounter.
void read_clock(uint32_t low, uint32_t high) {
    uint32_t counter[2] = {0, 0};
    title_function<uint32_t XB_STDCALL(uint32_t*)>(0x001ed6ca)(counter);
    title_global<uint32_t>(low) = counter[0];
    title_global<uint32_t>(high) = counter[1];
}

// A thread that ended while resumed (+0x10 bit 3) leaves its list (bit 11: the waiting list, count +0x10; else the
// run list, count +0) and is freed. Returns 1 when it was freed. `ecx` = what the original's unlink leaves in ECX
// (Scr_DestroyThreadContents passes it on): its +0 word (`first_link`) or +4 word.
uint32_t free_if_ended(uint8_t* scheduler, uint8_t* t, bool first_copy) {
    if (!((sched_word(t, 0x10) >> 3) & 1)) return 0;
    if (sched_word(scheduler, 0x854) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t))) sched_word(scheduler, 0x854) = 0;
    uint32_t ecx;
    const bool waiting = (sched_word(t, 0x10) >> 11) & 1;
    if (waiting) {
        sched_word(sched_ptr(sched_word(t, 0)), 4) = sched_word(t, 4);
        sched_word(sched_ptr(sched_word(t, 4)), 0) = sched_word(t, 0);
        --sched_word(scheduler, 0x10);
        ecx = first_copy ? sched_word(t, 0) : sched_word(t, 4);
    } else {
        sched_word(sched_ptr(sched_word(t, 0)), 4) = sched_word(t, 4);
        sched_word(sched_ptr(sched_word(t, 4)), 0) = sched_word(t, 0);
        --sched_word(scheduler, 0);
        ecx = sched_word(t, 4);
    }
    Scr_DestroyThreadContents(t, ecx);
    SmallHeap_Free(title_global<uint8_t*>(0x0095f488), t);
    return 1;
}

}  // namespace

// ESI = scheduler, the frame time on the stack (callee pops). The threads' stacks go back where they were saved, below
// the original's frame: the C frame sits below a gap.
PLATFORM_ASM_ENTRY(Scr_RunThreads_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl 4(%ebp)\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Scr_RunThreads) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

void XB_CDECL Scr_RunThreads(uint32_t entry, uint8_t* scheduler, float frame_time) {
    const uint32_t top = entry - 0x11c;
    // Aborts everything first when the level ends or restarts (0x0095f508 / 0x0095f509).
    if (title_global<uint8_t>(0x0095f508) || title_global<uint8_t>(0x0095f509))
        Scr_AbortAllThreads(reinterpret_cast<uint8_t*>(uintptr_t{0x008dd1d8}));
    if (!(frame_time == title_global<float>(0x0083eafc))) {  // 0: paused
        read_clock(0x00927318, 0x0092731c);
        sched_word(scheduler, 0x858) = 0;
        Scr_MovePendingEntityWaits(scheduler);
        Scr_ProcessAnimNotifies(reinterpret_cast<uint8_t*>(uintptr_t{0x008feba0}));
        // Every thread of the run list (+4, end node +8) is resumed; threads it wakes (+0x858) run at once, the ones
        // still waking others kept on a 64-deep stack and resumed again last-in first.
        uint8_t* node = sched_ptr(sched_word(scheduler, 4));
        uint8_t* next = node ? sched_ptr(sched_word(node, 0)) : nullptr;
        if (node != scheduler + 8 && next) {
            for (;;) {
                uint8_t* const thread = node;
                resume_thread(top, 0x0030ba9e, scheduler, thread);
                if (next) {
                    node = next;
                    next = sched_ptr(sched_word(next, 0));
                }
                if (((sched_word(thread, 0x10) >> 3) & 1)) {
                    Scr_FreeThread(scheduler, 0, thread);
                } else if (sched_word(scheduler, 0x858)) {
                    uint8_t* stack[0x40];
                    stack[0] = thread;
                    int32_t depth = 1;
                    do {
                        while (sched_word(scheduler, 0x858)) {
                            uint8_t* const woken = sched_ptr(sched_word(scheduler, 0x858));
                            sched_word(scheduler, 0x858) = 0;
                            resume_thread(top, 0x0030bb03, scheduler, woken);
                            if (free_if_ended(scheduler, woken, true)) continue;
                            if (sched_word(scheduler, 0x858)) {
                                if (depth < 0x40) stack[depth++] = woken;
                            } else {
                                sched_word(woken, 0x10) |= 0x80;
                            }
                        }
                        const int32_t last = depth - 1;
                        uint8_t* const resumed = stack[last <= 0 ? 0 : last];
                        if (depth) depth = last;
                        resume_thread(top, 0x0030bbaf, scheduler, resumed);
                        if (free_if_ended(scheduler, resumed, false)) {
                        } else if (sched_word(scheduler, 0x858)) {
                            if (depth < 0x40) stack[depth++] = resumed;
                        } else {
                            sched_word(resumed, 0x10) |= 0x80;
                        }
                    } while (depth);
                }
                sched_word(thread, 0x10) |= 0x80;
                if (!next) break;
            }
        }
        read_clock(0x009270c8, 0x009270cc);
        // The waiting list (+0x14, end +0x18) moves back to the run list's tail (+0xc), bit 11 cleared.
        while (sched_word(scheduler, 0x14) != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scheduler + 0x18))) {
            uint8_t* moved = sched_ptr(sched_word(scheduler, 0x1c));
            if (moved == scheduler + 0x14) {
                moved = nullptr;
            } else {
                const uint32_t prev = sched_word(moved, 4);
                sched_word(scheduler, 0x1c) = prev;
                sched_word(sched_ptr(prev), 0) = sched_word(moved, 0);
                --sched_word(scheduler, 0x10);
            }
            sched_word(moved, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(scheduler + 8));
            const uint32_t tail = sched_word(scheduler, 0xc);
            sched_word(moved, 4) = tail;
            sched_word(sched_ptr(tail), 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(moved));
            const uint32_t count = sched_word(scheduler, 0);
            sched_word(scheduler, 0xc) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(moved));
            sched_word(scheduler, 0) = count + 1;
            sched_word(moved, 0x10) &= 0xfffff7ff;
        }
        if (title_global<uint8_t>(0x0095f508) || title_global<uint8_t>(0x0095f509)) {
            title_global<uint8_t>(0x0095f509) = 0;
            const TitleRegs none{};
            if (uint8_t* const import = title_global<uint8_t*>(0x0095f49c))
                title_call_at_regs(entry - 0x114, sched_word(import, 0x88c), &none, 0, nullptr);
            G_FreeAllCodNotes();
            if (title_global<uint8_t>(0x0095f508)) {
                title_global<uint8_t>(0x0095f508) = 0;
                DebugDraw_Flush(1);
                DebugDraw_Shutdown();
                const uint8_t* const running = title_global<uint8_t*>(0x0093fcf0);  // com_sv_running
                title_global<uint32_t>(0x00929c00) = 1;
                if (!*reinterpret_cast<const uint32_t*>(running + 0x20))
                    title_call_at_regs(entry - 0x114, 0x002f3de0, &none, 0, nullptr);  // CheckReloadStatus (stack-sensitive)
            }
        }
    }
}

}  // namespace game
