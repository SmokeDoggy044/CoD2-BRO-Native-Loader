// cod_bro_launcher: where the player points the game at their game files and sets the
// options, saved in settings.ini (src/platform/settings.h), then starts the game. The game
// runs without it (it reads the same file); it starts the launcher by itself when it
// finds no game files.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // WinMain on Windows

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "bindings.h"
#include "front_picture.h"
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
#include "ui_theme.h"
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

// The borderless window moves when dragged by an empty part of the page (set each frame; SDL asks from its
// event handling, which may be another thread).
std::atomic<bool> g_draggable{false};

SDL_HitTestResult SDLCALL hit_test(SDL_Window*, const SDL_Point*, void*) {
    return g_draggable.load() ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
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
    Launcher(SDL_Window* window, SDL_Renderer* renderer)
        : window_(window), renderer_(renderer), settings_(platform::load_settings()) {
        if (auto keys = platform::combined_setting(settings_, "keys")) input::parse_bindings(*keys, bindings_);
        launcher::remove_update_leftovers(platform::executable_dir());
        if (check_updates()) update_.emplace(COD_BRO_RELEASES, COD_BRO_VERSION);
        load_pictures();
    }

    // Developer (COD_BRO_LAUNCHER_PAGE): opens "options" or "tech" instead of the front page.
    void show_page(std::string_view page) {
        if (page == "options") page_ = Page::Options;
        if (page == "tech") page_ = Page::TechSupport;
    }

    ~Launcher() {
        for (SDL_Texture* texture : {picture_.texture, logo_.texture})
            if (texture) SDL_DestroyTexture(texture);
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
        if (pictures_for_ != asset_dir(settings_)) load_pictures();  // the game files folder changed
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       (page_ == Page::Front ? ImGuiWindowFlags_NoBackground : 0);
        ImGui::Begin("launcher", nullptr, flags);
        bool keep_open = true;
        if (page_ == Page::Front)
            keep_open = front();
        else if (page_ == Page::TechSupport)
            tech_support();
        else
            options();
        // Borderless window: empty parts of the page (not a child window's, so no scroll bars) move it.
        g_draggable = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() &&
                      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        ImGui::End();
        if (!window_buttons()) keep_open = !save();
        return keep_open && !restart_after_update();
    }

    // The borderless window's own minimise and close buttons, top right on every page. False = close.
    bool window_buttons() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float size = ImGui::GetFrameHeight();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 2 * size - gap - 4, viewport->Pos.y + 4));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("window buttons", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.6f));
        if (ImGui::Button("_", ImVec2(size, size))) SDL_MinimizeWindow(window_);
        ImGui::SetItemTooltip("Minimise");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.12f, 1.0f));
        const bool close = ImGui::Button("X", ImVec2(size, size));
        ImGui::SetItemTooltip("Close (saves the options)");
        ImGui::PopStyleColor(2);
        ImGui::End();
        ImGui::PopStyleVar();
        return !close;
    }

