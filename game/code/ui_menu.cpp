#include "ui_menu.h"

#include <cstring>

#include "cl.h"
#include "fe.h"
#include "ngl.h"
#include "script_string.h"
#include "snd.h"
#include "stream.h"
#include "title_data.h"
#include "tr_main.h"
#include "ui_panel.h"

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
const char* text_at(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

// A title object's vtable slot (byte offset).
template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using VoidMethod = void XB_THISCALL(uint8_t*);
using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);
using PairMethod = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
using BoolMethod = uint8_t XB_THISCALL(uint8_t*);
using PositionMethod = void XB_THISCALL(uint8_t*, float, float);
using ColorMethod = uint32_t* XB_THISCALL(uint8_t*, uint32_t*, uint32_t, uint32_t);
using GetColorMethod = uint32_t* XB_THISCALL(uint8_t*, uint32_t*);

void call(uint8_t* object, uint32_t offset) { method<VoidMethod>(object, offset)(object); }
void call(uint8_t* object, uint32_t offset, uint32_t a) { method<WordMethod>(object, offset)(object, a); }
void call(uint8_t* object, uint32_t offset, uint32_t a, uint32_t b) {
    method<PairMethod>(object, offset)(object, a, b);
}

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free
uint8_t* z_malloc(uint32_t size) { return title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(size); }
uint8_t* heap_alloc16(uint32_t size) {
    return title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c30)(size);  // Heap_Alloc16
}

// Menus.
uint8_t* menu_item(uint8_t* menu, int32_t index) { return field<uint8_t**>(menu, 4)[index]; }
int32_t item_count(uint8_t* menu) { return field<int16_t>(menu, 0x26); }
int32_t selection(uint8_t* menu) { return field<int16_t>(menu, 0x22); }
bool menu_flag(uint8_t* menu, int32_t bit) { return (field<uint8_t>(menu, 0x2a) >> bit) & 1; }
bool hidden(uint8_t* item) { return method<BoolMethod>(item, 0x24)(item) != 0; }
bool item_hidden(uint8_t* menu, int32_t index) { return hidden(menu_item(menu, index)); }
void select(uint8_t* menu, int32_t index, uint32_t sound) { call(menu, 0x88, uint32_t(index), sound); }
void scroll_to(uint8_t* menu, int32_t first) { call(menu, 0x8c, uint32_t(first)); }

// Screen managers.
uint8_t* screen(uint8_t* mgr, int32_t index) { return field<uint8_t**>(mgr, 4)[index]; }

// A sound handle (table 0x00962860: {pointer, generation} per slot) whose voice still plays (states 2-5).
bool sound_playing(uint32_t handle) {
    const uint32_t slot = handle & 0xfff;
    if (slot >= 0x200 || (handle >> 12) != title_global<uint32_t>(0x00962864 + slot * 8)) return false;
    const int32_t* const channel = title_global<int32_t*>(0x00962860 + slot * 8);
    if (!channel) return false;
    const int32_t voice = *channel;
    if (voice == -1) return false;
    const uint32_t state = SndVoice_GetState(voice);
    return state == 4 || state == 2 || state == 3 || state == 5;
}

// S_PlaySoundAlias of a UI alias at the origin (volumes -1) into `handle` (EDX: the handle slot, or for the screen
// manager the axis); false without the alias.
bool play_ui_sound(uint32_t name, uint32_t& handle, bool axis_in_edx) {
    alignas(16) float origin[4];
    alignas(16) float axis[4];
    std::memcpy(origin, &title_global<float>(0x0082e110), sizeof origin);  // (0, 0, 0, 0)
    std::memcpy(axis, &title_global<float>(0x0082e290), sizeof axis);      // (0, 0, 0, 1)
    const uint32_t table = title_global<uint32_t>(0x0095f4c4);
    const int32_t alias = S_FindSoundByHash(text_at(name));
    if (alias == -1) return false;
    using PlayAlias = uint32_t* XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t,
                                                     uint32_t, const float*, const float*, uint32_t, uint32_t,
                                                     uint32_t, uint32_t);
    title_function<PlayAlias>(0x002c30a0)(uint32_t(alias), axis_in_edx ? address_of(axis) : address_of(&handle), table,
                                          &handle, 0, 1, 0, axis, origin, 0xbf800000, 0xbf800000, 0xbf800000,
                                          0xbf800000);  // S_PlaySoundAlias
    return true;
}

// The "UI_Select" sound into the handle field unless it still plays (0 without the alias).
void play_select_sound_if_idle(uint32_t& field_handle) {
    if (sound_playing(field_handle)) return;
    uint32_t handle = 0;
    field_handle = play_ui_sound(0x0082cc00, handle, false) ? handle : 0;  // "UI_Select"
}

// The menu item fields both constructors set after the text.
void init_item(uint8_t* item, uint8_t* text, uint8_t* menu) {
    field<uint8_t*>(item, 0x10) = text;
    field<uint8_t*>(item, 4) = menu;
    item[0x14] = method<BoolMethod>(menu, 0xa4)(menu);
    item[0x15] = 0;
    item[0x16] = 0;
    field<int16_t>(item, 8) = -1;
    field<int16_t>(item, 0xa) = -1;
    field<int16_t>(item, 0xc) = -1;
    field<int16_t>(item, 0xe) = -1;
    if (text) call(text, 0x40, 1);
    call(item, 0x94);
}

// The dialog of a screen manager: +0x04 [+0x0c != 0].
uint8_t* dialog_of(uint8_t* mgr) { return field<uint8_t**>(mgr, 4)[field<uint32_t>(mgr, 0xc) != 0]; }

}  // namespace

// --- Menu items ---

PLATFORM_ASM_ENTRY(UIMenuItem_Ctor_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIMenuItem_Ctor) "addl $16, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL UIMenuItem_Ctor(uint8_t* item, uint8_t* text, uint8_t* menu, uint8_t owns) {
    field<uint32_t>(item, 0) = 0x008290c8;  // vtable
    init_item(item, text, menu);
    item[0x17] = owns;
    return item;
}

uint8_t* XB_THISCALL UIMenuItem_DeletingDtor(uint8_t* item, uint32_t flags) {
    field<uint32_t>(item, 0) = 0x008290c8;  // vtable
    uint8_t* const text = field<uint8_t*>(item, 0x10);
    if (item[0x17] && text) call(text, 0, 1);
    if (flags & 1) z_free(item);
    return item;
}

PLATFORM_ASM_ENTRY(UIMenuItem_Init_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIMenuItem_Init) "addl $20, %esp\n"
                   "ret $12")

uint8_t* XB_CDECL UIMenuItem_Init(int32_t kind, uint8_t* item, const char* string, uint8_t* menu, int32_t mode) {
    field<uint32_t>(item, 0) = 0x008290c8;  // vtable
    if (kind == 8) kind = field<int32_t>(method<uint8_t* XB_THISCALL(uint8_t*)>(menu, 0xa8)(menu), 8);
    uint8_t* text;
    if (mode == 1) {
        uint8_t* const block = z_malloc(0x60);
        text = block ? UIText_Init(block, kind, string, 0, 0, 0, 2, 0x3f800000, 0, 0, 0) : nullptr;
    } else {
        uint8_t* const block = z_malloc(0x98);
        text = block ? UIText_Construct(block, kind, 0, 0, 0, 2, 0x3f800000, 0, 0, 0) : nullptr;
        call(text, 0x13c, uint32_t(mode));
        call(text, 0x78, address_of(string));
    }
    init_item(item, text, menu);
    item[0x17] = 1;
    call(text, 0x40, 0);
    return item;
}

void XB_THISCALL UIMenuItem_UpdateColor(uint8_t* item) {
    uint8_t* const text = field<uint8_t*>(item, 0x10);
    uint32_t local0 = 0;
    uint32_t local1 = 0;
    uint32_t normal;    // EDI
    uint32_t selected;  // EDX
    uint32_t pulse;     // EBP
    bool steady;        // BL
    if (item[0x14] == 0xb) {
        steady = true;
        const uint32_t own = *method<GetColorMethod>(text, 0xb4)(text, &local0);
        normal = *method<ColorMethod>(item, 0x90)(item, &local1, own, 0x8c);
        const uint32_t own2 = *method<GetColorMethod>(text, 0xb4)(text, &local1);
        selected = *method<ColorMethod>(item, 0x90)(item, &local0, own2, 0xff);
        pulse = selected;
    } else {
        const uint32_t scheme = 0x0095fd30 + uint32_t(int32_t(int8_t(item[0x14]))) * 16;
        normal = title_global<uint32_t>(scheme);
        selected = title_global<uint32_t>(scheme + 4);
        pulse = title_global<uint32_t>(scheme + 8);
        steady = title_global<uint8_t>(scheme + 0xc) == 0;
    }
    bool highlight;
    if (item[0x15]) {
        const int32_t flags = field<int16_t>(field<uint8_t*>(item, 4), 0x2a);
        highlight = ((flags >> 3) & 1) && item[0x16];
        if (!highlight) {
            if (!((flags >> 4) & 1) && !steady) {
                method<void XB_THISCALL(uint8_t*, uint32_t, uint32_t, uint32_t)>(text, 0x94)(text, selected, pulse,
                                                                                         0x3ecccccd);  // 0.4
                return;
            }
            call(text, 0x90, ((flags >> 5) & 1) ? normal : selected);
            return;
        }
    } else {
        if (!text) return;
        highlight = item[0x16] != 0;
        if (!highlight) {
            call(text, 0x90, normal);
            return;
        }
    }
    const uint32_t dim = *method<ColorMethod>(item, 0x90)(item, &local1, normal, 0x64);
    call(text, 0x90, dim);
}

