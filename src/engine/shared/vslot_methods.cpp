#include "vslot_methods.h"

#include "cg_hud.h"
#include "nal_anim.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "title_data.h"
#include "title_heap.h"
#include "fx.h"
#include "fx_load.h"
#include "r_init.h"

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

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Deleting destructors that only put the class's vtable back before the optional free.
template <typename T>
T* delete_z_free(T* self, uint32_t flags, uint32_t vtable) {
    word<uint32_t>(self, 0) = vtable;
    if (flags & 1) own_function<void XB_CDECL(T*)>(&Z_Free)(self);  // Z_Free (also through its jmp at 0x00011050)
    return self;
}

template <typename T>
T* delete_aligned_free(T* self, uint32_t flags, uint32_t vtable) {
    word<uint32_t>(self, 0) = vtable;
    if (flags & 1) own_function<void XB_CDECL(T*)>(&Mem_AlignedFree)(self);  // Mem_AlignedFree
    return self;
}

// A material's NewInstance slot: a new 8-byte instance of the same class over `section`
// (Mem_AlignedAlloc(8, 4, 0)). The original also stores the section through a null result.
uint32_t* new_material_instance(uint32_t section, uint32_t vtable) {
    auto* instance = static_cast<uint32_t*>(own_function<void* XB_CDECL(uint32_t, uint32_t, uint32_t)>(
        &Mem_AlignedAlloc)(8, 4, 0));  // Mem_AlignedAlloc
    if (instance) instance[0] = vtable;
    instance[1] = section;
    return instance;
}

// A material's NewVertexIter slot: a stack iterator {vtable, material, 0} cloned through the iterator
// vtable's Clone slot (called through the vtable word, as the original does).
uint32_t** new_vertex_iter(const void* material, uint32_t** out, uint32_t vtable) {
    uint32_t iter[3] = {vtable, address_of(material), 0};
    using Clone = uint32_t* XB_THISCALL(const uint32_t*);
    *out = title_function<Clone>(title_global<uint32_t>(vtable + 4))(iter);
    return out;
}

}  // namespace

// --- ngl/cd* material instances and their vertex iterators/writers ---

void XB_THISCALL nglMaterial_AddSectionOffset(uint32_t* self, uint32_t delta) { self[1] += delta; }

void XB_THISCALL nglMaterial_Free(uint32_t* self) {
    own_function<void XB_CDECL(uint32_t*)>(&Mem_AlignedFree)(self);  // Mem_AlignedFree
}

// Iterator {vtable, material, index}: index below the section's vertex count (+0x40).
uint32_t XB_THISCALL nglVertexIter_IsValid(const uint32_t* self) {
    const auto* material = reinterpret_cast<const uint32_t*>(uintptr_t{self[1]});
    return self[2] < word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{material[1]}), 0x40) ? 1 : 0;
}

// Writer {vtable, index, material}.
uint32_t XB_THISCALL nglVertexWriter_IsValid(const uint32_t* self) {
    const auto* material = reinterpret_cast<const uint32_t*>(uintptr_t{self[2]});
    return self[1] < word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{material[1]}), 0x40) ? 1 : 0;
}

uint32_t* XB_THISCALL nglVertexIterBase_DeletingDtor(uint32_t* self, uint32_t flags) {
    return delete_z_free(self, flags, 0x00832668);
}

uint32_t* XB_THISCALL nglDebugMeshVertexIter_DeletingDtor(uint32_t* self, uint32_t flags) {
    return delete_z_free(self, flags, 0x00832668);
}

uint32_t** XB_THISCALL nglPolyMaterial_NewVertexIter(const void* self, uint32_t** out) {
    return new_vertex_iter(self, out, 0x0083be3c);
}
uint32_t** XB_THISCALL cdOcean_NewVertexIter(const void* self, uint32_t** out) {
    return new_vertex_iter(self, out, 0x007b6944);
}
uint32_t** XB_THISCALL cdSky_NewVertexIter(const void* self, uint32_t** out) {
    return new_vertex_iter(self, out, 0x007b7a6c);
}
uint32_t** XB_THISCALL nglDebugMesh_NewVertexIter(const void* self, uint32_t** out) {
    return new_vertex_iter(self, out, 0x007be7d8);
}

