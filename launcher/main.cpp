// cod_bro_launcher: where the player points the game at their game files and sets the
// options, saved in settings.ini (platform/settings.h), then starts the game. The game
// runs without it (it reads the same file); it starts the launcher by itself when it
// finds no game files.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // WinMain on Windows

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bindings.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "log.h"
#include "output_devices.h"
#include "category_volumes.h"
#include "enhanced_features.h"
#include "reverb_tuning.h"
#include "paths.h"
#include "program.h"
#include "settings.h"
#include "update_check.h"
#include "updater.h"

namespace fs = std::filesystem;

namespace {

// The game's defaults (Options in loader/main.cpp); only other values are saved, so
// a changed default still reaches players who never touched that setting.
struct Choices {
    std::string assets;  // empty: <executable dir>/assets
    std::string aspect = "16:9";  // 4:3, 16:9, or wider W:H (experimental)
    bool fullscreen = false;
    int scale = 2;
    float render_scale = 0;  // 0: the window's (fullscreen: the screen's) height
    int anisotropy = 16;
    float brightness = 1.0f;
    bool sharp_text = true;
    int fps = 60;
    std::string d3d9 = "auto";  // auto, dxvk, system (Windows only)
    float mouse_sensitivity = 0.11f;
    input::Bindings bindings = input::default_bindings();
    bool aim_assist = true;
    std::string button_prompts = "auto";  // auto, keyboard, controller
    bool silent = false;
    std::string audio = "enhanced";  // original, enhanced
    std::string speakers = "auto";   // auto, stereo, headphones, quad, 5.1, 7.1
    std::string audio_device;        // empty: the system's
    audio::ReverbTuning reverb;
    audio::CategoryVolumes volumes;
    audio::EnhancedFeatures features;
    std::string language = "english";  // the console's: english, german, french, spanish, italian
    int net_port = 3074;  // system link: UDP net_port .. net_port + 3
    // Launcher only: saved as "false" (which the game skips) when off.
    bool check_updates = true;
};

// The width over the height of an aspect setting ("W:H"), or 0 if it is not one.
float aspect_ratio(const std::string& aspect) {
    float w = 0, h = 0;
    char end = 0;
    if (std::sscanf(aspect.c_str(), "%f:%f%c", &w, &h, &end) != 2 || !(w > 0) || !(h > 0)) return 0;
    return w / h;
}

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

Choices read_choices(const platform::Settings& settings) {
    Choices c;
    if (auto v = settings.get("assets")) c.assets = *v;
    // What the game takes (--aspect): 4:3, or 16:9 up to 4:1.
    if (auto v = settings.get("aspect");
        v && (*v == "4:3" || (aspect_ratio(*v) >= 16.0f / 9.0f - 0.001f && aspect_ratio(*v) <= 4.0f)))
        c.aspect = *v;
    if (auto v = settings.get("fullscreen")) c.fullscreen = *v == "true";
    if (auto v = settings.get("scale")) c.scale = std::clamp(std::atoi(v->c_str()), 1, 8);
    if (auto v = settings.get("render-scale")) c.render_scale = std::clamp(std::strtof(v->c_str(), nullptr), 0.5f, 8.0f);
    if (auto v = settings.get("anisotropy")) c.anisotropy = std::clamp(std::atoi(v->c_str()), 1, 16);
    if (auto v = settings.get("brightness")) c.brightness = std::clamp(std::strtof(v->c_str(), nullptr), 0.5f, 2.0f);
    if (auto v = settings.get("sharp-text")) c.sharp_text = *v != "off";
    if (auto v = settings.get("fps")) c.fps = *v == "30" ? 30 : 60;
    if (auto v = settings.get("d3d9"); v && (*v == "dxvk" || *v == "system")) c.d3d9 = *v;
    if (auto v = settings.get("mouse-sensitivity"))
        c.mouse_sensitivity = std::clamp(std::strtof(v->c_str(), nullptr), 0.01f, 10.0f);
    if (auto v = settings.get("keys")) input::parse_bindings(*v, c.bindings);
    if (auto v = settings.get("aim-assist")) c.aim_assist = *v != "off";
    if (auto v = settings.get("button-prompts"); v && (*v == "keyboard" || *v == "controller")) c.button_prompts = *v;
    if (auto v = settings.get("silent")) c.silent = *v == "true";
    if (auto v = settings.get("audio"); v && *v == "original") c.audio = *v;
    if (auto v = settings.get("speakers");
        v && (*v == "stereo" || *v == "headphones" || *v == "quad" || *v == "5.1" || *v == "7.1"))
        c.speakers = *v;
    if (auto v = settings.get("audio-device")) c.audio_device = *v;
    if (auto v = settings.get("reverb")) c.reverb = audio::ReverbTuning::parse(*v);
    if (auto v = settings.get("volume")) c.volumes = audio::CategoryVolumes::parse(*v);
    if (auto v = settings.get("enhanced")) c.features = audio::EnhancedFeatures::parse(*v);
    if (auto v = settings.get("language")) c.language = *v;
    if (auto v = settings.get("net-port")) c.net_port = std::clamp(std::atoi(v->c_str()), 1024, 65532);
    if (auto v = settings.get("check-updates")) c.check_updates = *v != "false";
    return c;
}

// Keeps keys the launcher does not know (added by hand).
void write_choices(const Choices& c, platform::Settings& settings) {
    const Choices d;
    auto put = [&](const char* key, bool is_default, const std::string& value) {
        if (is_default)
            settings.erase(key);
        else
            settings.set(key, value);
    };
    put("assets", c.assets.empty(), c.assets);
    put("aspect", c.aspect == d.aspect, c.aspect);
    put("fullscreen", c.fullscreen == d.fullscreen, c.fullscreen ? "true" : "false");
    put("scale", c.scale == d.scale, std::to_string(c.scale));
    put("render-scale", c.render_scale == d.render_scale, number(c.render_scale));
    put("anisotropy", c.anisotropy == d.anisotropy, std::to_string(c.anisotropy));
    put("brightness", c.brightness == d.brightness, number(c.brightness));
    put("sharp-text", c.sharp_text == d.sharp_text, c.sharp_text ? "on" : "off");
    put("fps", c.fps == d.fps, std::to_string(c.fps));
    put("d3d9", c.d3d9 == d.d3d9, c.d3d9);
    put("mouse-sensitivity", c.mouse_sensitivity == d.mouse_sensitivity, number(c.mouse_sensitivity));
    put("keys", c.bindings == d.bindings, input::format_bindings(c.bindings));
    put("aim-assist", c.aim_assist == d.aim_assist, c.aim_assist ? "on" : "off");
    put("button-prompts", c.button_prompts == d.button_prompts, c.button_prompts);
    put("silent", c.silent == d.silent, c.silent ? "true" : "false");
    put("audio", c.audio == d.audio, c.audio);
    put("speakers", c.speakers == d.speakers, c.speakers);
    put("audio-device", c.audio_device.empty(), c.audio_device);
    put("reverb", c.reverb == d.reverb, c.reverb.format());
    put("volume", c.volumes == d.volumes, c.volumes.format());
    put("enhanced", c.features == d.features, c.features.format());
    put("language", c.language == d.language, c.language);
    put("net-port", c.net_port == d.net_port, std::to_string(c.net_port));
    put("check-updates", c.check_updates == d.check_updates, "false");
}

fs::path asset_dir(const Choices& c) { return c.assets.empty() ? platform::default_asset_dir() : fs::path(c.assets); }

bool has_multiplayer(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "codmp_xboxf.xbe", ec);
}

