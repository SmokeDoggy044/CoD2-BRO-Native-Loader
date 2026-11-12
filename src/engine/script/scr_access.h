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
float XB_CDECL ScrEnt_GetFloat_2fc(uint32_t handle);  // 0x000170e0 (gap)
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
void XB_CDECL ScrEnt_SetInt_370(uint32_t handle, uint32_t value);  // 0x000176b0 (gap)
uint32_t XB_CDECL ScrEnt_GetInt_370(uint32_t handle);  // 0x00017710 (gap)
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

// Actor (entity +0x240) and sentient (entity +0x244) fields by entity handle, same scheme; the
// name gives the field offset (or its use). References come back through the result pointer
// (0 when invalid), 16/8-bit fields in AX/AL. 0x00018430..0x0001b520.
void XB_CDECL ScrActor_SetFloat_d4(uint32_t handle, uint32_t bits);  // 0x00018430
float XB_CDECL ScrActor_GetFloat_d4(uint32_t handle);  // 0x00018490
void XB_CDECL ScrActor_SetFloat_d8(uint32_t handle, uint32_t bits);  // 0x00018500
float XB_CDECL ScrActor_GetFloat_d8(uint32_t handle);  // 0x00018560
void XB_CDECL ScrActor_SetVec_108(uint32_t handle, ScrVec3 value);  // 0x000185d0
uint32_t* XB_CDECL ScrActor_GetVec_108(uint32_t* out, uint32_t handle);  // 0x00018640
void XB_CDECL ScrActor_SetVec_114(uint32_t handle, ScrVec3 value);  // 0x00018720
uint32_t* XB_CDECL ScrActor_GetVec_114(uint32_t* out, uint32_t handle);  // 0x00018790
void XB_CDECL ScrActor_SetVec_120(uint32_t handle, ScrVec3 value);  // 0x00018870
uint32_t* XB_CDECL ScrActor_GetVec_120(uint32_t* out, uint32_t handle);  // 0x000188e0
void XB_CDECL ScrActor_SetFloat_828(uint32_t handle, uint32_t bits);  // 0x000189c0
float XB_CDECL ScrActor_GetFloat_828(uint32_t handle);  // 0x00018a20
void XB_CDECL ScrActor_SetFloat_82c(uint32_t handle, uint32_t bits);  // 0x00018a90
float XB_CDECL ScrActor_GetFloat_82c(uint32_t handle);  // 0x00018af0
void XB_CDECL ScrActor_SetFloat_824(uint32_t handle, uint32_t bits);  // 0x00018b60
float XB_CDECL ScrActor_GetFloat_824(uint32_t handle);  // 0x00018bc0
void XB_CDECL ScrActor_SetInt_830(uint32_t handle, uint32_t value);  // 0x00018c30
uint32_t XB_CDECL ScrActor_GetInt_830(uint32_t handle);  // 0x00018c90
void XB_CDECL ScrActor_SetInt_7b0(uint32_t handle, uint32_t value);  // 0x00018cf0
uint32_t XB_CDECL ScrActor_GetInt_7b0(uint32_t handle);  // 0x00018d50
void XB_CDECL ScrActor_SetInt_7b4(uint32_t handle, uint32_t value);  // 0x00018db0
uint32_t XB_CDECL ScrActor_GetInt_7b4(uint32_t handle);  // 0x00018e10
void XB_CDECL ScrActor_SetShort_7dc(uint32_t handle, uint32_t value);  // 0x00018e70
uint32_t XB_CDECL ScrActor_GetShort_7dc(uint32_t handle);  // 0x00018ed0
void XB_CDECL ScrActor_SetFloat_7b8(uint32_t handle, uint32_t bits);  // 0x00018f40
float XB_CDECL ScrActor_GetFloat_7b8(uint32_t handle);  // 0x00018fa0
void XB_CDECL ScrActor_SetFloat_7bc(uint32_t handle, uint32_t bits);  // 0x00019010
float XB_CDECL ScrActor_GetFloat_7bc(uint32_t handle);  // 0x00019070
void XB_CDECL ScrActor_SetInt_228(uint32_t handle, uint32_t value);  // 0x000190e0
uint32_t XB_CDECL ScrActor_GetInt_228(uint32_t handle);  // 0x00019140
void XB_CDECL ScrActor_SetInt_22c(uint32_t handle, uint32_t value);  // 0x000191a0
uint32_t XB_CDECL ScrActor_GetInt_22c(uint32_t handle);  // 0x00019200
void XB_CDECL ScrActor_SetVec_234(uint32_t handle, ScrVec3 value);  // 0x00019260
uint32_t* XB_CDECL ScrActor_GetVec_234(uint32_t* out, uint32_t handle);  // 0x000192d0
void XB_CDECL ScrActor_SetInt_230(uint32_t handle, uint32_t value);  // 0x000193b0
uint32_t XB_CDECL ScrActor_GetInt_230(uint32_t handle);  // 0x00019410
void XB_CDECL ScrActor_SetInt_240(uint32_t handle, uint32_t value);  // 0x00019470
uint32_t XB_CDECL ScrActor_GetInt_240(uint32_t handle);  // 0x000194d0
void XB_CDECL ScrActor_SetInt_194(uint32_t handle, uint32_t value);  // 0x00019530
uint32_t XB_CDECL ScrActor_GetInt_194(uint32_t handle);  // 0x00019590
void XB_CDECL ScrActor_SetFloat_7a4(uint32_t handle, uint32_t bits);  // 0x000195f0
float XB_CDECL ScrActor_GetFloat_7a4(uint32_t handle);  // 0x00019650
void XB_CDECL ScrActor_SetFloat_154(uint32_t handle, uint32_t bits);  // 0x000196c0
float XB_CDECL ScrActor_GetFloat_154(uint32_t handle);  // 0x00019720
void XB_CDECL ScrActor_SetFloat_7e4(uint32_t handle, uint32_t bits);  // 0x00019790
float XB_CDECL ScrActor_GetFloat_7e4(uint32_t handle);  // 0x000197f0
void XB_CDECL ScrActor_SetInt_7e8(uint32_t handle, uint32_t value);  // 0x00019860
uint32_t XB_CDECL ScrActor_GetInt_7e8(uint32_t handle);  // 0x000198c0
void XB_CDECL ScrActor_SetInt_7ec(uint32_t handle, uint32_t value);  // 0x00019920
uint32_t XB_CDECL ScrActor_GetInt_7ec(uint32_t handle);  // 0x00019980
void XB_CDECL ScrActor_SetInt_1308(uint32_t handle, uint32_t value);  // 0x000199e0
uint32_t XB_CDECL ScrActor_GetInt_1308(uint32_t handle);  // 0x00019a40
void XB_CDECL ScrActor_SetString_ec(uint32_t handle, ScriptString value);  // 0x00019aa0
ScriptString* XB_CDECL ScrActor_GetString_ec(ScriptString* out, uint32_t handle);  // 0x00019b20
void XB_CDECL ScrActor_SetInt_f0(uint32_t handle, uint32_t value);  // 0x00019bd0
uint32_t XB_CDECL ScrActor_GetInt_f0(uint32_t handle);  // 0x00019c30
void XB_CDECL ScrActor_SetInt_f4(uint32_t handle, uint32_t value);  // 0x00019c90
uint32_t XB_CDECL ScrActor_GetInt_f4(uint32_t handle);  // 0x00019cf0
void XB_CDECL ScrActor_SetString_f8(uint32_t handle, ScriptString value);  // 0x00019d50
ScriptString* XB_CDECL ScrActor_GetString_f8(ScriptString* out, uint32_t handle);  // 0x00019dd0
void XB_CDECL ScrActor_SetInt_7d8(uint32_t handle, uint32_t value);  // 0x00019e80
uint32_t XB_CDECL ScrActor_GetInt_7d8(uint32_t handle);  // 0x00019ee0
void XB_CDECL ScrActor_SetFloat_130c(uint32_t handle, uint32_t bits);  // 0x00019f40
float XB_CDECL ScrActor_GetFloat_130c(uint32_t handle);  // 0x00019fa0
void XB_CDECL ScrActor_SetRef_1310(uint32_t handle, uint32_t value);  // 0x0001a010
uint32_t* XB_CDECL ScrActor_GetRef_1310(uint32_t* out, uint32_t handle);  // 0x0001a070
void XB_CDECL ScrActor_SetInt_131c(uint32_t handle, uint32_t value);  // 0x0001a0f0
uint32_t XB_CDECL ScrActor_GetInt_131c(uint32_t handle);  // 0x0001a150
void XB_CDECL ScrActor_SetInt_1328(uint32_t handle, uint32_t value);  // 0x0001a1b0
uint32_t XB_CDECL ScrActor_GetInt_1328(uint32_t handle);  // 0x0001a210
void XB_CDECL ScrActor_SetRef_12a0(uint32_t handle, uint32_t value);  // 0x0001a270
uint32_t* XB_CDECL ScrActor_GetRef_12a0(uint32_t* out, uint32_t handle);  // 0x0001a2d0
void XB_CDECL ScrActor_SetInt_cc(uint32_t handle, uint32_t value);  // 0x0001a350
uint32_t XB_CDECL ScrActor_GetInt_cc(uint32_t handle);  // 0x0001a3b0
void XB_CDECL ScrActor_SetByte_1374(uint32_t handle, uint32_t value);  // 0x0001a410
uint32_t XB_CDECL ScrActor_GetByte_1374(uint32_t handle);  // 0x0001a470
void XB_CDECL ScrActor_SetByte_7de(uint32_t handle, uint32_t value);  // 0x0001a4d0
uint32_t XB_CDECL ScrActor_GetByte_7de(uint32_t handle);  // 0x0001a530
void XB_CDECL ScrActor_SetInt_1378(uint32_t handle, uint32_t value);  // 0x0001a590
uint32_t XB_CDECL ScrActor_GetInt_1378(uint32_t handle);  // 0x0001a5f0
void XB_CDECL ScrActor_SetInt_137c(uint32_t handle, uint32_t value);  // 0x0001a650
uint32_t XB_CDECL ScrActor_GetInt_137c(uint32_t handle);  // 0x0001a6b0
void XB_CDECL ScrActor_SetString_1384(uint32_t handle, ScriptString value);  // 0x0001a710
ScriptString* XB_CDECL ScrActor_GetString_1384(ScriptString* out, uint32_t handle);  // 0x0001a790
void XB_CDECL ScrActor_SetString_1388(uint32_t handle, ScriptString value);  // 0x0001a840
ScriptString* XB_CDECL ScrActor_GetString_1388(ScriptString* out, uint32_t handle);  // 0x0001a8c0
void XB_CDECL ScrActor_SetString_138c(uint32_t handle, ScriptString value);  // 0x0001a970
ScriptString* XB_CDECL ScrActor_GetString_138c(ScriptString* out, uint32_t handle);  // 0x0001a9f0
void XB_CDECL ScrActor_SetString_2c0(uint32_t handle, ScriptString value);  // 0x0001aaa0
ScriptString* XB_CDECL ScrActor_GetString_2c0(ScriptString* out, uint32_t handle);  // 0x0001ab20
void XB_CDECL ScrActor_SetInt_184(uint32_t handle, uint32_t value);  // 0x0001abd0
uint32_t XB_CDECL ScrActor_GetInt_184(uint32_t handle);  // 0x0001ac30
void XB_CDECL ScrSentient_SetTeam(uint32_t handle, ScriptString value);  // 0x0001ac90
ScriptString* XB_CDECL ScrSentient_GetTeam(ScriptString* out, uint32_t handle);  // 0x0001ad00
void XB_CDECL ScrSentient_SetInt_28(uint32_t handle, uint32_t value);  // 0x0001ada0
uint32_t XB_CDECL ScrSentient_GetInt_28(uint32_t handle);  // 0x0001ae00
void XB_CDECL ScrSentient_SetFloat_2c(uint32_t handle, uint32_t bits);  // 0x0001ae60
float XB_CDECL ScrSentient_GetFloat_2c(uint32_t handle);  // 0x0001aec0
void XB_CDECL ScrSentient_SetRef_48(uint32_t handle, uint32_t value);  // 0x0001af20
uint32_t* XB_CDECL ScrSentient_GetRef_48(uint32_t* out, uint32_t handle);  // 0x0001af80
void XB_CDECL ScrSentient_SetGoalNode(uint32_t handle, uint32_t value);  // 0x0001b000
uint32_t* XB_CDECL ScrSentient_GetGoalNode(uint32_t* out, uint32_t handle);  // 0x0001b060
void XB_CDECL ScrSentient_SetFloat_18(uint32_t handle, uint32_t bits);  // 0x0001b0e0
float XB_CDECL ScrSentient_GetFloat_18(uint32_t handle);  // 0x0001b140
void XB_CDECL ScrSentient_SetGoalArc(uint32_t handle, uint32_t bits);  // 0x0001b1a0
float XB_CDECL ScrSentient_GetGoalArc(uint32_t handle);  // 0x0001b200
void XB_CDECL ScrSentient_SetRef_3c(uint32_t handle, uint32_t value);  // 0x0001b260
uint32_t* XB_CDECL ScrSentient_GetRef_3c(uint32_t* out, uint32_t handle);  // 0x0001b2c0
void XB_CDECL ScrSentient_SetInt_30(uint32_t handle, uint32_t value);  // 0x0001b340
uint32_t XB_CDECL ScrSentient_GetInt_30(uint32_t handle);  // 0x0001b3a0
void XB_CDECL ScrSentient_SetInt_34(uint32_t handle, uint32_t value);  // 0x0001b400
uint32_t XB_CDECL ScrSentient_GetInt_34(uint32_t handle);  // 0x0001b460
void XB_CDECL ScrSentient_SetInt_38(uint32_t handle, uint32_t value);  // 0x0001b4c0
uint32_t XB_CDECL ScrSentient_GetInt_38(uint32_t handle);  // 0x0001b520

