#include "fe_setup.h"

#include "fe_setup_internal.h"

namespace game {

using namespace setup;

SETUP_ENTRY(FE_SetupControllerDisconnected, "%edi")

void XB_CDECL FE_SetupControllerDisconnected(uint8_t* menu, uint8_t* panel) {
    field<uint8_t*>(menu, 0x34) = panel;
    uint8_t* const source = Panel_FindText(text_at(0x0082c9a0), panel);  // "Resume"
    field<uint8_t*>(menu, 0x38) = copy_text(source, 0);
    call(field<uint8_t*>(menu, 0x38), 0x13c, 8);
    call(field<uint8_t*>(menu, 0x38), 0x90, 0xffffffff);  // colour: four 0xff bytes
    call(field<uint8_t*>(menu, 0x38), 0xe4, 0x14);
}

SETUP_ENTRY(FE_SetupLoadMaubeugeMenu, "%esi")

void XB_CDECL FE_SetupLoadMaubeugeMenu(uint8_t* menu, uint8_t* panel) {
    field<uint8_t*>(menu, 0x34) = panel;
    field<uint8_t*>(menu, 0x44) = Panel_FindPicture(panel, text_at(0x0082bc14));  // "maubeuge3"
    field<uint8_t*>(menu, 0x48) = find_picture(menu, 0x0082bc08);                 // "maubeuge2"
    field<uint8_t*>(menu, 0x4c) = find_picture(menu, 0x0082bbfc);                 // "maubeuge1"
    field<uint8_t*>(menu, 0x50) = find_picture(menu, kBack);
    field<uint8_t*>(menu, 0x54) = Panel_FindText(text_at(kLoadingMenu), field<uint8_t*>(menu, 0x34));
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
    field<uint8_t*>(menu, 0x58) = copy_text(source, 0x40);
    call(field<uint8_t*>(menu, 0x58), 0x13c, 1);
    call(field<uint8_t*>(menu, 0x58), 0x60, 0x3fa66666);  // 1.3
}

SETUP_GAP_ENTRY(FE_SetupMemcardMenu, "%esi")

void XB_CDECL FE_SetupMemcardMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    field<uint8_t*>(menu, 0x30) = panel;
    static constexpr uint32_t kNames[5] = {0x0082ca54, 0x0082ca44, 0x0082ca38, 0x0082ca2c, 0x0082ca24};
    field<uint8_t*>(menu, 0x10) = Panel_FindPicture(panel, text_at(kNames[0]));
    for (uint32_t i = 1; i < 5; ++i)
        field<uint8_t*>(menu, 0x10 + i * 4) = Panel_FindPicture(field<uint8_t*>(menu, 0x30), text_at(kNames[i]));
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x30));
    uint8_t* const old = field<uint8_t*>(menu, 0x24);
    if (old) call(old, 0, 1);  // deleting destructor
    uint8_t* const copy = copy_text(source, 0);
    field<uint8_t*>(menu, 0x24) = copy;
    call(field<uint8_t*>(menu, 0x24), 0x13c, 1);
    TitleRegs regs{};
    regs.ebx = address_of(copy);
    regs.esi = address_of(menu);
    regs.edi = address_of(source);
    const uint32_t args[1] = {0x0082ca0c};
    method_at(entry_esp - 12, regs, field<uint8_t*>(menu, 0x24), 0x6c, 1, args);  // push ecx / ebx / edi
}

SETUP_GAP_ENTRY(FE_SetupInGameLiveOptions, "%esi")

void XB_CDECL FE_SetupInGameLiveOptions(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    setup_live(menu, panel, entry_esp);
}

SETUP_GAP_ENTRY(FE_SetupXboxLiveSingle, "%esi")

void XB_CDECL FE_SetupXboxLiveSingle(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    setup_live(menu, panel, entry_esp);
}

SETUP_ENTRY(FE_SetupLoadSicilyMenu, "%esi")

void XB_CDECL FE_SetupLoadSicilyMenu(uint8_t* menu, uint8_t* panel) {
    static constexpr uint32_t kNames[3] = {0x0082bc34, 0x0082bc28, 0x0082bc20};
    setup_loading(menu, panel, 1, 0x13, kNames, 3, 0x98, 0x94, 0xa4);
}

SETUP_ENTRY(FE_SetupLoadAfricaMenu, "%esi")

void XB_CDECL FE_SetupLoadAfricaMenu(uint8_t* menu, uint8_t* panel) {
    static constexpr uint32_t kNames[5] = {0x0082bc60, 0x0082bc58, 0x0082bc48, 0x0082bc3c, 0x0082cc0c};
    setup_loading(menu, panel, 0x17, 0x30, kNames, 5, 0x10c, 0x108, 0x120);
}

SETUP_ENTRY(FE_SetupLoadingMenu, "%esi")

void XB_CDECL FE_SetupLoadingMenu(uint8_t* menu, uint8_t* panel) {
    static constexpr uint32_t kNames[5] = {0x0082bcc0, 0x0082bcb8, 0x0082bcac, 0x0082bca0, 0x0082bc90};
    setup_loading(menu, panel, 1, 0x2a, kNames, 5, 0xf4, 0xf0, 0x108);
}

SETUP_GAP_ENTRY(FE_SetupAutosaveMenu, "%esi")

void XB_CDECL FE_SetupAutosaveMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    static constexpr uint32_t kTexts[4] = {0x0082aeb4, 0x0082aea8, 0x0082ae9c, 0x0082ae90};
    setup_list(menu, panel, entry_esp, 0x3c, true, kTexts, 4, nullptr, 0, 0x40);
    // The item links: +0xa = the item below, +8 = the item above.
    field<int16_t>(item(menu, 0), 0xa) = 1;
    field<int16_t>(item(menu, 1), 0xa) = 2;
    field<int16_t>(item(menu, 2), 0xa) = 3;
    field<int16_t>(item(menu, 3), 8) = 2;
    field<int16_t>(item(menu, 2), 8) = 1;
    field<int16_t>(item(menu, 1), 8) = 0;
}

SETUP_GAP_ENTRY(FE_SetupSinglePlayerMenu, "%esi")

void XB_CDECL FE_SetupSinglePlayerMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    static constexpr uint32_t kTexts[5] = {0x0082c420, 0x0082c418, 0x0082c408, 0x0082c3fc, 0x0082c3ec};
    static constexpr uint32_t kItemTexts[5] = {0x0082c3dc, 0x0082c3cc, 0x0082c3bc, 0x0082c3a8, 0x0082c390};
    setup_list(menu, panel, entry_esp, 0x38, true, kTexts, 5, kItemTexts, 5, 0x34);
}

