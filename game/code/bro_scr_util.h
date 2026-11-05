#pragma once

// Shared compiled level-script helpers in .text (levelscript module): small utilities the level
// sections call, stubs whose script bodies were empty (they only release their by-value string and
// array arguments), constructors of script parameter blocks. Names are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

// Functions whose arguments are mostly unused words: an entry hands the C version the address of
// the stack argument block (args[0] = the first argument). Result in EAX.
#define BRO_SCR_ARGS_ENTRY(name)                                           \
    PLATFORM_ASM_ENTRY(name##_entry, "leal 4(%esp), %eax\n"               \
                                     "pushl %eax\n" PLATFORM_ASM_CALL(name) \
                                     "addl $4, %esp\n"                     \
                                     "ret")
#define BRO_SCR_ARGS_DECL(name)                                                   \
    extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL name(uint32_t* args); \
    extern "C" void name##_entry(void);

namespace game {

// 0x0011a880: `out` = `text` followed by "%f,%f,%f" of `v` (0x00458ce0 builds it into a temporary).
ScriptString* XB_CDECL ScriptString_FromCStringPlusVec(ScriptString* out, const char* text, ScrVec3 v);
// 0x0011bdc0 (thiscall, callee pops 4): the hash map stored under key 0x4fcdc42e in the key table
// at self - 0x14 (0x0052f900: a copy, or a new empty map when the key is missing). Returns out.
uint8_t* XB_THISCALL BroScr_GetMap_h4fcdc42e(uint8_t* self, uint8_t* out);
// 0x0011f180: fills the import block's ScriptKeyTable callbacks (+4 FromPairs, +8 Delete,
// +0xc 0x0011e800, +0x10 Clone); returns the block.
uint32_t* XB_CDECL BroRuntime_SetImportCallbacks(uint32_t* block);
// 0x0011bac0 (thiscall): a spawner-group parameter block (0xe0 bytes) set to the script runtime's
// "undefined" values (float, vector lane, int, handle), four null strings and two zero words.
uint8_t* XB_THISCALL BroScr_SpawnerGroupParams_Init(uint8_t* self);
// 0x001201c0 (dead): *out = whether all three words of the vector are -1; returns out.
uint8_t* XB_CDECL BroScr_IsVecAllMinusOne(uint8_t* out, const int32_t* v);
// 0x001201f0 (dead): ends on notify 0xf9ed2681, then GScr_MoveTo(ent, &pos, time, 0, 0).
void XB_CDECL BroScr_MoveToThread(uint32_t ent, ScrVec3 pos, uint32_t time);
// 0x00120220: ends on notify 0xf9ed2681; rotates `ent` within `time` to the yaw of `to - from`
// (Scr_VectorToAngles), keeping its current pitch and roll (two ScrEnt_GetAngles reads).
void XB_CDECL BroScr_RotateToFaceDirection(uint32_t ent, ScrVec3 from, ScrVec3 to, uint32_t time);
// 0x00120350 / 0x00120360 / 0x00120370 / 0x001203d0 (args[4]), 0x00120460 (args[10]), 0x00120480
// (args[12]): empty script bodies that release their one string argument (tail jump).
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg_4)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg_5)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg_6)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg_2)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncStrArg_3)
// 0x00120380 (args[4], args[8]) / 0x001203e0 (args[4], args[11]): two strings released.
BRO_SCR_ARGS_DECL(BroScr_EmptyFunc2StrArgs)
BRO_SCR_ARGS_DECL(BroScr_EmptyFunc2StrArgs_2)
// 0x00136020 (string args[4]) / 0x00136070 (args[7]) / 0x001360d0 (args[10]): the array argument's
// data (args[1]) freed when set, then the string released. 0x001360c0: the array only.
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncArrStrArgs)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncArrStrArgs_2)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncArrStrArgs_3)
BRO_SCR_ARGS_DECL(BroScr_EmptyFuncArrArg)
// 0x001204a0: flak parameters by type into `out` (words 0, 1, 2, 5, 6 from .data 0x008cd41c..):
// "flaktank" sets its set, then "flakairlow" overrides words 2 and 5, any other type (also
// "flaktank") the defaults. Releases `type`.
void XB_CDECL BroScr_IsFlakTarget(uint32_t unused, ScriptString type, uint32_t* out);
// 0x00120580: GScr_Vehicle_MakeUnusable when the low byte of `unusable` is set, else MakeUsable.
void XB_CDECL BroScr_VehicleSetUnusable(uint32_t vehicle, uint32_t unusable);
// 0x00120b00: deletes `ent` when the cvar g_gameskill is "3".
void XB_CDECL BroScr_GetGameSkill(uint32_t ent);
// 0x00120b80: the level's default cvars (eleven Scr_SetCvar, SetHudVisible(1), one more), then
// six cvars (0x0082c0e0, "skipto", "debug", "chain", "fallback", "debug_mortars") reset when
// their value equals "" (0x0082dfeb).
void XB_CDECL BroScr_InitLevelCvars();

