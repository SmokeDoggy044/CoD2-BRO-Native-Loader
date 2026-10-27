#include "fe_setup_bonus.h"

#include "fe_setup_internal.h"
#include "ngl.h"

namespace game {

using namespace setup;

namespace {

constexpr uint32_t kTitleColour = 0xff282119;  // bytes 19 21 28 ff
constexpr uint32_t kHelpText = 0x0082b618;     // "BONUS_HELPBAR_FULLSCREEN"
constexpr uint32_t kNumber = 0x0082ba30;       // "Number"
constexpr uint32_t kPicture = 0x0082ba40;      // "picture"

// MSVC _ftol: truncation through int64, low half.
int32_t ftol(long double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// The panel title (+0x6c at `top`, EBX = its name; EDI as the caller set it) and its colour from a second find.
void title(uint32_t top, TitleRegs& regs, uint8_t* menu, uint32_t name, uint32_t text) {
    set_title(top, regs, menu, 0x34, name, text);
    regs.ebx = name;
}

// Items 0..count-1 from the panel texts `names`, then the item texts (+0x30, EBX = the last name, EDI = the menu
// vtable).
void items(uint32_t top, TitleRegs& regs, uint8_t* menu, const uint32_t* names, const uint32_t* texts, uint32_t count) {
    add_items(top, regs, menu, 0x34, names, count);
    regs.ebx = names[count - 1];
    regs.edi = field<uint32_t>(menu, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t args[1] = {texts[i]};
        method_at(top, regs, item(menu, i), 0x30, 1, args);
    }
}

// The pictures `names` to `offset` + i * 4, then each hidden (+0x7c (0)).
void hidden_pictures(uint8_t* menu, uint32_t offset, const uint32_t* names, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) field<uint8_t*>(menu, offset + i * 4) = find_picture(menu, names[i]);
    for (uint32_t i = 0; i < count; ++i) call(field<uint8_t*>(menu, offset + i * 4), 0x7c, 0);
}

// The Helpbar copy to +0x38 (+0x13c (1)) with the help text; EBX = the copy, EDI = the Helpbar text after it.
void help(uint32_t top, TitleRegs& regs, uint8_t* menu, uint32_t text) {
    uint8_t* const source = Panel_FindText(text_at(kHelpbar), field<uint8_t*>(menu, 0x34));
    uint8_t* const copy = copy_text(source, 0);
    field<uint8_t*>(menu, 0x38) = copy;
    call(field<uint8_t*>(menu, 0x38), 0x13c, 1);
    regs.ebx = address_of(copy);
    regs.edi = address_of(source);
    const uint32_t args[1] = {text};
    method_at(top, regs, field<uint8_t*>(menu, 0x38), 0x6c, 1, args);
}

// A panel text to `offset` with its text (+0x6c at `top`, EBX = its name) and the title colour.
void named_text(uint32_t top, TitleRegs& regs, uint8_t* menu, uint32_t offset, uint32_t name, uint32_t text) {
    uint8_t* const found = Panel_FindText(text_at(name), field<uint8_t*>(menu, 0x34));
    field<uint8_t*>(menu, offset) = found;
    regs.ebx = name;
    const uint32_t args[1] = {text};
    method_at(top, regs, found, 0x6c, 1, args);
    call(field<uint8_t*>(menu, offset), 0x90, kTitleColour);
}

// The Number text to `offset` with the title colour.
void number(uint8_t* menu, uint32_t offset, TitleRegs& regs) {
    uint8_t* const found = Panel_FindText(text_at(kNumber), field<uint8_t*>(menu, 0x34));
    regs.ebx = kNumber;
    field<uint8_t*>(menu, offset) = found;
    call(found, 0x90, kTitleColour);
}

// A copy of the panel text `name` with its colour, scale, y, x and kind (ECX 0x40, EDX 0x10) to `offset`, +0x13c
// (6), the wrapped text (+0x140 (text, 300, -1.0) at `top`, EBX = the copy, EDI = the source) and the title colour.
void info_text(uint32_t top, TitleRegs& regs, uint8_t* menu, uint32_t offset, uint32_t name, uint32_t text) {
    uint8_t* const source = Panel_FindText(text_at(name), field<uint8_t*>(menu, 0x34));
    uint8_t* const block = z_malloc(0x98);
    uint8_t* copy = nullptr;
    if (block) {
        uint32_t local = 0;
        const uint32_t colour = *method<ColourMethod>(source, 0xb4)(source, &local);
        const float scale = static_cast<float>(get(source, 0xa4));
        const float y = static_cast<float>(get(source, 0xcc));
        const float x = static_cast<float>(get(source, 0xc8));
        const int32_t kind = method<IntMethod>(source, 0xb0)(source);
        copy = UIText_Construct(block, kind, bits(x), bits(y), 0, 0, bits(scale), colour, 0x40, 0x10);
    }
    field<uint8_t*>(menu, offset) = copy;
    call(copy, 0x13c, 6);
    regs.ebx = address_of(block);
    regs.edi = address_of(source);
    const uint32_t args[3] = {text, 0x12c, 0xbf800000};
    method_at(top, regs, field<uint8_t*>(menu, offset), 0x140, 3, args);
    call(field<uint8_t*>(menu, offset), 0x90, kTitleColour);
}

// The "picture" frame: width / height to `fields` / +4 (+0xb0 / +0xb4 truncated; the materials panel: width + 100
// and half of it), x / y (+0x9c / +0xa0 minus half the size) to +8 / +0xc, the picture hidden, and the quad at
// `quad` over it (z 0).
void picture_frame(uint8_t* menu, uint32_t fields, uint32_t quad, bool materials) {
    uint8_t* const picture = find_picture(menu, kPicture);
    if (materials) {
        const int32_t width = ftol(get(picture, 0xb0) + 100.0f);
        field<int32_t>(menu, fields) = width;
        field<int32_t>(menu, fields + 4) = ftol(static_cast<long double>(width) * 0.5L);
    } else {
        field<int32_t>(menu, fields) = ftol(get(picture, 0xb0));
        field<int32_t>(menu, fields + 4) = ftol(get(picture, 0xb4));
    }
    field<int32_t>(menu, fields + 8) =
        ftol(get(picture, 0x9c) - static_cast<long double>(field<int32_t>(menu, fields)) * 0.5L);
    field<int32_t>(menu, fields + 12) =
        ftol(get(picture, 0xa0) - static_cast<long double>(field<int32_t>(menu, fields + 4)) * 0.5L);
    call(picture, 0x7c, 0);
    NglQuad* const q = reinterpret_cast<NglQuad*>(menu + quad);
    nglInitQuad(q);
    const int32_t width = field<int32_t>(menu, fields);
    const int32_t height = field<int32_t>(menu, fields + 4);
    const int32_t x = field<int32_t>(menu, fields + 8);
    const int32_t y = field<int32_t>(menu, fields + 12);
    nglSetQuadRect(q, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width + x),
                   static_cast<float>(height + y));
    nglSetQuadZ(q, 0.0f);
}

// A plain quad at `quad`: the rectangle, an optional colour, z.
void plain_quad(uint8_t* menu, uint32_t quad, float x0, float y0, float x1, float y1, bool black, float z) {
    NglQuad* const q = reinterpret_cast<NglQuad*>(menu + quad);
    nglInitQuad(q);
    nglSetQuadRect(q, x0, y0, x1, y1);
    if (black) nglSetQuadColor(q, 0xff000000);
    nglSetQuadZ(q, z);
}

// The ten "New_Stamp" style pictures the vehicles / weapons / characters panels share (from +0x44 / +0x50).
constexpr uint32_t kStamps[10] = {0x0082ba98, 0x0082b8a8, 0x0082b89c, 0x0082b890, 0x0082b884,
                                  0x0082b878, 0x0082b86c, 0x0082b650, 0x0082b644, 0x0082b634};

}  // namespace

