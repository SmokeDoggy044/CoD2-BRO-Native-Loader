// The effect command queue (0x002a5a40-0x002a6d30 and its helpers): game and cgame code
// asks for sounds and effects by filling a 0x210-byte spawn command in the effect list's
// queue (list 0x0095f5b4: queued count +0x910, commands from +0x920, the command being
// built +0x8d20) and committing it, which allocates the effect instance (+0x4c = its id).
// Names are descriptive.
#pragma once

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00024520 (was FUN_00024520; destination in EBX, source on the stack, stdcall): copies
// a spawn command (all but the word at +0x1bc; the script string at +0x1fc by
// ScriptString_Assign). Returns the destination.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FxSpawnCmd_Copy(uint8_t* dst, const uint8_t* src);
extern "C" void FxSpawnCmd_Copy_entry(void);

// 0x00296d80 (was FUN_00296d80; instance in EAX, owner handle in ECX): a new effect
// instance: +0x44 = handle, +0x4c = the next id (0x0095f5b8; never 0), +0x50 = 2, +0/+0x48
// = 0. Returns the instance.
uint8_t* XB_REGS3 XB_CDECL FxInstance_Init(uint8_t* effect, uint32_t unused_edx, uint32_t handle);

// 0x002a5a40 (list in ESI; owner entity and handle on the stack, stdcall): starts a command
// in the list's queue. A full queue (64) first drops its first kind-0 command (moving the last
// one there), then is flushed (FX_FlushSpawnQueue). Handle -1 = the entity's +0x220, or the
// default's (0x0095f638)->+0x34. Returns the command (also in +0x8d20).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FXCmd_Alloc_body(uint32_t entry, uint8_t* list, const uint8_t* entity,
                                                             int32_t handle);
extern "C" void FXCmd_Alloc_entry(void);

// 0x00298e90 (list in ESI): when the command's entity handle (+0x1f0) is live (table
// 0x0097a968), allocates the effect instance (0x54 bytes from the small heap 0x0095f5bc) and
// appends it to the list (+0, at most 0x200); else drops the command. Returns the new id or 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FXCmd_Commit_body(uint8_t* list);
extern "C" void FXCmd_Commit_entry(void);

// 0x00296b80: for every instance of the list with id `id`, slot 2 (thiscall, a, b) of each of
// its components.
void XB_STDCALL FXParam_Broadcast(const uint8_t* list, uint32_t id, uint32_t a, uint32_t b);

// The command builders (cdecl; owner entity in EAX unless noted, EAX/ECX handed to the C
// body). `entry` = the entry ESP (the stack arguments start at entry + 4). Each returns
// FXCmd_Commit's id (0 when skipped).
#define FXCMD_BUILDER(name)                                                                       \
    extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL name##_body(uint32_t entry, uint8_t* eax, uint32_t ecx); \
    extern "C" void name##_entry(void);
// 0x002a5bb0 (value): kind 9, value at +0x128.
FXCMD_BUILDER(FXCmd_Type9)
// 0x002a5c40 (EAX entity, ECX tag name; name, value, transform): kind 8 at the entity
// model's tag (+0x204 = the tag index); skipped without a model or tag.
FXCMD_BUILDER(FXCmd_Type8AtTag)
// 0x002a5ee0 (name, value): kind 8, name at +0xa8.
FXCMD_BUILDER(FXCmd_Type8)
// 0x002a5fd0 / 0x002a60e0 (name, transform): kinds 7 / 6, name at +0x18.
FXCMD_BUILDER(FXCmd_Type7AtTransform)
FXCMD_BUILDER(FXCmd_Type6AtTransform)
// 0x002a61f0 (value, transform): kind 5, value at +0x12c.
FXCMD_BUILDER(FXCmd_Type5AtTransform)
// 0x002a62d0 (name, value) / 0x002a63d0 (name, value, looped): kind 4 weapon sounds.
FXCMD_BUILDER(FXCmd_PlayWeaponSound)
FXCMD_BUILDER(FXCmd_PlayWeaponSoundLoop)
// 0x002a64e0 (entity, name, value, bone) / 0x002a6600 (entity, name, value, bone, extra):
// kind 4 at a bone (+0x20e; none when the entity's +0x248 is set).
FXCMD_BUILDER(FXCmd_PlayWeaponSoundAtBone)
FXCMD_BUILDER(FXCmd_PlayWeaponSoundAtBoneEx)
// 0x002a6740 (entity, alias, point, looped) / 0x002a6880 (entity, alias, value): kind 3 sound
// aliases (name at +0x134 and as the script string); skipped without a name.
FXCMD_BUILDER(FXCmd_PlaySoundAliasAtPoint)
FXCMD_BUILDER(FXCmd_PlaySoundAliasMode7)
// 0x002a6990 (ECX entity; alias, looped, handle, flag 8).
FXCMD_BUILDER(FXCmd_PlaySoundAlias)
// 0x002a6ab0 (transform) / 0x002a6b70 (value, transform) / 0x002a6c40 (value, transform):
// kinds 2 / 1 / 0; the last two are skipped when the entity's +0x23c has +0x58c set.
FXCMD_BUILDER(FXCmd_Type2AtTransform)
FXCMD_BUILDER(FXCmd_Type1AtTransform)
FXCMD_BUILDER(FXCmd_Type0AtTransform)
#undef FXCMD_BUILDER

// 0x00023a70 (EAX): a placement's two rotations = identity, its bone (+0x20) -1; returns it.
uint8_t* XB_REGS1 XB_CDECL FXCmd_InitPlacement(uint8_t* placement);
// 0x00023cf0: a queued command record (0x210 bytes): cleared head, placement at +0x1c0,
// empty script string at +0x1fc, ids +0x204/+0x208/+0x20e -1.
uint8_t* XB_THISCALL FXCmd_RecordCtor(uint8_t* cmd);

}  // namespace game
