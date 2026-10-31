#include "fe_menus.h"

#include "cl.h"
#include "fe.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"
#include "ui_menu.h"

// Calls a title function with EAX, EBX, ECX, EDX, ESI and EDI from `regs` and `count` stack words (args[0] pushed
// last); returns its EAX. ESP comes back from EBP, so cdecl and stdcall callees both fit.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL fe_menus_call(uint32_t function, const game::TitleRegs* regs,
                                                          uint32_t count, const uint32_t* args);
PLATFORM_ASM_ENTRY(fe_menus_call,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %edx\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "pushl -4(%edx,%ecx,4)\n"
                   "decl %ecx\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %edx\n"
                   "movl 0(%edx), %eax\n"
                   "movl 4(%edx), %ebx\n"
                   "movl 8(%edx), %ecx\n"
                   "movl 16(%edx), %esi\n"
                   "movl 20(%edx), %edi\n"
                   "movl 12(%edx), %edx\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

// The text setters (UIText vtable +0x78 / +0x30 / +0x6c, the dialog text) leave the address of a stack buffer in a
// heap record, so the functions that call them run each title call at the original's ESP: their entries move the C
// frame below a gap and hand the C version the entry ESP (the original's call positions are counted from it).
#define FE_GAP_ENTRY(name, pushes, c_name, c_arg_bytes, ret)                                                     \
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
const char* text_at(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }
char* mutable_text_at(uint32_t address) { return reinterpret_cast<char*>(uintptr_t{address}); }
uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

constexpr uint32_t kEmptyString = 0x0082dfeb;  // ""
constexpr uint32_t kLoading = 0x0082bcf4;      // "FEMENU_LOADING"
constexpr uint32_t kZero = 0x0082e098;         // "0"
constexpr uint32_t kOne = 0x0082e058;          // "1"
constexpr uint32_t kReboot = 0x0082c894;       // "reboot"

// Where title calls run: 0 from the C frame, else at this ESP (the original's ESP before the call's pushes).
uint32_t g_top = 0;
struct AtTop {
    uint32_t saved;
    explicit AtTop(uint32_t top) : saved(g_top) { g_top = top; }
    ~AtTop() { g_top = saved; }
};

uint32_t title_call(uint32_t function, TitleRegs regs, uint32_t count = 0, const uint32_t* args = nullptr) {
    if (g_top) return title_call_at_regs(g_top, function, &regs, count, args);
    return fe_menus_call(function, &regs, count, args);
}

// A title object's vtable slot (byte offset), thiscall.
uint32_t slot(uint8_t* object, uint32_t offset) { return field<uint32_t>(field<uint8_t*>(object, 0), offset); }
void call(uint8_t* object, uint32_t offset) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    title_call(slot(object, offset), regs);
}
void call(uint8_t* object, uint32_t offset, uint32_t a) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    const uint32_t args[1] = {a};
    title_call(slot(object, offset), regs, 1, args);
}
void call(uint8_t* object, uint32_t offset, uint32_t a, uint32_t b) {
    TitleRegs regs{};
    regs.ecx = address_of(object);
    const uint32_t args[2] = {a, b};
    title_call(slot(object, offset), regs, 2, args);
}

constexpr uint32_t kZeroFloat = 0x00000000;
constexpr uint32_t kOneFloat = 0x3f800000;
void set_alpha(uint8_t* text, uint32_t alpha) { call(text, 0x7c, alpha); }

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

// Panel_FreeElements (EAX) and Panel_Draw (ESI; EDI = the menu).
void panel_free_elements(uint8_t* panel) {
    TitleRegs regs{};
    regs.eax = address_of(panel);
    title_call(0x00316620, regs);
}
void panel_draw(uint8_t* panel, uint8_t* menu) {
    TitleRegs regs{};
    regs.esi = address_of(panel);
    regs.edi = address_of(menu);
    title_call(0x00310010, regs);
}

// The destructors' shared part: the vtable, the panel +0x34 and its two aligned blocks.
void free_panel(uint8_t* menu, uint32_t vtable) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = vtable;
    if (!panel) return;
    panel_free_elements(panel);
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

// An owned object's deleting destructor (vtable +0x00 (1)).
void delete_owned(uint8_t* menu, uint32_t offset) {
    uint8_t* const object = field<uint8_t*>(menu, offset);
    if (object) call(object, 0, 1);
}

void menu_base_dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

