#include "fe_special.h"

#include <cstring>

#include "cl.h"
#include "fe.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"
#include "movie.h"
#include "snd.h"
#include "qcommon_core.h"

// The gap entries (see fe_bonus.cpp): the C frame moves below a gap; the C version gets the menu, the entry
// EBX / EDI / ESI and the entry ESP.
#define SPECIAL_GAP_ENTRY(name, ret)                                                                           \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x30000") "pushl %edx\n"                \
                                     "pushl %edx\n pushl %esi\n pushl %edi\n pushl %ebx\n"                     \
                                     "pushl %ecx\n" PLATFORM_ASM_CALL(name) "addl $20, %esp\n"                 \
                                     "popl %esp\n" ret)
// The button handlers (ret 4: the pad) and the RestoreSelection slot (no stack argument).
#define SPECIAL_HANDLER_ENTRY(name) SPECIAL_GAP_ENTRY(name, "ret $4")
#define SPECIAL_PLAIN_ENTRY(name) SPECIAL_GAP_ENTRY(name, "ret")

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

int32_t selection(uint8_t* menu) { return field<int16_t>(menu, 0x22); }

void z_free(void* p) { own_function<void XB_CDECL(void*)>(&Z_Free)(p); }  // Z_Free

// A title object's vtable slot (byte offset).
template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using VoidMethod = void XB_THISCALL(uint8_t*);
using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);
using PairMethod = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
using FloatMethod = void XB_THISCALL(uint8_t*, float);
using PositionMethod = void XB_THISCALL(uint8_t*, float, float);
using GetMethod = long double XB_THISCALL(uint8_t*);  // an x87 result

void call(uint8_t* object, uint32_t offset) { method<VoidMethod>(object, offset)(object); }
void call(uint8_t* object, uint32_t offset, uint32_t a) { method<WordMethod>(object, offset)(object, a); }
void call(uint8_t* object, uint32_t offset, uint32_t a, uint32_t b) {
    method<PairMethod>(object, offset)(object, a, b);
}
long double get(uint8_t* object, uint32_t offset) { return method<GetMethod>(object, offset)(object); }

constexpr uint32_t kOne = 0x3f800000;  // 1.0f

// Title calls at the original's ESP `top` (before the call's pushes) with the original's registers.
struct Calls {
    uint32_t top;
    TitleRegs regs;

    uint32_t run(uint32_t function, uint32_t count = 0, const uint32_t* args = nullptr) const {
        return title_call_at_regs(top, function, &regs, count, args);
    }
    // EAX = value (the FE_BonusCE* load image helpers).
    void run_eax(uint32_t function, uint32_t eax) const {
        TitleRegs r = regs;
        r.eax = eax;
        title_call_at_regs(top, function, &r, 0, nullptr);
    }
    void method_args(uint8_t* object, uint32_t slot, uint32_t count, const uint32_t* args) const {
        TitleRegs r = regs;
        r.ecx = address_of(object);
        title_call_at_regs(top, field<uint32_t>(field<uint8_t*>(object, 0), slot), &r, count, args);
    }
    void method(uint8_t* object, uint32_t slot) const { method_args(object, slot, 0, nullptr); }
    void method(uint8_t* object, uint32_t slot, uint32_t a) const {
        const uint32_t args[1] = {a};
        method_args(object, slot, 1, args);
    }
    void method(uint8_t* object, uint32_t slot, uint32_t a, uint32_t b) const {
        const uint32_t args[2] = {a, b};
        method_args(object, slot, 2, args);
    }
};

// The registers after the usual `push esi; mov esi, ecx` prologue: ESI = the menu, the rest as at the entry.
Calls after_push_esi(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp) {
    Calls c{entry_esp - 4, TitleRegs{}};
    c.regs.ebx = ebx;
    c.regs.ecx = address_of(menu);
    c.regs.esi = address_of(menu);
    c.regs.edi = edi;
    return c;
}


// The concept menu's current item of a category (1-based).
int32_t& concept_item(uint8_t* menu, int32_t category) {
    return field<int32_t>(menu, 0x130 + uint32_t(category) * 4);
}

// The concept menu's OnDown / OnUp: `push esi`, then `push edi` (EDI = the old row) around the move only.
void concept_move(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot) {
    if (menu[0x40]) return;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    Calls inner = c;
    inner.top -= 4;  // push edi
    const int32_t old = selection(menu);
    inner.regs.edi = uint32_t(old);
    inner.method(menu, slot);
    const int32_t row = selection(menu);
    if (row == old) return;
    if (concept_item(menu, row) == 0) concept_item(menu, row) = 1;
    c.run_eax(own_address(&FE_BonusCELoadImage_entry), address_of(menu));
}

// The materials menu's OnDown / OnUp: `push esi; push edi` (EDI = the old row); the image when the row changed.
void materials_move(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot) {
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    const int32_t old = selection(menu);
    c.regs.edi = uint32_t(old);
    c.method(menu, slot);
    if (selection(menu) == old) return;
    c.run_eax(own_address(&FE_BonusCEMainLoadImage_entry), address_of(menu));
}

void init_menu(uint8_t* menu, int32_t rows, uint8_t* owner) { UIMenu_Init(menu, rows, owner, 0x140, 0xf0, 8, 0); }

void delete_object(uint8_t* object) { call(object, 0, 1); }

