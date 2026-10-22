#include "g_scr_runtime.h"

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

using NotifyHandler = void XB_STDCALL(uint8_t*, uint32_t, const uint32_t*);

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
    if (*reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(ent, notify, nullptr);
}

// The handler gets the parameter block's address and keeps it in a global (0x008dd1e4) for the
// script thread: the block sits where the original's frame has it (entry ESP - 0x10) and the
// handler runs at the original's ESP. The asm entry keeps the area above the C frame free.
PLATFORM_ASM_ENTRY(G_NotifyWithEntity_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithEntity) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
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
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_NotifyWithOwnHandle) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
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
    if (ent && *reinterpret_cast<const uint32_t*>(ent + 0x234))
        title_function<NotifyHandler>(0x002b6de0)(ent, notify, nullptr);
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

}  // namespace game
