#include "fe_options.h"

#include "cl.h"
#include "fe.h"
#include "movie.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A title object's vtable slot (byte offset).
template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using VoidMethod = void XB_THISCALL(uint8_t*);
using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);
using PairMethod = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
using GetColorMethod = uint32_t* XB_THISCALL(uint8_t*, uint32_t*);

void call(uint8_t* object, uint32_t offset) { method<VoidMethod>(object, offset)(object); }
void call(uint8_t* object, uint32_t offset, uint32_t a) { method<WordMethod>(object, offset)(object, a); }
void call(uint8_t* object, uint32_t offset, uint32_t a, uint32_t b) {
    method<PairMethod>(object, offset)(object, a, b);
}

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

uint8_t* menu_item(uint8_t* menu, int32_t index) { return field<uint8_t**>(menu, 4)[index]; }
int32_t selection(uint8_t* menu) { return field<int16_t>(menu, 0x22); }

constexpr uint32_t kOn = 0x0082c550;   // "FEMENU_COP_ON"
constexpr uint32_t kOff = 0x0082c540;  // "FEMENU_COP_OFF"

// FEMENU_COP_ON / _OFF into a label (+0x6c).
void show_on_off(uint8_t* label, uint8_t on) { call(label, 0x6c, on ? kOn : kOff); }

// The label coloured like the item (item +0x84 into a word, label +0x90).
void copy_color(uint8_t* item, uint8_t* label) {
    uint32_t color;
    const uint32_t* const out = method<GetColorMethod>(item, 0x84)(item, &color);
    call(label, 0x90, *out);
}

// The options menus' destructor: the panel +0x34, the object +0x38 (when the class has one), the UIMenu part.
void destroy_menu(uint8_t* menu, uint32_t vtable, bool has_object) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = vtable;
    if (panel) Panel_Delete(panel);
    if (has_object) {
        if (uint8_t* const object = field<uint8_t*>(menu, 0x38)) call(object, 0, 1);
    }
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

void init_menu(uint8_t* menu, int32_t rows, uint8_t* owner) { UIMenu_Init(menu, rows, owner, 0x140, 0xf0, 8, 0); }

// The background movie, opened full screen once (0x0095f91a).
void open_background_movie() {
    uint8_t& open = title_global<uint8_t>(0x0095f91a);
    if (open) return;
    if (!Movie_Open(&title_global<const char>(0x0082cacc))) return;  // "Propgnda"
    nglSetQuadRect(title_global<NglQuad*>(0x0095f458), 0.0f, 0.0f, 640.0f, 480.0f);
    open = 1;
}

// The help bar +0x38: FEMENU_HELPBAR_BACK above the row, else FEMENU_OP_HELPBAR.
void set_help_bar(uint8_t* menu, int32_t row) {
    call(field<uint8_t*>(menu, 0x38), 0x6c, field<int16_t>(menu, 0x22) < row ? 0x0082c560u : 0x0082c37cu);
}

// 0x0030e390 (menu in EBX): the five labels +0x3c.. coloured like items 0-4.
void gameplay_label_colors(uint8_t* menu) {
    TitleRegs regs{};
    regs.ebx = address_of(menu);
    ui_call_regs(0x0030e390, &regs, 0, nullptr);
}

// 0x0030e4c0 (menu in ESI): the in-game options' label colours.
void advanced_label_colors(uint8_t* menu) {
    TitleRegs regs{};
    regs.esi = address_of(menu);
    ui_call_regs(0x0030e4c0, &regs, 0, nullptr);
}

// 0x0031ee10 (bar in EAX; menu, value on the stack, ret 8): a slider bar's quads and the volume it sets.
void set_slider(uint8_t* bar, uint8_t* menu, int32_t value) {
    title_function<void XB_REGS1 XB_STDCALL(uint8_t*, uint8_t*, int32_t)>(0x0031ee10)(bar, menu, value);
}

// A slider value moved by one (right: up) and clamped to 0..50; the sound +0x14 unless clamped.
void step_slider(uint8_t* menu, uint32_t offset, uint8_t right) {
    int32_t& value = field<int32_t>(menu, offset);
    value = int32_t(uint32_t(value) + (right ? 1u : 0xffffffffu));
    if (value < 0) {
        value = 0;
    } else if (value > 0x32) {
        value = 0x32;
    } else {
        call(menu, 0x14);
    }
}

// The left / right arrow of a row flashed white (+0x70).
void flash_arrow(uint8_t* arrow) {
    if (!arrow) return;
    call(arrow, 0x70, 0xffffffffu);
}