void free_menu(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

void delete_panel_at(uint8_t* menu, uint32_t offset) {
    if (uint8_t* const panel = field<uint8_t*>(menu, offset)) Panel_Delete(panel);
}

void delete_if_set(uint8_t* menu, uint32_t offset) {
    if (uint8_t* const object = field<uint8_t*>(menu, offset)) delete_object(object);
}

// The image release of the OnLeave handlers: texture, file, buffer.
void release_image(uint8_t* menu, uint32_t file_offset, uint32_t buffer_offset, uint32_t texture_offset,
                   bool clear_texture) {
    nglResetDeviceBindings();
    if (void* const texture = field<void*>(menu, texture_offset)) nglReleaseTexture(texture);
    const int32_t file = field<int32_t>(menu, file_offset);
    if (clear_texture) field<uint32_t>(menu, texture_offset) = 0;
    if (file != -1) {
        own_function<void XB_CDECL(int32_t)>(&File_Close)(file);  // File_Close
        if (void* const buffer = field<void*>(menu, buffer_offset)) Mem_AlignedFree(buffer);
    }
    field<uint32_t>(menu, buffer_offset) = 0;
}

}  // namespace

// --- Destructors ---

void XB_REGS1 XB_CDECL SinglePlayerMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x00829410;
    delete_panel_at(menu, 0x38);
    delete_if_set(menu, 0x34);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL DifficultyMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x00828c50;
    delete_panel_at(menu, 0x34);
    delete_if_set(menu, 0x38);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL ChapterMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x008299f0;
    delete_panel_at(menu, 0x34);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL SpecialMaterialsMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x00828ac0;
    delete_panel_at(menu, 0x34);
    delete_if_set(menu, 0x38);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL SpecialConceptMenu_Dtor(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x00829ed8;
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) {
        Panel_Delete(panel);
        field<uint32_t>(menu, 0x34) = 0;
    }
    if (uint8_t* const help = field<uint8_t*>(menu, 0x38)) {
        delete_object(help);
        field<uint32_t>(menu, 0x38) = 0;
    }
    free_menu(menu);
}

uint8_t* XB_THISCALL SinglePlayerMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    SinglePlayerMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL DifficultyMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    DifficultyMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL ChapterMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    ChapterMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL SpecialMaterialsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    SpecialMaterialsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL SpecialConceptMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    SpecialConceptMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

// --- Constructors ---

uint8_t* XB_REGS3 XB_CDECL XboxLiveMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 3, owner);
    menu[0x2a] |= 0x80;
    menu[0x40] = 0;
    field<uint32_t>(menu, 0x44) = 0;
    menu[0x48] = 0;
    menu[0x49] = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0) = 0x0083c7e0;  // XboxLiveMenu vtable
    field<int32_t>(menu, 0x3c) = -1;
    menu[0x33] = 5;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL SpecialConceptMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 6, owner);
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x128) = 0;
    field<uint32_t>(menu, 0x12c) = 0;
    menu[0x40] = 0;
    field<uint32_t>(menu, 0x48) = 0;
    field<uint32_t>(menu, 0x4c) = 0;
    field<uint32_t>(menu, 0) = 0x00829ed8;  // SpecialConceptMenu vtable
    menu[0x33] = 5;
    field<int32_t>(menu, 0x44) = -1;
    for (uint32_t i = 0; i < 6; ++i) field<uint32_t>(menu, 0x130 + i * 4) = 0;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL SpecialMaterialsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 5, owner);
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x4c) = 0;
    field<uint32_t>(menu, 0x50) = 0;
    field<uint32_t>(menu, 0x54) = 0;
    field<uint32_t>(menu, 0x58) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<uint32_t>(menu, 0x44) = 0;
    field<uint32_t>(menu, 0x48) = 0;
    field<uint32_t>(menu, 0x60) = 0;
    field<uint32_t>(menu, 0x64) = 0;
    field<uint32_t>(menu, 0) = 0x00828ac0;  // SpecialMaterialsMenu vtable
    menu[0x33] = 5;
    field<int32_t>(menu, 0x5c) = -1;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL SinglePlayerMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 5, owner);
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    menu[0x44] = 0;
    field<uint32_t>(menu, 0x48) = 0;
    field<uint32_t>(menu, 0) = 0x00829410;  // SinglePlayerMenu vtable
    menu[0x33] = 5;
    field<float>(menu, 0x40) = 0.0f;
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL ChapterMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIGridMenu_Init(menu, 0xe, owner, 0);
    menu[0x2a] |= 0x80;
    field<uint32_t>(menu, 0x19c) = title_global<uint32_t>(0x0083066c);  // 0.3
    field<uint32_t>(menu, 0x1a0) = title_global<uint32_t>(0x0083ec64);  // 320.0
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x18c) = 0;
    field<uint32_t>(menu, 0x190) = 0;
    field<uint32_t>(menu, 0x194) = 0;
    menu[0x1a8] = 0;
    field<uint32_t>(menu, 0x1ac) = 0;
    menu[0x1b0] = 0;
    field<uint32_t>(menu, 0x1b4) = 0;
    field<uint32_t>(menu, 0) = 0x008299f0;  // ChapterMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x1a4) = title_global<uint32_t>(0x00830f60);  // 1.2
    return menu;
}

// --- Xbox Live / single player ---

void XB_THISCALL XboxLiveMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    if (uint8_t* const text = field<uint8_t*>(menu, 0x38)) call(text, 0x3c);
    if (!title_global<uint8_t>(0x0095f916)) {
        if (uint8_t* const movie = title_global<uint8_t*>(0x0095f454)) {
            call(movie, 8);
            Subtitle_Draw();
            const uint32_t texture = own_function<uint32_t XB_THISCALL(uint8_t*)>(&Movie_GetFrameTexture)(
                title_global<uint8_t*>(0x0095f454));  // Movie_GetFrameTexture
            uint8_t* const quad = title_global<uint8_t*>(0x0095f458);
            field<uint32_t>(quad, 0x60) = texture;
            nglListAddQuad(reinterpret_cast<const NglQuad*>(quad));
        }
    }
    UIMenu_Draw(menu);
}