uint32_t* XB_THISCALL nglPosMaterial_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x00832760); }
uint32_t* XB_THISCALL nglPolyMaterial_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x0083be78); }
uint32_t* XB_THISCALL cdGunSight_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b441c); }
uint32_t* XB_THISCALL cdGun_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b46f4); }
uint32_t* XB_THISCALL cdGlass_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b49c4); }
uint32_t* XB_THISCALL cdSimpleAlpha_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b4cac); }
uint32_t* XB_THISCALL cdPropeller_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b4f88); }
uint32_t* XB_THISCALL cdChar_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b551c); }
uint32_t* XB_THISCALL cdPrelit_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b57f8); }
uint32_t* XB_THISCALL cdAirplaneMetal_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b5acc); }
uint32_t* XB_THISCALL cdSimpleUVAnim_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b5dac); }
uint32_t* XB_THISCALL cdSimplePrelit_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b608c); }
uint32_t* XB_THISCALL cdSimple_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b6368); }
uint32_t* XB_THISCALL cdRiver_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b6690); }
uint32_t* XB_THISCALL cdOcean_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b6930); }
uint32_t* XB_THISCALL cdWater_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b6c04); }
uint32_t* XB_THISCALL cdWorldBlend_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b6edc); }
uint32_t* XB_THISCALL cdWorldVertexLit_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b71d0); }
uint32_t* XB_THISCALL cdWorld_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b74a4); }
uint32_t* XB_THISCALL cdBackground_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b777c); }
uint32_t* XB_THISCALL cdSky_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007b7a58); }
uint32_t* XB_THISCALL nglDebugMesh_NewInstance(void*, uint32_t section) { return new_material_instance(section, 0x007be7f4); }

uint8_t* XB_THISCALL ShaderPCUV_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007b4160); }
uint8_t* XB_THISCALL cdSky_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007b7a20); }

// cdOcean's shader-type slot 11 (between GetVertexShader 0x009020e8 and GetPixelShader).
uint32_t XB_CDECL cdOcean_GetVertexShader2() { return 0x009020c4; }

// --- ngl/anim resource managers and their skip-list iterators ---

namespace {

// A manager's NewIterator: a 12-byte iterator over the skip list at +4, on the head's first node
// (null when the allocation fails).
uint32_t* new_skip_list_iter(uint8_t* manager, uint32_t vtable) {
    auto* iter = static_cast<uint32_t*>(own_function<void* XB_CDECL(uint32_t, uint32_t, uint32_t)>(
        &Mem_AlignedAlloc)(0xc, 4, 0));  // Mem_AlignedAlloc
    if (!iter) return nullptr;
    uint8_t* list = manager + 4;
    iter[0] = vtable;
    iter[1] = address_of(list);
    const uint32_t head = word<uint32_t>(list, 8);
    iter[2] = head ? word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{head}), 4) : 0;
    return iter;
}

// The managers' Add/Remove slots: `add ecx, 4; jmp` to the skip list's own method.
template <typename R, auto Target>
R list_forward(uint8_t* self, const uint32_t* object) {
    return target_function<R XB_THISCALL(uint8_t*, const uint32_t*), Target>()(self + 4, object);
}

}  // namespace

void XB_THISCALL SkipListIter_Reset(uint32_t* self) {
    const uint32_t head = word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{self[1]}), 8);
    self[2] = head ? word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{head}), 4) : 0;
}

uint32_t XB_THISCALL SkipListIter_IsValid(const uint32_t* self) { return self[2] != 0 ? 1 : 0; }

void XB_THISCALL SkipListIter_Next(uint32_t* self) {
    if (self[2]) self[2] = word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{self[2]}), 4);
}

uint32_t XB_THISCALL SkipListIter_Current(const uint32_t* self) {
    return self[2] ? word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{self[2]}), 0) : 0;
}

uint32_t* XB_THISCALL nglTextureManager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007bc964); }
uint32_t* XB_THISCALL nglPaletteManager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007bc9b0); }
uint32_t* XB_THISCALL nglFontManager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007bd11c); }
uint32_t* XB_THISCALL nglMeshFileManager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007bdfa8); }
uint32_t* XB_THISCALL nglMeshManager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007bdff4); }
uint32_t* XB_THISCALL nglSection3Manager_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007be040); }
uint32_t* XB_THISCALL nglMeshFileCache_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007c12a8); }
uint32_t* XB_THISCALL AnimManager2c9c_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007c2cc0); }
uint32_t* XB_THISCALL AnimManager2ce8_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007c2d0c); }
uint32_t* XB_THISCALL AnimManager2d34_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007c2d58); }
uint32_t* XB_THISCALL AnimManager2d80_NewIterator(uint8_t* self) { return new_skip_list_iter(self, 0x007c2da4); }

