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

}  // namespace game