void XB_THISCALL SinglePlayerMenu_Update(uint8_t* menu, float dt) {
    SCR_LoadingPump();
    if (title_global<uint8_t>(0x0095f91e)) return;
    UIMenu_Update(menu, dt);
    menu[0x44] = 0;
}

// --- Chapters ---

PLATFORM_ASM_ENTRY(ChapterMenu_SlideStep_entry, "pushl 4(%esp)\n"
                                                "pushl %edi\n" PLATFORM_ASM_CALL(ChapterMenu_SlideStep) "addl $8, %esp\n"
                                                "ret $4")

void XB_CDECL ChapterMenu_SlideStep(uint8_t* menu, float dt) {
    auto chapter_element = [menu](uint32_t base, uint32_t index) {
        return field<uint8_t*>(menu, base + uint32_t(field<int32_t>(menu, index)) * 4);
    };
    auto bits = [](float value) {
        uint32_t word;
        std::memcpy(&word, &value, 4);
        return word;
    };
    const float x = float(get(chapter_element(0x74, 0x190), 0x9c));
    // SSE: the slide's progress and this step's shift (span = 320 - the start x +0x198, duration +0x19c).
    const float span = title_global<float>(0x0083ec64) - field<float>(menu, 0x198);
    const float moved = x - field<float>(menu, 0x198);
    const float step = dt / field<float>(menu, 0x19c);
    float progress = moved / span + step;
    float shift = span * step;
    const float next = float(get(chapter_element(0x74, 0x190), 0x9c) + platform_x87_ld(shift));
    bool arrived = false;
    if (platform_x87_greater(next, title_global<float>(0x0083ec64)) && next > field<float>(menu, 0x198))
        arrived = true;
    else if (title_global<float>(0x0083ec64) > next && field<float>(menu, 0x198) > next)
        arrived = true;
    if (arrived) {  // past 320: the rest of the way, progress 1
        const long double now = get(chapter_element(0x74, 0x190), 0x9c);
        progress = title_global<float>(0x0083eb20);
        shift = float(platform_x87_ld(title_global<float>(0x0083ec64)) - now);
        menu[0x1a8] = 0;
    }
    for (uint32_t i = 0; i < 0xe; ++i) {
        static const uint32_t kPlaced[3] = {0x3c, 0x74, 0xac};  // x += shift (+0xa0 y first, +0x9c x, +0x8c set)
        for (uint32_t offset : kPlaced) {
            uint8_t* const element = field<uint8_t*>(menu, offset + i * 4);
            const float y = float(get(element, 0xa0));
            const float new_x = float(get(element, 0x9c) + platform_x87_ld(shift));
            method<PositionMethod>(element, 0x8c)(element, new_x, y);
        }
        static const uint32_t kMoved[3] = {0xe4, 0x11c, 0x154};  // vtable +0x70 (shift, 0)
        for (uint32_t offset : kMoved) call(field<uint8_t*>(menu, offset + i * 4), 0x70, bits(shift), 0);
    }
    // The selected chapter (+0x190) fades in and grows towards +0x1a4, the previous one (+0x194) the other way (x87,
    // 1.0 as a double).
    const double one = title_global<double>(0x0083eb10);
    call(chapter_element(0xac, 0x190), 0x7c, bits(progress));
    call(chapter_element(0xac, 0x190), 0x64, bits(float((platform_x87_ld(field<float>(menu, 0x1a4)) - one) * progress + one)));
    call(chapter_element(0x74, 0x190), 0x64, bits(float((platform_x87_ld(field<float>(menu, 0x1a4)) - one) * progress + one)));
    const long double rest = platform_x87_ld(one) - progress;
    const double rest_stored = double(rest);
    call(chapter_element(0xac, 0x194), 0x7c, bits(float(rest)));
    call(chapter_element(0xac, 0x194), 0x64, bits(float((platform_x87_ld(field<float>(menu, 0x1a4)) - one) * rest_stored + one)));
    call(chapter_element(0x74, 0x194), 0x64, bits(float((platform_x87_ld(field<float>(menu, 0x1a4)) - one) * rest_stored + one)));
}

PLATFORM_ASM_ENTRY(ChapterMenu_UpdateList_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(ChapterMenu_UpdateList) "addl $4, %esp\n"
                   "ret")

