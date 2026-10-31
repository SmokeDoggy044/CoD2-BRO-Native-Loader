#include "fe.h"

#include <cstring>
#include <type_traits>

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

// The FE_Bonus*FreeImage helpers (menu in ESI): nglResetDeviceBindings, the texture released (and its pointer
// cleared, except the main menu's), the image file closed and its buffer freed, the buffer pointer cleared.
void free_image(uint8_t* menu, uint32_t file_offset, uint32_t buffer_offset, uint32_t texture_offset,
                bool clear_texture) {
    nglResetDeviceBindings();
    if (void* const texture = field<void*>(menu, texture_offset)) nglReleaseTexture(texture);
    const int32_t file = field<int32_t>(menu, file_offset);
    if (clear_texture) field<uint32_t>(menu, texture_offset) = 0;
    if (file != -1) {
        title_function<void XB_CDECL(int32_t)>(0x000cac90)(file);  // File_Close
        if (void* const buffer = field<void*>(menu, buffer_offset)) Mem_AlignedFree(buffer);
    }
    field<uint32_t>(menu, buffer_offset) = 0;
}

}  // namespace

int32_t XB_REGS2 XB_CDECL FE_BonusCEItemGroup(uint32_t, uint8_t* menu) {
    const int16_t row = field<int16_t>(menu, 0x22);
    const int32_t item = field<int32_t>(menu, 0x130 + uint32_t(int32_t(row)) * 4);
    if (row == 3 || row == 4 || row == 5) return item;
    auto in = [item](int32_t lo, int32_t hi) { return item >= lo && item <= hi; };
    if (row == 0) {
        if (in(1, 3)) return 1;
        if (in(4, 11)) return 2;
        if (in(12, 18)) return 3;
        if (in(19, 26)) return 4;
        if (in(27, 33)) return 5;
        return 6;
    }
    if (row == 1) {
        if (in(1, 2)) return 1;
        if (in(3, 4)) return 2;
        if (in(5, 7)) return 3;
        if (in(8, 9)) return 4;
        if (in(10, 11)) return 5;
        if (in(12, 13)) return 6;
        return item != 14 ? 8 : 7;
    }
    if (in(1, 2)) return 1;
    if (item == 3) return 2;
    if (item == 4) return 3;
    if (in(5, 8)) return 4;
    if (in(9, 11)) return 5;
    if (in(12, 13)) return 6;
    return 7;
}

int32_t XB_REGS2 XB_CDECL FE_BonusCEItemIndex(uint32_t, uint8_t* menu) {
    const int16_t row = field<int16_t>(menu, 0x22);
    const int32_t item = field<int32_t>(menu, 0x130 + uint32_t(int32_t(row)) * 4);
    if (row == 3 || row == 4 || row == 5) return 0;
    auto in = [item](int32_t lo, int32_t hi) { return item >= lo && item <= hi; };
    if (row == 0) {
        if (in(1, 3)) return item;
        if (in(4, 11)) return item - 3;
        if (in(12, 18)) return item - 11;
        if (in(19, 26)) return item - 18;
        if (in(27, 33)) return item - 26;
        return item - 33;
    }
    if (row == 1) {
        if (in(1, 2)) return item;
        if (in(3, 4)) return item - 2;
        if (in(5, 7)) return item - 4;
        if (in(8, 9)) return item - 7;
        if (in(10, 11)) return item - 9;
        if (in(12, 13)) return item - 11;
        if (item == 14) return 1;
        return item - 14;
    }
    if (in(1, 2)) return item;
    if (item == 3 || item == 4) return 1;
    if (in(5, 8)) return item - 4;
    if (in(9, 11)) return item - 8;
    if (in(12, 13)) return item - 11;
    return item - 13;
}

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

#define FE_FREE_IMAGE(name, file, buffer, texture, clear)                                                     \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl %esi\n" PLATFORM_ASM_CALL(name) "addl $4, %esp\n"                \
                                     "ret")                                                                    \
    void XB_CDECL name(uint8_t* menu) { free_image(menu, file, buffer, texture, clear); }

FE_FREE_IMAGE(FE_BonusCEFreeImage, 0x44, 0x48, 0x4c, true)
FE_FREE_IMAGE(FE_BonusConceptFreeImage, 0xc4, 0xc8, 0xcc, true)
FE_FREE_IMAGE(FE_BonusVehicleFreeImage, 0x8c, 0x90, 0x94, true)
FE_FREE_IMAGE(FE_BonusWeaponFreeImage, 0xb0, 0xb4, 0xb8, true)
FE_FREE_IMAGE(FE_BonusMainFreeImage, 0x88, 0x8c, 0x90, false)

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

// The two "mark every slot empty" helpers (menu in EAX) push ESI and call the slot-text update with the menu in ESI;
// the update runs at the original's ESP (its set-text calls store stack addresses). The entry hands over the entry
// ESP, the menu, ESI and EDI.
#define MARK_SLOTS_EMPTY_ENTRY(name)                                                                                \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %eax, %ecx\n"                                                           \
                                     "movl %esp, %eax\n" GAME_GAP_ESP("0x10000") "pushl %eax\n"                     \
                                     "pushl %ebp\n"                                                                 \
                                     "pushl %edi\n"                                                                 \
                                     "pushl %esi\n"                                                                 \
                                     "pushl %ecx\n"                                                                 \
                                     "pushl %eax\n" PLATFORM_ASM_CALL(name) "addl $16, %esp\n"                      \
                                     "popl %ebp\n"                                                                  \
                                     "popl %esp\n"                                                                  \
                                     "ret")

namespace {

void mark_slots_empty(uint32_t entry, uint8_t* menu, uint32_t esi, uint32_t edi, uint32_t flags, uint32_t update,
                      uint32_t ret) {
    for (uint32_t i = 0; i < 4; ++i) menu[flags + i * 12] = 1;
    *reinterpret_cast<uint32_t*>(uintptr_t{entry - 4}) = esi;  // push esi
    const TitleRegs regs{address_of(menu) + flags + 48, 0, 0, 0, address_of(menu), edi};
    title_call_at_regs(entry - 4, update, &regs, 0, nullptr);
    *reinterpret_cast<uint32_t*>(uintptr_t{entry - 8}) = ret;
}

}  // namespace

MARK_SLOTS_EMPTY_ENTRY(AutosaveMenu_MarkSlotsEmpty)

void XB_CDECL AutosaveMenu_MarkSlotsEmpty(uint32_t entry, uint8_t* menu, uint32_t esi, uint32_t edi) {
    mark_slots_empty(entry, menu, esi, edi, 0x5c, 0x0030ccb0, 0x0030cf8e);  // AutosaveMenu_UpdateSlotTexts
}

MARK_SLOTS_EMPTY_ENTRY(ProfileMenu_MarkSlotsEmpty)

void XB_CDECL ProfileMenu_MarkSlotsEmpty(uint32_t entry, uint8_t* menu, uint32_t esi, uint32_t edi) {
    mark_slots_empty(entry, menu, esi, edi, 0x6c, 0x00312690, 0x0031286e);  // FE_SaveSlotsUpdateText
}

#undef MARK_SLOTS_EMPTY_ENTRY

// The autosave menu's twin of FE_SaveSlotsUpdateText (the same entry): slots {name +0x54, empty +0x5c}, no help bar.
PLATFORM_ASM_ENTRY(AutosaveMenu_UpdateSlotTexts_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %eax\n"
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AutosaveMenu_UpdateSlotTexts) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL AutosaveMenu_UpdateSlotTexts(uint32_t entry, uint8_t* menu, uint32_t edi) {
    if (menu[0x35]) {  // card corrupt; push edi
        *reinterpret_cast<uint32_t*>(uintptr_t{entry - 4}) = edi;
        for (uint32_t i = 0; i < 4; ++i)
            set_text_at(entry - 4, 0x0030ccd0, menu, menu_item(menu, i), 0x30, 0x0082ada0, i * 4);  // FEMENU_OP_CORRUPT
        return;
    }
    // return addresses {new, name}: one call site per slot, but two for the last
    static constexpr uint32_t kReturns[4][2] = {
        {0x0030ccf6, 0x0030ccf6}, {0x0030cd19, 0x0030cd19}, {0x0030cd3c, 0x0030cd3c}, {0x0030cd56, 0x0030cd66}};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* const item = menu_item(menu, i);
        if (menu[0x5c + i * 12])
            set_text_at(entry, kReturns[i][0], menu, item, 0x30, 0x0082ad90, edi);  // FEMENU_OP_NEW
        else
            set_text_at(entry, kReturns[i][1], menu, item, 0x30, field<uint32_t>(menu, 0x54 + i * 12), edi);
    }
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

uint32_t XB_REGS1 XB_STDCALL BonusConceptsMenu_ChangeItem(int32_t mode, uint8_t* menu) {
    auto item = [menu](int32_t i) -> int32_t& { return field<int32_t>(menu, 0x8c + uint32_t(i) * 4); };
    auto best = [](int32_t i) -> int32_t& { return title_global<int32_t>(0x008dced8 + uint32_t(i) * 4); };
    if (mode == 1) {  // next: up to the chapter's unlocked count; the stored bests raised
        for (int32_t i = 0; i < 13; ++i) {
            if (i == field<int16_t>(menu, 0x22) && int32_t(unlocked_level_word(i + 1)) > item(i)) item(i) = item(i) + 1;
            if (best(i) < item(i)) best(i) = item(i);
        }
        return 0;
    }
    if (mode == 2) {  // previous: down to 1
        for (int32_t i = 0; i < 13; ++i)
            if (i == field<int16_t>(menu, 0x22) && item(i) > 1) item(i) = item(i) - 1;
        return 0;
    }
    int32_t& current = item(field<int16_t>(menu, 0x22));
    if (current == 0) current = 1;
    for (int32_t i = 0; i < 13; ++i)
        if (best(i) < item(i)) best(i) = item(i);
    return 0;
}

uint32_t XB_STDCALL BonusConceptsMenu_UpdateNewMarks(uint8_t* menu) {
    for (uint32_t i = 0; i < 13; ++i) {
        menu[0x7c + i] = 0;
        if (int32_t(unlocked_level_word(int32_t(i) + 1)) > title_global<int32_t>(0x008dced8 + i * 4)) menu[0x7c + i] = 1;
        uint8_t* const entry = menu_item(menu, i);
        if (method<uint8_t XB_THISCALL(uint8_t*)>(entry, 0x24)(entry) == 1) menu[0x7c + i] = 0;  // locked
    }
    return 0;
}

