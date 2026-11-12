#pragma once

// Game module: small pieces of the compiled-script runtime - helpers the compiled level
// scripts call directly, and the thread / entity handle checks behind the script interface
// (slots at 0x00922140; thread slots 0x008dd228 {thread, generation} x 256, 8-bit index).

#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x0011d890: *out = the name's key hash (interface HashKeyNoCase, 0x009229e4); returns out.
uint32_t* XB_CDECL Scr_HashName(uint32_t* out, const char* name);
// 0x0011d930: Scr_EndOn (interface 0x00922188) with the name's hash in place of the name (the
// original jumps there with the argument slot replaced).
void XB_CDECL Scr_EndOnString(uint32_t thread, const char* name);
// 0x002f62b0: a script thread is running (0x008dda2c set); AL only.
uint32_t XB_CDECL Scr_InThread();
// 0x002f6270: the handle names a live thread; AL only.
uint32_t XB_CDECL Scr_ThreadHandleIsValid(uint32_t handle);
// 0x002fae90: the handle names a live entity.
uint32_t XB_CDECL Scr_EntityHandleIsValid(uint32_t handle);
// 0x002f6220: flags a live thread (+0x10) to end (8 | 0x40 | 4).
void XB_CDECL Scr_TerminateThread(uint32_t handle);
// 0x002b75c0 (entity in ESI, callee pops 4): queues the notify {next, prev, notify, entity
// number, argument 0} (small heap 0x0095f56c) on the pending list (0x008dd200, tail
// 0x008dd204, count 0x008dd1f8) and, when the entity has a script object (+0x234), runs its
// notify handler (0x002b6de0) at once.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_Notify(uint8_t* ent, uint32_t notify);
extern "C" void Scr_Notify_entry(void);
// 0x000265a0 (entity in EDI, callee pops 8): the same with an entity argument (a 0x10-byte
// value {0x007f964c, 0, 0, *other} from heap 0x0095f574); the handler gets {*other, 0, f, f}
// (f = 0x00922eb8).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_NotifyWithEntity(uint32_t entry, uint8_t* ent, uint32_t notify,
                                                             const uint32_t* other, uint32_t ebx, uint32_t esi);
extern "C" void G_NotifyWithEntity_entry(void);
// 0x0024f570 (entity in EAX, notified entity in EDI): G_NotifyWithEntity on EDI's entity with
// EAX's entity number as the argument.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_NotifyWithOwnHandle(uint32_t entry, const uint8_t* ent, uint8_t* notified,
                                                                uint32_t notify, uint32_t ebx, uint32_t esi);
extern "C" void G_NotifyWithOwnHandle_entry(void);
// 0x002be3e0 (entity in EAX, new notify in EBX): sends the pending sound-done notify (+0x3c4)
// and sets the next one (+0x3c8 cleared).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SetSoundDoneNotify(uint8_t* ent, uint32_t notify);
extern "C" void G_SetSoundDoneNotify_entry(void);
// 0x0011d8b0: *out = the key hash of a script string's text ({rep*}, text at rep + 6; a null
// rep hashes a null pointer); returns out.
// 0x002fb3d0: script memory - the small-block heap (0x0095f4a4) for sizes up to its largest
// class, else the general heap (0x0095f4a0).
uint32_t* XB_CDECL Scr_Alloc(uint32_t size, uint32_t flags);
// 0x002fbe00: Scr_Notify by entity handle (the record keeps the handle); the handler runs when
// the handle is live and the entity has a script object.
void XB_CDECL Scr_QueueNotify(uint32_t handle, uint32_t notify);
// 0x002fbd70 / 0x002fbc40: queue a notify for a handle with one value (kinds 0x008317d8 /
// 0x008317ec); no handler run.
void XB_CDECL Scr_QueueNotifyCallback26(uint32_t handle, uint32_t notify, uint32_t value);
void XB_CDECL Scr_QueueNotifyCallback29(uint32_t handle, uint32_t notify, uint32_t value);
// 0x002fdbf0: the running thread's handle (+0x14), given one from the pool on first use.
uint32_t XB_CDECL Scr_GetCurrentThreadHandle();
// 0x002f5250 (script string in ESI): the team mask of a team name (script strings 0x00984cd0,
// 0x00984cd4, 0x00984cd8 -> 2, 4, 8); anything else breaks into the debugger and gives 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_TeamFromString(const void* name);
extern "C" void Scr_TeamFromString_entry(void);
// 0x0011f660 (callee pops 8): count zeroed pointers from Scr_Alloc.
uint32_t* XB_STDCALL Scr_AllocPtrArray(uint32_t count, uint32_t unused);
// 0x002f5e10: frees script memory to the heap that owns it.
void XB_CDECL Scr_Free(void* block);
uint32_t* XB_CDECL Scr_HashScriptString(uint32_t* out, const uint8_t* const* string);

