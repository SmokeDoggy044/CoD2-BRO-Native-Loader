#include "fe_profile.h"

#include "cl.h"
#include "fe.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"

// As fe_menus.cpp: the entries move the C frame below a gap and hand the C version the entry ESP; every title call
// then runs where the original's would (the text setters keep a stack buffer's address in a heap record).
#define PROFILE_GAP_ENTRY(name, pushes, c_name, c_arg_bytes, ret)                                                \
    PLATFORM_ASM_ENTRY(name, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"                          \
                             "pushl %edx\n" pushes PLATFORM_ASM_CALL(c_name) "addl $" c_arg_bytes ", %esp\n"     \
                             "popl %esp\n" ret)

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }
uint8_t* stack_bytes(uint32_t address) { return reinterpret_cast<uint8_t*>(uintptr_t{address}); }

constexpr uint32_t kEmptyString = 0x0082dfeb;    // ""
constexpr uint32_t kDialogOk = 0x0082b0b0;       // "MEM_DIALOG_OK"
constexpr uint32_t kDialogYes = 0x0082c8b8;      // "MEM_DIALOG_YES"
constexpr uint32_t kDialogNo = 0x0082c8a8;       // "MEM_DIALOG_NO"
constexpr uint32_t kInsertCard = 0x0082b0c0;     // "MEM_ERROR_INSERT_CARD"
constexpr uint32_t kContinueNoSave = 0x0082b0f4;  // "MEM_CONTINUE_WO_SAVING"
constexpr uint32_t kReturnToMain = 0x0082aec0;   // "MEM_RETURN_TO_MAIN"
constexpr uint32_t kProfileName = 0x0082b1bc;    // "profileName"

// Title functions called at their addresses.
constexpr uint32_t kUI_ShowDialog = 0x003319e0;
constexpr uint32_t kUI_DialogAddButton = 0x00012320;
constexpr uint32_t kUI_DialogLayoutButtons = 0x00012390;
constexpr uint32_t kDialog_LayoutButtons = 0x0031f4b0;
constexpr uint32_t kDialogMgr_CloseDialog = 0x0031f6e0;
constexpr uint32_t kPanel_FindText = 0x003215c0;
constexpr uint32_t kProfileSave_PrepareRequest = 0x0031acf0;
constexpr uint32_t kSave_BeginRead = 0x000d26f0;
constexpr uint32_t kSave_SetPort = 0x000d0c90;
constexpr uint32_t kSave_GetFreeSpace = 0x000d1b40;
constexpr uint32_t kSave_ProfilesDirExists = 0x0030c300;
constexpr uint32_t kSave_Update = 0x000d24a0;
constexpr uint32_t kUIMenu_Update = 0x00314ba0;
constexpr uint32_t kFE_ProfileSelect = 0x00338710;

// The save manager (g_saves) and the profile globals.
uint8_t* saves() { return title_global<uint8_t*>(0x0095f480); }
uint32_t& now_bits() { return title_global<uint32_t>(0x008ddaa8); }
uint32_t& session_start_bits() { return title_global<uint32_t>(0x0095fd14); }

// Where title calls run: 0 from the C frame, else at this ESP (the original's ESP before the call's pushes).
uint32_t g_top = 0;
struct AtTop {
    uint32_t saved;
    explicit AtTop(uint32_t top) : saved(g_top) { g_top = top; }
    ~AtTop() { g_top = saved; }
};

uint32_t title_call(uint32_t function, TitleRegs regs, uint32_t count = 0, const uint32_t* args = nullptr) {
    if (g_top) return title_call_at_regs(g_top, function, &regs, count, args);
    return ui_call_regs(function, &regs, count, args);
}
TitleRegs eax_regs(uint32_t eax) {
    TitleRegs regs{};
    regs.eax = eax;
    return regs;
}
TitleRegs esi_regs(uint8_t* esi) {
    TitleRegs regs{};
    regs.esi = address_of(esi);
    return regs;
}

// A tail jump: the callee runs at the entry ESP and returns to this function's caller; the return address slot
// (overwritten by the call) is put back.
void tail_call(uint32_t entry_esp, uint32_t function, TitleRegs regs) {
    const uint32_t return_address = stack_word(entry_esp);
    {
        AtTop at(entry_esp + 4);
        title_call(function, regs);
    }
    stack_word(entry_esp) = return_address;
}

// A title object's vtable slot (byte offset), thiscall.
uint32_t slot(uint8_t* object, uint32_t offset) { return field<uint32_t>(field<uint8_t*>(object, 0), offset); }
uint32_t call(uint8_t* object, uint32_t offset) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    return title_call(slot(object, offset), regs);
}
uint32_t call(uint8_t* object, uint32_t offset, uint32_t a) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    const uint32_t args[1] = {a};
    return title_call(slot(object, offset), regs, 1, args);
}
uint32_t call(uint8_t* object, uint32_t offset, uint32_t a, uint32_t b) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    const uint32_t args[2] = {a, b};
    return title_call(slot(object, offset), regs, 2, args);
}

// The menu's item (the item array +0x04 re-read each time, as the original does).
uint8_t* item(uint8_t* menu, int32_t index) { return field<uint8_t**>(menu, 4)[index]; }
int32_t selection(uint8_t* menu) { return field<int16_t>(menu, 0x22); }
// Item vtable +0x24: hidden (AL).
bool hidden(uint8_t* menu, int32_t index) { return (call(item(menu, index), 0x24) & 0xff) != 0; }
// Item vtable +0x20: shown (0) / hidden (1).
void set_hidden(uint8_t* menu, int32_t index, uint32_t hide) { call(item(menu, index), 0x20, hide); }
// The selected item's grid neighbour (+0x08 up, +0x0a down, +0x0c left, +0x0e right).
int16_t& neighbour(uint8_t* menu, int32_t index, uint32_t offset) { return field<int16_t>(item(menu, index), offset); }

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

// The dialog manager (0x0095f900) and its current dialog (+0x04 [+0x0c != 0]).
uint8_t* dialog_mgr() { return title_global<uint8_t*>(0x0095f900); }
uint8_t* current_dialog() {
    uint8_t* const mgr = dialog_mgr();
    return field<uint8_t**>(mgr, 4)[field<uint32_t>(mgr, 0xc) != 0 ? 1 : 0];
}
// The front end's (0x0095f8f8) and the in-game screens' (0x0095f8fc) screen.
uint8_t* front_end_screen(uint32_t offset) {
    return field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8f8), 4), offset);
}
uint8_t* in_game_screen(uint32_t offset) {
    return field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8fc), 4), offset);
}

