#include "fe.h"

#include <cstring>

#include "ngl.h"
#include "qcommon.h"
#include "script_string.h"
#include "snd.h"
#include "title_data.h"

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

const float& g(uint32_t address) { return title_global<float>(address); }
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
const char* text_at(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

// A title object's vtable slot (byte offset).
template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using TextMethod = void XB_THISCALL(uint8_t*, const char*);
using FlagMethod = void XB_THISCALL(uint8_t*, uint32_t);
using WrapMethod = void XB_THISCALL(uint8_t*, const char*, uint32_t, uint32_t);

void set_text(uint8_t* item, uint32_t offset, const char* text) { method<TextMethod>(item, offset)(item, text); }
// +0x140: wrapped text, width 300, -1.0
void set_wrapped(uint8_t* item, const char* text) { method<WrapMethod>(item, 0x140)(item, text, 300, 0xbf800000); }

uint8_t* menu_item(uint8_t* menu, uint32_t index) { return field<uint8_t**>(menu, 4)[index]; }

int32_t title_sprintf(char* out, uint32_t format, int32_t a, int32_t b = 0) {
    return title_function<int32_t XB_CDECL(char*, const char*, int32_t, int32_t)>(0x001ee901)(out, text_at(format),
                                                                                             a, b);  // _sprintf
}

// The original's inline strcmp: a null name (an empty ScriptString) faults there too.
bool same_text(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (!*a) return true;
    }
}

// "" or "0" in front of a number below 10.
uint32_t leading_zero(int32_t value) { return value > 9 ? 0x0082dfeb : 0x0082e098; }

const char* format_play_time(int32_t* time) {
    int32_t hours = time[0x30 / 4] + time[0x34 / 4] * 24;
    if (hours > 999) {
        hours = 999;
        time[0x34 / 4] = 41;
        time[0x30 / 4] = 15;
    }
    const int32_t seconds = time[0x28 / 4];
    const int32_t minutes = time[0x2c / 4];
    const uint32_t args[] = {0x0082adb4,  // "%s%i:%s%i:%s%i"
                             leading_zero(hours),   uint32_t(hours),   leading_zero(minutes),
                             uint32_t(minutes),     leading_zero(seconds), uint32_t(seconds)};
    return va(args);
}

// Bonus level records (0x48 bytes from 0x0095f9b0): the first word of level n (1-based) while it is unlocked
// (0x008dce54 set or 0x008dce44 above n), else 0.
uint32_t unlocked_level_word(int32_t n) {
    if (title_global<uint8_t>(0x008dce54) || title_global<int32_t>(0x008dce44) > n)
        return title_global<uint32_t>(0x0095f968 + uint32_t(n) * 0x48);
    return 0;
}

void set_vehicle_or_weapon_text(uint8_t* menu, uint32_t items, uint32_t name_format, uint32_t info_format,
                                uint32_t first_count, uint32_t categories) {
    char text[0x20];
    int32_t index = field<int16_t>(menu, 0x22);
    title_sprintf(text, name_format, index + 1, field<int32_t>(menu, items + uint32_t(index) * 4));
    set_text(field<uint8_t*>(menu, 0x3c), 0x6c, text);
    index = field<int16_t>(menu, 0x22);
    title_sprintf(text, info_format, index + 1, field<int32_t>(menu, items + uint32_t(index) * 4));
    set_wrapped(field<uint8_t*>(menu, 0x44), text);
    int32_t counts[18] = {};
    title_function<void XB_STDCALL(uint32_t, int32_t*)>(0x00312e30)(0x0095f924, counts);
    index = field<int16_t>(menu, 0x22);
    const int32_t count = uint32_t(index) < categories ? counts[first_count + uint32_t(index)] : 0;
    title_sprintf(text, 0x0082bb5c, field<int32_t>(menu, items + uint32_t(index) * 4), count);  // "%i / %i"
    set_text(field<uint8_t*>(menu, 0x40), 0x6c, text);
}

}  // namespace

PLATFORM_ASM_ENTRY(FE_BonusCESetText_entry, "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusCESetText) "addl $4, %esp\n"
                                            "ret")

void XB_CDECL FE_BonusCESetText(uint8_t* menu) {
    static constexpr int32_t kCounts[6] = {0x26, 0xf, 0xf, 3, 9, 0xb};
    const int32_t index = field<int16_t>(menu, 0x22);
    char text[0x20];
    title_sprintf(text, 0x0082bb5c, field<int32_t>(menu, 0x130 + uint32_t(index) * 4),
                  uint32_t(index) <= 5 ? kCounts[index] : 0);  // "%i / %i"
    set_text(field<uint8_t*>(menu, 0x3c), 0x6c, text);
}

PLATFORM_ASM_ENTRY(FE_BonusArtCategoryLabel_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusArtCategoryLabel) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_BonusArtCategoryLabel(uint8_t* menu) {
    static constexpr uint32_t kTexts[6] = {0x0082a928, 0x0082a914, 0x0082a900, 0x0082a8f0, 0x0082a8e0, 0x0082a8d0};
    for (uint32_t i = 0; i < 6; ++i) set_text(menu_item(menu, i), 0x30, text_at(kTexts[i]));
}

PLATFORM_ASM_ENTRY(FE_BonusMovieLabel_entry, "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusMovieLabel) "addl $4, %esp\n"
                                             "ret")

void XB_CDECL FE_BonusMovieLabel(uint8_t* menu) {
    static constexpr uint32_t kTexts[5] = {0x0082aa8c, 0x0082aa7c, 0x0082aa6c, 0x0082aa5c, 0x0082aa4c};
    for (uint32_t i = 0; i < 5; ++i) set_text(menu_item(menu, i), 0x30, text_at(kTexts[i]));
    const uint16_t saved = field<uint16_t>(menu, 0x40);
    menu[0x3c] = 0;
    field<uint16_t>(menu, 0x22) = saved;
}

const char* XB_REGS1 XB_CDECL FE_FormatPlayTime(int32_t* time) { return format_play_time(time); }

const char* XB_REGS1 XB_CDECL FE_FormatPlayTime2(int32_t* time) { return format_play_time(time); }