// The dialog manager (0x0095f900) and its current dialog (+0x04 [+0x0c != 0]).
uint8_t* dialog_mgr() { return title_global<uint8_t*>(0x0095f900); }
uint8_t* current_dialog() {
    uint8_t* const mgr = dialog_mgr();
    return field<uint8_t**>(mgr, 4)[field<uint32_t>(mgr, 0xc) != 0 ? 1 : 0];
}

// UI_ShowDialog (by its title address, so it runs at the original's ESP) with no OK / YES-NO buttons, title "" and
// the game kept running.
void show_dialog(uint32_t name) {
    TitleRegs regs{};
    regs.eax = name;
    const uint32_t args[5] = {address_of(dialog_mgr()), 0, 0, kEmptyString, 1};
    title_call(0x003319e0, regs, 5, args);
}
// UI_DialogAddButton by its title address.
void add_button(uint32_t label, uint32_t handler) {
    TitleRegs regs{};
    regs.eax = address_of(dialog_mgr());
    const uint32_t args[2] = {label, handler};
    title_call(0x00012320, regs, 2, args);
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
// Dialog_LayoutButtons (AL = 1, EDI = the dialog; EAX's upper bytes from the manager pointer).
void layout_buttons() {
    TitleRegs regs{};
    regs.eax = (address_of(dialog_mgr()) & ~0xffu) | 1;
    regs.edi = address_of(current_dialog());
    title_call(0x0031f4b0, regs);
}

// A yes / no dialog with NO selected (the pause and Live dialogs: frame of two pushes).
void yes_no_dialog(uint32_t entry_esp, uint32_t name, uint32_t yes, uint32_t yes_handler, uint32_t no,
                   uint32_t no_handler, bool mark_live) {
    AtTop at(entry_esp - 8);
    show_dialog(name);
    add_button_inline(yes, yes_handler);
    add_button_inline(no, no_handler);
    if (mark_live) {
        uint8_t* const live = field<uint8_t*>(dialog_mgr(), 0x1c);
        if (live) live[0x45] = 1;
    }
    select_no();
    layout_buttons();
}

// Loading screens.
struct LoadingLayout {
    uint32_t missions;        // texts from +0x48
    uint32_t chapters;        // chapter texts
    uint32_t chapter_offset;  // first chapter text
    uint32_t title_offset;    // the title text
    uint32_t hidden_offset;   // the text Draw hides
    uint32_t show_missions;   // shows the missions up to EAX + 1 (EDI = menu, BL = 1)
};
constexpr LoadingLayout kLoading9 = {0x2a, 5, 0xf4, 0x10c, 0xf0, 0x00313610};
constexpr LoadingLayout kLoadingAfrica = {0x30, 5, 0x10c, 0x124, 0x108, 0x00313360};
constexpr LoadingLayout kLoadingSicily = {0x13, 3, 0x98, 0xa8, 0x94, 0x00313120};

// ClearElements with its entry ESP (two pushes before the calls).
void clear_elements(uint8_t* menu, const LoadingLayout& layout, uint32_t entry_esp) {
    AtTop at(entry_esp - 8);
    for (uint32_t i = 0; i < layout.missions; ++i) set_alpha(field<uint8_t*>(menu, 0x48 + i * 4), kZeroFloat);
    for (uint32_t i = 0; i < layout.chapters; ++i)
        set_alpha(field<uint8_t*>(menu, layout.chapter_offset + i * 4), kZeroFloat);
    call(field<uint8_t*>(menu, layout.title_offset), 0x78, kEmptyString);
}

void loading_draw(uint8_t* menu, const LoadingLayout& layout) {
    set_alpha(field<uint8_t*>(menu, layout.hidden_offset), kZeroFloat);
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) panel_draw(panel, menu);
    UIMenu_Draw(menu);
    call(field<uint8_t*>(menu, layout.title_offset), 0x3c);
    for (uint32_t i = 0; i < layout.missions; ++i) call(field<uint8_t*>(menu, 0x4c), 0x44);
    for (uint32_t i = 0; i < layout.chapters; ++i) call(field<uint8_t*>(menu, layout.chapter_offset + 4), 0x44);
}

// A chapter's missions: first (+0x3c) and last.
struct MissionRange {
    int32_t chapter, first, last;
};