uint32_t* XB_REGS1 XB_CDECL Profile_CopyBonusBests(uint32_t* out) {
    // 17 profile words (0x008dce90..0x008dced0; words 12-14 read from 0x008dcec8 down) and a zero, built on the stack
    // and copied out.
    static constexpr uint32_t kWords[17] = {0x008dce90, 0x008dce94, 0x008dce98, 0x008dce9c, 0x008dcea0, 0x008dcea4,
                                            0x008dcea8, 0x008dceac, 0x008dceb0, 0x008dceb4, 0x008dceb8, 0x008dcebc,
                                            0x008dcec8, 0x008dcec4, 0x008dcec0, 0x008dcecc, 0x008dced0};
    uint32_t bests[18];
    for (uint32_t i = 0; i < 17; ++i) bests[i] = title_global<uint32_t>(kWords[i]);
    bests[17] = 0;
    platform_rep_movs(out, bests, sizeof bests);
    return out;
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
    BonusConceptsMenu_UpdateNewMarks(menu);
    field<uint32_t>(menu, 0x40) = 0x3f800000;  // 1.0
    menu[0x44] = 0;
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

uint8_t XB_CDECL MissionSuccessFailMenu_ReloadCheckpoint() {
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

long double XB_THISCALL UIElement_GetDepth(uint8_t* element) { return field<float>(element, 8); }

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

void XB_REGS3 XB_STDCALL UIQuad_SetVertexUVs(const float* us, uint32_t, const float* vs, uint8_t* quad) {
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

PLATFORM_ASM_ENTRY(UIQuad_GetMaxUV_entry,
                   "subl $16, %esp\n"
                   "movups %xmm5, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIQuad_GetMaxUV) "addl $8, %esp\n"
                   "movups (%esp), %xmm5\n"
                   "addl $16, %esp\n"
                   "ret")

void XB_CDECL UIQuad_GetMaxUV(float* out, const uint8_t* source) {
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

// UI text (UIText_* / UITextBase_*) -------------------------------------------------------------------------------

namespace {

void script_string_release(uint8_t* string) {
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(string);  // ScriptString_Release
}

// The font of a text kind (ui_fonts 0x0095f8d4; none for kinds 7 and 8).
const uint8_t* text_font(int32_t kind) {
    return kind == 7 || kind == 8 ? nullptr : title_global<const uint8_t*>(0x0095f8d4 + uint32_t(kind) * 4);
}

// vtable +0xdc: sets (on) or clears the flag bits `mask` of +0x5c.
void text_set_flags(uint8_t* text, uint32_t mask, uint32_t on) {
    method<void XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(text, 0xdc)(text, mask, on);
}

// vtable +0xd8: whether any of the flag bits `mask` of +0x5c are set.
bool text_has_flags(uint8_t* text, uint32_t mask) {
    return method<uint8_t XB_THISCALL(uint8_t*, uint32_t)>(text, 0xd8)(text, mask) != 0;
}

void text_layout(uint8_t* text) { UIText_LayoutLines(text); }

}  // namespace

void XB_THISCALL UITextBase_SetUniformScale(uint8_t* text, float scale) {
    field<float>(text, 0x38) = scale;
    field<float>(text, 0x3c) = scale;
    field<uint32_t>(text, 0x40) = 0;
}

void XB_THISCALL UITextBase_SetScale(uint8_t* text, float sx, float sy) {
    field<float>(text, 0x38) = sx;
    field<float>(text, 0x3c) = sy;
    field<uint32_t>(text, 0x40) = 0;
}

void XB_THISCALL UITextBase_MoveBy(uint8_t* text, float dx, float dy) {
    using Get = long double XB_THISCALL(uint8_t*);
    const float y = static_cast<float>(method<Get>(text, 0xcc)(text) + dy);  // get y
    const float x = static_cast<float>(method<Get>(text, 0xc8)(text) + dx);  // get x
    method<void XB_THISCALL(uint8_t*, float, float)>(text, 0x7c)(text, x, y);  // set position
}

void XB_THISCALL UITextBase_SetPosition(uint8_t* text, float x, float y) {
    field<float>(text, 0x2c) = x;
    field<float>(text, 0x30) = y;
    field<uint32_t>(text, 0x34) = 0;
}

ScriptString* XB_THISCALL UITextBase_GetName(uint8_t* text, ScriptString* out) {
    ScriptString_Copy(out, reinterpret_cast<const ScriptString*>(text + 0x54));
    return out;
}

ScriptString* XB_THISCALL UITextBase_GetText(uint8_t* text, ScriptString* out) {
    ScriptString_Copy(out, reinterpret_cast<const ScriptString*>(text + 0x1c));
    return out;
}

namespace {

void text_base_dtor(uint8_t* text) {
    field<uint32_t>(text, 0) = 0x008298b8;  // UITextBase vtable
    if (field<void*>(text, 0x14)) {
        z_free(field<void*>(text, 0x14));
        field<uint32_t>(text, 0x14) = 0;
    }
    script_string_release(text + 0x54);
    script_string_release(text + 0x1c);
    field<uint32_t>(text, 0) = 0x00829244;  // UIElement vtable
}

}  // namespace

uint8_t* XB_THISCALL UITextBase_DeletingDtor(uint8_t* text, uint32_t flags) {
    text_base_dtor(text);
    if (flags & 1) z_free(text);
    return text;
}

void XB_THISCALL UITextBase_SetHorzAlign(uint8_t* text, int32_t align) {
    text_set_flags(text, 0x30, 0);
    if (align == 0x10 || align == 0x20) text_set_flags(text, uint32_t(align), 1);
}

void XB_THISCALL UITextBase_SetVertAlign(uint8_t* text, int32_t align) {
    text_set_flags(text, 0xc0, 0);
    if (align == 0x40 || align == 0x80) text_set_flags(text, uint32_t(align), 1);
}

void XB_THISCALL UIText_SetHorzAlign(uint8_t* text, int32_t align) {
    UITextBase_SetHorzAlign(text, align);
    text_layout(text);
}

void XB_THISCALL UIText_SetVertAlign(uint8_t* text, int32_t align) {
    UITextBase_SetVertAlign(text, align);
    text_layout(text);
}

void XB_THISCALL UIText_SetUniformScale(uint8_t* text, float scale) {
    UITextBase_SetUniformScale(text, scale);
    text_layout(text);
}

void XB_THISCALL UIText_SetScale(uint8_t* text, float sx, float sy) {
    UITextBase_SetScale(text, sx, sy);
    text_layout(text);
}

void XB_THISCALL UIText_SetPosition(uint8_t* text, float x, float y) {
    UITextBase_SetPosition(text, x, y);
    text_layout(text);
}

PLATFORM_ASM_ENTRY(UIText_Dtor_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIText_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIText_Dtor(uint8_t* text) {
    field<uint32_t>(text, 0) = 0x0082a450;  // UIText vtable
    if (field<uint8_t*>(text, 0x80)) {
        // vector deleting destructor of the lines (count at -4; 3 = destroy each, free the block)
        title_function<void XB_REGS1 XB_STDCALL(uint8_t*, uint32_t)>(0x00012480)(field<uint8_t*>(text, 0x80), 3);
        field<uint32_t>(text, 0x80) = 0;
    }
    text_base_dtor(text);
}

uint8_t* XB_THISCALL UIText_DeletingDtor(uint8_t* text, uint32_t flags) {
    UIText_Dtor(text);
    if (flags & 1) z_free(text);
    return text;
}

namespace {

// A text line (0x20 bytes): its buffer +0x18 freed, its script string +0x14 released.
void text_line_dtor(uint8_t* line) {
    if (void* const buffer = field<void*>(line, 0x18)) {
        z_free(buffer);
        field<uint32_t>(line, 0x18) = 0;
    }
    script_string_release(line + 0x14);
}

}  // namespace

uint8_t* XB_REGS1 XB_STDCALL UITextLine_VectorDeletingDtor(uint8_t* lines, uint32_t flags) {
    if (!(flags & 2)) {
        text_line_dtor(lines);
        if (flags & 1) z_free(lines);
        return lines;
    }
    uint8_t* const block = lines - 4;  // the count, then the lines
    for (int32_t i = field<int32_t>(block, 0) - 1; i >= 0; --i) text_line_dtor(lines + uint32_t(i) * 0x20);
    if (flags & 1) z_free(block);
    return block;
}

long double XB_REGS2 XB_CDECL UI_GetTextWidth(int32_t kind, const char* text, float scale) {
    if (!text || kind == 8) return title_global<float>(0x0083eafc);  // 0
    const uint8_t* const font = kind == 7 ? nullptr : title_global<const uint8_t*>(0x0095f8d4 + uint32_t(kind) * 4);
    int32_t width = 0, height = 0;
    nglGetTextExtent(font, text, &width, &height, scale, scale);
    long double result = width;  // FILD, + 2^32 when negative (unsigned)
    if (width < 0) result += title_global<float>(0x0083eb2c);
    return result;
}

void XB_THISCALL UIText_Draw(uint8_t* text) {
    if (!method<uint8_t XB_THISCALL(uint8_t*)>(text, 0x30)(text)) return;  // shown
    using DrawLines = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
    const uint8_t scrolled = text[0x94];
    const uint32_t first = scrolled ? field<uint32_t>(text, 0x88) : 0;
    if (scrolled)
        method<DrawLines>(text, 0x100)(text, first, field<uint32_t>(text, 0x8c));
    else
        method<DrawLines>(text, 0x100)(text, first, field<uint32_t>(text, 0x78));
}

void XB_THISCALL UIText_SetLineSpacing(uint8_t* text, int32_t spacing) {
    if (spacing == -1) {
        int32_t width = int32_t(address_of(text));  // the push ecx slot
        int32_t height = spacing;                   // the argument slot the original reuses
        nglGetTextExtent(text_font(field<int32_t>(text, 0x18)), text_at(0x0082d764), &width, &height,
                         field<float>(text, 0x38), field<float>(text, 0x3c));  // "!"
        field<float>(text, 0x68) = platform_x87_unsigned_to_float(uint32_t(height));
    } else {
        field<float>(text, 0x68) = static_cast<float>(spacing);
    }
    field<uint32_t>(text, 0x6c) = field<uint32_t>(text, 0x68);
}

long double XB_THISCALL UIText_GetLineHeight(uint8_t* text) {
    const ScriptStringRep* rep = field<ScriptStringRep*>(text, 0x1c);
    const char* string = rep ? rep->text : nullptr;
    int32_t width = 0;
    int32_t height = 0;
    nglGetTextExtent(text_font(field<int32_t>(text, 0x18)), string, &width, &height, field<float>(text, 0x38),
                     field<float>(text, 0x3c));
    return platform_x87_unsigned_to_float(uint32_t(height));
}

long double XB_THISCALL UIText_GetTextHeight(uint8_t* text) {
    const float line = static_cast<float>(UIText_GetLineHeight(text));
    const int32_t count = method<int32_t XB_THISCALL(uint8_t*)>(text, 0xc4)(text) - 1;  // line count
    return static_cast<long double>(count) * field<float>(text, 0x6c) + line;
}

// UI text lines (0x20 bytes): +0x00 kind (8), +0x04 (x, y, z), +0x14 the text (ScriptString), +0x18 an owned block.
uint8_t* XB_THISCALL UITextLine_Construct(uint8_t* line) {
    const uint32_t word = title_global<uint32_t>(0x00922eb8);
    field<uint32_t>(line, 4) = word;
    field<uint32_t>(line, 8) = word;
    field<uint32_t>(line, 0xc) = word;
    ScriptString_FromRep(reinterpret_cast<ScriptString*>(line + 0x14), nullptr);
    title_function<void XB_THISCALL(uint8_t*, const char*)>(0x0011dd20)(line + 0x14, text_at(0x0082dfeb));  // ""
    field<uint32_t>(line, 4) = 0;
    field<uint32_t>(line, 8) = 0;
    field<uint32_t>(line, 0xc) = 0;
    field<uint32_t>(line, 0) = 8;
    field<uint32_t>(line, 0x18) = 0;
    return line;
}

void XB_THISCALL UITextLine_Dtor(uint8_t* line) {
    if (field<void*>(line, 0x18)) {
        z_free(field<void*>(line, 0x18));
        field<uint32_t>(line, 0x18) = 0;
    }
    script_string_release(line + 0x14);
}

void XB_THISCALL UITextBase_ApplyAlignOffset(uint8_t* text, float* x, float* y, uint32_t string) {
    using Get = long double XB_THISCALL(uint8_t*);
    const float height = static_cast<float>(method<Get>(text, 0xd4)(text));
    const float width = static_cast<float>(method<Get>(text, 0xd0)(text));
    if (text_has_flags(text, 0x20))
        *x = *x - width;
    else if (!text_has_flags(text, 0x10))
        *x = *x - width * g(0x0083eb24);  // 0.5
    if (text_has_flags(text, 0x80))
        *y = *y - height;
    else if (!text_has_flags(text, 0x40))
        *y = *y - height * g(0x0083eb24);
    script_string_release(reinterpret_cast<uint8_t*>(&string));  // the by-value ScriptString argument
}

void XB_THISCALL UITextBase_CopyFrom(uint8_t* text, uint8_t* source) {
    using Assign = void XB_THISCALL(uint8_t*, uint8_t*);
    field<uint32_t>(text, 4) = field<uint32_t>(source, 4);
    field<uint32_t>(text, 8) = field<uint32_t>(source, 8);
    field<uint32_t>(text, 0xc) = field<uint32_t>(source, 0xc);
    text[0x10] = source[0x10];
    if (field<void*>(text, 0x14)) {
        z_free(field<void*>(text, 0x14));
        field<uint32_t>(text, 0x14) = 0;
    }
    if (field<uint32_t*>(source, 0x14)) {
        uint32_t* const block = static_cast<uint32_t*>(z_malloc(0x10));
        if (block) {
            const uint32_t* from = field<uint32_t*>(source, 0x14);
            for (int i = 0; i < 4; ++i) block[i] = from[i];
        }
        field<uint32_t*>(text, 0x14) = block;
    } else {
        field<uint32_t>(text, 0x14) = 0;
    }
    field<uint32_t>(text, 0x18) = field<uint32_t>(source, 0x18);
    title_function<Assign>(0x0011d600)(text + 0x1c, source + 0x1c);  // ScriptString_Assign
    for (uint32_t offset = 0x20; offset < 0x50; offset += 4) field<uint32_t>(text, offset) = field<uint32_t>(source, offset);
    field<uint32_t>(text, 0x50) = field<uint32_t>(source, 0x50);
    title_function<Assign>(0x0011d600)(text + 0x54, source + 0x54);
    text[0x5c] = source[0x5c];
    field<uint32_t>(text, 0x58) = field<uint32_t>(source, 0x58);
}

void XB_THISCALL UIText_ScrollBy(uint8_t* text, float delta) {
    if (!text[0x94]) return;
    const float old = field<float>(text, 0x90);
    const uint8_t wrap = text[0x95];
    const float pos = delta + old;
    field<float>(text, 0x90) = pos;
    float total = static_cast<float>(field<int32_t>(text, 0x78)) * field<float>(text, 0x6c);
    const int32_t margin = field<int32_t>(text, 0x84);
    if (wrap) {
        total = total + static_cast<float>(margin * 2);
    } else {
        const float m = static_cast<float>(margin);
        total = total - m;
        if (m > total) total = m;
    }
    const float m = static_cast<float>(margin);
    const float top = wrap ? m : 0.0f;
    if (pos > top) field<float>(text, 0x90) = top;
    const float bottom = 0.0f - total;
    if (bottom > field<float>(text, 0x90)) field<float>(text, 0x90) = bottom;
    method<void XB_THISCALL(uint8_t*, float, float)>(text, 0x70)(text, 0.0f, field<float>(text, 0x90) - old);  // move by
    const float one = g(0x0083eb20);
    const float per_line = one / field<float>(text, 0x6c);
    const int32_t first = static_cast<int32_t>(0.0f - per_line * field<float>(text, 0x90));
    const int32_t last = static_cast<int32_t>(static_cast<float>(field<int32_t>(text, 0x84)) * per_line +
                                              static_cast<float>(first) + one);
    field<int32_t>(text, 0x88) = first;
    field<int32_t>(text, 0x8c) = last;
}

void XB_THISCALL UIText_AllocLines(uint8_t* text, int32_t count) {
    field<int32_t>(text, 0x7c) = count;
    field<int32_t>(text, 0x78) = 0;
    uint8_t* const lines = field<uint8_t*>(text, 0x80);
    if (lines) {
        // the lines' vector delete, inlined
        for (int32_t i = reinterpret_cast<int32_t*>(lines)[-1] - 1; i >= 0; --i) UITextLine_Dtor(lines + i * 0x20);
        z_free(lines - 4);
    }
    const int32_t n = field<int32_t>(text, 0x7c);
    uint8_t* const block = static_cast<uint8_t*>(heap_alloc16(uint32_t(n) * 0x20 + 4));
    if (!block) {
        field<uint32_t>(text, 0x80) = 0;
        return;
    }
    field<int32_t>(block, 0) = n;
    for (int32_t i = 0; i < n; ++i) UITextLine_Construct(block + 4 + i * 0x20);
    field<uint8_t*>(text, 0x80) = block + 4;
}

namespace {

// The "UI_Highlight" sound (S_PlaySoundAlias at the origin, volumes -1); the result with its low word 0xffff.
uint32_t play_highlight_sound() {
    alignas(16) float origin[4];
    alignas(16) float axis[4];
    std::memcpy(origin, &title_global<float>(0x0082e110), sizeof origin);  // (0, 0, 0, 0)
    std::memcpy(axis, &title_global<float>(0x0082e290), sizeof axis);      // (0, 0, 0, 1)
    const uint32_t table = title_global<uint32_t>(0x0095f4c4);
    const int32_t alias = S_FindSoundByHash(text_at(0x0082cbf0));  // "UI_Highlight"
    if (alias == -1) return 0xffffffff;
    uint32_t handle = 0;
    using PlayAlias = uint32_t* XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t,
                                                     uint32_t, const float*, const float*, uint32_t, uint32_t,
                                                     uint32_t, uint32_t);
    const uint32_t result = address_of(title_function<PlayAlias>(0x002c30a0)(
        uint32_t(alias), address_of(&handle), table, &handle, 0, 1, 0, axis, origin, 0xbf800000, 0xbf800000,
        0xbf800000, 0xbf800000));  // S_PlaySoundAlias
    return result | 0xffff;
}

}  // namespace

uint32_t XB_THISCALL UITextTable_SelectNext(uint8_t* table) {
    uint32_t selected = field<uint32_t>(table, 0x50);
    if (selected < field<uint32_t>(table, 0x20) - 1) {
        const uint32_t rows = field<uint32_t>(table, 0x48);
        ++selected;
        const uint32_t end = field<uint32_t>(table, 0x4c) + rows;
        field<uint32_t>(table, 0x50) = selected;
        if (selected >= end) field<uint32_t>(table, 0x4c) = selected - rows + 1;
    }
    return play_highlight_sound();
}

uint32_t XB_THISCALL UITextTable_SelectPrev(uint8_t* table) {
    uint32_t selected = field<uint32_t>(table, 0x50);
    if (selected > 0) {
        --selected;
        field<uint32_t>(table, 0x50) = selected;
        if (selected < field<uint32_t>(table, 0x4c)) field<uint32_t>(table, 0x4c) = selected;
    }
    return play_highlight_sound();
}

PLATFORM_ASM_ENTRY(UIText_LayoutLines_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(UIText_LayoutLines) "addl $4, %esp\n"
                   "ret")

void XB_CDECL UIText_LayoutLines(uint8_t* text) {
    int32_t width = 0;
    int32_t height = 0;
    nglGetTextExtent(text_font(field<int32_t>(text, 0x18)), text_at(0x0082d6e4), &width, &height,
                     field<float>(text, 0x38), field<float>(text, 0x3c));  // " "
    const float space = platform_x87_unsigned_to_float(uint32_t(height));
    const float half = g(0x0083eb24);
    for (int32_t i = 0; i < field<int32_t>(text, 0x78); ++i) {
        const float line_width = field<float>(field<uint8_t*>(text, 0x80) + i * 0x20, 0x10);
        float x;
        if (text_has_flags(text, 0x10))
            x = field<float>(text, 0x2c);
        else if (text_has_flags(text, 0x20))
            x = field<float>(text, 0x2c) - line_width;
        else
            x = field<float>(text, 0x2c) - line_width * half;
        float y;
        if (text_has_flags(text, 0x40)) {
            y = static_cast<float>(i) * field<float>(text, 0x6c) + field<float>(text, 0x30);
        } else if (text_has_flags(text, 0x80)) {
            y = static_cast<float>(i - field<int32_t>(text, 0x78) + 1) * field<float>(text, 0x6c) +
                field<float>(text, 0x30) - space;
        } else {
            const float half_count = static_cast<float>(field<int32_t>(text, 0x78)) * half;
            y = (static_cast<float>(i) + half - half_count) * field<float>(text, 0x6c) + field<float>(text, 0x30) -
                space * half;
        }
        uint8_t* const line = field<uint8_t*>(text, 0x80) + i * 0x20;
        field<float>(line, 4) = x;
        field<float>(line, 8) = y;
        field<uint32_t>(line, 0xc) = 0;
    }
    // +0x70: the baseline offset, from glyph 'A' of the text's font and glyph '!' of font 2 (glyph records 0x1c
    // bytes from the font's +0x28, first character +0x38; +0x0c height, +0x14 offset).
    const uint8_t* font = text_font(field<int32_t>(text, 0x18));
    const uint8_t* const a = field<uint8_t*>(const_cast<uint8_t*>(font), 0x28) +
                             (0x41 - field<int32_t>(const_cast<uint8_t*>(font), 0x38)) * 0x1c;
    uint8_t* const font2 = title_global<uint8_t*>(0x0095f8dc);
    const uint8_t* const bang = field<uint8_t*>(font2, 0x28) + (0x21 - field<int32_t>(font2, 0x38)) * 0x1c;
    const int32_t* const ga = reinterpret_cast<const int32_t*>(a);
    const int32_t* const gb = reinterpret_cast<const int32_t*>(bang);
    const float above = (static_cast<float>(ga[3]) * half + static_cast<float>(ga[5])) * field<float>(text, 0x3c);
    field<float>(text, 0x70) =
        (static_cast<float>(gb[3]) * half + static_cast<float>(gb[5])) * field<float>(text, 0x64) - above +
        g(0x0083eb20);
}

namespace {

float negated(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits ^= 0x80000000u;  // XORPS with the sign mask 0x0082e0b0
    std::memcpy(&value, &bits, 4);
    return value;
}

}  // namespace

void XB_THISCALL UITextBase_SetTransform(uint8_t* text, const float* matrix, float alpha) {
    const bool applied = text[0x10] & 1;
    if (applied) {
        field<float>(text, 0x2c) = matrix[12] + field<float>(text, 0x2c);
        field<float>(text, 0x30) = matrix[13] + field<float>(text, 0x30);
    } else {
        field<float>(text, 0x2c) = matrix[12] + field<float>(text, 0x20);
        field<float>(text, 0x30) = field<float>(text, 0x24) + matrix[13];
    }
    using SetAlpha = void XB_THISCALL(uint8_t*, float);
    if (applied) {
        field<float>(text, 0x38) = matrix[0] * field<float>(text, 0x38);
        field<float>(text, 0x3c) = matrix[5] * field<float>(text, 0x3c);
        method<SetAlpha>(text, 0x88)(text, field<float>(text, 4) * alpha);
    } else {
        // MULPS of rows 0 and 1 by the base scales: every lane's flags
        float row0[4];
        float row1[4];
        const float sx = field<float>(text, 0x44);
        const float sy = field<float>(text, 0x48);
        for (int k = 0; k < 4; ++k) row0[k] = matrix[k] * sx;
        platform_keep(row0);
        field<float>(text, 0x38) = row0[0];
        for (int k = 0; k < 4; ++k) row1[k] = matrix[4 + k] * sy;
        platform_keep(row1);
        field<float>(text, 0x3c) = row1[1];
        method<SetAlpha>(text, 0x88)(text, alpha);
    }
}

void XB_THISCALL UIText_SetTransform(uint8_t* text, const float* matrix, float alpha) {
    const bool applied = text[0x10] & 1;
    if (applied) {
        const float y = field<float>(text, 0x30) + matrix[13];
        field<float>(text, 0x2c) = matrix[12] + field<float>(text, 0x2c);
        field<float>(text, 0x30) = y;
    } else {
        const float y = field<float>(text, 0x24) + matrix[13];
        field<float>(text, 0x2c) = matrix[12] + field<float>(text, 0x20);
        field<float>(text, 0x30) = y;
    }
    field<uint32_t>(text, 0x34) = 0;
    if (applied) {
        field<float>(text, 0x38) = matrix[0] * field<float>(text, 0x38);
        field<float>(text, 0x3c) = negated(matrix[5]) * field<float>(text, 0x3c);
    } else {
        field<float>(text, 0x38) = matrix[0];
        field<float>(text, 0x3c) = negated(matrix[5]);
        field<uint32_t>(text, 0x40) = 0;
    }
    field<float>(text, 0x6c) = field<float>(text, 0x68) * field<float>(text, 0x3c);
    using SetAlpha = void XB_THISCALL(uint8_t*, float);
    if (applied)
        method<SetAlpha>(text, 0x88)(text, field<float>(text, 4) * alpha);
    else
        method<SetAlpha>(text, 0x88)(text, alpha);
    UIText_LayoutLines(text);
}

// Title helpers taking EAX and ESI plus one stack word (ret 4).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ui_call_eax_esi(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t arg);
PLATFORM_ASM_ENTRY(ui_call_eax_esi,
                   "pushl %esi\n"
                   "movl 8(%esp), %ecx\n"
                   "movl 12(%esp), %eax\n"
                   "movl 16(%esp), %esi\n"
                   "pushl 20(%esp)\n"
                   "call *%ecx\n"
                   "popl %esi\n"
                   "ret")

namespace {

using Str = ScriptString;

Str* str_substring(Str* self, Str* out, int32_t start, int32_t length) {
    return title_function<Str* XB_THISCALL(Str*, Str*, int32_t, int32_t)>(0x0011e290)(self, out, start, length);
}
void str_assign(Str* self, const Str* other) {
    title_function<void XB_THISCALL(Str*, const Str*)>(0x0011d600)(self, other);
}
void str_assign_text(Str* self, const char* text) {
    title_function<void XB_THISCALL(Str*, const char*)>(0x0011dd20)(self, text);
}
void str_release(Str* self) { script_string_release(reinterpret_cast<uint8_t*>(self)); }
const char* str_text(const Str* self) { return self->rep ? self->rep->text : nullptr; }
uint32_t str_length(const Str* self) { return self->rep ? self->rep->length : 0; }

// The width of `text` in `font` at (scale, scale) (nglGetTextExtent, unsigned to float on the x87).
float text_extent_width(const uint8_t* font, const char* text, float scale) {
    int32_t width = 0;
    int32_t height = 0;
    nglGetTextExtent(font, text, &width, &height, scale, scale);
    return platform_x87_unsigned_to_float(uint32_t(width));
}

const uint8_t* button_font() { return title_global<const uint8_t*>(0x0095f8dc); }  // ui_fonts[2]

// 0x00315620 (text in EDI, out in ESI): the length of the button token ("~select", ...) at `text` and its glyph
// string in *out; 0 (out not written) without a '~'.
int32_t translate_button_token(const char* text, const char** out) {
    return int32_t(title_call_regs(0x00315620, 0, address_of(out), address_of(text)));
}

// 0x0030f950 (kind in EAX, text in EDX): the width of the text in the kind's font at (scale, scale), x87; the
// constant 0x0083eafc for no text or kind 8.
long double text_width(int32_t kind, const char* text, float scale) {
    return title_function<long double XB_REGS2 XB_CDECL(int32_t, const char*, float)>(0x0030f950)(kind, text, scale);
}

// 0x00315cd0 (block in EAX, out in ESI; colour on the stack): the colour faded towards the block's (16 bytes at
// +0x14 of a UI text). Returns the colour word.
uint32_t text_fade_colour(uint8_t* text, uint32_t colour) {
    uint32_t out = 0;
    const uint32_t result =
        ui_call_eax_esi(0x00315cd0, field<uint32_t>(text, 0x14), address_of(&out), colour);
    return *reinterpret_cast<uint32_t*>(uintptr_t{result});
}

// The text colour +0x50 (faded with flags bit 3) with its alpha times the fade alpha +0x04 (x87, _ftol).
uint32_t text_draw_colour(uint8_t* text) {
    uint32_t colour = field<uint32_t>(text, 0x50);
    if (text[0x5c] & 8) colour = text_fade_colour(text, colour);
    const int32_t alpha = platform_x87_mul_add_ftol(int32_t(colour >> 24), field<float>(text, 4), 0, false);
    return (colour & 0x00ffffffu) | (uint32_t(uint8_t(alpha)) << 24);
}

long double element_depth(uint8_t* element) {
    return method<long double XB_THISCALL(uint8_t*)>(element, 0x28)(element);  // UIElement_GetDepth
}

}  // namespace

uint32_t XB_STDCALL UITextLine_LayoutTokens(uint8_t* line, float scale, float button_scale) {
    Str* const text = reinterpret_cast<Str*>(line + 0x14);
    if (field<int32_t>(line, 0x1c) / 2 <= 0) return uint32_t(field<int32_t>(line, 0x1c) / 2);
    float width = 0.0f;
    int32_t pos = 0;
    int32_t marks = 1;
    const char* glyph = nullptr;  // kept across tokens like the original's slot
    for (int32_t i = 0; i < field<int32_t>(line, 0x1c) / 2; ++i) {
        const int32_t start = ScriptString_FindChar(text, uint32_t(pos), '~');
        Str before;
        str_substring(text, &before, pos, start - pos);
        float w = 0.0f;
        const int32_t kind = field<int32_t>(line, 0);
        if (str_text(&before) && kind != 8) w = text_extent_width(text_font(kind), str_text(&before), scale);
        width = w + width;
        str_release(&before);
        field<int16_t*>(line, 0x18)[i * 4] = int16_t(start);
        field<int16_t*>(line, 0x18)[i * 4 + 1] = int16_t(static_cast<int32_t>(width));
        const int32_t consumed = translate_button_token(str_text(text) + start, &glyph);
        // the text with the token replaced by its glyphs
        Str rest, prefix, joined, all;
        str_substring(text, &rest, consumed + start, int32_t(str_length(text)) - consumed - start);
        str_substring(text, &prefix, 0, start);
        title_function<Str* XB_CDECL(Str*, const Str*, const char*)>(0x0011eec0)(&joined, &prefix, glyph);  // ConcatCString
        title_function<Str* XB_CDECL(Str*, const Str*, const Str*)>(0x0011e340)(&all, &joined, &rest);  // Concat
        str_assign(text, &all);
        str_release(&all);
        str_release(&joined);
        str_release(&prefix);
        str_release(&rest);
        int32_t glyph_length = 0;  // the original's inline strlen
        while (glyph[glyph_length]) ++glyph_length;
        pos = glyph_length + start;
        if (!text->rep || uint32_t(pos) >= text->rep->length) {
            field<int32_t>(line, 0x1c) = marks;
        } else {
            float w2 = 0.0f;
            if (glyph) w2 = text_extent_width(button_font(), glyph, button_scale);
            width = w2 + width;
            field<int16_t*>(line, 0x18)[i * 4 + 2] = int16_t(pos);
            field<int16_t*>(line, 0x18)[i * 4 + 3] = int16_t(static_cast<int32_t>(width));
        }
        if (i == field<int32_t>(line, 0x1c) / 2 - 1 || !text->rep || uint32_t(pos) >= text->rep->length) {
            Str tail;
            str_substring(text, &tail, pos, int32_t(str_length(text)) - pos);
            float w3 = 0.0f;
            const int32_t kind2 = field<int32_t>(line, 0);
            if (str_text(&tail) && kind2 != 8) w3 = text_extent_width(text_font(kind2), str_text(&tail), scale);
            field<float>(line, 0x10) = w3 + width;
            str_release(&tail);
        }
        marks += 2;
    }
    return uint32_t(field<int32_t>(line, 0x1c) / 2);
}

PLATFORM_ASM_ENTRY(UITextLine_Init_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UITextLine_Init) "addl $20, %esp\n"
                   "ret $12")

uint32_t XB_CDECL UITextLine_Init(uint8_t* line, const char* string, int32_t kind, float scale, float button_scale) {
    Str* const text = reinterpret_cast<Str*>(line + 0x14);
    str_assign_text(text, string);
    field<uint32_t>(line, 4) = 0;
    field<int32_t>(line, 0) = kind;
    field<uint32_t>(line, 8) = 0;
    field<uint32_t>(line, 0xc) = 0;
    field<uint32_t>(line, 0x10) = 0;
    field<int32_t>(line, 0x1c) = 0;
    // two token marks per '~'
    int32_t pos = 0;
    while (text->rep && text->rep->text[0] && pos >= 0) {
        pos = ScriptString_FindChar(text, uint32_t(pos), '~');
        if (pos >= 0) {
            field<int32_t>(line, 0x1c) += 2;
            ++pos;
        }
    }
    if (field<void*>(line, 0x18)) {
        z_free(field<void*>(line, 0x18));
        field<uint32_t>(line, 0x18) = 0;
    }
    const int32_t marks = field<int32_t>(line, 0x1c);
    if (marks > 0) {
        void* const block = heap_alloc16(uint32_t(marks) * 4);
        if (block)
            title_function<void XB_STDCALL(void*, uint32_t, int32_t, uint32_t)>(0x00011240)(
                block, 4, marks, 0x00011750);  // eh_vector_constructor_iterator
        field<void*>(line, 0x18) = block;
        return UITextLine_LayoutTokens(line, scale, button_scale);
    }
    field<float>(line, 0x10) = static_cast<float>(text_width(field<int32_t>(line, 0), str_text(text), scale));
    return 0;
}

PLATFORM_ASM_ENTRY(UIText_MeasureWord_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UIText_MeasureWord) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $16, %esp\n"
                   "ret")

float XB_CDECL UIText_MeasureWord(const char* word, int32_t kind, float scale, float button_scale) {
    if (!word[0]) return 0.0f;
    float width = 0.0f;
    Str text;
    title_function<void XB_THISCALL(Str*, const char*)>(0x0011eb50)(&text, word);  // ScriptString_FromCString
    int32_t pos = 0;
    const char* glyph = nullptr;
    for (;;) {
        const int32_t start = ScriptString_FindChar(&text, uint32_t(pos), '~');
        if (start == -1) break;
        Str before;
        str_substring(&text, &before, pos, start - pos);
        float w = 0.0f;
        if (str_text(&before) && kind != 8) w = text_extent_width(text_font(kind), str_text(&before), scale);
        width = w + width;
        str_release(&before);
        pos = translate_button_token(str_text(&text) + start, &glyph) + start;
        float w2 = 0.0f;
        if (glyph) w2 = text_extent_width(button_font(), glyph, button_scale);
        width = w2 + width;
        if (pos < 0) break;
    }
    if (pos != 0) {
        Str tail;
        str_substring(&text, &tail, pos, int32_t(str_length(&text)) - pos);
        width = static_cast<float>(text_width(kind, str_text(&tail), scale) + width);
        str_release(&tail);
    } else {
        width = static_cast<float>(text_width(kind, str_text(&text), scale) + width);
    }
    str_release(&text);
    return width;
}

PLATFORM_ASM_ENTRY(UITextLine_Draw_entry,
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(UITextLine_Draw) "addl $32, %esp\n"
                   "ret $28")

void XB_CDECL UITextLine_Draw(uint8_t* line, float depth, uint32_t colour, uint32_t button_colour, float sx, float sy,
                              float button_scale, float button_drop) {
    Str* const text = reinterpret_cast<Str*>(line + 0x14);
    const int16_t* tokens;
    if (field<int32_t>(line, 0x1c) <= 0) {
        nglListAddText(text_font(field<int32_t>(line, 0)), str_text(text), field<float>(line, 4), field<float>(line, 8),
                       depth, colour, sx, sy);
        return;
    }
    Str part;
    str_substring(text, &part, 0, field<int16_t*>(line, 0x18)[0]);
    nglListAddText(text_font(field<int32_t>(line, 0)), str_text(&part), field<float>(line, 4), field<float>(line, 8),
                   depth, colour, sx, sy);
    str_release(&part);
    for (int32_t i = 0; i < (field<int32_t>(line, 0x1c) + 1) / 2; ++i) {
        const int32_t mark = i * 2;
        // the token's glyphs in the button font
        tokens = field<int16_t*>(line, 0x18);
        const int32_t start = tokens[i * 4];
        const int32_t end = mark == field<int32_t>(line, 0x1c) - 1 ? int32_t(str_length(text)) : tokens[i * 4 + 2];
        Str glyphs;
        str_substring(text, &glyphs, start, end - start);
        const float y = field<float>(line, 8) - button_drop;
        const float x = static_cast<float>(field<int16_t*>(line, 0x18)[i * 4 + 1]) + field<float>(line, 4);
        nglListAddText(button_font(), str_text(&glyphs), x, y, depth, button_colour, button_scale, button_scale);
        str_release(&glyphs);
        // the text after it, up to the next token
        const int32_t count = field<int32_t>(line, 0x1c);
        if (mark < count - 1) {
            tokens = field<int16_t*>(line, 0x18);
            const int32_t from = tokens[i * 4 + 2];
            const int32_t to = mark == count - 2 ? int32_t(str_length(text)) : tokens[i * 4 + 4];
            Str after;
            str_substring(text, &after, from, to - from);
            const float ax = static_cast<float>(field<int16_t*>(line, 0x18)[i * 4 + 3]) + field<float>(line, 4);
            nglListAddText(text_font(field<int32_t>(line, 0)), str_text(&after), ax, field<float>(line, 8), depth,
                           colour, sx, sy);
            str_release(&after);
        }
    }
}

void XB_THISCALL UIText_DrawLines(uint8_t* text, int32_t first, int32_t end) {
    if (!method<uint8_t XB_THISCALL(uint8_t*)>(text, 0x30)(text)) return;
    if (first < 0) first = 0;
    if (end > field<int32_t>(text, 0x78)) end = field<int32_t>(text, 0x78);
    const uint32_t colour = text_draw_colour(text);
    const uint32_t button_colour = field<uint32_t>(text, 0x60);
    for (int32_t i = first; i < end; ++i) {
        uint8_t* line = field<uint8_t*>(text, 0x80) + i * 0x20;
        if (field<int32_t>(line, 0) == 8) continue;
        if (ScriptString_Equals(reinterpret_cast<const Str*>(line + 0x14), text_at(0x0082dfeb))) continue;  // ""
        const float drop = field<float>(text, 0x70);
        const float button_scale = field<float>(text, 0x64);
        const float sy = field<float>(text, 0x3c);
        const float sx = field<float>(text, 0x38);
        const float depth = static_cast<float>(element_depth(text));
        UITextLine_Draw(field<uint8_t*>(text, 0x80) + i * 0x20, depth, colour, button_colour, sx, sy, button_scale,
                        drop);
    }
}

void XB_THISCALL UITextBase_Draw(uint8_t* text) {
    if (!method<uint8_t XB_THISCALL(uint8_t*)>(text, 0x30)(text)) return;
    Str* const string = reinterpret_cast<Str*>(text + 0x1c);
    if (ScriptString_Equals(string, text_at(0x0082dfeb))) return;  // ""
    const uint32_t colour = text_draw_colour(text);
    float x = field<float>(text, 0x2c);
    float y = field<float>(text, 0x30);
    Str copy;
    ScriptString_Copy(&copy, string);  // handed over by value, released by the callee
    method<void XB_THISCALL(uint8_t*, float*, float*, uint32_t)>(text, 0xfc)(text, &x, &y,
                                                                          address_of(copy.rep));
    const char* const chars = str_text(string);
    const uint8_t* const font = text_font(field<int32_t>(text, 0x18));
    const float sy = field<float>(text, 0x3c);
    const float sx = field<float>(text, 0x38);
    const float depth = static_cast<float>(element_depth(text));
    nglListAddText(font, chars, x, y, depth, colour, sx, sy);
}

uint32_t XB_STDCALL UIText_WrapLines(uint8_t* text, const char* string, int32_t width, uint8_t store) {
    using StrFn = void XB_THISCALL(Str*, const char*);
    if (field<int32_t>(text, 0x18) == 8) return 0;
    int32_t count = 0;
    int32_t pos = 0;
    float line_width = 0.0f;
    float measured = 0.0f;
    Str line;
    title_function<StrFn>(0x0011eb50)(&line, text_at(0x0082dfeb));  // ScriptString_FromCString("")
    auto trim = [&]() {
        title_function<StrFn>(0x0011ed20)(&line, text_at(0x0082e09c));  // TrimLeftChars(" \n\t\r")
        title_function<StrFn>(0x0011edf0)(&line, text_at(0x0082e09c));  // TrimRightChars
    };
    auto store_line = [&]() {
        UITextLine_Init(field<uint8_t*>(text, 0x80) + count * 0x20, str_text(&line), field<int32_t>(text, 0x18),
                        field<float>(text, 0x38), field<float>(text, 0x64));
    };
    if (string[0]) {
        const float max_width = static_cast<float>(width);
        char word[0x100];
        do {
            const char* const from = string + pos;
            const int32_t n = title_function<int32_t XB_CDECL(const char*, const char*)>(0x001ef010)(
                from, text_at(0x0082d280));  // _strcspn(" -\n\r")
            pos += n;
            const char separator = string[pos];
            title_function<char* XB_CDECL(char*, const char*, int32_t)>(0x001eed80)(word, from, n);  // _strncpy
            word[n] = 0;
            bool newline = false;
            if (separator) {
                ++pos;
                if (separator == '-' || separator == ' ') {  // kept on the word (inline strcat)
                    char* end = word;
                    while (*end) ++end;
                    end[0] = separator;
                    end[1] = 0;
                } else if (separator == '\n') {
                    newline = true;
                }
            }
            if (newline) {
                // the line so far ends here; the word before the newline starts the next one
                line_width = 0.0f;
                trim();
                if (store && count < field<int32_t>(text, 0x7c)) store_line();
                str_assign_text(&line, text_at(0x0082dfeb));
                ++count;
            }
            if (title_function<int32_t XB_CDECL(const char*, const char*, int32_t)>(0x001eeaf0)(
                    word, text_at(0x0082d27c), 2) == 0) {  // _strncmp(word, "\f[", 2): a kind change
                field<int32_t>(text, 0x18) =
                    title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(word + 2);  // _atol
                for (int32_t k = 0;; ++k) {
                    word[k] = word[k + 4];
                    if (!word[k]) break;
                }
            }
            const float w = UIText_MeasureWord(word, field<int32_t>(text, 0x18), field<float>(text, 0x38),
                                               field<float>(text, 0x64));
            measured = w + line_width;
            if (measured > max_width) {
                line_width = w;
                trim();
                if (store && count < field<int32_t>(text, 0x7c)) store_line();
                str_assign_text(&line, word);
                ++count;
            } else {
                title_function<StrFn>(0x0011ebc0)(&line, word);  // ScriptString_AppendCString
                line_width = measured;
            }
            if (!string[pos]) {
                trim();
                if (line.rep && line.rep->length) {
                    if (store && count < field<int32_t>(text, 0x7c)) store_line();
                    ++count;
                }
            }
        } while (string[pos]);
    }
    str_release(&line);
    return uint32_t(count);
}

void XB_THISCALL UIText_SetWrappedText(uint8_t* text, uint32_t string, int32_t width, float a3) {
    const bool unset = a3 == g(0x0083eb34);  // -1.0 (UCOMISS; nothing else uses it)
    platform_keep(unset);
    field<int32_t>(text, 0x74) = width;
    Str* const source = reinterpret_cast<Str*>(&string);
    field<uint32_t>(text, 0x78) = UIText_WrapLines(text, str_text(source), width, 0);
    if (field<uint8_t*>(text, 0x80))
        title_function<void XB_REGS1 XB_STDCALL(uint8_t*, uint32_t)>(0x00012480)(field<uint8_t*>(text, 0x80), 3);
    const int32_t n = field<int32_t>(text, 0x78);
    uint8_t* lines = nullptr;
    if (uint8_t* const block = static_cast<uint8_t*>(heap_alloc16(uint32_t(n) * 0x20 + 4))) {
        field<int32_t>(block, 0) = n;
        lines = block + 4;
        for (int32_t i = 0; i < n; ++i) UITextLine_Construct(lines + i * 0x20);
    }
    field<uint32_t>(text, 0x7c) = field<uint32_t>(text, 0x78);
    field<uint8_t*>(text, 0x80) = lines;
    UIText_WrapLines(text, str_text(source), width, 1);
    str_assign_text(reinterpret_cast<Str*>(text + 0x1c),
                    str_text(reinterpret_cast<Str*>(field<uint8_t*>(text, 0x80) + 0x14)));
    UIText_LayoutLines(text);
    str_release(source);  // the by-value argument
}

void XB_THISCALL UITextTable_Draw(uint8_t* table) {
    using Get = long double XB_THISCALL(uint8_t*);
    using SetFloat = void XB_THISCALL(uint8_t*, float);
    using SetText = void XB_THISCALL(uint8_t*, const char*);
    using Call = void XB_THISCALL(uint8_t*);
    uint8_t* item = field<uint8_t*>(table, 0x10);
    const float x0 = static_cast<float>(method<Get>(item, 0xc8)(item));
    item = field<uint8_t*>(table, 0x10);
    const float y0 = static_cast<float>(method<Get>(item, 0xcc)(item));
    auto text_item = [&]() { return field<uint8_t*>(table, 0x10); };
    auto set_x_by = [&](float dx) {
        uint8_t* t = text_item();
        const float x = static_cast<float>(method<Get>(t, 0xc8)(t) + dx);
        t = text_item();
        method<SetFloat>(t, 0x80)(t, x);
    };
    auto set_y_by = [&](float dy) {
        uint8_t* t = text_item();
        const float y = static_cast<float>(method<Get>(t, 0xcc)(t) + dy);
        t = text_item();
        method<SetFloat>(t, 0x84)(t, y);
    };
    auto draw_text = [&](const Str* s) {
        uint8_t* t = text_item();
        method<SetText>(t, 0x78)(t, str_text(s));
    };
    if (table[0x34]) {  // the header row: four texts +0x38.., column offsets +0x24..
        table[0x15] = 0;
        method<Call>(table, 0x94)(table);
        for (uint32_t k = 0; k < 4; ++k) {
            draw_text(reinterpret_cast<const Str*>(table + 0x38 + k * 4));
            method<Call>(text_item(), 0x3c)(text_item());
            set_x_by(field<float>(table, 0x24 + k * 4));
        }
        set_y_by(field<float>(table, 0x58));
    }
    for (uint32_t i = 0; field<uint32_t>(table, 0x4c) + i < field<uint32_t>(table, 0x20) && i < field<uint32_t>(table, 0x48);
         ++i) {
        const uint32_t row = field<uint32_t>(table, 0x4c) + i;
        if (row == field<uint32_t>(table, 0x50)) {
            table[0x15] = 1;
            method<Call>(table, 0x94)(table);
        }
        // row record: +0x04 the first text, +0x0c column count, +0x10.. the column texts
        draw_text(reinterpret_cast<const Str*>(field<uint8_t**>(table, 0x18)[row] + 4));
        method<SetFloat>(text_item(), 0x80)(text_item(), x0);
        method<Call>(text_item(), 0x3c)(text_item());
        for (uint32_t c = 0; c < field<uint32_t>(field<uint8_t**>(table, 0x18)[row], 0xc); ++c) {
            draw_text(reinterpret_cast<const Str*>(field<uint8_t**>(table, 0x18)[row] + 0x10 + c * 4));
            set_x_by(field<float>(table, 0x24 + c * 4));
            method<Call>(text_item(), 0x3c)(text_item());
        }
        set_y_by(field<float>(table, 0x54));
        if (row == field<uint32_t>(table, 0x50)) {
            table[0x15] = 0;
            method<Call>(table, 0x94)(table);
        }
    }
    method<SetFloat>(text_item(), 0x80)(text_item(), x0);
    method<SetFloat>(text_item(), 0x84)(text_item(), y0);
}

// --- Front-end helpers run at the original's stack position ---
//
// The set-text methods leave a stack buffer's address in a heap word, and the image loaders hand stack buffers to
// title callees: these functions run every title call where the original makes it. The entry (FE_FRAME_ENTRY) moves
// the C frame below a gap and hands the C version the entry registers and the entry ESP; each call runs at the
// original's ESP (`depth` bytes below the entry, before the call's pushes) and leaves the original's return address
// and saved registers in the dead stack.
#define FE_FRAME_ENTRY(name, ret)                                                                                 \
    PLATFORM_ASM_ENTRY(name##_entry, GAME_GAP_ESP("0x10000") "pushl %ebp\n pushl %edi\n pushl %esi\n"            \
                                     "pushl %edx\n pushl %ecx\n pushl %ebx\n pushl %eax\n movl %esp, %eax\n"      \
                                     "pushl 28(%eax)\n pushl %eax\n" PLATFORM_ASM_CALL(name)                      \
                                     "addl $36, %esp\n popl %esp\n" ret)

namespace {

uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

template <typename T>
uint32_t word_of(T value) {
    if constexpr (std::is_pointer_v<T>)
        return address_of(value);
    else
        return static_cast<uint32_t>(value);
}

struct Frame {
    uint32_t entry;  // the original's entry ESP
    TitleRegs regs;  // the registers title calls get

    Frame(const FrameIn* in, uint32_t entry_esp)
        : entry(entry_esp), regs{in->eax, in->ebx, in->ecx, in->edx, in->esi, in->edi} {}

    uint32_t& word(uint32_t depth) const { return stack_word(entry - depth); }
    template <typename T = uint8_t>
    T* local(uint32_t depth) const {
        return reinterpret_cast<T*>(uintptr_t{entry - depth});
    }
    // A title call at ESP = entry - depth (before its pushes; args[0] pushed last), returning to `ret`.
    template <typename... A>
    uint32_t call(uint32_t depth, uint32_t ret, uint32_t function, A... a) const {
        const uint32_t args[] = {word_of(a)..., 0};
        const uint32_t top = entry - depth;
        const uint32_t result = title_call_at_regs(top, function, &regs, sizeof...(A), args);
        stack_word(top - 4 * sizeof...(A) - 4) = ret;
        return result;
    }
    // A thiscall method (vtable byte offset `slot`) of `object`.
    template <typename... A>
    uint32_t method(uint32_t depth, uint32_t ret, uint8_t* object, uint32_t slot, A... a) {
        const uint32_t saved = regs.ecx;
        regs.ecx = address_of(object);
        const uint32_t result = call(depth, ret, field<uint32_t>(field<uint8_t*>(object, 0), slot), a...);
        regs.ecx = saved;
        return result;
    }
};

constexpr uint32_t kSprintf = 0x001ee901;
constexpr uint32_t kFileOpenRead = 0x000c9d40, kFileGetSize = 0x000c9d10, kFileReadAt = 0x000caff0;
constexpr uint32_t kFileClose = 0x000cac90;
constexpr uint32_t kMemAlignedAlloc = 0x00093950, kMemAlignedFree = 0x000939b0;
constexpr uint32_t kHashStrInit = 0x00011060, kNglLoadTexture = 0x000635a0, kNglSetQuadTexture = 0x0005de20;
constexpr uint32_t kNglResetDeviceBindings = 0x00064660, kNglReleaseTexture = 0x00063630;

// The image fields of a menu: file handle, buffer, texture.
struct ImageFields {
    uint32_t file, buffer, texture;
};

// The inline free of the character / collector's-edition images (frame: one push below sub esp,0x40).
void free_image_inline(Frame& f, uint8_t* menu, const ImageFields& image, const uint32_t (&rets)[4]) {
    if (!field<uint32_t>(menu, image.buffer)) return;
    f.call(0x44, rets[0], kNglResetDeviceBindings);
    if (const uint32_t texture = field<uint32_t>(menu, image.texture)) f.call(0x44, rets[1], kNglReleaseTexture, texture);
    const int32_t file = field<int32_t>(menu, image.file);
    if (file != -1) {
        f.call(0x44, rets[2], kFileClose, file);
        if (const uint32_t buffer = field<uint32_t>(menu, image.buffer)) f.call(0x44, rets[3], kMemAlignedFree, buffer);
    }
    field<uint32_t>(menu, image.buffer) = 0;
}

// The return addresses of the loading tail's calls: open, size, alloc, read, hash, load, set quad.
struct LoadRets {
    uint32_t open, size, alloc, read, hash, load, quad;
};

// Open `name` (a buffer in the frame), read it whole into an aligned buffer and make the texture `hash_name` on the
// quad. Depths below the entry: the open call; the ESP before the `pushes` words pushed after the open (1: EDI, 2:
// EBX and EDI); the hash and texture calls; the quad call. Returns false when the file does not open.
struct LoadDepths {
    uint32_t open, base, pushes, hash, quad;
};

bool load_image_file(Frame& f, uint8_t* menu, const ImageFields& image, uint32_t name, const LoadDepths& d,
                     uint32_t hash_name, uint32_t quad, const LoadRets& rets) {
    const int32_t file = int32_t(f.call(d.open, rets.open, kFileOpenRead, title_global<uint32_t>(0x0095f6f4), name));
    field<int32_t>(menu, image.file) = file;
    if (file == -1) return false;
    uint32_t depth = d.base;
    if (d.pushes == 2) {
        depth += 4;
        f.word(depth) = f.regs.ebx;
    }
    if (d.pushes >= 1) {
        depth += 4;
        f.word(depth) = f.regs.edi;
    }
    const uint32_t size = f.call(depth, rets.size, kFileGetSize, file);
    const uint32_t aligned = (size + 0x7fff) & 0xffff8000u;
    f.regs.ebx = size;
    f.regs.edi = aligned;
    const uint32_t buffer = f.call(depth + 4, rets.alloc, kMemAlignedAlloc, aligned, 0x80, 0x30000);
    field<uint32_t>(menu, image.buffer) = buffer;
    f.call(depth + 0x10, rets.read, kFileReadAt, field<int32_t>(menu, image.file), 0, buffer, aligned);
    const uint32_t hash = f.entry - 0x20;
    f.regs.ecx = hash;
    f.call(d.hash, rets.hash, kHashStrInit, hash_name);
    const uint32_t texture =
        f.call(d.hash, rets.load, kNglLoadTexture, hash, 0, field<uint32_t>(menu, image.buffer), size);
    field<uint32_t>(menu, image.texture) = texture;
    f.call(d.quad, rets.quad, kNglSetQuadTexture, menu + quad, texture);
    return true;
}

}  // namespace

// The bonus text helpers (menu in ESI; sub esp 0x20 / 0x68, the text buffer at the body ESP): every call at the body
// ESP, with the original's return addresses.
namespace {

// A wrapped text (+0x140 (text, 300, -1.0)).
void set_wrapped_at(Frame& f, uint32_t depth, uint32_t ret, uint8_t* item, uint32_t text) {
    f.method(depth, ret, item, 0x140, text, 0x12c, 0xbf800000u);
}

// The vehicle / weapon texts: name (+0x3c), info (+0x44, wrapped), "%i / %i" of the item and its category's unlock
// count (+0x40). `rets`: the seven calls' return addresses.
void set_vehicle_or_weapon_text(Frame& f, uint8_t* menu, uint32_t items, uint32_t name_format, uint32_t info_format,
                                uint32_t first_count, uint32_t categories, const uint32_t (&rets)[7]) {
    const uint32_t text = f.entry - 0x68;
    int32_t index = field<int16_t>(menu, 0x22);
    f.call(0x68, rets[0], kSprintf, text, name_format, index + 1, field<int32_t>(menu, items + uint32_t(index) * 4));
    f.method(0x68, rets[1], field<uint8_t*>(menu, 0x3c), 0x6c, text);
    index = field<int16_t>(menu, 0x22);
    f.call(0x68, rets[2], kSprintf, text, info_format, index + 1, field<int32_t>(menu, items + uint32_t(index) * 4));
    set_wrapped_at(f, 0x68, rets[3], field<uint8_t*>(menu, 0x44), text);
    int32_t* const counts = f.local<int32_t>(0x48);
    f.call(0x68, rets[4], 0x00312e30, 0x0095f924, counts);  // Profile_SumMissionStats
    index = field<int16_t>(menu, 0x22);
    const int32_t count = uint32_t(index) < categories ? counts[first_count + uint32_t(index)] : 0;
    f.call(0x68, rets[5], kSprintf, text, 0x0082bb5c, field<int32_t>(menu, items + uint32_t(index) * 4), count);
    f.method(0x68, rets[6], field<uint8_t*>(menu, 0x40), 0x6c, text);
}

}  // namespace

FE_FRAME_ENTRY(FE_BonusCharacterSetText, "ret")

uint32_t XB_CDECL FE_BonusCharacterSetText(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->esi});
    const uint32_t text = f.entry - 0x20;
    f.call(0x20, 0x0030dbd8, kSprintf, text, 0x0082b7b4, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_NAME
    f.method(0x20, 0x0030dbe7, field<uint8_t*>(menu, 0x68), 0x6c, text);
    f.call(0x20, 0x0030dbfc, kSprintf, text, 0x0082b7a0, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_STATS
    set_wrapped_at(f, 0x20, 0x0030dc19, field<uint8_t*>(menu, 0x6c), text);
    f.call(0x20, 0x0030dc2e, kSprintf, text, 0x0082b78c, field<int16_t>(menu, 0x22) + 1);  // BONUS_CHAR_%i_BIO
    return f.method(0x20, 0x0030dc4b, field<uint8_t*>(menu, 0x70), 0x140, text, 0x12c, 0xbf800000u);
}

FE_FRAME_ENTRY(FE_BonusVehicleSetText, "ret")

uint32_t XB_CDECL FE_BonusVehicleSetText(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    static constexpr uint32_t kRets[7] = {0x0031be9d, 0x0031beac, 0x0031bec6, 0x0031bee3,
                                          0x0031bef2, 0x0031bf3d, 0x0031bf4c};
    set_vehicle_or_weapon_text(f, reinterpret_cast<uint8_t*>(uintptr_t{in->esi}), 0x70, 0x0082b3e0, 0x0082b3c8, 10, 6,
                               kRets);
    return 0;
}

FE_FRAME_ENTRY(FE_BonusWeaponSetText, "ret")

uint32_t XB_CDECL FE_BonusWeaponSetText(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    static constexpr uint32_t kRets[7] = {0x0031cc50, 0x0031cc5f, 0x0031cc7c, 0x0031cc99,
                                          0x0031cca8, 0x0031cd0e, 0x0031cd1d};
    set_vehicle_or_weapon_text(f, reinterpret_cast<uint8_t*>(uintptr_t{in->esi}), 0x84, 0x0082b564, 0x0082b54c, 0, 10,
                               kRets);
    return 0;
}

FE_FRAME_ENTRY(FE_BonusCharacterLoadImage, "ret")

uint32_t XB_CDECL FE_BonusCharacterLoadImage(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x44) = in->esi;  // push esi
    f.regs.esi = in->eax;
    constexpr ImageFields kImage{0x74, 0x78, 0x7c};
    free_image_inline(f, menu, kImage, {0x0031d5c2, 0x0031d5cf, 0x0031d5e0, 0x0031d5f0});
    f.call(0x44, 0x0031d5ff, 0x0030dbc0);  // FE_BonusCharacterSetText
    const uint32_t name = f.entry - 0x40;
    f.call(0x44, 0x0031d614, kSprintf, name, 0x0082b7d0, field<int16_t>(menu, 0x22) + 1);  // bonus\char\char%i.dds
    load_image_file(f, menu, kImage, name, {0x50, 0x44, 2, 0x4c, 0x5c}, 0x0082b7c8, 0x80,  // "texchar"
                    {0x0031d624, 0x0031d637, 0x0031d655, 0x0031d665, 0x0031d676, 0x0031d687, 0x0031d697});
    return 0;
}

FE_FRAME_ENTRY(FE_BonusCEMainLoadImage, "ret")

uint32_t XB_CDECL FE_BonusCEMainLoadImage(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x44) = in->esi;  // push esi
    f.regs.esi = in->eax;
    constexpr ImageFields kImage{0x5c, 0x60, 0x64};
    free_image_inline(f, menu, kImage, {0x0031a912, 0x0031a91f, 0x0031a930, 0x0031a940});
    const uint32_t name = f.entry - 0x40;
    if (field<int16_t>(menu, 0x22) == 2)
        f.call(0x44, 0x0031a96a, kSprintf, name, 0x0082a950, 3, field<int32_t>(menu, 0x48) + 1);  // ce_%i_%i.dds
    else
        f.call(0x44, 0x0031a983, kSprintf, name, 0x0082a938, field<int16_t>(menu, 0x22) + 1);  // ce_%i.dds
    load_image_file(f, menu, kImage, name, {0x44, 0x44, 2, 0x4c, 0x5c}, name, 0x68,
                    {0x0031a997, 0x0031a9aa, 0x0031a9c8, 0x0031a9d8, 0x0031a9e9, 0x0031a9fa, 0x0031aa07});
    return 0;
}