PLATFORM_ASM_ENTRY(FE_BonusCharacterSetText_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusCharacterSetText) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_BonusCharacterSetText(uint8_t* menu) {
    char text[0x20];
    title_sprintf(text, 0x0082b7b4, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_NAME
    set_text(field<uint8_t*>(menu, 0x68), 0x6c, text);
    title_sprintf(text, 0x0082b7a0, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_STATS
    set_wrapped(field<uint8_t*>(menu, 0x6c), text);
    title_sprintf(text, 0x0082b78c, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_BIO
    set_wrapped(field<uint8_t*>(menu, 0x70), text);
}

PLATFORM_ASM_ENTRY(FE_AspectOptionLabel_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_AspectOptionLabel) "addl $8, %esp\n"
                   "ret")

void XB_CDECL FE_AspectOptionLabel(uint8_t* menu, uint32_t ecx) {
    set_text(field<uint8_t*>(menu, 0x3c), 0x6c, text_at(menu[0x50] ? 0x0082bd44 : 0x0082bd34));
    uint32_t color = ecx;
    uint8_t* const item = menu_item(menu, 0);
    const uint32_t* got = method<uint32_t* XB_THISCALL(uint8_t*, uint32_t*)>(item, 0x84)(item, &color);
    uint8_t* const label = field<uint8_t*>(menu, 0x3c);
    method<FlagMethod>(label, 0x90)(label, *got);
}

PLATFORM_ASM_ENTRY(FE_CommandDisplayName_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(FE_CommandDisplayName) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL FE_CommandDisplayName(int32_t key) {
    static constexpr struct {
        uint32_t command;
        uint32_t name;
    } kNames[] = {
        {0x0082d6ac, 0x0082bf9c}, {0x0082dfa8, 0x0082bf88}, {0x0082d694, 0x0082bf74}, {0x0082d678, 0x0082bf60},
        {0x0082dfb0, 0x0082bf48}, {0x0082d654, 0x0082bf34}, {0x0082dfc0, 0x0082bf20}, {0x0082d5f4, 0x0082bf0c},
        {0x0082d640, 0x0082bef8}, {0x0082d62c, 0x0082bee4}, {0x0082bed8, 0x0082bec0}, {0x0082d5d4, 0x0082bea0},
        {0x0082d5ac, 0x0082be80}, {0x0082d610, 0x0082be68}, {0x0082be5c, 0x0082be44}, {0x0082be38, 0x0082be24},
        {0x0082d6cc, 0x0082be08}, {0x0082bdfc, 0x0082bde4},
    };
    const char* binding = text_at(0x0082dfeb);  // ""
    if (key != -1) {
        binding = title_global<const char*>(0x0093425c + uint32_t(key) * 8);  // keys[key].binding
        if (!binding) return text_at(0x0082dfeb);
    }
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    for (const auto& entry : kNames)
        if (!title_function<Stricmp>(0x001f1443)(binding, text_at(entry.command))) return text_at(entry.name);
    if (key == 13) return text_at(0x0082bfe8);
    return text_at(key == 27 ? 0x0082bfd0 : 0x0082bdd8);  // FEMENU_NA
}

// The set-text methods store the address of a stack local in a heap word (0x100006a8), so
// every call runs where the original's call puts it: the entry moves our frame below a gap,
// passes the original's entry ESP, and each call leaves the original's return address.
PLATFORM_ASM_ENTRY(FE_SaveSlotsUpdateText_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")  // nothing saved above the gap: the calls' arguments go there
                   "pushl %eax\n"
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FE_SaveSlotsUpdateText) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

namespace {

// One thiscall method call (one stack argument) with ESP at `top`, returning to `ret`.
void set_text_at(uint32_t top, uint32_t ret, uint8_t* menu, uint8_t* item, uint32_t offset, uint32_t text,
                 uint32_t edi) {
    const uint32_t vtable = field<uint32_t>(item, 0);
    const TitleRegs regs{text, 0, address_of(item), vtable, address_of(menu), edi};
    title_call_at_regs(top, field<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{vtable}), offset), &regs, 1, &text);
    *reinterpret_cast<uint32_t*>(uintptr_t{top - 8}) = ret;
}

}  // namespace

void XB_CDECL FE_SaveSlotsUpdateText(uint32_t entry, uint8_t* menu, uint32_t edi) {
    if (menu[0x36]) {  // card corrupt; push edi
        *reinterpret_cast<uint32_t*>(uintptr_t{entry - 4}) = edi;
        for (uint32_t i = 0; i < 4; ++i)
            set_text_at(entry - 4, 0x003126b0, menu, menu_item(menu, i), 0x30, 0x0082ada0, i * 4);
        return;
    }
    // save slots {name +0x64, empty +0x6c}, 12 bytes each; return addresses {new, name}
    static constexpr uint32_t kReturns[4][2] = {
        {0x003126d0, 0x003126d9}, {0x003126f0, 0x00312701}, {0x0031271b, 0x0031272c}, {0x00312746, 0x0031275a}};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* const item = menu_item(menu, i);
        if (menu[0x6c + i * 12])
            set_text_at(entry, kReturns[i][0], menu, item, 0x30, 0x0082ad90, edi);  // FEMENU_OP_NEW
        else
            set_text_at(entry, kReturns[i][1], menu, item, 0x34, field<uint32_t>(menu, 0x64 + i * 12), edi);
    }
    const bool empty = menu[(uint32_t(field<int16_t>(menu, 0x22)) + 9) * 12] == 1;
    if (field<uint32_t>(menu, 0x4c) || empty)
        set_text_at(entry, 0x0031276e, menu, field<uint8_t*>(menu, 0x40), 0x6c, 0x0082c37c, edi);
    else
        set_text_at(entry, 0x0031278c, menu, field<uint8_t*>(menu, 0x40), 0x6c, 0x0082b10c, edi);
}

PLATFORM_ASM_ENTRY(FE_BonusConceptSetText_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusConceptSetText) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_BonusConceptSetText(uint8_t* menu) {
    const int32_t index = field<int16_t>(menu, 0x22);
    const uint32_t count = unlocked_level_word(index + 1);
    char text[0x20];
    title_sprintf(text, 0x0082bb5c, field<int32_t>(menu, 0x8c + uint32_t(index) * 4), int32_t(count));  // "%i / %i"
    set_text(field<uint8_t*>(menu, 0x3c), 0x6c, text);
}

PLATFORM_ASM_ENTRY(FE_BonusLevelLabel_entry, "pushl %edi\n" PLATFORM_ASM_CALL(FE_BonusLevelLabel) "addl $4, %esp\n"
                                             "ret")

void XB_CDECL FE_BonusLevelLabel(uint8_t* menu) {
    for (uint32_t i = 0; i < 13; ++i) {
        const int32_t n = int32_t(i) + 1;
        method<FlagMethod>(menu_item(menu, i), 0x20)(menu_item(menu, i), 0);
        const int32_t value = int32_t(unlocked_level_word(n));
        const int32_t best = title_global<int32_t>(0x008dced8 + i * 4);
        if (!value) method<FlagMethod>(menu_item(menu, i), 0x20)(menu_item(menu, i), 1);
        if (value > best && best > 0) field<int32_t>(menu, 0x8c + i * 4) = best + 1;
        char text[0x20];
        title_sprintf(text, 0x0082b3b0, n);  // BONUS_BONUS_LEVEL_%i
        set_text(menu_item(menu, i), 0x30, text);
        uint8_t* const item = menu_item(menu, i);
        if (method<uint8_t XB_THISCALL(uint8_t*)>(item, 0x24)(item) == 1)
            set_text(menu_item(menu, i), 0x30, text_at(0x0082bb8c));  // BONUS_LOCKED
    }
    title_function<void XB_STDCALL(uint8_t*)>(0x00312b40)(menu);
    field<uint32_t>(menu, 0x40) = 0x3f800000;  // 1.0
    menu[0x44] = 0;
}

PLATFORM_ASM_ENTRY(FE_BonusVehicleSetText_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusVehicleSetText) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_BonusVehicleSetText(uint8_t* menu) {
    set_vehicle_or_weapon_text(menu, 0x70, 0x0082b3e0, 0x0082b3c8, 10, 6);
}

PLATFORM_ASM_ENTRY(FE_BonusWeaponSetText_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_BonusWeaponSetText) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_BonusWeaponSetText(uint8_t* menu) {
    set_vehicle_or_weapon_text(menu, 0x84, 0x0082b564, 0x0082b54c, 0, 10);
}

PLATFORM_ASM_ENTRY(UIMenu_Init_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIMenu_Init) "addl $28, %esp\n"
                   "ret $20")

uint8_t* XB_CDECL UIMenu_Init(uint8_t* menu, int32_t count, uint8_t* owner, uint32_t a2, uint32_t a3, uint16_t a4,
                              uint32_t flags) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // vtable
    field<uint32_t>(menu, 0x2c) = 0;
    field<uint32_t>(menu, 4) =
        title_function<uint32_t XB_CDECL(uint32_t)>(0x00042c30)(uint32_t(count) * 4);  // Heap_Alloc16
    for (int32_t i = 0; i < count; ++i) field<uint32_t*>(menu, 4)[i] = 0;
    field<uint32_t>(menu, 0xc) = a2;
    field<uint32_t>(menu, 0x10) = a3;
    field<uint16_t>(menu, 0x20) = 0;
    field<uint16_t>(menu, 0x22) = 0xffff;
    field<uint8_t*>(menu, 8) = owner;
    field<uint16_t>(menu, 0x2a) = uint16_t(flags & ~0x200u);
    field<uint16_t>(menu, 0x26) = uint16_t(count);
    field<uint32_t>(menu, 0x14) = 0x1c;
    field<uint32_t>(menu, 0x18) = 0;
    field<uint16_t>(menu, 0x28) = a4;
    field<uint16_t>(menu, 0x24) = 0xffff;
    menu[0x31] = 1;
    menu[0x30] = 0;
    menu[0x33] = owner ? method<uint8_t XB_THISCALL(uint8_t*)>(owner, 0x24)(owner) : 0;
    return menu;
}

PLATFORM_ASM_ENTRY(UIText_Init_entry,
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIText_Init) "addl $44, %esp\n"
                   "ret $40")

