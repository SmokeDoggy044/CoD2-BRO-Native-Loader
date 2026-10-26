#include "fe_bonus.h"

#include "fe.h"
#include "ngl.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"

// The gap entries (see fe_menus.cpp): the C frame moves below a gap; the C version gets the menu, `extra` (pushed
// before ECX: the next parameters), the entry EBX / EDI / ESI and the entry ESP.
#define BONUS_GAP_ENTRY(name, c_name, extra, c_arg_bytes, ret)                                                 \
    PLATFORM_ASM_ENTRY(name, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"                        \
                             "pushl %edx\n pushl %esi\n pushl %edi\n pushl %ebx\n" extra                      \
                             "pushl %ecx\n" PLATFORM_ASM_CALL(c_name) "addl $" c_arg_bytes ", %esp\n"         \
                             "popl %esp\n" ret)
// The button handlers (ret 4: the pad) and the RestoreSelection slots (no stack argument).
#define BONUS_HANDLER_ENTRY(name) BONUS_GAP_ENTRY(name##_entry, name, "", "20", "ret $4")
#define BONUS_PLAIN_ENTRY(name) BONUS_GAP_ENTRY(name##_entry, name, "", "20", "ret")

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }

int32_t selection(uint8_t* menu) { return field<int16_t>(menu, 0x22); }

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

// A title object's vtable slot (byte offset), thiscall, from C.
void delete_object(uint8_t* object) {
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(field<uint32_t>(field<uint8_t*>(object, 0), 0))(object, 1);
}

// Title calls at the original's ESP `top` (before the call's pushes) with the original's registers.
struct Calls {
    uint32_t top;
    TitleRegs regs;

    uint32_t run(uint32_t function, uint32_t count = 0, const uint32_t* args = nullptr) const {
        return title_call_at_regs(top, function, &regs, count, args);
    }
    // EAX = value (the FE_Bonus* load image helpers, the label helpers).
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

// A tail jump: the callee runs at the entry ESP (its return goes to our caller) with `regs`.
void tail_jump(uint32_t entry_esp, uint32_t function, const TitleRegs& regs) {
    const uint32_t return_address = stack_word(entry_esp);
    title_call_at_regs(entry_esp + 4, function, &regs, 0, nullptr);
    stack_word(entry_esp) = return_address;
}

// Back to the bonus materials screen: `pop esi; mov [esp+4], 8; jmp [owner vtable + 0x0c]`.
void back_to_materials(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp) {
    uint8_t* const owner = field<uint8_t*>(menu, 8);
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.ecx = address_of(owner);
    regs.esi = esi;
    regs.edi = edi;
    stack_word(entry_esp + 4) = 8;
    tail_jump(entry_esp, field<uint32_t>(field<uint8_t*>(owner, 0), 0xc), regs);
}

// The image helpers of a class (free: menu in ESI, load: menu in EAX) and its full screen flag.
struct ImageClass {
    uint32_t free_image;
    uint32_t load_image;
    uint32_t full_screen;
    bool moves_help_bar;  // vehicles / concepts move the help bar (+0x70)
};
constexpr ImageClass kWeapons{0x0030db60, 0x0031cd50, 0xac, false};   // FE_BonusWeapon*
constexpr ImageClass kVehicles{0x0030db00, 0x0031bf70, 0x88, true};   // FE_BonusVehicle*
constexpr ImageClass kConcepts{0x0030daa0, 0x0031b730, 0xc0, true};   // FE_BonusConcept*

constexpr uint32_t kHelpFullScreen = 0x0082b618;  // "BONUS_HELPBAR_FULLSCREEN"
constexpr uint32_t kHelpBackOnly = 0x0082b854;    // "BONUS_HELPBAR_BACK_ONLY"
constexpr uint32_t k200Float = 0x43480000;

// Out of full screen: the help bar back, the flag off, the image freed and loaded again.
void leave_full_screen(const Calls& c, uint8_t* menu, const ImageClass& image) {
    uint8_t* const help = field<uint8_t*>(menu, 0x38);
    c.method(help, 0x6c, kHelpFullScreen);
    if (image.moves_help_bar) c.method(help, 0x70, 0, 0);
    menu[image.full_screen] = 0;
    c.run(image.free_image);
    c.run_eax(image.load_image, address_of(menu));
}

void on_back(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp, const ImageClass& image) {
    if (!menu[image.full_screen]) {
        back_to_materials(menu, ebx, edi, esi, entry_esp);
        return;
    }
    leave_full_screen(after_push_esi(menu, ebx, edi, entry_esp), menu, image);
}

void on_a(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, const ImageClass& image) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    uint8_t* const help = field<uint8_t*>(menu, 0x38);
    c.method(help, 0x6c, kHelpBackOnly);
    if (image.moves_help_bar) c.method(help, 0x70, k200Float, 0);
    menu[image.full_screen] = 1;
    c.run_eax(image.load_image, address_of(menu));
}