PLATFORM_ASM_ENTRY(UIOptionItem_PlaySelectSound_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(UIOptionItem_PlaySelectSound) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIOptionItem_PlaySelectSound(uint8_t* item) {
    if (!item[0x28]) return;
    play_select_sound_if_idle(field<uint32_t>(item, 0x24));
}

uint32_t XB_THISCALL UIOptionItem_Next(uint8_t* item) {
    ++field<int16_t>(item, 0x18);
    if (field<int16_t>(item, 0x18) >= field<int16_t>(item, 0x1c)) field<int16_t>(item, 0x18) = 0;
    UIOptionItem_PlaySelectSound(item);
    const uint32_t rep = field<uint32_t*>(item, 0x20)[field<int16_t>(item, 0x18)];
    uint8_t* const text = field<uint8_t*>(item, 0x10);
    const uint32_t result =
        method<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(text, 0x78)(text, rep ? rep + 6 : 0);  // the text
    return result | 0xffff;
}

void XB_STDCALL UIOptionItem_Dtor(uint8_t* item) {
    field<uint32_t>(item, 0) = 0x00829680;  // vtable
    uint8_t* const strings = field<uint8_t*>(item, 0x20);
    if (strings) {
        const int32_t count = field<int32_t>(strings, -4);
        if (count - 1 >= 0) {
            uint8_t* string = strings + count * 4;
            for (int32_t n = count; n != 0; --n) {
                string -= 4;
                title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(string);  // ScriptString_Release
            }
        }
        z_free(strings - 4);
    }
    const uint32_t handle = field<uint32_t>(item, 0x24);
    const uint32_t slot = handle & 0xfff;
    const auto live = [&] { return slot < 0x200 && (handle >> 12) == title_global<uint32_t>(0x00962864 + slot * 8); };
    if (live() && title_global<uint32_t>(0x00962860 + slot * 8))
        S_StopChannel(live() ? title_global<uint8_t*>(0x00962860 + slot * 8) : nullptr);
    field<uint32_t>(item, 0) = 0x008290c8;  // UIMenuItem vtable
    uint8_t* const text = field<uint8_t*>(item, 0x10);
    if (item[0x17] && text) call(text, 0, 1);  // deleting destructor (a tail jump in the original)
}

uint8_t* XB_THISCALL UIOptionItem_DeletingDtor(uint8_t* item, uint32_t flags) {
    UIOptionItem_Dtor(item);
    if (flags & 1) z_free(item);
    return item;
}

// --- Dialogs ---

void XB_REGS1 XB_STDCALL UI_DialogAddButton(uint8_t* mgr, uint32_t label, uint32_t handler) {
    uint8_t* const dialog = dialog_of(mgr);
    call(menu_item(dialog, field<int32_t>(dialog, 0x34)), 0x30, label);
    field<uint32_t>(dialog, 0x38 + field<uint32_t>(dialog, 0x34) * 4) = handler;
    call(menu_item(dialog, field<int32_t>(dialog, 0x34)), 0x20, 0);
    ++field<int32_t>(dialog, 0x34);
}

void XB_REGS1 XB_STDCALL UI_DialogSetDefaultButton(uint8_t* mgr, int16_t index) {
    uint8_t* const dialog = dialog_of(mgr);
    field<int16_t>(dialog, 0x22) = index;
    dialog[0x5a] = 1;
}

void XB_REGS1 XB_STDCALL UI_DialogLayoutButtons(uint8_t* mgr, uint8_t flag) {
    uint8_t* const dialog = dialog_of(mgr);
    title_call_regs(0x0031f4b0, (address_of(mgr) & ~0xffu) | flag, 0, address_of(dialog));  // Dialog_LayoutButtons
}

// --- Menus ---

uint8_t* XB_THISCALL UIMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // vtable
    UIMenu_FreeItems(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

void XB_THISCALL UIMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // vtable
    UIMenu_FreeItems(menu);
}

void XB_THISCALL UIMenu_OnAccept(uint8_t* menu, uint32_t) {
    if (selection(menu) < 0) return;
    if (item_hidden(menu, selection(menu))) return;
    call(menu, 0x38, uint32_t(selection(menu)));
}

void XB_THISCALL UIMenu_SelectFirst(uint8_t* menu) {
    int32_t i = 0;
    while (i < item_count(menu) && item_hidden(menu, i)) ++i;
    if (i < item_count(menu)) select(menu, i, 0);
}

void XB_THISCALL UIMenu_SetItemsLayer(uint8_t* menu, uint32_t a, uint32_t b) {
    for (int32_t i = 0; i < item_count(menu); ++i) call(menu_item(menu, i), 0x60, a, b);
}

void XB_THISCALL UIMenu_SetItemsScale(uint8_t* menu, uint32_t a) {
    for (int32_t i = 0; i < item_count(menu); ++i) call(menu_item(menu, i), 0x5c, a);
}

void XB_THISCALL UIMenu_RestoreSelection(uint8_t* menu) {
    if (field<int16_t>(menu, 0x22) == -1) field<int16_t>(menu, 0x22) = field<int16_t>(menu, 0x24);
    const int16_t index = field<int16_t>(menu, 0x22);
    if (index != -1 && !item_hidden(menu, index))
        select(menu, selection(menu), 0);
    else
        call(menu, 0x34);  // the default selection
    menu[0x32] = 0xff;
    if (menu_flag(menu, 0)) {
        scroll_to(menu, selection(menu));
    } else if (menu_flag(menu, 6)) {
        call(menu, 0x1c);  // a tail jump in the original
    }
}

void XB_THISCALL UIMenu_Update(uint8_t* menu, float dt) {
    if (menu_flag(menu, 7) && menu[0x32] != 0xff) {
        const float timer = field<float>(menu, 0x1c) - dt;
        field<float>(menu, 0x1c) = timer;
        if (0.0f >= timer) {
            call(menu, 0xc0);  // repeat
            field<float>(menu, 0x1c) = title_global<float>(0x0083ebf8);  // 0.1
            if ((field<uint16_t>(menu, 0x2a) >> 8) & 1) field<float>(menu, 0x1c) = title_global<float>(0x0083edd0);
        }
    }
    uint32_t bits;
    std::memcpy(&bits, &dt, 4);
    for (int32_t i = 0; i < item_count(menu); ++i) call(menu_item(menu, i), 0x10, bits);
}

namespace {

// The walk over the rows of a scrolling menu from `index` on: skips hidden items (flag 6) and past the end wraps
// to item 0 (flag 1) - without flag 1 the index past the end is used as it is, as in the original.
int32_t next_shown_row(uint8_t* menu, int32_t index) {
    for (;;) {
        if (index < item_count(menu) && (!menu_flag(menu, 6) || !item_hidden(menu, index))) return index;
        ++index;
        if (index < item_count(menu)) continue;
        if (!menu_flag(menu, 1)) return index;
        index = 0;
    }
}

// The y of a row: top-aligned (flag 10), bottom-aligned (flag 11) or centred on the menu's y.
int32_t row_y(uint8_t* menu, int32_t row) {
    const int32_t flags = field<int16_t>(menu, 0x2a);
    const uint32_t offset = uint32_t(field<int32_t>(menu, 0x14)) * uint32_t(row);
    if ((flags >> 10) & 1) return int32_t(offset + field<uint32_t>(menu, 0x10));
    if ((flags >> 11) & 1) return int32_t(offset - field<uint32_t>(menu, 0x18) * 2 + field<uint32_t>(menu, 0x10));
    return int32_t(offset + field<uint32_t>(menu, 0x10) - field<uint32_t>(menu, 0x18));
}

void place_item(uint8_t* menu, uint8_t* item, int32_t y) {
    const float fy = float(y);
    const float fx = float(field<int32_t>(menu, 0xc));
    method<PositionMethod>(item, 0x2c)(item, fx, fy);
}

}  // namespace

void XB_THISCALL UIMenu_Draw(uint8_t* menu) {
    if (!(menu[0x2a] & 1)) {
        for (int32_t i = 0; i < item_count(menu); ++i) {
            if (menu_flag(menu, 6) && item_hidden(menu, i)) continue;
            call(menu_item(menu, i), 0xc);
        }
        return;
    }
    int32_t index = field<int16_t>(menu, 0x20);
    for (int32_t row = 0; row < field<int16_t>(menu, 0x28); ++row, ++index) {
        index = next_shown_row(menu, index);
        call(menu_item(menu, index), 0xc);
    }
}

void XB_THISCALL UIMenu_Layout(uint8_t* menu) {
    const int16_t count = field<int16_t>(menu, 0x26);
    const int16_t rows = field<int16_t>(menu, 0x28);
    if (count > rows) field<uint16_t>(menu, 0x2a) |= 1;
    int32_t last;
    if (menu[0x2a] & 1) {
        last = rows - 1;
    } else if (menu_flag(menu, 6)) {
        int32_t shown = 0;
        if (count > 0) {
            for (int32_t i = 0; i < item_count(menu); ++i)
                if (!item_hidden(menu, i)) ++shown;
        }
        last = shown - 1;
    } else {
        last = count - 1;
    }
    field<int32_t>(menu, 0x18) = int32_t(uint32_t(last) * field<uint32_t>(menu, 0x14)) / 2;
    if (field<int16_t>(menu, 0x26) >= 1) select(menu, 0, 0);
    menu[0x2b] |= 2;
    if (menu[0x2a] & 1) {
        scroll_to(menu, 0);
        return;
    }
    int32_t row = 0;
    for (int32_t i = 0; i < item_count(menu); ++i) {
        if (menu_flag(menu, 6) && item_hidden(menu, i)) continue;
        place_item(menu, menu_item(menu, i), row_y(menu, row));
        ++row;
    }
}

void XB_THISCALL UIMenu_ScrollTo(uint8_t* menu, int32_t first) {
    field<int16_t>(menu, 0x20) = int16_t(first);
    int32_t index = first;
    for (int32_t row = 0; row < field<int16_t>(menu, 0x28); ++row, ++index) {
        index = next_shown_row(menu, index);
        place_item(menu, menu_item(menu, index), row_y(menu, row));
    }
}

namespace {

// Steps from `index` (by `step` mod the count) while the item there is hidden, until back at the selection.
int32_t skip_hidden(uint8_t* menu, int32_t index, int32_t step) {
    if (!item_hidden(menu, index)) return index;
    do {
        if (index == selection(menu)) break;
        index = (item_count(menu) + index + step) % item_count(menu);
    } while (item_hidden(menu, index));
    return index;
}

// The end of SelectPrev / SelectNext: the neighbour `index`, past hidden ones unless flag 3, the move sound when
// the selection changes, then select it with sound.
void finish_move(uint8_t* menu, int32_t index, int32_t step) {
    if (!menu_flag(menu, 3)) index = skip_hidden(menu, index, step);
    if (index != selection(menu) && menu[0x31]) call(menu, 0x10);
    select(menu, index, 1);
}

}  // namespace

void XB_THISCALL UIMenu_SelectPrev(uint8_t* menu) {
    if (menu[0x30]) return;
    if (!menu_flag(menu, 1)) {
        if (field<int16_t>(menu, 0x22) == 0) return;
        int32_t i = selection(menu) - 1;
        if (i < 0) return;
        while (item_hidden(menu, i)) {
            if (--i < 0) return;
        }
    }
    if ((menu[0x2a] & 1) && field<int16_t>(menu, 0x22) == field<int16_t>(menu, 0x20)) {
        int32_t index = (field<int16_t>(menu, 0x20) + item_count(menu) - 1) % item_count(menu);
        if (menu_flag(menu, 6)) index = skip_hidden(menu, index, -1);
        scroll_to(menu, index);
    }
    finish_move(menu, (selection(menu) + item_count(menu) - 1) % item_count(menu), -1);
}

void XB_THISCALL UIMenu_SelectNext(uint8_t* menu) {
    if (menu[0x30]) return;
    if (!menu_flag(menu, 1)) {
        const int32_t count = item_count(menu);
        if (selection(menu) == count - 1) return;
        int32_t i = selection(menu) + 1;
        while (i < item_count(menu) && item_hidden(menu, i)) ++i;
        if (i >= item_count(menu)) return;
    }
    if (menu[0x2a] & 1) {
        const int32_t first = field<int16_t>(menu, 0x20);
        if (selection(menu) == field<int16_t>(menu, 0x28) + first - 1) {
            int32_t index = (first + 1) % item_count(menu);
            if (menu_flag(menu, 6)) index = skip_hidden(menu, index, 1);
            scroll_to(menu, index);
        }
    }
    finish_move(menu, (selection(menu) + 1) % item_count(menu), 1);
}

void XB_THISCALL UIMenu_PlaySelectSoundIfIdle(uint8_t* menu) { play_select_sound_if_idle(field<uint32_t>(menu, 0x2c)); }

void XB_THISCALL UIMenu_PlaySelectSound(uint8_t* menu) {
    uint32_t handle = 0;
    field<uint32_t>(menu, 0x2c) = play_ui_sound(0x0082cc00, handle, false) ? handle : 0;  // "UI_Select"
}

// --- Grid menus ---

PLATFORM_ASM_ENTRY(UIGridMenu_Init_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIGridMenu_Init) "addl $16, %esp\n"
                   "ret $8")

uint8_t* XB_CDECL UIGridMenu_Init(uint8_t* menu, int32_t count, uint8_t* owner, uint32_t flags) {
    field<uint32_t>(menu, 0x2c) = 0;
    field<uint16_t>(menu, 0x28) = 1;
    menu[0x31] = 1;
    field<uint32_t>(menu, 0xc) = 0;
    field<uint32_t>(menu, 0x10) = 0;
    field<uint16_t>(menu, 0x26) = 0;
    field<uint32_t>(menu, 0x14) = 0;
    field<uint32_t>(menu, 0x18) = 0;
    field<uint16_t>(menu, 0x2a) = 0;
    field<uint32_t>(menu, 4) = 0;
    field<uint16_t>(menu, 0x20) = 0;
    field<uint16_t>(menu, 0x22) = 0xffff;
    field<uint16_t>(menu, 0x24) = 0xffff;
    field<uint32_t>(menu, 8) = 0;
    menu[0x30] = 0;
    field<uint32_t>(menu, 0) = 0x008294d8;  // vtable
    field<uint8_t*>(menu, 4) = heap_alloc16(uint32_t(count) * 4);
    for (int32_t i = 0; i < count; ++i) field<uint32_t*>(menu, 4)[i] = 0;
    field<uint16_t>(menu, 0x20) = 0;
    field<uint16_t>(menu, 0x22) = 0xffff;
    field<uint8_t*>(menu, 8) = owner;
    field<uint16_t>(menu, 0x2a) = uint16_t(flags & ~0x200u);
    field<uint16_t>(menu, 0x26) = uint16_t(count);
    field<uint32_t>(menu, 0x14) = 0x1c;
    field<uint32_t>(menu, 0x18) = 0;
    menu[0x33] = method<BoolMethod>(owner, 0x24)(owner);
    return menu;
}

namespace {

// A grid move: the neighbour at `offset` of the selected item, past hidden items (unless flag 3) along the same
// direction until back at the selection; selected with sound when it is another item.
void grid_move(uint8_t* menu, uint32_t offset) {
    int32_t index = field<int16_t>(menu_item(menu, selection(menu)), offset);
    if (!menu_flag(menu, 3)) {
        while (index >= 0 && item_hidden(menu, index) && index != selection(menu))
            index = field<int16_t>(menu_item(menu, index), offset);
        if (index == selection(menu)) return;
    }
    if (index >= 0) select(menu, index, 1);
}

}  // namespace

void XB_THISCALL UIGridMenu_MoveRight(uint8_t* menu) { grid_move(menu, 0xe); }
void XB_THISCALL UIGridMenu_MoveLeft(uint8_t* menu) { grid_move(menu, 0xc); }
void XB_THISCALL UIGridMenu_MoveDown(uint8_t* menu) { grid_move(menu, 0xa); }
void XB_THISCALL UIGridMenu_MoveUp(uint8_t* menu) { grid_move(menu, 8); }

// --- Screen managers ---

PLATFORM_ASM_ENTRY(UIScreenMgr_Init_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIScreenMgr_Init) "addl $12, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL UIScreenMgr_Init(uint8_t* mgr, int32_t capacity, uint8_t* owner) {
    field<int32_t>(mgr, 0x10) = capacity;
    field<uint32_t>(mgr, 0) = 0x0082a5f8;  // vtable
    field<uint8_t*>(mgr, 4) = heap_alloc16(uint32_t(capacity) << 2);
    field<uint32_t>(mgr, 0x14) = 0;
    field<uint8_t*>(mgr, 8) = owner;
    field<int32_t>(mgr, 0xc) = -1;
    mgr[0x1a] = 0;
    call(mgr, 0x18);  // create the screens
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) call(screen(mgr, i), 0x1c);
    return mgr;
}

void XB_THISCALL UIScreenMgr_Dtor(uint8_t* mgr) {
    field<uint32_t>(mgr, 0) = 0x0082a5f8;  // vtable
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) {
        if (i == 16) continue;
        uint8_t* const s = screen(mgr, i);
        if (s) call(s, 0, 1);
    }
    z_free(field<void*>(mgr, 4));
}

