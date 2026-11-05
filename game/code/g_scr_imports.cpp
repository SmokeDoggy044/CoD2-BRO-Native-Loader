#include "g_scr_imports.h"

#include "title_data.h"

namespace game {

// The tables below are generated from the disassembly (store order kept): {offset, function address}.

namespace {

// One word of the game import block (pointer at 0x0095f49c): the script and engine functions the level script calls.
struct ImportSlot {
    uint32_t offset;
    uint32_t value;
};

// Each store re-reads the block pointer, as the original does.
void set_imports(const ImportSlot* slots, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        *reinterpret_cast<uint32_t*>(title_global<uint8_t*>(0x0095f49c) + slots[i].offset) = slots[i].value;
}

void copy_level_imports() {
    uint32_t* const out = reinterpret_cast<uint32_t*>(title_global<uint8_t*>(0x0095f49c) + 0x800);
    const uint32_t* const in = reinterpret_cast<const uint32_t*>(uintptr_t{0x009270d0});  // g_levelScriptImports
    for (uint32_t i = 0; i < 0x27; ++i) out[i] = in[i];  // REP MOVSD, front to back
}

const ImportSlot kObjectiveImports[] = {
    {0x15c, 0x002fef50},  // GScr_ObjectiveAddNoText
    {0x160, 0x002feea0},  // GScr_ObjectiveAddIntText
    {0x164, 0x002fec10},  // GScr_ObjectiveAdd
    {0x168, 0x002feb50},  // GScr_ObjectiveAddIntTextWithPosition
    {0x16c, 0x002fe6f0},  // GScr_ObjectiveAddWithPosition
    {0x170, 0x002fee50},  // GScr_ObjectiveAddLocalized
    {0x174, 0x002feaf0},  // GScr_ObjectiveAddLocalizedWithPosition
    {0x178, 0x002fc8a0},  // GScr_ObjectiveDelete
    {0x17c, 0x002fe5e0},  // GScr_ObjectiveState
    {0x180, 0x002ffb70},  // GScr_ObjectiveStringLocalized
    {0x184, 0x00300ba0},  // GScr_ObjectiveStringByHash
    {0x188, 0x002ffb50},  // GScr_ObjectiveString
    {0x18c, 0x002fc7f0},  // GScr_ObjectivePosition
    {0x190, 0x002fe250},  // GScr_ObjectiveCurrent
    {0x194, 0x002fc710},  // GScr_ObjectiveToggleRing
};

const ImportSlot kPlayerImports[] = {
    {0x684, 0x002fcda0},  // GScr_PlayerGiveWeapon
    {0x688, 0x002fcce0},  // GScr_PlayerGiveWeaponWithAmmo
    {0x68c, 0x002f8360},  // GScr_PlayerTakeWeapon
    {0x690, 0x002f82a0},  // GScr_PlayerTakeAllWeapons
    {0x694, 0x002f8230},  // GScr_PlayerGetCurrentWeapon
    {0x698, 0x002f80d0},  // GScr_PlayerHasWeapon
    {0x69c, 0x002f8160},  // GScr_PlayerGetWeaponTotalAmmo
    {0x6a0, 0x002ffc00},  // GScr_PlayerSwitchToWeapon
    {0x6a4, 0x002fcc20},  // GScr_PlayerGiveMaxAmmo
    {0x6a8, 0x002fcb60},  // GScr_PlayerGiveMaxAmmoByAmmoType
    {0x6ac, 0x002f8000},  // GScr_GetWeaponAmmoFraction
    {0x6b0, 0x002f7ef0},  // GScr_PlayerGetWeaponMaxAmmoFraction
    {0x6b4, 0x002fcb20},  // GScr_PlayerSetOrigin
    {0x6b8, 0x002f7e90},  // GScr_PlayerSetVec_10
    {0x6bc, 0x002fcae0},  // GScr_PlayerSetAngles
    {0x6c0, 0x002f7e20},  // GScr_PlayerGetAngles
    {0x6c4, 0x002f7dd0},  // GScr_PlayerGetFlag40_594
    {0x6c8, 0x002f7d80},  // GScr_PlayerGetFlag1_594
    {0x6cc, 0x002f7d30},  // GScr_PlayerGetFlag20_594
    {0x6d0, 0x002f7ce0},  // GScr_PlayerGetFlag20_2c
    {0x6d4, 0x002fca90},  // GScr_PlayerGetBool_60
    {0x6d8, 0x00301ee0},  // GScr_PlayerSetViewModel
    {0x6dc, 0x002f7c80},  // GScr_PlayerAllowEFlag100000
    {0x6e0, 0x002f7c20},  // GScr_PlayerAllowPmFlag80000
    {0x6e4, 0x002f7bc0},  // GScr_PlayerAllowPmFlag100000
    {0x6e8, 0x002f7b60},  // GScr_PlayerAllowPmFlag200000
    {0x6ec, 0x002f7b00},  // GScr_PlayerAllowPmFlag400000
    {0x6f0, 0x002f7aa0},  // GScr_PlayerAllowPmFlag800000
    {0x6f4, 0x002f4b30},  // GScr_ReturnZero445
    {0x6f8, 0x002f4b20},  // GScr_ReturnZero446
    {0x6fc, 0x002fca80},  // GScr_ClosePopup
    {0x700, 0x002f4b10},  // GScr_EmptyStub448
    {0x704, 0x002f7a50},  // GScr_PlayerSetInt_58c
    {0x708, 0x002f4b00},  // GScr_SetFreezeControls
    {0x70c, 0x002fca10},  // GScr_ToggleNoclip
    {0x710, 0x002fc9d0},  // GScr_SetReverb
    {0x714, 0x002f79d0},  // GScr_PlayerIsLookingAt
    {0x718, 0x002ffb90},  // GScr_PlayLocalSound
    {0x71c, 0x002f7980},  // GScr_PlayerSetNotFlag_630
    {0x720, 0x002f78a0},  // GScr_PlayerGetWeaponSlotWeapon
    {0x724, 0x002f76c0},  // GScr_CheckWeaponSlot
    {0x728, 0x002f75e0},  // GScr_PlayerGetWeaponSlotAmmo
    {0x72c, 0x002f74a0},  // GScr_PlayerSetWeaponSlotAmmo
    {0x730, 0x002f73c0},  // GScr_PlayerGetWeaponSlotClipAmmo
    {0x734, 0x002f72d0},  // GScr_PlayerSetWeaponSlotClipAmmo
    {0x738, 0x002f7210},  // GScr_PlayerGetWeaponSlotClipSize
};

const ImportSlot kMoverImports[] = {
    {0x654, 0x00302ee0},  // GScr_MoveTo
    {0x658, 0x00303a70},  // GScr_MoveX
    {0x65c, 0x00303a40},  // GScr_MoveY
    {0x660, 0x00303a10},  // GScr_MoveZ
    {0x664, 0x00302e60},  // GScr_MoverMoveGravity
    {0x668, 0x00302d60},  // GScr_RotateTo
    {0x66c, 0x003039e0},  // GScr_RotatePitch
    {0x670, 0x003039b0},  // GScr_RotateYaw
    {0x674, 0x00303980},  // GScr_RotateRoll
    {0x678, 0x00302c90},  // GScr_RotateVelocity
    {0x67c, 0x003026c0},  // GScr_MoverSolid
    {0x680, 0x00302640},  // GScr_MoverNotSolid
};

const ImportSlot kVehicleImports[] = {
    {0x73c, 0x00307090},  // GScr_Vehicle_AttachPath
    {0x740, 0x002f71a0},  // GScr_Vehicle_StartPath
    {0x744, 0x002f7120},  // GScr_Vehicle_SetSwitchNode
    {0x748, 0x002f70b0},  // GScr_Vehicle_SetWaitNode
    {0x74c, 0x002f7040},  // GScr_Vehicle_SetMaxSpeed
    {0x750, 0x002f6fb0},  // GScr_Vehicle_SetSpeed
    {0x754, 0x002f6f40},  // GScr_Vehicle_SetSpeedMode2
    {0x758, 0x00302a80},  // GScr_Vehicle_PushFromPoint
    {0x75c, 0x00307010},  // GScr_Vehicle_Kill
    {0x760, 0x002f6e20},  // GScr_Vehicle_GetWheelSurface
    {0x764, 0x002f6de0},  // GScr_Vehicle_GetSpeedMPH
    {0x768, 0x002f6d70},  // GScr_Vehicle_GetOwnerHandle
    {0x76c, 0x002f6d30},  // GScr_Vehicle_Set200
    {0x770, 0x002f6cf0},  // GScr_Vehicle_Clear200
    {0x774, 0x002f6ca0},  // GScr_Vehicle_MakeUnusable
    {0x778, 0x002f6c10},  // GScr_Vehicle_MakeUsable
    {0x77c, 0x003053c0},  // GScr_Vehicle_EjectDriver
    {0x780, 0x002f6bc0},  // GScr_Vehicle_Set190
    {0x784, 0x002f6b80},  // GScr_Vehicle_Clear190
    {0x788, 0x002f6ac0},  // GScr_Vehicle_SetTurretTargetVec
    {0x78c, 0x002f69d0},  // GScr_Vehicle_SetTurretTargetEnt
    {0x790, 0x002f6950},  // GScr_Vehicle_ClearTurretTarget
    {0x794, 0x00308150},  // GScr_Vehicle_FireWeapon
    {0x798, 0x002f68f0},  // GScr_Vehicle_Is180Clear
};

const ImportSlot kActorImports[] = {
    {0x540, 0x002fffa0},  // GScr_SetGoalPosAndYaw
    {0x544, 0x002fff10},  // GScr_SetGoalPos
    {0x548, 0x002ffe40},  // GScr_SetGoalPosFromNode
    {0x54c, 0x00307c10},  // GScr_SetGoalEntity
    {0x550, 0x002f9080},  // GScr_SentientNearestEnemyDist
    {0x554, 0x00307cc0},  // GScr_AnimScriptedFlag0
    {0x558, 0x00308a90},  // GScr_ActorShootAt
    {0x55c, 0x00308a30},  // GScr_ActorShoot
    {0x560, 0x00307300},  // GScr_ActorMelee
    {0x564, 0x002f9020},  // Actor_CallStateSlot5
    {0x568, 0x002f8fa0},  // GScr_ActorSetAimParams
    {0x56c, 0x00304880},  // GScr_ActorBlendAimToPoint
    {0x570, 0x003055b0},  // GScr_ActorEnterProne
    {0x574, 0x002f8f50},  // GScr_ActorExitProne
    {0x578, 0x002f8ed0},  // GScr_ActorSetPitchBlendAnims
    {0x57c, 0x00306e90},  // GScr_ActorSetAimAnimWeights
    {0x580, 0x00300e20},  // GScr_ActorSetLookAnims
    {0x590, 0x00306db0},  // GScr_ActorCanShootWithOffset
    {0x594, 0x003072d0},  // GScr_ActorCanShoot
    {0x598, 0x00307ed0},  // GScr_ActorCanSee
    {0x59c, 0x003080e0},  // GScr_ActorCanSeeDefaultAge
    {0x5a0, 0x00308070},  // GScr_ActorCanSeeEnemy
    {0x5a4, 0x00308010},  // GScr_ActorCanSeeEnemyDefaultAge
    {0x5a8, 0x00308c50},  // GScr_ActorFindBetterCover
    {0x5ac, 0x00306d50},  // GScr_ActorGetNearestNode
    {0x5b0, 0x00306b60},  // GScr_DropWeapon
    {0x5b4, 0x00306740},  // GScr_ActorIsSafeMovePoint
    {0x5b8, 0x003063c0},  // GScr_ActorTryTeleport
    {0x5bc, 0x002fd100},  // GScr_ActorBeginCriticalSection
    {0x5c0, 0x002f8e70},  // GScr_ActorEndCriticalSection
    {0x5c4, 0x002f8e00},  // GScr_ActorInCriticalSection
    {0x5c8, 0x002f8da0},  // GScr_GetPathRemainingLength
    {0x5cc, 0x002f8d50},  // GScr_ActorPathHasFlag1
    {0x5d0, 0x002f8c20},  // GScr_ActorAllowedStances
    {0x5d4, 0x002fcf80},  // GScr_ActorIsStanceAllowed
    {0x5d8, 0x002fcf20},  // GScr_ActorStartReacquire
    {0x5dc, 0x002f8bc0},  // GScr_ActorHasPendingSentientSlot
    {0x5e0, 0x00307950},  // GScr_CheckGrenadeThrow
    {0x5e4, 0x00307720},  // GScr_ActorThrowGrenade
    {0x5e8, 0x00305560},  // GScr_ActorGrabGrenade
    {0x5ec, 0x00307690},  // GScr_ActorUseTurret
    {0x5f0, 0x00303b70},  // GScr_ActorLeaveTurret
    {0x5f4, 0x00307250},  // GScr_ActorCanUseTurret
    {0x5f8, 0x002f8b20},  // GScr_ActorSetTraverseMode
    {0x5fc, 0x002f8a40},  // GScr_ActorSetAnimMode
    {0x600, 0x00300bf0},  // GScr_ActorSetOrientMode
    {0x604, 0x002ffd40},  // GScr_ActorGetMotionYaw
    {0x608, 0x002f8a00},  // ScrActor_SetInt_1370
    {0x60c, 0x002f89a0},  // GScr_ActorGetTurret
    {0x610, 0x002f4bc0},  // Sys_DebugBreak
    {0x614, 0x002f4bb0},  // Sys_DebugBreak
    {0x618, 0x002f87f0},  // GScr_ActorTeleport
    {0x61c, 0x00308f20},  // GScr_ActorApplyAnimMove
    {0x620, 0x00302be0},  // GScr_ActorAdvanceAnimTime
    {0x624, 0x002fced0},  // GScr_ActorGetObstacleKind_39c
    {0x628, 0x002ffcb0},  // GScr_ActorGetObstacleYaw_3a8
    {0x62c, 0x002fce80},  // GScr_ActorGetObstacleKind_2b0
    {0x630, 0x002f87b0},  // ScrActor_GetByte_3b1
    {0x634, 0x002f4ba0},  // GScr_EmptyStub397
    {0x638, 0x002f87a0},  // GScr_EmptyStub398
    {0x63c, 0x002f4b90},  // GScr_EmptyStub399
    {0x640, 0x00306310},  // GScr_ActorNodeCanSeeEnemyNode
    {0x644, 0x003062a0},  // GScr_ActorUpdateNearestNode
    {0x648, 0x002f8480},  // GScr_ActorGetPathNodeOrOrigin
    {0x64c, 0x00305470},  // GScr_ActorMayMoveToPoint
    {0x650, 0x002f8420},  // GScr_ActorSetFlag2000000
    {0x584, 0x002f8710},  // GScr_ActorLookAt
    {0x588, 0x002f8540},  // GScr_ActorSetLookRates
    {0x58c, 0x002f86b0},  // GScr_ActorStopLookAt
    {0x8bc, 0x002f4b80},  // GScr_EmptyStub559
    {0x8c0, 0x002f4b70},  // GScr_EmptyStub560
    {0x8c4, 0x002f4b60},  // GScr_EmptyStub561
};

const ImportSlot kGameImports[] = {
    {0x084, 0x00307d40},  // GScr_GetEntArray
    {0x8a8, 0x002fb230},  // GScr_GetEntScriptObject
    {0x8ac, 0x002fb2a0},  // GScr_GetLevelEnt8Handle
    {0x8b0, 0x002fb290},  // GScr_GetPlayerHandle
    {0x088, 0x002fafb0},  // GScr_GetEntByNum
    {0x08c, 0x002faf40},  // GScr_GetNode
    {0x090, 0x002faf10},  // GScr_GetVehicleNode
    {0x094, 0x002fd650},  // Vehicle_CountActive
    {0x098, 0x002faf00},  // GScr_EmptyStub38
    {0x09c, 0x00305f00},  // GScr_PlaySound
    {0x0a0, 0x00305e70},  // GScr_PlaySoundAtPosition
    {0x0a4, 0x00305de0},  // GScr_PlayLoopSound
    {0x0a8, 0x00305d80},  // GScr_PlaySoundMode7
    {0x0ac, 0x002faef0},  // GScr_StopSound
    {0x0b0, 0x002fd630},  // GScr_IsSoundPlaying
    {0x0b4, 0x002ff3c0},  // GScr_FreeSound
    {0x0b8, 0x002faed0},  // GScr_SetSoundParam
    {0x0bc, 0x00305f90},  // GScr_PlayerWeaponSound
    {0x0c0, 0x002f5c90},  // GScr_EntList190_Enable
    {0x0c4, 0x002f5c70},  // GScr_EntList190_Disable
    {0x0c8, 0x002f4ea0},  // GScr_Set95f4b4
    {0x0cc, 0x00300390},  // GScr_PlaySoundWithNotify
    {0x0d0, 0x002fae30},  // GScr_IsAlive
    {0x0d4, 0x002fae90},  // Scr_EntityHandleIsValid
    {0x0d8, 0x002fadf0},  // GScr_IsPlayer
    {0x0dc, 0x002fadb0},  // GScr_IsAI
    {0x0e0, 0x002fad70},  // GScr_IsSentient
    {0x0e4, 0x002f5c50},  // GScr_IsValidHandle
    {0x0e8, 0x002f5c40},  // GScr_IsNotMinusOne
    {0x2d4, 0x003056c0},  // GScr_LocationalTraceSurface
    {0x2d8, 0x00303600},  // GScr_TraceSurfaceType
    {0x1e0, 0x00300140},  // GScr_SaveCheckpoint
    {0x1e8, 0x002f5830},  // GScr_GetPersistentVector
    {0x1ec, 0x002f5920},  // GScr_GetPersistentInt
    {0x1f0, 0x002f58d0},  // GScr_GetPersistentFloat
    {0x1f4, 0x002f5960},  // GScr_SetPersistentVector
    {0x1f8, 0x002f59d0},  // GScr_SetPersistentInt
    {0x1fc, 0x002f59a0},  // GScr_SetPersistentFloat
    {0x1e4, 0x00301640},  // GScr_RestorePlayerLoadout
    {0x200, 0x002f4bf0},  // GScr_Set8db198
    {0x204, 0x002f4be0},  // GScr_Set8db19c
    {0x208, 0x002f9550},  // GScr_SetForcedLod
    {0x268, 0x002f5a60},  // GScr_GetSpgVariant
    {0x26c, 0x002f9260},  // GScr_SetEnt250Field_f4
    {0x270, 0x00301f60},  // GScr_SwapEntField_dc
    {0x274, 0x002fd1e0},  // GScr_ShakeRumble
    {0x278, 0x002fd2d0},  // GScr_ShakeRumbleNamed
    {0x27c, 0x003000e0},  // GScr_SetIntMapValue
    {0x280, 0x00300090},  // GScr_GetIntMapValue
    {0x20c, 0x002f9510},  // GScr_SetEntFlag2000000
    {0x210, 0x002f5a30},  // GScr_HudObjectives_Set13d0
    {0x214, 0x002f5a10},  // GScr_SuppressObjectiveUpdateMessage
    {0x218, 0x002f94c0},  // GScr_HasVisibleModel
    {0x21c, 0x002f59f0},  // GScr_Set964b84
    {0x220, 0x002fefa0},  // GScr_SetHudMode
    {0x224, 0x002fef80},  // GScr_HudSetTextByHash
    {0x228, 0x002f94a0},  // GScr_HudSetMeterCount
    {0x22c, 0x002f9480},  // GScr_HudComp24_SetE0
    {0x230, 0x002f9460},  // GScr_HudComp24_SetEcTwo
    {0x234, 0x002f9410},  // GScr_HudComp24_Push
    {0x238, 0x002f93f0},  // GScr_HudComp24_Clear
    {0x23c, 0x002fd3a0},  // GScr_HudSetComponentColor
    {0x240, 0x002f93e0},  // GScr_HudComp20_Set10
    {0x244, 0x002f93d0},  // GScr_HudComp20_Enable
    {0x248, 0x002f93c0},  // GScr_HudComp20_Disable
    {0x24c, 0x002f93a0},  // GScr_HudComp20_EnableWith
    {0x250, 0x002f9380},  // GScr_HudComp1c_SetMode0
    {0x254, 0x002f9360},  // GScr_HudComp1c_SetMode1
    {0x258, 0x002f9340},  // GScr_HudComp1c_SetMode2
    {0x25c, 0x002f9320},  // GScr_HudComp1c_Off
    {0x260, 0x00308af0},  // GScr_SetAIFlags
    {0x264, 0x002f92b0},  // GScr_SetEntFlag20000000
    {0x380, 0x00307cf0},  // GScr_AnimScriptedNoNotify
    {0x384, 0x00307460},  // GScr_AnimScripted
    {0x388, 0x00301ba0},  // GScr_StopAnimScripted
    {0x38c, 0x003051c0},  // GScr_Attach
    {0x390, 0x003056a0},  // GScr_AttachWithOwner
    {0x394, 0x00305660},  // GScr_AttachNoTag
    {0x398, 0x00305150},  // GScr_Detach
    {0x39c, 0x00305630},  // GScr_DetachNoTag
    {0x3a0, 0x00305110},  // GScr_DetachAll
    {0x3a4, 0x002fad20},  // GScr_GetAttachSize
    {0x3a8, 0x002facd0},  // GScr_GetAttachModelName
    {0x3ac, 0x002fac80},  // GScr_GetAttachTagName
    {0x3b0, 0x002fac30},  // GScr_TestEntBit299
    {0x4bc, 0x002fd570},  // GScr_CheckScriptAnimTreeCvars
    {0x3b4, 0x003034e0},  // GScr_LinkToTagWithOffset
    {0x3b8, 0x00303400},  // GScr_LinkToTag
    {0x3bc, 0x00303330},  // GScr_LinkTo
    {0x3c0, 0x00303260},  // GScr_PlayerLinkTo
    {0x3c4, 0x003031d0},  // GScr_LinkToTagUnchecked
    {0x3c8, 0x00303150},  // GScr_LinkToUnchecked
    {0x3cc, 0x002fabf0},  // GScr_Unlink
    {0x3d0, 0x002fab10},  // GScr_EnableLinkTo
    {0x3d4, 0x00308df0},  // GScr_DoSpawnNamed
    {0x3d8, 0x00308ff0},  // GScr_DoSpawn
    {0x3dc, 0x00308cc0},  // GScr_ForceSpawnNamed
    {0x3e0, 0x00308fb0},  // GScr_ForceSpawn
    {0x3e4, 0x002faa30},  // GScr_GetOrigin
    {0x3e8, 0x003050b0},  // GScr_GetEye
    {0x538, 0x00300530},  // FUN_00300530
    {0x53c, 0x002fd740},  // GScr_GetLocalizedString
    {0x3ec, 0x002ff320},  // GScr_UseBy
    {0x3f0, 0x003045f0},  // GScr_IsTouching
    {0x3f4, 0x00305050},  // GScr_DoorLock
    {0x3f8, 0x00304ff0},  // GScr_DoorUnlock
    {0x3fc, 0x002fa9d0},  // GScr_DoorIsLocked
    {0x400, 0x002fd520},  // GScr_Delete
    {0x404, 0x002fd4d0},  // GScr_SetRefEntFlag_ef
    {0x408, 0x00304570},  // GScr_SetModel
    {0x40c, 0x002fd490},  // GScr_SetRefEntByte_ee
    {0x410, 0x002fa960},  // GScr_GetNormalHealth
    {0x414, 0x002fa830},  // GScr_SetNormalHealth
    {0x418, 0x002ff1a0},  // GScr_DoDamage
    {0x41c, 0x002fa7f0},  // GScr_SetTakeDamage
    {0x420, 0x002fa7b0},  // GScr_SetEntInList960070
    {0x424, 0x002fa770},  // GScr_ClearEntFlag400
    {0x428, 0x002fa730},  // GScr_SetEntFlag400
    {0x42c, 0x003029b0},  // GScr_SetContents
    {0x430, 0x00304500},  // GScr_DisconnectPaths
    {0x434, 0x002ff120},  // GScr_ConnectPaths
    {0x438, 0x002fa6c0},  // GScr_Turret_SetFlag4
    {0x43c, 0x002fa650},  // GScr_Turret_ClearFlag4
    {0x440, 0x00308bb0},  // GScr_TurretCall272
    {0x444, 0x002fa580},  // GScr_Turret_SetMode
    {0x448, 0x002fa500},  // GScr_Turret_GetUser
    {0x44c, 0x002fa4c0},  // GScr_GetOwnerHandle
    {0x450, 0x002fa420},  // GScr_Turret_SetTargetEntity
    {0x454, 0x002fa3b0},  // GScr_Turret_ClearTargetEntity
    {0x458, 0x002fa2c0},  // GScr_Turret_SetTeam
    {0x45c, 0x002fa250},  // GScr_Turret_MakeUsable
    {0x460, 0x002fa1e0},  // GScr_Turret_MakeUnusable
    {0x464, 0x002fa160},  // GScr_Turret_Set54
    {0x468, 0x002fa0e0},  // GScr_Turret_SetMaxRange
    {0x46c, 0x002fa070},  // GScr_Turret_GetMaxRange
    {0x470, 0x002f9ff0},  // GScr_Turret_GetOwner
    {0x474, 0x002f9ef0},  // GScr_SetCursorHint
    {0x478, 0x002f9e80},  // GScr_ResetHintCache
    {0x47c, 0x002ff060},  // GScr_SetHintString
    {0x480, 0x002f9e20},  // GScr_SetHintString28c
    {0x48c, 0x002feff0},  // GScr_GetAnimLength
    {0x490, 0x002f9e00},  // GScr_AnimHasNotetrack
    {0x494, 0x00301af0},  // GScr_ClearAnim
    {0x498, 0x00301a50},  // GScr_SetAnimKnob
    {0x488, 0x003019b0},  // GScr_SetAnimKnobAll
    {0x49c, 0x00301910},  // GScr_SetAnim
    {0x4ac, 0x00301860},  // GScr_GetAnimTime
    {0x4a0, 0x00301790},  // GScr_SetFlaggedAnimKnob
    {0x4a4, 0x003016f0},  // GScr_SetFlaggedAnimKnobAll
    {0x4a8, 0x00301650},  // GScr_SetFlaggedAnim
    {0x4b0, 0x00304f50},  // GScr_UseAnimTree
    {0x4b4, 0x00304e60},  // GScr_ClearAnimTree
    {0x484, 0x00300360},  // GScr_StartSubtitle
    {0x4b8, 0x00300320},  // GScr_DebugDrawAnimTrees
    {0x4c0, 0x002f9d90},  // GScr_GetStance
    {0x4c4, 0x00307350},  // GScr_MagicGrenade
    {0x4c8, 0x00307ca0},  // GScr_MagicGrenadeDefaultFuse
    {0x4cc, 0x00304e00},  // GScr_MagicGrenadeManual
    {0x4d0, 0x00305610},  // GScr_MagicGrenadeManualDefaultFuse
    {0x4d4, 0x002f9d30},  // GScr_Turret_GetFlag1
    {0x4d8, 0x002fd420},  // GScr_SetGoalNode
    {0x4dc, 0x00303cc0},  // GScr_GetTagOrigin
    {0x4e0, 0x00303c70},  // GScr_GetTagAngles
    {0x4e4, 0x00300160},  // GScr_ShellShock
    {0x4e8, 0x002f9cd0},  // GScr_ClearClientVec4f4
    {0x4ec, 0x002f9bf0},  // GScr_ViewKick
    {0x4f0, 0x002f9bb0},  // GScr_SetEFlag8000
    {0x4f4, 0x002f9b70},  // GScr_ClearEFlag8000
    {0x4f8, 0x002f9b10},  // GScr_MoveGravity
    {0x4fc, 0x00302060},  // GScr_LocalToWorldCoords
    {0x500, 0x002f9ad0},  // GScr_GetEntityNumber
    {0x504, 0x002f9a90},  // GScr_SetEntFlag40000
    {0x508, 0x002f9a50},  // GScr_ClearEntFlag40000
    {0x50c, 0x002f9a10},  // GScr_ClearEntFlag80000
    {0x510, 0x002f99d0},  // GScr_SetEntFlag80000
    {0x514, 0x003028d0},  // GScr_ScriptMover_SetActorBox
    {0x518, 0x002f9900},  // GScr_SetSpawnerTeam
    {0x51c, 0x002f98a0},  // GScr_Turret_SetArc_30
    {0x520, 0x002f9850},  // GScr_Turret_SetArc_38
    {0x524, 0x002f9800},  // GScr_Turret_SetArc_2c
    {0x528, 0x002f97b0},  // GScr_Turret_SetArc_34
    {0x52c, 0x002f9760},  // GScr_Turret_Set3c
    {0x530, 0x002f9710},  // GScr_Turret_Set40
    {0x534, 0x00304dc0},  // GScr_SetModelNoFlag
    {0x154, 0x002f4c20},  // GScr_GetPadStick
    {0x158, 0x002f4c00},  // GScr_Set8d20e0
};

const ImportSlot kGameImports2[] = {
    {0x908, 0x00015c50},  // ScrHudElem_Set_04
    {0x90c, 0x00015c70},  // ScrHudElem_Get_04
    {0x910, 0x00015c90},  // ScrHudElem_Set_08
    {0x914, 0x00015cb0},  // ScrHudElem_Get_08
    {0x918, 0x00015cd0},  // ScrHudElem_Set_14
    {0x91c, 0x00015cf0},  // ScrHudElem_Get_14
    {0x920, 0x00015d10},  // ScrHudElem_Set_18
    {0x924, 0x00015d30},  // ScrHudElem_Get_18
    {0x928, 0x00015d50},  // ScrHudElem_SetFloat_6c
    {0x92c, 0x00015d70},  // ScrHudElem_GetFloat_6c
    {0x930, 0x00015d90},  // ScrHudElem_SetFloat_0c
    {0x934, 0x00015db0},  // ScrHudElem_GetFloat_0c
    {0x938, 0x00015dd0},  // ScrHudElem_SetAlpha
    {0x93c, 0x00015df0},  // ScrHudElem_GetAlpha
    {0x940, 0x00015e10},  // ScrHudElem_SetColorByte0
    {0x944, 0x00015e30},  // ScrHudElem_GetColorByte0
    {0x948, 0x00015e50},  // ScrHudElem_SetColorByte1
    {0x94c, 0x00015e70},  // ScrHudElem_GetColorByte1
    {0x950, 0x00015e90},  // ScrHudElem_SetColorByte2
    {0x954, 0x00015eb0},  // ScrHudElem_GetColorByte2
    {0x95c, 0x00015ed0},  // ScrPathNode_SetTargetname
    {0x958, 0x00015f50},  // ScrPathNode_GetTargetname
    {0x964, 0x00016010},  // ScrPathNode_SetTarget
    {0x960, 0x00016090},  // ScrPathNode_GetTarget
    {0x96c, 0x00016150},  // ScrPathNode_SetAnimscript
    {0x968, 0x000161d0},  // ScrPathNode_GetAnimscript
    {0x974, 0x00016290},  // ScrPathNode_SetScriptNoteworthy
    {0x970, 0x00016310},  // ScrPathNode_GetScriptNoteworthy
    {0x97c, 0x000163d0},  // ScrPathNode_SetOrigin
    {0x978, 0x00016450},  // ScrPathNode_GetOrigin
    {0x984, 0x00016530},  // ScrPathNode_SetAngles
    {0x980, 0x000165b0},  // ScrPathNode_GetAngles
    {0x98c, 0x00016690},  // ScrPathNode_SetRadius
    {0x988, 0x00016700},  // ScrPathNode_GetRadius
    {0x994, 0x00016770},  // ScrPathNode_SetSpawnflags
    {0x990, 0x000167e0},  // ScrPathNode_GetSpawnflags
    {0x99c, 0x00016850},  // ScrPathNode_SetType
    {0x998, 0x000168d0},  // ScrPathNode_GetType
    {0x9a4, 0x00016990},  // ScrVehNode_SetTargetname
    {0x9a0, 0x000169c0},  // ScrVehNode_GetTargetname
    {0x9ac, 0x00016a00},  // ScrVehNode_SetTarget
    {0x9a8, 0x00016a30},  // ScrVehNode_GetTarget
    {0x9b4, 0x00016a70},  // ScrVehNode_SetOrigin
    {0x9b0, 0x00016aa0},  // ScrVehNode_GetOrigin
    {0x9bc, 0x00016af0},  // ScrVehNode_SetAngles
    {0x9b8, 0x00016b20},  // ScrVehNode_GetAngles
    {0x9c4, 0x00016b70},  // ScrVehNode_SetSpeed
    {0x9c0, 0x00016b90},  // ScrVehNode_GetSpeed
    {0x9cc, 0x00016bb0},  // ScrVehNode_SetLookahead
    {0x9c8, 0x00016bd0},  // ScrVehNode_GetLookahead
    {0x9d4, 0x00016bf0},  // ScrVehNode_SetScriptNoteworthy
    {0x9d0, 0x00016c20},  // ScrVehNode_GetScriptNoteworthy
    {0x9dc, 0x00016c60},  // ScrEnt_SetClassname
    {0x9d8, 0x00016cd0},  // ScrEnt_GetClassname
    {0x9e4, 0x00016d70},  // ScrEnt_SetOrigin
    {0x9e0, 0x00016de0},  // ScrEnt_GetOrigin
    {0x9ec, 0x00016eb0},  // ScrEnt_SetModel
    {0x9e8, 0x00016f20},  // ScrEnt_GetModel
    {0x9f4, 0x00016fc0},  // ScrEnt_SetSpawnflags
    {0x9f0, 0x00017020},  // ScrEnt_GetSpawnflags
    {0x9fc, 0x00017080},
    {0x9f8, 0x000170e0},
    {0xa04, 0x00017140},  // ScrEnt_SetClosespeed
    {0xa00, 0x000171a0},  // ScrEnt_GetClosespeed
    {0xa0c, 0x00017200},  // ScrEnt_SetTarget
    {0xa08, 0x00017270},  // ScrEnt_GetTarget
    {0xa14, 0x00017310},  // ScrEnt_SetTargetname
    {0xa10, 0x00017380},  // ScrEnt_GetTargetname
    {0xa34, 0x00017420},  // ScrEnt_SetTeamname
    {0xa30, 0x00017490},  // ScrEnt_GetTeamname
    {0xa3c, 0x00017530},  // ScrEnt_SetWait
    {0xa38, 0x00017590},  // ScrEnt_GetWait
    {0xa44, 0x000175f0},  // ScrEnt_SetRandom
    {0xa40, 0x00017650},  // ScrEnt_GetRandom
    {0xa4c, 0x000176b0},
    {0xa48, 0x00017710},
    {0xa54, 0x00017770},  // ScrEnt_SetHealth
    {0xa50, 0x000177d0},  // ScrEnt_GetHealth
    {0xa5c, 0x00017830},  // ScrEnt_SetDmg
    {0xa58, 0x00017890},  // ScrEnt_GetDmg
    {0xa64, 0x000178f0},  // ScrEnt_SetAngles
    {0xa60, 0x00017960},  // ScrEnt_GetAngles
    {0xa74, 0x00017a30},  // ScrEnt_SetRotate
    {0xa70, 0x00017aa0},  // ScrEnt_GetRotate
    {0xa7c, 0x00017b70},  // ScrEnt_SetDegrees
    {0xa78, 0x00017bd0},  // ScrEnt_GetDegrees
    {0xa84, 0x00017080},
    {0xa80, 0x000170e0},
    {0xa9c, 0x00017c30},  // ScrEnt_SetKey
    {0xa98, 0x00017c90},  // ScrEnt_GetKey
    {0xaa4, 0x00017cf0},  // ScrEnt_SetDelay
    {0xaa0, 0x00017d50},  // ScrEnt_GetDelay
    {0xac4, 0x000176b0},
    {0xac0, 0x00017710},
    {0xacc, 0x00017db0},  // ScrEnt_SetSpawnitem
    {0xac8, 0x00017e20},  // ScrEnt_GetSpawnitem
    {0xadc, 0x00017ec0},  // ScrEnt_SetGroupname
    {0xad8, 0x00017f30},  // ScrEnt_GetGroupname
    {0xaec, 0x00017fd0},  // ScrEnt_SetScriptNoteworthy
    {0xae8, 0x00018040},  // ScrEnt_GetScriptNoteworthy
    {0xaf4, 0x000180e0},  // ScrEnt_SetMaxhealth
    {0xaf0, 0x00018140},  // ScrEnt_GetMaxhealth
    {0xb04, 0x000181a0},  // ScrEnt_SetAnimname
    {0xb00, 0x00018210},  // ScrEnt_GetAnimname
    {0xb0c, 0x000182b0},  // ScrEnt_SetPersistentIndex
    {0xb08, 0x00018310},  // ScrEnt_GetPersistentIndex
    {0xb14, 0x00018370},  // ScrEnt_SetTakedamage
    {0xb10, 0x000183d0},  // ScrEnt_GetTakedamage
    {0xb1c, 0x00018430},  // ScrActor_SetFloat_d4
    {0xb18, 0x00018490},  // ScrActor_GetFloat_d4
    {0xb24, 0x00018500},  // ScrActor_SetFloat_d8
    {0xb20, 0x00018560},  // ScrActor_GetFloat_d8
    {0xb2c, 0x000185d0},  // ScrActor_SetVec_108
    {0xb28, 0x00018640},  // ScrActor_GetVec_108
    {0xb34, 0x00018720},  // ScrActor_SetVec_114
    {0xb30, 0x00018790},  // ScrActor_GetVec_114
    {0xb3c, 0x00018870},  // ScrActor_SetVec_120
    {0xb38, 0x000188e0},  // ScrActor_GetVec_120
    {0xb44, 0x000189c0},  // ScrActor_SetFloat_828
    {0xb40, 0x00018a20},  // ScrActor_GetFloat_828
    {0xb4c, 0x00018a90},  // ScrActor_SetFloat_82c
    {0xb48, 0x00018af0},  // ScrActor_GetFloat_82c
    {0xb54, 0x00018b60},  // ScrActor_SetFloat_824
    {0xb50, 0x00018bc0},  // ScrActor_GetFloat_824
    {0xb5c, 0x00018c30},  // ScrActor_SetInt_830
    {0xb58, 0x00018c90},  // ScrActor_GetInt_830
    {0xb64, 0x00018cf0},  // ScrActor_SetInt_7b0
    {0xb60, 0x00018d50},  // ScrActor_GetInt_7b0
    {0xb6c, 0x00018db0},  // ScrActor_SetInt_7b4
    {0xb68, 0x00018e10},  // ScrActor_GetInt_7b4
    {0xb74, 0x00018e70},  // ScrActor_SetShort_7dc
    {0xb70, 0x00018ed0},  // ScrActor_GetShort_7dc
    {0xb7c, 0x00018f40},  // ScrActor_SetFloat_7b8
    {0xb78, 0x00018fa0},  // ScrActor_GetFloat_7b8
    {0xb84, 0x00019010},  // ScrActor_SetFloat_7bc
    {0xb80, 0x00019070},  // ScrActor_GetFloat_7bc
    {0xb8c, 0x000190e0},  // ScrActor_SetInt_228
    {0xb88, 0x00019140},  // ScrActor_GetInt_228
    {0xb94, 0x000191a0},  // ScrActor_SetInt_22c
    {0xb90, 0x00019200},  // ScrActor_GetInt_22c
    {0xb9c, 0x00019260},  // ScrActor_SetVec_234
    {0xb98, 0x000192d0},  // ScrActor_GetVec_234
    {0xba4, 0x000193b0},  // ScrActor_SetInt_230
    {0xba0, 0x00019410},  // ScrActor_GetInt_230
    {0xbac, 0x00019470},  // ScrActor_SetInt_240
    {0xba8, 0x000194d0},  // ScrActor_GetInt_240
    {0xbb4, 0x00019530},  // ScrActor_SetInt_194
    {0xbb0, 0x00019590},  // ScrActor_GetInt_194
    {0xbbc, 0x000195f0},  // ScrActor_SetFloat_7a4
    {0xbb8, 0x00019650},  // ScrActor_GetFloat_7a4
    {0xbc4, 0x000196c0},  // ScrActor_SetFloat_154
    {0xbc0, 0x00019720},  // ScrActor_GetFloat_154
    {0xbcc, 0x00019790},  // ScrActor_SetFloat_7e4
    {0xbc8, 0x000197f0},  // ScrActor_GetFloat_7e4
    {0xbd4, 0x00019860},  // ScrActor_SetInt_7e8
    {0xbd0, 0x000198c0},  // ScrActor_GetInt_7e8
    {0xbdc, 0x00019920},  // ScrActor_SetInt_7ec
    {0xbd8, 0x00019980},  // ScrActor_GetInt_7ec
    {0xbe4, 0x000199e0},  // ScrActor_SetInt_1308
    {0xbe0, 0x00019a40},  // ScrActor_GetInt_1308
    {0xbec, 0x00019aa0},  // ScrActor_SetString_ec
    {0xbe8, 0x00019b20},  // ScrActor_GetString_ec
    {0xbf4, 0x00019bd0},  // ScrActor_SetInt_f0
    {0xbf0, 0x00019c30},  // ScrActor_GetInt_f0
    {0xbfc, 0x00019c90},  // ScrActor_SetInt_f4
    {0xbf8, 0x00019cf0},  // ScrActor_GetInt_f4
    {0xc04, 0x00019d50},  // ScrActor_SetString_f8
    {0xc00, 0x00019dd0},  // ScrActor_GetString_f8
    {0xc0c, 0x00019e80},  // ScrActor_SetInt_7d8
    {0xc08, 0x00019ee0},  // ScrActor_GetInt_7d8
    {0xc14, 0x00019f40},  // ScrActor_SetFloat_130c
    {0xc10, 0x00019fa0},  // ScrActor_GetFloat_130c
    {0xc1c, 0x0001a010},  // ScrActor_SetRef_1310
    {0xc18, 0x0001a070},  // ScrActor_GetRef_1310
    {0xc24, 0x0001a0f0},  // ScrActor_SetInt_131c
    {0xc20, 0x0001a150},  // ScrActor_GetInt_131c
    {0xc2c, 0x0001a1b0},  // ScrActor_SetInt_1328
    {0xc28, 0x0001a210},  // ScrActor_GetInt_1328
    {0xc34, 0x0001a270},  // ScrActor_SetRef_12a0
    {0xc30, 0x0001a2d0},  // ScrActor_GetRef_12a0
    {0xc3c, 0x0001a350},  // ScrActor_SetInt_cc
    {0xc38, 0x0001a3b0},  // ScrActor_GetInt_cc
    {0xc44, 0x0001a410},  // ScrActor_SetByte_1374
    {0xc40, 0x0001a470},  // ScrActor_GetByte_1374
    {0xc4c, 0x0001a4d0},  // ScrActor_SetByte_7de
    {0xc48, 0x0001a530},  // ScrActor_GetByte_7de
    {0xc54, 0x0001a590},  // ScrActor_SetInt_1378
    {0xc50, 0x0001a5f0},  // ScrActor_GetInt_1378
    {0xc5c, 0x0001a650},  // ScrActor_SetInt_137c
    {0xc58, 0x0001a6b0},  // ScrActor_GetInt_137c
    {0xc64, 0x0001a710},  // ScrActor_SetString_1384
    {0xc60, 0x0001a790},  // ScrActor_GetString_1384
    {0xc6c, 0x0001a840},  // ScrActor_SetString_1388
    {0xc68, 0x0001a8c0},  // ScrActor_GetString_1388
    {0xc74, 0x0001a970},  // ScrActor_SetString_138c
    {0xc70, 0x0001a9f0},  // ScrActor_GetString_138c
    {0xc7c, 0x0001aaa0},  // ScrActor_SetString_2c0
    {0xc78, 0x0001ab20},  // ScrActor_GetString_2c0
    {0xc84, 0x0001abd0},  // ScrActor_SetInt_184
    {0xc80, 0x0001ac30},  // ScrActor_GetInt_184
    {0xc8c, 0x0001ac90},  // ScrSentient_SetTeam
    {0xc88, 0x0001ad00},  // ScrSentient_GetTeam
    {0xc94, 0x0001ada0},  // ScrSentient_SetInt_28
    {0xc90, 0x0001ae00},  // ScrSentient_GetInt_28
    {0xc9c, 0x0001ae60},  // ScrSentient_SetFloat_2c
    {0xc98, 0x0001aec0},  // ScrSentient_GetFloat_2c
    {0xca4, 0x0001af20},  // ScrSentient_SetRef_48
    {0xca0, 0x0001af80},  // ScrSentient_GetRef_48
    {0xcac, 0x0001b000},  // ScrSentient_SetGoalNode
    {0xca8, 0x0001b060},  // ScrSentient_GetGoalNode
    {0xcb4, 0x0001b0e0},  // ScrSentient_SetFloat_18
    {0xcb0, 0x0001b140},  // ScrSentient_GetFloat_18
    {0xcbc, 0x0001b1a0},  // ScrSentient_SetGoalArc
    {0xcb8, 0x0001b200},  // ScrSentient_GetGoalArc
    {0xcc4, 0x0001b260},  // ScrSentient_SetRef_3c
    {0xcc0, 0x0001b2c0},  // ScrSentient_GetRef_3c
    {0xccc, 0x0001b340},  // ScrSentient_SetInt_30
    {0xcc8, 0x0001b3a0},  // ScrSentient_GetInt_30
    {0xcd4, 0x0001b400},  // ScrSentient_SetInt_34
    {0xcd0, 0x0001b460},  // ScrSentient_GetInt_34
    {0xcec, 0x0001b4c0},  // ScrSentient_SetInt_38
    {0xce8, 0x0001b520},  // ScrSentient_GetInt_38
};

const ImportSlot kLevelImports[] = {
    {0x000, 0xbfbfbfbf},
    {0x000, 0x002f53f0},  // GScr_EmptyStub0
    {0x004, 0x002f53e0},  // GScr_EmptyStub1
    {0x008, 0x002ffa10},  // GScr_SendGameMessage
    {0x00c, 0x002ff9c0},  // GScr_SendGameMessageBold
    {0x010, 0x002fbf80},  // GScr_Print3D
    {0x014, 0x002fbf20},  // GScr_DebugLine
    {0x018, 0x001ee901},  // _sprintf
    {0x01c, 0x001ef4d0},  // LIBCMT::_strcpy
    {0x020, 0x001eed80},  // _strncpy
    {0x034, 0x002fbea0},  // Scr_SaveThreadLocals
    {0x038, 0x002f5330},  // GScr_EmptyStub14
    {0x03c, 0x002fddb0},  // Scr_WaitRecord15
    {0x040, 0x002fde40},  // Scr_WaitRecord16
    {0x02c, 0x002fdd00},  // Scr_CreateThread
    {0x028, 0x002fdc40},  // Scr_BeginThread
    {0x030, 0x00300a40},  // Scr_BeginThreadEndOn
    {0x044, 0x00300850},  // Scr_WaitRecord17
    {0x048, 0x003007b0},  // Scr_EndOn
    {0x04c, 0x002f62b0},  // Scr_InThread
    {0x050, 0x002f6270},  // Scr_ThreadHandleIsValid
    {0x054, 0x002f5310},  // GScr_EmptyStub21
    {0x058, 0x002f5320},  // GScr_EmptyStub22
    {0x05c, 0x002fdbf0},  // Scr_GetCurrentThreadHandle
    {0x060, 0x002f6220},  // Scr_TerminateThread
    {0x0ec, 0x000142f0},  // Scr_RandomInt
    {0x0f0, 0x00014320},  // Scr_RandomFloat
    {0x0f4, 0x00014370},  // Scr_RandomIntRange
    {0x0f8, 0x000143b0},  // Scr_RandomFloatRange
    {0x0fc, 0x00014440},  // Scr_SinCosDegrees
    {0x100, 0x00011020},  // Scr_Sin
    {0x104, 0x00011000},  // Scr_Cos
    {0x108, 0x00011030},  // Scr_Tan
    {0x10c, 0x000139f0},  // Scr_Asin
    {0x110, 0x000139e0},  // Scr_Acos
    {0x114, 0x00013a00},  // Scr_Atan
    {0x118, 0x00013a10},  // Scr_Atan2
    {0x11c, 0x00014470},  // Scr_Log
    {0x120, 0x00011010},  // Scr_Abs
    {0x124, 0x002f62c0},  // Scr_Sqrt
    {0x128, 0x00013ef0},  // Scr_Distance
    {0x12c, 0x00013f50},  // Scr_DistanceSquared
    {0x130, 0x00013e70},  // Scr_Length
    {0x134, 0x00013ea0},  // Scr_LengthSquared
    {0x138, 0x00013fa0},  // Scr_Closer
    {0x13c, 0x00013ed0},  // Scr_VectorDot
    {0x140, 0x00014030},  // Scr_VectorNormalize
    {0x144, 0x000140b0},  // Scr_VectorToAngles
    {0x148, 0x000140d0},  // Scr_AnglesToUp
    {0x14c, 0x00014150},  // Scr_AnglesToRight
    {0x150, 0x000141e0},  // Scr_AnglesToForward
    {0x198, 0x002fdab0},  // GScr_GetCvar
    {0x19c, 0x002fda90},  // GScr_GetCvarInt
    {0x1a0, 0x002fda60},  // GScr_GetCvarFloat
    {0x1a4, 0x002ff950},  // GScr_SetCvar
    {0x1a8, 0x002ff8d0},  // GScr_SetCvarInt
    {0x1ac, 0x002ff850},  // GScr_SetCvarFloat
    {0x1b0, 0x003061e0},  // GScr_Spawn
    {0x1b4, 0x00306120},  // GScr_SpawnWithFlags
    {0x1b8, 0x003038a0},  // GScr_SpawnBoxEntity
    {0x1bc, 0x00306030},  // GScr_SpawnVehicle
    {0x1c0, 0x00305330},  // GScr_SpawnTurret
    {0x1c4, 0x00305280},  // GScr_SpawnTurretWithAngles
    {0x1c8, 0x00300780},  // GScr_MusicPlay
    {0x1cc, 0x002ff840},  // GScr_MusicStop
    {0x1d0, 0x00300750},  // GScr_AmbientPlay
    {0x1d4, 0x002f6200},  // GScr_SetMusicTrack
    {0x1d8, 0x00300710},  // GScr_SoundBankLoad
    {0x1dc, 0x003006d0},  // GScr_SoundBankUnload
    {0x284, 0x002f52d0},  // GScr_GetTime
    {0x288, 0x002f61f0},  // GScr_GetFrameTime
    {0x28c, 0x002f52b0},  // GScr_GetDifficulty
    {0x290, 0x002fd9b0},  // GScr_GetAI
    {0x294, 0x002ff810},  // GScr_GetAIAll
    {0x298, 0x002ff760},  // GScr_GetSpawnerArray
    {0x29c, 0x002ff680},  // GScr_GetSpawnerTeamArray
    {0x2a0, 0x002fbb30},  // GScr_GetWeaponModel
    {0x2a4, 0x002fbad0},  // GScr_GetWeaponDisplayName
    {0x2a8, 0x002fba20},  // GScr_GetWeaponClassname
    {0x2ac, 0x002fb970},  // GScr_GetWeaponClassname2
    {0x2b0, 0x002fd8c0},  // GScr_GetKeyBinding
    {0x2bc, 0x003043c0},  // GScr_TransformLocalOffset
    {0x2c0, 0x00304150},  // GScr_GetAnimStartAngles
    {0x2c4, 0x00303fe0},  // GScr_GetAnimWorldMoveDelta
    {0x2c8, 0x00303f90},  // GScr_GetMoveDelta
    {0x2cc, 0x00303f30},  // GScr_GetAngleDelta
    {0x2dc, 0x002f5240},  // GScr_EmptyStub183
    {0x2e0, 0x002f5230},  // GScr_EmptyStub184
    {0x2e4, 0x002fd8a0},  // GScr_SetConfigstring9Float
    {0x2e8, 0x00306fd0},  // GScr_RadiusDamage
    {0x2ec, 0x00306f60},  // GScr_RadiusDamageFromEntity
    {0x2f0, 0x002f5220},  // GScr_SetRadiusDamageFlag
    {0x2f4, 0x002fb890},  // GScr_MissionSuccess
    {0x2f8, 0x00300630},  // GScr_MissionFailed
    {0x2fc, 0x002fb7f0},  // GScr_ChangeLevelWithDelay
    {0x300, 0x002fb7a0},  // GScr_ChangeLevel
    {0x304, 0x00302320},  // GScr_Earthquake
    {0x308, 0x002f5210},  // GScr_SetCompassMarkersEnabled
    {0x30c, 0x002fd890},  // G_SetupVehiclePool_Thunk
    {0x2d0, 0x002f51f0},  // GScr_SetMissileMaxLifetime
    {0x314, 0x002ff5a0},  // GScr_RandomTracer
    {0x318, 0x00308720},  // GScr_MagicBullet
    {0x31c, 0x002f51e0},  // GScr_EmptyStub199
    {0x320, 0x002f51d0},  // GScr_EmptyStub200
    {0x324, 0x003022e0},  // GScr_GetModelBoneCount
    {0x328, 0x00302210},  // GScr_GetModelBoneName
    {0x32c, 0x002fb750},  // GScr_WeaponGetFloat_5dc
    {0x330, 0x002fb700},  // GScr_WeaponGetTableValue_5a0
    {0x334, 0x002fb6c0},  // GScr_WeaponGet_6d4
    {0x338, 0x002fb680},  // GScr_WeaponGet_6d8
    {0x33c, 0x002fb620},  // GScr_WeaponType
    {0x340, 0x00303880},  // GScr_BadPlace_Delete
    {0x344, 0x00303e70},  // GScr_BadPlace_Cylinder
    {0x348, 0x00303d20},  // GScr_BadPlace_Arc
    {0x34c, 0x002fb600},  // GScr_GetCurrentZoneId
    {0x350, 0x002ff590},  // Actor_FreeAllCorpses
    {0x7a4, 0x002f5140},  // GScr_SetHudVisible
    {0x7a8, 0x003005a0},  // GScr_HudSetText
    {0x7ac, 0x00301c80},  // GScr_HudSetShader
    {0x7b0, 0x002fd820},  // GScr_HudSetTimer4
    {0x7b4, 0x002fd7b0},  // GScr_HudSetTimer
    {0x7b8, 0x002fd790},  // GScr_HudSetTimer6
    {0x7bc, 0x002fd770},  // GScr_HudSetTimer7
    {0x7c0, 0x003021d0},  // GScr_HudSetShaderTimer8
    {0x7c4, 0x00302190},  // GScr_HudSetShaderTimer9
    {0x7c8, 0x002f6010},  // GScr_HudSetValue
    {0x7cc, 0x002f5f90},  // GScr_HudFadeOverTime
    {0x7d0, 0x002f5ef0},  // GScr_HudScaleOverTime
    {0x7d4, 0x002f5e60},  // GScr_HudMoveOverTime
    {0x7d8, 0x002fb4e0},  // GScr_HudClear
    {0x7dc, 0x002f5e40},  // GScr_HudDestroy
    {0x7e0, 0x002f62d0},  // ScrIface_Malloc
    {0x7e4, 0x002f5440},  // ScrIface_Free
    {0x7e8, 0x002f5420},  // ScrIface_Alloc
    {0x7ec, 0x002f5400},  // ScrIface_SmallFree
    {0x7f0, 0x001ef6a0},  // memset2
    {0x8a0, 0x001eeb81},  // _atol
    {0x89c, 0x002f5340},  // ScrIface_atof
    {0x7f4, 0x002f5300},  // GScr_ReturnFalse509
    {0x7f8, 0x002f52f0},  // GScr_ReturnFalse510
    {0x7fc, 0x002f52e0},  // GScr_ReturnFalse511
    {0x8a4, 0x002f5120},  // ScrIface_HashKeyNoCase
    {0x378, 0x002f51c0},  // GScr_EmptyStub222
    {0x37c, 0x002f51b0},  // GScr_EmptyStub223
    {0x354, 0x002fd840},  // GScr_FindZone
    {0x358, 0x002fb5a0},  // GScr_GetEntZone
    {0x35c, 0x002ff500},  // GScr_GetZoneAtPoint
    {0x360, 0x002f6130},  // GScr_IsZoneLoaded
    {0x364, 0x002f6110},  // GScr_ZoneSetField34
    {0x368, 0x002f60f0},  // GScr_ZoneClearField34
    {0x36c, 0x00307fa0},  // GScr_LoadZoneSync
    {0x370, 0x002f51a0},  // GScr_EmptyStub220
    {0x374, 0x002f5190},  // GScr_EmptyStub221
    {0x064, 0x002fbe00},  // Scr_QueueNotify
    {0x068, 0x002fbd70},  // Scr_QueueNotifyCallback26
    {0x074, 0x002fbc40},  // Scr_QueueNotifyCallback29
    {0x06c, 0x002fbcd0},  // Scr_QueueNotifyWithString
    {0x070, 0x002fbba0},  // Scr_QueueNotifyWith2Args
    {0x078, 0x002fbec0},  // GScr_EntHasNotifyListener
    {0x07c, 0x002fdb70},  // GScr_SetEntVar
    {0x080, 0x002fdb20},  // GScr_GetEntVar
    {0x8c8, 0x00236a70},  // GScr_SetFogDistances
    {0x8cc, 0x00238350},  // GScr_SetFogColorBytes
    {0x8d0, 0x00236a20},  // GScr_SetFogColor
    {0x8d4, 0x002f5110},  // GScr_SetFogPassFinalAlpha
    {0x8d8, 0x002f5100},  // GScr_SetFogPassStep
    {0x8dc, 0x002f50f0},  // GScr_SetFogPassAlpha
    {0x8e0, 0x002f50e0},  // GScr_SetFogPassCount
    {0x8e4, 0x00300b30},  // NameMap_Insert
    {0x8e8, 0x002f6320},  // IntHashMap_FindValue
    {0x8ec, 0x002f5470},  // ScrIface_HashStringNoCase
};

const ImportSlot kLevelImports2[] = {
    {0x8b4, 0x002f53b0},  // GScr_PushToList95fdf0
    {0x8b8, 0x002f5350},  // GScr_CopyStringFromEngine
    {0x79c, 0x000144d0},  // GScr_PhysicsLaunch
    {0x7a0, 0x00014590},  // GScr_PhysicsStop
    {0x2b4, 0x00234ae0},  // GScr_EmptyStub173
    {0x2b8, 0x00234ad0},  // GScr_EmptyStub174
    {0x8f0, 0x002c4110},  // G_CreateCodNote
    {0x8f4, 0x002a8260},  // G_StartCodNote
    {0x8f8, 0x002c40c0},  // G_FreeCodNote
    {0x8fc, 0x002c4070},  // G_FreeAllCodNotes
    {0x904, 0x002b74c0},  // GScr_ClearEntFlag4000000
    {0x900, 0x002cead0},  // GScr_SetEntFlag4000000
};

const ImportSlot kLevelImports3[] = {
    {0xcf0, 0x00044bc0},  // cdOcean_SetBaseHeight
    {0xcf4, 0x00044be0},  // cdOcean_SetWaveSetVec04
    {0xcf8, 0x00044ca0},  // cdOcean_SetWaveSetVec30
    {0xcfc, 0x00044c10},  // cdOcean_SetWaveSetVec10
    {0xd00, 0x00044d30},  // cdOcean_SetWaveOrigin
    {0xd04, 0x00044df0},  // cdOcean_SetWaveDirection
    {0xd08, 0x00044e10},  // cdOcean_SetWaveFalloff
    {0xd0c, 0x00044e30},  // cdOcean_SetWaveLength
    {0xd10, 0x00044e50},  // cdOcean_SetWaveAmplitude
    {0xd14, 0x00044e70},  // cdOcean_SetWavePhase
    {0xd18, 0x00044e90},  // cdOcean_SetWaveSpeed
};

}  // namespace

void XB_CDECL G_SetupObjectiveScriptImports() {
    set_imports(kObjectiveImports, sizeof(kObjectiveImports) / sizeof(kObjectiveImports[0]));
}

void XB_CDECL G_SetupPlayerScriptImports() {
    set_imports(kPlayerImports, sizeof(kPlayerImports) / sizeof(kPlayerImports[0]));
}

void XB_CDECL G_SetupMoverScriptImports() {
    set_imports(kMoverImports, sizeof(kMoverImports) / sizeof(kMoverImports[0]));
}

void XB_CDECL G_SetupVehicleScriptImports() {
    set_imports(kVehicleImports, sizeof(kVehicleImports) / sizeof(kVehicleImports[0]));
}

void XB_CDECL G_SetupActorScriptImports() {
    set_imports(kActorImports, sizeof(kActorImports) / sizeof(kActorImports[0]));
}

void XB_CDECL G_SetupGameScriptImports() {
    set_imports(kGameImports, sizeof(kGameImports) / sizeof(kGameImports[0]));
    G_SetupActorScriptImports();
    G_SetupMoverScriptImports();
    G_SetupPlayerScriptImports();
    set_imports(kGameImports2, sizeof(kGameImports2) / sizeof(kGameImports2[0]));
}

void XB_CDECL G_SetupLevelScriptImports() {
    set_imports(kLevelImports, sizeof(kLevelImports) / sizeof(kLevelImports[0]));
    // The level script's own imports (0x27 words from g_levelScriptImports, 0x009270d0) at +0x800.
    copy_level_imports();
    set_imports(kLevelImports2, sizeof(kLevelImports2) / sizeof(kLevelImports2[0]));
    G_SetupGameScriptImports();
    G_SetupObjectiveScriptImports();
    G_SetupVehicleScriptImports();
    set_imports(kLevelImports3, sizeof(kLevelImports3) / sizeof(kLevelImports3[0]));
}

}  // namespace game
