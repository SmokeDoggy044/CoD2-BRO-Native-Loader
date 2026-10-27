#include "fe_setup_special.h"

#include "fe_setup_internal.h"
#include "ngl.h"

namespace game {

using namespace setup;

namespace {

uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

// copy_text with the colour local at `local` (the original's argument slot, which keeps the colour).
uint8_t* copy_text_into(uint8_t* source, uint32_t* local) {
    uint8_t* const block = z_malloc(0x98);
    if (!block) return nullptr;
    const uint32_t colour = *method<ColourMethod>(source, 0xb4)(source, local);
    const float scale = static_cast<float>(get(source, 0xa4));
    const float y = static_cast<float>(get(source, 0xcc));
    const float x = static_cast<float>(get(source, 0xc8));
    const int32_t kind = method<IntMethod>(source, 0xb0)(source);
    return UIText_Construct(block, kind, bits(x), bits(y), 0, 0, bits(scale), colour, 0, 0);
}

int sprintf_title(char* out, uint32_t format, int32_t value) {
    return title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901)(out, text_at(format), value);
}

constexpr uint32_t kEmpty = 0x0082dfeb;  // ""
constexpr uint32_t kTitleColour = 0xff282119;  // bytes 19 21 28 ff

// The two collector's-edition panels: the title text, the items from the panel texts `names` with their texts,
// the Helpbar copy at +0x38 with `help`. The setters at the original's ESP (push ecx / ebx / edi).
void setup_special_list(uint8_t* menu, uint8_t* panel, uint32_t top, uint32_t title_text, const uint32_t* names,
                        const uint32_t* item_texts, uint32_t count, uint32_t help) {
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    set_title(top, regs, menu, 0x34, 0x0082a9e0, title_text);  // "Title_SpecialMaterial"
    add_items(top, regs, menu, 0x34, names, count);
    regs.ebx = names[count - 1];
    regs.edi = field<uint32_t>(menu, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t args[1] = {item_texts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
    setup_help(top, regs, menu, 0x34, 0x38, help);
}

// The "picture" frame: its x / y (+0xb0 / +0xb4) and its centre less half of them (+0x9c / +0xa0) as integers
// from `rect` (+0, +4, +8, +0xc), hidden (+0x7c (0)); the quad at `quad` over that rectangle (z 0).
void setup_picture_quad(uint8_t* menu, uint32_t rect, uint32_t quad) {
    uint8_t* const picture = find_picture(menu, 0x0082ba40);  // "picture"
    const double half = title_global<double>(0x0083ecb0);    // 0.5
    field<int32_t>(menu, rect) = static_cast<int32_t>(static_cast<int64_t>(get(picture, 0xb0)));
    field<int32_t>(menu, rect + 4) = static_cast<int32_t>(static_cast<int64_t>(get(picture, 0xb4)));
    const long double cx = get(picture, 0x9c);
    field<int32_t>(menu, rect + 8) = static_cast<int32_t>(
        static_cast<int64_t>(cx - static_cast<long double>(field<int32_t>(menu, rect)) * half));
    const long double cy = get(picture, 0xa0);
    field<int32_t>(menu, rect + 0xc) = static_cast<int32_t>(
        static_cast<int64_t>(cy - static_cast<long double>(field<int32_t>(menu, rect + 4)) * half));
    call(picture, 0x7c, 0);
    NglQuad* const q = reinterpret_cast<NglQuad*>(menu + quad);
    nglInitQuad(q);
    const int32_t w = field<int32_t>(menu, rect), h = field<int32_t>(menu, rect + 4);
    const int32_t x = field<int32_t>(menu, rect + 8), y = field<int32_t>(menu, rect + 0xc);
    nglSetQuadRect(q, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w + x),
                   static_cast<float>(h + y));
    nglSetQuadZ(q, 0.0f);
}

}  // namespace

SETUP_GAP_ENTRY(FE_SetupLiberatorMenu, "%ecx")

void XB_CDECL FE_SetupLiberatorMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ebx / esi / edi
    field<uint8_t*>(menu, 8) = panel;
    // ammobak12..1 to +0xc, ammotop12..1 to +0x10 (every 8 bytes), then six target pictures to +0x6c..+0x80.
    for (uint32_t i = 0; i < 12; ++i)
        field<uint8_t*>(menu, 0xc + i * 8) = Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(0x0082daa4 - i * 0xc));
    for (uint32_t i = 0; i < 12; ++i)
        field<uint8_t*>(menu, 0x10 + i * 8) = Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(0x0082da14 - i * 0xc));
    // destroyer / tanker / refinery / train / drums / aagun
    static constexpr uint32_t kTargets[6] = {0x0082d984, 0x0082d97c, 0x0082d970, 0x0082d968, 0x0082d960, 0x0082d958};
    for (uint32_t i = 0; i < 6; ++i)
        field<uint8_t*>(menu, 0x6c + i * 4) = Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(kTargets[i]));
    if (field<uint8_t*>(menu, 8))
        for (uint32_t i = 0; i < 6; ++i) call(field<uint8_t*>(menu, 0x6c + i * 4), 0x34, 0);
    // tanker01 / aagun01 / drums01 / train01 / refinery01 / aagun02 / drums02 / train02 / refinery02 hidden.
    static constexpr uint32_t kHidden[9] = {0x0082d94c, 0x0082d944, 0x0082d93c, 0x0082d934, 0x0082d928,
                                            0x0082d920, 0x0082d918, 0x0082d910, 0x0082d904};
    for (const uint32_t name : kHidden) call(Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(name)), 0x34, 0);
    // background04 / 03 / 05 / 2: x (+0x9c) to +0x84.., y (+0xa0) to +0x94..
    static constexpr uint32_t kBackgrounds[4] = {0x0082dadc, 0x0082dacc, 0x0082dabc, 0x0082dab0};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* const picture = Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(kBackgrounds[i]));
        field<float>(menu, 0x84 + i * 4) = static_cast<float>(get(picture, 0x9c));
        field<float>(menu, 0x94 + i * 4) = static_cast<float>(get(picture, 0xa0));
    }
    call(Panel_FindPicture(field<uint8_t*>(menu, 8), text_at(0x0082daec)), 0x34, 0);  // "led2"
    field<uint32_t>(menu, 0xe4) = 0;
    field<uint32_t>(menu, 0xe8) = 0;
    field<uint8_t>(menu, 0xe0) = 1;
    title_function<void XB_REGS1 XB_STDCALL(uint8_t*, int32_t)>(0x00311090)(menu, 0);  // HudCompMeter_SetCount
    field<float>(menu, 0xec) = 0.0f;
    // The cross icons and the HUD manager's +0x1c (vtable +4 (panel)), at the original's ESP.
    uint8_t* const hud = title_global<uint8_t*>(0x0095f8f0);  // g_hudManager
    TitleRegs regs{};
    regs.eax = address_of(hud);
    regs.ebx = address_of(hud);
    regs.edi = field<uint32_t>(hud, 0x20);
    regs.esi = address_of(field<uint8_t*>(menu, 8));
    title_call_at_regs(top, 0x003175b0, &regs, 0, nullptr);  // FE_LiberatorCreateCrossIcons
    const uint32_t args[1] = {regs.esi};
    method_at(top, regs, field<uint8_t*>(hud, 0x1c), 4, 1, args);
}

