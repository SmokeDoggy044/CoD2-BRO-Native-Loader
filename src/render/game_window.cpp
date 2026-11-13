// The back ends' shared window, events, prompt, picture placement, gamma and overlay text. See game_window.h.

#include "game_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "log.h"

namespace render {

GameWindow::~GameWindow() {
    close_menu();
    if (window_) SDL_DestroyWindow(window_);
    if (sdl_started_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool GameWindow::open(const BackendConfig& config, SDL_WindowFlags api_flags) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        LOG_ERROR("render", "SDL video: %s", SDL_GetError());
        return false;
    }
    sdl_started_ = true;
    width_ = config.width;
    height_ = config.height;
    const uint32_t scale = config.window_scale ? config.window_scale : 1;
    const uint32_t window_height = config.height * scale;
    const uint32_t window_width = config.display_aspect > 0
                                      ? static_cast<uint32_t>(std::lround(window_height * config.display_aspect))
                                      : config.width * scale;
    window_ = SDL_CreateWindow("Call of Duty 2: Big Red One", static_cast<int>(window_width),
                               static_cast<int>(window_height),
                               api_flags | SDL_WINDOW_RESIZABLE | (config.hidden_window ? SDL_WINDOW_HIDDEN : 0));
    if (!window_) {
        LOG_ERROR("render", "window: %s", SDL_GetError());
        return false;
    }
    if (config.fullscreen) {
        SDL_SetWindowFullscreen(window_, true);  // borderless, at the desktop's mode
        SDL_SyncWindow(window_);
    }
    brightness_ = config.brightness > 0 ? config.brightness : 1.0f;
    {  // the identity ramp until the title sets one (brightness applies from the start)
        std::lock_guard lock(gamma_mutex_);
        for (uint32_t i = 0; i < 256; ++i)
            for (auto& channel : gamma_pending_) channel[i] = static_cast<uint8_t>(i);
        gamma_changed_ = true;
    }
    pc_menu_ = config.pc_menu;
    pc_menu_shown_ = config.pc_menu_shown;
    menu_font_ = config.menu_font;
    return true;
}

void GameWindow::pixel_size(int& width, int& height) const {
    width = height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    width = std::max(width, 1);
    height = std::max(height, 1);
}

void GameWindow::largest_display(uint32_t& width, uint32_t& height) const {
    int pixel_width = 0, pixel_height = 0;
    pixel_size(pixel_width, pixel_height);
    width = static_cast<uint32_t>(pixel_width);
    height = static_cast<uint32_t>(pixel_height);
    int display_count = 0;
    if (SDL_DisplayID* displays = SDL_GetDisplays(&display_count)) {
        for (int i = 0; i < display_count; ++i) {
            if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i])) {
                const float density = mode->pixel_density > 0 ? mode->pixel_density : 1.0f;
                width = std::max(width, static_cast<uint32_t>(std::lround(mode->w * density)));
                height = std::max(height, static_cast<uint32_t>(std::lround(mode->h * density)));
            }
        }
        SDL_free(displays);
    }
}

double GameWindow::refresh_rate() const {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    if (!mode) return 0;
    if (mode->refresh_rate_numerator && mode->refresh_rate_denominator)
        return static_cast<double>(mode->refresh_rate_numerator) / mode->refresh_rate_denominator;
    return mode->refresh_rate;
}

bool GameWindow::pump_events() {
    if (closed_) return false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && pc_menu_ &&
            (event.key.scancode == SDL_SCANCODE_GRAVE || (menu_open_ && event.key.scancode == SDL_SCANCODE_ESCAPE))) {
            set_menu_open(!menu_open_);
            continue;
        }
        if (menu_open_) ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F12 && !event.key.repeat)
            mark_requested_ = true;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F11 && !event.key.repeat)
            highlight_toggled_ = true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.mod & SDL_KMOD_ALT) &&
            (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER)) {
            const bool fullscreen = !(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(window_, fullscreen);
            LOG_INFO("render", "%s (Alt+Enter)", fullscreen ? "fullscreen" : "windowed");
        }
    }
    return true;
}

bool GameWindow::take_mark_request() { return std::exchange(mark_requested_, false); }
bool GameWindow::take_highlight_toggle() { return std::exchange(highlight_toggled_, false); }

