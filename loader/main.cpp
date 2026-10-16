// Route-B loader: runs the original default.xbe natively on 32-bit x86 Linux, with
// the Xbox kernel (and, later, the SDK libraries) replaced by our own code.

#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "crash.h"
#include "diag_summary.h"
#include "d3d.h"
#include "xinput.h"
#include "dsound.h"
#include "kernel.h"
#include "kernel_thunks.h"
#include "log.h"
#include "memory.h"
#include "patches.h"
#include "paths.h"
#include "process.h"
#include "script.h"
#include "settings.h"
#include "symbols.h"
#include "thread_env.h"
#include "xbe.h"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::optional<fs::path> assets;
    std::optional<fs::path> xbe;
    std::optional<fs::path> symbols;
    std::optional<fs::path> data;
    platform::LogLevel log_level = platform::LogLevel::Info;
    bool trace_kernel = false;
    uint32_t memory_mb = 64;
    uint32_t scale = 2;
    uint32_t anisotropy = 16;
    float render_scale = 0;  // 0: the window's (fullscreen: the screen's) height
    bool fullscreen = false;
    bool widescreen = true;
    uint32_t screenshot_interval = 0;
    std::optional<fs::path> screenshot_dir;
    bool silent = false;
    std::string press_script;
    float mouse_sensitivity = 0.11f;
    bool aim_assist = true;
    int32_t start_level = -1;
    std::optional<fs::path> diag_dir;
    uint32_t diag_seconds = 10;
    // Set by a title reboot (see reboot_title), not by users.
    std::optional<int> launch_data_fd;
    std::optional<int64_t> session_start_ns;
};

// The command line, repeated when the title reboots into another XBE.
std::vector<std::string> g_args;
fs::path g_xbe;  // the running XBE
std::optional<fs::path> g_diag_dir;

void print_usage(const char* program) {
    std::printf(
        "usage: %s [options]\n"
        "  --assets DIR     game files (default: COD_BRO_ASSETS, ./assets, or the dev folder)\n"
        "  --xbe FILE       executable to load (default: <assets>/default.xbe)\n"
        "  --symbols FILE   Ghidra symbol export: SDK replacements and readable backtraces\n"
        "                   (default: default_xbe_symbols.txt next to the loader, or tools/data)\n"
        "  --data DIR       where saves and the cache drive go (default: the user data folder)\n"
        "  --debug          debug logging\n"
        "  --trace          log every kernel call (implies --debug)\n"
        "  --silent         no sound output (audio still runs, into a silent device)\n"
        "  --memory MB      Xbox memory budget (default 64, like a retail Xbox; devkit: 128)\n"
        "  --fullscreen     borderless fullscreen (Alt+Enter switches while playing)\n"
        "  --windowed       in a window (default; overrides fullscreen in settings.ini)\n"
        "  --scale N        window size as a multiple of the game's resolution (default 2)\n"
        "  --render-scale X render resolution as a multiple of the game's 640x480 (default: the\n"
        "                   window's or screen's height; above it = supersampling, e.g. 4)\n"
        "  --aspect A       16:9 (default: the game's widescreen mode, wider view and HUD) or 4:3\n"
        "  --anisotropy N   anisotropic filtering for mipmapped textures, 1-16 (default 16;\n"
        "                   1 = what the game asks for)\n"
        "  --screenshots N DIR  save every Nth frame as DIR/frame_<n>.bmp (debugging)\n"
        "  --press LIST     scripted presses on port 0, e.g. start@20,a@25.5,back@80+4 (seconds after\n"
        "                   start, +S holds the button S seconds;\n"
        "                   buttons: a b x y black white lt rt start back up down left right ls rs)\n"
        "  --diag DIR [S]   diagnostics while playing: DIR/log.txt, a screenshot every S seconds\n"
        "                   (default 10); F12 marks a problem: screenshot, a note typed while the\n"
        "                   game is paused (DIR/marks.txt), draw dump in the log; new warnings\n"
        "                   and errors are marked by themselves; F11 shows draws that need\n"
        "                   missing features in magenta; DIR/summary.txt on exit\n"
        "  --mouse-sensitivity DEG  degrees the view turns per mouse count (default 0.11)\n"
        "  --aim-assist on|off  the view following an enemy under the crosshair (default on)\n"
        "  --level N        start mission N (0-13, 0 = prologue) once the menus are up (testing)\n",
        program);
}

