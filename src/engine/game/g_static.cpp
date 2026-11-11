#include "g_static.h"

#include <cstring>
#include <initializer_list>

#include "cg_ents.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "phys.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "ragdoll.h"
#include "title_data.h"
#include "script_string.h"

namespace game {

namespace {

// Four floats stored as one 16-byte block {a, a, b, a} (built on the aligned stack, moved with MOVAPS): bit copies.
void store_vec4_aaba(uint32_t address, uint32_t a, uint32_t b) {
    const uint32_t lanes[4] = {a, a, b, a};
    std::memcpy(&title_global<uint8_t>(address), lanes, sizeof lanes);
}

uint32_t constant(uint32_t address) { return title_global<uint32_t>(address); }

// Mem_AlignedFree of the block a global holds, the global cleared.
void free_aligned_global(uint32_t address) {
    void* const block = title_global<void*>(address);
    if (!block) return;
    Mem_AlignedFree(block);
    title_global<uint32_t>(address) = 0;
}

void release_script_string(uint32_t address) {
    own_function<void XB_THISCALL(uint32_t)>(&ScriptString_Release_entry)(address);  // ScriptString_Release
}

void at_exit(uint32_t function) { title_function<int32_t XB_CDECL(uint32_t)>(0x001eed36)(function); }  // _atexit

// The 128 placeholder weapon items' names ("emptyitem_...").
const uint32_t kEmptyItemNames[128] = {
    0x00836904, 0x00836914, 0x00836924, 0x00836934, 0x00836944, 0x00836954, 0x00836964, 0x00836974,
    0x00836984, 0x00836994, 0x008369a4, 0x008369b4, 0x008369c4, 0x008369d4, 0x008369e4, 0x008369f4,
    0x00836a04, 0x00836a14, 0x00836a24, 0x00836a34, 0x00836a44, 0x00836a54, 0x00836a64, 0x00836a74,
    0x00836a84, 0x00836a94, 0x00836aa4, 0x00836ab4, 0x00836ac4, 0x00836ad4, 0x00836ae4, 0x00836af4,
    0x00836b04, 0x00836b14, 0x00836b24, 0x00836b34, 0x00836b44, 0x00836b54, 0x00836b64, 0x00836b74,
    0x00836b84, 0x00836b94, 0x00836ba4, 0x00836bb4, 0x00836bc4, 0x00836bd4, 0x00836be4, 0x00836bf4,
    0x00836c04, 0x00836c14, 0x00836c24, 0x00836c34, 0x00836c44, 0x00836c54, 0x00836c64, 0x00836c74,
    0x00836c84, 0x00836c94, 0x00836ca4, 0x00836cb4, 0x00836cc4, 0x00836cd4, 0x00836ce4, 0x00836cf4,
    0x00836d04, 0x00836d14, 0x00836d24, 0x00836d34, 0x00836d44, 0x00836d54, 0x00836d64, 0x00836d74,
    0x00836d84, 0x00836d94, 0x00836da4, 0x00836db4, 0x00836dc4, 0x00836dd4, 0x00836de4, 0x00836df4,
    0x00836e04, 0x00836e14, 0x00836e24, 0x00836e34, 0x00836e44, 0x00836e54, 0x00836e64, 0x00836e74,
    0x00836e84, 0x00836e94, 0x00836ea4, 0x00836eb4, 0x00836ec4, 0x00836ed4, 0x00836ee4, 0x00836ef4,
    0x00836f04, 0x00836f14, 0x00836f24, 0x00836f34, 0x00836f48, 0x00836f5c, 0x00836f70, 0x00836f84,
    0x00836f98, 0x00836fac, 0x00836fc0, 0x00836fd4, 0x00836fe8, 0x00836ffc, 0x00837010, 0x00837024,
    0x00837038, 0x0083704c, 0x00837060, 0x00837074, 0x00837088, 0x0083709c, 0x008370b0, 0x008370c4,
    0x008370d8, 0x008370ec, 0x00837100, 0x00837114, 0x00837128, 0x0083713c, 0x00837150, 0x00837164,
};

// The other seven items: name and words +8..+0x30 (+0 is the name's hash).
const uint32_t kItems[7][12] = {
    {0x00837178, 0x00837198, 0x008371a8, 0x00000000, 0x008371c8, 0x008371c8, 0x008371e8, 0x0000000a, 0x00000002, 0xffffffff, 0xffffffff, 0xffffffff},
    {0x00837204, 0x00837198, 0x00837228, 0x00000000, 0x008371c8, 0x008371c8, 0x00837248, 0x0000000a, 0x00000002, 0xffffffff, 0xffffffff, 0xffffffff},
    {0x00837268, 0x0083727c, 0x00837290, 0x00000000, 0x008372c0, 0x00000000, 0x008372d4, 0x0000000a, 0x00000003, 0x00000000, 0x00000000, 0x00000000},
    {0x008372e4, 0x008372f8, 0x00837310, 0x00000000, 0x00837340, 0x00000000, 0x00837350, 0x00000019, 0x00000003, 0x00000000, 0x00000000, 0x00000000},
    {0x0083735c, 0x00837370, 0x00837384, 0x00000000, 0x008373b4, 0x00000000, 0x008373c8, 0x00000032, 0x00000003, 0x00000000, 0x00000000, 0x00000000},
    {0x008373d8, 0x008373ec, 0x00837408, 0x00000000, 0x008373b4, 0x00000000, 0x0083744c, 0x000003e7, 0x00000005, 0x00000000, 0x00000000, 0x00000000},
    {0x0083745c, 0x00837470, 0x00837488, 0x00000000, 0x008373b4, 0x00000000, 0x008374cc, 0x000003e7, 0x00000004, 0xffffffff, 0xffffffff, 0xffffffff},
};



}  // namespace

void XB_CDECL StaticInit_HandleTable_8dda2c() {
    HandleTable256_Init(reinterpret_cast<uint8_t*>(uintptr_t{0x008dd208}));  // scr_threadHandles
    title_global<uint32_t>(0x008dda2c) = 0;
    title_global<uint32_t>(0x008dda30) = 0;
    title_global<uint32_t>(0x008dda34) = 0;
}

void XB_CDECL StaticInit_ItemList() {
    // bg_itemlist (0x0096bd44, 0x34 bytes per item): 128 placeholder weapon items (name hash, name, six empty strings
    // 0x0082dfeb, five zeros), then seven others; one more zero word and the 12 words after the list cleared.
    uint32_t* item = &title_global<uint32_t>(0x0096bd44);
    for (uint32_t i = 0; i < 128; ++i, item += 13) {
        item[0] = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{kEmptyItemNames[i]}));
        item[1] = kEmptyItemNames[i];
        for (uint32_t w = 2; w < 8; ++w) item[w] = 0x0082dfeb;
        for (uint32_t w = 8; w < 13; ++w) item[w] = 0;
    }
    for (uint32_t i = 0; i < 7; ++i, item += 13) {
        item[0] = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{kItems[i][0]}));
        for (uint32_t w = 0; w < 12; ++w) item[1 + w] = kItems[i][w];
    }
    item[0] = 0;  // 0x0096d8b0
    for (uint32_t w = 0; w < 0xc; ++w) title_global<uint32_t>(0x0096d8b4 + w * 4) = 0;  // REP STOSD
}

