#pragma once

// Compiled level-script waits around notifies, turrets, music and small AI animations
// (levelscript module, .text 0x00181610-0x0018c700). Every one waits (Scr_Wait*, Scr_WaitTill*):
// class thread_switch, kept out of kFunctions until script threads run as coroutines. Names are
// ours (from the bodies). "death" is hash 0x06730ae8; the level handle is word +4 of the level
// script object (0x00922ef8).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// An anim command passed by value (13 words): w[0] the kind (1 SetAnim, 2 SetAnimKnobAll,
// 3 SetFlaggedAnimKnobAll, 4 advance and teleport), w[1..6] anim arguments, w[7..9] an origin,
// w[10..12] angles.
struct AnimCommand {
    uint32_t w[13];
};

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

// --- 0x00183930-0x0018c700 ---

// 0x00183930: the compiled test is inverted: for time NOT above 0 (or NaN): ends on 0x07b4e7f7
// (ai) and 0xeb04625e (turret), waits a frame (WaitFrames(0)) and `time`, notifies 0xeb04625e.
void XB_CDECL BroScr_TurretStateChangeAfterTime_2(uint32_t ai, float time, uint32_t turret);
// 0x001839a0: ends (ai 0x07b4e7f7, turret 0xeb04625e), knob 0x008cf7b0 on the turret, then the
// mg42 fire/sound loop (as 0x001818c0). 0x00183b30 / 0x00183c20: ends on ai death and 0x07b4e7f7,
// turret 0xeb04625e (and death in _2), the same loop.
void XB_CDECL BroScr_Mg42ShootLoop(uint32_t ai, uint32_t turret);
void XB_CDECL BroScr_Mg42ShootLoop_2(uint32_t ai, uint32_t turret);
void XB_CDECL BroScr_Mg42ShootLoop_3(uint32_t ai, uint32_t turret);
// 0x00183ad0: BroScr_TurretStateChangeAfterTime plus an end on the turret's death.
void XB_CDECL BroScr_TurretStateChangeAfterTime_3(uint32_t ai, float time, uint32_t turret);
// 0x00183d70: advance the anim time, teleport to the command's origin/angles, wait 0.05 s.
void XB_CDECL BroScr_ActorAdvanceAnimAndTeleport(uint32_t actor, AnimCommand command);
// 0x00183db0: runs the command by kind (1-4; others nothing). Kind 4 waits.
void XB_CDECL BroScr_ApplyAnimCommand(uint32_t actor, AnimCommand command);
// 0x00183ea0 / 0x00183ef0: copies of BroScr_NotifyOnNotify / BroScr_NotifyAfterTime.
void XB_CDECL BroScr_NotifyOnNotify_2(uint32_t ent, uint32_t wait, uint32_t notify, uint32_t end);
void XB_CDECL BroScr_NotifyAfterTime_2(uint32_t ent, float time, uint32_t notify, uint32_t end);
// 0x00183f50: ends on 0x07b4e7f7 and `end`; forever "misc_step1" / "misc_step2" every 0.5 s.
void XB_CDECL BroScr_MiscStepSounds(uint32_t ent, uint32_t end);
// 0x00184020: ends on 0x9657e884; waits for 0x07b4e7f7.
void XB_CDECL BroScr_WaitKillAnimScript(uint32_t ent);
// 0x00184090 / 0x0018c090: waits 300 s / 0.05 s, deletes `ent`.
void XB_CDECL BroScr_DeleteAfter300(uint32_t ent);
void XB_CDECL BroScr_DeleteAfterFrame(uint32_t ent);
// 0x001840b0: ends on 0x07b4e7f7, death, 0x6660fd4f; prone aim weights (45 up / 45 down) every 0.1 s.
void XB_CDECL BroScr_ProneAimWeightsLoop(uint32_t actor);
// 0x00184120-0x001841e0: copies of BroScr_CoverCrouchPlayIdle_1/_2 (idles 0x008ceec8, 0x008ceed4).
void XB_CDECL BroScr_CoverCrouchPlayIdle_3(uint32_t actor);
void XB_CDECL BroScr_CoverCrouchPlayIdle_4(uint32_t actor);
void XB_CDECL BroScr_CoverCrouchPlayIdle_5(uint32_t actor);
void XB_CDECL BroScr_CoverCrouchPlayIdle_6(uint32_t actor);
// 0x00184b30: ends on death and `end`; waits `time`, notifies `notify`.
void XB_CDECL BroScr_NotifyAfterTime_3(uint32_t ent, uint32_t notify, uint32_t end, float time);
// 0x00184b90: ends on 0x07b4e7f7 and 0x47e458b4; reads the eye position every 0.05 s (unused).
void XB_CDECL BroScr_GetEyeLoop(uint32_t ent);
// 0x0018ba10: waits for the `notify` string on `ent`, deletes `first` and `second` when valid.
void XB_CDECL BroScr_DeleteEntsOnNotify(uint32_t unused, uint32_t ent, ScriptString notify, uint32_t first,
                                        uint32_t second);
