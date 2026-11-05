#pragma once

// Compiled level-script mortar loops (levelscript module, .text 0x00170a90-0x00173040): shells on
// groups of mortar entities or around an entity, timed by the level's per-type delay maps (+0x208
// min, +0x2e8 max). Every one waits (Scr_Wait): class thread_switch, kept out of kFunctions until
// script threads run as coroutines. All but MortarGroupNearEntLoop never return. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"

namespace game {

// 0x00170a90: ends on `end` (level); a BroScr_MortarEntInit thread per group entity; forever: 0.1 s,
// a fresh copy of `group`; with the type's salvo count (level +0x71c) above 0 and a RandomInt(count)
// + 1 above 0: the copy shuffled, the first count of them shelled (BroScr_ShellIncomingThenImpact,
// floats undefined) 0.05-0.5 s apart, then 0.1-0.5 s; else all valid ones shuffled and shelled with
// the type's delay range between them.
[[noreturn]] void XB_CDECL BroScr_MortarGroupShuffledLoop(uint32_t unused, DynArray group, uint32_t end, uint32_t type);
// 0x00170fb0: ends on `end` (level); min / max distance 400 / 1200 when undefined; waits the type's
// delay (truncated); forever: a shell at `ent` (BroScr_PlayShellIncoming, BroScr_MortarThinkWithDefaults)
// when the player is between min and max (squared), then the delay again.
[[noreturn]] void XB_CDECL BroScr_MortarLoopNearPlayer(uint32_t ent, float min_dist, float max_dist, uint32_t end,
                                                     uint32_t type);
// 0x00171530: nothing (the arrays freed) for an empty group, an undefined type or an invalid `ent`.
// Ends on `end` (level); forever: the type's delay, the group entities between min and max of
// `ent` (squared), up to `count` random ones of them shelled (BroScr_ShellIncomingThenImpact_2) 0.5-1 s
// apart.
void XB_CDECL BroScr_MortarGroupNearEntLoop(uint32_t unused, uint32_t ent, DynArray group, float min_dist, float max_dist,
                                           uint32_t end, uint32_t type, int32_t count, DynArray extra);
// 0x00172a10: ends on 0x8ff01d8d; a script_origin; forever: at the player when inside the zone of
// `ent` (speed, -speed; notify 0x42d71e20 on the level), else around `ent`: its right * RandomFloat(
// speed or -speed, 50/50) plus its back (yaw + 180) forward * RandomFloat(count); dropped to the
// ground, shelled (BroScr_PlayShellIncoming, BroScr_MortarThinkWithDefaults); then the level's
// mortar delay (+0x808 + RandomFloat(+0x780)) or min + RandomFloat(max).
[[noreturn]] void XB_CDECL BroScr_RandomMortarsAroundEnt(uint32_t ent, uint32_t type, float speed, int32_t count,
                                                       uint32_t min, uint32_t max);

}  // namespace game
