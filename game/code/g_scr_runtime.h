#pragma once

// Game module: small pieces of the compiled-script runtime - helpers the compiled level
// scripts call directly, and the thread / entity handle checks behind the script interface
// (slots at 0x00922140; thread slots 0x008dd228 {thread, generation} x 256, 8-bit index).

#include <cstdint>

#include "compiler.h"

namespace game {

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

}  // namespace game
