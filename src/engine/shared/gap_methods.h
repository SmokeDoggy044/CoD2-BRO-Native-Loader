#pragma once

// Gap methods (own engine host 0.3): short functions Ghidra had no entry for, reached through vtables,
// callbacks and title addresses. Accessors, stubs, thunks and adjustors of classes whose own files come later.

#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// UI elements, texts and menus
uint32_t XB_THISCALL UIElement_GetFlagBit2(uint8_t* self);  // 0x00011330
uint8_t XB_THISCALL UITextBase_HasFlag2(uint8_t* self);  // 0x000113a0
ScriptString* XB_THISCALL UITextBase_SetString54(uint8_t* self, const char* text);  // 0x000114b0
ScriptString* XB_THISCALL UITextBase_SetString1C(uint8_t* self, const char* text);  // 0x00011530
void XB_THISCALL UITextBase_SetFloat2C(uint8_t* self, uint32_t bits);  // 0x00011580
void XB_THISCALL UITextBase_SetField58(uint8_t* self, uint32_t value);  // 0x000115c0
float XB_THISCALL UITextBase_GetFloat38(uint8_t* self);  // 0x00011610
uint32_t XB_THISCALL UITextBase_GetField18(uint8_t* self);  // 0x00011640
uint32_t* XB_THISCALL UITextBase_GetField50(uint8_t* self, uint32_t* out);  // 0x00011650
void XB_THISCALL UITextBase_SetField50(uint8_t* self, uint32_t value);  // 0x003159a0
float XB_THISCALL UITextBase_GetFloat2C(uint8_t* self);  // 0x00011690
uint8_t XB_THISCALL UITextBase_TestFlags5C(uint8_t* self, uint32_t mask);  // 0x000116a0
uint8_t* XB_THISCALL UITextLine_ElementCtor(uint8_t* self);  // 0x00011750
void XB_THISCALL UIText_SetFloat64(uint8_t* self, uint32_t bits);  // 0x000118c0
float XB_THISCALL UIElement_GetFloat14(uint8_t* self);  // 0x00011a20
float XB_THISCALL UIElement_GetFloat18(uint8_t* self);  // 0x00011a30
float XB_THISCALL UIElement_GetFloat30(uint8_t* self);  // 0x00011a50
extern "C" void UIMenuItem_InnerSlot15(void);  // 0x00011c00
extern "C" void UIMenuItem_InnerSlot1(void);  // 0x00011c10
extern "C" void UIMenuItem_InnerSlot27(void);  // 0x00011c70
extern "C" void UIMenuItem_InnerSlot30(void);  // 0x00011cc0
extern "C" void UIMenuItem_InnerSlot24(void);  // 0x00011d80
extern "C" void UIMenuItem_InnerSlot41(void);  // 0x00011e00
uint8_t XB_THISCALL UIMenuItem_GetByte16(uint8_t* self);  // 0x00011c40
void XB_THISCALL UIMenu_RestoreSelectionVirtual(uint8_t* self, uint32_t);  // 0x00011ee0
uint8_t XB_THISCALL UIMenu_GetByte33(uint8_t* self);  // 0x00011f20
uint32_t XB_THISCALL UIMenu_GetField8(uint8_t* self);  // 0x00011f30
uint8_t XB_THISCALL UIElement_GetByte1A(uint8_t* self);  // 0x00011f80
uint8_t XB_THISCALL UIObject_GetByte4(uint8_t* self);  // 0x00011fb0