uint8_t* XB_THISCALL nglTextureIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bc978); }
uint8_t* XB_THISCALL nglPaletteIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bc9c4); }
uint8_t* XB_THISCALL nglFontIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bd130); }
uint8_t* XB_THISCALL nglMeshFileIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bdfbc); }
uint8_t* XB_THISCALL nglSection3Iter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007be054); }
uint8_t* XB_THISCALL nglMeshFileCacheIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c12bc); }
uint8_t* XB_THISCALL AnimManager2c9cIter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c2cd4); }
uint8_t* XB_THISCALL AnimManager2ce8Iter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c2d20); }
uint8_t* XB_THISCALL AnimManager2d34Iter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c2d6c); }
uint8_t* XB_THISCALL AnimManager2d80Iter_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c2db8); }
uint8_t* XB_THISCALL nglPaletteManagerBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bc91c); }
uint8_t* XB_THISCALL nglFontManagerBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bd0d4); }
uint8_t* XB_THISCALL nglSection3ManagerBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007bdf30); }
uint8_t* XB_THISCALL nglMeshFileCacheBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c1260); }

uint8_t XB_THISCALL nglTextureManager_Remove(uint8_t* self, const uint32_t* texture) {
    return list_forward<uint8_t, 0x00063c40>(self, texture);  // nglSkipList_RemoveTexture
}
uint8_t XB_THISCALL nglMeshManager_Remove(uint8_t* self, const uint32_t* object) {
    return list_forward<uint8_t, 0x00069b40>(self, object);  // nglSkipList_RemoveIndirect
}
const uint32_t* XB_THISCALL nglMeshManager_Add(uint8_t* self, const uint32_t* object) {
    return list_forward<const uint32_t*, 0x0006a0f0>(self, object);  // skip-list insert on the indirect name
}
uint8_t XB_THISCALL AnimManager_Remove(uint8_t* self, const uint32_t* object) {
    return list_forward<uint8_t, 0x0007bed0>(self, object);  // AnimManager_SkipListRemove
}
const uint32_t* XB_THISCALL AnimManager_Add(uint8_t* self, const uint32_t* object) {
    return list_forward<const uint32_t*, 0x00083db0>(self, object);  // AnimManager_SkipListInsert
}
const uint32_t* XB_THISCALL NalManagerKey8_Add(uint8_t* self, const uint32_t* object) {
    return list_forward<const uint32_t*, 0x000841b0>(self, object);  // skip-list insert on the name at +8
}

// Slot 8 (Flush) of the mesh/section managers' base: nothing to flush.
uint32_t XB_STDCALL nglMeshManagerBase_FlushNop(uint32_t, uint32_t, uint32_t) { return 1; }

// --- Fx (aps) domains, ops and actions ---

