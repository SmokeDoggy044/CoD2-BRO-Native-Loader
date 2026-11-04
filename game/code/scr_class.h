#pragma once

// Destructors of the per-level script classes (the "*_wad" Level/Anim objects: one class per
// mission on a shared base, vtables 0x007f5f4c..0x00823508; ScrClass_<vtable>_Dtor). Each sets its
// vtable, then destroys its members from the last to the first: script strings
// (ScriptString_Release), owned blocks (Scr_Free when set), hash maps (their destructors in the
// level code, by title address), string arrays and arrays of 0x18-byte records whose first word
// is an owned block ({data, ?, count}: each element, then the data block), and finally the base
// class (ScrClass_007f8b00_Dtor, whose base is ScrClass_007f8af8_Dtor). The bodies are generated
// from the disassembly by a script (each body checked call for call against it).

#include <cstdint>

#include "compiler.h"

namespace game {

void XB_THISCALL ScrClass_007f5f4c_Dtor(uint8_t* self);  // 0x0011a9b0
void XB_THISCALL ScrClass_007f8af8_Dtor(uint8_t* self);  // 0x0011ab50
void XB_THISCALL ScrClass_007f8b00_Dtor(uint8_t* self);  // 0x0011ad40
void XB_THISCALL ScrClass_007f8b08_Dtor(uint8_t* self);  // 0x0011b130
void XB_THISCALL ScrClass_007fceb4_Dtor(uint8_t* self);  // 0x0011b570
void XB_THISCALL ScrClass_007ff7d4_Dtor(uint8_t* self);  // 0x0011b890
void XB_THISCALL ScrClass_00802298_Dtor(uint8_t* self);  // 0x0011be10
void XB_THISCALL ScrClass_00804cfc_Dtor(uint8_t* self);  // 0x0011c030
void XB_THISCALL ScrClass_0080873c_Dtor(uint8_t* self);  // 0x0011c1d0
void XB_THISCALL ScrClass_0080ddf8_Dtor(uint8_t* self);  // 0x0011ca20
void XB_THISCALL ScrClass_008134a4_Dtor(uint8_t* self);  // 0x0011cb70
void XB_THISCALL ScrClass_00815830_Dtor(uint8_t* self);  // 0x0011ccd0
void XB_THISCALL ScrClass_00819a34_Dtor(uint8_t* self);  // 0x0011ce10
void XB_THISCALL ScrClass_0081d404_Dtor(uint8_t* self);  // 0x0011d150
void XB_THISCALL ScrClass_00820538_Dtor(uint8_t* self);  // 0x0011d220
void XB_THISCALL ScrClass_00821f34_Dtor(uint8_t* self);  // 0x0011d390
void XB_THISCALL ScrClass_00823508_Dtor(uint8_t* self);  // 0x0011d470
// 0x0011cca0: sets vtable 0x007f5f9c, then tail-jumps to ScrClass_007f5f4c_Dtor.
void XB_THISCALL ScrClass_007f5f9c_Dtor(uint8_t* self);
// 0x0011cdd0: ScrClass_007f5f9c_Dtor, then Scr_Free(self) when bit 0 of `flags` is set; returns self.
uint8_t* XB_THISCALL ScrWad_Anim_DeletingDtor(uint8_t* self, uint32_t flags);

// Script-thread functor destructors (base vtable 0x007f48d4; see scr_class.cpp).
void XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_Dtor(uint8_t* self);  // 0x0017b8e0
void XB_THISCALL AeThreadFunctor3_ent_str_vec_Dtor(uint8_t* self);  // 0x0017b780
void XB_THISCALL AeThreadFunctor4_ent_ent_arrf_i_Dtor(uint8_t* self);  // 0x0017c8d0
void XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_Dtor(uint8_t* self);  // 0x0017c9c0
void XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_Dtor(uint8_t* self);  // 0x0017b6a0
void XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_Dtor(uint8_t* self);  // 0x001e57e0
void XB_THISCALL AeThreadFunctor4_ent_str_i_str_Dtor(uint8_t* self);  // 0x0017b950
void XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_Dtor(uint8_t* self);  // 0x0017b710
void XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_Dtor(uint8_t* self);  // 0x0017b860
void XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_Dtor(uint8_t* self);  // 0x001e5680
void XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_Dtor(uint8_t* self);  // 0x0017b7f0
void XB_THISCALL AeThreadFunctor5_vt00825c50_Dtor(uint8_t* self);  // 0x001e5850
void XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_Dtor(uint8_t* self);  // 0x001e5770
void XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_Dtor(uint8_t* self);  // 0x0017c940
void XB_THISCALL AeThreadFunctor_Dtor_2(uint8_t* self);  // 0x001e56f0
void XB_THISCALL AeThreadFunctor_Dtor(uint8_t* self);  // 0x001e5740
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b680
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b6f0
uint8_t* XB_THISCALL AeThreadFunctor3_ent_str_vec_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b760
uint8_t* XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b7d0
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b840
uint8_t* XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b8c0
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_i_str_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017b930
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_arrf_i_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017c8b0
uint8_t* XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017c920
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0017c9a0
uint8_t* XB_THISCALL AeThreadFunctor_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x001e5640
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x001e5660
uint8_t* XB_THISCALL AeThreadFunctor_DeletingDtor_2(uint8_t* self, uint32_t flags);  // 0x001e56d0
uint8_t* XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x001e5750
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x001e57c0
uint8_t* XB_THISCALL AeThreadFunctor5_vt00825c50_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x001e5830

// Script-thread functor constructors (thiscall, callee pops the arguments; see scr_class.cpp).
uint8_t* XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3);  // 0x0017a9a0
uint8_t* XB_THISCALL AeThreadFunctor3_ent_str_vec_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);  // 0x0017a650
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);  // 0x0017c830
uint8_t* XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);  // 0x0017a3c0
uint8_t* XB_THISCALL AeThreadFunctor4_ent_i_pathnode_str_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);  // 0x001e3ec0
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);  // 0x001e4280
uint8_t* XB_THISCALL AeThreadFunctor4_ent_str_i_str_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);  // 0x0017aa40
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x0017a490
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x0017a870
uint8_t* XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x001e3df0
uint8_t* XB_THISCALL AeThreadFunctor5_ent_hash_AnimDirections_AnimAngles_f_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x001e4080
uint8_t* XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x0017a740
uint8_t* XB_THISCALL AeThreadFunctor5_ent_vec_vec_vec_i_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x001e4330
uint8_t* XB_THISCALL AeThreadFunctor5_vt00825c50_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);  // 0x001e4430
uint8_t* XB_THISCALL AeThreadFunctor6_ent_hash_i_str_i_ent_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);  // 0x001e3fb0
uint8_t* XB_THISCALL AeThreadFunctor7_ent_hash_vec_hash_f_hash_f_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);  // 0x0017a550
uint8_t* XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9);  // 0x001e4150
uint8_t* XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_Ctor(uint8_t* self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10);  // 0x0017c770


