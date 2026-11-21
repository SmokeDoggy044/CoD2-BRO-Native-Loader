// cod_bro: our own engine (own engine host, step H3). Our C runs the whole game: start-up, Sys_Main, every engine
// function; the original XBE only provides its data, the levels' code and the two keep lists (host_image.h). No
// kernel: the title's kernel table holds fatal stubs, and our C's platform layer (TitleHost) gets memory, threads,
// files and time from here. Level switches restart the title in this process.
//
// Other releases run here too (the French/Spanish/Italian disc): our C uses this release's addresses, which the
// release map checks against theirs and logs where they differ.
//
// What cod_bro_loader does and this does not: the Xbox kernel set-up (file system, settings, tick thread), moving
// addresses to another release's places, the compare harness, --text-trap/--text-clear, and exec reboots.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sharp_text.h"
#include "ui_text.h"
#include "game_texts.h"
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
#include "program.h"
#include "options.h"
#include "os.h"
#include "patches.h"
#include "pc_menu.h"
#include "profiler.h"
#include "release_map.h"
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
    // Another release (the French/Spanish/Italian disc's campaign differs in one level's code and in .rdata's
    // contents): our engine runs it too - its C takes only data and level code from the dump. The release map logs
    // whether every place our C depends on is the same there (ReleaseMap::keeps_places), so a player's log names
    // what to fix if one is not.
    if (image.code_fingerprint() != title->code_fingerprint) {
        LOG_WARN("host", "%s is another release (time stamp %08x, code fingerprint %016llx; ours %016llx)",
                 xbe.string().c_str(), image.header().time_date,
                 static_cast<unsigned long long>(image.code_fingerprint()),
                 static_cast<unsigned long long>(title->code_fingerprint));
        image.log_sections();
        loader::ReleaseSignatures signatures;
        if (!signatures.load(loader::data_file(title->signature_file))) {
            LOG_WARN("host", "cannot read %s: this release's places are not checked",
                     loader::data_file(title->signature_file).string().c_str());
        } else {
            loader::ImageBytes bytes;
            bytes.base = image.base();
            bytes.bytes = {reinterpret_cast<const uint8_t*>(uintptr_t{image.base()}), image.size()};
            for (const loader::XbeSectionHeader* section : image.sections())
                bytes.sections.push_back({image.section_name(*section), section->virtual_addr, section->virtual_size});
            loader::ReleaseMap map;
            map.build(signatures, bytes);
            std::vector<uint32_t> replaced;
            for (const game::Function& function : game::functions())
                if (function.campaign) replaced.push_back(function.campaign);
            std::vector<uint32_t> changed;
            std::string why;
            const bool same = map.keeps_places(replaced, why, changed);
            std::string list;
            for (size_t i = 0; i < changed.size() && i < 24; ++i) {
                char text[12];
                std::snprintf(text, sizeof(text), "%s%08x", i ? " " : "", changed[i]);
                list += text;
            }
            if (same)
                LOG_INFO("host", "every place our engine depends on is the same in this release (%zu functions of "
                         "level code differ and run as this release's: %s%s)", changed.size(), list.c_str(),
                         changed.size() > 24 ? " ..." : "");
            else
                LOG_WARN("host", "this release differs where our engine depends on it (%s; %zu functions differ: "
                         "%s%s); if the game misbehaves, please report it with this log", why.c_str(), changed.size(),
                         list.c_str(), changed.size() > 24 ? " ..." : "");
        }
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

    // The title's memory: the same allocator and layout as the loader's, so replays compare across both. More than
    // the Xbox's 64 MB by default: the game's physical pool and sound memory grow with it (Mem_ExtraBytes).
    constexpr uint32_t kOwnEngineMemoryMb = 128;
    xbox::MemoryConfig memory;
    memory.physical_bytes = (options.memory_mb ? options.memory_mb : kOwnEngineMemoryMb) * 1024 * 1024;
    // The title's virtual memory (its CRT heap segments and thread stacks) needs 1 MB in every mission; the Xbox's
    // 1 GB range only took address space from our renderer and drivers. Both ranges go to the Xbox's places when
    // free, else where the host has room (our C reaches contiguous memory through xbox::contiguous_base).
    constexpr uint32_t kOwnEngineVirtualBytes = 64u << 20;
    memory.virtual_end = memory.virtual_base + kOwnEngineVirtualBytes;
    memory.host_placed = true;
    if (!xbox::memory_init(memory) || !xbox::place_launch_data_page({}))
        return loader::startup_failed("cannot set up the game's memory");
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::install_fatal_kernel_thunks(image);  // a kernel call is a bug

    if (!options.bindings_error.empty())
        LOG_WARN("input", "keys setting: %s (ignored; the rest applies)", options.bindings_error.c_str());
    if (const std::string keys = input::format_bindings(options.bindings); !keys.empty())
        LOG_INFO("input", "keys: %s", keys.c_str());
    input::set_bindings(options.bindings);
    if (!options.pad_bindings_error.empty())
        LOG_WARN("input", "pad setting: %s (ignored; the rest applies)", options.pad_bindings_error.c_str());
    input::set_pad_bindings(options.pad_bindings);
    input::set_trigger_threshold(options.trigger_threshold);
    input::set_pad_family_override(options.pad_glyphs);
    loader::set_notice_allowed(options.show_notice ||
                               (options.press_script.empty() && options.menu_walk.empty() && !options.screenshot_dir &&
                                options.replay.mode == xbox::replay::Mode::Off && !options.compare),
                               options.show_notice);
    loader::set_live_pad_glyphs(!options.pad_glyphs                                      ? "auto"
                                : *options.pad_glyphs == input::PadFamily::PlayStation ? "playstation"
                                : *options.pad_glyphs == input::PadFamily::Nintendo    ? "nintendo"
                                : *options.pad_glyphs == input::PadFamily::Steam       ? "steam"
                                                                                       : "xbox");
    const float aspect = options.aspect;
    loader::install_title_patches(*title, {options.mouse_sensitivity, options.start_level, options.aim_assist,
                                           options.frame_rate, options.late_frames_wait,
                                           options.widescreen ? aspect : 0.0f, options.field_of_view,
                                           options.button_prompts, options.pc_patches.value_or(true),
                                           options.exec_text, options.forced_calls, options.mounted_gun_fix, options.glow,
                                           options.sprint,
                                           loader::sprint_seconds(options.sprint_duration), options.letterbox,
                                           options.more_bodies, options.skip_intro,
                                           options.hit_markers, options.god_mode, options.infinite_ammo,
                                           options.free_camera});
    // Before any text or font (ui_text.h, sharp_text.h).
    if (const char* code = options.text_language == "polish"      ? "pl"
                           : options.text_language == "russian"   ? "ru"
                           : options.text_language == "ukrainian"  ? "uk"
                           : options.text_language == "portuguese" ? "pt_BR"
                                                                   : nullptr) {
        platform::set_text_language(code);
        text::set_sharp_text_script(platform::text_script());
    }
    loader::start_game_texts(options.game_text_template);
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
