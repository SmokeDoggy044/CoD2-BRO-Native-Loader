#pragma once

// The FE_SetupPanel helpers: each fills one front-end screen's menu object from its loaded panel (pictures and
// texts by name, the help bar copy).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00325c90 (menu in EDI, panel in EAX): panel to +0x34; +0x38 = a copy of the "Resume" text (+0x13c (8),
// colour -1, +0xe4 (20)).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupControllerDisconnected(uint8_t* menu, uint8_t* panel);
extern "C" void FE_SetupControllerDisconnected_entry(void);
// 0x003278b0 (menu in ESI, panel in EAX): pictures maubeuge3/2/1 and Back to +0x44..+0x50, the LoadingMenu
// text to +0x54, the Helpbar copy (ECX flag 0x40, scale 1.3) to +0x58.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupLoadMaubeugeMenu(uint8_t* menu, uint8_t* panel);
extern "C" void FE_SetupLoadMaubeugeMenu_entry(void);
// 0x00329fe0 (menu in ESI, panel in EAX): panel to +0x30, five pictures to +0x10..+0x20, the old +0x24 text
// deleted, +0x24 = the Helpbar copy with its text (+0x6c, at the original's ESP).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupMemcardMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupMemcardMenu_entry(void);
// 0x00223140 (menu in ESI, panel in EAX): items 0-2 from three panel texts, their texts, +0x38 = the Helpbar
// copy with its text (the setters at the original's ESP).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupInGameLiveOptions(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupInGameLiveOptions_entry(void);
// 0x00223260: the same code as FE_SetupInGameLiveOptions.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupXboxLiveSingle(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupXboxLiveSingle_entry(void);
// 0x003279b0 (menu in ESI, panel in EAX): numbered pictures 1-19 to +0x48, three named to +0x98, Back +0x94,
// LoadingMenu text +0xa4, the Helpbar copy (flag 0x40, scale 1.3) +0xa8.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupLoadSicilyMenu(uint8_t* menu, uint8_t* panel);
extern "C" void FE_SetupLoadSicilyMenu_entry(void);
// 0x00327b00 (menu in ESI, panel in EAX): numbered pictures 23-70 to +0x48, five named to +0x10c, Back +0x108,
// LoadingMenu text +0x120, the Helpbar copy +0x124.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupLoadAfricaMenu(uint8_t* menu, uint8_t* panel);
extern "C" void FE_SetupLoadAfricaMenu_entry(void);
// 0x00327c70 (menu in ESI, panel in EAX): numbered pictures 1-42 to +0x48, five named to +0xf4, Back +0xf0,
// LoadingMenu text +0x108, the Helpbar copy +0x10c.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupLoadingMenu(uint8_t* menu, uint8_t* panel);
extern "C" void FE_SetupLoadingMenu_entry(void);

// 0x00325210 (menu in ESI, panel in EAX): panel to +0x3c (Panel_SetLayer 2), items 0-3 from four panel texts,
// +0x40 = the Helpbar copy with its text, then the item up / down links (+8 / +0xa).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupAutosaveMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupAutosaveMenu_entry(void);

// 0x00328d10 (menu in ESI, panel in EAX): panel to +0x38 (Panel_SetLayer 2), items 0-4 from five panel texts and
// their texts, +0x34 = the Helpbar copy with its text.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupSinglePlayerMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupSinglePlayerMenu_entry(void);

// 0x00325e60 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), difficulty0..2 hidden, items easy /
// medium / hard, the arrows to +0x3c / +0x40, the Helpbar copy to +0x38, the item links.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupDifficultyMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupDifficultyMenu_entry(void);
// 0x00325ac0 (menu in ESI, panel in EAX): panel to +0x38; the 40 key texts / shadows, four key pictures, the
// Helpbar copy to +0x3c, the title and the profile name text.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupCreateProfileMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupCreateProfileMenu_entry(void);
// 0x0032a660 (panel in EAX, menu on the stack, ret 4): the login status texts; the ybutton copy to +0x1c.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FE_SetupLoginStatus(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupLoginStatus_entry(void);

