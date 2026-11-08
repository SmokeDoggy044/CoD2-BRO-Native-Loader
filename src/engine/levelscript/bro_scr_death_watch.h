#pragma once

// Compiled level-script thread bodies (levelscript module) that wait for a death, a notify or
// a timer: physics launches of the dead, the flak panzer's loop sounds, objective trackers, the
// dead quote. They suspend the script thread, so they are not in kFunctions (class thread_switch
// in docs/research/levelscript_classes.csv); testable once script threads are coroutines.

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00121cc0: launches the dead `ent` (GScr_PhysicsLaunch) at `force` (or (0, 0, 0) when all three
// lanes are undefined) along the normalised `attacker - ent` with |z|, after reading two death
// entity vars (results unused); after 0.1 s does health + 50 damage from its origin.
void XB_CDECL BroScr_DeathPhysicsLaunch(uint32_t ent, uint32_t attacker, uint32_t launch_arg, ScrVec3 force);
// 0x00121ea0: deletes `ent` after 5 s.
void XB_CDECL BroScr_DeleteAfter5s(uint32_t ent);
// 0x00122040: ends on the level's "mine death"; when the player (level +0x7f8) dies and the level's
// flag byte +0xf0 is clear, sets ui_deadquote to one of 42 quotes at random.
void XB_CDECL BroScr_SetDeadQuote();
// 0x00122240: objective `index` "active" with `text`, current; updated with the count left on each
// 0x67979261 notify on the "<name>_manager" entity; then the level's flag byte +0x2d0 is set and
// "objective<index>_tracker_complete" queued on the level. Ends on the level's `endon` when set.
// Releases name, text and endon.
void XB_CDECL BroScr_ObjectiveTrackerManager(ScriptString name, int32_t index, ScriptString text, int32_t count,
                                             ScriptString endon);
// 0x00122470: after the level's "flak died with flaknum <flaknum>", queues "flakpanzer_loopoff" and
// "flakpanzer_loop_fastoff" on ent.
void XB_CDECL BroScr_FlakPanzerStopLoopSounds(uint32_t ent, int32_t flaknum);
// 0x00122540: ends on "broken"; an earthquake (0.05, 2 s, radius 2500) at ent's origin every second.
void XB_CDECL BroScr_EarthquakeUntilBroken(uint32_t ent);
// 0x001225b0: sets ent's health to 1; on its "death" queues "end_sequence" on `self`.
void XB_CDECL BroScr_EndSequenceOnDeath(uint32_t self, uint32_t ent);
// 0x00122600: ends on "death"; teleports the actor to `pos` (goal there, radius 200); when it has a
// target, after the level's "flak died with flaknum <flaknum>" its goal becomes the target node
// (radius: the node's, or 128).
void XB_CDECL BroScr_FlakWaitDeath(uint32_t ent, ScrVec3 pos, int32_t flaknum);
// 0x001227d0: after the level's "flak died with flaknum <flaknum>" (and a 2 x 0.05 s countdown)
// deletes ent. Builds "<name>_null" without using it. Releases `name`.
void XB_CDECL BroScr_FlakWaitDeath_2(uint32_t ent, ScriptString name, int32_t flaknum);
// 0x00122910: ends on the level's "flak died with flaknum <flaknum>"; switches ent's loop sound to
// the fast one for 2.08 s (loopoff, loop_faston, then loop_fastoff, loopon).
void XB_CDECL BroScr_FlakPanzerLoopSounds(uint32_t ent, int32_t flaknum);
// 0x00122a60: restores ent's starting health on each "damage" while it is alive.
void XB_CDECL BroScr_RestoreHealthOnDamage(uint32_t ent);
// 0x00122b50: throws the stone gib away from `from` (150..349 along the direction, z rand(200) + 200),
// rolling +-(1500..4000) over 5 s; deletes it after 6 s.
void XB_CDECL BroScr_StoneGibLaunch(uint32_t ent, ScrVec3 from);
// 0x00123250: sums ent's movement every 0.15 s; while the vehicle's speed (slot 0x00922b38, ST0) is over
// 1, takes 24 units off for each 24 travelled (its forward vector and a second speed read unused).
void XB_CDECL BroScr_TrackWheelOrigin(uint32_t vehicle, uint32_t ent);
// 0x001233d0 / 0x001233e0: Scr_Wait(0.15) / Scr_Wait(0).
void XB_CDECL BroScr_WaitPoint15();
void XB_CDECL BroScr_WaitZero();
// 0x001235f0: ends on ent's death and the level's `endon` (when set), then waits forever.
void XB_CDECL BroScr_IdleUntilEnd(uint32_t ent, ScrVec3 unused, ScriptString endon);
// 0x00123670 / 0x00123900: turns the player to (pitch, yaw, roll) over `time` seconds, frame by frame
// (player flag 0x58c set meanwhile, cleared after when the low byte of `release` is set); pitch is
// first brought into [-90, 90], the yaw delta into [-180, 180]. The second eases in and out.
void XB_CDECL BroScr_LerpPlayerAngles(uint32_t player, float pitch, float yaw, float roll, float time, uint32_t release);
void XB_CDECL BroScr_LerpPlayerAngles_2(uint32_t player, float pitch, float yaw, float roll, float time,
                                        uint32_t release);
// 0x00123c00: plays "window_break" (or ent's script_noteworthy as the sound) on ent's target window,
// sets the window's entity flag 0x400 and clears it on the window's own target; after 1 s deletes
// the window and ent.
void XB_CDECL Bro__window_gmi__new_window_break(uint32_t ent);
// 0x00124d40: sets key 0xddfa2c42 on the level's script object for 8 s.
void XB_CDECL BroScr_ShowContextTextBriefly();
// 0x001250a0: deletes the drone after the level's delay (+0x544, 5 when undefined) and 10 s (both
// scaled down beyond 512 units of the player, at least 1 s each); counts it off (level +0x508).
void XB_CDECL BroScr_DroneDeleteLater(uint32_t drone);

}  // namespace game