// 0x001213e0 (dead): out = a - b; returns out.
uint32_t* XB_CDECL BroScr_VectorSubtract(uint32_t* out, ScrVec3 a, ScrVec3 b);
// 0x001233f0 / 0x00123430 (dead): out = (0, y, 0) / (x, y, 0) of (x, y, z); returns out.
uint32_t* XB_CDECL BroScr_MakeVec0Y0(uint32_t* out, ScrVec3 v);
uint32_t* XB_CDECL BroScr_MakeVecXY0(uint32_t* out, ScrVec3 v);
// Ground traces (GScr_LocationalTraceSurface from `start` down to `end`; out = the hit position,
// the trace result's word 1-3): 0x00121420 from start to (x, y, (z + 32) - 5000), no entity skipped
// (the second argument is unused); 0x00121b30 from (x, y, z + 1000) to (x, y, (z + 1000) - 2000),
// skipping the entity at 0x00922e84; 0x00124f70 from start to (x, y, (z + 32) - 500), skipping `skip`.
uint32_t* XB_CDECL BroScr_TraceGroundPos(uint32_t* out, uint32_t unused, ScrVec3 start);
uint32_t* XB_CDECL BroScr_TraceGroundPos_2(uint32_t* out, ScrVec3 start);
uint32_t* XB_CDECL BroScr_TraceGroundPos_3(uint32_t* out, uint32_t skip, ScrVec3 start);
// 0x00128360: ends on notify 0x06730ae8, then moves `ent` to the ground below its origin
// (BroScr_TraceGroundPos).
void XB_CDECL BroScr_DropToGround(uint32_t ent);
// 0x00121270: the American victory/defeat backdrops and campaign cvars, and the player's US hands
// view model (player handle 0x00922eec).
void XB_CDECL BroScr_SetAmericanScreens();
// 0x00121ed0: "ui_victoryquote" = one of 38 quote names (Scr_RandomInt(38)).
void XB_CDECL BroScr_SetRandomVictoryQuote();
// 0x00122440 / 0x00122a20: Scr_QueueNotify of "explode" / of "flakpanzer_loopoff", then
// "flakpanzer_loop_faston" on `ent`.
void XB_CDECL BroScr_NotifyExplode(uint32_t ent);
void XB_CDECL BroScr_FlakPanzerLoopSound(uint32_t ent);
// 0x00122f50 (dead): the tank debug cvars default to "off" when unset; "debug_tankall" (not
// "off") turns stundraw, tankque and tanksamples on and itself off; "debug_tanknone" (not "off")
// turns those and itself off.
void XB_CDECL BroScr_InitTankDebugCvars();
// 0x00123470: the entity's script_team field: "friendly"/"enemy" become "allies"/"axis",
// "axis"/"allies" stay; when not set (or anything else), `team` if it is "axis", "allies" or
// "neutral". Releases `team`.
void XB_CDECL BroScr_NormalizeTeamName(uint32_t ent, ScriptString team);
// 0x00123d70: launches a wood gib away from `from`: velocity = normalize(origin - from) *
// (150 + RandomInt(200)) with z = 200 + RandomFloat(200); rolls by 1500 + RandomFloat(2500)
// (negated when x > 0); GScr_MoverMoveGravity for 12 s.
void XB_CDECL BroScr_WoodGibLaunch(uint32_t ent, ScrVec3 from);
// 0x00123f00: names the gib (`name`), moves it to `pos` in 0.2 s and turns its yaw by
// RandomFloat(30) - 15 in 0.2 s (Scr_RandomInt(10) picks between two identical calls).
void XB_CDECL BroScr_WoodGibInit(uint32_t ent, ScrVec3 pos, ScriptString name);
// 0x001286f0 (dead): ScrEnt_SetHealth(ent, health), 600 when health is the undefined int (tail jump).
void XB_CDECL BroScr_SetHealthOrDefault(uint32_t ent, uint32_t health);
// 0x00125060 (dead): deletes a valid `ent` and counts down the word at +0x508 of the block
// that 0x00922ee0 points to.
void XB_CDECL BroScr_DeleteAndCountDown(uint32_t ent);

