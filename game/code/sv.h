#pragma once

// Server-side engine code (Q3's sv_*.c): game-module glue, entity linking, server statics.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002a8750: __break 1 "Bad game system trap" with the trap number; -1.
int32_t XB_CDECL SV_GameSystemCalls(const int32_t* args);
// 0x002aacb0 (entity in EAX): unlinks the entity from its world sector (+0xdc; chain at +0xe0, sector list
// +0x10), frees sectors left empty (no entities, +0x14 clear, both children the null sector 0x0093568c) onto the
// free list 0x00935688 and recomputes the contents (+8) up the parent chain (+0x18). Returns 0.
int32_t XB_REGS1 XB_CDECL SV_UnlinkEntity(uint8_t* ent);
// 0x002bffb0 (entity in EAX, open in ECX): CM_AdjustAreaPortalState(+0x1fc, +0x200, open) unless +0x200 is -1.
void XB_REGS3 XB_CDECL SV_AdjustAreaPortalState(const uint8_t* ent, uint32_t unused, uint32_t open);
// 0x002c4ed0: the 0xec0-byte server static (0x00927e8c; __break 0 when already up or out of memory), 0x540 at
// 0x00927e90, sv_running 1.
void XB_CDECL SV_AllocServerStatic();
// 0x002c5140 / 0x002c51a0 / 0x002c5200: g_player_maxhealth 500 / 1100 / 2000, g_gameskill 2 / 1 / 0, and
// g_reloading 2 while the 0x0093f800 cvar is set.
void XB_CDECL SV_SetSkillEasy();
void XB_CDECL SV_SetSkillMedium();
void XB_CDECL SV_SetSkillHard();
// 0x002cf830 (entity in ECX): the entity's clip model (+0xdc), else the temporary box model of its bounds
// (+0xe0 mins, +0xf0 maxs, contents +0x134; capsule when +0xd4 has 0x200).
uint8_t* XB_REGS3 XB_CDECL SV_ClipHandleForEntity(uint32_t unused_eax, uint32_t unused_edx, uint8_t* ent);

// 0x002af3d0 (command buffer in EAX: +4 size, +8 base, +0xc text pointers, +0x10 lengths, +0x14 write cursor;
// slot and text on the stack): stores the text in the buffer's zero-filled space - at the cursor (wrapped to the
// base when it would pass the end) when the bytes there are free, else the first run of len + 1 zero bytes that
// starts after a zero (__break 1 "Reliable command buffer overflow" when the scan reaches the end, or for a
// zero-size buffer); '%' becomes '.'.
void XB_REGS1 XB_CDECL SV_AddServerCommand(uint8_t* buffer, int32_t slot, const char* text);

// 0x002bfd10 (client in ESI, null = everyone through the server static when its state is 1; format and
// arguments on the stack): _vsprintf into a 0x800-byte buffer, then SV_AddServerCommand at the client's next
// reliable sequence (+0x18, & 0x3f).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_SendServerCommand(uint8_t* client, const uint32_t* args);
extern "C" void SV_SendServerCommand_entry(void);
// 0x002bffd0 (text in EAX; entity handle on the stack: index & 0xfff, generation >> 12 checked against the
// table at 0x0097a968): "%s" to everyone for handle 0, else to the server static's client when the entity has a
// client (+0x23c).
void XB_REGS1 XB_CDECL SV_GameSendServerCommand(const char* text, uint32_t handle);

// areaParms_t: +0x10 mins, +0x20 maxs (16-byte rows), +0x30 list, +0x34 count, +0x38 max count, +0x3c content
// mask. World sector: +0 axis, +8 contents, +0xc dist, +0x10 entities (chained at +0xe0), +0x1c / +0x20 children.
// 0x002c72e0: Q3's SV_AreaEntities_r - entity numbers (+0x154) of the sector tree's entities whose content
// (+0x64) matches and whose bounds (+0x30 / +0x40) overlap; stops when the list is full.
void XB_CDECL SV_AreaEntities_r(const uint8_t* node, uint8_t* parms);
// 0x002cc400 (maxs in EAX, mins in ECX; list, max count, content mask on the stack): from the root sector
// 0x00935664; returns the count.
int32_t XB_REGS3 XB_CDECL SV_AreaEntities(const float* maxs, uint32_t unused, const float* mins, int32_t* list,
                                         int32_t max_count, uint32_t content_mask);

// 0x002e01a0: Q3's SV_PointContents - the world contents plus every touching entity's (except the pass
// entity), each through its clip model (or the temporary box of its bounds) moved by its origin/angles
// (+0x140 / +0x150); masked with the content mask.
uint32_t XB_CDECL SV_PointContents(const float* point, int32_t pass_entity, uint32_t content_mask);
// 0x002f2e40: bumps the frame counter 0x0093fc28 (skipping 0), runs G_RunFrame with the "in game frame" flag
// 0x0095f510 set. G_RunFrame stores stack addresses in globals: the asm entry moves the C frame below a gap
// and G_RunFrame runs at the original's ESP with the entry's EBX/ESI/EDI.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_RunGameFrame(uint32_t top, int32_t time, uint32_t ebx, uint32_t esi,
                                                        uint32_t edi);
extern "C" void SV_RunGameFrame_entry(void);
// 0x002af5e0: VM_Call(gvm, 1, 0) (GAME_SHUTDOWN), the 0x8c-byte vm record cleared, gvm/currentVM null.
void XB_CDECL SV_ShutdownGameProgs();