SETUP_GAP_ENTRY(FE_SetupSpecialConceptMenu, "%esi")

void XB_CDECL FE_SetupSpecialConceptMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    // ArtDir / CharCon / LvlCon / Liberator / MiscArt / OverMap
    static constexpr uint32_t kNames[6] = {0x0082a8c8, 0x0082a8c0, 0x0082a8b8, 0x0082a8ac, 0x0082a8a4, 0x0082a89c};
    static constexpr uint32_t kItemTexts[6] = {0x0082a928, 0x0082a914, 0x0082a900, 0x0082a8f0, 0x0082a8e0, 0x0082a8d0};
    // "BONUS_CE_ARTS", "BONUS_HELPBAR_FULLSCREEN"
    setup_special_list(menu, panel, top, 0x0082aa4c, kNames, kItemTexts, 6, 0x0082b618);
    field<uint8_t*>(menu, 0x3c) = Panel_FindText(text_at(0x0082ba30), field<uint8_t*>(menu, 0x34));  // "Number"
    call(field<uint8_t*>(menu, 0x3c), 0x90, kTitleColour);
    setup_picture_quad(menu, 0x118, 0x50);
    // The full-screen quad at +0xb4.
    NglQuad* const screen = reinterpret_cast<NglQuad*>(menu + 0xb4);
    nglInitQuad(screen);
    nglSetQuadRect(screen, 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadZ(screen, 0.0f);
    field<uint8_t*>(menu, 0x128) = find_picture(menu, 0x0082c150);  // "arrowL"
    call(field<uint8_t*>(menu, 0x128), 0x7c, 0);
    field<uint8_t*>(menu, 0x12c) = find_picture(menu, 0x0082c130);  // "arrowR"
    call(field<uint8_t*>(menu, 0x12c), 0x7c, 0);
}