// `open_last`: the last row also takes every later chapter. `entry_esp`: run the mission list call at the
// original's ESP (two pushes), or 0.
void set_mission_range(uint8_t* menu, const MissionRange* ranges, int32_t count, bool open_last,
                       const LoadingLayout& layout, uint32_t entry_esp) {
    const int32_t chapter = title_global<int32_t>(0x008dce48);  // g_currentChapter
    int32_t last = 0;
    for (int32_t i = 0; i < count; ++i) {
        const bool hit = (open_last && i == count - 1) ? chapter >= ranges[i].chapter : chapter == ranges[i].chapter;
        if (hit) {
            field<int32_t>(menu, 0x3c) = ranges[i].first;
            last = ranges[i].last;
            break;
        }
    }
    const int32_t first = field<int32_t>(menu, 0x3c);
    field<int32_t>(menu, 0x40) = last - first + 1;
    if (first < 1) return;
    AtTop at(entry_esp ? entry_esp - 8 : 0);
    TitleRegs regs{};
    regs.eax = uint32_t(first - 1);
    regs.ebx = 1;
    regs.edi = address_of(menu);
    title_call(layout.show_missions, regs);
}

constexpr MissionRange kRanges9[] = {{9, 0, 10}, {10, 11, 26}, {11, 27, 32}, {12, 33, 38}, {13, 39, 41}};
constexpr MissionRange kRangesAfrica[] = {{1, 0, 12}, {2, 13, 29}, {3, 30, 34}, {4, 35, 38}, {5, 39, 47}};
constexpr MissionRange kRangesSicily[] = {{6, 0, 5}, {7, 6, 11}, {8, 12, 18}};

// RestoreSelection (push ebx): UIMenu_RestoreSelection (by its title address), the fields, ClearElements (its entry
// at entry - 8), the title's label, then SetMissionRange as the tail call (entry ESP = this entry's).
void loading_restore_selection(uint8_t* menu, const LoadingLayout& layout, const MissionRange* ranges,
                               int32_t count, bool open_last, uint32_t entry_esp) {
    {
        AtTop at(entry_esp - 4);
        TitleRegs regs{};
        regs.ecx = address_of(menu);
        title_call(0x00314b20, regs);  // UIMenu_RestoreSelection
    }
    menu[0x38] = 0;
    field<int32_t>(menu, 0x40) = 0;
    field<int32_t>(menu, 0x3c) = 0;
    clear_elements(menu, layout, entry_esp - 8);
    {
        AtTop at(entry_esp - 4);
        call(field<uint8_t*>(menu, layout.title_offset), 0x6c, kLoading);
    }
    set_mission_range(menu, ranges, count, open_last, layout, entry_esp);
}

}  // namespace

// --- Destructors ---

void XB_REGS1 XB_CDECL PauseMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x008289f8);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL LoadingMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x00829180);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL LoadAfricaMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x0082a1f8);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL LoadSicilyMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x00828718);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL LoadMaubeugeMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x008295a0);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL MissionSuccessFailMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x00828d18);
    delete_owned(menu, 0x38);
    delete_owned(menu, 0x3c);
    menu_base_dtor(menu);
}

void XB_REGS1 XB_CDECL InGameLiveOptionsMenu_Dtor(uint8_t* menu) {
    free_panel(menu, 0x0083c8e0);
    delete_owned(menu, 0x38);
    menu_base_dtor(menu);
}

uint8_t* XB_THISCALL PauseMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    PauseMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL LoadingMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    LoadingMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL LoadAfricaMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    LoadAfricaMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL LoadSicilyMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    LoadSicilyMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL LoadMaubeugeMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    LoadMaubeugeMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL MissionSuccessFailMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    MissionSuccessFailMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL InGameLiveOptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    InGameLiveOptionsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

// --- Constructors ---

uint8_t* XB_REGS3 XB_CDECL InGameLiveOptionsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 3, owner, 0x140, 0xf0, 8, 0);
    menu[0x40] = 0;
    field<uint32_t>(menu, 0x44) = 0;
    menu[0x48] = 0;
    menu[0x49] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0) = 0x0083c8e0;  // vtable
    field<int32_t>(menu, 0x3c) = -1;
    menu[0x33] = 9;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL MissionSuccessFailMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 3, owner, 0x140, 0xf0, 8, 0);
    NglQuad* const quad = reinterpret_cast<NglQuad*>(menu + 0x60);
    field<uint32_t>(menu, 0) = 0x00828d18;  // vtable
    menu[0x33] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x58) = 0;
    menu[0x5c] = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    menu[0x54] = 0;
    menu[0x5d] = 0;
    menu[0x55] = 1;
    nglInitQuad(quad);
    nglSetQuadRect(quad, 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadZ(quad, 300.0f);
    nglSetQuadColor(quad, 0xff000000);
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL LoadMaubeugeMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 0, owner, 0x140, 0xf0, 8, 0);
    menu[0x33] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    menu[0x38] = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<uint32_t>(menu, 0) = 0x008295a0;  // vtable
    return menu;
}