// Unreached UI slots (host_unreached 2026-11-12)
void XB_THISCALL UIElement_SetFlagBit1(uint8_t* self, uint32_t on);  // 0x000112c0
void XB_THISCALL UIElement_ClearFlagBit0(uint8_t* self);  // 0x000112e0
uint32_t XB_THISCALL UIElement_HasFlagBit4Or5(uint8_t* self);  // 0x00011310
uint8_t* XB_THISCALL UIElement_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x00011360
uint8_t XB_THISCALL UITextBase_ReturnFalse(uint8_t* self);  // 0x000113b0
void XB_THISCALL UITextBase_SetFont(uint8_t* self, uint32_t value);  // 0x000113c0
void XB_THISCALL UITextBase_SetFlag5CBit2(uint8_t* self, uint32_t on);  // 0x000113d0
void XB_THISCALL UITextBase_SetOffset44(uint8_t* self, uint32_t x, uint32_t y);  // 0x00011470
void XB_THISCALL UITextBase_SetFloat30(uint8_t* self, uint32_t bits);  // 0x00011590
float XB_THISCALL UITextBase_GetFloat3C(uint8_t* self);  // 0x00011620
uint32_t XB_THISCALL UITextBase_GetFlag5CBit2(uint8_t* self);  // 0x00011630
int32_t XB_THISCALL UITextBase_GetFlags5C(uint8_t* self);  // 0x00011660
uint32_t XB_THISCALL UITextBase_GetHorzAlign(uint8_t* self);  // 0x00011670
uint32_t XB_THISCALL UITextBase_GetVertAlign(uint8_t* self);  // 0x00011680
float* XB_THISCALL UITextBase_GetZeroVector(uint8_t* self, float* out);  // 0x000116e0
uint32_t XB_THISCALL UIText_GetField78(uint8_t* self);  // 0x00011760
uint32_t XB_THISCALL UIText_GetField7C(uint8_t* self);  // 0x00011770
uint32_t XB_THISCALL UIText_GetField74(uint8_t* self);  // 0x00011780
void XB_THISCALL UIText_SetDefaultLineSpacing(uint8_t* self);  // 0x00011790
void XB_THISCALL UIText_SetField60(uint8_t* self, uint32_t value);  // 0x000118d0
void XB_THISCALL UIText_SetField74(uint8_t* self, uint32_t value);  // 0x000118e0
void XB_THISCALL UIText_GetFields2C30(uint8_t* self, uint32_t* a, uint32_t* b);  // 0x000118f0
void XB_THISCALL UIText_SetByte96(uint8_t* self, uint32_t value);  // 0x00011910
void XB_THISCALL UIPicture_ScaleAboutUniform(uint8_t* self, uint32_t cx, uint32_t cy, uint32_t scale,
                                             uint32_t relative);  // 0x00011980