SETUP_GAP_ENTRY(FE_SetupSpecialMaterialsMenu, "%esi")

void XB_CDECL FE_SetupSpecialMaterialsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    // BobMovie / VetMovie / FlyThru / BehindMovie / ConceptArt
    static constexpr uint32_t kNames[5] = {0x0082a9c4, 0x0082a9b8, 0x0082a9b0, 0x0082a9a4, 0x0082baa4};
    static constexpr uint32_t kItemTexts[5] = {0x0082aa8c, 0x0082aa7c, 0x0082aa6c, 0x0082aa5c, 0x0082aa4c};
    // "BONUS_CE_TITLE", "BONUS_OP_HELPBAR"
    setup_special_list(menu, panel, top, 0x0082a9d0, kNames, kItemTexts, 5, 0x0082bb64);
    setup_picture_quad(menu, 0xcc, 0x68);
    // Title / Number to +0x4c / +0x50, both emptied (+0x78, EDI = the quad as in the original) and coloured.
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(menu + 0x68);
    static constexpr uint32_t kTexts[2] = {0x0082ba38, 0x0082ba30};
    for (uint32_t i = 0; i < 2; ++i) {
        field<uint8_t*>(menu, 0x4c + i * 4) = Panel_FindText(text_at(kTexts[i]), field<uint8_t*>(menu, 0x34));
        regs.ebx = kTexts[i];
        const uint32_t args[1] = {kEmpty};
        method_at(top, regs, field<uint8_t*>(menu, 0x4c + i * 4), 0x78, 1, args);
        call(field<uint8_t*>(menu, 0x4c + i * 4), 0x90, kTitleColour);
    }
    field<uint8_t*>(menu, 0x54) = find_picture(menu, 0x0082c150);  // "arrowL"
    call(field<uint8_t*>(menu, 0x54), 0x7c, 0);
    field<uint8_t*>(menu, 0x58) = find_picture(menu, 0x0082c130);  // "arrowR"
    call(field<uint8_t*>(menu, 0x58), 0x7c, 0);
}

// FE_SetupChapterMenu: panel in EAX, menu on the stack (ret 4). The original counts its loop in the argument slot
// (14 after the loop), then uses it as the colour local of the help bar copy; the last text call runs with the
// caller's EBX, which the entry hands over.
PLATFORM_ASM_ENTRY(FE_SetupChapterMenu_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                                              "pushl %ebx\n pushl %edx\n pushl %eax\n pushl 4(%edx)\n"
                                              PLATFORM_ASM_CALL(FE_SetupChapterMenu)
                                              "addl $16, %esp\n"
                                              "popl %esp\n ret $4")