// UI_ShowDialog with no OK / YES-NO buttons, title "" and the game kept running.
void show_dialog(uint32_t name) {
    const uint32_t args[5] = {address_of(dialog_mgr()), 0, 0, kEmptyString, 1};
    title_call(kUI_ShowDialog, eax_regs(name), 5, args);
}
// UI_DialogAddButton by its title address.
void add_button(uint32_t label, uint32_t handler) {
    const uint32_t args[2] = {label, handler};
    title_call(kUI_DialogAddButton, eax_regs(address_of(dialog_mgr())), 2, args);
}
// UI_DialogAddButton inlined: the next button's label (+0x30), handler and shown (+0x20 (0)).
void add_button_inline(uint32_t label, uint32_t handler) {
    uint8_t* const dialog = current_dialog();
    call(field<uint8_t**>(dialog, 4)[field<int32_t>(dialog, 0x34)], 0x30, label);
    field<uint32_t>(dialog, 0x38 + field<int32_t>(dialog, 0x34) * 4) = handler;
    call(field<uint8_t**>(dialog, 4)[field<int32_t>(dialog, 0x34)], 0x20, 0);
    ++field<int32_t>(dialog, 0x34);
}
void select_no() {
    uint8_t* const dialog = current_dialog();
    field<int16_t>(dialog, 0x22) = 1;
    dialog[0x5a] = 1;
}
// Dialog_LayoutButtons (AL = 1, EDI = the dialog; EAX's upper bytes from the manager pointer). Returns its EAX.
uint32_t layout_buttons() {
    TitleRegs regs{};
    regs.eax = (address_of(dialog_mgr()) & ~0xffu) | 1;
    regs.edi = address_of(current_dialog());
    return title_call(kDialog_LayoutButtons, regs);
}
// UI_DialogLayoutButtons (EAX = the manager, flag 1).
void layout_buttons_mgr() {
    const uint32_t one[1] = {1};
    title_call(kUI_DialogLayoutButtons, eax_regs(address_of(dialog_mgr())), 1, one);
}
// A one-button dialog (OK and the like).
uint32_t one_button_dialog(uint32_t name, uint32_t label, uint32_t handler) {
    show_dialog(name);
    add_button_inline(label, handler);
    return layout_buttons();
}

// The destructors' shared part: the vtable, the panel and its two aligned blocks, the owned object, the UIMenu part.
void menu_dtor(uint8_t* menu, uint32_t vtable, uint32_t panel_offset, uint32_t owned_offset) {
    uint8_t* const panel = field<uint8_t*>(menu, panel_offset);
    field<uint32_t>(menu, 0) = vtable;
    if (panel) {
        TitleRegs regs{};
        regs.eax = address_of(panel);
        title_call(0x00316620, regs);  // Panel_FreeElements
        if (field<void*>(panel, 0xc)) {
            Mem_AlignedFree(field<void*>(panel, 0xc));
            field<void*>(panel, 0xc) = nullptr;
        }
        if (field<void*>(panel, 0)) {
            Mem_AlignedFree(field<void*>(panel, 0));
            field<void*>(panel, 0) = nullptr;
        }
        z_free(panel);
    }
    if (uint8_t* const owned = field<uint8_t*>(menu, owned_offset)) call(owned, 0, 1);
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

// Panel_FindText (EBX = name; the panel on the stack).
uint8_t* find_text(uint8_t* panel, uint32_t name) {
    TitleRegs regs{};
    regs.ebx = name;
    const uint32_t args[1] = {address_of(panel)};
    return reinterpret_cast<uint8_t*>(uintptr_t{title_call(kPanel_FindText, regs, 1, args)});
}

// The play time (seconds +0x44, minutes +0x48, hours +0x4c, days +0x50 of a slot record) in seconds.
int32_t slot_seconds(uint8_t* record) {
    return ((field<int32_t>(record, 0x50) * 24 + field<int32_t>(record, 0x4c)) * 60 + field<int32_t>(record, 0x48)) *
               60 +
           field<int32_t>(record, 0x44);
}

// The save warning: `flag` the overwrite flag, `slot` the slot written, `split` FE_SplitPlayTime(2).
uint32_t save_warning(uint8_t* menu, uint32_t flag, int32_t slot, uint32_t split, uint32_t entry_esp) {
    float now, start;
    const uint32_t now_word = now_bits(), start_word = session_start_bits();
    __builtin_memcpy(&now, &now_word, 4);
    __builtin_memcpy(&start, &start_word, 4);
    float session = now - start;
    __builtin_memcpy(&session_start_bits(), &session, 4);
    uint8_t* const s = saves();
    float total = session;
    if (menu[flag] && s[0x11e7 + slot]) {
        uint8_t* const record = s[0x11e7 + slot] ? s + 0x28c + slot * 0x3cc : nullptr;
        total = static_cast<float>(slot_seconds(record)) + session;
    }
    AtTop at(entry_esp - 8);  // push ebx, push edi
    const uint32_t times[4] = {0x008dce58, 0x008dce5c, 0x008dce60, 0x008dce64};  // fe_playTime*
    title_call(split, eax_regs(static_cast<uint32_t>(static_cast<int32_t>(total))), 4, times);
    TitleRegs regs{};
    regs.ebx = address_of(s);
    regs.esi = address_of(menu);
    // The slot re-read: +0x4c (autosave) / the selection +0x22 (profile menu).
    const uint32_t init_args[2] = {static_cast<uint32_t>(flag == 0x50 ? field<int32_t>(menu, 0x4c) : selection(menu)),
                                   0};
    title_call(0x0030c4d0, regs, 2, init_args);  // SaveSlot_Init
    show_dialog(menu[flag] ? 0x0082ae24 : 0x0082ae0c);  // MEM_XBOX_OVERWRITE_WARNING / MEM_XBOX_SAVE_WARNING
    return layout_buttons();
}

void corrupt_dialog(uint8_t* menu, uint32_t state_offset, uint32_t name, uint32_t handler, uint32_t entry_esp) {
    const int32_t state = field<int32_t>(menu, state_offset);
    if (state != 0 && state != 2) return;
    AtTop at(entry_esp - 8);
    one_button_dialog(name, kDialogOk, handler);
}

void start_profile_load(uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    show_dialog(0x0082b064);  // MEM_XBOX_CHECK_WARNING
    layout_buttons();
    uint8_t* const s = saves();
    title_global<uint8_t>(0x008d50dd) = 0;
    TitleRegs regs{};
    regs.eax = address_of(s);
    regs.esi = address_of(s);
    title_call(kProfileSave_PrepareRequest, regs);
    const uint32_t args[1] = {address_of(s + 4)};
    title_call(kSave_BeginRead, regs, 1, args);
}

void unformatted_dialog(uint32_t yes_handler, uint32_t no_handler, uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    show_dialog(0x0082b07c);  // MEM_ERROR_UNFORMATTED
    add_button_inline(kDialogYes, yes_handler);
    add_button_inline(kDialogNo, no_handler);
    select_no();
    layout_buttons();
}

// The storage checks' first part (sub esp, 0x20; push ebx): the request, port 8 and the free space into the stack
// block at entry - 0x20. True for an unformatted unit.
bool read_free_space(uint8_t* menu, uint32_t entry_esp, uint32_t clear_a, uint32_t clear_b, uint32_t clear_c,
                     uint32_t clear_d) {
    TitleRegs regs = esi_regs(menu);
    regs.eax = address_of(saves());
    AtTop at(entry_esp - 0x24);
    title_call(kProfileSave_PrepareRequest, regs);
    menu[clear_a] = 0;
    menu[clear_b] = 0;
    menu[clear_c] = 0;
    const uint32_t port[1] = {8};
    regs = esi_regs(menu);
    title_call(kSave_SetPort, regs, 1, port);
    menu[clear_d] = 0;
    const uint32_t block[1] = {entry_esp - 0x20};
    int32_t result;
    {
        AtTop below(entry_esp - 0x28);  // the port argument still pushed
        result = static_cast<int32_t>(title_call(kSave_GetFreeSpace, regs, 1, block));
    }
    return result == -2 || (result == 0 && stack_word(entry_esp - 8) == 0);
}

}  // namespace

