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

// World sectors (+0 axis, +4 brush contents, +8 entity contents, +0xc dist, +0x10 entities, +0x14 static models
// chained at +0xdc (bounds +0xa0 / +0xac, contents via +0x68 -> +0x40), +0x18 parent / free-list link, +0x1c / +0x20 children above / below dist; null sector 0x0093568c,
// free list 0x00935688, root 0x00935664). The server entity (gentity + 0xd0) keeps +0x64 contents, +0xdc sector,
// +0xe0 next, +0x134 linked contents, +0x138 / +0x140 linked xy bounds.
// 0x002aae40 (maxs in EDX, mins in ESI): takes a sector off the free list, split axis = the longer of x/y, dist
// = its midpoint; null when the list is empty or that side is not over 512 units.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL SV_AllocSector(const float* maxs, const float* mins);
extern "C" void SV_AllocSector_entry(void);
// 0x002b1f70 (sector in EDI; region mins, maxs on the stack): moves the sector's entities and brushes that lie
// wholly on one side of its split into that child, allocating children (SV_AllocSector over the parent region)
// as needed; stops when none is free.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_SplitSector(uint8_t* node, const float* mins, const float* maxs);
extern "C" void SV_SplitSector_entry(void);
// 0x002ba9d0 (server entity in ESI; absmin, absmax on the stack, x/y used): walks down from the root to the
// sector the bounds straddle (or a missing child), ORing the contents into each; keeps the current sector when it
// is the same and the contents are covered, else unlinks and relinks there, then splits that sector.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_LinkToSector(uint8_t* sv, const float* absmin, const float* absmax);
extern "C" void SV_LinkToSector_entry(void);
// 0x002d7fe0 (gentity in EDI): Q3's SV_LinkEntity - packed bbox (+0xac), absmin/absmax (+0x100 / +0x110, from
// RadiusFromBounds when a brush model is rotated) widened by 1, areas (+0x1fc / +0x200) and clusters (+0x1b4 /
// +0x1b8, last cluster +0x1f8) from CM_BoxLeafnums, the portal cell (+0x458, cached leaf +0x130 within the
// radius +0x12c of +0x120), then SV_LinkToSector with the bounds (or the DObj's bounds for +0xd4 & 0x18).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_LinkEntity(uint8_t* ent);
extern "C" void SV_LinkEntity_entry(void);
// 0x002e8270 (gentity in ESI; mins, maxs, capsule flag on the stack): CM_TransformedBoxTrace of a box at the
// origin against the entity's clip model; returns the trace's startsolid byte (+0x3d).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SV_ClipToEntity(uint8_t* ent, const float* mins, const float* maxs,
                                                            uint32_t capsule_flag);
extern "C" void SV_ClipToEntity_entry(void);

// Clip blocks: box (SV_Trace) +0 -extents, +0x10 extents, +0x20 extents + 1, +0x30 / +0x40 start / end moved to
// the box center, +0x50 trace (fraction +0x70, solid bytes +0x8c / +0x8d), +0x90 pass entity, +0x94 its owner,
// +0x98 content mask, +0x9c capsule flag; point: +0 start, +0x10 end, +0x20 trace (fraction +0x40), +0x60 pass,
// +0x64 owner, +0x68 mask, +0x6c / +0x70 / +0x74 SV_Trace's arguments 7, 10, 8.
// 0x002e80b0 (box clip in EAX, server entity in ECX): Q3's SV_ClipMoveToEntities body for one entity - skips other
// contents, the pass entity, its owned entities and entities sharing its owner; CM_TransformedBoxTrace against the
// clip model (angles zero for non-brush models; x/z angles under 0x008fef08 dropped for +0x134 & 0x800000); a
// nearer hit replaces the clip's trace (entity number +0x224 at +0x30), else only the solid bytes are ORed in.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_ClipMoveToEntity(uint8_t* clip, uint8_t* sv);
extern "C" void SV_ClipMoveToEntity_entry(void);
// 0x002e88d0: the box sweep through the sector tree (fractions p1f..p2f, points p1..p2, the clip's +0x20 extents as
// the split offset), Q3 SV_ClipMoveToEntities style; then this sector's entities.
void XB_CDECL SV_ClipMoveToEntities_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                      const float* p2);
// 0x002e84d0: the point version (no offset); each entity through the title's 0x002e79c0 (entity in EAX, clip).
void XB_CDECL SV_PointTraceToEntities_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                        const float* p2);
// 0x002db3e0 (start in EAX; results, end, content mask): the static models of the sector tree (title 0x002d5130 over
// a trace block preset to the results' fraction); a nearer hit replaces the results with endpos set.
void XB_REGS1 XB_CDECL SV_TraceStaticModels(const float* start, uint8_t* results, const float* end,
                                            uint32_t content_mask);
