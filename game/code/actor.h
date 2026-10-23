#pragma once

// Game module, AI: actors (g_actors 0x00985384, 0x1400 bytes each), sentients (g_sentients
// 0x00985378, 33 x 0x60) and the per-state callback table g_aiStateFuncs (0x00830c28: 13
// states x 9 slots - Start, Finish, Suspend, Resume, Think, ?, Touch, Pain, NodeClaimRevoked).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x000123d0: the team hostile to `team` ({0, 2, 1, 0, 0}[team]).
int32_t XB_CDECL Sentient_EnemyTeam(int32_t team);
// 0x001fe040 (out in EAX, sentient in ECX): the sentient's entity's current origin (+0x140).
float* XB_REGS3 XB_CDECL Sentient_GetOrigin(float* out, uint32_t unused_edx, uint8_t* const* sentient);
// 0x001fa5b0 (sentient in EAX): goal yaw (+0x20) is not -1.
uint32_t XB_REGS1 XB_CDECL Sentient_HasGoalYaw(const uint8_t* sentient);
// 0x001fc2b0 (sentient in EAX, yaw in XMM0): stores the yaw wrapped into [0, 360) at +0x20.
extern "C" PLATFORM_HIDDEN void XB_CDECL Sentient_SetGoalYaw(uint8_t* sentient, float yaw);
extern "C" void Sentient_SetGoalYaw_entry(void);
// 0x001fa570 (team mask in ESI): the first used sentient (+0x5c >= 0) whose team (+4) is in
// the mask, or 0.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Sentient_FirstOfTeams(uint32_t mask);
extern "C" void Sentient_FirstOfTeams_entry(void);
// 0x001fa510 (previous sentient in EAX, team mask in EDI): the next such sentient after it.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Sentient_NextOfTeams(const uint8_t* previous, uint32_t mask);
extern "C" void Sentient_NextOfTeams_entry(void);
// 0x001fa480: clears the sentient bookkeeping (0x0095f420 = -1, ten words after it = 0) and
// marks every sentient unused (+0x5c = -1).
void XB_CDECL Sentient_ClearAll();

// AI state callbacks (actor in ECX).
// 0x001fb280: the grenade state resumes only while +0x1310 is 0.
uint32_t XB_THISCALL Actor_Grenade_Resume(const uint8_t* actor);
// 0x001fb460: +0xc4 = level time; 0.
uint32_t XB_THISCALL Actor_CriticalSection_Start(uint8_t* actor);
// 0x001fc520: +0x12c = 1, +0x268 = 2; 0.
uint32_t XB_THISCALL Actor_Negotiation_Start(uint8_t* actor);
// 0x001faec0 (path in EAX): lookahead distance (+0x3b4) at most 0.001.
uint32_t XB_REGS1 XB_CDECL Path_LookaheadAtMinimum(const uint8_t* path);
// 0x001fa7c0 (actor in EAX): one of the four sentient slots (+0x12a8 stride 0x18) is set and
// its +0x14 word is 0.
uint32_t XB_REGS1 XB_CDECL Actor_HasPendingSentientSlot(const uint8_t* actor);
// 0x001fbfb0 (actor in EDX, state in ESI): the state is on the actor's state stack (+0xa4,
// top index +0xbc).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_IsInState(const uint8_t* actor, uint32_t state);
extern "C" void Actor_IsInState_entry(void);
// 0x001fc210 (record in EAX, visible in CL, XMM0 value): {visible byte, value, time of the
// last update, time last seen}.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_SetSentientVisibility(uint8_t* record, uint32_t visible, float value);
extern "C" void Actor_SetSentientVisibility_entry(void);