// --- Destructors ---

void XB_REGS1 XB_CDECL ProfileMenu_Dtor(uint8_t* menu) { menu_dtor(menu, 0x00828ed0, 0x3c, 0x40); }

void XB_REGS1 XB_CDECL AutosaveMenu_Dtor(uint8_t* menu) { menu_dtor(menu, 0x0082a130, 0x3c, 0x40); }

void XB_REGS1 XB_CDECL CreateProfileMenu_Dtor(uint8_t* menu) { menu_dtor(menu, 0x0082a068, 0x38, 0x3c); }

uint8_t* XB_THISCALL ProfileMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    ProfileMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL AutosaveMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    AutosaveMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL CreateProfileMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    CreateProfileMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

// --- Constructors ---

uint8_t* XB_REGS3 XB_CDECL AutosaveMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 4, owner, 0x140, 0xf0, 8, 0);
    field<uint32_t>(menu, 0) = 0x0082a130;  // vtable
    menu[0x33] = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<uint32_t>(menu, 0x44) = 0;
    field<uint32_t>(menu, 0x48) = 1;
    menu[0x84] = 0;
    menu[0x85] = 0;
    menu[0x50] = 0;
    menu[0x51] = 0;
    menu[0x52] = 0;
    menu[0x37] = 0;
    menu[0x34] = 0;
    menu[0x38] = 0;
    menu[0x39] = 0;
    for (int32_t i = 0; i < 4; ++i) menu[0x5c + i * 0xc] = 1;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL ProfileMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 4, owner, 0x140, 0xf0, 8, 0);
    field<uint32_t>(menu, 0) = 0x00828ed0;  // vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<uint32_t>(menu, 0x44) = 0;
    field<uint32_t>(menu, 0x48) = 0;
    field<uint32_t>(menu, 0x4c) = 1;
    menu[0x95] = 0;
    menu[0x96] = 0;
    menu[0x97] = 0;
    field<uint32_t>(menu, 0x98) = 0;  // 0.0f
    menu[0x54] = 0;
    for (int32_t i = 0; i < 4; ++i) menu[0x6c + i * 0xc] = 1;
    field<uint32_t>(menu, 0x5c) = 0;
    menu[0x60] = 0;
    menu[0x34] = 0;
    menu[0x61] = 0;
    menu[0x35] = 0;
    menu[0x94] = 0;
    menu[0x37] = 0;
    menu[0x38] = 0;
    menu[0x39] = 0;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL CreateProfileMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIGridMenu_Init(menu, 0x28, owner, 0);
    menu[0x2a] |= 0x80;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    menu[0x198] = 0;
    menu[0x1a8] = 0;
    field<uint32_t>(menu, 0x1ac) = 0;
    menu[0x1b0] = 0;
    field<uint32_t>(menu, 0x194) = 0;
    menu[0x34] = 0;
    field<uint32_t>(menu, 0) = 0x0082a068;  // vtable
    menu[0x33] = 8;
    return menu;
}

// --- Create profile ---

void XB_THISCALL CreateProfileMenu_MoveRight(uint8_t* menu) {
    const int32_t old = selection(menu);
    if (selection(menu) == 0x24 && hidden(menu, 19)) neighbour(menu, selection(menu), 0xe) = 0x27;
    if (selection(menu) == 0x1b && hidden(menu, 36)) neighbour(menu, selection(menu), 0xe) = 0x13;
    if (selection(menu) == 0x27 && hidden(menu, 37)) return;
    UIGridMenu_MoveRight(menu);
    if (old != selection(menu)) call(menu, 0x10);
    if (selection(menu) == 0x24) neighbour(menu, 36, 0xe) = 0x13;
    if (selection(menu) == 0x1b) neighbour(menu, 27, 0xe) = 0x24;
}

void XB_THISCALL CreateProfileMenu_MoveLeft(uint8_t* menu) {
    const int32_t old = selection(menu);
    if (selection(menu) == 0x27 && hidden(menu, 35)) {
        neighbour(menu, selection(menu), 0xc) = 0x25;
        if (hidden(menu, 37)) neighbour(menu, selection(menu), 0xc) = 0x24;
    }
    if (selection(menu) == 0x24 && hidden(menu, 27)) return;
    if (selection(menu) == 0x25 && hidden(menu, 39)) neighbour(menu, selection(menu), 0xc) = 0x23;
    UIGridMenu_MoveLeft(menu);
    if (old != selection(menu)) call(menu, 0x10);
    if (selection(menu) == 0x27) neighbour(menu, 39, 0xc) = 0x23;
    if (selection(menu) == 0x25) neighbour(menu, 37, 0xc) = 0x27;
}

void XB_THISCALL CreateProfileMenu_MoveDown(uint8_t* menu) {
    const int32_t old = selection(menu);
    if (old >= 0x1c && old <= 0x23 && hidden(menu, 38)) {
        neighbour(menu, selection(menu), 0xa) = 0x25;
        if (hidden(menu, 37)) neighbour(menu, selection(menu), 0xa) = 0x27;
    }
    if (selection(menu) == 0x1b && hidden(menu, 39)) neighbour(menu, selection(menu), 0xa) = 0x23;
    UIGridMenu_MoveDown(menu);
    if (old != selection(menu)) call(menu, 0x10);
    const int32_t now = selection(menu);
    if (now >= 0x1c && now <= 0x23) neighbour(menu, now, 0xa) = 0x26;
    if (selection(menu) == 0x1b) neighbour(menu, 27, 0xa) = 0x27;
}

void XB_THISCALL CreateProfileMenu_MoveUp(uint8_t* menu) {
    const int32_t old = selection(menu);
    if (old == 0x25 && hidden(menu, 36)) neighbour(menu, selection(menu), 0x8) = 0x13;
    UIGridMenu_MoveUp(menu);
    if (old != selection(menu)) call(menu, 0x10);
    if (selection(menu) == 0x25) neighbour(menu, 37, 0x8) = 0x24;
}