// 0x0001b5c0: frees the queued notify records of a singly linked range (iterators {node, next};
// first, next, end, end's next, allocator tag): each record's value (+0x10) through its deleting
// destructor, the record back to heap 0x0095f56c.
void XB_CDECL NotifyList_FreeRange(uint32_t* first, uint32_t* next, uint32_t* end, uint32_t* end_next,
                                   uint32_t tag);
// 0x0001ec10: deleting destructor of a chained entity notify block (next block +0x38, freed
// first; heap 0x0095f568).
uint8_t* XB_THISCALL EntNotifyBlock_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x00022df0 (entity in EBX, callee pops 8): Scr_Notify carrying a script string (value kind
// 0x007f963c).
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_NotifyWithString(uint8_t* ent, uint32_t notify,
                                                              const ScriptString* text);
extern "C" void Scr_NotifyWithString_entry(void);
// 0x000264d0 (entity in EDI, callee pops 8): G_NotifyWithEntity with value kind 0x008317d8.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_NotifyWithEntity_2(uint32_t entry, uint8_t* ent, uint32_t notify,
                                                              const uint32_t* other, uint32_t ebx, uint32_t esi);
extern "C" void G_NotifyWithEntity_2_entry(void);
// 0x00026670 (stdcall): a notify carrying an int and an entity handle (value kind 0x00839c20); the
// handler gets {other, 0, (float)value, undefined}.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_NotifyWithIntEntity(uint32_t entry, uint8_t* ent, uint32_t notify,
                                                               const int32_t* value, const uint32_t* other);
extern "C" void G_NotifyWithIntEntity_entry(void);

// 0x00029000 (entity in EBX, callee pops 12): a notify carrying two script strings (value kind
// 0x00839c2c, 0x14 bytes); the handler gets no parameter block.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_NotifyWithTwoStrings(uint8_t* ent, uint32_t notify,
                                                                const ScriptString* first,
                                                                const ScriptString* second);
extern "C" void G_NotifyWithTwoStrings_entry(void);
// The script wait family: not in kFunctions (they suspend the script thread; no test process can
// run them). Scr_SuspendWaitTime/Frames/Till (0x0011d950/0x0011d990/0x0011d9d0) have no C: they
// push all registers, save the thread's stack (Scr_SaveThreadLocals) and switch away; the
// scheduler resumes them with POPA/POPF.
// 0x0011db10 (3 bytes, too short to hook): returns false (AL).
uint8_t XB_CDECL Scr_ReturnFalse();
// 0x0011e4b0: waits exactly `seconds` (g_scrWaitTime 0x00922124, Scr_SuspendWaitTime).
void XB_CDECL Scr_WaitExact(float seconds);
// 0x0011e4f0: waits a random time from seconds to 1.05 x seconds (Scr_RandomFloatRange).
void XB_CDECL Scr_Wait(float seconds);
// 0x0011e540: waits `frames` frames (0x00922128, Scr_SuspendWaitFrames).
void XB_CDECL Scr_WaitFrames(uint32_t frames);
// 0x0011e570 / 0x0011e5e0: waits for `notify` on object (nothing for a null object); the second
// with a timeout (0x00922138).
void XB_CDECL Scr_WaitTill(uint32_t object, uint32_t notify);
void XB_CDECL Scr_WaitTillTimeout(uint32_t object, uint32_t notify, float timeout);
// 0x0011e650 / 0x0011e6d0: waits for `notify` with three argument words (0x00922120, 0x0092212c,
// 0x00922e60); flag 0x00922e5c 1 / 0.
void XB_CDECL Scr_WaitTillArgs(uint32_t object, uint32_t notify, uint32_t a, uint32_t b, uint32_t c);
void XB_CDECL Scr_WaitTillArgsNoFlag(uint32_t object, uint32_t notify, uint32_t a, uint32_t b, uint32_t c);
// 0x0011e750: Scr_WaitTill(object, HashKeyNoCase(name)).
void XB_CDECL Scr_WaitTillString(uint32_t object, const char* name);
// 0x0011e780 / 0x0011e7c0: waits on an object (0x00922e6c), notify word 0x00922130 = 1 / 0.
void XB_CDECL Scr_WaitOnObject(uint32_t object);
void XB_CDECL Scr_WaitOnObjectNoFlag(uint32_t object);
// 0x0011b830: Scr_WaitTillString(object, the string's text or null).
void XB_CDECL Scr_WaitTillScriptString(uint32_t object, const ScriptString* name);