uint8_t* XB_THISCALL UIScreenMgr_DeletingDtor(uint8_t* mgr, uint32_t flags) {
    UIScreenMgr_Dtor(mgr);
    if (flags & 1) z_free(mgr);
    return mgr;
}

void XB_THISCALL UIScreenMgr_Update(uint8_t* mgr, uint32_t dt) {
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0 && screen(mgr, active)) call(screen(mgr, active), 0x30, dt);
    call(mgr, 0x14);
}

void XB_THISCALL UIScreenMgr_LayoutAll(uint8_t* mgr) {
    call(mgr, 0x18);
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) call(screen(mgr, i), 0x1c);
}

// Every call runs where the original's puts it, with the sound's origin / axis / handle locals at the original's
// frame addresses: S_PlaySoundAlias keeps their addresses (host sound state differed in the stack address byte).
PLATFORM_ASM_ENTRY(UIScreenMgr_SetActiveScreen_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 4(%ebp)\n"
                   "pushl %ecx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(UIScreenMgr_SetActiveScreen) "leal -0x20004(%ebp), %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret $4")

void XB_CDECL UIScreenMgr_SetActiveScreen(uint32_t entry, uint8_t* mgr, int32_t index) {
    const uint32_t top = ((entry - 4) & ~0xfu) - 0x30;  // push ebp; and esp,-16; sub esp,0x24; three pushes
    TitleRegs regs{};
    regs.esi = address_of(mgr);
    regs.edi = uint32_t(index);
    const auto method_at = [&](uint8_t* object, uint32_t offset, uint32_t count, const uint32_t* args) {
        regs.ecx = address_of(object);
        title_call_at_regs(top, field<uint32_t>(field<uint8_t*>(object, 0), offset), &regs, count, args);
    };
    if (field<int32_t>(mgr, 0xc) != -1) {
        const uint32_t next[1] = {index >= 0 ? address_of(screen(mgr, index)) : 0};
        method_at(screen(mgr, field<int32_t>(mgr, 0xc)), 0x44, 1, next);
    }
    const uint32_t old[1] = {uint32_t(field<int32_t>(mgr, 0xc))};
    field<int32_t>(mgr, 0xc) = index;
    if (index >= 0) {
        method_at(screen(mgr, index), 0x3c, 1, old);
        uint32_t* const handle = reinterpret_cast<uint32_t*>(uintptr_t{top + 0xc});
        uint32_t* const origin = reinterpret_cast<uint32_t*>(uintptr_t{top + 0x10});  // (0, 0, 0, 0)
        uint32_t* const axis = reinterpret_cast<uint32_t*>(uintptr_t{top + 0x20});    // (0, 0, 0, 1)
        for (uint32_t i = 0; i < 4; ++i) {
            origin[i] = title_global<uint32_t>(0x0082e110 + i * 4);
            axis[i] = title_global<uint32_t>(0x0082e290 + i * 4);
        }
        *handle = title_global<uint32_t>(0x0095f4c4);
        const uint32_t name[1] = {0x0082cbf0};  // "UI_Highlight"
        regs.ecx = 0;
        regs.ebx = 0;
        const uint32_t alias = title_call_at_regs(top, 0x000c5810, &regs, 1, name);  // S_FindSoundByHash
        if (alias != 0xffffffffu) {
            const uint32_t args[10] = {address_of(handle), 0, 1, 0, address_of(axis), address_of(origin),
                                       0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000};
            regs.eax = alias;
            regs.ecx = *handle;
            regs.edx = address_of(axis);
            title_call_at_regs(top, 0x002c30a0, &regs, 10, args);  // S_PlaySoundAlias
        }
    }
    method_at(mgr, 0x18, 0, nullptr);
    field<int32_t>(mgr, 0xc) = index;
}

int32_t XB_REGS1 XB_CDECL UIScreenMgr_PadBitToButton(int32_t bit) {
    switch (bit) {
        case 1: return 15;
        case 4: return 3;
        case 8: return 1;
        case 0x10: return 0;
        case 0x20: return 2;
        case 0x40: return 5;
        case 0x80: return 6;
        case 0x100: return 4;
        case 0x200: return 7;
        case 0x400: return 9;
        case 0x800: return 8;
        case 0x1000: return 11;
        case 0x2000: return 10;
        default: return 14;
    }
}

void XB_THISCALL UIScreenMgr_OnButtonDown(uint8_t* mgr, int32_t bit, int32_t pad) {
    if (field<int32_t>(mgr, 0xc) < 0) return;
    uint8_t* const active = screen(mgr, field<int32_t>(mgr, 0xc));
    call(active, 0x80, uint32_t(pad), uint32_t(bit));
    uint32_t slot;
    switch (bit) {
        case 1: slot = 0x48; break;
        case 2: slot = 0x4c; break;
        case 4: slot = 0x50; break;
        case 8: slot = 0x54; break;
        case 0x10: slot = 0x58; break;
        case 0x20: slot = 0x5c; break;
        case 0x40: slot = 0x60; break;
        case 0x80: slot = 0x64; break;
        case 0x100: slot = 0x68; break;
        case 0x200: slot = 0x6c; break;
        case 0x400: slot = 0x70; break;
        case 0x800: slot = 0x74; break;
        case 0x1000: slot = 0x78; break;
        case 0x2000: slot = 0x7c; break;
        default: return;
    }
    uint8_t* const s = screen(mgr, field<int32_t>(mgr, 0xc));
    call(s, slot, uint32_t(pad));
}

namespace {

// 0x0030ea00: whether the active pad's stick (or d-pad) points in the UI direction bit (4-32).
bool stick_direction_held(int32_t bit) { return title_function<uint8_t XB_CDECL(int32_t)>(0x0030ea00)(bit) != 0; }

using PadCreate = uint8_t* XB_CDECL();
void pad_create() { title_function<PadCreate>(0x00055e40)(); }

}  // namespace

void XB_THISCALL UIScreenMgr_ReadStickDirections(uint8_t* mgr) {
    for (int32_t bit = 4; bit <= 0x20; bit <<= 1) {
        if (stick_direction_held(bit))
            field<uint16_t>(mgr, 0x18) |= uint16_t(bit);
        else
            field<uint16_t>(mgr, 0x18) &= uint16_t(~bit);
    }
}

void XB_THISCALL UIScreenMgr_HandleInput(uint8_t* mgr) {
    int32_t& active_pad = title_global<int32_t>(0x008d50d8);
    for (int32_t bit = 1; bit < 0x4000; bit <<= 1) {
        if (active_pad == -1) {
            const int32_t button = UIScreenMgr_PadBitToButton(bit);
            for (int32_t pad = 0; pad < 4; ++pad) {
                pad_create();
                if (IN_PadButtonPressed(pad, uint32_t(button))) {
                    uint8_t* const live = title_global<uint8_t*>(0x0095f6fc);  // g_liveMgr
                    title_global<int32_t>(0x008dcf18) = pad;
                    active_pad = pad;
                    field<int32_t>(live, 0x44d0) = pad;
                    break;
                }
            }
        }
        if (bit >= 4 && bit <= 0x20) {
            if (stick_direction_held(bit) && !(field<int16_t>(mgr, 0x18) & bit)) {
                call(mgr, 0x28, uint32_t(bit), uint32_t(active_pad));
                field<uint16_t>(mgr, 0x18) |= uint16_t(bit);
                return;
            }
            if (!stick_direction_held(bit) && (field<int16_t>(mgr, 0x18) & bit)) {
                call(mgr, 0x2c, uint32_t(bit), uint32_t(active_pad));
                field<uint16_t>(mgr, 0x18) &= uint16_t(~bit);
                return;
            }
        }
        const uint32_t button = uint32_t(UIScreenMgr_PadBitToButton(bit));
        pad_create();
        if (IN_PadButtonPressed(active_pad, button)) {
            const uint32_t again = uint32_t(UIScreenMgr_PadBitToButton(bit));
            pad_create();
            IN_PadConsumePressed(active_pad, again);
            call(mgr, 0x28, uint32_t(bit), uint32_t(active_pad));
            return;
        }
        pad_create();
        if (IN_PadConsumeReleased(active_pad, button)) {
            const uint32_t again = uint32_t(UIScreenMgr_PadBitToButton(bit));
            pad_create();
            title_function<uint8_t XB_STDCALL(int32_t, uint32_t)>(0x00056570)(active_pad, again);  // IN_PadReturnFalse
            call(mgr, 0x2c, uint32_t(bit), uint32_t(active_pad));
            return;
        }
    }
}

// --- Button tokens ---

uint8_t XB_REGS3 XB_CDECL UI_MatchButtonToken(const char* token, uint32_t, const char* text, uint32_t* out,
                                              uint32_t glyph, int32_t* length) {
    const int32_t n = int32_t(std::strlen(token));
    for (int32_t i = 0; i < n; ++i) {
        const char t = text[i];
        const char k = token[i];
        if (t == k) {
            if (t == 0 && k == 0) break;
            continue;
        }
        if (t < 'A' || t > 'Z' || k != t + 0x20) return 0;
    }
    *out = glyph;
    *length = n;
    return 1;
}

PLATFORM_ASM_ENTRY(UI_TranslateButtonToken_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(UI_TranslateButtonToken) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL UI_TranslateButtonToken(const char* text, uint32_t* out) {
    if (*text != '~') return 0;
    // (token, glyph) string pairs in the original's order.
    static const uint32_t kTokens[19][2] = {
        {0x0082d7b4, 0x0082d7bc},  // "~cross" '"'
        {0x0082d7a8, 0x0082d7b0},  // "~circle" '%'
        {0x0082d79c, 0x0082d7a4},  // "~square" '#'
        {0x0082d78c, 0x0082d798},  // "~triangle" '$'
        {0x0082d780, 0x0082d788},  // "~gc_z" '('
        {0x0082d77c, 0x0082d788},  // "~l1" '('
        {0x0082d774, 0x0082d778},  // "~r1" '\''
        {0x0082d768, 0x0082d7b0},  // "~menu_back" '%'
        {0x0082d760, 0x0082d764},  // "~r2" '!'
        {0x0082d758, 0x0082d75c},  // "~l2" '&'
        {0x0082d74c, 0x0082d754},  // "~right" '*'
        {0x0082d740, 0x0082d748},  // "~back" ','
        {0x0082d730, 0x0082d73c},  // "~forward" ')'
        {0x0082d724, 0x0082d72c},  // "~left" '+'
        {0x0082d718, 0x0082d720},  // "~start" '-'
        {0x0082d710, 0x0082e058},  // "~select" '1'
        {0x0082d704, 0x0082d70c},  // "~updown" '/'
        {0x0082d6f4, 0x0082d700},  // "~both_lr" '.'
        {0x0082d6e8, 0x0082e098},  // "~navig_all" '0'
    };
    *out = 0x0082dfeb;  // ""
    int32_t length = 0;
    for (const auto& pair : kTokens)
        if (UI_MatchButtonToken(text_at(pair[0]), 0, text, out, pair[1], &length)) return length;
    return 0;
}

// --- Drawing ---

void XB_STDCALL UI_DrawPillarboxBars(uint8_t* object) {
    NglQuad quad;
    nglInitQuad(&quad);
    uint8_t* const element = field<uint8_t*>(object, 0xc);
    const long double depth = method<long double XB_THISCALL(uint8_t*)>(element, 0x28)(element);
    nglSetQuadZ(&quad, float(depth - title_global<float>(0x0083eb90)));  // 100
    nglSetQuadRect(&quad, 0.0f, 0.0f, 80.0f, 480.0f);
    nglSetQuadColor(&quad, 0xff000000);
    nglListAddQuad(&quad);
    nglSetQuadRect(&quad, 560.0f, 0.0f, 640.0f, 480.0f);
    nglListAddQuad(&quad);
}

// --- Font tables ---

namespace {

// A 31-character copy of the name (the title's _strncpy) looked up with R_FindFont.
int32_t find_font(const char* name) {
    char copy[32];
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(copy, name, 0x1f);  // _strncpy
    copy[31] = 0;
    return R_FindFont(copy, 1);
}

}  // namespace

PLATFORM_ASM_ENTRY(UIFontTable_SetByName_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIFontTable_SetByName) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL UIFontTable_SetByName(const char* name, uint8_t* font, uint8_t* table) {
    const int32_t index = find_font(name);
    if (index != 8) {
        field<uint8_t*>(table, 4 + uint32_t(index) * 4) = font;
        table[0x3c + index] = font != nullptr;
    }
    return index;
}

PLATFORM_ASM_ENTRY(UI_SetFontByName_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UI_SetFontByName) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL UI_SetFontByName(const char* name, uint8_t* font) {
    const int32_t index = find_font(name);
    if (index != 8) {
        title_global<uint8_t*>(0x0095f8d4 + uint32_t(index) * 4) = font;  // ui_fonts
        title_global<uint8_t>(0x0095f90c + uint32_t(index)) = font != nullptr;  // ui_fontLoaded
    }
    return index;
}

PLATFORM_ASM_ENTRY(UIFontTable_AcquireFont_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(UIFontTable_AcquireFont) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL UIFontTable_AcquireFont(int32_t index, uint8_t* table) {
    const char* const name = text_at(0x0095f7e8 + uint32_t(index) * 32);
    using HashStrInit = char* XB_THISCALL(char*, const char*);
    char hashed[32];
    title_function<HashStrInit>(0x00011060)(hashed, name);  // HashStr_Init
    nglFindTexture(hashed);
    title_function<HashStrInit>(0x00011060)(hashed, name);
    field<void*>(table, 4 + uint32_t(index) * 4) = nglFindFontAddRef(hashed);
}

}  // namespace game

namespace game {

// --- Scroll bar ---

PLATFORM_ASM_ENTRY(UIScrollBar_Construct_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(UIScrollBar_Construct) "addl $4, %esp\n"
                   "ret")

namespace {

// A 0x40-byte picture of the name with one quad (vtable +0x3c: points, colours, 1, depth, no texture).
uint8_t* scroll_picture(uint32_t name, const float* points, const uint32_t* colors, uint32_t depth) {
    uint8_t* const block = z_malloc(0x40);
    uint8_t* const picture = block ? UIPicture_Init(block, text_at(name)) : nullptr;
    method<void XB_THISCALL(uint8_t*, const float*, const uint32_t*, uint32_t, uint32_t, const char*)>(picture, 0x3c)(
        picture, points, colors, 1, depth, text_at(0x0082dfeb));
    return picture;
}

// A 0x98-byte UI text (kind 0, layer 1, scale 0.6) at (x, y), black, flag 0x40, +0xe4 (20).
uint8_t* scroll_text(uint32_t x, uint32_t y) {
    uint8_t* const block = z_malloc(0x98);
    uint8_t* const text = block ? UIText_Construct(block, 0, x, y, 0, 1, 0x3f19999a, 0, 0, 0) : nullptr;
    return text;
}

void scroll_text_style(uint8_t* text) {
    call(text, 0x50, 0x40);
    call(text, 0x90, 0xff000000);
    call(text, 0xe4, 0x14);
}

}  // namespace

void XB_CDECL UIScrollBar_Construct(uint8_t* bar) {
    field<uint8_t*>(bar, 0x28) = scroll_text(0, 0);
    scroll_text_style(field<uint8_t*>(bar, 0x28));
    call(field<uint8_t*>(bar, 0x28), 0x13c, 3);
    field<uint8_t*>(bar, 0x2c) = scroll_text(0xc2200000, 0xc2200000);  // (-40, -40)
    scroll_text_style(field<uint8_t*>(bar, 0x2c));
    call(field<uint8_t*>(bar, 0x2c), 0x110, 0x3f4ccccd);  // 0.8
    const auto g = [](uint32_t address) { return title_global<float>(address); };
    const uint32_t grey[4] = {0xffc8c8c8, 0xffc8c8c8, 0xffc8c8c8, 0xffc8c8c8};
    // The back (471..475 x 132..348), the arrows (centre 472.5: 125/130 and 350/355) and the indicator (467.5..477.5 x
    // 130..135).
    const float back[12] = {g(0x0083ef5c), g(0x0083ef58), 0.0f, g(0x0083ef54), g(0x0083ef58), 0.0f,
                            g(0x0083ef5c), g(0x0083ef50), 0.0f, g(0x0083ef54), g(0x0083ef50), 0.0f};
    field<uint8_t*>(bar, 8) = scroll_picture(0x0082ca98, back, grey, 0x42a00000);  // "scroll_back", 80
    const float top[12] = {g(0x0083ef48), g(0x0083ef4c), 0.0f, g(0x0083ef48), g(0x0083ef4c), 0.0f,
                           g(0x0083ef44), g(0x0083ef3c), 0.0f, g(0x0083ef40), g(0x0083ef3c), 0.0f};
    field<uint8_t*>(bar, 0) = scroll_picture(0x0082ca84, top, grey, 0x42a00000);  // "scroll_arrow_top"
    const float bottom[12] = {g(0x0083ef44), g(0x0083ef38), 0.0f, g(0x0083ef40), g(0x0083ef38), 0.0f,
                              g(0x0083ef48), g(0x0083ef34), 0.0f, g(0x0083ef48), g(0x0083ef34), 0.0f};
    field<uint8_t*>(bar, 4) = scroll_picture(0x0082ca70, bottom, grey, 0x42a00000);  // "scroll_arrow_bottom"
    const float indicator[12] = {g(0x0083ef44), g(0x0083ef3c), 0.0f, g(0x0083ef40), g(0x0083ef3c), 0.0f,
                                 g(0x0083ef44), g(0x0083ef30), 0.0f, g(0x0083ef40), g(0x0083ef30), 0.0f};
    field<uint8_t*>(bar, 0xc) = scroll_picture(0x0082ca5c, indicator, grey, 0x42480000);  // "scroll_indicator", 50
    field<float>(bar, 0x34) = g(0x0083ef48);
}

}  // namespace game

// Calls a title function taking ESI and EDI and two stack words it pops (ret 8): by-value ScriptStrings.
extern "C" PLATFORM_HIDDEN void XB_CDECL ui_call_esi_edi_2(uint32_t function, uint32_t esi, uint32_t edi, uint32_t w0,
                                                          uint32_t w1);
PLATFORM_ASM_ENTRY(ui_call_esi_edi_2,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "movl 12(%ebp), %esi\n"
                   "movl 16(%ebp), %edi\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

namespace game {

namespace {

using StringMethod = void XB_THISCALL(ScriptString*, const char*);
void string_release(ScriptString* s) { title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(s); }
// A by-value copy (the callee releases it).
uint32_t string_copy(const ScriptString* s) {
    ScriptString copy;
    ScriptString_Copy(&copy, s);
    return address_of(copy.rep);
}

}  // namespace

// The original keeps its two strings in stack slots (the message in the manager's argument slot, the heading in the
// pushed-ECX slot) and the text setters it reaches keep a stack address: the entry moves the C frame below a gap and
// every call runs at the original's body ESP (entry - 0x14) with its return address.
PLATFORM_ASM_ENTRY(UI_ShowDialog_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                                        "pushl %edi\n pushl %esi\n pushl %ebp\n pushl %ebx\n pushl %edx\n pushl %eax\n"
                                        PLATFORM_ASM_CALL(UI_ShowDialogAt) "addl $24, %esp\n popl %esp\n ret $0x14")

namespace {

struct DialogFrame {
    uint32_t body;  // the original's ESP after its five pushes
    TitleRegs regs{};

    // A title call at the body ESP (args[0] pushed last) returning to `ret`.
    uint32_t call(uint32_t ret, uint32_t function, uint32_t count = 0, const uint32_t* args = nullptr,
                  uint32_t top_offset = 0) {
        const uint32_t top = body - top_offset;
        const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
        *reinterpret_cast<uint32_t*>(uintptr_t{top - 4 * count - 4}) = ret;
        return result;
    }
    uint32_t method(uint32_t ret, uint32_t object, uint32_t function, uint32_t count = 0, const uint32_t* args = nullptr,
                    uint32_t top_offset = 0) {
        regs.ecx = object;
        return call(ret, function, count, args, top_offset);
    }
    // A by-value copy of `source` into the argument slot at body - top_offset - 4 (ScriptString_Copy).
    void copy(uint32_t ret, uint32_t top_offset, uint32_t source) {
        const uint32_t args[1] = {source};
        method(ret, body - top_offset - 4, 0x0011d5e0, 1, args, top_offset + 4);
    }
    // A dialog button: text (+0x30), handler, +0x20 (0), count +0x34.
    void button(uint8_t* dialog, uint32_t label, uint32_t handler, uint32_t ret_text, uint32_t ret_flag) {
        regs.esi = address_of(dialog);
        const uint32_t text_args[1] = {label};
        uint8_t* item = menu_item(dialog, field<int32_t>(dialog, 0x34));
        method(ret_text, address_of(item), field<uint32_t>(field<uint8_t*>(item, 0), 0x30), 1, text_args);
        field<uint32_t>(dialog, 0x38 + field<uint32_t>(dialog, 0x34) * 4) = handler;
        item = menu_item(dialog, field<int32_t>(dialog, 0x34));
        const uint32_t flag_args[1] = {0};
        method(ret_flag, address_of(item), field<uint32_t>(field<uint8_t*>(item, 0), 0x20), 1, flag_args);
        ++field<int32_t>(dialog, 0x34);
    }
};

}  // namespace

void XB_CDECL UI_ShowDialogAt(const char* name, uint32_t entry, uint32_t ebx, uint32_t ebp, uint32_t esi,
                              uint32_t edi) {
    uint32_t* const slots = reinterpret_cast<uint32_t*>(uintptr_t{entry});
    uint8_t* const mgr = reinterpret_cast<uint8_t*>(uintptr_t{slots[1]});
    const uint8_t ok = uint8_t(slots[2]), yes_no = uint8_t(slots[3]), keep_running = uint8_t(slots[5]);
    const char* const title = reinterpret_cast<const char*>(uintptr_t{slots[4]});
    slots[-2] = ebx;  // push ebx / ebp / esi / edi (push ecx's slot holds the heading)
    slots[-3] = ebp;
    slots[-4] = esi;
    slots[-5] = edi;
    const uint32_t message = entry + 4, heading = entry - 4;  // ScriptStrings in the stack slots
    DialogFrame f{entry - 0x14};
    f.regs.ebx = title_global<uint32_t>(0x0095f5d8);  // rm_locStrings
    f.regs.esi = f.regs.edi = address_of(name);
    const char* text = name;
    if (name) {
        f.regs.eax = address_of(name);
        const uint32_t hash = f.call(0x003319fc, 0x00024440);  // Loc_HashKey
        f.regs.eax = f.regs.ebx;
        const uint32_t hash_args[1] = {hash};
        const uint8_t* const entry_rec =
            reinterpret_cast<const uint8_t*>(uintptr_t{f.call(0x00331a04, 0x002984f0, 1, hash_args)});  // Zone_FindByHash
        if (entry_rec) {
            text = field<const char*>(const_cast<uint8_t*>(entry_rec), 8);
            if (!text) text = text_at(0x008379b4);  // "NO STRING"
        }
    }
    f.regs.edi = address_of(text);
    const uint32_t from_text[1] = {address_of(text)};
    f.method(0x00331a1e, message, 0x0011eb50, 1, from_text);  // ScriptString_FromCString
    const uint32_t from_empty[1] = {0x0082dfeb};  // ""
    f.method(0x00331a2c, heading, 0x0011eb50, 1, from_empty);
    if (*title) {
        const uint32_t from_title[1] = {address_of(title)};
        f.method(0x00331a3f, heading, 0x0011dd20, 1, from_title);  // ScriptString_AssignCString
    }
    const auto* const rep = *reinterpret_cast<const uint8_t* const*>(uintptr_t{message});
    const bool has_text = rep && field<uint16_t>(const_cast<uint8_t*>(rep), 2);
    // The two copies of the same sequence (message as is / replaced by the name).
    static constexpr uint32_t kRets[2][5] = {{0x00331a5a, 0x00331a67, 0x00331a74, 0x00331a7c, 0x00331a99},
                                             {0x00331ab8, 0x00331ac5, 0x00331ad2, 0x00331ada, 0x00331af7}};
    const uint32_t* const rets = kRets[has_text ? 0 : 1];
    if (!has_text) {
        const uint32_t from_name[1] = {address_of(name)};
        f.method(0x00331aad, message, 0x0011dd20, 1, from_name);
    }
    f.regs.eax = 0;
    f.regs.edx = keep_running;
    f.method(rets[0], address_of(mgr), 0x0032dec0);  // CL_PauseForDialog
    f.copy(rets[1], 0, heading);
    f.copy(rets[2], 4, message);
    f.regs.esi = field<uint32_t>(mgr, 0x1c);
    const uint32_t set_args[2] = {*reinterpret_cast<uint32_t*>(uintptr_t{f.body - 8}),
                                  *reinterpret_cast<uint32_t*>(uintptr_t{f.body - 4})};
    f.call(rets[3], 0x00313f20, 2, set_args);  // UIDialog_SetText (takes both copies)
    uint8_t* const dialog = dialog_of(mgr);
    f.regs.edi = address_of(dialog);
    f.copy(rets[4], 0, heading);
    f.copy(0x00331b04, 4, message);
    const uint32_t dialog_args[2] = {*reinterpret_cast<uint32_t*>(uintptr_t{f.body - 8}),
                                     *reinterpret_cast<uint32_t*>(uintptr_t{f.body - 4})};
    f.call(0x00331b09, 0x0030e6f0, 2, dialog_args);
    if (ok || yes_no) {
        if (ok) {
            f.button(dialog_of(mgr), 0x0082c9ec, 0x00313dc0, 0x00331b34, 0x00331b4f);  // "INGAME_DIALOG_OK"
        } else {
            f.button(dialog_of(mgr), 0x0082c9d8, 0x00313dc0, 0x00331b92, 0x00331bad);  // "INGAME_DIALOG_YES"
            f.button(dialog_of(mgr), 0x0082c9c4, 0x00313db0, 0x00331bd3, 0x00331bee);  // "INGAME_DIALOG_NO"
        }
        uint8_t* const layout = dialog_of(mgr);
        f.regs.eax = (field<uint32_t>(mgr, 0xc) & ~0xffu) | (ok ? 1 : 0);
        f.regs.ecx = field<uint32_t>(mgr, 0xc) != 0;
        f.regs.edx = field<uint32_t>(mgr, 4);
        f.regs.edi = address_of(layout);
        f.call(0x00331c08, 0x0031f4b0);  // Dialog_LayoutButtons
    }
    f.method(0x00331c11, heading, 0x0011d640);  // ScriptString_Release
    f.method(0x00331c1a, message, 0x0011d640);
}

void XB_REGS1 XB_STDCALL UI_ShowDialog(const char* name, uint8_t* mgr, uint8_t ok, uint8_t yes_no, const char* title,
                                       uint8_t keep_running) {
    using Show = void XB_REGS1 XB_STDCALL(const char*, uint8_t*, uint8_t, uint8_t, const char*, uint8_t);
    title_function<Show>(0x003319e0)(name, mgr, ok, yes_no, title, keep_running);
}

// --- Dialogs ---

namespace {

// The dialog overlay (the dialog manager's +0x1c) of the UI root's dialog manager (0x0095f900).
uint8_t* root_overlay() { return field<uint8_t*>(title_global<uint8_t*>(0x0095f900), 0x1c); }
int32_t line_count(uint8_t* text) { return method<int32_t XB_THISCALL(uint8_t*)>(text, 0xc4)(text); }
// The overlay's lines: the body's (+0x2c) and, when the heading (+0x28) has any, the heading's plus one.
int32_t overlay_lines(uint8_t* overlay) {
    const int32_t body = line_count(field<uint8_t*>(overlay, 0x2c));
    int32_t heading = 0;
    if (line_count(field<uint8_t*>(overlay, 0x28)) > 0) heading = line_count(field<uint8_t*>(overlay, 0x28)) + 1;
    return heading + body;
}
// Whether a text's text (vtable +0xa0, a copy) is not "".
bool has_text(uint8_t* text) {
    ScriptString copy;
    const ScriptString* const s =
        method<const ScriptString* XB_THISCALL(uint8_t*, ScriptString*)>(text, 0xa0)(text, &copy);
    const bool empty = ScriptString_Equals(s, text_at(0x0082dfeb));  // ""
    string_release(&copy);
    return !empty;
}
void delete_object(uint8_t* object) {
    if (object) call(object, 0, 1);
}
void set_position(uint8_t* object, float x, float y) { method<PositionMethod>(object, 0x2c)(object, x, y); }

}  // namespace

PLATFORM_ASM_ENTRY(Dialog_LayoutButtons_entry,
                   "pushl %edi\n"
                   "movzbl %al, %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Dialog_LayoutButtons) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Dialog_LayoutButtons(uint8_t vertical, uint8_t* dialog) {
    dialog[0x58] = vertical;
    TitleRegs regs{};
    regs.eax = dialog[0x59];
    regs.esi = address_of(root_overlay());
    const uint32_t args[1] = {vertical ? field<uint32_t>(dialog, 0x34) : 1u};
    ui_call_regs(0x00313e40, &regs, 1, args);  // the overlay's layout for the button count
    for (int32_t i = 0; i < field<int32_t>(dialog, 0x34); ++i) call(menu_item(dialog, i), 0x4c, 0);
    if (vertical) {
        for (int32_t i = 0; i < field<int32_t>(dialog, 0x34); ++i) {
            int32_t lines = overlay_lines(root_overlay());
            if (lines > 10) lines = 10;
            set_position(menu_item(dialog, i), 320.0f, float((lines + i * 2 + 0x19) * 10));
        }
    } else {
        int32_t lines = overlay_lines(root_overlay());
        if (lines > 10) lines = 10;
        const float y = platform_x87_int_to_float((lines * 5 + 0x7d) * 2);
        set_position(menu_item(dialog, 0), 280.0f, y);
        set_position(menu_item(dialog, 1), 360.0f, y);
    }
    if (!dialog[0x5a]) {
        call(dialog, 0x34);
        return;
    }
    select(dialog, field<int16_t>(dialog, 0x22), 1);
    dialog[0x5a] = 0;
}

namespace {

// A thiscall method (or any title function) run where the original's call puts it: ESP = top minus the arguments.
uint32_t call_at(uint32_t top, uint32_t function, uint32_t ecx, uint32_t count, const uint32_t* args) {
    TitleRegs regs{};
    regs.ecx = ecx;
    return title_call_at_regs(top, function, &regs, count, args);
}
uint32_t slot_of(uint8_t* object, uint32_t offset) { return field<uint32_t>(field<uint8_t*>(object, 0), offset); }
uint32_t method_at(uint32_t top, uint8_t* object, uint32_t offset, uint32_t count = 0, const uint32_t* args = nullptr) {
    return call_at(top, slot_of(object, offset), address_of(object), count, args);
}

}  // namespace

// The callees run at the original's stack positions (a title callee keeps a stack address in the heap at
// 0x100006a8): the C frame sits below a gap; the expanded text and the colour word are the original's locals at
// entry - 4 / entry - 8, the by-value strings its argument slots.
// EBP is saved below the gap: the original's expanded text lives at entry - 4.
PLATFORM_ASM_ENTRY(UIDialog_SetText_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(UIDialog_SetText) "leal -0x20004(%ebp), %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret $8")

void XB_CDECL UIDialog_SetText(uint32_t entry, uint8_t* overlay) {
    const uint32_t top = entry - 0x14;  // after sub esp,8 and three pushes
    ScriptString* const text = reinterpret_cast<ScriptString*>(uintptr_t{entry + 4});
    ScriptString* const heading = reinterpret_cast<ScriptString*>(uintptr_t{entry + 8});
    ScriptString* const expanded = reinterpret_cast<ScriptString*>(uintptr_t{entry - 4});
    uint32_t& word = *reinterpret_cast<uint32_t*>(uintptr_t{entry - 8});
    {
        ScriptString_Copy(reinterpret_cast<ScriptString*>(uintptr_t{entry - 0x18}), text);
        TitleRegs regs{};
        regs.edi = address_of(expanded);
        const uint32_t args[1] = {field<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{entry - 0x18}), 0)};
        title_call_at_regs(top, 0x0030f4d0, &regs, 1, args);  // ScriptString_ExpandNewlines (out in EDI)
    }
    {
        const uint32_t args[3] = {string_copy(expanded), 0x118, 0xbf800000};  // width 280, -1.0
        method_at(top, field<uint8_t*>(overlay, 0x2c), 0x14c, 3, args);
    }
    {
        const uint32_t args[3] = {string_copy(heading), 0x118, 0xbf800000};
        method_at(top, field<uint8_t*>(overlay, 0x28), 0x144, 3, args);
    }
    word = title_global<uint32_t>(0x0095f8f8) ? 0xff000000u : 0xffffffffu;
    {
        const uint32_t args[1] = {word};
        method_at(top, field<uint8_t*>(overlay, 0x2c), 0x90, 1, args);
    }
    overlay[0x44] = 0;
    const int32_t body_lines = int32_t(method_at(top, field<uint8_t*>(overlay, 0x2c), 0xc4));
    int32_t lines = 0;
    if (int32_t(method_at(top, field<uint8_t*>(overlay, 0x28), 0xc4)) > 0) {
        lines = int32_t(method_at(top, field<uint8_t*>(overlay, 0x28), 0xc4)) + 1;
    }
    uint8_t flags = overlay[0x44];
    if (lines + body_lines > 10) {
        flags |= 4;
    } else {
        flags &= ~4;
    }
    overlay[0x44] = flags;
    reinterpret_cast<uint8_t*>(&word)[0] = (flags >> 2) & 1;  // the scroll flag over the colour's upper bytes
    {
        const uint32_t args[1] = {word};
        for (uint32_t offset = 0; offset < 0x10; offset += 4) {
            method_at(top, field<uint8_t*>(overlay, offset), 0x34, 1, args);
        }
    }
    int32_t heading_lines = 0;
    {
        const uint32_t args[2] = {address_of(heading), 0x0082dfeb};  // ""
        if (!(call_at(top, 0x0011d7c0, 0, 2, args) & 0xff)) {  // ScriptString_Equals
            heading_lines = int32_t(method_at(top, field<uint8_t*>(overlay, 0x28), 0xc4)) + 1;
        }
    }
    if ((overlay[0x44] >> 2) & 1) {
        field<float>(overlay, 0x3c) = 0.0f;
        overlay[0x44] &= 0xfc;
        uint8_t* const body = field<uint8_t*>(overlay, 0x2c);
        const long double height = method<long double XB_THISCALL(uint8_t*)>(body, 0xd4)(body);
        word = uint32_t(heading_lines * 20);
        const int32_t extent = int32_t(int64_t(height + (long double)int32_t(word)));  // FIADD, _ftol
        field<float>(overlay, 0x38) = float(0xc8 - extent);
        const uint32_t half[1] = {0x3f000000};  // 0.5
        method_at(top, field<uint8_t*>(overlay, 0), 0x78, 1, half);
        method_at(top, field<uint8_t*>(overlay, 4), 0x78, 1, half);
        const uint32_t args[2] = {field<uint32_t>(overlay, 0x34), 0x43068000};  // 134.5
        method_at(top, field<uint8_t*>(overlay, 0xc), 0x8c, 2, args);
    }
    const uint32_t flag[1] = {0x40};
    method_at(top, field<uint8_t*>(overlay, 0x2c), 0x50, 1, flag);
    method_at(top, field<uint8_t*>(overlay, 0x28), 0x50, 1, flag);
    call_at(top, 0x0011d640, address_of(expanded), 0, nullptr);  // ScriptString_Release
    call_at(top, 0x0011d640, address_of(text), 0, nullptr);
    call_at(top, 0x0011d640, address_of(heading), 0, nullptr);
}

void XB_THISCALL UIDialog_Draw(uint8_t* dialog) {
    for (int32_t i = 0; i < 3; ++i) call(menu_item(dialog, i), 0x58, title_global<uint32_t>(0x0095f8f8) ? 3 : 4);
    if (field<float>(root_overlay(), 0x40) >= 0.0f) UIMenu_Draw(dialog);
}

uint8_t* XB_THISCALL UIDialog_DeletingDtor(uint8_t* dialog, uint32_t flags) {
    field<uint32_t>(dialog, 0) = 0x00829d48;  // UIDialog vtable
    string_release(reinterpret_cast<ScriptString*>(dialog + 0x50));
    string_release(reinterpret_cast<ScriptString*>(dialog + 0x4c));
    field<uint32_t>(dialog, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(dialog);
    if (flags & 1) z_free(dialog);
    return dialog;
}

uint8_t* XB_REGS3 XB_CDECL UIDialog_Construct(uint8_t* owner, uint32_t, uint8_t* dialog) {
    UIMenu_Init(dialog, 3, owner, 0, 0, 8, 0);
    field<uint32_t>(dialog, 0) = 0x00829d48;  // UIDialog vtable
    ScriptString_FromRep(reinterpret_cast<ScriptString*>(dialog + 0x4c), nullptr);
    ScriptString_FromRep(reinterpret_cast<ScriptString*>(dialog + 0x50), nullptr);
    dialog[0x33] = 3;
    for (int32_t i = 0; i < 3; ++i) {
        uint8_t* item = z_malloc(0x18);
        if (item) item = UIMenuItem_Init(8, item, text_at(0x0082dfeb), dialog, 1);
        field<uint8_t**>(dialog, 4)[i] = item;
    }
    for (int32_t i = 0; i < item_count(dialog); ++i) call(menu_item(dialog, i), 0x5c, 0x3f19999a);  // 0.6
    dialog[0x2a] |= 0x40;
    field<uint32_t>(dialog, 0x34) = 0;
    dialog[0x58] = 0;
    dialog[0x59] = 0;
    field<uint32_t>(dialog, 0x44) = 0;
    field<uint32_t>(dialog, 0x48) = 0;
    dialog[0x5a] = 0;
    field<uint32_t>(dialog, 0x54) = title_global<uint32_t>(0x0083eb34);  // -1.0
    return dialog;
}

PLATFORM_ASM_ENTRY(DialogMgr_DestroyContents_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(DialogMgr_DestroyContents) "addl $4, %esp\n"
                   "ret")

void XB_CDECL DialogMgr_DestroyContents(uint8_t* overlay) {
    delete_object(field<uint8_t*>(overlay, 0x28));
    delete_object(field<uint8_t*>(overlay, 0x2c));
    delete_object(field<uint8_t*>(overlay, 0));
    delete_object(field<uint8_t*>(overlay, 4));
    delete_object(field<uint8_t*>(overlay, 8));
    delete_object(field<uint8_t*>(overlay, 0xc));
    if (uint8_t* const panel = field<uint8_t*>(overlay, 0x30)) Panel_Delete(panel);
    delete_object(field<uint8_t*>(overlay, 0x24));
}

void XB_THISCALL DialogMgr_Dtor(uint8_t* mgr) {
    uint8_t* const overlay = field<uint8_t*>(mgr, 0x1c);
    field<uint32_t>(mgr, 0) = 0x0082881c;  // DialogMgr vtable
    if (overlay) {
        DialogMgr_DestroyContents(overlay);
        z_free(overlay);
    }
    for (int32_t i = 0; i < 2; ++i) delete_object(screen(mgr, i));
    UIScreenMgr_Dtor(mgr);
}

uint8_t* XB_THISCALL DialogMgr_DeletingDtor(uint8_t* mgr, uint32_t flags) {
    DialogMgr_Dtor(mgr);
    if (flags & 1) z_free(mgr);
    return mgr;
}

uint8_t* XB_REGS1 XB_CDECL DialogMgr_Construct(uint8_t* mgr) {
    UIScreenMgr_Init(mgr, 2, nullptr);
    field<uint32_t>(mgr, 0) = 0x0082881c;  // DialogMgr vtable
    mgr[0x1a] = 1;
    uint8_t* const overlay = z_malloc(0x48);
    if (overlay) {
        field<uint32_t>(overlay, 0) = 0;
        field<uint32_t>(overlay, 4) = 0;
        field<uint32_t>(overlay, 8) = 0;
        field<uint32_t>(overlay, 0xc) = 0;
        field<float>(overlay, 0x3c) = 0.0f;
        field<float>(overlay, 0x40) = 0.0f;
        overlay[0x44] = 0;
        field<uint32_t>(overlay, 0x30) = 0;
        field<uint32_t>(overlay, 0x10) = 0;
        field<uint32_t>(overlay, 0x14) = 0;
        field<uint32_t>(overlay, 0x24) = 0;
        field<uint32_t>(overlay, 0x28) = 0;
        field<uint32_t>(overlay, 0x2c) = 0;
        overlay[0x45] = 0;
        field<uint32_t>(overlay, 0x1c) = 0;
        field<uint32_t>(overlay, 0x20) = 0;
    }
    field<uint8_t*>(mgr, 0x1c) = overlay;
    for (int32_t i = 0; i < 2; ++i) {
        uint8_t* dialog = z_malloc(0x5c);
        if (dialog) dialog = UIDialog_Construct(mgr, 0, dialog);
        field<uint8_t**>(mgr, 4)[field<int32_t>(mgr, 0x14)] = dialog;
        ++field<int32_t>(mgr, 0x14);
    }
    mgr[0x20] = 0;
    return mgr;
}

void XB_THISCALL DialogMgr_Update(uint8_t* mgr, uint32_t dt) {
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0 && screen(mgr, active)) call(screen(mgr, active), 0x30, dt);
    call(mgr, 0x14);
    TitleRegs regs{};
    regs.esi = address_of(field<uint8_t*>(mgr, 0x1c));
    const uint32_t args[1] = {dt};
    ui_call_regs(0x0032df90, &regs, 1, args);  // the overlay's fade (CL_UpdatePauseOverlay)
    call(mgr, 0x20, title_global<uint32_t>(0x0095f8f8) ? 1 : 4);
}

void XB_THISCALL DialogMgr_Draw(uint8_t* mgr) {
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0) call(screen(mgr, active), 0x28);
    DialogMgr_DrawOverlay(field<uint8_t*>(mgr, 0x1c));
}

void XB_THISCALL DialogMgr_OnButtonUp(uint8_t* mgr, uint32_t bit, uint32_t pad) {
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0 && screen(mgr, active)) call(screen(mgr, active), 0x84, pad, bit);
    uint8_t* const overlay = field<uint8_t*>(mgr, 0x1c);
    const uint8_t flags = overlay[0x44];
    if (!((flags >> 2) & 1)) return;
    if (bit == 4) {
        overlay[0x44] = flags & 0xfe;
        call(field<uint8_t*>(overlay, 0), 0x78, 0x3f000000);  // 0.5
    } else if (bit == 8) {
        overlay[0x44] = flags & 0xfd;
        call(field<uint8_t*>(overlay, 4), 0x78, 0x3f000000);
    }
}

