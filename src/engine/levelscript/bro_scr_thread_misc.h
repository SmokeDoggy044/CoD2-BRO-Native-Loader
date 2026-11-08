#pragma once

// Compiled level-script thread bodies (levelscript module), assorted: a drone's dropped gun, the
// flak reload loop, the trigger spawner and spawn init, notify-or-timeout wrappers. They suspend the
// script thread (directly or through a callee or a thread they start), so they are not in
// kFunctions (class thread_switch in docs/research/levelscript_classes.csv).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x0012f6c0: attaches ent's model field 0x1a7e7545 at "tag_weapon_right", then drops a
// "script_model" `model` from that tag: rotates at `velocity` and moves under gravity for 5 s,
// deleted after 6 s. Releases model.
void XB_CDECL BroScr_AttachDroneGun(uint32_t unused, uint32_t ent, ScriptString model, ScrVec3 velocity);
// 0x0012f8b0: while ent's int field 0xa027aabf is 1: waits for "turret_fire", fires the vehicle
// weapon, 1 s, plays "flak_reload".
void XB_CDECL BroScr_FlakFireAndReload(uint32_t ent);
// 0x00130870: waits for `notify` on the level, then sets ent's bool field 0x75c85f71 (added as the
// undefined bool in a word whose upper bytes are `notify`'s, the reused stack slot).
void XB_CDECL BroScr_FxCreationSoundWait(uint32_t ent, uint32_t notify);
// 0x00130b50: a spawned AI's init: drops health by script_health (when defined); spawnflag 1
// starts BroScr_SpawnedThink (functor thread on the level), else BroScr_SpawnerFinishSpawn and a
// BroScr_SetupFriendlyFireMissionFail thread.
void XB_CDECL BroScr_AiSpawnInit(uint32_t ent);
// 0x00130c80: ends on spawner death; with the spawner's count left and `trigger`'s target equal to
// its targetname: force-spawns (int field 0x02d62a20 = 1) or spawns, waits trigger's wait (when
// positive) and, count left, starts itself again.
void XB_CDECL BroScr_TriggerSpawnerThink(uint32_t trigger, uint32_t spawner);
// 0x001301d0: one strafing round from ent's "tag_flash": traces flak_dist along the tag's forward
// (LocationalTraceSurface). A "default" surface or a fraction above 0.95: a flak sound thread
// (BroScr_PlayFlakSound) at the trace end after distance * 5e-05 s. Otherwise, within flak_dist (and
// 96 of 100 rolls; the undefined bool cleared): after the same wait, the surface's "strafe_*_hit"
// sound at the hit point facing back to the tag (BroScr_PlaySoundAtTempOrigin thread), else the
// flak sound at the hit point (the undefined vector when the undefined bool is set).
void XB_CDECL BroScr_StrafeImpacts(uint32_t ent);
// 0x00130df0 / 0x00130ee0 / 0x00130fc0 / 0x001310b0: BroScr_WaitForNotifyOrTimeout with the empty
// endon and the undefined timeout (_4: `timeout`), the notify (_2.._4: empty), the second vector
// (_3: undefined) as given. Release their own strings.
void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2,
                                                ScriptString notify);
void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_2(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2);
void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_3(uint32_t a, ScrVec3 v1, ScriptString s);
void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_4(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2, float timeout);

}  // namespace game