std::string option_name(std::string arg) {
    if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') arg.insert(0, "-");  // -diag works like --diag
    return arg;
}

std::optional<Options> parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = option_name(argv[i]);
        auto value = [&]() -> std::optional<fs::path> {
            if (i + 1 < argc) return fs::path(argv[++i]);
            std::fprintf(stderr, "%s needs a value\n", arg.c_str());
            return std::nullopt;
        };
        if (arg == "--assets") {
            if (!(options.assets = value())) return std::nullopt;
        } else if (arg == "--xbe") {
            if (!(options.xbe = value())) return std::nullopt;
        } else if (arg == "--data") {
            if (!(options.data = value())) return std::nullopt;
        } else if (arg == "--symbols") {
            if (!(options.symbols = value())) return std::nullopt;
        } else if (arg == "--memory") {
            if (i + 1 >= argc || (options.memory_mb = static_cast<uint32_t>(std::atoi(argv[++i]))) < 16 ||
                options.memory_mb > 1024) {
                std::fprintf(stderr, "--memory needs a size in MB (16-1024)\n");
                return std::nullopt;
            }
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--windowed") {
            options.fullscreen = false;
        } else if (arg == "--scale") {
            if (i + 1 >= argc || (options.scale = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1 ||
                options.scale > 8) {
                std::fprintf(stderr, "--scale needs a number (1-8)\n");
                return std::nullopt;
            }
        } else if (arg == "--render-scale") {
            if (i + 1 >= argc || (options.render_scale = std::strtof(argv[++i], nullptr)) < 0.5f ||
                options.render_scale > 8.0f) {
                std::fprintf(stderr, "--render-scale needs a number (0.5-8)\n");
                return std::nullopt;
            }
        } else if (arg == "--aspect") {
            const std::string aspect = i + 1 < argc ? argv[++i] : "";
            if (aspect == "16:9") {
                options.widescreen = true;
            } else if (aspect == "4:3") {
                options.widescreen = false;
            } else {
                std::fprintf(stderr, "--aspect needs 16:9 or 4:3\n");
                return std::nullopt;
            }
        } else if (arg == "--anisotropy") {
            if (i + 1 >= argc || (options.anisotropy = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1 ||
                options.anisotropy > 16) {
                std::fprintf(stderr, "--anisotropy needs a number (1-16)\n");
                return std::nullopt;
            }
        } else if (arg == "--screenshots") {
            if (i + 2 >= argc || (options.screenshot_interval = static_cast<uint32_t>(std::atoi(argv[++i]))) < 1) {
                std::fprintf(stderr, "--screenshots needs a frame interval and a folder\n");
                return std::nullopt;
            }
            options.screenshot_dir = fs::path(argv[++i]);
        } else if (arg == "--press") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--press needs a list of button@seconds\n");
                return std::nullopt;
            }
            options.press_script = argv[++i];
        } else if (arg == "--mouse-sensitivity") {
            if (i + 1 >= argc || (options.mouse_sensitivity = std::strtof(argv[++i], nullptr)) <= 0.0f ||
                options.mouse_sensitivity > 10.0f) {
                std::fprintf(stderr, "--mouse-sensitivity needs degrees per count (0-10)\n");
                return std::nullopt;
            }
        } else if (arg == "--aim-assist") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            if (value != "on" && value != "off") {
                std::fprintf(stderr, "--aim-assist needs on or off\n");
                return std::nullopt;
            }
            options.aim_assist = value == "on";
        } else if (arg == "--level") {
            if (i + 1 >= argc || (options.start_level = std::atoi(argv[++i])) < 0 || options.start_level > 13) {
                std::fprintf(stderr, "--level needs a mission number (0-13)\n");
                return std::nullopt;
            }
        } else if (arg == "--diag") {
            if (!(options.diag_dir = value())) return std::nullopt;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.diag_seconds = static_cast<uint32_t>(std::atoi(argv[++i]));
                if (options.diag_seconds < 1) {
                    std::fprintf(stderr, "--diag needs seconds >= 1\n");
                    return std::nullopt;
                }
            }
        } else if (arg == "--launch-data" && i + 1 < argc) {
            options.launch_data_fd = std::atoi(argv[++i]);
        } else if (arg == "--session-start" && i + 1 < argc) {
            options.session_start_ns = std::strtoll(argv[++i], nullptr, 10);
        } else if (arg == "--silent") {
            options.silent = true;
        } else if (arg == "--debug") {
            options.log_level = platform::LogLevel::Debug;
        } else if (arg == "--trace") {
            options.log_level = platform::LogLevel::Trace;
            options.trace_kernel = true;
        } else {
            print_usage(argv[0]);
            return std::nullopt;
        }
    }
    return options;
}