void advanced_show_labels(uint8_t* menu) {
    show_on_off(field<uint8_t*>(menu, 0x38), menu[0xbe]);
    show_on_off(field<uint8_t*>(menu, 0x3c), menu[0xbf]);
    show_on_off(field<uint8_t*>(menu, 0x40), menu[0xbc]);
    show_on_off(field<uint8_t*>(menu, 0x44), menu[0xbd]);
    if (title_global<int32_t>(0x0095f5dc) == 0) show_on_off(field<uint8_t*>(menu, 0x48), menu[0xc0]);  // g_languageIndex
    show_on_off(field<uint8_t*>(menu, 0x4c), menu[0xc1]);
    show_on_off(field<uint8_t*>(menu, 0x50), menu[0xc2]);
}

void advanced_set_sliders(uint8_t* menu) {
    set_slider(field<uint8_t*>(menu, 0x8c), menu, field<int32_t>(menu, 0xa4));
    set_slider(field<uint8_t*>(menu, 0x90), menu, field<int32_t>(menu, 0xa8));
    set_slider(field<uint8_t*>(menu, 0x94), menu, field<int32_t>(menu, 0xac));
}

void controller_bar_a(uint8_t* menu) { title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x0031e9d0)(menu); }
void controller_bar_b(uint8_t* menu) { title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x0031e810)(menu); }
void controls_update_text(uint8_t* menu) {
    title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x00335d60)(menu);  // FE_ControlsUpdateText
}

}  // namespace

// --- Destructors ---

void XB_REGS1 XB_CDECL OptionsMenu_Dtor(uint8_t* menu) { destroy_menu(menu, 0x00829348, true); }
void XB_REGS1 XB_CDECL GameplayMenu_Dtor(uint8_t* menu) { destroy_menu(menu, 0x00828930, true); }
void XB_REGS1 XB_CDECL ControllerConfigMenu_Dtor(uint8_t* menu) { destroy_menu(menu, 0x00829c48, true); }
void XB_REGS1 XB_CDECL ScreenMenu_Dtor(uint8_t* menu) { destroy_menu(menu, 0x00828b88, true); }
void XB_REGS1 XB_CDECL AdvancedOptionsMenu_Dtor(uint8_t* menu) { destroy_menu(menu, 0x0082a388, false); }

uint8_t* XB_THISCALL OptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    OptionsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL GameplayMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    GameplayMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL ControllerConfigMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    ControllerConfigMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL ScreenMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    ScreenMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL AdvancedOptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    AdvancedOptionsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

// --- Constructors ---

uint8_t* XB_REGS3 XB_CDECL ControllerConfigMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 7, owner);
    field<uint32_t>(menu, 0) = 0x00829c48;  // ControllerConfigMenu vtable
    field<uint32_t>(menu, 0xc4) = 0;
    field<uint16_t>(menu, 0x2a) |= 0x180;
    field<int32_t>(menu, 0xb8) = 0x19;
    field<int32_t>(menu, 0xbc) = 0x19;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    menu[0xcc] = 0;
    field<int32_t>(menu, 0xc0) = 0;
    field<uint32_t>(menu, 0xd4) = 0;
    menu[0x33] = 5;
    field<float>(menu, 0xc8) = 0.0f;
    field<int32_t>(menu, 0xd0) = -1;
    menu[0xcd] = 1;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL GameplayMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 7, owner);
    field<uint32_t>(menu, 0) = 0x00828930;  // GameplayMenu vtable
    field<uint32_t>(menu, 0x78) = 0;
    menu[0x7d] = 1;
    menu[0x7e] = 1;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    menu[0x7c] = 0;
    menu[0x7f] = 0;
    menu[0x80] = 0;
    menu[0x33] = 5;
    field<float>(menu, 0x84) = 0.0f;
    field<int32_t>(menu, 0x88) = -1;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL ScreenMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 3, owner);
    field<uint32_t>(menu, 0) = 0x00828b88;  // ScreenMenu vtable
    field<uint32_t>(menu, 0x4c) = 0;
    menu[0x2a] |= 0x40;
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<float>(menu, 0x54) = 0.0f;
    field<int32_t>(menu, 0x58) = -1;
    menu[0x50] = 1;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL AdvancedOptionsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 0xc, owner);
    field<uint32_t>(menu, 0) = 0x0082a388;  // AdvancedOptionsMenu vtable
    field<uint32_t>(menu, 0xb4) = 0;
    field<uint16_t>(menu, 0x2a) |= 0x180;
    menu[0x33] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<float>(menu, 0xb8) = 0.0f;
    field<int32_t>(menu, 0xb0) = -1;
    return menu;
}