// 0x001fb860 (entity in ECX): the actor's squad slot (int16 +0x1382) when squads are on
// (0x00986008), the actor has one (+0x137c) and the sentient's team (+4) is 2; else -1.
int32_t XB_THISCALL Actor_GetSquadSlot(const uint8_t* ent);
// 0x001fc0c0 (team mask in ESI): the first used actor (+0x1360 >= 0) whose sentient's team is
// in the mask, or 0.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Actor_FirstOfTeams(uint32_t mask);
extern "C" void Actor_FirstOfTeams_entry(void);
// 0x001fc060 (previous actor in EAX, team mask in ESI): the next one after it (index +0x1382).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Actor_NextOfTeams(const uint8_t* previous, uint32_t mask);
extern "C" void Actor_NextOfTeams_entry(void);
// 0x001fb3f0 (attacker sentient in EAX, actor in ECX): records the attacker (+0x12a4) and the
// time in its per-sentient record (+0x864, 0x50 each) unless the entity ignores it (flag
// 0x10000000 at +0x2a4).
void XB_REGS3 XB_CDECL Actor_WasAttackedBy(const uint8_t* attacker, uint32_t, uint8_t* actor);
// 0x001fa680 (actor in ESI): clears the enemy's last-known time in the per-sentient record
// (+0x868) of the sentient at +4 -> +0x3c.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_ForgetEnemyLastKnownTime(uint8_t* actor);
extern "C" void Actor_ForgetEnemyLastKnownTime_entry(void);
// 0x001fe0c0 (actor in ESI): the turret the actor holds (+0x136c) names the actor's entity
// as its user (entity handle +0x1a0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_IsUsingTurret(const uint8_t* actor);
extern "C" void Actor_IsUsingTurret_entry(void);
// 0x001fd2b0 (entity pointer in ECX, node in EDX): squared distance from the entity's origin
// to the node's (+0x44); differences stored as floats, the sum unrounded in ST0.
long double XB_REGS3 XB_CDECL Actor_NodeDistanceSquared(uint32_t, const uint8_t* node, uint8_t* const* ent);
// 0x001fb640 / 0x001fb6a0 (actor in EAX, callee pops 4): the anim script to run (+0x254) - the
// given one within 200 ms before +0x798, else the run / walk script; +0x27c = 1, +0x285 = 0.
uint8_t* XB_REGS1 XB_STDCALL Actor_AnimRun(uint8_t* actor, uint32_t script);
uint8_t* XB_REGS1 XB_STDCALL Actor_AnimWalk(uint8_t* actor, uint32_t script);
// 0x001fbf10 (state in EAX, actor in ECX): no state on the stack above the base (+0xa8..) is
// one of the five that block it (table 0x00830da0 + state * 20).
uint32_t XB_REGS3 XB_CDECL Actor_IsStatePushAllowed(uint32_t state, uint32_t, const uint8_t* actor);
// 0x001fc020 (actor in ESI, callee pops 4): sets the top state slot and runs its Start, then
// the state each Start returns, 13 at most; returns the last result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_StartState(uint32_t entry, uint8_t* actor, uint32_t state,
                                                             uint32_t ebx);
extern "C" void Actor_StartState_entry(void);
// 0x001fbd60 (path in EAX, actor in EDX): copies the path's points (int16 count +0x382, 0x1c
// each) and its 0x5c-byte header (+0x380) into the actor's path (+0x3c0).
const uint8_t* XB_REGS2 XB_CDECL Path_CopyToActor(const uint8_t* path, uint8_t* actor);
// 0x001fbda0 (actor in EAX, path in EDX): the reverse.
void XB_REGS2 XB_CDECL Path_CopyFromActor(const uint8_t* actor, uint8_t* path);
// 0x00200400 (to in EAX, from in ECX): copies what one sentient knows about another (+0x18..
// +0x48), clearing +0x14 and +0x1c.
uint8_t* XB_REGS3 XB_CDECL Actor_CopySentientInfo(uint8_t* to, uint32_t, const uint8_t* from);
// 0x001fb5c0 (actor in EAX): the reacquire anim script - the actor's own (+0x270, mode +0x274,
// 1 -> 2) on team 2, else the shared one with mode 2.
uint8_t* XB_REGS1 XB_CDECL Actor_AnimReacquire(uint8_t* actor);
// 0x001fa950 (actor in EDI, callee pops 4): the suppression slot (+0x12a8, 4 x 0x18 {time,
// sentient}) holding the sentient, else the one with the oldest time.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_FindSentientSlot(const uint8_t* actor, uint32_t sentient);
extern "C" void Actor_FindSentientSlot_entry(void);
// 0x001faee0 (path in EAX): shrinks the lookahead distance (+0x3b4; (d - 6.4) * 0.75 with flag
// 2 of +0x3bc, else (d - 12.8) * 0.5625; at least 0.001) and clears flags 1 and 2.
uint8_t* XB_REGS1 XB_CDECL Path_ShrinkLookaheadBy(uint8_t* path);
// 0x001fb1f0 (actor in EAX): no look-at target; limits 120 / 90 / 90.
uint8_t* XB_REGS1 XB_CDECL Actor_ClearLookAt(uint8_t* actor);
// 0x001faf40 (path in ECX, distance in XMM1): the remaining path (current segment +0x3b8 +
// +0x3b0, then the points' lengths +0x14 backwards) is longer than distance; always with
// +0x386 set.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Path_IsLongerThan(const uint8_t* path, float distance);
extern "C" void Path_IsLongerThan_entry(void);
// 0x001fce60 (path in EAX, goal in ECX, callee pops 4): the path's end (+0x390) is farther
// from the goal than radius + 0.005 (squared distance against it as is), or flag 4 (+0x3bc)
// is set with the point count (+0x380) at most a quarter of +0x382.
uint32_t XB_REGS3 XB_STDCALL Path_IsStale(const uint8_t* path, uint32_t, const float* goal, float radius);
// 0x001fe110 (actor in EAX, callee pops 4): whether an event from the entity at +0x224 is
// worth noticing for the actor info block at base - the sentient's record (+0x850, 0x50 each):
// still suppressed (+0x1c after now), never noticed, or 2 s since (+0x18).
uint32_t XB_REGS1 XB_STDCALL Actor_IsEventWorthNoticing(const uint8_t* event, const uint8_t* base);
// 0x001fa7f0 (actor in EAX, sentient in EDX): clears the suppression slots holding it.
uint8_t* XB_REGS2 XB_CDECL Actor_ClearSentientSlots(uint8_t* actor, uint32_t sentient);
// 0x001fc2f0: a free sentient (+0x5c == -1), else the unused one freed longest ago (the
// largest other negative +0x5c); cleared, +0x5c = level time. 0 when none.
uint8_t* XB_CDECL Sentient_Alloc();
// 0x00204850 (out in EBX, direction in ESI, callee pops 4): the direction's pitch and yaw +
// offset, through 16-bit angles, into +4 and +8/+0xc.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_SetOrientFromDir(uint8_t* out, const float* dir, float yaw_offset);
extern "C" void Actor_SetOrientFromDir_entry(void);
// 0x001fbc50 (actor in EAX): clears the move history (+0x1394 index, 20 floats from +0x1398).
uint8_t* XB_REGS1 XB_CDECL Actor_ClearMoveHistory(uint8_t* actor);
// 0x00200050 (sentient in ECX, actor in ESI): records where the sentient is now in the actor's
// record of it (+0x850 + index * 0x50: time +0x18, origin +0x20, node +0x30/+0x34, the
// actor's own origin +0x40). Returns the record.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Actor_RecordSentientPosition(const uint8_t* sentient, uint8_t* actor);
extern "C" void Actor_RecordSentientPosition_entry(void);
// 0x00203130 (actor in EAX): the entity's sight bounds (+0x430 = FLT_MAX x4, +0x440 = their
// negation) and the per-sentient records' times (+0x860) reset.
void XB_REGS1 XB_CDECL Actor_ResetSentientInfo(uint8_t* actor);
// 0x001fc3f0 (sentient in EAX, actor in ESI, distance in XMM1): (2000 - d)^2 * 0.00125 minus
// the ms since it was last seen (0 while visible), rounded; 0 beyond 2000 or when ignored.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Actor_ScoreEnemyByDistance(const uint8_t* sentient, const uint8_t* actor,
                                                                       float distance);
