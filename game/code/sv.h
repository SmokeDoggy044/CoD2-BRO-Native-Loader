#pragma once

// Server-side engine code (Q3's sv_*.c): game-module glue, entity linking, server statics.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002a8750: __break 1 "Bad game system trap" with the trap number; -1.
int32_t XB_CDECL SV_GameSystemCalls(const int32_t* args);
// 0x002aacb0 (entity in EAX): unlinks the entity from its world sector (+0xdc; chain at +0xe0, sector list
// +0x10), frees sectors left empty (no entities, +0x14 clear, both children the null sector 0x0093568c) onto the
// free list 0x00935688 and recomputes the contents (+8) up the parent chain (+0x18). Returns 0.
int32_t XB_REGS1 XB_CDECL SV_UnlinkEntity(uint8_t* ent);
// 0x002bffb0 (entity in EAX, open in ECX): CM_AdjustAreaPortalState(+0x1fc, +0x200, open) unless +0x200 is -1.
void XB_REGS3 XB_CDECL SV_AdjustAreaPortalState(const uint8_t* ent, uint32_t unused, uint32_t open);
// 0x002c4ed0: the 0xec0-byte server static (0x00927e8c; __break 0 when already up or out of memory), 0x540 at
// 0x00927e90, sv_running 1.
void XB_CDECL SV_AllocServerStatic();
// 0x002c5140 / 0x002c51a0 / 0x002c5200: g_player_maxhealth 500 / 1100 / 2000, g_gameskill 2 / 1 / 0, and
// g_reloading 2 while the 0x0093f800 cvar is set.
void XB_CDECL SV_SetSkillEasy();
void XB_CDECL SV_SetSkillMedium();
void XB_CDECL SV_SetSkillHard();
// 0x002cf830 (entity in ECX): the entity's clip model (+0xdc), else the temporary box model of its bounds
// (+0xe0 mins, +0xf0 maxs, contents +0x134; capsule when +0xd4 has 0x200).
uint8_t* XB_REGS3 XB_CDECL SV_ClipHandleForEntity(uint32_t unused_eax, uint32_t unused_edx, uint8_t* ent);

}  // namespace game