void XB_THISCALL CreateProfileMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x38)) {
        TitleRegs regs{};
        regs.esi = address_of(panel);
        regs.edi = address_of(menu);
        title_call(0x00310010, regs);  // Panel_Draw
        UIMenu_Draw(menu);
    }
    if (uint8_t* const owned = field<uint8_t*>(menu, 0x3c)) call(owned, 0x3c);
    if (!title_global<uint8_t>(0x0095f916) && title_global<uint8_t*>(0x0095f454)) {
        call(title_global<uint8_t*>(0x0095f454), 8);
        Subtitle_Draw();
        uint8_t* const texture =
            title_function<uint8_t* XB_THISCALL(uint8_t*)>(0x000cf100)(title_global<uint8_t*>(0x0095f454));
        uint8_t* const quad = title_global<uint8_t*>(0x0095f458);
        field<uint8_t*>(quad, 0x60) = texture;
        nglListAddQuad(reinterpret_cast<const NglQuad*>(quad));
    }
    call(field<uint8_t*>(menu, 0x184), 0x44);
    call(field<uint8_t*>(menu, 0x188), 0x44);
    call(field<uint8_t*>(menu, 0x18c), 0x44);
    call(field<uint8_t*>(menu, 0x190), 0x44);
}

PROFILE_GAP_ENTRY(CreateProfileMenu_ToggleCase_entry, "pushl %esi\n", CreateProfileMenu_ToggleCase, "8", "ret")
PROFILE_GAP_ENTRY(CreateProfileMenu_Backspace_entry, "pushl %esi\n", CreateProfileMenu_Backspace, "8", "ret")
PROFILE_GAP_ENTRY(CreateProfileMenu_AddChar_entry, "pushl %esi\n pushl %eax\n", CreateProfileMenu_AddChar, "12",
                  "ret")
PROFILE_GAP_ENTRY(CreateProfileMenu_Finish_entry, "pushl %esi\n", CreateProfileMenu_Finish, "8", "ret")
PROFILE_GAP_ENTRY(CreateProfileMenu_HandleSelect_entry, "pushl 4(%edx)\n pushl %ecx\n",
                  CreateProfileMenu_HandleSelect, "12", "ret $4")

void XB_CDECL CreateProfileMenu_ToggleCase(uint8_t* menu, uint32_t entry_esp) {
    // The one-character string lives in the push ecx slot ([esp+6] after push ebx): entry - 2.
    uint8_t* const text = stack_bytes(entry_esp - 2);
    const bool capitals = menu[0x1a8] != 0;
    text[1] = 0;
    AtTop at(entry_esp - 8);
    menu[0x1a8] = capitals ? 0 : 1;
    for (int32_t i = 10; i <= 0x23; ++i) {
        text[0] = static_cast<uint8_t>(i + (capitals ? 0x57 : 0x37));
        call(item(menu, i), 0x30, address_of(text));
    }
}

void XB_CDECL CreateProfileMenu_Backspace(uint8_t* menu, uint32_t entry_esp) {
    int32_t& length = field<int32_t>(menu, 0x1ac);
    if (length <= 0) return;
    --length;
    menu[0x198 + length] = '_';
    menu[0x199 + length] = 0;
    {
        AtTop at(entry_esp - 4);  // push ebx
        uint8_t* const text = find_text(field<uint8_t*>(menu, 0x38), kProfileName);
        call(text, 0x6c, address_of(menu + 0x198));
    }
    menu[0x198 + length] = 0;
    if (length < 15) {
        AtTop at(entry_esp - 4);  // push edi
        for (int32_t i = 0; i <= 0x27; ++i) set_hidden(menu, i, 0);
    }
    if (length != 0) return;
    AtTop at(entry_esp);
    set_hidden(menu, 38, 1);
    set_hidden(menu, 36, 1);
    set_hidden(menu, 39, 1);
}

void XB_CDECL CreateProfileMenu_AddChar(int32_t key, uint8_t* menu, uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push ebx
    set_hidden(menu, 38, 0);
    set_hidden(menu, 36, 0);
    set_hidden(menu, 39, 0);
    int32_t& length = field<int32_t>(menu, 0x1ac);
    if (length >= 15) return;
    uint8_t c = 'a';
    if (key <= 9)
        c = static_cast<uint8_t>(key + '0');
    else if (key <= 0x23)
        c = static_cast<uint8_t>(key + (menu[0x1a8] ? 0x37 : 0x57));
    else if (key == 0x26)
        c = ' ';
    menu[0x198 + length] = c;
    ++length;
    if (length < 15) {
        menu[0x198 + length] = '_';
        menu[0x199 + length] = 0;
    } else {
        menu[0x198 + length] = 0;
    }
    uint8_t* const text = find_text(field<uint8_t*>(menu, 0x38), kProfileName);
    call(text, 0x6c, address_of(menu + 0x198));
    menu[0x198 + length] = 0;
    if (length != 15) return;
    AtTop full(entry_esp - 8);  // push edi
    for (int32_t i = 0; i <= 0x23; ++i) set_hidden(menu, i, 1);
    set_hidden(menu, 37, 1);
    set_hidden(menu, 38, 1);
    call(menu, 0x88, 0x27, 1);
}

void XB_CDECL CreateProfileMenu_Finish(uint8_t* menu, uint32_t entry_esp) {
    for (int32_t i = field<int32_t>(menu, 0x1ac) - 1; i > 0 && menu[0x198 + i] == ' '; --i) menu[0x198 + i] = 0;
    for (int32_t i = 0; i < 16; ++i) title_global<uint8_t>(0x008dce30 + i) = menu[0x198 + i];
    {
        AtTop at(entry_esp);
        const uint32_t clear[1] = {0};
        title_call(0x00316320, TitleRegs{}, 1, clear);  // Movie_Stop
    }
    uint32_t opened;
    {
        AtTop at(entry_esp - 4);  // Movie_Stop's argument still pushed
        const uint32_t name[1] = {0x0082cacc};      // "Propgnda"
        opened = title_call(0x00332030, TitleRegs{}, 1, name);  // Movie_Open
    }
    AtTop at(entry_esp);
    if (opened & 0xff) {
        const uint32_t rect[5] = {address_of(title_global<uint8_t*>(0x0095f458)), 0, 0, 0x44200000, 0x43f00000};
        title_call(0x0005ded0, TitleRegs{}, 5, rect);  // nglSetQuadRect (0, 0) - (640, 480)
        title_global<uint8_t>(0x0095f91a) = 1;
    }
    if (title_global<uint8_t>(0x008dd1cf) == 1) {
        call(title_global<uint8_t*>(0x0095f8f8), 0xc, 1);
        return;
    }
    if (menu[0x34]) {
        menu[0x34] = 0;
        call(title_global<uint8_t*>(0x0095f8f8), 0xc, 1);
        return;
    }
    field<uint32_t>(front_end_screen(0x3c), 0x4c) = 3;
    call(field<uint8_t*>(menu, 8), 0xc, 0xf);
}

void XB_CDECL CreateProfileMenu_HandleSelect(uint8_t* menu, int32_t index, uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push esi
    title_call(0x0032d660, esi_regs(menu));  // UI_PlayTypeSound
    TitleRegs regs = esi_regs(menu);
    regs.eax = static_cast<uint32_t>(index);
    if (index <= 0x23) {
        title_call(0x00325980, regs);  // AddChar
        return;
    }
    regs.ecx = static_cast<uint32_t>(index - 0x24);
    switch (index) {
        case 0x24: title_call(0x003258c0, regs); break;  // Backspace
        case 0x25: title_call(0x0030d4c0, regs); break;  // ToggleCase
        case 0x26: title_call(0x00325980, regs); break;  // AddChar (space)
        case 0x27: title_call(0x00338b70, regs); break;  // Finish
        default: break;
    }
}

