#pragma once

// Compiled level-script fx threads (levelscript module, .text): tread effects, tree bursts, the
// looping-fx and "_fx_creation" sound origins and their default-argument wrappers. Every one
// reaches a script wait (BroScr_FxCreationSound / BroScr_FxCreationSoundThink, Scr_Wait*,
// Scr_CreateThread): class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"

namespace game {

// 0x00163b40: ends on "death" of both; nothing when the cvar scr_gmi_fast is not "0". Warns when
// the level's tread-fx map (+0x57c, key 0x438c482f) is empty. While `tread` is valid: waits
// RandomFloat(0.2), adds the distance `tread` moved to the distance travelled (starting at 0.001);
// with the vehicle's speed (slot 0x00922b38) above 1, the vehicle within 6000 of the player and
// the distance past the int field 0x8473dd9d: the surface (BroScr_TreadFxSurfaceThink(vehicle,
// wheel)) notifies 0xd33dc7a9 when it changed and, unless "nofx", plays at forward(tread angles)
// * -(speed / float field 0xdf09fd77 * int field 0xb42c3b83 (1 when undefined)); the int field
// is taken off the distance.
void XB_CDECL BroScr_TreadFx(uint32_t vehicle, uint32_t tread, uint32_t wheel);
// 0x001640c0: with `wait_trigger`: waits for 0xe2a210cf on `ent`, then Bro__treeburst_gmi__
// tree_burst_playBurst on each entity `ent` targets. Else waits for the level notify named by
// the burst_notify field (when set) and plays `ent`'s burst.
void XB_CDECL BroScr_TreeBurstTriggerThink(uint32_t ent, uint8_t wait_trigger);
// 0x00164750: after a frame, for each "_fx_looping" entity: with a script_fxid (else the string
// field 0xe5d3774f) a BroScr_FxCreationSoundThink thread on it with that sound and the origin of
// the entity it targets (undefined without; the 0xe5d3774f target is deleted).
void XB_CDECL BroScr_StartLoopingFx();
// 0x00164e20 / 0x00165120: spawns a "script_origin" at `origin` named "_fx_creation" and runs
// BroScr_FxCreationSound(it, sound, origin, to, delay) / BroScr_FxCreationSoundThink(it, sound,
// to, a, b, c, d) on it. `unused` is not read.
void XB_CDECL BroScr_SpawnFxCreationOrigin(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t delay);
void XB_CDECL BroScr_SpawnFxCreationOrigin_2(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t a,
                                             uint32_t b, uint32_t c, uint32_t d);
// 0x00164f20 / 0x00164fb0 / 0x00165030 / 0x001650a0: BroScr_FxCreationSound with the missing
// arguments undefined (vectors: the undefined lane; delay: the undefined float); _4 takes the
// origin of `at`.
void XB_CDECL BroScr_FxCreationSoundCall(uint32_t ent, uint32_t sound);
void XB_CDECL BroScr_FxCreationSoundCall_2(uint32_t ent, uint32_t sound, ScrVec3 to);
void XB_CDECL BroScr_FxCreationSoundCall_3(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t delay);
void XB_CDECL BroScr_FxCreationSoundCall_4(uint32_t ent, uint32_t sound, uint32_t at, uint32_t delay);
// 0x00165210 / 0x00165290 / 0x001652f0 / 0x00165350: BroScr_FxCreationSoundThink with the missing
// arguments undefined (handles, floats, vector; the first word of the plain call: the word at
// 0x00922e88).
void XB_CDECL BroScr_FxCreationSoundThinkCall(uint32_t ent, uint32_t sound);
void XB_CDECL BroScr_FxCreationSoundThinkCall_2(uint32_t ent, uint32_t sound, ScrVec3 to);
void XB_CDECL BroScr_FxCreationSoundThinkCall_3(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t a);
void XB_CDECL BroScr_FxCreationSoundThinkCall_4(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t a, uint32_t b);
// 0x001660e0 / 0x00166170: BroScr_SpawnFxCreationOrigin with the missing arguments undefined; the
// first passes the word at +4 of the level block (0x00922ee0) as the unused argument.
void XB_CDECL BroScr_SpawnFxCreationOriginCall(uint32_t unused, uint32_t sound, ScrVec3 origin);
void XB_CDECL BroScr_SpawnFxCreationOriginCall_2(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to);
// 0x001661d0 / 0x00166260 / 0x001662d0 / 0x00166350 / 0x001663e0: BroScr_SpawnFxCreationOrigin_2
// with the missing arguments undefined.
void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call(uint32_t unused, uint32_t sound, ScrVec3 origin);
void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_2(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to);
void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_3(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to,
                                                  uint32_t a);
void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_4(uint32_t unused, uint32_t sound, ScrVec3 origin, uint32_t a);
void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_5(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to,
                                                  uint32_t a, uint32_t b);

}  // namespace game