SETUP_GAP_ENTRY(FE_SetupDifficultyMenu, "%esi")

void XB_CDECL FE_SetupDifficultyMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x18;  // 0x10 locals, push ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    // Hide difficulty0..2.
    for (int32_t i = 0; i < 3; ++i) {
        char name[16];
        title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901)(name, text_at(0x0082b24c), i);
        hide_picture(field<uint8_t*>(menu, 0x34), name);
    }
    static constexpr uint32_t kNames[3] = {0x0082b244, 0x0082b23c, 0x0082b234};  // easy / medium / hard
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 3);
    field<uint8_t*>(menu, 0x3c) = find_picture(menu, 0x0082b228);  // "arrow_lft"
    field<uint8_t*>(menu, 0x40) = find_picture(menu, 0x0082b21c);  // "arrow_rht"
    setup_help(top, regs, menu, 0x34, 0x38);
    // The item links: +0xe = the item to the right, +0xc = the item to the left.
    field<int16_t>(item(menu, 0), 0xe) = 1;
    field<int16_t>(item(menu, 1), 0xe) = 2;
    field<int16_t>(item(menu, 1), 0xc) = 0;
    field<int16_t>(item(menu, 2), 0xc) = 1;
}

SETUP_GAP_ENTRY(FE_SetupCreateProfileMenu, "%esi")

void XB_CDECL FE_SetupCreateProfileMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x24;  // 0x18 locals, push edi / ebx / ebp
    field<uint8_t*>(menu, 0x38) = panel;
    if (!panel) return;
    Panel_SetLayer(panel, 2);
    // The 40 keyboard keys: text%i to +0xe4, shadow%i (picture) to +0x44.
    for (int32_t i = 1; i <= 40; ++i) {
        char name[16];
        auto sprintf = title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901);
        sprintf(name, text_at(0x0082b1a8), i);
        field<uint8_t*>(menu, 0xe4 + uint32_t(i - 1) * 4) = Panel_FindText(name, field<uint8_t*>(menu, 0x38));
        sprintf(name, text_at(0x0082b19c), i);
        field<uint8_t*>(menu, 0x44 + uint32_t(i - 1) * 4) = find_picture_at(menu, 0x38, name);
    }
    static constexpr uint32_t kKeys[4] = {0x0082b194, 0x0082b188, 0x0082b180, 0x0082b178};  // shift/backspace/space/done
    for (uint32_t i = 0; i < 4; ++i)
        field<uint8_t*>(menu, 0x184 + i * 4) = find_picture_at(menu, 0x38, text_at(kKeys[i]));
    TitleRegs regs{};
    regs.esi = address_of(menu);
    setup_help(top, regs, menu, 0x38, 0x3c);
    // The title text (EBX = its name, EBP 41 in the original).
    uint8_t* const title = Panel_FindText(text_at(0x0082b168), field<uint8_t*>(menu, 0x38));  // "CreateProfile"
    regs.ebx = 0x0082b168;
    const uint32_t title_args[1] = {0x0082b14c};  // "FEMENU_TITLE_CREATE_PROFILE"
    method_at(top, regs, title, 0x6c, 1, title_args);
    uint8_t* const again = Panel_FindText(text_at(0x0082b168), field<uint8_t*>(menu, 0x38));
    call(again, 0x90, 0xff282119);  // colour bytes 19 21 28 ff
    uint8_t* const name = Panel_FindText(text_at(0x0082b1bc), field<uint8_t*>(menu, 0x38));  // "profileName"
    regs.ebx = 0x0082b1bc;
    const uint32_t name_args[1] = {0x0082b1b8};
    method_at(top, regs, name, 0x6c, 1, name_args);
}

// FE_SetupLoginStatus: panel in EAX, menu on the stack (ret 4); its argument slot is reused as the colour local and
// finally holds the tail call's text argument.
PLATFORM_ASM_ENTRY(FE_SetupLoginStatus_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                                              "pushl %edx\n pushl %eax\n pushl 4(%edx)\n" PLATFORM_ASM_CALL(FE_SetupLoginStatus)
                                              "addl $12, %esp\n"
                                              "popl %esp\n ret $4")

uint32_t XB_CDECL FE_SetupLoginStatus(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    title_global<uint8_t*>(0x0095f47c) = panel;
    if (!panel) return address_of(panel);
    uint32_t& slot = *reinterpret_cast<uint32_t*>(uintptr_t{entry_esp + 4});
    const uint32_t top = entry_esp - 16;  // push ebp / ebx / esi / edi
    uint8_t* const login = Panel_FindText(text_at(0x0082cac0), panel);  // "login_text"
    title_global<uint8_t*>(0x0095f478) = login;
    TitleRegs regs{};
    regs.ebx = 0x0082cac0;
    const uint32_t login_args[1] = {0x0082cb98};  // "FEMENU_NOT_SIGNED_IN"
    method_at(top, regs, login, 0x6c, 1, login_args);
    slot = 0xff000000;
    call(title_global<uint8_t*>(0x0095f478), 0x90, slot);
    uint8_t* const source = Panel_FindText(text_at(0x0082cab8), title_global<uint8_t*>(0x0095f47c));  // "ybutton"
    slot = 0xffffffff;
    uint8_t* const copy = copy_text_colour(source, slot, 0, 0x20);
    field<uint8_t*>(menu, 0x1c) = copy;
    call(copy, 0x13c, 1);
    // The tail call (jmp): ESP back at the entry, the text in the argument slot, returning to our caller's address.
    slot = 0x0082caa4;  // "FEMENU_XBOX_LIVE"
    regs.ebx = 0x0082cab8;
    regs.esi = address_of(source);
    regs.edi = address_of(copy);
    const uint32_t args[1] = {slot};
    uint8_t* const text = field<uint8_t*>(menu, 0x1c);
    regs.ecx = address_of(text);
    uint32_t& ret = *reinterpret_cast<uint32_t*>(uintptr_t{entry_esp});
    const uint32_t caller = ret;
    const uint32_t result =
        title_call_at_regs(entry_esp + 8, field<uint32_t>(field<uint8_t*>(text, 0), 0x6c), &regs, 1, args);
    ret = caller;
    return result;
}

SETUP_GAP_ENTRY(FE_SetupSoundMenu, "%ecx")

