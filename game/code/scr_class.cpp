#include "scr_class.h"

#include "title_data.h"

namespace game {

namespace {

void set_vtable(uint8_t* self, uint32_t vtable) { *reinterpret_cast<uint32_t*>(self) = vtable; }

uint32_t word_at(const uint8_t* self, uint32_t offset) { return *reinterpret_cast<const uint32_t*>(self + offset); }

void scr_free(uint32_t block) {
    title_function<void XB_CDECL(uint32_t)>(0x002f5e10)(block);  // Scr_Free
}

// A member destructor (thiscall) by title address.
void member(uint32_t function, uint8_t* self, uint32_t offset) {
    title_function<void XB_THISCALL(uint8_t*)>(function)(self + offset);
}

void release(uint8_t* self, uint32_t offset) { member(0x0011d640, self, offset); }  // ScriptString_Release

void free_member(uint8_t* self, uint32_t offset) {
    if (const uint32_t block = word_at(self, offset)) scr_free(block);
}

// {data, ?, count} of script strings: releases each, then frees the data (re-read, as the original).
// The end is data + count * 4 in 32-bit arithmetic, like the original's LEA.
void release_strings(uint8_t* self, uint32_t offset) {
    const uint32_t data = word_at(self, offset);
    if (!data) return;
    const uint32_t end = data + word_at(self, offset + 8) * 4u;
    for (uint32_t p = data; p != end; p += 4) member(0x0011d640, reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(p)), 0);
    scr_free(word_at(self, offset));
}

// {data, ?, count} of 0x18-byte records whose first word is an owned block.
void free_blocks(uint8_t* self, uint32_t offset) {
    const uint32_t data = word_at(self, offset);
    if (!data) return;
    const uint32_t end = data + word_at(self, offset + 8) * 0x18u;
    for (uint32_t p = data; p != end; p += 0x18) {
        if (const uint32_t block = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(p))) scr_free(block);
    }
    scr_free(word_at(self, offset));
}

}  // namespace

// The original's C++ exception frames (unwind states per member) have no effect.
void XB_THISCALL ScrClass_007f5f4c_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f5f4c);
    release(self, 0x194);
    release(self, 0x160);
    free_member(self, 0x130);
    release(self, 0x114);
    release(self, 0xa4);
    release(self, 0x98);
    release(self, 0x64);
    release(self, 0x10);
}

void XB_THISCALL ScrClass_007f8af8_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f8af8);
    member(0x00461b50, self, 0x1d8);  // HashMap_Dtor_PROLOGUE_8
    release(self, 0x1c8);
    release(self, 0x1c4);
    member(0x00461b50, self, 0x19c);  // HashMap_Dtor_PROLOGUE_8
    member(0x00465720, self, 0x160);  // HashMap_Dtor_PROLOGUE_24
    member(0x00461ae0, self, 0x124);  // HashMap_Dtor_PROLOGUE_7
    member(0x0045ec50, self, 0xfc);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0xe4);
    free_member(self, 0xd0);
    member(0x00460f10, self, 0xa4);  // HashMap_Dtor_PROLOGUE_6
    free_member(self, 0x98);
    free_member(self, 0x8c);
    free_member(self, 0x78);
    free_member(self, 0x6c);
    release(self, 0x64);
    free_member(self, 0x50);
    member(0x0045ebe0, self, 0x28);  // HashMap_Dtor_PROLOGUE
    release_strings(self, 0x18);
    free_member(self, 0xc);
}