// 0x0018bf00: with cvar debug "1", waits 3 s in 0.05 s steps; releases `unused`.
void XB_CDECL BroScr_DebugCvarWait(uint32_t, ScriptString unused);
// 0x0018bfc0: ends on 0xed4ff1b2 and 0x6a48b933; waits forever (the debug draw compiled out).
void XB_CDECL BroScr_FallbackDebugLoop(uint32_t ent);
// 0x0018bff0: on 0x0032304c, goal radius (+0x18) = the node's radius when the node is valid and
// its radius above 0, else 512.
void XB_CDECL BroScr_SetGoalRadiusFromNodeOnNotify(uint32_t ai, uint32_t node);
// 0x0018c130: ends on `ent`'s death; on `other`'s death deletes `ent` when valid.
void XB_CDECL BroScr_DeleteOnOtherDeath(uint32_t other, uint32_t ent);
// 0x0018c190: the music trigger callback (level +0xcc busy, +0x68 enabled, +0x150 current):
// ambient by the trigger's script_noteworthy, else the default track 0x2d7828fe; 0.25 s later
// the callback var is set again.
void XB_CDECL Bro__util__Switch_Music(uint32_t trigger);
// 0x0018c320: the same for an ambient name ("outside" or none: the default); releases `name`.
void XB_CDECL BroScr_AmbientPlayOutside(ScriptString name);
// 0x0018c4e0 / 0x0018c5a0: the gunner idle (0x008cf0d8) / 30cal fire knob on the AI, then
// BroScr_TurretIdleAnimLoop / _2 (never return).
void XB_CDECL BroScr_TurretIdleAnimStart(uint32_t ai, uint32_t turret);
void XB_CDECL BroScr_Turret30calFire(uint32_t ai, uint32_t turret);
// 0x0018c6a0: ("time must be > 0" built and dropped) ends on `notify`, 0x07b4e7f7, death; waits
// `time`, notifies `notify`.
void XB_CDECL BroScr_NotifyAfterTimeChecked(uint32_t ent, uint32_t notify, float time);

// --- 0x0018da20-0x0019a99a ---

// 0x0018da20 / 0x0018dad0: the prone mg42 fire knob on the AI (_2 first ends on 0x07b4e7f7),
// then BroScr_Mg42ShootLoop / _2 (never return).
void XB_CDECL BroScr_Mg42AnimThenShootLoop(uint32_t ai, uint32_t turret);
void XB_CDECL BroScr_Mg42AnimThenShootLoop_2(uint32_t ai, uint32_t turret);
// 0x0018dbc0: for a standing/crouching actor (or `force`): direction blend from int +0x230, the
// minor-pain front/back/left/right set by hit location (+0x240; some locations none, unknown
// ones assert), weighted by damage (+0x22c: 1 above 200, else (damage + 50) * 0.004), played
// 0.05 s, cleared over 0.2 s / weight.
void XB_CDECL BroScr_PainAnimByHitLocation(uint32_t actor, uint8_t force);
// 0x00199120 / 0x00199200: ends on death; a BroScr_NotifyWhenOtherDies thread (target, ai);
// runs to the target's origin ignoring all (AI flags 1,1,1) until the goal, then flags 0,0,0,
// goal = the node slot 0x009226ec(ai, 0x008cd76c, 1 / 0) gives (-1 -> 0); notifies 0x455955a2.
void XB_CDECL BroScr_ActorRunToEntIgnoringAll(uint32_t ai, uint32_t target);
void XB_CDECL BroScr_ActorRunToEntIgnoringAll_2(uint32_t ai, uint32_t target);
// 0x00199940: unless cvar mg42 is "off": key 0xc51219a9 = mg, then keeps the mg at `ent`'s origin.
void XB_CDECL BroScr_MgFollowEntOrigin(uint32_t mg, uint32_t unused, uint32_t ent);
// 0x0019a150: forever int key 0x070b8a34 = 0 on 0xd50105fc, = 1 on 0xccd105e5.
void XB_CDECL BroScr_TrackStartFlagLoop(uint32_t ent);
// 0x0019a400: a free mg (key 0x7b1e85ba 0) is taken by `ai` (used_an_mg42) until its death,
// then "get new user" is notified on the mg.
void XB_CDECL BroScr_MgWaitNewUser(uint32_t mg, uint32_t ai);
// 0x0019a500: at the goal: int +0x1308 and float +0x7e4 back from keys 0xac91e5fb / 0x62271501,
// notifies 0xed4ff1b2 and 0x6a48b933.
void XB_CDECL BroScr_FallbackOnGoalRestore(uint32_t ai);
// 0x0019a8b0 / 0x0019a930: ends on ("fallback",) "fallback_clear_goal", "fallback_clear_death";
// forever reads key 0xa33a251d (unused) every 0.05 s.
void XB_CDECL BroScr_FallbackClearGoalWait(uint32_t ai);
void XB_CDECL BroScr_FallbackClearGoalWait_2(uint32_t ai);
// 0x00199630: notifies/ends on "stop random_spread" (and "StopFiring"), aims the turret at
// `target`; forever moves the target to the turret's key 0xcf1e52a5 entity (see the C) plus a
// random offset (+-20 x/y, 20-60..20 z) every 0.2 s.
void XB_CDECL BroScr_TurretRandomSpread(uint32_t turret, uint32_t target);
// 0x0019a200: after 0.1 s, a live actor gets the stand idle (crouch where standing is not
// allowed, prone where neither), idleset 0x61 (not prone), pose +0x184; 0.2 s later "end" on
// 0x2c6a01b1 and 0x8d403617.
void XB_CDECL BroScr_SetIdlePoseByAllowedStance(uint32_t actor);

}  // namespace game