SETUP_GAP_ENTRY(FE_SetupBonusConceptsMenu, "%ecx")

void XB_CDECL FE_SetupBonusConceptsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 0x34;  // 0x24 locals, push ebx / ebp / esi / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    title(top, regs, menu, 0x0082b3a0, 0x0082bb9c);
    static constexpr uint32_t kNames[13] = {0x0082b394, 0x0082b384, 0x0082b36c, 0x0082b358, 0x0082b344,
                                            0x0082b338, 0x0082b324, 0x0082b314, 0x0082b300, 0x0082b2f4,
                                            0x0082b2e0, 0x0082b2d0, 0x0082b2bc};  // Select_...
    add_items(top, regs, menu, 0x34, kNames, 13);
    // Item i's text BONUS_BONUS_LEVEL_<i+1> (+0x30 with the original's stack buffer: the setter keeps its
    // address), then New_Stamp_<i+1> to +0x48 + i * 4, hidden.
    auto sprintf = title_function<int XB_CDECL(char*, const char*, int32_t)>(0x001ee901);
    char* const buffer = reinterpret_cast<char*>(uintptr_t{top + 0x14});
    for (int32_t i = 1; i <= 13; ++i) {
        sprintf(buffer, text_at(0x0082b3b0), i);
        regs.ebx = uint32_t(i - 1) * 4;
        regs.edi = uint32_t(i);
        const uint32_t args[1] = {address_of(buffer)};
        method_at(top, regs, item(menu, uint32_t(i - 1)), 0x30, 1, args);
        sprintf(buffer, text_at(0x0082b2ac), i);
        uint8_t* const stamp = Panel_FindPicture(field<uint8_t*>(menu, 0x34), buffer);
        field<uint8_t*>(menu, 0x48 + uint32_t(i - 1) * 4) = stamp;
        call(stamp, 0x7c, 0);
    }
    help(top, regs, menu, kHelpText);
    number(menu, 0x3c, regs);
    picture_frame(menu, 0x1fc, 0xd0, false);
    plain_quad(menu, 0x134, 0.0f, -70.0f, 640.0f, 600.0f, false, 0.0f);
    plain_quad(menu, 0x198, 0.0f, 0.0f, 640.0f, 480.0f, true, 1.0f);
}