// OnLeave of the concepts / vehicles / weapons menus from the image free on.
void on_leave(const Calls& c, uint8_t* menu, const ImageClass& image) {
    c.run(image.free_image);
    c.method(menu, 0x88, 0, 1);
    if (menu[image.full_screen]) leave_full_screen(c, menu, image);
}

// The stdcall helpers taking the menu: 0x003129e0 (EAX = 1: next item), 0x00312b40 (the level texts), 0x0031c0e0
// (the vehicle category texts).
void stdcall_menu(const Calls& c, uint32_t function, uint8_t* menu) {
    const uint32_t args[1] = {address_of(menu)};
    c.run(function, 1, args);
}

// The concepts' stored bests (13 words from 0x008dced8) raised to the current items (+0x8c..).
void concepts_raise_bests(uint8_t* menu) {
    for (uint32_t i = 0; i < 13; ++i) {
        int32_t& best = title_global<int32_t>(0x008dced8 + i * 4);
        const int32_t item = field<int32_t>(menu, 0x8c + i * 4);
        if (best < item) best = item;
    }
}

void concepts_move(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot) {
    if (menu[0xc0]) return;
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    const int32_t old = selection(menu);
    c.regs.edi = uint32_t(old);
    c.method(menu, slot);
    const int32_t row = selection(menu);
    if (row == old) return;
    int32_t& item = field<int32_t>(menu, 0x8c + uint32_t(row) * 4);
    if (item == 0) item = 1;
    concepts_raise_bests(menu);
    stdcall_menu(c, 0x00312b40, menu);
    c.run_eax(kConcepts.load_image, address_of(menu));
}

// The vehicles' six stored bests (0x008dceb8..) against their category items.
void vehicles_raise_bests(uint8_t* menu) {
    static const uint32_t kItems[6] = {0x70, 0x74, 0x80, 0x7c, 0x78, 0x84};
    for (uint32_t i = 0; i < 6; ++i) {
        int32_t& best = title_global<int32_t>(0x008dceb8 + i * 4);
        const int32_t item = field<int32_t>(menu, kItems[i]);
        if (best < item) best = item;
    }
}

void vehicles_step(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t direction) {
    if (menu[0x88]) return;
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    c.regs.edi = direction;
    c.run_eax(0x0031bc50, address_of(menu));
    stdcall_menu(c, 0x0031c0e0, menu);
    c.run_eax(kVehicles.load_image, address_of(menu));
}

void vehicles_move(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot) {
    if (menu[0x88]) return;
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    Calls inner = c;
    inner.top -= 4;  // push edi (popped again before the rest)
    const int32_t old = selection(menu);
    inner.regs.edi = uint32_t(old);
    inner.method(menu, slot);
    const int32_t row = selection(menu);
    if (row == old) return;
    int32_t& item = field<int32_t>(menu, 0x70 + uint32_t(row) * 4);
    if (item == 0) item = 1;
    vehicles_raise_bests(menu);
    stdcall_menu(c, 0x0031c0e0, menu);
    c.run_eax(kVehicles.load_image, address_of(menu));
}

// 0x0031c870 (menu in EAX, direction in EDI), then 0x0031ceb0 (menu in EDI) and the image (EDI = menu still).
void weapons_update(Calls& c, uint8_t* menu, uint32_t direction) {
    c.regs.edi = direction;
    c.run_eax(0x0031c870, address_of(menu));
    c.regs.edi = address_of(menu);
    c.run(0x0031ceb0);
    c.run_eax(kWeapons.load_image, address_of(menu));
}

