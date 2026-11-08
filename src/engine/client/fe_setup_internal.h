#pragma once

// Shared by the FE_Setup* files (fe_setup*.cpp): the entry macros and the panel helpers.

#include <cstdint>
#include <cstring>

#include "fe.h"
#include "fe_setup.h"
#include "title_data.h"
#include "ui_panel.h"

// The functions that call a text setter (UIText vtable +0x6c, the item text +0x30, the menu's add item +4) run
// those calls at the original's ESP: the entry moves the C frame below a gap and hands the C version the entry ESP
// (see FE_GAP_ENTRY in fe_menus.cpp). The C version gets the menu, the panel and the entry ESP.
#define SETUP_GAP_ENTRY(name, menu_reg)                                                                           \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"                   \
                                     "pushl %edx\n pushl %eax\n pushl " menu_reg "\n" PLATFORM_ASM_CALL(name)     \
                                     "addl $12, %esp\n"                                                           \
                                     "popl %esp\n ret")
// The plain ones: menu and panel (EAX) to the C version.
#define SETUP_ENTRY(name, menu_reg)                                                                               \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl %eax\n pushl " menu_reg "\n" PLATFORM_ASM_CALL(name)                  \
                                     "addl $8, %esp\n ret")

namespace game::setup {

template <typename T>
inline T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

inline uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
inline const char* text_at(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

inline uint32_t bits(float value) {
    uint32_t out;
    std::memcpy(&out, &value, 4);
    return out;
}

inline uint8_t* z_malloc(uint32_t size) { return title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(size); }

// A title object's vtable slot (byte offset).
template <typename F>
inline F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);
using GetMethod = long double XB_THISCALL(uint8_t*);  // an x87 result
using IntMethod = int32_t XB_THISCALL(uint8_t*);
using ColourMethod = uint32_t* XB_THISCALL(uint8_t*, uint32_t*);
using PairMethod = void XB_THISCALL(uint8_t*, float, float);

inline void call(uint8_t* object, uint32_t offset, uint32_t a) { method<WordMethod>(object, offset)(object, a); }
inline long double get(uint8_t* object, uint32_t offset) { return method<GetMethod>(object, offset)(object); }

// A title method call at the original's ESP `top` (before the call's pushes) with the original's registers.
inline void method_at(uint32_t top, TitleRegs regs, uint8_t* object, uint32_t offset, uint32_t count, const uint32_t* args) {
    regs.ecx = address_of(object);
    title_call_at_regs(top, field<uint32_t>(field<uint8_t*>(object, 0), offset), &regs, count, args);
}

inline constexpr uint32_t kLoadingMenu = 0x0082bc84;  // "LoadingMenu"
inline constexpr uint32_t kHelpbar = 0x0082cbd0;      // "Helpbar"
inline constexpr uint32_t kBack = 0x0082c7c8;         // "Back"
inline constexpr uint32_t kNumberFormat = 0x0082bccc;  // the loading picture names (sprintf with the 1-based index)

// The inlined help bar copy: a new UIText (Z_Malloc 0x98) with the source text's colour (+0xb4 into a local),
// scale (+0xa4), y (+0xcc), x (+0xc8) and kind (+0xb0), in that order; depth / layer 0, the caller's ECX flag,
// EDX 0. Null when the allocation fails.
inline uint8_t* copy_text(uint8_t* source, uint32_t ecx) {
    uint8_t* const block = z_malloc(0x98);
    if (!block) return nullptr;
    uint32_t local = 0;
    const uint32_t colour = *method<ColourMethod>(source, 0xb4)(source, &local);
    const float scale = static_cast<float>(get(source, 0xa4));
    const float y = static_cast<float>(get(source, 0xcc));
    const float x = static_cast<float>(get(source, 0xc8));
    const int32_t kind = method<IntMethod>(source, 0xb0)(source);
    return UIText_Construct(block, kind, bits(x), bits(y), 0, 0, bits(scale), colour, ecx, 0);
}

// The help bar copy with a fixed colour and EDX flag (FE_SetupLoginStatus): scale, y, x, kind as in copy_text.
inline uint8_t* copy_text_colour(uint8_t* source, uint32_t colour, uint32_t ecx, uint32_t edx) {
    uint8_t* const block = z_malloc(0x98);
    if (!block) return nullptr;
    const float scale = static_cast<float>(get(source, 0xa4));
    const float y = static_cast<float>(get(source, 0xcc));
    const float x = static_cast<float>(get(source, 0xc8));
    const int32_t kind = method<IntMethod>(source, 0xb0)(source);
    return UIText_Construct(block, kind, bits(x), bits(y), 0, 0, bits(scale), colour, ecx, edx);
}

// The copy of the panel text `source_name` (default Helpbar) at `help_offset` (+0x13c (1)) with the help text `text`
// (+0x6c at the original's ESP `top`; EBX = the copy, EDI = the source text).
inline void setup_help(uint32_t top, TitleRegs regs, uint8_t* menu, uint32_t panel_offset, uint32_t help_offset,
                uint32_t text = 0x0082c37c, uint32_t source_name = kHelpbar) {
    uint8_t* const source = Panel_FindText(text_at(source_name), field<uint8_t*>(menu, panel_offset));
    uint8_t* const copy = copy_text(source, 0);
    field<uint8_t*>(menu, help_offset) = copy;
    call(field<uint8_t*>(menu, help_offset), 0x13c, 1);
    regs.ebx = address_of(copy);
    regs.edi = address_of(source);
    const uint32_t args[1] = {text};  // default "FEMENU_OP_HELPBAR"
    method_at(top, regs, field<uint8_t*>(menu, help_offset), 0x6c, 1, args);
}

// The picture `name` through its slot in the panel's picture list (+0, count +8; slot -1 when absent): +0x34 (0).
inline void hide_picture(uint8_t* list, const char* name) {
    uint8_t* const picture = Panel_FindPicture(list, name);
    uint8_t** const pictures = field<uint8_t**>(list, 0);
    const int32_t count = field<int32_t>(list, 8);
    int32_t index = -1;
    for (int32_t j = 0; j < count; ++j)
        if (pictures[j] == picture) {
            index = j;
            break;
        }
    call(pictures[index], 0x34, 0);
}

// A panel text's title (+0x6c at `top`, EBX = its name) and its colour (+0x90, bytes 19 21 28 ff) from a second find.
inline void set_title(uint32_t top, TitleRegs regs, uint8_t* menu, uint32_t panel_offset, uint32_t name, uint32_t text) {
    uint8_t* const title = Panel_FindText(text_at(name), field<uint8_t*>(menu, panel_offset));
    regs.ebx = name;
    const uint32_t args[1] = {text};
    method_at(top, regs, title, 0x6c, 1, args);
    uint8_t* const again = Panel_FindText(text_at(name), field<uint8_t*>(menu, panel_offset));
    call(again, 0x90, 0xff282119);
}

// Items 0..count-1 from the panel texts `names` (menu vtable +4 (index, text, 0) at the original's ESP `top`, EBX =
// the name, EDI = the menu vtable, as in the original).
inline void add_items(uint32_t top, TitleRegs regs, uint8_t* menu, uint32_t panel_offset, const uint32_t* names,
               uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* const text = Panel_FindText(text_at(names[i]), field<uint8_t*>(menu, panel_offset));
        regs.ebx = names[i];
        regs.edi = field<uint32_t>(menu, 0);
        const uint32_t args[3] = {i, address_of(text), 0};
        method_at(top, regs, menu, 4, 3, args);
    }
}