SETUP_GAP_ENTRY(FE_SetupBonusVehiclesMenu, "%ecx")

void XB_CDECL FE_SetupBonusVehiclesMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 16;  // push ecx / ebx / esi / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    title(top, regs, menu, 0x0082b4c4, 0x0082bba8);
    // land / air / sea / artillery / antiAir / rocket
    static constexpr uint32_t kNames[6] = {0x0082b4bc, 0x0082b4b8, 0x0082b4b4, 0x0082b4a8, 0x0082b4a0, 0x0082b498};
    static constexpr uint32_t kTexts[6] = {0x0082b538, 0x0082b528, 0x0082b4e8, 0x0082b500, 0x0082b518, 0x0082b4d4};
    items(top, regs, menu, kNames, kTexts, 6);
    hidden_pictures(menu, 0x50, kStamps, 6);
    help(top, regs, menu, kHelpText);
    named_text(top, regs, menu, 0x3c, 0x0082b488, 0x0082b474);  // "Vehicles_Title", "BONUS_VEH_1_1_NAME"
    number(menu, 0x40, regs);
    info_text(top, regs, menu, 0x44, 0x0082b464, 0x0082b450);  // "Vehicles_Info", "BONUS_VEH_1_1_INFO"
    picture_frame(menu, 0x1c4, 0x98, false);
    plain_quad(menu, 0xfc, 100.0f, 40.0f, 550.0f, 430.0f, false, 0.0f);
    plain_quad(menu, 0x160, 0.0f, 0.0f, 640.0f, 480.0f, true, 1.0f);
}

SETUP_GAP_ENTRY(FE_SetupBonusWeaponsMenu, "%ecx")

void XB_CDECL FE_SetupBonusWeaponsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 16;  // push ecx / ebx / esi / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    title(top, regs, menu, 0x0082b6b0, 0x0082bbb8);
    static constexpr uint32_t kNames[10] = {0x0082b6ac, 0x0082b6a4, 0x0082b6a0, 0x0082b698, 0x0082b690,
                                            0x0082b688, 0x0082b680, 0x0082b674, 0x0082b668, 0x0082b65c};
    static constexpr uint32_t kTexts[10] = {0x0082b77c, 0x0082b768, 0x0082b758, 0x0082b744, 0x0082b730,
                                            0x0082b71c, 0x0082b708, 0x0082b6f0, 0x0082b6d8, 0x0082b6c0};
    items(top, regs, menu, kNames, kTexts, 10);
    hidden_pictures(menu, 0x50, kStamps, 10);
    help(top, regs, menu, kHelpText);
    named_text(top, regs, menu, 0x3c, 0x0082b608, 0x0082b5f4);  // "Weapons_Title", "BONUS_WEAP_1_1_NAME"
    number(menu, 0x40, regs);
    info_text(top, regs, menu, 0x44, 0x0082b5e4, 0x0082b5d0);  // "Weapons_Info", "BONUS_WEAP_1_1_INFO"
    picture_frame(menu, 0x184, 0xbc, false);
    plain_quad(menu, 0x120, 0.0f, 0.0f, 640.0f, 480.0f, false, 0.0f);
}