void XB_CDECL StaticInit_DownVec4_95ff20() { store_vec4_aaba(0x0095ff20, 0, constant(0x0083eb34)); }
void XB_CDECL StaticInit_Vec4_960060() { store_vec4_aaba(0x00960060, constant(0x0083ef14), constant(0x0083eb00)); }
void XB_CDECL StaticInit_Vec4_95fe60() { store_vec4_aaba(0x0095fe60, constant(0x0083ef10), constant(0x0083ecdc)); }

void XB_CDECL StaticInit_G_EntityList() {
    G_EntityList_Init(&title_global<uint32_t>(0x0097a8c0));
    title_global<uint32_t>(0x0097d36c) = 0;
}

void XB_CDECL StaticInit_DownVec4_984ab0() { store_vec4_aaba(0x00984ab0, 0, constant(0x0083eb34)); }
void XB_CDECL StaticInit_Vec4_97a8a0() { store_vec4_aaba(0x0097a8a0, constant(0x0083ef14), 0); }
void XB_CDECL StaticInit_Vec4_984d00() { store_vec4_aaba(0x00984d00, constant(0x0083ef10), constant(0x0083ecdc)); }
void XB_CDECL StaticInit_Vec4_988390() { store_vec4_aaba(0x00988390, constant(0x0083f298), 0); }
void XB_CDECL StaticInit_Vec4_985360() { store_vec4_aaba(0x00985360, constant(0x0083ef00), constant(0x0083ebf0)); }