void XB_CDECL FE_SetupSoundMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 16;  // push ecx / ebx / esi / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    static constexpr uint32_t kNames[3] = {0x0082bd28, 0x0082c2dc, 0x0082c2d0};  // MusicVolume / ResetToDefault / SaveAndExit
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 3);
    regs.ebx = kNames[2];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[3] = {0x0082c720, 0x0082c2b0, 0x0082c294};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    uint8_t* const title = Panel_FindText(text_at(0x0082bd1c), field<uint8_t*>(menu, 0x34));  // "SoundMenu"
    regs.ebx = 0x0082bd1c;
    const uint32_t title_args[1] = {0x0082c324};
    method_at(top, regs, title, 0x6c, 1, title_args);
    uint8_t* const again = Panel_FindText(text_at(0x0082bd1c), field<uint8_t*>(menu, 0x34));
    call(again, 0x90, 0xff282119);  // colour bytes 19 21 28 ff
    uint8_t* const left = find_picture(menu, 0x0082bd64);  // "arrowleft01"
    call(left, 0x7c, 0);
    uint8_t* const right = find_picture(menu, 0x0082bd54);  // "arrowright01"
    call(right, 0x7c, 0);
    field<uint8_t*>(menu, 0x3c) = find_picture(menu, 0x0082bd04);  // "MusicVolumeProgbar01"
    regs.edi = field<uint32_t>(menu, 0);
    setup_help(top, regs, menu, 0x34, 0x38);
    uint8_t* const first = find_picture(menu, 0x0082c31c);
    uint8_t* const back = find_picture(menu, 0x0082c314);  // "mp_back"
    if (first) call(first, 0x7c, 0);
    if (back) call(back, 0x7c, 0);
}

SETUP_GAP_ENTRY(FE_SetupProfileMenu, "%esi")

void XB_CDECL FE_SetupProfileMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x3c) = panel;
    Panel_SetLayer(panel, 2);
    static constexpr uint32_t kNames[4] = {0x0082aeb4, 0x0082aea8, 0x0082ae9c, 0x0082ae90};  // Profile1..4
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x3c, kNames, 4);
    regs.edi = field<uint32_t>(menu, 0);
    set_title(top, regs, menu, 0x3c, 0x0082b13c, 0x0082b128);  // "LoadProfileMenu", "FEMENU_LOAD_PROFILE"
    regs.edi = field<uint32_t>(menu, 0);
    setup_help(top, regs, menu, 0x3c, 0x40, 0x0082b10c);  // "FEMENU_LOAD_HELPBAR_XBOX"
    field<uint8_t*>(menu, 0x58) = find_picture_at(menu, 0x3c, text_at(0x0082ae88));  // "profile"
    field<int16_t>(item(menu, 0), 0xa) = 1;
    field<int16_t>(item(menu, 1), 0xa) = 2;
    field<int16_t>(item(menu, 2), 0xa) = 3;
    field<int16_t>(item(menu, 3), 8) = 2;
    field<int16_t>(item(menu, 2), 8) = 1;
    field<int16_t>(item(menu, 1), 8) = 0;
}

SETUP_GAP_ENTRY(FE_SetupMainMenu, "%esi")

void XB_CDECL FE_SetupMainMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x1c;  // 0x10 locals, push ebx / ebp / edi
    field<uint8_t*>(menu, 0x38) = panel;
    Panel_SetLayer(panel, 2);
    static constexpr uint32_t kNames[4] = {0x0082c508, 0x0082c4f4, 0x0082c4e4, 0x0082c4d4};  // ... "extrasselect"
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x38, kNames, 4);
    const bool special = title_global<uint8_t>(0x0095e7f8) != 0;  // the special edition item
    regs.ebx = kNames[3];
    regs.edi = field<uint32_t>(menu, 0);
    if (special) {
        static constexpr uint32_t kSpecial[1] = {0x0082c4c4};  // "specialedselect"
        uint8_t* const text = Panel_FindText(text_at(kSpecial[0]), field<uint8_t*>(menu, 0x38));
        regs.ebx = kSpecial[0];
        const uint32_t args[3] = {4, address_of(text), 0};
        method_at(top, regs, menu, 4, 3, args);
    } else {
        const uint32_t args[2] = {4, 0x0082c4b8};  // "DISABLED"
        method_at(top, regs, menu, 8, 2, args);
        const uint32_t hide[1] = {1};
        method_at(top, regs, item(menu, 4), 0x20, 1, hide);
    }
    static constexpr uint32_t kItemTexts[3] = {0x0082c4a0, 0x0082c490, 0x0082c96c};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    if (special) {
        const uint32_t extras[1] = {0x0082c478};  // "FEMENU_OPTION_EXTRAS"
        method_at(top, regs, item(menu, 3), 0x30, 1, extras);
        const uint32_t ce[1] = {0x0082c464};  // "FEMENU_OPTION_CE"
        method_at(top, regs, item(menu, 4), 0x30, 1, ce);
    } else {
        const uint32_t bonus[1] = {0x0082c450};  // "FEMENU_OPTION_BONUS"
        method_at(top, regs, item(menu, 3), 0x30, 1, bonus);
    }
    setup_help(top, regs, menu, 0x38, 0x34, 0x0082c438);  // "FEMENU_HELPBAR_MANAGER"
    // Hide check01..04 (check05 with the special edition item).
    const int32_t count = special ? 5 : 4;
    for (int32_t i = 0; i < count; ++i) {
        char name[16];
        title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901)(name, text_at(0x0082c42c), i + 1);
        hide_picture(field<uint8_t*>(menu, 0x38), name);
    }
}

SETUP_GAP_ENTRY(FE_SetupScreenMenu, "%esi")