// ScrCall_* (thiscall on an object holding the handle at +0, callee pops the pointer): call the
// accessor through its builtin-table pointer. Getters fill `out` and return it; setters pass
// the value (a new reference for strings) and return the value pointer. ScrCall_ScrEnt_SetInt_370
// is FUN_00119f60 (slot 0x00922b8c = import +0xa4c, the setter at 0x000176b0).
ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetTarget(const uint32_t* self, ScriptString* out);  // 0x00119a80, through 0x00922aa0
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetTargetname(const uint32_t* self, ScriptString* out);  // 0x00119b70, through 0x00922ae0
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetTarget(const uint32_t* self, ScriptString* out);  // 0x00119be0, through 0x00922ae8
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetScriptNoteworthy(const uint32_t* self, ScriptString* out);  // 0x00119c90, through 0x00922b10
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetClassname(const uint32_t* self, ScriptString* out);  // 0x00119d00, through 0x00922b18
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetModel(const uint32_t* self, ScriptString* out);  // 0x00119de0, through 0x00922b28
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetTarget(const uint32_t* self, ScriptString* out);  // 0x00119e50, through 0x00922b48
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetTargetname(const uint32_t* self, ScriptString* out);  // 0x00119ef0, through 0x00922b50
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetGroupname(const uint32_t* self, ScriptString* out);  // 0x0011a030, through 0x00922c18
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetScriptNoteworthy(const uint32_t* self, ScriptString* out);  // 0x0011a0d0, through 0x00922c28
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetAnimname(const uint32_t* self, ScriptString* out);  // 0x0011a170, through 0x00922c40
ScriptString* XB_THISCALL ScrCall_ScrSentient_GetTeam(const uint32_t* self, ScriptString* out);  // 0x0011b2d0, through 0x00922dc8
ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetScriptNoteworthy(const uint32_t* self, ScriptString* out);  // 0x0011b9a0, through 0x00922ab0
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_ec(const uint32_t* self, ScriptString* out);  // 0x0011cae0, through 0x00922d28
ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetTargetname(const uint32_t* self, ScriptString* out);  // 0x0011cd30, through 0x00922a98
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_1384(const uint32_t* self, ScriptString* out);  // 0x0011f740, through 0x00922da0
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_2c0(const uint32_t* self, ScriptString* out);  // 0x0011f7b0, through 0x00922db8
uint32_t* XB_THISCALL ScrCall_ScrPathNode_GetOrigin(const uint32_t* self, uint32_t* out);  // 0x00119af0, through 0x00922ab8
uint32_t* XB_THISCALL ScrCall_ScrPathNode_GetAngles(const uint32_t* self, uint32_t* out);  // 0x00119b30, through 0x00922ac0
uint32_t* XB_THISCALL ScrCall_ScrVehNode_GetOrigin(const uint32_t* self, uint32_t* out);  // 0x00119c50, through 0x00922af0
uint32_t* XB_THISCALL ScrCall_ScrEnt_GetOrigin(const uint32_t* self, uint32_t* out);  // 0x00119da0, through 0x00922b20
uint32_t* XB_THISCALL ScrCall_ScrEnt_GetAngles(const uint32_t* self, uint32_t* out);  // 0x00119ff0, through 0x00922ba0
uint32_t* XB_THISCALL ScrCall_ScrVehNode_GetAngles(const uint32_t* self, uint32_t* out);  // 0x0011b240, through 0x00922af8
const ScrVec3* XB_THISCALL ScrCall_ScrEnt_SetOrigin(const uint32_t* self, const ScrVec3* value);  // 0x00119d70, through 0x00922b24
const ScrVec3* XB_THISCALL ScrCall_ScrEnt_SetAngles(const uint32_t* self, const ScrVec3* value);  // 0x00119fc0, through 0x00922ba4
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetTargetname(const uint32_t* self, const ScriptString* value);  // 0x00119ec0, through 0x00922b54
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetScriptNoteworthy(const uint32_t* self, const ScriptString* value);  // 0x0011a0a0, through 0x00922c2c
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetAnimname(const uint32_t* self, const ScriptString* value);  // 0x0011a140, through 0x00922c44
const ScriptString* XB_THISCALL ScrCall_ScrActor_SetString_ec(const uint32_t* self, const ScriptString* value);  // 0x0011a240, through 0x00922d2c
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetGroupname(const uint32_t* self, const ScriptString* value);  // 0x0011b2a0, through 0x00922c1c
const uint32_t* XB_THISCALL ScrCall_ScrEnt_SetInt_370(const uint32_t* self, const uint32_t* value);  // 0x00119f60, through 0x00922b8c
const uint32_t* XB_THISCALL ScrCall_ScrEnt_SetHealth(const uint32_t* self, const uint32_t* value);  // 0x00119f80, through 0x00922b94
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetFloat_7e4(const uint32_t* self, const uint32_t* value);  // 0x0011a1e0, through 0x00922d0c
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_7e8(const uint32_t* self, const uint32_t* value);  // 0x0011a200, through 0x00922d14
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1308(const uint32_t* self, const uint32_t* value);  // 0x0011a220, through 0x00922d24
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1328(const uint32_t* self, const uint32_t* value);  // 0x0011a270, through 0x00922d6c
const uint32_t* XB_THISCALL ScrCall_ScrSentient_SetFloat_18(const uint32_t* self, const uint32_t* value);  // 0x0011a290, through 0x00922df4
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1378(const uint32_t* self, const uint32_t* value);  // 0x0011b810, through 0x00922d94
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_184(const uint32_t* self, const uint32_t* value);  // 0x0011f820, through 0x00922dc4