void XB_CDECL ChapterMenu_UpdateList(uint8_t* menu) {
    if (!field<uint8_t*>(menu, 0x34)) return;
    int32_t& unlocked = title_global<int32_t>(0x008dce44);
    int32_t& current = title_global<int32_t>(0x008dce48);
    if (unlocked > 0xd) unlocked = 0xd;
    if (current > 0xd) current = 0xd;
    if (field<int16_t>(menu, 0x22) > 0xd) field<int16_t>(menu, 0x22) = 0xd;
    if (field<int32_t>(menu, 0x1ac) > 0xd) field<int32_t>(menu, 0x1ac) = 0xd;
    for (int32_t i = 0; i < 0xe; ++i) {
        uint8_t* const lock = field<uint8_t*>(menu, 0x3c + uint32_t(i) * 4);
        uint32_t flag = 0;
        if (title_global<int32_t>(0x008dce44) < i && i != 0) {
            call(lock, 0x7c, kOne);
            flag = 1;
        } else {
            call(lock, 0x7c, 0);
        }
        call(field<uint8_t**>(menu, 4)[i], 0x20, flag);
    }
    int32_t& chosen = field<int32_t>(menu, 0x190);
    chosen = field<int32_t>(menu, 0x1ac);
    if (chosen < 0) chosen = 0;
    if (chosen > 0xd) chosen = 0xd;
    field<int32_t>(menu, 0x194) = chosen;
    const long double x = get(field<uint8_t*>(menu, 0x74 + uint32_t(chosen) * 4), 0x9c);
    field<float>(menu, 0x198) = float(x);
    const float offset = float(platform_x87_ld(title_global<float>(0x0083ec64)) - x);  // 320 - x (x87)
    for (uint32_t i = 0; i < 0xe; ++i) {
        // Placed by x, y (+0xa0 y first, +0x9c x, +0x8c set).
        static const uint32_t kPlaced[3] = {0x3c, 0x74, 0xac};
        for (uint32_t offset_xy : kPlaced) {
            uint8_t* const element = field<uint8_t*>(menu, offset_xy + i * 4);
            const float y = float(get(element, 0xa0));
            const float new_x = float(get(element, 0x9c) + platform_x87_ld(offset));
            method<PositionMethod>(element, 0x8c)(element, new_x, y);
        }
        // Placed by one coordinate (+0xc8 get, +0x80 set).
        static const uint32_t kShifted[3] = {0xe4, 0x11c, 0x154};
        for (uint32_t offset_one : kShifted) {
            uint8_t* const element = field<uint8_t*>(menu, offset_one + i * 4);
            method<FloatMethod>(element, 0x80)(element, float(get(element, 0xc8) + platform_x87_ld(offset)));
        }
        call(field<uint8_t*>(menu, 0xac + i * 4), 0x64, kOne);
        call(field<uint8_t*>(menu, 0x74 + i * 4), 0x64, kOne);
    }
    uint8_t* const selected = field<uint8_t*>(menu, 0xac + uint32_t(field<int32_t>(menu, 0x190)) * 4);
    call(selected, 0x7c, kOne);
    call(field<uint8_t*>(menu, 0xac + uint32_t(field<int32_t>(menu, 0x190)) * 4), 0x64, field<uint32_t>(menu, 0x1a4));
    call(field<uint8_t*>(menu, 0x74 + uint32_t(field<int32_t>(menu, 0x190)) * 4), 0x64, field<uint32_t>(menu, 0x1a4));
}

void XB_THISCALL ChapterMenu_OnDown(uint8_t* menu, uint32_t) {
    if (!menu[0x1b0]) return;
    const int32_t state = field<int32_t>(menu, 0x1b4);
    if (state == 2 || state == 3) field<int32_t>(menu, 0x1b4) = state + 1;
}

void XB_THISCALL ChapterMenu_OnUp(uint8_t* menu, uint32_t) {
    if (!menu[0x1b0]) return;
    const int32_t state = field<int32_t>(menu, 0x1b4);
    if (state == 0 || state == 1) field<int32_t>(menu, 0x1b4) = state + 1;
}

void XB_THISCALL ChapterMenu_OnButton100(uint8_t* menu, uint32_t) {
    if (!menu[0x1b0]) return;
    const int32_t state = field<int32_t>(menu, 0x1b4);
    if (state == 8 || state == 0xa) {
        field<int32_t>(menu, 0x1b4) = state + 1;
        return;
    }
    if (state != 0xc) return;
    title_global<int32_t>(0x008dce44) = 0xe;
    title_global<uint8_t>(0x008dce54) = 1;
    for (uint32_t i = 0; i < 0xe; ++i) {
        call(field<uint8_t*>(menu, 0x3c + i * 4), 0x7c, 0);
        call(field<uint8_t**>(menu, 4)[i], 0x20, 0);
    }
}

namespace {

// After a grid move that changed the chapter.
void chapter_moved(uint8_t* menu, int32_t row, int32_t old) {
    field<int32_t>(menu, 0x190) = row;
    field<int32_t>(menu, 0x194) = old;
    menu[0x1a8] = 1;
    field<float>(menu, 0x198) = float(get(field<uint8_t*>(menu, 0x74 + uint32_t(row) * 4), 0x9c));
    call(menu, 0x10);
}

}  // namespace

void XB_THISCALL ChapterMenu_OnRight(uint8_t* menu, uint32_t) {
    if (menu[0x1b0]) {
        const int32_t state = field<int32_t>(menu, 0x1b4);
        if (state == 6 || state == 7 || state == 9 || state == 0xb) field<int32_t>(menu, 0x1b4) = state + 1;
    }
    const int16_t current = field<int16_t>(menu, 0x22);
    if (current == 0xd) return;
    const int32_t old = current;
    UIGridMenu_MoveRight(menu);
    const int32_t row = selection(menu);
    if (row != old) chapter_moved(menu, row, old);
}

void XB_THISCALL ChapterMenu_OnLeft(uint8_t* menu, uint32_t) {
    if (menu[0x1b0]) {
        const int32_t state = field<int32_t>(menu, 0x1b4);
        if (state == 4 || state == 5) field<int32_t>(menu, 0x1b4) = state + 1;
    }
    const int16_t current = field<int16_t>(menu, 0x22);
    if (current == 0) return;
    const int32_t old = current;
    UIGridMenu_MoveLeft(menu);
    const int32_t row = selection(menu);
    if (row != old) chapter_moved(menu, row, old);
}