void XB_CDECL FE_SetupScreenMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    static constexpr uint32_t kNames[3] = {0x0082bdcc, 0x0082bdbc, 0x0082c2d0};  // ScreenSize / ResetToDefaults / SaveAndExit
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 3);
    regs.ebx = kNames[2];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[3] = {0x0082bda4, 0x0082c2b0, 0x0082c294};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    // The size selection text: 4x3 when +0x50 is set, else 16x9; item 0's colour (+0x84).
    uint8_t* const selection = Panel_FindText(text_at(0x0082bd90), field<uint8_t*>(menu, 0x34));
    field<uint8_t*>(menu, 0x3c) = selection;
    regs.ebx = 0x0082bd90;
    const uint32_t size_args[1] = {field<uint8_t>(menu, 0x50) ? 0x0082bd44u : 0x0082bd34u};
    method_at(top, regs, selection, 0x6c, 1, size_args);
    uint32_t local = 0;
    const uint32_t colour = *method<ColourMethod>(item(menu, 0), 0x84)(item(menu, 0), &local);
    call(field<uint8_t*>(menu, 0x3c), 0x90, colour);
    set_title(top, regs, menu, 0x34, 0x0082bd84, 0x0082bd70);  // "ScreenMenu", "FEMENU_OP_SCREEN"
    field<uint8_t*>(menu, 0x44) = find_picture(menu, 0x0082bd64);  // "arrowleft01"
    field<uint8_t*>(menu, 0x48) = find_picture(menu, 0x0082bd54);  // "arrowright01"
    field<uint32_t>(menu, 0x4c) = *method<ColourMethod>(field<uint8_t*>(menu, 0x44), 0xac)(field<uint8_t*>(menu, 0x44), &local);
    setup_help(top, regs, menu, 0x34, 0x38);
    uint8_t* const mp_menu = find_picture(menu, 0x0082c31c);  // "mp_menu"
    uint8_t* const back = find_picture(menu, 0x0082c314);     // "mp_back"
    if (mp_menu) call(mp_menu, 0x7c, 0);
    if (back) call(back, 0x7c, 0);
    call(item(menu, 0), 0x20, 1);
}

SETUP_GAP_ENTRY(FE_SetupOptionsMenu, "%ecx")

void XB_CDECL FE_SetupOptionsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x14;  // 8 bytes of locals, push ebx / esi / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    static constexpr uint32_t kNames[3] = {0x0082c370, 0x0082c364, 0x0082c35c};  // Gameplay / Controller / Sound
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 3);
    regs.ebx = kNames[2];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[3] = {0x0082c348, 0x0082c334, 0x0082c324};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    // Items 0 and 1 move by half the gap between them, item 2 the other way (+0x70 y, +0x6c x, +0x2c (x, y)).
    const float second = static_cast<float>(get(item(menu, 1), 0x70));
    const double half = static_cast<double>((second - get(item(menu, 0), 0x70)) * 0.5L);
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t* const entry = item(menu, i);
        const float y = static_cast<float>(i < 2 ? get(entry, 0x70) + half : get(entry, 0x70) - half);
        const float x = static_cast<float>(get(entry, 0x6c));
        method<PairMethod>(entry, 0x2c)(entry, x, y);
    }
    setup_help(top, regs, menu, 0x34, 0x38);
    uint8_t* const mp_menu = find_picture(menu, 0x0082c31c);  // "mp_menu"
    uint8_t* const back = find_picture(menu, 0x0082c314);     // "mp_back"
    if (mp_menu) call(mp_menu, 0x7c, 0);
    if (back) call(back, 0x7c, 0);
}

SETUP_GAP_ENTRY(FE_SetupPauseMenu, "%esi")

void XB_CDECL FE_SetupPauseMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    uint8_t* const title = Panel_FindText(text_at(0x0082c9b8), field<uint8_t*>(menu, 0x34));  // "GamePaused"
    regs.ebx = 0x0082c9b8;
    regs.edi = address_of(panel);
    const uint32_t title_args[1] = {0x0082c9a8};  // "FEMENU_PAUSED"
    method_at(top, regs, title, 0x6c, 1, title_args);
    // Resume / Options / Restart / Quit / XBoxLive
    static constexpr uint32_t kNames[5] = {0x0082c9a0, 0x0082c998, 0x0082c990, 0x0082c988, 0x0082c97c};
    add_items(top, regs, menu, 0x34, kNames, 5);
    regs.ebx = kNames[4];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr struct {
        uint32_t index, text;
    } kItemTexts[5] = {{1, 0x0082c96c}, {2, 0x0082c954}, {0, 0x0082c940}, {3, 0x0082c92c}, {4, 0x0082c91c}};
    for (const auto& entry : kItemTexts) {
        const uint32_t args[1] = {entry.text};
        method_at(top, regs, item(menu, entry.index), 0x30, 1, args);
    }
    for (uint32_t i = 0; i < 5; ++i) call(item(menu, i), 0x5c, 0x3f333333);  // 0.7
    setup_help(top, regs, menu, 0x34, 0x38, 0x0082c8f4, 0x0082c910);  // "FEMENU_HELPBAR_UNPAUSE_FULL", "Helpbar1"
    setup_help(top, regs, menu, 0x34, 0x3c, 0x0082ca0c, 0x0082c8e8);  // "FEMENU_HELPBAR_SELECT", "Helpbar2"
    // PauseBackground and whitelines 10 lower (+0xa0 y, +0x9c x, +0x8c (x, y)), then both help texts (+0xcc y,
    // +0xc8 x, +0x7c (x, y)).
    static constexpr uint32_t kPictures[2] = {0x0082ca44, 0x0082c8dc};
    for (const uint32_t name : kPictures) {
        uint8_t* const picture = find_picture(menu, name);
        if (!picture) continue;
        const float y = static_cast<float>(get(picture, 0xa0) + 10.0f);
        const float x = static_cast<float>(get(picture, 0x9c));
        method<PairMethod>(picture, 0x8c)(picture, x, y);
    }
    static constexpr uint32_t kHelps[2] = {0x38, 0x3c};
    for (const uint32_t offset : kHelps) {
        uint8_t* const text = field<uint8_t*>(menu, offset);
        const float y = static_cast<float>(get(text, 0xcc) + 10.0f);
        const float x = static_cast<float>(get(text, 0xc8));
        method<PairMethod>(text, 0x7c)(text, x, y);
    }
}

SETUP_GAP_ENTRY(FE_SetupGameplayMenu, "%esi")