inline uint8_t* item(uint8_t* menu, uint32_t index) { return field<uint8_t*>(field<uint8_t*>(menu, 4), index * 4); }

inline uint8_t* find_picture_at(uint8_t* menu, uint32_t panel_offset, const char* name) {
    return Panel_FindPicture(field<uint8_t*>(menu, panel_offset), name);
}

inline uint8_t* find_picture(uint8_t* menu, uint32_t name) { return Panel_FindPicture(field<uint8_t*>(menu, 0x34), text_at(name)); }

// The three loading screens: `count` numbered pictures from +0x48 (names from sprintf of `first` + i), then the
// five named pictures `names` to `named` + i * 4, Back to `back`, the LoadingMenu text to `title`, and the help bar
// copy (ECX flag 0x40, scale 1.3) to `title` + 4.
inline void setup_loading(uint8_t* menu, uint8_t* panel, int32_t first, int32_t count, const uint32_t* names,
                   uint32_t name_count, uint32_t named, uint32_t back, uint32_t title) {
    field<uint8_t*>(menu, 0x34) = panel;
    for (int32_t i = 0; i < count; ++i) {
        char name[16];
        title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901)(name, text_at(kNumberFormat),
                                                                              first + i);  // _sprintf
        field<uint8_t*>(menu, 0x48 + uint32_t(i) * 4) = Panel_FindPicture(field<uint8_t*>(menu, 0x34), name);
    }
    for (uint32_t i = 0; i < name_count; ++i) field<uint8_t*>(menu, named + i * 4) = find_picture(menu, names[i]);
    field<uint8_t*>(menu, back) = find_picture(menu, kBack);
    field<uint8_t*>(menu, title) = Panel_FindText(text_at(kLoadingMenu), field<uint8_t*>(menu, 0x34));
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
    field<uint8_t*>(menu, title + 4) = copy_text(source, 0x40);
    call(field<uint8_t*>(menu, title + 4), 0x13c, 1);
    call(field<uint8_t*>(menu, title + 4), 0x60, 0x3fa66666);  // 1.3
}

// The list menus: panel to `panel_offset` (Panel_SetLayer 2 when `layer`), items from the panel texts `names`, the
// item texts `item_texts` (item vtable +0x30), the Helpbar copy at `help_offset` (+0x13c (1)) with its text (+0x6c);
// the setters at the original's ESP (three pushes).
inline void setup_list(uint8_t* menu, uint8_t* panel, uint32_t entry_esp, uint32_t panel_offset, bool layer,
                const uint32_t* names, uint32_t count, const uint32_t* item_texts, uint32_t text_count,
                uint32_t help_offset) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, panel_offset) = panel;
    if (layer) Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    add_items(top, regs, menu, panel_offset, names, count);
    for (uint32_t i = 0; i < text_count; ++i) {
        const uint32_t args[1] = {item_texts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, panel_offset));
    uint8_t* const copy = copy_text(source, 0);
    field<uint8_t*>(menu, help_offset) = copy;
    call(field<uint8_t*>(menu, help_offset), 0x13c, 1);
    regs.ebx = address_of(copy);
    regs.edi = address_of(source);
    const uint32_t args[1] = {0x0082c37c};
    method_at(top, regs, field<uint8_t*>(menu, help_offset), 0x6c, 1, args);
}

// FE_SetupInGameLiveOptions / FE_SetupXboxLiveSingle (the same code twice).
inline void setup_live(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    static constexpr uint32_t kTexts[3] = {0x0083c9fc, 0x0083c9ec, 0x0083c9e0};
    static constexpr uint32_t kItemTexts[3] = {0x0083c9cc, 0x0083ca34, 0x0083ca5c};
    setup_list(menu, panel, entry_esp, 0x34, false, kTexts, 3, kItemTexts, 3, 0x38);
}

}  // namespace game::setup
