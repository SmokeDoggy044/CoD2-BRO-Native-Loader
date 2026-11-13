// Route-B loader: runs the original default.xbe natively on 32-bit x86 (Linux or
// Windows), with the Xbox kernel and the SDK libraries replaced by our own code.

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
#include "kernel_thunks.h"
#include "hooks.h"
#include "host_image.h"
#include "qcommon_fs.h"
#include "text_trap.h"
#include "static_init.h"
#include "title_start.h"
#include "log.h"
#include "options.h"
#include "title_setup.h"
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


namespace fs = std::filesystem;

using loader::Options;
using loader::option_name;
using loader::parse_args;
using loader::print_usage;

namespace {

// The command line, repeated when the title reboots into another XBE.
std::vector<std::string> g_args;
fs::path g_xbe;  // the running XBE
std::optional<fs::path> g_diag_dir;
fs::path g_log_path;           // this run's log file
bool g_tell_player = false;    // error boxes (not in scripted runs)

// What went wrong, for a player without a console (a message box on Windows).
void tell_player(const std::string& what) {
    if (!g_tell_player) return;
    loader::os::show_error("Call of Duty 2: Big Red One", what + "\n\nDetails are in the log:\n" + g_log_path.string());
}

// The mapped image, for release_map.h.
loader::ImageBytes image_bytes(const loader::XbeImage& image) {
    loader::ImageBytes bytes;
    bytes.base = image.base();
    bytes.bytes = {reinterpret_cast<const uint8_t*>(uintptr_t{image.base()}), image.size()};
    for (const loader::XbeSectionHeader* section : image.sections())
        bytes.sections.push_back({image.section_name(*section), section->virtual_addr, section->virtual_size});
    return bytes;
}

// The title's writable image memory (headers, writable sections - .data, the SDK data, the per-level sections) as
// it was before the entry ran, for in-process restarts (own engine host 5b; reference counts of loaded sections
// live in the headers).
struct ImageSnapshot {
    struct Range {
        uint32_t address;
        std::vector<uint8_t> bytes;
    };
    std::vector<Range> ranges;

    void take(const loader::XbeImage& image) {
        auto add = [&](uint32_t address, uint32_t size) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(uintptr_t{address});
            ranges.push_back({address, std::vector<uint8_t>(bytes, bytes + size)});
        };
        add(image.base(), image.header().size_of_headers);
        for (const loader::XbeSectionHeader* section : image.sections())
            if (section->flags & loader::kSectionWritable) add(section->virtual_addr, section->virtual_size);
    }
    void restore() const {
        for (const Range& range : ranges)
            std::memcpy(reinterpret_cast<void*>(uintptr_t{range.address}), range.bytes.data(), range.bytes.size());
    }
};

int startup_failed(const std::string& what) {
    tell_player("The game could not start: " + what + ".");
    return 1;
}


uint32_t current_kthread() {
    const loader::ThreadEnv* env = loader::current_thread_env();
    return env ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(env->kthread)) : 0;
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

// HalReturnToFirmware(QuickReboot): the Xbox reboots into `xbe` with the launch data
// page kept in memory. Here the loader replaces itself (exec) with the same command
// line for `xbe`, passing the page in a file and the session start time (log and
// --diag times continue). Scripted presses, --level and --exec are for the first boot only.
[[noreturn]] void reboot_title(const fs::path& xbe, std::span<const uint8_t> launch_data_page) {
    // Patches and symbols exist for the disc's two titles only.
    const bool same_title = fs::path(xbe).filename() == g_xbe.filename();
    if (!loader::find_title(xbe.filename().string())) {
        LOG_ERROR("loader", "reboot: %s is not supported", xbe.filename().string().c_str());
        std::_Exit(1);
    }
    if (g_diag_dir) {
        // To repeat this boot: --launch-data 3 3<launch_data.bin
        if (std::FILE* file = std::fopen((*g_diag_dir / "launch_data.bin").string().c_str(), "wb")) {
            std::fwrite(launch_data_page.data(), 1, launch_data_page.size(), file);
            std::fclose(file);
        }
    }
    loader::compare::flush();
    xbox::replay::stop();  // a recording or replay covers one boot
    const int fd = loader::os::pass_launch_data(launch_data_page);
    if (fd < 0) {
        LOG_ERROR("loader", "reboot: cannot pass the launch data");
        std::_Exit(1);
    }
    std::vector<std::string> args = {g_args[0]};
    for (size_t i = 1; i < g_args.size(); ++i) {
        const std::string name = option_name(g_args[i]);
        if (name == "--xbe" || name == "--launch-data" || name == "--session-start" || name == "--press" ||
            name == "--level" || name == "--exec" || name == "--force" || name == "--multiplayer" || (name == "--symbols" && !same_title) ||
            name == "--record" || name == "--replay" || name == "--replay-hashes" || name == "--replay-speed") {
            ++i;  // dropped with its value
            continue;
        }
        if (name == "--replay-dump" || name == "--replay-reference" || name == "--replay-shots") {  // a recording covers one boot
            i += 2;
            continue;
        }
        args.push_back(g_args[i]);
    }
    const auto start = platform::session_start().time_since_epoch();
    args.insert(args.end(), {"--xbe", xbe.string(), "--launch-data", std::to_string(fd), "--session-start",
                             std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(start).count())});
    LOG_INFO("loader", "reboot: starting %s", xbe.string().c_str());
    loader::os::restart(args);
}

