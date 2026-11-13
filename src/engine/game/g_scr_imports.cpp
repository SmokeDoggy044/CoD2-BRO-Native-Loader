#include "g_scr_imports.h"

#include "title_data.h"
#include "actor.h"
#include "g_main.h"
#include "g_scr_builtins.h"
#include "g_scr_builtins2.h"
#include "g_scr_math.h"
#include "g_scr_runtime.h"
#include "scr_fiber.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "shaders.h"
#include "title_crt.h"
#include "unr_methods.h"

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
    {0x15c, own_address(&GScr_ObjectiveAddNoText)},  // GScr_ObjectiveAddNoText
    {0x160, own_address(&GScr_ObjectiveAddIntText)},  // GScr_ObjectiveAddIntText
    {0x164, own_address(&GScr_ObjectiveAdd)},  // GScr_ObjectiveAdd
    {0x168, own_address(&GScr_ObjectiveAddIntTextWithPosition)},  // GScr_ObjectiveAddIntTextWithPosition
    {0x16c, own_address(&GScr_ObjectiveAddWithPosition)},  // GScr_ObjectiveAddWithPosition
    {0x170, own_address(&GScr_ObjectiveAddLocalized)},  // GScr_ObjectiveAddLocalized
    {0x174, own_address(&GScr_ObjectiveAddLocalizedWithPosition)},  // GScr_ObjectiveAddLocalizedWithPosition
    {0x178, own_address(&GScr_ObjectiveDelete)},  // GScr_ObjectiveDelete
    {0x17c, own_address(&GScr_ObjectiveState)},  // GScr_ObjectiveState
    {0x180, own_address(&GScr_ObjectiveStringLocalized)},  // GScr_ObjectiveStringLocalized
    {0x184, own_address(&GScr_ObjectiveStringByHash)},  // GScr_ObjectiveStringByHash
    {0x188, own_address(&GScr_ObjectiveString)},  // GScr_ObjectiveString
    {0x18c, own_address(&GScr_ObjectivePosition)},  // GScr_ObjectivePosition
    {0x190, own_address(&GScr_ObjectiveCurrent)},  // GScr_ObjectiveCurrent
    {0x194, own_address(&GScr_ObjectiveToggleRing)},  // GScr_ObjectiveToggleRing
};

const ImportSlot kPlayerImports[] = {
    {0x684, own_address(&GScr_PlayerGiveWeapon)},  // GScr_PlayerGiveWeapon
    {0x688, own_address(&GScr_PlayerGiveWeaponWithAmmo)},  // GScr_PlayerGiveWeaponWithAmmo
    {0x68c, own_address(&GScr_PlayerTakeWeapon)},  // GScr_PlayerTakeWeapon
    {0x690, own_address(&GScr_PlayerTakeAllWeapons)},  // GScr_PlayerTakeAllWeapons
    {0x694, own_address(&GScr_PlayerGetCurrentWeapon)},  // GScr_PlayerGetCurrentWeapon
    {0x698, own_address(&GScr_PlayerHasWeapon)},  // GScr_PlayerHasWeapon
    {0x69c, own_address(&GScr_PlayerGetWeaponTotalAmmo)},  // GScr_PlayerGetWeaponTotalAmmo
    {0x6a0, own_address(&GScr_PlayerSwitchToWeapon)},  // GScr_PlayerSwitchToWeapon
    {0x6a4, own_address(&GScr_PlayerGiveMaxAmmo)},  // GScr_PlayerGiveMaxAmmo
    {0x6a8, own_address(&GScr_PlayerGiveMaxAmmoByAmmoType)},  // GScr_PlayerGiveMaxAmmoByAmmoType
    {0x6ac, own_address(&GScr_GetWeaponAmmoFraction)},  // GScr_GetWeaponAmmoFraction
    {0x6b0, own_address(&GScr_PlayerGetWeaponMaxAmmoFraction)},  // GScr_PlayerGetWeaponMaxAmmoFraction
    {0x6b4, own_address(&GScr_PlayerSetOrigin)},  // GScr_PlayerSetOrigin
    {0x6b8, own_address(&GScr_PlayerSetVec_10)},  // GScr_PlayerSetVec_10
    {0x6bc, own_address(&GScr_PlayerSetAngles)},  // GScr_PlayerSetAngles
    {0x6c0, own_address(&GScr_PlayerGetAngles)},  // GScr_PlayerGetAngles
    {0x6c4, own_address(&GScr_PlayerGetFlag40_594)},  // GScr_PlayerGetFlag40_594
    {0x6c8, own_address(&GScr_PlayerGetFlag1_594)},  // GScr_PlayerGetFlag1_594
    {0x6cc, own_address(&GScr_PlayerGetFlag20_594)},  // GScr_PlayerGetFlag20_594
    {0x6d0, own_address(&GScr_PlayerGetFlag20_2c)},  // GScr_PlayerGetFlag20_2c
    {0x6d4, own_address(&GScr_PlayerGetBool_60)},  // GScr_PlayerGetBool_60
    {0x6d8, own_address(&GScr_PlayerSetViewModel)},  // GScr_PlayerSetViewModel
    {0x6dc, own_address(&GScr_PlayerAllowEFlag100000)},  // GScr_PlayerAllowEFlag100000
    {0x6e0, own_address(&GScr_PlayerAllowPmFlag80000)},  // GScr_PlayerAllowPmFlag80000
    {0x6e4, own_address(&GScr_PlayerAllowPmFlag100000)},  // GScr_PlayerAllowPmFlag100000
    {0x6e8, own_address(&GScr_PlayerAllowPmFlag200000)},  // GScr_PlayerAllowPmFlag200000
    {0x6ec, own_address(&GScr_PlayerAllowPmFlag400000)},  // GScr_PlayerAllowPmFlag400000
    {0x6f0, own_address(&GScr_PlayerAllowPmFlag800000)},  // GScr_PlayerAllowPmFlag800000
    {0x6f4, own_address(&GScr_ReturnZero445)},  // GScr_ReturnZero445
    {0x6f8, own_address(&GScr_ReturnZero446)},  // GScr_ReturnZero446
    {0x6fc, own_address(&GScr_ClosePopup)},  // GScr_ClosePopup
    {0x700, own_address(&GScr_EmptyStub448)},  // GScr_EmptyStub448
    {0x704, own_address(&GScr_PlayerSetInt_58c)},  // GScr_PlayerSetInt_58c
    {0x708, own_address(&GScr_SetFreezeControls)},  // GScr_SetFreezeControls
    {0x70c, own_address(&GScr_ToggleNoclip)},  // GScr_ToggleNoclip
    {0x710, own_address(&GScr_SetReverb)},  // GScr_SetReverb
    {0x714, own_address(&GScr_PlayerIsLookingAt)},  // GScr_PlayerIsLookingAt
    {0x718, own_address(&GScr_PlayLocalSound)},  // GScr_PlayLocalSound
    {0x71c, own_address(&GScr_PlayerSetNotFlag_630)},  // GScr_PlayerSetNotFlag_630
    {0x720, own_address(&GScr_PlayerGetWeaponSlotWeapon)},  // GScr_PlayerGetWeaponSlotWeapon
    {0x724, own_address(&GScr_CheckWeaponSlot)},  // GScr_CheckWeaponSlot
    {0x728, own_address(&GScr_PlayerGetWeaponSlotAmmo)},  // GScr_PlayerGetWeaponSlotAmmo
    {0x72c, own_address(&GScr_PlayerSetWeaponSlotAmmo)},  // GScr_PlayerSetWeaponSlotAmmo
    {0x730, own_address(&GScr_PlayerGetWeaponSlotClipAmmo)},  // GScr_PlayerGetWeaponSlotClipAmmo
    {0x734, own_address(&GScr_PlayerSetWeaponSlotClipAmmo)},  // GScr_PlayerSetWeaponSlotClipAmmo
    {0x738, own_address(&GScr_PlayerGetWeaponSlotClipSize)},  // GScr_PlayerGetWeaponSlotClipSize
};

const ImportSlot kMoverImports[] = {
    {0x654, own_address(&GScr_MoveTo)},  // GScr_MoveTo
    {0x658, own_address(&GScr_MoveX)},  // GScr_MoveX
    {0x65c, own_address(&GScr_MoveY)},  // GScr_MoveY
    {0x660, own_address(&GScr_MoveZ)},  // GScr_MoveZ
    {0x664, own_address(&GScr_MoverMoveGravity)},  // GScr_MoverMoveGravity
    {0x668, own_address(&GScr_RotateTo)},  // GScr_RotateTo
    {0x66c, own_address(&GScr_RotatePitch)},  // GScr_RotatePitch
    {0x670, own_address(&GScr_RotateYaw)},  // GScr_RotateYaw
    {0x674, own_address(&GScr_RotateRoll)},  // GScr_RotateRoll
    {0x678, own_address(&GScr_RotateVelocity)},  // GScr_RotateVelocity
    {0x67c, own_address(&GScr_MoverSolid)},  // GScr_MoverSolid
    {0x680, own_address(&GScr_MoverNotSolid)},  // GScr_MoverNotSolid
};