// Functor Invoke (vtable slot 1; thread switch: kept original)
void XB_THISCALL AeThreadFunctor2_ent_RespawnerAttributes_Invoke(uint8_t* self);  // 0x0017aa00
void XB_THISCALL AeThreadFunctor3_ent_hash_hash_Invoke(uint8_t* self);  // 0x0017a600
void XB_THISCALL AeThreadFunctor3_ent_str_vec_Invoke(uint8_t* self);  // 0x0017a6d0
void XB_THISCALL AeThreadFunctor3_ent_vec_b_Invoke(uint8_t* self);  // 0x0017ab40
void XB_THISCALL AeThreadFunctor4_ent_ent_arrf_i_Invoke(uint8_t* self);  // 0x0017c1b0
void XB_THISCALL AeThreadFunctor4_ent_ent_b_hash_Invoke(uint8_t* self);  // 0x001e3dc0
void XB_THISCALL AeThreadFunctor4_ent_ent_ent_arrent_Invoke(uint8_t* self);  // 0x0017caf0
void XB_THISCALL AeThreadFunctor4_ent_ent_i_vec_Invoke(uint8_t* self);  // 0x001e3f70
void XB_THISCALL AeThreadFunctor4_ent_ent_str_vec_Invoke(uint8_t* self);  // 0x0017a440
void XB_THISCALL AeThreadFunctor4_ent_f_f_b_Invoke(uint8_t* self);  // 0x0017a3a0
void XB_THISCALL AeThreadFunctor4_ent_hash_i_f_Invoke(uint8_t* self);  // 0x001e4130
void XB_THISCALL AeThreadFunctor4_ent_i_pathnode_str_Invoke(uint8_t* self);  // 0x001e3f30
void XB_THISCALL AeThreadFunctor4_ent_str_hash_hash_Invoke(uint8_t* self);  // 0x001e42f0
void XB_THISCALL AeThreadFunctor4_ent_str_i_str_Invoke(uint8_t* self);  // 0x0017aac0
void XB_THISCALL AeThreadFunctor5_ent_ent_RespawnerAttributes_i_str_Invoke(uint8_t* self);  // 0x0017a900
void XB_THISCALL AeThreadFunctor5_ent_ent_hash_ent_hash_Invoke(uint8_t* self);  // 0x0017a370
void XB_THISCALL AeThreadFunctor5_ent_ent_i_str_b_Invoke(uint8_t* self);  // 0x0017a510
void XB_THISCALL AeThreadFunctor5_ent_ent_str_ent_ent_Invoke(uint8_t* self);  // 0x001e3e70
void XB_THISCALL AeThreadFunctor5_ent_f_f_hash_hash_Invoke(uint8_t* self);  // 0x0017a720
void XB_THISCALL AeThreadFunctor5_ent_hash_AnimDirections_AnimAngles_f_Invoke(uint8_t* self);  // 0x001e40e0
void XB_THISCALL AeThreadFunctor5_ent_vec_str_vec_f_Invoke(uint8_t* self);  // 0x0017a7e0
void XB_THISCALL AeThreadFunctor5_ent_vec_vec_vec_i_Invoke(uint8_t* self);  // 0x001e43a0
void XB_THISCALL AeThreadFunctor5_vt00825c50_Invoke(uint8_t* self);  // 0x001e44b0
void XB_THISCALL AeThreadFunctor6_ent_hash_f_i_f_f_Invoke(uint8_t* self);  // 0x001e44f0
void XB_THISCALL AeThreadFunctor6_ent_hash_i_str_i_ent_Invoke(uint8_t* self);  // 0x001e4030
void XB_THISCALL AeThreadFunctor6_vt008245b4_Invoke(uint8_t* self);  // 0x0017bbe0
void XB_THISCALL AeThreadFunctor7_ent_anim_anim_anim_anim_anim_anim_Invoke(uint8_t* self);  // 0x001e4400
void XB_THISCALL AeThreadFunctor7_ent_ent_f_i_i_i_f_Invoke(uint8_t* self);  // 0x0017a840
void XB_THISCALL AeThreadFunctor7_ent_hash_vec_hash_f_hash_f_Invoke(uint8_t* self);  // 0x0017a5c0
void XB_THISCALL AeThreadFunctor8_ent_anim_str_f_hash_str_f_b_Invoke(uint8_t* self);  // 0x001e41f0
void XB_THISCALL AeThreadFunctor9_ent_ent_arrent_f_f_hash_hash_i_arr_Invoke(uint8_t* self);  // 0x0017ca40
void XB_THISCALL AeThreadFunctor9_ent_hash_i_i_f_f_f_f_hash_Invoke(uint8_t* self);  // 0x0017a620

}  // namespace game