// --- Pause menu ---

void XB_THISCALL PauseMenu_Draw(uint8_t* menu) {
    if (uint8_t* const a = field<uint8_t*>(menu, 0x38)) call(a, 0x3c);
    if (uint8_t* const b = field<uint8_t*>(menu, 0x3c)) call(b, 0x3c);
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) panel_draw(panel, menu);
    UIMenu_Draw(menu);
}

void XB_THISCALL PauseMenu_OnDown(uint8_t* menu, uint32_t) {
    const int32_t old = field<int16_t>(menu, 0x22);
    call(menu, 0xb4);
    if (old != 4) return;
    call(menu, 0x88, 0, 1);
    call(menu, 0x10);
}

void XB_THISCALL PauseMenu_OnUp(uint8_t* menu, uint32_t) {
    const int32_t old = field<int16_t>(menu, 0x22);
    call(menu, 0xb0);
    if (old != 0) return;
    call(menu, 0x88, 4, 1);
    call(menu, 0x10);
}

FE_GAP_ENTRY(PauseMenu_ShowRestartDialog_entry, "", PauseMenu_ShowRestartDialog, "4", "ret")
FE_GAP_ENTRY(PauseMenu_ShowQuitDialog_entry, "", PauseMenu_ShowQuitDialog, "4", "ret")

void XB_CDECL PauseMenu_ShowRestartDialog(uint32_t entry_esp) {
    // MEM_RESTART_WARNING, MEM_DIALOG_YES, MEM_DIALOG_NO
    yes_no_dialog(entry_esp, 0x0082c880, 0x0082c8b8, 0x0032dc70, 0x0082c8a8, 0x0030e6a0, false);
}

void XB_CDECL PauseMenu_ShowQuitDialog(uint32_t entry_esp) {
    // MEM_QUIT_WARNING, MEM_DIALOG_YES, MEM_DIALOG_NO
    yes_no_dialog(entry_esp, 0x0082c8c8, 0x0082c8b8, 0x0032dd00, 0x0082c8a8, 0x0030e6a0, false);
}

// --- In-game Live options ---

void XB_THISCALL InGameLiveOptionsMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) panel_draw(panel, menu);
    if (uint8_t* const a = field<uint8_t*>(menu, 0x38)) call(a, 0x3c);
    if (!title_global<uint8_t>(0x0095f916) && title_global<uint8_t*>(0x0095f454)) {
        call(title_global<uint8_t*>(0x0095f454), 8);
        Subtitle_Draw();
        uint8_t* const texture =
            title_function<uint8_t* XB_THISCALL(uint8_t*)>(0x000cf100)(title_global<uint8_t*>(0x0095f454));
        uint8_t* const quad = title_global<uint8_t*>(0x0095f458);
        field<uint8_t*>(quad, 0x60) = texture;
        nglListAddQuad(reinterpret_cast<const NglQuad*>(quad));
    }
    UIMenu_Draw(menu);
}

FE_GAP_ENTRY(InGameLiveOptionsMenu_ShowConfirmDialog_entry, "", InGameLiveOptionsMenu_ShowConfirmDialog, "4", "ret")
FE_GAP_ENTRY(InGameLiveOptionsMenu_ShowDashboardDialog_entry, "", InGameLiveOptionsMenu_ShowDashboardDialog, "4",
             "ret")

void XB_CDECL InGameLiveOptionsMenu_ShowConfirmDialog(uint32_t entry_esp) {
    // MEM_CONFIRM_FRIEND_JOIN, INGAME_DIALOG_YES, INGAME_DIALOG_NO
    yes_no_dialog(entry_esp, 0x0083c9a4, 0x0082c9d8, 0x0002bd10, 0x0082c9c4, 0x00224220, true);
}