// --- Dialogs ---

PROFILE_GAP_ENTRY(AutosaveMenu_ShowSaveWarning_entry, "pushl %esi\n", AutosaveMenu_ShowSaveWarning, "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowSaveWarning_entry, "pushl %esi\n", ProfileMenu_ShowSaveWarning, "8", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowNoSaveDialog_entry, "pushl %eax\n", AutosaveMenu_ShowNoSaveDialog, "8", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowCorruptDialog_entry, "pushl %eax\n", AutosaveMenu_ShowCorruptDialog, "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowCorruptDialog_entry, "pushl %eax\n", ProfileMenu_ShowCorruptDialog, "8", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_OnSaveOpFailed_entry, "pushl %esi\n pushl %ecx\n pushl %eax\n",
                  AutosaveMenu_OnSaveOpFailed, "16", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_OnSaveOpFailed_entry, "pushl %esi\n pushl %ecx\n pushl %eax\n",
                  ProfileMenu_OnSaveOpFailed, "16", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_OnSaveSucceeded_entry, "pushl %eax\n", AutosaveMenu_OnSaveSucceeded, "8", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_StartProfileLoad_entry, "", AutosaveMenu_StartProfileLoad, "4", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_StartProfileLoad_entry, "", ProfileMenu_StartProfileLoad, "4", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowUnformattedDialog_entry, "", AutosaveMenu_ShowUnformattedDialog, "4", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowUnformattedDialog_entry, "", ProfileMenu_ShowUnformattedDialog, "4", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowNoSaveDataDialog_entry, "", AutosaveMenu_ShowNoSaveDataDialog, "4", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_CheckStorage_entry, "pushl %esi\n", AutosaveMenu_CheckStorage, "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_CheckStorage_entry, "pushl %esi\n", ProfileMenu_CheckStorage, "8", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_SelectSlot_entry, "pushl %ecx\n pushl %eax\n", AutosaveMenu_SelectSlot, "12", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_Update_entry, "pushl 4(%edx)\n pushl %ecx\n", AutosaveMenu_Update, "12", "ret $4")
PROFILE_GAP_ENTRY(ProfileMenu_Update_entry, "pushl 4(%edx)\n pushl %ecx\n", ProfileMenu_Update, "12", "ret $4")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowFormatSuccessDialog_entry, "", AutosaveMenu_ShowFormatSuccessDialog, "4", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowFormatSuccessDialog_entry, "", ProfileMenu_ShowFormatSuccessDialog, "4", "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_OnProfilesRead_entry, "pushl %ebx\n", AutosaveMenu_OnProfilesRead, "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_OnProfilesRead_entry, "pushl 4(%edx)\n", ProfileMenu_OnProfilesRead, "8", "ret $4")
PROFILE_GAP_ENTRY(ProfileMenu_ShowSaveErrorDialog_entry, "pushl %eax\n", ProfileMenu_ShowSaveErrorDialog, "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowSaveSuccessDialog_entry, "pushl %eax\n", ProfileMenu_ShowSaveSuccessDialog, "8",
                  "ret")
PROFILE_GAP_ENTRY(AutosaveMenu_ShowDeleteSuccessDialog_entry, "pushl %eax\n", AutosaveMenu_ShowDeleteSuccessDialog,
                  "8", "ret")
PROFILE_GAP_ENTRY(ProfileMenu_ShowDeleteSuccessDialog_entry, "", ProfileMenu_ShowDeleteSuccessDialog, "4", "ret")

uint32_t XB_CDECL AutosaveMenu_ShowSaveWarning(uint8_t* menu, uint32_t entry_esp) {
    return save_warning(menu, 0x50, field<int32_t>(menu, 0x4c), 0x0030cdf0, entry_esp);  // FE_SplitPlayTime2
}

uint32_t XB_CDECL ProfileMenu_ShowSaveWarning(uint8_t* menu, uint32_t entry_esp) {
    return save_warning(menu, 0x54, selection(menu), 0x0030d140, entry_esp);  // FE_SplitPlayTime
}

void XB_CDECL AutosaveMenu_ShowNoSaveDialog(uint8_t* menu, uint32_t entry_esp) {
    if (menu[0x34]) return;
    AtTop at(entry_esp - 8);
    show_dialog(kInsertCard);
    add_button_inline(kContinueNoSave, 0x003125d0);
    layout_buttons();
}

void XB_CDECL AutosaveMenu_ShowCorruptDialog(uint8_t* menu, uint32_t entry_esp) {
    menu[0x34] = 0;
    menu[0x35] = 1;
    corrupt_dialog(menu, 0x44, 0x0082ab84, 0x003336d0, entry_esp);  // XBOX_CORRUPT_DETECT
}

void XB_CDECL ProfileMenu_ShowCorruptDialog(uint8_t* menu, uint32_t entry_esp) {
    menu[0x36] = 1;
    menu[0x35] = 0;
    corrupt_dialog(menu, 0x48, 0x0082af10, 0x00334e50, entry_esp);  // MEM_XBOX_CORRUPT_DETECT
}

void XB_CDECL AutosaveMenu_OnSaveOpFailed(int32_t error, int32_t operation, uint8_t* menu, uint32_t entry_esp) {
    menu[0x84] = 0;
    menu[0x85] = 0;
    title_global<uint8_t>(0x008d50dd) = 1;
    const int32_t state = field<int32_t>(menu, 0x44);
    if (error == -12 || (error == -11 && state == 2)) {
        tail_call(entry_esp, 0x003337c0, eax_regs(address_of(menu)));  // AutosaveMenu_ShowCorruptDialog
        return;
    }
    if (state != 2 && operation != 4) {
        {
            AtTop at(entry_esp - 4);
            title_call(0x0030cf70, eax_regs(address_of(menu)));
        }
        field<int32_t>(menu, 0x44) = 1;
        tail_call(entry_esp, kDialogMgr_CloseDialog, eax_regs(address_of(dialog_mgr())));
        return;
    }
    AtTop at(entry_esp - 8);
    if (operation == 1 && field<int32_t>(menu, 0x48) != 3) {
        show_dialog(0x0082af94);                 // MEM_XBOX_LOAD_FAILED
        add_button(0x0082af7c, 0x003125d0);      // MEM_CONTINUE_WO_LOADING
    } else if (operation == 4) {
        show_dialog(0x0082b050);                 // MEM_FORMAT_FAILED
        add_button(0x0082b034, 0x003125d0);      // MEM_CONTINUE_WO_FORMATING
    } else {
        show_dialog(menu[0x50] ? 0x0082af40 : 0x0082af28);  // MEM_XBOX_OVERWRITE_FAILED / MEM_XBOX_SAVE_FAILED
        add_button(kContinueNoSave, 0x003125d0);
        add_button(0x0082af70, 0x0030cef0);  // MEM_RETRY
    }
    layout_buttons();
    menu[0x34] = 1;
}