void weapons_step(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t direction) {
    if (menu[0xac]) return;
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    weapons_update(c, menu, direction);
}

void weapons_move(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot) {
    if (menu[0xac]) return;
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    const int32_t old = selection(menu);
    c.regs.edi = uint32_t(old);
    c.method(menu, slot);
    if (selection(menu) == old) return;
    weapons_update(c, menu, 0);
}

// The characters' / materials' OnUp-style moves: `push esi; push edi` (ESI = menu, EDI = the old row); the image
// when the row changed.
void move_and_load(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t entry_esp, uint32_t slot,
                   uint32_t load_image) {
    Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.top -= 4;  // push edi
    const int32_t old = selection(menu);
    c.regs.edi = uint32_t(old);
    c.method(menu, slot);
    if (selection(menu) == old) return;
    c.run_eax(load_image, address_of(menu));
}

constexpr uint32_t kCharacterLoadImage = 0x0031d5b0;  // FE_BonusCharacterLoadImage
constexpr uint32_t kMainLoadImage = 0x0031d9d0;       // FE_BonusMainLoadImage
constexpr uint32_t kRestoreSelection = 0x00314b20;    // UIMenu_RestoreSelection

// Row 0 selected again (+0x22 = 0, then +0x88 (0, 1)).
void select_row0(const Calls& c, uint8_t* menu) {
    field<int16_t>(menu, 0x22) = 0;
    c.method(menu, 0x88, 0, 1);
}

// The RestoreSelection slots' tail jump (ESI back to the entry's).
void restore_selection_tail(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp) {
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.ecx = address_of(menu);
    regs.esi = esi;
    regs.edi = edi;
    tail_jump(entry_esp, kRestoreSelection, regs);
}

void init_menu(uint8_t* menu, int32_t rows, uint8_t* owner) { UIMenu_Init(menu, rows, owner, 0x140, 0xf0, 8, 0); }

// The destructors' panel part (the vtable first).
void delete_panel(uint8_t* menu, uint32_t vtable) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = vtable;
    if (panel) Panel_Delete(panel);
}

void free_menu(uint8_t* menu) {
    field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

void delete_if_set(uint8_t* menu, uint32_t offset) {
    if (uint8_t* const object = field<uint8_t*>(menu, offset)) delete_object(object);
}

}  // namespace

// --- Destructors ---

