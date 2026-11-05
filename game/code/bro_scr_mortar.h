#pragma once

// Compiled level-script mortar groups and mortar impacts.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"

namespace game {

// 0x00173040: defaults f1 = 400, f2 = 1200 (undefined float), i = 1 (undefined int);
// BroScr_MortarEntInit on each entity of `ents`, then a BroScr_MortarGroupNearEntLoop thread on the
// level object's word (+4) with pointers to all arguments (the values after the defaults). Frees
// both arrays.
void XB_CDECL BroScr_StartMortarGroupNearEnt(uint32_t ent, DynArray ents, float f1, float f2, uint32_t handle,
                                             uint32_t w, uint32_t i, DynArray extra);
// 0x001728b0: per entity of `ents`: a BroScr_MortarEntInit thread, then a BroScr_MortarLoopNearPlayer
// thread (functor {0x00824488, function, ent, a4..a7}). `self` and the last word are unused. Frees
// `ents`.
void XB_CDECL BroScr_StartMortarLoopsNearPlayer(uint32_t self, DynArray ents, uint32_t a4, uint32_t a5, uint32_t a6,
                                                uint32_t a7, uint32_t unused);
// 0x0016fa40: radius / max / min damage default to the level maps (+0x65c / +0x690 / +0x7a0) value
// for `type`, else 300 / 400 / 25; GScr_RadiusDamage at the entity's origin, then
// BroScr_MortarThink(ent, origin, a4, a5, a6, type).
void XB_CDECL BroScr_MortarThinkWithDefaults(uint32_t ent, uint32_t radius, uint32_t max_damage, uint32_t min_damage,
                                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t type);

}  // namespace game
