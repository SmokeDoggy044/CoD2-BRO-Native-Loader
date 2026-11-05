#pragma once

// Compiled AI anim scripts around note tracks and move plays (levelscript module, .text
// 0x001a07d0 and 0x001ac460-0x001aee80): the note-track handler loop, the run / walk / crouch
// move plays, dropping the weapon. Every one waits (Scr_Wait*, note tracks) or reaches a wait:
// class thread_switch, kept out of kFunctions until script threads run as coroutines. Names
// are ours (from the bodies).
//
// Keys: 0x0ccee5c1 anim_movement, 0x764918ae anim_alertness, int +0x184 the actor's pose
// (0x070b89a4 stand, 0xc1165808 crouch, 0x0738b1a5 prone, 0x0030880b back).

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x001a07d0: plays `anim` flagged 0x54e3b2b6 blending over `time`; after time / 2 sets the pose
// (int +0x184) to `pose`, anim_movement to `movement`, anim_alertness 0xc4a729a7 (`aiming`) or
// 0xc028584c; then waits out the rest of `time`.
void XB_CDECL BroScr_PlayStanceTransitionAnim(uint32_t ent, uint32_t anim, float time, uint32_t pose,
                                              uint32_t movement, uint32_t aiming);
// 0x001ac460 (14 bytes): BroScr_HeadLookMainLoop(ent) (never returns).
void XB_CDECL BroScr_HeadLookMainLoop_Thunk(uint32_t ent);
// 0x001ac9f0 (proposed BroScr_LookAtPlayerLoop): ends on death / 0x247afc7f; r = random 8..10;
// waits r / 2 + random(r / 2); then forever BroScr_TryLookAtPlayer and a wait of random
// 0.7 r .. r.
void XB_CDECL FUN_001ac9f0(uint32_t ent);
// 0x001acae0: waits for a note of `notify` and notifies it again as 0x71445fee (with the note).
void XB_CDECL BroScr_ForwardNotifyString(uint32_t ent, uint32_t notify);
// 0x001acd90: stand run: the combat run anim (field run_combatanim, or combatrun with the
// directional move thread) after the combat delay, else the field 0x93348a05 or precombatrun1
// and combatrun; pose stand, movement 0x1c6ce (BroScr_PlayStanceTransitionAnim, 1 s).
void XB_CDECL BroScr_StandCombatRun(uint32_t ent);
// 0x001acf80: stand walk (1.6 s, movement 0x3a09eb, aiming): the walk combat anim fields
// (walk_combatanim / 0x30248704 one time in five after the combat delay; 0x9dc74345 /
// 0xb8e86881), else stand_walk_combat loops (50 / 30 / 20 %).
void XB_CDECL BroScr_StandCombatWalk(uint32_t ent);
// 0x001ad1f0: crouch run: the field 0x66973bee on the body (0.4 s blend), else the blend-back
// anim, the crouch-run directional move thread and crouchrun (0.6 s, crouch, movement 0x1c6ce).
void XB_CDECL BroScr_CrouchRun(uint32_t ent);
// 0x001ad330: crouch walk: the field 0x66973bee on the body, else crouchwalk_loop (0.8 s, crouch,
// movement 0x3a09eb, aiming).
void XB_CDECL BroScr_CrouchWalkLoop(uint32_t ent);
// 0x001ad910: with a weapon (int +0x1378, weapon index +0xf0): GScr_DropWeapon of the named
// weapon at the gun hand with 75 + random(50) as its float argument; the dropped weapon deletes
// itself after 300 s (BroScr_DeleteAfter300 thread). Then
// BroScr_PutGunInHand(ent, 0x0036b1a5).
void XB_CDECL BroScr_DropWeapon(uint32_t ent);
// 0x001ada20: waits for notes of `notify` and runs their commands (poses, movement, alertness,
// gravity, step / bodyfall / melee sounds, gun hand, helmet swap, reload, drop gun), others go to
// `callback(ent, hash)`; returns the first "end"-type note (hashes 0x199a4 / 0x9aa82fee /
// 0xcc5542ad) in *out. `text` is released.
ScriptString* XB_CDECL BroScr_HandleNoteTracks(ScriptString* out, uint32_t ent, uint32_t notify, uint32_t callback,
                                              ScriptString text);
// 0x001aeac0: ends on `end` and 0x07b4e7f7; forever: BroScr_HandleNoteTracks; when it returned
// within 0.05 s twice in a row, waits out the 0.05 s.
void XB_CDECL BroScr_HandleNoteTracksForever(uint32_t ent, uint32_t notify, uint32_t end, uint32_t callback,
                                             ScriptString text);
// 0x001aeca0: BroScr_HandleNoteTracksForever as a thread ending on 0x6cd0168f, which `time`
// seconds later is notified.
void XB_CDECL BroScr_HandleNoteTracksForTime(uint32_t ent, float time, uint32_t notify, uint32_t callback,
                                             ScriptString text);
// 0x001aed40: unarmed_crouch_idle1 with note tracks at the rate field 0x6937c3b9; 60 %: then
// unarmed_crouch_twitch1.
void XB_CDECL BroScr_UnarmedCrouchIdle(uint32_t ent);

}  // namespace game