void XB_REGS1 XB_CDECL BonusMaterialsMenu_Dtor(uint8_t* menu) {
    delete_panel(menu, 0x0082a708);
    delete_if_set(menu, 0x38);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL BonusConceptsMenu_Dtor(uint8_t* menu) {
    delete_panel(menu, 0x00828f98);
    delete_if_set(menu, 0x38);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL BonusVehiclesMenu_Dtor(uint8_t* menu) {
    delete_panel(menu, 0x00829ab8);
    delete_if_set(menu, 0x38);
    delete_if_set(menu, 0x44);
    free_menu(menu);
}

void XB_REGS1 XB_CDECL BonusWeaponsMenu_Dtor(uint8_t* menu) {
    uint8_t* const panel = field<uint8_t*>(menu, 0x34);
    field<uint32_t>(menu, 0) = 0x00828868;
    if (panel) {
        Panel_Delete(panel);
        field<uint32_t>(menu, 0x34) = 0;
    }
    if (uint8_t* const help = field<uint8_t*>(menu, 0x38)) {
        delete_object(help);
        field<uint32_t>(menu, 0x38) = 0;
    }
    if (uint8_t* const info = field<uint8_t*>(menu, 0x44)) {
        delete_object(info);
        field<uint32_t>(menu, 0x44) = 0;
    }
    free_menu(menu);
}

void XB_REGS1 XB_CDECL BonusCharactersMenu_Dtor(uint8_t* menu) {
    delete_panel(menu, 0x00829728);
    delete_if_set(menu, 0x38);
    delete_if_set(menu, 0x6c);
    delete_if_set(menu, 0x70);
    free_menu(menu);
}

uint8_t* XB_THISCALL BonusMaterialsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    BonusMaterialsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL BonusWeaponsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    BonusWeaponsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL BonusVehiclesMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    BonusVehiclesMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL BonusCharactersMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    BonusCharactersMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

uint8_t* XB_THISCALL BonusConceptsMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    BonusConceptsMenu_Dtor(menu);
    if (flags & 1) z_free(menu);
    return menu;
}

// --- Constructors ---

uint8_t* XB_REGS3 XB_CDECL BonusConceptsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 0xd, owner);
    field<uint32_t>(menu, 0) = 0x00828f98;  // BonusConceptsMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<float>(menu, 0x40) = 1.0f;
    menu[0x44] = 0;
    menu[0xc0] = 0;
    field<int32_t>(menu, 0xc4) = -1;
    field<uint32_t>(menu, 0xc8) = 0;
    field<uint32_t>(menu, 0xcc) = 0;
    for (uint32_t i = 0; i < 0xd; ++i) {
        field<uint32_t>(menu, 0x48 + i * 4) = 0;
        menu[0x7c + i] = 0;
        field<uint32_t>(menu, 0x8c + i * 4) = 0;
    }
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL BonusVehiclesMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 6, owner);
    field<uint32_t>(menu, 0) = 0x00829ab8;  // BonusVehiclesMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x44) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<float>(menu, 0x48) = 1.0f;
    menu[0x4c] = 0;
    menu[0x88] = 0;
    field<int32_t>(menu, 0x8c) = -1;
    field<uint32_t>(menu, 0x90) = 0;
    field<uint32_t>(menu, 0x94) = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        field<uint32_t>(menu, 0x50 + i * 4) = 0;
        menu[0x68 + i] = 0;
        field<uint32_t>(menu, 0x70 + i * 4) = 0;
    }
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL BonusWeaponsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 0xa, owner);
    field<uint32_t>(menu, 0) = 0x00828868;  // BonusWeaponsMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x3c) = 0;
    field<uint32_t>(menu, 0x44) = 0;
    field<uint32_t>(menu, 0x40) = 0;
    field<float>(menu, 0x48) = 1.0f;
    menu[0x4c] = 0;
    menu[0xac] = 0;
    field<int32_t>(menu, 0xb0) = -1;
    field<uint32_t>(menu, 0xb4) = 0;
    field<uint32_t>(menu, 0xb8) = 0;
    for (uint32_t i = 0; i < 0xa; ++i) {
        field<uint32_t>(menu, 0x50 + i * 4) = 0;
        menu[0x78 + i] = 0;
        field<uint32_t>(menu, 0x84 + i * 4) = 0;
    }
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL BonusCharactersMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 7, owner);
    field<uint32_t>(menu, 0) = 0x00829728;  // BonusCharactersMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x70) = 0;
    field<uint32_t>(menu, 0x6c) = 0;
    field<uint32_t>(menu, 0x68) = 0;
    field<float>(menu, 0x3c) = 1.0f;
    menu[0x40] = 0;
    field<int32_t>(menu, 0x74) = -1;
    field<uint32_t>(menu, 0x78) = 0;
    field<uint32_t>(menu, 0x7c) = 0;
    for (uint32_t i = 0; i < 7; ++i) {
        field<uint32_t>(menu, 0x44 + i * 4) = 0;
        menu[0x60 + i] = 0;
    }
    return menu;
}

uint8_t* XB_REGS3 XB_CDECL BonusMaterialsMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    init_menu(menu, 6, owner);
    field<uint32_t>(menu, 0) = 0x0082a708;  // BonusMaterialsMenu vtable
    menu[0x33] = 5;
    field<uint32_t>(menu, 0x34) = 0;
    field<uint32_t>(menu, 0x38) = 0;
    field<uint32_t>(menu, 0x68) = 0;
    field<uint32_t>(menu, 0x6c) = 0;
    field<uint32_t>(menu, 0x70) = 0;
    field<uint32_t>(menu, 0x74) = 0;
    field<float>(menu, 0x40) = 1.0f;
    menu[0x3c] = 0;
    menu[0x44] = 0;
    field<uint32_t>(menu, 0x80) = 0;
    field<uint32_t>(menu, 0x84) = 0;
    field<uint32_t>(menu, 0x78) = 0;
    field<uint32_t>(menu, 0x7c) = 0;
    field<int32_t>(menu, 0x88) = -1;
    field<uint32_t>(menu, 0x8c) = 0;
    field<uint32_t>(menu, 0x90) = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        field<uint32_t>(menu, 0x48 + i * 4) = 0;
        menu[0x60 + i] = 0;
    }
    return menu;
}