void XB_REGS1 XB_CDECL DialogMgr_CloseDialog(uint8_t* mgr) {
    if (field<int32_t>(mgr, 0xc) != 1 || !((mgr[0x20] >> 1) & 1)) {
        field<uint8_t*>(mgr, 0x1c)[0x44] |= 8;
        return;
    }
    call(mgr, 0xc, 0);
    const uint32_t heading = string_copy(reinterpret_cast<ScriptString*>(dialog_of(mgr) + 0x50));
    const uint32_t text = string_copy(reinterpret_cast<ScriptString*>(dialog_of(mgr) + 0x4c));
    {
        TitleRegs regs{};
        regs.esi = field<uint32_t>(mgr, 0x1c);
        const uint32_t args[2] = {text, heading};
        ui_call_regs(0x00313f20, &regs, 2, args);  // UIDialog_SetText (overlay in ESI)
    }
    uint8_t* const dialog = dialog_of(mgr);
    Dialog_LayoutButtons(dialog[0x58], dialog);
    mgr[0x20] &= 0xfd;
}

void XB_REGS1 XB_CDECL DialogMgr_DrawOverlay(uint8_t* overlay) {
    {
        TitleRegs regs{};
        regs.esi = address_of(overlay);
        ui_call_regs(0x003141c0, &regs, 0, nullptr);  // DialogMgr_DrawBackground
    }
    if (!((overlay[0x44] >> 2) & 1)) {
        nglPushScene(0);
        nglSetClearFlags(0);
        nglSetSceneDepthTest(0);
        nglSetSceneDepthEnable(0);
        call(field<uint8_t*>(overlay, 0x2c), 0x3c);
        if (has_text(field<uint8_t*>(overlay, 0x28))) call(field<uint8_t*>(overlay, 0x28), 0x3c);
        nglPopScene();
    }
    if (!((overlay[0x44] >> 2) & 1)) return;
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    nglSetViewportRect(180.0f, 140.0f, 460.0f, 340.0f);
    int32_t offset = 0;
    if (has_text(field<uint8_t*>(overlay, 0x28))) offset = (line_count(field<uint8_t*>(overlay, 0x28)) + 1) * 20;
    uint8_t* const heading = field<uint8_t*>(overlay, 0x28);
    method<PositionMethod>(heading, 0x7c)(heading, 320.0f, field<float>(overlay, 0x3c) + 140.0f);
    offset += 0x8c;
    uint8_t* const body = field<uint8_t*>(overlay, 0x2c);
    method<PositionMethod>(body, 0x7c)(body, 320.0f, float(offset) + field<float>(overlay, 0x3c));
    int32_t first = -1 - int32_t(field<float>(overlay, 0x3c) * title_global<float>(0x0083edc8));  // 0.05
    if (has_text(field<uint8_t*>(overlay, 0x28))) {
        call(field<uint8_t*>(overlay, 0x28), 0x3c);
        first -= line_count(field<uint8_t*>(overlay, 0x28));
    }
    call(field<uint8_t*>(overlay, 0x2c), 0x100, uint32_t(first), uint32_t(first + 0xc));
    nglPopScene();
}