// --- Moving between rows ---

void XB_THISCALL GameplayMenu_OnUp(uint8_t* menu, uint32_t) {
    call(menu, 0xb0);
    gameplay_label_colors(menu);
}

void XB_THISCALL GameplayMenu_OnDown(uint8_t* menu, uint32_t) {
    call(menu, 0xb4);
    gameplay_label_colors(menu);
}

void XB_THISCALL ControllerConfigMenu_OnUp(uint8_t* menu, uint32_t) {
    call(menu, 0xb0);
    ControllerConfigMenu_UpdateLabelColors(menu);
}

void XB_THISCALL ControllerConfigMenu_OnDown(uint8_t* menu, uint32_t) {
    call(menu, 0xb4);
    ControllerConfigMenu_UpdateLabelColors(menu);
}

void XB_THISCALL AdvancedOptionsMenu_OnUp(uint8_t* menu, uint32_t) {
    const int32_t row = selection(menu);
    call(menu, 0xb0);
    if (row == 0) {
        call(menu, 0x88, 0xb, 1);
        call(menu, 0x10);
    }
    advanced_label_colors(menu);
}

void XB_THISCALL AdvancedOptionsMenu_OnDown(uint8_t* menu, uint32_t) {
    const int32_t row = selection(menu);
    call(menu, 0xb4);
    if (row == 0xb) {
        call(menu, 0x88, 0, 1);
        call(menu, 0x10);
    }
    advanced_label_colors(menu);
}

// --- Controller menu ---

PLATFORM_ASM_ENTRY(ControllerConfigMenu_UpdateLabelColors_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ControllerConfigMenu_UpdateLabelColors) "addl $4, %esp\n"
                   "ret")

void XB_CDECL ControllerConfigMenu_UpdateLabelColors(uint8_t* menu) {
    copy_color(menu_item(menu, 0), field<uint8_t*>(menu, 0x3c));
    copy_color(menu_item(menu, 1), field<uint8_t*>(menu, 0x40));
    copy_color(menu_item(menu, 4), field<uint8_t*>(menu, 0x44));
}

void XB_REGS1 XB_CDECL ControllerConfigMenu_UpdateInvertAimText(uint8_t* menu) {
    show_on_off(field<uint8_t*>(menu, 0x3c), menu[0xcc]);
    ControllerConfigMenu_UpdateLabelColors(menu);
}

void XB_REGS1 XB_CDECL ControllerConfigMenu_UpdateVibrationText(uint8_t* menu) {
    show_on_off(field<uint8_t*>(menu, 0x40), menu[0xcd]);
    ControllerConfigMenu_UpdateLabelColors(menu);
}

PLATFORM_ASM_ENTRY(ControllerConfigMenu_ChangeValue_entry,
                   "movzbl %bl, %eax\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(ControllerConfigMenu_ChangeValue) "addl $8, %esp\n"
                   "ret")

void XB_CDECL ControllerConfigMenu_ChangeValue(uint8_t* menu, uint8_t right) {
    switch (selection(menu)) {
        case 0:
            menu[0xcc] = menu[0xcc] == 0;
            ControllerConfigMenu_UpdateInvertAimText(menu);
            call(menu, 0x10);
            break;
        case 1:
            menu[0xcd] = menu[0xcd] == 0;
            ControllerConfigMenu_UpdateVibrationText(menu);
            call(menu, 0x10);
            break;
        case 2:
            step_slider(menu, 0xb8, right);
            controller_bar_a(menu);
            break;
        case 3:
            step_slider(menu, 0xbc, right);
            controller_bar_b(menu);
            break;
        case 4: {
            int32_t& preset = field<int32_t>(menu, 0xc0);
            preset = int32_t(uint32_t(preset) + (right ? 1u : 0xffffffffu));
            if (preset < 0) preset = 0;
            if (preset > 3) preset = 3;
            controls_update_text(menu);
            call(menu, 0x10);
            break;
        }
        default:
            break;
    }
    const int16_t row = field<int16_t>(menu, 0x22);
    if (row != 0 && row != 1 && row != 4) return;
    field<int32_t>(menu, 0xd0) = row == 4 ? 2 : row;
    field<float>(menu, 0xc8) = title_global<float>(0x0083ebf8);  // 0.1
    flash_arrow(field<uint8_t*>(menu, 0x48 + (field<int32_t>(menu, 0xd0) * 2 + (right != 0)) * 4));
}

