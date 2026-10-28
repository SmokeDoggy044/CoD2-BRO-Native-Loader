#pragma once

// XAnim anim trees (0x00249ae0 - 0x002fc670): per-entity trees over the anim data of
// rm_animTrees (0x0095f524), one anim info each (table 0x00965cf0, 0x30 bytes) for the
// node's time, weights and notetrack position; the tree being worked on is 0x00928588.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002aecd0 (index in EAX, anim data in ECX): 1 when node `index` is a leaf or has a sync
// flag (+0x34 & 3).
uint32_t XB_REGS3 XB_CDECL AnimTree_IsLeafOrSynced(uint32_t index, uint32_t unused_edx, const uint8_t* anims);
// 0x002aee60: 1 when node `index` has an anim info with a non-zero weight and is a leaf or
// has such a child below it.
uint8_t XB_CDECL AnimTree_HasActiveLeaf_r(const uint8_t* tree, uint32_t index);
// 0x002aeef0 (index in ECX, tree in ESI): 1 when every parent of node `index` up to the
// root has a non-zero weight.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL AnimTree_ParentsActive(uint32_t index, const uint8_t* tree);
extern "C" void AnimTree_ParentsActive_entry(void);
// 0x002af080 (anim in EAX, one stack word): 1.0 in XMM0, always.
extern "C" PLATFORM_HIDDEN float XB_CDECL AnimTree_CheckLeafNotes();
extern "C" void AnimTree_CheckLeafNotes_entry(void);
// 0x002a8330 (node in EAX, time in XMM2): index of the node's earliest notetrack at or after
// `time` (AX = -1 without notetracks).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL AnimData_FindNotetrack(const uint8_t* node, float time);
extern "C" void AnimData_FindNotetrack_entry(void);
// 0x002aebf0 (index in EDX): the anim info's next notetrack (+2) from its time; -1 at the end.
void XB_REGS2 XB_CDECL AnimTree_UpdateNoteIndex(uint32_t unused_eax, uint32_t index);
// 0x00249ae0 (tree in ECX, 16-bit node index on the stack): 1 when the node has no anim
// info, wrapped around, reached time 1 or passed its frame.
uint32_t XB_REGS3 XB_CDECL AnimTree_HasAnimEnded(uint32_t unused_eax, uint32_t unused_edx, const uint8_t* tree,
                                               uint32_t index);
// 0x002bfaa0 (string in EAX, entity in EDX, &notify in ECX, &plain notify on the stack):
// Scr_NotifyWithString(notify, string), then Scr_Notify(plain).
void XB_REGS3 XB_CDECL AnimTree_NotifyNotetrack(uint32_t string, uint8_t* ent, const uint32_t* notify,
                                               const uint32_t* plain);
// 0x002aefe0: node `index` and everything below it take the time 0x00927f58 / frame 0x0093ef64.
void XB_CDECL AnimTree_SetTime_r(uint32_t index);
// 0x002aedb0: the leaf under node `index` reached through the children with the highest
// goal weight (+0x20); a leaf is its own; 0 when no child has weight.
uint32_t XB_CDECL AnimTree_FindHeaviestLeaf_r(uint32_t index);
// 0x0024b700: 1 when the anim `handle` (entry << 16 | node) has a notetrack named *name.
uint8_t XB_CDECL XAnim_HasNotetrack(uint32_t handle, const uint32_t* name);

// 0x002aed00: sets the sync flag (1, or 2 for flag byte 0) on the children of node `index`
// and below; a node already synced is reported (__break) and the flag goes on its first leaf.
void XB_CDECL AnimTree_SetSyncFlag_r(const uint8_t* anims, uint32_t index, uint32_t flag);
// 0x002b7ad0: the first nodes with a sync flag (+0x34 & 3; 3 is reported) under node `index`
// get flag 4 and their children the sync flag (SetSyncFlag_r).
void XB_CDECL AnimTree_MarkSync_r(const uint8_t* anims, uint32_t index);
// 0x002b7a30: every anim info below node `index` takes the times of `src` (+0x10/+0x14,
// frames +0x18/+0x1a) and its next notetrack again.
void XB_CDECL AnimTree_SyncChildTimes_r(uint32_t index, const uint8_t* src);
// 0x002b7c60 (info, node; XMM0): the least result over the children that have weight and
// goal weight, 1.0 without them (the notetrack checks are compiled out).
extern "C" PLATFORM_HIDDEN void XB_CDECL AnimTree_CheckNotes_r(const uint8_t* info, const uint8_t* node, float* result);
extern "C" void AnimTree_CheckNotes_r_entry(void);
// 0x002af0a0 (XMM0): a leaf's 1 / anim length, else its children's frequency x rate averaged
// by weight. The entry keeps XMM2-7 (AnimTree_AdvanceTime_r_2 keeps XMM3 across it).
extern "C" PLATFORM_HIDDEN void XB_CDECL AnimTree_GetAvgFrequency_r(uint32_t index, float* result);
extern "C" void AnimTree_GetAvgFrequency_r_entry(void);
// 0x002be5a0: restarts node `index` (time and frames 0) unless it is still playing (or
// keep_active is set); under a synced ancestor that one restarts and the nodes in between
// take its time.
void XB_CDECL AnimTree_ResetTime(uint32_t index, uint32_t keep_active);
// 0x002be720 (tree in EAX, index in EBX, blend time on the stack): the node fades out over
// the blend time (+0x1c; the shorter one when already fading), or stops at once when it is
// not playing; anim (+4) and notetrack (+2) cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnimTree_ClearGoalWeight(const uint8_t* tree, uint32_t index, uint32_t blend_time);
extern "C" void AnimTree_ClearGoalWeight_entry(void);

}  // namespace game