const ImportSlot kVehicleImports[] = {
    {0x73c, own_address(&GScr_Vehicle_AttachPath_entry)},  // GScr_Vehicle_AttachPath
    {0x740, own_address(&GScr_Vehicle_StartPath)},  // GScr_Vehicle_StartPath
    {0x744, own_address(&GScr_Vehicle_SetSwitchNode)},  // GScr_Vehicle_SetSwitchNode
    {0x748, own_address(&GScr_Vehicle_SetWaitNode)},  // GScr_Vehicle_SetWaitNode
    {0x74c, own_address(&GScr_Vehicle_SetMaxSpeed)},  // GScr_Vehicle_SetMaxSpeed
    {0x750, own_address(&GScr_Vehicle_SetSpeed)},  // GScr_Vehicle_SetSpeed
    {0x754, own_address(&GScr_Vehicle_SetSpeedMode2)},  // GScr_Vehicle_SetSpeedMode2
    {0x758, own_address(&GScr_Vehicle_PushFromPoint)},  // GScr_Vehicle_PushFromPoint
    {0x75c, own_address(&GScr_Vehicle_Kill_entry)},  // GScr_Vehicle_Kill
    {0x760, own_address(&GScr_Vehicle_GetWheelSurface)},  // GScr_Vehicle_GetWheelSurface
    {0x764, own_address(&GScr_Vehicle_GetSpeedMPH)},  // GScr_Vehicle_GetSpeedMPH
    {0x768, own_address(&GScr_Vehicle_GetOwnerHandle)},  // GScr_Vehicle_GetOwnerHandle
    {0x76c, own_address(&GScr_Vehicle_Set200)},  // GScr_Vehicle_Set200
    {0x770, own_address(&GScr_Vehicle_Clear200)},  // GScr_Vehicle_Clear200
    {0x774, own_address(&GScr_Vehicle_MakeUnusable)},  // GScr_Vehicle_MakeUnusable
    {0x778, own_address(&GScr_Vehicle_MakeUsable)},  // GScr_Vehicle_MakeUsable
    {0x77c, own_address(&GScr_Vehicle_EjectDriver)},  // GScr_Vehicle_EjectDriver
    {0x780, own_address(&GScr_Vehicle_Set190)},  // GScr_Vehicle_Set190
    {0x784, own_address(&GScr_Vehicle_Clear190)},  // GScr_Vehicle_Clear190
    {0x788, own_address(&GScr_Vehicle_SetTurretTargetVec)},  // GScr_Vehicle_SetTurretTargetVec
    {0x78c, own_address(&GScr_Vehicle_SetTurretTargetEnt)},  // GScr_Vehicle_SetTurretTargetEnt
    {0x790, own_address(&GScr_Vehicle_ClearTurretTarget)},  // GScr_Vehicle_ClearTurretTarget
    {0x794, own_address(&GScr_Vehicle_FireWeapon_entry)},  // GScr_Vehicle_FireWeapon
    {0x798, own_address(&GScr_Vehicle_Is180Clear)},  // GScr_Vehicle_Is180Clear
};

const ImportSlot kActorImports[] = {
    {0x540, own_address(&GScr_SetGoalPosAndYaw)},  // GScr_SetGoalPosAndYaw
    {0x544, own_address(&GScr_SetGoalPos)},  // GScr_SetGoalPos
    {0x548, own_address(&GScr_SetGoalPosFromNode)},  // GScr_SetGoalPosFromNode
    {0x54c, own_address(&GScr_SetGoalEntity_entry)},  // GScr_SetGoalEntity
    {0x550, own_address(&GScr_SentientNearestEnemyDist)},  // GScr_SentientNearestEnemyDist
    {0x554, own_address(&GScr_AnimScriptedFlag0_entry)},  // GScr_AnimScriptedFlag0
    {0x558, own_address(&GScr_ActorShootAt_entry)},  // GScr_ActorShootAt
    {0x55c, own_address(&GScr_ActorShoot_entry)},  // GScr_ActorShoot
    {0x560, own_address(&GScr_ActorMelee_entry)},  // GScr_ActorMelee
    {0x564, own_address(&Actor_CallStateSlot5_entry)},  // Actor_CallStateSlot5
    {0x568, own_address(&GScr_ActorSetAimParams)},  // GScr_ActorSetAimParams
    {0x56c, own_address(&GScr_ActorBlendAimToPoint)},  // GScr_ActorBlendAimToPoint
    {0x570, own_address(&GScr_ActorEnterProne)},  // GScr_ActorEnterProne
    {0x574, own_address(&GScr_ActorExitProne)},  // GScr_ActorExitProne
    {0x578, own_address(&GScr_ActorSetPitchBlendAnims)},  // GScr_ActorSetPitchBlendAnims
    {0x57c, own_address(&GScr_ActorSetAimAnimWeights_entry)},  // GScr_ActorSetAimAnimWeights
    {0x580, own_address(&GScr_ActorSetLookAnims)},  // GScr_ActorSetLookAnims
    {0x590, own_address(&GScr_ActorCanShootWithOffset_entry)},  // GScr_ActorCanShootWithOffset
    {0x594, own_address(&GScr_ActorCanShoot_entry)},  // GScr_ActorCanShoot
    {0x598, own_address(&GScr_ActorCanSee_entry)},  // GScr_ActorCanSee
    {0x59c, own_address(&GScr_ActorCanSeeDefaultAge_entry)},  // GScr_ActorCanSeeDefaultAge
    {0x5a0, own_address(&GScr_ActorCanSeeEnemy_entry)},  // GScr_ActorCanSeeEnemy
    {0x5a4, own_address(&GScr_ActorCanSeeEnemyDefaultAge_entry)},  // GScr_ActorCanSeeEnemyDefaultAge
    {0x5a8, own_address(&GScr_ActorFindBetterCover_entry)},  // GScr_ActorFindBetterCover
    {0x5ac, own_address(&GScr_ActorGetNearestNode_entry)},  // GScr_ActorGetNearestNode
    {0x5b0, own_address(&GScr_DropWeapon)},  // GScr_DropWeapon
    {0x5b4, own_address(&GScr_ActorIsSafeMovePoint_entry)},  // GScr_ActorIsSafeMovePoint
    {0x5b8, own_address(&GScr_ActorTryTeleport_entry)},  // GScr_ActorTryTeleport
    {0x5bc, own_address(&GScr_ActorBeginCriticalSection)},  // GScr_ActorBeginCriticalSection
    {0x5c0, own_address(&GScr_ActorEndCriticalSection)},  // GScr_ActorEndCriticalSection
    {0x5c4, own_address(&GScr_ActorInCriticalSection)},  // GScr_ActorInCriticalSection
    {0x5c8, own_address(&GScr_GetPathRemainingLength)},  // GScr_GetPathRemainingLength
    {0x5cc, own_address(&GScr_ActorPathHasFlag1)},  // GScr_ActorPathHasFlag1
    {0x5d0, own_address(&GScr_ActorAllowedStances)},  // GScr_ActorAllowedStances
    {0x5d4, own_address(&GScr_ActorIsStanceAllowed)},  // GScr_ActorIsStanceAllowed
    {0x5d8, own_address(&GScr_ActorStartReacquire)},  // GScr_ActorStartReacquire
    {0x5dc, own_address(&GScr_ActorHasPendingSentientSlot)},  // GScr_ActorHasPendingSentientSlot
    {0x5e0, own_address(&GScr_CheckGrenadeThrow_entry)},  // GScr_CheckGrenadeThrow
    {0x5e4, own_address(&GScr_ActorThrowGrenade_entry)},  // GScr_ActorThrowGrenade
    {0x5e8, own_address(&GScr_ActorGrabGrenade)},  // GScr_ActorGrabGrenade
    {0x5ec, own_address(&GScr_ActorUseTurret_entry)},  // GScr_ActorUseTurret
    {0x5f0, own_address(&GScr_ActorLeaveTurret)},  // GScr_ActorLeaveTurret
    {0x5f4, own_address(&GScr_ActorCanUseTurret_entry)},  // GScr_ActorCanUseTurret
    {0x5f8, own_address(&GScr_ActorSetTraverseMode)},  // GScr_ActorSetTraverseMode
    {0x5fc, own_address(&GScr_ActorSetAnimMode)},  // GScr_ActorSetAnimMode
    {0x600, own_address(&GScr_ActorSetOrientMode)},  // GScr_ActorSetOrientMode
    {0x604, own_address(&GScr_ActorGetMotionYaw)},  // GScr_ActorGetMotionYaw
    {0x608, own_address(&ScrActor_SetInt_1370)},  // ScrActor_SetInt_1370
    {0x60c, own_address(&GScr_ActorGetTurret)},  // GScr_ActorGetTurret
    {0x610, own_address(&Sys_DebugBreak)},  // Sys_DebugBreak
    {0x614, own_address(&Sys_DebugBreak)},  // Sys_DebugBreak
    {0x618, own_address(&GScr_ActorTeleport)},  // GScr_ActorTeleport
    {0x61c, own_address(&GScr_ActorApplyAnimMove_entry)},  // GScr_ActorApplyAnimMove
    {0x620, own_address(&GScr_ActorAdvanceAnimTime)},  // GScr_ActorAdvanceAnimTime
    {0x624, own_address(&GScr_ActorGetObstacleKind_39c)},  // GScr_ActorGetObstacleKind_39c
    {0x628, own_address(&GScr_ActorGetObstacleYaw_3a8)},  // GScr_ActorGetObstacleYaw_3a8
    {0x62c, own_address(&GScr_ActorGetObstacleKind_2b0)},  // GScr_ActorGetObstacleKind_2b0
    {0x630, own_address(&ScrActor_GetByte_3b1)},  // ScrActor_GetByte_3b1
    {0x634, own_address(&GScr_EmptyStub397)},  // GScr_EmptyStub397
    {0x638, own_address(&GScr_EmptyStub398)},  // GScr_EmptyStub398
    {0x63c, own_address(&GScr_EmptyStub399)},  // GScr_EmptyStub399
    {0x640, own_address(&GScr_ActorNodeCanSeeEnemyNode_entry)},  // GScr_ActorNodeCanSeeEnemyNode
    {0x644, own_address(&GScr_ActorUpdateNearestNode_entry)},  // GScr_ActorUpdateNearestNode
    {0x648, own_address(&GScr_ActorGetPathNodeOrOrigin)},  // GScr_ActorGetPathNodeOrOrigin
    {0x64c, own_address(&GScr_ActorMayMoveToPoint_entry)},  // GScr_ActorMayMoveToPoint
    {0x650, own_address(&GScr_ActorSetFlag2000000)},  // GScr_ActorSetFlag2000000
    {0x584, own_address(&GScr_ActorLookAt)},  // GScr_ActorLookAt
    {0x588, own_address(&GScr_ActorSetLookRates)},  // GScr_ActorSetLookRates
    {0x58c, own_address(&GScr_ActorStopLookAt)},  // GScr_ActorStopLookAt
    {0x8bc, own_address(&GScr_EmptyStub559)},  // GScr_EmptyStub559
    {0x8c0, own_address(&GScr_EmptyStub560)},  // GScr_EmptyStub560
    {0x8c4, own_address(&GScr_EmptyStub561)},  // GScr_EmptyStub561
};