FE_FRAME_ENTRY(FE_BonusMainLoadImage, "ret")

uint32_t XB_CDECL FE_BonusMainLoadImage(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x44) = in->esi;  // push esi
    f.regs.esi = in->eax;
    if (field<uint32_t>(menu, 0x8c)) f.call(0x44, 0x0031d9e5, 0x0030dcc0);  // FE_BonusMainFreeImage
    const uint32_t name = f.entry - 0x40;
    if (field<int16_t>(menu, 0x22) == 1) {
        // the movie row: its item's picture (bonus\main\mov%i.dds), the language's own for the first ones
        const int32_t language = title_global<int32_t>(0x0095f5dc);  // g_languageIndex
        int32_t n;
        if (language != 0 || title_global<uint8_t>(0x0095ee00)) {
            n = field<int32_t>(menu, 0x7c) + (language == 1 ? 3 : 2);
        } else {
            n = field<int32_t>(menu, 0x7c) + 1;
            if (n == 1)
                n = 2;
            else if (n == 2)
                n = 1;
        }
        f.call(0x44, 0x0031da69, kSprintf, name, 0x0082b9dc, n);
    } else {
        f.call(0x44, 0x0031da69, kSprintf, name, 0x0082b9c4, field<int16_t>(menu, 0x22) + 1);  // tex%i.dds
    }
    load_image_file(f, menu, ImageFields{0x88, 0x8c, 0x90}, name, {0x44, 0x44, 2, 0x4c, 0x5c}, 0x0082b9bc, 0x94,  // "texMain"
                    {0x0031da7c, 0x0031da92, 0x0031dab0, 0x0031dac6, 0x0031dad7, 0x0031daeb, 0x0031dafe});
    return 0;
}