std::optional<std::string> GameWindow::prompt(const std::vector<std::string>& heading,
                                              const std::function<void(const std::vector<std::string>&)>& show) {
    constexpr size_t kMaxText = 200, kLineChars = 76;
    const bool captured = SDL_GetWindowRelativeMouseMode(window_);
    SDL_SetWindowRelativeMouseMode(window_, false);
    SDL_StartTextInput(window_);
    std::string text;
    std::optional<std::string> result;
    bool done = false;
    while (!done) {
        SDL_Event event;
        bool have = SDL_WaitEventTimeout(&event, 100);
        while (have && !done) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: closed_ = done = true; break;
                case SDL_EVENT_TEXT_INPUT:
                    if (text.size() < kMaxText) text += event.text.text;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_BACKSPACE) {
                        while (!text.empty() && (text.back() & 0xC0) == 0x80) text.pop_back();  // UTF-8
                        if (!text.empty()) text.pop_back();
                    } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                        result = text;
                        done = true;
                    } else if (event.key.key == SDLK_ESCAPE) {
                        done = true;
                    }
                    break;
                default: break;
            }
            have = SDL_PollEvent(&event);
        }
        std::vector<std::string> lines = heading;
        const std::string field = "> " + text + "_";
        for (size_t i = 0; i < field.size(); i += kLineChars) lines.push_back(field.substr(i, kLineChars));
        lines.push_back("Return: save   Escape: skip");
        show(lines);
    }
    SDL_StopTextInput(window_);
    // Let go of Return/Escape before the title reads the keyboard again.
    for (int i = 0; i < 100; ++i) {
        SDL_PumpEvents();
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (!keys[SDL_SCANCODE_RETURN] && !keys[SDL_SCANCODE_ESCAPE] && !keys[SDL_SCANCODE_KP_ENTER]) break;
        SDL_Delay(10);
    }
    if (captured && !closed_) SDL_SetWindowRelativeMouseMode(window_, true);
    return result;
}

WindowRect GameWindow::place_picture(const WindowRect& area, double aspect) {
    int32_t width = area.width(), height = static_cast<int32_t>(std::lround(area.width() / aspect));
    if (height > area.height()) {
        height = area.height();
        width = static_cast<int32_t>(std::lround(area.height() * aspect));
    }
    const int32_t left = area.left + (area.width() - width) / 2, top = area.top + (area.height() - height) / 2;
    const WindowRect dest{left, top, left + width, top + height};
    std::lock_guard lock(picture_mutex_);
    picture_ = dest;
    return dest;
}

bool GameWindow::window_to_screen(float x, float y, float& screen_x, float& screen_y) {
    WindowRect picture;
    {
        std::lock_guard lock(picture_mutex_);
        picture = picture_;
    }
    const float height = static_cast<float>(picture.height());
    if (height <= 0.0f) return false;
    const float scale = static_cast<float>(height_) / height;
    screen_x = static_cast<float>(width_) / 2 + (x - static_cast<float>(picture.left + picture.right) / 2) * scale;
    screen_y = (y - static_cast<float>(picture.top)) * scale;
    return true;
}

void GameWindow::set_menu_renderer(std::function<bool()> init_renderer, std::function<void()> shutdown_renderer) {
    init_menu_renderer_ = std::move(init_renderer);
    shutdown_menu_renderer_ = std::move(shutdown_renderer);
}

// Dear ImGui is set up the first time the menu opens.
bool GameWindow::init_menu() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    int window_width = 0, window_height = 0;
    SDL_GetWindowSize(window_, &window_width, &window_height);
    const float size = std::clamp(static_cast<float>(window_height) / 34.0f, 15.0f, 48.0f);
    if (menu_font_.empty() || !io.Fonts->AddFontFromFileTTF(menu_font_.c_str(), size)) {
        ImFontConfig font;
        font.SizePixels = std::round(size);
        io.Fonts->AddFontDefault(&font);
    }
    ImGui::GetStyle().ScaleAllSizes(size / 16.0f);
    // InitForD3D asserts outside Windows (DXVK is D3D9 there too); both only set up SDL input.
    if (!ImGui_ImplSDL3_InitForOther(window_) || !init_menu_renderer_ || !init_menu_renderer_()) {
        LOG_ERROR("render", "the PC menu cannot start (Dear ImGui)");
        ImGui::DestroyContext();
        pc_menu_ = nullptr;
        return false;
    }
    menu_ready_ = true;
    return true;
}

void GameWindow::close_menu() {
    if (!menu_ready_) return;
    if (shutdown_menu_renderer_) shutdown_menu_renderer_();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    menu_ready_ = menu_open_ = false;
}