uint8_t* XB_CDECL UIText_Init(uint8_t* text, int32_t kind, const char* string, uint32_t x, uint32_t y, int32_t depth,
                              int32_t layer, uint32_t scale, uint8_t a8, uint8_t a9, uint32_t a10) {
    using Assign = void XB_THISCALL(uint8_t*, const char*);
    text[0x10] = 4;
    field<uint32_t>(text, 4) = 0x3f800000;  // 1.0
    field<uint32_t>(text, 0) = 0x008298b8;  // vtable
    ScriptString_FromRep(reinterpret_cast<ScriptString*>(text + 0x1c), nullptr);
    const uint32_t colour = title_global<uint32_t>(0x00922eb8);
    for (uint32_t offset = 0x20; offset <= 0x4c; offset += 4) field<uint32_t>(text, offset) = colour;
    field<uint32_t>(text, 0x50) = 0;
    ScriptString_FromRep(reinterpret_cast<ScriptString*>(text + 0x54), nullptr);
    field<int32_t>(text, 0x18) = kind;
    title_function<Assign>(0x0011dd20)(text + 0x1c, string);  // ScriptString_AssignCString
    field<uint32_t>(text, 0x2c) = x;
    field<uint32_t>(text, 0x30) = y;
    field<uint32_t>(text, 0x34) = 0;
    float d = static_cast<float>(depth);
    if (layer == 8) {
        field<float>(text, 8) = d;
    } else {
        if (0.0f > d)
            d = 0.0f;
        else if (d > 1000.0f)
            d = 1000.0f;
        const float key = (static_cast<float>(layer * 1000) + d) * g(0x0083ec68);  // 0.125
        title_function<void XB_THISCALL(uint8_t*, float)>(field<uint32_t>(field<uint8_t*>(text, 0), 0x24))(text, key);
    }
    field<uint32_t>(text, 0x38) = scale;
    field<uint32_t>(text, 0x3c) = scale;
    field<uint32_t>(text, 0x44) = scale;
    field<uint32_t>(text, 0x40) = 0;
    field<uint32_t>(text, 0x48) = scale;
    field<int32_t>(text, 0x58) = -1;
    field<uint32_t>(text, 0x4c) = 0;
    field<uint32_t>(text, 0x50) = a10;
    title_function<Assign>(0x0011dd20)(text + 0x54, text_at(0x0082dfeb));  // ""
    const uint8_t flags = uint8_t(a9 | a8 | 4);
    field<uint32_t>(text, 0x14) = 0;
    text[0x5c] = text[0x53] ? uint8_t(flags | 1) : flags;
    return text;
}

PLATFORM_ASM_ENTRY(UIText_Construct_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n" PLATFORM_ASM_CALL(UIText_Construct) "addl $40, %esp\n"
                   "ret $32")

uint8_t* XB_CDECL UIText_Construct(uint8_t* text, int32_t kind, uint32_t x, uint32_t y, int32_t depth, int32_t layer,
                                   uint32_t scale, uint32_t a8, uint32_t ecx, uint32_t edx) {
    UIText_Init(text, kind, text_at(0x0082dfeb), x, y, depth, layer, scale, uint8_t(edx), uint8_t(ecx), a8);
    field<uint32_t>(text, 0) = 0x0082a450;  // vtable
    field<uint32_t>(text, 0x60) = 0;
    for (uint32_t offset = 0x78; offset <= 0x80; offset += 4) field<uint32_t>(text, offset) = 0;
    text[0x94] = 0;
    text[0x95] = 0;
    for (uint32_t offset = 0x84; offset <= 0x8c; offset += 4) field<uint32_t>(text, offset) = 0;
    field<uint32_t>(text, 0x90) = 0;
    text[0x96] = 0;
    field<uint32_t>(text, 0x64) = 0x3f800000;  // 1.0
    field<uint32_t>(text, 0x70) = 0;
    field<uint32_t>(text, 0x60) = 0xffffffff;  // colour
    const int32_t type = field<int32_t>(text, 0x18);
    field<int32_t>(text, 0x74) = -1;
    const uint8_t* font = type == 7 || type == 8 ? nullptr : title_global<const uint8_t*>(0x0095f8d4 + uint32_t(type) * 4);
    int32_t width = int32_t(a8);
    int32_t height = -1;  // the argument slot the original reuses holds the colour bytes
    nglGetTextExtent(font, text_at(0x0082d764), &width, &height, field<float>(text, 0x38), field<float>(text, 0x3c));  // "!"
    const float line = platform_x87_unsigned_to_float(uint32_t(height));
    field<float>(text, 0x68) = line;
    field<float>(text, 0x6c) = line;
    return text;
}

PLATFORM_ASM_ENTRY(Panel_FindText_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Panel_FindText) "addl $8, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL Panel_FindText(const char* name, uint8_t* panel) {
    for (uint32_t i = 0; i < field<uint32_t>(panel, 0x14); ++i) {
        uint8_t* const item = field<uint8_t**>(panel, 0xc)[i];
        uint32_t name_string = address_of(panel);  // the argument slot the original reuses
        const uint32_t rep =
            *method<const uint32_t* XB_THISCALL(uint8_t*, uint32_t*)>(item, 0x9c)(item, &name_string);
        const char* text = rep ? text_at(rep + 6) : nullptr;
        const bool differs = !same_text(text, name);
        title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&name_string);  // ScriptString_Release
        if (!differs) return field<uint8_t**>(panel, 0xc)[i];
    }
    return nullptr;
}

uint8_t* XB_REGS1 XB_STDCALL Panel_FindPicture(uint8_t* list, const char* name) {
    const uint32_t count = field<uint32_t>(list, 8);
    uint8_t** const pictures = field<uint8_t**>(list, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rep = field<uint32_t>(pictures[i], 0x3c);  // name (ScriptString)
        if (same_text(rep ? text_at(rep + 6) : nullptr, name)) return pictures[i];
    }
    return reinterpret_cast<uint8_t*>(
        uintptr_t{title_call_regs(0x00320170, 0, 0, 0x0095f8d0)});  // Panel_GetDefaultPicture (EDI)
}

PLATFORM_ASM_ENTRY(FE_SetDifficultyPicture_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FE_SetDifficultyPicture) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL FE_SetDifficultyPicture(uint8_t* menu, int32_t selection) {
    for (int32_t i = 0; i < 3; ++i) {
        char name[0xc];
        title_sprintf(name, 0x0082b24c, i);  // difficulty%i
        uint8_t* const list = field<uint8_t*>(menu, 0x34);
        uint8_t* const picture = Panel_FindPicture(list, name);
        uint8_t** const pictures = field<uint8_t**>(list, 0);
        const uint32_t count = field<uint32_t>(list, 8);
        int32_t index = -1;
        for (uint32_t k = 0; k < count; ++k) {
            if (pictures[k] == picture) {
                index = int32_t(k);
                break;
            }
        }
        uint8_t* const shown = pictures[index];  // [-1] when not found, as in the original
        method<FlagMethod>(shown, 0x34)(shown, i == selection ? 1 : 0);
    }
    const int16_t index = field<int16_t>(menu, 0x22);
    const uint32_t one = 0x3f800000;  // 1.0
    uint8_t* const first = field<uint8_t*>(menu, 0x3c);
    method<FlagMethod>(first, 0x7c)(first, index == 0 ? 0 : one);
    uint8_t* const second = field<uint8_t*>(menu, 0x40);
    method<FlagMethod>(second, 0x7c)(second, index == 0 || index == 1 ? one : 0);
}

PLATFORM_ASM_ENTRY(UIMenu_FreeItems_entry, "pushl %esi\n" PLATFORM_ASM_CALL(UIMenu_FreeItems) "addl $4, %esp\n"
                                           "ret")

void XB_CDECL UIMenu_FreeItems(uint8_t* menu) {
    for (int32_t i = 0; i < field<int16_t>(menu, 0x26); ++i) {
        uint8_t* const item = field<uint8_t**>(menu, 4)[i];
        if (item) method<FlagMethod>(item, 0)(item, 1);  // deleting destructor
        field<uint8_t**>(menu, 4)[i] = nullptr;
    }
    title_function<void XB_CDECL(void*)>(0x000428d0)(field<void*>(menu, 4));  // Z_Free
    const uint32_t handle = field<uint32_t>(menu, 0x2c);
    const uint32_t slot = handle & 0xfff;
    const auto live = [&] { return slot < 0x200 && (handle >> 12) == title_global<uint32_t>(0x00962864 + slot * 8); };
    if (live() && title_global<uint32_t>(0x00962860 + slot * 8))
        S_StopChannel(live() ? title_global<uint8_t*>(0x00962860 + slot * 8) : nullptr);
    field<uint32_t>(menu, 0x2c) = 0;
    field<uint32_t>(menu, 4) = 0;
    field<uint16_t>(menu, 0x26) = 0;
}