// Both triggers (pad buttons 9 and 8) held arm the button sequence; letting go resets it.
void XB_THISCALL ChapterMenu_Update(uint8_t* menu, float dt) {
    if (!title_global<uint8_t>(0x0095f91e)) {
        if (!menu[0x1a8])
            UIMenu_Update(menu, dt);
        else
            ChapterMenu_SlideStep(menu, dt);
    }
    bool held = false;
    int32_t pad = title_global<int32_t>(0x008d50d8);
    IN_PadCreate();
    if (IN_PadButtonDown(pad, 9)) {
        pad = title_global<int32_t>(0x008d50d8);
        IN_PadCreate();
        held = IN_PadButtonDown(pad, 8) != 0;
    }
    if (held) {
        menu[0x1b0] = 1;
    } else {
        menu[0x1b0] = 0;
        field<int32_t>(menu, 0x1b4) = 0;
    }
    SCR_LoadingPump();
}

// --- Collector's edition: leaving ---

void XB_THISCALL SpecialConceptMenu_OnLeave(uint8_t* menu, uint32_t) {
    release_image(menu, 0x44, 0x48, 0x4c, true);
    call(menu, 0x88, 0, 1);
}

void XB_THISCALL SpecialMaterialsMenu_OnLeave(uint8_t* menu, uint32_t) { release_image(menu, 0x5c, 0x60, 0x64, false); }

// --- Collector's edition art ---

SPECIAL_HANDLER_ENTRY(SpecialConceptMenu_OnA)
SPECIAL_HANDLER_ENTRY(SpecialConceptMenu_OnRight)
SPECIAL_HANDLER_ENTRY(SpecialConceptMenu_OnLeft)
SPECIAL_HANDLER_ENTRY(SpecialConceptMenu_OnDown)
SPECIAL_HANDLER_ENTRY(SpecialConceptMenu_OnUp)
SPECIAL_PLAIN_ENTRY(SpecialConceptMenu_RestoreSelection)

void XB_CDECL SpecialConceptMenu_OnA(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.method(field<uint8_t*>(menu, 0x38), 0x6c, 0x0082b854);  // "BONUS_HELPBAR_BACK_ONLY"
    menu[0x40] = 1;
    c.run_eax(own_address(&FE_BonusCELoadImage_entry), address_of(menu));
}

// The image reload (vtable +0x10, FE_BonusCELoadImage: stack buffers handed to title code) is tail-called at the
// entry ESP: the C version returns its address (0: none) and the entry jumps with ECX = the menu.
PLATFORM_ASM_ENTRY(SpecialConceptMenu_NextItem_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SpecialConceptMenu_NextItem) "movl 8(%esp), %ecx\n"
                   "addl $12, %esp\n"
                   "testl %eax, %eax\n"
                   "jz 1f\n"
                   "jmp *%eax\n"
                   "1:\n"
                   "ret")

uint32_t XB_CDECL SpecialConceptMenu_NextItem(int32_t direction, uint32_t, uint8_t* menu) {
    static constexpr int32_t kCounts[6] = {0x26, 0xf, 0xf, 3, 9, 0xb};
    const int32_t row = selection(menu);
    const uint32_t reload = field<uint32_t>(field<uint8_t*>(menu, 0), 0x10);
    if (direction == 1) {
        if (uint32_t(row) > 5) return 0;
        int32_t& item = field<int32_t>(menu, 0x130 + uint32_t(row) * 4);
        if (item >= kCounts[row]) return 0;
        ++item;
        return reload;
    }
    int32_t& item = *reinterpret_cast<int32_t*>(menu + 0x130 + row * 4);
    if (direction == 2) {
        if (item <= 1) return 0;
        --item;
        return reload;
    }
    if (item == 0) item = 1;
    return 0;
}

// Leaving the full-screen image: the help bar text, +0x40 = 0, the image released (file +0x44, buffer +0x48,
// texture +0x4c), then FE_BonusCELoadImage (EAX = the menu) tail-called at the entry ESP.
PLATFORM_ASM_ENTRY(SpecialConceptMenu_LeaveFullScreen_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(SpecialConceptMenu_LeaveFullScreen) "addl $4, %esp\n"
                   "movl %esi, %eax\n"
                   "jmp " PLATFORM_ASM_SYMBOL(FE_BonusCELoadImage_entry))

void XB_CDECL SpecialConceptMenu_LeaveFullScreen(uint8_t* menu) {
    call(field<uint8_t*>(menu, 0x38), 0x6c, 0x0082b618);  // "BONUS_HELPBAR_FULLSCREEN"
    menu[0x40] = 0;
    release_image(menu, 0x44, 0x48, 0x4c, true);
}

void XB_CDECL SpecialConceptMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (menu[0x40]) return;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.run_eax(own_address(&SpecialConceptMenu_NextItem_entry), 1);  // the category's next item (ECX / ESI = the menu)
    c.run_eax(own_address(&FE_BonusCELoadImage_entry), address_of(menu));
}

void XB_CDECL SpecialConceptMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (menu[0x40]) return;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    if (concept_item(menu, selection(menu)) > 1) {
        --concept_item(menu, selection(menu));
        c.method(menu, 0x10);
    }
    c.run_eax(own_address(&FE_BonusCELoadImage_entry), address_of(menu));
}

void XB_CDECL SpecialConceptMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    concept_move(menu, ebx, edi, entry_esp, 0xb4);
}

void XB_CDECL SpecialConceptMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    concept_move(menu, ebx, edi, entry_esp, 0xb0);
}

void XB_CDECL SpecialConceptMenu_RestoreSelection(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,
                                                  uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    field<int16_t>(menu, 0x22) = 0;
    concept_item(menu, 0) = 1;
    c.method(menu, 0x88, 0, 1);
    menu[0x40] = 0;
    c.run(own_address(&FE_BonusArtCategoryLabel_entry));  // FE_BonusArtCategoryLabel (menu in ESI)
    c.run_eax(own_address(&FE_BonusCELoadImage_entry), address_of(menu));
    // pop esi; jmp UIMenu_RestoreSelection: the callee returns to our caller.
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.ecx = address_of(menu);
    regs.esi = esi;
    regs.edi = edi;
    const uint32_t return_address = stack_word(entry_esp);
    title_call_at_regs(entry_esp + 4, own_address(&UIMenu_RestoreSelection), &regs, 0, nullptr);
    stack_word(entry_esp) = return_address;
}

