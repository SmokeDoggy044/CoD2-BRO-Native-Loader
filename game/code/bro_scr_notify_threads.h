#pragma once

// Compiled level-script waits around notifies, turrets, music and small AI animations
// (levelscript module, .text 0x00181610-0x001838a3). Every one waits (Scr_Wait*, Scr_WaitTill*):
// class thread_switch, kept out of kFunctions until script threads run as coroutines. Names are
// ours (from the bodies). "death" is hash 0x06730ae8; the level handle is word +4 of the level
// script object (0x00922ef8).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00181610: ends on death; saves the sentient float +0x18 (goal radius), sets it to 64, waits
// for 0x011066c4 on the level, restores it.
void XB_CDECL BroScr_SetGoalRadius64UntilNotify(uint32_t ai);
// 0x00181670: ends on 0x455955a2; waits for `other`'s death, then notifies 0x455955a2 on `ent`.
void XB_CDECL BroScr_NotifyWhenOtherDies(uint32_t ent, uint32_t other);
// 0x00181700: forever: level +0x190 = 0, waits for "trigger" (0xe2a210cf) on `trigger`, level
// +0x1c8 = `name`; while the player touches the trigger: level +0x190 = trigger, wait 1 s.
// Never returns (`name` is never released).
void XB_CDECL BroScr_TrackTriggerTouch(uint32_t trigger, ScriptString name);
// 0x001817b0: reads `ent`'s origin (unused), waits for 0xad76f051 on it; then on `turret`:
// notifies 0xcaa6bec3, sets mode 0xad76f051 and targets the player.
void XB_CDECL BroScr_TurretAutoAiOnNotify(uint32_t ent, uint32_t turret);
// 0x001818c0 (symbol file: a second BroScr_Mg42ShootLoop_2; ours BroScr_Mg42ShootSoundLoop):
// ends on 0xeb04625e; forever: fire (slot 0x00922580, 0), wait 0.06 s, "mg42_shoot" sound,
// wait 0.06 s.
void XB_CDECL BroScr_Mg42ShootSoundLoop(uint32_t turret);
// 0x00181960: for time > 0: ends on "turretstatechange", waits `time`, notifies it.
void XB_CDECL BroScr_TurretStateChangeWait(uint32_t turret, float time);
// 0x00181b70: ends on death; AI flags (1, 1, 0) until 0x0032304c, then (0, 0, 0).
void XB_CDECL BroScr_SetAIFlagsUntilNotify(uint32_t ai);
// 0x00181bc0: waits for `ent`'s death, then notifies "fallback_reached_goal<index>" on the level.
void XB_CDECL BroScr_NotifyFallbackReachedGoal(uint32_t unused, uint32_t ent, int32_t index);
// 0x00181ce0: Scr_Wait(0.05).
void XB_CDECL BroScr_WaitFrame();
// 0x00181cf0: the friendly-wave trigger callback: clears its trigger callback var, notifies
// 0xaf6b65f9 on the level, level +0x1d0 = the trigger when its targetname is "friendly_wave"
// (else the undefined entity), waits 1 s, sets the callback again.
void XB_CDECL Bro__spawner_gmi__friendly_wave_callback(uint32_t trigger);
// 0x00181df0: ends on `turret`'s 0x7b91738c; waits for `ai`'s death, then notifies it.
void XB_CDECL BroScr_NotifyMg42FinishedOnDeath(uint32_t unused, uint32_t ai, uint32_t turret);
// 0x00181e30: ends on `turret`'s 0x7b91738c; `user` byte +0x1374 = 1 and hint
// "CGAME_USEAIONMG42" until "trigger" on it, then byte 0, an empty hint, leave turret (slot
// 0x00922730), and 0x7b91738c notified on `turret`.
void XB_CDECL BroScr_UseAiOnMg42Hint(uint32_t user, uint32_t turret);
// 0x00181f20: ends on "friendly_finished_using_mg42"; waits for "trigger" on `trigger`, notifies it.
void XB_CDECL BroScr_NotifyFinishedUsingMg42(uint32_t trigger, uint32_t ent);
// 0x001821f0: ends on "death"; deletes `ent` on the `notify` string; releases it.
void XB_CDECL BroScr_DeleteOnNotify(uint32_t ent, ScriptString notify);
// 0x001822e0: waits for "death", then (r_texturebits not "16") shell shock "death" for 3 s.
void XB_CDECL BroScr_ShellShockUntilDeath(uint32_t ent);
// 0x001823a0: a script_origin at `origin` plays `sound` (notify "sounddone" 0x9a73c1c0), waits
// for it and 0.5 s, deletes it; releases `sound`.
void XB_CDECL BroScr_PlaySoundOnTempOrigin_4(uint32_t unused, ScriptString sound, ScrVec3 origin);
// 0x00182490: ends on "death"; waits for "stop player feels important", then sets the health.
void XB_CDECL BroScr_PlayerFeelsImportant(uint32_t player, uint32_t health);
// 0x001824d0: a "sound_blend" entity at `ent`'s origin, its forward vector, the end time; then
// forever reads scr_debug_blender (the debug draw was compiled out) and waits 0.05 s.
void XB_CDECL BroScr_SoundBlendDebug(uint32_t ent, int32_t seconds);
// 0x00182640: Scr_Wait(0) forever.
void XB_CDECL BroScr_WaitForever_2();
// 0x00182650: music stop, wait 0.2 s, music play `track`.
void XB_CDECL BroScr_MusicStopThenPlay(const ScriptString* track);
// 0x00182670: music stop, slot 0x00922314(1.0), wait 0.1 s, play `track`; level +0x150 =
// 0x2d7828fe, +0xcc = 0, +0x68 = 1; releases `track`.
void XB_CDECL BroScr_PlayLevelMusic(ScriptString track);
// 0x00182700: for time > 0: ends on 0x07b4e7f7 (ai) and 0xeb04625e (turret), waits, notifies
// 0xeb04625e on the turret.
void XB_CDECL BroScr_TurretStateChangeAfterTime(uint32_t ai, float time, uint32_t turret);
// 0x00182750 / 0x00182830: ends (ai: death, 0x07b4e7f7; turret: 0xeb04625e, and death in the
// first), the turret idle knob (0x008cf684), then fires every 0.1 s forever.
void XB_CDECL BroScr_TurretIdleAnimLoop(uint32_t ai, uint32_t turret);
void XB_CDECL BroScr_TurretIdleAnimLoop_2(uint32_t ai, uint32_t turret);
// 0x001828d0: ends on both deaths and 0x1e4aecac; waits for 0x259b8110 on `ent`, notifies 0x1e4aecac.
void XB_CDECL BroScr_RelayNotifyWhileBothAlive(uint32_t ent, uint32_t other);
// 0x00183170: reads the actor string +0x1384 (unused), plays `anim` over the body, notifies
// 0x281d9462, waits `time`; releases `name`.
void XB_CDECL BroScr_CornerPlayAnimForTime(uint32_t actor, float time, uint32_t anim, ScriptString name);
// 0x00183290 / 0x001832d0: the flagged (0xde61361c) cover-crouch idle (0x008ceec8 / 0x008ceed4), wait 1 s.
void XB_CDECL BroScr_CoverCrouchPlayIdle_1(uint32_t actor);
void XB_CDECL BroScr_CoverCrouchPlayIdle_2(uint32_t actor);
// 0x001833d0 / 0x00183430: the right / left corner stand pose (crouch where standing is not
// allowed) with look_straight, then waits `time`.
void XB_CDECL BroScr_StandAnimForTime(uint32_t actor, float time);
void XB_CDECL BroScr_StandAnimForTime_2(uint32_t actor, float time);
// 0x001834a0: a random roll (negated for dir.x > 0) and a gravity throw along `dir`, waits 10 s,
// deletes `ent`.
void XB_CDECL BroScr_RandomTumble(uint32_t ent, ScrVec3 dir);
// 0x00183790: Scr_WaitFrames(0); releases `unused`.
void XB_CDECL BroScr_WaitFrameStrArg(uint32_t, uint32_t, ScriptString unused);
// 0x00183810: ends on 0x07b4e7f7, death and `end`; waits for `wait`, notifies `notify`.
void XB_CDECL BroScr_NotifyOnNotify(uint32_t ent, uint32_t wait, uint32_t notify, uint32_t end);
// 0x00183860: ends on 0x07b4e7f7, death and `end`; waits `time`, notifies `notify`.
void XB_CDECL BroScr_NotifyAfterTime(uint32_t ent, float time, uint32_t notify, uint32_t end);

}  // namespace game