void XB_CDECL FE_SetupChapterMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp, uint32_t caller_ebx) {
    field<uint8_t*>(menu, 0x34) = panel;
    if (!panel) return;
    uint32_t& slot = stack_word(entry_esp + 4);
    char* const name = reinterpret_cast<char*>(uintptr_t{entry_esp - 0x14});  // the original's name buffer
    const uint32_t top = entry_esp - 0x2c;  // 0x1c locals, push ebp / edi / esi / ebx
    Panel_SetLayer(panel, 2);
    field<uint8_t*>(menu, 0x18c) = Panel_FindPicture(field<uint8_t*>(menu, 0x34), text_at(0x0082ab54));  // "background"
    slot = 0;
    TitleRegs regs{};
    for (int32_t i = 0; i < 14; ++i) {
        const int32_t n = i + 1;
        const uint32_t at = uint32_t(i) * 4;
        sprintf_title(name, 0x0082ab4c, n);  // lock%i, hidden
        field<uint8_t*>(menu, 0x3c + at) = Panel_FindPicture(field<uint8_t*>(menu, 0x34), name);
        call(field<uint8_t*>(menu, 0x3c + at), 0x7c, 0);
        sprintf_title(name, 0x0082ab40, n);  // chapter%iD
        field<uint8_t*>(menu, 0x74 + at) = Panel_FindPicture(field<uint8_t*>(menu, 0x34), name);
        sprintf_title(name, 0x0082ab34, n);  // chapter%i, hidden
        field<uint8_t*>(menu, 0xac + at) = Panel_FindPicture(field<uint8_t*>(menu, 0x34), name);
        call(field<uint8_t*>(menu, 0xac + at), 0x7c, 0);
        sprintf_title(name, 0x0082ab24, n);  // chaptername%i
        field<uint8_t*>(menu, 0xe4 + at) = Panel_FindText(name, field<uint8_t*>(menu, 0x34));
        sprintf_title(name, 0x0082ab1c, n);  // title%i
        field<uint8_t*>(menu, 0x11c + at) = Panel_FindText(name, field<uint8_t*>(menu, 0x34));
        sprintf_title(name, 0x0082ab14, n);  // quote%i
        field<uint8_t*>(menu, 0x154 + at) = Panel_FindText(name, field<uint8_t*>(menu, 0x34));
        // The item from the chapter name; EBX = the buffer, EDI = n, ESI = &title[i] in the original.
        regs.ebx = address_of(name);
        regs.edi = uint32_t(n);
        regs.esi = address_of(menu) + 0x11c + at;
        const uint32_t item_args[3] = {uint32_t(i), field<uint32_t>(menu, 0xe4 + at), 0};
        method_at(top, regs, menu, 4, 3, item_args);
        sprintf_title(name, 0x0082ab74, n);  // FEMENU_QUOTE_%i as the item text
        regs.ebx = at;
        const uint32_t text_args[1] = {address_of(name)};
        method_at(top, regs, item(menu, uint32_t(i)), 0x30, 1, text_args);
        sprintf_title(name, 0x0082ab60, i);  // FEMENU_CHAPTER_%i (0-based) as the title
        method_at(top, regs, field<uint8_t*>(menu, 0x11c + at), 0x6c, 1, text_args);
        call(field<uint8_t*>(menu, 0x11c + at), 0x90, kTitleColour);
        const uint32_t empty_args[1] = {kEmpty};
        method_at(top, regs, field<uint8_t*>(menu, 0x154 + at), 0x78, 1, empty_args);
        // The item links: +0xe = the next chapter, +0xc = the previous one.
        if (i != 13) field<int16_t>(item(menu, uint32_t(i)), 0xe) = int16_t(i + 1);
        if (i != 0) field<int16_t>(item(menu, uint32_t(i)), 0xc) = int16_t(i - 1);
        slot = uint32_t(n);
    }
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
    uint8_t* const copy = copy_text_into(source, &slot);
    field<uint8_t*>(menu, 0x38) = copy;
    call(field<uint8_t*>(menu, 0x38), 0x13c, 1);
    regs.ebx = caller_ebx;
    regs.esi = address_of(source);
    regs.edi = address_of(copy);
    const uint32_t args[1] = {0x0082c37c};  // "FEMENU_OP_HELPBAR"
    method_at(entry_esp - 0x28, regs, field<uint8_t*>(menu, 0x38), 0x6c, 1, args);  // after pop ebx
}

// FE_SetupMissionSuccessFailMenu: panel in EAX, menu on the stack (ret 4); its argument slot is the colour local
// (0xffb4b4b4 at the end).
PLATFORM_ASM_ENTRY(FE_SetupMissionSuccessFailMenu_entry,
                   "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                   "pushl %edx\n pushl %eax\n pushl 4(%edx)\n" PLATFORM_ASM_CALL(FE_SetupMissionSuccessFailMenu)
                   "addl $12, %esp\n"
                   "popl %esp\n ret $4")