// --- Collector's edition materials ---

SPECIAL_HANDLER_ENTRY(SpecialMaterialsMenu_OnRight)
SPECIAL_HANDLER_ENTRY(SpecialMaterialsMenu_OnLeft)
SPECIAL_HANDLER_ENTRY(SpecialMaterialsMenu_OnDown)
SPECIAL_HANDLER_ENTRY(SpecialMaterialsMenu_OnUp)

void XB_CDECL SpecialMaterialsMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (field<int16_t>(menu, 0x22) != 2) return;
    const int32_t item = field<int32_t>(menu, 0x48);
    if (item >= 2) return;
    field<int32_t>(menu, 0x48) = item + 1;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.run_eax(own_address(&FE_BonusCEMainLoadImage_entry), address_of(menu));
    c.method(menu, 0x10);
}

void XB_CDECL SpecialMaterialsMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (field<int16_t>(menu, 0x22) != 2) return;
    const int32_t item = field<int32_t>(menu, 0x48);
    if (item <= 0) return;
    field<int32_t>(menu, 0x48) = item - 1;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.run_eax(own_address(&FE_BonusCEMainLoadImage_entry), address_of(menu));
    c.method(menu, 0x10);
}

void XB_CDECL SpecialMaterialsMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    materials_move(menu, ebx, edi, entry_esp, 0xb4);
}

void XB_CDECL SpecialMaterialsMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    materials_move(menu, ebx, edi, entry_esp, 0xb0);
}

// --- Unreached slots (host_unreached 2026-11-12) ---

// 0x00012f10 (vtable +0x38): the selected difficulty starts the game.
void XB_THISCALL DifficultyMenu_HandleSelect(uint8_t* menu, int32_t difficulty) {
    DifficultyMenu_StartGame(difficulty, menu);
}

// 0x00013550 (vtable +0x38): the item index (stack) into SpecialMaterialsMenu_HandleSelect (EAX, menu in ECX). The
// call is inside the first 5 bytes: not compared.
PLATFORM_ASM_ENTRY(SpecialMaterialsMenu_OnSelect_entry,
                   "movl 4(%esp), %eax\n" PLATFORM_ASM_CALL(SpecialMaterialsMenu_HandleSelect_entry)
                   "ret $4")

// 0x00013600 (vtable +0x64): full screen (+0x40) -> SpecialConceptMenu_LeaveFullScreen (menu in ESI); else back to
// screen 0x12 through the manager's +0x0c as a tail jump (argument slot rewritten, runs at the entry ESP: its
// sound keeps stack addresses). Not compared.
PLATFORM_ASM_ENTRY(SpecialConceptMenu_OnBack_entry,
                   "cmpb $0, 0x40(%ecx)\n"
                   "je 1f\n"
                   "pushl %esi\n"
                   "movl %ecx, %esi\n" PLATFORM_ASM_CALL(SpecialConceptMenu_LeaveFullScreen_entry)
                   "popl %esi\n"
                   "ret $4\n"
                   "1: movl 8(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "movl $0x12, 4(%esp)\n"
                   "jmp *0xc(%eax)")


namespace {

void draw_object(uint8_t* object) {
    if (object) call(object, 0x3c);
}

void add_quad(uint8_t* menu, uint32_t offset) { nglListAddQuad(reinterpret_cast<const NglQuad*>(menu + offset)); }

}  // namespace

// Full screen (+0x40): the full-screen quad (+0xb4) and the help bar only.
void XB_THISCALL SpecialConceptMenu_Draw(uint8_t* menu) {
    if (!menu[0x40]) {
        if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
        draw_object(field<uint8_t*>(menu, 0x38));
        add_quad(menu, 0x50);
        UIMenu_Draw(menu);
        return;
    }
    add_quad(menu, 0xb4);
    draw_object(field<uint8_t*>(menu, 0x38));
}

void XB_THISCALL SpecialMaterialsMenu_Draw(uint8_t* menu) {
    if (!title_global<uint8_t>(0x0095f91e)) {
        if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
        draw_object(field<uint8_t*>(menu, 0x38));
        add_quad(menu, 0x68);
        UIMenu_Draw(menu);
    }
    Movie_DrawFrame();
}

SPECIAL_PLAIN_ENTRY(SpecialMaterialsMenu_RestoreSelection)

// Movie_Stop(0), 0x0095f91a and +0x48 cleared, the texts +0x4c / +0x50 "" (+0x78), the pictures +0x54 / +0x58
// hidden (+0x7c 0), FE_BonusMovieLabel (menu in ESI), FE_BonusCEMainLoadImage (EAX), UIMenu_RestoreSelection, then
// the saved row (+0x40) selected with sound (+0x88).
void XB_CDECL SpecialMaterialsMenu_RestoreSelection(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t,
                                                    uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    const uint32_t zero[1] = {0};
    c.run(own_address(&Movie_Stop), 1, zero);
    title_global<uint8_t>(0x0095f91a) = 0;
    field<uint32_t>(menu, 0x48) = 0;
    c.method(field<uint8_t*>(menu, 0x4c), 0x78, 0x0082dfeb);
    c.method(field<uint8_t*>(menu, 0x50), 0x78, 0x0082dfeb);
    c.method(field<uint8_t*>(menu, 0x54), 0x7c, 0);
    c.method(field<uint8_t*>(menu, 0x58), 0x7c, 0);
    c.run(own_address(&FE_BonusMovieLabel_entry));
    c.run_eax(own_address(&FE_BonusCEMainLoadImage_entry), address_of(menu));
    c.run(own_address(&UIMenu_RestoreSelection));
    c.method(menu, 0x88, field<uint32_t>(menu, 0x40), 1);
}

