#pragma once

// FE_Setup* panel helpers for the collector's-edition, Liberator, chapter and mission success/fail panels: the register-argument functions FE_SetupPanel calls at level start
// (helpers shared with fe_setup.cpp in fe_setup_internal.h).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00323160 (menu in ECX, panel in EAX): panel to +8; the ammo pictures (+0xc / +0x10, every 8 bytes), the six
// targets to +0x6c (hidden with a panel) and nine more hidden, the four background positions to +0x84 / +0x94, the
// meter count 0, then the cross icons and the HUD manager's +0x1c (vtable +4) at the original's ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupLiberatorMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupLiberatorMenu_entry(void);
// 0x00324740 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, six items with their texts,
// the Helpbar copy to +0x38, the Number text +0x3c, the picture rectangle +0x118..+0x124 with its quad +0x50, the
// full-screen quad +0xb4, the arrows +0x128 / +0x12c hidden.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupSpecialConceptMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupSpecialConceptMenu_entry(void);
// 0x00324ab0 (menu in ESI, panel in EAX): as FE_SetupSpecialConceptMenu with five items, the rectangle at +0xcc and
// its quad +0x68, the emptied Title / Number texts +0x4c / +0x50, the arrows +0x54 / +0x58.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupSpecialMaterialsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupSpecialMaterialsMenu_entry(void);
// 0x00324f70 (panel in EAX, menu on the stack, ret 4): panel to +0x34 (Panel_SetLayer 2), background +0x18c; per
// chapter 1-14 the lock / chapter%iD / chapter pictures (+0x3c / +0x74 / +0xac), the name / title / quote texts
// (+0xe4 / +0x11c / +0x154), the item with its quote text, the title, the item links; the Helpbar copy to +0x38.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupChapterMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp,
                                                             uint32_t caller_ebx);
extern "C" void FE_SetupChapterMenu_entry(void);
// 0x00325370 (panel in EAX, menu on the stack, ret 4): panel to +0x34 (Panel_SetLayer 2), three items, three grey
// texts +0x40..+0x48, whitelines +0x50 hidden, the Helpbar copy +0x38, the savecontinue copy +0x3c with its text,
// the item links, a new grey UIText +0x4c with item 0's scale.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupMissionSuccessFailMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupMissionSuccessFailMenu_entry(void);

}  // namespace game
