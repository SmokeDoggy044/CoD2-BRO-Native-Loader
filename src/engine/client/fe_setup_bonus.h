#pragma once

// FE_Setup* panel helpers for the bonus gallery panels (BonusConcepts / Vehicles / Weapons / Characters / Materials): the register-argument functions FE_SetupPanel calls at level start
// (helpers shared with fe_setup.cpp in fe_setup_internal.h).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00326010 (menu in ECX, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, 13 items with their
// BONUS_BONUS_LEVEL_<n> texts, New_Stamp_<n> to +0x48 (hidden), the Helpbar copy to +0x38, Number to +0x3c, the
// picture frame (+0x1fc) and quad (+0xd0), two full-screen quads (+0x134, +0x198 black at z 1).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupBonusConceptsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupBonusConceptsMenu_entry(void);

// 0x00326460 (menu in ECX, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, six items with their texts,
// six stamps to +0x50 (hidden), the Helpbar copy to +0x38, Vehicles_Title +0x3c, Number +0x40, the wrapped
// Vehicles_Info copy +0x44, the picture frame (+0x1c4) and quad (+0x98), quads +0xfc and +0x160 (black at z 1).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupBonusVehiclesMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupBonusVehiclesMenu_entry(void);

// 0x00326970 (menu in ECX, panel in EAX): as the vehicles panel with ten items and stamps, Weapons_Title / Number /
// Weapons_Info, the picture frame (+0x184) and quad (+0xbc), one full-screen quad (+0x120).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupBonusWeaponsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupBonusWeaponsMenu_entry(void);

// 0x00326f20 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, seven items with their
// texts, seven stamps to +0x44 (hidden), the Helpbar copy to +0x38, Character_Name +0x68, the wrapped
// Character_Stats / Character_Bio copies +0x6c / +0x70, the picture frame (+0xe4) and quad (+0x80).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupBonusCharactersMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupBonusCharactersMenu_entry(void);

// 0x00327490 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, six items with their texts,
// six stamps to +0x48 (hidden), the Helpbar copy to +0x38, the picture frame (+0xf8, width + 100) and quad (+0x94),
// Title / Number +0x70 / +0x74 emptied, two pictures to +0x68 / +0x6c (hidden), +0x78 = 2.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupBonusMaterialsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupBonusMaterialsMenu_entry(void);

}  // namespace game
