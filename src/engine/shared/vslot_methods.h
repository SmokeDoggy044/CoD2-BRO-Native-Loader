#pragma once

// Virtual slots (own engine host 0.3, tools/host_unreached.py class "virtual"): short methods of engine classes
// whose vtables our C installs but which no replay reaches - cd*/ngl material instances and vertex iterators,
// ngl/zone/anim resource managers and their skip-list iterators, Fx domains/ops/components, CM shapes and
// small library classes. Grouped by class; each row in functions.cpp says which address it was.

#include <cstdint>

#include "compiler.h"

namespace game {

// --- ngl/cd* material instances (8 bytes: vtable, section) and their vertex iterators/writers ---
void XB_THISCALL nglMaterial_AddSectionOffset(uint32_t* self, uint32_t delta);  // 0x0001b950
void XB_THISCALL nglMaterial_Free(uint32_t* self);                                // 0x000491e0
uint32_t XB_THISCALL nglVertexIter_IsValid(const uint32_t* self);                 // 0x00029740
uint32_t XB_THISCALL nglVertexWriter_IsValid(const uint32_t* self);               // 0x0001f1d0
uint32_t* XB_THISCALL nglVertexIterBase_DeletingDtor(uint32_t* self, uint32_t flags);       // 0x00052810
uint32_t* XB_THISCALL nglDebugMeshVertexIter_DeletingDtor(uint32_t* self, uint32_t flags);  // 0x0006e900
uint32_t** XB_THISCALL nglPolyMaterial_NewVertexIter(const void* self, uint32_t** out);     // 0x000296b0
uint32_t** XB_THISCALL cdOcean_NewVertexIter(const void* self, uint32_t** out);              // 0x00050d50
uint32_t** XB_THISCALL cdSky_NewVertexIter(const void* self, uint32_t** out);                // 0x000553d0
uint32_t** XB_THISCALL nglDebugMesh_NewVertexIter(const void* self, uint32_t** out);         // 0x0006e950
uint32_t* XB_THISCALL nglPosMaterial_NewInstance(void* self, uint32_t section);              // 0x0001f170
uint32_t* XB_THISCALL nglPolyMaterial_NewInstance(void* self, uint32_t section);             // 0x000296e0
uint32_t* XB_THISCALL cdGunSight_NewInstance(void* self, uint32_t section);                  // 0x00047f00
uint32_t* XB_THISCALL cdGun_NewInstance(void* self, uint32_t section);                       // 0x00048660
uint32_t* XB_THISCALL cdGlass_NewInstance(void* self, uint32_t section);                     // 0x00049250
uint32_t* XB_THISCALL cdSimpleAlpha_NewInstance(void* self, uint32_t section);               // 0x0004a9c0
uint32_t* XB_THISCALL cdPropeller_NewInstance(void* self, uint32_t section);                 // 0x0004b140
uint32_t* XB_THISCALL cdChar_NewInstance(void* self, uint32_t section);                      // 0x0004c040
uint32_t* XB_THISCALL cdPrelit_NewInstance(void* self, uint32_t section);                    // 0x0004c7b0
uint32_t* XB_THISCALL cdAirplaneMetal_NewInstance(void* self, uint32_t section);             // 0x0004d140
uint32_t* XB_THISCALL cdSimpleUVAnim_NewInstance(void* self, uint32_t section);              // 0x0004d940
uint32_t* XB_THISCALL cdSimplePrelit_NewInstance(void* self, uint32_t section);              // 0x0004e030
uint32_t* XB_THISCALL cdSimple_NewInstance(void* self, uint32_t section);                    // 0x0004e790
uint32_t* XB_THISCALL cdRiver_NewInstance(void* self, uint32_t section);                     // 0x0004ebe0
uint32_t* XB_THISCALL cdOcean_NewInstance(void* self, uint32_t section);                     // 0x00050db0
uint32_t* XB_THISCALL cdWater_NewInstance(void* self, uint32_t section);                     // 0x000515d0
uint32_t* XB_THISCALL cdWorldBlend_NewInstance(void* self, uint32_t section);                // 0x00052830
uint32_t* XB_THISCALL cdWorldVertexLit_NewInstance(void* self, uint32_t section);            // 0x00052f00
uint32_t* XB_THISCALL cdWorld_NewInstance(void* self, uint32_t section);                     // 0x00054080
uint32_t* XB_THISCALL cdBackground_NewInstance(void* self, uint32_t section);                // 0x00054760
uint32_t* XB_THISCALL cdSky_NewInstance(void* self, uint32_t section);                       // 0x00055430
uint32_t* XB_THISCALL nglDebugMesh_NewInstance(void* self, uint32_t section);                // 0x0006e980
uint8_t* XB_THISCALL ShaderPCUV_DeletingDtor(uint8_t* self, uint32_t flags);                 // 0x00047690
uint8_t* XB_THISCALL cdSky_DeletingDtor(uint8_t* self, uint32_t flags);                      // 0x000553b0
uint32_t XB_CDECL cdOcean_GetVertexShader2();                                                // 0x0004e930

// --- ngl/anim resource managers (vtable: DeletingDtor, ClassName, Find, Add, Remove, NewIterator, ForEachPass..)
// and their skip-list iterators {vtable, list (manager + 4), node} (DeletingDtor, Reset, IsValid, Next, Current) ---
void XB_THISCALL SkipListIter_Reset(uint32_t* self);                // 0x0006a040
uint32_t XB_THISCALL SkipListIter_IsValid(const uint32_t* self);    // 0x00083d70
void XB_THISCALL SkipListIter_Next(uint32_t* self);                 // 0x00063ec0
uint32_t XB_THISCALL SkipListIter_Current(const uint32_t* self);    // 0x00063ed0
uint32_t* XB_THISCALL nglTextureManager_NewIterator(uint8_t* self);  // 0x00063e80
uint32_t* XB_THISCALL nglPaletteManager_NewIterator(uint8_t* self);  // 0x00064140
uint32_t* XB_THISCALL nglFontManager_NewIterator(uint8_t* self);     // 0x00066700
uint32_t* XB_THISCALL nglMeshFileManager_NewIterator(uint8_t* self);  // 0x00069da0
uint32_t* XB_THISCALL nglMeshManager_NewIterator(uint8_t* self);      // 0x0006a000
uint32_t* XB_THISCALL nglSection3Manager_NewIterator(uint8_t* self);  // 0x0006a090
uint32_t* XB_THISCALL nglMeshFileCache_NewIterator(uint8_t* self);    // 0x0007aec0
uint32_t* XB_THISCALL AnimManager2c9c_NewIterator(uint8_t* self);     // 0x00083d30
uint32_t* XB_THISCALL AnimManager2ce8_NewIterator(uint8_t* self);     // 0x00083f00
uint32_t* XB_THISCALL AnimManager2d34_NewIterator(uint8_t* self);     // 0x00084030
uint32_t* XB_THISCALL AnimManager2d80_NewIterator(uint8_t* self);     // 0x00084150
uint8_t* XB_THISCALL nglTextureIter_DeletingDtor(uint8_t* self, uint32_t flags);         // 0x00063ee0
uint8_t* XB_THISCALL nglPaletteIter_DeletingDtor(uint8_t* self, uint32_t flags);         // 0x00064180
uint8_t* XB_THISCALL nglFontIter_DeletingDtor(uint8_t* self, uint32_t flags);            // 0x00066740
uint8_t* XB_THISCALL nglMeshFileIter_DeletingDtor(uint8_t* self, uint32_t flags);        // 0x00069de0
uint8_t* XB_THISCALL nglSection3Iter_DeletingDtor(uint8_t* self, uint32_t flags);        // 0x0006a0d0
uint8_t* XB_THISCALL nglMeshFileCacheIter_DeletingDtor(uint8_t* self, uint32_t flags);   // 0x0007af00
uint8_t* XB_THISCALL AnimManager2c9cIter_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x00083d80
uint8_t* XB_THISCALL AnimManager2ce8Iter_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x00083f40
uint8_t* XB_THISCALL AnimManager2d34Iter_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x00084070
uint8_t* XB_THISCALL AnimManager2d80Iter_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x00084190
uint8_t* XB_THISCALL nglPaletteManagerBase_DeletingDtor(uint8_t* self, uint32_t flags);   // 0x00063c20
uint8_t* XB_THISCALL nglFontManagerBase_DeletingDtor(uint8_t* self, uint32_t flags);      // 0x000666d0
uint8_t* XB_THISCALL nglSection3ManagerBase_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x00069b20
uint8_t* XB_THISCALL nglMeshFileCacheBase_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x0007ad60
uint8_t XB_THISCALL nglTextureManager_Remove(uint8_t* self, const uint32_t* texture);       // 0x00063e70
uint8_t XB_THISCALL nglMeshManager_Remove(uint8_t* self, const uint32_t* object);           // 0x00069ff0
const uint32_t* XB_THISCALL nglMeshManager_Add(uint8_t* self, const uint32_t* object);      // 0x0006a080
uint8_t XB_THISCALL AnimManager_Remove(uint8_t* self, const uint32_t* object);              // 0x00083d20
const uint32_t* XB_THISCALL AnimManager_Add(uint8_t* self, const uint32_t* object);         // 0x00083da0
const uint32_t* XB_THISCALL NalManagerKey8_Add(uint8_t* self, const uint32_t* object);      // 0x00083f60
uint32_t XB_STDCALL nglMeshManagerBase_FlushNop(uint32_t, uint32_t, uint32_t);             // 0x00069af0

// --- Fx (aps) domains, ops and actions ---
// Type tags (slot "TypeTag": four characters, first in the high byte) and byte sizes (slot "Size", one unused argument).
uint32_t XB_CDECL apsTypeTag_1dBx();  // 0x00099240
uint32_t XB_CDECL apsTypeTag_3dBx();  // 0x00099330
uint32_t XB_CDECL apsTypeTag_1dPt();  // 0x00099540
uint32_t XB_CDECL apsTypeTag_3dPt();  // 0x000995b0
uint32_t XB_CDECL apsTypeTag_1dBu();  // 0x00099690
uint32_t XB_CDECL apsTypeTag_3dBu();  // 0x00099990
uint32_t XB_CDECL apsTypeTag_SMsh();  // 0x0009cef0
uint32_t XB_CDECL apsTypeTag_Rect();  // 0x0009ea40
uint32_t XB_CDECL apsTypeTag_CUVA();  // 0x0009eb80
uint32_t XB_CDECL apsTypeTag_CBlb();  // 0x0009ecc0
uint32_t XB_CDECL apsTypeTag_Blbo();  // 0x0009f1c0
uint32_t XB_CDECL apsTypeTag_EScW();  // 0x000a4920
uint32_t XB_CDECL apsTypeTag_EScH();  // 0x000a4940
uint32_t XB_CDECL apsTypeTag_VAVD();  // 0x000a49e0
uint32_t XB_CDECL apsTypeTag_ATEX();  // 0x000a4a20
uint32_t XB_CDECL apsTypeTag_ATEY();  // 0x000a4a30
uint32_t XB_CDECL apsTypeTag_ATEZ();  // 0x000a4a40
uint32_t XB_CDECL apsTypeTag_PAtr();  // 0x000a4a50
uint32_t XB_CDECL apsTypeTag_LAtr();  // 0x000a4a60
uint32_t XB_CDECL apsTypeTag_DLAt();  // 0x000a4a70
uint32_t XB_CDECL apsTypeTag_KTau();  // 0x000a4a80
uint32_t XB_CDECL apsTypeTag_SpDe();  // 0x000a4aa0
uint32_t XB_CDECL apsTypeTag_YHSp();  // 0x000a63c0
uint32_t XB_CDECL apsTypeTag_ZHSp();  // 0x000a63d0
uint32_t XB_CDECL apsTypeTag_SpSu();  // 0x000a63e0
uint32_t XB_CDECL apsTypeTag_Line();  // 0x000a63f0
uint32_t XB_CDECL apsTypeTag_Disc();  // 0x000a6410
uint32_t XB_CDECL apsTypeTag_Crcl();  // 0x000a6430
uint32_t XB_STDCALL FxDomain_Size(uint32_t);  // 0x00097cc0
uint32_t XB_STDCALL FxConst_Size(uint32_t);  // 0x00099550
uint32_t XB_STDCALL FxConstVector_Size(uint32_t);  // 0x000995c0
uint32_t XB_STDCALL FxRange_Size(uint32_t);  // 0x000996a0
uint32_t XB_STDCALL FxVectorRange_Size(uint32_t);  // 0x000999a0
uint32_t XB_STDCALL FxOp31_Size(uint32_t);  // 0x0009cc90
uint32_t XB_STDCALL FxOp1d5_Size(uint32_t);  // 0x0009e180
uint32_t XB_STDCALL FxOp1dd_Size(uint32_t);  // 0x0009ecd0
uint32_t XB_STDCALL FxOpdd_Size(uint32_t);  // 0x0009eec0
uint32_t XB_STDCALL FxSphereDomain_Size(uint32_t);  // 0x000a63b0
uint32_t XB_STDCALL FxLineDomain_Size(uint32_t);  // 0x000a6400
uint32_t XB_STDCALL FxDiscDomain_Size(uint32_t);  // 0x000a6420
float XB_CDECL apsVFunc_ReturnOne();                                          // 0x000a4980
uint32_t XB_STDCALL FxLineDomain_Within(uint32_t, uint32_t);                    // 0x000a4ea0
uint32_t XB_STDCALL FxDiscDomain_Within(uint32_t, uint32_t);                    // 0x000a4eb0
uint32_t XB_STDCALL FxCircleDomain_Within(uint32_t, uint32_t);                  // 0x000a4ec0
uint8_t* XB_THISCALL FxDomain_DeletingDtor(uint8_t* self, uint32_t flags);      // 0x0009a280
uint8_t* XB_THISCALL FxOpBase_DeletingDtor(uint8_t* self, uint32_t flags);      // 0x0009eea0
int32_t XB_THISCALL apsAction_GetSize(const uint8_t* self, uint32_t context);   // 0x0009c370
uint8_t* XB_THISCALL FxPoolAllocator_DeletingDtor(uint8_t* self, uint32_t flags);              // 0x0009c260
uint32_t XB_THISCALL FxPoolAllocator_Alloc(uint8_t* self, uint32_t size, uint32_t tag);        // 0x0009c290
uint32_t XB_THISCALL FxPoolAllocator_Free(uint8_t* self, uint32_t pointer);                     // 0x0009c2b0
uint32_t XB_THISCALL FxPoolAllocator_SetTag(uint8_t* self, uint32_t pointer, uint32_t tag, uint32_t unused);  // 0x0009c2c0
uint8_t* XB_THISCALL AlignedAllocator_DeletingDtor(uint8_t* self, uint32_t flags);              // 0x00094100

// --- allocators ---
uint8_t* XB_THISCALL AllocatorBase_DeletingDtor(uint8_t* self, uint32_t flags);           // 0x00023de0
void* XB_THISCALL BudgetAllocator_Alloc(uint8_t* self, int32_t size);                      // 0x0007bd90
void XB_THISCALL BudgetAllocator_Free(uint8_t* self, void* pointer, int32_t size);         // 0x0007bdc0
uint8_t* XB_THISCALL BudgetAllocator_DeletingDtor(uint8_t* self, uint32_t flags);         // 0x0007bde0
uint8_t XB_CDECL EmptyVFunc_ReturnTrue();                                                  // 0x00024220

// --- CM / physics shapes (vtable: .., Support, .., slots 4-7 contact queries, type, DeletingDtor) ---
float* XB_THISCALL PhysShape_SupportPoint(const uint8_t* self, float* out, const float* dir);  // 0x0001b9b0
uint8_t XB_STDCALL PhysShape_NoContact16(uint32_t, uint32_t, uint32_t, uint32_t);            // 0x0001bdb0
uint8_t XB_STDCALL PhysShape_NoContact20(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);  // 0x0001bda0
uint8_t XB_STDCALL CM_Mesh_NoContactSlot4(uint32_t, uint32_t, uint32_t, uint32_t);           // 0x000acc20
uint8_t XB_STDCALL CM_Mesh_NoContactSlot6(uint32_t, uint32_t, uint32_t, uint32_t);           // 0x000acc30
uint8_t XB_STDCALL CM_TriMesh_NoRayHit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);    // 0x000acc40
uint8_t XB_STDCALL CM_Mesh_NoContactSlot7(uint32_t, uint32_t, uint32_t, uint32_t);           // 0x000acc50
uint32_t XB_CDECL CM_TriMesh_GetType();                                                      // 0x0001be30
uint8_t* XB_THISCALL PhysBox_DeletingDtor(uint8_t* self, uint32_t flags);                    // 0x000acbd0
uint8_t* XB_THISCALL CM_WorldTrees_DeletingDtor(uint8_t* self, uint32_t flags);              // 0x0001d8f0
uint8_t* XB_THISCALL LevelManager_DeletingDtor(uint8_t* self, uint32_t flags);               // 0x0001ec40
void XB_STDCALL NAL_Cursor_SkipSizedBlock(uint8_t** cursor);                                  // 0x0001db70