// The languages the game's text is on the disc in (Global.<code>.stb in the packs), for
// the log: the title picks its language from the console's and the disc's region (a
// European disc has no English text), so the language setting only chooses among these.
void log_disc_languages(const fs::path& assets, uint32_t language) {
    std::string codes;
    for (const char* name : {"sp/global.cod", "mp/global.cod"}) {
        const auto pack = text::KapfPack::open(assets / name);
        if (!pack) continue;
        for (const auto& entry : pack->entries()) {
            const std::string_view file = entry.name;
            if (!file.starts_with("Global.") || !file.ends_with(".stb") || file.size() <= 11) continue;
            const std::string code(file.substr(7, file.size() - 11));
            if (codes.find(code) == std::string::npos) codes += (codes.empty() ? "" : " ") + code;
        }
        break;
    }
    LOG_INFO("loader", "console language %u; text languages on the disc: %s", language,
             codes.empty() ? "unknown" : codes.c_str());
}

xbox::KernelConfig kernel_config(const Options& options, const fs::path& assets, const fs::path& xbe) {
    static std::string image_file_name;
    image_file_name = "\\Device\\CdRom0\\" + xbe.filename().string();
    xbox::KernelConfig config{};
    config.image_file_name = image_file_name.c_str();
    config.trace_calls = options.trace_kernel;
    config.current_thread = current_kthread;
    config.current_irql = loader::current_irql_location;
    config.attach_kernel_thread = attach_kernel_thread;
    config.call_guest = call_guest;
    config.memory.physical_bytes = options.memory_mb * 1024 * 1024;
    config.widescreen = options.widescreen;
    config.language = options.language;
    config.cdrom_root = assets;
    config.data_root = options.data.value_or(platform::user_data_dir());
    if (options.launch_data_fd) config.launch_data_page = loader::os::read_launch_data(*options.launch_data_fd);
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

int loader_main(int argc, char** argv) {
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
    }
    const std::optional<Options>& options = parsed;
    g_args.assign(argv, argv + argc);
    g_diag_dir = options->diag_dir;
    if (options->session_start_ns)
        platform::set_session_start(std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(*options->session_start_ns))));
    platform::log_set_level(options->log_level);
    g_tell_player = options->press_script.empty() && !options->screenshot_dir;
    if (options->diag_dir) {
        fs::create_directories(*options->diag_dir);
        g_log_path = *options->diag_dir / "log.txt";
        if (!platform::log_set_file(g_log_path.string().c_str())) {
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
            return 1;
        }
        LOG_INFO("diag", "diagnostics in %s: a screenshot every %u s, F12 marks a problem",
                 options->diag_dir->string().c_str(), options->diag_seconds);
        // summary.txt when the process ends (not on a reboot: the session goes on).
        std::atexit([] { loader::write_diag_summary(*g_diag_dir); });
    } else {
        // Always a log, so that a player without a console can send it: one run per
        // file (a reboot continues it), in the user data folder.
        std::error_code ec;
        fs::create_directories(platform::user_data_dir(), ec);
        g_log_path = platform::user_data_dir() / "log.txt";
        if (!options->launch_data_fd) fs::remove(g_log_path, ec);
        constexpr long kPlayerLogLimit = 8l << 20;
        if (!platform::log_set_file(g_log_path.string().c_str(), kPlayerLogLimit))
            std::fprintf(stderr, "cannot write %s\n", g_log_path.string().c_str());
    }
    loader::set_fatal_exit_hook([](bool error) {
        loader::compare::flush();
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

    const auto assets = platform::resolve_asset_root(options->assets);
    if (!assets) {
        if (!start_launcher_for_assets(argc, argv))
            return startup_failed("no game files (copy your game dump into the assets folder)");
        return 1;
    }
    LOG_INFO("loader", "assets: %s", assets->string().c_str());

    static loader::XbeImage image;
    const fs::path xbe = options->xbe.value_or(*assets / (options->multiplayer ? "codmp_xboxf.xbe" : "default.xbe"));
    const loader::TitleInfo* title = loader::find_title(xbe.filename().string());
    if (!title) return startup_failed(xbe.filename().string() + " is not one of the game's executables");
    // Wider than 16:9 keeps the HUD at 16:9 through the campaign's HUD manager, which
    // multiplayer does not have (patches.cpp, "Wider than 16:9").
    float aspect = options->aspect;
    if (title->id == loader::TitleId::Multiplayer && aspect > 16.0f / 9.0f + 0.001f) {
        LOG_WARN("loader", "multiplayer: pictures wider than 16:9 are campaign only for now; 16:9");
        aspect = 16.0f / 9.0f;
    }
    if (!image.load(xbe)) return startup_failed("cannot load " + xbe.string());
    if (options->text_clear_out) loader::snapshot_code_sections(image);
    // A release has the data files next to the loader; builds use the source tree's.
    auto data_file = [](const char* name) {
        const fs::path next_to_loader = platform::executable_dir() / name;
        return fs::exists(next_to_loader) ? next_to_loader : fs::path(COD_BRO_SYMBOLS_DIR) / name;
    };
    if (options->host && !loader::clear_code_sections(image, {data_file("code_section_data.txt"),
                                                              data_file("level_eh_code.txt")}))
        return startup_failed("cannot prepare the host image");
    const bool other_build = image.code_fingerprint() != title->code_fingerprint;
    bool release_map_ready = false;
    if (other_build) {
        LOG_WARN("loader", "%s is another build (time stamp %08x, code fingerprint %016llx) than the supported one "
                 "(%016llx); its sections:", xbe.filename().string().c_str(), image.header().time_date,
                 static_cast<unsigned long long>(image.code_fingerprint()),
                 static_cast<unsigned long long>(title->code_fingerprint));
        image.log_sections();
        // The port's addresses are the supported build's; find where they are in this one.
        loader::ReleaseSignatures signatures;
        const fs::path signature_path = data_file(title->signature_file);
        if (signatures.load(signature_path)) {
            loader::ReleaseMap map;
            map.build(signatures, image_bytes(image));
            loader::set_release_map(map);
            release_map_ready = true;
        } else {
            LOG_ERROR("loader", "cannot read %s; the port's changes cannot be placed in this build",
                      signature_path.string().c_str());
        }
        // Without the map, another release's campaign ran on 0.1.8 (issue #9: the
        // French/Spanish/Italian disc), but its multiplayer crashed.
        if (!release_map_ready && title->id == loader::TitleId::Multiplayer && !options->any_release)
            return startup_failed(std::string(title->signature_file) + " is missing or damaged");
    }
    g_xbe = xbe;

    static loader::SymbolTable symbols;
    const fs::path symbol_path = options->symbols ? *options->symbols : data_file(title->symbol_file);
    if (symbols.load(symbol_path))
        LOG_INFO("loader", "%zu symbols from %s", symbols.size(), symbol_path.string().c_str());
    else
        LOG_WARN("loader", "no symbols at %s; backtraces will show addresses only", symbol_path.string().c_str());
    if (options->write_signatures) {
        if (other_build) return startup_failed("--write-signatures needs the supported build");
        const loader::ReleaseSignatures signatures =
            loader::make_signatures(image_bytes(image), symbols.in_range(0, 0xFFFFFFFF));
        if (!signatures.save(*options->write_signatures))
            return startup_failed("cannot write " + options->write_signatures->string());
        LOG_INFO("loader", "%zu function signatures and %zu data references written to %s",
                 signatures.functions.size(), signatures.data.size(), options->write_signatures->string().c_str());
        return 0;
    }
    if (release_map_ready) {
        loader::release_map().log_summary(&symbols);
        // Names and the SDK replacements follow the functions to where they are here.
        const size_t dropped = symbols.relocate([](uint32_t address) { return loader::release_map().find(address); });
        if (dropped) LOG_INFO("release", "%zu symbols are in changed code and were left out", dropped);
    }

    loader::install_crash_handler(image, symbols);
    loader::start_profiler(options->profile ? options->profile->c_str() : std::getenv("XB_PROFILE"),
                         static_cast<uint32_t>(options->frame_rate), image, symbols);
    loader::os::release_reserved_ranges();
    log_disc_languages(*assets, options->language);
    if (!xbox::kernel_init(kernel_config(*options, *assets, xbe))) return startup_failed("cannot set up the Xbox memory");
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::install_kernel_thunks(image, options->host);
    if (!options->bindings_error.empty())
        LOG_WARN("input", "keys setting: %s (ignored; the rest applies)", options->bindings_error.c_str());
    if (const std::string keys = input::format_bindings(options->bindings); !keys.empty())
        LOG_INFO("input", "keys: %s", keys.c_str());
    input::set_bindings(options->bindings);
    loader::install_title_patches(*title, {options->mouse_sensitivity, options->start_level, options->aim_assist,
                                           options->frame_rate, options->late_frames_wait,
                                           options->widescreen ? aspect : 0.0f, options->field_of_view,
                                           options->button_prompts, options->pc_patches.value_or(!options->compare),
                                           options->exec_text, options->forced_calls});
    if (!loader::prepare_fs_access(image)) return startup_failed("cannot prepare the game's code");
    loader::configure_title_services(*options, *assets, *title, aspect, [] {
        loader::run_fatal_exit_hook(false);
        tell_player(
            "The game could not create a graphics device.\n\nUpdate the graphics driver. If that does not help, "
            "choose another Graphics setting in the launcher.");
        std::_Exit(1);
    });
    if (!options->press_script.empty() && !input::set_script(options->press_script)) return 1;
    if (!options->replay_at_switch && !xbox::replay::start(options->replay)) return 1;
    if (!loader::install_sdk_replacements(image, symbols, *title)) {
        LOG_ERROR("loader", "SDK replacements need the symbol file (%s)", symbol_path.string().c_str());
        return startup_failed(std::string(title->symbol_file) + " is missing or damaged");
    }
    // After the title patches and SDK replacements: they must not hook the same code.
    loader::compare::Config compare;
    compare.enabled = options->compare;
    compare.all_c = options->own_main;
    compare.multiplayer = title->id == loader::TitleId::Multiplayer;
    compare.image_begin = image.base();
    compare.image_end = image.base() + image.size();
    compare.stack_limit = [](uint32_t esp) {
        // A script thread's own stack (scr_fiber.cpp), else the title thread's.
        uint32_t limit = 0, base = 0;
        if (game::scr_fiber_stack(&limit, &base) && esp >= limit && esp < base) return limit;
        const loader::ThreadEnv* env = loader::current_thread_env();
        return env ? env->stack_limit : 0u;
    };
    if (title->id != loader::TitleId::Multiplayer) {
        // Scr_RestoreContext (0x002f5660) returns to the scheduler through the context at 0x0092709c; a
        // suspend keeps its ESP in a title word (Scr_SuspendWaitTime/Frames/Till: 0x00922ed4/d8/dc) and in the
        // fiber (Scr_SaveThreadLocals).
        compare.switch_return_word = 0x009270c0;
        compare.switch_ranges = [](std::vector<std::pair<uint32_t, uint32_t>>& out) {
            out.push_back({0x00922ed4, 0x00922ee0});
            game::scr_fiber_switch_ranges(out);
        };
    }
    compare.describe = loader::describe_address;
    if (options->compare_all) compare.enough_matches = 0;
    if (options->compare) compare.sdk_calls = loader::recorded_sdk_calls();
    if (options->compare) compare.host_code.push_back(loader::rdtsc_trampolines());
    if (options->compare) compare.host_code.push_back(loader::scene_force_thunk());
    if (options->compare)
        compare.results = fs::path(COD_BRO_SYMBOLS_DIR) / (compare.multiplayer ? "functions_mp.csv" : "functions.csv");
    // Test processes run the DirectSound replacements without output (src/audio/test_mode.h).
    if (options->compare)
        audio::set_test_mode(loader::compare::in_test_flag(), [](audio::TestLock which) {
            loader::compare::host_lock_held(static_cast<uint32_t>(which));
        });
    compare.before_fork = audio::hold_for_fork;
    compare.after_fork = audio::release_after_fork;
    loader::compare::install(game::functions(), compare);
    std::atexit([] { loader::compare::flush(); });
    if (const size_t missed = loader::missed_title_addresses()) {
        LOG_WARN("loader", "%zu of the port's changes were not found in this release (see above)", missed);
        // The campaign started anyway before there was a map (0.1.8 on the issue #9 disc).
        if (title->id == loader::TitleId::Multiplayer && !options->any_release)
            return startup_failed(xbe.filename().string() +
                                  " is from another release of the game, and some of the places the port changes "
                                  "were not found in it (the log lists them; please attach the log and mention "
                                  "your disc's region and languages in a report)");
    } else if (release_map_ready) {
        LOG_INFO("loader", "all of the port's changes were found in this release");
    }

    // The title's initial thread. Like the Xbox's, it has no TLS block (TlsData is
    // null); XAPI only creates TLS for the threads it starts itself.
    loader::ThreadEnv* env = loader::create_thread_env(image.header().pe_stack_commit, 0);
    if (!env || !loader::activate_thread_env(env)) return startup_failed("cannot prepare the game's main thread");

    if (options->own_main) {
        // The XBE entry starts our own start-up (title_start.cpp): heap and CRT state in C, then Sys_Main.
        loader::install_jump(image.entry_point(), reinterpret_cast<const void*>(&game::title_start));
        LOG_INFO("loader", "own main: our own start-up and Sys_Main run as C");
    }
    if (options->text_trap_list && !loader::install_text_trap(*options->text_trap_list, *options->text_trap_out))
        return 1;
    if (options->text_clear_out && !loader::install_text_clear(*options->text_clear_out, {data_file("code_section_data.txt"), data_file("level_eh_code.txt")})) return 1;

    // In-process restarts (own engine host 5b) put the title's memory back to this point.
    ImageSnapshot image_snapshot;
    bool replay_pending = options->replay_at_switch && options->replay.mode != xbox::replay::Mode::Off;
    if (options->own_main) {
        image_snapshot.take(image);
        xbox::snapshot_memory();
    }
    if (options->text_clear_out) loader::text_clear_protect();

    LOG_INFO("loader", "starting title at %08x", image.entry_point());
    // Four zero dwords stand in for arguments in case the entry point reads any.
    const uint32_t args[4] = {};
    loader::run_on_xbox_stack(image.entry_point(), *env, args);
    while (options->own_main) {
        // The title switches levels by launching itself again (XLaunchNewImageA): here in this process, on the same
        // window and devices, once its main thread has ended.
        const std::vector<uint8_t> page = loader::wait_for_title_restart();
        LOG_INFO("loader", "restart: the title starts again in this process");
        if (options->diag_dir)
            if (std::FILE* file = std::fopen((*options->diag_dir / "launch_data.bin").string().c_str(), "wb")) {
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
        if (std::exchange(replay_pending, false) && !xbox::replay::start(options->replay)) return 1;
        LOG_INFO("loader", "starting title at %08x", image.entry_point());
        loader::run_on_xbox_stack(image.entry_point(), *env, args);
    }
    // The entry point only starts the title's main thread and returns. Park this
    // thread; the process lives on until the title quits. It must not exit: once the
    // initial thread is gone, Linux no longer resolves /proc/self/exe, which DXVK
    // and SDL read (and on Windows the stub's code it would return to is gone).
    LOG_INFO("loader", "title entry point returned");
    for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
}