void XB_CDECL FE_SetupGameplayMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    // Subtitles / Crosshair / Friendlytags / TankStyle / ADSToggle / ResetToDefault / SaveAndExit
    static constexpr uint32_t kNames[7] = {0x0082c308, 0x0082c820, 0x0082c2f8, 0x0082c2ec,
                                           0x0082c7e0, 0x0082c2dc, 0x0082c2d0};
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 7);
    regs.ebx = kNames[6];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[7] = {0x0082c708, 0x0082c750, 0x0082c734, 0x0082c6f0,
                                               0x0082c6d8, 0x0082c2b0, 0x0082c294};
    for (uint32_t i = 0; i < 7; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    call(item(menu, 3), 0x5c, 0x3f333333);  // 0.7
    call(item(menu, 4), 0x5c, 0x3f333333);
    set_title(top, regs, menu, 0x34, 0x0082c284, 0x0082c348);  // "GameplayMenu", "FEMENU_OP_GAMEPLAY"
    // The on/off texts: VibrationOnOff / CrossHairOnOff / FriendlyTagsOnOff / TankStyleOnOff / ADSToggleOnOff.
    static constexpr uint32_t kValues[5] = {0x0082c68c, 0x0082c274, 0x0082c260, 0x0082c250, 0x0082c63c};
    for (uint32_t i = 0; i < 5; ++i)
        field<uint8_t*>(menu, 0x3c + i * 4) = Panel_FindText(text_at(kValues[i]), field<uint8_t*>(menu, 0x34));
    // arrowleft02 / arrowright02 / arrowleft04 .. arrowright07 to +0x50..+0x74.
    static constexpr uint32_t kArrows[10] = {0x0082c244, 0x0082c234, 0x0082c228, 0x0082c218, 0x0082c20c,
                                             0x0082c1fc, 0x0082c1f0, 0x0082c1e0, 0x0082c1d4, 0x0082c1c4};
    for (uint32_t i = 0; i < 10; ++i) field<uint8_t*>(menu, 0x50 + i * 4) = find_picture(menu, kArrows[i]);
    uint32_t local = 0;
    field<uint32_t>(menu, 0x78) = *method<ColourMethod>(field<uint8_t*>(menu, 0x50), 0xac)(field<uint8_t*>(menu, 0x50), &local);
    setup_help(top, regs, menu, 0x34, 0x38);
    uint8_t* const mp_menu = find_picture(menu, 0x0082c31c);  // "mp_menu"
    uint8_t* const back = find_picture(menu, 0x0082c314);     // "mp_back"
    if (mp_menu) call(mp_menu, 0x7c, 0);
    if (back) call(back, 0x7c, 0);
    if (!title_global<int32_t>(0x0095f5dc)) return;  // g_languageIndex: no subtitles item outside English
    const uint32_t tail = entry_esp - 4;  // after pop edi / ebx
    call(item(menu, 0), 0x20, 1);
    call(field<uint8_t*>(menu, 0x50), 0x7c, 0);
    call(field<uint8_t*>(menu, 0x54), 0x7c, 0);
    const uint32_t empty[1] = {0x0082dfeb};  // ""
    method_at(tail, regs, item(menu, 0), 0x34, 1, empty);
    method_at(tail, regs, field<uint8_t*>(menu, 0x3c), 0x78, 1, empty);
    call(field<uint8_t*>(menu, 0x3c), 0x88, 0);
}

SETUP_GAP_ENTRY(FE_SetupControllerConfigMenu, "%esi")

void XB_CDECL FE_SetupControllerConfigMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x10;  // push ecx / ebx / ebp / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    // InvertAim / Vibration / HorizontalSens / Verticalsens / Configuration / ResetToDefault / SaveAndExit
    static constexpr uint32_t kNames[7] = {0x0082c858, 0x0082c84c, 0x0082c83c, 0x0082c1b4,
                                           0x0082c1a4, 0x0082c2dc, 0x0082c2d0};
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, 0x34, kNames, 7);
    regs.ebx = kNames[6];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[7] = {0x0082c7b0, 0x0082c798, 0x0082c780, 0x0082c768,
                                               0x0082c190, 0x0082c2b0, 0x0082c294};
    for (uint32_t i = 0; i < 7; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    set_title(top, regs, menu, 0x34, 0x0082c178, 0x0082c164);  // "ControllerConfiguration", "FEMENU_COP_CONTROLS"
    // InvertAimOnOff / VibrationOnOff / configABCD to +0x3c..+0x44.
    static constexpr uint32_t kValues[3] = {0x0082c69c, 0x0082c68c, 0x0082c158};
    for (uint32_t i = 0; i < 3; ++i)
        field<uint8_t*>(menu, 0x3c + i * 4) = Panel_FindText(text_at(kValues[i]), field<uint8_t*>(menu, 0x34));
    // The slider arrows: arrowL / arrowL01 / arrowL02 to +0x48 / +0x50 / +0x58, arrowR.. to +0x4c / +0x54 / +0x5c.
    static constexpr struct {
        uint32_t offset, name;
    } kArrows[6] = {{0x48, 0x0082c150}, {0x50, 0x0082c144}, {0x58, 0x0082c138},
                    {0x4c, 0x0082c130}, {0x54, 0x0082c124}, {0x5c, 0x0082c118}};
    for (const auto& arrow : kArrows) field<uint8_t*>(menu, arrow.offset) = find_picture(menu, arrow.name);
    uint32_t local = 0;
    field<uint32_t>(menu, 0xc4) = *method<ColourMethod>(field<uint8_t*>(menu, 0x48), 0xac)(field<uint8_t*>(menu, 0x48), &local);
    field<uint8_t*>(menu, 0x60) = find_picture(menu, 0x0082c100);  // "HorizontalSensProgbar01"
    field<uint8_t*>(menu, 0x64) = find_picture(menu, 0x0082c0e8);  // "VerticalSensProgbar01"
    // The button-layout labels ("start" .. "ClickR") to +0x68..+0xb4.
    static constexpr uint32_t kLabels[20] = {0x0082c0e0, 0x0082c0d8, 0x0082c0d0, 0x0082c0c4, 0x0082c0b4,
                                             0x0082c0a4, 0x0082c09c, 0x0082c098, 0x0082c08c, 0x0082c080,
                                             0x0082c078, 0x0082c070, 0x0082c060, 0x0082c054, 0x0082c048,
                                             0x0082c03c, 0x0082c034, 0x0082c030, 0x0082c028, 0x0082c020};
    for (uint32_t i = 0; i < 20; ++i)
        field<uint8_t*>(menu, 0x68 + i * 4) = Panel_FindText(text_at(kLabels[i]), field<uint8_t*>(menu, 0x34));
    regs.ebx = kLabels[19];
    // ClickL / ClickR: FEMENU_CMD_CLICK in black.
    const uint32_t click[1] = {0x0082c00c};
    method_at(top, regs, field<uint8_t*>(menu, 0xb0), 0x6c, 1, click);
    method_at(top, regs, field<uint8_t*>(menu, 0xb4), 0x6c, 1, click);
    call(field<uint8_t*>(menu, 0xb0), 0x90, 0xff000000);
    call(field<uint8_t*>(menu, 0xb4), 0x90, 0xff000000);
    uint8_t* const mp_menu = find_picture(menu, 0x0082c31c);  // "mp_menu"
    uint8_t* const back = find_picture(menu, 0x0082c314);     // "mp_back"
    if (mp_menu) call(mp_menu, 0x7c, 0);
    if (back) call(back, 0x7c, 0);
    setup_help(top, regs, menu, 0x34, 0x38);
}