// 0x0011f1b0: appends the path nodes matching (key, value) (GScr_GetNode, at most 0x400; -1 handles
// as 0) to a script pointer array after DynArray_ReserveInt0 (0x0045d480) and PtrArray_Reserve.
void XB_CDECL Scr_GetNodeArray(uint32_t key, uint32_t value, uint32_t* array);
// 0x0011f240: the same with an empty script string as key and value (all nodes); reserves only
// when there are any.
void XB_CDECL Scr_GetNodeArrayNoKey(uint32_t* array);
// 0x0011f320: vehicle nodes (GScr_GetVehicleNode) into a vehicle node array (0x0045c4a0 reset,
// PtrArray_Reserve2, append 0x0045c550).
void XB_CDECL Scr_GetVehicleNodeArray(uint32_t key, uint32_t value, uint32_t* array);

// 0x002980f0 (list in EAX, entity in ECX): a new entity wait list for the entity, appended to
// the global list; returns it.
uint32_t* XB_REGS3 XB_CDECL Scr_LinkEntityWaitList(uint32_t* list, uint32_t unused_edx, const uint8_t* ent);
// 0x0029ac20 (list in EBX): frees the list's waits; 1 when it is empty.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_EntityWaitList_FreeWaits(uint32_t* list);
extern "C" void Scr_EntityWaitList_FreeWaits_entry(void);
// 0x0029d500 (list in EAX): unlinks and empties an entity wait list.
void XB_REGS1 XB_CDECL Scr_UnlinkEntityWaitList(uint32_t* list);
// 0x0029ed00: frees the waits of every entity wait list, the empty lists too.
void XB_CDECL Scr_PruneEntityWaitLists();

// 0x002f4d40 / 0x002f4e50: empty setters (bare ret) of the script field tables 0x008d2144 / 0x008d211c.
void XB_CDECL Scr_NopRefSetter();
void XB_CDECL Scr_NopFieldSetter();
// 0x002f6350 (was FUN_002f6350; record in EAX, thread in ECX, callee pops 0x18): a wait record (vtable 0x008317f8)
// {owner, four values, flag byte +0x2c}; +0x28 bit n for each value that is 0. Returns the record.
uint8_t* XB_REGS3 XB_STDCALL Scr_InitWaitRecord4(uint8_t* record, uint32_t unused_edx, uint32_t thread, uint32_t owner,
                                                 uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t flag);
// 0x002f6600 (saved {chunk, size} in EAX, destination in ECX): copies a saved thread stack back from its chain of
// 0xfc-byte chunks; counts restores in 0x0098ad18.
void XB_REGS3 XB_CDECL Scr_RestoreThreadStack(const uint32_t* saved, uint32_t unused_edx, uint8_t* dst);
// 0x002fb420 / 0x002fb480: Scr_Notify on the level entity with "unloaded_<name>" / "loaded_<name>".
void XB_CDECL Scr_NotifyUnloaded(const char* name);
void XB_CDECL Scr_NotifyLoaded(const char* name);
// 0x002fbba0: queues a notify by handle with a two-value record (kind 0x00831810: a float and a word).
void XB_CDECL Scr_QueueNotifyWith2Args(uint32_t handle, uint32_t notify, uint32_t value, uint32_t value2);
// 0x002fbcd0: queues a notify by handle with a script string (kind 0x007f963c).
void XB_CDECL Scr_QueueNotifyWithString(uint32_t handle, uint32_t notify, const ScriptString* text);
// 0x002fbea0 (no callers): ScrThread_AppendData into the current thread (0x008dda2c).
uint32_t XB_CDECL Scr_SaveThreadLocals(uint32_t data);
// 0x002fc2a0 (was FUN_002fc2a0): marks every thread record (list +4) ended (flags 0x48) and frees the pending
// notify list (+0x24).
void XB_STDCALL Scr_AbortAllThreads(uint8_t* scheduler);
// 0x002fc3c0 (was FUN_002fc3c0; thread in EDX, value in ESI; Scr_EndOn): whether the thread's record list (+0x1c)
// has a kind-3 record on the value.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_HasEndOnRecord(uint8_t* thread, uint32_t value);
extern "C" void Scr_HasEndOnRecord_entry(void);