void XB_CDECL ProfileMenu_OnSaveOpFailed(int32_t error, int32_t operation, uint8_t* menu, uint32_t entry_esp) {
    menu[0x95] = 0;
    menu[0x96] = 0;
    title_global<uint8_t>(0x008d50dd) = 1;
    const int32_t state = field<int32_t>(menu, 0x48);
    if (error == -12 || error == -1 || (error == -11 && state == 2)) {
        tail_call(entry_esp, 0x00334f40, eax_regs(address_of(menu)));  // ProfileMenu_ShowCorruptDialog
        return;
    }
    if (state != 2 && operation != 4) {
        {
            AtTop at(entry_esp - 4);
            title_call(0x00312850, eax_regs(address_of(menu)));
        }
        field<int32_t>(menu, 0x48) = 1;
        tail_call(entry_esp, kDialogMgr_CloseDialog, eax_regs(address_of(dialog_mgr())));
        return;
    }
    AtTop at(entry_esp - 4);  // push ebx
    uint32_t label = kContinueNoSave, handler = 0x0031b250;
    bool save = false;
    if (operation == 1) {
        const int32_t mode = field<int32_t>(menu, 0x4c);
        if (mode == 3) {
            save = true;
        } else {
            show_dialog(0x0082af94);  // MEM_XBOX_LOAD_FAILED
            if (mode == 1) {
                label = 0x0082af70;  // MEM_RETRY
                handler = 0x00312800;
            } else {
                label = 0x0082af7c;  // MEM_CONTINUE_WO_LOADING
            }
        }
    } else if (operation == 2) {
        if (!menu[0x34]) {
            save = true;
        } else {
            menu[0x34] = 0;
            show_dialog(0x0082af5c);  // MEM_DELETE_FAILED
            label = kDialogOk;
            handler = 0x0033a090;
        }
    } else if (operation == 4) {
        show_dialog(0x0082b050);  // MEM_FORMAT_FAILED
        label = 0x0082b034;      // MEM_CONTINUE_WO_FORMATING
    } else {
        save = true;
    }
    if (save) show_dialog(menu[0x54] ? 0x0082af40 : 0x0082af28);
    add_button(label, handler);
    layout_buttons_mgr();
    menu[0x35] = 1;
    menu[0x38] = 1;
}

void XB_CDECL AutosaveMenu_OnSaveSucceeded(uint8_t* menu, uint32_t entry_esp) {
    if (!menu[0x36]) {
        title_global<uint8_t>(0x0095ee08) = field<int32_t>(in_game_screen(0x1c), 0x48) == 4 ? 1 : 0;
        tail_call(entry_esp, 0x002b1eb0, eax_regs(0x0082c894));  // Cmd_ExecuteRegisteredCommand("reboot")
        return;
    }
    const uint32_t name = menu[0x50] ? 0x0082aff0 : 0x0082afdc;  // MEM_OVERWRITE_SUCCESS / MEM_SAVE_SUCCESS
    AtTop at(entry_esp - 8);
    one_button_dialog(name, kDialogOk, 0x00312580);
}

void XB_CDECL AutosaveMenu_StartProfileLoad(uint32_t entry_esp) { start_profile_load(entry_esp); }

void XB_CDECL ProfileMenu_StartProfileLoad(uint32_t entry_esp) { start_profile_load(entry_esp); }

void XB_CDECL AutosaveMenu_ShowUnformattedDialog(uint32_t entry_esp) {
    unformatted_dialog(0x0030cee0, 0x003125d0, entry_esp);
}

void XB_CDECL ProfileMenu_ShowUnformattedDialog(uint32_t entry_esp) {
    unformatted_dialog(0x0030d2e0, 0x0031b250, entry_esp);
}

void XB_CDECL AutosaveMenu_ShowNoSaveDataDialog(uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    one_button_dialog(0x0082b094, kDialogOk, 0x003125d0);  // MEM_XBOX_ERROR_NO_SAVE_DATA
}

void XB_CDECL AutosaveMenu_CheckStorage(uint8_t* menu, uint32_t entry_esp) {
    const bool unformatted = read_free_space(menu, entry_esp, 0x84, 0x85, 0x38, 0x52);
    AtTop at(entry_esp - 0x24);
    const TitleRegs regs = esi_regs(menu);
    int32_t mode = field<int32_t>(menu, 0x48);
    if (unformatted) {
        if (mode == 2) {
            title_call(0x00333d00, regs);  // AutosaveMenu_ShowNoSaveDataDialog
            return;
        }
        if (mode == 0) {
            title_call(0x00333c10, regs);  // AutosaveMenu_ShowUnformattedDialog
            return;
        }
        if (mode != 1) return;
    } else {
        if (title_call(kSave_ProfilesDirExists, regs) & 0xff) {
            title_call(0x00333bb0, regs);  // AutosaveMenu_StartProfileLoad
            return;
        }
        mode = field<int32_t>(menu, 0x48);
        if (mode == 2) {
            menu[0x38] = 1;
            title_call(0x00333d00, regs);  // AutosaveMenu_ShowNoSaveDataDialog
            return;
        }
        if (mode != 1 && mode != 0) return;
    }
    title_call(0x0030cf70, eax_regs(address_of(menu)));
    field<int32_t>(menu, 0x44) = 1;
}

void XB_CDECL ProfileMenu_CheckStorage(uint8_t* menu, uint32_t entry_esp) {
    const bool unformatted = read_free_space(menu, entry_esp, 0x95, 0x96, 0x37, 0x61);
    AtTop at(entry_esp - 0x24);
    const TitleRegs regs = esi_regs(menu);
    int32_t mode = field<int32_t>(menu, 0x4c);
    if (unformatted) {
        if (mode == 2) {
            title_call(0x003352d0, regs);
            return;
        }
        if (mode == 0 || mode == 3) {
            menu[0x37] = 1;
            title_call(0x003351e0, regs);  // ProfileMenu_ShowUnformattedDialog
            return;
        }
        if (mode != 1) return;
    } else {
        if (!title_global<uint8_t>(0x008dd1d0) && !(title_call(0x00335460, regs) & 0xff)) return;  // CheckFreeSpace2
        if (title_call(kSave_ProfilesDirExists, regs) & 0xff) {
            title_call(0x00335180, regs);  // ProfileMenu_StartProfileLoad
            return;
        }
        menu[0x39] = 0;
        if (field<int32_t>(menu, 0x48) == 2 && field<int32_t>(menu, 0x4c) == 3) {
            menu[0x34] = 0;
            TitleRegs select = regs;
            select.eax = field<uint32_t>(menu, 0x50);
            const uint32_t args[1] = {address_of(menu)};
            title_call(kFE_ProfileSelect, select, 1, args);
            return;
        }
        mode = field<int32_t>(menu, 0x4c);
        if (mode == 2) {
            title_call(0x003352d0, regs);
            return;
        }
        if (mode != 0 && mode != 3 && mode != 1) return;
    }
    title_call(0x00312850, eax_regs(address_of(menu)));
    field<int32_t>(menu, 0x48) = 1;
}

