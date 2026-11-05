#pragma once

// Compiled level-script actor helpers: cover-node lookups, direction names and blend weights
// (anim state object 0x00922efc), the player's stance offset.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x00184230: the actor's node (GetRef_48 read twice; the second read invalid -> its nearest
// node, -1 -> 0), cleared when the node is more than 16 units from the actor. *out = node.
// Gap entries (the C versions take the entry ESP; cdecl (out, actor); EAX = out): the nearest-node
// slot keeps a frame address in 0x00929b40, so it runs at the original's ESP.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroScr_GetNodeAtPosition_c(uint32_t entry_esp);
extern "C" void BroScr_GetNodeAtPosition_entry(void);
// 0x00184310: the node's yaw, else the actor's float +0x154.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroScr_GetNodeOrActorYaw_c(uint32_t entry_esp);
extern "C" void BroScr_GetNodeOrActorYaw_entry(void);
// 0x00184390: AnglesToForward of the node's angles (into a vector preset to the undefined
// lane), else the undefined vector 0x00922ec4.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroScr_GetCoverNodeForward_c(uint32_t entry_esp);
extern "C" void BroScr_GetCoverNodeForward_entry(void);

// 0x00184a20: (int)yaw % 360 -> "front" (< 45 or > 315), "left" (< 135), "back" (< 225), "right"
// (as hashes).
uint32_t* XB_CDECL BroScr_YawToDirectionName(uint32_t* out, float yaw);
// 0x00184520: yaw % 360 to -180..180, SinCosDegrees; the third output > 0 -> +0x13c else
// -it -> +0x140, the second > 0 -> +0x148 else -it -> +0x144 (the other of each pair 0).
void XB_CDECL BroScr_SetDirectionBlendWeights(uint32_t a1, float yaw);

// 0x001819c0: (0, 0, 5 prone / 25 crouch / 50 stand) from the player's (0x009236dc) stance.
uint32_t* XB_CDECL BroScr_GetPlayerStanceOffset(uint32_t* out);

// 0x00181b10: level object (0x00922ef8) defaults for load_gmi: +0x88 = 10, +0x200, +0xe0,
// +0x198, +0xdc, +0xf4 = 0, byte +0xf0 = 0. EAX = 0.
uint32_t XB_CDECL BroScr_LoadGmiInitLevelVars();

// 0x001814c0 (thiscall, callee pops 4): *out = the word ScrSentient_GetRef_3c (slot 0x00922e00)
// returns for the handle at self; returns out.
uint32_t* XB_THISCALL BroScr_GetSentientRef3c(const uint32_t* self, uint32_t* out);
// 0x001814e0: out = GScr_GetEye(*ent) (into an undefined vector first); returns out.
uint32_t* XB_CDECL BroScr_GetEyePos(uint32_t* out, const uint32_t* ent);
// 0x001835b0: GScr_MagicGrenadeManual(ent, the origin of "TAG_WEAPON_RIGHT", (0, 0, 10), 3.0).
void XB_CDECL BroScr_DropWeaponRight(uint32_t ent);

}  // namespace game