PLATFORM_ASM_ENTRY(DialogMgr_DrawBackground_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(DialogMgr_DrawBackground) "addl $4, %esp\n"
                   "ret")

void XB_CDECL DialogMgr_DrawBackground(uint8_t* overlay) {
    constexpr uint32_t kOne = 0x3f800000;  // 1.0f (0x0083eb20)
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    uint8_t* const quad = field<uint8_t*>(overlay, 0x30);
    if (title_global<uint8_t*>(0x0095f8f8)) {
        field<int32_t>(quad, 0x18) = -1;
        if (uint8_t* const back = field<uint8_t*>(overlay, 0x10)) call(back, 0x7c, kOne);
        if (uint8_t* const plain = field<uint8_t*>(overlay, 0x14)) call(plain, 0x7c, 0);
        call(field<uint8_t*>(overlay, 0x10), 0x44);
    } else {
        field<int32_t>(quad, 0x18) = 0;
        const bool alt = overlay[0x45] != 0;  // +0x45: the +0x1c / +0x20 pair instead of +0x14
        if (uint8_t* const back = field<uint8_t*>(overlay, 0x10)) call(back, 0x7c, 0);
        if (uint8_t* const plain = field<uint8_t*>(overlay, 0x14)) call(plain, 0x7c, alt ? 0 : kOne);
        if (uint8_t* const left = field<uint8_t*>(overlay, 0x1c)) call(left, 0x7c, alt ? kOne : 0);
        if (uint8_t* const right = field<uint8_t*>(overlay, 0x20)) call(right, 0x7c, alt ? kOne : 0);
        if (overlay[0x45]) {
            call(field<uint8_t*>(overlay, 0x1c), 0x44);
            call(field<uint8_t*>(overlay, 0x20), 0x44);
        } else {
            call(field<uint8_t*>(overlay, 0x14), 0x44);
        }
    }
    call(field<uint8_t*>(overlay, 0x18), 0x44);
    nglPopScene();
    uint8_t* const pause = title_global<uint8_t*>(0x0095f900);
    uint8_t* const screen = field<uint8_t**>(pause, 4)[field<uint32_t>(pause, 0xc) != 0 ? 1 : 0];
    if (field<int32_t>(screen, 0x34) <= 0) return;
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    call(field<uint8_t*>(overlay, 0x24), 0x3c);
    nglPopScene();
}