namespace {

// The weapon / vehicle / concept image loaders (frame: sub esp,0x40; push ebx; push esi).
struct CategoryImage {
    uint32_t free_image, set_text;
    uint32_t items;  // the current item per category
    uint32_t full_screen;
    ImageFields image;
    uint32_t format, format_full, hash, hash_full, quad, quad_full;
    uint32_t rets[12];  // free, set text, sprintf, open, size, alloc, read, hash (full), hash, load, quad (full), quad
    int32_t (*folder)(const uint8_t* menu, int32_t n);  // the folder number of category n (1-based)
};

void load_category_image(const FrameIn* in, uint32_t entry_esp, const CategoryImage& c) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x44) = in->ebx;  // push ebx
    f.word(0x48) = in->esi;  // push esi
    f.regs.esi = in->eax;
    if (field<uint32_t>(menu, c.image.buffer)) f.call(0x48, c.rets[0], c.free_image);
    f.call(0x48, c.rets[1], c.set_text);
    const int32_t index = field<int16_t>(menu, 0x22);
    const bool full = menu[c.full_screen] != 0;
    f.regs.ebx = (f.regs.ebx & ~0xffu) | menu[c.full_screen];
    const uint32_t name = f.entry - 0x40;
    f.call(0x48, c.rets[2], kSprintf, name, full ? c.format_full : c.format, c.folder(menu, index + 1), index + 1,
           field<int32_t>(menu, c.items + uint32_t(index) * 4));
    const LoadRets rets{c.rets[3], c.rets[4], c.rets[5], c.rets[6], full ? c.rets[7] : c.rets[8], c.rets[9],
                        full ? c.rets[10] : c.rets[11]};
    load_image_file(f, menu, c.image, name, {0x48, 0x48, 1, 0x48, 0x48}, full ? c.hash_full : c.hash,
                    full ? c.quad_full : c.quad, rets);
}

}  // namespace