uint32_t XB_CDECL apsTypeTag_1dBx() { return 0x31644278; }
uint32_t XB_CDECL apsTypeTag_3dBx() { return 0x33644278; }
uint32_t XB_CDECL apsTypeTag_1dPt() { return 0x31645074; }
uint32_t XB_CDECL apsTypeTag_3dPt() { return 0x33645074; }
uint32_t XB_CDECL apsTypeTag_1dBu() { return 0x31644275; }
uint32_t XB_CDECL apsTypeTag_3dBu() { return 0x33644275; }
uint32_t XB_CDECL apsTypeTag_SMsh() { return 0x534d7368; }
uint32_t XB_CDECL apsTypeTag_Rect() { return 0x52656374; }
uint32_t XB_CDECL apsTypeTag_CUVA() { return 0x43555641; }
uint32_t XB_CDECL apsTypeTag_CBlb() { return 0x43426c62; }
uint32_t XB_CDECL apsTypeTag_Blbo() { return 0x426c626f; }
uint32_t XB_CDECL apsTypeTag_EScW() { return 0x45536357; }
uint32_t XB_CDECL apsTypeTag_EScH() { return 0x45536348; }
uint32_t XB_CDECL apsTypeTag_VAVD() { return 0x56415644; }
uint32_t XB_CDECL apsTypeTag_ATEX() { return 0x41544558; }
uint32_t XB_CDECL apsTypeTag_ATEY() { return 0x41544559; }
uint32_t XB_CDECL apsTypeTag_ATEZ() { return 0x4154455a; }
uint32_t XB_CDECL apsTypeTag_PAtr() { return 0x50417472; }
uint32_t XB_CDECL apsTypeTag_LAtr() { return 0x4c417472; }
uint32_t XB_CDECL apsTypeTag_DLAt() { return 0x444c4174; }
uint32_t XB_CDECL apsTypeTag_KTau() { return 0x4b546175; }
uint32_t XB_CDECL apsTypeTag_SpDe() { return 0x53704465; }
uint32_t XB_CDECL apsTypeTag_YHSp() { return 0x59485370; }
uint32_t XB_CDECL apsTypeTag_ZHSp() { return 0x5a485370; }
uint32_t XB_CDECL apsTypeTag_SpSu() { return 0x53705375; }
uint32_t XB_CDECL apsTypeTag_Line() { return 0x4c696e65; }
uint32_t XB_CDECL apsTypeTag_Disc() { return 0x44697363; }
uint32_t XB_CDECL apsTypeTag_Crcl() { return 0x4372636c; }

uint32_t XB_STDCALL FxDomain_Size(uint32_t) { return 0x4; }
uint32_t XB_STDCALL FxConst_Size(uint32_t) { return 0x8; }
uint32_t XB_STDCALL FxConstVector_Size(uint32_t) { return 0x10; }
uint32_t XB_STDCALL FxRange_Size(uint32_t) { return 0xc; }
uint32_t XB_STDCALL FxVectorRange_Size(uint32_t) { return 0x1c; }
uint32_t XB_STDCALL FxOp31_Size(uint32_t) { return 0x14; }
uint32_t XB_STDCALL FxOp1d5_Size(uint32_t) { return 0x40; }
uint32_t XB_STDCALL FxOp1dd_Size(uint32_t) { return 0x50; }
uint32_t XB_STDCALL FxOpdd_Size(uint32_t) { return 0x40; }
uint32_t XB_STDCALL FxSphereDomain_Size(uint32_t) { return 0x20; }
uint32_t XB_STDCALL FxLineDomain_Size(uint32_t) { return 0x30; }
uint32_t XB_STDCALL FxDiscDomain_Size(uint32_t) { return 0x40; }

float XB_CDECL apsVFunc_ReturnOne() { return title_global<float>(0x0083eb20); }  // 1.0f

uint32_t XB_STDCALL FxLineDomain_Within(uint32_t, uint32_t) { return 0; }
uint32_t XB_STDCALL FxDiscDomain_Within(uint32_t, uint32_t) { return 0; }
uint32_t XB_STDCALL FxCircleDomain_Within(uint32_t, uint32_t) { return 0; }

uint8_t* XB_THISCALL FxDomain_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007c57a0); }
uint8_t* XB_THISCALL FxOpBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007c66f0); }

// The action's record size: 0x1c + 4 per value (+0xa) and child (+0x12) slot, plus each child's own Size (slot 5).
int32_t XB_THISCALL apsAction_GetSize(const uint8_t* self, uint32_t context) {
    const int32_t children = word<int16_t>(self, 0x12);
    int32_t size = (children + word<int16_t>(self, 0xa)) * 4 + 0x1c;
    const auto* child = reinterpret_cast<uint8_t* const*>(uintptr_t{word<uint32_t>(self, 0xc)});
    for (const auto* end = child + children; child != end; ++child) {
        if (!*child) continue;
        using Size = int32_t XB_THISCALL(uint8_t*, uint32_t);
        size += title_function<Size>(title_global<uint32_t>(word<uint32_t>(*child, 0) + 0x14))(*child, context);
    }
    return size;
}

// The particle library's allocator (vtable 0x007c5fc0, AlignedAllocator's derived class) on the pool set at 0x00909f08.
uint8_t* XB_THISCALL FxPoolAllocator_DeletingDtor(uint8_t* self, uint32_t flags) {
    title_global<uint32_t>(0x00909f04) = 0;
    return delete_z_free(self, flags, 0x007c4d90);
}

