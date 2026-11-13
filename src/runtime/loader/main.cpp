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
#include "title_calls.h"
#include "filesystem.h"
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
#include "title_session.h"
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

// The running XBE (a reboot into the other title keeps the symbols only for the same one).
fs::path g_xbe;

// The mapped image, for release_map.h.
loader::ImageBytes image_bytes(const loader::XbeImage& image) {
    loader::ImageBytes bytes;
    bytes.base = image.base();
    bytes.bytes = {reinterpret_cast<const uint8_t*>(uintptr_t{image.base()}), image.size()};
    for (const loader::XbeSectionHeader* section : image.sections())
        bytes.sections.push_back({image.section_name(*section), section->virtual_addr, section->virtual_size});
    return bytes;
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
    if (loader::diag_dir()) {
        // To repeat this boot: --launch-data 3 3<launch_data.bin
        if (std::FILE* file = std::fopen((*loader::diag_dir() / "launch_data.bin").string().c_str(), "wb")) {
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
    std::vector<std::string> args = {loader::session_args()[0]};
    for (size_t i = 1; i < loader::session_args().size(); ++i) {
        const std::string name = option_name(loader::session_args()[i]);
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
        args.push_back(loader::session_args()[i]);
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
    config.current_thread = loader::current_kthread;
    config.current_irql = loader::current_irql_location;
    config.attach_kernel_thread = loader::attach_kernel_thread;
    config.call_guest = loader::call_guest;
    config.memory.physical_bytes = options.memory_mb * 1024 * 1024;
    config.widescreen = options.widescreen;
    config.language = options.language;
    config.cdrom_root = assets;
    config.data_root = options.data.value_or(platform::user_data_dir());
    if (options.launch_data_fd) config.launch_data_page = loader::os::read_launch_data(*options.launch_data_fd);
    config.reboot = reboot_title;
    return config;
}


}  // namespace

int loader_main(int argc, char** argv) {
    std::optional<Options> parsed;
    if (const int failed = loader::load_options(argc, argv, parsed)) return failed;
    const std::optional<Options>& options = parsed;
    if (!loader::start_session(*options, argc, argv)) return 1;

    const auto assets = loader::find_assets(*options, argc, argv);
    if (!assets) return 1;

    static loader::XbeImage image;
    const fs::path xbe = options->xbe.value_or(*assets / (options->multiplayer ? "codmp_xboxf.xbe" : "default.xbe"));
    const loader::TitleInfo* title = loader::find_title(xbe.filename().string());
    if (!title) return loader::startup_failed(xbe.filename().string() + " is not one of the game's executables");
    // Wider than 16:9 keeps the HUD at 16:9 through the campaign's HUD manager, which
    // multiplayer does not have (patches.cpp, "Wider than 16:9").
    float aspect = options->aspect;
    if (title->id == loader::TitleId::Multiplayer && aspect > 16.0f / 9.0f + 0.001f) {
        LOG_WARN("loader", "multiplayer: pictures wider than 16:9 are campaign only for now; 16:9");
        aspect = 16.0f / 9.0f;
    }
    if (!image.load(xbe)) return loader::startup_failed("cannot load " + xbe.string());
    if (options->text_clear_out) loader::snapshot_code_sections(image);
    if (options->host && !loader::clear_code_sections(image, {loader::data_file("code_section_data.txt"),
                                                              loader::data_file("level_eh_code.txt")}))
        return loader::startup_failed("cannot prepare the host image");
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
        const fs::path signature_path = loader::data_file(title->signature_file);
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
            return loader::startup_failed(std::string(title->signature_file) + " is missing or damaged");
    }
    g_xbe = xbe;

    static loader::SymbolTable symbols;
    const fs::path symbol_path = options->symbols ? *options->symbols : loader::data_file(title->symbol_file);
    if (symbols.load(symbol_path))
        LOG_INFO("loader", "%zu symbols from %s", symbols.size(), symbol_path.string().c_str());
    else
        LOG_WARN("loader", "no symbols at %s; backtraces will show addresses only", symbol_path.string().c_str());
    if (options->write_signatures) {
        if (other_build) return loader::startup_failed("--write-signatures needs the supported build");
        const loader::ReleaseSignatures signatures =
            loader::make_signatures(image_bytes(image), symbols.in_range(0, 0xFFFFFFFF));
        if (!signatures.save(*options->write_signatures))
            return loader::startup_failed("cannot write " + options->write_signatures->string());
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
    if (!xbox::kernel_init(kernel_config(*options, *assets, xbe))) return loader::startup_failed("cannot set up the Xbox memory");
    xbox::charge_image_pages(loader::initial_image_pages(image));
    loader::set_kernel_links({[](bool format) { return xbox::mount_utility_drive(format); },
                              [](const char* name) -> void* {
                                  for (const xbox::KernelImpl& function : xbox::kernel_impls())
                                      if (std::strcmp(function.name, name) == 0) return function.address;
                                  return nullptr;
                              }});
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
    if (!loader::prepare_fs_access(image)) return loader::startup_failed("cannot prepare the game's code");
    loader::configure_title_services(*options, *assets, *title, aspect, [] {
        loader::run_fatal_exit_hook(false);
        loader::tell_player(
            "The game could not create a graphics device.\n\nUpdate the graphics driver. If that does not help, "
            "choose another Graphics setting in the launcher.");
        std::_Exit(1);
    });
    // The kernel's objects and files: under --own-main our C makes its own handles (title_objects/title_files);
    // kernel code that meets one of them forwards it to ours.
    if (options->own_main) {
        xbox::ForeignObjects foreign;
        foreign.owns = [](xbox::Handle handle) { return game::objects::is_ours(handle); };
        foreign.set_event = [](xbox::Handle handle, int32_t state) { game::objects::set_event(handle, state != 0); };
        foreign.queue_apc = [](xbox::XbPtr routine, uint32_t a1, uint32_t a2, uint32_t a3) {
            game::objects::queue_apc(routine, a1, a2, a3);
        };
        foreign.host_file = [](xbox::Handle handle) { return game::files::host_file(handle); };
        xbox::set_foreign_objects(foreign);
    }
    audio::set_title_file_lookup([](uint32_t handle) { return xbox::host_file(handle); });
    xbox::set_irql_calls({&xbox::raise_irql_to, &xbox::lower_irql_to});
    xbox::set_event_signal([](uint32_t handle) { xbox::set_event_handle(handle); });
    if (!options->press_script.empty() && !input::set_script(options->press_script)) return 1;
    if (!options->replay_at_switch && !xbox::replay::start(options->replay)) return 1;
    if (!loader::install_sdk_replacements(image, symbols, *title)) {
        LOG_ERROR("loader", "SDK replacements need the symbol file (%s)", symbol_path.string().c_str());
        return loader::startup_failed(std::string(title->symbol_file) + " is missing or damaged");
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
            return loader::startup_failed(xbe.filename().string() +
                                  " is from another release of the game, and some of the places the port changes "
                                  "were not found in it (the log lists them; please attach the log and mention "
                                  "your disc's region and languages in a report)");
    } else if (release_map_ready) {
        LOG_INFO("loader", "all of the port's changes were found in this release");
    }

    // The title's initial thread. Like the Xbox's, it has no TLS block (TlsData is
    // null); XAPI only creates TLS for the threads it starts itself.
    loader::ThreadEnv* env = loader::create_thread_env(image.header().pe_stack_commit, 0);
    if (!env || !loader::activate_thread_env(env)) return loader::startup_failed("cannot prepare the game's main thread");

    if (options->own_main) {
        // The XBE entry starts our own start-up (title_start.cpp): heap and CRT state in C, then Sys_Main.
        loader::install_jump(image.entry_point(), reinterpret_cast<const void*>(&game::title_start));
        LOG_INFO("loader", "own main: our own start-up and Sys_Main run as C");
    }
    if (options->text_trap_list && !loader::install_text_trap(*options->text_trap_list, *options->text_trap_out))
        return 1;
    if (options->text_clear_out && !loader::install_text_clear(*options->text_clear_out, {loader::data_file("code_section_data.txt"), loader::data_file("level_eh_code.txt")})) return 1;

    return loader::run_title(image, *env, *options);
}