SETUP_GAP_ENTRY(FE_SetupAdvancedOptionsMenu, "%esi")

void XB_CDECL FE_SetupAdvancedOptionsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    uint8_t* const title = Panel_FindText(text_at(0x0082c864), field<uint8_t*>(menu, 0x34));
    regs.ebx = 0x0082c864;
    regs.edi = address_of(panel);
    const uint32_t title_args[1] = {0x0082c96c};  // "FEMENU_OPTIONS"
    method_at(top, regs, title, 0x6c, 1, title_args);
    // The in-game options: controller rows, then sound, gameplay and the subtitles row (7), then the rest.
    static constexpr uint32_t kNames[12] = {0x0082c858, 0x0082c84c, 0x0082c83c, 0x0082c82c, 0x0082c820, 0x0082c810,
                                            0x0082c804, 0x0082c7f8, 0x0082c7ec, 0x0082c7e0, 0x0082c7d0, 0x0082c7c8};
    add_items(top, regs, menu, 0x34, kNames, 12);
    regs.ebx = kNames[11];
    regs.edi = field<uint32_t>(menu, 0);
    static constexpr uint32_t kItemTexts[12] = {0x0082c7b0, 0x0082c798, 0x0082c780, 0x0082c768,
                                                0x0082c750, 0x0082c734, 0x0082c720, 0x0082c708,
                                                0x0082c6f0, 0x0082c6d8, 0x0082c6bc, 0x0082c6ac};
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t args[1] = {kItemTexts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    for (uint32_t i = 0; i < 12; ++i) call(item(menu, i), 0x5c, 0x3f333333);  // 0.7
    // The value texts (InvertAimOnOff, VibrationOnOff, ..., ADSToggleOnOff) to +0x38..+0x50, scaled 0.7 (+0x60).
    static constexpr uint32_t kValues[7] = {0x0082c69c, 0x0082c68c, 0x0082c680, 0x0082c670,
                                            0x0082c660, 0x0082c64c, 0x0082c63c};
    for (uint32_t i = 0; i < 7; ++i)
        field<uint8_t*>(menu, 0x38 + i * 4) = Panel_FindText(text_at(kValues[i]), field<uint8_t*>(menu, 0x34));
    for (uint32_t i = 0; i < 7; ++i) call(field<uint8_t*>(menu, 0x38 + i * 4), 0x60, 0x3f333333);
    // The row arrows: arrowL1..7 to +0x54, +0x5c, .. +0x84, arrowR1..7 to +0x58, .. +0x88.
    static constexpr uint32_t kLeft[7] = {0x0082c634, 0x0082c62c, 0x0082c624, 0x0082c61c,
                                          0x0082c614, 0x0082c60c, 0x0082c604};
    static constexpr uint32_t kRight[7] = {0x0082c5fc, 0x0082c5f4, 0x0082c5ec, 0x0082c5e4,
                                           0x0082c5dc, 0x0082c5d4, 0x0082c5cc};
    for (uint32_t i = 0; i < 7; ++i) field<uint8_t*>(menu, 0x54 + i * 8) = find_picture(menu, kLeft[i]);
    for (uint32_t i = 0; i < 7; ++i) field<uint8_t*>(menu, 0x58 + i * 8) = find_picture(menu, kRight[i]);
    uint32_t local = 0;
    field<uint32_t>(menu, 0xb4) = *method<ColourMethod>(field<uint8_t*>(menu, 0x54), 0xac)(field<uint8_t*>(menu, 0x54), &local);
    field<uint8_t*>(menu, 0x8c) = find_picture(menu, 0x0082c5bc);  // "HSensProgbar2"
    field<uint8_t*>(menu, 0x90) = find_picture(menu, 0x0082c5ac);  // "VSensProgbar2"
    field<uint8_t*>(menu, 0x94) = find_picture(menu, 0x0082c598);  // "MusicVolumeProgbar2"
    setup_help(top, regs, menu, 0x34, 0x98, 0x0082c580, 0x0082c910);  // "FEMENU_HELPBAR_UNPAUSE", "Helpbar1"
    setup_help(top, regs, menu, 0x34, 0x9c, 0x0082ca0c, 0x0082c8e8);  // "FEMENU_HELPBAR_SELECT", "Helpbar2"
    setup_help(top, regs, menu, 0x34, 0xa0, 0x0082c560, 0x0082c574);  // "FEMENU_HELPBAR_BACK", "Helpbar3"
    if (!title_global<int32_t>(0x0095f5dc)) return;  // g_languageIndex
    // Outside English the subtitles row (item 7, its arrows +0x74 / +0x78, its value +0x48) goes and the rows below
    // move up into the places above them (+0x70 y / +0x6c x / +0x2c for items, +0xa0 / +0x9c / +0x8c for pictures,
    // +0xcc / +0xc8 / +0x7c for texts).
    call(item(menu, 7), 0x20, 1);
    call(field<uint8_t*>(menu, 0x74), 0x7c, 0);
    call(field<uint8_t*>(menu, 0x78), 0x7c, 0);
    const uint32_t empty[1] = {0x0082dfeb};  // ""
    method_at(top, regs, item(menu, 7), 0x34, 1, empty);
    method_at(top, regs, field<uint8_t*>(menu, 0x48), 0x78, 1, empty);
    call(field<uint8_t*>(menu, 0x48), 0x88, 0);
    auto move = [](uint8_t* from, uint8_t* to, uint32_t get_y, uint32_t get_x, uint32_t set) {
        const float y = static_cast<float>(get(from, get_y));
        const float x = static_cast<float>(get(from, get_x));
        method<PairMethod>(to, set)(to, x, y);
    };
    move(item(menu, 8), item(menu, 9), 0x70, 0x6c, 0x2c);
    move(item(menu, 7), item(menu, 8), 0x70, 0x6c, 0x2c);
    static constexpr struct {
        uint32_t from, to, get_y, get_x, set;
    } kMoves[6] = {{0x7c, 0x84, 0xa0, 0x9c, 0x8c}, {0x80, 0x88, 0xa0, 0x9c, 0x8c}, {0x74, 0x7c, 0xa0, 0x9c, 0x8c},
                   {0x78, 0x80, 0xa0, 0x9c, 0x8c}, {0x4c, 0x50, 0xcc, 0xc8, 0x7c}, {0x48, 0x4c, 0xcc, 0xc8, 0x7c}};
    for (const auto& m : kMoves)
        move(field<uint8_t*>(menu, m.from), field<uint8_t*>(menu, m.to), m.get_y, m.get_x, m.set);
}