uint32_t XB_THISCALL FxPoolAllocator_Alloc(uint8_t*, uint32_t size, uint32_t tag) {
    if (!size) return 0;
    using Alloc = uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t);
    return own_function<Alloc>(&apsPoolSet_Alloc)(title_global<uint32_t>(0x00909f08), size, tag);  // apsPoolSet_Alloc
}

uint32_t XB_THISCALL FxPoolAllocator_Free(uint8_t*, uint32_t pointer) {
    using Release = uint32_t XB_THISCALL(uint32_t, uint32_t);
    return own_function<Release>(&FxPools_Release)(title_global<uint32_t>(0x00909f08), pointer);  // FxPools_Release
}

uint32_t XB_THISCALL FxPoolAllocator_SetTag(uint8_t*, uint32_t pointer, uint32_t tag, uint32_t) {
    if (!tag) return 0;
    using SetTag = uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t);
    return own_function<SetTag>(&FxPools_SetSlotTag)(title_global<uint32_t>(0x00909f08), pointer, tag);  // FxPools_SetSlotTag
}

uint8_t* XB_THISCALL AlignedAllocator_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007c4d90); }

// --- allocators ---

// The allocator interface's base (vtable 0x0083760c: DeletingDtor, Alloc, Free).
uint8_t* XB_THISCALL AllocatorBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x0083760c); }

// vtable 0x007c2400: Mem_AlignedAlloc(size, 16) while the running total (+8) stays within the budget (+4).
void* XB_THISCALL BudgetAllocator_Alloc(uint8_t* self, int32_t size) {
    const int32_t used = word<int32_t>(self, 8) + size;
    if (used > word<int32_t>(self, 4)) return nullptr;
    word<int32_t>(self, 8) = used;
    return own_function<void* XB_CDECL(int32_t, uint32_t, uint32_t)>(&Mem_AlignedAlloc)(size, 0x10, 0);  // Mem_AlignedAlloc
}

void XB_THISCALL BudgetAllocator_Free(uint8_t* self, void* pointer, int32_t size) {
    word<int32_t>(self, 8) -= size;
    own_function<void XB_CDECL(void*)>(&Mem_AlignedFree)(pointer);  // Mem_AlignedFree
}

uint8_t* XB_THISCALL BudgetAllocator_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x0083760c); }

uint8_t XB_CDECL EmptyVFunc_ReturnTrue() { return 1; }

// --- CM / physics shapes ---

// Support slot of shapes that are a point: the point at +0x10, whatever the direction.
float* XB_THISCALL PhysShape_SupportPoint(const uint8_t* self, float* out, const float*) {
    for (uint32_t i = 0; i < 4; ++i) out[i] = word<float>(self, 0x10 + i * 4);
    return out;
}