// 0x0032a910 (panel in EAX, its file name in EDX; the owner on the stack, ret 4): the panel set up by its file name -
// each FE_Setup* helper with its menu (from the front-end / in-game screen lists, the dialog manager or the owner), the
// title screen's help text, the HUD's vtable +4; an unknown panel is deleted. FE_SetupPanelAt is the C version
// behind FE_SetupPanel_entry (the entry's registers and ESP).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL FE_SetupPanelAt(uint8_t* panel, const char* name, uint32_t entry,
                                                              uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void FE_SetupPanel_entry(void);

// 0x00327de0 (menu in ECX, panel in EAX): panel to +0x34 (Panel_SetLayer 2), three items with their texts, the
// title, the arrows hidden, the volume bar to +0x3c, the Helpbar copy to +0x38, two more pictures hidden.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupSoundMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupSoundMenu_entry(void);

// 0x003256b0 (menu in ESI, panel in EAX): panel to +0x3c (Panel_SetLayer 2), items Profile1..4, the title, the
// Helpbar copy to +0x40, the profile picture to +0x58, the item links.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupProfileMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupProfileMenu_entry(void);

// 0x00328f70 (menu in ESI, panel in EAX): panel to +0x38 (Panel_SetLayer 2), four items and the special edition
// item (or a DISABLED one) by 0x0095e7f8, the item texts, the Helpbar copy to +0x34, the check pictures hidden.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupMainMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupMainMenu_entry(void);

// 0x00328af0 (menu in ECX, panel in EAX): panel to +0x34 (Panel_SetLayer 2), three items with their texts spread
// around half their gap, the Helpbar copy to +0x38, mp_menu / mp_back hidden.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupOptionsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupOptionsMenu_entry(void);

// 0x00329c80 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), the title, five items with their texts
// (+0x5c 0.7), the Helpbar1 / Helpbar2 copies to +0x38 / +0x3c, the background pictures and help texts 10 lower.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupPauseMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupPauseMenu_entry(void);

// 0x00329480 (menu in ESI, panel in EAX): the in-game options panel: panel to +0x34 (Panel_SetLayer 2), the title,
// twelve items with their texts (all scaled 0.7), the value texts +0x38..+0x50, the row arrows +0x54..+0x88 (+0xb4
// the first one's +0xac), three bars +0x8c..+0x94, the Helpbar1..3 copies to +0x98..+0xa0; outside English the
// subtitles row hidden and the rows below moved up.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupAdvancedOptionsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupAdvancedOptionsMenu_entry(void);

// 0x00328720 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), seven items with their texts (items 3 /
// 4 scaled 0.7), the title, the on/off texts to +0x3c..+0x4c, the arrows to +0x50..+0x74 (+0x78 the first one's
// +0xac), the Helpbar copy to +0x38, mp_menu / mp_back hidden; outside English the subtitles item and its arrows off.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupGameplayMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupGameplayMenu_entry(void);

// 0x00328210 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), seven items with their texts, the title,
// the on/off and layout texts +0x3c..+0x44, the slider arrows +0x48..+0x5c (+0xc4 the first one's +0xac), the
// sensitivity bars +0x60 / +0x64, the button-layout labels +0x68..+0xb4 (the two clicks black), mp_menu / mp_back
// hidden, the Helpbar copy to +0x38.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupControllerConfigMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupControllerConfigMenu_entry(void);

// 0x00327fd0 (menu in ESI, panel in EAX): panel to +0x34 (Panel_SetLayer 2), three items with their texts, the size
// selection text to +0x3c, the title, the arrows to +0x44 / +0x48 (+0x4c the left one's +0xac), the Helpbar copy to
// +0x38, mp_menu / mp_back hidden, item 0 +0x20 (1).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetupScreenMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp);
extern "C" void FE_SetupScreenMenu_entry(void);

}  // namespace game
