#pragma once

// Compiled level-script world effects: tree bursts, window and stone breaks, minefields, loop
// sound origins.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0015d510: *out = the level's vector for `name` (map at 0x00922ee0 +0x3b4, by
// Scr_HashScriptString; HashMap_Index inserts an undefined one when missing), or (0, 0, 0) when
// `name` is empty or all three lanes are the undefined lane. Releases `name`; returns out.
uint32_t* XB_CDECL BroScr_GetLevelVecOrZero(uint32_t* out, ScriptString name);

// 0x0015d480: the oak tree burst model: level vector map (+0x3b4) entry for
// "p_env_oak_grn_med_b_d_anim_stnd" = (0, 0, 68), string map (+0x388) entry for it =
// "p_env_oak_grn_med_b" (Scr_HashName keys).
void XB_CDECL BroScr_TreeBurstModelFor();
// 0x001682c0: level string maps: +0x238 [0x8c1b319d] = "treeburst", +0x604 [0xf815b725] =
// "artillery_treeburst"; BroScr_TreeBurstModelFor; then a thread (Scr_StartThreadFunctor1) on
// BroScr_SetupTreeBursts with the level object's word at +4.
void XB_CDECL BroScr_TreeBurstInit();
// A thread (functor {0x007f5ba0, function, ent}, Scr_CreateThread) on every entity with the
// targetname: 0x00165f90 (dead) BroScr_WindowBreakThink on "window_break"; 0x001639f0 (dead)
// BroScr_StoneBreakTriggerThink on "stone_break", after the level effect map's (+0x57c)
// 0xdcb4052e = "impact_lmg_concrete"; 0x001654a0 BroScr_MinefieldTriggerThink on "minefield",
// after 0x7857af8e = "artillery_generic" (skipped when the count is <= 0, signed).
void XB_CDECL BroScr_GetWindowBreaks();
void XB_CDECL BroScr_StoneBreakFx();
void XB_CDECL BroScr_MinefieldTriggers();
// 0x0015d650: *out = the undamaged model for `name` (level string map +0x388; a copy of
// "p_env_oak_grn_med_b" when `name` is empty or the map's value is). Builds the script's unused
// warning first ("...modelname is undefined!  ret..." + that model + 0x00824ff0).
// Releases `name`; returns out.
ScriptString* XB_CDECL Bro__treeburst_gmi__tree_burst_getUndamagedModel(ScriptString* out, ScriptString name);

}  // namespace game