uint32_t current_kthread() {
    const loader::ThreadEnv* env = loader::current_thread_env();
    return env ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(env->kthread)) : 0;
}

uint8_t* current_irql() {
    loader::ThreadEnv* env = loader::current_thread_env();
    return env ? &env->kpcr->irql : nullptr;
}

void attach_kernel_thread() {
    loader::ThreadEnv* env = loader::create_thread_env(0, 0);
    if (!env || !loader::activate_thread_env(env)) LOG_ERROR("loader", "cannot prepare a kernel thread");
}

void call_guest(uint32_t function, const uint32_t* args, uint32_t count) {
    loader::ThreadEnv* env = loader::current_thread_env();
    if (!env) {
        LOG_ERROR("loader", "call to %08x from a thread without an Xbox environment", function);
        return;
    }
    loader::run_on_xbox_stack(function, *env, std::span(args, count));
}

// The page the previous title left in LaunchDataPage, from the memory file
// reboot_title passed on.
std::vector<uint8_t> read_launch_data(int fd) {
    std::vector<uint8_t> page(xbox::kLaunchDataPageSize);
    const ssize_t got = pread(fd, page.data(), page.size(), 0);
    close(fd);
    if (got != static_cast<ssize_t>(page.size())) {
        LOG_ERROR("loader", "launch data: read %zd of %zu bytes", got, page.size());
        return {};
    }
    return page;
}

// HalReturnToFirmware(QuickReboot): the Xbox reboots into `xbe` with the launch data
// page kept in memory. Here the loader replaces itself (exec) with the same command
// line for `xbe`, passing the page in a memory file and the session start time (log
// and --diag times continue). Scripted presses and --level are for the first boot only.
[[noreturn]] void reboot_title(const fs::path& xbe, std::span<const uint8_t> launch_data_page) {
    // Patches and symbols are for default.xbe (multiplayer is codmp_xboxf.xbe).
    if (fs::path(xbe).filename() != fs::path(g_xbe).filename()) {
        LOG_ERROR("loader", "reboot: %s is not supported, only %s", xbe.filename().string().c_str(),
                  g_xbe.filename().string().c_str());
        _exit(1);
    }
    if (g_diag_dir) {
        // To repeat this boot: --launch-data 3 3<launch_data.bin
        if (std::FILE* file = std::fopen((*g_diag_dir / "launch_data.bin").string().c_str(), "wb")) {
            std::fwrite(launch_data_page.data(), 1, launch_data_page.size(), file);
            std::fclose(file);
        }
    }
    const int fd = memfd_create("cod_bro_launch_data", 0);  // inherited by the new image
    if (fd < 0 || write(fd, launch_data_page.data(), launch_data_page.size()) !=
                      static_cast<ssize_t>(launch_data_page.size())) {
        LOG_ERROR("loader", "reboot: cannot pass the launch data");
        _exit(1);
    }
    std::vector<std::string> args = {g_args[0]};
    for (size_t i = 1; i < g_args.size(); ++i) {
        const std::string name = option_name(g_args[i]);
        if (name == "--xbe" || name == "--launch-data" || name == "--session-start" || name == "--press" ||
            name == "--level") {
            ++i;  // dropped with its value
            continue;
        }
        args.push_back(g_args[i]);
    }
    const auto start = platform::session_start().time_since_epoch();
    args.insert(args.end(), {"--xbe", xbe.string(), "--launch-data", std::to_string(fd), "--session-start",
                             std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(start).count())});
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    LOG_INFO("loader", "reboot: starting %s", xbe.string().c_str());
    execv("/proc/self/exe", argv.data());
    LOG_ERROR("loader", "reboot: exec failed: %s", std::strerror(errno));
    _exit(1);
}