// Builtin wrappers (cdecl), 0x0011a2e0..0x0011a860: unwrap handle objects (pointers to the
// handle), add the builtin's fixed arguments (zeros) and return its result - through `out` for
// returned handles (GetNode: -1 becomes 0). LinkTo/IsTouching are tail jumps (the handles go into
// the wrapper's argument slots). RandomFloatRange/DistanceSquared pass the x87 result on;
// EntityHandleIsValid is SETNE AL over the builtin's EAX.
uint32_t* XB_CDECL ScrCall_GScr_GetEntArray(uint32_t* out, uint32_t key, uint32_t value, uint32_t array);
uint32_t* XB_CDECL ScrCall_GScr_GetNode(uint32_t* out, uint32_t key, uint32_t value);
uint32_t* XB_CDECL ScrCall_GScr_GetVehicleNode(uint32_t* out, uint32_t key, uint32_t value);
uint32_t* XB_CDECL ScrCall_GScr_SpawnVehicle(uint32_t* out, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                                             uint32_t f);
uint32_t* XB_CDECL ScrCall_GScr_Spawn(uint32_t* out, uint32_t a, uint32_t b, uint32_t c);
uint32_t XB_CDECL ScrCall_GScr_PlaySound(const uint32_t* ent, uint32_t sound);
uint32_t XB_CDECL ScrCall_GScr_LinkTo(const uint32_t* ent, const uint32_t* parent);
uint32_t XB_CDECL ScrCall_GScr_IsTouching(const uint32_t* ent, const uint32_t* other);
uint32_t* XB_CDECL ScrCall_GScr_ForceSpawn(uint32_t* out, const uint32_t* spawner, uint32_t flag);
uint32_t* XB_CDECL ScrCall_GScr_UseBy(uint32_t* out, const uint32_t* ent, const uint32_t* user);
long double XB_CDECL ScrCall_Scr_RandomFloatRange(uint32_t low, uint32_t high);
long double XB_CDECL ScrCall_Scr_DistanceSquared(uint32_t a, uint32_t b);
uint32_t XB_CDECL ScrCall_GScr_ObjectiveAddLocalizedWithPosition(uint32_t index, uint32_t state, uint32_t text,
                                                                 uint32_t position);