// 0x002fc440 (thread in ESI; Scr_ResumeThread): a thread whose owner entity (+8) is gone or that has ended (bit 0x40)
// is marked ended (0x48) and runnable (4) unless bit 0; else each wait record is polled (vtable +4: 2 done, 3 ended)
// and the finished ones (+0xc) unlinked and deleted.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_UpdateThreadWaits(uint8_t* thread);
extern "C" void Scr_UpdateThreadWaits_entry(void);
// 0x002fc540 (thread in EDI): deletes its wait records (ScrWaitList_DeleteRange; the tag word from the entry's ECX
// slot), its context object (+0xc), its handle (+0x14) and its saved-stack chunks (+0x2c).
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_DestroyThreadContents(uint8_t* thread, uint32_t ecx);
extern "C" void Scr_DestroyThreadContents_entry(void);
// 0x002fdc40 / 0x002fdd00 (no callers found): a new thread record {a, b} (heap 0x0095f488) on the notify list
// (0x008dd1f0, flag 0x800, current new thread 0x008dda30) / run list (0x008dd1e0); a script handle when the flag
// byte is set. Returns the handle (+0x14). The first three arguments are not read.
uint32_t XB_CDECL Scr_BeginThread(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t b, uint32_t with_handle);
uint32_t XB_CDECL Scr_CreateThread(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t b, uint32_t with_handle);
// 0x002fdf90 (scheduler in EAX, thread in ECX): unlinks the thread (counted at scheduler +0x10 for notify threads,
// +0 else; +0x854 cleared when it is the thread), Scr_DestroyThreadContents, the record back to heap 0x0095f488.
void XB_REGS3 XB_CDECL Scr_FreeThread(uint8_t* scheduler, uint32_t unused_edx, uint8_t* thread);
// 0x002fdff0: moves the scheduler's pending entity waits (+0x24) to their entities' wait lists (+0x230); waits on
// gone entities are deleted.
void XB_STDCALL Scr_MovePendingEntityWaits(uint8_t* scheduler);
// 0x002fe200 (G_InitAnimTrees): AnimTree_Setup (entry index, arg) per anim tree after the first, the count to 0x0092716c, then the
// game import's +0x85c and +0x864 (tail call with the same arguments).
void XB_CDECL G_SetupAnimTrees(uint32_t callback, uint32_t arg);
// 0x002ffa60 (kind in EAX, value in EDX; record, entity handle on the stack, callee pops): a wait record (vtable
// 0x00831800); an end-on on a live entity's death notify (0x0095fdfc) also links a thread reference into the
// entity's wait list. Returns the record.
uint8_t* XB_REGS2 XB_STDCALL Scr_InitWaitRecord(uint32_t kind, uint32_t value, uint8_t* record, uint32_t handle);
// 0x003007b0 (no callers found): the current thread ends on the live entity's notify (Scr_HasEndOnRecord first;
// Sys_DebugBreak for the main thread).
void XB_CDECL Scr_EndOn(uint32_t handle, uint32_t notify);
// 0x00300a40 (no callers found): a new thread {a, b} waiting on the entity's notify (kind 2), on the run list.
// Returns 0.
uint32_t XB_CDECL Scr_BeginThreadEndOn(uint32_t, uint32_t, uint32_t, uint32_t a, uint32_t handle, uint32_t notify,
                                       uint32_t b);