// 0x002d7210 / 0x002d7290 (entity in EDI): the entity's DObj (+0x22c) gets a skeleton when it has none
// (Z_MallocDebug of 0x30 + 0x40 per model (+0x2f), DObj_CreateSkeleton), then DObj_UpdateSkeletons; an entity
// controller (+0x34c, table 0x008da8b0) then runs with a 16-byte part mask - all clear (create) / all set
// (ensure). Nothing happens when the skeleton exists.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_DObjCreateSkeleton(uint8_t* ent);
extern "C" void SV_DObjCreateSkeleton_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_DObjEnsureSkeleton(uint8_t* ent);
extern "C" void SV_DObjEnsureSkeleton_entry(void);

// 0x002f4770: the server console commands (once; flag 0x0098f2cc) through Cmd_AddServerCommand.
void XB_CDECL SV_AddOperatorCommands();
// 0x002f48e0 (unhookable: CALL first, not in kFunctions): SV_AddOperatorCommands, then the server cvars (some
// kept in globals).
void XB_CDECL SV_Init();

// 0x00299f00 (maxs in EAX, mins in ECX; unnamed in Ghidra): Q3's RadiusFromBounds - per axis the larger
// magnitude (x87 compare), sqrt((z^2 + y^2) + x^2) through the title's __CIsqrt, returned on the x87 as a float.
long double XB_REGS3 XB_CDECL RadiusFromBounds(const float* maxs, uint32_t unused, const float* mins);

// 0x002ca1a0 (point, entity handle on the stack; unnamed in Ghidra): Q3's per-entity PVS test of
// SV_AddEntitiesVisibleFromPoint - not linked (+0xd0) or flag 1 (+0xd4) -> 0; flag 0x20, +0x1a8 or no clusters
// (+0x1b4) -> 1; else the point's leaf area connected to the entity's areas (+0x1fc / +0x200), then its clusters
// (+0x1b8) in the point cluster's PVS, then the range up to lastCluster (+0x1f8) with Q3's off-by-one.
int32_t XB_CDECL SV_EntityVisibleFromPoint(const float* point, uint32_t handle);

// Sight clips against entities (G_SightTrace): the clip block +0 start, +0x10 end, +0x20/+0x24 pass entities,
// +0x28/+0x2c their owners, +0x30 content mask.
// 0x002e7770 (sector link = entity + 0xd0 in EAX, clip in EDI): content mask against +0x64, pass entity / owner
// (+0x224, +0x1a0) skips, then CM_TransformedSightTrace with the entity's clip handle, origin (+0x140), point-sized
// {0, 0, 0, 1} bounds and angles (+0x150 for brush models, else {0, 0, 0, 1}). Returns -1 when blocked, else 0.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL SV_SightClipToEntity(uint8_t* link, uint8_t* clip);
extern "C" void SV_SightClipToEntity_entry(void);
// 0x002e8350 (clip, sector node, start/end fractions, start/end points): walks the sector tree along the segment
// (both ends on one side -> that child; else split at the plane, near side first), then the node's entities;
// returns the first nonzero SV_SightClipToEntity.
// On record as mismatched (2026-10-23): only dead-stack words below the entry differ (likely a saved frame value 0xb0 apart,
// also with the children and entity clips at their title addresses) - the C frame layout, not the walk.
int32_t XB_CDECL SV_SectorSightTrace_r(uint8_t* clip, uint8_t* node, float f1, float f2, const float* p1,
                                       const float* p2);

// The box version (G_SightTrace with bounds): clip +0 mins, +0x10 maxs, +0x20 per-axis extents, +0x30 start, +0x40
// end, +0x50/+0x54 pass entities, +0x58/+0x5c owners, +0x60 content mask, +0x64 capsule flag.
// 0x002e78b0 (clip in EAX, sector link in ECX): SV_SightClipToEntity with the clip's bounds and capsule flag.
int32_t XB_REGS3 XB_CDECL SV_BoxSightClipToEntity(uint8_t* clip, uint32_t unused, uint8_t* link);
// 0x002e8650 (clip, node, fractions, points): Q3's SV_ClipMoveToEntities-style sector walk with the extents as the
// plane offset (split fractions (t1 -+ offset) / (t1 - t2), clamped), then the node's entities.
int32_t XB_CDECL SV_SectorBoxSightTrace_r(uint8_t* clip, uint8_t* node, float f1, float f2, const float* p1,
                                          const float* p2);

// 0x002e80b0 (clip in EAX, sector link in ECX): Q3's per-entity part of SV_ClipMoveToEntities - clip +0 mins,
// +0x10 maxs, +0x30 start, +0x40 end, +0x50 the 0x40-byte trace (fraction +0x70, startsolid/allsolid +0x8c/+0x8d),
// +0x90 pass entity, +0x94 its owner, +0x98 content mask, +0x9c capsule flag. CM_TransformedBoxTrace with the
// entity's clip handle and angles (+0x150; tiny pitch/roll zeroed for flag 0x800000; {0, 0, 0, 1} without a brush
// model); a smaller fraction replaces the trace (entity number +0x224), else the solid flags are merged.
// Registered without a result (the original leaves a byte of the flags in AL).
int32_t XB_REGS3 XB_CDECL SV_ClipTraceToEntity(uint8_t* clip, uint32_t unused, uint8_t* link);

// 0x002e88d0 (clip, node, fractions, points): Q3's SV_ClipMoveToEntities walk over the sector tree - stops when the
// start fraction is not below the trace's (+0x70); same-side children or both split halves (as
// SV_SectorBoxSightTrace_r, no early out), then SV_ClipTraceToEntity for each of the node's entities.
// On record as mismatched (2026-10-23): likely dead-stack words only (the original's locals where the C frames left zeros).
void XB_CDECL SV_SectorClipTrace_r(uint8_t* clip, uint8_t* node, float f1, float f2, const float* p1,
                                   const float* p2);

}  // namespace game