uint8_t XB_STDCALL PhysShape_NoContact16(uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint8_t XB_STDCALL PhysShape_NoContact20(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint8_t XB_STDCALL CM_Mesh_NoContactSlot4(uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint8_t XB_STDCALL CM_Mesh_NoContactSlot6(uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint8_t XB_STDCALL CM_TriMesh_NoRayHit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint8_t XB_STDCALL CM_Mesh_NoContactSlot7(uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }
uint32_t XB_CDECL CM_TriMesh_GetType() { return 6; }

uint8_t* XB_THISCALL PhysBox_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_z_free(self, flags, 0x007cc6c0); }

uint8_t* XB_THISCALL CM_WorldTrees_DeletingDtor(uint8_t* self, uint32_t flags) {
    word<uint32_t>(self, 0) = 0x00832788;
    own_function<void XB_CDECL(const char*)>(&Cmd_RemoveCommand)(&title_global<const char>(0x00833520));  // "cg"
    own_function<void XB_THISCALL(uint8_t*)>(&ResourceManager_Destruct)(self);  // ResourceManager_Destruct
    if (flags & 1) own_function<void XB_CDECL(uint8_t*)>(&Z_Free)(self);  // Z_Free
    return self;
}

uint8_t* XB_THISCALL LevelManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    word<uint32_t>(self, 0) = 0x00832614;
    own_function<void XB_THISCALL(uint8_t*)>(&ResourceManager_Destruct)(self);  // ResourceManager_Destruct
    if (flags & 1) own_function<void XB_CDECL(uint8_t*)>(&Z_Free)(self);  // Z_Free
    return self;
}

// A channel class's block skip: the block starts with its own byte size.
void XB_STDCALL NAL_Cursor_SkipSizedBlock(uint8_t** cursor) { *cursor += word<uint32_t>(*cursor, 0); }

// --- std::exception (LIBCMT) ---

// std::exception's destructor: frees the message when the exception owns it (_m_doFree).
void XB_THISCALL std_exception_Dtor(uint8_t* self) {
    word<uint32_t>(self, 0) = 0x00827904;  // std::exception
    if (word<uint32_t>(self, 8) != 0) title_crt_free(word<uint32_t>(self, 4));  // _free
}

// --- std::logic_error ---

uint8_t* XB_THISCALL std_logic_error_DeletingDtor(uint8_t* self, uint32_t flags) {
    word<uint32_t>(self, 0) = 0x007f6748;  // std::logic_error
    if (word<uint32_t>(self, 0x24) >= 0x10)
        own_function<void XB_CDECL(uint8_t*)>(&Z_Free)(
            reinterpret_cast<uint8_t*>(uintptr_t{word<uint32_t>(self, 0x10)}));  // Z_Free (the string's buffer)
    word<uint32_t>(self, 0x24) = 0xf;
    word<uint32_t>(self, 0x20) = 0;
    self[0x10] = 0;
    std_exception_Dtor(self);
    if (flags & 1) own_function<void XB_CDECL(uint8_t*)>(&Z_Free)(self);  // Z_Free
    return self;
}

const char* XB_THISCALL std_logic_error_What(const uint8_t* self) {
    if (word<uint32_t>(self, 0x24) >= 0x10) return reinterpret_cast<const char*>(uintptr_t{word<uint32_t>(self, 0x10)});
    return reinterpret_cast<const char*>(self + 0x10);
}

// --- NAL anims ---

uint32_t XB_THISCALL NAL_GenericAnim_GetField60(const uint8_t* self) { return word<uint32_t>(self, 0x60); }

// The track table (+0x64, 20-byte records): the record's first word.
uint32_t XB_THISCALL NAL_GenericAnim_GetTrackWord(const uint8_t* self, int32_t index) {
    return word<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{word<uint32_t>(self, 0x64)}), uint32_t(index) * 20);
}

uint8_t* XB_THISCALL NAL_GenericAnim_GetState(uint8_t* self) { return self + 0xc8; }

void XB_STDCALL NAL_GenericAnim_CopyState(void* dst, const void* src) {
    own_function<void XB_THISCALL(void*, const void*)>(&NAL_AnimState_Copy)(dst, src);  // NAL_AnimState_Copy
}

void XB_STDCALL NAL_GenericAnim_BlendStates(uint32_t dst, uint32_t weight, uint32_t a, uint32_t b) {
    uint32_t args[4] = {dst, weight, a, b};  // the argument block the original pushes (Blend moves its cursors in it)
    NAL_AnimState_Blend(args);
}

uint8_t* XB_THISCALL NAL_AnimBase_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_aligned_free(self, flags, 0x007c2370); }

// --- misc ---

uint8_t* XB_THISCALL HudCompGrenadeCook_DeletingDtor(uint8_t* self, uint32_t flags) {
    HudCompGrenadeCook_Dtor(self);
    if (flags & 1) own_function<void XB_CDECL(uint8_t*)>(&Z_Free)(self);  // Z_Free
    return self;
}

uint32_t XB_CDECL WaitTilOutputInst2_ArgCount() { return 2; }

void XB_CDECL HudManager_Nop() {}

void XB_STDCALL nglDebugLineBatch_Nop(uint32_t) {}

// The script functor base (vtable 0x007f48d4): freed through the builtin table's SmallFree (0x0092292c).
uint8_t* XB_THISCALL ScrFunctorBase_DeletingDtor(uint8_t* self, uint32_t flags) {
    word<uint32_t>(self, 0) = 0x007f48d4;
    if (flags & 1) title_global<void XB_CDECL (*)(void*)>(0x0092292c)(self);  // ScrIface_SmallFree
    return self;
}

}  // namespace game
