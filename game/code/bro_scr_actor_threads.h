#pragma once

// Compiled level-script actor threads (levelscript module, .text 0x0019f090-0x001b9a00): look-at
// ends, MG42 gunner recovery / pain, aim blending, cover idles, wounded walk, the anim-script turret
// entries. Every one waits (Scr_Wait*, Scr_WaitTill*, a note-track wait, Scr_CallThread*): class
// thread_switch, kept out of kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0019f090: ends on death; waits for 0x247afc7f, then BroScr_StopLookingAt.
void XB_CDECL BroScr_StopLookingAtOnNotify(uint32_t ent);
// 0x0019f320 / 0x0019f6f0 (_2 ends on 0x07b4e7f7 first): a BroScr_TurretSetProneRecoverAnim /
// BroScr_EmptyFunc_60 call thread (ent, a2), actor int +0x1370 = the prone MG42 recover anim, that
// anim as flagged knob 0x54e3b2b6 (1, 0.2, 1, restart) and a wait for it.
void XB_CDECL BroScr_TurretProneMG42Recover(uint32_t ent, uint32_t a2);
void XB_CDECL BroScr_TurretProneMG42Recover_2(uint32_t ent, uint32_t a2);
// 0x0019f9d0: ends on 0x07b4e7f7 / death; waits `time`, then handle field 0x0ccee5c1 = 0x385d90.
void XB_CDECL BroScr_SetAnimMovementStopAfter(uint32_t ent, float time);
// 0x001a2450: with actor int +0x184 0x19669cd9: +0x184 = 0xc1165808 for field 0xb7f3f874 values
// 0x06088a8c / 0x068698d3, else 0x070b89a4; BroScr_SetPoseMovement(0x070b89a4, 0x3a09eb); the
// wounded walk loop anim knob (1, 0.1, 1); waits 10 s.
void XB_CDECL BroScr_WoundedWalkLoop(uint32_t ent);
// 0x001a6fe0: waits for "friendly_finished_using_mg42" on `ai`, then BroScr_LeaveTurretToNode.
void XB_CDECL BroScr_WaitFriendlyFinishedMG42(uint32_t unused, uint32_t ai);
// 0x001a95a0: BroScr_MagicBulletShield(ent, 100000000, 5, its health, 1) (never returns).
[[noreturn]] void XB_CDECL BroScr_MagicBulletShieldDefault(uint32_t ent);
// 0x001abc70 / 0x001abd00 / 0x001abd90 (_3 also ends on `other`'s death): ends on death and
// `end`; waits for `notify`, notifies `end`; BroScr_StopLookingAt when the look id 0x747cde6c is
// still `id`. (end, notify) = (0xa538f810, 0x807e5a80) / (0x671c92d3, 0xd6604ba3) /
// (0x1651c9e7, 0x0fc3c1c9).
void XB_CDECL BroScr_LookAtEndOnNotify_1(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtEndOnNotify_2(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtEndOnNotify_3(uint32_t ent, uint32_t other, uint32_t id);
// 0x001b31a0 (debug): forever BroScr_DebugDrawRadiusCircle of the first two chain nodes (array
// 0x0605894e), one frame each round.
[[noreturn]] void XB_CDECL BroScr_DebugDrawChainRadiiLoop(uint32_t ent, uint32_t a2);
// 0x001b6a50 / 0x001b6a80 / 0x001b9310 / 0x001b9710 / 0x001b9740: BroScr_AnimScript_Turret_2 /
// _Turret_2 / _Turret_3 / _Turret / _Turret (never return) with their three callbacks.
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_1(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_2(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_3(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_4(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_5(uint32_t ent, uint32_t a2);
// 0x001b6dc0 / 0x001b6e70 / 0x001b6f20: a BroScr_LookAtEndOnNotify_1 / _2 / _3 call thread, ends on
// death (and `other`'s) and that one's `end`; waits for 0x07b4e7f7, notifies `end`;
// BroScr_StopLookingAt when the look id is still `id`.
void XB_CDECL BroScr_LookAtUntilKillAnimscript_1(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtUntilKillAnimscript_2(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtUntilKillAnimscript_3(uint32_t ent, uint32_t other, uint32_t id);
// 0x001b6fe0: ends on 0x07b4e7f7 / `end`; forever BroScr_BlendAimAnimsByYaw(ent, a..base), 0.2 s.
[[noreturn]] void XB_CDECL BroScr_BlendAimAnimsByYawLoop(uint32_t ent, uint32_t end, uint32_t anim_a, uint32_t anim_b,
                                                       uint32_t anim_c, uint32_t a_bits, uint32_t b_bits,
                                                       uint32_t c_bits, uint32_t base_bits);
// 0x001b7180: hidelowwallb idle1 (75%) or twitch1 as flagged knob 0x54e3b2b6 on Body (1, 0.1,
// rate 0.9 + RandomFloat(0.2), restart), then its note tracks.
void XB_CDECL BroScr_CoverCrouchRandomIdleAnim(uint32_t ent);
// 0x001b7bc0: ends on death / 0xad9b8a7b; note tracks of 0x1c859d65; int 0xfd6181bd = 1; notify
// 0x1c87198d.
void XB_CDECL BroScr_AnimFaceWaitNotetracks(uint32_t ent);
// 0x001b9950: the stand MG42 gunner pain anim as flagged knob 0x94d136d5 (1, 0.1, 1), then its
// note tracks.
void XB_CDECL BroScr_PlayStandMG42GunnerPain(uint32_t ent);

}  // namespace game
