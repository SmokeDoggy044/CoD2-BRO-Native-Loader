#include "gap_methods.h"

#include <cstring>
#include <initializer_list>

#include "cl.h"
#include "cm.h"
#include "nal_anim.h"
#include "ngl.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "ragdoll.h"
#include "bro_scr_cover_turret.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T, typename P>
T& word(P* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

template <typename T, typename P>
const T& word(const P* base, uint32_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

// The function in slot `offset` of self's vtable.
template <typename F>
F* vcall(uint8_t* self, uint32_t offset) {
    return title_function<F>(title_global<uint32_t>(word<uint32_t>(self, 0) + offset));
}

// cd* vertex iterators: placement constructors that only set the vtable.
uint32_t* vertex_iter_ctor(uint32_t* iter, uint32_t vtable) {
    if (iter) *iter = vtable;
    return iter;
}

}  // namespace

// --- UI elements, texts and menus ---

uint32_t XB_THISCALL UIElement_GetFlagBit2(uint8_t* self) {
    return (self[0x10] >> 2) & 1;
}

uint8_t XB_THISCALL UITextBase_HasFlag2(uint8_t* self) {
    return vcall<uint8_t XB_THISCALL(uint8_t*, uint32_t)>(self, 0xd8)(self, 2);  // slot 54: UITextBase_TestFlags5C
}

ScriptString* XB_THISCALL UITextBase_SetString54(uint8_t* self, const char* text) {
    return ScriptString_AssignCString(reinterpret_cast<ScriptString*>(self + 0x54), text);
}

ScriptString* XB_THISCALL UITextBase_SetString1C(uint8_t* self, const char* text) {
    return ScriptString_AssignCString(reinterpret_cast<ScriptString*>(self + 0x1c), text);
}

void XB_THISCALL UITextBase_SetFloat2C(uint8_t* self, uint32_t bits) {
    word<uint32_t>(self, 0x2c) = bits;
}

void XB_THISCALL UITextBase_SetField58(uint8_t* self, uint32_t value) {
    word<uint32_t>(self, 0x58) = value;
}

float XB_THISCALL UITextBase_GetFloat38(uint8_t* self) {
    return word<float>(self, 0x38);
}

uint32_t XB_THISCALL UITextBase_GetField18(uint8_t* self) {
    return word<uint32_t>(self, 0x18);
}

uint32_t* XB_THISCALL UITextBase_GetField50(uint8_t* self, uint32_t* out) {
    *out = word<uint32_t>(self, 0x50);
    return out;
}

// 0x003159a0 (vtable slot 36): the +0x50 word, then flag bit 3 cleared and bit 0 (changed) set.
void XB_THISCALL UITextBase_SetField50(uint8_t* self, uint32_t value) {
    word<uint32_t>(self, 0x50) = value;
    self[0x5c] = static_cast<uint8_t>((self[0x5c] & 0xf7) | 1);
}

float XB_THISCALL UITextBase_GetFloat2C(uint8_t* self) {
    return word<float>(self, 0x2c);
}

uint8_t XB_THISCALL UITextBase_TestFlags5C(uint8_t* self, uint32_t mask) {
    return (mask & static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(self[0x5c])))) != 0;
}

// 0x00011750: UITextLine_Init passes it to the array constructor
uint8_t* XB_THISCALL UITextLine_ElementCtor(uint8_t* self) {
    word<uint16_t>(self, 0) = 0;
    word<uint16_t>(self, 2) = 0;
    return self;
}

void XB_THISCALL UIText_SetFloat64(uint8_t* self, uint32_t bits) {
    word<uint32_t>(self, 0x64) = bits;
}

float XB_THISCALL UIElement_GetFloat14(uint8_t* self) {
    return word<float>(self, 0x14);
}

float XB_THISCALL UIElement_GetFloat18(uint8_t* self) {
    return word<float>(self, 0x18);
}

float XB_THISCALL UIElement_GetFloat30(uint8_t* self) {
    return word<float>(self, 0x30);
}

