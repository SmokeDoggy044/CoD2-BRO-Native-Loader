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
// 0x002f48e0: SV_AddOperatorCommands, then the server cvars (some
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
// The C body: the rebuilt frame (ESP after the original's prologue), the entry EBP (arguments at +8), EBX.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_Trace(uint32_t frame, uint32_t ebp, uint32_t start);
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
// 0x002c4e50: Q3's SV_UpdateInfoConfigstrings - the title's 0x002ab560(1), configstrings 0 / 1 from the cvar
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
// The C body: the rebuilt frame (ESP after the original's prologue) and the entry EBP (arguments at +8).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_LocationalTrace(uint32_t frame, uint32_t ebp, uint32_t entry_esi);
extern "C" void SV_LocationalTrace_entry(void);

// 0x002cc880 (start in EAX, static model in ECX; clip block, end, content mask on the stack): one static model of
// SV_TraceStaticModels_r - its XModel's base skeleton (XModel_CalcSkeleton into an alloca'd buffer, bones of the
// first LOD's parts), the segment in the model's axis (+0x90 origin, +0xb8 3x3), the title's 0x0023d070 trace;
// a hit (>= 0) gets endpos from the world start/end, the normal rotated back and normalized (x87 sqrt), +0x30 = 0,
// and replaces the block's trace.
void XB_REGS3 XB_CDECL SV_TraceStaticModel(const float* start, uint32_t unused, uint8_t* inst, uint8_t* clip,
                                           const float* end, uint32_t content_mask);

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

// 0x002e75d0 (end in EDI, maxs in EBX; start, mins, entity handle, content mask, capsule flag on the stack): a sight
// trace against one entity - its contents (+0x134) in the mask, its bounds (+0x100 / +0x110) meeting the swept box
// (padded by 0x0082e640), then CM_TransformedSightTrace as SV_BoxSightClipToEntity. -1 when blocked, else 0.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL SV_EntitySightTrace(const float* end, const float* maxs, const float* start,
                                                                const float* mins, uint32_t handle,
                                                                uint32_t content_mask, uint32_t capsule_flag);
extern "C" void SV_EntitySightTrace_entry(void);


// 0x0023da30 (DObj, start, end, content mask, results): every model of the DObj (+0x2e count, +0x38 list) through
// XModel_TraceParts with its part of the skeleton (+0x20, from bone +0x60 * 0x40 + 0x30); a hit's bone becomes the
// part's +0x14 record's +4 word in results +0x14. Results: +0 fraction (in/out), +4 trace +0x24, +8 normal,
// +0x18 zeroed, +0x1c/+0x1d cleared.
void XB_CDECL SV_TraceDObjModels(const uint8_t* obj, const float* start, const float* end, uint32_t content_mask,
                                 uint8_t* results);
// 0x002991c0 (FX system in EAX, the level's 0x0095f5b4): frees its effects (+0 count, +4 list: components and the
// small-heap block), deletes its +0x808 objects (+0x804 count, vtable slot 0 with 1) and releases the 64 script
// strings at +0xb1c (0x210 apart, last first).
void XB_REGS1 XB_CDECL FxSystem_FreeLevel(uint8_t* fx);
// 0x002a86e0 (restart flag in ECX): one game frame for a restart - VM_Call(8, flag, sv +0) with 0x0095f510 set,
// then VM_Call(0x13) into 0x0092f094 (0x0093fc28 counts packets, never 0).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_GameRestartFrame(uint32_t entry_esp, uint32_t flag, uint32_t ebx, uint32_t esi,
                                                            uint32_t edi);
extern "C" void SV_GameRestartFrame_entry(void);
// 0x002aad90: Q3's SV_ClearWorld - the sector free list (0x3fc sectors of 0x24 bytes from 0x009356b0), the root
// split on the longer world axis at its midpoint, both children the null sector, cm +0x4c..+0x54 copied.
void XB_CDECL SV_ClearWorld();
// 0x002af4f0 (client in ESI): frees the frame buffers SV_AllocClientFrames made (+8, +0x10, +0xc) and clears
// the count (+4) when it is set.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_FreeClientFrames(uint8_t* client);
extern "C" void SV_FreeClientFrames_entry(void);
// 0x002af580 (count in EAX, client in ESI): count * 0x180 bytes of snapshot frames (+8, size +4, also +0x14) and
// two count-dword arrays (+0x10, +0xc), all zeroed.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL SV_AllocClientFrames(uint32_t count, uint8_t* client);
extern "C" void SV_AllocClientFrames_entry(void);
// 0x002b8200 (msg in EAX; from address by value): reads the -1 sequence and the command line (0x002aa480), tokenizes it; nothing
// is answered (NET_AdrToString runs once, again unless argv 0 is "disconnect").
void XB_REGS1 XB_CDECL SV_ConnectionlessPacket(uint8_t* msg, uint32_t type, uint32_t ip, uint32_t ipx1, uint32_t ipx2,
                                               uint32_t port_word);