const ImportSlot kGameImports[] = {
    {0x084, own_address(&GScr_GetEntArray)},  // GScr_GetEntArray
    {0x8a8, own_address(&GScr_GetEntScriptObject)},  // GScr_GetEntScriptObject
    {0x8ac, own_address(&GScr_GetLevelEnt8Handle)},  // GScr_GetLevelEnt8Handle
    {0x8b0, own_address(&GScr_GetPlayerHandle)},  // GScr_GetPlayerHandle
    {0x088, own_address(&GScr_GetEntByNum)},  // GScr_GetEntByNum
    {0x08c, own_address(&GScr_GetNode)},  // GScr_GetNode
    {0x090, own_address(&GScr_GetVehicleNode)},  // GScr_GetVehicleNode
    {0x094, own_address(&Vehicle_CountActive)},  // Vehicle_CountActive
    {0x098, own_address(&GScr_EmptyStub38)},  // GScr_EmptyStub38
    {0x09c, own_address(&GScr_PlaySound_entry)},  // GScr_PlaySound
    {0x0a0, own_address(&GScr_PlaySoundAtPosition_entry)},  // GScr_PlaySoundAtPosition
    {0x0a4, own_address(&GScr_PlayLoopSound_entry)},  // GScr_PlayLoopSound
    {0x0a8, own_address(&GScr_PlaySoundMode7_entry)},  // GScr_PlaySoundMode7
    {0x0ac, own_address(&GScr_StopSound)},  // GScr_StopSound
    {0x0b0, own_address(&GScr_IsSoundPlaying)},  // GScr_IsSoundPlaying
    {0x0b4, own_address(&GScr_FreeSound)},  // GScr_FreeSound
    {0x0b8, own_address(&GScr_SetSoundParam)},  // GScr_SetSoundParam
    {0x0bc, own_address(&GScr_PlayerWeaponSound_entry)},  // GScr_PlayerWeaponSound
    {0x0c0, own_address(&GScr_EntList190_Enable)},  // GScr_EntList190_Enable
    {0x0c4, own_address(&GScr_EntList190_Disable)},  // GScr_EntList190_Disable
    {0x0c8, own_address(&GScr_Set95f4b4)},  // GScr_Set95f4b4
    {0x0cc, own_address(&GScr_PlaySoundWithNotify)},  // GScr_PlaySoundWithNotify
    {0x0d0, own_address(&GScr_IsAlive)},  // GScr_IsAlive
    {0x0d4, own_address(&Scr_EntityHandleIsValid)},  // Scr_EntityHandleIsValid
    {0x0d8, own_address(&GScr_IsPlayer)},  // GScr_IsPlayer
    {0x0dc, own_address(&GScr_IsAI)},  // GScr_IsAI
    {0x0e0, own_address(&GScr_IsSentient)},  // GScr_IsSentient
    {0x0e4, own_address(&GScr_IsValidHandle)},  // GScr_IsValidHandle
    {0x0e8, own_address(&GScr_IsNotMinusOne)},  // GScr_IsNotMinusOne
    {0x2d4, own_address(&GScr_LocationalTraceSurface_entry)},  // GScr_LocationalTraceSurface
    {0x2d8, own_address(&GScr_TraceSurfaceType)},  // GScr_TraceSurfaceType
    {0x1e0, own_address(&GScr_SaveCheckpoint)},  // GScr_SaveCheckpoint
    {0x1e8, own_address(&GScr_GetPersistentVector)},  // GScr_GetPersistentVector
    {0x1ec, own_address(&GScr_GetPersistentInt)},  // GScr_GetPersistentInt
    {0x1f0, own_address(&GScr_GetPersistentFloat)},  // GScr_GetPersistentFloat
    {0x1f4, own_address(&GScr_SetPersistentVector)},  // GScr_SetPersistentVector
    {0x1f8, own_address(&GScr_SetPersistentInt)},  // GScr_SetPersistentInt
    {0x1fc, own_address(&GScr_SetPersistentFloat)},  // GScr_SetPersistentFloat
    {0x1e4, own_address(&GScr_RestorePlayerLoadout)},  // GScr_RestorePlayerLoadout
    {0x200, own_address(&GScr_Set8db198)},  // GScr_Set8db198
    {0x204, own_address(&GScr_Set8db19c)},  // GScr_Set8db19c
    {0x208, own_address(&GScr_SetForcedLod)},  // GScr_SetForcedLod
    {0x268, own_address(&GScr_GetSpgVariant)},  // GScr_GetSpgVariant
    {0x26c, own_address(&GScr_SetEnt250Field_f4)},  // GScr_SetEnt250Field_f4
    {0x270, own_address(&GScr_SwapEntField_dc)},  // GScr_SwapEntField_dc
    {0x274, own_address(&GScr_ShakeRumble)},  // GScr_ShakeRumble
    {0x278, own_address(&GScr_ShakeRumbleNamed)},  // GScr_ShakeRumbleNamed
    {0x27c, own_address(&GScr_SetIntMapValue)},  // GScr_SetIntMapValue
    {0x280, own_address(&GScr_GetIntMapValue)},  // GScr_GetIntMapValue
    {0x20c, own_address(&GScr_SetEntFlag2000000)},  // GScr_SetEntFlag2000000
    {0x210, own_address(&GScr_HudObjectives_Set13d0)},  // GScr_HudObjectives_Set13d0
    {0x214, own_address(&GScr_SuppressObjectiveUpdateMessage)},  // GScr_SuppressObjectiveUpdateMessage
    {0x218, own_address(&GScr_HasVisibleModel)},  // GScr_HasVisibleModel
    {0x21c, own_address(&GScr_Set964b84)},  // GScr_Set964b84
    {0x220, own_address(&GScr_SetHudMode)},  // GScr_SetHudMode
    {0x224, own_address(&GScr_HudSetTextByHash_entry)},  // GScr_HudSetTextByHash
    {0x228, own_address(&GScr_HudSetMeterCount)},  // GScr_HudSetMeterCount
    {0x22c, own_address(&GScr_HudComp24_SetE0)},  // GScr_HudComp24_SetE0
    {0x230, own_address(&GScr_HudComp24_SetEcTwo)},  // GScr_HudComp24_SetEcTwo
    {0x234, own_address(&GScr_HudComp24_Push)},  // GScr_HudComp24_Push
    {0x238, own_address(&GScr_HudComp24_Clear)},  // GScr_HudComp24_Clear
    {0x23c, own_address(&GScr_HudSetComponentColor)},  // GScr_HudSetComponentColor
    {0x240, own_address(&GScr_HudComp20_Set10)},  // GScr_HudComp20_Set10
    {0x244, own_address(&GScr_HudComp20_Enable)},  // GScr_HudComp20_Enable
    {0x248, own_address(&GScr_HudComp20_Disable)},  // GScr_HudComp20_Disable
    {0x24c, own_address(&GScr_HudComp20_EnableWith)},  // GScr_HudComp20_EnableWith
    {0x250, own_address(&GScr_HudComp1c_SetMode0)},  // GScr_HudComp1c_SetMode0
    {0x254, own_address(&GScr_HudComp1c_SetMode1)},  // GScr_HudComp1c_SetMode1
    {0x258, own_address(&GScr_HudComp1c_SetMode2)},  // GScr_HudComp1c_SetMode2
    {0x25c, own_address(&GScr_HudComp1c_Off)},  // GScr_HudComp1c_Off
    {0x260, own_address(&GScr_SetAIFlags)},  // GScr_SetAIFlags
    {0x264, own_address(&GScr_SetEntFlag20000000)},  // GScr_SetEntFlag20000000
    {0x380, own_address(&GScr_AnimScriptedNoNotify_entry)},  // GScr_AnimScriptedNoNotify
    {0x384, own_address(&GScr_AnimScripted_entry)},  // GScr_AnimScripted
    {0x388, own_address(&GScr_StopAnimScripted)},  // GScr_StopAnimScripted
    {0x38c, own_address(&GScr_Attach)},  // GScr_Attach
    {0x390, own_address(&GScr_AttachWithOwner)},  // GScr_AttachWithOwner
    {0x394, own_address(&GScr_AttachNoTag)},  // GScr_AttachNoTag
    {0x398, own_address(&GScr_Detach)},  // GScr_Detach
    {0x39c, own_address(&GScr_DetachNoTag)},  // GScr_DetachNoTag
    {0x3a0, own_address(&GScr_DetachAll)},  // GScr_DetachAll
    {0x3a4, own_address(&GScr_GetAttachSize)},  // GScr_GetAttachSize
    {0x3a8, own_address(&GScr_GetAttachModelName)},  // GScr_GetAttachModelName
    {0x3ac, own_address(&GScr_GetAttachTagName)},  // GScr_GetAttachTagName
    {0x3b0, own_address(&GScr_TestEntBit299)},  // GScr_TestEntBit299
    {0x4bc, own_address(&GScr_CheckScriptAnimTreeCvars)},  // GScr_CheckScriptAnimTreeCvars
    {0x3b4, own_address(&GScr_LinkToTagWithOffset)},  // GScr_LinkToTagWithOffset
    {0x3b8, own_address(&GScr_LinkToTag)},  // GScr_LinkToTag
    {0x3bc, own_address(&GScr_LinkTo)},  // GScr_LinkTo
    {0x3c0, own_address(&GScr_PlayerLinkTo)},  // GScr_PlayerLinkTo
    {0x3c4, own_address(&GScr_LinkToTagUnchecked)},  // GScr_LinkToTagUnchecked
    {0x3c8, own_address(&GScr_LinkToUnchecked)},  // GScr_LinkToUnchecked
    {0x3cc, own_address(&GScr_Unlink)},  // GScr_Unlink
    {0x3d0, own_address(&GScr_EnableLinkTo)},  // GScr_EnableLinkTo
    {0x3d4, own_address(&GScr_DoSpawnNamed_entry)},  // GScr_DoSpawnNamed
    {0x3d8, own_address(&GScr_DoSpawn_entry)},  // GScr_DoSpawn
    {0x3dc, own_address(&GScr_ForceSpawnNamed_entry)},  // GScr_ForceSpawnNamed
    {0x3e0, own_address(&GScr_ForceSpawn_entry)},  // GScr_ForceSpawn
    {0x3e4, own_address(&GScr_GetOrigin)},  // GScr_GetOrigin
    {0x3e8, own_address(&GScr_GetEye)},  // GScr_GetEye
    {0x538, own_address(&Scr_StartAnimNotify_entry)},  // FUN_00300530
    {0x53c, own_address(&GScr_GetLocalizedString)},  // GScr_GetLocalizedString
    {0x3ec, own_address(&GScr_UseBy)},  // GScr_UseBy
    {0x3f0, own_address(&GScr_IsTouching_entry)},  // GScr_IsTouching
    {0x3f4, own_address(&GScr_DoorLock)},  // GScr_DoorLock
    {0x3f8, own_address(&GScr_DoorUnlock)},  // GScr_DoorUnlock
    {0x3fc, own_address(&GScr_DoorIsLocked)},  // GScr_DoorIsLocked
    {0x400, own_address(&GScr_Delete)},  // GScr_Delete
    {0x404, own_address(&GScr_SetRefEntFlag_ef)},  // GScr_SetRefEntFlag_ef
    {0x408, own_address(&GScr_SetModel)},  // GScr_SetModel
    {0x40c, own_address(&GScr_SetRefEntByte_ee)},  // GScr_SetRefEntByte_ee
    {0x410, own_address(&GScr_GetNormalHealth)},  // GScr_GetNormalHealth
    {0x414, own_address(&GScr_SetNormalHealth)},  // GScr_SetNormalHealth
    {0x418, own_address(&GScr_DoDamage)},  // GScr_DoDamage
    {0x41c, own_address(&GScr_SetTakeDamage)},  // GScr_SetTakeDamage
    {0x420, own_address(&GScr_SetEntInList960070)},  // GScr_SetEntInList960070
    {0x424, own_address(&GScr_ClearEntFlag400)},  // GScr_ClearEntFlag400
    {0x428, own_address(&GScr_SetEntFlag400)},  // GScr_SetEntFlag400
    {0x42c, own_address(&GScr_SetContents)},  // GScr_SetContents
    {0x430, own_address(&GScr_DisconnectPaths_entry)},  // GScr_DisconnectPaths
    {0x434, own_address(&GScr_ConnectPaths)},  // GScr_ConnectPaths
    {0x438, own_address(&GScr_Turret_SetFlag4)},  // GScr_Turret_SetFlag4
    {0x43c, own_address(&GScr_Turret_ClearFlag4)},  // GScr_Turret_ClearFlag4
    {0x440, own_address(&GScr_TurretCall272_entry)},  // GScr_TurretCall272
    {0x444, own_address(&GScr_Turret_SetMode)},  // GScr_Turret_SetMode
    {0x448, own_address(&GScr_Turret_GetUser)},  // GScr_Turret_GetUser
    {0x44c, own_address(&GScr_GetOwnerHandle)},  // GScr_GetOwnerHandle
    {0x450, own_address(&GScr_Turret_SetTargetEntity)},  // GScr_Turret_SetTargetEntity
    {0x454, own_address(&GScr_Turret_ClearTargetEntity)},  // GScr_Turret_ClearTargetEntity
    {0x458, own_address(&GScr_Turret_SetTeam)},  // GScr_Turret_SetTeam
    {0x45c, own_address(&GScr_Turret_MakeUsable)},  // GScr_Turret_MakeUsable
    {0x460, own_address(&GScr_Turret_MakeUnusable)},  // GScr_Turret_MakeUnusable
    {0x464, own_address(&GScr_Turret_Set54)},  // GScr_Turret_Set54
    {0x468, own_address(&GScr_Turret_SetMaxRange)},  // GScr_Turret_SetMaxRange
    {0x46c, own_address(&GScr_Turret_GetMaxRange)},  // GScr_Turret_GetMaxRange
    {0x470, own_address(&GScr_Turret_GetOwner)},  // GScr_Turret_GetOwner
    {0x474, own_address(&GScr_SetCursorHint)},  // GScr_SetCursorHint
    {0x478, own_address(&GScr_ResetHintCache)},  // GScr_ResetHintCache
    {0x47c, own_address(&GScr_SetHintString)},  // GScr_SetHintString
    {0x480, own_address(&GScr_SetHintString28c)},  // GScr_SetHintString28c
    {0x48c, own_address(&GScr_GetAnimLength)},  // GScr_GetAnimLength
    {0x490, own_address(&GScr_AnimHasNotetrack)},  // GScr_AnimHasNotetrack
    {0x494, own_address(&GScr_ClearAnim)},  // GScr_ClearAnim
    {0x498, own_address(&GScr_SetAnimKnob)},  // GScr_SetAnimKnob
    {0x488, own_address(&GScr_SetAnimKnobAll)},  // GScr_SetAnimKnobAll
    {0x49c, own_address(&GScr_SetAnim)},  // GScr_SetAnim
    {0x4ac, own_address(&GScr_GetAnimTime)},  // GScr_GetAnimTime
    {0x4a0, own_address(&GScr_SetFlaggedAnimKnob)},  // GScr_SetFlaggedAnimKnob
    {0x4a4, own_address(&GScr_SetFlaggedAnimKnobAll)},  // GScr_SetFlaggedAnimKnobAll
    {0x4a8, own_address(&GScr_SetFlaggedAnim)},  // GScr_SetFlaggedAnim
    {0x4b0, own_address(&GScr_UseAnimTree)},  // GScr_UseAnimTree
    {0x4b4, own_address(&GScr_ClearAnimTree)},  // GScr_ClearAnimTree
    {0x484, own_address(&GScr_StartSubtitle)},  // GScr_StartSubtitle
    {0x4b8, own_address(&GScr_DebugDrawAnimTrees)},  // GScr_DebugDrawAnimTrees
    {0x4c0, own_address(&GScr_GetStance)},  // GScr_GetStance
    {0x4c4, own_address(&GScr_MagicGrenade_entry)},  // GScr_MagicGrenade
    {0x4c8, own_address(&GScr_MagicGrenadeDefaultFuse_entry)},  // GScr_MagicGrenadeDefaultFuse
    {0x4cc, own_address(&GScr_MagicGrenadeManual)},  // GScr_MagicGrenadeManual
    {0x4d0, own_address(&GScr_MagicGrenadeManualDefaultFuse)},  // GScr_MagicGrenadeManualDefaultFuse
    {0x4d4, own_address(&GScr_Turret_GetFlag1)},  // GScr_Turret_GetFlag1
    {0x4d8, own_address(&GScr_SetGoalNode)},  // GScr_SetGoalNode
    {0x4dc, own_address(&GScr_GetTagOrigin)},  // GScr_GetTagOrigin
    {0x4e0, own_address(&GScr_GetTagAngles)},  // GScr_GetTagAngles
    {0x4e4, own_address(&GScr_ShellShock)},  // GScr_ShellShock
    {0x4e8, own_address(&GScr_ClearClientVec4f4)},  // GScr_ClearClientVec4f4
    {0x4ec, own_address(&GScr_ViewKick)},  // GScr_ViewKick
    {0x4f0, own_address(&GScr_SetEFlag8000)},  // GScr_SetEFlag8000
    {0x4f4, own_address(&GScr_ClearEFlag8000)},  // GScr_ClearEFlag8000
    {0x4f8, own_address(&GScr_MoveGravity)},  // GScr_MoveGravity
    {0x4fc, own_address(&GScr_LocalToWorldCoords)},  // GScr_LocalToWorldCoords
    {0x500, own_address(&GScr_GetEntityNumber)},  // GScr_GetEntityNumber
    {0x504, own_address(&GScr_SetEntFlag40000)},  // GScr_SetEntFlag40000
    {0x508, own_address(&GScr_ClearEntFlag40000)},  // GScr_ClearEntFlag40000
    {0x50c, own_address(&GScr_ClearEntFlag80000)},  // GScr_ClearEntFlag80000
    {0x510, own_address(&GScr_SetEntFlag80000)},  // GScr_SetEntFlag80000
    {0x514, own_address(&GScr_ScriptMover_SetActorBox)},  // GScr_ScriptMover_SetActorBox
    {0x518, own_address(&GScr_SetSpawnerTeam)},  // GScr_SetSpawnerTeam
    {0x51c, own_address(&GScr_Turret_SetArc_30)},  // GScr_Turret_SetArc_30
    {0x520, own_address(&GScr_Turret_SetArc_38)},  // GScr_Turret_SetArc_38
    {0x524, own_address(&GScr_Turret_SetArc_2c)},  // GScr_Turret_SetArc_2c
    {0x528, own_address(&GScr_Turret_SetArc_34)},  // GScr_Turret_SetArc_34
    {0x52c, own_address(&GScr_Turret_Set3c)},  // GScr_Turret_Set3c
    {0x530, own_address(&GScr_Turret_Set40)},  // GScr_Turret_Set40
    {0x534, own_address(&GScr_SetModelNoFlag)},  // GScr_SetModelNoFlag
    {0x154, own_address(&GScr_GetPadStick)},  // GScr_GetPadStick
    {0x158, own_address(&GScr_Set8d20e0)},  // GScr_Set8d20e0
};