// 0x00011c00: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot15,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x3c(%eax)")

// 0x00011c10: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot1,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x4(%eax)")

// 0x00011c70: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot27,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x6c(%eax)")

// 0x00011cc0: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot30,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x78(%eax)")

// 0x00011d80: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot24,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x60(%eax)")

// 0x00011e00: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot41,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0xa4(%eax)")

uint8_t XB_THISCALL UIMenuItem_GetByte16(uint8_t* self) {
    return self[0x16];
}

void XB_THISCALL UIMenu_RestoreSelectionVirtual(uint8_t* self, uint32_t) {
    vcall<void XB_THISCALL(uint8_t*)>(self, 0x40)(self);  // slot 16: *Menu_RestoreSelection
}

uint8_t XB_THISCALL UIMenu_GetByte33(uint8_t* self) {
    return self[0x33];
}

uint32_t XB_THISCALL UIMenu_GetField8(uint8_t* self) {
    return word<uint32_t>(self, 0x8);
}

uint8_t XB_THISCALL UIElement_GetByte1A(uint8_t* self) {
    return self[0x1a];
}

uint8_t XB_THISCALL UIObject_GetByte4(uint8_t* self) {
    return self[0x4];
}

// --- Script strings and script helpers ---

// 0x00011270: static initializers pass it to the array constructor
ScriptString* XB_THISCALL ScriptString_InitEmpty(ScriptString* self) {
    return ScriptString_FromRep(self, nullptr);
}

ScriptString* XB_THISCALL ScriptString_DefaultCtor(ScriptString* self) {
    ScriptString_FromRep(self, nullptr);
    return self;
}

// 0x0001ec00: element destructor of CG_CreateViewWeaponDObj's array
void XB_THISCALL ScriptStringAt4_Release(uint8_t* self) {
    ScriptString_Release(reinterpret_cast<ScriptString*>(self + 4));
}

int32_t XB_CDECL Scr_AtolThunk(const char* text) {
    return title_global<int32_t XB_CDECL (*)(const char*)>(0x009229e0)(text);  // the builtin table's atol
}

// 0x00013de0: EH unwind funclets jump to it
void XB_THISCALL WaitTilOutput_Dtor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x007f48c4;  // WaitTilOutput vtable
}

// 0x0007b5f0: level code jumps to it
uint8_t* XB_THISCALL Object_SetSecondaryVtable8bade0(uint8_t* self) {
    word<uint32_t>(self, 4) = 0x008bade0;
    return self;
}

void XB_CDECL BroScr_EmptyFunc_80() {}

void XB_CDECL BroScr_EmptyFunc_81() {}

void XB_CDECL BroScr_EmptyFunc_82() {}

void XB_CDECL G_LevelScriptEmptyCallback() {}

void XB_CDECL BroScr_EmptyFunc_77_Thunk() {
    BroScr_EmptyFunc_77();
}

// --- Shared virtual stubs ---

uint8_t* XB_THISCALL Object_ReturnSelf(uint8_t* self) {
    return self;
}

uint32_t XB_CDECL VFunc_Return8() {
    return 8;
}

uint32_t XB_CDECL VFunc_Return9() {
    return 9;
}

uint32_t XB_CDECL VFunc_ReturnGlobal933e3c() {
    return 0x00933e3c;
}

uint32_t XB_CDECL EmptyVFunc_ReturnZero() {
    return 0;
}

uint8_t XB_STDCALL EmptyVFunc_ReturnTrue4(uint32_t) {
    return 1;
}

void XB_STDCALL EmptyVFunc_Ret20(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}

void XB_STDCALL EmptyVFunc_Ret12(uint32_t, uint32_t, uint32_t) {}

void XB_STDCALL EmptyVFunc_Ret8(uint32_t, uint32_t) {}

void XB_STDCALL nglPolyVertexWriter_Nop4(uint32_t) {}

void XB_STDCALL FxParticleComponent_Nop4(uint32_t) {}