// 0x0031af10 (+0x38): the chosen chapter into the front end's screen +0x34 (+0x48, +0x4c cleared), then the front
// end's screen 13 (tail jump). Not compared.
UI_FRONT_END_TAIL_ARG(ChapterMenu_HandleSelect_entry, ChapterMenu_HandleSelect, 0xd)

void XB_CDECL ChapterMenu_HandleSelect(uint32_t index) {
    title_global<uint8_t>(0x0095f91a) = 0;
    Movie_Stop(0);
    const auto chapter_screen = [] {
        return field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8f8), 4), 0x34);
    };
    field<uint32_t>(chapter_screen(), 0x48) = index;
    chapter_screen()[0x4c] = 0;
}

// Deactivated (+0x44): the background movie stops unless it is kept (0x0095f91a).
void XB_THISCALL ChapterMenu_OnDeactivate(uint8_t*, uint8_t*) {
    if (!title_global<uint8_t>(0x0095f91a)) Movie_Stop(0);
}

void XB_THISCALL DifficultyMenu_Draw(uint8_t* menu) {
    if (!title_global<uint8_t>(0x0095f91e)) {
        if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
        draw_object(field<uint8_t*>(menu, 0x38));
        UIMenu_Draw(menu);
        call(field<uint8_t*>(menu, 0x3c), 0x44);
        call(field<uint8_t*>(menu, 0x40), 0x44);
    }
    Movie_DrawFrame();
}

void XB_THISCALL SinglePlayerMenu_Draw(uint8_t* menu) {
    if (!title_global<uint8_t>(0x0095f91e)) {
        if (uint8_t* const panel = field<uint8_t*>(menu, 0x38)) Panel_Draw(panel);
        UIMenu_Draw(menu);
        draw_object(field<uint8_t*>(menu, 0x34));
    }
    Movie_DrawFrame();
}

// 0x0032db40 (+0x38): unless busy (+0x44), the item (+0x3c): 0 starts the game at the current chapter (14 -> 13; all
// sounds and the background movie stopped, 0x0095f920 = it); 1 the chapter list (front end screen +0x34: +0x48 = 0,
// +0x4c = 1) -> screen 13; 2 the chapter into the difficulty screen (+0x44 +0x1ac) -> screen 17; 3 / 4 the profile
// screen (+0x3c: +0x94 = 0, +0x4c = 0 / 2) -> the front end's screen 15. Screens through tail jumps (owner, or the
// front end: the C part's 0x100 bit). Not compared.
PLATFORM_ASM_ENTRY(SinglePlayerMenu_HandleSelect_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(SinglePlayerMenu_HandleSelect) "popl %ecx\n"
                   "addl $4, %esp\n"
                   "testl %eax, %eax\n"
                   "js 1f\n"
                   "testl $0x100, %eax\n"
                   "jz 2f\n"
                   "andl $0xff, %eax\n"
                   "movl 0x0095f8f8, %ecx\n"
                   "jmp 3f\n"
                   "2: movl 8(%ecx), %ecx\n"
                   "3: movl %eax, 4(%esp)\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0xc(%eax)\n"
                   "1: ret $4")

int32_t XB_CDECL SinglePlayerMenu_HandleSelect(uint8_t* menu, uint32_t index) {
    if (menu[0x44]) return -1;
    field<uint32_t>(menu, 0x3c) = index;
    int32_t& chapter = title_global<int32_t>(0x008dce48);  // g_currentChapter
    const auto screen = [](uint32_t offset) {
        return field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8f8), 4), offset);
    };
    switch (index) {
    case 0:
        if (chapter == 0xe) chapter = 0xd;
        S_StopAllSounds(title_global<uint8_t*>(0x0095f4c4));
        Movie_Stop(0);
        title_global<uint8_t>(0x0095f91a) = 0;
        title_global<int32_t>(0x0095f920) = chapter;
        return -1;
    case 1:
        field<uint32_t>(screen(0x34), 0x48) = 0;
        screen(0x34)[0x4c] = 1;
        return 0xd;
    case 2:
        field<int32_t>(screen(0x44), 0x1ac) = chapter;
        return 0x11;
    case 3:
    case 4:
        screen(0x3c)[0x94] = 0;
        field<uint32_t>(screen(0x3c), 0x4c) = index == 3 ? 0 : 2;
        return 0x100 | 0xf;
    default: return -1;
    }
}