extern "C" void Actor_ScoreEnemyByDistance_entry(void);
// 0x001fabd0 (path in ECX, out in EDX, origin in ESI): the point at most 60 units along the
// lookahead direction (+0x39c, x and y only); 0 without a path or with flag 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Path_GetLookaheadPoint(const uint8_t* path, float* out,
                                                                    const float* origin);
extern "C" void Path_GetLookaheadPoint_entry(void);
// 0x001fbaf0 (out in EAX, actor in ECX): the x/y sum of the ten move-history steps (+0x1398),
// z = 0.
float* XB_REGS3 XB_CDECL Actor_SumMoveHistory(float* out, uint32_t, const uint8_t* actor);
// 0x002ba1c0 (sentient in EDX): the claim expires in 5 s (+8); each claimed node (handles
// +0x58, +0x5a; 0 and 0xffff = none, the first none ends it) loses a claim (+0x26) and, at
// none left, keeps the claimer (+4) and expiry (+8).
void XB_REGS2 XB_CDECL Sentient_ClaimNodeTimed(uint32_t, uint8_t* sentient);
// 0x002ba360 (claimer in EAX, sentient in ECX): claims the nodes with no expiry
// (0x7fffffff), adding a claim to each.
void XB_REGS3 XB_CDECL Sentient_ClaimNodeIndefinitely(uint32_t claimer, uint32_t, uint8_t* sentient);
// 0x001fa860 (actor in EAX): expires the suppression slots - after 300 ms for those with
// +0x14 set, else after +0x1308 ms - clearing the sentient's +0x5a byte.
uint8_t* XB_REGS1 XB_CDECL Actor_ExpirePathPlanes(uint8_t* actor);
// 0x00201520 (actor in EAX): the entity stands within 3 units of the sentient's claimed node
// (+4 -> handle +0x48; node origin +0x44).
uint32_t XB_REGS1 XB_CDECL Actor_IsOnClaimedNode(const uint8_t* actor);
// 0x001fcef0 (path in EDX, origin in ECX): takes the area swept by the points between the
// lookahead start (+0x380) and end (+0x382) - |cross(point direction, point - origin)| times
// the length, the first relative to +0x3c8 - off the lookahead distance (+0x3b4, at least 64).
void XB_REGS3 XB_CDECL Path_ShrinkLookahead(uint32_t, uint8_t* path, const float* origin);
// 0x001fde90 (sentient in ESI, callee pops 4): yaw lies within the goal arc (+0x24 either side
// of the goal yaw +0x20; fmod 360 on the x87, wrapped into [-180, 180]); always without a goal
// yaw (-1).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Sentient_IsYawInGoalArc(const uint8_t* sentient, float yaw);
extern "C" void Sentient_IsYawInGoalArc_entry(void);
// 0x001fed20 (count in EAX, path in ESI): drops the first count points - indices (+0x380..
// +0x388) moved back, the rest moved down (or the path reset to its end +0x3cc when at most
// one point is left) - and restarts the lookahead at the first point.
extern "C" PLATFORM_HIDDEN void XB_CDECL Path_DropPassedPoints(uint32_t count, uint8_t* path);
extern "C" void Path_DropPassedPoints_entry(void);
// 0x00201b90 (actor in EAX, callee pops 4): turns the entity's yaw (+0x154) toward the desired
// yaw (+0x14c with +0x140 set, else +0x138; kept in +0x154) by at most msec * rate, through
// a 16-bit angle; pitch and roll cleared.
void XB_REGS1 XB_STDCALL Actor_UpdateYaw(uint8_t* actor, int32_t msec);
// 0x00202a10 / 0x00202a30 / 0x00202a50 (actor in ECX, toucher in EDX): the turret, noncombat
// and lmg states' Touch - records where the toucher's sentient (+0x244) is. Identical copies.
void XB_REGS3 XB_CDECL Actor_Turret_Touch(uint32_t, const uint8_t* other, uint8_t* actor);
void XB_REGS3 XB_CDECL Actor_Noncombat_Touch(uint32_t, const uint8_t* other, uint8_t* actor);
void XB_REGS3 XB_CDECL Actor_Lmg_Touch(uint32_t, const uint8_t* other, uint8_t* actor);
// 0x00202b60 (actor in EAX, callee pops 4): records the sentient's position unless the actor's
// entity ignores events (0x10000000 at +0x2a4).
void XB_REGS1 XB_STDCALL Actor_EventNoticeSentient(uint8_t* actor, const uint8_t* sentient);
// 0x00202ba0 (actor in EAX, attacker sentient in EDI): Actor_WasAttackedBy and its position
// recorded, unless the entity ignores events.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_EventAttacked(uint8_t* actor, const uint8_t* attacker);
extern "C" void Actor_EventAttacked_entry(void);
// 0x001fd260 (grenade entity in EDI): every actor forgets it as its grenade (+0x1310); the
// grenade's +0x378 cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_ForgetGrenade(uint8_t* grenade);
extern "C" void Actor_ForgetGrenade_entry(void);
// 0x00204060 (actor in ESI): the actor's entity's anim tree, created on first use.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_GetAnimTree(const uint8_t* actor);
extern "C" void Actor_GetAnimTree_entry(void);
// 0x001fd320 (next state in EDX, actor in ECX): the animscripted state's Finish - frees the
// entity's scripted-anim block (+0x3d4) unless the next state is animscripted again (10).
void XB_REGS3 XB_CDECL Actor_AnimScripted_Finish(uint32_t, uint32_t next, const uint8_t* actor);
// 0x001fd860 (actor in EAX): collapses the queued state transitions (count +0xa0, entries
// {state, param} from +0x28) pairwise through g_aiTransitionMerge (0x00830e00, [a * 4 + b];
// -1 = keep both, 0 = both cancel); without +0x2d0 the queue is reset to one entry.
uint8_t* XB_REGS1 XB_CDECL Actor_MergeStateTransitions(uint8_t* actor);
// 0x002000e0 (actor in EAX): queues a pop transition (3), merges, drops the top state.
void XB_REGS1 XB_CDECL Actor_PopState(uint8_t* actor);
// 0x00200110 (actor in EAX, state in ESI): queues a set transition (1, state), merges, and
// replaces the top state.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_SetState(uint8_t* actor, uint32_t state);
extern "C" void Actor_SetState_entry(void);
// 0x00201a10 / 0x00201a80 (actor in EAX, callee pops 4): Actor_AnimRun / Actor_AnimWalk, or
// for an actor on a vehicle path (+0x1390) stop or walk by its speed (+0x2a0, +0x2a4) with mode
// 7.
uint8_t* XB_REGS1 XB_STDCALL Actor_AnimMoveRun(uint8_t* actor, uint32_t script);
uint8_t* XB_REGS1 XB_STDCALL Actor_AnimMoveWalk(uint8_t* actor, uint32_t script);
// 0x00202bd0 (sentient in EAX, actor in ECX): the actor learns where the sentient's enemy
// (+0x3c) is - from the other actor's newer record (CopySentientInfo), or by recording it.
void XB_REGS3 XB_CDECL Actor_EventLearnFromActor(const uint8_t* sentient, uint32_t, uint8_t* actor);
// 0x001fc360 (callee pops 4): clears the threat time (+0x86c) of every sentient of the enemy
// team.
void XB_STDCALL Actor_ClearSentientThreatTimes(uint8_t* actor);
// 0x001fd1f0 (actor in ECX): the lmg state's Start - with a weapon of class 3 (+0xf0 hash)
// it arms the fire timers (+0x28 = 0x2bc unless 0x2be, +0x2c, +0x34) and stays (0); otherwise
// it goes to exposed (2) when the sentient has an enemy (+4 -> +0x3c), else noncombat (5).
uint32_t XB_THISCALL Actor_Lmg_Start(uint8_t* actor);
// 0x002026f0 (actor in ESI, state in EDI): finishes every state on the stack (Finish with the
// new state in EDX), clears the queues and sets the state.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_ForceState(uint32_t entry, uint8_t* actor, uint32_t state, uint32_t ebx);
extern "C" void Actor_ForceState_entry(void);
// 0x00202c40 (actor in EDI): ends the running anim script - its stop callback (+0x250 -> +0xc,
// EAX = the script, the entity number on the stack), its thread (+0x24c) flagged to end and
// "killanimscript" notified.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_StopAnimScript(uint8_t* actor);
extern "C" void Actor_StopAnimScript_entry(void);
// 0x00201380 (mode in EAX, actor in ESI): mode 1 with a live threat time (+0x86c record of the
// enemy) sets the exposed state (1); then arms the reacquire timers (+0x28 = 0xca, +0x2c,
// +0x34).
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_Exposed_StartReacquire(uint32_t mode, uint8_t* actor);
extern "C" void Actor_Exposed_StartReacquire_entry(void);
// 0x00200960 (callee pops 8): moves the sentient's node claim (+0x48 handle) to node -
// the old node's claim times out, the new one is claimed indefinitely.
void XB_STDCALL Sentient_SetClaimedNode(uint8_t* sentient, uint32_t node);
// 0x002014a0 (actor in ECX, attacker entity in EDX, callee pops 0x14): the cover state's Pain
// - an allied actor (team 1) hurt by a non-ally marks its claimed node dangerous for 5 s
// (node +0x14 + team * 4).
void XB_REGS3 XB_STDCALL Actor_Cover_Pain(uint32_t, const uint8_t* attacker, const uint8_t* actor, uint32_t,
                                          uint32_t, uint32_t, uint32_t, uint32_t);