// 0x002e9af0 (start in EBX; results, mins, maxs, end, pass entity, content mask, capsule flag, three more words
// for the point clip, the static-model flag, one more): Q3's SV_Trace - CM_BoxTrace against the world (hit entity
// = the world's number), the static models, then the entities: the point walk for a zero-size box, else the box
// walk with endpos recomputed from the original start/end.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_Trace(const float* start, uint8_t* results, const float* mins,
                                                 const float* maxs, const float* end, uint32_t pass,
                                                 uint32_t content_mask, uint32_t capsule_flag, uint32_t arg7,
                                                 uint32_t arg8, uint32_t static_models, uint32_t arg10);
extern "C" void SV_Trace_entry(void);

// 0x002c0f60 (mins in EAX, maxs in EDI, p in ESI, q on the stack, leave fraction in XMM4): slab test of the
// segment against the box, sign -1 against mins then +1 against maxs, entering fractions pulled back by 0.125;
// 1 when the segment misses (or enters at or after it leaves), 0 when it reaches the box.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SegmentMissesBounds(const float* mins, const float* maxs, const float* p,
                                                                const float* q, float leave);
extern "C" void SegmentMissesBounds_entry(void);
// 0x002d5130: SV_PointTraceToEntities_r over the sectors' static-model lists (block: +0 trace, fraction +0x20,
// +0x40 mask, +0x50 start, +0x60 end; sector contents +4); each model whose contents match and whose bounds
// (+0xa0 / +0xac) the segment reaches goes to the title's 0x002cc880 (block + 0x50 in EAX, model in ECX).
void XB_CDECL SV_TraceStaticModels_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                     const float* p2);
// 0x002e79c0 (server entity in EAX; point clip on the stack): the point version of SV_ClipMoveToEntity. With the
// clip's hit-box flag (+0x6c) and a DObj (+0x22c) whose entity has +0xd4 & 0x18: a box test (DObj bounds or the
// 0x00960110 / 0x0095ff70 box), VM_Call(gvm, 0x12, entity number), the segment moved into the entity's axis
// (AnglesToAxisLeft), then 0x0023da30 (0x10) or DObj_TraceBoneBoxes with radius |local start| * +0x70; a hit
// rotates the normal back. Else CM_TransformedBoxTrace of a point against the clip model. A hit replaces the
// clip's trace (+0x3c = the clip's byte, +0x3d ORed, +0x28 contents, +0x30 entity number).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_PointTraceToEntity(uint8_t* sv, uint8_t* clip);
extern "C" void SV_PointTraceToEntity_entry(void);
// 0x002bfd80: Q3's SV_SetConfigstring - __break 1 for an index outside 0..0x3ff, "" for null; unchanged text
// does nothing; else ScriptString_AssignCString into 0x00964ce0[index] and, while the server runs (0x00964cd4
// == 2, client state 1), "cs %i %s" or for 0xe8+ characters "bcs0/1/2 %i %s" chunks of 0xe7.
void XB_CDECL SV_SetConfigstring(int32_t index, const char* value);