void open_folder(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string url = fs::absolute(dir, ec).generic_string();
    url = (url.starts_with("/") ? "file://" : "file:///") + url;
    if (!SDL_OpenURL(url.c_str())) LOG_WARN("launcher", "cannot open %s: %s", url.c_str(), SDL_GetError());
}

// SDL's folder dialog answers from another thread.
std::mutex g_picked_mutex;
std::optional<std::string> g_picked;

void SDLCALL folder_picked(void*, const char* const* files, int) {
    if (!files || !files[0]) return;  // cancelled or failed
    std::lock_guard lock(g_picked_mutex);
    g_picked = files[0];
}

void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

class Launcher {
public:
    explicit Launcher(SDL_Window* window)
        : window_(window), settings_(platform::load_settings()), choices_(read_choices(settings_)) {
        launcher::remove_update_leftovers(platform::executable_dir());
        if (choices_.check_updates) update_.emplace(COD_BRO_RELEASES, COD_BRO_VERSION);
    }

    // While a key cell waits for its key (controls()): takes key presses, mouse buttons
    // and the wheel before the UI sees them. True if it took `event`.
    bool capture_event(const SDL_Event& event) {
        if (!capturing_) return false;
        input::InputCode input = 0;
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) return true;
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    capturing_.reset();
                    return true;
                }
                input = static_cast<input::InputCode>(event.key.scancode);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button < 1 || event.button.button > 5) return true;
                input = static_cast<input::InputCode>(input::kMouseButton + event.button.button);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.y == 0.0f) return true;
                input = event.wheel.y > 0.0f ? input::kWheelUp : input::kWheelDown;
                break;
            case SDL_EVENT_KEY_UP:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return true;
            default:
                return false;
        }
        if (input::is_reserved(input)) {
            key_notice_ = input::input_name(input) + " is kept for the menus and the loader.";
        } else {
            input::bind(choices_.bindings, capturing_->first, capturing_->second, input);
            key_notice_.clear();
        }
        capturing_.reset();
        return true;
    }

    // One frame of the UI. Returns false once the launcher should close.
    bool frame() {
        {
            std::lock_guard lock(g_picked_mutex);
            if (g_picked) {
                choices_.assets = fs::path(*g_picked) == platform::default_asset_dir() ? "" : *g_picked;
                g_picked.reset();
            }
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        update_notice();
        if (!platform::has_game_files(asset_dir(choices_)))
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "No game files yet: see Game files on the General tab.");
        // The tabs scroll on their own; the buttons stay at the bottom.
        const ImGuiStyle& style = ImGui::GetStyle();
        float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2 + style.SeparatorTextBorderSize;
        if (!error_.empty()) footer += ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("tabs", ImVec2(0, -footer));
        if (ImGui::BeginTabBar("sections")) {
            tab("General", [&] { general(); });
            tab("Graphics", [&] { graphics(); });
            tab("Sound", [&] { sound(); });
            tab("Controls", [&] { controls(); });
            tab("Multiplayer", [&] { system_link(); });
            tab("Help", [&] { reporting(); });
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        const bool keep_open = buttons();
        ImGui::End();
        return keep_open && !restart_after_update();
    }

private:
    template <typename Body>
    static void tab(const char* name, Body body) {
        if (!ImGui::BeginTabItem(name)) return;
        ImGui::Spacing();
        // Controls get the left 60%, labels the rest.
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        body();
        ImGui::PopItemWidth();
        ImGui::EndTabItem();
    }

    void general() {
        game_files();
        ImGui::SeparatorText("Language");
        language();
        updates();
    }

    // At the top while a newer release is out: a link to it and, in an installed
    // release (not a dev build), Update now.
    void update_notice() {
        if (!update_) return;
        const launcher::UpdateCheck::Result r = update_->result();
        if (r.status != launcher::UpdateCheck::Status::Available) return;
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "Version %s is available (this is %s).", r.version.c_str(),
                           COD_BRO_VERSION);
        ImGui::SameLine();
        if (ImGui::SmallButton("Download page") && !SDL_OpenURL(r.url.c_str()))
            LOG_WARN("launcher", "cannot open %s: %s", r.url.c_str(), SDL_GetError());
        if (!updater_ && launcher::is_release_install(platform::executable_dir())) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Update now"))
                updater_.emplace(COD_BRO_RELEASES, r.tag, r.version, COD_BRO_PACKAGE, platform::executable_dir());
        }
        if (!updater_) return;
        const launcher::Updater::Result u = updater_->result();
        if (u.status == launcher::Updater::Status::Failed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "Update failed: %s", u.message.c_str());
        else
            ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "%s", u.message.c_str());
    }

    // While an update downloads or installs, the game must not start.
    bool updating() const {
        return updater_ && updater_->result().status != launcher::Updater::Status::Failed;
    }

    // Once an update is installed: saves the options and starts the new launcher.
    // Returns true when this one should close.
    bool restart_after_update() {
        if (!updater_ || restarted_ || updater_->result().status != launcher::Updater::Status::Installed) return false;
        restarted_ = true;
        write_choices(choices_, settings_);
        platform::save_settings(settings_);
        if (platform::start_program(platform::sibling_executable("cod_bro_launcher"))) return true;
        error_ = "The update is installed; start the launcher again to use it.";
        return false;
    }

    void updates() {
        ImGui::SeparatorText("Updates");
        if (ImGui::Checkbox("Check for updates", &choices_.check_updates) && choices_.check_updates && !update_)
            update_.emplace(COD_BRO_RELEASES, COD_BRO_VERSION);
        help_marker("When the launcher opens, asks GitHub for the newest release and shows a link to it "
                    "if it is newer. Update now downloads and installs it, then restarts the launcher; your "
                    "game files, saves and settings stay as they are. Nothing is installed by itself.");
        ImGui::SameLine();
        const char* status = "";
        if (update_ && choices_.check_updates) {
            switch (update_->result().status) {
                case launcher::UpdateCheck::Status::Checking: status = ": checking..."; break;
                case launcher::UpdateCheck::Status::UpToDate: status = ": up to date"; break;
                case launcher::UpdateCheck::Status::Available: status = ": an update is available"; break;
                case launcher::UpdateCheck::Status::Failed: status = ": could not check"; break;
            }
        }
        ImGui::TextDisabled("Version %s%s", COD_BRO_VERSION, status);
    }

    void game_files() {
        ImGui::SeparatorText("Game files");
        const fs::path dir = asset_dir(choices_);
        const bool found = platform::has_game_files(dir);
        ImGui::TextWrapped("%s", dir.string().c_str());
        if (found)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "Game files found.");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "Copy your game dump into the assets folder.");
        if (ImGui::Button("Open folder")) open_folder(dir);
        ImGui::SameLine();
        if (ImGui::Button("Choose another folder...")) {
            const std::string start = dir.string();
            SDL_ShowOpenFolderDialog(folder_picked, nullptr, window_, start.c_str(), false);
        }
        if (!choices_.assets.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Use the assets folder")) choices_.assets.clear();
        }
    }

    void graphics() {
        ImGui::SeparatorText("Window");
        // The screen's own shape, offered when it is wider than 16:9 (3440x1440 is a little
        // wider than 21:9).
        std::string screen;
        if (const SDL_DisplayMode* display = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)))
            if (display->h > 0 && display->w * 9 > display->h * 16 + 9)
                screen = std::to_string(display->w) + ":" + std::to_string(display->h);
        std::vector<std::pair<std::string, std::string>> aspects = {
            {"16:9", "16:9 (widescreen)"},
            {"4:3", "4:3 (original)"},
            {"21:9", "21:9 ultrawide (experimental)"},
            {"32:9", "32:9 super ultrawide (experimental)"}};
        if (!screen.empty() && screen != "21:9" && screen != "32:9")
            aspects.push_back({screen, "Your screen, " + screen + " (experimental)"});
        if (std::none_of(aspects.begin(), aspects.end(), [&](const auto& a) { return a.first == choices_.aspect; }))
            aspects.push_back({choices_.aspect, choices_.aspect + " (experimental)"});
        if (ImGui::BeginCombo("Aspect ratio", std::find_if(aspects.begin(), aspects.end(), [&](const auto& a) {
                                                  return a.first == choices_.aspect;
                                              })->second.c_str())) {
            for (const auto& [value, label] : aspects)
                if (ImGui::Selectable(label.c_str(), value == choices_.aspect)) choices_.aspect = value;
            ImGui::EndCombo();
        }
        const float ratio = aspect_ratio(choices_.aspect);
        if (ratio > 16.0f / 9.0f + 0.001f)
            help_marker("Experimental. The view gets wider; the HUD and the menus keep their 16:9 and 4:3 "
                        "places in the middle. Campaign only: multiplayer stays 16:9. Some screens may "
                        "not look right yet (please report them).");

        int mode = choices_.fullscreen ? 1 : 0;
        ImGui::Combo("Display mode", &mode, "Window\0" "Fullscreen\0");
        choices_.fullscreen = mode == 1;
        help_marker("Alt+Enter switches between the two while playing.");

        const int height = 480 * choices_.scale;
        const int width = static_cast<int>(height * ratio + 0.5f);
        ImGui::BeginDisabled(choices_.fullscreen);
        ImGui::SliderInt("Window size", &choices_.scale, 1, 8, "x%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d", width, height);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Picture");
        // What "render at window/screen size" means in render scale (the game's 480 lines).
        float native_scale = static_cast<float>(choices_.scale);
        if (choices_.fullscreen)
            if (const SDL_DisplayMode* display = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)))
                native_scale = std::round(display->h * std::max(display->pixel_density, 1.0f) / 48.0f) / 10.0f;
        bool match = choices_.render_scale == 0;
        if (ImGui::Checkbox(choices_.fullscreen ? "Render at screen size" : "Render at window size", &match))
            choices_.render_scale = match ? 0 : std::clamp(native_scale, 0.5f, 8.0f);
        help_marker("Off: choose the render resolution yourself. Above the window size it smooths edges "
                    "(supersampling), below it runs faster.");
        if (!match) {
            ImGui::SliderFloat("Render scale", &choices_.render_scale, 0.5f, 8.0f, "x%.1f",
                               ImGuiSliderFlags_AlwaysClamp);
            const int render_height = static_cast<int>(480 * choices_.render_scale + 0.5f);
            ImGui::SameLine();
            ImGui::TextDisabled("%dx%d", static_cast<int>(render_height * ratio + 0.5f),
                                render_height);
        }

        static constexpr int kAnisotropy[] = {1, 2, 4, 8, 16};
        int index = 0;
        while (index < 4 && kAnisotropy[index] < choices_.anisotropy) ++index;
        ImGui::Combo("Texture filtering", &index, "Original\0" "Anisotropic x2\0" "Anisotropic x4\0"
                     "Anisotropic x8\0" "Anisotropic x16\0");
        choices_.anisotropy = kAnisotropy[index];

        int fps = choices_.fps == 60 ? 1 : 0;
        ImGui::Combo("Frame rate", &fps, "30 fps (original)\0" "60 fps\0");
        choices_.fps = fps == 1 ? 60 : 30;
        help_marker("The game runs at the same speed either way; 60 fps needs about twice the "
                    "processor and graphics time.");

        ImGui::SliderFloat("Brightness", &choices_.brightness, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        help_marker("1.00 looks as on the Xbox. Higher brightens the midtones (dark interiors, night "
                    "missions); black and white stay as they are.");

        ImGui::Checkbox("Sharper text", &choices_.sharp_text);
        help_marker("Draws the game's text again at the render resolution, from free fonts that look like "
                    "the original ones (included with the game). Off: the original low-resolution letters.");

#ifdef _WIN32
        static constexpr const char* kD3D9[] = {"auto", "dxvk", "system"};
        int d3d9 = 0;
        while (d3d9 < 2 && choices_.d3d9 != kD3D9[d3d9]) ++d3d9;
        ImGui::Combo("Graphics", &d3d9, "Automatic\0" "Vulkan (DXVK)\0" "Direct3D 9 (Windows)\0");
        choices_.d3d9 = kD3D9[d3d9];
        help_marker("Automatic uses Vulkan through the included DXVK and falls back to the Direct3D 9 that "
                    "comes with Windows when Vulkan is not available. Choose one yourself if the picture "
                    "or the speed is wrong with the other.");
#endif
    }

    void controls() {
        ImGui::SeparatorText("Mouse and keyboard");
        ImGui::SliderFloat("Mouse sensitivity", &choices_.mouse_sensitivity, 0.01f, 1.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        help_marker("Degrees the view turns per mouse count.");
        ImGui::Checkbox("Aim assist", &choices_.aim_assist);
        help_marker("The view follows an enemy under the crosshair, as on the Xbox. Applies to the "
                    "controller and the mouse alike, in the campaign and in multiplayer.");
        keys();
        ImGui::SeparatorText("On screen");
        static constexpr const char* kPrompts[] = {"auto", "keyboard", "controller"};
        int prompts = 0;
        while (prompts < 2 && choices_.button_prompts != kPrompts[prompts]) ++prompts;
        ImGui::Combo("Button prompts", &prompts, "Follow the last used device\0" "Keyboard and mouse\0" "Controller\0");
        choices_.button_prompts = kPrompts[prompts];
        help_marker("The buttons the game shows in its hints and menus: keys and mouse buttons, or the Xbox "
                    "controller's. They follow the controller style chosen in the game's options.");
    }

    // The keyboard and mouse bindings (input/bindings.h): click a cell, then press a key,
    // a mouse button or turn the wheel; right-click clears it.
    void keys() {
        ImGui::SeparatorText("Keys");
        ImGui::TextWrapped("Click a key, then press the new key or mouse button (Esc cancels, right-click "
                           "clears it).");
        help_marker("The actions of the game's Default controller style. With another style chosen in the "
                    "game's options, each key keeps its action. Fixed: the arrow keys, Enter and Backspace "
                    "(menus), Esc (pause), Tab (objectives), F10 (frees the mouse).");
        if (!key_notice_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "%s", key_notice_.c_str());
        if (!ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) return;
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Other key");
        ImGui::TableHeadersRow();
        for (size_t a = 0; a < input::kActionCount; ++a) {
            const auto action = static_cast<input::Action>(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(input::action_label(action));
            if (action == input::Action::DpadDown) help_marker("Nothing in the Default style; other styles use it.");
            for (size_t slot = 0; slot < input::kInputsPerAction; ++slot) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(a * input::kInputsPerAction + slot));
                const bool waiting = capturing_ && capturing_->first == action && capturing_->second == slot;
                const std::string name = input::input_name(choices_.bindings[action][slot]);
                const std::string label = waiting ? "Press a key..." : name.empty() ? "-" : name;
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) {
                    capturing_.emplace(action, slot);
                    key_notice_.clear();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    input::bind(choices_.bindings, action, slot, 0);
                    capturing_.reset();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (ImGui::Button("Default keys")) {
            choices_.bindings = input::default_bindings();
            capturing_.reset();
            key_notice_.clear();
        }
    }

    void language() {
        static constexpr const char* kLanguages[] = {"english", "german", "french", "spanish", "italian"};
        int language = 0;
        while (language < 4 && choices_.language != kLanguages[language]) ++language;
        ImGui::Combo("Language", &language, "English\0" "German\0" "French\0" "Spanish\0" "Italian\0");
        choices_.language = kLanguages[language];
        help_marker("The Xbox's language setting. The game can only use languages that are on your disc: "
                    "the North American Standard Edition disc is English only, a European disc has its own languages (and may "
                    "have no English). The log lists the disc's languages.");
    }

    void sound() {
        ImGui::SeparatorText("Output");
        static constexpr std::pair<const char*, const char*> kSpeakers[] = {
            {"auto", "Automatic"}, {"stereo", "Stereo speakers"}, {"headphones", "Headphones (3D sound)"},
            {"quad", "Quad (4.0)"}, {"5.1", "Surround 5.1"},       {"7.1", "Surround 7.1"}};
        const auto* speakers = std::find_if(std::begin(kSpeakers), std::end(kSpeakers),
                                            [&](const auto& l) { return choices_.speakers == l.first; });
        if (ImGui::BeginCombo("Speakers", speakers->second)) {
            for (const auto& [value, label] : kSpeakers)
                if (ImGui::Selectable(label, value == choices_.speakers)) choices_.speakers = value;
            ImGui::EndCombo();
        }
        help_marker("Automatic: what your output device has, with 3D sound for headphones when the "
                    "game's own settings ask for headphones. Otherwise this layout, whatever the game's "
                    "settings say. Headphones (3D sound) places sounds around you, also above and "
                    "behind (HRTF).");

        if (!audio_devices_) audio_devices_ = audio::output_devices();
        if (ImGui::BeginCombo("Output device",
                              choices_.audio_device.empty() ? "System default" : choices_.audio_device.c_str())) {
            if (ImGui::Selectable("System default", choices_.audio_device.empty())) choices_.audio_device.clear();
            for (const std::string& name : *audio_devices_)
                if (ImGui::Selectable(name.c_str(), name == choices_.audio_device)) choices_.audio_device = name;
            ImGui::EndCombo();
        }
        help_marker("A device that is gone when the game starts falls back to the system default.");
        ImGui::Checkbox("Mute sound", &choices_.silent);

        ImGui::SeparatorText("Volume");
        ImGui::SliderFloat("Effects volume", &choices_.volumes.effects, 0.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_AlwaysClamp);
        help_marker("Gunfire, explosions, vehicles, footsteps, the menus' sounds.");
        ImGui::SliderFloat("Ambience volume", &choices_.volumes.ambience, 0.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_AlwaysClamp);
        help_marker("The levels' background: distant battle, wind, rain, the hum of a room.");
        ImGui::SliderFloat("Dialogue volume", &choices_.volumes.dialogue, 0.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_AlwaysClamp);
        help_marker("Orders, squad chatter, briefings.");
        ImGui::SliderFloat("Music volume", &choices_.volumes.music, 0.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_AlwaysClamp);
        help_marker("On top of the game's own volume setting, for Original and Enhanced sound "
                    "alike. Also adjustable while playing: PC menu (` key).");
        ImGui::SeparatorText("Enhanced sound");
        bool enhanced = choices_.audio == "enhanced";
        ImGui::Checkbox("Enhanced sound", &enhanced);
        choices_.audio = enhanced ? "enhanced" : "original";
        help_marker("Off: the sound as on the Xbox. On: the improvements below that are switched on. Also "
                    "switchable while playing in the PC menu (` key), each improvement too.");
        ImGui::BeginDisabled(!enhanced);
        ImGui::Indent();
        audio::EnhancedFeatures& f = choices_.features;
        ImGui::Checkbox("Finer resampling", &f.resampler);
        help_marker("OpenAL's best resampler (23rd order Sinc) instead of its default.");
        ImGui::Checkbox("More voices", &f.voices);
        help_marker("Up to 192 sounds placed in the world at once instead of the Xbox's 36, and effects "
                    "drop fewer of their sounds in busy fights.");
        ImGui::Checkbox("Reverb", &f.reverb);
        help_marker("Reverb that follows the place you are in: room size, walls, stone or wood, open sky.");
        ImGui::BeginDisabled(!f.reverb);
        if (ImGui::TreeNode("Advanced reverb")) {
            audio::ReverbTuning& t = choices_.reverb;
            ImGui::SliderFloat("Amount", &t.amount, -12.0f, 12.0f, "%+.1f dB", ImGuiSliderFlags_AlwaysClamp);
            help_marker("The reverb's overall level.");
            ImGui::SliderFloat("Decay", &t.decay, 0.25f, 4.0f, "x%.2f",
                               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            help_marker("How long the reverb lasts, times what the room's size and materials give.");
            ImGui::SliderFloat("Brightness", &t.brightness, 0.5f, 2.0f, "x%.2f",
                               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            help_marker("Below 1 the reverb's highs die away sooner (darker), above 1 later (brighter).");
            ImGui::SliderFloat("Early reflections", &t.reflections, -12.0f, 12.0f, "%+.1f dB",
                               ImGuiSliderFlags_AlwaysClamp);
            help_marker("The first echoes off nearby walls.");
            ImGui::SliderFloat("Room clutter", &t.clutter, 0.0f, 0.3f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            help_marker("What soaks up sound besides the walls the game knows about (furniture, debris, "
                        "openings). Lower makes rooms ring longer. Default 0.07.");
            if (ImGui::Button("Default reverb")) t = audio::ReverbTuning{};
            ImGui::TextDisabled("Also adjustable while playing: PC menu (` key).");
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("Occlusion", &f.occlusion);
        help_marker("Sounds behind walls and buildings muffled; in view, heard in full.");
        ImGui::Checkbox("Close-range boost", &f.close_boost);
        help_marker("Gunfire and bullets louder near you: full within 15 m, fading to the game's own level "
                    "at 30 m (your own gunfire always full). The game mixes them quietly; 0 dB = its level.");
        ImGui::BeginDisabled(!f.close_boost);
        ImGui::Indent();
        ImGui::SliderFloat("Others' gunfire", &choices_.volumes.close_gunfire, 0.0f, 12.0f, "%+.1f dB",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Your gunfire", &choices_.volumes.own_gunfire, 0.0f, 12.0f, "%+.1f dB",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Bullet impacts and flybys", &choices_.volumes.close_bullets, 0.0f, 12.0f, "%+.1f dB",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::Unindent();
        ImGui::EndDisabled();
    }

    // Multiplayer over the local network (online/lan.h): every game on the LAN has to use
    // the same ports.
    void system_link() {
        ImGui::SeparatorText("System Link");
        ImGui::TextWrapped("System Link plays with other PCs on your local network, or with a second copy "
                           "of the game on this PC. Xbox Live is not available.");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        if (ImGui::InputInt("System Link port", &choices_.net_port, 1, 10))
            choices_.net_port = std::clamp(choices_.net_port, 1024, 65532);
        ImGui::SameLine();
        ImGui::TextDisabled("UDP %d-%d", choices_.net_port, choices_.net_port + 3);
        help_marker("Use the same port on every PC. A firewall has to let these UDP ports through on the "
                    "local network (Windows asks the first time you play).");
    }

    // For bug reports: the log of the last run (and settings.ini next to it) and the
    // saves, so players can attach them to an issue.
    void reporting() {
        ImGui::SeparatorText("Reporting a problem");
        ImGui::TextWrapped("Attach log.txt from the log folder (the last run) to your report; add the save "
                           "folder if the problem depends on your progress.");
        if (ImGui::Button("Open log folder")) open_folder(platform::user_data_dir());
        help_marker("log.txt is replaced on every start, so copy it before playing again. settings.ini "
                    "(these options) is in the same folder.");
        ImGui::SameLine();
        if (ImGui::Button("Open save folder")) {
            // `data = DIR` in settings.ini (--data) moves the saves.
            const std::optional<std::string> data = settings_.get("data");
            open_folder((data ? fs::path(*data) : platform::user_data_dir()) / "E" / "UDATA");
        }
        help_marker("One folder per game profile with its saves, as on the Xbox hard disk.");
        ImGui::SeparatorText("Low frame rate");
        ImGui::Checkbox("Measure performance", &profile_);
        help_marker("For reports about a low frame rate: the next game started from here writes to its "
                    "log where the time goes: 10 seconds each time the frame rate drops to under 80% of its setting (at most once a minute). "
                    "It is not saved; tick it again for later reports.");
    }

    bool buttons() {
        ImGui::Separator();
        const bool ready = platform::has_game_files(asset_dir(choices_)) && !updating();
        ImGui::BeginDisabled(!ready);
        const bool campaign = ImGui::Button("Play", ImVec2(ImGui::GetFontSize() * 7, 0));
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_multiplayer(asset_dir(choices_)));
        const bool multiplayer = ImGui::Button("Multiplayer", ImVec2(ImGui::GetFontSize() * 7, 0));
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        const bool play = campaign || multiplayer;
        ImGui::SameLine();
        const bool quit = ImGui::Button("Quit");
        const char* reset_label = "Reset to defaults";
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(reset_label).x -
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(reset_label)) {
            const std::string assets = choices_.assets;  // the game files stay where they are
            choices_ = Choices{};
            choices_.assets = assets;
        }
        ImGui::SetItemTooltip("Every option on every tab back to its default; the game files folder stays.");
        if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "%s", error_.c_str());

        if (play || quit) {
            write_choices(choices_, settings_);
            if (!platform::save_settings(settings_)) {
                error_ = "Cannot save " + platform::settings_path().string();
                return true;
            }
        }
        if (play) {
            const fs::path game = platform::sibling_executable(COD_BRO_GAME_EXECUTABLE);
            std::vector<std::string> args;
            if (multiplayer) args.push_back("--multiplayer");
            if (profile_) args.insert(args.end(), {"--profile", "auto"});
            if (!platform::start_program(game, args)) {
                error_ = "Cannot start " + game.string();
                return true;
            }
        }
        return !(play || quit);
    }

    SDL_Window* window_;
    platform::Settings settings_;
    Choices choices_;
    std::string error_;
    std::optional<launcher::UpdateCheck> update_;
    std::optional<launcher::Updater> updater_;
    std::optional<std::vector<std::string>> audio_devices_;  // listed once
    bool restarted_ = false;
    bool profile_ = false;  // Measure performance: --profile auto, not saved
    std::optional<std::pair<input::Action, size_t>> capturing_;  // the key cell waiting for a key
    std::string key_notice_;
};

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("launcher", "SDL: %s", SDL_GetError());
        return 1;
    }
    const float ui_scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    // Tall enough for every option; on a short screen the rest scrolls.
    int height = static_cast<int>(440 * ui_scale);
    if (SDL_Rect usable; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        height = std::min(height, usable.h - static_cast<int>(40 * ui_scale));
    SDL_Window* window = SDL_CreateWindow("Call of Duty 2: Big Red One", static_cast<int>(560 * ui_scale), height,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        LOG_ERROR("launcher", "window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    io.FontGlobalScale = ui_scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Launcher launcher(window);
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (!launcher.capture_event(event)) ImGui_ImplSDL3_ProcessEvent(&event);
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (!launcher.frame()) running = false;
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
