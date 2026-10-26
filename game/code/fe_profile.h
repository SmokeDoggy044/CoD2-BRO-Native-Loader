#pragma once

// The profile and save-slot menus: the front end's profile menu (ProfileMenu, vtable 0x00828ed0: load / save /
// switch / delete a profile on the memory unit), the in-game autosave menu (AutosaveMenu, 0x0082a130: picks the
// slot a save goes to) and the create-profile menu (CreateProfileMenu, 0x0082a068: a 40-key on-screen keyboard
// grid for the profile name). UIMenu subclasses (ui_menu.h); methods called through their vtables (thiscall).
//
// Save manager (g_saves 0x0095f480, see ui_menu.h): +0x04 the request (Save_BeginRead), +0x28c four 0x3cc-byte
// slot records (+0x1c the profile: play time seconds / minutes / hours / days at +0x28..+0x34 of it, +0x100 a
// word compared against 0x008dcf14), +0x11e7 four slot-used flags, +0x11ec the slot to write.
// AutosaveMenu: +0x34 a failure dialog is up, +0x35 corrupt, +0x36 / +0x37 overwrite (slot data changed / used),
// +0x38 no save data, +0x39 delete pending, +0x3c the panel, +0x40 an owned object, +0x44 storage state (0 / 1
// none, 2 checking), +0x48 mode (1 = continue without saving), +0x4c the slot, +0x50 overwriting, +0x51 select
// after the read, +0x52 cleared on a check, +0x54 four slot rows (12 bytes: profile, play-time text, empty flag
// +0x08), +0x84 / +0x85 "no card" / "check storage" requests (Update).
// ProfileMenu: +0x34 delete pending, +0x35 an error dialog is up, +0x36 corrupt, +0x37 unformatted, +0x38 a
// result dialog is up, +0x39 cleared before a load, +0x3c the panel, +0x40 the help bar text, +0x44 the item
// selected on entry, +0x48 storage state (2 = checking), +0x4c mode (0 load, 1 / 2 save, 3 delete), +0x50 the
// slot, +0x54 overwriting, +0x5c / +0x60 / +0x61 flags, +0x64 four slot rows (as above, empty flag +0x6c),
// +0x95 / +0x96 "save error" / "check storage" requests, +0x97 profiles read once, +0x98 the read's time.
// CreateProfileMenu (a grid menu of 40 items: 0-9 digits, 10-35 letters, 36 backspace, 37 caps, 38 space, 39
// done): +0x34 return to the previous screen, +0x38 the panel ("profileName" text), +0x3c an owned object,
// +0x184..+0x190 four texts drawn last, +0x194 cleared, +0x198 the name (15 characters, '_' cursor), +0x1a8 capitals,
// +0x1ac the name length.
// Dialog functions run their title calls at the original's ESP (gap entries, the entry ESP as the last argument):
// the UI text setters leave a stack buffer's address in a heap record (fe_menus.h).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00013290 / 0x000133d0 / 0x00330980 (menu in EAX): the vtable, the panel (+0x3c / +0x3c / +0x38) freed
// (Panel_FreeElements, its blocks +0x0c and +0x00, Z_Free), the owned object (+0x40 / +0x40 / +0x3c) deleted
// (vtable +0x00 (1)), then the UIMenu part (UIMenu_FreeItems).
void XB_REGS1 XB_CDECL ProfileMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL AutosaveMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL CreateProfileMenu_Dtor(uint8_t* menu);
// 0x00013310 / 0x00013440 / 0x00013820: the destructor, then Z_Free with flags bit 0. Return the menu.
uint8_t* XB_THISCALL ProfileMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL AutosaveMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL CreateProfileMenu_DeletingDtor(uint8_t* menu, uint32_t flags);

// 0x00330730 / 0x003308f0 (owner in EAX, menu in ECX): UIMenu_Init (4 items at (320, 240), 8, no flags), the
// vtable and fields, the four slot rows empty. 0x00335570: UIGridMenu_Init (40 items), repeat (flag 7), the name
// cleared. Return the menu.
uint8_t* XB_REGS3 XB_CDECL AutosaveMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL ProfileMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL CreateProfileMenu_Construct(uint8_t* owner, uint32_t edx, uint8_t* menu);