void XB_THISCALL ScrClass_007f8b00_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f8b00);
    member(0x00461bc0, self, 0x814);  // HashMap_Dtor_PROLOGUE_9
    release(self, 0x810);
    free_member(self, 0x7fc);
    member(0x0045ec50, self, 0x7c8);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x7a0);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x790);
    free_member(self, 0x784);
    free_member(self, 0x770);
    member(0x00463200, self, 0x744);  // HashMap_Dtor_PROLOGUE_18
    member(0x0045ec50, self, 0x71c);  // HashMap_Dtor_PROLOGUE_2
    member(0x00465830, self, 0x6f0);  // HashMap_Dtor_PROLOGUE_25
    release(self, 0x6e8);
    member(0x0045ebe0, self, 0x6bc);  // HashMap_Dtor_PROLOGUE
    release(self, 0x6b8);
    member(0x0045ec50, self, 0x690);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x684);
    member(0x0045ec50, self, 0x65c);  // HashMap_Dtor_PROLOGUE_2
    member(0x00464b70, self, 0x634);  // HashMap_Dtor_PROLOGUE_22
    member(0x0045ebe0, self, 0x604);  // HashMap_Dtor_PROLOGUE
    member(0x00464b00, self, 0x5dc);  // HashMap_Dtor_PROLOGUE_21
    member(0x00463200, self, 0x5b0);  // HashMap_Dtor_PROLOGUE_18
    member(0x0045ebe0, self, 0x57c);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x554);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x548);
    member(0x0045ec50, self, 0x51c);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x50c);
    free_member(self, 0x4f8);
    free_member(self, 0x4ec);
    free_member(self, 0x4e0);
    member(0x0045ec50, self, 0x4b4);  // HashMap_Dtor_PROLOGUE_2
    release(self, 0x4b0);
    member(0x0045ebe0, self, 0x488);  // HashMap_Dtor_PROLOGUE
    release(self, 0x480);
    member(0x0045ebe0, self, 0x458);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x430);  // HashMap_Dtor_PROLOGUE_2
    member(0x00462320, self, 0x408);  // HashMap_Dtor_PROLOGUE_15
    member(0x0045ec50, self, 0x3e0);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x3b4);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ebe0, self, 0x388);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x35c);  // HashMap_Dtor_PROLOGUE_2
    release(self, 0x358);
    member(0x0045ec50, self, 0x32c);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x2e8);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x2d4);
    member(0x004652a0, self, 0x2a8);  // HashMap_Dtor_PROLOGUE_23
    member(0x00461bc0, self, 0x274);  // HashMap_Dtor_PROLOGUE_9
    release(self, 0x264);
    member(0x0045ebe0, self, 0x238);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x208);  // HashMap_Dtor_PROLOGUE_2
    member(0x0011ab50, self, 0x0);  // ScrClass_007f8af8_Dtor
}