// 0x002b82d0: frees the local client's frames, releases the 0x400 configstrings (0x00964ce0) and clears sv
// (0x00964cd0, 0x1018 bytes) and 0x0092f548.
void XB_CDECL SV_ClearServer();
// 0x002b8390 (index in EAX, size in ESI, buffer in EDI): configstring `index` (text at +6 of its string record)
// copied with a NUL (__break 1 for a size under 1 or an index outside 0..0x3ff); EAX = _strncpy's result.
extern "C" PLATFORM_HIDDEN char* XB_CDECL SV_GetConfigstring(int32_t index, int32_t size, char* buffer);
extern "C" void SV_GetConfigstring_entry(void);
// 0x002ba910 (static model in ECX): walks from the root while the model's bounds (+0xa0 / +0xac) lie on one side
// of the split (contents ORed in), links the model at that sector's +0x14 (chain +0xdc), then SV_SplitSector over
// the region narrowed on the way (x/y of the world bounds).
void XB_THISCALL SV_LinkStaticModelToSector(uint8_t* inst);
// 0x002bdbd0 (bank list in EAX; 32-byte name hash on the stack): 0 when the 0x84-byte entry with that hash (+0xc,
// +8 count) has one of its six states (+0x30..+0x44) at 1 or 3 (still loading), else 1 (also when none matches).
uint8_t XB_REGS1 XB_STDCALL SndBankList_IsLoaded(const uint8_t* list, const uint32_t* hash);
// 0x002c1ff0 (persistent level data in EAX, the level's 0x0095f4e0): frees its buffers (+0x580, +0x574 list
// +0x570, +0x560 aligned, +0x364, +0x358 list +0x354) and releases its script strings (+0x55c, +0x558, 16 at
// +0x394 0x1c apart, 6 at +8).
void XB_REGS1 XB_CDECL LevelPersist_Free(uint8_t* data);
// 0x002c4f50: Q3's SV_RestartGameProgs - the saved game buffer (0x00965ce0, size 0x00965ce4) copied into a new
// block, VM_Call(1, savegame) (GAME_SHUTDOWN), VM_Call(0, Com_Milliseconds(), 1, savegame, sv +0) (GAME_INIT);
// EAX = the init result, the client's +0x164 cleared.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SV_RestartGameProgs(uint32_t entry_esp, uint32_t savegame, uint32_t ebx,
                                                                uint32_t esi, uint32_t edi);
extern "C" void SV_RestartGameProgs_entry(void);
// 0x002ca360 (name buffer in EAX, 0x80 bytes): "save/autosave/<mapname>.svg" from cvar mapname ('/' becomes '-'; any other
// character than letters, digits, '_', '-' and '\\' gives ""); "" unless FS_ReadFile
// finds it.
void XB_REGS1 XB_CDECL SV_CheckAutosave(char* name);
// 0x002d86b0: Q3's SV_ShutdownServer when com_sv_running is set: game progs, SV_ClearServer, the client block
// freed, svs cleared (15 dwords at 0x00927e84), sv_running "0"; with 0x00947704 set r_uiFullScreen "1",
// CL_ClearState and cls.state 0.
void XB_CDECL SV_ShutdownServer();
// 0x002e03a0 (from address by value): the local client (0x00927e8c) cleared, its player entity allocated
// (G_AllocPlayerEntity, EDI = 0x0095f534), its netchan (+0x690) cleared and given the address, 0x40 frames.
void XB_CDECL SV_ConnectLocalClient(uint32_t type, uint32_t ip, uint32_t ipx1, uint32_t ipx2, uint32_t port_word);
// 0x002f2d30: Q3's SV_InitGameProgs - VM_Create("game", SV_GameSystemCalls) into gvm (__break 0 when null), no
// saved game buffer, VM_Call(0, Com_Milliseconds(), 0, savegame, sv +0), the client's +0x164 cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_InitGameProgs(uint32_t entry_esp, uint32_t savegame, uint32_t ebx, uint32_t esi,
                                                         uint32_t edi);
