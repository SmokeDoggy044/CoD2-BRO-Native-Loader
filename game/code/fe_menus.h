#pragma once

// The menus shown around levels: the pause menu, the mission success / fail menu, the in-game Live options menu and
// the chapter loading screens (LoadingMenu: chapters 9-13, LoadAfricaMenu: 1-5, LoadSicilyMenu: 6-8,
// LoadMaubeugeMenu). UIMenu subclasses (ui_menu.h); methods called through their vtables (thiscall).
//
// Shared fields after the UIMenu base: +0x34 the panel (Panel_FreeElements / Panel_Draw; +0x00 and +0x0c aligned
// blocks), +0x38 / +0x3c owned UI objects (pause, mission menus; vtable +0x00 deleting dtor, +0x3c draw).
// Loading screens: +0x38 cleared, +0x3c first mission index, +0x40 mission count, +0x48 the mission texts (42 /
// 48 / 19), then the chapter texts (5 at +0xf4 / 5 at +0x10c / 3 at +0x98) and the title text (+0x10c / +0x124 /
// +0xa8). Functions that set texts run their title calls at the original's ESP (gap entries, the entry ESP
// as the last argument): the setters leave a stack buffer's address in a heap record. UI object vtable slots:
// +0x3c draw, +0x44 draw (text), +0x6c set label, +0x78 set text, +0x7c set alpha.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00012b80 / 0x00012ca0 / 0x00012d20 / 0x00012da0 / 0x00012e20 (menu in EAX): the menu's vtable, the panel +0x34
// freed (Panel_FreeElements, its blocks +0x0c and +0x00 with Mem_AlignedFree, Z_Free), then the UIMenu part
// (UIMenu_FreeItems). 0x00013330 / 0x00223e10 also delete the owned objects +0x38 and +0x3c / +0x38.
void XB_REGS1 XB_CDECL PauseMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadingMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadAfricaMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadSicilyMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadMaubeugeMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL MissionSuccessFailMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL InGameLiveOptionsMenu_Dtor(uint8_t* menu);
// 0x00012be0 / 0x00012d00 / 0x00012d80 / 0x00012e00 / 0x00012e80 / 0x000133b0 / 0x0002bd30: the destructor, then
// Z_Free with flags bit 0. Return the menu.
uint8_t* XB_THISCALL PauseMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL LoadingMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL LoadAfricaMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL LoadSicilyMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL LoadMaubeugeMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL MissionSuccessFailMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL InGameLiveOptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);

// 0x00223e80 / 0x00330820 / 0x00330e80 (owner in EAX, menu in ECX): UIMenu_Init (3 / 3 / 0 items at (320, 240),
// 8, no flags), the subclass vtable and fields; the mission menu's full-screen black quad +0x60 (z 300). Return the
// menu.
uint8_t* XB_REGS3 XB_CDECL InGameLiveOptionsMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL MissionSuccessFailMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL LoadMaubeugeMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);

// 0x0031f440: the owned objects +0x38 / +0x3c drawn, the panel, then UIMenu_Draw.
void XB_THISCALL PauseMenu_Draw(uint8_t* menu);
// 0x002226e0: the panel, +0x38 drawn; unless 0x0095f916 the background movie (0x0095f454: vtable +0x08, the
// subtitles, its frame texture into the quad 0x0095f458 +0x60, nglListAddQuad); then UIMenu_Draw.
void XB_THISCALL InGameLiveOptionsMenu_Draw(uint8_t* menu);
// 0x00313c80 / 0x00313cb0 (ret 4, the pad unused): next / previous item (+0xb4 / +0xb0), wrapping 4 -> 0 / 0 -> 4
// themselves (select (+0x88 (index, 1)) and the move sound +0x10).
void XB_THISCALL PauseMenu_OnDown(uint8_t* menu, uint32_t pad);
void XB_THISCALL PauseMenu_OnUp(uint8_t* menu, uint32_t pad);
// 0x00336490 / 0x00336580: a dialog MEM_RESTART_WARNING / MEM_QUIT_WARNING with MEM_DIALOG_YES (0x0032dc70 /
// 0x0032dd00) and MEM_DIALOG_NO (0x0030e6a0), default NO, buttons laid out (AL = 1).
extern "C" PLATFORM_HIDDEN void XB_CDECL PauseMenu_ShowRestartDialog(uint32_t entry_esp);
extern "C" void PauseMenu_ShowRestartDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL PauseMenu_ShowQuitDialog(uint32_t entry_esp);
extern "C" void PauseMenu_ShowQuitDialog_entry(void);
// 0x00224060 / 0x00224160: as the pause dialogs with MEM_CONFIRM_FRIEND_JOIN (YES 0x0002bd10, NO 0x00224220) /
// MEM_BOOT_DASH (YES 0x002226c0, NO 0x00223a20) and INGAME_DIALOG_YES / _NO; the dialog manager's +0x1c +0x45
// set; the dashboard one clears g_liveMgr +0x44cb after.
extern "C" PLATFORM_HIDDEN void XB_CDECL InGameLiveOptionsMenu_ShowConfirmDialog(uint32_t entry_esp);
extern "C" void InGameLiveOptionsMenu_ShowConfirmDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL InGameLiveOptionsMenu_ShowDashboardDialog(uint32_t entry_esp);
extern "C" void InGameLiveOptionsMenu_ShowDashboardDialog_entry(void);

