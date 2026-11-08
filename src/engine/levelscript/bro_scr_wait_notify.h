#pragma once

// Compiled level-script thread bodies (levelscript module) that wait: notify relays, temporary
// sound origins, siren / fire loops. Most suspend the script thread (Scr_Wait*, Scr_WaitTill*), so
// they are not in kFunctions (class thread_switch in docs/research/levelscript_classes.csv); they become
// testable once script threads are coroutines. The ones marked "registered" do not wait.

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x0011fb00: ends on `notify`, waits for 0x56caf2eb on `object`, then queues `notify` on `self`.
void XB_CDECL BroScr_NotifyAfterWaitTill_56caf2eb(uint32_t self, uint32_t object, uint32_t notify);
// 0x0011fb40: the same with Scr_WaitTillArgs(object, 0xce00786e, 0x199a4, 0, 0) (an anim's end).
void XB_CDECL BroScr_NotifyAfterSingleAnim(uint32_t self, uint32_t object, uint32_t notify);
// 0x0011fb90: ends on 0x3621f548 and death (0x06730ae8), then BroScr_HandleNoteTracks (0x001ada20)
// with a copy of the empty string (0x00922e90); its result string is released.
void XB_CDECL BroScr_AnimNoteTracksThread(uint32_t ent, uint32_t anim);
// 0x0011fc20: ends on 0x0032304c; after `ent`'s death queues 0x2253e8ee on `other`.
void XB_CDECL BroScr_AnimReachNotifyOnDeath(uint32_t unused, uint32_t ent, uint32_t other);
// 0x00120030: each frame, ent's flag 0x400 is set while the player (0x00922eec) looks more than 45
// degrees away from it (yaw of ent - player against the player's yaw), cleared when he looks back.
void XB_CDECL BroScr_FlagWhilePlayerLooksAway(uint32_t ent);
// 0x001205b0: waits (0.05 s steps) for health below 5, stops the scripted anim and does
// health + 100 damage from (0, 0, 0).
void XB_CDECL BroScr_KillWhenHealthLow(uint32_t ent);
// 0x00120630 (was FUN_00120630): Scr_WaitTill(object, 0x08d272e5).
void XB_CDECL BroScr_WaitTill_h08d272e5(uint32_t object);
// 0x00120650: "flak_generic" on a temporary script_origin at `origin`; deleted 1 s after the sound.
void XB_CDECL BroScr_PlayFlakSound(uint32_t unused, ScrVec3 origin);
// 0x00120750: `sound` at `position` from a temporary script_origin spawned at `origin`; deleted 1 s
// after the sound. Releases `sound`.
void XB_CDECL BroScr_PlaySoundAtTempOrigin(ScrVec3 origin, uint32_t unused, ScriptString sound, ScrVec3 position);
// 0x00120860: waits frames until 5000 ms of game time have passed.
void XB_CDECL BroScr_WaitFiveSeconds();
// 0x00120890: a full-screen "black" hud elem (level +0x2e0, 640x480) and, when `text` is set, a
// text elem (level +0x62c, at 320,220) faded in over 1.2 s; after 0.1 + 2.2 s the text fades out,
// 1.6 s later both are destroyed. Releases `text`.
void XB_CDECL BroScr_FadeFromBlack(ScriptString text);
// 0x00120e70 / 0x00120f10 (the second was inside the first's Ghidra body): on each "trigger" queues
// "start fx<n>" / "stop fx<n>" on the level (n: the trigger's int field 0x7fa1fc35 / 0x0bfd0e20).
void XB_CDECL BroScr_StartFxOnTrigger(uint32_t unused0, uint32_t unused1, uint32_t trigger);
void XB_CDECL BroScr_StopFxOnTrigger(uint32_t unused0, uint32_t unused1, uint32_t trigger);
// 0x00120fb0 (inside BroScr_StartFxOnTrigger's Ghidra body): on "trigger", BroScr_GetFriendlyChainTriggers
// with the trigger's chain string (field 0x83048760).
void XB_CDECL BroScr_FriendlyChainOnTrigger(uint32_t trigger);
// 0x00120ff0 (inside BroScr_StartFxOnTrigger's Ghidra body): waits for "trigger".
void XB_CDECL BroScr_WaitTillTrigger(uint32_t ent);
// 0x00121010: ends on the level's "killexplodertriggers<n>" (n = script_exploder), clears int map
// entry n, then on "trigger" runs BroScr_Exploder(level, n) and queues the kill notify.
void XB_CDECL BroScr_ExploderTriggerThink(uint32_t trigger);
// 0x00121110 (inside BroScr_ExploderTriggerThink's Ghidra body): swaps the panzerfaust and its ammo box
// to their objective models on "infinite panzerfaust objective on" and back on "... off", forever.
void XB_CDECL BroScr_PanzerfaustObjectiveModels(uint32_t unused, uint32_t level, uint32_t weapon, uint32_t box);
// 0x00121320 (registered): when alive, fires the vehicle's weapon and sets the fire anim knob
// (0x008cd4b8; 1.0, 0.2 s, rate 1.0, restart).
void XB_CDECL BroScr_VehicleFireAnimIfAlive(uint32_t vehicle);
void XB_CDECL BroScr_VehicleFireAnimIfAlive_2(uint32_t vehicle);  // 0x00122d10, same body
// 0x00121360: AnimScriptedNoNotify(ent, 0xaec5813d, origin, angles, anim), waits for its end, then
// queues 0x93b8e727 on ent.
void XB_CDECL BroScr_AnimScriptedThenNotify(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim);
// 0x00122af0 / 0x00122d50: the same body.
void XB_CDECL BroScr_AnimScriptedThenNotify_2(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim);
void XB_CDECL BroScr_AnimScriptedThenNotify_3(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim);
// 0x001213c0 (registered): fires the vehicle's weapon when it is alive.
void XB_CDECL BroScr_VehicleFireIfAlive(uint32_t vehicle);
// 0x001219d0 / 0x00122ad0 / 0x00123be0 (registered): the same body.
void XB_CDECL BroScr_VehicleFireIfAlive_2(uint32_t vehicle);
void XB_CDECL BroScr_VehicleFireIfAlive_3(uint32_t vehicle);
void XB_CDECL BroScr_VehicleFireIfAlive_4(uint32_t vehicle);
// 0x00121540: `sound` on a temporary script_origin (spawned at (0, 0, 1), moved to `origin`),
// deleted when the sound is done. Releases `sound`.
void XB_CDECL BroScr_PlaySoundOnTempOrigin(uint32_t unused, ScriptString sound, ScrVec3 origin);
// 0x001217b0 / 0x00122dc0: the same body.
void XB_CDECL BroScr_PlaySoundOnTempOrigin_2(uint32_t unused, ScriptString sound, ScrVec3 origin);
void XB_CDECL BroScr_PlaySoundOnTempOrigin_3(uint32_t unused, ScriptString sound, ScrVec3 origin);
// 0x00121630: ends on "stop_mg42s"; plays "stuka_siren" on ent again each time it is done.
void XB_CDECL BroScr_StukaSiren(uint32_t ent);
void XB_CDECL BroScr_StukaSiren_2(uint32_t ent);  // 0x001218a0, same body
void XB_CDECL BroScr_StukaSiren_3(uint32_t ent);  // 0x00122eb0, same body
// 0x001216d0: anim tree "stuka"; on "takeoff" sets the two takeoff anims (0x008cd4d0, 0x008cd4c4).
void XB_CDECL BroScr_StukaTakeoff(uint32_t ent);
// 0x00121960: AnimScriptedNoNotify(ent, 0xe2ea7dce, origin, angles, (int)anim) and waits for its end.
void XB_CDECL BroScr_TankDeadGuyPlayAnim(uint32_t ent, ScrVec3 origin, ScrVec3 angles, float anim);
// 0x00121a00: while alive, fires the vehicle's weapon on each 0xac1c7bac notify.
void XB_CDECL BroScr_VehicleTurretFireLoop(uint32_t vehicle);
// 0x00127e30 / 0x00127e90 / 0x001283d0 / 0x00128f30 / 0x00128f90: the same body.
void XB_CDECL BroScr_VehicleTurretFireLoop_2(uint32_t vehicle);
void XB_CDECL BroScr_VehicleTurretFireLoop_3(uint32_t vehicle);
void XB_CDECL BroScr_VehicleTurretFireLoop_4(uint32_t vehicle);
void XB_CDECL BroScr_VehicleTurretFireLoop_7(uint32_t vehicle);
void XB_CDECL BroScr_VehicleTurretFireLoop_8(uint32_t vehicle);
// 0x00128720 / 0x00128780: the same on "turret_fire" (Scr_WaitTillString).
void XB_CDECL BroScr_VehicleTurretFireLoop_5(uint32_t vehicle);
void XB_CDECL BroScr_VehicleTurretFireLoop_6(uint32_t vehicle);
// 0x00127d30 / 0x00127db0 / 0x001287e0 / 0x00128860: on "turret_fire", BroScr_VehicleFireAnimIfAlive's body.
void XB_CDECL BroScr_TankTurretFireLoopRecoil(uint32_t vehicle);
void XB_CDECL BroScr_TankTurretFireLoopRecoil_2(uint32_t vehicle);
void XB_CDECL BroScr_TankTurretFireLoopRecoil_3(uint32_t vehicle);
void XB_CDECL BroScr_TankTurretFireLoopRecoil_4(uint32_t vehicle);
// 0x00121a60: ends on 0x92b8c65f; after `ent`'s death queues 0x92b8c65f on the level and on `self`.
void XB_CDECL BroScr_NotifyLevelOnDeath(uint32_t self, uint32_t ent);
// 0x00121ac0: waits for `count` 0x92b8c65f notifies on the level.
void XB_CDECL BroScr_WaitTillNotifyCount(int32_t count);
// 0x00121b10: Scr_WaitTill(object, 0xcc07a20a).
void XB_CDECL BroScr_WaitTill_hcc07a20a(uint32_t object);

}  // namespace game
