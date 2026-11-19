// Our settings pages in the game's own menus (own engine phase 4.2; fe_pc.h). The host (loader, menu_pages.cpp)
// keeps the pages; this file shows the current one on a host menu in the game's style and passes the input on.
//
// A host menu shows a page by running with our vtable (a copy of its own with the draw, update, input and
// activation slots replaced), our item array (one UIMenuItem per row around a copy of the host's label text) and
// our value texts (copies of its value text); the host panel stays for the background and the title, its other
// texts and pictures are hidden, and its first arrow pair moves to the selected row. Closing the pages puts all of
// it back, so the host's own menu is unchanged.

#include <cstdio>
#include <cstring>

#include "cl.h"
#include "fe.h"
#include "fe_options.h"
#include "fe_pc.h"
#include "fe_setup_internal.h"
#include "movie.h"
#include "ngl.h"
#include "qcommon.h"
#include "snd.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"

namespace game {

namespace {

using setup::bits;
using setup::copy_text;
using setup::field;
using setup::method;

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

using VoidMethod = void XB_THISCALL(uint8_t*);
using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);
using FloatPairMethod = void XB_THISCALL(uint8_t*, float, float);
using BoolMethod = uint8_t XB_THISCALL(uint8_t*);
using DepthMethod = float XB_THISCALL(uint8_t*);
using DepthLayerMethod = void XB_THISCALL(uint8_t*, float, uint32_t);
using WidthMethod = float XB_THISCALL(uint8_t*);
using GetColorMethod = uint32_t* XB_THISCALL(uint8_t*, uint32_t*);
using TextMethod = void XB_THISCALL(uint8_t*, const char*);

void call(uint8_t* object, uint32_t offset) { method<VoidMethod>(object, offset)(object); }
void call(uint8_t* object, uint32_t offset, uint32_t a) { method<WordMethod>(object, offset)(object, a); }
bool visible(uint8_t* element) { return method<BoolMethod>(element, 0x30)(element) != 0; }
void set_visible(uint8_t* element, bool on) { call(element, 0x34, on ? 1u : 0u); }
// +0x6c: the localized text of a key; a string that is no key shows as it is.
// An empty string would leave the text without lines, which UIText_SetText reads (line 0's +0x14): a space.
void set_text(uint8_t* text, const char* string) { method<TextMethod>(text, 0x6c)(text, *string ? string : " "); }
void set_position(uint8_t* text, float x, float y) { method<FloatPairMethod>(text, 0x7c)(text, x, y); }
float text_width(uint8_t* text) { return method<WidthMethod>(text, 0xd0)(text); }

// The game's language (g_languageIndex: 0 English, 1 German, 2 French, 3 Spanish, 4 Italian).
int32_t game_language() { return title_global<int32_t>(0x0095f5dc); }

const char* element_name(uint8_t* element, uint32_t name_offset) {
    const uint32_t rep = field<uint32_t>(element, name_offset);
    return rep ? reinterpret_cast<const char*>(uintptr_t{rep + 6}) : "";
}

bool in_list(const char* name, const char* const* list) {
    for (; *list; ++list)
        if (!std::strcmp(name, *list)) return true;
    return false;
}

// A host: its panel's names and where the rows go (640x480).
struct Host {
    const char* title;            // panel text: the page title
    const char* label;            // panel text the row labels copy (font, scale, colour)
    const char* value;            // panel text the values copy
    const char* arrow_left;       // the arrow pair that marks the selected value
    const char* arrow_right;
    const char* const* keep_texts;     // panel texts left as they are (the rest hidden)
    const char* const* keep_pictures;  // panel pictures left as they are (the rest hidden)
    float top, bottom, max_spacing;    // first and last row's y, the largest row spacing
    float label_x, value_x, centre_x;  // left edge of labels, middle of values and of centred rows
    float scale;                       // text scale against the copied text
    float arrow_gap;                   // between a value's edge and its arrow's middle
    uint32_t return_screen;            // where the pages close to
    bool front_end;
};

constexpr const char* kFrontKeepTexts[] = {"ScreenMenu", "Helpbar", nullptr};
constexpr const char* kFrontKeepPictures[] = {"paper2", "paperback", "back02", "back02xx", "letterboxx",
                                              "paperxx", "mp_back", "mp_menu", nullptr};
constexpr const char* kGameKeepTexts[] = {"AdvancedOptions", "Helpbar1", "Helpbar2", "Helpbar3", nullptr};
constexpr const char* kGameKeepPictures[] = {"PauseBackground", "letterboxx", "backxx", "Plane01", nullptr};

const Host kHosts[2] = {
    {"ScreenMenu", "ScreenSize", "ScreenSizeSelection", "arrowleft01", "arrowright01", kFrontKeepTexts,
     kFrontKeepPictures, 152.0f, 362.0f, 31.0f, 116.0f, 462.0f, 318.0f, 0.78f, 16.0f, 3, true},
    {"AdvancedOptions", "InvertAim", "InvertAimOnOff", "arrowL1", "arrowR1", kGameKeepTexts, kGameKeepPictures,
     123.0f, 362.0f, 24.0f, 149.0f, 444.0f, 318.0f, 1.0f, 22.0f, 0, false},
};

constexpr int32_t kMax = kPcPageSlots;  // the menu's item slots; the page's rows scroll through them
constexpr uint32_t kVtableWords = 0x34;  // through +0xcc

struct Hidden {
    uint8_t* element;
    bool was_visible;
};

struct Shown {
    uint8_t* menu = nullptr;
    const Host* host = nullptr;
    uint32_t vtable[kVtableWords] = {};
    uint32_t host_vtable = 0;
    uint8_t* host_items = nullptr;
    uint8_t host_fields[0x0c] = {};  // +0x20..+0x2b: first shown, selection, saved selection, count, rows, flags
    uint8_t** items = nullptr;       // our item array (kMax)
    uint8_t* values[kMax] = {};
    uint8_t* arrows[2] = {};
    float arrow_at[2][2] = {};
    bool arrow_visible[2] = {};
    Hidden hidden[96] = {};
    int32_t hidden_count = 0;
    uint8_t* title = nullptr;
    char title_was[64] = {};
    PcPageView view = {};  // what is shown
    int32_t first = 0;     // the page row in slot 0 (scrolling)
    uint8_t* markers[2] = {};       // up / down: more rows above / below (our triangles, make_marker)
    float slot_top = 0, slot_spacing = 0;  // where lay_out put slot 0, and the step
    bool laid_out = false;
} g_shown;

int32_t g_request = -1;

PcSettingsHooks& hooks() { return pc_settings_hooks(); }

uint8_t* panel_of(uint8_t* menu) { return field<uint8_t*>(menu, 0x34); }

uint8_t* row_item(int32_t row) { return g_shown.items[row]; }
uint8_t* row_item_of(uint8_t* menu, int32_t row) { return field<uint8_t**>(menu, 4)[row]; }

// Rows in slots: how many show, and the page row of a slot.
int32_t shown_slots() { return std::max(0, std::min(g_shown.view.rows - g_shown.first, kMax)); }
int32_t slot_row(int32_t slot) { return g_shown.first + slot; }

void copy_colour(uint8_t* item, uint8_t* text) {
    uint32_t colour;
    const uint32_t* const out = method<GetColorMethod>(item, 0x84)(item, &colour);
    call(text, 0x90, *out);
}

// A new text like the panel text `source` (the base UI text class, as the panel's own: font, colour, alignment,
// draw order), scaled.
uint8_t* new_text(uint8_t* source, float scale, int32_t layer = 2) {
    uint8_t* const text = setup::z_malloc(0x60);
    if (!text) return nullptr;
    const float size = field<float>(source, 0x38) * scale;
    UIText_Init(text, field<int32_t>(source, 0x18), " ", bits(field<float>(source, 0x2c)), bits(field<float>(source, 0x30)),
                0, layer, bits(size), static_cast<uint8_t>(field<uint8_t>(source, 0x5c) & 0xf0), 0, field<uint32_t>(source, 0x50));
    field<uint32_t>(text, 0x08) = field<uint32_t>(source, 0x08);  // the panel's draw order
    return text;
}

void set_align_left(uint8_t* text, bool left) {
    uint8_t& flags = field<uint8_t>(text, 0x5c);
    flags = static_cast<uint8_t>((flags & ~0x30) | (left ? 0x10 : 0));
}

// The scroll markers: up above slot 0 while rows are above, down below the last slot while rows are below.
void place_markers() {
    Shown& s = g_shown;
    const int32_t shown = std::max(0, std::min(s.view.rows - s.first, kMax));
    const bool more[2] = {s.first > 0, s.first + shown < s.view.rows};
    // Below: clear of the labels' descenders; above: under the title's rule.
    const float step = std::max(s.slot_spacing, 18.0f);
    const float y[2] = {s.slot_top - step * 0.66f, s.slot_top + s.slot_spacing * float(shown - 1) + step * 0.86f};
    for (int k = 0; k < 2; ++k) {
        uint8_t* const marker = s.markers[k];
        if (!marker) continue;
        set_visible(marker, more[k]);
        if (more[k]) UIPicture_SetPosition(marker, s.host->centre_x, y[k]);
    }
}

// A scroll marker: a 16 x 9 triangle around (0, 0) in the menus' dark ink, one quad with its tip twice, as the
// game makes its scroll bar's arrows (UIScrollBar_Construct: a 0x40-byte picture, vtable +0x3c, depth 80).
uint8_t* make_marker(bool up) {
    uint8_t* const block = setup::z_malloc(0x40);
    if (!block) return nullptr;
    uint8_t* const picture = UIPicture_Init(block, up ? "pc_scroll_up" : "pc_scroll_down");
    const float tip = up ? -4.5f : 4.5f, base = -tip;
    const float points[12] = {0.0f, tip, 0.0f, 0.0f, tip, 0.0f, -8.0f, base, 0.0f, 8.0f, base, 0.0f};
    const uint32_t ink[4] = {0xff3a3026, 0xff3a3026, 0xff3a3026, 0xff3a3026};
    using AddQuad = void XB_THISCALL(uint8_t*, const float*, const uint32_t*, uint32_t, uint32_t, const char*);
    method<AddQuad>(picture, 0x3c)(picture, points, ink, 1, bits(80.0f), "");
    set_visible(picture, false);
    return picture;
}

// The rows' places for the view's row count; texts set; arrows to the selection.
void lay_out() {
    Shown& s = g_shown;
    const Host& h = *s.host;
    s.first = std::max(0, std::min(s.first, s.view.rows - kMax));
    const int32_t rows = shown_slots();
    field<int16_t>(s.menu, 0x26) = static_cast<int16_t>(rows);
    float spacing = rows > 1 ? (h.bottom - h.top) / float(rows - 1) : 0.0f;
    if (spacing > h.max_spacing) spacing = h.max_spacing;
    // Pages shorter than the space sit in its middle.
    const float top = h.top + ((h.bottom - h.top) - spacing * float(rows - 1)) * 0.5f;
    s.slot_top = top;
    s.slot_spacing = spacing;
    for (int32_t i = 0; i < kMax; ++i) {
        uint8_t* const item = row_item(i);
        uint8_t* const text = field<uint8_t*>(item, 0x10);
        if (i >= rows) {
            set_visible(text, false);
            set_visible(s.values[i], false);
            continue;
        }
        set_visible(text, true);
        const PcPageRow& row = s.view.row[slot_row(i)];
        const float y = top + spacing * float(i);
        const bool value = row.kind == kPcRowValue;
        set_align_left(text, value);
        set_text(text, row.label);
        set_position(text, value ? h.label_x : h.centre_x, y);
        set_text(s.values[i], row.value);
        set_position(s.values[i], h.value_x, y);
        set_visible(s.values[i], value);
        UIMenuItem_SetHighlight(item, row.enabled ? 0 : 1);  // dimmed (alpha 0x64)
        copy_colour(item, s.values[i]);
    }
    s.laid_out = true;
    place_markers();
}

// The arrows around the selected row's value (hidden on other rows).
void place_arrows() {
    Shown& s = g_shown;
    const int32_t slot = field<int16_t>(s.menu, 0x22);
    const int32_t row = slot_row(slot);
    const bool show = slot >= 0 && slot < shown_slots() && s.view.row[row].kind == kPcRowValue && s.view.row[row].enabled;
    for (int k = 0; k < 2; ++k) {
        uint8_t* const arrow = s.arrows[k];
        if (!arrow) continue;
        set_visible(arrow, show);
        if (!show) continue;
        uint8_t* const value = s.values[slot];
        const float half = text_width(value) * 0.5f + s.host->arrow_gap;
        const float y = static_cast<float>(setup::get(value, 0xcc));
        UIPicture_SetPosition(arrow, s.host->value_x + (k ? half : -half), y);
    }
}

// Selects page row `row`, scrolling the slots so it shows (sound: the menu's move sound).
void select_row(int32_t row, bool sound) {
    Shown& s = g_shown;
    if (row < 0 || row >= s.view.rows) return;
    const int32_t first = row < s.first ? row : row >= s.first + kMax ? row - kMax + 1 : s.first;
    if (first != s.first) {
        s.first = first;
        lay_out();
    }
    UIMenu_SetSelection(s.menu, row - s.first, sound ? 1 : 0);
}

// Up / down: the previous / next row that can be selected, wrapping; scrolls.
void move_selection(int32_t step) {
    Shown& s = g_shown;
    const int32_t n = s.view.rows;
    if (n <= 0) return;
    int32_t row = slot_row(field<int16_t>(s.menu, 0x22));
    for (int32_t tries = 0; tries < n; ++tries) {
        row = (row + step + n) % n;
        if (s.view.row[row].enabled) break;
    }
    select_row(row, true);
}

// The host's view again; texts changed where it differs.
void refresh() {
    Shown& s = g_shown;
    PcPageView view;
    view.language = game_language();
    hooks().page_view(&view);
    const bool rows_changed = view.rows != s.view.rows || !s.laid_out;
    bool changed = rows_changed || std::strcmp(view.title, s.view.title) != 0;
    for (int32_t i = 0; !changed && i < view.rows; ++i)
        changed = std::memcmp(&view.row[i], &s.view.row[i], sizeof view.row[i]) != 0;
    const int32_t select = view.selected;
    if (rows_changed) s.first = 0;  // another page
    if (changed) {
        s.view = view;
        if (s.title) set_text(s.title, view.title);
        lay_out();
    }
    if (select >= 0 && select < s.view.rows) select_row(select, false);
    if (field<int16_t>(s.menu, 0x22) >= shown_slots()) select_row(s.first, false);
    // A dimmed row cannot be selected (up / down skip it): the first row that can.
    const int32_t at = slot_row(field<int16_t>(s.menu, 0x22));
    if (at >= 0 && at < s.view.rows && !s.view.row[at].enabled) {
        for (int32_t i = 0; i < s.view.rows; ++i) {
            if (!s.view.row[i].enabled) continue;
            select_row(i, false);
            break;
        }
    }
    for (int32_t i = 0; i < shown_slots(); ++i) copy_colour(row_item(i), s.values[i]);
    place_arrows();
}

void leave();

// After the host's answer: close, open the Controller screen, or stay (refreshed).
void handle(int32_t result) {
    Shown& s = g_shown;
    uint8_t* const menu = s.menu;
    switch (result) {
        case kPcResultClose: {
            const uint32_t screen = s.host->return_screen;
            uint8_t* const owner = field<uint8_t*>(menu, 8);
            leave();
            call(owner, 0xc, screen);
            return;
        }
        case kPcResultController: call(field<uint8_t*>(menu, 8), 0xc, 5); return;  // the session stays
        default: refresh(); return;
    }
}

int32_t selected_row() { return field<int16_t>(g_shown.menu, 0x22); }

void input(int32_t what) {
    const int32_t row = slot_row(selected_row());
    const bool value_row = row >= 0 && row < g_shown.view.rows && g_shown.view.row[row].kind == kPcRowValue;
    const int32_t result = hooks().page_input(row, what);
    if ((what == kPcInputLeft || what == kPcInputRight) && value_row) call(g_shown.menu, 0x10);  // the move sound
    handle(result);
}

// --- Our vtable's slots ---

void XB_THISCALL page_draw(uint8_t* menu) {
    if (uint8_t* const panel = panel_of(menu)) Panel_Draw(panel);
    if (g_shown.host->front_end) {
        if (uint8_t* const help = field<uint8_t*>(menu, 0x38)) call(help, 0x3c);
    } else {  // the Options screen's prompts (Continue, Back), as its own draw does
        if (uint8_t* const text = field<uint8_t*>(menu, 0x98)) call(text, 0x3c);
        if (uint8_t* const text = field<uint8_t*>(menu, 0xa0)) call(text, 0x3c);
    }
    UIMenu_Draw(menu);
    for (int32_t i = 0; i < shown_slots(); ++i)
        if (g_shown.view.row[slot_row(i)].kind == kPcRowValue) call(g_shown.values[i], 0x3c);
    for (uint8_t* marker : g_shown.markers)
        if (marker && visible(marker)) call(marker, 0x44);  // UIPicture_Draw
    if (g_shown.host->front_end) Movie_DrawFrame();
}

void XB_THISCALL page_update(uint8_t* menu, float dt) {
    UIMenu_Update(menu, dt);
    refresh();
    if (g_shown.host->front_end) SCR_LoadingPump();
}

void XB_THISCALL page_up(uint8_t*, uint32_t) {
    move_selection(-1);
    place_arrows();
}
void XB_THISCALL page_down(uint8_t*, uint32_t) {
    move_selection(1);
    place_arrows();
}
void XB_THISCALL page_left(uint8_t*, uint32_t) { input(kPcInputLeft); }
void XB_THISCALL page_right(uint8_t*, uint32_t) { input(kPcInputRight); }
void XB_THISCALL page_a(uint8_t* menu, uint32_t) {
    if (field<uint8_t>(menu, 0x30)) return;  // locked
    call(menu, 0x10);
    input(kPcInputAccept);
}
void XB_THISCALL page_accept(uint8_t*, uint32_t) { input(kPcInputAccept); }
void XB_THISCALL page_back(uint8_t*, uint32_t) { input(kPcInputBack); }

// In a level, Start (Escape) leaves the pages and resumes the game as the Options screen's does, saving first.
void XB_THISCALL page_start(uint8_t* menu, uint32_t pad) {
    const uint32_t original = field<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{g_shown.host_vtable}), 0x4c);
    // Apply and Exit is the root's last row.
    while (true) {
        PcPageView view;
        view.language = game_language();
        hooks().page_view(&view);
        const int32_t result = hooks().page_input(view.rows - 1, kPcInputAccept);
        if (result == kPcResultClose) break;
        if (result != kPcResultPage) {  // a sub-page's last row is Back: it went up a page
            hooks().page_input(-1, kPcInputBack);
        }
    }
    leave();
    title_function<WordMethod>(original)(menu, pad);
}

