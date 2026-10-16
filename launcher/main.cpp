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

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "log.h"
#include "paths.h"
#include "process.h"
#include "settings.h"

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
    float mouse_sensitivity = 0.11f;
    bool aim_assist = true;
    bool silent = false;
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
    if (auto v = settings.get("mouse-sensitivity"))
        c.mouse_sensitivity = std::clamp(std::strtof(v->c_str(), nullptr), 0.01f, 10.0f);
    if (auto v = settings.get("aim-assist")) c.aim_assist = *v != "off";
    if (auto v = settings.get("silent")) c.silent = *v == "true";
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
    put("mouse-sensitivity", c.mouse_sensitivity == d.mouse_sensitivity, number(c.mouse_sensitivity));
    put("aim-assist", c.aim_assist == d.aim_assist, c.aim_assist ? "on" : "off");
    put("silent", c.silent == d.silent, c.silent ? "true" : "false");
}

fs::path asset_dir(const Choices& c) { return c.assets.empty() ? platform::default_asset_dir() : fs::path(c.assets); }

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
        : window_(window), settings_(platform::load_settings()), choices_(read_choices(settings_)) {}

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
        game_files();
        display();
        controls();
        const bool keep_open = buttons();
        ImGui::End();
        return keep_open;
    }

private:
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
    }

    void controls() {
        ImGui::SeparatorText("Controls and sound");
        ImGui::SliderFloat("Mouse sensitivity", &choices_.mouse_sensitivity, 0.01f, 1.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        help_marker("Degrees the view turns per mouse count.");
        ImGui::Checkbox("Aim assist", &choices_.aim_assist);
        help_marker("The view follows an enemy under the crosshair, as on the Xbox. Applies to the "
                    "controller and the mouse alike.");
        ImGui::Checkbox("Mute sound", &choices_.silent);
    }

    bool buttons() {
        ImGui::Separator();
        const bool ready = platform::has_game_files(asset_dir(choices_));
        ImGui::BeginDisabled(!ready);
        const bool play = ImGui::Button("Play", ImVec2(ImGui::GetFontSize() * 7, 0));
        ImGui::EndDisabled();
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
            if (!platform::start_program(game)) {
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
};

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("launcher", "SDL: %s", SDL_GetError());
        return 1;
    }
    const float ui_scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window* window = SDL_CreateWindow("Call of Duty 2: Big Red One", static_cast<int>(560 * ui_scale),
                                          static_cast<int>(400 * ui_scale), SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