// --- Create profile ---

// 0x0031b310 / 0x0031b3d0 / 0x0031b4b0 / 0x0031b580: the keyboard grid's moves - the neighbours of the edge keys
// (19, 27, 35-39) patched around hidden keys before UIGridMenu_Move*, the move sound (+0x10) when the selection
// changed, then the neighbours set back.
void XB_THISCALL CreateProfileMenu_MoveRight(uint8_t* menu);
void XB_THISCALL CreateProfileMenu_MoveLeft(uint8_t* menu);
void XB_THISCALL CreateProfileMenu_MoveDown(uint8_t* menu);
void XB_THISCALL CreateProfileMenu_MoveUp(uint8_t* menu);
// 0x0031b5f0: the panel and UIMenu_Draw (with a panel only), the owned object (+0x3c), the background movie
// (as InGameLiveOptionsMenu_Draw), then the texts +0x184..+0x190 (+0x44).
void XB_THISCALL CreateProfileMenu_Draw(uint8_t* menu);
// 0x0030d4c0 (menu in ESI): capitals +0x1a8 toggled; the letter keys 10-35 get "a".."z" / "A".."Z" (+0x30, a
// two-byte stack string).
extern "C" PLATFORM_HIDDEN void XB_CDECL CreateProfileMenu_ToggleCase(uint8_t* menu, uint32_t entry_esp);
extern "C" void CreateProfileMenu_ToggleCase_entry(void);
// 0x003258c0 (menu in ESI): one character off the name ('_' cursor), the "profileName" text (+0x6c); below 15
// characters every key shown (+0x20 (0)); an empty name hides backspace, space and done (+0x20 (1)).
extern "C" PLATFORM_HIDDEN void XB_CDECL CreateProfileMenu_Backspace(uint8_t* menu, uint32_t entry_esp);
extern "C" void CreateProfileMenu_Backspace_entry(void);
// 0x00325980 (key in EAX, menu in ESI): backspace, space and done shown; below 15 characters the key's character
// (0-9 digits, 10-35 letters by +0x1a8, 38 space, else 'a') appended with the '_' cursor, the "profileName" text
// (+0x6c); at 15 every key but done hidden and done selected (+0x88 (39, 1)).
extern "C" PLATFORM_HIDDEN void XB_CDECL CreateProfileMenu_AddChar(int32_t key, uint8_t* menu, uint32_t entry_esp);
extern "C" void CreateProfileMenu_AddChar_entry(void);
// 0x00338b70 (menu in ESI): trailing spaces cut (the first character kept), the name into the profile name
// 0x008dce30 (16 bytes), the "Propgnda" movie (full screen, 0x0095f91a); then with a checkpoint (0x008dd1cf == 1)
// or +0x34 the front end's +0x0c (1), else screen 15's +0x4c = 3 and the owner's +0x0c (15).
extern "C" PLATFORM_HIDDEN void XB_CDECL CreateProfileMenu_Finish(uint8_t* menu, uint32_t entry_esp);
extern "C" void CreateProfileMenu_Finish_entry(void);
// 0x00338c40 (ret 4): UI_PlayTypeSound, then keys 0-35 and 38 AddChar, 36 Backspace, 37 ToggleCase, 39 Finish.
extern "C" PLATFORM_HIDDEN void XB_CDECL CreateProfileMenu_HandleSelect(uint8_t* menu, int32_t index,
                                                                       uint32_t entry_esp);
extern "C" void CreateProfileMenu_HandleSelect_entry(void);

// --- Autosave ---

