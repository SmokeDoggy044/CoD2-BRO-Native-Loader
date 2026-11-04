#pragma once

// The GScr_ builtins: the engine functions compiled level scripts call through the script import
// table (g_scr_imports.cpp). Plain cdecl; script values come in as raw words (entity handles: index in
// the low 12 bits, generation above; vectors by pointer; floats by value).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x000144d0: G_PhysicsLaunch on the handle's entity with copies of point and dir (16-byte aligned,
// the 4th lane = x), hit location 4.
void XB_CDECL GScr_PhysicsLaunch(uint32_t handle, const float* point, const float* dir, float scale,
                                 uint32_t local_point);
// 0x00014590: G_FreeEntityPhysics on the handle's entity (tail jump), nothing when stale.
void XB_CDECL GScr_PhysicsStop(uint32_t handle);
// 0x00234ad0, 0x00234ae0: empty builtins.
void XB_CDECL GScr_EmptyStub174();
void XB_CDECL GScr_EmptyStub173();
// 0x00236a20: the fog colour r_fogColor (0x008db10c..0x008db114).
void XB_CDECL GScr_SetFogColor(float r, float g, float b);
// 0x00236a70: the fog start (0x0095f698) and end (0x008db104) distances.
void XB_CDECL GScr_SetFogDistances(float start, float end);
// 0x00238350: the fog colour from 0-255 ints (times 1/255, 0x0083eb30).
void XB_CDECL GScr_SetFogColorBytes(int32_t r, int32_t g, int32_t b);
// 0x002557c0 (once, byte 0x0098ad59): the script constants: 132 name hashes (scr_const_*, each text also
// entered in the name map with its script hash), 131 script strings from temporaries and 4 assigned from text.
void XB_CDECL GScr_LoadConsts();
// 0x002b74c0: clears flag 0x4000000 of the handle's entity's +0x2a4.
void XB_CDECL GScr_ClearEntFlag4000000(uint32_t handle);
// 0x002cead0: sets flag 0x4000000 of +0x2a4, then clears node 0 of every model's anim tree (the
// pointer list at +0x22c, count byte +0x2e of it; AnimTree_ClearNode_r).
void XB_CDECL GScr_SetEntFlag4000000(uint32_t handle);
// 0x002f4a50 (name in ESI, out in EDI): the objective state of a name (the script strings at
// 0x00984b08, 0x00984ac4, 0x00984b74, 0x00984b04, 0x00984ae0, 0x00984b48 = 0-5); false and 0 for others.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ObjectiveStateFromString(const ScriptString* name, uint32_t* state);
extern "C" void GScr_ObjectiveStateFromString_entry(void);
// 0x002f4b00: g_freezeControls (0x00927314) from a bool.
void XB_CDECL GScr_SetFreezeControls(uint32_t freeze);
// 0x002f4b10, 0x002f4b60, 0x002f4b70: empty builtins.
void XB_CDECL GScr_EmptyStub448();
// 0x002f4b20, 0x002f4b30: builtins returning 0.
uint32_t XB_CDECL GScr_ReturnZero446();
uint32_t XB_CDECL GScr_ReturnZero445();
void XB_CDECL GScr_EmptyStub561();
void XB_CDECL GScr_EmptyStub560();

// 0x002f4b80, 0x002f4b90, 0x002f4ba0: empty builtins.
void XB_CDECL GScr_EmptyStub559();
void XB_CDECL GScr_EmptyStub399();
void XB_CDECL GScr_EmptyStub397();
// 0x002f4be0, 0x002f4bf0, 0x002f4c00: one float global each (0x008db19c, 0x008db198, 0x008d20e0).
void XB_CDECL GScr_Set8db19c(float value);
void XB_CDECL GScr_Set8db198(float value);
void XB_CDECL GScr_Set8d20e0(float value);
// 0x002f4c20: a stick of the active pad (0x008d50d8; pad 0 when -1) as -1..1 floats (the high bytes
// times 1/127, 0x0083ec60). Both outputs are zeroed first.
void XB_CDECL GScr_GetPadStick(int32_t stick, float* x, float* y);
// 0x002f4ea0: the byte at 0x0095f4b4.
void XB_CDECL GScr_Set95f4b4(uint32_t value);
// 0x002f50e0 .. 0x002f5110: the fog pass settings g_fogPassCount (0x00897960), g_fogPassAlpha
// (0x0089795c), g_fogPassStep (0x00897958), g_fogPassFinalAlpha (0x00897954).
void XB_CDECL GScr_SetFogPassCount(int32_t count);
void XB_CDECL GScr_SetFogPassAlpha(float alpha);
void XB_CDECL GScr_SetFogPassStep(float step);
void XB_CDECL GScr_SetFogPassFinalAlpha(float alpha);
// 0x002f5140: the HUD-hidden byte 0x0095f91f = !visible.
void XB_CDECL GScr_SetHudVisible(uint32_t visible);
// 0x002f5190 .. 0x002f51e0: empty builtins.
void XB_CDECL GScr_EmptyStub221();
void XB_CDECL GScr_EmptyStub220();
void XB_CDECL GScr_EmptyStub223();
void XB_CDECL GScr_EmptyStub222();
void XB_CDECL GScr_EmptyStub200();
void XB_CDECL GScr_EmptyStub199();
// 0x002f51f0: g_missileMaxLifetime (0x00985ffc) = seconds * 1000.
void XB_CDECL GScr_SetMissileMaxLifetime(float seconds);
// 0x002f5210: g_compassMarkersEnabled (0x00986008) from a bool.
void XB_CDECL GScr_SetCompassMarkersEnabled(uint32_t enabled);
// 0x002f5220: the radius-damage flag 0x00986010 from a bool.
void XB_CDECL GScr_SetRadiusDamageFlag(uint32_t enabled);
// 0x002f5230, 0x002f5240: empty builtins.
void XB_CDECL GScr_EmptyStub184();
void XB_CDECL GScr_EmptyStub183();