void GameWindow::set_menu_open(bool open) {
    if (open == menu_open_ || (open && !menu_ready_ && !init_menu())) return;
    menu_open_ = open;
    if (pc_menu_shown_) pc_menu_shown_(open);  // the game's input first, so it does not take the mouse
    if (open) {
        menu_recapture_ = SDL_GetWindowRelativeMouseMode(window_);
        SDL_SetWindowRelativeMouseMode(window_, false);
    } else if (menu_recapture_) {
        SDL_SetWindowRelativeMouseMode(window_, true);
    }
    LOG_INFO("render", "PC menu %s", open ? "opened" : "closed");
}

void GameWindow::menu_frame() {
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const bool keep = pc_menu_();
    ImGui::Render();
    if (!keep) set_menu_open(false);
}

void GameWindow::set_gamma_ramp(const uint8_t (&ramp)[3][256]) {
    std::lock_guard lock(gamma_mutex_);
    std::memcpy(gamma_pending_, ramp, sizeof(gamma_pending_));
    gamma_changed_ = true;
}

bool GameWindow::take_gamma_table(uint32_t (&table)[256], bool& identity) {
    std::lock_guard lock(gamma_mutex_);
    if (!gamma_changed_) return false;
    gamma_changed_ = false;
    if (brightness_ != 1.0f)
        for (auto& channel : gamma_pending_)
            for (uint8_t& value : channel)
                value = static_cast<uint8_t>(
                    std::lround(255.0 * std::pow(value / 255.0, 1.0 / static_cast<double>(brightness_))));
    identity = true;
    for (uint32_t i = 0; i < 256; ++i) {
        for (const auto& channel : gamma_pending_) identity = identity && channel[i] == i;
        table[i] = 0xFF000000u | uint32_t{gamma_pending_[0][i]} << 16 | uint32_t{gamma_pending_[1][i]} << 8 |
                   gamma_pending_[2][i];
    }
    return true;
}

// --- TextImage ---

namespace {
constexpr int kChar = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
constexpr int kPadding = 4, kLineHeight = kChar + 3;
}  // namespace

TextImage::TextImage() {
    surface_ = SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_ARGB8888);
    if (surface_) renderer_ = SDL_CreateSoftwareRenderer(surface_);
    if (!renderer_) LOG_WARN("render", "overlay text: %s", SDL_GetError());
}

TextImage::~TextImage() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (surface_) SDL_DestroySurface(surface_);
}

bool TextImage::render(const std::vector<std::string>& lines, uint32_t& width, uint32_t& height) {
    if (!renderer_) {
        width = height = 0;
        return false;
    }
    if (rendered_ && lines == last_lines_) {
        width = last_width_;
        height = last_height_;
        return false;
    }
    size_t longest = 0;
    for (const std::string& line : lines) longest = std::max(longest, line.size());
    width = std::min<uint32_t>(kWidth, static_cast<uint32_t>(longest * kChar + 2 * kPadding));
    height = std::min<uint32_t>(kHeight, static_cast<uint32_t>(lines.size() * kLineHeight + 2 * kPadding - 3));

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    const SDL_FRect box{0, 0, static_cast<float>(width), static_cast<float>(height)};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer_, &box);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (size_t i = 0; i < lines.size(); ++i) {
        // First line yellow (a heading), the rest white.
        if (i == 0)
            SDL_SetRenderDrawColor(renderer_, 255, 220, 64, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer_, kPadding, static_cast<float>(kPadding + i * kLineHeight), lines[i].c_str());
    }
    SDL_RenderPresent(renderer_);
    last_lines_ = lines;
    last_width_ = width;
    last_height_ = height;
    rendered_ = true;
    return true;
}

const void* TextImage::pixels() const { return surface_ ? surface_->pixels : nullptr; }
uint32_t TextImage::pitch() const { return surface_ ? static_cast<uint32_t>(surface_->pitch) : 0; }

// BGRX rows -> bottom-up 24-bit BMP.
bool write_bmp(const char* path, const uint8_t* pixels, int32_t pitch, uint32_t width, uint32_t height) {
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const uint32_t row = (width * 3 + 3) & ~3u;
    const uint32_t image = row * height;
    uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int offset, uint32_t v) { std::memcpy(header + offset, &v, 4); };
    put32(2, 54 + image);
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    header[26] = 1;
    header[28] = 24;
    put32(34, image);
    std::fwrite(header, 1, sizeof(header), file);
    std::vector<uint8_t> line(row, 0);
    for (uint32_t y = height; y-- > 0;) {
        const uint8_t* src = pixels + static_cast<int64_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x) std::memcpy(&line[x * 3], src + x * 4, 3);
        std::fwrite(line.data(), 1, row, file);
    }
    return std::fclose(file) == 0;
}

}  // namespace render