// 0x002053b0 (actor in EAX, pitch in XMM0, callee pops 4): the desired angles (+0x100, +0x104,
// through 16-bit angles) and their axis (+0x108 forward, +0x114 right, +0x120 up).
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_SetDesiredAngles(uint8_t* actor, float pitch, float yaw);
extern "C" void Actor_SetDesiredAngles_entry(void);
// 0x001fc480 (entity in ESI): an actor spawner's entity (type 0xc, no contents / clip, ...);
// +0x370 = 1 unless the spawn vars have "count" (hash cached in 0x0098b5d0). Returns 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_InitSpawner(uint8_t* ent);
extern "C" void Actor_InitSpawner_entry(void);
// 0x00200360 (memory context index in EAX, -1 = none): a zeroed actor (0x1400 bytes, 16-byte
// aligned; context 0x0095f638 +0x38 list, else the heap) in the first free g_actors slot
// (+0x1382 = slot, -1 when full), spawn time +0x1360, Actor_InitDefaults. 0 when out of memory.
uint8_t* XB_REGS1 XB_CDECL Actor_Alloc(int32_t context);
// 0x00200e20 (path in EAX, callee pops 4): one of the path's links (consecutive points' nodes,
// +0x18) lies in the bad place (PathNode_IsLinkInBadPlace).
uint32_t XB_REGS1 XB_STDCALL Path_CrossesBadPlace(const uint8_t* path, uint32_t bad_place);
// 0x00205a70: ends the bad places (32 at 0x0098a570, 0x28 bytes: expiry +0, name +4, kind +8,
// +9, arc +0xc) whose time is up - arcs (kind 1) taken off the nodes, the name cleared.
void XB_CDECL G_ExpireBadPlaces();
// 0x001fc130: empties g_actors; 0x0095ee0c = 0x00987a94, 0x0095f44c = -1.
void XB_CDECL Actor_ClearAll();
// 0x001fd990 (actor in EAX): default tuning (ranges, accuracy, timers; state stack base 5).
uint8_t* XB_REGS1 XB_CDECL Actor_InitDefaults(uint8_t* actor);
// 0x001fff80 (actor in EDX): the movement block (+0x290, 0x4c words) cleared and set up from
// the entity (number +0x224, bounds +0xe0/+0xf0) with clip mask 0x2820011.
void XB_REGS2 XB_CDECL Actor_InitPhysics(uint32_t, uint8_t* actor);