PLATFORM_ASM_ENTRY(Profile_SetDefaults_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Profile_SetDefaults) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Profile_SetDefaults(uint8_t* p) {
    if (title_global<uint8_t>(0x0095ee09)) return;
    field<int32_t>(p, 0x44) = 0x19;
    field<int32_t>(p, 0x48) = 0x19;
    field<int32_t>(p, 0x10) = 0;
    field<int32_t>(p, 0x14) = -1;
    field<int32_t>(p, 0x18) = 0;
    field<int32_t>(p, 0x1c) = 0;
    field<int32_t>(p, 0x20) = 1;
    p[0x24] = 0;
    p[0x25] = 0;
    for (uint32_t offset = 0x28; offset <= 0x34; offset += 4) field<int32_t>(p, offset) = 0;
    p[0xe0] = 0;
    field<int32_t>(p, 0xe4) = 0;
    p[0x38] = 0;
    p[0x39] = 1;
    p[0x3a] = 1;
    field<int32_t>(p, 0x3c) = 0;
    p[0x40] = 0;
    p[0x41] = 0;
    p[0x42] = 1;
    if (uint8_t* const invert = title_global<uint8_t*>(0x00929c0c)) p[0x41] = 0.0f > field<float>(invert, 0x1c);
    field<int32_t>(p, 0x4c) =
        field<int32_t>(Cvar_Get(text_at(0x0082e084), text_at(0x0082e098), 1), 0x20);  // cg_controllerconfig "0"
    p[0x50] = 1;
    field<int32_t>(p, 0x54) = 1;
    field<int32_t>(p, 0x58) = 0x32;
    p[0x5c] = 0;
    p[0x5d] = 0;
    for (uint32_t offset = 0x60; offset <= 0xdc; offset += 4) field<int32_t>(p, offset) = 0;
    field<int32_t>(p, 0x2f8) = 0;
    field<int32_t>(p, 0x2fc) = 0;
    p[0x39c] = 0;
    p[0x2f0] = 0;
    field<int32_t>(p, 0x2f4) = 0x7fffffff;
    p[0x39d] = 1;
    p[0x39e] = 0;
    p[0x3a0] = 0;
}

void XB_CDECL UI_PlayTypeSound() {
    alignas(16) float origin[4];
    alignas(16) float axis[4];
    std::memcpy(origin, &title_global<float>(0x0082e110), sizeof origin);  // (0, 0, 0, 0)
    std::memcpy(axis, &title_global<float>(0x0082e290), sizeof axis);      // (0, 0, 0, 1)
    const uint32_t table = title_global<uint32_t>(0x0095f4c4);
    const int32_t alias = S_FindSoundByHash(text_at(0x0082b1b0));  // "UI_Type"
    if (alias == -1) return;
    uint32_t handle = 0;
    using PlayAlias = uint32_t* XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t,
                                                     uint32_t, const float*, const float*, uint32_t, uint32_t,
                                                     uint32_t, uint32_t);
    title_function<PlayAlias>(0x002c30a0)(uint32_t(alias), address_of(&handle), table, &handle, 0, 0, 0, axis, origin,
                                          0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000);  // S_PlaySoundAlias
}

uint8_t XB_CDECL PauseMenu_ReloadCheckpoint() {
    char* const one = const_cast<char*>(text_at(0x0082e058));  // "1"
    if (title_global<uint8_t>(0x008dd1cf) == 1) {
        title_global<uint8_t>(0x0095ee08) = 1;
        Cvar_Set2(text_at(0x0082c89c), one, 1);              // g_reloading
        Cmd_ExecuteRegisteredCommand(text_at(0x0082c894));  // "reboot"
        return 1;
    }
    field<int32_t>(field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8fc), 4), 0x1c), 0x48) = 4;
    uint8_t* const fe = title_global<uint8_t*>(0x0095f8fc);
    title_global<uint8_t>(0x0095f919) = 1;
    Cvar_Set2(text_at(0x0082ca00), one, 1);  // cl_paused
    method<FlagMethod>(fe, 0xc)(fe, 7);
    return 0;
}


void XB_THISCALL UIElement_SetDepth(uint8_t* element, float depth) { field<float>(element, 8) = depth; }

void XB_THISCALL UIElement_SetAlign(uint8_t* element, int16_t align) {
    const uint8_t flags = element[0x10] & 0x3f;
    element[0x10] = flags;
    if (align == 0)
        element[0x10] = flags | 0x40;
    else if (align == 2)
        element[0x10] = flags | 0x80;
}

void XB_THISCALL UIElement_UpdateFade(uint8_t* element, float dt) {
    if (!method<uint8_t XB_THISCALL(uint8_t*)>(element, 0x30)(element)) return;
    const uint8_t flags = element[0x10];
    if (flags & 0x10) {
        const float alpha = dt / field<float>(element, 0xc) + field<float>(element, 4);
        field<float>(element, 4) = alpha;
        if (alpha >= 1.0f) {
            field<float>(element, 4) = 1.0f;
            element[0x10] = flags & 0xcf;
        }
    } else if (flags & 0x20) {
        const float alpha = field<float>(element, 4) - dt / field<float>(element, 0xc);
        field<float>(element, 4) = alpha;
        if (0.0f >= alpha) {
            field<float>(element, 4) = 0.0f;
            element[0x10] = flags & 0xcb;
        }
    }
}

namespace {

NglQuad* screen_quad(uint8_t* quad) { return reinterpret_cast<NglQuad*>(quad + 0x40); }
// Base corner point i (x, y, z).
float* base_point(uint8_t* quad, int32_t i) { return &field<float>(quad, uint32_t(i) * 12); }

}  // namespace

void XB_REGS1 XB_STDCALL UIQuad_Translate(uint8_t* quad, float dx, float dy) {
    NglQuad* const q = screen_quad(quad);
    for (int32_t i = 0; i < 4; ++i) {
        const float y = q->verts[i].y + dy;
        const float x = q->verts[i].x + dx;
        nglSetQuadVertexPos(q, i, x, y);
    }
}

void XB_REGS1 XB_CDECL UIQuad_ResetPositions(uint8_t* quad) {
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(screen_quad(quad), i, base_point(quad, i)[0], base_point(quad, i)[1]);
}

void XB_REGS3 XB_CDECL UIQuad_SetCornerPositions(uint8_t* quad, uint32_t, const float* points) {
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(screen_quad(quad), i, points[i * 3], points[i * 3 + 1]);
}

void XB_REGS1 XB_STDCALL UIQuad_SetVertexPositions(const float* xs, uint8_t* quad, const float* ys) {
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(screen_quad(quad), i, xs[i], ys[i]);
}

void XB_REGS3 XB_CDECL UIQuad_SetCornerUVs(uint8_t* quad, uint32_t, const float* points) {
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexUV(screen_quad(quad), i, points[i * 3], points[i * 3 + 1]);
}

void XB_REGS3 XB_STDCALL UIPictureBlock_SetVertexUVs(const float* us, uint32_t, const float* vs, uint8_t* quad) {
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexUV(screen_quad(quad), i, us[i], vs[i]);
}

PLATFORM_ASM_ENTRY(UIQuad_PlaceScaled_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIQuad_PlaceScaled) "addl $20, %esp\n"
                   "popl %ecx\n"
                   "popl %edx\n"
                   "ret $16")

void XB_CDECL UIQuad_PlaceScaled(uint8_t* quad, float x, float y, float sx, float sy) {
    float min_x = 1000.0f, max_x = -1000.0f, min_y = 1000.0f, max_y = -1000.0f;
    for (int32_t i = 0; i < 4; ++i) {
        const float* const p = base_point(quad, i);
        if (min_x > p[0]) min_x = p[0];
        if (p[0] > max_x) max_x = p[0];
        if (min_y > p[1]) min_y = p[1];
        if (p[1] > max_y) max_y = p[1];
    }
    const float cx = (max_x - min_x) * 0.5f + min_x;
    const float cy = (max_y - min_y) * 0.5f + min_y;
    NglQuad* const q = screen_quad(quad);
    for (int32_t i = 0; i < 4; ++i) {
        const float* const p = base_point(quad, i);
        q->verts[i].x = x - (cx - p[0]) * sx;
        q->verts[i].y = y - (cy - p[1]) * sy;
    }
}

PLATFORM_ASM_ENTRY(UIQuad_SetWipe_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(UIQuad_SetWipe) "addl $20, %esp\n"
                   "ret $16")

