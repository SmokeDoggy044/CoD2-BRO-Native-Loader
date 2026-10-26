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

void XB_THISCALL UIScreenMgr_Update(uint8_t* mgr, uint32_t dt) {
    const int32_t active = field<int32_t>(mgr, 0xc);
    if (active >= 0 && screen(mgr, active)) call(screen(mgr, active), 0x30, dt);
    call(mgr, 0x14);
}

void XB_THISCALL UIScreenMgr_LayoutAll(uint8_t* mgr) {
    call(mgr, 0x18);
    for (int32_t i = 0; i < field<int32_t>(mgr, 0x14); ++i) call(screen(mgr, i), 0x1c);
}

void XB_THISCALL UIScreenMgr_SetActiveScreen(uint8_t* mgr, int32_t index) {
    if (field<int32_t>(mgr, 0xc) != -1) {
        uint8_t* const next = index >= 0 ? screen(mgr, index) : nullptr;
        call(screen(mgr, field<int32_t>(mgr, 0xc)), 0x44, address_of(next));
    }
    const int32_t old = field<int32_t>(mgr, 0xc);
    field<int32_t>(mgr, 0xc) = index;
    if (index >= 0) {
        call(screen(mgr, index), 0x3c, uint32_t(old));
        uint32_t handle = title_global<uint32_t>(0x0095f4c4);
        play_ui_sound(0x0082cbf0, handle, true);  // "UI_Highlight"
    }
    call(mgr, 0x18);
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
void string_from(ScriptString* s, const char* text) { title_function<StringMethod>(0x0011eb50)(s, text); }
void string_assign(ScriptString* s, const char* text) { title_function<StringMethod>(0x0011dd20)(s, text); }
void string_release(ScriptString* s) { title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(s); }
// A by-value copy (the callee releases it).
uint32_t string_copy(const ScriptString* s) {
    ScriptString copy;
    ScriptString_Copy(&copy, s);
    return address_of(copy.rep);
}

}  // namespace

void XB_REGS1 XB_STDCALL UI_ShowDialog(const char* name, uint8_t* mgr, uint8_t ok, uint8_t yes_no, const char* title,
                                       uint8_t keep_running) {
    const char* text = name;
    if (name) {
        const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x00024440)(name);  // Loc_HashKey
        const uint8_t* const entry = Zone_FindByHash(title_global<uint8_t*>(0x0095f5d8), hash);  // rm_locStrings
        if (entry) {
            text = field<const char*>(const_cast<uint8_t*>(entry), 8);
            if (!text) text = text_at(0x008379b4);  // "NO STRING"
        }
    }
    ScriptString message;
    string_from(&message, text);
    ScriptString heading;
    string_from(&heading, text_at(0x0082dfeb));  // ""
    if (*title) string_assign(&heading, title);
    if (!message.rep || !message.rep->length) string_assign(&message, name);
    CL_PauseForDialog(0, keep_running, mgr);
    {
        const uint32_t second = string_copy(&heading);
        const uint32_t first = string_copy(&message);
        ui_call_esi_edi_2(0x00313f20, field<uint32_t>(mgr, 0x1c), 0, first, second);  // UIDialog_SetText
    }
    {
        uint8_t* const dialog = dialog_of(mgr);
        const uint32_t second = string_copy(&heading);
        const uint32_t first = string_copy(&message);
        ui_call_esi_edi_2(0x0030e6f0, 0, address_of(dialog), first, second);
    }
    if (ok) {
        UI_DialogAddButton(mgr, 0x0082c9ec, 0x00313dc0);  // "INGAME_DIALOG_OK"
        uint8_t* const dialog = dialog_of(mgr);
        title_call_regs(0x0031f4b0, (field<uint32_t>(mgr, 0xc) & ~0xffu) | 1, address_of(dialog),
                        address_of(dialog));  // Dialog_LayoutButtons
    } else if (yes_no) {
        UI_DialogAddButton(mgr, 0x0082c9d8, 0x00313dc0);  // "INGAME_DIALOG_YES"
        UI_DialogAddButton(mgr, 0x0082c9c4, 0x00313db0);  // "INGAME_DIALOG_NO"
        uint8_t* const dialog = dialog_of(mgr);
        title_call_regs(0x0031f4b0, field<uint32_t>(mgr, 0xc) & ~0xffu, address_of(dialog),
                        address_of(dialog));  // Dialog_LayoutButtons
    }
    string_release(&heading);
    string_release(&message);
}

}  // namespace game