FE_FRAME_ENTRY(FE_BonusWeaponLoadImage, "ret")

uint32_t XB_CDECL FE_BonusWeaponLoadImage(const FrameIn* in, uint32_t entry_esp) {
    static constexpr CategoryImage kWeapon{
        0x0030db60, 0x0031cc30, 0x84, 0xac, {0xb0, 0xb4, 0xb8}, 0x0082b598, 0x0082b5b4, 0x0082b57c, 0x0082b588,
        0xbc, 0x120,  // bonus\weapon%i\wp%i_%i(f).dds, "texweapon" / "texweaponfull"
        {0x0031cd66, 0x0031cd6b, 0x0031cdb3, 0x0031cdc7, 0x0031cde0, 0x0031cdfe, 0x0031ce14, 0x0031ce30, 0x0031ce4b,
         0x0031ce5f, 0x0031ce85, 0x0031cea1},
        [](const uint8_t*, int32_t n) { return n <= 2 ? 1 : 2 + (n > 5 ? 1 : 0); }};
    load_category_image(in, entry_esp, kWeapon);
    return 0;
}

FE_FRAME_ENTRY(FE_BonusVehicleLoadImage, "ret")

uint32_t XB_CDECL FE_BonusVehicleLoadImage(const FrameIn* in, uint32_t entry_esp) {
    static constexpr CategoryImage kVehicle{
        0x0030db00, 0x0031be80, 0x70, 0x88, {0x8c, 0x90, 0x94}, 0x0082b414, 0x0082b430, 0x0082b3f8, 0x0082b404,
        0x98, 0xfc,  // bonus\vehicle%i\vh%i_%i(f).dds, "texvehicle" / "texvehicleFull"
        {0x0031bf86, 0x0031bf8b, 0x0031bfe0, 0x0031bff4, 0x0031c00d, 0x0031c02b, 0x0031c041, 0x0031c05d, 0x0031c078,
         0x0031c08c, 0x0031c0b2, 0x0031c0ce},
        // category 1: folder 1 up to item 13, then 2; category 2: 3; the rest: 4
        [](const uint8_t* menu, int32_t n) {
            if (n == 1) return field<int32_t>(const_cast<uint8_t*>(menu), 0x70) > 13 ? 2 : 1;
            return n == 2 ? 3 : 4;
        }};
    load_category_image(in, entry_esp, kVehicle);
    return 0;
}

FE_FRAME_ENTRY(FE_BonusConceptLoadImage, "ret")

uint32_t XB_CDECL FE_BonusConceptLoadImage(const FrameIn* in, uint32_t entry_esp) {
    static constexpr CategoryImage kConcept{
        0x0030daa0, 0x00312ad0, 0x8c, 0xc0, {0xc4, 0xc8, 0xcc}, 0x0082b270, 0x0082b28c, 0x0082b25c, 0x0082b264,
        0xd0, 0x134,  // bonus\concept%i\cp%i_%i(f).dds, "texart" / "texartFull"
        {0x0031b746, 0x0031b74b, 0x0031b785, 0x0031b798, 0x0031b7b1, 0x0031b7cf, 0x0031b7e5, 0x0031b801, 0x0031b81c,
         0x0031b830, 0x0031b856, 0x0031b872},
        [](const uint8_t*, int32_t n) { return n > 7 ? 2 : 1; }};
    load_category_image(in, entry_esp, kConcept);
    return 0;
}

FE_FRAME_ENTRY(FE_BonusCELoadImage, "ret")

uint32_t XB_CDECL FE_BonusCELoadImage(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x44) = in->ebx;  // push ebx
    f.word(0x48) = in->esi;  // push esi
    f.word(0x4c) = in->edi;  // push edi
    f.regs.esi = in->eax;
    if (field<uint32_t>(menu, 0x48)) f.call(0x4c, 0x0031a4a4, 0x0030c010);  // FE_BonusCEFreeImage
    f.call(0x4c, 0x0031a4a9, 0x0030bf90);                                     // FE_BonusCESetText
    f.regs.edx = in->eax;
    const int32_t group = int32_t(f.call(0x4c, 0x0031a4b0, 0x0030be60));  // the item's group (EDX = menu)
    f.regs.ebx = uint32_t(group);
    const int32_t item = int32_t(f.call(0x4c, 0x0031a4b7, 0x0030bd30));  // the item within it (0: one picture)
    // the folder / file prefixes and the folder number of the category (+0x22)
    uint32_t folder, prefix;  // "%s%i\%s%i"
    int32_t number = 1;
    switch (field<int16_t>(menu, 0x22)) {
        case 0:
            folder = 0x0082a894, prefix = 0x0082a88c;
            if (group < 3 || (group == 3 && item == 1))
                number = 1;
            else if (group < 5)
                number = 2;
            else
                number = 3;
            break;
        case 1:
            folder = prefix = 0x0082a884;
            number = (group >= 4 ? 1 : 0) + 1;
            break;
        case 2:
            folder = 0x0082a87c, prefix = 0x0082a878;
            number = (group >= 5 ? 1 : 0) + 1;
            break;
        case 3:
            folder = 0x0082a870, prefix = 0x0082a868;
            break;
        case 4:
            folder = 0x0082a860, prefix = 0x0082a858;  // "misc" / "misc_"
            break;
        default:
            folder = 0x0082a850, prefix = 0x0082a848;  // "over" / "over_"
            break;
    }
    const bool full = menu[0x40] != 0;
    const uint32_t name = f.entry - 0x40;
    if (item)
        f.call(0x4c, full ? 0x0031a58b : 0x0031a5b8, kSprintf, name, full ? 0x0082a82c : 0x0082a7f8, folder, number,
               prefix, group, item);  // bonus_ce\%s%i\%s%i_%i(f).dds
    else
        f.call(0x4c, 0x0031a5d0, kSprintf, name, full ? 0x0082a814 : 0x0082a7e0, folder, number, prefix,
               group);  // bonus_ce\%s%i\%s%i(f).dds
    f.regs.eax = uint32_t(item);
    f.regs.edi = prefix;
    load_image_file(f, menu, ImageFields{0x44, 0x48, 0x4c}, name, {0x4c, 0x4c, 0, 0x4c, 0x4c},
                    full ? 0x0082a7d4 : 0x0082a7cc, full ? 0xb4 : 0x50,  // "texCEFull" / "texCE"
                    {0x0031a5e4, 0x0031a5f9, 0x0031a617, 0x0031a627, full ? 0x0031a63fu : 0x0031a657u, 0x0031a668,
                     full ? 0x0031a685u : 0x0031a69cu});
    return 0;
}

namespace {

constexpr uint32_t kSumMissionStats = 0x00312e30;  // Profile_SumMissionStats (stdcall: stats, out)
constexpr uint32_t kBonusLocked = 0x0082bb8c;      // "BONUS_LOCKED"

// The locked items' text: BONUS_LOCKED on every item whose vtable +0x24 says locked.
void set_locked_texts(Frame& f, uint8_t* menu, uint32_t count, uint32_t depth, uint32_t ret_query,
                      uint32_t ret_text) {
    for (uint32_t i = 0; i < count; ++i) {
        if (uint8_t(f.method(depth, ret_query, menu_item(menu, i), 0x24)) == 1)
            f.method(depth, ret_text, menu_item(menu, i), 0x30, kBonusLocked);
    }
}

// The current items raised to the stored bests: item i = best + 1 while the unlocked count is above it.
void raise_items(uint8_t* menu, uint32_t items, const int32_t* unlocked, const int32_t* best, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        if (unlocked[i] > best[i] && best[i] > 0) field<int32_t>(menu, items + i * 4) = best[i] + 1;
}

}  // namespace

FE_FRAME_ENTRY(FE_BonusCharacterLabel, "ret")

uint32_t XB_CDECL FE_BonusCharacterLabel(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x4c) = in->ebx;  // push ebx
    f.word(0x50) = in->esi;  // push esi
    f.word(0x54) = in->edi;  // push edi
    f.regs.edi = in->eax;
    int32_t* const counts = f.local<int32_t>(0x48);
    f.call(0x54, 0x0031d897, kSumMissionStats, 0x0095f924, counts);
    const int32_t reached = counts[16];  // the characters reached
    f.regs.ebx = uint32_t(reached);
    for (uint32_t i = 0; i < 7; ++i) {
        f.regs.esi = i + 1;
        f.method(0x54, 0x0031d8ba, menu_item(menu, i), 0x20, reached < int32_t(i) + 1 ? 1 : 0);  // hidden
    }
    static constexpr uint32_t kTexts[7] = {0x0082b9a8, 0x0082b994, 0x0082b980, 0x0082b96c,
                                           0x0082b958, 0x0082b944, 0x0082b930};  // BONUS_BONUS_CHAR_1..7
    for (uint32_t i = 0; i < 7; ++i)
        f.method(0x54, 0x0031d8d0 + i * 0x10, menu_item(menu, i), 0x30, kTexts[i]);
    set_locked_texts(f, menu, 7, 0x54, 0x0031d93d, 0x0031d951);
    f.call(0x54, 0x0031d95e, 0x0031d6b0);  // the character texts (EDI = menu)
    field<uint32_t>(menu, 0x3c) = title_global<uint32_t>(0x0083eb20);  // 1.0
    menu[0x40] = 0;
    return 0;
}

FE_FRAME_ENTRY(FE_BonusVehicleCategoryLabel, "ret")

uint32_t XB_CDECL FE_BonusVehicleCategoryLabel(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->esi});
    f.word(0x94) = in->edi;  // push edi
    int32_t* const counts = f.local<int32_t>(0x90);  // per category unlocked: vehicles at 10..15
    int32_t* const best = f.local<int32_t>(0x48);
    f.call(0x94, 0x0031c5e6, kSumMissionStats, 0x0095f924, counts);
    f.regs.eax = address_of(best);
    f.call(0x94, 0x0031c5ef, 0x00312d60);  // the stored bests (EAX = out)
    for (uint32_t i = 0; i < 6; ++i) {
        f.regs.edi = i * 4;
        f.method(0x94, 0x0031c5fe, menu_item(menu, i), 0x20, 0);
    }
    const int32_t* const unlocked = counts + 10;
    f.regs.edi = uint32_t(unlocked[0]);
    if (!unlocked[0]) f.method(0x94, 0x0031c61a, menu_item(menu, 0), 0x20, 1);
    f.word(0x98) = in->ebx;  // push ebx
    f.regs.ebx = uint32_t(unlocked[1]);
    f.word(0x9c) = in->ebp;  // push ebp
    if (!unlocked[1]) f.method(0x9c, 0x0031c631, menu_item(menu, 1), 0x20, 1);
    // the other categories in the order 4, 3, 2, 5
    static constexpr struct {
        uint32_t item, ret;
    } kHides[4] = {{4, 0x0031c646}, {3, 0x0031c65b}, {2, 0x0031c670}, {5, 0x0031c685}};
    for (const auto& h : kHides) {
        if (!unlocked[h.item]) f.method(0x9c, h.ret, menu_item(menu, h.item), 0x20, 1);
    }
    raise_items(menu, 0x70, unlocked, best + 10, 6);
    static constexpr struct {
        uint32_t item, text;
    } kTexts[6] = {{0, 0x0082b538}, {1, 0x0082b528}, {4, 0x0082b518},
                   {3, 0x0082b500}, {2, 0x0082b4e8}, {5, 0x0082b4d4}};  // LAND, AIR, SEA, ARTILLERY, ANTI_AIR, ROCKET
    f.regs.ebx = in->ebx;
    f.regs.edi = uint32_t(unlocked[0]);
    for (uint32_t i = 0; i < 6; ++i)
        f.method(0x94, 0x0031c70e + i * 0x10, menu_item(menu, kTexts[i].item), 0x30, kTexts[i].text);
    set_locked_texts(f, menu, 6, 0x94, 0x0031c76b, 0x0031c77f);
    f.call(0x94, 0x0031c78d, 0x0031c0e0, menu);  // the category texts (stdcall menu)
    field<uint32_t>(menu, 0x48) = title_global<uint32_t>(0x0083eb20);  // 1.0
    menu[0x4c] = 0;
    return 0;
}

