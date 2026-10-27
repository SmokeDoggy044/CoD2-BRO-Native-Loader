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
    // The tail call (jmp, ESP back at the entry): run here below the entry with the same text.
    slot = 0x0082caa4;  // "FEMENU_XBOX_LIVE"
    regs.ebx = 0x0082cab8;
    regs.esi = address_of(source);
    regs.edi = address_of(copy);
    const uint32_t args[1] = {slot};
    uint8_t* const text = field<uint8_t*>(menu, 0x1c);
    regs.ecx = address_of(text);
    return title_call_at_regs(entry_esp, field<uint32_t>(field<uint8_t*>(text, 0), 0x6c), &regs, 1, args);
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

}  // namespace game