void XB_CDECL UIQuad_SetWipe(uint8_t* quad, float amount, int32_t mode, float uv_scale, float scale) {
    if (mode == 0) return;
    float t = amount;
    if (t > 1.0f) t = 2.0f - t;
    const float* const p0 = base_point(quad, 0);
    const float* const p3 = base_point(quad, 3);
    float size = mode == 1 || mode == 2 ? p3[0] - p0[0] : p3[1] - p0[1];
    size *= scale;
    NglQuad* const q = screen_quad(quad);
    // The two edges' x and y: the wiping side moves by size * t.
    const float x0 = mode == 1 ? q->verts[3].x - size * t : q->verts[0].x;
    const float x3 = mode == 2 ? size * t + x0 : q->verts[3].x;
    const float y0 = mode == 4 ? q->verts[3].y - size * t : q->verts[0].y;
    const float y3 = mode == 3 ? size * t + y0 : q->verts[3].y;
    const float u0 = q->verts[0].u;
    const float u1 = q->verts[1].u;
    // Vertices 0 and 1 with the same u: the texture runs down the quad, so the corners go in column order.
    bool columns;
    if (u0 >= u1)
        columns = 0.001f > u0 - u1;
    else
        columns = 0.001f > u1 - u0;
    float xs[4], ys[4];
    xs[0] = x0;
    ys[0] = y0;
    xs[3] = x3;
    ys[3] = y3;
    if (columns) {
        xs[1] = x0;
        xs[2] = x3;
        ys[2] = y0;
        ys[1] = y3;
    } else {
        xs[1] = x3;
        xs[2] = x0;
        ys[2] = y3;
        ys[1] = y0;
    }
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(q, i, xs[i], ys[i]);
    if (!(uv_scale > 0.0f)) return;
    const float ua = mode == 1 ? q->verts[3].u - t * uv_scale : q->verts[0].u;
    const float ub = mode == 2 ? t * uv_scale + ua : q->verts[3].u;
    const float va = mode == 4 ? q->verts[3].v - t * uv_scale : q->verts[0].v;
    const float vb = mode == 3 ? t * uv_scale + va : q->verts[3].v;
    float us[4], vs[4];
    us[0] = ua;
    vs[0] = va;
    us[3] = ub;
    vs[3] = vb;
    if (columns) {
        us[1] = ua;
        us[2] = ub;
        vs[2] = va;
        vs[1] = vb;
    } else {
        us[1] = ub;
        us[2] = ua;
        vs[2] = vb;
        vs[1] = va;
    }
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexUV(q, i, us[i], vs[i]);
}

uint8_t* XB_REGS3 XB_CDECL Color_Modulate(uint32_t, uint32_t, uint8_t* out, uint32_t a, uint32_t b) {
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = uint8_t(int32_t((a >> (i * 8)) & 0xff) * int32_t((b >> (i * 8)) & 0xff) / 255);
    return out;
}

PLATFORM_ASM_ENTRY(UIQuad_GetMaxCorner_entry,
                   "subl $16, %esp\n"
                   "movups %xmm5, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIQuad_GetMaxCorner) "addl $8, %esp\n"
                   "movups (%esp), %xmm5\n"
                   "addl $16, %esp\n"
                   "ret")

void XB_CDECL UIQuad_GetMaxCorner(float* out, const uint8_t* source) {
    const NglQuadVertex* const v = reinterpret_cast<const NglQuadVertex*>(source + 0x40);
    float u = v[0].u, w = v[0].v;
    for (int32_t i = 1; i < 4; ++i) {
        if (v[i].u > u) u = v[i].u;
        if (v[i].v > w) w = v[i].v;
    }
    out[0] = u;
    out[1] = w;
    out[2] = 0.0f;
}

PLATFORM_ASM_ENTRY(UIQuad_SetVertexAlpha_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIQuad_SetVertexAlpha) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL UIQuad_SetVertexAlpha(uint8_t* quad, int32_t vertex, float alpha) {
    const uint32_t old = screen_quad(quad)->verts[vertex].color;
    const uint32_t a = uint32_t(platform_x87_mul_ftol(alpha, g(0x0083eb8c))) & 0xff;  // 255.0
    nglSetQuadVertexColor(screen_quad(quad), vertex, a << 24 | (old & 0x00ffffff));
}

void XB_REGS3 XB_STDCALL UIQuad_SetVertexColor(int32_t vertex, uint32_t, uint8_t* quad, uint32_t color) {
    const uint32_t old = screen_quad(quad)->verts[vertex].color;
    nglSetQuadVertexColor(screen_quad(quad), vertex, (old & 0xff000000) | (color & 0x00ffffff));
}

namespace {

// MSVC's min/max of four values: (3 vs 2) vs (1 vs 0), each pick keeping the first operand on NaN.
float min4(float v0, float v1, float v2, float v3) {
    const float b = v3 > v2 ? v2 : v3;
    const float a = v1 > v0 ? v0 : v1;
    return b > a ? a : b;
}
float max4(float v0, float v1, float v2, float v3) {
    const float b = v2 > v3 ? v2 : v3;
    const float a = v0 > v1 ? v0 : v1;
    return a > b ? a : b;
}

}  // namespace

void XB_REGS3 XB_CDECL UIQuad_GetVertexMin(float* out, uint32_t, const uint8_t* quad) {
    const NglQuadVertex* const v = reinterpret_cast<const NglQuadVertex*>(quad + 0x40);
    const uint32_t z = title_global<uint32_t>(0x00922eb8);
    const float x = min4(v[0].x, v[1].x, v[2].x, v[3].x);
    const float y = min4(v[0].y, v[1].y, v[2].y, v[3].y);
    out[0] = x;
    out[1] = y;
    std::memcpy(&out[2], &z, 4);
}

void XB_REGS3 XB_CDECL UIQuad_GetVertexMax(float* out, uint32_t, const uint8_t* quad) {
    const NglQuadVertex* const v = reinterpret_cast<const NglQuadVertex*>(quad + 0x40);
    const uint32_t z = title_global<uint32_t>(0x00922eb8);
    const float x = max4(v[0].x, v[1].x, v[2].x, v[3].x);
    const float y = max4(v[0].y, v[1].y, v[2].y, v[3].y);
    out[0] = x;
    out[1] = y;
    std::memcpy(&out[2], &z, 4);
}

void XB_REGS3 XB_STDCALL UIQuad_SetVertexAlphaScaled(uint8_t* quad, uint32_t, int32_t vertex, float alpha) {
    UIQuad_SetVertexAlpha(quad, vertex, field<float>(quad, 0x30 + uint32_t(vertex) * 4) * alpha);
}

PLATFORM_ASM_ENTRY(UIQuad_ApplyMatrix_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(UIQuad_ApplyMatrix) "addl $16, %esp\n"
                   "ret $8")

void XB_CDECL UIQuad_ApplyMatrix(const float* matrix, float z, uint8_t* quad, uint32_t use_quad) {
    float xs[4], ys[4];
    for (int32_t i = 0; i < 4; ++i) {
        float x, y;
        if (uint8_t(use_quad)) {
            x = screen_quad(quad)->verts[i].x;
            y = screen_quad(quad)->verts[i].y;
        } else {
            x = base_point(quad, i)[0];
            y = base_point(quad, i)[1];
        }
        // All four lanes, as the packed original computes them (MXCSR flags); the point's w lane is x.
        float r[4];
        for (int32_t lane = 0; lane < 4; ++lane) {
            // Separate products, then the running sum first in each ADDPS: on two NaNs ADDPS keeps the first.
            const float px = matrix[lane] * x;
            const float py = matrix[4 + lane] * y;
            const float pz = matrix[8 + lane] * z;
            float s = matrix[12 + lane];
            s += px;
            s += py;
            s += pz;
            r[lane] = s;
        }
        platform_keep(r);
        xs[i] = r[0];
        ys[i] = r[1];
    }
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(screen_quad(quad), i, xs[i], ys[i]);
}