private:
    enum class Page { Front, Options, TechSupport };

    // The picture's texture and the shape it is shown at.
    struct Shown {
        SDL_Texture* texture = nullptr;
        float aspect = 1.0f;
    };

    Shown make_texture(const std::optional<launcher::Picture>& picture) {
        if (!picture) return {};
        SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                 picture->width, picture->height);
        if (!texture) {
            LOG_WARN("launcher", "picture texture: %s", SDL_GetError());
            return {};
        }
        SDL_UpdateTexture(texture, nullptr, picture->rgba.data(), picture->width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        return {texture, picture->aspect};
    }

    // The front page's picture: unlocked concept art (the saves' profiles) or the main menu's, and the logo.
    void load_pictures() {
        for (SDL_Texture* texture : {picture_.texture, logo_.texture})
            if (texture) SDL_DestroyTexture(texture);
        picture_ = logo_ = {};
        pictures_for_ = asset_dir(settings_);
        if (!platform::has_game_files(pictures_for_)) return;
        const int chapters = launcher::unlocked_concept_chapters(save_dir());
        const std::vector<fs::path> art = launcher::unlocked_concept_art(pictures_for_, chapters);
        std::mt19937 random(std::random_device{}());
        const std::optional<launcher::Picture> picture = launcher::front_picture(pictures_for_, art, random);
        picture_ = make_texture(picture);
        logo_ = make_texture(launcher::game_logo(pictures_for_));
        LOG_INFO("launcher", "picture: %s (%d concept chapters unlocked, %zu images)",
                 picture ? picture->source.c_str() : "none", chapters, art.size());
    }

    // The saves' folder (`data = DIR` in settings.ini moves them, as --data).
    fs::path save_dir() const {
        const std::optional<std::string> data = settings_.get("data");
        return (data ? fs::path(*data) : platform::user_data_dir()) / "E" / "UDATA";
    }

    // Draws the picture over the whole window (cut to the window's shape), darker towards the buttons.
    void draw_picture() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const ImVec2 lo = viewport->Pos;
        const ImVec2 hi(lo.x + viewport->Size.x, lo.y + viewport->Size.y);
        if (picture_.texture) {
            const float window = viewport->Size.x / viewport->Size.y;
            ImVec2 uv0(0, 0), uv1(1, 1);
            if (picture_.aspect > window) {
                const float cut = (1.0f - window / picture_.aspect) / 2;
                uv0.x = cut;
                uv1.x = 1 - cut;
            } else {
                const float cut = (1.0f - picture_.aspect / window) / 2;
                uv0.y = cut;
                uv1.y = 1 - cut;
            }
            draw->AddImage(reinterpret_cast<ImTextureID>(picture_.texture), lo, hi, uv0, uv1);
        }
        const float x = lo.x + viewport->Size.x * 0.45f;
        draw->AddRectFilledMultiColor(ImVec2(x, lo.y), hi, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 200),
                                      IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 0));
        if (logo_.texture) {
            const float w = viewport->Size.x * 0.42f;  // top right, above the buttons
            const ImVec2 at(hi.x - w - viewport->Size.x * 0.02f, lo.y + viewport->Size.y * 0.04f);
            draw->AddImage(reinterpret_cast<ImTextureID>(logo_.texture), at, ImVec2(at.x + w, at.y + w / logo_.aspect));
        }
    }

    // A big button in the front page's column.
    bool front_button(const char* label) {
        return ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 10, ImGui::GetFrameHeight() * 1.5f));
    }

    // The front page: the picture, Play (and Multiplayer), Options, Tech Support, Exit. Returns false to close.
    bool front() {
        draw_picture();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const bool files = platform::has_game_files(asset_dir(settings_));
        const bool multiplayer_files = has_multiplayer(asset_dir(settings_));
        const int rows = 4 + (multiplayer_files ? 1 : 0);
        const float column = ImGui::GetFontSize() * 10;
        const float row = ImGui::GetFrameHeight() * 1.5f + ImGui::GetStyle().ItemSpacing.y;
        const float x = viewport->Size.x - column - ImGui::GetFontSize() * 2;
        float y = viewport->Size.y * 0.42f;
        y = std::min(y, viewport->Size.y - rows * row - ImGui::GetFontSize() * 2);
        bool campaign = false, multiplayer = false, quit = false;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.12f, 0.10f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.15f, 0.12f, 1.0f));
        ImGui::SetCursorPos(ImVec2(x, y));
        ImGui::BeginGroup();
        ImGui::BeginDisabled(!files || updating());
        campaign = front_button("Play");
        if (multiplayer_files) multiplayer = front_button("Multiplayer");
        ImGui::EndDisabled();
        if (front_button("Options")) page_ = Page::Options;
        if (front_button("Tech Support")) page_ = Page::TechSupport;
        quit = front_button("Exit");
        ImGui::EndGroup();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // Bottom left: what needs doing first, the update notice, the version.
        const float lines = 2 + (error_.empty() ? 0 : 1);
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                                   viewport->Size.y - lines * ImGui::GetTextLineHeightWithSpacing() -
                                       ImGui::GetStyle().WindowPadding.y));
        ImGui::BeginGroup();
        if (!files)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "No game files yet: Options, General, Game files.");
        update_notice();
        if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "%s", error_.c_str());
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 0.75f), "Version %s", COD_BRO_VERSION);
        ImGui::EndGroup();

        if (campaign || multiplayer) return !start_game(multiplayer);
        if (quit) return !save();
        return true;
    }

    // Saves the options; false (and the error shown) when it cannot.
    bool save() {
        if (platform::save_settings(settings_)) return true;
        error_ = "Cannot save " + platform::settings_path().string();
        return false;
    }

    // Saves and starts the game; true when it started (the launcher then closes).
    bool start_game(bool multiplayer) {
        if (!save()) return false;
        const fs::path game = platform::sibling_executable(COD_BRO_GAME_EXECUTABLE);
        std::vector<std::string> args;
        if (multiplayer) args.push_back("--multiplayer");
        if (profile_) args.insert(args.end(), {"--profile", "auto"});
        if (platform::start_program(game, args)) return true;
        error_ = "Cannot start " + game.string();
        return false;
    }

    // Tech Support: what a problem report needs, the version and updates.
    void tech_support() {
        ImGui::BeginChild("tech", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y)));
        reporting();
        updates();
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("Back", ImVec2(ImGui::GetFontSize() * 7, 0))) page_ = Page::Front;
    }

    // Options: the settings tabs; Back saves them.
    void options() {
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
            ImGui::EndTabBar();
        }
        buttons();
    }

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

    // Options' footer: Back (saves, to the front page) and Reset to defaults.
    void buttons() {
        ImGui::Separator();
        if (ImGui::Button("Back", ImVec2(ImGui::GetFontSize() * 7, 0)) && save()) {
            key_table_.waiting.reset();
            page_ = Page::Front;
        }
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
    }

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    Page page_ = Page::Front;
    Shown picture_, logo_;
    fs::path pictures_for_;        // the game files folder the pictures came from
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
    SDL_Window* window = SDL_CreateWindow("Call of Duty 2: Big Red One", static_cast<int>(700 * ui_scale), height,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        LOG_ERROR("launcher", "window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (!SDL_SetWindowHitTest(window, hit_test, nullptr)) LOG_WARN("launcher", "window dragging: %s", SDL_GetError());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ui::apply_theme();
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    io.FontGlobalScale = ui_scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Launcher launcher(window, renderer);
    // Developer: COD_BRO_LAUNCHER_SHOT=FILE saves the window as a BMP after a few frames and exits
    // (COD_BRO_LAUNCHER_PAGE=options|tech picks the page) - for checking the look without a person.
    const char* shot = std::getenv("COD_BRO_LAUNCHER_SHOT");
    if (const char* page = std::getenv("COD_BRO_LAUNCHER_PAGE")) launcher.show_page(page);
    for (int frames = 0, running = 1; running; ++frames) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = 0;
            if (!launcher.capture_event(event)) ImGui_ImplSDL3_ProcessEvent(&event);
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (!launcher.frame()) running = 0;
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (shot && frames == 5) {
            if (SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(surface, shot);
                SDL_DestroySurface(surface);
            }
            running = 0;
        }
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
