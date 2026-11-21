// The in-game PC menu (pc_menu.h). Runs on the title's thread inside the renderer's
// present(), so it only reads and asks; the title applies changes at its frame end.

#include "pc_menu.h"

#include <atomic>

#include <SDL3/SDL_events.h>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bindings.h"
#include "cheats.h"
#include "devices.h"
#include "enhanced_graphics.h"
#include "fe_pc.h"
#include "game_window.h"
#include "float_env.h"
#include "imgui.h"
#include "log.h"
#include "patches.h"
#include "settings.h"
#include "settings_schema.h"
#include "settings_widgets.h"
#include "title_data.h"
#include "ui_text.h"
#include "snd_env.h"
#include "sound_engine.h"

namespace loader {

namespace {

constexpr int kGameFov = 70;  // cg_fov's default (cgame cvarTable)
platform::Settings g_on_open;  // the live settings when the menu opened (what closing saves against)
ui::KeyTable g_key_table;     // the Controls tab (events and drawing both on the presenting thread)

}  // namespace

// The settings the menu changes while playing (platform::kLive), as settings.ini text, from what runs now.
namespace {
std::string g_pad_glyphs = "auto";  // the pad-glyphs setting as last applied (input keeps only the override)
}  // namespace

void set_live_pad_glyphs(const std::string& value) { g_pad_glyphs = value.empty() ? "auto" : value; }

platform::Settings live_values() {
    platform::Settings v;
    ui::put_setting(v, "graphics", render::graphics_enhanced() ? "enhanced" : "original");
    platform::set_combined_setting(v, "graphics-features", render::graphics_features().format());
    ui::put_setting(v, "audio", audio::enhanced() ? "enhanced" : "original");
    ui::put_setting(v, "hit-markers", hit_markers() ? "on" : "off");
    ui::put_setting(v, "god-mode", god_mode() ? "on" : "off");
    ui::put_setting(v, "infinite-ammo", infinite_ammo() ? "on" : "off");
    ui::put_setting(v, "free-camera", free_camera() ? "on" : "off");
    platform::set_combined_setting(v, "enhanced", audio::enhanced_features().format());
    platform::set_combined_setting(v, "volume", audio::category_volumes().format());
    platform::set_combined_setting(v, "reverb", game::SndEnv_Tuning().format());
    if (const float fov = field_of_view(); fov > 0) ui::put_setting(v, "fov", std::to_string(std::lround(fov)));
    ui::put_setting(v, "mouse-sensitivity", ui::number_text(mouse_sensitivity()));
    platform::set_combined_setting(v, "keys", input::format_bindings(input::bindings()));
    platform::set_combined_setting(v, "pad", input::format_pad_bindings(input::pad_bindings()));
    ui::put_setting(v, "trigger-threshold", ui::number_text(input::trigger_threshold()));
    ui::put_setting(v, "pad-glyphs", g_pad_glyphs);
    return v;
}

namespace {

std::string combined(const platform::Settings& v, const char* name) {
    return platform::combined_setting(v, name).value_or("");
}

}  // namespace

// Applies what differs between two live_values() forms.
void apply_live(const platform::Settings& before, const platform::Settings& after) {
    const auto differs = [&](const char* key) { return before.get(key) != after.get(key); };
    const auto differs_combined = [&](const char* name) { return combined(before, name) != combined(after, name); };
    if (differs("graphics") || differs_combined("graphics-features"))
        render::set_graphics(ui::setting_value(after, "graphics") == "enhanced",
                             render::GraphicsFeatures::parse(combined(after, "graphics-features")));
    if (differs("audio")) audio::set_enhanced(ui::setting_value(after, "audio") == "enhanced");
    if (differs_combined("enhanced")) audio::set_enhanced_features(audio::EnhancedFeatures::parse(combined(after, "enhanced")));
    if (differs_combined("volume")) audio::set_category_volumes(audio::CategoryVolumes::parse(combined(after, "volume")));
    if (differs_combined("reverb")) game::SndEnv_SetTuning(audio::ReverbTuning::parse(combined(after, "reverb")));
    if (differs("fov")) set_field_of_view(std::strtof(ui::setting_value(after, "fov").c_str(), nullptr));
    if (differs_combined("keys")) {
        input::Bindings bindings;
        input::parse_bindings(combined(after, "keys"), bindings);
        input::set_bindings(bindings);
    }
    if (differs_combined("pad")) {
        input::Bindings pad;
        input::parse_pad_bindings(combined(after, "pad"), pad);
        input::set_pad_bindings(pad);
    }
    if (differs("pad-glyphs")) {
        g_pad_glyphs = ui::setting_value(after, "pad-glyphs");
        input::set_pad_family_override(g_pad_glyphs == "xbox"          ? std::optional(input::PadFamily::Xbox)
                                       : g_pad_glyphs == "playstation" ? std::optional(input::PadFamily::PlayStation)
                                       : g_pad_glyphs == "nintendo"    ? std::optional(input::PadFamily::Nintendo)
                                       : g_pad_glyphs == "steam"       ? std::optional(input::PadFamily::Steam)
                                                                       : std::nullopt);
    }
    if (differs("trigger-threshold"))
        input::set_trigger_threshold(std::strtof(ui::setting_value(after, "trigger-threshold").c_str(), nullptr));
    if (differs("hit-markers")) set_hit_markers(ui::setting_value(after, "hit-markers") == "on");
    if (differs("god-mode")) set_god_mode(ui::setting_value(after, "god-mode") == "on");
    if (differs("infinite-ammo")) set_infinite_ammo(ui::setting_value(after, "infinite-ammo") == "on");
    if (differs("free-camera")) set_free_camera(ui::setting_value(after, "free-camera") == "on");
    if (differs("mouse-sensitivity"))
        set_mouse_sensitivity(std::strtof(ui::setting_value(after, "mouse-sensitivity").c_str(), nullptr));
}

namespace {

// The field of view: the game's own value until one is chosen, campaign only.
bool fov_widget(const platform::SettingUi& ui, platform::Settings& values, bool& changed) {
    if (!field_of_view_supported()) {
        ImGui::TextDisabled("Field of view: campaign only for now.");
        return true;
    }
    const float game = game_field_of_view();
    const std::optional<std::string> asked = values.get("fov");
    int fov = asked ? std::atoi(asked->c_str()) : game > 0 ? static_cast<int>(std::lround(game)) : kGameFov;
    if (ImGui::SliderInt(ui.label, &fov, 50, 120, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        ui::put_setting(values, "fov", std::to_string(fov));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Default") && asked) {
        values.set("fov", std::to_string(kGameFov));  // asked for again: back to the game's own
        changed = true;
    }
    ui::help_marker(ui.tip);
    if (game <= 0) ImGui::TextDisabled("Applies once a mission is running.");
    return true;
}

void live_tab(std::initializer_list<platform::SettingGroup> groups) {
    const platform::Settings before = live_values();
    platform::Settings values = before;
    ui::DrawOptions options;
    options.require_flags = platform::kLive;
    options.custom = [](const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& v,
                        bool& changed) { return std::string_view(def.key) == "fov" && fov_widget(ui, v, changed); };
    ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
    if (ui::draw_settings(groups, values, options)) apply_live(before, values);
    ImGui::PopItemWidth();
}

}  // namespace

namespace {

// The one-time notice (1.0.6): the controller works directly and starts with the Modern layout. Shown once, in
// the PC menu's window, at the first menu the game shows; OK (or closing the menu) records it in settings.ini.
constexpr const char* kNoticeId = "controller-1.0.6";
enum class Notice { Unknown, Pending, Showing, Done };
std::atomic<Notice> g_notice{Notice::Unknown};
std::atomic<bool> g_notice_allowed{true};  // set_notice_allowed
std::atomic<bool> g_notice_forced{false};
std::atomic<bool> g_notice_dismissed{false};  // a controller / key press closed it (pc_menu_event)

void notice_done() {
    g_notice = Notice::Done;
    platform::Settings file = platform::load_settings();
    file.set("notice-seen", kNoticeId);
    if (!platform::save_settings(file))
        LOG_WARN("loader", "cannot save the settings to %s", platform::settings_path().string().c_str());
    LOG_INFO("loader", "notice %s shown", kNoticeId);
}

// The notice's texts (English; lang/*.po translate them, lang/template.pot lists them).
constexpr const char* kNoticeTitle = "What's new";
constexpr const char* kNoticeHeading = "New controller support";
constexpr const char* kNoticeIntro =
    "Your controller now talks to the game directly instead of pretending to be the original Xbox pad, and it starts "
    "with a modern layout:";
constexpr const char* kNoticeLine1 = "RT fire, LT aim, RB grenade";
constexpr const char* kNoticeLine2 = "A jump, B crouch, X use / reload, Y next weapon";
constexpr const char* kNoticeLine3 = "L3 sprint, R3 melee, D-pad left / right lean";
constexpr const char* kNoticeOutro =
    "The game's original layouts, every single button, invert, vibration and look speed are under Options > Controls "
    "> Controller (and in the launcher). The on-screen prompts show your controller's own buttons.";
constexpr const char* kNoticeOk = "OK";
constexpr const char* kNoticeHint = "A, B or Start on the controller, Enter or a click";

// A notice text in the game's language (UTF-8 for Dear ImGui).
std::string notice_text(const char* english) {
    const int32_t language = game::title_global<int32_t>(0x0095f5dc);  // g_languageIndex
    return platform::ui_text_utf8(language, english).value_or(english);
}

bool draw_notice() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    const std::string title = notice_text(kNoticeTitle) + "###notice";
    ImGui::Begin(title.c_str(), &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
    ImGui::SeparatorText(notice_text(kNoticeHeading).c_str());
    ImGui::TextWrapped("%s", notice_text(kNoticeIntro).c_str());
    for (const char* line : {kNoticeLine1, kNoticeLine2, kNoticeLine3}) ImGui::BulletText("%s", notice_text(line).c_str());
    ImGui::TextWrapped("%s", notice_text(kNoticeOutro).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::Button(notice_text(kNoticeOk).c_str(), ImVec2(ImGui::GetFontSize() * 6.0f, 0))) open = false;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", notice_text(kNoticeHint).c_str());
    if (g_notice_dismissed.exchange(false)) open = false;
    ImGui::End();
    if (!open) notice_done();
    return open;
}

// The one-time sprint question (1.0.7): the campaign's own sprint stays off by default (as on the Xbox); after the
// notice, at a menu, players are asked once whether they want it. settings.ini keeps `sprint-asked` (and `sprint =
// on` for yes); not asked when sprint is already on.
enum class Ask { Unknown, Pending, Showing, Done };
std::atomic<Ask> g_ask{Ask::Unknown};
std::atomic<int> g_ask_choice{0};   // the highlighted answer: 0 keep it off, 1 turn it on
std::atomic<int> g_ask_answer{-1};  // a controller / key press answered (pc_menu_event)

constexpr const char* kAskTitle = "Sprint";
constexpr const char* kAskText =
    "The campaign has the game's own sprint (faster running with stamina), but the original game never lets you use "
    "it. Do you want to sprint in the campaign?";
constexpr const char* kAskNote =
    "Off is the original game. You can change this at any time under Options > Controls > Sprint, or in the launcher.";
constexpr const char* kAskOff = "Keep it off (original)";
constexpr const char* kAskOn = "Turn sprint on";
constexpr const char* kAskHint = "Left / right to choose, A or Enter to confirm, B or Escape keeps it off";

void ask_done(bool on) {
    g_ask = Ask::Done;
    platform::Settings file = platform::load_settings();
    file.set("sprint-asked", "1");
    if (on) file.set("sprint", "on");
    if (!platform::save_settings(file))
        LOG_WARN("loader", "cannot save the settings to %s", platform::settings_path().string().c_str());
    const bool now = on && set_sprint_now(true);
    LOG_INFO("loader", "sprint question: %s%s", on ? "on" : "kept off", on && !now ? " (from the next start)" : "");
}

bool draw_ask() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    const std::string title = notice_text(kAskTitle) + "###sprint";
    ImGui::Begin(title.c_str(), &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextWrapped("%s", notice_text(kAskText).c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("%s", notice_text(kAskNote).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    int answer = -1;
    const char* labels[2] = {kAskOff, kAskOn};
    for (int i = 0; i < 2; ++i) {
        if (i) ImGui::SameLine();
        const bool chosen = g_ask_choice.load() == i;
        if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(notice_text(labels[i]).c_str())) answer = i;
        if (chosen) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) g_ask_choice = i;
    }
    ImGui::TextDisabled("%s", notice_text(kAskHint).c_str());
    if (const int pressed = g_ask_answer.exchange(-1); pressed >= 0) answer = pressed;
    ImGui::End();
    if (!open) answer = 0;  // the window's close box: as it was
    if (answer < 0) return true;
    ask_done(answer == 1);
    return false;
}

}  // namespace

std::vector<const char*> notice_texts() {
    return {kNoticeTitle, kNoticeHeading, kNoticeIntro, kNoticeLine1, kNoticeLine2, kNoticeLine3, kNoticeOutro,
            kNoticeOk, kNoticeHint, kAskTitle, kAskText, kAskNote, kAskOff, kAskOn, kAskHint};
}

void set_notice_allowed(bool allowed, bool forced) {
    g_notice_allowed = allowed;
    g_notice_forced = forced;
}

void notice_poll() {
    if (!g_notice_allowed.load()) return;
    Notice state = g_notice.load();
    if (state == Notice::Unknown) {
        const auto seen = platform::load_settings().get("notice-seen");
        state = seen && *seen == kNoticeId && !g_notice_forced.load() ? Notice::Done : Notice::Pending;
        g_notice = state;
        LOG_DEBUG("loader", "notice %s: %s", kNoticeId, state == Notice::Done ? "seen" : "pending (shows at the first menu)");
    }
    if (state == Notice::Pending && input::in_menu()) {
        g_notice = Notice::Showing;
        LOG_DEBUG("loader", "notice %s: a menu shows, opening it", kNoticeId);
        render::request_pc_menu();
        return;
    }
    if (state != Notice::Done) return;
    Ask ask = g_ask.load();
    if (ask == Ask::Unknown) {
        const platform::Settings file = platform::load_settings();
        const bool asked = file.get("sprint-asked").has_value() || file.get("sprint") == std::optional<std::string>("on");
        ask = asked && !g_notice_forced.load() ? Ask::Done : Ask::Pending;
        g_ask = ask;
    }
    if (ask != Ask::Pending || !input::in_menu()) return;
    g_ask = Ask::Showing;
    g_ask_choice = 0;
    LOG_DEBUG("loader", "sprint question: a menu shows, opening it");
    render::request_pc_menu();
}

namespace {
bool g_pc_menu_settings = true;  // set_pc_menu_settings: the settings tabs (--dev); off: the notice only
}  // namespace

void set_pc_menu_settings(bool on) { g_pc_menu_settings = on; }

bool pc_menu() {
    if (g_notice.load() == Notice::Showing) return draw_notice();
    if (g_ask.load() == Ask::Showing) return draw_ask();
    if (!g_pc_menu_settings) return false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    bool open = true;
    ImGui::Begin("PC settings", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    ImGui::TextDisabled("The game keeps running. ` or Escape closes this menu; changes are saved.");
    if (ImGui::BeginTabBar("pc_menu_tabs")) {
        using G = platform::SettingGroup;
        if (ImGui::BeginTabItem("General")) {
            live_tab({G::Display});
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics")) {
            live_tab({G::Graphics});
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sound")) {
            live_tab({G::Sound});
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            live_tab({G::Controls});
            ImGui::SeparatorText("Keys");
            input::Bindings bindings = input::bindings();
            if (ui::draw_key_table(g_key_table, bindings)) input::set_bindings(bindings);
            ImGui::EndTabItem();
        } else {
            g_key_table.waiting.reset();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) open = false;
    ImGui::End();
    return open;
}

bool pc_menu_event(const void* sdl_event) {
    // The notice closes with the pad's A, B or Start, or Enter (ImGui's own pad navigation may not see the pad).
    if (g_notice.load() == Notice::Showing) {
        const auto* event = static_cast<const SDL_Event*>(sdl_event);
        const bool pad = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                         (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                          event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST ||
                          event->gbutton.button == SDL_GAMEPAD_BUTTON_START);
        const bool key = event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
                         (event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER);
        if (pad || key) {
            g_notice_dismissed = true;
            return true;
        }
    }
    // The sprint question: left / right choose, A, Start or Enter confirm, B or Escape keep it off.
    if (g_ask.load() == Ask::Showing) {
        const auto* event = static_cast<const SDL_Event*>(sdl_event);
        if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            switch (event->gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: g_ask_choice = 0; return true;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: g_ask_choice = 1; return true;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                case SDL_GAMEPAD_BUTTON_START: g_ask_answer = g_ask_choice.load(); return true;
                case SDL_GAMEPAD_BUTTON_EAST: g_ask_answer = 0; return true;
                default: return true;
            }
        }
        if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            if (event->gaxis.value < -16000) g_ask_choice = 0;
            if (event->gaxis.value > 16000) g_ask_choice = 1;
            return true;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            switch (event->key.scancode) {
                case SDL_SCANCODE_LEFT: g_ask_choice = 0; return true;
                case SDL_SCANCODE_RIGHT: g_ask_choice = 1; return true;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER: g_ask_answer = g_ask_choice.load(); return true;
                default: break;
            }
        }
    }
    input::Bindings bindings = input::bindings();
    bool changed = false;
    const bool taken = ui::key_table_event(g_key_table, bindings, sdl_event, changed);
    if (changed) input::set_bindings(bindings);
    return taken;
}

void pc_menu_shown(bool open) {
    input::set_game_input_blocked(open);
    if (!open && g_notice.load() == Notice::Showing) notice_done();  // closed with ` or Escape
    if (!open && g_ask.load() == Ask::Showing) ask_done(false);       // closed with ` or Escape: kept off
    if (!open && g_notice.load() == Notice::Done) input::wait_for_release();  // the press that closed it stays ours
    g_key_table = {};
    if (open) {
        g_on_open = live_values();
        return;
    }
    // What changed goes into settings.ini, one line per setting (the defaults removed).
    const platform::Settings now = live_values();
    platform::Settings file = platform::load_settings();
    std::string saved;
    for (const platform::SettingDef& def : platform::setting_defs()) {
        if (!(def.flags & platform::kLive) || now.get(def.key) == g_on_open.get(def.key)) continue;
        if (const std::optional<std::string> value = now.get(def.key))
            file.set(def.key, *value);
        else
            file.erase(def.key);
        saved += std::string(saved.empty() ? "" : " ") + def.key + "=" + ui::setting_value(now, def.key);
    }
    if (saved.empty()) return;
    if (platform::save_settings(file))
        LOG_INFO("loader", "PC menu saved: %s", saved.c_str());
    else
        LOG_WARN("loader", "cannot save the PC menu's settings to %s", platform::settings_path().string().c_str());
}

void install_menu_settings() {
    // Called from title threads (the front end's menus): host code under the title's float modes.
    game::PcSettingsHooks& hooks = game::pc_settings_hooks();
    hooks.graphics_enhanced = [] { return render::graphics_enhanced(); };
    hooks.set_graphics_enhanced = [](bool on) {
        platform::HostFloatEnv env;
        render::set_graphics(on, render::graphics_features());
        LOG_INFO("loader", "graphics %s (front-end menu)", on ? "enhanced" : "original");
    };
    hooks.save_graphics = [] {
        platform::HostFloatEnv env;
        const bool graphics = render::graphics_enhanced();
        platform::Settings settings = platform::load_settings();
        settings.set("graphics", graphics ? "enhanced" : "original");
        if (platform::save_settings(settings))
            LOG_INFO("loader", "%s graphics saved (front-end menu)", graphics ? "enhanced" : "original");
        else
            LOG_WARN("loader", "cannot save the graphics setting to %s", platform::settings_path().string().c_str());
    };
}

}  // namespace loader