xbox::KernelConfig kernel_config(const Options& options, const fs::path& assets, const fs::path& xbe) {
    static std::string image_file_name;
    image_file_name = "\\Device\\CdRom0\\" + xbe.filename().string();
    xbox::KernelConfig config{};
    config.image_file_name = image_file_name.c_str();
    config.trace_calls = options.trace_kernel;
    config.current_thread = current_kthread;
    config.current_irql = current_irql;
    config.attach_kernel_thread = attach_kernel_thread;
    config.call_guest = call_guest;
    config.memory.physical_bytes = options.memory_mb * 1024 * 1024;
    config.widescreen = options.widescreen;
    config.cdrom_root = assets;
    config.data_root = options.data.value_or(platform::user_data_dir());
    if (options.launch_data_fd) config.launch_data_page = read_launch_data(*options.launch_data_fd);
    config.reboot = reboot_title;
    return config;
}

// Without game files the player gets the launcher, which says where they go. Not when
// the folder was named on the command line or in COD_BRO_ASSETS (development).
bool start_launcher_for_assets(int argc, char** argv) {
    if (const char* env = std::getenv("COD_BRO_ASSETS"); env && *env) return false;
    for (int i = 1; i < argc; ++i)
        if (option_name(argv[i]) == "--assets") return false;
    const fs::path launcher = platform::sibling_executable("cod_bro_launcher");
    if (!fs::exists(launcher)) return false;
    LOG_INFO("loader", "no game files: starting %s", launcher.string().c_str());
    return platform::start_program(launcher);
}

}  // namespace

