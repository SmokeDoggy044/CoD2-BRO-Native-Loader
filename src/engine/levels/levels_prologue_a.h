#pragma once

// Prologue level-section script bodies (0x0041a000-0x00458000), lifter pilot list a: output of the
// same script compiler as the levelscript module (bro_scr_*.cpp). Names are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x0041aa00: *out = "SQD_DEN_SIT_09_Medic" / "SQD_DEN_SIT_08_ManDownMed" (Scr_RandomInt(2) 0 / 1), else
// a copy of the empty string. Returns out.
ScriptString* XB_CDECL BroLvl_prologue_RandomManDownMedicLine(ScriptString* out);
// 0x0041ba00 (stored pointer): the level's anim handle for the two names (hashed; AnimHandle_GetSet
// 0x0041b800), 0 when it has none.
uint32_t XB_CDECL BroLvl_prologue_AnimHandleByName(const char* set, const char* anim);
// 0x0041ba60: the debug cvars skipto, artist, create_shrimp_paths, no_shrimps, no_scene_anim set to "0"
// when empty.
void XB_CDECL BroLvl_prologue_InitDebugCvars();
// 0x0041cd70: the tank's destroyed model (v_sherman_d1), turret target cleared, an earthquake at its
// origin, "vehicle_explode", and its mgturret deleted when defined.
void XB_CDECL BroLvl_prologue_ShermanDestroyed(uint32_t tank);
// 0x004214a0: deletes the entity whose script_noteworthy is the name `name_id` (IntHashMap_FindValue)
// and the one targeting it; the name is built in the argument's slot and released.
void XB_CDECL BroLvl_prologue_DeleteNoteworthyAndTargeter(uint32_t unused, uint32_t name_id);
// 0x004273d0: *out = {level +0x848 actor, talk-stop anim when within 160 (sqrt) of
// event3_bloomfield_spot1 else 0, the HoldUpHold anim, its line, angles (0, 180, 0)}. Returns out.
uint32_t* XB_CDECL BroLvl_prologue_Event3BloomfieldHoldUp(uint32_t* out);
// 0x0042c220: *out = whether every entity of the level block's +0x880 list has origin x < -1760.
uint8_t* XB_CDECL BroLvl_prologue_AllList880XBelowNeg1760(uint8_t* out);
// 0x004309b0: a thread per entity named `name` (targetname) that waits min + random(max - min) and
// then damages it by its health + 50 (0x00420740). Frees the list, releases name.
void XB_CDECL BroLvl_prologue_KillByTargetnameAfterRandomDelay(uint32_t unused, ScriptString name, float min,
                                                                  float max);
// 0x00432da0: a BroScr_DronePathsStartAllCall_6 thread (level, the event1_drones3d entities,
// "event1_drones", 0, 3, 5, false).
void XB_CDECL BroLvl_prologue_StartEvent1Drones3d();
// 0x0044f1e0: Kelly's 12 dialogue lines into the level block's +0x124 map.
void XB_CDECL BroLvl_prologue_InitDialogueKelly();

}  // namespace game