// 0x001fe3c0 (path in EAX): the negotiation node at the current path point (+0x386; record
// handle +0x18) and the linked node (type 0x11) whose script name (+0x30) equals its own (+0x38);
// 0 if none.
uint8_t* XB_REGS1 XB_CDECL Path_GetNegotiationEndNode(const uint8_t* path);
// 0x001fee70 (path in ESI, point in EBX): from the last path point back toward the start, the
// point nearest `point` in xy (__CIsqrt distances); the weighted |cross| sums of the segments up to
// it, plus the last segment's ((+0x14 - path +0x3c8) * |cross|), are added to path +0x3b4.
extern "C" PLATFORM_HIDDEN void XB_CDECL Path_AccumulateTrim(uint8_t* path, const float* point);
extern "C" void Path_AccumulateTrim_entry(void);

// 0x001fac60 (path in EAX, point in EBX): moves the path's end (+0x380 count, +0x3cc point, +0x3c8
// remaining length of the last segment) back toward `point` along the direction +0x39c: from the
// last segment back to the first point ahead of `point`, then interpolated (or one whole point
// dropped when the step covers the segment); the lookahead count +0x384 follows.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Path_AdvanceAlong(uint8_t* path, const float* point);
extern "C" void Path_AdvanceAlong_entry(void);