void XB_CDECL StaticInit_SpawnFuncList() {
    // A std::list (allocator byte 0x00983e1c - a never-written stack byte, 0 here; head 0x00983e24, size 0), a
    // vector of 9 copies of the head pointer (0x00983e2c), two words 1, and its atexit destructor (0x00379c10).
    title_global<uint8_t>(0x00983e1c) = 0;
    uint8_t* const head = std_list_BuyHeadZ();
    title_global<uint32_t>(0x00983e24) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head));
    title_global<uint32_t>(0x00983e28) = 0;
    const uint32_t value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head));
    std_vector_ConstructN4Z_2(9, &title_global<uint8_t>(0x00983e2c), &value);
    title_global<uint32_t>(0x00983e3c) = 1;
    title_global<uint32_t>(0x00983e40) = 1;
    at_exit(0x00379c10);
}

void XB_CDECL StaticInit_Vec4_989230() { store_vec4_aaba(0x00989230, 0, constant(0x0083eb34)); }

void XB_CDECL StaticInit_VehiclePhysPool() {
    VehiclePhysPool_Reset(&title_global<uint8_t>(0x008fe0d0));
    at_exit(0x00379d40);
}

void XB_CDECL StaticInit_DownVec4_98aa80() { store_vec4_aaba(0x0098aa80, 0, constant(0x0083eb34)); }
void XB_CDECL StaticInit_g_actorMins() { store_vec4_aaba(0x0098a560, constant(0x0083ef14), 0); }
void XB_CDECL StaticInit_g_actorMaxs() { store_vec4_aaba(0x0098a550, constant(0x0083ef10), constant(0x0083ecdc)); }

void XB_CDECL AtExit_ReleaseScriptStrings_986028() {
    release_script_string(0x00986028);
    release_script_string(0x00985ec0);  // tail jump
}

void XB_CDECL AtExit_FreeAligned_984cec() { free_aligned_global(0x00984cec); }
void XB_CDECL AtExit_g_entityRemoveQueue() { free_aligned_global(0x00987a78); }
void XB_CDECL AtExit_FreeAligned_97a640() { free_aligned_global(0x0097a640); }

PLATFORM_ASM_ENTRY(GScr_ConstStrings_Dtor_entry, "pushl %esi\n" PLATFORM_ASM_CALL(GScr_ConstStrings_Dtor) "addl $4, %esp\n"
                                                "ret")

void XB_CDECL GScr_ConstStrings_Dtor(uint8_t* strings) {
    // Members +0x21c down to +0 (the last one a tail jump).
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(strings));
    for (uint32_t offset = 0x220; offset != 0;) {
        offset -= 4;
        release_script_string(base + offset);
    }
}

void XB_CDECL AtExit_984ac0() {
    TitleRegs regs{};
    regs.esi = 0x00984ac0;
    cg_call_regs(own_address(&GScr_ConstStrings_Dtor_entry), &regs, nullptr, 0);  // GScr_ConstStrings_Dtor
}

void XB_CDECL AtExit_9883b0() { VehiclePathState_Dtor(&title_global<uint8_t>(0x009883b0)); }  // tail jump
void XB_CDECL AtExit_FreeAligned_984ce0() { free_aligned_global(0x00984ce0); }
void XB_CDECL AtExit_FreeAligned_97a8b0() { free_aligned_global(0x0097a8b0); }
void XB_CDECL AtExit_RagdollPool() { RagdollPool_ReleaseAllPhysics(&title_global<uint8_t>(0x008dfde0)); }
void XB_CDECL AtExit_RagdollPhysPool() { RagdollPhysPool_DestroyAll(&title_global<uint8_t>(0x008f0470)); }