const ImportSlot kGameImports2[] = {
    {0x908, own_address(ScrHudElem_Set_04)},  // ScrHudElem_Set_04
    {0x90c, own_address(ScrHudElem_Get_04)},  // ScrHudElem_Get_04
    {0x910, own_address(ScrHudElem_Set_08)},  // ScrHudElem_Set_08
    {0x914, own_address(ScrHudElem_Get_08)},  // ScrHudElem_Get_08
    {0x918, own_address(ScrHudElem_Set_14)},  // ScrHudElem_Set_14
    {0x91c, own_address(ScrHudElem_Get_14)},  // ScrHudElem_Get_14
    {0x920, own_address(ScrHudElem_Set_18)},  // ScrHudElem_Set_18
    {0x924, own_address(ScrHudElem_Get_18)},  // ScrHudElem_Get_18
    {0x928, own_address(ScrHudElem_SetFloat_6c)},  // ScrHudElem_SetFloat_6c
    {0x92c, own_address(ScrHudElem_GetFloat_6c)},  // ScrHudElem_GetFloat_6c
    {0x930, own_address(ScrHudElem_SetFloat_0c)},  // ScrHudElem_SetFloat_0c
    {0x934, own_address(ScrHudElem_GetFloat_0c)},  // ScrHudElem_GetFloat_0c
    {0x938, own_address(ScrHudElem_SetAlpha)},  // ScrHudElem_SetAlpha
    {0x93c, own_address(ScrHudElem_GetAlpha)},  // ScrHudElem_GetAlpha
    {0x940, own_address(ScrHudElem_SetColorByte0)},  // ScrHudElem_SetColorByte0
    {0x944, own_address(ScrHudElem_GetColorByte0)},  // ScrHudElem_GetColorByte0
    {0x948, own_address(ScrHudElem_SetColorByte1)},  // ScrHudElem_SetColorByte1
    {0x94c, own_address(ScrHudElem_GetColorByte1)},  // ScrHudElem_GetColorByte1
    {0x950, own_address(ScrHudElem_SetColorByte2)},  // ScrHudElem_SetColorByte2
    {0x954, own_address(ScrHudElem_GetColorByte2)},  // ScrHudElem_GetColorByte2
    {0x95c, own_address(&ScrPathNode_SetTargetname)},  // ScrPathNode_SetTargetname
    {0x958, own_address(&ScrPathNode_GetTargetname)},  // ScrPathNode_GetTargetname
    {0x964, own_address(&ScrPathNode_SetTarget)},  // ScrPathNode_SetTarget
    {0x960, own_address(&ScrPathNode_GetTarget)},  // ScrPathNode_GetTarget
    {0x96c, own_address(&ScrPathNode_SetAnimscript)},  // ScrPathNode_SetAnimscript
    {0x968, own_address(&ScrPathNode_GetAnimscript)},  // ScrPathNode_GetAnimscript
    {0x974, own_address(&ScrPathNode_SetScriptNoteworthy)},  // ScrPathNode_SetScriptNoteworthy
    {0x970, own_address(&ScrPathNode_GetScriptNoteworthy)},  // ScrPathNode_GetScriptNoteworthy
    {0x97c, own_address(&ScrPathNode_SetOrigin)},  // ScrPathNode_SetOrigin
    {0x978, own_address(&ScrPathNode_GetOrigin)},  // ScrPathNode_GetOrigin
    {0x984, own_address(&ScrPathNode_SetAngles)},  // ScrPathNode_SetAngles
    {0x980, own_address(&ScrPathNode_GetAngles)},  // ScrPathNode_GetAngles
    {0x98c, own_address(&ScrPathNode_SetRadius)},  // ScrPathNode_SetRadius
    {0x988, own_address(&ScrPathNode_GetRadius)},  // ScrPathNode_GetRadius
    {0x994, own_address(&ScrPathNode_SetSpawnflags)},  // ScrPathNode_SetSpawnflags
    {0x990, own_address(&ScrPathNode_GetSpawnflags)},  // ScrPathNode_GetSpawnflags
    {0x99c, own_address(&ScrPathNode_SetType)},  // ScrPathNode_SetType
    {0x998, own_address(&ScrPathNode_GetType)},  // ScrPathNode_GetType
    {0x9a4, own_address(&ScrVehNode_SetTargetname)},  // ScrVehNode_SetTargetname
    {0x9a0, own_address(&ScrVehNode_GetTargetname)},  // ScrVehNode_GetTargetname
    {0x9ac, own_address(&ScrVehNode_SetTarget)},  // ScrVehNode_SetTarget
    {0x9a8, own_address(&ScrVehNode_GetTarget)},  // ScrVehNode_GetTarget
    {0x9b4, own_address(&ScrVehNode_SetOrigin)},  // ScrVehNode_SetOrigin
    {0x9b0, own_address(&ScrVehNode_GetOrigin)},  // ScrVehNode_GetOrigin
    {0x9bc, own_address(&ScrVehNode_SetAngles)},  // ScrVehNode_SetAngles
    {0x9b8, own_address(&ScrVehNode_GetAngles)},  // ScrVehNode_GetAngles
    {0x9c4, own_address(&ScrVehNode_SetSpeed)},  // ScrVehNode_SetSpeed
    {0x9c0, own_address(&ScrVehNode_GetSpeed)},  // ScrVehNode_GetSpeed
    {0x9cc, own_address(&ScrVehNode_SetLookahead)},  // ScrVehNode_SetLookahead
    {0x9c8, own_address(&ScrVehNode_GetLookahead)},  // ScrVehNode_GetLookahead
    {0x9d4, own_address(&ScrVehNode_SetScriptNoteworthy)},  // ScrVehNode_SetScriptNoteworthy
    {0x9d0, own_address(&ScrVehNode_GetScriptNoteworthy)},  // ScrVehNode_GetScriptNoteworthy
    {0x9dc, own_address(&ScrEnt_SetClassname)},  // ScrEnt_SetClassname
    {0x9d8, own_address(&ScrEnt_GetClassname)},  // ScrEnt_GetClassname
    {0x9e4, own_address(&ScrEnt_SetOrigin)},  // ScrEnt_SetOrigin
    {0x9e0, own_address(&ScrEnt_GetOrigin)},  // ScrEnt_GetOrigin
    {0x9ec, own_address(&ScrEnt_SetModel)},  // ScrEnt_SetModel
    {0x9e8, own_address(&ScrEnt_GetModel)},  // ScrEnt_GetModel
    {0x9f4, own_address(&ScrEnt_SetSpawnflags)},  // ScrEnt_SetSpawnflags
    {0x9f0, own_address(&ScrEnt_GetSpawnflags)},  // ScrEnt_GetSpawnflags
    {0x9fc, 0x00017080},
    {0x9f8, own_address(&ScrEnt_GetFloat_2fc)},
    {0xa04, own_address(&ScrEnt_SetClosespeed)},  // ScrEnt_SetClosespeed
    {0xa00, own_address(&ScrEnt_GetClosespeed)},  // ScrEnt_GetClosespeed
    {0xa0c, own_address(&ScrEnt_SetTarget)},  // ScrEnt_SetTarget
    {0xa08, own_address(&ScrEnt_GetTarget)},  // ScrEnt_GetTarget
    {0xa14, own_address(&ScrEnt_SetTargetname)},  // ScrEnt_SetTargetname
    {0xa10, own_address(&ScrEnt_GetTargetname)},  // ScrEnt_GetTargetname
    {0xa34, own_address(&ScrEnt_SetTeamname)},  // ScrEnt_SetTeamname
    {0xa30, own_address(&ScrEnt_GetTeamname)},  // ScrEnt_GetTeamname
    {0xa3c, own_address(&ScrEnt_SetWait)},  // ScrEnt_SetWait
    {0xa38, own_address(&ScrEnt_GetWait)},  // ScrEnt_GetWait
    {0xa44, own_address(&ScrEnt_SetRandom)},  // ScrEnt_SetRandom
    {0xa40, own_address(&ScrEnt_GetRandom)},  // ScrEnt_GetRandom
    {0xa4c, own_address(&ScrEnt_SetInt_370)},
    {0xa48, own_address(&ScrEnt_GetInt_370)},
    {0xa54, own_address(&ScrEnt_SetHealth)},  // ScrEnt_SetHealth
    {0xa50, own_address(&ScrEnt_GetHealth)},  // ScrEnt_GetHealth
    {0xa5c, own_address(&ScrEnt_SetDmg)},  // ScrEnt_SetDmg
    {0xa58, own_address(&ScrEnt_GetDmg)},  // ScrEnt_GetDmg
    {0xa64, own_address(&ScrEnt_SetAngles)},  // ScrEnt_SetAngles
    {0xa60, own_address(&ScrEnt_GetAngles)},  // ScrEnt_GetAngles
    {0xa74, own_address(&ScrEnt_SetRotate)},  // ScrEnt_SetRotate
    {0xa70, own_address(&ScrEnt_GetRotate)},  // ScrEnt_GetRotate
    {0xa7c, own_address(&ScrEnt_SetDegrees)},  // ScrEnt_SetDegrees
    {0xa78, own_address(&ScrEnt_GetDegrees)},  // ScrEnt_GetDegrees
    {0xa84, 0x00017080},
    {0xa80, own_address(&ScrEnt_GetFloat_2fc)},
    {0xa9c, own_address(&ScrEnt_SetKey)},  // ScrEnt_SetKey
    {0xa98, own_address(&ScrEnt_GetKey)},  // ScrEnt_GetKey
    {0xaa4, own_address(&ScrEnt_SetDelay)},  // ScrEnt_SetDelay
    {0xaa0, own_address(&ScrEnt_GetDelay)},  // ScrEnt_GetDelay
    {0xac4, own_address(&ScrEnt_SetInt_370)},
    {0xac0, own_address(&ScrEnt_GetInt_370)},
    {0xacc, own_address(&ScrEnt_SetSpawnitem)},  // ScrEnt_SetSpawnitem
    {0xac8, own_address(&ScrEnt_GetSpawnitem)},  // ScrEnt_GetSpawnitem
    {0xadc, own_address(&ScrEnt_SetGroupname)},  // ScrEnt_SetGroupname
    {0xad8, own_address(&ScrEnt_GetGroupname)},  // ScrEnt_GetGroupname
    {0xaec, own_address(&ScrEnt_SetScriptNoteworthy)},  // ScrEnt_SetScriptNoteworthy
    {0xae8, own_address(&ScrEnt_GetScriptNoteworthy)},  // ScrEnt_GetScriptNoteworthy
    {0xaf4, own_address(&ScrEnt_SetMaxhealth)},  // ScrEnt_SetMaxhealth
    {0xaf0, own_address(&ScrEnt_GetMaxhealth)},  // ScrEnt_GetMaxhealth
    {0xb04, own_address(&ScrEnt_SetAnimname)},  // ScrEnt_SetAnimname
    {0xb00, own_address(&ScrEnt_GetAnimname)},  // ScrEnt_GetAnimname
    {0xb0c, own_address(&ScrEnt_SetPersistentIndex)},  // ScrEnt_SetPersistentIndex
    {0xb08, own_address(&ScrEnt_GetPersistentIndex)},  // ScrEnt_GetPersistentIndex
    {0xb14, own_address(&ScrEnt_SetTakedamage)},  // ScrEnt_SetTakedamage
    {0xb10, own_address(&ScrEnt_GetTakedamage)},  // ScrEnt_GetTakedamage
    {0xb1c, own_address(&ScrActor_SetFloat_d4)},  // ScrActor_SetFloat_d4
    {0xb18, own_address(&ScrActor_GetFloat_d4)},  // ScrActor_GetFloat_d4
    {0xb24, own_address(&ScrActor_SetFloat_d8)},  // ScrActor_SetFloat_d8
    {0xb20, own_address(&ScrActor_GetFloat_d8)},  // ScrActor_GetFloat_d8
    {0xb2c, own_address(&ScrActor_SetVec_108)},  // ScrActor_SetVec_108
    {0xb28, own_address(&ScrActor_GetVec_108)},  // ScrActor_GetVec_108
    {0xb34, own_address(&ScrActor_SetVec_114)},  // ScrActor_SetVec_114
    {0xb30, own_address(&ScrActor_GetVec_114)},  // ScrActor_GetVec_114
    {0xb3c, own_address(&ScrActor_SetVec_120)},  // ScrActor_SetVec_120
    {0xb38, own_address(&ScrActor_GetVec_120)},  // ScrActor_GetVec_120
    {0xb44, own_address(&ScrActor_SetFloat_828)},  // ScrActor_SetFloat_828
    {0xb40, own_address(&ScrActor_GetFloat_828)},  // ScrActor_GetFloat_828
    {0xb4c, own_address(&ScrActor_SetFloat_82c)},  // ScrActor_SetFloat_82c
    {0xb48, own_address(&ScrActor_GetFloat_82c)},  // ScrActor_GetFloat_82c
    {0xb54, own_address(&ScrActor_SetFloat_824)},  // ScrActor_SetFloat_824
    {0xb50, own_address(&ScrActor_GetFloat_824)},  // ScrActor_GetFloat_824
    {0xb5c, own_address(&ScrActor_SetInt_830)},  // ScrActor_SetInt_830
    {0xb58, own_address(&ScrActor_GetInt_830)},  // ScrActor_GetInt_830
    {0xb64, own_address(&ScrActor_SetInt_7b0)},  // ScrActor_SetInt_7b0
    {0xb60, own_address(&ScrActor_GetInt_7b0)},  // ScrActor_GetInt_7b0
    {0xb6c, own_address(&ScrActor_SetInt_7b4)},  // ScrActor_SetInt_7b4
    {0xb68, own_address(&ScrActor_GetInt_7b4)},  // ScrActor_GetInt_7b4
    {0xb74, own_address(&ScrActor_SetShort_7dc)},  // ScrActor_SetShort_7dc
    {0xb70, own_address(&ScrActor_GetShort_7dc)},  // ScrActor_GetShort_7dc
    {0xb7c, own_address(&ScrActor_SetFloat_7b8)},  // ScrActor_SetFloat_7b8
    {0xb78, own_address(&ScrActor_GetFloat_7b8)},  // ScrActor_GetFloat_7b8
    {0xb84, own_address(&ScrActor_SetFloat_7bc)},  // ScrActor_SetFloat_7bc
    {0xb80, own_address(&ScrActor_GetFloat_7bc)},  // ScrActor_GetFloat_7bc
    {0xb8c, own_address(&ScrActor_SetInt_228)},  // ScrActor_SetInt_228
    {0xb88, own_address(&ScrActor_GetInt_228)},  // ScrActor_GetInt_228
    {0xb94, own_address(&ScrActor_SetInt_22c)},  // ScrActor_SetInt_22c
    {0xb90, own_address(&ScrActor_GetInt_22c)},  // ScrActor_GetInt_22c
    {0xb9c, own_address(&ScrActor_SetVec_234)},  // ScrActor_SetVec_234
    {0xb98, own_address(&ScrActor_GetVec_234)},  // ScrActor_GetVec_234
    {0xba4, own_address(&ScrActor_SetInt_230)},  // ScrActor_SetInt_230
    {0xba0, own_address(&ScrActor_GetInt_230)},  // ScrActor_GetInt_230
    {0xbac, own_address(&ScrActor_SetInt_240)},  // ScrActor_SetInt_240
    {0xba8, own_address(&ScrActor_GetInt_240)},  // ScrActor_GetInt_240
    {0xbb4, own_address(&ScrActor_SetInt_194)},  // ScrActor_SetInt_194
    {0xbb0, own_address(&ScrActor_GetInt_194)},  // ScrActor_GetInt_194
    {0xbbc, own_address(&ScrActor_SetFloat_7a4)},  // ScrActor_SetFloat_7a4
    {0xbb8, own_address(&ScrActor_GetFloat_7a4)},  // ScrActor_GetFloat_7a4
    {0xbc4, own_address(&ScrActor_SetFloat_154)},  // ScrActor_SetFloat_154
    {0xbc0, own_address(&ScrActor_GetFloat_154)},  // ScrActor_GetFloat_154
    {0xbcc, own_address(&ScrActor_SetFloat_7e4)},  // ScrActor_SetFloat_7e4
    {0xbc8, own_address(&ScrActor_GetFloat_7e4)},  // ScrActor_GetFloat_7e4
    {0xbd4, own_address(&ScrActor_SetInt_7e8)},  // ScrActor_SetInt_7e8
    {0xbd0, own_address(&ScrActor_GetInt_7e8)},  // ScrActor_GetInt_7e8
    {0xbdc, own_address(&ScrActor_SetInt_7ec)},  // ScrActor_SetInt_7ec
    {0xbd8, own_address(&ScrActor_GetInt_7ec)},  // ScrActor_GetInt_7ec
    {0xbe4, own_address(&ScrActor_SetInt_1308)},  // ScrActor_SetInt_1308
    {0xbe0, own_address(&ScrActor_GetInt_1308)},  // ScrActor_GetInt_1308
    {0xbec, own_address(&ScrActor_SetString_ec)},  // ScrActor_SetString_ec
    {0xbe8, own_address(&ScrActor_GetString_ec)},  // ScrActor_GetString_ec
    {0xbf4, own_address(&ScrActor_SetInt_f0)},  // ScrActor_SetInt_f0
    {0xbf0, own_address(&ScrActor_GetInt_f0)},  // ScrActor_GetInt_f0
    {0xbfc, own_address(&ScrActor_SetInt_f4)},  // ScrActor_SetInt_f4
    {0xbf8, own_address(&ScrActor_GetInt_f4)},  // ScrActor_GetInt_f4
    {0xc04, own_address(&ScrActor_SetString_f8)},  // ScrActor_SetString_f8
    {0xc00, own_address(&ScrActor_GetString_f8)},  // ScrActor_GetString_f8
    {0xc0c, own_address(&ScrActor_SetInt_7d8)},  // ScrActor_SetInt_7d8
    {0xc08, own_address(&ScrActor_GetInt_7d8)},  // ScrActor_GetInt_7d8
    {0xc14, own_address(&ScrActor_SetFloat_130c)},  // ScrActor_SetFloat_130c
    {0xc10, own_address(&ScrActor_GetFloat_130c)},  // ScrActor_GetFloat_130c
    {0xc1c, own_address(&ScrActor_SetRef_1310)},  // ScrActor_SetRef_1310
    {0xc18, own_address(&ScrActor_GetRef_1310)},  // ScrActor_GetRef_1310
    {0xc24, own_address(&ScrActor_SetInt_131c)},  // ScrActor_SetInt_131c
    {0xc20, own_address(&ScrActor_GetInt_131c)},  // ScrActor_GetInt_131c
    {0xc2c, own_address(&ScrActor_SetInt_1328)},  // ScrActor_SetInt_1328
    {0xc28, own_address(&ScrActor_GetInt_1328)},  // ScrActor_GetInt_1328
    {0xc34, own_address(&ScrActor_SetRef_12a0)},  // ScrActor_SetRef_12a0
    {0xc30, own_address(&ScrActor_GetRef_12a0)},  // ScrActor_GetRef_12a0
    {0xc3c, own_address(&ScrActor_SetInt_cc)},  // ScrActor_SetInt_cc
    {0xc38, own_address(&ScrActor_GetInt_cc)},  // ScrActor_GetInt_cc
    {0xc44, own_address(&ScrActor_SetByte_1374)},  // ScrActor_SetByte_1374
    {0xc40, own_address(&ScrActor_GetByte_1374)},  // ScrActor_GetByte_1374
    {0xc4c, own_address(&ScrActor_SetByte_7de)},  // ScrActor_SetByte_7de
    {0xc48, own_address(&ScrActor_GetByte_7de)},  // ScrActor_GetByte_7de
    {0xc54, own_address(&ScrActor_SetInt_1378)},  // ScrActor_SetInt_1378
    {0xc50, own_address(&ScrActor_GetInt_1378)},  // ScrActor_GetInt_1378
    {0xc5c, own_address(&ScrActor_SetInt_137c)},  // ScrActor_SetInt_137c
    {0xc58, own_address(&ScrActor_GetInt_137c)},  // ScrActor_GetInt_137c
    {0xc64, own_address(&ScrActor_SetString_1384)},  // ScrActor_SetString_1384
    {0xc60, own_address(&ScrActor_GetString_1384)},  // ScrActor_GetString_1384
    {0xc6c, own_address(&ScrActor_SetString_1388)},  // ScrActor_SetString_1388
    {0xc68, own_address(&ScrActor_GetString_1388)},  // ScrActor_GetString_1388
    {0xc74, own_address(&ScrActor_SetString_138c)},  // ScrActor_SetString_138c
    {0xc70, own_address(&ScrActor_GetString_138c)},  // ScrActor_GetString_138c
    {0xc7c, own_address(&ScrActor_SetString_2c0)},  // ScrActor_SetString_2c0
    {0xc78, own_address(&ScrActor_GetString_2c0)},  // ScrActor_GetString_2c0
    {0xc84, own_address(&ScrActor_SetInt_184)},  // ScrActor_SetInt_184
    {0xc80, own_address(&ScrActor_GetInt_184)},  // ScrActor_GetInt_184
    {0xc8c, own_address(&ScrSentient_SetTeam)},  // ScrSentient_SetTeam
    {0xc88, own_address(&ScrSentient_GetTeam)},  // ScrSentient_GetTeam
    {0xc94, own_address(&ScrSentient_SetInt_28)},  // ScrSentient_SetInt_28
    {0xc90, own_address(&ScrSentient_GetInt_28)},  // ScrSentient_GetInt_28
    {0xc9c, own_address(&ScrSentient_SetFloat_2c)},  // ScrSentient_SetFloat_2c
    {0xc98, own_address(&ScrSentient_GetFloat_2c)},  // ScrSentient_GetFloat_2c
    {0xca4, own_address(&ScrSentient_SetRef_48)},  // ScrSentient_SetRef_48
    {0xca0, own_address(&ScrSentient_GetRef_48)},  // ScrSentient_GetRef_48
    {0xcac, own_address(&ScrSentient_SetGoalNode)},  // ScrSentient_SetGoalNode
    {0xca8, own_address(&ScrSentient_GetGoalNode)},  // ScrSentient_GetGoalNode
    {0xcb4, own_address(&ScrSentient_SetFloat_18)},  // ScrSentient_SetFloat_18
    {0xcb0, own_address(&ScrSentient_GetFloat_18)},  // ScrSentient_GetFloat_18
    {0xcbc, own_address(&ScrSentient_SetGoalArc)},  // ScrSentient_SetGoalArc
    {0xcb8, own_address(&ScrSentient_GetGoalArc)},  // ScrSentient_GetGoalArc
    {0xcc4, own_address(&ScrSentient_SetRef_3c)},  // ScrSentient_SetRef_3c
    {0xcc0, own_address(&ScrSentient_GetRef_3c)},  // ScrSentient_GetRef_3c
    {0xccc, own_address(&ScrSentient_SetInt_30)},  // ScrSentient_SetInt_30
    {0xcc8, own_address(&ScrSentient_GetInt_30)},  // ScrSentient_GetInt_30
    {0xcd4, own_address(&ScrSentient_SetInt_34)},  // ScrSentient_SetInt_34
    {0xcd0, own_address(&ScrSentient_GetInt_34)},  // ScrSentient_GetInt_34
    {0xcec, own_address(&ScrSentient_SetInt_38)},  // ScrSentient_SetInt_38
    {0xce8, own_address(&ScrSentient_GetInt_38)},  // ScrSentient_GetInt_38
};

