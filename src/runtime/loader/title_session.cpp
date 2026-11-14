// Start-up steps the loader and the own engine host share (title_session.h).

#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "title_crt.h"
#include "replay.h"
#include "bindings.h"
#include "compare.h"
#include "crash.h"
#include "diag_summary.h"
#include "functions.h"
#include "gfx_device.h"
#include "enhanced_graphics.h"
#include "gfx_verify.h"
#include "ngl.h"
#include "ngl_gfx.h"
#include "snd.h"
#include "title_xapi.h"
#include "title_host.h"
#include "title_files.h"
#include "title_objects.h"
#include "dispatcher.h"
#include "snd_env.h"
#include "sound_engine.h"
#include "kernel.h"
#include "civil_time.h"
#include "kernel_memory.h"
#include "title_threads.h"
#include "hooks.h"
#include "host_image.h"
#include "qcommon_fs.h"
#include "text_trap.h"
#include "static_init.h"
#include "title_start.h"
#include "log.h"
#include "os.h"
#include "patches.h"
#include "pc_menu.h"
#include "profiler.h"
#include "release_map.h"
#include "paths.h"
#include "program.h"
#include "script.h"
#include "scr_fiber.h"
#include "settings.h"
#include "shader_dump.h"
#include "kapf.h"
#include "key_prompts.h"
#include "sharp_text.h"
#include "symbols.h"
#include "thread_env.h"
#include "xbe.h"
#include "xbox_d3d_api.h"
#include "test_mode.h"
#include "xbox_dsound.h"
#include "xbox_input.h"
#include "xonline.h"
#include "options.h"
#include "title_session.h"

namespace loader {

namespace {

std::vector<std::string> g_args;
std::optional<fs::path> g_diag_dir;
fs::path g_log_path;           // this run's log file
bool g_tell_player = false;    // error boxes (not in scripted runs)

}  // namespace

// What went wrong, for a player without a console (a message box on Windows).
void tell_player(const std::string& what) {
    if (!g_tell_player) return;
    loader::os::show_error("Call of Duty 2: Big Red One", what + "\n\nDetails are in the log:\n" + g_log_path.string());
}

namespace {

// The title's writable image memory (headers, writable sections - .data, the SDK data, the per-level sections) as
// it was before the entry ran, for in-process restarts (own engine host 5b; reference counts of loaded sections
// live in the headers).
struct ImageSnapshot {
    struct Range {
        uint32_t address;
        std::vector<uint8_t> bytes;
    };
    std::vector<Range> ranges;

    void take(const XbeImage& image) {
        auto add = [&](uint32_t address, uint32_t size) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(uintptr_t{address});
            ranges.push_back({address, std::vector<uint8_t>(bytes, bytes + size)});
        };
        add(image.base(), image.header().size_of_headers);
        for (const XbeSectionHeader* section : image.sections())
            if (section->flags & kSectionWritable) add(section->virtual_addr, section->virtual_size);
    }
    void restore() const {
        for (const Range& range : ranges)
            std::memcpy(reinterpret_cast<void*>(uintptr_t{range.address}), range.bytes.data(), range.bytes.size());
    }
};


// Without game files the player gets the launcher, which says where they go. Not when
// the folder was named on the command line or in COD_BRO_ASSETS (development).
bool start_launcher_for_assets(int argc, char** argv) {
    if (const char* env = std::getenv("COD_BRO_ASSETS"); env && *env) return false;
    for (int i = 1; i < argc; ++i)
        if (loader::option_name(argv[i]) == "--assets") return false;
    const fs::path launcher = platform::sibling_executable("cod_bro_launcher");
    if (!fs::exists(launcher)) return false;
    LOG_INFO("loader", "no game files: starting %s", launcher.string().c_str());
    return platform::start_program(launcher);
}

}  // namespace

int startup_failed(const std::string& what) {
    tell_player("The game could not start: " + what + ".");
    return 1;
}


const std::optional<fs::path>& diag_dir() { return g_diag_dir; }
std::vector<std::string>& session_args() { return g_args; }

fs::path data_file(const char* name) {
    // A release has the data files next to the program; builds use the source tree's.
    const fs::path next_to_loader = platform::executable_dir() / name;
    return fs::exists(next_to_loader) ? next_to_loader : fs::path(COD_BRO_SYMBOLS_DIR) / name;
}