// Repeats while left / right is held (the menu's repeat state +0x32: 0x10 left, 0x20 right).
void XB_THISCALL page_repeat(uint8_t* menu) {
    if (menu[0x32] == 0x20)
        input(kPcInputRight);
    else if (menu[0x32] == 0x10)
        input(kPcInputLeft);
}

// Activated again (back from the Controller screen): the selection and the view.
void XB_THISCALL page_restore(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    refresh();
}

// Another screen comes (`next`: its object): the pages close unless it is the Controller screen (the session goes
// on and the pages show again when it comes back).
void XB_THISCALL page_deactivated(uint8_t* menu, uint32_t next) {
    uint8_t* const controller = field<uint8_t**>(field<uint8_t*>(menu, 8), 4)[5];
    if (g_shown.host->front_end && next == address_of(controller) && hooks().page_active()) return;
    if (hooks().page_active && hooks().page_active()) hooks().page_input(-1, kPcInputBack);  // undone
    leave();
}

uint8_t* XB_THISCALL page_deleting_dtor(uint8_t* menu, uint32_t flags) {
    const uint32_t original = field<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{g_shown.host_vtable}), 0);
    leave();
    return title_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t)>(original)(menu, flags);
}

void hide_panel(uint8_t* panel, const Host& h) {
    Shown& s = g_shown;
    for (uint32_t i = 0; i < field<uint32_t>(panel, 0x14); ++i) {
        uint8_t* const text = field<uint8_t**>(panel, 0xc)[i];
        if (in_list(element_name(text, 0x54), h.keep_texts) || s.hidden_count >= 96) continue;
        s.hidden[s.hidden_count++] = {text, visible(text)};
        set_visible(text, false);
    }
    for (uint32_t i = 0; i < field<uint32_t>(panel, 8); ++i) {
        uint8_t* const picture = field<uint8_t**>(panel, 0)[i];
        const char* const name = element_name(picture, 0x3c);
        if (in_list(name, h.keep_pictures) || !std::strcmp(name, h.arrow_left) || !std::strcmp(name, h.arrow_right) ||
            s.hidden_count >= 96)
            continue;
        s.hidden[s.hidden_count++] = {picture, visible(picture)};
        set_visible(picture, false);
    }
}