void XB_THISCALL UIPicture_SetQuadUVs(uint8_t* self, uint32_t index, const float* us, const float* vs);  // 0x000119b0
void XB_THISCALL UIElement_GetFields14_18(uint8_t* self, uint32_t* a, uint32_t* b);  // 0x00011a00
void XB_THISCALL UIWorldPicture_CopyFrom(uint8_t* self, uint8_t* source);  // 0x00011af0
void XB_THISCALL UIWorldPicture_SetWorldPos(uint8_t* self, uint32_t x, uint32_t y, uint32_t z);  // 0x00011b20
uint32_t* XB_THISCALL UIWorldPicture_GetWorldPos(uint8_t* self, uint32_t* out);  // 0x00011b40
void XB_THISCALL UIMenuItem_InnerSlot56Zero(uint8_t* self);  // 0x00011c20
void XB_THISCALL UIMenuItem_SetText(uint8_t* self, uint32_t string);  // 0x00011c80
void XB_THISCALL UIMenuItem_InnerSlot59(uint8_t* self, uint32_t a, uint32_t b, uint32_t c);  // 0x00011cd0
void XB_THISCALL UIMenuItem_SetColorScheme(uint8_t* self, uint32_t value);  // 0x00011d60
uint32_t* XB_THISCALL UIMenuItem_GetText(uint8_t* self, uint32_t* out);  // 0x00011da0
uint8_t XB_THISCALL UIMenuItem_GetColorScheme(uint8_t* self);  // 0x00011e20
extern "C" void UIMenuItem_InnerSlot13(void);  // 0x00011c30
extern "C" void UIMenuItem_InnerSlot31(void);  // 0x00011c60
extern "C" void UIMenuItem_InnerSlot19(void);  // 0x00011d00
extern "C" void UIMenuItem_InnerSlot20(void);  // 0x00011d10
extern "C" void UIMenuItem_InnerSlot57(void);  // 0x00011d20
extern "C" void UIMenuItem_InnerSlot21(void);  // 0x00011d30
extern "C" void UIMenuItem_InnerSlot60(void);  // 0x00011d40
extern "C" void UIMenuItem_InnerSlot61(void);  // 0x00011d50
extern "C" void UIMenuItem_InnerSlot7(void);  // 0x00011d90
extern "C" void UIMenuItem_InnerSlot52(void);  // 0x00011dc0
extern "C" void UIMenuItem_InnerSlot51(void);  // 0x00011de0
extern "C" void UIMenuItem_InnerSlot49(void);  // 0x00011df0
extern "C" void UIMenuItem_InnerSlot42(void);  // 0x00011e10
extern "C" void UIMenuItem_InnerSlot10(void);  // 0x00011e50
extern "C" void UIMenuItem_InnerSlot70(void);  // 0x00011e60
void XB_THISCALL UIMenu_SetPlaysMoveSound(uint8_t* self, uint32_t value);  // 0x00011eb0
void XB_THISCALL UIMenu_SetLocked(uint8_t* self, uint32_t value);  // 0x00011ec0
void XB_THISCALL UIMenu_Slot9Zero(uint8_t* self);  // 0x00011ed0
void XB_THISCALL UIMenu_SetResult(uint8_t* self, uint32_t value);  // 0x00011f10
int32_t XB_THISCALL UIMenu_GetFlags(uint8_t* self);  // 0x00011f40
void XB_THISCALL UIMenu_SetRowSpacing(uint8_t* self, uint32_t value);  // 0x000241c0
void XB_THISCALL UIScreenMgr_SetByte1A(uint8_t* self, uint32_t value);  // 0x00011f70
// Script strings and script helpers
ScriptString* XB_THISCALL ScriptString_InitEmpty(ScriptString* self);  // 0x00011270
ScriptString* XB_THISCALL ScriptString_DefaultCtor(ScriptString* self);  // 0x0001dc00
void XB_THISCALL ScriptStringAt4_Release(uint8_t* self);  // 0x0001ec00
int32_t XB_CDECL Scr_AtolThunk(const char* text);  // 0x0011da60
void XB_THISCALL WaitTilOutput_Dtor(uint8_t* self);  // 0x00013de0
uint8_t* XB_THISCALL Object_SetSecondaryVtable8bade0(uint8_t* self);  // 0x0007b5f0
void XB_CDECL BroScr_EmptyFunc_80();  // 0x0011ff70
void XB_CDECL BroScr_EmptyFunc_81();  // 0x00121790
void XB_CDECL BroScr_EmptyFunc_82();  // 0x00123660
void XB_CDECL G_LevelScriptEmptyCallback();  // 0x002f5130
void XB_CDECL BroScr_EmptyFunc_77_Thunk();  // 0x00124aa0