uint32_t XB_CDECL ScrCall_Scr_QueueNotify(const uint32_t* ent, const char* name);
uint32_t XB_CDECL ScrCall_GScr_PlaySoundWithNotify(const uint32_t* ent, uint32_t sound, uint32_t notify);
uint32_t XB_CDECL ScrCall_Scr_EntityHandleIsValid(const uint32_t* ent);

// More builtin wrappers (0x0011a460..0x0011d010). Vector results go through a local the wrapper
// fills with the default vector (0x00922eb8) first and copies out whole (GetPersistentVector
// copies from the returned pointer instead). ShakeRumbleNamed takes its string by value and
// releases it after the builtin; the anim-knob wrappers add the builtin's trailing 1.
uint32_t XB_CDECL ScrCall_GScr_SetFlaggedAnimKnobAll(const uint32_t* ent, const uint32_t* anim, uint32_t a, uint32_t b,
                                                     uint32_t c, uint32_t d, uint32_t e);  // 0x0011a460
uint32_t* XB_CDECL ScrCall_GScr_GetTagOrigin(uint32_t* out, const uint32_t* ent, uint32_t tag);  // 0x0011a4a0
uint32_t* XB_CDECL ScrCall_GScr_GetTagAngles(uint32_t* out, const uint32_t* ent, uint32_t tag);  // 0x0011a500
ScriptString* XB_CDECL ScrCall_GScr_GetLocalizedString(ScriptString* out, uint32_t key);  // 0x0011a560
uint32_t* XB_CDECL ScrCall_Scr_VectorToAngles(uint32_t* out, uint32_t vec);  // 0x0011a610
uint32_t* XB_CDECL ScrCall_Scr_AnglesToForward(uint32_t* out, uint32_t angles);  // 0x0011a660
ScriptString* XB_CDECL ScrCall_GScr_GetCvar(ScriptString* out, uint32_t name);  // 0x0011a6b0
uint32_t* XB_CDECL ScrCall_Scr_VectorNormalize(uint32_t* out, uint32_t vec);  // 0x0011b340
uint32_t* XB_CDECL ScrCall_GScr_GetPersistentVector(uint32_t* out, uint32_t key);  // 0x0011b390
ScriptStringRep* XB_CDECL ScrCall_GScr_ShakeRumbleNamed(uint32_t a, uint32_t b, ScriptString name,
                                                        uint32_t c);  // 0x0011b3d0