uint8_t* find_text(uint8_t* panel, const char* name) { return Panel_FindText(name, panel); }

uint8_t* find_picture(uint8_t* panel, const char* name) {
    uint8_t* const picture = Panel_FindPicture(panel, name);
    // Panel_FindPicture answers the shared default picture when the name is missing.
    return picture && !std::strcmp(element_name(picture, 0x3c), name) ? picture : nullptr;
}

void leave() {
    Shown& s = g_shown;
    uint8_t* const menu = s.menu;
    if (!menu) return;
    // Our rows and values go; the host's array, fields and vtable come back.
    for (int32_t i = 0; i < kMax; ++i) {
        if (s.items && s.items[i]) UIMenuItem_DeletingDtor(s.items[i], 1);
        if (s.values[i]) call(s.values[i], 0, 1);
        s.values[i] = nullptr;
    }
    if (s.items) own_function<void XB_CDECL(void*)>(&Z_Free)(s.items);
    field<uint8_t*>(menu, 4) = s.host_items;
    std::memcpy(menu + 0x20, s.host_fields, sizeof s.host_fields);
    field<uint32_t>(menu, 0) = s.host_vtable;
    for (int32_t i = 0; i < s.hidden_count; ++i) set_visible(s.hidden[i].element, s.hidden[i].was_visible);
    for (int k = 0; k < 2; ++k) {
        if (!s.arrows[k]) continue;
        UIPicture_SetPosition(s.arrows[k], s.arrow_at[k][0], s.arrow_at[k][1]);
        set_visible(s.arrows[k], s.arrow_visible[k]);
    }
    for (uint8_t* marker : s.markers)
        if (marker) call(marker, 0, 1);  // deleting destructor
    if (s.title) set_text(s.title, s.title_was);
    s = Shown{};
}

}  // namespace