void XB_THISCALL ControllerConfigMenu_RestoreSelection(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    open_background_movie();
    menu[0xcc] = title_global<uint8_t>(0x008dce71);
    menu[0xcd] = title_global<uint8_t>(0x008dce72);
    field<uint32_t>(menu, 0xb8) = title_global<uint32_t>(0x008dce74);
    field<uint32_t>(menu, 0xbc) = title_global<uint32_t>(0x008dce78);
    field<uint32_t>(menu, 0xd4) = title_global<uint32_t>(0x008dce7c);
    int32_t preset = title_global<int32_t>(0x008dce7c);
    if (preset == 4) {
        preset = 0;
        title_global<int32_t>(0x008dce7c) = 0;
    }
    field<int32_t>(menu, 0xc0) = preset;
    show_on_off(field<uint8_t*>(menu, 0x3c), menu[0xcc]);
    ControllerConfigMenu_UpdateLabelColors(menu);
    show_on_off(field<uint8_t*>(menu, 0x40), menu[0xcd]);
    ControllerConfigMenu_UpdateLabelColors(menu);
    controller_bar_a(menu);
    controller_bar_b(menu);
    controls_update_text(menu);
}

// --- In-game options ---

PLATFORM_ASM_ENTRY(AdvancedOptionsMenu_ToggleSelected_entry,
                   "movzbl %bl, %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AdvancedOptionsMenu_ToggleSelected) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AdvancedOptionsMenu_ToggleSelected(uint8_t* menu, uint8_t right) {
    // The flag rows: the flag and its label; 0xff for the sliders.
    static constexpr uint32_t kFlags[10] = {0xbe, 0xbf, 0, 0, 0xbc, 0xbd, 0, 0xc0, 0xc1, 0xc2};
    static constexpr uint32_t kLabels[10] = {0x38, 0x3c, 0, 0, 0x40, 0x44, 0, 0x48, 0x4c, 0x50};
    const int32_t row = selection(menu);
    if (uint32_t(row) > 9) return;
    switch (row) {
        case 2:
            step_slider(menu, 0xa4, right);
            set_slider(field<uint8_t*>(menu, 0x8c), menu, field<int32_t>(menu, 0xa4));
            break;
        case 3:
            step_slider(menu, 0xa8, right);
            set_slider(field<uint8_t*>(menu, 0x90), menu, field<int32_t>(menu, 0xa8));
            break;
        case 6:
            step_slider(menu, 0xac, right);
            set_slider(field<uint8_t*>(menu, 0x94), menu, field<int32_t>(menu, 0xac));
            break;
        default: {
            uint8_t& flag = menu[kFlags[row]];
            flag = flag == 0;
            // Row 7's label only for language 0 (g_languageIndex).
            if (row != 7 || title_global<int32_t>(0x0095f5dc) == 0)
                show_on_off(field<uint8_t*>(menu, kLabels[row]), flag);
            call(menu, 0x10);
            break;
        }
    }
    const int16_t now = field<int16_t>(menu, 0x22);
    if (now != 2 && now != 3 && now != 6) {
        int32_t arrow_row = now;
        field<int32_t>(menu, 0xb0) = arrow_row;
        if (now == 4 || now == 5) {
            field<int32_t>(menu, 0xb0) = arrow_row - 2;
        } else if (now == 7 || now == 8 || now == 9) {
            field<int32_t>(menu, 0xb0) = arrow_row - 3;
        }
        field<float>(menu, 0xb8) = title_global<float>(0x0083ebf8);  // 0.1
        flash_arrow(field<uint8_t*>(menu, 0x54 + (field<int32_t>(menu, 0xb0) * 2 + (right != 0)) * 4));
    }
    advanced_label_colors(menu);
}

PLATFORM_ASM_ENTRY(AdvancedOptionsMenu_LoadSettings_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(AdvancedOptionsMenu_LoadSettings) "addl $4, %esp\n"
                   "ret")

void XB_CDECL AdvancedOptionsMenu_LoadSettings(uint8_t* menu) {
    uint8_t* const pause = field<uint8_t**>(title_global<uint8_t*>(0x0095f8fc), 4)[0];  // the in-game screens' pause menu
    menu[0xbc] = pause[0x40];
    menu[0xbe] = title_global<uint8_t>(0x008dce71);
    menu[0xbf] = title_global<uint8_t>(0x008dce72);
    field<uint32_t>(menu, 0xa4) = title_global<uint32_t>(0x008dce74);
    field<uint32_t>(menu, 0xa8) = title_global<uint32_t>(0x008dce78);
    field<uint32_t>(menu, 0xac) = title_global<uint32_t>(0x008dce88);
    menu[0xbd] = title_global<uint8_t>(0x008dce6a);
    menu[0xc0] = title_global<uint8_t>(0x008dce68);
    menu[0xc1] = title_global<uint32_t>(0x008dce6c) != 0;
    menu[0xc2] = title_global<uint8_t>(0x008dce70);
    advanced_show_labels(menu);
    advanced_set_sliders(menu);
}

