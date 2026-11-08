#pragma once

// Shared compiled level scripts (levelscript module, push c 2, part 3): script array helpers
// (DynArray {data, capacity, size}; indexing past the size grows the array, as the compiled
// `array[i]` does), random models, spawner kills and threads started per element. Array and string
// arguments passed by value are released / freed by the callee.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct DynArray;
struct ScriptString;

// 0x001a4620 (dead): SetModel(ent, a random entry of `models`, 0); frees `models`.
void XB_CDECL BroScr_SetRandomModel(uint32_t ent, DynArray models);
// 0x001a46c0 / 0x001a48d0 (dead): empty script bodies releasing a string array (args 0-2).
// 0x001a4890 (dead): the same with the array in args 1-3.
void XB_CDECL BroScr_EmptyFunc_StrArray(DynArray strings);
void XB_CDECL BroScr_EmptyFunc_StrArray_2(uint32_t unused, DynArray strings);
void XB_CDECL BroScr_EmptyFunc_StrArray_3(DynArray strings);
// 0x001a4700 (dead): out = a copy of a random entry of `strings`; frees the array. Returns out.
ScriptString* XB_CDECL BroScr_RandomArrayString(ScriptString* out, DynArray strings);
// 0x001a47b0 (dead): GScr_AttachNoTag(ent, a random entry of `models`); frees `models`.
void XB_CDECL BroScr_AttachRandomModel(uint32_t ent, DynArray models);
// 0x001a53c0 (dead): BroScr_SetCreditsName(actor, the word at 0x00922ebc).
void XB_CDECL BroScr_SetCreditsNameDefault(uint32_t actor);
// 0x001a5830 (dead): targetname = "null " + the origin as "%f,%f,%f", key 0x070b8a34 = 1, then a
// BroScr_TrackStartFlagLoop (0x0019a150) thread on the entity.
void XB_CDECL BroScr_SetTargetnameFromOrigin(uint32_t ent);
// 0x001a61f0: deletes every valid spawner whose 0x460bf109 key is defined and equals `group`.
void XB_CDECL BroScr_KillSpawners(uint32_t group);
// 0x001a75d0: a thread of `function` on the level object for each entity of `ents`.
void XB_CDECL BroScr_ArrayThread(DynArray* ents, uint32_t function);
// 0x001a7650 (dead): calls `functor` (vtable slot 1: (&unused, &entry)) for each entry of `array`
// not in `exclude` (up to 1024 kept), or for every entry when `exclude` is empty.
void XB_CDECL BroScr_ArrayExclude(uint32_t unused, DynArray* array, DynArray* exclude, void* functor);
// 0x001a7790 / 0x001a7870 (dead): appends to `out` each entry of `array` other than *value / value.
void XB_CDECL BroScr_ArrayRemove(DynArray* array, const uint32_t* value, DynArray* out);
void XB_CDECL BroScr_ArrayRemove_2(DynArray* array, uint32_t value, DynArray* out);
// 0x001a7950: notifies `message` on every entity of `ents`; releases `message`.
void XB_CDECL BroScr_NotifyArray(DynArray* ents, ScriptString message);

}  // namespace game

namespace game {

struct ScrVec3;

// 0x001a7a00: BroScr_TriggerOff on each "trigger_friendlychain" whose script_chain (0x8007a3d3)
// equals `chain`. 0x001a7b70: those triggers moved back to their realorigin (when defined).
// 0x001a7d10: those triggers (script_chain defined) deleted. All release `chain`.
void XB_CDECL BroScr_GetFriendlyChainTriggers(ScriptString chain);
void XB_CDECL BroScr_MoveFriendlyChainTrigger(ScriptString chain);
void XB_CDECL BroScr_DeleteFriendlyChainTrigger(ScriptString chain);
// 0x001aa3b0: the node the first friendly-chain trigger of `chain` targets (GScr_GetNode by
// targetname; 0 for none / -1); without such a trigger the error text is built and dropped and
// the result is 0. Releases `chain`; returns out.
uint32_t* XB_CDECL BroScr_GetFriendlyChainNode(uint32_t* out, ScriptString chain);
// 0x001a8840: appends `ent` when it is a valid entity.
void XB_CDECL BroScr_AppendIfValidEnt(DynArray* array, uint32_t ent);
// 0x001a8590 (dead): whether any live AI of `team` touches `trigger` (collected, then re-checked
// alive). Releases `team`.
uint8_t* XB_CDECL BroScr_AnyAliveAITouching(uint8_t* out, uint32_t trigger, ScriptString team);
// 0x001a8a90: the entity of `array` closest to `origin`, skipping invalid ones and those in
// `exclude` (when it has storage); the undefined entity (0x00922e84) for an array without storage
// or, with `exclude`, no live entry left. As compiled, the start entry is measured from entry 0
// when there is no `exclude`. 0x001a8fc0: the same over path nodes (valid = not 0 / -1, no
// liveness test, undefined node 0x00922ea0). Both return out.
uint32_t* XB_CDECL BroScr_GetClosestFromArray(uint32_t* out, ScrVec3 origin, DynArray* array, DynArray* exclude);
uint32_t* XB_CDECL BroScr_GetClosestNodeFromArray(uint32_t* out, ScrVec3 origin, DynArray* nodes, DynArray* exclude);
// 0x001aa9c0 (dead): stores each entity's distance from `origin` under key 0xa171373f (stops at
// the first entity whose origin is the undefined vector), then exchange-sorts `ents` by it.
void XB_CDECL BroScr_SortByDistance(ScrVec3 origin, DynArray* ents);
// 0x001a94c0: BroScr_GetClosestFromArray over the AI of `team` (all AI for an empty string), the
// word at 0x00922e84 when there are none. Releases `team`.
uint32_t* XB_CDECL BroScr_GetClosestAI(uint32_t* out, ScrVec3 origin, ScriptString team, DynArray* exclude);
// 0x001aa670 (FUN_001aa670, dead; proposed BroScr_CheckAlliesField_ha1763cc1): reads field
// 0xa1763cc1's defined state on every "allies" AI (result unused).
void XB_CDECL BroScr_CheckAlliesField_ha1763cc1();
// 0x001a8870 (dead): the entries of `array` other than `ent` (an array named "emptyarray" when
// none are kept) copied back over its first `size` entries; nothing for an array without storage
// or an invalid `ent`.
void XB_CDECL BroScr_ArrayRemoveEnt(DynArray* array, uint32_t ent);
// 0x001aa750 (dead): damages every AI, then the player, within `radius` of `position` by
// (radius - distance) x 2 (the player's damage from its own origin).
void XB_CDECL BroScr_DamageAllAiInRange(ScrVec3 position, int32_t radius);

}  // namespace game

namespace game {

// 0x001a5c90 (FUN_001a5c90, dead; proposed BroScr_DeleteTeamEntsWithKeyValue): for the "axis" then
// "allies" entities (key 0x3b984d), a BroScr_DeleteAfterFrame thread on each whose 0x8109a0a9 key
// is defined and, as decimal text, equals `value`. Releases `value`.
void XB_CDECL BroScr_DeleteTeamEntsWithKeyValue(ScriptString value);

}  // namespace game
