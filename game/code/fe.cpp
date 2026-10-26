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

}  // namespace game