// --- std::logic_error / length_error (MSVC std::string message: buffer +0x10, size +0x20, capacity +0x24) ---
void XB_THISCALL std_exception_Dtor(uint8_t* self);                                 // 0x001ef760
uint8_t* XB_THISCALL std_logic_error_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0001c2d0
const char* XB_THISCALL std_logic_error_What(const uint8_t* self);                 // 0x00013dd0

// --- NAL anims (NAL_GenericAnim vtable 0x007c23b0 slots 5, 6, 9, 13, 14; the anim base 0x007c2370) ---
uint32_t XB_THISCALL NAL_GenericAnim_GetField60(const uint8_t* self);                   // 0x0007bc40
uint32_t XB_THISCALL NAL_GenericAnim_GetTrackWord(const uint8_t* self, int32_t index);  // 0x0007bc50
uint8_t* XB_THISCALL NAL_GenericAnim_GetState(uint8_t* self);                           // 0x0007bc90
void XB_STDCALL NAL_GenericAnim_CopyState(void* dst, const void* src);                  // 0x0007bce0
void XB_STDCALL NAL_GenericAnim_BlendStates(uint32_t dst, uint32_t weight, uint32_t a, uint32_t b);  // 0x0007bd00
uint8_t* XB_THISCALL NAL_AnimBase_DeletingDtor(uint8_t* self, uint32_t flags);          // 0x0007b950

// --- misc ---
uint8_t* XB_THISCALL HudCompGrenadeCook_DeletingDtor(uint8_t* self, uint32_t flags);    // 0x00012420
uint32_t XB_CDECL WaitTilOutputInst2_ArgCount();                                        // 0x00029120
void XB_CDECL HudManager_Nop();                                                         // 0x00311ad0
void XB_STDCALL nglDebugLineBatch_Nop(uint32_t);                                        // 0x0006c040
uint8_t* XB_THISCALL ScrFunctorBase_DeletingDtor(uint8_t* self, uint32_t flags);        // 0x0011a2c0

}  // namespace game