void XB_STDCALL EmptyVFunc_Ret4(uint32_t) {}

void XB_CDECL R_RenderView_EmptyCallback() {}

void XB_CDECL NAL_EmptyVFunc() {}

void XB_CDECL Phys_EmptyCallback() {}

void XB_THISCALL ResourceManager_ClearSlot(uint8_t* self, uint32_t index) {
    word<uint32_t>(self, 4 + index * 4) = 0;
}

void XB_THISCALL FxComponentB_ClearSlot(uint8_t* self, uint32_t index) {
    word<uint32_t>(self, 4 + index * 4) = 0;
}

void XB_THISCALL nglVertexWriter_ClearSlot(uint8_t* self, uint32_t index) {
    word<uint32_t>(self, 4 + index * 4) = 0;
}

uint32_t XB_THISCALL Functor_Invoke(uint8_t* self) {
    return reinterpret_cast<uint32_t XB_CDECL (*)(uint32_t)>(uintptr_t{word<uint32_t>(self, 0x10)})(word<uint32_t>(self, 0x14));
}

// 0x00047db0: tail jump through a member: not compared
PLATFORM_ASM_ENTRY(nglMaterial_CallField8,
                   "jmp *8(%ecx)")

// 0x001fa990: Actor_UpdateSight's sort
int32_t XB_CDECL Actor_CompareSightEntries(const uint8_t* a, const uint8_t* b) {
    return static_cast<int32_t>(word<uint32_t>(b, 4) - word<uint32_t>(a, 4));
}

// --- Thunks and callbacks into other C ---

void XB_CDECL CM_DrawWorldTreesDebugCallback() {
    CM_DrawWorldTreeDebug(title_global<uint8_t*>(0x0095f4fc));
}

uint8_t* XB_CDECL Z_MallocThunk(uint32_t size) {
    return Z_Malloc(size);
}

uint8_t* XB_CDECL Heap_AllocAlignedThunk(uint32_t align, uint32_t size) {
    return Heap_AllocAligned(align, size);
}

uint32_t XB_THISCALL nglRenderTextNode_Thunk65970(uint8_t* self, uint32_t a, uint32_t b, uint32_t c) {
    return own_function<uint32_t XB_THISCALL(uint8_t*, uint32_t, uint32_t, uint32_t)>(&nglFontManager_Release)(self, a, b, c);
}

uint32_t XB_THISCALL nglResourceManager_Slot12At4(uint8_t* self, uint32_t arg) {
    return own_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(&nglSkipList_FindTexture)(self + 4, arg);
}

uint32_t XB_THISCALL nglMeshFileManager_Slot21At4(uint8_t* self, uint32_t arg) {
    return own_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(&nglSkipList_FindIndirect)(self + 4, arg);
}

uint32_t XB_THISCALL nglResourceManager_Slot11At4(uint8_t* self, uint32_t arg) {
    return own_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(&nglSkipList_FindNamed)(self + 4, arg);
}

uint32_t XB_THISCALL NAL_Slot46At4(uint8_t* self, uint32_t arg) {
    return own_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(&nglSkipList_FindName8)(self + 4, arg);
}

const uint32_t* XB_THISCALL nglResourceManager_InsertTextureAt4(uint8_t* self, const uint32_t* texture) {
    return nglSkipList_InsertTexture(reinterpret_cast<NglSkipList*>(self + 4), texture);
}

bool XB_THISCALL nglResourceManager_RemoveAt4(uint8_t* self, const uint32_t* object) {
    return nglSkipList_Remove(reinterpret_cast<NglSkipList*>(self + 4), object);
}

const uint32_t* XB_THISCALL NAL_SkipListFindAt4(uint8_t* self, const uint32_t* key) {
    return AnimManager_SkipListFind(reinterpret_cast<const NglSkipList*>(self + 4), key);
}