// Shared virtual stubs
uint8_t* XB_THISCALL Object_ReturnSelf(uint8_t* self);  // 0x0001b920
uint32_t XB_CDECL VFunc_Return8();  // 0x0001bee0
uint32_t XB_CDECL VFunc_Return9();  // 0x0001c0f0
uint32_t XB_CDECL VFunc_ReturnGlobal933e3c();  // 0x0001db40
uint32_t XB_CDECL EmptyVFunc_ReturnZero();  // 0x0001db50
uint8_t XB_STDCALL EmptyVFunc_ReturnTrue4(uint32_t);  // 0x000253a0
void XB_STDCALL EmptyVFunc_Ret20(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);  // 0x0001db60
void XB_STDCALL EmptyVFunc_Ret12(uint32_t, uint32_t, uint32_t);  // 0x000229f0
void XB_STDCALL EmptyVFunc_Ret8(uint32_t, uint32_t);  // 0x00022bb0
void XB_STDCALL nglPolyVertexWriter_Nop4(uint32_t);  // 0x00236550
void XB_STDCALL FxParticleComponent_Nop4(uint32_t);  // 0x002930b0
void XB_STDCALL EmptyVFunc_Ret4(uint32_t);  // 0x002a8190
void XB_CDECL R_RenderView_EmptyCallback();  // 0x00045690
void XB_CDECL NAL_EmptyVFunc();  // 0x00086ce0
void XB_CDECL Phys_EmptyCallback();  // 0x00225370
void XB_THISCALL ResourceManager_ClearSlot(uint8_t* self, uint32_t index);  // 0x002368b0
void XB_THISCALL FxComponentB_ClearSlot(uint8_t* self, uint32_t index);  // 0x00293530
void XB_THISCALL nglVertexWriter_ClearSlot(uint8_t* self, uint32_t index);  // 0x002a8cf0
uint32_t XB_THISCALL Functor_Invoke(uint8_t* self);  // 0x00063080
extern "C" void nglMaterial_CallField8(void);  // 0x00047db0
int32_t XB_CDECL Actor_CompareSightEntries(const uint8_t* a, const uint8_t* b);  // 0x001fa990

// Thunks and callbacks into other C
void XB_CDECL CM_DrawWorldTreesDebugCallback();  // 0x0001d0d0
uint8_t* XB_CDECL Z_MallocThunk(uint32_t size);  // 0x00042c10
uint8_t* XB_CDECL Heap_AllocAlignedThunk(uint32_t align, uint32_t size);  // 0x00042c50
uint32_t XB_THISCALL nglRenderTextNode_Thunk65970(uint8_t* self, uint32_t a, uint32_t b, uint32_t c);  // 0x000666c0
uint32_t XB_THISCALL nglResourceManager_Slot12At4(uint8_t* self, uint32_t arg);  // 0x00063da0
uint32_t XB_THISCALL nglMeshFileManager_Slot21At4(uint8_t* self, uint32_t arg);  // 0x00069f20
uint32_t XB_THISCALL nglResourceManager_Slot11At4(uint8_t* self, uint32_t arg);  // 0x0007aea0
uint32_t XB_THISCALL NAL_Slot46At4(uint8_t* self, uint32_t arg);  // 0x00083ee0
const uint32_t* XB_THISCALL nglResourceManager_InsertTextureAt4(uint8_t* self, const uint32_t* texture);  // 0x00063f00
bool XB_THISCALL nglResourceManager_RemoveAt4(uint8_t* self, const uint32_t* object);  // 0x0007aeb0
const uint32_t* XB_THISCALL NAL_SkipListFindAt4(uint8_t* self, const uint32_t* key);  // 0x00083d10
bool XB_THISCALL NAL_RemoveKey8At4(uint8_t* self, const uint32_t* object);  // 0x00083ef0
void XB_THISCALL nglShader_DrawQuadAtC(uint8_t* self);  // 0x00079010
uint32_t XB_STDCALL cdOcean_LoadTextures(uint8_t* material);  // 0x0004ea90
void XB_STDCALL cdOcean_ReleaseTextures(uint8_t* material);  // 0x0004eb00
void XB_CDECL SCR_UpdateLoadingIconForced();  // 0x00299780
void XB_CDECL IN_MeleeUp();  // 0x002b4710
void XB_CDECL IN_MeleeDown();  // 0x002b4720
extern "C" void Phys_RagdollStepCallback(void);  // 0x00233c30