// Inlined entity field access (the ScrField_* bodies the compiled script repeats): the entity's
// script object (or the shared empty table 0x00922e94), the value word for `hash`, added when
// missing as the undefined int (ScriptKeyTable_Set) / as 0 for an entity-handle field
// (ScriptKeyTable_SetValue). Not title functions.
uint32_t* BroScr_FieldSlot(uint32_t ent, uint32_t hash);
uint32_t* BroScr_FieldHandleSlot(uint32_t ent, uint32_t hash);

// 0x00124cf0: BroScr_LookAtEntity(ent, target, time, 0x06279a34, 0, 0) with time = `ms` as a
// float (10000 when undefined).
void XB_CDECL BroScr_AnimNotetrack_Alert(uint32_t ent, uint32_t target, int32_t ms);
// 0x0012f1c0 / 0x0012f250: when the attached entity (handle field 0x4225d5c5) is set: deletes it,
// ScrActor_SetInt_1378(ent, 1) and BroScr_PutGunInHand(ent, 0x003798b4 / 0x07151174).
void XB_CDECL BroScr_AnimNotetrack_DeleteAttached(uint32_t ent);
void XB_CDECL BroScr_AnimNotetrack_DeleteAttached_2(uint32_t ent);
// 0x0012f840: ends on "death"; field 0xa027aabf = 1, health 600.
void XB_CDECL BroScr_TankShellReadySetHealth(uint32_t ent);
// 0x0012fa00: string field 0x46861906 = the empty string (0x00922e90), handle field 0x0d60fb3d =
// `target`.
void XB_CDECL BroScr_SetTargetsGroupAndEnt(uint32_t ent, uint32_t target);
// 0x001319d0 / 0x00132020 / 0x00134300: plays the entity's sound (string field 0x22282ca0) at
// its origin, then starts the thread 0x00121540 / 0x001217b0 / 0x00122dc0 with (ent, "Plane_crash",
// origin) (Scr_StartThreadFunctorN).
void XB_CDECL BroScr_PlaneCrashSound(uint32_t ent);
void XB_CDECL BroScr_PlaneCrashSound_2(uint32_t ent);
void XB_CDECL BroScr_PlaneCrashSound_3(uint32_t ent);
// 0x00134400: deletes the left/right turrets (handle fields 0xef6522f9 / 0xeead8089) when set,
// then sets both fields to 0. 0x00131ad0 / 0x00132120: the same, the fields set to the word at
// 0x00922e84.
void XB_CDECL BroScr_DeleteVehicleTurrets(uint32_t ent);
void XB_CDECL BroScr_DeleteVehicleTurrets_2(uint32_t ent);
void XB_CDECL BroScr_DeleteVehicleTurrets_3(uint32_t ent);
// 0x00132410 (dead): the slot 0x00922b8c setter with field 0x56aed3da, then the origin from the
// vector field 0xcc045c84.
void XB_CDECL BroScr_RestoreSpawnerSavedState(uint32_t ent);
// 0x00132830 (dead): handle field 0xd952b5cf = `target`, field 0x5cd3d8b8 = max(count, 0).
void XB_CDECL BroScr_SetFollowEnt(uint32_t ent, uint32_t target, int32_t count);
// 0x001328c0 (dead): field 0x96f9883f = `distance` (0 when undefined), raised to field
// 0xea4a313a - 1 when it is smaller (both re-read through the table).
void XB_CDECL BroScr_SetLagDistance(uint32_t ent, int32_t distance);

// 0x00128ff0: a breakable window: the trigger's target (the window) and the window's target (the
// broken pane) by targetname (GScr_GetEntArray with no buffer); `ent` is deleted when either is
// missing. Else flags 0x40000 on `ent`, 0x80000 on and 0x400 off the window, 0x400 on the pane
// (made non-solid), and `ent`'s var 0xe2a210cf = hash of "_window_gmi::new_window_break".
void XB_CDECL BroScr_WindowBreakSetup(uint32_t ent);

}  // namespace game