// --- Screen managers and root menus ---

namespace {

// The pause / loading screens' plain UIMenu part (owner, rows, position 320, 240, layer 8).
void init_screen_menu(uint8_t* menu, int32_t count, uint8_t* owner) { UIMenu_Init(menu, count, owner, 0x140, 0xf0, 8, 0); }

// A screen constructor taking the owner in EAX and the memory in ECX (returns the screen).
uint8_t* construct_screen(uint32_t function, uint32_t size, uint8_t* owner) {
    uint8_t* const memory = z_malloc(size);
    if (!memory) return nullptr;
    return title_function<uint8_t* XB_REGS3 XB_CDECL(uint8_t*, uint32_t, uint8_t*)>(function)(owner, 0, memory);
}

// A localized string (rm_locStrings): the entry's text ("NO STRING" when null), the name itself without an entry.
const char* localized(const char* name) {
    uint8_t* const zone = title_global<uint8_t*>(0x0095f5d8);  // rm_locStrings
    const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x00024440)(name);  // Loc_HashKey
    const uint8_t* const entry = Zone_FindByHash(zone, hash);
    if (!entry) return name;
    const char* const text = field<const char*>(const_cast<uint8_t*>(entry), 8);
    return text ? text : text_at(0x008379b4);  // "NO STRING"
}

void add_screen(uint8_t* mgr, uint8_t* screen_object) {
    field<uint8_t**>(mgr, 4)[field<int32_t>(mgr, 0x14)] = screen_object;
    ++field<int32_t>(mgr, 0x14);
}

// The loading screens (0x44 bytes and up): +0x33, +0x34, +0x38 cleared, +0x44 = -1.
uint8_t* loading_screen(uint32_t size, uint32_t vtable, uint8_t* owner) {
    uint8_t* const menu = z_malloc(size);
    if (!menu) return nullptr;
    init_screen_menu(menu, 0, owner);
    field<uint32_t>(menu, 0) = vtable;
    menu[0x33] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    menu[0x38] = 0;
    field<int32_t>(menu, 0x44) = -1;
    return menu;
}

}  // namespace

void XB_REGS1 XB_CDECL UIRootMenu_Dtor(uint8_t* menu) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = 0x00829280;  // UIRootMenu vtable
    if (panel) Panel_Delete(panel);
    delete_object(field<uint8_t*>(menu, 0x38));
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

uint8_t* XB_THISCALL UIRootMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    UIRootMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

void XB_REGS1 XB_CDECL TitleMenu_Dtor(uint8_t* menu) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = 0x008297f0;  // TitleMenu vtable
    if (panel) Panel_Delete(panel);
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

uint8_t* XB_THISCALL TitleMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    TitleMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

void XB_REGS1 XB_CDECL MainMenu_Dtor(uint8_t* menu) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x38);
    field<uint32_t>(menu, 0) = 0x00829b80;  // MainMenu vtable
    if (panel) Panel_Delete(panel);
    delete_object(field<uint8_t*>(menu, 0x34));
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

uint8_t* XB_THISCALL MainMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    MainMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL MainMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_screen_menu(menu, 5, owner);
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    menu[0x44] = 0;
    field<uint32_t>(menu, 0x48) = 0;
    field<uint32_t>(menu, 0) = 0x00829b80;  // MainMenu vtable
    menu[0x33] = 5;
    field<float>(menu, 0x40) = 0.0f;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL TitleMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_screen_menu(menu, 0, owner);
    menu[0x2a] |= 0x40;
    menu[0x33] = 0;
    menu[0x3c] = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0) = 0x008297f0;  // TitleMenu vtable
    field<float>(menu, 0x38) = 0.0f;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL StorageMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 0, owner, 0, 0, 8, 0);
    field<uint32_t>(menu, 0x34) = 0x00829ab4;
    field<uint32_t>(menu, 0) = 0x00829e10;     // vtable
    field<uint32_t>(menu, 0x34) = 0x00829e0c;  // the device-removed listener's vtable
    title_function<void XB_CDECL(uint8_t*)>(0x000d0cc0)(menu + 0x34);  // Save_SetDeviceRemovedListener
    field<uint32_t>(menu, 0x38) = 0;
    menu[0x3c] = 0;
    menu[0x3d] = 0;
    menu[0x40] = 0;
    menu[0x41] = 0;
    return menu;
}

uint8_t* XB_THISCALL InGameScreens_DeletingDtor(uint8_t* mgr, uint32_t flags) {
    field<uint32_t>(mgr, 0) = 0x008286cc;  // InGameScreens vtable
    UIScreenMgr_Dtor(mgr);
    if (flags & 1) z_free(mgr);
    return mgr;
}

// The callees run at the original's stack positions (as UIDialog_SetText: the pause menu's dialogs).
PLATFORM_ASM_ENTRY(InGameScreens_Update_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 8(%ebp)\n"
                   "pushl %ecx\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(InGameScreens_Update) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $4")

void XB_CDECL InGameScreens_Update(uint32_t entry, uint8_t* mgr, uint32_t dt) {
    const uint32_t top = entry - 4;  // after push esi
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0 && screen(mgr, active)) {
        const uint32_t args[1] = {dt};
        method_at(top, screen(mgr, active), 0x30, 1, args);
    }
    method_at(top, mgr, 0x14);
}