bool XB_THISCALL NAL_RemoveKey8At4(uint8_t* self, const uint32_t* object) {
    return NalManager_SkipListRemoveKey8(reinterpret_cast<NglSkipList*>(self + 4), object);
}

void XB_THISCALL nglShader_DrawQuadAtC(uint8_t* self) {
    nglDrawQuad(reinterpret_cast<NglQuad*>(self + 0xc));
}

uint32_t XB_STDCALL cdOcean_LoadTextures(uint8_t* material) {
    return nglMaterial_LoadThreeTexturesAndOptional(material);
}

void XB_STDCALL cdOcean_ReleaseTextures(uint8_t* material) {
    nglMaterial_ReleaseThreeTexturesAndOptional(material);
}

void XB_CDECL SCR_UpdateLoadingIconForced() {
    SCR_UpdateLoadingIcon_2(1);
}

void XB_CDECL IN_MeleeUp() {
    IN_KeyUp(&title_global<uint8_t>(0x00934098));  // -melee
}

void XB_CDECL IN_MeleeDown() {
    IN_KeyDown(&title_global<uint8_t>(0x00934098));  // +melee
}

// 0x00233c30: call in the first 5 bytes, then a tail jump: not compared
PLATFORM_ASM_ENTRY(Phys_RagdollStepCallback,
                   PLATFORM_ASM_CALL(Ragdoll_CollideAll_entry)
                   "movl $0x0022a0b0, %eax\n"
                   "jmp *%eax")

// --- Animation and renderer state tests ---

uint32_t XB_THISCALL NAL_GenericAnim_IsFormat10302(uint8_t* self) {
    return word<uint32_t>(self, 0x4) == 0x10302;
}

uint32_t XB_THISCALL NAL_GenericAnim_IsFormat10301(uint8_t* self) {
    return word<uint32_t>(self, 0x2c) == 0x10301;
}

uint32_t XB_THISCALL NAL_StreamedAnim_IsState5(uint8_t* self) {
    return word<uint32_t>(self, 0x1c) == 5;
}

// --- Materials (cd*) and effect type tags ---

uint32_t* XB_CDECL cdGunSightVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b441c);
}

uint32_t* XB_CDECL cdGunVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b46f4);
}

uint32_t* XB_CDECL cdGlassVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b49c4);
}

uint32_t* XB_CDECL cdSimpleAlphaVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b4cac);
}

uint32_t* XB_CDECL cdPropellerVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b4f88);
}

uint32_t* XB_CDECL cdDecalVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b5254);
}

uint32_t* XB_CDECL cdCharVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b551c);
}

uint32_t* XB_CDECL cdPrelitVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b57f8);
}

uint32_t* XB_CDECL cdAirplaneMetalVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b5acc);
}

uint32_t* XB_CDECL cdSimpleUVAnimVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b5dac);
}

uint32_t* XB_CDECL cdSimplePrelitVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b608c);
}

uint32_t* XB_CDECL cdSimpleVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b6368);
}

uint32_t* XB_CDECL cdRiverVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b6690);
}

uint32_t* XB_CDECL cdWorldBlendVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b6edc);
}

uint32_t* XB_CDECL cdWorldVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b74a4);
}

uint32_t* XB_CDECL cdBackgroundVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b777c);
}

uint32_t* XB_CDECL cdSkyVertexIter_Ctor(uint32_t* iter) {
    return vertex_iter_ctor(iter, 0x007b7a58);
}

uint32_t XB_CDECL cdOcean_GetVertexShader() {
    return 0x009020e8;
}

uint32_t XB_CDECL cdOcean_ConstSlot13() {
    return 0xffffffa8u;
}

uint32_t XB_CDECL cdOcean_ConstSlot12() {
    return 0xffffffa6u;
}

uint32_t XB_CDECL apsTypeTag_SrcA() {
    return 0x53726341;  // 'SrcA'
}

uint32_t XB_CDECL apsTypeTag_Brst() {
    return 0x42727374;  // 'Brst'
}

uint32_t XB_CDECL apsTypeTag_RSpn() {
    return 0x5253706e;  // 'RSpn'
}

