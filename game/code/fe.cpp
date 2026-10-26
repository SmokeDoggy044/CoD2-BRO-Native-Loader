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

}  // namespace game