void XB_THISCALL ScrClass_007f8b08_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f8b08);
    member(0x0045ec50, self, 0x898);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x880);
    free_member(self, 0x874);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_007fceb4_Dtor(uint8_t* self) {
    set_vtable(self, 0x007fceb4);
    free_member(self, 0x9d8);
    free_member(self, 0x9c0);
    free_member(self, 0x9ac);
    free_member(self, 0x99c);
    free_blocks(self, 0x974);
    free_member(self, 0x93c);
    free_blocks(self, 0x930);
    free_member(self, 0x908);
    free_member(self, 0x8f4);
    free_blocks(self, 0x8e4);
    free_member(self, 0x8d8);
    free_member(self, 0x8c8);
    free_blocks(self, 0x8a0);
    member(0x004b1da0, self, 0x860);  // HashMap_Dtor_ORAN
    free_member(self, 0x840);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_007ff7d4_Dtor(uint8_t* self) {
    set_vtable(self, 0x007ff7d4);
    free_member(self, 0x940);
    free_member(self, 0x934);
    free_member(self, 0x90c);
    free_member(self, 0x8dc);
    free_member(self, 0x8c8);
    free_member(self, 0x878);
    free_member(self, 0x85c);
    free_member(self, 0x84c);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00802298_Dtor(uint8_t* self) {
    set_vtable(self, 0x00802298);
    free_member(self, 0x9d8);
    free_member(self, 0x9b8);
    release_strings(self, 0x9a8);
    free_member(self, 0x988);
    free_member(self, 0x978);
    release(self, 0x970);
    free_member(self, 0x960);
    member(0x0052eac0, self, 0x934);  // HashMap_Dtor_KASSPASS_2
    release(self, 0x928);
    member(0x0045ec50, self, 0x8f8);  // HashMap_Dtor_PROLOGUE_2
    member(0x0052dfd0, self, 0x8c8);  // HashMap_Dtor_KASSPASS
    free_member(self, 0x8b0);
    free_member(self, 0x89c);
    free_member(self, 0x888);
    free_member(self, 0x870);
    free_member(self, 0x860);
    free_member(self, 0x850);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00804cfc_Dtor(uint8_t* self) {
    set_vtable(self, 0x00804cfc);
    free_member(self, 0x8cc);
    free_member(self, 0x8b0);
    free_member(self, 0x884);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_0080873c_Dtor(uint8_t* self) {
    set_vtable(self, 0x0080873c);
    free_member(self, 0xde8);
    free_member(self, 0xdd8);
    free_member(self, 0xdc8);
    free_member(self, 0xdb4);
    release_strings(self, 0xda4);
    free_member(self, 0xd90);
    release_strings(self, 0xd84);
    release(self, 0xd80);
    member(0x005b5d00, self, 0xd54);  // HashMap_Dtor_LIBEATOR_3
    free_member(self, 0xd40);
    release_strings(self, 0xd28);
    release_strings(self, 0xd0c);
    release(self, 0xd08);
    free_member(self, 0xcfc);
    free_member(self, 0xcec);
    release_strings(self, 0xcdc);
    member(0x005b5d00, self, 0xca4);  // HashMap_Dtor_LIBEATOR_3
    free_member(self, 0xc94);
    free_member(self, 0xc84);
    free_member(self, 0xc60);
    free_member(self, 0xc44);
    release_strings(self, 0xc34);
    release_strings(self, 0xc28);
    free_member(self, 0xc0c);
    member(0x005b5d00, self, 0xbe0);  // HashMap_Dtor_LIBEATOR_3
    free_member(self, 0xbcc);
    free_member(self, 0xbc0);
    release_strings(self, 0xb94);
    release_strings(self, 0xb74);
    free_member(self, 0xb5c);
    release_strings(self, 0xb50);
    free_member(self, 0xb38);
    release_strings(self, 0xb18);
    release_strings(self, 0xb0c);
    free_member(self, 0xaf8);
    free_member(self, 0xaec);
    free_member(self, 0xae0);
    free_member(self, 0xad0);
    free_member(self, 0xac0);
    free_member(self, 0xab4);
    free_member(self, 0xa94);
    free_member(self, 0xa58);
    free_member(self, 0xa34);
    free_member(self, 0xa08);
    free_member(self, 0x9fc);
    free_member(self, 0x9e8);
    free_member(self, 0x9dc);
    free_member(self, 0x9bc);
    free_member(self, 0x9b0);
    free_member(self, 0x9a4);
    free_member(self, 0x998);
    free_member(self, 0x980);
    free_member(self, 0x974);
    free_member(self, 0x964);
    free_member(self, 0x944);
    member(0x005b5c90, self, 0x91c);  // HashMap_Dtor_LIBEATOR_2
    free_member(self, 0x910);
    free_member(self, 0x904);
    free_member(self, 0x8ec);
    member(0x005b5c90, self, 0x8b0);  // HashMap_Dtor_LIBEATOR_2
    free_member(self, 0x894);
    release(self, 0x88c);
    release_strings(self, 0x878);
    free_member(self, 0x864);
    release_strings(self, 0x84c);
    free_member(self, 0x840);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_0080ddf8_Dtor(uint8_t* self) {
    set_vtable(self, 0x0080ddf8);
    free_member(self, 0x8ac);
    release(self, 0x898);
    free_member(self, 0x880);
    free_member(self, 0x870);
    free_member(self, 0x84c);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_008134a4_Dtor(uint8_t* self) {
    set_vtable(self, 0x008134a4);
    release_strings(self, 0x97c);
    free_member(self, 0x968);
    free_member(self, 0x924);
    member(0x0045ebe0, self, 0x8f4);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x8c0);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x8a4);
    free_member(self, 0x898);
    release(self, 0x88c);
    free_member(self, 0x874);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00815830_Dtor(uint8_t* self) {
    set_vtable(self, 0x00815830);
    free_member(self, 0x930);
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00819a34_Dtor(uint8_t* self) {
    set_vtable(self, 0x00819a34);
    free_member(self, 0xa94);
    free_member(self, 0xa60);
    free_member(self, 0xa48);
    free_member(self, 0xa3c);
    free_member(self, 0xa08);
    free_member(self, 0x9f8);
    free_member(self, 0x9d8);
    free_member(self, 0x9cc);
    member(0x0045ebe0, self, 0x984);  // HashMap_Dtor_PROLOGUE
    free_member(self, 0x978);
    member(0x0045ec50, self, 0x938);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x910);  // HashMap_Dtor_PROLOGUE_2
    release(self, 0x908);
    release(self, 0x904);
    member(0x0045ec50, self, 0x8dc);  // HashMap_Dtor_PROLOGUE_2
    free_member(self, 0x8d0);
    free_member(self, 0x8c4);
    release(self, 0x8b4);
    free_member(self, 0x8a4);
    free_member(self, 0x884);
    member(0x0045ec50, self, 0x858);  // HashMap_Dtor_PROLOGUE_2
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_0081d404_Dtor(uint8_t* self) {
    set_vtable(self, 0x0081d404);
    free_member(self, 0xa64);
    free_member(self, 0x9a4);
    free_member(self, 0x964);
    member(0x0045ec50, self, 0x8e0);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ebe0, self, 0x8b4);  // HashMap_Dtor_PROLOGUE
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00820538_Dtor(uint8_t* self) {
    set_vtable(self, 0x00820538);
    release(self, 0x95c);
    member(0x0045ebe0, self, 0x910);  // HashMap_Dtor_PROLOGUE
    member(0x0045ec50, self, 0x8d4);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x8a0);  // HashMap_Dtor_PROLOGUE_2
    member(0x0045ec50, self, 0x858);  // HashMap_Dtor_PROLOGUE_2
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00821f34_Dtor(uint8_t* self) {
    set_vtable(self, 0x00821f34);
    member(0x0078d830, self, 0x968);  // HashMap_Dtor_BUCHHOLZ_2
    member(0x0078d910, self, 0x938);  // HashMap_Dtor_BUCHHOLZ_4
    free_member(self, 0x91c);
    member(0x0045f1a0, self, 0x8d8);  // HashMap_Dtor_PROLOGUE_4
    member(0x00464b70, self, 0x8a8);  // HashMap_Dtor_PROLOGUE_22
    member(0x0045fe40, self, 0x87c);  // HashMap_Dtor_PROLOGUE_5
    member(0x0078d8a0, self, 0x840);  // HashMap_Dtor_BUCHHOLZ_3
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_00823508_Dtor(uint8_t* self) {
    set_vtable(self, 0x00823508);
    free_member(self, 0x8fc);
    member(0x0045ebe0, self, 0x8ac);  // HashMap_Dtor_PROLOGUE
    member(0x0011ad40, self, 0x0);  // ScrClass_007f8b00_Dtor
}

void XB_THISCALL ScrClass_007f5f9c_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f5f9c);
    title_function<void XB_THISCALL(uint8_t*)>(0x0011a9b0)(self);  // ScrClass_007f5f4c_Dtor
}

uint8_t* XB_THISCALL ScrWad_Anim_DeletingDtor(uint8_t* self, uint32_t flags) {
    title_function<void XB_THISCALL(uint8_t*)>(0x0011cca0)(self);  // ScrClass_007f5f9c_Dtor
    if (flags & 1) scr_free(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)));
    return self;
}

// --- Script-thread functors (AeThreadFunctor*: the bound arguments of a thread started from
// C, vtable per argument list, base vtable 0x007f48d4). The destructors release the bound
// strings / owned blocks / RespawnerAttributes (0x0052b970) and then store the base vtable;
// the deleting destructors free through the builtin table's SmallFree (0x0092292c).

namespace {

template <uint32_t Dtor>
uint8_t* deleting(uint8_t* self, uint32_t flags) {
    title_function<void XB_THISCALL(uint8_t*)>(Dtor)(self);
    if (flags & 1) title_global<void XB_CDECL (*)(void*)>(0x0092292c)(self);  // ScrIface_SmallFree
    return self;
}

}  // namespace

void XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_Dtor(uint8_t* self) {
    member(0x0052b970, self, 0xc);  // FUN_0052b970
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor3_ent_str_vec_Dtor(uint8_t* self) {
    release(self, 0xc);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor4_ent_ent_arrf_i_Dtor(uint8_t* self) {
    free_member(self, 0x10);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_Dtor(uint8_t* self) {
    free_member(self, 0x14);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_Dtor(uint8_t* self) {
    release(self, 0x10);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_Dtor(uint8_t* self) {
    release(self, 0xc);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor4_ent_str_i_str_Dtor(uint8_t* self) {
    release(self, 0x14);
    release(self, 0xc);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_Dtor(uint8_t* self) {
    release(self, 0x14);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_Dtor(uint8_t* self) {
    release(self, 0xf4);
    member(0x0052b970, self, 0x10);  // FUN_0052b970
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_Dtor(uint8_t* self) {
    release(self, 0x10);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_Dtor(uint8_t* self) {
    release(self, 0x18);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor5_vt00825c50_Dtor(uint8_t* self) {
    release(self, 0x18);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_Dtor(uint8_t* self) {
    release(self, 0x1c);
    release(self, 0x10);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_Dtor(uint8_t* self) {
    free_member(self, 0x30);
    free_member(self, 0x10);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor_Dtor_2(uint8_t* self) {
    release(self, 0x14);
    set_vtable(self, 0x007f48d4);
}

void XB_THISCALL AeThreadFunctor_Dtor(uint8_t* self) {
    set_vtable(self, 0x007f48d4);
}

uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b6a0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b710>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor3_ent_str_vec_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b780>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b7f0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b860>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b8e0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_i_str_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017b950>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_arrf_i_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017c8d0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017c940>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x0017c9c0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x001e5740>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x001e5680>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor_DeletingDtor_2(uint8_t* self, uint32_t flags) { return deleting<0x001e56f0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x001e5770>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x001e57e0>(self, flags); }
uint8_t* XB_THISCALL AeThreadFunctor5_vt00825c50_DeletingDtor(uint8_t* self, uint32_t flags) { return deleting<0x001e5850>(self, flags); }

}  // namespace game