// 0x001fa6e0 (actor in ECX, normals in EDX, distances in ESI): the enabled boundary planes (4 at
// actor +0x12a8, 0x18 apart: flag, -, normal xy +8, distance +0x10) copied out. Returns the count.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_GetBoundaryPlanes(const uint8_t* actor, float* normals, float* distances);
extern "C" void Actor_GetBoundaryPlanes_entry(void);
// 0x00200150 (actor in EAX, state in ECX): if a pushed state (+0xa8, count +0xbc) is one of the five
// that `state` blocks (table 0x00830ea8), pops it and everything above it (queued pops merged).
// Returns the actor (EAX unchanged).
uint8_t* XB_REGS2 XB_CDECL Actor_PopBlockingStates(uint8_t* actor, uint32_t state);

// 0x002001f0 (actor in EAX): applies the queued state transitions (+0x38, kind/state pairs, count
// +0xa0): 1 set (Finish the top, Start the new), 2 push (Suspend, depth + 1, Start), 3 pop (Finish,
// depth - 1, Resume; a state it returns is started after the new top's Finish). Then the queue is
// cleared and the state stack mirrored to +0xa4 (count +0xbc). Callbacks: table 0x00830c28.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_ApplyStateTransitions(uint32_t entry, uint8_t* actor, uint32_t edi);
extern "C" void Actor_ApplyStateTransitions_entry(void);

// 0x00205930 (actor in EAX, yaw delta in XMM4, pitch delta in XMM5): turns the actor's angle
// pairs (+0x130/+0x138 into +0x134 too, +0x144/+0x14c into +0x148) and the entity's yaw (+0x154; roll
// and pitch +0x150/+0x158 cleared) through 16-bit angles, then the desired angles.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_RotateOrient(uint8_t* actor, float yaw, float pitch);
extern "C" void Actor_RotateOrient_entry(void);

// 0x001fb8b0 (actor in EBX): ages the move history (ten 2D moves at +0x1398, head +0x1394): from the
// oldest to the newest each is scaled by n/(n+1) (n = 1, 2, ...), the newest not at all; head + 1.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_AdvanceMoveHistory(uint8_t* actor);
extern "C" void Actor_AdvanceMoveHistory_entry(void);

// 0x001fcbf0 (path in ECX, origin in EAX, point in ESI, squared radius in XMM4): whether the point is
// within the radius (xy) of the lookahead segment (+0x39c direction, +0x3b0 length, ending at
// origin + direction * length), the current segment (+0x388, remaining +0x3b8) or any earlier one.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Path_IsPointNearPath(const uint8_t* path, const float* origin, const float* point,
                                                                  float radius_sq);
extern "C" void Path_IsPointNearPath_entry(void);

// State callbacks (actor in ECX, the next state in EDX): 0x00200d90 noncombat Finish (releases the
// claimed node), 0x002014f0 cover Finish (+0x1364 = 0, +0x1368 = level time; the node is kept for
// state 9), 0x00201430 cover NodeClaimRevoked (releases it; a state 1 on top is set anew). Returns 1.
void XB_REGS3 XB_CDECL Actor_Noncombat_Finish(uint32_t, uint32_t, uint8_t* actor);
void XB_REGS3 XB_CDECL Actor_Cover_Finish(uint32_t, uint32_t next, uint8_t* actor);
uint32_t XB_REGS3 XB_CDECL Actor_Cover_NodeClaimRevoked(uint32_t, uint32_t, uint8_t* actor);
// 0x00201d40 (actor in EDX): whether the actor stands on its claimed node (+0x140 set, or within
// 80 units vertically and 2 in xy).
uint32_t XB_REGS2 XB_CDECL Actor_IsAtClaimedNode(uint32_t, const uint8_t* actor);