const ImportSlot kLevelImports[] = {
    {0x000, 0xbfbfbfbf},
    {0x000, own_address(&GScr_EmptyStub0)},  // GScr_EmptyStub0
    {0x004, own_address(&GScr_EmptyStub1)},  // GScr_EmptyStub1
    {0x008, own_address(&GScr_SendGameMessage)},  // GScr_SendGameMessage
    {0x00c, own_address(&GScr_SendGameMessageBold)},  // GScr_SendGameMessageBold
    {0x010, own_address(&GScr_Print3D)},  // GScr_Print3D
    {0x014, own_address(&GScr_DebugLine)},  // GScr_DebugLine
    {0x018, own_address(&title_sprintf)},  // _sprintf (our CRT)
    {0x01c, own_address(&title_strcpy)},  // LIBCMT::_strcpy
    {0x020, own_address(&title_strncpy)},  // _strncpy (our CRT)
    {0x034, own_address(&Scr_SaveThreadLocals)},  // Scr_SaveThreadLocals
    {0x038, own_address(&GScr_EmptyStub14)},  // GScr_EmptyStub14
    {0x03c, own_address(&Scr_WaitRecord15)},  // Scr_WaitRecord15
    {0x040, own_address(&Scr_WaitRecord16)},  // Scr_WaitRecord16
    {0x02c, own_address(&Scr_CreateThread)},  // Scr_CreateThread
    {0x028, own_address(&Scr_BeginThread)},  // Scr_BeginThread
    {0x030, own_address(&Scr_BeginThreadEndOn)},  // Scr_BeginThreadEndOn
    {0x044, own_address(&Scr_WaitRecord17)},  // Scr_WaitRecord17
    {0x048, own_address(&Scr_EndOn)},  // Scr_EndOn
    {0x04c, own_address(&Scr_InThread)},  // Scr_InThread
    {0x050, own_address(&Scr_ThreadHandleIsValid)},  // Scr_ThreadHandleIsValid
    {0x054, own_address(&GScr_EmptyStub21)},  // GScr_EmptyStub21
    {0x058, own_address(&GScr_EmptyStub22)},  // GScr_EmptyStub22
    {0x05c, own_address(&Scr_GetCurrentThreadHandle)},  // Scr_GetCurrentThreadHandle
    {0x060, own_address(&Scr_TerminateThread)},  // Scr_TerminateThread
    {0x0ec, own_address(&Scr_RandomInt)},  // Scr_RandomInt
    {0x0f0, own_address(&Scr_RandomFloat)},  // Scr_RandomFloat
    {0x0f4, own_address(&Scr_RandomIntRange)},  // Scr_RandomIntRange
    {0x0f8, own_address(&Scr_RandomFloatRange)},  // Scr_RandomFloatRange
    {0x0fc, own_address(&Scr_SinCosDegrees)},  // Scr_SinCosDegrees
    {0x100, own_address(&Scr_Sin)},  // Scr_Sin
    {0x104, own_address(&Scr_Cos)},  // Scr_Cos
    {0x108, own_address(&Scr_Tan)},  // Scr_Tan
    {0x10c, own_address(&Scr_Asin)},  // Scr_Asin
    {0x110, own_address(&Scr_Acos)},  // Scr_Acos
    {0x114, own_address(&Scr_Atan)},  // Scr_Atan
    {0x118, own_address(&Scr_Atan2)},  // Scr_Atan2
    {0x11c, own_address(&Scr_Log)},  // Scr_Log
    {0x120, own_address(&Scr_Abs)},  // Scr_Abs
    {0x124, own_address(&Scr_Sqrt)},  // Scr_Sqrt
    {0x128, own_address(&Scr_Distance)},  // Scr_Distance
    {0x12c, own_address(&Scr_DistanceSquared)},  // Scr_DistanceSquared
    {0x130, own_address(&Scr_Length)},  // Scr_Length
    {0x134, own_address(&Scr_LengthSquared)},  // Scr_LengthSquared
    {0x138, own_address(&Scr_Closer)},  // Scr_Closer
    {0x13c, own_address(&Scr_VectorDot)},  // Scr_VectorDot
    {0x140, own_address(&Scr_VectorNormalize)},  // Scr_VectorNormalize
    {0x144, own_address(&Scr_VectorToAngles)},  // Scr_VectorToAngles
    {0x148, own_address(&Scr_AnglesToUp)},  // Scr_AnglesToUp
    {0x14c, own_address(&Scr_AnglesToRight)},  // Scr_AnglesToRight
    {0x150, own_address(&Scr_AnglesToForward)},  // Scr_AnglesToForward
    {0x198, own_address(&GScr_GetCvar)},  // GScr_GetCvar
    {0x19c, own_address(&GScr_GetCvarInt)},  // GScr_GetCvarInt
    {0x1a0, own_address(&GScr_GetCvarFloat)},  // GScr_GetCvarFloat
    {0x1a4, own_address(&GScr_SetCvar)},  // GScr_SetCvar
    {0x1a8, own_address(&GScr_SetCvarInt)},  // GScr_SetCvarInt
    {0x1ac, own_address(&GScr_SetCvarFloat)},  // GScr_SetCvarFloat
    {0x1b0, own_address(&GScr_Spawn)},  // GScr_Spawn
    {0x1b4, own_address(&GScr_SpawnWithFlags)},  // GScr_SpawnWithFlags
    {0x1b8, own_address(&GScr_SpawnBoxEntity)},  // GScr_SpawnBoxEntity
    {0x1bc, own_address(&GScr_SpawnVehicle)},  // GScr_SpawnVehicle
    {0x1c0, own_address(&GScr_SpawnTurret)},  // GScr_SpawnTurret
    {0x1c4, own_address(&GScr_SpawnTurretWithAngles)},  // GScr_SpawnTurretWithAngles
    {0x1c8, own_address(&GScr_MusicPlay)},  // GScr_MusicPlay
    {0x1cc, own_address(&GScr_MusicStop)},  // GScr_MusicStop
    {0x1d0, own_address(&GScr_AmbientPlay)},  // GScr_AmbientPlay
    {0x1d4, own_address(&GScr_SetMusicTrack)},  // GScr_SetMusicTrack
    {0x1d8, own_address(&GScr_SoundBankLoad)},  // GScr_SoundBankLoad
    {0x1dc, own_address(&GScr_SoundBankUnload)},  // GScr_SoundBankUnload
    {0x284, own_address(&GScr_GetTime)},  // GScr_GetTime
    {0x288, own_address(&GScr_GetFrameTime)},  // GScr_GetFrameTime
    {0x28c, own_address(&GScr_GetDifficulty)},  // GScr_GetDifficulty
    {0x290, own_address(&GScr_GetAI)},  // GScr_GetAI
    {0x294, own_address(&GScr_GetAIAll)},  // GScr_GetAIAll
    {0x298, own_address(&GScr_GetSpawnerArray)},  // GScr_GetSpawnerArray
    {0x29c, own_address(&GScr_GetSpawnerTeamArray)},  // GScr_GetSpawnerTeamArray
    {0x2a0, own_address(&GScr_GetWeaponModel)},  // GScr_GetWeaponModel
    {0x2a4, own_address(&GScr_GetWeaponDisplayName)},  // GScr_GetWeaponDisplayName
    {0x2a8, own_address(&GScr_GetWeaponClassname)},  // GScr_GetWeaponClassname
    {0x2ac, own_address(&GScr_GetWeaponClassname2)},  // GScr_GetWeaponClassname2
    {0x2b0, own_address(&GScr_GetKeyBinding)},  // GScr_GetKeyBinding
    {0x2bc, own_address(&GScr_TransformLocalOffset_entry)},  // GScr_TransformLocalOffset
    {0x2c0, own_address(&GScr_GetAnimStartAngles_entry)},  // GScr_GetAnimStartAngles
    {0x2c4, own_address(&GScr_GetAnimWorldMoveDelta_entry)},  // GScr_GetAnimWorldMoveDelta
    {0x2c8, own_address(&GScr_GetMoveDelta_entry)},  // GScr_GetMoveDelta
    {0x2cc, own_address(&GScr_GetAngleDelta_entry)},  // GScr_GetAngleDelta
    {0x2dc, own_address(&GScr_EmptyStub183)},  // GScr_EmptyStub183
    {0x2e0, own_address(&GScr_EmptyStub184)},  // GScr_EmptyStub184
    {0x2e4, own_address(&GScr_SetConfigstring9Float)},  // GScr_SetConfigstring9Float
    {0x2e8, own_address(&GScr_RadiusDamage_entry)},  // GScr_RadiusDamage
    {0x2ec, own_address(&GScr_RadiusDamageFromEntity_entry)},  // GScr_RadiusDamageFromEntity
    {0x2f0, own_address(&GScr_SetRadiusDamageFlag)},  // GScr_SetRadiusDamageFlag
    {0x2f4, own_address(&GScr_MissionSuccess)},  // GScr_MissionSuccess
    {0x2f8, own_address(&GScr_MissionFailed)},  // GScr_MissionFailed
    {0x2fc, own_address(&GScr_ChangeLevelWithDelay)},  // GScr_ChangeLevelWithDelay
    {0x300, own_address(&GScr_ChangeLevel)},  // GScr_ChangeLevel
    {0x304, own_address(&GScr_Earthquake)},  // GScr_Earthquake
    {0x308, own_address(&GScr_SetCompassMarkersEnabled)},  // GScr_SetCompassMarkersEnabled
    {0x30c, own_address(&G_SetupVehiclePool)},  // G_SetupVehiclePool_Thunk: a jmp to it
    {0x2d0, own_address(&GScr_SetMissileMaxLifetime)},  // GScr_SetMissileMaxLifetime
    {0x314, own_address(&GScr_RandomTracer)},  // GScr_RandomTracer
    {0x318, own_address(&GScr_MagicBullet_entry)},  // GScr_MagicBullet
    {0x31c, own_address(&GScr_EmptyStub199)},  // GScr_EmptyStub199
    {0x320, own_address(&GScr_EmptyStub200)},  // GScr_EmptyStub200
    {0x324, own_address(&GScr_GetModelBoneCount)},  // GScr_GetModelBoneCount
    {0x328, own_address(&GScr_GetModelBoneName)},  // GScr_GetModelBoneName
    {0x32c, own_address(&GScr_WeaponGetFloat_5dc)},  // GScr_WeaponGetFloat_5dc
    {0x330, own_address(&GScr_WeaponGetTableValue_5a0)},  // GScr_WeaponGetTableValue_5a0
    {0x334, own_address(&GScr_WeaponGet_6d4)},  // GScr_WeaponGet_6d4
    {0x338, own_address(&GScr_WeaponGet_6d8)},  // GScr_WeaponGet_6d8
    {0x33c, own_address(&GScr_WeaponType)},  // GScr_WeaponType
    {0x340, own_address(&GScr_BadPlace_Delete)},  // GScr_BadPlace_Delete
    {0x344, own_address(&GScr_BadPlace_Cylinder)},  // GScr_BadPlace_Cylinder
    {0x348, own_address(&GScr_BadPlace_Arc)},  // GScr_BadPlace_Arc
    {0x34c, own_address(&GScr_NewHudElem)},  // GScr_NewHudElem
    {0x350, own_address(&Actor_FreeAllCorpses)},  // Actor_FreeAllCorpses
    {0x7a4, own_address(&GScr_SetHudVisible)},  // GScr_SetHudVisible
    {0x7a8, own_address(&GScr_HudSetText)},  // GScr_HudSetText
    {0x7ac, own_address(&GScr_HudSetShader)},  // GScr_HudSetShader
    {0x7b0, own_address(&GScr_HudSetTimer4)},  // GScr_HudSetTimer4
    {0x7b4, own_address(&GScr_HudSetTimer)},  // GScr_HudSetTimer
    {0x7b8, own_address(&GScr_HudSetTimer6)},  // GScr_HudSetTimer6
    {0x7bc, own_address(&GScr_HudSetTimer7)},  // GScr_HudSetTimer7
    {0x7c0, own_address(&GScr_HudSetShaderTimer8)},  // GScr_HudSetShaderTimer8
    {0x7c4, own_address(&GScr_HudSetShaderTimer9)},  // GScr_HudSetShaderTimer9
    {0x7c8, own_address(&GScr_HudSetValue)},  // GScr_HudSetValue
    {0x7cc, own_address(&GScr_HudFadeOverTime)},  // GScr_HudFadeOverTime
    {0x7d0, own_address(&GScr_HudScaleOverTime)},  // GScr_HudScaleOverTime
    {0x7d4, own_address(&GScr_HudMoveOverTime)},  // GScr_HudMoveOverTime
    {0x7d8, own_address(&GScr_HudClear)},  // GScr_HudClear
    {0x7dc, own_address(&GScr_HudDestroy)},  // GScr_HudDestroy
    {0x7e0, own_address(&ScrIface_Malloc)},  // ScrIface_Malloc
    {0x7e4, own_address(&ScrIface_Free)},  // ScrIface_Free
    {0x7e8, own_address(&ScrIface_Alloc)},  // ScrIface_Alloc
    {0x7ec, own_address(&ScrIface_SmallFree)},  // ScrIface_SmallFree
    {0x7f0, own_address(&title_memset)},  // memset2 (our CRT)
    {0x8a0, own_address(&title_atol)},  // _atol (our CRT)
    {0x89c, own_address(&ScrIface_atof)},  // ScrIface_atof
    {0x7f4, own_address(&GScr_ReturnFalse509)},  // GScr_ReturnFalse509
    {0x7f8, own_address(&GScr_ReturnFalse510)},  // GScr_ReturnFalse510
    {0x7fc, own_address(&GScr_ReturnFalse511)},  // GScr_ReturnFalse511
    {0x8a4, own_address(&ScrIface_HashKeyNoCase)},  // ScrIface_HashKeyNoCase
    {0x378, own_address(&GScr_EmptyStub222)},  // GScr_EmptyStub222
    {0x37c, own_address(&GScr_EmptyStub223)},  // GScr_EmptyStub223
    {0x354, own_address(&GScr_FindZone)},  // GScr_FindZone
    {0x358, own_address(&GScr_GetEntZone)},  // GScr_GetEntZone
    {0x35c, own_address(&GScr_GetZoneAtPoint)},  // GScr_GetZoneAtPoint
    {0x360, own_address(&GScr_IsZoneLoaded)},  // GScr_IsZoneLoaded
    {0x364, own_address(&GScr_ZoneSetField34)},  // GScr_ZoneSetField34
    {0x368, own_address(&GScr_ZoneClearField34)},  // GScr_ZoneClearField34
    {0x36c, own_address(&GScr_LoadZoneSync)},  // GScr_LoadZoneSync
    {0x370, own_address(&GScr_EmptyStub220)},  // GScr_EmptyStub220
    {0x374, own_address(&GScr_EmptyStub221)},  // GScr_EmptyStub221
    {0x064, own_address(&Scr_QueueNotify)},  // Scr_QueueNotify
    {0x068, own_address(&Scr_QueueNotifyCallback26)},  // Scr_QueueNotifyCallback26
    {0x074, own_address(&Scr_QueueNotifyCallback29)},  // Scr_QueueNotifyCallback29
    {0x06c, own_address(&Scr_QueueNotifyWithString)},  // Scr_QueueNotifyWithString
    {0x070, own_address(&Scr_QueueNotifyWith2Args)},  // Scr_QueueNotifyWith2Args
    {0x078, own_address(&GScr_EntHasNotifyListener)},  // GScr_EntHasNotifyListener
    {0x07c, own_address(&GScr_SetEntVar)},  // GScr_SetEntVar
    {0x080, own_address(&GScr_GetEntVar)},  // GScr_GetEntVar
    {0x8c8, own_address(&GScr_SetFogDistances)},  // GScr_SetFogDistances
    {0x8cc, own_address(&GScr_SetFogColorBytes)},  // GScr_SetFogColorBytes
    {0x8d0, own_address(&GScr_SetFogColor)},  // GScr_SetFogColor
    {0x8d4, own_address(&GScr_SetFogPassFinalAlpha)},  // GScr_SetFogPassFinalAlpha
    {0x8d8, own_address(&GScr_SetFogPassStep)},  // GScr_SetFogPassStep
    {0x8dc, own_address(&GScr_SetFogPassAlpha)},  // GScr_SetFogPassAlpha
    {0x8e0, own_address(&GScr_SetFogPassCount)},  // GScr_SetFogPassCount
    {0x8e4, own_address(&NameMap_Insert)},  // NameMap_Insert
    {0x8e8, own_address(&IntHashMap_FindValue)},  // IntHashMap_FindValue
    {0x8ec, own_address(&ScrIface_HashStringNoCase)},  // ScrIface_HashStringNoCase
};