// --- FE_SetupPanel ---
//
// Every helper derives its frame from its entry ESP (their text setters keep stack addresses), so the dispatcher
// calls each one where the original does: entry - 0x10 after its four pushes (the first branch: - 0xc), with the
// original's return address; the tail jumps run at the entry ESP itself with the stack argument replaced.
PLATFORM_ASM_ENTRY(FE_SetupPanel_entry, "movl %esp, %ecx\n" GAME_GAP_ESP("0x10000") "pushl %ecx\n"
                                        "pushl %ebp\n pushl %edi\n pushl %esi\n pushl %ebx\n pushl %ecx\n"
                                        "pushl %edx\n pushl %eax\n" PLATFORM_ASM_CALL(FE_SetupPanelAt)
                                        "addl $28, %esp\n popl %esp\n ret $4")

namespace {

// Where a branch finds its menu: [[0x0095f8f8]+4] (front end), [[0x0095f8fc]+4] (in game), [0x0095f900], the stack
// argument (the owner) or the owner's +0x20.
enum class Source : uint8_t { Front, Game, Dialogs, Owner, OwnerHud };
// The register the helper takes the menu in, or the stack argument (a tail jump).
enum class Pass : uint8_t { Esi, Ecx, Edi, Tail };

struct Branch {
    uint32_t name, length;  // the panel file name, compared over `length` bytes (the NUL included)
    Source source;
    uint32_t offset;
    Pass pass;
    uint32_t function, ret;  // the helper and the original's return address (0 for a tail jump)
};

constexpr Branch kBranches[] = {
    {0x0082cf78, 0x22, Source::Front, 0x08, Pass::Esi, 0x00328d10, 0x0032a96a},  // FE_SetupSinglePlayerMenu
    {0, 0, Source::Front, 0, Pass::Esi, 0, 0},                                     // the title screen (below)
    {0x0082cf44, 0x19, Source::Front, 0x3c, Pass::Esi, 0x003256b0, 0x0032a9e6},  // FE_SetupProfileMenu
    {0x0082cf2c, 0x16, Source::Front, 0x34, Pass::Esi, 0x00325e60, 0x0032aa10},  // FE_SetupDifficultyMenu
    {0x0082cf0c, 0x1d, Source::Front, 0x0c, Pass::Ecx, 0x00328af0, 0x0032aa3a},  // FE_SetupOptionsMenu
    {0x0082cef0, 0x19, Source::Front, 0x44, Pass::Tail, 0x00324f70, 0},           // FE_SetupChapterMenu
    {0x0082ced8, 0x16, Source::Dialogs, 0x1c, Pass::Esi, 0x00329fe0, 0x0032aa8c},  // FE_SetupMemcardMenu
    {0x0082cebc, 0x1a, Source::Front, 0x20, Pass::Esi, 0x00327490, 0x0032aab6},  // FE_SetupBonusMaterialsMenu
    {0x0082ce9c, 0x20, Source::Front, 0x24, Pass::Esi, 0x00326f20, 0x0032aae0},  // FE_SetupBonusCharactersMenu
    {0x0082ce7c, 0x1d, Source::Front, 0x28, Pass::Ecx, 0x00326970, 0x0032ab0a},  // FE_SetupBonusWeaponsMenu
    {0x0082ce5c, 0x1e, Source::Front, 0x2c, Pass::Ecx, 0x00326460, 0x0032ab34},  // FE_SetupBonusVehiclesMenu
    {0x0082ce3c, 0x1e, Source::Front, 0x30, Pass::Ecx, 0x00326010, 0x0032ab5e},  // FE_SetupBonusConceptsMenu
    {0, 0, Source::Front, 0, Pass::Esi, 0, 0},                                     // the special edition main menu
    {0x0082ce00, 0x1a, Source::Front, 0x4c, Pass::Esi, 0x00324740, 0x0032abb7},  // FE_SetupSpecialConceptMenu
    {0x0082cde4, 0x1c, Source::Front, 0x48, Pass::Esi, 0x00324ab0, 0x0032abe1},  // FE_SetupSpecialMaterialsMenu
    {0x0082cdd0, 0x11, Source::Game, 0x00, Pass::Esi, 0x00329c80, 0x0032ac0a},   // FE_SetupPauseMenu
    {0x0082cdb4, 0x1c, Source::Game, 0x04, Pass::Esi, 0x00329480, 0x0032ac34},   // FE_SetupAdvancedOptionsMenu
    {0x0082cda0, 0x13, Source::Game, 0x08, Pass::Esi, 0x00327c70, 0x0032ac5e},   // FE_SetupLoadingMenu
    {0x0082cd88, 0x16, Source::Game, 0x10, Pass::Esi, 0x003279b0, 0x0032ac88},   // FE_SetupLoadSicilyMenu
    {0x0082cd70, 0x18, Source::Game, 0x14, Pass::Esi, 0x003278b0, 0x0032acb2},   // FE_SetupLoadMaubeugeMenu
    {0x0082cd58, 0x16, Source::Game, 0x0c, Pass::Esi, 0x00327b00, 0x0032acdc},   // FE_SetupLoadAfricaMenu
    {0x0082cd38, 0x1e, Source::Game, 0x18, Pass::Tail, 0x00325370, 0},            // FE_SetupMissionSuccessFailMenu
    {0x0082cd24, 0x14, Source::Game, 0x1c, Pass::Esi, 0x00325210, 0x0032ad31},   // FE_SetupAutosaveMenu
    {0x0082cd10, 0x14, Source::Front, 0x10, Pass::Esi, 0x00328720, 0x0032ad5b},  // FE_SetupGameplayMenu
    {0x0082ccf0, 0x20, Source::Front, 0x14, Pass::Esi, 0x00328210, 0x0032ad85},  // FE_SetupControllerConfigMenu
    {0x0082ccdc, 0x13, Source::Owner, 0x28, Pass::Tail, 0x0032a660, 0},            // FE_SetupLoginStatus
    {0x0082ccc4, 0x17, Source::Front, 0x50, Pass::Esi, 0x00223260, 0x0032add5},  // FE_SetupXboxLiveSingle
    {0x0082cca8, 0x1b, Source::Game, 0x20, Pass::Esi, 0x00223140, 0x0032adff},   // FE_SetupInGameLiveOptions
    {0x0082cc94, 0x12, Source::Front, 0x18, Pass::Esi, 0x00327fd0, 0x0032ae29},  // FE_SetupScreenMenu
    {0x0082cc80, 0x11, Source::Front, 0x1c, Pass::Ecx, 0x00327de0, 0x0032ae53},  // FE_SetupSoundMenu
    {0x0082cc5c, 0x24, Source::Owner, 0x24, Pass::Edi, 0x00325c90, 0x0032ae78},  // FE_SetupControllerDisconnected
    {0, 0, Source::Front, 0, Pass::Esi, 0, 0},                                     // hud_menu (below)
    {0x0082cc34, 0x15, Source::OwnerHud, 0x24, Pass::Ecx, 0x00323160, 0x0032aec6},  // FE_SetupLiberatorMenu
    {0x0082cc18, 0x19, Source::Front, 0x38, Pass::Esi, 0x00325ac0, 0x0032aef0},  // FE_SetupCreateProfileMenu
};

// repe cmpsb over `length` bytes.
bool same_name(const char* name, uint32_t expected, uint32_t length) {
    const char* const other = text_at(expected);
    for (uint32_t i = 0; i < length; ++i)
        if (name[i] != other[i]) return false;
    return true;
}

uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

// A helper's tail jump: ESP = entry with the stack argument `arg`, returning straight to the dispatcher's caller.
uint32_t tail_call(uint32_t entry, uint32_t function, const TitleRegs& regs, uint32_t arg) {
    const uint32_t ret = stack_word(entry);
    const uint32_t args[1] = {arg};
    const uint32_t result = title_call_at_regs(entry + 8, function, &regs, 1, args);
    stack_word(entry) = ret;
    return result;
}

}  // namespace