// 0x003334b0 / 0x00334b70 (menu in ESI): the session time 0x0095fd14 = now (0x008ddaa8) - it, plus an overwritten
// slot's play time, split into fe_playTime* (FE_SplitPlayTime2 / FE_SplitPlayTime); the slot written (SaveSlot_Init,
// the slot +0x4c / the selection +0x22); MEM_XBOX_OVERWRITE_WARNING (+0x50 / +0x54) or MEM_XBOX_SAVE_WARNING with no
// buttons. Return Dialog_LayoutButtons' AL.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL AutosaveMenu_ShowSaveWarning(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowSaveWarning_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ProfileMenu_ShowSaveWarning(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_ShowSaveWarning_entry(void);
// 0x00333640 (menu in EAX): unless +0x34, MEM_ERROR_INSERT_CARD with MEM_CONTINUE_WO_SAVING (0x003125d0).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowNoSaveDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowNoSaveDialog_entry(void);
// 0x003337c0 / 0x00334f40 (menu in EAX): the corrupt flag (+0x35 / +0x36); in storage state 0 or 2 (+0x44 /
// +0x48) XBOX_CORRUPT_DETECT / MEM_XBOX_CORRUPT_DETECT with MEM_DIALOG_OK (0x003336d0 / 0x00334e50).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowCorruptDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowCorruptDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowCorruptDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_ShowCorruptDialog_entry(void);
// 0x00333860 / 0x0033a130 (error in EAX, operation in ECX: 1 load, 2 delete, 4 format, else save; menu in ESI):
// the requests cleared, 0x008d50dd = 1; error -12 (also -1 for the profile menu) or -11 while checking: the
// corrupt dialog (tail call); outside a check (and not formatting) 0x0030cf70 / 0x00312850, state 1 and the
// dialog closed (tail call); else the load / delete / format / save (overwrite) failed dialog with its buttons.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_OnSaveOpFailed(int32_t error, int32_t operation, uint8_t* menu,
                                                                    uint32_t entry_esp);
extern "C" void AutosaveMenu_OnSaveOpFailed_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_OnSaveOpFailed(int32_t error, int32_t operation, uint8_t* menu,
                                                                   uint32_t entry_esp);