extern "C" void SV_InitGameProgs_entry(void);
// 0x002a6e00: Q3's SV_Shutdown for a level end - frees the level's managers and blocks (0x0095f5e0 ... 0x0095f5c4:
// deleting destructors, LevelPersist_Free, FxSystem_FreeLevel, 0x002ce6a0, S_FreeLevelSounds, StreamMem_Free,
// IN_StopAllRumble), PakRender_Shutdown, Phys_DestroyWorld, r_uiFullScreen "1" + CL_ClearState when 0x00947704
// is set, CL_ShutdownCGame, the front-end return state, re's shutdown (0x009284e8) with 0, SV_ShutdownServer, then
// CL_StartHunkUsers (tail jump).
void XB_CDECL SV_Shutdown();
// 0x002f2e70 (client in EDI; first, savegame on the stack): the client's entity number from its player entity,
// ClientConnect, VM_Call(3) (GAME_CLIENT_BEGIN); unless both flags are set also SV_UpdateInfoConfigstrings(50),
// VM_Call(7, savegame) and G_RunFrame(50) with 0x0095f510 set.
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_RestartClient(uint32_t entry_esp, uint8_t* client, uint32_t first,
                                                         uint32_t savegame, uint32_t ebx, uint32_t esi, uint32_t edi);
extern "C" void SV_RestartClient_entry(void);
// 0x002f2f50: Q3's SV_MapRestart - 1 (nothing done) when the frame time is the restart's or the server is off;
// else cl_paused "1", SV_RestartGameProgs (0 when it fails), a new sv_serverid, the local client reconnected
// (frames, netchan, CL_ResetLocalConnection), path node script keys, SV_RestartClient, one game frame
// (SV_GameRestartFrame, VM_Call(0x15), SV_SendClientMessages), configstrings refreshed, cls.state 2, cl_paused "0".
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SV_MapRestart(uint32_t entry_esp, uint32_t savegame, uint32_t ebx,
                                                          uint32_t esi, uint32_t edi);
extern "C" void SV_MapRestart_entry(void);
// 0x002f4510 ("loadlastsave"): SV_CheckAutosave into 0x0093ef68 (__break 1 EXE_ERR_NO_LAST_SAVE when there is
// none), then CheckReloadStatus (tail jump) unless the server runs.
uint32_t XB_CDECL SV_LoadLastSave_f();
// 0x002f4550 ("loadgame"): with g_reloading 0 or 0x20 (then set "0"), argv 1 into 0x0093ef68 with "save/" in front
// unless it starts so (or "save\"), ".svg" appended unless its first '.' is followed by "svg", '\' turned into
// '/'; then CheckReloadStatus (tail jump) unless the server runs.
uint32_t XB_CDECL SV_LoadGame_f();
// 0x003767b0, 0x003767f0: CRT static initializers of two vec4 constants (x, x, z, x) at 0x00960110 (from 0x0083f1f4,
// 0x0083f180) and 0x0095ff70 (0x00830c1c, 0x0083ecdc).
void XB_CDECL StaticInit_Vec4_960110();
void XB_CDECL StaticInit_Vec4_95ff70();
// 0x002f3140 (map name, savegame flag): Q3's SV_SpawnServer for the local game - client and cgame shut down, the
// file system restarted, cvars (mapname, sv_serverid ...), the local client connected, configstrings cleared; the
// previous map freed (effects, stream zones, path nodes, anim trees); the map's stream entry, load sound and sound
// banks (+0x24 flags), CM_LoadMap and the zone stream, SV_InitGameProgs, the skill cvars, five settle frames, the
// server/system info configstrings, path-node script keys and anim scripts, the client begun, the cgame started,
// the map's sound bank waited for; loading bar steps 0.85 / 0.95 / 1.0 / 0.95. The asm entry rebuilds the frame.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SV_SpawnServer(uint32_t frame, uint32_t ebp, uint32_t ebx, uint32_t esi);
extern "C" void SV_SpawnServer_entry(void);
// 0x002f3f40 ("map"): with g_reloading 0 or 2, argv 1 lowered; the chapter movie (oran, gela and a third map) unless
// 0x00940178 is set; g_currentChapter from the 14-map table (g_chaptersUnlocked raised to it); a ".svg" name is a
// saved game: its file (under "save/") gives the map name and a cvar value (0x00832fb4); then SV_SpawnServer with
// the save flag and cvar 0x008340d0 "1".
void XB_CDECL SV_Map_f();

}  // namespace game