// Animation and renderer state tests
uint32_t XB_THISCALL NAL_GenericAnim_IsFormat10302(uint8_t* self);  // 0x0007bc30
uint32_t XB_THISCALL NAL_GenericAnim_IsFormat10301(uint8_t* self);  // 0x0007bd70
uint32_t XB_THISCALL NAL_StreamedAnim_IsState5(uint8_t* self);  // 0x0008e470

// Materials (cd*) and effect type tags
uint32_t* XB_CDECL cdGunSightVertexIter_Ctor(uint32_t* iter);  // 0x00047d70
uint32_t* XB_CDECL cdGunVertexIter_Ctor(uint32_t* iter);  // 0x00048580
uint32_t* XB_CDECL cdGlassVertexIter_Ctor(uint32_t* iter);  // 0x000489d0
uint32_t* XB_CDECL cdSimpleAlphaVertexIter_Ctor(uint32_t* iter);  // 0x0004a860
uint32_t* XB_CDECL cdPropellerVertexIter_Ctor(uint32_t* iter);  // 0x0004b080
uint32_t* XB_CDECL cdDecalVertexIter_Ctor(uint32_t* iter);  // 0x0004b840
uint32_t* XB_CDECL cdCharVertexIter_Ctor(uint32_t* iter);  // 0x0004bf60
uint32_t* XB_CDECL cdPrelitVertexIter_Ctor(uint32_t* iter);  // 0x0004c680
uint32_t* XB_CDECL cdAirplaneMetalVertexIter_Ctor(uint32_t* iter);  // 0x0004cbb0
uint32_t* XB_CDECL cdSimpleUVAnimVertexIter_Ctor(uint32_t* iter);  // 0x0004d880
uint32_t* XB_CDECL cdSimplePrelitVertexIter_Ctor(uint32_t* iter);  // 0x0004df50
uint32_t* XB_CDECL cdSimpleVertexIter_Ctor(uint32_t* iter);  // 0x0004e6b0
uint32_t* XB_CDECL cdRiverVertexIter_Ctor(uint32_t* iter);  // 0x0004e9d0
uint32_t* XB_CDECL cdWorldBlendVertexIter_Ctor(uint32_t* iter);  // 0x00051ad0
uint32_t* XB_CDECL cdWorldVertexIter_Ctor(uint32_t* iter);  // 0x00053400
uint32_t* XB_CDECL cdBackgroundVertexIter_Ctor(uint32_t* iter);  // 0x000546a0
uint32_t* XB_CDECL cdSkyVertexIter_Ctor(uint32_t* iter);  // 0x000552d0
uint32_t XB_CDECL cdOcean_GetVertexShader();  // 0x0004e920
uint32_t XB_CDECL cdOcean_ConstSlot13();  // 0x0004e960
uint32_t XB_CDECL cdOcean_ConstSlot12();  // 0x0004e970
uint32_t XB_CDECL apsTypeTag_SrcA();  // 0x000a4880
uint32_t XB_CDECL apsTypeTag_Brst();  // 0x000a4890
uint32_t XB_CDECL apsTypeTag_RSpn();  // 0x000a48a0
uint32_t XB_CDECL apsTypeTag_Life();  // 0x000a48b0
uint32_t XB_CDECL apsTypeTag_AlFd();  // 0x000a48c0
uint32_t XB_CDECL apsTypeTag_AFIO();  // 0x000a48d0
uint32_t XB_CDECL apsTypeTag_RFIO();  // 0x000a48e0
uint32_t XB_CDECL apsTypeTag_LnSc();  // 0x000a48f0
uint32_t XB_CDECL apsTypeTag_MFxV();  // 0x000a4990
uint32_t XB_CDECL apsTypeTag_ExSc();  // 0x000a4900
uint32_t XB_CDECL apsTypeTag_LScW();  // 0x000a4910
uint32_t XB_CDECL apsTypeTag_LScH();  // 0x000a4930
uint32_t XB_CDECL apsTypeTag_Move();  // 0x000a4950
uint32_t XB_CDECL apsTypeTag_OMov();  // 0x000a4960
uint32_t XB_CDECL apsTypeTag_PsMv();  // 0x000a4970
uint32_t XB_CDECL apsTypeTag_Forc();  // 0x000a49a0
uint32_t XB_CDECL apsTypeTag_ClSh();  // 0x000a49b0
uint32_t XB_CDECL apsTypeTag_VlDr();  // 0x000a49c0
uint32_t XB_CDECL apsTypeTag_AVDr();  // 0x000a49d0
uint32_t XB_CDECL apsTypeTag_WPRf();  // 0x000a49f0
uint32_t XB_CDECL apsTypeTag_UVFr();  // 0x000a4a00
uint32_t XB_CDECL apsTypeTag_AngT();  // 0x000a4a10
uint32_t XB_CDECL apsTypeTag_SpAc();  // 0x000a4a90
uint32_t XB_CDECL apsTypeTag_Traj();  // 0x000a4ab0