uint32_t XB_CDECL FE_SetupPanelAt(uint8_t* panel, const char* name, uint32_t entry, uint32_t ebx, uint32_t esi,
                                  uint32_t edi, uint32_t ebp) {
    const uint32_t owner = stack_word(entry + 4);
    const uint8_t special = title_global<uint8_t>(0x0095e7f8);  // the special edition main menu
    stack_word(entry - 4) = ebx;  // push ebx / esi / edi
    stack_word(entry - 8) = esi;
    stack_word(entry - 12) = edi;
    TitleRegs regs{};
    regs.eax = address_of(panel);
    auto front = [] { return field<uint8_t*>(title_global<uint8_t*>(0x0095f8f8), 4); };
    auto game = [] { return field<uint8_t*>(title_global<uint8_t*>(0x0095f8fc), 4); };
    if (same_name(name, 0x0082cf9c, 0x16) && !special) {
        regs.esi = field<uint32_t>(front(), 4);
        const uint32_t result = title_call_at_regs(entry - 12, 0x00328f70, &regs, 0, nullptr);  // FE_SetupMainMenu
        stack_word(entry - 16) = 0x0032a940;
        return result;
    }
    stack_word(entry - 16) = ebp;  // push ebp
    const uint32_t top = entry - 16;
    for (uint32_t i = 0; i < sizeof(kBranches) / sizeof(kBranches[0]); ++i) {
        const Branch& b = kBranches[i];
        if (i == 1) {  // the title screen: panel to the menu, layer 2, FEMENU_TITLE_PRESS_START_XBOX into its Helpbar
            if (!same_name(name, 0x0082cf60, 0x17)) continue;
            uint8_t* const menu = field<uint8_t*>(front(), 0);
            field<uint8_t*>(menu, 0x34) = panel;
            if (!panel) return address_of(panel);
            Panel_SetLayer(panel, 2);
            uint8_t* const help = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
            regs.ebx = kHelpbar;
            regs.ecx = address_of(help);
            regs.esi = address_of(menu);
            regs.edi = address_of(panel);
            return tail_call(entry, field<uint32_t>(field<uint8_t*>(help, 0), 0x6c), regs, 0x0082cbb0);
        }
        if (i == 12) {  // the special edition main menu
            if (!same_name(name, 0x0082ce1c, 0x1e) || special != 1) continue;
            regs.esi = field<uint32_t>(front(), 4);
            const uint32_t result = title_call_at_regs(top, 0x00328f70, &regs, 0, nullptr);
            stack_word(top - 4) = 0x0032ab8d;
            return result;
        }
        if (i == 31) {  // hud_menu: the owner's +0x20 vtable +4 (panel)
            if (!same_name(name, 0x0082cc4c, 0xf)) continue;
            uint8_t* const hud = field<uint8_t*>(reinterpret_cast<uint8_t*>(uintptr_t{owner}), 0x20);
            regs.ecx = address_of(hud);
            return tail_call(entry, field<uint32_t>(field<uint8_t*>(hud, 0), 4), regs, address_of(panel));
        }
        if (!same_name(name, b.name, b.length)) continue;
        uint8_t* base = nullptr;
        switch (b.source) {
            case Source::Front: base = front(); break;
            case Source::Game: base = game(); break;
            case Source::Dialogs: base = title_global<uint8_t*>(0x0095f900); break;
            case Source::Owner: base = reinterpret_cast<uint8_t*>(uintptr_t{owner}); break;
            case Source::OwnerHud: base = field<uint8_t*>(reinterpret_cast<uint8_t*>(uintptr_t{owner}), 0x20); break;
        }
        const uint32_t menu = field<uint32_t>(base, b.offset);
        switch (b.pass) {
            case Pass::Esi: regs.esi = menu; break;
            case Pass::Ecx: regs.ecx = menu; break;
            case Pass::Edi: regs.edi = menu; break;
            case Pass::Tail: regs.ecx = menu; return tail_call(entry, b.function, regs, menu);
        }
        const uint32_t result = title_call_at_regs(top, b.function, &regs, 0, nullptr);
        stack_word(top - 4) = b.ret;
        return result;
    }
    if (!panel) return 0;
    regs.esi = address_of(panel);
    const uint32_t args[1] = {1};
    const uint32_t result = title_call_at_regs(top, 0x000121d0, &regs, 1, args);  // Panel_DeletingDtor
    stack_word(top - 8) = 0x0032af04;
    return result;
}

}  // namespace game