void XB_CDECL InGameLiveOptionsMenu_ShowDashboardDialog(uint32_t entry_esp) {
    // MEM_BOOT_DASH, INGAME_DIALOG_YES, INGAME_DIALOG_NO
    yes_no_dialog(entry_esp, 0x0083c9bc, 0x0082c9d8, 0x002226c0, 0x0082c9c4, 0x00223a20, true);
    title_global<uint8_t*>(0x0095f6fc)[0x44cb] = 0;  // g_liveMgr
}

// --- Mission success / fail ---

PLATFORM_ASM_ENTRY(MissionSuccessFailMenu_ClearTexts_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(MissionSuccessFailMenu_ClearTexts) "addl $4, %esp\n"
                   "ret")

void XB_CDECL MissionSuccessFailMenu_ClearTexts(uint8_t* menu) {
    call(field<uint8_t*>(menu, 0x40), 0x78, 0x0082dfeb);  // ""
    call(field<uint8_t*>(menu, 0x44), 0x78, 0x0082dfeb);
}

PLATFORM_ASM_ENTRY(MissionSuccessFailMenu_SetFailedText_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(MissionSuccessFailMenu_SetFailedText) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL MissionSuccessFailMenu_SetFailedText(uint8_t* menu, uint32_t text) {
    call(field<uint8_t*>(menu, 0x40), 0x6c, 0x0082ad18);  // "INGAME_MISSION_FAILED"
    call(field<uint8_t*>(menu, 0x44), 0x6c, text);
}

uint8_t XB_CDECL MissionSuccessFailMenu_OnRetryYes() {
    title_global<uint8_t>(0x0095ee08) = 1;
    Cvar_Set2(text_at(0x0082c89c), mutable_text_at(kOne), 1);  // g_reloading
    Cmd_ExecuteRegisteredCommand(text_at(kReboot));
    return 0;
}

FE_GAP_ENTRY(MissionSuccessFailMenu_ShowRetryDialog_entry, "pushl 4(%edx)\n", MissionSuccessFailMenu_ShowRetryDialog,
             "8", "ret $4")

void XB_CDECL MissionSuccessFailMenu_ShowRetryDialog(uint8_t* menu, uint32_t entry_esp) {
    constexpr uint32_t kYes = 0x0082c8b8, kNo = 0x0082c8a8;  // MEM_DIALOG_YES / _NO
    constexpr uint32_t kOnRetryYes = 0x0032d340, kOnNo = 0x0030cf90;
    AtTop at(entry_esp - 12);  // three pushes
    if (title_global<uint8_t>(0x008dd1cf) == 1) {
        show_dialog(0x0082c8c8);  // MEM_QUIT_WARNING
        add_button_inline(kYes, kOnRetryYes);
        add_button_inline(kNo, kOnNo);
    } else if (field<uint32_t>(menu, 0x58) == 0) {
        show_dialog(0x0082abcc);       // MEM_SAVE_QUIT
        add_button(kYes, 0x003307a0);  // MissionSuccessFailMenu_ReloadCheckpoint
        add_button(kNo, kOnRetryYes);
    } else {
        show_dialog(0x0082c8c8);  // MEM_QUIT_WARNING
        add_button(kYes, kOnRetryYes);
        add_button(kNo, kOnNo);
    }
    select_no();
    layout_buttons();
}

FE_GAP_ENTRY(MissionSuccessFailMenu_HandleSelect_entry, "pushl 4(%edx)\n pushl %ecx\n",
             MissionSuccessFailMenu_HandleSelect, "12", "ret $4")

void XB_CDECL MissionSuccessFailMenu_HandleSelect(uint8_t* menu, int32_t index, uint32_t entry_esp) {
    AtTop at(entry_esp - 8);  // two pushes
    const int32_t state = field<int32_t>(menu, 0x58);
    TitleRegs sound{};
    sound.eax = title_global<uint32_t>(0x0095f4c4);
    if (state == 2) {
        if (!menu[0x5c]) return;
        title_global<int32_t>(0x008d9d48) = 1;
        menu[0x54] = 0;
        Cvar_Set2(text_at(0x0082ca00), mutable_text_at(kZero), 1);  // cl_paused
        Cbuf_AddText(text_at(0x0082bc74));                           // "fade 0 0 0 0 3\n"
        Cvar_Set2(text_at(0x0082bc68), mutable_text_at(kZero), 1);  // cg_norender
        title_global<uint8_t>(0x0095f919) = 0;
        sound.eax = title_global<uint32_t>(0x0095f4c4);
        title_call(0x002c8d30, sound);  // stops the idle channels
        sound.eax = title_global<uint32_t>(0x0095f4c4);
        title_call(0x002b6ba0, sound);              // S_ResumeAllChannels
        title_global<uint32_t>(0x0092bdf4) &= ~2u;  // cls_keyCatchers
        Key_ClearStates();
        return;
    }
    if (menu[0x5d]) {
        call(field<uint8_t*>(menu, 0x4c), 0x78, kEmptyString);
        menu[0x5d] = 0;
        if (index == 0)
            title_call(0x003307a0, TitleRegs{});  // MissionSuccessFailMenu_ReloadCheckpoint
        else if (index == 1)
            title_call(0x0032d340, TitleRegs{});  // MissionSuccessFailMenu_OnRetryYes
        return;
    }
    if (index == 2 || (index == 1 && (state == 0 || !title_global<uint8_t*>(0x0095f4e0)[0x390]))) {
        const uint32_t args[1] = {address_of(menu)};
        title_call(0x00334070, TitleRegs{}, 1, args);  // MissionSuccessFailMenu_ShowRetryDialog
        return;
    }
    if (index == 1) {
        show_dialog(0x0082c880);             // MEM_RESTART_WARNING
        add_button(0x0082c8b8, 0x0032d370);  // MEM_DIALOG_YES
        add_button(0x0082c8a8, 0x0030cf90);  // MEM_DIALOG_NO
        TitleRegs regs{};
        regs.eax = address_of(dialog_mgr());
        const uint32_t one[1] = {1};
        title_call(0x00012370, regs, 1, one);  // UI_DialogSetDefaultButton
        // The tail jump to UI_DialogLayoutButtons: flag 1 in this function's argument slot, run at the entry ESP
        // (its return address is this function's).
        const uint32_t return_address = stack_word(entry_esp);
        stack_word(entry_esp + 4) = 1;
        regs.eax = address_of(dialog_mgr());
        AtTop tail(entry_esp + 4);
        title_call(0x00012390, regs);
        stack_word(entry_esp) = return_address;
        return;
    }
    if (index != 0) return;
    if (state == 0) {
        title_global<int32_t>(0x008d9d48) = 1;
        if (title_global<uint8_t>(0x008dd1cf) == 1) {
            title_global<uint8_t>(0x0095ee08) = 0;
            Cmd_ExecuteRegisteredCommand(text_at(kReboot));
            return;
        }
        uint8_t* const screens = title_global<uint8_t*>(0x0095f8fc);
        field<uint32_t>(field<uint8_t*>(field<uint8_t*>(screens, 4), 0x1c), 0x48) = 0;
        // 0x0032de50 (manager in ESI, ret 4): cl_paused 1, 0x0095f919 = 1, then the manager's +0x0c (7).
        TitleRegs regs{};
        regs.esi = address_of(title_global<uint8_t*>(0x0095f8fc));
        const uint32_t args[1] = {7};
        title_call(0x0032de50, regs, 1, args);
        return;
    }
    field<int32_t>(menu, 0x58) = 2;
    call(field<uint8_t**>(menu, 4)[0], 0x30, 0x0082ad04);  // FEMENU_RELOADING
    call(field<uint8_t**>(menu, 4)[1], 0x34, kEmptyString);
    call(field<uint8_t**>(menu, 4)[2], 0x34, kEmptyString);
    call(field<uint8_t**>(menu, 4)[2], 0x20, 1);
    call(field<uint8_t*>(menu, 0x38), 0x78, kEmptyString);
    // 0x0030cfb0 (menu in ESI): the texts +0x40 and +0x44 set to "".
    TitleRegs regs{};
    regs.esi = address_of(menu);
    title_call(0x0030cfb0, regs);
    sound.eax = title_global<uint32_t>(0x0095f4c4);
    title_call(0x002b6ba0, sound);                               // S_ResumeAllChannels
    Cvar_Set2(text_at(0x0082ca00), mutable_text_at(kZero), 1);  // cl_paused
    Cmd_ExecuteRegisteredCommand(text_at(title_global<uint8_t*>(0x0095f4e0)[0x390] ? 0x0082abf4    // map_restart_check
                                                                                    : 0x0082abdc));  // _internal
}

// --- Loading screens ---

FE_GAP_ENTRY(LoadingMenu_ClearElements_entry, "pushl %ebx\n", LoadingMenu_ClearElements, "8", "ret")
FE_GAP_ENTRY(LoadAfricaMenu_ClearElements_entry, "pushl %ebx\n", LoadAfricaMenu_ClearElements, "8", "ret")
FE_GAP_ENTRY(LoadSicilyMenu_ClearElements_entry, "pushl %ebx\n", LoadSicilyMenu_ClearElements, "8", "ret")

void XB_CDECL LoadingMenu_ClearElements(uint8_t* menu, uint32_t entry_esp) {
    clear_elements(menu, kLoading9, entry_esp);
}
void XB_CDECL LoadAfricaMenu_ClearElements(uint8_t* menu, uint32_t entry_esp) {
    clear_elements(menu, kLoadingAfrica, entry_esp);
}
void XB_CDECL LoadSicilyMenu_ClearElements(uint8_t* menu, uint32_t entry_esp) {
    clear_elements(menu, kLoadingSicily, entry_esp);
}

void XB_REGS1 XB_CDECL LoadingMenu_SetMissionRange(uint8_t* menu) {
    set_mission_range(menu, kRanges9, 5, true, kLoading9, 0);
}

void XB_REGS1 XB_CDECL LoadAfricaMenu_SetMissionRange(uint8_t* menu) {
    set_mission_range(menu, kRangesAfrica, 5, true, kLoadingAfrica, 0);
}

void XB_REGS1 XB_CDECL LoadSicilyMenu_SetMissionRange(uint8_t* menu) {
    set_mission_range(menu, kRangesSicily, 3, false, kLoadingSicily, 0);
}

FE_GAP_ENTRY(LoadingMenu_RestoreSelection_entry, "pushl %ecx\n", LoadingMenu_RestoreSelection, "8", "ret")
FE_GAP_ENTRY(LoadAfricaMenu_RestoreSelection_entry, "pushl %ecx\n", LoadAfricaMenu_RestoreSelection, "8", "ret")
FE_GAP_ENTRY(LoadSicilyMenu_RestoreSelection_entry, "pushl %ecx\n", LoadSicilyMenu_RestoreSelection, "8", "ret")
FE_GAP_ENTRY(LoadMaubeugeMenu_RestoreSelection_entry, "pushl %ecx\n", LoadMaubeugeMenu_RestoreSelection, "8", "ret")

void XB_CDECL LoadingMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp) {
    loading_restore_selection(menu, kLoading9, kRanges9, 5, true, entry_esp);
}

