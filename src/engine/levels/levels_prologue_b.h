#pragma once

// Prologue level script bodies (level section 0x0041a000-0x00458000), lifter pilot list b: the same
// script compiler output as the levelscript module (bro_scr_*.cpp). The level block is the word at
// 0x00921cbc (+4 = the level's own entity), the player entity the word at 0x00921cd0. Names of the
// form BroLvl_prologue_* are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct DynArray;
struct ScriptString;
struct ScrVec3;

// 0x00422250: the line for `line` (hash) into out: three "all clear" variants (0xb807fa29), clear up /
// down / clear (0xcc2871d3 / 0xa1cfbdbe / 0x06079852), six "grenade" warnings (0x226688fc), three
// redshirt "Sarge" calls (0xf90d48f4), BroLvl_prologue_RandomManDownMedicLine (0x06e31d43); `index`
// picks the variant, a random one when it is the undefined int; anything else = the empty string.
// Returns out.
ScriptString* XB_CDECL BroLvl_prologue_KellyAllClearLines(ScriptString* out, uint32_t ent, uint32_t line,
                                                          uint32_t index);
// 0x00425510 (dead: no reference): the launch velocity from `start` to `end` in `time` seconds under
// g_gravity into out; a level thread 0x0041a260 on (start, velocity, time) first. Returns out.
uint32_t* XB_CDECL BroLvl_prologue_LaunchVelocity(uint32_t* out, uint32_t self, ScrVec3 start, ScrVec3 end,
                                                  uint32_t time);
// 0x004264b0: the entity's health set back to its field 0x166cb1df.
void XB_CDECL Bro_prologue__Health_Reset(uint32_t ent);
// 0x004268c0: frees the tank's sound field 0x4a3b5a6c (when defined), sets its entity var 0x06730ae8
// to "prologue::Vehicle_TankDeath", then a level thread 0x004256b0 at its origin (500, 2) and a
// thread 0x0041a800 on the tank.
void XB_CDECL Bro_prologue__Vehicle_TankDeath(uint32_t ent);
// 0x004275f0: dialogue record for "PRO_E03L05_BLO_WaitForThe" into out: {the speaker (level +0x848),
// 0, its anim 0x008c5ab8, the line, (0, 180, 0)}. Returns out.
uint32_t* XB_CDECL BroLvl_prologue_LineBloomfieldWaitForThe(uint32_t* out);
// 0x0042c6d0: per gunner of `gunners` a thread 0x0041d4a0 on (gunner, node "event3_mg_route<route+i>",
// whether it is the last one); frees the array.
void XB_CDECL BroLvl_prologue_Event3MgRouteThreads(uint32_t self, DynArray gunners, int32_t route);
// 0x00432010: BroLvl_prologue_RandomManDownMedicLine on the level entity, played (notify 0xfaca5d86)
// by the last ally closer to the player than 1e8 (the best distance is never updated).
void XB_CDECL BroLvl_prologue_AllyManDownMedicLine();
// 0x0043d730: swaps the squad models: with `lod` the entity goes on the level's LOD list (+0x874) and
// gets its LOD model; else the full model, for the level entity on each listed entity (the list is
// cleared after). `tag` 0xe2de2626 sets the model with the level's +0x854 word.
void XB_CDECL BroLvl_prologue_SwapLodModel(uint32_t ent, uint32_t lod, uint32_t tag);
// 0x0044ed20: three dialogue texts into the level's line table (+0x124, speaker 0x0627bcce).
void XB_CDECL BroLvl_prologue_InitDialogueMisc();
// 0x004533d0: the level's 16 import callbacks into block[5..20]. Returns block.
uint32_t* XB_CDECL LevelScript_prologue_SetImportCallbacks(uint32_t* block);

}  // namespace game