bool pc_pages() {
    const PcSettingsHooks& h = pc_settings_hooks();
    return pc_menus() && h.page_begin && h.page_view && h.page_input && h.page_active;
}

const char* pc_text(const char* english) {
    const PcSettingsHooks& h = pc_settings_hooks();
    return h.text ? h.text(game_language(), english) : english;
}

void pc_pages_request(int32_t root) { g_request = root; }

bool pc_quit_row() { return pc_pages() && pc_settings_hooks().quit_game; }

void pc_add_quit_row(uint8_t* menu, int32_t last) {
    if (field<int16_t>(menu, 0x26) < 6) return;  // built without room for it
    // The paper has no room below the last row (its checkboxes are part of the art), so Quit Game sits in the
    // bottom bar's free right half, level with the button prompts (the help text +0x34), right-aligned.
    uint8_t* const help = field<uint8_t*>(menu, 0x34);
    uint8_t* const text = new_text(help ? help : field<uint8_t*>(row_item_of(menu, last), 0x10), 1.0f, 0);
    if (!text) return;
    set_visible(text, true);
    set_text(text, pc_text(kPcTextQuitGame));
    field<uint8_t>(text, 0x5c) = static_cast<uint8_t>((field<uint8_t>(text, 0x5c) & ~0x30) | 0x20);  // right
    set_position(text, 620.0f, help ? field<float>(help, 0x30) : 428.0f);
    // The footer's white: colour scheme 11 = the text's own colour, dimmed unless selected.
    UIMenu_SetItem(menu, 5, text, 1);  // its colour: MainMenu_Draw (white like the bar, dimmed unless selected)
    // The original's hidden fifth row ("DISABLED", standard edition) is placed at (0, 0), where half of it shows
    // in the corner: not drawn.
    if (last == 3) {
        if (uint8_t* const item = row_item_of(menu, 4))
            if (uint8_t* const disabled = field<uint8_t*>(item, 0x10)) set_visible(disabled, false);
    }
}