// 0x002d87a0 (gentity in EAX): Q3's SV_SetBrushModel - the inline model of the entity's clip map (+0x220, -1 = the
// default from 0x0095f638 +0x34) through the title's 0x002afb10 (0x1fe / 0x1ff: 0x0095f504; 0: none), its bounds
// into +0xe0 / +0xf0 (0x002b2790), +0xdc model, contents -1, SV_LinkEntity.
void XB_REGS1 XB_CDECL SV_SetBrushModel(uint8_t* ent);
// 0x002c9de0 (msg in EAX; from address by value, 5 words): Q3's SV_PacketEvent - connectionless packets to
// SV_ConnectionlessPacket; else sequence + qport, the one client whose address (NET_CompareBaseAdr inline) and
// qport match takes the port, Netchan_Process (EAX chan, EBX msg) and the title's 0x002c4fe0 (EBX msg); unknown
// senders get "disconnect". 0x0095f510 set around it.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_PacketEvent(uint32_t entry_esp, uint8_t* msg, uint32_t type, uint32_t ip,
                                                       uint32_t ipx1, uint32_t ipx2, uint32_t port_word, uint32_t ebx,
                                                       uint32_t esi, uint32_t edi);
extern "C" void SV_PacketEvent_entry(void);
// 0x002c4e50 (unhookable: push 1 / call at its start; no kFunctions row): Q3's SV_UpdateInfoConfigstrings - the title's 0x002ab560(1), configstrings 0 / 1 from the cvar
// info strings when cvar_modifiedFlags has 4 / 8 (cleared), then VM_Call(gvm, 0xa, time).
void XB_CDECL SV_UpdateInfoConfigstrings(int32_t time);
// 0x002f4400 (msec in ESI): Q3's SV_Frame - sv_killserver, cl_paused, CheckReloadStatus, the info configstrings
// once, VM_Call(gvm, 0x13) as the time limit (shutdown + "vstr nextmap" past it), SV_RunGameFrame, 0x002e6d80;
// title callees at the original's ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_Frame(uint32_t entry_esp, uint32_t ebx, uint32_t msec, uint32_t edx);
extern "C" void SV_Frame_entry(void);

// 0x002e02c0 (client in EDX): Q3's SV_BuildClientSnapshot start - the client entity's playerState (0x520 bytes)
// into client +0x170 (__break 1 without +0x7c), the view origin (z + view height, AddLeanToPosition 16 / 20), its
// leaf, then the title's 0x002c4c50(leaf in EDX, entity number) for the visible entities.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_BuildClientSnapshot(uint8_t* client);
extern "C" void SV_BuildClientSnapshot_entry(void);
// 0x002b80f0 (msg in EAX, client in ESI): Q3's SV_UpdateServerCommandsToClient - each reliable command after the
// acknowledged one (+0x1c) up to +0x18 as svc_serverCommand (5), sequence, text (empty slot / buffer: the empty
// string at 0x008fef0c); +0x20 = +0x18. Bytes written without overflow checks.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_UpdateServerCommandsToClient(uint8_t* msg, uint8_t* client);
extern "C" void SV_UpdateServerCommandsToClient_entry(void);
// 0x002af350 (msg in ESI): svc_snapshot (7), VM_Call(gvm, 0x13) as the server time, the byte at 0x00927e88.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_WriteSnapshotToClient(uint8_t* msg);
extern "C" void SV_WriteSnapshotToClient_entry(void);
// 0x002e4750 (client in EBX): Q3's SV_SendClientMessages for the one client - snapshot, a 0x800-byte message
// (+0x60 first), commands, snapshot, svc_EOF, Netchan_Transmit; 0x0093e6b8 = com_frameNumber.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_SendClientMessages(uint8_t* client);
extern "C" void SV_SendClientMessages_entry(void);
// 0x002e6d80: with a server static, VM_Call(gvm, 0x15), SV_SendClientMessages, 0x0092f548 = 0.
void XB_CDECL SV_SendMessages();

