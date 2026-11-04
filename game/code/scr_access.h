#pragma once

// Field accessors of the level-script runtime: the get/set functions the game import block
// (pointer at 0x0095f49c) hands the level script for path nodes, vehicle nodes and entities.
// Path-node and entity accessors first offer the access to a hook (node, field offset, value
// address; 0x008d20e8 for most fields, its own word in 0x008d20ec..0x008d2170 for some - the
// path-node type's is set in the shipped game) and else read or write the field themselves.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// A vector passed by value (three words on the stack); copied as bits.
struct ScrVec3 {
    uint32_t v[3];
};

// Path nodes by handle (g_pathNodeBlocks, see actor.cpp's path_node). Invalid handles (0,
// 0xffff, missing block) make a setter do nothing and a getter return the empty value.
// Script strings are passed by value: setters release theirs; getters return a new reference
// through the hidden result pointer (the first argument).

// 0x00015ed0 / 0x00016010 / 0x00016150 / 0x00016290 / 0x00016850: targetname (+0x30), target
// (+0x38), animscript (+0x3c), script_noteworthy (+0x34), type (+0x28; hooks 0x008d216c /
// 0x008d2170, angles 0x008d2164 / 0x008d2168).
void XB_CDECL ScrPathNode_SetTargetname(uint32_t handle, ScriptString value);
void XB_CDECL ScrPathNode_SetTarget(uint32_t handle, ScriptString value);
void XB_CDECL ScrPathNode_SetAnimscript(uint32_t handle, ScriptString value);
void XB_CDECL ScrPathNode_SetScriptNoteworthy(uint32_t handle, ScriptString value);
void XB_CDECL ScrPathNode_SetType(uint32_t handle, ScriptString value);
// 0x00015f50 / 0x00016090 / 0x000161d0 / 0x00016310 / 0x000168d0: the same fields read.
ScriptString* XB_CDECL ScrPathNode_GetTargetname(ScriptString* out, uint32_t handle);
ScriptString* XB_CDECL ScrPathNode_GetTarget(ScriptString* out, uint32_t handle);
ScriptString* XB_CDECL ScrPathNode_GetAnimscript(ScriptString* out, uint32_t handle);
ScriptString* XB_CDECL ScrPathNode_GetScriptNoteworthy(ScriptString* out, uint32_t handle);
ScriptString* XB_CDECL ScrPathNode_GetType(ScriptString* out, uint32_t handle);
// 0x000163d0 / 0x00016530: origin (+0x44), angles (+0x50); getters write `out` (the default
// vector 0x00922eb8 for an invalid handle) and return it.
void XB_CDECL ScrPathNode_SetOrigin(uint32_t handle, ScrVec3 value);
uint32_t* XB_CDECL ScrPathNode_GetOrigin(uint32_t* out, uint32_t handle);
void XB_CDECL ScrPathNode_SetAngles(uint32_t handle, ScrVec3 value);
uint32_t* XB_CDECL ScrPathNode_GetAngles(uint32_t* out, uint32_t handle);
// 0x00016690 / 0x00016700: radius (+0x54, float; 0 for an invalid handle).
void XB_CDECL ScrPathNode_SetRadius(uint32_t handle, uint32_t bits);
float XB_CDECL ScrPathNode_GetRadius(uint32_t handle);
// 0x00016770 / 0x000167e0: spawnflags (+0x2c, 16 bits). The getter returns AX (EAX's upper half
// as the original leaves it; EAX = the caller's value comes in for that).
void XB_CDECL ScrPathNode_SetSpawnflags(uint32_t handle, uint32_t flags);
uint32_t XB_REGS1 XB_CDECL ScrPathNode_GetSpawnflags(uint32_t eax, uint32_t handle);

// Vehicle nodes (g_vehNodes, 0x0095c228); no hook. Fields: targetname (+0), target (+4),
// speed (+8), lookahead (+0xc), script_noteworthy (+0x10), origin (+0x14), angles (+0x2c).
// 0x00016990..0x00016c20.
void XB_CDECL ScrVehNode_SetTargetname(uint32_t handle, ScriptString value);
ScriptString* XB_CDECL ScrVehNode_GetTargetname(ScriptString* out, uint32_t handle);
void XB_CDECL ScrVehNode_SetTarget(uint32_t handle, ScriptString value);
ScriptString* XB_CDECL ScrVehNode_GetTarget(ScriptString* out, uint32_t handle);
void XB_CDECL ScrVehNode_SetScriptNoteworthy(uint32_t handle, ScriptString value);
ScriptString* XB_CDECL ScrVehNode_GetScriptNoteworthy(ScriptString* out, uint32_t handle);
void XB_CDECL ScrVehNode_SetOrigin(uint32_t handle, ScrVec3 value);
uint32_t* XB_CDECL ScrVehNode_GetOrigin(uint32_t* out, uint32_t handle);
void XB_CDECL ScrVehNode_SetAngles(uint32_t handle, ScrVec3 value);
uint32_t* XB_CDECL ScrVehNode_GetAngles(uint32_t* out, uint32_t handle);
void XB_CDECL ScrVehNode_SetSpeed(uint32_t handle, uint32_t bits);
float XB_CDECL ScrVehNode_GetSpeed(uint32_t handle);
void XB_CDECL ScrVehNode_SetLookahead(uint32_t handle, uint32_t bits);
float XB_CDECL ScrVehNode_GetLookahead(uint32_t handle);