// 0x001ff030 (weapon in ECX, target in EBX, callee pops 4: actor): 0 when a sentient of the teams
// the actor's team allows (all but table {0, 2, 1, 0, 0}[team]) stands within weapon def +0x724 * 1.2
// (compared with the squared distance, as the title does) of the target, else 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_GrenadeTargetClear(uint32_t weapon, const float* target, const uint8_t* actor);
extern "C" void Actor_GrenadeTargetClear_entry(void);
// 0x001ffac0 (cdecl ent): who gives way when two actors block each other: 5000 for a main
// character (script name after the first ' ' in the list 0x008db468), + 1000 (+0x7df), + 1000 without +0x29c,
// + 600 for sentient state 2, - 1000 (+0x5b), - the sentient's +0x18 truncated.
int32_t XB_CDECL Actor_MainCharacterPriority(const uint8_t* ent);

// 0x001fc750 (path in EAX, point in EDI): steps the path's end (+0x380 count, +0x3cc point, +0x3c8
// remaining length) back from its last point toward `point` while the weighted |cross| sum stays
// below +0x3b4 * 0.1765 and the points stay within 256 units (else +0x3bc |= 0x20), interpolating
// inside the last segment; the sum (* 0.75 without +0x3bc bit 1) is added to +0x3b4 (max 65536).
extern "C" PLATFORM_HIDDEN void XB_CDECL Path_StepBack(uint8_t* path, const float* point);
extern "C" void Path_StepBack_entry(void);

// 0x001fe820 (path in ESI, origin in ECX): the look direction +0x3a8 (2D, normalized): the path
// direction (+0x39c) when the path is short, flagged (+0x3bc bit 0) or its end lies ahead of the point
// A up to 60 units along the lookahead; else toward the path point where the weighted |cross| sum
// walking back from A's segment reaches +0x3b4 (or the path's start).
extern "C" PLATFORM_HIDDEN void XB_CDECL Path_RefreshForwardLookahead(uint8_t* path, const float* origin);
extern "C" void Path_RefreshForwardLookahead_entry(void);

// 0x00202010 (actor in ECX): asks the actors (list 0x00985384, 32) standing within 30 units (xy)
// of this actor's goal (+0x290) but not within 30 of the actor itself to move aside (their +0x1390 =
// our entity, if unset). Returns 1 when none was in the way.
uint32_t XB_REGS3 XB_CDECL Actor_AskActorsToMoveAside(uint32_t, uint32_t, uint8_t* actor);

// 0x002038b0 (node in EAX or 0 = the actor's claimed node, actor in EDX; callee pops 8: threat
// sentient or 0 = the actor's enemy record +0x1310, out script): whether a cover/concealment node
// (types 2-11) covers against the threat: beyond the kind's distance and the threat's yaw from the
// node (asin-series atan2, relative to node +0x50, through a 16-bit angle) outside the uncovered
// range. *script = the kind's cover anim script (0 for other nodes).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_IsCoverValidAgainst(const uint8_t* node, const uint8_t* actor,
                                                                       const uint8_t* threat, uint32_t* script);
extern "C" void Actor_IsCoverValidAgainst_entry(void);

// 0x001ffdb0 (actor in EDI): clears the actor's path (trimmed against the entity's origin). Returns 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_ClearPath(uint8_t* actor);
extern "C" void Actor_ClearPath_entry(void);
// State Start callbacks (actor in ECX; return the next state, 0): pain 0x00200c70 (+0xc0 = level time,
// pain anim script, +0x27c = 4), noncombat 0x00200db0 (timers +0x28/+0x2c/+0x34), anim custom 0x00201990,
// anim scripted 0x002019d0 (sentient +0x5a = 0); all clear the path first.
uint32_t XB_REGS3 XB_CDECL Actor_Pain_Start(uint32_t, uint32_t, uint8_t* actor);
uint32_t XB_REGS3 XB_CDECL Actor_Noncombat_Start(uint32_t, uint32_t, uint8_t* actor);
uint32_t XB_REGS3 XB_CDECL Actor_AnimCustom_Start(uint32_t, uint32_t, uint8_t* actor);
uint32_t XB_REGS3 XB_CDECL Actor_AnimScripted_Start(uint32_t, uint32_t, uint8_t* actor);
// 0x00201280 (other entity in EDX, actor in ECX): touched by the entity it is waiting for (+0x39c
// entity handle): the path is cleared when that entity has +0x23c set.
void XB_REGS3 XB_CDECL Actor_DefaultTouch(uint32_t, const uint8_t* other, uint8_t* actor);
// 0x00204610 (threat in ECX, actor in ESI, callee pops 4: node): +0x820 = the node's cover script if the
// node covers against the threat, else 0. Returns 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_UpdateCoverValidity(const uint8_t* threat, uint8_t* actor, const uint8_t* node);
extern "C" void Actor_UpdateCoverValidity_entry(void);
// 0x00202770 (actor in ESI, state in EDI; not in kFunctions: its CALL starts at byte 4, unhookable): pushes the
// state if allowed (popping what it blocks first);
// returns 1, or 0 when not allowed.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_PushState(uint8_t* actor, uint32_t state);
extern "C" void Actor_PushState_entry(void);