SETUP_GAP_ENTRY(FE_SetupBonusCharactersMenu, "%esi")

void XB_CDECL FE_SetupBonusCharactersMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    title(top, regs, menu, 0x0082b91c, 0x0082bbc8);
    static constexpr uint32_t kNames[7] = {0x0082b908, 0x0082b8f8, 0x0082b8e8, 0x0082b8e0,
                                           0x0082b8d4, 0x0082b8c4, 0x0082b8b4};
    static constexpr uint32_t kTexts[7] = {0x0082b9a8, 0x0082b994, 0x0082b980, 0x0082b96c,
                                           0x0082b958, 0x0082b944, 0x0082b930};
    items(top, regs, menu, kNames, kTexts, 7);
    hidden_pictures(menu, 0x44, kStamps, 7);
    help(top, regs, menu, 0x0082b854);                          // "BONUS_HELPBAR_BACK_ONLY"
    named_text(top, regs, menu, 0x68, 0x0082b844, 0x0082b830);  // "Character_Name", "BONUS_CHAR_1_NAME"
    info_text(top, regs, menu, 0x6c, 0x0082b820, 0x0082b80c);   // "Character_Stats", "BONUS_CHAR_1_STATS"
    info_text(top, regs, menu, 0x70, 0x0082b7fc, 0x0082b7e8);   // "Character_Bio", "BONUS_CHAR_1_BIO"
    picture_frame(menu, 0xe4, 0x80, false);
}

SETUP_GAP_ENTRY(FE_SetupBonusMaterialsMenu, "%esi")

void XB_CDECL FE_SetupBonusMaterialsMenu(uint8_t* menu, uint8_t* panel, uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // push ecx / ebx / edi
    field<uint8_t*>(menu, 0x34) = panel;
    Panel_SetLayer(panel, 2);
    TitleRegs regs{};
    regs.esi = address_of(menu);
    regs.edi = address_of(panel);
    title(top, regs, menu, 0x0082baec, 0x0082bae0);
    static constexpr uint32_t kNames[6] = {0x0082bad8, 0x0082bad0, 0x0082bac4, 0x0082babc, 0x0082bab0, 0x0082baa4};
    static constexpr uint32_t kTexts[6] = {0x0082bbec, 0x0082bbdc, 0x0082bbc8, 0x0082bbb8, 0x0082bba8, 0x0082bb9c};
    items(top, regs, menu, kNames, kTexts, 6);
    // New_Stamp_1 / _movie / _02 .. _05 to +0x48, hidden.
    static constexpr uint32_t kMaterialStamps[6] = {0x0082ba98, 0x0082ba88, 0x0082ba78,
                                                    0x0082ba68, 0x0082ba58, 0x0082ba48};
    hidden_pictures(menu, 0x48, kMaterialStamps, 6);
    help(top, regs, menu, 0x0082bb64);
    picture_frame(menu, 0xf8, 0x94, true);
    // Title / Number to +0x70 / +0x74: empty text (+0x78) and the title colour.
    static constexpr struct {
        uint32_t offset, name;
    } kLabels[2] = {{0x70, 0x0082ba38}, {0x74, kNumber}};
    regs.edi = address_of(menu + 0x94);
    for (const auto& label : kLabels) {
        uint8_t* const found = Panel_FindText(text_at(label.name), field<uint8_t*>(menu, 0x34));
        field<uint8_t*>(menu, label.offset) = found;
        regs.ebx = label.name;
        const uint32_t args[1] = {0x0082dfeb};  // ""
        method_at(top, regs, found, 0x78, 1, args);
        call(field<uint8_t*>(menu, label.offset), 0x90, kTitleColour);
    }
    field<uint8_t*>(menu, 0x68) = find_picture(menu, 0x0082c150);
    call(field<uint8_t*>(menu, 0x68), 0x7c, 0);
    field<uint8_t*>(menu, 0x6c) = find_picture(menu, 0x0082c130);
    call(field<uint8_t*>(menu, 0x6c), 0x7c, 0);
    field<uint32_t>(menu, 0x78) = 2;
}

}  // namespace game