void XB_CDECL FE_SetupMissionSuccessFailMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    uint32_t& slot = stack_word(entry_esp + 4);
    const uint32_t top = entry_esp - 16;  // push ebx / ebp / esi / edi
    constexpr uint32_t kGrey = 0xffb4b4b4;
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    // savecontinue / continuewithoutsaving / quit (EBX = the name, ESI = the menu vtable, EDI = the panel).
    static constexpr uint32_t kNames[3] = {0x0082ac58, 0x0082ac40, 0x0082ac38};
    TitleRegs regs{};
    regs.edi = address_of(panel);
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t* const text = Panel_FindText(text_at(kNames[i]), field<uint8_t*>(menu, 0x34));
        regs.ebx = kNames[i];
        regs.esi = field<uint32_t>(menu, 0);
        const uint32_t args[3] = {i, address_of(text), 0};
        method_at(top, regs, menu, 4, 3, args);
    }
    // MissionFailed / FailedReason / BonusUnlocked to +0x40..+0x48, grey.
    static constexpr uint32_t kTexts[3] = {0x0082ac28, 0x0082ac18, 0x0082ac08};
    for (uint32_t i = 0; i < 3; ++i)
        field<uint8_t*>(menu, 0x40 + i * 4) = Panel_FindText(text_at(kTexts[i]), field<uint8_t*>(menu, 0x34));
    slot = kGrey;
    for (uint32_t i = 0; i < 3; ++i) call(field<uint8_t*>(menu, 0x40 + i * 4), 0x90, kGrey);
    field<uint8_t*>(menu, 0x50) = find_picture(menu, 0x0082c8dc);  // "whitelines", hidden
    call(field<uint8_t*>(menu, 0x50), 0x7c, 0);
    // The Helpbar copy (+0x38) and the savecontinue copy (+0x3c) with the level-loaded text.
    uint8_t* const help_source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
    field<uint8_t*>(menu, 0x38) = copy_text_into(help_source, &slot);
    call(field<uint8_t*>(menu, 0x38), 0x13c, 1);
    uint8_t* const source = Panel_FindText(text_at(kNames[0]), field<uint8_t*>(menu, 0x34));
    uint8_t* const copy = copy_text_into(source, &slot);
    field<uint8_t*>(menu, 0x3c) = copy;
    call(field<uint8_t*>(menu, 0x3c), 0x13c, 1);
    regs.ebx = 0;
    regs.esi = 1;
    regs.edi = address_of(copy);
    const uint32_t args[1] = {0x0082bcd4};  // "FEMENU_XBOX_LEVEL_LOADING_DONE"
    method_at(top, regs, field<uint8_t*>(menu, 0x3c), 0x6c, 1, args);
    // The item links: +0xa = the item below, +8 = the item above.
    field<int16_t>(item(menu, 0), 0xa) = 1;
    field<int16_t>(item(menu, 1), 0xa) = 2;
    field<int16_t>(item(menu, 1), 8) = 0;
    field<int16_t>(item(menu, 2), 8) = 1;
    // A new empty grey UIText at (320, 253), layer 2, scale 1, then item 0's scale (+0x78) as its scale (+0x60).
    uint8_t* const block = z_malloc(0x60);
    uint8_t* text = nullptr;
    if (block) {
        slot = 0;
        text = UIText_Init(block, 0, text_at(kEmpty), bits(320.0f), bits(253.0f), 0, 2, bits(1.0f), 0, 0, slot);
    }
    slot = kGrey;
    field<uint8_t*>(menu, 0x4c) = text;
    call(text, 0x90, kGrey);
    uint8_t* const first = item(menu, 0);
    const float scale = static_cast<float>(get(first, 0x78));
    using FloatMethod = void XB_THISCALL(uint8_t*, float);
    uint8_t* const target = field<uint8_t*>(menu, 0x4c);
    method<FloatMethod>(target, 0x60)(target, scale);
}

}  // namespace game