// Options that change how the game plays (its state, not only the picture or sound): a recording keeps their
// values as "#game NAME=VALUE" lines after its command line, and a replay takes them from there, so it plays as
// recorded. A recording made before an option existed lacks its line and gets the original behaviour. Naming the
// option on the replay's command line overrides the recording (A/B checks: the run then differs from it).
void apply_game_options(Options& options, bool replaying, const std::string& recorded_command_line) {
    struct GameOption {
        const char* name;
        const char* flag;
        bool Options::* field;
        bool original;
    };
    static constexpr GameOption kGameOptions[] = {
        {"mounted_gun_fix", "--mounted-gun-fix", &Options::mounted_gun_fix, false},
    };
    for (const GameOption& option : kGameOptions) {
        const std::string key = std::string("#game ") + option.name + "=";
        if (replaying && options.given.count(option.flag)) {
            LOG_INFO("replay", "%s %s from the command line, not as recorded", option.name,
                     options.*option.field ? "on" : "off");
        } else if (replaying) {
            bool value = option.original;
            const size_t at = recorded_command_line.find(key);
            if (at != std::string::npos) value = recorded_command_line.compare(at + key.size(), 1, "1") == 0;
            if (options.*option.field != value)
                LOG_INFO("replay", "%s %s as recorded", option.name, value ? "on" : "off");
            options.*option.field = value;
        }
        options.replay.command_line += key + (options.*option.field ? "1" : "0") + "\n";
    }
}

int load_options(int argc, char** argv, std::optional<Options>& parsed_out) {
    // settings.ini first, so the command line overrides it. A reboot re-reads it.
    // Not with --compare: the comparison runs the game as on the Xbox, whatever the
    // player chose in the launcher.
    bool comparing = false;
    for (int i = 1; i < argc; ++i)
        comparing |= option_name(argv[i]) == "--compare" || option_name(argv[i]) == "--compare-all";
    // Record/replay: a recording keeps the settings.ini text and command line it ran with, and a replay uses that
    // text in place of the player's settings.ini (which may have changed since, and is another file under Wine).
    std::string replay_file, settings_text, recorded_command_line;
    bool replaying = false;
    for (int i = 1; i + 1 < argc; ++i)
        if (option_name(argv[i]) == "--record" || option_name(argv[i]) == "--replay") {
            replaying = option_name(argv[i]) == "--replay";
            replay_file = argv[i + 1];
        }
    if (replaying) {
        if (!xbox::replay::read_recorded_settings(replay_file, settings_text, recorded_command_line)) {
            std::fprintf(stderr, "%s is not a recording of this build's format (record it again)\n", replay_file.c_str());
            return 2;
        }
    } else if (!replay_file.empty() && !comparing) {
        std::ifstream file(platform::settings_path(), std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        settings_text = text.str();
    }
    const platform::Settings settings = comparing ? platform::Settings{}
                                        : replaying ? platform::Settings::parse(settings_text)
                                                    : platform::load_settings();
    std::vector<std::string> merged = settings.as_args();
    merged.insert(merged.begin(), argv[0]);
    merged.insert(merged.end(), argv + 1, argv + argc);
    std::vector<char*> merged_argv;
    for (std::string& arg : merged) merged_argv.push_back(arg.data());
    std::optional<Options> parsed = parse_args(static_cast<int>(merged_argv.size()), merged_argv.data());
    if (!parsed) {
        if (!settings.empty())
            std::fprintf(stderr, "(options include %s)\n", platform::settings_path().string().c_str());
        return 2;
    }
    parsed->replay.settings_text = settings_text;
    for (int i = 0; i < argc; ++i) parsed->replay.command_line += std::string(argv[i]) + "\n";
    if (replaying)
        LOG_INFO("replay", "using the recording's settings.ini; it was recorded with: %s", [&] {
            std::string line = recorded_command_line;
            std::replace(line.begin(), line.end(), '\n', ' ');
            return line;
        }().c_str());
    if (!parsed->pc_patches.value_or(!parsed->compare)) {
        // PC features off: the presentation is the Xbox's too (4:3 without the EEPROM
        // widescreen flag, the game's own fonts and button icons, 640x480, its texture filtering), unless
        // an option asks for one of them explicitly.
        const auto unless_given = [&](const char* name, auto& field, auto value) {
            if (!parsed->given.count(name)) field = value;
        };
        unless_given("--aspect", parsed->widescreen, false);
        unless_given("--sharp-text", parsed->sharp_text, false);
        unless_given("--button-prompts", parsed->button_prompts, loader::TitlePatchConfig::ButtonPrompts::Controller);
        unless_given("--render-scale", parsed->render_scale, 1.0f);
        unless_given("--anisotropy", parsed->anisotropy, uint32_t{1});
        unless_given("--brightness", parsed->brightness, 1.0f);
        unless_given("--audio", parsed->audio_enhanced, false);
        unless_given("--graphics", parsed->graphics_enhanced, false);
        parsed->mounted_gun_fix = false;
    }
    apply_game_options(*parsed, replaying, recorded_command_line);
    parsed_out = std::move(parsed);
    return 0;
}

bool start_session(const Options& options, int argc, char** argv) {
    g_args.assign(argv, argv + argc);
    g_diag_dir = options.diag_dir;
    if (options.session_start_ns)
        platform::set_session_start(std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(*options.session_start_ns))));
    platform::log_set_level(options.log_level);
    g_tell_player = options.press_script.empty() && !options.screenshot_dir;
    if (options.diag_dir) {
        fs::create_directories(*options.diag_dir);
        g_log_path = *options.diag_dir / "log.txt";
        if (!platform::log_set_file(g_log_path.string().c_str())) {
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
            return false;
        }
        LOG_INFO("diag", "diagnostics in %s: a screenshot every %u s, F12 marks a problem",
                 options.diag_dir->string().c_str(), options.diag_seconds);
        // summary.txt when the process ends (not on a reboot: the session goes on).
        std::atexit([] { loader::write_diag_summary(*g_diag_dir); });
    } else {
        // Always a log, so that a player without a console can send it: one run per
        // file (a reboot continues it), in the user data folder.
        std::error_code ec;
        fs::create_directories(platform::user_data_dir(), ec);
        g_log_path = platform::user_data_dir() / "log.txt";
        if (!options.launch_data_fd) fs::remove(g_log_path, ec);
        constexpr long kPlayerLogLimit = 8l << 20;
        if (!platform::log_set_file(g_log_path.string().c_str(), kPlayerLogLimit))
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
    }
    loader::set_fatal_exit_hook([](bool error) {
        loader::compare::flush();
        if (!error) render::save_caches();  // a crash may have left the GPU device unusable
        if (g_diag_dir) loader::write_diag_summary(*g_diag_dir);
        if (error) tell_player("The game stopped after an error.");
    });
    // An uncaught C++ exception (e.g. std::thread failing to start one) would end the
    // process without a word.
    std::set_terminate([] {
        const char* what = "unknown";
        if (const std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& e) {
                what = e.what();
            } catch (...) {
            }
        }
        LOG_ERROR("fatal", "uncaught C++ exception: %s", what);
        loader::run_fatal_exit_hook(true);
        std::_Exit(3);
    });
    return true;
}