int main(int argc, char** argv) {
    // settings.ini first, so the command line overrides it. A reboot re-reads it.
    const platform::Settings settings = platform::load_settings();
    std::vector<std::string> merged = settings.as_args();
    merged.insert(merged.begin(), argv[0]);
    merged.insert(merged.end(), argv + 1, argv + argc);
    std::vector<char*> merged_argv;
    for (std::string& arg : merged) merged_argv.push_back(arg.data());
    const std::optional<Options> options = parse_args(static_cast<int>(merged_argv.size()), merged_argv.data());
    if (!options) {
        if (!settings.empty())
            std::fprintf(stderr, "(options include %s)\n", platform::settings_path().string().c_str());
        return 2;
    }
    g_args.assign(argv, argv + argc);
    g_diag_dir = options->diag_dir;
    if (options->session_start_ns)
        platform::set_session_start(std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(*options->session_start_ns))));
    platform::log_set_level(options->log_level);
    if (options->diag_dir) {
        fs::create_directories(*options->diag_dir);
        const fs::path log = *options->diag_dir / "log.txt";
        if (!platform::log_set_file(log.string().c_str())) {
            std::fprintf(stderr, "cannot write %s\n", log.string().c_str());
            return 1;
        }
        LOG_INFO("diag", "diagnostics in %s: a screenshot every %u s, F12 marks a problem",
                 options->diag_dir->string().c_str(), options->diag_seconds);
        // summary.txt when the process ends (not on a reboot: the session goes on).
        std::atexit([] { loader::write_diag_summary(*g_diag_dir); });
        loader::set_fatal_exit_hook([] { loader::write_diag_summary(*g_diag_dir); });
    }

    const auto assets = platform::resolve_asset_root(options->assets);
    if (!assets) {
        start_launcher_for_assets(argc, argv);
        return 1;
    }
    LOG_INFO("loader", "assets: %s", assets->string().c_str());

    static loader::XbeImage image;
    const fs::path xbe = options->xbe.value_or(*assets / "default.xbe");
    if (!image.load(xbe)) return 1;
    g_xbe = xbe;

    static loader::SymbolTable symbols;
    // A release has it next to the loader; builds use the source tree's.
    fs::path symbol_path = platform::executable_dir() / "default_xbe_symbols.txt";
    if (options->symbols)
        symbol_path = *options->symbols;
    else if (!fs::exists(symbol_path))
        symbol_path = COD_BRO_DEFAULT_SYMBOLS;
    if (symbols.load(symbol_path))
        LOG_INFO("loader", "%zu symbols from %s", symbols.size(), symbol_path.string().c_str());
    else
        LOG_WARN("loader", "no symbols at %s; backtraces will show addresses only", symbol_path.string().c_str());

    loader::install_crash_handler(image, symbols);
    if (!xbox::kernel_init(kernel_config(*options, *assets, xbe))) return 1;
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::install_kernel_thunks(image);
    loader::install_title_patches({options->mouse_sensitivity, options->start_level, options->aim_assist});
    audio::configure({options->silent ? audio::OutputConfig::Device::Loopback : audio::OutputConfig::Device::Default});
    render::D3DConfig d3d_config;
    d3d_config.backend.window_scale = options->scale;
    d3d_config.backend.anisotropy = options->anisotropy;
    if (options->widescreen) {
        d3d_config.backend.display_aspect = 16.0f / 9.0f;
        d3d_config.backend.pillarbox_flat_frames = true;  // the menus are 4:3 layouts
        d3d_config.backend.narrow_ui_draw = &loader::in_game_menu_draw;
    }
    d3d_config.backend.fullscreen = options->fullscreen;
    d3d_config.backend.render_scale = options->render_scale;  // 0: the back end picks
    d3d_config.screenshot_interval = options->screenshot_interval;
    if (options->diag_dir) {
        d3d_config.diag_dir = options->diag_dir->string();
        d3d_config.diag_seconds = options->diag_seconds;
        d3d_config.pause_audio = &audio::set_paused;
    }
    if (options->screenshot_dir) {
        fs::create_directories(*options->screenshot_dir);
        d3d_config.screenshot_dir = options->screenshot_dir->string();
    }
    d3d_config.device_pointer = loader::kTitleD3DDevicePointer;
    d3d_config.render_state = loader::kTitleD3DRenderState;
    d3d_config.texture_state = loader::kTitleD3DTextureState;
    render::configure(d3d_config);
    input::configure({loader::kTitleGamepadType});
    if (!options->press_script.empty() && !input::set_script(options->press_script)) return 1;
    if (!loader::install_sdk_replacements(image, symbols)) {
        LOG_ERROR("loader", "SDK replacements need the symbol file (%s)", symbol_path.string().c_str());
        return 1;
    }

    // The title's initial thread. Like the Xbox's, it has no TLS block (TlsData is
    // null); XAPI only creates TLS for the threads it starts itself.
    loader::ThreadEnv* env = loader::create_thread_env(image.header().pe_stack_commit, 0);
    if (!env || !loader::activate_thread_env(env)) return 1;

    LOG_INFO("loader", "starting title at %08x", image.entry_point());
    // Four zero dwords stand in for arguments in case the entry point reads any.
    const uint32_t args[4] = {};
    loader::run_on_xbox_stack(image.entry_point(), *env, args);
    // The entry point only starts the title's main thread and returns. Park this
    // thread; the process lives on until the title quits. It must not exit: once the
    // initial thread is gone, Linux no longer resolves /proc/self/exe, which DXVK
    // and SDL read.
    LOG_INFO("loader", "title entry point returned");
    for (;;) pause();
}