// 0x00333190 (+0x30): unless 0x0095f91e: UIMenu_Update; while loading (+0x3c) SCR_LoadingPump only; else the main
// menu music once the delay (+0x44) runs out, the help bar (+0x38) by row, and on the flythrough row (2) its title /
// "n / 3" / arrows (+0x4c, +0x50, +0x54, +0x58) by page (+0x48), elsewhere cleared. The last arrow call is a tail
// jump in the original: its value stays in the dt argument slot.
void XB_THISCALL SpecialMaterialsMenu_Update(uint8_t* menu, float dt) {
    if (title_global<uint8_t>(0x0095f91e)) return;
    UIMenu_Update(menu, dt);
    if (menu[0x3c]) {
        SCR_LoadingPump();
        return;
    }
    int32_t& delay = field<int32_t>(menu, 0x44);
    if (delay) --delay;
    if (delay == 1) {
        delay = 0;
        Music_Play(title_global<uint8_t*>(0x0095f4cc), reinterpret_cast<const char*>(uintptr_t{0x0082cbe0}));  // "music_mainmenu"
    }
    call(field<uint8_t*>(menu, 0x38), 0x6c,
         field<int16_t>(menu, 0x22) == 4 ? 0x0082bb64u : 0x0082bb78u);  // BONUS_OP_HELPBAR / BONUS_HELPBAR_PLAY
    uint8_t* const title = field<uint8_t*>(menu, 0x4c);
    constexpr uint32_t kShown = 0x3f800000;  // 1.0
    uint32_t last = 0;
    if (field<int16_t>(menu, 0x22) == 2) {
        const int32_t page = field<int32_t>(menu, 0x48);
        static const uint32_t kTitles[3] = {0x0082aa38, 0x0082aa1c, 0x0082aa00};  // BONUS_FLYTHROUGH1..3
        static const uint32_t kPages[3] = {0x0082aa30, 0x0082aa14, 0x0082a9f8};   // "1 / 3".."3 / 3"
        const int32_t i = page == 0 ? 0 : page == 1 ? 1 : 2;
        call(title, 0x6c, kTitles[i]);
        call(field<uint8_t*>(menu, 0x50), 0x6c, kPages[i]);
        call(field<uint8_t*>(menu, 0x54), 0x7c, i == 0 ? 0 : kShown);
        last = i == 2 ? 0 : kShown;
    } else {
        call(title, 0x78, 0x0082dfeb);  // ""
        call(field<uint8_t*>(menu, 0x50), 0x78, 0x0082dfeb);
        call(field<uint8_t*>(menu, 0x54), 0x7c, 0);
    }
    *reinterpret_cast<volatile uint32_t*>(&dt) = last;
    call(field<uint8_t*>(menu, 0x58), 0x7c, last);
}

// 0x003355c0 (+0x30): unless 0x0095f91e UIMenu_Update and the difficulty picture; a changed row (+0x50) plays the
// move sound (+0x10); SCR_LoadingPump.
void XB_THISCALL DifficultyMenu_Update(uint8_t* menu, float dt) {
    if (!title_global<uint8_t>(0x0095f91e)) {
        UIMenu_Update(menu, dt);
        FE_SetDifficultyPicture(menu, field<int16_t>(menu, 0x22));
    }
    if (field<int32_t>(menu, 0x50) != field<int16_t>(menu, 0x22)) {
        call(menu, 0x10);
        field<int32_t>(menu, 0x50) = field<int16_t>(menu, 0x22);
    }
    SCR_LoadingPump();
}

namespace {

// The background movie ("Propgnda") opened full screen once (0x0095f91a).
void open_background_movie() {
    if (title_global<uint8_t>(0x0095f91a)) return;
    if (!Movie_Open(reinterpret_cast<const char*>(uintptr_t{0x0082cacc}))) return;
    nglSetQuadRect(title_global<NglQuad*>(0x0095f458), 0.0f, 0.0f, 640.0f, 480.0f);
    title_global<uint8_t>(0x0095f91a) = 1;
}

}  // namespace

// 0x00337e00 (+0x40): +0x1a8 cleared, the chapter list, UIMenu_RestoreSelection, the selection = the chapter
// (+0x1ac) with its three elements coloured 0xff282119 (+0x90), item 13's left neighbour = 12, the background movie.
void XB_THISCALL ChapterMenu_RestoreSelection(uint8_t* menu) {
    menu[0x1a8] = 0;
    ChapterMenu_UpdateList(menu);
    UIMenu_RestoreSelection(menu);
    field<int16_t>(menu, 0x22) = static_cast<int16_t>(field<int32_t>(menu, 0x1ac));
    static const uint32_t kElements[3] = {0xe4, 0x11c, 0x154};
    for (uint32_t offset : kElements)
        call(field<uint8_t*>(menu, offset + uint32_t(field<int32_t>(menu, 0x1ac)) * 4), 0x90, 0xff282119u);
    field<int16_t>(field<uint8_t**>(menu, 4)[13], 0xc) = 0xc;
    open_background_movie();
}

// 0x00339170 (+0x40): the idle time reset (+0x40 = 0x008ddaa8, +0x44 / +0x48 cleared); item 0 hidden without an
// unlocked chapter (then the saved row at least 1), items 3 / 4 shown with 0x008dd1cf == 1; UIMenu_RestoreSelection,
// the saved row (+0x3c) selected, the background movie.
void XB_THISCALL SinglePlayerMenu_RestoreSelection(uint8_t* menu) {
    field<uint32_t>(menu, 0x40) = title_global<uint32_t>(0x008ddaa8);
    menu[0x44] = 0;
    field<uint32_t>(menu, 0x48) = 0;
    uint32_t hide = 0;
    if (title_global<int32_t>(0x008dce44) < 0) {  // g_chaptersUnlocked
        if (!field<uint32_t>(menu, 0x3c)) field<uint32_t>(menu, 0x3c) = 1;
        hide = 1;
    }
    uint8_t** const items = field<uint8_t**>(menu, 4);
    call(items[0], 0x20, hide);
    const uint32_t extra = title_global<uint8_t>(0x008dd1cf) == 1 ? 1 : 0;
    call(field<uint8_t**>(menu, 4)[3], 0x20, extra);
    call(field<uint8_t**>(menu, 4)[4], 0x20, extra);
    UIMenu_RestoreSelection(menu);
    call(menu, 0x88, field<uint32_t>(menu, 0x3c), 1);
    open_background_movie();
}

}  // namespace game