uint32_t XB_CDECL apsTypeTag_Life() {
    return 0x4c696665;  // 'Life'
}

uint32_t XB_CDECL apsTypeTag_AlFd() {
    return 0x416c4664;  // 'AlFd'
}

uint32_t XB_CDECL apsTypeTag_AFIO() {
    return 0x4146494f;  // 'AFIO'
}

uint32_t XB_CDECL apsTypeTag_RFIO() {
    return 0x5246494f;  // 'RFIO'
}

uint32_t XB_CDECL apsTypeTag_MFxV() {
    return 0x4d467856;  // 'MFxV'
}

uint32_t XB_CDECL apsTypeTag_LnSc() {
    return 0x4c6e5363;  // 'LnSc'
}

uint32_t XB_CDECL apsTypeTag_ExSc() {
    return 0x45785363;  // 'ExSc'
}

uint32_t XB_CDECL apsTypeTag_LScW() {
    return 0x4c536357;  // 'LScW'
}

uint32_t XB_CDECL apsTypeTag_LScH() {
    return 0x4c536348;  // 'LScH'
}

uint32_t XB_CDECL apsTypeTag_Move() {
    return 0x4d6f7665;  // 'Move'
}

uint32_t XB_CDECL apsTypeTag_OMov() {
    return 0x4f4d6f76;  // 'OMov'
}

uint32_t XB_CDECL apsTypeTag_PsMv() {
    return 0x50734d76;  // 'PsMv'
}

uint32_t XB_CDECL apsTypeTag_Forc() {
    return 0x466f7263;  // 'Forc'
}

uint32_t XB_CDECL apsTypeTag_ClSh() {
    return 0x436c5368;  // 'ClSh'
}

uint32_t XB_CDECL apsTypeTag_VlDr() {
    return 0x566c4472;  // 'VlDr'
}

uint32_t XB_CDECL apsTypeTag_AVDr() {
    return 0x41564472;  // 'AVDr'
}

uint32_t XB_CDECL apsTypeTag_WPRf() {
    return 0x57505266;  // 'WPRf'
}

uint32_t XB_CDECL apsTypeTag_UVFr() {
    return 0x55564672;  // 'UVFr'
}

uint32_t XB_CDECL apsTypeTag_AngT() {
    return 0x416e6754;  // 'AngT'
}

uint32_t XB_CDECL apsTypeTag_SpAc() {
    return 0x53704163;  // 'SpAc'
}

uint32_t XB_CDECL apsTypeTag_Traj() {
    return 0x5472616a;  // 'Traj'
}

// --- Start-up: static initializers and constructors they call ---

// 0x000d440b: the UIX alert feature object the static initializers build at 0x00920ec0.
uint8_t* XB_THISCALL UIX_CAlertFeature_Ctor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x007cfed8;  // vtable
    word<uint32_t>(self, 0x8) = 0;
    word<uint32_t>(self, 0xc) = 0;
    word<uint32_t>(self, 0x2f0) = 0;
    word<uint32_t>(self, 0x2f4) = 0;
    word<uint32_t>(self, 0x2f8) = 0;
    for (uint32_t i = 0; i < 4; ++i) word<uint32_t>(self, 0x10 + i * 0xb8) = 0;
    return self;
}

// 0x000d5e3c: the UIX friends feature object the static initializers build at 0x009211c0.
uint8_t* XB_THISCALL UIX_CFriendsFeature_Ctor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x007cffc0;  // vtable
    word<uint32_t>(self, 0x30) = 0xffffffffu;
    for (uint32_t at : {0x24u, 0x28u, 0x2cu, 0x44u, 0xb0u, 0xb8u, 0xbcu, 0xc0u}) word<uint32_t>(self, at) = 0;
    word<uint32_t>(self, 0xb4) = 1;
    for (uint32_t at = 0x34; at < 0x44; at += 4) word<uint32_t>(self, at) = 0;
    word<uint32_t>(self, 0xc) = 0xffffffffu;
    word<uint32_t>(self, 0x8) = 0;
    for (uint32_t at = 0x58; at < 0xac; at += 4) word<uint32_t>(self, at) = 0;
    word<uint16_t>(self, 0xac) = 0;
    return self;
}