// 0x0030cfd0 (menu in ESI; text on the stack, ret 4 through the tail call): INGAME_MISSION_FAILED into the label
// +0x40, the text into +0x44 (vtable +0x6c).
extern "C" PLATFORM_HIDDEN void XB_CDECL MissionSuccessFailMenu_SetFailedText(uint8_t* menu, uint32_t text);
extern "C" void MissionSuccessFailMenu_SetFailedText_entry(void);
// 0x0032d340: the retry button: 0x0095ee08 = 1, g_reloading 1, "reboot". Returns 0 (AL).
uint8_t XB_CDECL MissionSuccessFailMenu_OnRetryYes();
// 0x00334070 (stdcall, ret 4): with a checkpoint (0x008dd1cf == 1) or the state +0x58 set: MEM_QUIT_WARNING, YES
// OnRetryYes, NO 0x0030cf90; else MEM_SAVE_QUIT, YES MissionSuccessFailMenu_ReloadCheckpoint, NO OnRetryYes. Default NO.
extern "C" PLATFORM_HIDDEN void XB_CDECL MissionSuccessFailMenu_ShowRetryDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void MissionSuccessFailMenu_ShowRetryDialog_entry(void);
// 0x003341e0 (ret 4): state +0x58 2 (reloading): once +0x5c is set unpause (cl_paused 0, the fade, cg_norender 0,
// sounds resumed, key catcher bit 1 cleared). With a pending choice (+0x5d) item 0 reloads the checkpoint, 1
// retries. Else item 0: state 0 ends (reboot with a checkpoint, else the screen manager's screen 7) or restarts
// (FEMENU_RELOADING, map_restart_check / _internal); item 1: MEM_RESTART_WARNING (with a state and 0x0095f4e0
// +0x390) or the retry dialog; item 2: the retry dialog.
extern "C" PLATFORM_HIDDEN void XB_CDECL MissionSuccessFailMenu_HandleSelect(uint8_t* menu, int32_t index, uint32_t entry_esp);
extern "C" void MissionSuccessFailMenu_HandleSelect_entry(void);

// 0x0030df00 / 0x0030de90 / 0x0030de20 (menu in EBX): every mission and chapter text's alpha 0 (+0x7c), the title
// text "" (+0x78).
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadingMenu_ClearElements(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadingMenu_ClearElements_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadAfricaMenu_ClearElements(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadAfricaMenu_ClearElements_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadSicilyMenu_ClearElements(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadSicilyMenu_ClearElements_entry(void);
// 0x0031e2a0 / 0x0031e190 / 0x0031e0a0 (menu in EAX): the chapter's (g_currentChapter) first mission +0x3c and the
// count +0x40; from first 1 on the missions up to it shown (0x00313610 / 0x00313360 / 0x00313120: EAX = first - 1,
// EDI = menu, BL = 1). An unknown chapter keeps +0x3c and counts from last 0.
void XB_REGS1 XB_CDECL LoadingMenu_SetMissionRange(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadAfricaMenu_SetMissionRange(uint8_t* menu);
void XB_REGS1 XB_CDECL LoadSicilyMenu_SetMissionRange(uint8_t* menu);
// 0x0031e380 / 0x0031e270 / 0x0031e160: UIMenu_RestoreSelection, +0x38 / +0x40 / +0x3c cleared, ClearElements, the
// title's label FEMENU_LOADING, then SetMissionRange (tail call). 0x0031e050: the same without the mission list
// (+0x38, +0x3c cleared, label +0x58, alphas +0x44 1.0, +0x48 / +0x4c 0).
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadingMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadingMenu_RestoreSelection_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadAfricaMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadAfricaMenu_RestoreSelection_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadSicilyMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadSicilyMenu_RestoreSelection_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL LoadMaubeugeMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp);
extern "C" void LoadMaubeugeMenu_RestoreSelection_entry(void);
// 0x0031e320 / 0x0031e210 / 0x0031e100: one text's alpha 0 (+0xf0 / +0x108 / +0x94), the panel, UIMenu_Draw, the
// title (+0x3c), then the second mission text (+0x4c) drawn (+0x44) once per mission text and the second chapter
// text once per chapter (the title's loops index one element). 0x0031e000: +0x50 alpha 0, the panel, UIMenu_Draw,
// +0x58 drawn, then +0x44..+0x4c (+0x44 each).
void XB_THISCALL LoadingMenu_Draw(uint8_t* menu);
void XB_THISCALL LoadAfricaMenu_Draw(uint8_t* menu);
void XB_THISCALL LoadSicilyMenu_Draw(uint8_t* menu);
void XB_THISCALL LoadMaubeugeMenu_Draw(uint8_t* menu);

}  // namespace game