// Entities by handle (handle table 0x0097a968; setters also refuse handle 0). Each accessor
// names its field offset and hook in scr_access.cpp. Strings and vectors as above; ints return
// 0, floats 0 for an invalid handle. 0x00016c60..0x000183d0.
void XB_CDECL ScrEnt_SetClassname(uint32_t handle, ScriptString value);  // 0x00016c60
ScriptString* XB_CDECL ScrEnt_GetClassname(ScriptString* out, uint32_t handle);  // 0x00016cd0
void XB_CDECL ScrEnt_SetOrigin(uint32_t handle, ScrVec3 value);  // 0x00016d70
uint32_t* XB_CDECL ScrEnt_GetOrigin(uint32_t* out, uint32_t handle);  // 0x00016de0
void XB_CDECL ScrEnt_SetModel(uint32_t handle, ScriptString value);  // 0x00016eb0
ScriptString* XB_CDECL ScrEnt_GetModel(ScriptString* out, uint32_t handle);  // 0x00016f20
void XB_CDECL ScrEnt_SetSpawnflags(uint32_t handle, uint32_t value);  // 0x00016fc0
uint32_t XB_CDECL ScrEnt_GetSpawnflags(uint32_t handle);  // 0x00017020
void XB_CDECL ScrEnt_SetClosespeed(uint32_t handle, uint32_t bits);  // 0x00017140
float XB_CDECL ScrEnt_GetClosespeed(uint32_t handle);  // 0x000171a0
void XB_CDECL ScrEnt_SetTarget(uint32_t handle, ScriptString value);  // 0x00017200
ScriptString* XB_CDECL ScrEnt_GetTarget(ScriptString* out, uint32_t handle);  // 0x00017270
void XB_CDECL ScrEnt_SetTargetname(uint32_t handle, ScriptString value);  // 0x00017310
ScriptString* XB_CDECL ScrEnt_GetTargetname(ScriptString* out, uint32_t handle);  // 0x00017380
void XB_CDECL ScrEnt_SetTeamname(uint32_t handle, ScriptString value);  // 0x00017420
ScriptString* XB_CDECL ScrEnt_GetTeamname(ScriptString* out, uint32_t handle);  // 0x00017490
void XB_CDECL ScrEnt_SetWait(uint32_t handle, uint32_t bits);  // 0x00017530
float XB_CDECL ScrEnt_GetWait(uint32_t handle);  // 0x00017590
void XB_CDECL ScrEnt_SetRandom(uint32_t handle, uint32_t bits);  // 0x000175f0
float XB_CDECL ScrEnt_GetRandom(uint32_t handle);  // 0x00017650
void XB_CDECL ScrEnt_SetHealth(uint32_t handle, uint32_t value);  // 0x00017770
uint32_t XB_CDECL ScrEnt_GetHealth(uint32_t handle);  // 0x000177d0
void XB_CDECL ScrEnt_SetDmg(uint32_t handle, uint32_t value);  // 0x00017830
uint32_t XB_CDECL ScrEnt_GetDmg(uint32_t handle);  // 0x00017890
void XB_CDECL ScrEnt_SetAngles(uint32_t handle, ScrVec3 value);  // 0x000178f0
uint32_t* XB_CDECL ScrEnt_GetAngles(uint32_t* out, uint32_t handle);  // 0x00017960
void XB_CDECL ScrEnt_SetRotate(uint32_t handle, ScrVec3 value);  // 0x00017a30
uint32_t* XB_CDECL ScrEnt_GetRotate(uint32_t* out, uint32_t handle);  // 0x00017aa0
void XB_CDECL ScrEnt_SetDegrees(uint32_t handle, uint32_t bits);  // 0x00017b70
float XB_CDECL ScrEnt_GetDegrees(uint32_t handle);  // 0x00017bd0
void XB_CDECL ScrEnt_SetKey(uint32_t handle, uint32_t value);  // 0x00017c30
uint32_t XB_CDECL ScrEnt_GetKey(uint32_t handle);  // 0x00017c90
void XB_CDECL ScrEnt_SetDelay(uint32_t handle, uint32_t bits);  // 0x00017cf0
float XB_CDECL ScrEnt_GetDelay(uint32_t handle);  // 0x00017d50
void XB_CDECL ScrEnt_SetSpawnitem(uint32_t handle, ScriptString value);  // 0x00017db0
ScriptString* XB_CDECL ScrEnt_GetSpawnitem(ScriptString* out, uint32_t handle);  // 0x00017e20
void XB_CDECL ScrEnt_SetGroupname(uint32_t handle, ScriptString value);  // 0x00017ec0
ScriptString* XB_CDECL ScrEnt_GetGroupname(ScriptString* out, uint32_t handle);  // 0x00017f30
void XB_CDECL ScrEnt_SetScriptNoteworthy(uint32_t handle, ScriptString value);  // 0x00017fd0
ScriptString* XB_CDECL ScrEnt_GetScriptNoteworthy(ScriptString* out, uint32_t handle);  // 0x00018040
void XB_CDECL ScrEnt_SetMaxhealth(uint32_t handle, uint32_t value);  // 0x000180e0
uint32_t XB_CDECL ScrEnt_GetMaxhealth(uint32_t handle);  // 0x00018140
void XB_CDECL ScrEnt_SetAnimname(uint32_t handle, ScriptString value);  // 0x000181a0
ScriptString* XB_CDECL ScrEnt_GetAnimname(ScriptString* out, uint32_t handle);  // 0x00018210
void XB_CDECL ScrEnt_SetPersistentIndex(uint32_t handle, uint32_t value);  // 0x000182b0
uint32_t XB_CDECL ScrEnt_GetPersistentIndex(uint32_t handle);  // 0x00018310
void XB_CDECL ScrEnt_SetTakedamage(uint32_t handle, uint32_t value);  // 0x00018370
uint32_t XB_CDECL ScrEnt_GetTakedamage(uint32_t handle);  // 0x000183d0

}  // namespace game