FE_FRAME_ENTRY(FE_BonusWeaponCategoryLabel, "ret")

uint32_t XB_CDECL FE_BonusWeaponCategoryLabel(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x94) = in->esi;  // push esi
    f.word(0x98) = in->edi;  // push edi
    f.regs.edi = in->eax;
    int32_t* const unlocked = f.local<int32_t>(0x90);  // weapons at 0..9
    int32_t* const best = f.local<int32_t>(0x48);
    f.call(0x98, 0x0031d269, kSumMissionStats, 0x0095f924, unlocked);
    f.regs.eax = address_of(best);
    f.call(0x98, 0x0031d272, 0x00312d60);  // the stored bests (EAX = out)
    for (uint32_t i = 0; i < 10; ++i) {
        f.regs.esi = i * 4;
        f.method(0x98, 0x0031d281, menu_item(menu, i), 0x20, 0);
    }
    f.regs.esi = uint32_t(unlocked[0]);
    if (!unlocked[0]) f.method(0x98, 0x0031d29d, menu_item(menu, 0), 0x20, 1);
    f.word(0x9c) = in->ebx;  // push ebx
    f.word(0xa0) = in->ebp;  // push ebp
    f.regs.ebx = uint32_t(unlocked[1]);
    for (uint32_t i = 1; i < 10; ++i) {
        if (!unlocked[i]) f.method(0xa0, 0x0031d2b4 + (i - 1) * 0x15, menu_item(menu, i), 0x20, 1);
    }
    raise_items(menu, 0x84, unlocked, best, 10);
    f.regs.ebx = in->ebx;
    static constexpr uint32_t kTexts[10] = {0x0082b77c, 0x0082b768, 0x0082b758, 0x0082b744, 0x0082b730,
                                            0x0082b71c, 0x0082b708, 0x0082b6f0, 0x0082b6d8, 0x0082b6c0};
    for (uint32_t i = 0; i < 10; ++i)  // SMG, RIFLE, HMG, PISTOL, GRENADE, ASSAULT, SNIPER, CREW_SERVED, ...
        f.method(0x98, 0x0031d439 + i * 0x10, menu_item(menu, i), 0x30, kTexts[i]);
    set_locked_texts(f, menu, 10, 0x98, 0x0031d4db, 0x0031d4ef);
    f.call(0x98, 0x0031d4fc, 0x0031ceb0);  // the category texts (EDI = menu)
    field<uint32_t>(menu, 0x48) = title_global<uint32_t>(0x0083eb20);  // 1.0
    menu[0x4c] = 0;
    return 0;
}

FE_FRAME_ENTRY(FE_BonusMenuLabel, "ret")

uint32_t XB_CDECL FE_BonusMenuLabel(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x4) = in->ecx;  // push ecx (its low byte becomes a flag below)
    f.word(0x8) = in->esi;  // push esi
    f.word(0xc) = in->edi;  // push edi
    f.regs.edi = in->eax;
    field<int32_t>(menu, 0x7c) = 0;
    // row 1's item count: the movies unlocked by the chapters reached (all with 0x008dce54)
    int32_t count;
    if (title_global<uint8_t>(0x008dce54)) {
        count = 6;
    } else {
        const int32_t chapters = title_global<int32_t>(0x008dce44);  // g_chaptersUnlocked
        count = chapters >= 9 ? 5 : chapters >= 6 ? 4 : chapters >= 1 ? 3 : 2;
    }
    field<int32_t>(menu, 0x78) = count;
    f.word(0x10) = in->ebp;  // push ebp
    const int32_t language = title_global<int32_t>(0x0095f5dc);  // g_languageIndex
    if (language != 0 || title_global<uint8_t>(0x0095ee00)) {
        field<int32_t>(menu, 0x78) -= 1;
        if (language == 1) field<int32_t>(menu, 0x78) -= 1;
    }
    f.method(0x10, 0x0031dea3, menu_item(menu, 1), 0x20, field<int32_t>(menu, 0x78) <= 0 ? 1 : 0);
    menu[0x61] = title_global<int32_t>(0x008dcf0c) != field<int32_t>(menu, 0x78);
    // rows 2-5 hidden before chapter 2: the flag byte in the push ecx slot, pushed as a whole word
    f.local(0x4)[0] = title_global<int32_t>(0x008dce44) < 2;
    const uint32_t hidden = f.word(0x4);
    for (uint32_t i = 2; i < 6; ++i) {
        f.regs.esi = i * 4;
        f.method(0x10, 0x0031ded3, menu_item(menu, i), 0x20, hidden);
    }
    static constexpr uint32_t kTexts[6] = {0x0082bbec, 0x0082bbdc, 0x0082bbc8,
                                           0x0082bbb8, 0x0082bba8, 0x0082bb9c};  // CREDITS .. ARTS
    for (uint32_t i = 0; i < 6; ++i) f.method(0x10, 0x0031deea + i * 0x10, menu_item(menu, i), 0x30, kTexts[i]);
    set_locked_texts(f, menu, 6, 0xc, 0x0031df4b, 0x0031df5f);
    f.call(0xc, 0x0031df6c, 0x0031db10);  // EDI = menu
    const uint16_t saved = field<uint16_t>(menu, 0x80);
    menu[0x3c] = 0;
    field<uint32_t>(menu, 0x40) = title_global<uint32_t>(0x0083eb20);  // 1.0
    menu[0x44] = 0;
    field<uint16_t>(menu, 0x22) = saved;
    return 0;
}

FE_FRAME_ENTRY(SpecialMaterialsMenu_PlayMovie, "ret $4")

uint32_t XB_CDECL SpecialMaterialsMenu_PlayMovie(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->edi});
    f.word(0x4) = in->esi;  // push esi
    f.regs.esi = title_global<uint32_t>(0x0095f4cc);
    f.call(0x4, 0x0033306c, 0x002c8bc0);  // S_StopSoundHandle (ESI = the handle)
    f.call(0x4, 0x00333071, 0x000c3700);  // SndSys_Update
    f.call(0x4, 0x00333078, 0x00316320, 1);  // Movie_Stop
    f.call(0x4, 0x00333080, kNglResetDeviceBindings);
    if (const uint32_t texture = field<uint32_t>(menu, 0x64)) f.call(0x4, 0x0033308d, kNglReleaseTexture, texture);
    const int32_t file = field<int32_t>(menu, 0x5c);
    if (file != -1) {
        f.call(0x4, 0x0033309e, kFileClose, file);
        if (const uint32_t buffer = field<uint32_t>(menu, 0x60)) f.call(0x4, 0x003330ae, kMemAlignedFree, buffer);
    }
    f.regs.esi = stack_word(entry_esp + 4);  // the movie name
    field<uint32_t>(menu, 0x60) = 0;
    menu[0x3c] = 1;
    title_global<uint8_t>(0x008dce8d) = 1;
    f.call(0x4, 0x003330d1, 0x00332230, 0x0082dfeb);  // CL_PlayMovie (ESI = name; "" pushed)
    field<int32_t>(menu, 0x44) = 5;
    menu[0x3c] = 0;
    f.regs.eax = in->edi;
    return f.call(0x4, 0x003330e6, 0x0031a900);  // FE_BonusCEMainLoadImage
}

FE_FRAME_ENTRY(SpecialMaterialsMenu_HandleSelect, "ret")

uint32_t XB_CDECL SpecialMaterialsMenu_HandleSelect(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->ecx});
    f.word(0x4) = in->edi;  // push edi
    f.regs.edi = in->ecx;
    if (menu[0x3c]) return in->eax;  // a movie is playing
    const uint32_t index = in->eax;
    field<int32_t>(menu, 0x40) = field<int16_t>(menu, 0x22);
    auto play = [&](uint32_t ret, uint32_t name) { return f.call(0x4, ret, 0x00333060, name); };  // SpecialMaterialsMenu_PlayMovie
    switch (index) {
        case 0:
            return play(0x00333117, 0x0082a998);  // "Bringing"
        case 1:
            return play(0x00333123, 0x0082a990);  // "TheMen"
        case 2: {
            const int32_t map = field<int32_t>(menu, 0x48);
            if (map == 0) return play(0x00333136, 0x0082a988);  // "MPKass"
            if (map == 1) return play(0x00333147, 0x0082a980);  // "MPMaub"
            return play(0x00333153, 0x0082a974);                // "MPTroina"
        }
        case 3:
            return play(0x0033315f, 0x0082a96c);  // "Behind"
        case 4: {
            uint8_t* const dialogs = title_global<uint8_t*>(0x0095f8f8);
            return f.method(0x4, 0x0033316e, dialogs, 0xc, 0x13);
        }
        default:
            return index;
    }
}

FE_FRAME_ENTRY(FE_ProfileSelect, "ret $4")

uint32_t XB_CDECL FE_ProfileSelect(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    const int32_t slot = int32_t(in->eax);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{stack_word(entry_esp + 4)});
    f.word(0x3a8) = in->ebp;  // push ebp
    f.word(0x3ac) = in->esi;  // push esi
    f.regs.esi = in->eax;
    menu[0x34] = 0;
    const uint32_t room = f.call(0x3ac, 0x0033872a, 0x00335460);  // MemDlg_CheckFreeSpace2
    if (!uint8_t(room)) return room;
    field<int32_t>(title_global<uint8_t*>(0x0095f480), 0x11ec) = slot;
    f.word(0x3b0) = in->edi;  // push edi
    if (menu[0x6c + slot * 12]) {  // an empty slot: confirm the overwrite (NO selected)
        uint8_t* const dialogs = title_global<uint8_t*>(0x0095f900);
        f.regs.eax = 0x0082ae6c;  // MEM_XBOX_OVERWRITE_CONFIRM
        f.call(0x3b0, 0x003387b0, 0x003319e0, dialogs, 0, 0, 0x0082dfeb, 1);  // UI_ShowDialog
        f.regs.eax = title_global<uint32_t>(0x0095f900);
        f.call(0x3b0, 0x003387c4, 0x00012320, 0x0082c8b8, 0x00334c70);  // UI_DialogAddButton: MEM_DIALOG_YES
        f.regs.eax = title_global<uint32_t>(0x0095f900);
        f.call(0x3b0, 0x003387d8, 0x00012320, 0x0082c8a8, 0x0030d2f0);  // MEM_DIALOG_NO
        uint8_t* mgr = title_global<uint8_t*>(0x0095f900);
        uint8_t* dialog = field<uint8_t**>(mgr, 4)[field<uint32_t>(mgr, 0xc) ? 1 : 0];
        field<int16_t>(dialog, 0x22) = 1;
        dialog[0x5a] = 1;
        mgr = title_global<uint8_t*>(0x0095f900);
        dialog = field<uint8_t**>(mgr, 4)[field<uint32_t>(mgr, 0xc) ? 1 : 0];
        f.regs.eax = (address_of(mgr) & ~0xffu) | 1;
        f.regs.ecx = field<uint32_t>(mgr, 0xc) ? 1 : 0;
        f.regs.edx = field<uint32_t>(mgr, 4);
        f.regs.esi = field<uint32_t>(mgr, 0xc);
        f.regs.edi = address_of(dialog);
        return f.call(0x3b0, 0x00338813, 0x0031f4b0);  // Dialog_LayoutButtons
    }
    menu[0x54] = 0;
    if (field<int32_t>(menu, 0x4c) == 3) {  // the default profile's settings applied
        uint8_t* const profile = f.local(0x3a4);
        profile[0] = 0;
        field<int32_t>(profile, 0xe8) = -1;
        f.regs.esi = address_of(profile);
        f.call(0x3b0, 0x0033876c, 0x0032cf50);  // Profile_SetDefaults (ESI)
        f.regs.eax = 0x008dce30;
        f.regs.ecx = address_of(profile);
        f.regs.edi = f.call(0x3b0, 0x0033877a, 0x00311f90);  // profile copy (EAX = 0x008dce30, ECX = source)
        f.call(0x3b0, 0x00338781, 0x00332e20);               // CL_ApplyProfileSettings (EDI)
    }
    f.regs.esi = address_of(menu);
    return f.call(0x3b0, 0x00338788, 0x00334b70);  // ProfileMenu_ShowSaveWarning (ESI = menu)
}

FE_FRAME_ENTRY(FE_LoadTheatreBackground, "ret")

uint32_t XB_CDECL FE_LoadTheatreBackground(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const object = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x88) = in->ebx;  // push ebx
    f.word(0x8c) = in->esi;  // push esi
    f.regs.esi = in->eax;
    if (uint8_t* const old = field<uint8_t*>(object, 0x28)) {
        f.method(0x8c, 0x0031ff47, old, 0, 1);  // deleting destructor
        field<uint32_t>(object, 0x28) = 0;
    }
    // the language folder: "de", "fr", "es", "it", else "en"
    static constexpr uint32_t kFolders[4] = {0x0082d010, 0x0082d008, 0x0082d004, 0x0082d00c};
    const uint32_t language = title_global<uint32_t>(0x0095f5dc) - 1;  // g_languageIndex
    title_global<uint8_t>(0x008d50dd) = 0;
    const uint32_t folder = language <= 3 ? kFolders[language] : 0x0082d014;
    const int32_t chapter = title_global<int32_t>(0x008dce48);  // g_currentChapter
    constexpr uint32_t kVa = 0x002a93d0;
    uint32_t name;
    if (chapter > 0 && chapter < 9) {
        if (chapter <= 5)
            name = f.call(0x8c, 0x0031ffa6, kVa, 0x0082cfd4, folder);  // textures\%s\africa.dds
        else
            name = f.call(0x8c, 0x0031ffba, kVa, 0x0082cfbc, folder);  // textures\%s\sicily.dds
    } else {
        name = f.call(0x8c, 0x0031ffcf, kVa, 0x0082cfec, folder);  // textures\%s\europe.dds
    }
    const int32_t file = int32_t(f.call(0x94, 0x0031ffdb, kFileOpenRead, title_global<uint32_t>(0x0095f6f4), name));
    if (file == -1) return uint32_t(file);
    f.regs.ebx = uint32_t(file);
    f.word(0x90) = in->ebp;  // push ebp
    f.word(0x94) = in->edi;  // push edi
    const uint32_t size = f.call(0x94, 0x0031fff1, kFileGetSize, file);
    const uint32_t aligned = (size + 0x7fff) & 0xffff8000u;
    f.regs.edi = size;
    f.regs.esi = aligned;
    const uint32_t buffer = f.call(0x98, 0x0032000f, kMemAlignedAlloc, aligned, 0x80, 0x30000);
    f.call(0xa4, 0x0032001b, kFileReadAt, file, 0, buffer, aligned);
    const uint32_t hash = f.entry - 0x84;
    f.regs.ecx = hash;
    f.call(0x94, 0x0032002c, kHashStrInit, 0x0082cfb4);  // "texLoad"
    const uint32_t texture = f.call(0x94, 0x0032003a, kNglLoadTexture, hash, 0, buffer, size);
    f.regs.esi = texture;
    const uint32_t quad = f.entry - 0x64;
    f.call(0xa4, 0x00320046, 0x0005ddb0, quad);                                // nglInitQuad
    f.call(0xa8, 0x0032005e, 0x0005ded0, quad, 0, 0, 0x44200000, 0x43f00000);  // nglSetQuadRect 640 x 480
    f.call(0xbc, 0x00320069, kNglSetQuadTexture, quad, texture);
    f.call(0xc4, 0x00320078, 0x0005df20, quad, 0x43960000);  // nglSetQuadZ 300
    // two frames with the picture
    for (uint32_t frames = 2; frames; --frames) {
        f.regs.edi = frames;
        f.call(0x94, 0x00320085, kNglResetDeviceBindings);
        f.call(0x94, 0x0032008a, 0x00064690);        // nglBeginFrame
        f.call(0x94, 0x0032008f, 0x0005e430, 0xf3);  // nglSetClearFlags
        f.call(0x98, 0x0032009b, 0x0005e490, 0);     // nglSetSceneDepthEnable
        f.call(0x9c, 0x003200a5, 0x0005e270, quad);  // nglListAddQuad
        f.call(0x94, 0x003200ad, 0x00064ec0);        // nglEndFrame
    }
    f.regs.edi = 0;
    f.call(0x94, 0x003200b5, kNglResetDeviceBindings);
    f.call(0x94, 0x003200bb, kNglReleaseTexture, texture);
    f.call(0x98, 0x003200c1, kFileClose, file);
    return f.call(0x9c, 0x003200c7, kMemAlignedFree, buffer);
}

namespace {

// S_SetHandleVolume (0x002bd140): handle in EAX, volume in XMM0.
void set_handle_volume(uint32_t handle, float volume) {
    __asm__ volatile("movss %1, %%xmm0\n\tcall *%2"
                     : "+a"(handle)
                     : "m"(volume), "r"(uintptr_t{0x002bd140})
                     : "ecx", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "cc", "memory");
}

}  // namespace

PLATFORM_ASM_ENTRY(FE_ApplyControlSettings_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FE_ApplyControlSettings) "addl $4, %esp\n"
                   "ret")