// --- Draw ---

void XB_THISCALL BonusCharactersMenu_Draw(uint8_t* menu) {
    using DrawMethod = void XB_THISCALL(uint8_t*);
    const auto draw = [](uint8_t* object) {
        if (object) title_function<DrawMethod>(field<uint32_t>(field<uint8_t*>(object, 0), 0x3c))(object);
    };
    if (uint8_t* const panel = field<uint8_t*>(menu, 0x34)) Panel_Draw(panel);
    draw(field<uint8_t*>(menu, 0x38));
    draw(field<uint8_t*>(menu, 0x68));
    draw(field<uint8_t*>(menu, 0x6c));
    draw(field<uint8_t*>(menu, 0x70));
    nglListAddQuad(reinterpret_cast<const NglQuad*>(menu + 0x80));
    UIMenu_Draw(menu);
}

// --- Full screen (A / B) ---

BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnBack)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnBack)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnBack)
BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnA)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnA)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnA)

void XB_CDECL BonusWeaponsMenu_OnBack(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp) {
    on_back(menu, ebx, edi, esi, entry_esp, kWeapons);
}
void XB_CDECL BonusVehiclesMenu_OnBack(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp) {
    on_back(menu, ebx, edi, esi, entry_esp, kVehicles);
}
void XB_CDECL BonusConceptsMenu_OnBack(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi, uint32_t entry_esp) {
    on_back(menu, ebx, edi, esi, entry_esp, kConcepts);
}
void XB_CDECL BonusWeaponsMenu_OnA(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    on_a(menu, ebx, edi, entry_esp, kWeapons);
}
void XB_CDECL BonusVehiclesMenu_OnA(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    on_a(menu, ebx, edi, entry_esp, kVehicles);
}
void XB_CDECL BonusConceptsMenu_OnA(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    on_a(menu, ebx, edi, entry_esp, kConcepts);
}

// --- Leaving the screen ---

BONUS_HANDLER_ENTRY(BonusCharactersMenu_OnLeave)
BONUS_HANDLER_ENTRY(BonusMaterialsMenu_OnLeave)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnLeave)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnLeave)
BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnLeave)

void XB_CDECL BonusCharactersMenu_OnLeave(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.run(0x00064660);  // nglResetDeviceBindings
    if (const uint32_t texture = field<uint32_t>(menu, 0x7c)) {
        const uint32_t args[1] = {texture};
        c.run(0x00063630, 1, args);  // nglReleaseTexture
    }
    const int32_t file = field<int32_t>(menu, 0x74);
    if (file != -1) {
        const uint32_t args[1] = {uint32_t(file)};
        c.run(0x000cac90, 1, args);  // File_Close
        if (const uint32_t buffer = field<uint32_t>(menu, 0x78)) {
            const uint32_t free_args[1] = {buffer};
            c.run(0x000939b0, 1, free_args);  // Mem_AlignedFree
        }
    }
    field<uint32_t>(menu, 0x78) = 0;
    c.method(menu, 0x88, 0, 1);
}

void XB_CDECL BonusMaterialsMenu_OnLeave(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    after_push_esi(menu, ebx, edi, entry_esp).run(0x0030dcc0);  // FE_BonusMainFreeImage
}

void XB_CDECL BonusConceptsMenu_OnLeave(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    on_leave(after_push_esi(menu, ebx, edi, entry_esp), menu, kConcepts);
}

void XB_CDECL BonusVehiclesMenu_OnLeave(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.method(title_global<uint8_t*>(0x0095f478), 0x88, 0x3f800000);  // 1.0
    c.method(field<uint8_t*>(title_global<uint8_t*>(0x0095f8f8), 0x1c), 0x6c, 0x0082caa4);  // "FEMENU_XBOX_LIVE"
    on_leave(c, menu, kVehicles);
}

void XB_CDECL BonusWeaponsMenu_OnLeave(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    on_leave(after_push_esi(menu, ebx, edi, entry_esp), menu, kWeapons);
}

// --- Concept art ---

BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnRight)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnLeft)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnDown)
BONUS_HANDLER_ENTRY(BonusConceptsMenu_OnUp)