const ImportSlot kLevelImports2[] = {
    {0x8b4, own_address(&GScr_PushToList95fdf0)},  // GScr_PushToList95fdf0
    {0x8b8, own_address(&GScr_CopyStringFromEngine)},  // GScr_CopyStringFromEngine
    {0x79c, own_address(&GScr_PhysicsLaunch)},  // GScr_PhysicsLaunch
    {0x7a0, own_address(&GScr_PhysicsStop)},  // GScr_PhysicsStop
    {0x2b4, own_address(&GScr_EmptyStub173)},  // GScr_EmptyStub173
    {0x2b8, own_address(&GScr_EmptyStub174)},  // GScr_EmptyStub174
    {0x8f0, own_address(&G_CreateCodNote)},  // G_CreateCodNote
    {0x8f4, own_address(&G_StartCodNote)},  // G_StartCodNote
    {0x8f8, own_address(&G_FreeCodNote)},  // G_FreeCodNote
    {0x8fc, own_address(&G_FreeAllCodNotes)},  // G_FreeAllCodNotes
    {0x904, own_address(&GScr_ClearEntFlag4000000)},  // GScr_ClearEntFlag4000000
    {0x900, own_address(&GScr_SetEntFlag4000000)},  // GScr_SetEntFlag4000000
};