// 0x0030b9e0 (scheduler in ESI, frame time on the stack, ret 4): the script scheduler's frame - aborts on level
// end/restart, then (frame time not 0) pending entity waits, anim notifies, every run-list thread resumed (woken
// threads at once, a 64-deep stack of wakers), the waiting list moved back, level-end cleanup (cod notes, debug draw,
// CheckReloadStatus). `entry` = the original's ESP at its entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_RunThreads(uint32_t entry, uint8_t* scheduler, float frame_time);
extern "C" void Scr_RunThreads_entry(void);

// 0x000142c0: ScrWaitRecord_Dtor (ESI = self), then SmallHeap_Free on 0x0095f494 when flags & 1.
uint8_t* XB_THISCALL ScrWaitRecord_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x00013d60 (slot 0 of the wait records' base vtable 0x008317e4 and of the frame / time records): the base vtable,
// then SmallHeap_Free on 0x0095f494 when flags & 1. Returns self.
uint8_t* XB_THISCALL ScrWaitBase_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x002f5620 (vtable 0x00831808 slot 1, one unread word): the frame count +0x14 decremented; at 0 or below the done
// byte +0xc set and 2 returned, else 0.
uint32_t XB_THISCALL ScrWaitFramesRecord_Check(uint8_t* self, uint32_t unused);
// 0x002f6400 (vtable 0x00831824 slot 1, one unread word): the time +0x14 less the frame time 0x008ddaa0; at 0 or below
// the done byte +0xc set and 2 returned, else 0 (also for NaN).
uint32_t XB_THISCALL ScrWaitTimeRecord_Check(uint8_t* self, uint32_t unused);
// 0x000147e0 (EAX = handle table, EBX = out): takes the first free bit of the 256-bit set at
// the table's start and writes the handle (generation at +0x24 + index * 8, << 8 | index).
// None free: ScrThread_HandlesExhausted, index -1.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL ScrThread_AllocHandle(uint32_t* table, uint32_t* out);
extern "C" void ScrThread_AllocHandle_entry(void);
// 0x000148a0 (vtable slot 1, entity output): copies the value (+0xc) into `other`; returns it.
uint32_t XB_THISCALL WaitTilOutputInst1_ent_VFunc1(const uint8_t* self, uint8_t* other);
// 0x00014b70 (vtable slot 1, string output): assigns the string (+0xc) to `other`'s.
uint32_t XB_THISCALL WaitTilOutputInst1_str_VFunc1(uint8_t* self, uint8_t* other);
// 0x00014b90 (two-value output): copies +0xc (and +0x10 when `other` takes two values,
// its vtable slot 2) into `other`.
uint32_t XB_THISCALL WaitTilOutputInst2_CopyArgs(const uint8_t* self, uint8_t* other);
// 0x00014c50: releases the string (+0xc), base vtable, SmallHeap_Free on 0x0095f574 when flags & 1.
uint8_t* XB_THISCALL WaitTilOutputInst1_str_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x00014dd0 (ESI = handle table): out of thread handles - reports each one in use to the
// callback at +0x820 (if set), then Sys_DebugBreak.
extern "C" PLATFORM_HIDDEN void XB_CDECL ScrThread_HandlesExhausted(uint32_t* table);
extern "C" void ScrThread_HandlesExhausted_entry(void);

// 0x0001b580: deletes the wait records of a list range (vtable slot 0, flag 1): `node` first,
// then each link from `first` up to (not including) `last`, one step behind the walk.
void XB_CDECL ScrWaitList_DeleteRange(uint8_t* node, uint32_t* first, uint32_t unused, uint32_t* last);
// 0x00028440: base wait-output destructor (vtable 0x007f48c4), SmallHeap_Free on 0x0095f574
// when flags & 1.
uint8_t* XB_THISCALL WaitTilOutputInst1_ent_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x000290d0 (two-string output): assigns +0xc (and +0x10 when `other` takes two values).
uint32_t XB_THISCALL WaitTilOutputInst2_str_str_CopyArgs(uint8_t* self, uint8_t* other);
// 0x00029130: releases both strings (+0x10, +0xc), base vtable, SmallHeap_Free when flags & 1.
uint8_t* XB_THISCALL WaitTilOutputInst2_str_str_DeletingDtor(uint8_t* self, uint32_t flags);

// 0x0024b860 (EAX = object): the rm_animTrees handle (index << 16) of the tree at object+8.
uint32_t XB_REGS1 XB_CDECL AnimTree_RootHandle(uint8_t* obj);
// 0x0029acb0 (EDX = callback list, pops 8): calls the object of the node with *key (slot 1, argument); AL = found.
uint32_t XB_REGS2 XB_STDCALL EntNotifyCallbacks_CallByKey(uint32_t, uint8_t* list, const uint32_t* key,
                                                         uint32_t argument);
// 0x002adfe0 (EAX = table): zeroes the 0x3c-byte entity notify-callback table; returns it.
uint8_t* XB_REGS1 XB_CDECL EntNotifyCallbacks_Init(uint8_t* self);
// 0x002a84e0 (EDX = vm; asm entry, the entry point at the original's ESP).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL VM_Call_c(uint32_t entry_esp, uint8_t* vm, uint32_t ebx);
extern "C" void VM_Call_entry(void);
// 0x002a8580 / 0x002af280 / 0x002f2b80.
int32_t XB_CDECL VM_DllSyscall(uint32_t arg);
void XB_CDECL VM_Clear();
uint8_t* XB_CDECL VM_Create(const char* module, uint32_t system_calls);
// 0x002f64f0 (EAX = record, XMM0 = timeout, pops 12; asm entry).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ScrWaitTimeoutRecord_Init(uint8_t* self, uint32_t a, uint32_t b,
                                                                       uint32_t c, uint32_t timeout_bits);
