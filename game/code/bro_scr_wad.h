#pragma once

// The per-level script wads' deleting destructors (vtable slot 0 of each wad class): the class
// destructor (scr_class.cpp), then Scr_Free(self) when bit 0 of `flags` is set; returns self.

#include <cstdint>

#include "compiler.h"

namespace game {

uint8_t* XB_THISCALL anim_wad_Anim_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011aa70
uint8_t* XB_THISCALL anim_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011ad10
uint8_t* XB_THISCALL util_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011b0f0
uint8_t* XB_THISCALL prologue_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011b110
uint8_t* XB_THISCALL oran_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011b550
uint8_t* XB_THISCALL tunisia_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011b870
uint8_t* XB_THISCALL kasserine_pass_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011bdf0
uint8_t* XB_THISCALL kasserine_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011c010
uint8_t* XB_THISCALL liberator_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011c1b0
uint8_t* XB_THISCALL gela_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011ca00
uint8_t* XB_THISCALL piano_lupo_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011cb50
uint8_t* XB_THISCALL troina_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011ccb0
uint8_t* XB_THISCALL omaha_beach_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011cdf0
uint8_t* XB_THISCALL mons1_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011d130
uint8_t* XB_THISCALL eilendorf_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011d200
uint8_t* XB_THISCALL buchholz_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011d370
uint8_t* XB_THISCALL dragons_teeth_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags);  // 0x0011d450

}  // namespace game
