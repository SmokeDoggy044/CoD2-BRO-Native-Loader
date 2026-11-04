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

}  // namespace game