void pc_add_pause_quit_row(uint8_t* menu) {
    if (field<int16_t>(menu, 0x26) < 6) return;  // built without room for it
    uint8_t* const quit = field<uint8_t*>(row_item_of(menu, 4), 0x10);
    uint8_t* const restart = field<uint8_t*>(row_item_of(menu, 3), 0x10);
    if (!quit || !restart) return;
    uint8_t* const text = new_text(quit, 1.0f);
    if (!text) return;
    set_visible(text, true);
    set_text(text, pc_text(kPcTextQuitGame));
    const float spacing = field<float>(quit, 0x30) - field<float>(restart, 0x30);
    set_position(text, field<float>(quit, 0x2c), field<float>(quit, 0x30) + spacing);
    UIMenu_SetItem(menu, 5, text, 1);
}

void pc_set_game_volume(int32_t volume) {
    title_global<int32_t>(0x008dce88) = volume < 0 ? 0 : volume > 50 ? 50 : volume;
    SoundMenu_OnBack(0);
}

int32_t pc_controller_value(int32_t which) {
    switch (which) {
        case kPcControlInvert: return title_global<uint8_t>(0x008dce71);
        case kPcControlVibration: return title_global<uint8_t>(0x008dce72);
        case kPcControlLookX: return title_global<int32_t>(0x008dce74);
        case kPcControlLookY: return title_global<int32_t>(0x008dce78);
        default: return 0;
    }
}