uint32_t XB_CDECL AutosaveMenu_SelectSlot(int32_t slot, uint8_t* menu, uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    TitleRegs regs = esi_regs(menu);
    regs.edi = static_cast<uint32_t>(slot);
    const uint32_t room = title_call(0x00333e20, regs);  // MemDlg_CheckFreeSpace3
    if (!(room & 0xff)) return room;
    field<int32_t>(saves(), 0x11ec) = slot;
    field<int32_t>(menu, 0x4c) = slot;
    if (!menu[0x36] && !menu[0x37]) {
        menu[0x50] = 0;
        return title_call(0x003334b0, regs);  // AutosaveMenu_ShowSaveWarning
    }
    menu[0x50] = 1;
    show_dialog(0x0082ae6c);  // MEM_XBOX_OVERWRITE_CONFIRM
    add_button(kDialogYes, 0x003335a0);
    add_button(kDialogNo, 0x003125d0);
    select_no();
    return layout_buttons();
}

void XB_CDECL AutosaveMenu_Update(uint8_t* menu, uint32_t dt, uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push esi
    const TitleRegs regs = esi_regs(menu);
    title_call(kSave_Update, regs);
    if (menu[0x84]) {
        menu[0x84] = 0;
        title_call(0x00333640, eax_regs(address_of(menu)));  // AutosaveMenu_ShowNoSaveDialog
    }
    if (menu[0x85]) {
        const uint8_t no_data = menu[0x38];
        menu[0x85] = 0;
        const int32_t state = field<int32_t>(menu, 0x44);
        if (no_data || state == 1) {
            call(menu, 0x40);
        } else if (state == 2) {
            title_call(0x00333ee0, regs);  // AutosaveMenu_CheckStorage
        } else {
            const int32_t mode = field<int32_t>(menu, 0x48);
            if (mode == 0 || mode == 2) call(menu, 0x40);
        }
    }
    if (title_global<uint8_t>(0x0095f91e)) return;
    TitleRegs update{};
    update.ecx = address_of(menu);
    update.esi = address_of(menu);
    const uint32_t args[1] = {dt};
    title_call(kUIMenu_Update, update, 1, args);
}

namespace {
// now > t + 0.5 (0x0083ecb0, a double) on the x87: FLD, FADD QWORD, FCOMIP.
bool profiles_read_due(uint32_t now_word, uint32_t stamp_word) {
    float now, stamp;
    __builtin_memcpy(&now, &now_word, 4);
    __builtin_memcpy(&stamp, &stamp_word, 4);
    const long double due = static_cast<long double>(stamp) + static_cast<long double>(title_global<double>(0x0083ecb0));
    return static_cast<long double>(now) > due;
}
}  // namespace

void XB_CDECL ProfileMenu_Update(uint8_t* menu, uint32_t dt, uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push esi
    const TitleRegs regs = esi_regs(menu);
    title_call(0x00331fb0, regs);  // SCR_LoadingPump
    title_call(kSave_Update, regs);
    uint32_t help = 0x0082c37c;  // FEMENU_OP_HELPBAR
    if (field<int32_t>(menu, 0x4c) == 0 && menu[(selection(menu) + 9) * 0xc] != 1) help = 0x0082b10c;
    call(field<uint8_t*>(menu, 0x40), 0x6c, help);  // FEMENU_LOAD_HELPBAR_XBOX otherwise
    if (menu[0x95] || menu[0x96]) {
        menu[0x97] = 0;
    } else if (menu[0x97] && profiles_read_due(now_bits(), field<uint32_t>(menu, 0x98))) {
        const uint32_t args[1] = {address_of(menu)};
        title_call(0x0033a2a0, regs, 1, args);  // ProfileMenu_OnProfilesRead
    }
    if (menu[0x95]) {
        menu[0x95] = 0;
        title_call(0x00334ca0, eax_regs(address_of(menu)));  // ProfileMenu_ShowSaveErrorDialog
    }
    if (menu[0x96]) {
        const uint8_t unformatted = menu[0x37];
        menu[0x96] = 0;
        const int32_t state = field<int32_t>(menu, 0x48);
        if (unformatted || state == 1) {
            call(menu, 0x40);
        } else if (state == 2) {
            title_call(0x00338960, regs);  // ProfileMenu_CheckStorage
        } else {
            const int32_t mode = field<int32_t>(menu, 0x4c);
            if (mode == 0 || mode == 2 || mode == 1) call(menu, 0x40);
        }
    }
    if (title_global<uint8_t>(0x0095f91e)) return;
    TitleRegs update{};
    update.ecx = address_of(menu);
    update.esi = address_of(menu);
    const uint32_t args[1] = {dt};
    title_call(kUIMenu_Update, update, 1, args);
}

void XB_CDECL AutosaveMenu_ShowFormatSuccessDialog(uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    one_button_dialog(0x0082afc8, kDialogOk, 0x003380d0);  // MEM_FORMAT_SUCCESS
}

void XB_CDECL ProfileMenu_ShowFormatSuccessDialog(uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push edi
    show_dialog(0x0082afc8);  // MEM_FORMAT_SUCCESS
    uint8_t* const dialog = current_dialog();
    field<uint32_t>(dialog, 0x54) = title_global<uint32_t>(0x0083eba8);
    field<uint32_t>(dialog, 0x48) = 0x003388c0;
    layout_buttons();
}

void XB_CDECL AutosaveMenu_OnProfilesRead(uint8_t* menu, uint32_t entry_esp) {
    if (menu[0x51]) {
        menu[0x51] = 0;
        TitleRegs regs{};
        regs.eax = field<uint32_t>(menu, 0x4c);
        regs.ecx = address_of(menu);
        regs.ebx = address_of(menu);
        tail_call(entry_esp, 0x00337f00, regs);  // AutosaveMenu_SelectSlot
        return;
    }
    title_global<uint8_t>(0x008d50dd) = 1;
    menu[0x36] = 0;
    AtTop at(entry_esp - 12);  // push ebp, esi, edi
    for (int32_t i = 0; i < 4; ++i) {
        uint8_t* const s = saves();
        uint8_t* const row = menu + 0x54 + i * 0xc;
        if (s[0x11e7 + i]) {
            if (s[0x2a8 + i * 0x3cc]) {
                uint8_t* const profile = s + 0x2a8 + i * 0x3cc;
                if (title_global<uint8_t>(0x008dcf10) && i == title_global<int32_t>(0x008dce40) &&
                    field<uint32_t>(s, 0x38c + i * 0x3cc) != title_global<uint32_t>(0x008dcf14)) {
                    title_global<uint32_t>(0x008dcf14) = field<uint32_t>(s, 0x38c + i * 0x3cc);
                    menu[0x36] = 1;
                }
                field<uint8_t*>(row, 0) = profile;
                TitleRegs regs = eax_regs(address_of(profile));
                regs.ebx = address_of(menu);
                field<uint32_t>(row, 4) = title_call(0x0030cd70, regs);  // FE_FormatPlayTime
                row[8] = 0;
                session_start_bits() = now_bits();
                continue;
            }
            s[0x11e7 + i] = 0;
        }
        row[8] = 1;
    }
    if (title_global<uint8_t>(0x008dcf10)) {
        bool empty = true;
        for (int32_t i = 0; i < 4; ++i)
            if (menu[0x5c + i * 0xc] == 0) empty = false;
        if (empty) {
            title_global<uint32_t>(0x008dcf14) = 0;
            menu[0x36] = 1;
        }
    }
    TitleRegs regs = esi_regs(menu);
    regs.ebx = address_of(menu);
    title_call(0x0030ccb0, regs);
    title_call(kDialogMgr_CloseDialog, eax_regs(address_of(dialog_mgr())));
    uint8_t* const screen = in_game_screen(0x1c);
    if (!title_global<uint8_t>(0x008dcf10)) {
        field<int32_t>(screen, 0x44) = 1;
        return;
    }
    field<int32_t>(screen, 0x44) = 2;
    TitleRegs select{};
    select.eax = title_global<uint32_t>(0x008dce40);
    select.ecx = address_of(menu);
    select.ebx = address_of(menu);
    tail_call(entry_esp, 0x00337f00, select);  // AutosaveMenu_SelectSlot
}