uint8_t* XB_STDCALL InGameScreens_Construct(uint8_t* mgr) {
    UIScreenMgr_Init(mgr, 9, nullptr);
    field<uint32_t>(mgr, 0) = 0x008286cc;  // InGameScreens vtable
    uint8_t* pause = z_malloc(0x44);
    if (pause) {
        init_screen_menu(pause, 5, mgr);
        field<uint16_t>(pause, 0x2a) |= 0x180;
        field<uint32_t>(pause, 0) = 0x008289f8;  // PauseMenu vtable
        pause[0x33] = 0;
        field<uint32_t>(pause, 0x34) = 0;
        pause[0x40] = 1;
    }
    add_screen(mgr, pause);
    add_screen(mgr, construct_screen(0x003315e0, 0xc4, mgr));  // AdvancedOptionsMenu_Construct
    add_screen(mgr, loading_screen(0x110, 0x00829180, mgr));  // LoadingMenu
    add_screen(mgr, loading_screen(0x128, 0x0082a1f8, mgr));  // LoadAfricaMenu
    add_screen(mgr, loading_screen(0xac, 0x00828718, mgr));   // LoadSicilyMenu
    uint8_t* maubeuge = z_malloc(0x5c);
    if (maubeuge) {
        init_screen_menu(maubeuge, 0, mgr);
        field<uint32_t>(maubeuge, 0) = 0x008295a0;  // LoadMaubeugeMenu vtable
        maubeuge[0x33] = 0;
        field<uint32_t>(maubeuge, 0x34) = 0;
        maubeuge[0x38] = 0;
        field<uint32_t>(maubeuge, 0x3c) = 0;
        field<uint32_t>(maubeuge, 0x40) = 0;
    }
    add_screen(mgr, maubeuge);
    add_screen(mgr, construct_screen(0x00330820, 0xc4, mgr));  // MissionSuccessFailMenu_Construct
    add_screen(mgr, construct_screen(0x00330730, 0x88, mgr));  // AutosaveMenu_Construct
    add_screen(mgr, construct_screen(0x00223e80, 0x4c, mgr));  // InGameLiveOptionsMenu_Construct
    call(mgr, 0x18);
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) call(screen(mgr, i), 0x1c);
    return mgr;
}

uint8_t* XB_STDCALL FrontEndMenu_Construct(uint8_t* mgr) {
    UIScreenMgr_Init(mgr, 0x15, nullptr);
    field<uint32_t>(mgr, 0) = 0x0082905c;  // FrontEndMenu vtable
    uint8_t* title = z_malloc(0x44);
    if (title) {
        init_screen_menu(title, 0, mgr);
        title[0x2a] |= 0x40;
        field<uint32_t>(title, 0) = 0x008297f0;  // TitleMenu vtable
        title[0x33] = 0;
        field<float>(title, 0x38) = 0.0f;
        title[0x3c] = 0;
        field<uint32_t>(title, 0x40) = 0;
        field<uint32_t>(title, 0x34) = 0;
    }
    add_screen(mgr, title);
    uint8_t* main = z_malloc(0x4c);
    if (main) main = MainMenu_Construct(mgr, 0, main);
    add_screen(mgr, main);
    add_screen(mgr, construct_screen(0x00331210, 0x4c, mgr));  // SinglePlayerMenu_Construct
    uint8_t* options = z_malloc(0x40);
    if (options) {
        init_screen_menu(options, 3, mgr);
        options[0x2a] |= 0x80;
        field<uint32_t>(options, 0) = 0x00829348;  // OptionsMenu vtable
        options[0x33] = 7;
        field<uint32_t>(options, 0x34) = 0;
        field<uint32_t>(options, 0x38) = 0;
        field<uint32_t>(options, 0x3c) = 0;
    }
    add_screen(mgr, options);
    // (constructor, size): Gameplay, ControllerConfig, Screen, Sound, BonusMaterials, BonusCharacters, BonusWeapons,
    // BonusVehicles, BonusConcepts.
    static const uint32_t kMenus[][2] = {
        {0x00331150, 0x8c},  {0x003310d0, 0xd8},  {0x00330fd0, 0x5c},  {0x00330f80, 0x44},  {0x00330de0, 0x108},
        {0x00330d60, 0xf4},  {0x00330c40, 0x194}, {0x00330bb0, 0x1d4}, {0x00330ab0, 0x20c},
    };
    for (const auto& menu : kMenus) add_screen(mgr, construct_screen(menu[0], menu[1], mgr));
    uint8_t* difficulty = z_malloc(0x54);
    if (difficulty) {
        UIGridMenu_Init(difficulty, 3, mgr, 0);
        field<uint32_t>(difficulty, 0) = 0x00828c50;  // DifficultyMenu vtable
        difficulty[0x33] = 0;
        field<uint32_t>(difficulty, 0x34) = 0;
        field<uint32_t>(difficulty, 0x38) = 0;
        field<int32_t>(difficulty, 0x48) = -1;
        field<uint32_t>(difficulty, 0x50) = 1;
    }
    add_screen(mgr, difficulty);
    // CreateProfile, Profile, 0x003308a0, Chapter, SpecialMaterials, SpecialConcept, XboxLive.
    static const uint32_t kMoreMenus[][2] = {
        {0x00335570, 0x1b4}, {0x003308f0, 0x9c}, {0x003308a0, 0x44}, {0x00333380, 0x1b8},
        {0x003305e0, 0xdc},  {0x00330560, 0x148}, {0x00223ed0, 0x4c},
    };
    for (const auto& menu : kMoreMenus) add_screen(mgr, construct_screen(menu[0], menu[1], mgr));
    NglQuad* const top = reinterpret_cast<NglQuad*>(mgr + 0x20);
    NglQuad* const bottom = reinterpret_cast<NglQuad*>(mgr + 0x84);
    nglInitQuad(top);
    nglInitQuad(bottom);
    nglSetQuadRect(top, 0.0f, 0.0f, 640.0f, 320.0f);
    nglSetQuadRect(bottom, 0.0f, 320.0f, 640.0f, 480.0f);
    mgr[0xe8] = 0;
    mgr[0xf5] = 1;
    mgr[0xf4] = 0;
    field<uint32_t>(mgr, 0xf0) = 0;
    field<uint32_t>(mgr, 0xec) = 0;
    field<uint32_t>(mgr, 0x1c) = 0;
    field<int32_t>(mgr, 0xc) = -1;
    call(mgr, 0x18);
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) call(screen(mgr, i), 0x1c);
    nglSetQuadZ(top, 300.0f);
    nglSetQuadZ(bottom, 300.0f);
    return mgr;
}

void XB_THISCALL UIRoot_Dtor(uint8_t* root) {
    field<uint32_t>(root, 0) = 0x0082a5f4;  // UIRoot vtable
    delete_object(field<uint8_t*>(root, 0x28));
    delete_object(field<uint8_t*>(root, 0x2c));
    delete_object(field<uint8_t*>(root, 0x30));
    delete_object(field<uint8_t*>(root, 0x20));
    delete_object(field<uint8_t*>(root, 0x24));
    delete_object(field<uint8_t*>(root, 0x34));
}

void XB_CDECL AtExit_UIRoot() { UIRoot_Dtor(&title_global<uint8_t>(0x0095f8d0)); }  // g_uiRoot

void XB_CDECL StaticInit_UIColors_95fd30() {
    // (global, ARGB colour) in the original's store order.
    static const uint32_t kColors[][2] = {
        {0x0095fd30, 0xffb4b4b4},
        {0x0095fd34, 0xff8c3232},
        {0x0095fd38, 0xff8c3232},
        {0x0095fd40, 0x8c8c8c8c},
        {0x0095fd44, 0xffb4b4b4},
        {0x0095fd48, 0xffb4b4b4},
        {0x0095fd50, 0xffb4b4b4},
        {0x0095fd54, 0xffffffff},
        {0x0095fd58, 0xffffffff},
        {0x0095fd60, 0xff000000},
        {0x0095fd64, 0xff8c3232},
        {0x0095fd68, 0xff8c3232},
        {0x0095fd70, 0xffffffff},
        {0x0095fd74, 0xff8c3232},
        {0x0095fd78, 0xff8c3232},
        {0x0095fd80, 0xaa282119},
        {0x0095fd84, 0xff282119},
        {0x0095fd88, 0xff282119},
        {0x0095fd90, 0x8c1f2c2c},
        {0x0095fda0, 0x8c491c1b},
        {0x0095fdb0, 0xffb4b4b4},
        {0x0095fd94, 0xff1f2c2c},
        {0x0095fd98, 0xff1f2c2c},
        {0x0095fda4, 0xff491c1b},
        {0x0095fda8, 0xff491c1b},
        {0x0095fdb4, 0xffb4b4b4},
        {0x0095fdb8, 0xffb4b4b4},
        {0x0095fdc0, 0xffa89c8d},
        {0x0095fdc4, 0xffebba50},
        {0x0095fdc8, 0xffebba50}
    };
    for (const auto& color : kColors) title_global<uint32_t>(color[0]) = color[1];
}

uint8_t* XB_THISCALL UIRoot_DeletingDtor(uint8_t* root, uint32_t flags) {
    UIRoot_Dtor(root);
    if (flags & 1) z_free(root);
    return root;
}

void XB_REGS1 XB_CDECL UIRoot_InitChapterTable(uint8_t* table) {
    for (int32_t i = 0; i < 14 * 0x12; ++i) field<uint32_t>(table, i * 4) = 0;
    // (offset, value) pairs, in the original's store order.
    static const uint16_t kEntries[][2] = {
        {0xb8, 3},   {0x148, 3},  {0x48, 2},   {0x4c, 2},   {0x54, 1},   {0x58, 2},   {0x5c, 1},   {0x60, 1},
        {0x64, 2},   {0x70, 2},   {0x74, 2},   {0x78, 1},   {0x88, 4},   {0x8c, 2},   {0xbc, 1},   {0xc4, 1},
        {0xd4, 2},   {0xd8, 1},   {0xec, 1},   {0xf0, 1},   {0xf4, 1},   {0x100, 6},  {0x11c, 2},  {0x164, 2},
        {0x174, 1},  {0x194, 4},  {0x1ac, 2},  {0x1b0, 1},  {0x1b4, 1},  {0x1b8, 1},  {0x1d0, 1},  {0x1d8, 1},
        {0x1dc, 2},  {0x1e8, 2},  {0x1f4, 2},  {0x1fc, 1},  {0x20c, 1},  {0x220, 1},  {0x23c, 2},  {0x248, 1},
        {0x24c, 1},  {0x264, 1},  {0x268, 1},  {0x284, 2},  {0x29c, 1},  {0x2a8, 1},  {0x2b0, 1},  {0x2c0, 1},
        {0x2c8, 1},  {0x2cc, 2},  {0x2fc, 1},  {0x310, 1},  {0x314, 2},  {0x31c, 1},  {0x338, 1},  {0x35c, 2},
        {0x3a0, 1},  {0x3a4, 2},  {0x3cc, 1},  {0x3e4, 1},  {0x3ec, 2},
    };
    for (const auto& entry : kEntries) field<uint32_t>(table, entry[0]) = entry[1];
}

PLATFORM_ASM_ENTRY(UIRoot_Construct_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_Construct) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL UIRoot_Construct(uint8_t* root) {
    field<uint32_t>(root, 0) = 0x0082a5f4;  // UIRoot vtable
    UIRoot_InitChapterTable(root + 0x54);
    field<uint32_t>(root, 0x28) = 0;
    field<uint32_t>(root, 0x2c) = 0;
    field<uint32_t>(root, 0x30) = 0;
    root[0x43] = 0;
    field<uint32_t>(root, 0x34) = 0;
    field<float>(root, 0x38) = 0.0f;
    root[0x48] = 0;
    root[0x49] = 0;
    field<float>(root, 0x444) = 0.0f;
    root[0x45] = 0;
    root[0x44] = 0;
    root[0x4a] = 0;
    root[0x4b] = 0;
    root[0x4c] = 0;
    root[0x4d] = 0;
    root[0x46] = 0;
    root[0x4f] = 0;
    root[0x4e] = 0;
    root[0x47] = 1;
    field<int32_t>(root, 0x50) = -1;
    for (int32_t i = 0; i < 7; ++i) {
        field<uint32_t>(root, 4 + i * 4) = 0;
        root[0x3c + i] = 0;
    }
    uint8_t* const menu = z_malloc(0x3c);
    if (menu) {
        field<uint32_t>(menu, 0x2c) = 0;
        field<uint32_t>(menu, 0xc) = 0;
        field<uint32_t>(menu, 0x10) = 0;
        field<uint16_t>(menu, 0x26) = 0;
        field<uint32_t>(menu, 0x14) = 0;
        field<uint32_t>(menu, 0x18) = 0;
        field<uint16_t>(menu, 0x2a) = 0;
        field<uint32_t>(menu, 4) = 0;
        field<uint16_t>(menu, 0x20) = 0;
        field<uint32_t>(menu, 8) = 0;
        menu[0x30] = 0;
        menu[0x33] = 0;
        field<uint32_t>(menu, 0x34) = 0;
        field<uint32_t>(menu, 0x38) = 0;
        field<uint16_t>(menu, 0x28) = 1;
        field<int16_t>(menu, 0x22) = -1;
        field<int16_t>(menu, 0x24) = -1;
        menu[0x31] = 1;
        field<uint32_t>(menu, 0) = 0x00829280;  // UIRootMenu vtable
    }
    field<uint8_t*>(root, 0x24) = menu;
    return root;
}

// --- UI root draw/update (root in ESI) ---