uint32_t XB_CDECL ScrCall_GScr_GetZoneAtPoint(const uint32_t* point);  // 0x0011b430
// 0x0011b450 (gap, m5 recording 11-12): GScr_ZoneSetField34 (zone, 0) through its builtin slot, then waits on the zone.
void XB_CDECL ScrCall_GScr_ZoneSetField34AndWait(uint32_t zone);
uint32_t XB_CDECL ScrCall_GScr_PlaySoundWithNotify_2(const uint32_t* ent, uint32_t sound);  // 0x0011b4b0
uint32_t* XB_CDECL ScrCall_GScr_GetOrigin(uint32_t* out, const uint32_t* ent);  // 0x0011ba30
uint32_t XB_CDECL ScrCall_GScr_AmbientPlay(uint32_t sound);  // 0x0011bff0
uint32_t XB_CDECL ScrCall_GScr_SetAnimKnobAll(const uint32_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                              uint32_t e);  // 0x0011d010
// 0x0011ba10: through 0x00922d74 (ScrCall_* setter, as above).
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetRef_12a0(const uint32_t* self, const uint32_t* value);
// 0x0011f840 / 0x0011f870 / 0x0011f8a0: anim builtins with the trailing 0 / 0 / 1.
uint32_t XB_CDECL ScrCall_GScr_SetAnimKnob(const uint32_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
uint32_t XB_CDECL ScrCall_GScr_SetAnim(const uint32_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
uint32_t XB_CDECL ScrCall_GScr_SetFlaggedAnimKnob(const uint32_t* ent, const uint32_t* anim, uint32_t a, uint32_t b,
                                                  uint32_t c, uint32_t d);
// 0x0011f8d0 / 0x0011f960 / 0x0011f9d0: string results.
ScriptString* XB_CDECL ScrCall_GScr_Vehicle_GetWheelSurface(ScriptString* out, const uint32_t* vehicle, uint32_t wheel);
ScriptString* XB_CDECL ScrCall_GScr_GetWeaponClassname(ScriptString* out, uint32_t weapon);
ScriptString* XB_CDECL ScrCall_GScr_GetWeaponClassname2(ScriptString* out, uint32_t weapon);
// 0x0011fa40 (thiscall, pops 4): {handle, false} in self, targetname "gunner" on the entity.
uint32_t* XB_THISCALL ScrCall_ScrEnt_SetTargetname_2(uint32_t* self, uint32_t handle);

// 0x00181450 (thiscall, pops 4): ScrActor_GetString_f8 through 0x00922d40, copied out.
ScriptString* XB_THISCALL ScrActor_GetString_f8_Copy(const uint32_t* self, ScriptString* out);
// 0x001815a0: string result of GScr_GetWeaponDisplayName (0x009223e4).
ScriptString* XB_CDECL ScrCall_GScr_GetWeaponDisplayName(ScriptString* out, uint32_t weapon);
// 0x00181530 / 0x00181560: SetFlaggedAnimKnob / SetFlaggedAnimKnobAll with 0 as the last argument.
uint32_t XB_CDECL ScrCall_SetFlaggedAnimKnobByRef(const uint32_t* ent, const uint32_t* anim, uint32_t a, uint32_t b,
                                                  uint32_t c, uint32_t d);
uint32_t XB_CDECL ScrCall_GScr_SetFlaggedAnimKnobAll_2(const uint32_t* ent, const uint32_t* anim, uint32_t a,
                                                       uint32_t b, uint32_t c, uint32_t d, uint32_t e);
// 0x00182060 / 0x001837e0: release a by-value string argument.
void XB_CDECL ScrLocal_ReleaseString_2(ScriptString value);
void XB_CDECL ScrLocal_ReleaseString(uint32_t, uint32_t, ScriptString value);
// 0x001e45f0 (thiscall, pops 4; asm entry): the path node's type string into out.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ScrPathNode_CopyType_c(uint32_t entry_esp, uint32_t ecx);
extern "C" void ScrPathNode_CopyType_entry(void);

// Field hooks (0x002f4ca0..0x002fb012; cdecl (object, field offset, value address)) and plain
// actor accessors (0x002f87b0, 0x002f8a00).
void XB_CDECL ScrSentient_SetFloat18Field(uint8_t* object, uint32_t, const float* value);
void XB_CDECL ScrSentient_GetTeamField(const uint8_t* object, uint32_t, ScriptString* out);
void XB_CDECL ScrPathNode_GetTypeField(const uint8_t* node, uint32_t, ScriptString* out);
void XB_CDECL ScrPathNode_SetAnglesField(uint8_t* node, uint32_t, const uint32_t* value);
void XB_CDECL ScrActor_GetString2c0Field(const uint8_t* actor, uint32_t, ScriptString* out);
void XB_CDECL ScrSentient_SetGoalArcField(uint8_t* object, uint32_t, const float* value);
void XB_CDECL ScrSentient_GetRef3cField(const uint8_t* object, uint32_t, uint32_t* out);
void XB_CDECL ScrPathNode_GetAnglesField(const uint8_t* node, uint32_t, uint32_t* out);
void XB_CDECL ScrActor_GetRef12a0Field(const uint8_t* object, uint32_t, uint32_t* out);
void XB_CDECL ScrActor_SetInt184Field(uint8_t* actor, uint32_t, const uint32_t* value);
uint32_t XB_CDECL ScrActor_GetByte_3b1(uint32_t handle);
void XB_CDECL ScrActor_SetInt_1370(uint32_t handle, uint32_t value);
void XB_CDECL ScrSentient_SetTeamFromString(uint8_t* object, uint32_t, const ScriptString* value);
void XB_CDECL ScrActor_SetRef12a0Field(uint8_t* actor, uint32_t, const uint32_t* value);
void XB_CDECL ScrEnt_GetModelField(const uint8_t* ent, uint32_t, ScriptString* out);

// 0x002fb020..0x002fb1c0, 0x002fd3c0: entity name/angles/health field hooks, the sentient's path node.
void XB_CDECL ScrEnt_SetAnimnameField(uint8_t* ent, uint32_t, const ScriptString* value);
void XB_CDECL ScrEnt_SetScriptNoteworthyField(uint8_t* ent, uint32_t, const ScriptString* value);
void XB_CDECL ScrEnt_SetTargetField(uint8_t* ent, uint32_t, const ScriptString* value);
void XB_CDECL ScrEnt_SetTargetnameField(uint8_t* ent, uint32_t, const ScriptString* value);
void XB_CDECL ScrEnt_SetGroupnameField(uint8_t* ent, uint32_t, const ScriptString* value);
void XB_CDECL ScrEnt_SetAnglesField(uint8_t* ent, uint32_t, const uint32_t* value);
void XB_CDECL ScrEnt_SetHealthField(uint8_t* ent, uint32_t, const int32_t* value);
void XB_CDECL ScrSentient_GetPathNode48Field(const uint8_t* object, uint32_t, uint32_t* out);

// 0x00302a00: the entity origin field hook.
void XB_CDECL ScrEnt_SetOriginField(uint8_t* ent, uint32_t, const uint32_t* value);

}  // namespace game
