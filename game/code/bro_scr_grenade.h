#pragma once

// Compiled level-script thread bodies (levelscript module): the scripted grenade throw at a target
// (anim, battle chatter, wait for the anim's release note, magic grenade with a computed velocity)
// and its pose/hand wrappers. They suspend the script thread (Scr_WaitTillArgs), so they are not in
// kFunctions (class thread_switch in docs/levelscript_classes.csv).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00125290: ends on death; plays `anim` scripted (yaw = the direction to `target` + `yaw_offset`)
// with "grenadeattack" chatter, waits for the anim's 0x0032aa25 note, then throws a magic grenade
// from the hand tag (`hand` undefined or 0x07151174: "Bip01 R Hand"; 0x003798b4: "tag_weapon_left";
// anything else: none) at ent's origin + `offset` (undefined: 0) toward `target`, arriving after
// `time` seconds (undefined: distance / 600) under g_gravity; `fuse` outside [0, 5] or undefined
// uses the default fuse. The field 0xe5f891d2 is set while throwing; notifies 0x3d8cf578 and
// handles the anim's note tracks.
void XB_CDECL BroScr_ThrowGrenadeAtTarget_2(uint32_t ent, ScrVec3 target, float time, uint32_t anim, ScrVec3 offset,
                                            float fuse, float yaw_offset, uint32_t hand);
// 0x001258a0: the same with a lob: vertical speed sqrt(2 g dz) to reach `target` at the top, right
// hand only, no yaw offset.
void XB_CDECL BroScr_ThrowGrenadeAtTarget(uint32_t ent, ScrVec3 target, uint32_t anim, ScrVec3 offset, float fuse);
// 0x001308e0: BroScr_ThrowGrenadeAtTarget_2 with the right hand and no yaw offset.
void XB_CDECL BroScr_ThrowGrenadeRightHand(uint32_t ent, ScrVec3 target, float time, uint32_t anim, ScrVec3 offset,
                                           float fuse);
// 0x00130950: BroScr_ThrowGrenadeAtTarget with the stand (offset z 72) or crouch (z 65) throw anim
// by the actor's pose (ScrActor_GetInt_184); nothing for another pose.
void XB_CDECL BroScr_ThrowGrenadeByPose(uint32_t ent, ScrVec3 target, float fuse);

}  // namespace game