namespace {

// The title's PtrVector {data, capacity, count}: Resize (0x00013970) takes the vector in ESI and the count in EBX,
// PushBack (0x000138c0) the vector in ESI and a pointer to the value on the stack.
extern "C" PLATFORM_HIDDEN void XB_CDECL ui_ptrvector_resize(uint8_t* vector, uint32_t count);
PLATFORM_ASM_ENTRY(ui_ptrvector_resize,
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "movl 12(%esp), %esi\n"
                   "movl 16(%esp), %ebx\n"
                   "movl $0x00013970, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "ret")

extern "C" PLATFORM_HIDDEN void XB_CDECL ui_ptrvector_push(uint8_t* vector, uint8_t* const* value);
PLATFORM_ASM_ENTRY(ui_ptrvector_push,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "pushl 12(%esp)\n"
                   "movl $0x000138c0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

void* z_malloc(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(size); }
void z_free(void* pointer) { title_function<void XB_CDECL(void*)>(0x000428d0)(pointer); }

uint32_t quad_count(uint8_t* picture) { return field<uint32_t>(picture, 0x28); }
uint8_t* picture_quad(uint8_t* picture, uint32_t i) { return field<uint8_t**>(picture, 0x20)[i]; }

// A new UI quad block (0xa8 bytes): base points = the word at 0x00922eb8, nglInitQuad, +0xa4 = 1. Null when the
// allocation fails.
uint8_t* new_quad() {
    uint8_t* const quad = static_cast<uint8_t*>(z_malloc(0xa8));
    if (!quad) return nullptr;
    const uint32_t c = title_global<uint32_t>(0x00922eb8);
    for (uint32_t i = 0; i < 12; ++i) field<uint32_t>(quad, i * 4) = c;
    nglInitQuad(screen_quad(quad));
    quad[0xa4] = 1;
    return quad;
}

// The picture's centre +0x14 = the centre of its bounds.
void set_centre_from_bounds(uint8_t* picture) {
    float min_buffer[3], max_buffer[3];
    const float* const min = UIPicture_GetBoundsMin(picture, min_buffer);
    const float* const max = UIPicture_GetBoundsMax(picture, max_buffer);
    const float half = g(0x0083eb24);  // 0.5
    field<float>(picture, 0x14) = (max[0] - min[0]) * half + min[0];
    field<float>(picture, 0x18) = (max[1] - min[1]) * half + min[1];
    field<float>(picture, 0x1c) = (max[2] - min[2]) * half + min[2];
}

void scale_x(uint8_t* picture, float scale, float cx) {
    for (uint32_t i = 0; i < quad_count(picture); ++i) {
        NglQuad* const q = screen_quad(picture_quad(picture, i));
        for (int32_t v = 0; v < 4; ++v) q->verts[v].x = (q->verts[v].x - cx) * scale + cx;
    }
    field<float>(picture, 0x14) = (field<float>(picture, 0x14) - cx) * scale + cx;
}

}  // namespace

long double XB_THISCALL UIPicture_GetWidth(uint8_t* picture) {
    float max_buffer[3], min_buffer[3];
    const float* const max = UIPicture_GetBoundsMax(picture, max_buffer);
    const float* const min = UIPicture_GetBoundsMin(picture, min_buffer);
    return static_cast<long double>(max[0]) - min[0];
}

long double XB_THISCALL UIPicture_GetHeight(uint8_t* picture) {
    float max_buffer[3], min_buffer[3];
    const float* const max = UIPicture_GetBoundsMax(picture, max_buffer);
    const float* const min = UIPicture_GetBoundsMin(picture, min_buffer);
    return static_cast<long double>(max[1]) - min[1];
}

void XB_THISCALL UIPicture_SetRect(uint8_t* picture, float x0, float y0, float x1, float y1) {
    float ys[4] = {y0, y0, y1, y1};
    float xs[4] = {x0, x1, x0, x1};
    method<void XB_THISCALL(uint8_t*, const float*, const float*)>(picture, 0x88)(picture, xs, ys);
}

PLATFORM_ASM_ENTRY(UIPicture_ScaleXForAspect_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIPicture_ScaleXForAspect) "addl $12, %esp\n"
                   "ret")

void XB_CDECL UIPicture_ScaleXForAspect(uint32_t widescreen, uint8_t* picture, float cx) {
    const float scale = uint8_t(widescreen) ? g(0x0083eb58) : g(0x0083eb80);  // 0.75 : 4/3
    scale_x(picture, scale, cx);
}

void XB_THISCALL UIPicture_SetDepth(uint8_t* picture, float depth) {
    field<float>(picture, 8) = depth;
    for (uint32_t i = 0; i < quad_count(picture); ++i) nglSetQuadZ(screen_quad(picture_quad(picture, i)), depth);
}

void XB_THISCALL UIPicture_SetQuadFlags(uint8_t* picture, uint32_t flags) {
    for (uint32_t i = 0; i < quad_count(picture); ++i) nglSetQuadFlags(screen_quad(picture_quad(picture, i)), flags);
}

void XB_THISCALL UIPicture_SetTexture(uint8_t* picture, void* texture) {
    for (uint32_t i = 0; i < quad_count(picture); ++i)
        nglSetQuadTexture(screen_quad(picture_quad(picture, i)), static_cast<NglTexture*>(texture));
}

PLATFORM_ASM_ENTRY(UIPicture_Dtor_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(UIPicture_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIPicture_Dtor(uint8_t* picture) {
    field<uint32_t>(picture, 0) = 0x0082a2c0;
    void* const texture = field<void*>(picture_quad(picture, 0), 0xa0);
    if (texture) nglReleaseTexture(texture);
    for (int32_t i = 0; i < field<int32_t>(picture, 0x28); ++i) z_free(picture_quad(picture, uint32_t(i)));
    ui_ptrvector_resize(picture + 0x20, 0);
    z_free(field<void*>(picture, 0x2c));
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(picture + 0x3c);  // ScriptString_Release
    if (field<uint8_t*>(picture, 0x20)) {
        Mem_AlignedFree(field<uint8_t*>(picture, 0x20));
        field<uint8_t*>(picture, 0x20) = nullptr;
    }
    field<uint32_t>(picture, 0) = 0x00829244;
}

void XB_THISCALL UIPicture_CopyFrom(uint8_t* picture, uint8_t* source) {
    field<uint32_t>(picture, 4) = field<uint32_t>(source, 4);
    field<uint32_t>(picture, 8) = field<uint32_t>(source, 8);
    field<uint32_t>(picture, 0xc) = field<uint32_t>(source, 0xc);
    picture[0x10] = source[0x10];
    title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(0x0011d600)(picture + 0x3c, source + 0x3c);  // ScriptString_Assign
    field<uint32_t>(picture, 0x30) = field<uint32_t>(source, 0x30);
    field<uint32_t>(picture, 0x34) = field<uint32_t>(source, 0x34);
    field<uint32_t>(picture, 0x38) = field<uint32_t>(source, 0x38);
    if (field<uint8_t*>(source, 0x2c)) {
        uint8_t* const block = static_cast<uint8_t*>(z_malloc(0x34));
        if (block) platform_rep_movs(block, field<uint8_t*>(source, 0x2c), 0x34);
        field<uint8_t*>(picture, 0x2c) = block;
    } else {
        field<uint8_t*>(picture, 0x2c) = nullptr;
    }
    for (int32_t i = 0; i < field<int32_t>(source, 0x28); ++i) {
        uint8_t* quad = static_cast<uint8_t*>(z_malloc(0xa8));
        if (quad) platform_rep_movs(quad, picture_quad(source, uint32_t(i)), 0xa8);
        ui_ptrvector_push(picture + 0x20, &quad);
    }
    field<uint32_t>(picture, 0x14) = field<uint32_t>(source, 0x14);
    field<uint32_t>(picture, 0x18) = field<uint32_t>(source, 0x18);
    field<float>(picture, 0x1c) = 0.0f;
}

PLATFORM_ASM_ENTRY(UIPicture_Init_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIPicture_Init) "addl $8, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL UIPicture_Init(uint8_t* picture, const char* name) {
    field<float>(picture, 4) = g(0x0083eb20);  // 1.0
    picture[0x10] = 4;
    field<uint32_t>(picture, 0) = 0x0082a2c0;
    const uint32_t c = title_global<uint32_t>(0x00922eb8);
    field<uint32_t>(picture, 0x14) = c;
    field<uint32_t>(picture, 0x18) = c;
    field<uint32_t>(picture, 0x1c) = c;
    field<uint32_t>(picture, 0x20) = 0;
    field<uint32_t>(picture, 0x24) = 0;
    field<uint32_t>(picture, 0x28) = 0;
    ScriptString* const string = reinterpret_cast<ScriptString*>(picture + 0x3c);
    ScriptString_FromRep(string, nullptr);
    title_function<void XB_THISCALL(ScriptString*, const char*)>(0x0011dd20)(string, name);  // ScriptString_AssignCString
    field<float>(picture, 0x14) = 0.0f;
    field<float>(picture, 0x18) = 0.0f;
    field<uint32_t>(picture, 0x2c) = 0;
    field<float>(picture, 0x1c) = 0.0f;
    field<float>(picture, 0x30) = 0.0f;
    field<float>(picture, 0x34) = g(0x0083eb20);
    field<float>(picture, 0x38) = g(0x0083eb20);
    return picture;
}

PLATFORM_ASM_ENTRY(UIMeshList_ScaleXForAspect_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %edi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(UIMeshList_ScaleXForAspect) "addl $12, %esp\n"
                   "ret")

void XB_CDECL UIMeshList_ScaleXForAspect(uint32_t widescreen, uint8_t* list, float cx) {
    for (int32_t i = 0; i < field<int32_t>(list, 8); ++i) {
        const float scale = uint8_t(widescreen) ? g(0x0083eb58) : g(0x0083eb80);
        scale_x(field<uint8_t**>(list, 0)[i], scale, cx);
    }
}

uint8_t* XB_REGS3 XB_CDECL UIWorldPicture_Construct(const char* name, uint32_t, uint8_t* picture) {
    UIPicture_Init(picture, name);
    const uint32_t c = title_global<uint32_t>(0x00922eb8);
    field<uint32_t>(picture, 0) = 0x00828df8;
    field<uint32_t>(picture, 0x40) = c;
    field<uint32_t>(picture, 0x44) = c;
    field<uint32_t>(picture, 0x48) = c;
    field<float>(picture, 0x40) = 0.0f;
    field<float>(picture, 0x44) = 0.0f;
    field<float>(picture, 0x48) = 0.0f;
    return picture;
}

void XB_THISCALL UIPicture_SetAlpha(uint8_t* picture, float alpha) {
    if (quad_count(picture) == 0) return;
    const uint32_t a = uint32_t(platform_x87_mul_ftol(alpha, g(0x0083eb8c))) & 0xff;  // 255.0
    for (uint32_t i = 0; i < quad_count(picture); ++i) {
        for (int32_t v = 0; v < 4; ++v) {
            NglQuad* const q = screen_quad(picture_quad(picture, i));
            nglSetQuadVertexColor(q, v, a << 24 | (q->verts[v].color & 0x00ffffff));
        }
    }
}

void XB_THISCALL UIPicture_SetColorRGB(uint8_t* picture, uint32_t color) {
    for (uint32_t i = 0; i < quad_count(picture); ++i)
        for (int32_t v = 0; v < 4; ++v) UIQuad_SetVertexColor(v, 0, picture_quad(picture, i), color);
}

void XB_THISCALL UIWorldPicture_UpdateScreenPos(uint8_t* picture) {
    // The position (x, y, z) with x again in the w lane; the result buffer starts as a copy of it.
    alignas(16) float point[4], out[4];
    point[0] = field<float>(picture, 0x40);
    point[1] = field<float>(picture, 0x44);
    point[2] = field<float>(picture, 0x48);
    point[3] = point[0];
    std::memcpy(out, point, sizeof out);
    nglProjectPointCopy(out, point);
    method<void XB_THISCALL(uint8_t*, float, float)>(picture, 0x8c)(picture, out[0], out[1]);
}

PLATFORM_ASM_ENTRY(UIPicture_GetBoundsMin_entry,
                   "subl $32, %esp\n"
                   "movups %xmm6, (%esp)\n"
                   "movups %xmm7, 16(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIPicture_GetBoundsMin) "addl $8, %esp\n"
                   "movups (%esp), %xmm6\n"
                   "movups 16(%esp), %xmm7\n"
                   "addl $32, %esp\n"
                   "ret $4")

float* XB_CDECL UIPicture_GetBoundsMin(uint8_t* picture, float* out) {
    uint8_t* const* const quads = field<uint8_t**>(picture, 0x20);
    float m[3];
    UIQuad_GetVertexMin(m, 0, quads[0]);
    float x = m[0], y = m[1];
    const uint32_t count = quad_count(picture);
    for (uint32_t i = 1; i < count; ++i) {
        UIQuad_GetVertexMin(m, 0, quads[i]);
        if (x > m[0]) x = m[0];
        if (y > m[1]) y = m[1];
    }
    out[0] = x;
    out[1] = y;
    field<uint32_t>(reinterpret_cast<uint8_t*>(out), 8) = title_global<uint32_t>(0x00922eb8);
    return out;
}

PLATFORM_ASM_ENTRY(UIPicture_GetBoundsMax_entry,
                   "subl $32, %esp\n"
                   "movups %xmm6, (%esp)\n"
                   "movups %xmm7, 16(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIPicture_GetBoundsMax) "addl $8, %esp\n"
                   "movups (%esp), %xmm6\n"
                   "movups 16(%esp), %xmm7\n"
                   "addl $32, %esp\n"
                   "ret $4")

float* XB_CDECL UIPicture_GetBoundsMax(uint8_t* picture, float* out) {
    uint8_t* const* const quads = field<uint8_t**>(picture, 0x20);
    float m[3];
    UIQuad_GetVertexMax(m, 0, quads[0]);
    float x = m[0], y = m[1];
    const uint32_t count = quad_count(picture);
    for (uint32_t i = 1; i < count; ++i) {
        UIQuad_GetVertexMax(m, 0, quads[i]);
        if (m[0] > x) x = m[0];
        if (m[1] > y) y = m[1];
    }
    out[0] = x;
    out[1] = y;
    field<uint32_t>(reinterpret_cast<uint8_t*>(out), 8) = title_global<uint32_t>(0x00922eb8);
    return out;
}

void XB_THISCALL UIPicture_ResetTransform(uint8_t* picture) {
    for (uint32_t i = 0; i < quad_count(picture); ++i) UIQuad_ResetPositions(picture_quad(picture, i));
    set_centre_from_bounds(picture);
    field<float>(picture, 0x34) = g(0x0083eb20);
    field<float>(picture, 0x38) = g(0x0083eb20);
    field<float>(picture, 0x30) = 0.0f;
}

void XB_THISCALL UIPicture_SetQuadCorners(uint8_t* picture, const float* xs, const float* ys) {
    const float max_y = max4(ys[0], ys[1], ys[2], ys[3]);
    const float max_x = max4(xs[0], xs[1], xs[2], xs[3]);
    const float min_y = min4(ys[0], ys[1], ys[2], ys[3]);
    const float min_x = min4(xs[0], xs[1], xs[2], xs[3]);
    const uint32_t count = quad_count(picture);
    if (count == 1) {
        UIQuad_SetVertexPositions(xs, picture_quad(picture, 0), ys);
    } else {
        // Every vertex mapped from the current bounds onto the box of (xs, ys).
        float max_buffer[3], min_buffer[3];
        UIPicture_GetBoundsMax(picture, max_buffer);
        UIPicture_GetBoundsMin(picture, min_buffer);
        const float height = max_buffer[1] - min_buffer[1];
        const float to_width = max_x - min_x;
        const float to_height = max_y - min_y;
        const float width = max_buffer[0] - min_buffer[0];
        if (count != 0) {
            const float inv_height = g(0x0083eb20) / height;
            const float inv_width = g(0x0083eb20) / width;
            for (uint32_t i = 0; i < quad_count(picture); ++i) {
                NglQuad* const q = screen_quad(picture_quad(picture, i));
                float nx[4], ny[4];
                for (int32_t v = 0; v < 4; ++v) {
                    nx[v] = (q->verts[v].x - min_buffer[0]) * inv_width * to_width + min_x;
                    ny[v] = (q->verts[v].y - min_buffer[1]) * inv_height * to_height + min_y;
                }
                for (int32_t v = 0; v < 4; ++v) nglSetQuadVertexPos(q, v, nx[v], ny[v]);
            }
        }
    }
    const float half = g(0x0083eb24);
    field<float>(picture, 0x14) = (max_x - min_x) * half + min_x;
    field<float>(picture, 0x18) = (max_y - min_y) * half + min_y;
    field<float>(picture, 0x1c) = 0.0f;
}

void XB_THISCALL UIPicture_SetVertexScaledAlpha(uint8_t* picture, float alpha) {
    for (uint32_t i = 0; i < quad_count(picture); ++i) {
        for (int32_t v = 0; v < 4; ++v) {
            uint8_t* const quad = picture_quad(picture, i);
            const uint32_t old = screen_quad(quad)->verts[v].color;
            // FLD alpha, FMUL scale, FMUL 255 on the x87, then _ftol.
            const long double product = static_cast<long double>(alpha) * field<float>(quad, 0x30 + uint32_t(v) * 4) *
                                        static_cast<long double>(g(0x0083eb8c));
            const uint32_t a = uint32_t(int32_t(int64_t(product))) & 0xff;
            nglSetQuadVertexColor(screen_quad(quad), v, a << 24 | (old & 0x00ffffff));
        }
    }
}

void XB_THISCALL UIPicture_AddQuad(uint8_t* picture, const float* points, const uint32_t* colors, uint32_t a3,
                                   uint32_t a4, const char* texture) {
    method<void XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(picture, 0x1c)(picture, a4, a3);
    const float one = g(0x0083eb20);
    float uvs[12] = {0.0f, 0.0f, 0.0f, one, 0.0f, 0.0f, 0.0f, one, 0.0f, one, one, 0.0f};
    uint8_t* quad = new_quad();
    // 0x003173e0 (quad in EAX, points in ECX; UVs, colours, depth on the stack): fills the quad.
    title_function<void XB_REGS3 XB_STDCALL(uint8_t*, uint32_t, const float*, const float*, const uint32_t*,
                                            uint32_t)>(0x003173e0)(quad, 0, points, uvs, colors,
                                                                   field<uint32_t>(picture, 8));
    ui_ptrvector_push(picture + 0x20, &quad);
    if (texture && *texture) {
        // 0x00322b30 (name in ESI): the texture, the language-prefixed one first.
        const uint32_t found = title_call_regs(0x00322b30, 0, address_of(texture), 0);
        method<void XB_THISCALL(uint8_t*, uint32_t)>(picture, 0x68)(picture, found);
    }
    set_centre_from_bounds(picture);
}

void XB_THISCALL UIPicture_SetTransform(uint8_t* picture, const float* matrix, float alpha) {
    for (uint32_t i = 0; i < quad_count(picture); ++i)
        UIQuad_ApplyMatrix(matrix, field<float>(picture, 8), picture_quad(picture, i), picture[0x10] & 1);
    if (picture[0x10] & 1)
        method<void XB_THISCALL(uint8_t*, float)>(picture, 0x7c)(picture, field<float>(picture, 4) * alpha);
    else
        method<void XB_THISCALL(uint8_t*, float)>(picture, 0x7c)(picture, alpha);
    picture[0x10] |= 1;
}

namespace {

// Calls a title function that takes EDI and three cdecl stack words.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ui_call_edi3(uint32_t function, uint32_t edi, uint32_t a, uint32_t b,
                                                         uint32_t c);
PLATFORM_ASM_ENTRY(ui_call_edi3,
                   "pushl %edi\n"
                   "movl 12(%esp), %edi\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "call *20(%esp)\n"
                   "addl $12, %esp\n"
                   "popl %edi\n"
                   "ret")

// Little-endian reads from the mesh data at *offset, advancing it.
int32_t read_s16(const uint8_t* data, uint32_t* offset) {
    const uint32_t o = *offset;
    *offset = o + 2;
    return int16_t(uint16_t(data[o] | data[o + 1] << 8));
}
uint32_t read_u32(const uint8_t* data, uint32_t* offset) {
    const uint32_t o = *offset;
    *offset = o + 4;
    return uint32_t(data[o]) | uint32_t(data[o + 1]) << 8 | uint32_t(data[o + 2]) << 16 | uint32_t(data[o + 3]) << 24;
}

void* heap_alloc16(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(0x00042c30)(size); }

}  // namespace

void XB_THISCALL UIPicture_LoadMesh(uint8_t* picture, const uint8_t* materials, const uint8_t* data, uint32_t* offset,
                                    const float* matrix) {
    const uint8_t* const material = materials + read_s16(data, offset) * 16;
    const int32_t count = read_s16(data, offset);
    const uint32_t c = title_global<uint32_t>(0x00922eb8);
    uint32_t* const positions = static_cast<uint32_t*>(heap_alloc16(uint32_t(count) * 12));
    if (positions)
        for (int32_t i = 0; i < count * 3; ++i) positions[i] = c;
    uint32_t* const colors = static_cast<uint32_t*>(heap_alloc16(uint32_t(count) * 4));
    if (colors)
        for (int32_t i = 0; i < count; ++i) colors[i] = 0;
    uint32_t* const uvs = static_cast<uint32_t*>(heap_alloc16(uint32_t(count) * 12));
    if (uvs)
        for (int32_t i = 0; i < count * 3; ++i) uvs[i] = c;
    // Per vertex: position (3 floats), colour (bytes R, G, B, A -> ARGB, modulated by the material colour +0x08),
    // u, v.
    for (int32_t i = 0; i < count; ++i) {
        for (int32_t k = 0; k < 3; ++k) positions[i * 3 + k] = read_u32(data, offset);
        const uint32_t o = *offset;
        const uint32_t color = uint32_t(data[o + 3]) << 24 | uint32_t(data[o]) << 16 | uint32_t(data[o + 1]) << 8 |
                               uint32_t(data[o + 2]);
        *offset = o + 4;
        colors[i] = color;
        uint32_t modulated;
        colors[i] = *reinterpret_cast<uint32_t*>(
            Color_Modulate(0, 0, reinterpret_cast<uint8_t*>(&modulated), color, field<uint32_t>(const_cast<uint8_t*>(material), 8)));
        uvs[i * 3] = read_u32(data, offset);
        uvs[i * 3 + 1] = read_u32(data, offset);
    }
    *offset += 2;
    const int32_t index_count = read_s16(data, offset);
    uint16_t* const indices = static_cast<uint16_t*>(heap_alloc16(uint32_t(index_count) * 2));
    for (int32_t i = 0; i < index_count; ++i) indices[i] = uint16_t(read_s16(data, offset));
    const int32_t triangles = index_count / 3;
    float m[16];
    std::memcpy(m, matrix, sizeof m);
    uint32_t quad_colors[4] = {0, 0, 0, 0};
    uint32_t base[12], screen[12], quad_uvs[12];
    for (int32_t i = 0; i < 12; ++i) {
        base[i] = c;
        screen[i] = c;
        quad_uvs[i] = c;
    }
    float depth = 0.0f;
    // The quad's four position indices; Panel_TrianglePairToQuad may leave words of it as they were.
    int32_t corners[4] = {0, 0, 0, 0};
    const uint16_t* triangle = indices;
    for (int32_t i = 0; i < triangles; ++i, triangle += 3) {
        // Two triangles sharing an edge (0x003101d0: EDI = this triangle, the next one's index) make one quad.
        if (i + 1 < triangles && ui_call_edi3(0x003101d0, uint32_t(i), address_of(indices), uint32_t(i + 1),
                                              address_of(corners)) == 2) {
            ++i;
            triangle += 3;
        } else {
            corners[0] = int16_t(triangle[0]);
            corners[1] = int16_t(triangle[1]);
            corners[2] = int16_t(triangle[2]);
            corners[3] = int16_t(triangle[2]);
        }
        for (int32_t k = 0; k < 4; ++k) {
            const uint32_t* const p = positions + corners[k] * 3;
            base[k * 3] = p[0];
            base[k * 3 + 1] = p[1];
            float x, y, z;
            std::memcpy(&x, &p[0], 4);
            std::memcpy(&y, &p[1], 4);
            std::memcpy(&z, &p[2], 4);
            // All four lanes of the packed transform (MXCSR flags): row 3 + x * row 0 + y * row 1 + z * row 2.
            float r[4];
            for (int32_t lane = 0; lane < 4; ++lane) {
                const float px = m[lane] * x;
                const float py = m[4 + lane] * y;
                const float pz = m[8 + lane] * z;
                float s = m[12 + lane];
                s += px;
                s += py;
                s += pz;
                r[lane] = s;
            }
            platform_keep(r);
            std::memcpy(&screen[k * 3], &r[0], 8);
            depth = r[2];
            const uint32_t* const uv = uvs + corners[k] * 3;
            quad_uvs[k * 3] = uv[0];
            quad_uvs[k * 3 + 1] = uv[1];
            quad_uvs[k * 3 + 2] = uv[2];
            quad_colors[k] = colors[corners[k]];
        }
        uint8_t* quad = new_quad();
        uint32_t depth_bits;
        std::memcpy(&depth_bits, &depth, 4);
        title_function<void XB_REGS3 XB_STDCALL(uint8_t*, uint32_t, const uint32_t*, const uint32_t*, const uint32_t*,
                                                uint32_t)>(0x003173e0)(quad, 0, screen, quad_uvs, quad_colors,
                                                                       depth_bits);
        for (int32_t k = 0; k < 12; ++k) field<uint32_t>(quad, uint32_t(k) * 4) = base[k];
        ui_ptrvector_push(picture + 0x20, &quad);
    }
    method<void XB_THISCALL(uint8_t*, float)>(picture, 0x24)(picture, depth);
    // Material +0x0c: use its texture +0x00; +0x0d: quad flag 2; +0x0e / +0x0f clear: flags 0x40 / 0x80.
    uint32_t flags = material[0xd] ? 2 : 0;
    if (!material[0xe]) flags |= 0x40;
    if (!material[0xf]) flags |= 0x80;
    uint32_t* const texture = *reinterpret_cast<uint32_t* const*>(material);
    if (texture && material[0xc]) {
        method<void XB_THISCALL(uint8_t*, uint32_t*)>(picture, 0x68)(picture, texture);
        nglAddRef(*reinterpret_cast<uint32_t* const*>(material));
    }
    if (flags) method<void XB_THISCALL(uint8_t*, uint32_t)>(picture, 0x6c)(picture, flags);
    z_free(positions);
    z_free(uvs);
    z_free(colors);
    z_free(indices);
    set_centre_from_bounds(picture);
}

}  // namespace game