// 0x002f52b0: the difficulty name (the text table 0x008d20d0 by g_skillLevel 0x00950b3c) into out
// (ScriptString_AssignCString; returns its result).
uint32_t XB_CDECL GScr_GetDifficulty(ScriptString* out);
// 0x002f52d0: level.time (0x0098540c).
int32_t XB_CDECL GScr_GetTime();
// 0x002f52e0, 0x002f52f0, 0x002f5300: builtins returning false.
uint8_t XB_CDECL GScr_ReturnFalse511();
uint8_t XB_CDECL GScr_ReturnFalse510();
uint8_t XB_CDECL GScr_ReturnFalse509();
// 0x002f5310, 0x002f5320, 0x002f5330: empty builtins.
void XB_CDECL GScr_EmptyStub21();
void XB_CDECL GScr_EmptyStub22();
void XB_CDECL GScr_EmptyStub14();
// 0x002f5350: the engine text for `id` (engine interface 0x0095f49c, slot +0x870) copied into buf (at most
// size - 1 characters, _strncpy, then terminated); false when the engine has none.
uint8_t XB_CDECL GScr_CopyStringFromEngine(uint32_t id, char* buf, uint32_t size);
// 0x002f53b0: appends a word to the list at 0x0095fdf0 (capacity u16 0x0095fdf4, count s16 0x0095fdf6) while
// there is room.
void XB_CDECL GScr_PushToList95fdf0(uint32_t value);
// 0x002f53e0, 0x002f53f0: empty builtins.
void XB_CDECL GScr_EmptyStub1();
void XB_CDECL GScr_EmptyStub0();
// The persistent values of the owner at 0x0095f4e0 (12-byte records {key, value, count} at +0x560, count
// +0x568; a vector continues in the records after its first, key 0).
// 0x002f5830: the vector stored under key, else three copies of the float at 0x00922eb8; returns out.
float* XB_CDECL GScr_GetPersistentVector(float* out, uint32_t key);
// 0x002f58d0: the float stored under key, else the float at 0x00922eb8.
float XB_CDECL GScr_GetPersistentFloat(uint32_t key);
// 0x002f5920: the int stored under key, else 0xfefefefe.
uint32_t XB_CDECL GScr_GetPersistentInt(uint32_t key);
// 0x002f5960 / 0x002f59a0 / 0x002f59d0: Persistent_SetValue with 3 / 1 / 1 values.
void XB_CDECL GScr_SetPersistentVector(uint32_t key, const uint32_t* value);
void XB_CDECL GScr_SetPersistentFloat(uint32_t key, uint32_t value);
void XB_CDECL GScr_SetPersistentInt(uint32_t key, uint32_t value);
// 0x002f59f0: the float 0x00964b84 and the word 0x00964b88.
void XB_CDECL GScr_Set964b84(float value, uint32_t word);
// 0x002f5a10: the HUD objectives' (g_hudManager 0x0095f8f0 -> +4) +0x13d4 and +0x13d8.
void XB_CDECL GScr_SuppressObjectiveUpdateMessage(uint32_t a, uint32_t b);
// 0x002f5a30: the HUD objectives' 16-bit +0x13d0 and +0x13d2.
void XB_CDECL GScr_HudObjectives_Set13d0(uint32_t a, uint32_t b);

}  // namespace game
