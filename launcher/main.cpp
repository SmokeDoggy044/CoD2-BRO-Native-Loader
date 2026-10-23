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

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "log.h"
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
    bool widescreen = true;
    bool fullscreen = false;
    int scale = 2;
    float render_scale = 0;  // 0: the window's (fullscreen: the screen's) height
    int anisotropy = 16;
    float brightness = 1.0f;
    bool sharp_text = true;
    int fps = 60;
    std::string d3d9 = "auto";  // auto, dxvk, system (Windows only)
    float mouse_sensitivity = 0.11f;
    bool aim_assist = true;
    std::string button_prompts = "auto";  // auto, keyboard, controller
    bool silent = false;
    std::string language = "english";  // the console's: english, german, french, spanish, italian
    int net_port = 3074;  // system link: UDP net_port .. net_port + 3
    // Launcher only: saved as "false" (which the game skips) when off.
    bool check_updates = true;
};

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

Choices read_choices(const platform::Settings& settings) {
    Choices c;
    if (auto v = settings.get("assets")) c.assets = *v;
    if (auto v = settings.get("aspect")) c.widescreen = *v != "4:3";
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
    if (auto v = settings.get("aim-assist")) c.aim_assist = *v != "off";
    if (auto v = settings.get("button-prompts"); v && (*v == "keyboard" || *v == "controller")) c.button_prompts = *v;
    if (auto v = settings.get("silent")) c.silent = *v == "true";
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
    put("aspect", c.widescreen == d.widescreen, c.widescreen ? "16:9" : "4:3");
    put("fullscreen", c.fullscreen == d.fullscreen, c.fullscreen ? "true" : "false");
    put("scale", c.scale == d.scale, std::to_string(c.scale));
    put("render-scale", c.render_scale == d.render_scale, number(c.render_scale));
    put("anisotropy", c.anisotropy == d.anisotropy, std::to_string(c.anisotropy));
    put("brightness", c.brightness == d.brightness, number(c.brightness));
    put("sharp-text", c.sharp_text == d.sharp_text, c.sharp_text ? "on" : "off");
    put("fps", c.fps == d.fps, std::to_string(c.fps));
    put("d3d9", c.d3d9 == d.d3d9, c.d3d9);
    put("mouse-sensitivity", c.mouse_sensitivity == d.mouse_sensitivity, number(c.mouse_sensitivity));
    put("aim-assist", c.aim_assist == d.aim_assist, c.aim_assist ? "on" : "off");
    put("button-prompts", c.button_prompts == d.button_prompts, c.button_prompts);
    put("silent", c.silent == d.silent, c.silent ? "true" : "false");
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
        game_files();
        display();
        controls();
        system_link();
        reporting();
        updates();
        const bool keep_open = buttons();
        ImGui::End();
        return keep_open && !restart_after_update();
    }

private:
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

    void display() {
        ImGui::SeparatorText("Display");
        int aspect = choices_.widescreen ? 0 : 1;
        ImGui::Combo("Aspect ratio", &aspect, "16:9 (widescreen)\0" "4:3 (original)\0");
        choices_.widescreen = aspect == 0;

        int mode = choices_.fullscreen ? 1 : 0;
        ImGui::Combo("Display mode", &mode, "Window\0" "Fullscreen\0");
        choices_.fullscreen = mode == 1;
        help_marker("Alt+Enter switches between the two while playing.");

        const int height = 480 * choices_.scale;
        const int width = choices_.widescreen ? (height * 16 + 4) / 9 : 640 * choices_.scale;
        ImGui::BeginDisabled(choices_.fullscreen);
        ImGui::SliderInt("Window size", &choices_.scale, 1, 8, "x%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d", width, height);
        ImGui::EndDisabled();

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
            ImGui::TextDisabled("%dx%d", choices_.widescreen ? (render_height * 16 + 4) / 9 : render_height * 4 / 3,
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
        ImGui::SeparatorText("Controls and sound");
        ImGui::SliderFloat("Mouse sensitivity", &choices_.mouse_sensitivity, 0.01f, 1.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        help_marker("Degrees the view turns per mouse count.");
        ImGui::Checkbox("Aim assist", &choices_.aim_assist);
        help_marker("The view follows an enemy under the crosshair, as on the Xbox. Applies to the "
                    "controller and the mouse alike, in the campaign and in multiplayer.");
        static constexpr const char* kPrompts[] = {"auto", "keyboard", "controller"};
        int prompts = 0;
        while (prompts < 2 && choices_.button_prompts != kPrompts[prompts]) ++prompts;
        ImGui::Combo("Button prompts", &prompts, "Follow the last used device\0" "Keyboard and mouse\0" "Controller\0");
        choices_.button_prompts = kPrompts[prompts];
        help_marker("The buttons the game shows in its hints and menus: keys and mouse buttons, or the Xbox "
                    "controller's. They follow the controller style chosen in the game's options.");
        ImGui::Checkbox("Mute sound", &choices_.silent);

        static constexpr const char* kLanguages[] = {"english", "german", "french", "spanish", "italian"};
        int language = 0;
        while (language < 4 && choices_.language != kLanguages[language]) ++language;
        ImGui::Combo("Language", &language, "English\0" "German\0" "French\0" "Spanish\0" "Italian\0");
        choices_.language = kLanguages[language];
        help_marker("The Xbox's language setting. The game can only use languages that are on your disc: "
                    "the North American Standard Edition disc is English only, a European disc has its own languages (and may "
                    "have no English). The log lists the disc's languages.");
    }

    // Multiplayer over the local network (online/lan.h): every game on the LAN has to use
    // the same ports.
    void system_link() {
        ImGui::SeparatorText("Multiplayer");
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
        ImGui::Checkbox("Measure performance", &profile_);
        help_marker("For reports about a low frame rate: the next game started from here writes to its "
                    "log where the time goes, for 10 seconds from the first moment it runs under 24 fps. "
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
        if (ImGui::Button("Reset to defaults")) {
            const std::string assets = choices_.assets;  // the game files stay where they are
            choices_ = Choices{};
            choices_.assets = assets;
        }
        ImGui::SameLine();
        const bool quit = ImGui::Button("Quit");
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
    bool restarted_ = false;
    bool profile_ = false;  // Measure performance: --profile auto, not saved
};

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("launcher", "SDL: %s", SDL_GetError());
        return 1;
    }
    const float ui_scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    // Tall enough for every option; on a short screen the rest scrolls.
    int height = static_cast<int>(690 * ui_scale);
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
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
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