void XB_CDECL FE_ApplyControlSettings(uint8_t* menu) {
    const float fifty = title_global<float>(0x0083ec30);
    const float two = title_global<float>(0x0083eba8);
    const int32_t sensitivity_x = field<int32_t>(menu, 0xa4);
    title_global<int32_t>(0x008dce74) = sensitivity_x;
    title_global<int32_t>(0x008dce78) = field<int32_t>(menu, 0xa8);
    Cvar_SetValue(text_at(0x0082e06c), static_cast<float>(sensitivity_x) / fifty * two);  // cl_sensitivityX
    Cvar_SetValue(text_at(0x0082e05c),
                  static_cast<float>(title_global<int32_t>(0x008dce78)) / fifty * two);  // cl_sensitivityY
    title_global<int32_t>(0x008dce88) = field<int32_t>(menu, 0xac);
    set_handle_volume(title_global<uint32_t>(0x0095f4cc), static_cast<float>(field<int32_t>(menu, 0xac)) / fifty);
    const float volume = static_cast<float>(field<int32_t>(menu, 0xac)) / fifty;
    float& level = field<float>(title_global<uint8_t*>(0x0095f4c4), 0x6018);
    level = volume;
    if (0.0f >= volume) level = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    if (level >= one) level = one;
    const float rest = one - level;
    title_function<void XB_CDECL(float)>(0x000c6490)(one - rest * rest);  // S_SetMasterVolume
    const uint8_t inverted = menu[0xbe];
    title_global<uint8_t>(0x008dce71) = inverted;
    if (uint8_t* const pitch = title_global<uint8_t*>(0x00929c0c)) {  // m_pitch
        const float value = field<float>(pitch, 0x1c);
        const bool flip = inverted ? value > title_global<float>(0x0083eafc) : 0.0f > value;
        if (flip) Cvar_SetValue(text_at(0x0082e07c), field<float>(pitch, 0x1c) * title_global<float>(0x0083eb34));
    }
    title_global<int32_t>(0x008dce6c) = menu[0xc1];
    Cvar_Set2(text_at(0x0082e044), const_cast<char*>(text_at(menu[0xc1] ? 0x0082e058 : 0x0082e098)),
              1);  // g_vehControlMode
    title_global<uint8_t>(0x008dce70) = menu[0xc2];
    title_global<uint8_t>(0x008dce72) = menu[0xbf];
    title_global<uint8_t>(0x008dce68) = menu[0xc0];
    title_global<uint8_t>(0x008dce6a) = menu[0xbd];
    switch (title_global<int32_t>(0x008dce7c)) {  // controller style
        case 1:
            Cbuf_AddText(text_at(0x0082e030));  // exec style_e.cfg
            break;
        case 2:
            Cbuf_AddText(text_at(0x0082e01c));  // exec style_f.cfg
            break;
        case 3:
            Cbuf_AddText(text_at(0x0082e008));  // exec style_g.cfg
            break;
        default:
            Cbuf_AddText(text_at(0x0082dff4));  // exec style_h.cfg
            break;
    }
    title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute
    Cvar_Set2(text_at(0x0082c538), const_cast<char*>(text_at(0x0082e058)), 1);  // cl_run 1
    uint8_t* const item = *field<uint8_t**>(title_global<uint8_t*>(0x0095f8fc), 4);
    item[0x40] = menu[0xbc];
}

FE_FRAME_ENTRY(FE_ControlsUpdateText, "ret")

uint32_t XB_CDECL FE_ControlsUpdateText(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->eax});
    f.word(0x4) = in->ecx;  // push ecx (the colour bytes below)
    f.word(0x8) = in->ebx;  // push ebx
    f.word(0xc) = in->esi;  // push esi
    f.word(0x10) = in->edi;  // push edi
    f.regs.ebx = in->eax;
    // the controller style's letter
    static constexpr uint32_t kStyles[4][2] = {
        {0x0082c008, 0x00335d85}, {0x0082c004, 0x00335daa}, {0x0082c000, 0x00335d9b}, {0x0082bffc, 0x00335daa}};
    const uint32_t style = field<uint32_t>(menu, 0xc0);
    if (style <= 3) f.method(0x10, kStyles[style][1], field<uint8_t*>(menu, 0x44), 0x78, kStyles[style][0]);
    f.word(0x14) = in->ebp;  // push ebp
    f.regs.esi = in->eax;
    f.call(0x14, 0x00335db2, 0x0030e080);  // ControllerConfigMenu_UpdateLabelColors (ESI = menu)
    f.regs.esi = field<uint32_t>(menu, 0xc0);
    f.call(0x14, 0x00335dbd, 0x00331030);  // CL_ExecControllerConfig (ESI = style)
    // each command's key name: {item, key}
    static constexpr uint32_t kKeys[16][2] = {{0x68, 0x0d}, {0x6c, 0x1b}, {0x70, 0xd4}, {0x74, 0xd5}, {0x78, 0xd3},
                                              {0x7c, 0xd6}, {0x80, 0xd7}, {0x84, 0xd9}, {0x88, 0xdb}, {0x8c, 0xd8},
                                              {0x90, 0xda}, {0x94, 0xdc}, {0x98, 0x9a}, {0x9c, 0x9b}, {0xa0, 0x9c},
                                              {0xa4, 0x9d}};
    static constexpr uint32_t kKeyRets[16][2] = {
        {0x00335dcf, 0x00335dd5}, {0x00335de4, 0x00335deb}, {0x00335dfa, 0x00335e01}, {0x00335e10, 0x00335e17},
        {0x00335e26, 0x00335e2d}, {0x00335e3c, 0x00335e43}, {0x00335e55, 0x00335e5f}, {0x00335e71, 0x00335e7b},
        {0x00335e8d, 0x00335e97}, {0x00335ea9, 0x00335eb3}, {0x00335ec5, 0x00335ecf}, {0x00335ee1, 0x00335eeb},
        {0x00335efd, 0x00335f07}, {0x00335f19, 0x00335f23}, {0x00335f35, 0x00335f3f}, {0x00335f51, 0x00335f5b}};
    f.regs.esi = address_of(menu + 0x68);
    for (uint32_t i = 0; i < 16; ++i) {
        uint8_t* const item = field<uint8_t*>(menu, kKeys[i][0]);
        f.regs.edi = kKeys[i][1];
        const uint32_t name = f.call(0x14, kKeyRets[i][0], 0x0030e0f0);  // FE_CommandDisplayName (EDI = key)
        f.method(0x14, kKeyRets[i][1], item, 0x6c, name);
    }
    // the fixed ones: PAUSE, OBJECTIVES, MOVE, LOOK
    static constexpr uint32_t kFixed[4][3] = {{0x68, 0x0082bfe8, 0x00335f67},
                                              {0x6c, 0x0082bfd0, 0x00335f74},
                                              {0xa8, 0x0082bfc0, 0x00335f84},
                                              {0xac, 0x0082bfb0, 0x00335f94}};
    for (const auto& fixed : kFixed) f.method(0x14, fixed[2], field<uint8_t*>(menu, fixed[0]), 0x6c, fixed[1]);
    // all 18 labels black (0xff000000, built in the push ecx slot)
    f.word(0x4) = 0xff000000u;
    const uint32_t black = f.word(0x4);
    f.regs.ebx = black;
    uint32_t result = 0;
    for (uint32_t i = 0; i < 18; ++i) {
        f.regs.edi = 18 - i;
        f.regs.esi = address_of(menu + 0x68 + i * 4);
        result = f.method(0x10, 0x00335fbc, field<uint8_t*>(menu, 0x68 + i * 4), 0x90, black);
    }
    return result;
}

FE_FRAME_ENTRY(MainMenu_Update, "ret $4")

uint32_t XB_CDECL MainMenu_Update(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->ecx});
    const uint32_t pad = stack_word(entry_esp + 4);
    f.word(0x1c) = in->edi;  // push edi
    f.regs.edi = in->ecx;
    if (field<int32_t>(menu, 0xc) == 0x10) {  // the selection on item 16: its handler, then the menu's +0x14
        if (uint8_t* const item = field<uint8_t**>(menu, 4)[0x10]) f.method(0x1c, 0x003367c2, item, 0x30, pad);
        return f.method(0x1c, 0x003367c9, menu, 0x14);
    }
    uint8_t* const live = title_global<uint8_t*>(0x0095f6fc);  // g_liveMgr
    menu[0xf4] = field<int32_t>(menu, 0xc) != 0x14 && field<int32_t>(live, 4) != 1;
    f.word(0x20) = in->ebx;  // push ebx
    f.word(0x24) = in->esi;  // push esi
    // the sign-in text black -> white: its colour (vtable +0xb4 into a local) against 0xff000000
    uint8_t* const status = title_global<uint8_t*>(0x0095f478);
    f.regs.ebx = (in->ebx & ~0xffu) | 0xff;
    f.word(0x18) = 0xff000000u;
    const uint32_t* const colour = reinterpret_cast<const uint32_t*>(
        uintptr_t{f.method(0x24, 0x00336819, status, 0xb4, f.entry - 0x14)});
    if (*colour == f.word(0x18)) {
        f.word(0x18) = 0xffffffffu;
        f.method(0x24, 0x00336844, title_global<uint8_t*>(0x0095f478), 0x90, f.word(0x18));
    }
    uint8_t* const text = title_global<uint8_t*>(0x0095f478);
    if (text) {
        uint8_t* const mgr = title_global<uint8_t*>(0x0095f6fc);
        const int32_t state = field<int32_t>(mgr, 4);
        if (state != field<int32_t>(menu, 0xec) || field<int32_t>(mgr, 0x446c) != field<int32_t>(menu, 0xf0)) {
            field<int32_t>(menu, 0xec) = state;
            const uint32_t result = field<uint32_t>(mgr, 0x446c);
            field<uint32_t>(menu, 0xf0) = result;
            if (state == 0) {  // signed out: by the sign-in result
                if (result == 0x80151082)
                    f.method(0x24, 0x0033693f, text, 0x6c, 0x0082cb80);
                else
                    f.method(0x24, 0x0033694b, text, 0x6c,
                             result == 0x80151080 || result == 0 ? 0x0082cb98 : 0x0082cb68);
            } else if (state == 1) {
                f.method(0x24, 0x0033691a, text, 0x6c, 0x0082cb54);  // FEMENU_SIGNING_IN
            } else if (state == 2) {
                // FEMENU_SIGNED_IN_AS with the gamertag
                f.regs.ecx = address_of(mgr);
                const uint32_t user = f.call(0x24, 0x003368a4, 0x00011220);  // Live_GetCurrentUser
                const uint32_t name = f.entry - 0x10;
                f.call(0x28, 0x003368b7, kSprintf, name, 0x0082cb50, user + 0x10);  // "%S"
                f.word(0x28) = user + 0x10;
                f.regs.esi = 0x0082cb3c;  // FEMENU_SIGNED_IN_AS
                const uint32_t localized = f.call(0x24, 0x003368c9, 0x0029d5f0, name);  // Loc_GetString
                f.regs.ecx = f.entry - 0x14;
                const uint32_t string = f.call(0x2c, 0x003368d3, 0x0011eb50, localized);  // ScriptString_FromCString
                f.call(0x2c, 0x003368de, 0x0011eec0, f.entry - 0x18, string);            // ScriptString_ConcatCString
                f.regs.ecx = f.entry - 0x14;
                f.call(0x24, 0x003368ea, 0x0011d640);  // ScriptString_Release
                const uint32_t joined = f.word(0x18);
                f.method(0x24, 0x00336905, title_global<uint8_t*>(0x0095f478), 0x78, joined ? joined + 6 : 0);
                f.regs.ecx = f.entry - 0x18;
                f.call(0x24, 0x0033690e, 0x0011d640);  // ScriptString_Release
            }
        }
    }
    uint32_t result = 0;
    if (menu[0xf5]) {
        if (title_global<uint8_t>(0x0095ee09)) {
            title_global<uint8_t>(0x0095ee09) = 0;
        } else {
            // the start-up movies: the legal screen of the language, then ATVI, Treyarch and the intro unless skipped
            const int32_t language = title_global<int32_t>(0x0095f5dc);
            title_global<uint32_t>(0x0095f460) = title_global<uint32_t>(0x0083ec28);  // 5000.0
            f.regs.esi = language == 2 ? 0x0082cb30 : language == 3 ? 0x0082cb24 : language == 4 ? 0x0082cb18 : 0x0082cb10;
            f.call(0x24, 0x003369ac, 0x00332230, 0x0082dfeb);  // CL_PlayMovie (ESI = name)
            title_global<float>(0x0095f460) = 0.0f;
            static constexpr uint32_t kMovies[3][2] = {
                {0x0082cb08, 0x003369d9}, {0x0082cafc, 0x003369fb}, {0x0082cbd8, 0x00336a1d}};  // ATVI, Treyarch
            for (const auto& movie : kMovies) {
                if (title_global<uint8_t>(0x0095f91c)) break;
                f.regs.esi = movie[0];
                title_global<uint8_t>(0x0095f46d) = 1;
                f.call(0x24, movie[1], 0x00332230, 0x0082dfeb);
            }
            f.regs.eax = title_global<uint32_t>(0x0095f4cc);
            result = f.call(0x24, 0x00336a2f, 0x002ce520, 0x0082cbe0);  // Music_Play (EAX = the handle)
        }
    } else {
        const int32_t selected = field<int32_t>(menu, 0xc);
        if (selected >= 0) {
            if (uint8_t* const item = field<uint8_t**>(menu, 4)[selected]) f.method(0x24, 0x00336a5e, item, 0x30, pad);
        }
        result = f.method(0x24, 0x00336a65, menu, 0x14);
        if (!menu[0xe8]) {
            f.word(0x28) = in->ebp;  // push ebp
            f.regs.edx = 0x0082caec;
            const uint32_t top = f.call(0x28, 0x00336a7a, 0x0023ec30);  // RE_RegisterShader "main_back_top"
            f.regs.esi = top;
            f.regs.edx = 0x0082cad8;
            const uint32_t bottom = f.call(0x28, 0x00336a86, 0x0023ec30);  // "main_back_bottom"
            const uint32_t fallback = title_global<uint32_t>(0x009026f8);  // ngl_defaultTexture
            result = fallback;
            if (top != fallback && bottom != fallback) {
                f.call(0x28, 0x00336a9f, kNglSetQuadTexture, menu + 0x20, top);
                result = f.call(0x30, 0x00336aac, kNglSetQuadTexture, menu + 0x84, bottom);
                menu[0xe8] = 1;
            }
        }
    }
    title_global<uint8_t>(0x0095f91b) = 1;
    menu[0xf5] = 0;
    return result;
}

FE_FRAME_ENTRY(ChapterMenu_Draw, "ret")

uint32_t XB_CDECL ChapterMenu_Draw(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->ecx});
    f.word(0x18) = in->ebx;  // push ebx
    f.word(0x1c) = in->ebp;  // push ebp
    f.word(0x20) = in->esi;  // push esi
    f.word(0x24) = in->edi;  // push edi
    f.regs.ebx = in->ecx;
    const uint32_t text = f.entry - 0x14;
    constexpr uint32_t kEmpty = 0x0082dfeb;
    // the quotes around the current one (+0x190, two either side): FEMENU_QUOTE_<i+1> and FEMENU_CHAPTER_<i>; the
    // others cleared; the texts +0x154.. cleared
    for (int32_t i = 0; i < 14; ++i) {
        f.regs.edi = uint32_t(i);
        f.regs.esi = address_of(menu + 0x154 + i * 4);
        const int32_t current = field<int32_t>(menu, 0x190);
        if (i >= current - 2 && i <= current + 2) {
            f.call(0x24, 0x0031afa7, kSprintf, text, 0x0082ab74, i + 1);  // FEMENU_QUOTE_%i
            f.method(0x24, 0x0031afbd, menu_item(menu, uint32_t(i)), 0x30, text);
            f.call(0x24, 0x0031afcd, kSprintf, text, 0x0082ab60, i);  // FEMENU_CHAPTER_%i
            f.method(0x24, 0x0031afdd, field<uint8_t*>(menu, 0x11c + uint32_t(i) * 4), 0x6c, text);
        } else {
            f.method(0x24, 0x0031aff2, menu_item(menu, uint32_t(i)), 0x34, kEmpty);
            f.method(0x24, 0x0031afff, field<uint8_t*>(menu, 0x11c + uint32_t(i) * 4), 0x78, kEmpty);
        }
        f.method(0x24, 0x0031b00b, field<uint8_t*>(menu, 0x154 + uint32_t(i) * 4), 0x78, kEmpty);
    }
    f.regs.edi = 14;
    f.regs.esi = address_of(menu + 0x154 + 14 * 4);
    uint32_t result = 0;
    if (!title_global<uint8_t>(0x0095f91e)) {
        if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) {
            f.regs.esi = address_of(panel);
            f.call(0x24, 0x0031b02d, 0x00310010);  // Panel_Draw (ESI = panel)
        }
        if (uint8_t* const picture = field<uint8_t*>(menu, 0x18c)) {
            f.method(0x24, 0x0031b04b, picture, 0x8c, 0x43a00000, 0x43700000);  // placed at (320, 240)
            f.method(0x24, 0x0031b056, field<uint8_t*>(menu, 0x18c), 0x44);
        }
        if (uint8_t* const help = field<uint8_t*>(menu, 0x38)) f.method(0x24, 0x0031b064, help, 0x3c);
    }
    if (!title_global<uint8_t>(0x0095f916)) {
        if (uint8_t* const movie = title_global<uint8_t*>(0x0095f454)) {
            f.method(0x24, 0x0031b07c, movie, 0x8);
            f.call(0x24, 0x0031b081, 0x0030fca0);  // Subtitle_Draw
            f.regs.ecx = title_global<uint32_t>(0x0095f454);
            const uint32_t texture = f.call(0x24, 0x0031b08c, 0x000cf100);  // Movie_GetFrameTexture (ECX)
            uint8_t* const quad = title_global<uint8_t*>(0x0095f458);
            field<uint32_t>(quad, 0x60) = texture;
            result = f.call(0x24, 0x0031b09b, 0x0005e270, quad);  // nglListAddQuad
        }
    }
    if (!title_global<uint8_t>(0x0095f91e)) {
        f.regs.ecx = in->ecx;
        result = f.call(0x24, 0x0031b0ae, 0x00314c30);  // UIMenu_Draw
    }
    return result;
}