void pc_set_controller_value(int32_t which, int32_t value) {
    const char* const sensitivity_x = reinterpret_cast<const char*>(uintptr_t{0x0082e06c});  // cl_sensitivityX
    const char* const sensitivity_y = reinterpret_cast<const char*>(uintptr_t{0x0082e05c});  // cl_sensitivityY
    const float scale = title_global<float>(0x0083ed7c);                                      // 0.04
    switch (which) {
        case kPcControlInvert: {
            const uint8_t inverted = value ? 1 : 0;
            title_global<uint8_t>(0x008dce71) = inverted;
            if (uint8_t* const pitch = title_global<uint8_t*>(0x00929c0c)) {  // m_pitch
                const float now = *reinterpret_cast<float*>(pitch + 0x1c);
                if (inverted ? now > 0.0f : 0.0f > now)
                    Cvar_SetValue(reinterpret_cast<const char*>(uintptr_t{0x0082e07c}),
                                  now * title_global<float>(0x0083eb34));  // m_pitch, * -1
            }
            break;
        }
        case kPcControlVibration: title_global<uint8_t>(0x008dce72) = value ? 1 : 0; break;
        case kPcControlLookX:
        case kPcControlLookY: {
            const int32_t speed = value < 0 ? 0 : value > 50 ? 50 : value;
            title_global<int32_t>(which == kPcControlLookX ? 0x008dce74 : 0x008dce78) = speed;
            Cvar_SetValue(which == kPcControlLookX ? sensitivity_x : sensitivity_y, static_cast<float>(speed) * scale);
            break;
        }
        default: break;
    }
}