void XB_CDECL LoadAfricaMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp) {
    loading_restore_selection(menu, kLoadingAfrica, kRangesAfrica, 5, true, entry_esp);
}

void XB_CDECL LoadSicilyMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp) {
    loading_restore_selection(menu, kLoadingSicily, kRangesSicily, 3, false, entry_esp);
}

void XB_CDECL LoadMaubeugeMenu_RestoreSelection(uint8_t* menu, uint32_t entry_esp) {
    AtTop at(entry_esp - 4);  // push esi
    TitleRegs regs{};
    regs.ecx = address_of(menu);
    title_call(0x00314b20, regs);  // UIMenu_RestoreSelection
    menu[0x38] = 0;
    call(field<uint8_t*>(menu, 0x58), 0x6c, kLoading);
    set_alpha(field<uint8_t*>(menu, 0x44), kOneFloat);
    set_alpha(field<uint8_t*>(menu, 0x48), kZeroFloat);
    set_alpha(field<uint8_t*>(menu, 0x4c), kZeroFloat);
    field<uint32_t>(menu, 0x3c) = 0;
}

void XB_THISCALL LoadingMenu_Draw(uint8_t* menu) { loading_draw(menu, kLoading9); }
void XB_THISCALL LoadAfricaMenu_Draw(uint8_t* menu) { loading_draw(menu, kLoadingAfrica); }
void XB_THISCALL LoadSicilyMenu_Draw(uint8_t* menu) { loading_draw(menu, kLoadingSicily); }

void XB_THISCALL LoadMaubeugeMenu_Draw(uint8_t* menu) {
    set_alpha(field<uint8_t*>(menu, 0x50), kZeroFloat);
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) panel_draw(panel, menu);
    UIMenu_Draw(menu);
    call(field<uint8_t*>(menu, 0x58), 0x3c);
    for (uint32_t i = 0; i < 3; ++i) call(field<uint8_t*>(menu, 0x44 + i * 4), 0x44);
}

}  // namespace game