namespace {

// A screen manager with an active screen (+0x0c >= 0); null-safe.
bool has_active(uint8_t* mgr) { return mgr && field<int32_t>(mgr, 0xc) >= 0; }

// The scene the UI layers draw in: no clears, no depth test or writes.
void push_ui_scene() {
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
}

// The front end's (+0x28) active screen, when in range, told +0x18 (flag).
void tell_front_end_screen(uint8_t* root, uint32_t flag) {
    uint8_t* const fe = field<uint8_t*>(root, 0x28);
    if (!fe) return;
    const int32_t active = field<int32_t>(fe, 0xc);
    if (active < 0 || active >= field<int32_t>(fe, 0x10)) return;
    call(screen(fe, active), 0x18, flag);
}

}  // namespace

PLATFORM_ASM_ENTRY(UIRoot_DrawInGameScreens_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_DrawInGameScreens) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIRoot_DrawInGameScreens(uint8_t* root) {
    if (!title_global<uint32_t>(0x0090259c) || !root[0x49] || !root[0x3c]) return;  // ngl_curScene
    if (!has_active(field<uint8_t*>(root, 0x30))) {
        push_ui_scene();
        call(field<uint8_t*>(root, 0x2c), 0x1c);
        nglPopScene();
        root[0x4e] = 0;
    }
    uint8_t* const dialogs = field<uint8_t*>(root, 0x30);
    if (!has_active(dialogs)) return;
    call(dialogs, 0x1c);
    root[0x4e] = 1;
}

PLATFORM_ASM_ENTRY(UIRoot_DrawFrontEnd_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_DrawFrontEnd) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIRoot_DrawFrontEnd(uint8_t* root) {
    if (!title_global<uint32_t>(0x0090259c) || !root[0x48] || !root[0x3c]) return;  // ngl_curScene
    push_ui_scene();
    call(field<uint8_t*>(root, 0x28), 0x1c);
    nglPopScene();
    uint8_t* const dialogs = field<uint8_t*>(root, 0x30);
    if (has_active(dialogs)) {
        call(dialogs, 0x1c);
        root[0x4e] = 1;
        return;
    }
    root[0x4e] = 0;
}

PLATFORM_ASM_ENTRY(UIRoot_DrawHud_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_DrawHud) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIRoot_DrawHud(uint8_t* root) {
    if (!root[0x49]) call(field<uint8_t*>(root, 0x20), 0x18);
    if (root[0x4f]) return;
    push_ui_scene();
    uint8_t* const active_dialogs = field<uint8_t*>(root, 0x30);
    if (!active_dialogs || (field<int32_t>(active_dialogs, 0xc) < 0 && !root[0x49])) call(field<uint8_t*>(root, 0x20), 0x14);
    nglPopScene();
    uint8_t* const dialogs = field<uint8_t*>(root, 0x30);
    if (has_active(dialogs)) call(dialogs, 0x1c);
}

PLATFORM_ASM_ENTRY(UIRoot_UpdateInGameScreens_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_UpdateInGameScreens) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL UIRoot_UpdateInGameScreens(uint8_t* root, uint32_t dt) {
    uint8_t* const dialogs = field<uint8_t*>(root, 0x30);
    if (has_active(dialogs)) {
        call(dialogs, 0x10, dt);
        uint8_t* const screens = field<uint8_t*>(root, 0x2c);
        root[0x4e] = 1;
        if (field<int32_t>(screens, 0xc) == 7) call(screens, 0x10, dt);
        return;
    }
    uint8_t* const screens = field<uint8_t*>(root, 0x2c);
    root[0x4e] = 0;
    call(screens, 0x10, dt);
}

PLATFORM_ASM_ENTRY(UIRoot_UpdateFrontEnd_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIRoot_UpdateFrontEnd) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL UIRoot_UpdateFrontEnd(uint8_t* root, uint32_t dt) {
    uint8_t* const dialogs = field<uint8_t*>(root, 0x30);
    if (has_active(dialogs)) {
        call(dialogs, 0x10, dt);
        root[0x4e] = 1;
        tell_front_end_screen(root, 1);
    } else {
        tell_front_end_screen(root, 0);
        root[0x4e] = 0;
    }
    if (uint8_t* const fe = field<uint8_t*>(root, 0x28)) call(fe, 0x10, dt);
}

void XB_REGS1 XB_CDECL TitleMenu_Accept(uint8_t* menu) {
    if (menu[0x3c]) return;
    uint8_t* const fe = title_global<uint8_t*>(0x0095f8f8);
    if (title_global<uint8_t>(0x008dd1cf) == 1) {
        call(fe, 0xc, title_global<uint8_t>(0x008dce30) ? 1 : 0xe);
        return;
    }
    screen(title_global<uint8_t*>(0x0095f8f8), 15)[0x94] = 0;
    field<uint32_t>(screen(title_global<uint8_t*>(0x0095f8f8), 15), 0x4c) = 1;
    call(title_global<uint8_t*>(0x0095f8f8), 0xc, 0xf);
}

void XB_STDCALL MemDlg_FormatNotEnoughBlocks(uint32_t, char* out) {
    title_function<uint32_t XB_CDECL(uint32_t)>(0x000d1e10)(0x400);  // Save_ClustersForSize
    title_function<uint32_t XB_CDECL()>(0x000d1ec0)();               // Save_GetClusterSize
    title_function<uint32_t XB_CDECL()>(0x000d1ec0)();
    const char* const first = localized(text_at(0x0082aabc));   // "MEM_ERROR_NOT_ENOUGH_BLOCKS1"
    const char* const second = localized(text_at(0x0082aa9c));  // "MEM_ERROR_NOT_ENOUGH_BLOCKS2"
    const int32_t blocks = title_function<int32_t XB_CDECL()>(0x0031ac10)();  // Save_GetBlocksNeeded
    title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(out, text_at(0x0082b1c8), first, blocks,
                                                                          second);  // _sprintf "%s %d %s"
}

// --- Memory-unit dialogs and save slots ---

namespace {

uint8_t* dialog_mgr() { return title_global<uint8_t*>(0x0095f900); }
bool profiles_dir_exists() { return title_function<uint8_t XB_CDECL()>(0x0030c300)() != 0; }  // Save_ProfilesDirExists
bool has_free_space() { return title_function<uint8_t XB_CDECL()>(0x0031ac60)() != 0; }

// The blocks a save needs (BytesToBlocks of 4 clusters of 1 KB, two and five clusters).
int32_t blocks_needed() {
    const int32_t clusters = title_function<int32_t XB_CDECL(uint32_t)>(0x000d1e10)(0x400);  // Save_ClustersForSize
    int32_t bytes = clusters * 4;
    bytes = bytes + title_function<int32_t XB_CDECL()>(0x000d1ec0)() * 2;  // Save_GetClusterSize
    const int32_t size = title_function<int32_t XB_CDECL()>(0x000d1ec0)();
    bytes = bytes + size * 4 + size;
    return title_function<int32_t XB_CDECL(int64_t)>(0x000d1610)(bytes);  // BytesToBlocks
}

// The "not enough blocks" dialog with the continue-without-saving button (and the manage-memory one).
void show_not_enough_blocks(const char* text, uint32_t continue_handler, uint32_t manage_handler) {
    UI_ShowDialog(text, dialog_mgr(), 0, 0, text_at(0x0082dfeb), 1);
    UI_DialogAddButton(dialog_mgr(), 0x0082b0f4, continue_handler);  // "MEM_CONTINUE_WO_SAVING"
    if (manage_handler) UI_DialogAddButton(dialog_mgr(), 0x0082b0d8, manage_handler);  // "MEM_XBOX_MANAGE_MEM_UNIT"
    Dialog_LayoutButtons(1, dialog_of(dialog_mgr()));
}

// Copies a save slot record (0x3cc bytes) into its write buffer (the slot's +0x11c0 pointer, cleared first for
// the save's size), with the device byte +0x11e6 after it.
void fill_write_buffer(uint8_t* saves, int32_t slot, const uint8_t* record) {
    const uint32_t size = title_function<uint32_t XB_CDECL(uint32_t)>(0x000d1e10)(0x400);  // Save_ClustersForSize
    uint8_t* const buffer = field<uint8_t*>(saves, 0x11c0 + slot * 4);
    for (uint32_t i = 0; i < size; ++i) buffer[i] = 0;
    platform_rep_movs(field<uint8_t*>(saves, 0x11c0 + slot * 4), record, 0x3cc);
    field<uint8_t*>(saves, 0x11c0 + slot * 4)[0x3cc] = saves[0x11e6];
}

}  // namespace

uint8_t XB_CDECL MemDlg_CheckFreeSpace3() {
    if (profiles_dir_exists() || has_free_space()) return 1;
    char text[0x200];
    blocks_needed();
    MemDlg_FormatNotEnoughBlocks(title_global<uint32_t>(0x0095f480), text);
    show_not_enough_blocks(text, 0x003125d0, 0);
    return 0;
}

uint8_t XB_REGS3 XB_CDECL MemDlg_CheckFreeSpace(uint8_t check, uint32_t, uint8_t* menu) {
    if (!check || profiles_dir_exists() || has_free_space()) return 1;
    char text[0x200];
    field<int32_t>(menu, 0x38) = blocks_needed();
    MemDlg_FormatNotEnoughBlocks(title_global<uint32_t>(0x0095f480), text);
    menu[0x41] = 1;
    show_not_enough_blocks(text, 0x00334410, 0x0030d040);
    return 0;
}

uint8_t XB_CDECL MemDlg_CheckFreeSpace2() {
    if (profiles_dir_exists() || has_free_space()) {
        if (!title_global<uint8_t>(0x008dd1d0)) title_function<void XB_CDECL()>(0x00312870)();
        return 1;
    }
    char text[0x200];
    blocks_needed();
    MemDlg_FormatNotEnoughBlocks(title_global<uint32_t>(0x0095f480), text);
    show_not_enough_blocks(text, 0x0031b2e0, 0x00335410);
    return 0;
}

uint8_t XB_REGS1 XB_STDCALL MemDlg_CheckSaveDevice(int32_t device, uint8_t present) {
    if (device != -2 && present) return 1;
    UI_ShowDialog(text_at(0x0082ad74), dialog_mgr(), 0, 0, text_at(0x0082dfeb), 1);  // "MEM_PS2_ERROR_NO_SAVE_DATA"
    UI_DialogAddButton(dialog_mgr(), 0x0082b0f4, 0x00334410);  // "MEM_CONTINUE_WO_SAVING"
    UI_DialogAddButton(dialog_mgr(), 0x0082af70, 0x00338470);  // "MEM_RETRY"
    Dialog_LayoutButtons(1, dialog_of(dialog_mgr()));
    return 0;
}

PLATFORM_ASM_ENTRY(SaveSlot_Init_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(SaveSlot_Init) "addl $12, %esp\n"
                   "ret $8")

uint32_t XB_CDECL SaveSlot_Init(uint8_t* saves, int32_t slot, uint8_t keep) {
    using StrNCpy = char* XB_CDECL(char*, const char*, uint32_t);
    title_global<uint8_t>(0x008d50dd) = 0;
    uint8_t* const record = saves + slot * 0x3cc;
    const char* const version = title_global<uint8_t>(0x0095e7f8) ? text_at(0x0082ab00) : text_at(0x0082aaf8);
    title_function<StrNCpy>(0x001eed80)(reinterpret_cast<char*>(record + 0x28c), version, 0x19);  // "9-21-05(CE)"
    const char* const data = title_function<const char* XB_CDECL()>(0x000d0c80)();  // Save_GetData
    title_function<StrNCpy>(0x001eed80)(reinterpret_cast<char*>(record + 0x64c), data, 0xc);
    saves[slot + 0x11e7] = 1;
    if (!keep) title_function<void XB_THISCALL(uint8_t*)>(0x0030c1a0)(saves);
    fill_write_buffer(saves, slot, record + 0x28c);
    const uint32_t result = title_function<uint32_t XB_CDECL(uint8_t*)>(0x000d2620)(saves + 4);  // Save_BeginWrite
    field<int32_t>(saves, 0x11e0) = slot;
    return result;
}

void XB_STDCALL SaveSlot_InitAll(uint8_t* saves) {
    using StrNCpy = char* XB_CDECL(char*, const char*, uint32_t);
    uint8_t profile[0x3a4];  // the defaults (the words Profile_SetDefaults leaves are zero on the dead stack)
    for (uint32_t i = 0; i < sizeof profile; ++i) profile[i] = 0;
    title_global<uint8_t>(0x008d50dd) = 0;
    field<int32_t>(profile, 0xe8) = -1;
    Profile_SetDefaults(profile);
    int32_t slot = 0;
    for (; slot < 4; ++slot) {
        uint8_t* const record = saves + 0x28c + slot * 0x3cc;
        const char* const version = title_global<uint8_t>(0x0095e7f8) ? text_at(0x0082ab00) : text_at(0x0082aaf8);
        title_function<StrNCpy>(0x001eed80)(reinterpret_cast<char*>(record), version, 0x19);
        const char* const data = title_function<const char* XB_CDECL()>(0x000d0c80)();  // Save_GetData
        title_function<StrNCpy>(0x001eed80)(reinterpret_cast<char*>(record + 0x3c0), data, 0xc);
        saves[slot + 0x11e7] = 1;
        profile[0] = 0;
        platform_rep_movs(record + 0x1c, profile, 0x3a4);
        fill_write_buffer(saves, slot, record);
    }
    title_function<void XB_CDECL(uint8_t*)>(0x000d2620)(saves + 4);  // Save_BeginWrite
    field<int32_t>(saves, 0x11e0) = slot;
}

}  // namespace game