int32_t pc_pages_take_request() {
    const int32_t root = g_request;
    g_request = -1;
    return root;
}

int32_t pc_page_return_screen(int32_t screen) {
    return pc_pages() && pc_settings_hooks().page_active() ? 6 : screen;
}

void pc_pages_enter(uint8_t* menu, int32_t host_index, int32_t root) {
    if (!pc_pages() || host_index < 0 || host_index > 1) return;
    if (g_shown.menu == menu) {  // back from the Controller screen: still shown
        refresh();
        return;
    }
    leave();
    uint8_t* const panel = panel_of(menu);
    const Host& h = kHosts[host_index];
    uint8_t* const label = panel ? find_text(panel, h.label) : nullptr;
    uint8_t* const value = panel ? find_text(panel, h.value) : nullptr;
    if (!label || !value) return;  // not the panel we know: the host stays as it is
    uint8_t** const items =
        reinterpret_cast<uint8_t**>(own_function<uint8_t* XB_CDECL(uint32_t)>(&Z_Malloc)(kMax * sizeof(uint32_t)));
    if (!items) return;
    if (root >= 0) hooks().page_begin(root);
    Shown& s = g_shown;
    s.menu = menu;
    s.host = &h;
    s.host_vtable = field<uint32_t>(menu, 0);
    s.host_items = field<uint8_t*>(menu, 4);
    std::memcpy(s.host_fields, menu + 0x20, sizeof s.host_fields);
    s.items = items;
    std::memset(items, 0, kMax * sizeof(uint32_t));
    field<uint8_t**>(menu, 4) = items;
    field<int16_t>(menu, 0x26) = 0;  // no rows until the first view (the host's count is its own array's)
    for (int32_t i = 0; i < kMax; ++i) {
        UIMenu_SetItem(menu, i, new_text(label, h.scale), 1);
        s.values[i] = new_text(value, h.scale);
    }
    // Our vtable: the host's, with the page slots.
    std::memcpy(s.vtable, reinterpret_cast<const void*>(uintptr_t{s.host_vtable}), sizeof s.vtable);
    s.vtable[0x00 / 4] = address_of(reinterpret_cast<const void*>(&page_deleting_dtor));
    s.vtable[0x28 / 4] = address_of(reinterpret_cast<const void*>(&page_draw));
    s.vtable[0x30 / 4] = address_of(reinterpret_cast<const void*>(&page_update));
    s.vtable[0x38 / 4] = address_of(reinterpret_cast<const void*>(&page_accept));
    s.vtable[0x40 / 4] = address_of(reinterpret_cast<const void*>(&page_restore));
    s.vtable[0x44 / 4] = address_of(reinterpret_cast<const void*>(&page_deactivated));
    if (!h.front_end) s.vtable[0x4c / 4] = address_of(reinterpret_cast<const void*>(&page_start));
    s.vtable[0x50 / 4] = address_of(reinterpret_cast<const void*>(&page_up));
    s.vtable[0x54 / 4] = address_of(reinterpret_cast<const void*>(&page_down));
    s.vtable[0x58 / 4] = address_of(reinterpret_cast<const void*>(&page_left));
    s.vtable[0x5c / 4] = address_of(reinterpret_cast<const void*>(&page_right));
    s.vtable[0x60 / 4] = address_of(reinterpret_cast<const void*>(&page_a));
    s.vtable[0x64 / 4] = address_of(reinterpret_cast<const void*>(&page_back));
    s.vtable[0xc0 / 4] = address_of(reinterpret_cast<const void*>(&page_repeat));
    field<uint32_t>(menu, 0) = address_of(s.vtable);
    // Fields: all rows shown (no scrolling), wraps, repeats left / right; dimmed rows drawn, skipped by up / down.
    field<int16_t>(menu, 0x20) = 0;
    field<int16_t>(menu, 0x28) = kMax;
    field<uint16_t>(menu, 0x2a) = static_cast<uint16_t>((field<uint16_t>(menu, 0x2a) & ~0x0049u) | 0x0182u);
    menu[0x30] = 0;
    // The panel: title, background and the arrows stay; the host's own rows and values hide.
    hide_panel(panel, h);
    s.title = find_text(panel, h.title);
    if (s.title) {
        const uint32_t rep = field<uint32_t>(s.title, 0x1c);
        std::snprintf(s.title_was, sizeof s.title_was, "%s", rep ? reinterpret_cast<const char*>(uintptr_t{rep + 6}) : "");
    }
    for (int k = 0; k < 2; ++k) s.markers[k] = make_marker(k == 0);
    const char* const arrow_names[2] = {h.arrow_left, h.arrow_right};
    for (int k = 0; k < 2; ++k) {
        s.arrows[k] = find_picture(panel, arrow_names[k]);
        if (!s.arrows[k]) continue;
        s.arrow_at[k][0] = field<float>(s.arrows[k], 0x14);
        s.arrow_at[k][1] = field<float>(s.arrows[k], 0x18);
        s.arrow_visible[k] = visible(s.arrows[k]);
    }
    refresh();
}

}  // namespace game