const ImportSlot kLevelImports3[] = {
    {0xcf0, own_address(&cdOcean_SetBaseHeight)},  // cdOcean_SetBaseHeight
    {0xcf4, own_address(&cdOcean_SetWaveSetVec04)},  // cdOcean_SetWaveSetVec04
    {0xcf8, own_address(&cdOcean_SetWaveSetVec30)},  // cdOcean_SetWaveSetVec30
    {0xcfc, own_address(&cdOcean_SetWaveSetVec10)},  // cdOcean_SetWaveSetVec10
    {0xd00, own_address(&cdOcean_SetWaveOrigin)},  // cdOcean_SetWaveOrigin
    {0xd04, own_address(&cdOcean_SetWaveDirection)},  // cdOcean_SetWaveDirection
    {0xd08, own_address(&cdOcean_SetWaveFalloff)},  // cdOcean_SetWaveFalloff
    {0xd0c, own_address(&cdOcean_SetWaveLength)},  // cdOcean_SetWaveLength
    {0xd10, own_address(&cdOcean_SetWaveAmplitude)},  // cdOcean_SetWaveAmplitude
    {0xd14, own_address(&cdOcean_SetWavePhase)},  // cdOcean_SetWavePhase
    {0xd18, own_address(&cdOcean_SetWaveSpeed)},  // cdOcean_SetWaveSpeed
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