// 0x000d7907: a member of the logon feature (+0x234).
uint8_t* XB_THISCALL UIX_CNotificationTask_Ctor(uint8_t* self) {
    word<uint32_t>(self, 0x4) = 0;
    word<uint32_t>(self, 0x8) = 0xffffffffu;
    word<uint32_t>(self, 0) = 0x007d0034;  // vtable
    word<uint32_t>(self, 0xc) = 3;
    for (uint32_t at = 0x10; at < 0x30; at += 4) word<uint32_t>(self, at) = 0;
    return self;
}

// 0x000d7eb7: the UIX logon feature object at 0x00921288.
uint8_t* XB_THISCALL UIX_CLogonFeature_Ctor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x007d0038;  // vtable
    word<uint32_t>(self, 0x22c) = 0xffffffffu;
    word<uint32_t>(self, 0x220) = 0;
    word<uint32_t>(self, 0x224) = 0;
    word<uint32_t>(self, 0x228) = 0;
    word<uint32_t>(self, 0x230) = 0;
    UIX_CNotificationTask_Ctor(self + 0x234);
    word<uint32_t>(self, 0x264) = 0;
    return self;
}

// 0x000da10c: the UIX players feature object at 0x009215a8.
uint8_t* XB_THISCALL UIX_CPlayersFeature_Ctor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x007d00c0;  // vtable
    word<uint32_t>(self, 0x8) = 0x007d00a8;  // second vtable
    for (uint32_t at : {0x20u, 0x24u, 0x2cu, 0x30u, 0x28u}) word<uint32_t>(self, at) = 0;
    word<uint32_t>(self, 0x38) = 2;
    for (uint32_t at : {0x40u, 0x44u, 0x3cu}) word<uint32_t>(self, at) = 0;
    word<uint32_t>(self, 0x4c) = 2;
    word<uint32_t>(self, 0x50) = 0;
    word<uint32_t>(self, 0x54) = 0;
    return self;
}

// 0x000dc527: the UIX voice mail object at 0x00921604.
uint8_t* XB_THISCALL UIX_CVoiceMailPseudoFeature_Ctor(uint8_t* self) {
    for (uint32_t at : {0xcu, 0x10u, 0x14u, 0x28u, 0x2cu}) word<uint32_t>(self, at) = 0;
    word<uint32_t>(self, 0) = 0x007d0160;  // vtable
    word<uint32_t>(self, 0x8) = 0x007d0144;  // second vtable
    const uint32_t fill = title_global<uint32_t>(0x007d040c);
    for (uint32_t at = 0x18; at < 0x28; at += 4) word<uint32_t>(self, at) = fill;
    return self;
}

namespace {

// XPP's two static arrays of 4 objects (0x1c bytes, vtable 0x007d047c, the rest zero), entries 8 bytes in.
void xpp_init_array(uint32_t at) {
    for (uint32_t i = 0; i < 4; ++i, at += 0x1c) {
        title_global<uint32_t>(at - 8) = 0x007d047c;
        title_global<uint32_t>(at - 4) = 0;
        title_global<uint16_t>(at) = 0;
        title_global<uint8_t>(at + 2) = 0;
        title_global<uint8_t>(at + 3) = 0;
        for (uint32_t k = 4; k <= 0x10; k += 4) title_global<uint32_t>(at + k) = 0;
    }
}

}  // namespace

// 0x007abead / 0x007abedd: .CRT$XC entries (_cinit).
void XB_CDECL XPP_StaticInit_921770() { xpp_init_array(0x00921770); }
void XB_CDECL XPP_StaticInit_9217e0() { xpp_init_array(0x009217e0); }

}  // namespace game
