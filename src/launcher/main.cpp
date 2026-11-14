// cod_bro_launcher: where the player points the game at their game files and sets the
// options, saved in settings.ini (src/platform/settings.h), then starts the game. The game
// runs without it (it reads the same file); it starts the launcher by itself when it
// finds no game files.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // WinMain on Windows

#include <algorithm>
#include <cstdlib>
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
#include "enhanced_graphics.h"
#include "reverb_tuning.h"
#include "paths.h"
#include "program.h"
#include "settings.h"
#include "settings_schema.h"
#include "settings_widgets.h"
#include "update_check.h"
#include "updater.h"

namespace fs = std::filesystem;

namespace {

// The width over the height of an aspect setting ("W:H"), or 0 if it is not one.
float aspect_ratio(const std::string& aspect) {
    float w = 0, h = 0;
    char end = 0;
    if (std::sscanf(aspect.c_str(), "%f:%f%c", &w, &h, &end) != 2 || !(w > 0) || !(h > 0)) return 0;
    return w / h;
}

fs::path asset_dir(const platform::Settings& settings) {
    const std::optional<std::string> assets = settings.get("assets");
    return !assets || assets->empty() ? platform::default_asset_dir() : fs::path(*assets);
}

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

using ui::help_marker;

class Launcher {
public:
    explicit Launcher(SDL_Window* window)
        : window_(window), settings_(platform::load_settings()) {
        if (auto keys = platform::combined_setting(settings_, "keys")) input::parse_bindings(*keys, bindings_);
        launcher::remove_update_leftovers(platform::executable_dir());
        if (check_updates()) update_.emplace(COD_BRO_RELEASES, COD_BRO_VERSION);
    }

    // While a key cell waits for its key (controls()): takes key presses, mouse buttons
    // and the wheel before the UI sees them. True if it took `event`.
    bool capture_event(const SDL_Event& event) {
        bool changed = false;
        const bool taken = ui::key_table_event(key_table_, bindings_, &event, changed);
        if (changed) store_bindings();
        return taken;
    }

    // One frame of the UI. Returns false once the launcher should close.
    bool frame() {
        {
            std::lock_guard lock(g_picked_mutex);
            if (g_picked) {
                if (fs::path(*g_picked) == platform::default_asset_dir())
                    settings_.erase("assets");
                else
                    settings_.set("assets", *g_picked);
                g_picked.reset();
            }
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        update_notice();
        if (!platform::has_game_files(asset_dir(settings_)))
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "No game files yet: see Game files on the General tab.");
        // The tab bar and the buttons stay; each tab's settings scroll between them.
        const ImGuiStyle& style = ImGui::GetStyle();
        float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2 + style.SeparatorTextBorderSize;
        if (!error_.empty()) footer += ImGui::GetTextLineHeightWithSpacing();
        footer_ = footer;
        if (ImGui::BeginTabBar("sections")) {
            tab("General", [&] { general(); });
            tab("Graphics", [&] { graphics(); });
            tab("Sound", [&] { sound(); });
            tab("Controls", [&] { controls(); });
            tab("Multiplayer", [&] { system_link(); });
            tab("Help", [&] { reporting(); });
            ImGui::EndTabBar();
        }
        const bool keep_open = buttons();
        ImGui::End();
        return keep_open && !restart_after_update();
    }

private:
    // A tab: the tab bar stays put, the tab's settings scroll above the buttons.
    template <typename Body>
    void tab(const char* name, Body body) {
        if (!ImGui::BeginTabItem(name)) return;
        ImGui::BeginChild(name, ImVec2(0, -footer_));
        ImGui::Spacing();
        // Controls get the left 60%, labels the rest.
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        body();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }

    void general() {
        game_files();
        ui::draw_settings({platform::SettingGroup::Files}, settings_);
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
        platform::save_settings(settings_);
        if (platform::start_program(platform::sibling_executable("cod_bro_launcher"))) return true;
        error_ = "The update is installed; start the launcher again to use it.";
        return false;
    }

    void updates() {
        ImGui::SeparatorText("Updates");
        bool check = check_updates();
        if (ImGui::Checkbox("Check for updates", &check)) {
            ui::put_setting(settings_, "check-updates", check ? "true" : "false");
            if (check && !update_) update_.emplace(COD_BRO_RELEASES, COD_BRO_VERSION);
        }
        help_marker("When the launcher opens, asks GitHub for the newest release and shows a link to it "
                    "if it is newer. Update now downloads and installs it, then restarts the launcher; your "
                    "game files, saves and settings stay as they are. Nothing is installed by itself.");
        ImGui::SameLine();
        const char* status = "";
        if (update_ && check_updates()) {
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
        const fs::path dir = asset_dir(settings_);
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
        if (settings_.get("assets")) {
            ImGui::SameLine();
            if (ImGui::Button("Use the assets folder")) settings_.erase("assets");
        }
    }

    bool check_updates() const { return ui::setting_value(settings_, "check-updates") == "true"; }

    // settings.ini's key-<action> lines from the bindings (none for actions with their default keys).
    void store_bindings() { platform::set_combined_setting(settings_, "keys", input::format_bindings(bindings_)); }

    // The screen's own shape, when it is wider than 16:9 (3440x1440 is a little wider than 21:9); "" otherwise.
    std::string screen_shape() const {
        if (const SDL_DisplayMode* display = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)))
            if (display->h > 0 && display->w * 9 > display->h * 16 + 9)
                return std::to_string(display->w) + ":" + std::to_string(display->h);
        return {};
    }