std::optional<fs::path> find_assets(const Options& options, int argc, char** argv) {
    const auto assets = platform::resolve_asset_root(options.assets);
    if (!assets) {
        if (!start_launcher_for_assets(argc, argv))
            { startup_failed("no game files (copy your game dump into the assets folder)");
            return std::nullopt;
        }
        return std::nullopt;
    }
    LOG_INFO("loader", "assets: %s", assets->string().c_str());
    return assets;
}

int run_title(XbeImage& image, ThreadEnv& env, const Options& options) {
    // In-process restarts (own engine host 5b) put the title's memory back to this point.
    ImageSnapshot image_snapshot;
    bool replay_pending = options.replay_at_switch && options.replay.mode != xbox::replay::Mode::Off;
    if (options.own_main) {
        image_snapshot.take(image);
        xbox::snapshot_memory();
    }
    if (options.text_clear_out) loader::text_clear_protect();

    LOG_INFO("loader", "starting title at %08x", image.entry_point());
    // Four zero dwords stand in for arguments in case the entry point reads any.
    const uint32_t args[4] = {};
    loader::run_on_xbox_stack(image.entry_point(), env, args);
    while (options.own_main) {
        // The title switches levels by launching itself again (XLaunchNewImageA): here in this process, on the same
        // window and devices, once its main thread has ended.
        const std::vector<uint8_t> page = loader::wait_for_title_restart();
        LOG_INFO("loader", "restart: the title starts again in this process");
        if (options.diag_dir)
            if (std::FILE* file = std::fopen((*options.diag_dir / "launch_data.bin").string().c_str(), "wb")) {
                std::fwrite(page.data(), 1, page.size(), file);
                std::fclose(file);
            }
        loader::compare::flush();
        xbox::replay::stop();  // a recording or replay covers one start
        render::restart_title();
        audio::forget_title_voices();
        game::title_restart_reset();
        image_snapshot.restore();
        xbox::restore_memory();
        loader::reset_thread_ids();
        if (!xbox::place_launch_data_page(page)) LOG_ERROR("loader", "restart: no memory for the launch data");
        if (std::exchange(replay_pending, false) && !xbox::replay::start(options.replay)) std::_Exit(1);
        LOG_INFO("loader", "starting title at %08x", image.entry_point());
        loader::run_on_xbox_stack(image.entry_point(), env, args);
    }
    // The entry point only starts the title's main thread and returns. Park this
    // thread; the process lives on until the title quits. It must not exit: once the
    // initial thread is gone, Linux no longer resolves /proc/self/exe, which DXVK
    // and SDL read (and on Windows the stub's code it would return to is gone).
    LOG_INFO("loader", "title entry point returned");
    for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
}

}  // namespace loader