PLATFORM_ASM_ENTRY(AdvancedOptionsMenu_ResetDefaults_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(AdvancedOptionsMenu_ResetDefaults) "addl $4, %esp\n"
                   "ret")

void XB_CDECL AdvancedOptionsMenu_ResetDefaults(uint8_t* menu) {
    menu[0xbf] = 1;
    menu[0xbc] = 1;
    menu[0xbd] = 1;
    menu[0xbe] = 0;
    field<int32_t>(menu, 0xa4) = 0x19;
    field<int32_t>(menu, 0xa8) = 0x19;
    field<int32_t>(menu, 0xac) = 0x32;
    menu[0xc0] = 0;
    menu[0xc1] = 0;
    menu[0xc2] = 0;
    advanced_set_sliders(menu);
    advanced_show_labels(menu);
}

void XB_THISCALL AdvancedOptionsMenu_RestoreSelection(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    const int32_t pad = title_global<int32_t>(0x008dcf18);
    IN_PadCreate();
    IN_PadConsumePressed(pad, 0xe);
    AdvancedOptionsMenu_LoadSettings(menu);
    advanced_label_colors(menu);
}

void XB_THISCALL AdvancedOptionsMenu_ResumeGame(uint8_t* menu, uint32_t) {
    {
        TitleRegs regs{};
        regs.esi = address_of(menu);
        ui_call_regs(0x00331340, &regs, 0, nullptr);  // FE_ApplyControlSettings (menu in ESI)
    }
    title_global<uint8_t>(0x0095f919) = 0;
    {
        TitleRegs regs{};
        regs.eax = title_global<uint32_t>(0x0095f4c4);
        ui_call_regs(0x002b6ba0, &regs, 0, nullptr);  // S_ResumeAllChannels
    }
    Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e098)),
              1);  // cl_paused "0"
    uint8_t* const pause = field<uint8_t**>(title_global<uint8_t*>(0x0095f8fc), 4)[0];
    title_global<uint8_t>(0x008dce69) = pause[0x40];
}

// --- Drawing ---

void XB_THISCALL ScreenMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    set_help_bar(menu, 1);
    call(field<uint8_t*>(menu, 0x38), 0x3c);
    UIMenu_Draw(menu);
    Movie_DrawFrame();
}

void XB_THISCALL ControllerConfigMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    set_help_bar(menu, 5);
    call(field<uint8_t*>(menu, 0x38), 0x3c);
    UIMenu_Draw(menu);
    Movie_DrawFrame();
}

void XB_THISCALL GameplayMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    set_help_bar(menu, 5);
    if (uint8_t* const bar = field<uint8_t*>(menu, 0x38)) call(bar, 0x3c);
    UIMenu_Draw(menu);
    Movie_DrawFrame();
}

void XB_THISCALL AdvancedOptionsMenu_Draw(uint8_t* menu) {
    if (uint8_t* const text = field<uint8_t*>(menu, 0x98)) call(text, 0x3c);
    if (field<int16_t>(menu, 0x22) >= 0xa) {
        if (uint8_t* const text = field<uint8_t*>(menu, 0x9c)) call(text, 0x3c);
    }
    if (uint8_t* const text = field<uint8_t*>(menu, 0xa0)) call(text, 0x3c);
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    UIMenu_Draw(menu);
}

// --- Restoring the selection ---

void XB_THISCALL OptionsMenu_RestoreSelection(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    call(menu, 0x88, field<uint32_t>(menu, 0x3c), 1);
    open_background_movie();
}

void XB_THISCALL GameplayMenu_RestoreSelection(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    open_background_movie();
    menu[0x7c] = title_global<uint8_t>(0x008dce68);
    menu[0x7e] = title_global<uint8_t>(0x008dce6a);
    menu[0x7d] = title_global<uint8_t>(0x008dce69);
    menu[0x7f] = title_global<uint32_t>(0x008dce6c) != 0;
    menu[0x80] = title_global<uint8_t>(0x008dce70);
    for (int32_t i = 0; i < 5; ++i) {
        // The first flag's label only for language 0 (g_languageIndex).
        if (title_global<int32_t>(0x0095f5dc) != 0 && i == 0) continue;
        show_on_off(field<uint8_t*>(menu, 0x3c + i * 4), menu[0x7c + i]);
    }
    gameplay_label_colors(menu);
}

}  // namespace game
