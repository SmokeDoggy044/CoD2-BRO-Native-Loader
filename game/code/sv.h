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

// 0x002af3d0 (command buffer in EAX: +4 size, +8 base, +0xc text pointers, +0x10 lengths, +0x14 write cursor;
// slot and text on the stack): stores the text in the buffer's zero-filled space - at the cursor (wrapped to the
// base when it would pass the end) when the bytes there are free, else the first run of len + 1 zero bytes that
// starts after a zero (__break 1 "Reliable command buffer overflow" when the scan reaches the end, or for a
// zero-size buffer); '%' becomes '.'.
void XB_REGS1 XB_CDECL SV_AddServerCommand(uint8_t* buffer, int32_t slot, const char* text);

// 0x002bfd10 (client in ESI, null = everyone through the server static when its state is 1; format and
// arguments on the stack): _vsprintf into a 0x800-byte buffer, then SV_AddServerCommand at the client's next
// reliable sequence (+0x18, & 0x3f).
extern "C" PLATFORM_HIDDEN void XB_CDECL SV_SendServerCommand(uint8_t* client, const uint32_t* args);
extern "C" void SV_SendServerCommand_entry(void);
// 0x002bffd0 (text in EAX; entity handle on the stack: index & 0xfff, generation >> 12 checked against the
// table at 0x0097a968): "%s" to everyone for handle 0, else to the server static's client when the entity has a
// client (+0x23c).
void XB_REGS1 XB_CDECL SV_GameSendServerCommand(const char* text, uint32_t handle);

// areaParms_t: +0x10 mins, +0x20 maxs (16-byte rows), +0x30 list, +0x34 count, +0x38 max count, +0x3c content
// mask. World sector: +0 axis, +8 contents, +0xc dist, +0x10 entities (chained at +0xe0), +0x1c / +0x20 children.
// 0x002c72e0: Q3's SV_AreaEntities_r - entity numbers (+0x154) of the sector tree's entities whose content
// (+0x64) matches and whose bounds (+0x30 / +0x40) overlap; stops when the list is full.
void XB_CDECL SV_AreaEntities_r(const uint8_t* node, uint8_t* parms);
// 0x002cc400 (maxs in EAX, mins in ECX; list, max count, content mask on the stack): from the root sector
// 0x00935664; returns the count.
int32_t XB_REGS3 XB_CDECL SV_AreaEntities(const float* maxs, uint32_t unused, const float* mins, int32_t* list,
                                         int32_t max_count, uint32_t content_mask);

}  // namespace game