extern "C" void ScrWaitTimeoutRecord_Init_entry(void);
// 0x002f6670 (EAX = data, ECX = end, pops 4) / 0x002f67a0.
uint32_t XB_REGS3 XB_STDCALL ScrThread_AppendData(const uint8_t* data, uint32_t, const uint8_t* end,
                                                  uint32_t* holder);
void XB_CDECL ScrThread_InitDataHeap();

// 0x002fc110 / 0x002fc1b0 (thiscall, pops 4): wait-record checks against the running thread.
uint32_t XB_THISCALL ScrWaitTimeoutRecord_Check(uint8_t* self, uint32_t thread);
uint32_t XB_THISCALL ScrWaitRecord_Check(uint8_t* self, uint32_t thread);
// 0x002fc230 (ESI = record; asm entry).
extern "C" PLATFORM_HIDDEN void XB_CDECL ScrWaitRecord_Dtor(uint8_t* self);
extern "C" void ScrWaitRecord_Dtor_entry(void);
// 0x002fdf40 (ESI = out, EDI = object; asm entry).
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL ScrObject_GetThreadHandle(uint32_t* out, uint8_t* object);
extern "C" void ScrObject_GetThreadHandle_entry(void);

// 0x00300530 (was FUN_00300530; builtin slot 0x538; asm entry, AnimNotify_Init at the original's ESP).
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_StartAnimNotify_c(uint32_t entry_esp, uint32_t edi);
extern "C" void Scr_StartAnimNotify_entry(void);
// 0x00373230 / 0x003732b0 (static initializers), 0x003798a0 / 0x00379e50 / 0x00379e70 (atexit).
void XB_CDECL StaticInit_List_95fe00();
void XB_CDECL StaticInit_DownVec4_95fde0();
void XB_CDECL AtExit_FreeList_95fdf0();
void XB_CDECL AtExit_ScrFree_922e90();
void XB_CDECL AtExit_ScriptKeyTable();

// 0x0029aba0: marks every live script thread in an owner's handle list (+0x20) to end; returns the last node.
uint32_t XB_THISCALL ThreadHandleList_EndAll(uint8_t* owner);

// Wait record checks (gap functions, 11-11; thiscall, pops 4). 0x002fc030 (vtable 0x008317f8 +4, the
// Scr_InitWaitRecord4 record): a stale entity ends it (3); else for a thread waiting on keys (flag bit 7) each of the
// four keys found in the entity's key list (+0x230) ends it (+0x2c 0) or is marked (+0x28 bit, key cleared) until
// all four are (+0x2c 1); returns +0x10 when ended, else 0. 0x002f6450 (vtable 0x0083181c +4): the zone state wait.
uint32_t XB_THISCALL ScrWaitRecord4_Check(uint8_t* self, uint32_t thread);
uint32_t XB_THISCALL ScrZoneWaitRecord_Check(uint8_t* self, uint32_t thread);

}  // namespace game