void XB_CDECL ProfileMenu_OnProfilesRead(uint8_t* menu, uint32_t entry_esp) {
    if (!menu[0x97]) {
        menu[0x97] = 1;
        field<uint32_t>(menu, 0x98) = now_bits();
        title_global<uint8_t>(0x008d50dd) = 0;
        return;
    }
    menu[0x97] = 0;
    title_global<uint8_t>(0x008d50dd) = 1;
    if (field<int32_t>(menu, 0x4c) == 3 || menu[0x60]) {
        AtTop at(entry_esp - 8);  // push ecx, push edi
        menu[0x60] = 0;
        TitleRegs regs{};
        regs.eax = field<uint32_t>(menu, 0x50);
        regs.edi = address_of(menu);
        const uint32_t args[1] = {address_of(menu)};
        title_call(kFE_ProfileSelect, regs, 1, args);
        return;
    }
    AtTop at(entry_esp - 0x14);  // push ecx, edi, ebx, ebp, esi
    for (int32_t i = 0; i < 4; ++i) {
        uint8_t* const s = saves();
        uint8_t* const row = menu + 0x64 + i * 0xc;
        if (s[0x11e7 + i]) {
            if (s[0x2a8 + i * 0x3cc]) {
                uint8_t* const profile = s + 0x2a8 + i * 0x3cc;
                field<uint8_t*>(row, 0) = profile;
                TitleRegs regs = eax_regs(address_of(profile));
                regs.esi = address_of(profile);
                regs.edi = address_of(menu);
                regs.ebx = static_cast<uint32_t>(i);
                field<uint32_t>(row, 4) = title_call(0x0030d0b0, regs);  // FE_FormatPlayTime2
                row[8] = 0;
                session_start_bits() = now_bits();
                if (field<int32_t>(menu, 0x48) == 2 && i == field<int32_t>(menu, 0x50)) {
                    regs.eax = 0x008dce30;
                    regs.ecx = address_of(profile);
                    const uint32_t copy = title_call(0x00312130, regs);  // Profile_Copy
                    title_global<uint8_t>(0x008dcf10) = 1;
                    title_global<int32_t>(0x008dce40) = i;
                    regs.eax = copy;
                    regs.edi = copy;
                    title_call(0x00332e20, regs);  // CL_ApplyProfileSettings
                    regs.esi = copy;
                    title_call(0x003304f0, regs);  // Profile_ApplySkill
                }
                continue;
            }
            s[0x11e7 + i] = 0;
        }
        row[8] = 1;
    }
    TitleRegs regs = esi_regs(menu);
    regs.edi = address_of(menu);
    if (field<int32_t>(menu, 0x4c) == 2) {
        bool empty = true;
        for (int32_t i = 0; i < 4; ++i)
            if (menu[0x6c + i * 0xc] == 0) empty = false;
        if (empty) {
            title_call(0x003352d0, regs);
            return;
        }
    }
    title_call(0x00312690, regs);  // FE_SaveSlotsUpdateText
    const int32_t mode = field<int32_t>(menu, 0x4c);
    if (field<int32_t>(menu, 0x48) != 0 && (mode == 1 || mode == 2)) {
        menu[0x38] = 1;
        show_dialog(0x0082b008);  // MEM_LOAD_SUCCESS
        add_button(kDialogOk, 0x0030d1e0);
        layout_buttons();
        return;
    }
    title_call(kDialogMgr_CloseDialog, eax_regs(address_of(dialog_mgr())));
    field<int32_t>(front_end_screen(0x3c), 0x48) = 1;
}

void XB_CDECL ProfileMenu_ShowSaveErrorDialog(uint8_t* menu, uint32_t entry_esp) {
    if (menu[0x35]) return;
    AtTop at(entry_esp - 8);  // push esi, push edi
    const int32_t state = field<int32_t>(menu, 0x48);
    if (state == 1 || state == 2) {
        const int32_t mode = field<int32_t>(menu, 0x4c);
        show_dialog(kInsertCard);
        if (state == 1 && mode == 1) {
            add_button(kDialogOk, 0x00325870);
            layout_buttons();
            return;
        }
        if (mode == 2)
            add_button(kReturnToMain, 0x0030d2f0);
        else
            add_button(kContinueNoSave, 0x0031b250);
        layout_buttons_mgr();
        return;
    }
    const int32_t mode = field<int32_t>(menu, 0x4c);
    if (mode == 0 || mode == 2) {
        show_dialog(kInsertCard);
        add_button(kReturnToMain, 0x0030d2f0);
        layout_buttons();
    }
    if (field<int32_t>(menu, 0x4c) == 1) {
        show_dialog(kInsertCard);
        add_button(kDialogOk, 0x00325870);
        layout_buttons_mgr();
    }
}

void XB_CDECL ProfileMenu_ShowSaveSuccessDialog(uint8_t* menu, uint32_t entry_esp) {
    title_global<uint32_t>(0x008dce40) = field<uint32_t>(menu, 0x50);
    menu[0x38] = 1;
    const uint32_t name = menu[0x54] ? 0x0082aff0 : 0x0082afdc;  // MEM_OVERWRITE_SUCCESS / MEM_SAVE_SUCCESS
    AtTop at(entry_esp - 8);
    one_button_dialog(name, kDialogOk, 0x0030d240);
}

void XB_CDECL AutosaveMenu_ShowDeleteSuccessDialog(uint8_t* menu, uint32_t entry_esp) {
    menu[0x39] = 0;
    AtTop at(entry_esp - 8);
    one_button_dialog(0x0082afac, kDialogOk, 0x00338300);  // MEM_DELETE_SUCCESSFUL_XBOX
}

void XB_CDECL ProfileMenu_ShowDeleteSuccessDialog(uint32_t entry_esp) {
    front_end_screen(0x3c)[0x39] = 1;
    AtTop at(entry_esp - 8);
    one_button_dialog(0x0082afac, kDialogOk, 0x00339f20);  // MEM_DELETE_SUCCESSFUL_XBOX
}

}  // namespace game