void XB_CDECL BonusConceptsMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (menu[0xc0]) return;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    Calls next = c;
    next.regs.eax = 1;
    stdcall_menu(next, 0x003129e0, menu);
    stdcall_menu(c, 0x00312b40, menu);
    c.run_eax(kConcepts.load_image, address_of(menu));
}

void XB_CDECL BonusConceptsMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (menu[0xc0]) return;
    for (int32_t i = 0; i < 0xd; ++i) {
        int32_t& item = field<int32_t>(menu, 0x8c + uint32_t(i) * 4);
        if (i == selection(menu) && item > 1) --item;
    }
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    stdcall_menu(c, 0x00312b40, menu);
    c.run_eax(kConcepts.load_image, address_of(menu));
}

void XB_CDECL BonusConceptsMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    concepts_move(menu, ebx, edi, entry_esp, 0xb4);
}

void XB_CDECL BonusConceptsMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    concepts_move(menu, ebx, edi, entry_esp, 0xb0);
}

// --- Vehicles ---

BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnRight)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnLeft)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnDown)
BONUS_HANDLER_ENTRY(BonusVehiclesMenu_OnUp)
BONUS_PLAIN_ENTRY(BonusVehiclesMenu_RestoreSelection)

void XB_CDECL BonusVehiclesMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    vehicles_step(menu, ebx, edi, entry_esp, 1);
}

void XB_CDECL BonusVehiclesMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    vehicles_step(menu, ebx, edi, entry_esp, 2);
}

void XB_CDECL BonusVehiclesMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    vehicles_move(menu, ebx, edi, entry_esp, 0xb4);
}

void XB_CDECL BonusVehiclesMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    vehicles_move(menu, ebx, edi, entry_esp, 0xb0);
}

void XB_CDECL BonusVehiclesMenu_RestoreSelection(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,
                                                 uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    select_row0(c, menu);
    field<int32_t>(menu, 0x70) = 1;
    menu[0x88] = 0;
    c.run(0x0031c5d0);  // FE_BonusVehicleCategoryLabel (menu in ESI)
    c.run_eax(kVehicles.load_image, address_of(menu));
    restore_selection_tail(menu, ebx, edi, esi, entry_esp);
}

// --- Weapons ---

BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnRight)
BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnLeft)
BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnDown)
BONUS_HANDLER_ENTRY(BonusWeaponsMenu_OnUp)
BONUS_PLAIN_ENTRY(BonusWeaponsMenu_RestoreSelection)

void XB_CDECL BonusWeaponsMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    weapons_step(menu, ebx, edi, entry_esp, 1);
}

void XB_CDECL BonusWeaponsMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    weapons_step(menu, ebx, edi, entry_esp, 2);
}

void XB_CDECL BonusWeaponsMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    weapons_move(menu, ebx, edi, entry_esp, 0xb4);
}

void XB_CDECL BonusWeaponsMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    weapons_move(menu, ebx, edi, entry_esp, 0xb0);
}

void XB_CDECL BonusWeaponsMenu_RestoreSelection(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,
                                                uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    select_row0(c, menu);
    field<int32_t>(menu, 0x84) = 1;
    menu[0xac] = 0;
    c.run_eax(0x0031d250, address_of(menu));  // FE_BonusWeaponCategoryLabel
    c.run_eax(kWeapons.load_image, address_of(menu));
    restore_selection_tail(menu, ebx, edi, esi, entry_esp);
}

// --- Characters ---

BONUS_HANDLER_ENTRY(BonusCharactersMenu_OnDown)
BONUS_HANDLER_ENTRY(BonusCharactersMenu_OnUp)
BONUS_PLAIN_ENTRY(BonusCharactersMenu_RestoreSelection)

void XB_CDECL BonusCharactersMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t, uint32_t, uint32_t entry_esp) {
    // push esi; push edi: EDI = the menu, ESI = the old row.
    Calls c{entry_esp - 8, TitleRegs{}};
    c.regs.ebx = ebx;
    c.regs.ecx = address_of(menu);
    c.regs.edi = address_of(menu);
    const int32_t old = selection(menu);
    c.regs.esi = uint32_t(old);
    c.method(menu, 0xb4);
    const int32_t row = selection(menu);
    if (row == old) return;
    int32_t& reached = title_global<int32_t>(0x008dced0);
    const int32_t count = row + 1;
    if (reached < count) reached = count;
    c.run(0x0031d6b0);  // the character labels (menu in EDI)
    c.run_eax(kCharacterLoadImage, address_of(menu));
}