extern "C" void ProfileMenu_OnSaveOpFailed_entry(void);
// 0x003339f0 (menu in EAX): without +0x36 0x0095ee08 = (the in-game screens' screen 7 +0x48 == 4) and "reboot"
// (tail call); else MEM_OVERWRITE_SUCCESS (+0x50) / MEM_SAVE_SUCCESS with MEM_DIALOG_OK (0x00312580).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_OnSaveSucceeded(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_OnSaveSucceeded_entry(void);
// 0x00333bb0 / 0x00335180: MEM_XBOX_CHECK_WARNING (no buttons), 0x008d50dd = 0, ProfileSave_PrepareRequest and
// Save_BeginRead of the save manager's request.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_StartProfileLoad(uint32_t entry_esp);
extern "C" void AutosaveMenu_StartProfileLoad_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_StartProfileLoad(uint32_t entry_esp);
extern "C" void ProfileMenu_StartProfileLoad_entry(void);
// 0x00333c10 / 0x003351e0: MEM_ERROR_UNFORMATTED with MEM_DIALOG_YES (0x0030cee0 / 0x0030d2e0) and MEM_DIALOG_NO
// (0x003125d0 / 0x0031b250), NO selected.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowUnformattedDialog(uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowUnformattedDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowUnformattedDialog(uint32_t entry_esp);
extern "C" void ProfileMenu_ShowUnformattedDialog_entry(void);
// 0x00333d00: MEM_XBOX_ERROR_NO_SAVE_DATA with MEM_DIALOG_OK (0x003125d0).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowNoSaveDataDialog(uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowNoSaveDataDialog_entry(void);
// 0x00333ee0 / 0x00338960 (menu in ESI): ProfileSave_PrepareRequest, port 8, Save_GetFreeSpace into a stack block;
// an unformatted unit (-2, or 0 with block +0x18 = 0) by mode: continue without saving (0x0030cf70 / 0x00312850,
// state 1), the no-save-data / 0x003352d0 path or the unformatted dialog; else (the profile menu: with room,
// MemDlg_CheckFreeSpace2 unless 0x008dd1d0) an existing profiles folder starts the load, a delete in a check goes
// to FE_ProfileSelect, the other modes as above.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_CheckStorage(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_CheckStorage_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_CheckStorage(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_CheckStorage_entry(void);
// 0x00337f00 (slot in EAX, menu in ECX): with room (MemDlg_CheckFreeSpace3) the slot into g_saves +0x11ec and
// +0x4c; a used slot asks MEM_XBOX_OVERWRITE_CONFIRM (YES 0x003335a0, NO 0x003125d0, NO selected), else the save
// warning. Returns the last callee's AL (0 without room).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL AutosaveMenu_SelectSlot(int32_t slot, uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_SelectSlot_entry(void);
// 0x00338040 / 0x0033a640 (ret 4): Save_Update (the profile menu: SCR_LoadingPump first, then the help bar text by
// mode / the selected row's empty flag); the pending requests (the profile menu: the delayed profiles read 0.5 s
// after +0x98); "check storage": vtable +0x40 or the storage check; then UIMenu_Update unless 0x0095f91e.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_Update(uint8_t* menu, uint32_t dt, uint32_t entry_esp);
extern "C" void AutosaveMenu_Update_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_Update(uint8_t* menu, uint32_t dt, uint32_t entry_esp);
extern "C" void ProfileMenu_Update_entry(void);
// 0x00338100: MEM_FORMAT_SUCCESS with MEM_DIALOG_OK (0x003380d0). 0x003388f0: MEM_FORMAT_SUCCESS without buttons,
// the dialog's +0x54 = 0x0083eba8's float and +0x48 = 0x003388c0.
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowFormatSuccessDialog(uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowFormatSuccessDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowFormatSuccessDialog(uint32_t entry_esp);
extern "C" void ProfileMenu_ShowFormatSuccessDialog_entry(void);
// 0x003381c0 (menu in EBX): with +0x51 the slot is selected (tail call); else per slot the profile and its play
// time text (FE_FormatPlayTime) or empty, the overwrite flag +0x36 from the active profile's word, 0x0030ccb0, the
// dialog closed; the in-game screens' screen 7 +0x44 = 1, or with an active profile (0x008dcf10) 2 and its slot
// selected (tail call).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_OnProfilesRead(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_OnProfilesRead_entry(void);
// 0x0033a2a0 (stdcall): the first call only stamps the time (+0x97, +0x98); then per slot the profile and its play
// time text (FE_FormatPlayTime2) or empty, loading the selected one (Profile_Copy into 0x008dce30,
// CL_ApplyProfileSettings, Profile_ApplySkill); all empty in mode 2: 0x003352d0; else the slot texts and
// MEM_LOAD_SUCCESS (0x0030d1e0) or the dialog closed. Delete mode / +0x60: FE_ProfileSelect.
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_OnProfilesRead(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_OnProfilesRead_entry(void);
// 0x00334ca0 (menu in EAX): unless +0x35, MEM_ERROR_INSERT_CARD with MEM_DIALOG_OK (0x00325870), MEM_RETURN_TO_MAIN
// (0x0030d2f0) or MEM_CONTINUE_WO_SAVING (0x0031b250) by storage state and mode.
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowSaveErrorDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_ShowSaveErrorDialog_entry(void);
// 0x00334fe0 (menu in EAX): the active slot 0x008dce40 = +0x50, +0x38 set, MEM_OVERWRITE_SUCCESS (+0x54) /
// MEM_SAVE_SUCCESS with MEM_DIALOG_OK (0x0030d240).
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowSaveSuccessDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void ProfileMenu_ShowSaveSuccessDialog_entry(void);
// 0x00339c70 (menu in EAX): +0x39 cleared; 0x00339fa0: the front end's screen 15 +0x39 set. MEM_DELETE_SUCCESSFUL_XBOX
// with MEM_DIALOG_OK (0x00338300 / 0x00339f20).
extern "C" PLATFORM_HIDDEN void XB_CDECL AutosaveMenu_ShowDeleteSuccessDialog(uint8_t* menu, uint32_t entry_esp);
extern "C" void AutosaveMenu_ShowDeleteSuccessDialog_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL ProfileMenu_ShowDeleteSuccessDialog(uint32_t entry_esp);
extern "C" void ProfileMenu_ShowDeleteSuccessDialog_entry(void);

}  // namespace game