// 0x001fd5a0 (actor in EAX): records this frame's move (+0x75c * 0.1) into the move history: a new slot
// every tenth frame (Actor_AdvanceMoveHistory), else added to the current one.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_RecordMoveHistory(uint8_t* actor);
extern "C" void Actor_RecordMoveHistory_entry(void);
// 0x001ffba0 (actor in EAX, callee pops 4: speed): a new history slot with the move direction blended
// from +0x75c to +0x768 by (speed per second - 95) / 95, normalized when blended.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_RecordMoveHistoryBlended(uint8_t* actor, float speed);
extern "C" void Actor_RecordMoveHistoryBlended_entry(void);
// 0x00202ae0 (actor in ECX): exposed NodeClaimRevoked: releases the node, clears the path and restarts
// the reacquire behaviour when exposed (+0xa4 = 2) in substate 0xd3. Returns 1.
uint32_t XB_REGS3 XB_CDECL Actor_Exposed_NodeClaimRevoked(uint32_t, uint32_t, uint8_t* actor);

// 0x002ba2a0 (sentient in EDX): drops the sentient's node claims (+0x58, +0x5a; a node whose claim count
// +0x26 reaches 0 loses its claimer +8) and clears +4/+8.
void XB_REGS2 XB_CDECL Sentient_ClearNodeClaim(uint32_t, uint8_t* sentient);
// 0x002025c0 (actor in ESI, callee pops 4: argument): one state machine step: queued transitions
// applied, the make-way request (+0x1390) ended when far enough or walking away (+0x27c = 1), then the
// current state's Think (slot 4, argument in EDX). Returns Think's result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_ThinkStateMachine(uint32_t entry, uint8_t* actor, uint32_t arg,
                                                                     uint32_t ebx);
extern "C" void Actor_ThinkStateMachine_entry(void);

// 0x00202f10 (actor in EBX): without a make-way target (+0x1390), picks the first other actor within
// 80 units vertically and 30 in xy - when this actor cowers or stands on its node, only one walking to an
// entity without a sentient - and stores its entity there.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_FindNearbyActor(uint8_t* actor);
extern "C" void Actor_FindNearbyActor_entry(void);
// 0x002f9020 (cdecl entity handle): runs slot 5 of the handle's actor's current state (actor in ECX,
// depth in EDX) with 0x0095f704 = its entity meanwhile.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_CallStateSlot5(uint32_t entry, uint32_t handle, uint32_t ebx, uint32_t esi,
                                                                  uint32_t edi);
extern "C" void Actor_CallStateSlot5_entry(void);

// 0x0002bd50 (node in ESI): a path node record cleared - its four script strings (+8..+0x14) from a null rep,
// floats +0x1c..+0x2c, words +0x30..+0x3a, +0x3c; returns the node.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PathNode_Construct(uint8_t* node);
extern "C" void PathNode_Construct_entry(void);
// 0x001fb7f0 (list in ESI, entity in EBX): removes every 0x18-byte row whose first or second pointer points at the
// entity (the last row moves into its place, count +0x78 drops, the scan restarts).
extern "C" PLATFORM_HIDDEN void XB_CDECL ActorBlockList_RemoveEntity(uint8_t* list, uint32_t ent);
extern "C" void ActorBlockList_RemoveEntity_entry(void);

// 0x001fe370: adds an entity to g_actorMove's touch list (+0x88 count, at most 0x20, +0x8c entries) once.
void XB_CDECL ActorMove_AddTouchEnt(uint32_t ent);
// 0x002b9f90 / 0x002ba010 (handle pointer in EAX; identical bodies): the path node of a 16-bit handle - block
// (handle >> 9) - 1 of g_pathNodeBlocks (+8), node handle & 0x1ff (0x80 bytes at the block's +0x10); null for
// 0, 0xffff or a missing block.
uint8_t* XB_REGS1 XB_CDECL PathNode_FromHandle(const uint16_t* handle);
uint8_t* XB_REGS1 XB_CDECL PathNode_FromHandle2(const uint16_t* handle);
// 0x002b1240 (node in EBX; sentient on the stack): 0 when another sentient's claim (+4) has not expired (+8 vs
// level_time) or a team-4 sentient other than this one stands on it (node number vs +0x50), else 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PathNode_IsAvailableFor(const uint8_t* node, const uint8_t* sentient);
extern "C" void PathNode_IsAvailableFor_entry(void);

}  // namespace game