void XB_CDECL BonusCharactersMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    move_and_load(menu, ebx, edi, entry_esp, 0xb0, kCharacterLoadImage);
}

void XB_CDECL BonusCharactersMenu_RestoreSelection(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,
                                                   uint32_t entry_esp) {
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    field<int32_t>(menu, 0x74) = -1;
    field<uint32_t>(menu, 0x78) = 0;
    field<uint32_t>(menu, 0x7c) = 0;
    select_row0(c, menu);
    int32_t& reached = title_global<int32_t>(0x008dced0);
    if (reached < 1) reached = 1;
    c.run_eax(0x0031d880, address_of(menu));  // FE_BonusCharacterLabel
    c.run_eax(kCharacterLoadImage, address_of(menu));
    restore_selection_tail(menu, ebx, edi, esi, entry_esp);
}

// --- Bonus materials ---

BONUS_HANDLER_ENTRY(BonusMaterialsMenu_OnDown)
BONUS_HANDLER_ENTRY(BonusMaterialsMenu_OnUp)
BONUS_HANDLER_ENTRY(BonusMaterialsMenu_OnRight)
BONUS_HANDLER_ENTRY(BonusMaterialsMenu_OnLeft)

void XB_CDECL BonusMaterialsMenu_OnDown(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    move_and_load(menu, ebx, edi, entry_esp, 0xb4, kMainLoadImage);
}

void XB_CDECL BonusMaterialsMenu_OnUp(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    move_and_load(menu, ebx, edi, entry_esp, 0xb0, kMainLoadImage);
}

void XB_CDECL BonusMaterialsMenu_OnRight(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (field<int16_t>(menu, 0x22) != 1) return;
    const int32_t next = int32_t(field<uint32_t>(menu, 0x7c) + 1);
    if (next >= field<int32_t>(menu, 0x78)) return;
    field<int32_t>(menu, 0x7c) = next;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.method(menu, 0x10);
    c.run_eax(kMainLoadImage, address_of(menu));
    int32_t best = title_global<int32_t>(0x008dcf0c);
    const int32_t item = field<int32_t>(menu, 0x7c);
    if (int32_t(uint32_t(best) - 1) < item) {
        best = int32_t(uint32_t(item) + 1);
        title_global<int32_t>(0x008dcf0c) = best;
    }
    menu[0x61] = best != field<int32_t>(menu, 0x78);
}

void XB_CDECL BonusMaterialsMenu_OnLeft(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t, uint32_t entry_esp) {
    if (field<int16_t>(menu, 0x22) != 1) return;
    const int32_t item = field<int32_t>(menu, 0x7c);
    if (item <= 0) return;
    field<int32_t>(menu, 0x7c) = item - 1;
    const Calls c = after_push_esi(menu, ebx, edi, entry_esp);
    c.method(menu, 0x10);
    c.run_eax(kMainLoadImage, address_of(menu));
}

BONUS_GAP_ENTRY(BonusMaterialsMenu_HandleSelect_entry, BonusMaterialsMenu_HandleSelect, "pushl %eax\n", "24", "ret")

void XB_CDECL BonusMaterialsMenu_HandleSelect(uint8_t* menu, int32_t index, uint32_t ebx, uint32_t, uint32_t esi,
                                              uint32_t entry_esp) {
    // push edi; mov edi, ecx.
    Calls c{entry_esp - 4, TitleRegs{}};
    c.regs.ebx = ebx;
    c.regs.esi = esi;
    c.regs.edi = address_of(menu);
    if (menu[0x3c]) return;
    field<int32_t>(menu, 0x80) = selection(menu);
    if (uint32_t(index) > 5) return;
    if (index <= 1) {  // the jump table's cases 0 and 1
        c.run(0x00335640);  // plays the selected bonus movie (menu in EDI)
        return;
    }
    c.method(title_global<uint8_t*>(0x0095f8f8), 0xc, uint32_t(7 + index));  // the front end's screen 9 .. 12
}

}  // namespace game