FE_FRAME_ENTRY(FE_CheckStartupStorage, "ret $4")

uint32_t XB_CDECL FE_CheckStartupStorage(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    const uint32_t menu = stack_word(entry_esp + 4);
    f.word(0x44) = in->esi;  // push esi
    f.word(0x48) = in->edi;  // push edi
    title_global<uint8_t>(0x008d50dd) = 0;
    f.regs.eax = title_global<uint32_t>(0x0095f480);
    f.call(0x48, 0x003346f6, 0x0031acf0);  // ProfileSave_PrepareRequest (EAX)
    // a zeroed block copied over the free-space block
    uint32_t* const zeros = f.local<uint32_t>(0x40);
    uint32_t* const block = f.local<uint32_t>(0x20);
    for (uint32_t i = 0; i < 8; ++i) zeros[i] = 0;
    for (uint32_t i = 0; i < 8; ++i) block[i] = zeros[i];
    f.regs.eax = 0;
    f.regs.ecx = 0;
    f.regs.esi = address_of(zeros + 8);
    f.regs.edi = address_of(block + 8);
    f.call(0x48, 0x0033472e, 0x000d0c90, 8);  // Save_SetPort
    f.regs.ecx = address_of(block);
    using Call64 = uint64_t XB_CDECL(uint32_t, uint32_t, const TitleRegs*, uint32_t, const uint32_t*);
    const uint32_t args[1] = {address_of(block)};
    const uint64_t space =
        reinterpret_cast<Call64*>(reinterpret_cast<uintptr_t>(&title_call_at_regs))(f.entry - 0x4c, 0x000d1b40, &f.regs, 1, args);  // Save_GetFreeSpace
    stack_word(f.entry - 0x54) = 0x00334738;
    const uint32_t device = uint32_t(space);
    f.regs.eax = device;
    f.regs.edi = block[6];
    // the "present" word: EDX's upper bytes, the low byte block +0x18 != 0
    const uint32_t present = (uint32_t(space >> 32) & ~0xffu) | (block[6] != 0 ? 1 : 0);
    f.regs.edx = present;
    uint32_t result = f.call(0x48, 0x0033474a, 0x003345d0, present);  // MemDlg_CheckSaveDevice (EAX = device)
    f.regs.esi = in->esi;
    f.regs.edi = in->edi;
    if (!uint8_t(result)) {
        f.regs.eax = title_global<uint32_t>(0x0095f900);
        f.call(0x40, 0x0033475a, 0x0031f6e0);  // DialogMgr_CloseDialog (EAX)
        result = f.method(0x40, 0x00334767, title_global<uint8_t*>(0x0095f8f8), 0xc, 0);
    }
    f.regs.eax = (result & ~0xffu) | (block[6] != 0 ? 1 : 0);
    f.regs.ecx = menu;
    f.regs.edx = block[6];
    result = f.call(0x40, 0x00334779, 0x003344e0);  // MemDlg_CheckFreeSpace (AL, ECX = menu)
    if (uint8_t(result)) {
        title_global<uint8_t*>(0x0095f480)[0x11e4] = 0;
        f.regs.eax = title_global<uint32_t>(0x0095f900);
        f.call(0x40, 0x0033478f, 0x0031f6e0);
        result = f.method(0x40, 0x003347a1, title_global<uint8_t*>(0x0095f8f8), 0xc, 0);
    }
    return result;
}

FE_FRAME_ENTRY(FE_LiberatorCreateCrossIcons, "ret")

uint32_t XB_CDECL FE_LiberatorCreateCrossIcons(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const owner = reinterpret_cast<uint8_t*>(uintptr_t{in->edi});
    f.word(0x44) = in->esi;  // push esi
    // one 22 x 22 quad in white
    const uint32_t size = title_global<uint32_t>(0x0083edd4);  // 22.0
    uint32_t* const points = f.local<uint32_t>(0x30);
    const uint32_t kPoints[12] = {0, 0, 0, size, 0, 0, 0, size, 0, size, size, 0};
    for (uint32_t i = 0; i < 12; ++i) points[i] = kPoints[i];
    uint32_t* const colours = f.local<uint32_t>(0x40);
    for (uint32_t i = 0; i < 4; ++i) colours[i] = 0xffffffffu;
    f.regs.eax = (in->eax & ~0xffu) | 0xff;
    // {picture name, texture, owner field, return addresses: malloc, init, add quad}
    static constexpr uint32_t kCrosses[2][6] = {
        {0x0082d844, 0x0082d834, 0x8, 0x00317650, 0x00317663, 0x00317687},   // normal_cross: ironcrosswhite
        {0x0082d828, 0x0082d818, 0xc, 0x0031768e, 0x003176a1, 0x003176c5}};  // ace_cross: ironcrossgold
    uint32_t result = 0;
    for (const auto& cross : kCrosses) {
        const uint32_t block = f.call(0x44, cross[3], 0x00042c10, 0x40);  // Z_Malloc
        uint8_t* picture = nullptr;
        if (block) {
            f.regs.esi = block;
            picture = reinterpret_cast<uint8_t*>(uintptr_t{f.call(0x44, cross[4], 0x00317050, cross[0])});  // UIPicture_Init
        }
        field<uint8_t*>(owner, cross[2]) = picture;
        f.regs.edx = field<uint32_t>(picture, 0);
        result = f.method(0x44, cross[5], picture, 0x3c, points, colours, 7, 0x42c80000, cross[1]);  // add quad, 100.0
    }
    return result;
}

FE_FRAME_ENTRY(FE_FadeOut, "ret")

uint32_t XB_CDECL FE_FadeOut(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    // the RDTSC start time (unused) through XAPI QueryPerformanceCounter: the loader patches both sites alike
    f.call(0x6c, 0x0032d898, 0x001ed6ca, f.entry - 0x6c);
    float& elapsed = *f.local<float>(0x6c);
    float t = 0.0f;
    float duration = title_global<float>(0x008d2090);
    if (duration > t) {
        f.word(0x70) = in->esi;  // push esi
        for (;;) {
            // the music channel faded to 1 - elapsed / duration
            uint32_t handle = title_global<uint32_t>(0x009600f4);
            t = title_global<float>(0x008d208c) + t;
            elapsed = t;
            const float fraction = t / duration;
            uint32_t index = handle & 0xfff;
            f.regs.ecx = handle;
            if (index < 0x200 && title_global<uint32_t>(0x00962864 + index * 8) == handle >> 12 &&
                title_global<uint32_t>(0x00962860 + index * 8)) {
                uint8_t* const channel = title_global<uint8_t*>(0x00962860 + index * 8);
                const int32_t voice = field<int32_t>(channel, 0);
                const float level = title_global<float>(0x0083eb20) - fraction;
                *f.local<float>(0x68) = level;
                if (voice != -1) {
                    const float volume = field<float>(channel, 0x1c) * level;
                    uint32_t bits;
                    std::memcpy(&bits, &volume, 4);
                    f.regs.esi = address_of(channel);
                    f.call(0x70, 0x0032d961, 0x000c44f0, voice, 0, bits);  // SndVoice_SetParam
                    field<float>(channel, 0x10) = *f.local<float>(0x68);
                }
            }
            f.call(0x70, 0x0032d97a, 0x002c8c20, title_global<uint32_t>(0x0095f4c4));  // S_UpdateChannels
            // two frames of the black quad at the fade alpha (0x008d2088 << 24)
            const uint32_t quad = f.entry - 0x64;
            for (uint32_t frames = 2; frames; --frames) {
                f.regs.esi = frames;
                f.call(0x70, 0x0032d985, kNglResetDeviceBindings);
                f.call(0x70, 0x0032d98a, 0x00064690);  // nglBeginFrame
                f.call(0x70, 0x0032d994, 0x0005ddb0, quad);  // nglInitQuad
                const int32_t height = int32_t(f.call(0x74, 0x0032d999, 0x00064650));  // nglGetScreenHeight
                const float h = static_cast<float>(height);
                std::memcpy(f.local(0x74), &h, 4);
                const int32_t width = int32_t(f.call(0x74, 0x0032d9a7, 0x00064640));  // nglGetScreenWidth
                const float w = static_cast<float>(width);
                uint32_t wb, hb;
                std::memcpy(&wb, &w, 4);
                std::memcpy(&hb, &h, 4);
                f.call(0x70, 0x0032d9bf, 0x0005ded0, quad, 0, 0, wb, hb);  // nglSetQuadRect
                f.call(0x84, 0x0032d9d2, 0x0005deb0, quad, title_global<uint32_t>(0x008d2088) << 24);  // nglSetQuadColor
                f.call(0x8c, 0x0032d9e0, 0x0005de40, quad, 2, 0);  // nglSetQuadBlend
                f.call(0x98, 0x0032d9ea, 0x0005e270, quad);        // nglListAddQuad
                f.call(0x70, 0x0032d9f2, 0x00064ec0);              // nglEndFrame
            }
            duration = title_global<float>(0x008d2090);
            if (!(duration > elapsed)) break;
            t = elapsed;
        }
        f.regs.esi = in->esi;
    }
    f.word(0x70) = in->ebx;  // push ebx
    f.regs.ebx = title_global<uint32_t>(0x0095f4c4);
    f.call(0x70, 0x0032da15, 0x002c8d60);                                 // S_StopAllSounds (EBX)
    const uint32_t mode = f.call(0x70, 0x0032da1a, 0x000c34f0);           // Snd_GetSpeakerMode
    f.call(0x70, 0x0032da20, 0x000c3500, mode);                           // S_SetMasterAttenuation
    return f.call(0x70, 0x0032da2f, 0x002c8c20, title_global<uint32_t>(0x0095f4c4));  // S_UpdateChannels
}

FE_FRAME_ENTRY(FE_LoadPanel, "ret")

uint32_t XB_CDECL FE_LoadPanel(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    f.word(0x4) = in->ebx;  // push ebx
    f.word(0x8) = in->esi;  // push esi
    f.word(0xc) = in->edi;  // push edi
    const uint32_t name = stack_word(entry_esp + 4);
    const uint32_t data = stack_word(entry_esp + 8);
    uint8_t* const panel = reinterpret_cast<uint8_t*>(uintptr_t{f.call(0xc, 0x003393fa, 0x00042c10, 0x60)});  // Z_Malloc
    if (panel) {
        for (uint32_t i = 0; i < 6; ++i) field<uint32_t>(panel, i * 4) = 0;
        field<int32_t>(panel, 0x18) = -1;
        panel[0x1c] = 0;
    }
    f.regs.ecx = 0;
    f.regs.ebx = name;
    f.regs.esi = data;
    f.regs.edi = address_of(panel);
    f.call(0xc, 0x00339432, 0x00337380, name);  // Panel_Load (ESI = data, EDI = panel)
    f.regs.eax = address_of(panel);
    f.regs.edx = name;
    return f.call(0xc, 0x00339440, 0x0032a910, 0x0095f8d0);  // FE_SetupPanel (EAX = panel, EDX = name; g_uiRoot)
}

FE_FRAME_ENTRY(FE_CreateMenus, "ret")

uint32_t XB_CDECL FE_CreateMenus(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const owner = reinterpret_cast<uint8_t*>(uintptr_t{in->esi});
    const uint32_t block = f.call(0, 0x0033705a, 0x00042c10, 0xf8);  // Z_Malloc
    uint8_t* front_end = nullptr;
    if (block) {
        f.regs.eax = block;
        front_end = reinterpret_cast<uint8_t*>(uintptr_t{f.call(0, 0x00337067, 0x00336ad0, block)});  // FrontEndMenu_Construct
    }
    field<uint8_t*>(owner, 0x28) = front_end;
    if (!title_global<uint8_t>(0x0095ee01)) {
        // the first screen: 0, or 1 with 0x0095ee09 (unless 0x008dd1ce asks for 0 once)
        if (title_global<uint8_t>(0x008dd1ce)) {
            f.method(0, 0x00337089, front_end, 0xc, 0);
            title_global<uint8_t>(0x008dd1ce) = 0;
        } else if (!title_global<uint8_t>(0x0095ee09)) {
            f.method(0, 0x003370a2, front_end, 0xc, 0);
        } else {
            f.method(0, 0x003370ab, front_end, 0xc, 1);
        }
    }
    const uint32_t dialogs = f.call(0, 0x003370b2, 0x00042c10, 0x24);  // Z_Malloc
    uint32_t result = 0;
    if (dialogs) {
        f.regs.eax = dialogs;
        result = f.call(0, 0x003370be, 0x00331cb0);  // DialogMgr_Construct (EAX)
    }
    field<uint32_t>(owner, 0x30) = result;
    return result;
}

FE_FRAME_ENTRY(FE_DifficultyMenu_Init, "ret")

uint32_t XB_CDECL FE_DifficultyMenu_Init(const FrameIn* in, uint32_t entry_esp) {
    Frame f(in, entry_esp);
    uint8_t* const menu = reinterpret_cast<uint8_t*>(uintptr_t{in->ecx});
    f.word(0x10) = in->ebx;  // push ebx
    f.word(0x14) = in->edi;  // push edi
    f.word(0x18) = in->esi;  // push esi
    f.regs.edi = in->ecx;
    const uint32_t name = f.entry - 0xc;
    // the pictures difficulty0-2 hidden
    for (int32_t i = 0; i < 3; ++i) {
        f.regs.ebx = uint32_t(i);
        f.call(0x18, 0x00338d90, kSprintf, name, 0x0082b24c, i);  // difficulty%i
        uint8_t* const panel = field<uint8_t*>(menu, 0x34);
        f.regs.esi = address_of(panel);
        f.regs.eax = address_of(panel);
        const uint32_t picture = f.call(0x18, 0x00338da2, 0x00321650, name);  // Panel_FindPicture (EAX = list)
        const uint32_t* const pictures = field<uint32_t*>(panel, 0);
        const uint32_t count = field<uint32_t>(panel, 8);
        int32_t index = -1;
        for (uint32_t k = 0; k < count; ++k)
            if (pictures[k] == picture) {
                index = int32_t(k);
                break;
            }
        f.method(0x18, 0x00338dc8, reinterpret_cast<uint8_t*>(uintptr_t{pictures[index]}), 0x34, 0);
    }
    f.regs.ecx = in->ecx;
    f.call(0x18, 0x00338dd5, 0x00314b20);  // UIMenu_RestoreSelection
    f.regs.esi = in->esi;
    f.regs.ebx = 1;
    if (!title_global<uint8_t>(0x0095f91a)) {
        // the propaganda movie behind the menu, full screen
        if (uint8_t(f.call(0x14, 0x00338dee, 0x00332030, 0x0082cacc))) {  // Movie_Open "Propgnda"
            f.call(0x14, 0x00338e0e, 0x0005ded0, title_global<uint32_t>(0x0095f458), 0, 0, 0x44200000, 0x43f00000);
            title_global<uint8_t>(0x0095f91a) = 1;
        }
    }
    field<int32_t>(menu, 0x50) = 1;
    const uint32_t result = f.call(0x14, 0x00338e20, 0x00325d60, 1);  // FE_SetDifficultyPicture (EDI = menu)
    field<int16_t>(menu, 0x22) = 1;
    return result;
}

namespace {

// Seconds (at most 99:59:59 + days) split into days, hours, minutes and seconds.
void split_play_time(int32_t seconds, int32_t* out_seconds, int32_t* out_minutes, int32_t* out_hours,
                     int32_t* out_days) {
    if (seconds > 0x57e3f) seconds = 0x57e3f;  // 359999
    const int32_t day_rest = seconds % 86400;
    *out_days = (seconds - day_rest) / 86400;
    const int32_t hour_rest = day_rest % 3600;
    *out_hours = (day_rest - hour_rest) / 3600;
    const int32_t minute_rest = hour_rest % 60;
    *out_minutes = (hour_rest - minute_rest) / 60;
    *out_seconds = minute_rest;
}

}  // namespace

int32_t* XB_REGS1 XB_STDCALL FE_SplitPlayTime(int32_t seconds, int32_t* out_seconds, int32_t* out_minutes,
                                              int32_t* out_hours, int32_t* out_days) {
    split_play_time(seconds, out_seconds, out_minutes, out_hours, out_days);
    return out_seconds;
}

int32_t* XB_REGS1 XB_STDCALL FE_SplitPlayTime2(int32_t seconds, int32_t* out_seconds, int32_t* out_minutes,
                                               int32_t* out_hours, int32_t* out_days) {
    split_play_time(seconds, out_seconds, out_minutes, out_hours, out_days);
    return out_seconds;
}

}  // namespace game