// 0x002a8610 (client in ESI): clears the reliable command slots (+0xc texts, zeroed for their +0x10 lengths)
// from the one before the acknowledged sequence (+0x1c) backwards until an empty slot or the acknowledged one.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_ClearAcknowledgedCommands(uint8_t* client);
extern "C" void SV_ClearAcknowledgedCommands_entry(void);
// 0x002c0040 (msg in EAX, client in ECX): Q3's SV_ClientCommand - sequence + text; a new one is tokenized and run
// through VM_Call(gvm, 5, client entity +0x164) while the server runs (0x00964cd4 == 2), then kept in +0x60 /
// +0x64 ("%s", 0x100). Returns 1.
uint32_t XB_REGS3 XB_CDECL SV_ClientCommand(uint8_t* msg, uint32_t unused, uint8_t* client);
// 0x002b8560 (delta flag in EAX, msg in ECX; client on the stack): Q3's SV_UserMove - deltaMessage (+0x168 =
// +0x24 or -1), 1..0x20 usercmds (MSG_ReadDeltaUsercmd chain from a zero command); for an active client
// (state 1) each command not after the last one goes to +0x30 and VM_Call(gvm, 6, entity).
// VM_Call runs at the original's ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_UserMove(uint32_t entry_esp, uint32_t delta, uint8_t* msg, uint8_t* client);
extern "C" void SV_UserMove_entry(void);
// 0x002c4c50 (leaf in EDX; client entity number on the stack): Q3's SV_AddEntitiesVisibleFromPoint over the
// entity list 0x0097d370 (count 0x0097d36c): linked entities not hidden from this client (+0xd4 flags 1, 0x800 /
// 0x2000 with +0xd8, itself) whose area connects and a cluster is in the leaf's PVS row (Q3's lastCluster check
// kept), broadcast (0x20) or cluster-less ones; others only with +0x1a8 (then +0x10 |= 0x80). Each gets the
// frame stamp 0x00985414 in its render record (+0xfc, CG_GetEntityRefEntity).
void XB_REGS2 XB_CDECL SV_AddEntitiesVisibleFromPoint(uint32_t unused, uint32_t leaf, uint32_t client_num);
// 0x002c4fe0 (msg in EBX; client on the stack): Q3's SV_ExecuteClientMessage - serverId (+0x28), messageAcknowledge
// (+0x24), reliableAcknowledge (+0x1c); the current server id: SV_ClearAcknowledgedCommands, client commands
// (op 2), then a move (op 0 delta / 1); the previous one: only the client commands (sequence read into the
// argument slot). Title callees at the original's ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_ExecuteClientMessage(uint32_t entry_esp, uint8_t* msg, uint32_t esi,
                                                                uint32_t edi);
extern "C" void SV_ExecuteClientMessage_entry(void);

// 0x002e95b0 (cdecl: two words for the world part, results, start, mins, maxs, end, pass entity, content mask): the
// capsule trace used by actor movement and path code - the world through the title's 0x002e20e0 (maxs in EAX,
// mins in ECX, start in EBX), then SV_Trace's box entity part with the capsule flag (owner read without the
// entity null check).
void XB_CDECL SV_CapsuleTrace(uint32_t arg0, uint32_t arg1, uint8_t* results, const float* start, const float* mins,
                              const float* maxs, const float* end, uint32_t pass, uint32_t content_mask);

// 0x002e9440 (cdecl: two words for the world part, results, start, end, pass entity, content mask): the point trace
// behind GScr_LocationalTraceSurface - the world through the title's 0x002d4a60 (end in EAX), then the point
// entity walk with the hit-box flag set (no priority table, radius scale 0); no endpos recompute.
void XB_CDECL SV_LocationalTrace(uint32_t arg0, uint32_t arg1, uint8_t* results, const float* start, const float* end,
                                 uint32_t pass, uint32_t content_mask);

// 0x002cc880 (start in EAX, static model in ECX; clip block, end, content mask on the stack): one static model of
// SV_TraceStaticModels_r - its XModel's base skeleton (XModel_CalcSkeleton into an alloca'd buffer, bones of the
// first LOD's parts), the segment in the model's axis (+0x90 origin, +0xb8 3x3), the title's 0x0023d070 trace;
// a hit (>= 0) gets endpos from the world start/end, the normal rotated back and normalized (x87 sqrt), +0x30 = 0,
// and replaces the block's trace.
void XB_REGS3 XB_CDECL SV_TraceStaticModel(const float* start, uint32_t unused, uint8_t* inst, uint8_t* clip,
                                           const float* end, uint32_t content_mask);

}  // namespace game
