// cod_bro: our own engine (own engine host, step H3). Our C runs the whole game: start-up, Sys_Main, every engine
// function; the original XBE only provides its data, the levels' code and the two keep lists (host_image.h). No
// kernel: the title's kernel table holds fatal stubs, and our C's platform layer (TitleHost) gets memory, threads,
// files and time from here. Level switches restart the title in this process.
//
// What cod_bro_loader does and this does not: the Xbox kernel set-up (file system, settings, tick thread), other
// releases' address maps, the compare harness, --text-trap/--text-clear, and exec reboots.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bindings.h"
#include "compare.h"
#include "crash.h"
#include "functions.h"
#include "hooks.h"
#include "host_image.h"
#include "kernel.h"
#include "kernel_memory.h"
#include "kernel_stubs.h"
#include "log.h"
#include "options.h"
#include "os.h"
#include "patches.h"
#include "profiler.h"
#include "replay.h"
#include "scr_fiber.h"
#include "script.h"
#include "symbols.h"
#include "test_mode.h"
#include "thread_env.h"
#include "title_session.h"
#include "title_setup.h"
#include "title_start.h"
#include "xbe.h"
#include "xbox_input.h"

namespace fs = std::filesystem;

int host_main(int argc, char** argv) {
    std::optional<loader::Options> parsed;
    if (const int failed = loader::load_options(argc, argv, parsed)) return failed;
    loader::Options& options = *parsed;
    // What this program is; the loader's switches for it are accepted and change nothing.
    options.own_main = options.host = true;
    if (options.compare || options.multiplayer || options.text_trap_list || options.text_clear_out ||
        options.write_signatures || options.xbe || options.launch_data_fd) {
        std::fprintf(stderr, "cod_bro runs the campaign on our own engine; --compare, --multiplayer, --text-trap, "
                             "--text-clear, --write-signatures, --xbe and reboots are cod_bro_loader's\n");
        return 2;
    }
    if (!loader::start_session(options, argc, argv)) return 1;
    const auto assets = loader::find_assets(options, argc, argv);
    if (!assets) return 1;

    // The game's data, its levels' code and the two keep lists come from the player's dump.
    static loader::XbeImage image;
    const fs::path xbe = *assets / "default.xbe";
    const loader::TitleInfo* title = loader::find_title(xbe.filename().string());
    if (!title || !image.load(xbe)) return loader::startup_failed("cannot load " + xbe.string());
    if (image.code_fingerprint() != title->code_fingerprint) {
        LOG_ERROR("host", "%s is another release (code fingerprint %016llx) than the one our engine is built on "
                  "(%016llx); cod_bro_loader runs it", xbe.string().c_str(),
                  static_cast<unsigned long long>(image.code_fingerprint()),
                  static_cast<unsigned long long>(title->code_fingerprint));
        return loader::startup_failed("default.xbe is from another release of the game; start cod_bro_loader");
    }
    if (!loader::clear_code_sections(image, {loader::data_file("code_section_data.txt"),
                                             loader::data_file("level_eh_code.txt")}))
        return loader::startup_failed("cannot prepare the game's image");

    static loader::SymbolTable symbols;
    const fs::path symbol_path = options.symbols ? *options.symbols : loader::data_file(title->symbol_file);
    if (!symbols.load(symbol_path))
        LOG_WARN("host", "no symbols at %s; backtraces will show addresses only", symbol_path.string().c_str());
    loader::install_crash_handler(image, symbols);
    loader::start_profiler(options.profile ? options.profile->c_str() : std::getenv("XB_PROFILE"),
                           static_cast<uint32_t>(options.frame_rate ? options.frame_rate : 60), image, symbols);
    loader::os::release_reserved_ranges();

    // The title's memory: the same allocator and layout as the loader's, so replays compare across both.
    xbox::MemoryConfig memory;
    memory.physical_bytes = options.memory_mb * 1024 * 1024;
    if (!xbox::memory_init(memory) || !xbox::place_launch_data_page({}))
        return loader::startup_failed("cannot set up the game's memory");
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::install_fatal_kernel_thunks(image);  // a kernel call is a bug

    if (!options.bindings_error.empty())
        LOG_WARN("input", "keys setting: %s (ignored; the rest applies)", options.bindings_error.c_str());
    if (const std::string keys = input::format_bindings(options.bindings); !keys.empty())
        LOG_INFO("input", "keys: %s", keys.c_str());
    input::set_bindings(options.bindings);
    const float aspect = options.aspect;
    loader::install_title_patches(*title, {options.mouse_sensitivity, options.start_level, options.aim_assist,
                                           options.frame_rate, options.late_frames_wait,
                                           options.widescreen ? aspect : 0.0f, options.field_of_view,
                                           options.button_prompts, options.pc_patches.value_or(true),
                                           options.exec_text, options.forced_calls, options.mounted_gun_fix, options.glow});
    if (!loader::prepare_fs_access(image)) return loader::startup_failed("cannot prepare the game's code");
    loader::configure_title_services(options, *assets, *title, aspect, [] {
        loader::run_fatal_exit_hook(false);
        loader::tell_player(
            "The game could not create a graphics device.\n\nUpdate the graphics driver. If that does not help, "
            "choose another Graphics setting in the launcher.");
        std::_Exit(1);
    });
    if (!options.press_script.empty() && !input::set_script(options.press_script)) return 1;
    if (!options.replay_at_switch && !xbox::replay::start(options.replay)) return 1;
    if (!loader::install_sdk_replacements(image, symbols, *title))
        return loader::startup_failed(std::string(title->symbol_file) + " is missing or damaged");

    // Every function with C, hooked at its original address (level code and data pointers reach them there).
    loader::compare::Config hooks;
    hooks.all_c = true;
    hooks.image_begin = image.base();
    hooks.image_end = image.base() + image.size();
    hooks.stack_limit = [](uint32_t esp) {
        uint32_t limit = 0, base = 0;
        if (game::scr_fiber_stack(&limit, &base) && esp >= limit && esp < base) return limit;
        const loader::ThreadEnv* env = loader::current_thread_env();
        return env ? env->stack_limit : 0u;
    };
    hooks.switch_return_word = 0x009270c0;
    hooks.switch_ranges = [](std::vector<std::pair<uint32_t, uint32_t>>& out) {
        out.push_back({0x00922ed4, 0x00922ee0});
        game::scr_fiber_switch_ranges(out);
    };
    hooks.describe = loader::describe_address;
    hooks.before_fork = audio::hold_for_fork;
    hooks.after_fork = audio::release_after_fork;
    loader::compare::install(game::functions(), hooks);

    // The title's initial thread (no TLS block, as on the Xbox); the XBE entry is our start-up (title_start.cpp).
    loader::ThreadEnv* env = loader::create_thread_env(image.header().pe_stack_commit, 0);
    if (!env || !loader::activate_thread_env(env)) return loader::startup_failed("cannot prepare the game's main thread");
    loader::install_jump(image.entry_point(), reinterpret_cast<const void*>(&game::title_start));
    LOG_INFO("host", "our own engine: start-up and Sys_Main run as C");
    return loader::run_title(image, *env, options);
}