// Start-up: static initializers and constructors they call
uint8_t* XB_THISCALL UIX_CAlertFeature_Ctor(uint8_t* self);  // 0x000d440b
uint8_t* XB_THISCALL UIX_CFriendsFeature_Ctor(uint8_t* self);  // 0x000d5e3c
uint8_t* XB_THISCALL UIX_CNotificationTask_Ctor(uint8_t* self);  // 0x000d7907
uint8_t* XB_THISCALL UIX_CLogonFeature_Ctor(uint8_t* self);  // 0x000d7eb7
uint8_t* XB_THISCALL UIX_CPlayersFeature_Ctor(uint8_t* self);  // 0x000da10c
uint8_t* XB_THISCALL UIX_CVoiceMailPseudoFeature_Ctor(uint8_t* self);  // 0x000dc527
void XB_THISCALL StdString_Tidy(uint8_t* self);  // 0x00014750
void XB_CDECL XPP_StaticInit_921770();  // 0x007abead
void XB_CDECL XPP_StaticInit_9217e0();  // 0x007abedd

void XB_THISCALL UIMenu_ForwardSelectPrev(uint8_t* self, uint32_t);  // 0x00011ef0
void XB_THISCALL UIMenu_ForwardSelectNext(uint8_t* self, uint32_t);  // 0x00011f00
void XB_THISCALL UIMenu_ForwardSlot46(uint8_t* self, uint32_t);  // 0x00011f50
void XB_THISCALL UIMenu_ForwardSlot47(uint8_t* self, uint32_t);  // 0x00011f60
void XB_THISCALL UIMenu_Slot47Nop(uint8_t* self);  // 0x0030f000
void XB_THISCALL UIMenu_Slot46Nop(uint8_t* self);  // 0x0030f010
void XB_THISCALL UIMenu_ClearHeldDirection(uint8_t* self, uint32_t, uint32_t direction);  // 0x00314aa0
void XB_THISCALL UIScreenMgr_ActiveScreenSlot33(uint8_t* self, uint32_t first, uint32_t second);  // 0x0030ec40
void XB_THISCALL ZeroWord_Ctor(uint32_t* self);  // 0x0001d0e0
uint32_t XB_CDECL cdOcean_StateSlot2();  // 0x0004f240
uint32_t XB_CDECL cdOcean_StateSlot4();  // 0x0004f250
uint32_t XB_STDCALL cdOcean_StateEntry(uint32_t a, uint32_t b, uint32_t c);  // 0x0004f260
uint32_t XB_CDECL cdOcean_ConstSlot5();  // 0x0004f280
uint32_t XB_CDECL cdOcean_ConstSlot6();  // 0x0004f290
uint32_t* XB_CDECL cdOceanVertexIter_Ctor(uint32_t* iter);  // 0x000508a0

}  // namespace game
