#include "bro_scr_wad.h"

#include "scr_class.h"
#include "title_data.h"

namespace game {

namespace {

uint8_t* delete_wad(uint8_t* self, uint32_t flags, void(XB_THISCALL* dtor)(uint8_t*)) {
    dtor(self);
    if (flags & 1) title_function<void XB_CDECL(uint8_t*)>(0x002f5e10)(self);  // Scr_Free
    return self;
}

}  // namespace

uint8_t* XB_THISCALL anim_wad_Anim_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007f5f4c_Dtor); }
uint8_t* XB_THISCALL anim_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007f8af8_Dtor); }
uint8_t* XB_THISCALL util_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007f8b00_Dtor); }
uint8_t* XB_THISCALL prologue_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007f8b08_Dtor); }
uint8_t* XB_THISCALL oran_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007fceb4_Dtor); }
uint8_t* XB_THISCALL tunisia_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_007ff7d4_Dtor); }
uint8_t* XB_THISCALL kasserine_pass_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00802298_Dtor); }
uint8_t* XB_THISCALL kasserine_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00804cfc_Dtor); }
uint8_t* XB_THISCALL liberator_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_0080873c_Dtor); }
uint8_t* XB_THISCALL gela_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_0080ddf8_Dtor); }
uint8_t* XB_THISCALL piano_lupo_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_008134a4_Dtor); }
uint8_t* XB_THISCALL troina_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00815830_Dtor); }
uint8_t* XB_THISCALL omaha_beach_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00819a34_Dtor); }
uint8_t* XB_THISCALL mons1_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_0081d404_Dtor); }
uint8_t* XB_THISCALL eilendorf_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00820538_Dtor); }
uint8_t* XB_THISCALL buchholz_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00821f34_Dtor); }
uint8_t* XB_THISCALL dragons_teeth_wad_Level_DeletingDtor(uint8_t* self, uint32_t flags) { return delete_wad(self, flags, &ScrClass_00823508_Dtor); }

}  // namespace game