    // The Display and Graphics settings the table's standard widgets do not fit.
    bool picture_widget(const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& values,
                        bool& changed) {
        const std::string_view key = def.key;
        const std::string aspect = ui::setting_value(values, "aspect");
        const float ratio = aspect == "4:3" ? 4.0f / 3.0f : std::max(aspect_ratio(aspect), 16.0f / 9.0f);
        const bool fullscreen = ui::setting_value(values, "fullscreen") == "true";
        const int scale = std::atoi(ui::setting_value(values, "scale").c_str());
        if (key == "aspect") {
            std::vector<std::pair<std::string, std::string>> aspects = {
                {"16:9", "16:9 (widescreen)"},
                {"4:3", "4:3 (original)"},
                {"21:9", "21:9 ultrawide (experimental)"},
                {"32:9", "32:9 super ultrawide (experimental)"}};
            if (const std::string screen = screen_shape(); !screen.empty() && screen != "21:9" && screen != "32:9")
                aspects.push_back({screen, "Your screen, " + screen + " (experimental)"});
            if (std::none_of(aspects.begin(), aspects.end(), [&](const auto& a) { return a.first == aspect; }))
                aspects.push_back({aspect, aspect + " (experimental)"});
            const auto current = std::find_if(aspects.begin(), aspects.end(), [&](const auto& a) { return a.first == aspect; });
            if (ImGui::BeginCombo("Aspect ratio", current->second.c_str())) {
                for (const auto& [value, label] : aspects)
                    if (ImGui::Selectable(label.c_str(), value == aspect) && value != aspect) {
                        ui::put_setting(values, "aspect", value);
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (ratio > 16.0f / 9.0f + 0.001f)
                help_marker("Experimental. The view gets wider; the HUD and the menus keep their 16:9 and 4:3 "
                            "places in the middle. Campaign only: multiplayer stays 16:9. Some screens may "
                            "not look right yet (please report them).");
            return true;
        }
        if (key == "scale") {
            int n = scale;
            if (ImGui::SliderInt("Window size", &n, 1, 8, "x%d", ImGuiSliderFlags_AlwaysClamp)) {
                ui::put_setting(values, "scale", std::to_string(n));
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%dx%d", static_cast<int>(480 * n * ratio + 0.5f), 480 * n);
            return true;
        }
        if (key == "render-scale") {
            // What "render at window/screen size" means in render scale (the game's 480 lines).
            float native_scale = static_cast<float>(scale);
            if (fullscreen)
                if (const SDL_DisplayMode* display = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)))
                    native_scale = std::round(display->h * std::max(display->pixel_density, 1.0f) / 48.0f) / 10.0f;
            const std::string value = ui::setting_value(values, "render-scale");
            bool match = value.empty();
            if (ImGui::Checkbox(fullscreen ? "Render at screen size" : "Render at window size", &match)) {
                if (match)
                    values.erase("render-scale");
                else
                    values.set("render-scale", ui::number_text(std::clamp(native_scale, 0.5f, 8.0f)));
                changed = true;
            }
            help_marker("Off: choose the render resolution yourself. Above the window size it smooths edges "
                        "(supersampling), below it runs faster.");
            if (!match) {
                float x = std::strtof(value.c_str(), nullptr);
                if (ImGui::SliderFloat("Render scale", &x, 0.5f, 8.0f, "x%.1f", ImGuiSliderFlags_AlwaysClamp)) {
                    values.set("render-scale", ui::number_text(x));
                    changed = true;
                }
                const int render_height = static_cast<int>(480 * x + 0.5f);
                ImGui::SameLine();
                ImGui::TextDisabled("%dx%d", static_cast<int>(render_height * ratio + 0.5f), render_height);
            }
            return true;
        }
        if (key == "anisotropy") {
            static constexpr int kAnisotropy[] = {1, 2, 4, 8, 16};
            int index = 0;
            while (index < 4 && kAnisotropy[index] < std::atoi(ui::setting_value(values, "anisotropy").c_str())) ++index;
            if (ImGui::Combo(ui.label, &index, "x1 (none)\0" "x2\0" "x4\0" "x8\0" "x16\0")) {
                ui::put_setting(values, "anisotropy", std::to_string(kAnisotropy[index]));
                changed = true;
            }
            return true;
        }
        if (key == "fps") {
            // Display (paced by the display), 60 (our own clock), 30 (as on the Xbox); another --fps limit from
            // settings.ini shows as a fourth entry so it is kept.
            const std::string value = ui::setting_value(values, "fps");
            const bool custom = value != "display" && value != "60" && value != "30";
            const std::string custom_label = custom ? "At most " + value + " fps" : "";
            const char* labels[] = {"Display's refresh rate", "60 fps", "30 fps (original)", custom_label.c_str()};
            static constexpr const char* kValues[] = {"display", "60", "30"};
            int fps = value == "display" ? 0 : value == "60" ? 1 : value == "30" ? 2 : 3;
            if (ImGui::Combo("Frame rate", &fps, labels, custom ? 4 : 3) && fps < 3) {
                ui::put_setting(values, "fps", kValues[fps]);
                changed = true;
            }
            help_marker("The game runs at the same speed either way. Display's refresh rate: each frame "
                        "waits for the display (smoothest). 60 fps keeps its own clock; 30 fps is the "
                        "Xbox's rate and needs about half the processor and graphics time.");
            return true;
        }
#ifndef _WIN32
        if (key == "d3d9") return true;  // Windows only
#endif
        return false;
    }

    void graphics() {
        ui::DrawOptions options;
        options.custom = [&](const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& values,
                             bool& changed) { return picture_widget(def, ui, values, changed); };
        ui::draw_settings({platform::SettingGroup::Display, platform::SettingGroup::Graphics}, settings_, options);
    }

    void controls() {
        ui::draw_settings({platform::SettingGroup::Controls}, settings_);
        keys();
    }

    // The keyboard and mouse bindings (src/input/bindings.h), the PC menu's table.
    void keys() {
        ImGui::SeparatorText("Keys");
        if (ui::draw_key_table(key_table_, bindings_)) store_bindings();
    }

    void sound() {
        ui::DrawOptions options;
        options.custom = [&](const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& values,
                             bool& changed) {
            if (std::string_view(def.key) != "audio-device") return false;
            if (!audio_devices_) audio_devices_ = audio::output_devices();
            const std::string device = ui::setting_value(values, "audio-device");
            if (ImGui::BeginCombo(ui.label, device.empty() ? "System default" : device.c_str())) {
                if (ImGui::Selectable("System default", device.empty()) && !device.empty()) {
                    values.erase("audio-device");
                    changed = true;
                }
                for (const std::string& name : *audio_devices_)
                    if (ImGui::Selectable(name.c_str(), name == device) && name != device) {
                        values.set("audio-device", name);
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            help_marker(ui.tip);
            return true;
        };
        ui::draw_settings({platform::SettingGroup::Sound}, settings_, options);
    }

    // Multiplayer over the local network (src/online/lan.h): every game on the LAN has to use
    // the same ports.
    void system_link() {
        ImGui::SeparatorText("System Link");
        ImGui::TextWrapped("System Link plays with other PCs on your local network, or with a second copy "
                           "of the game on this PC. Xbox Live is not available.");
        ui::DrawOptions options;
        options.custom = [&](const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& values,
                             bool& changed) {
            if (std::string_view(def.key) != "net-port") return false;
            int port = std::atoi(ui::setting_value(values, "net-port").c_str());
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
            if (ImGui::InputInt(ui.label, &port, 1, 10)) {
                ui::put_setting(values, "net-port", std::to_string(std::clamp(port, 1024, 65532)));
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("UDP %d-%d", port, port + 3);
            help_marker(ui.tip);
            return true;
        };
        ui::draw_settings({platform::SettingGroup::Network}, settings_, options);
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
        const bool ready = platform::has_game_files(asset_dir(settings_)) && !updating();
        ImGui::BeginDisabled(!ready);
        const bool campaign = ImGui::Button("Play", ImVec2(ImGui::GetFontSize() * 7, 0));
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_multiplayer(asset_dir(settings_)));
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
            // Every setting the tabs show (and the keys); the game files folder and hand-written lines stay.
            for (const platform::SettingUi& ui : platform::setting_uis()) settings_.erase(ui.key);
            settings_.erase("check-updates");
            bindings_ = input::default_bindings();
            store_bindings();
        }
        ImGui::SetItemTooltip("Every option on every tab back to its default; the game files folder stays.");
        if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "%s", error_.c_str());

        if (play || quit) {
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
    platform::Settings settings_;  // what the tabs edit, saved on Play/Quit
    float footer_ = 0;             // the buttons' height below the tabs
    input::Bindings bindings_ = input::default_bindings();
    std::string error_;
    std::optional<launcher::UpdateCheck> update_;
    std::optional<launcher::Updater> updater_;
    std::optional<std::vector<std::string>> audio_devices_;  // listed once
    bool restarted_ = false;
    bool profile_ = false;  // Measure performance: --profile auto, not saved
    ui::KeyTable key_table_;
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