void XB_CDECL AtExit_ReleaseScriptStrings_98aa74() {
    // 0x20 strings 0x28 bytes apart below 0x0098aa74, the last first.
    uint32_t address = 0x0098aa74;
    for (uint32_t i = 0; i < 0x20; ++i) {
        address -= 0x28;
        release_script_string(address);
    }
}

void XB_CDECL StaticInit_List_8feba0() {
    // An empty two-node ring: 0x008feba4 <-> 0x008feba8 (counts 0x008feba0 / 0x008feba8 0).
    title_global<uint32_t>(0x008feba0) = 0;
    title_global<uint32_t>(0x008feba4) = 0x008feba8;
    title_global<uint32_t>(0x008feba8) = 0;
    title_global<uint32_t>(0x008febac) = 0x008feba4;
}

}  // namespace game

PLATFORM_ASM_ENTRY(StaticObj_985370_Ctor_entry, "pushl %esi\n" PLATFORM_ASM_CALL(StaticObj_985370_Ctor) "addl $4, %esp\n ret")
PLATFORM_ASM_ENTRY(StaticObj_984ac0_Ctor_entry, "pushl %esi\n" PLATFORM_ASM_CALL(StaticObj_984ac0_Ctor) "addl $4, %esp\n ret")
PLATFORM_ASM_ENTRY(StaticObj_9883b0_Ctor_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(StaticObj_9883b0_Ctor) "addl $4, %esp\n ret")

namespace game {

namespace {
void empty_string(uint8_t* self, uint32_t at) { ScriptString_FromRep(reinterpret_cast<ScriptString*>(self + at), nullptr); }
uint32_t& word32(uint8_t* self, uint32_t at) { return *reinterpret_cast<uint32_t*>(self + at); }
}  // namespace

uint8_t* XB_THISCALL StaticObj_ScriptStrings15_Ctor(uint8_t* self) {
    for (uint32_t i = 0; i < 15; ++i) empty_string(self, i * 4);
    for (uint32_t i = 0; i < 15; ++i) word32(self, 0x3c + i * 0x104) = 0;
    word32(self, 0xf98) = 0;
    return self;
}

uint8_t* XB_THISCALL ScrUndefinedInts21_Ctor(uint8_t* self) {
    word32(self, 0) = 0;
    for (uint32_t at = 4; at <= 0x54; at += 4) word32(self, at) = title_global<uint32_t>(0x00922ec0);  // g_scrUndefinedInt
    return self;
}

void XB_THISCALL ScrStringHolder_Dtor(uint8_t* self) {
    ScriptString_Release(reinterpret_cast<ScriptString*>(self + 0xc));
    word32(self, 0) = 0x007f48c4;
}

}  // namespace game

extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_985370_Ctor(uint8_t* self) {
    using namespace game;
    for (uint32_t i = 0; i < 0x40; ++i) {
        word32(self, 0xb4 + i * 8) = 0;
        word32(self, 0xb8 + i * 8) = 0;
    }
    for (uint32_t at = 0xac0; at <= 0xb3c; at += 4) word32(self, at) = 0;
    empty_string(self, 0xb50);
    word32(self, 0xcb4) = 0;
    empty_string(self, 0xcb8);
    for (uint32_t i = 0; i < 0x100; ++i) {
        word32(self, 0xcfc + i * 0x10) = 0;
        word32(self, 0xd00 + i * 0x10) = 0;
    }
    return self;
}

extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_984ac0_Ctor(uint8_t* self) {
    for (uint32_t i = 0; i < 136; ++i) game::empty_string(self, i * 4);
    return self;
}

extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_9883b0_Ctor(uint8_t* self) {
    for (uint32_t i = 0; i < 2; ++i)
        for (const uint32_t at : {0x38u, 0x3cu, 0x48u}) game::empty_string(self, at + i * 0x40);
    return self;
}
