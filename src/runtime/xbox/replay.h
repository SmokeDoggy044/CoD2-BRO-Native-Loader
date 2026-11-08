#pragma once

// Record/replay (developer mode, own engine host step 0.3): everything the title's main thread reads from outside
// that changes from run to run - the clock (RDTSC, QueryPerformanceCounter), the pads, device changes, mouse
// motion and the menu pointer - is written to a file in the order it is read. A replay feeds the same values back
// in the same order, so a deterministic build plays the same game again; once per presented frame both write or
// check a hash of the game state (level locals, clients, every entity), and the replay reports the first frame that
// differs. Used to check that a new host or build runs the game exactly like the old one.
//
// Other title threads (file, sound) read the real values. When the replay's stream no longer matches what the
// title asks for (the run diverged), the replay logs it and continues live.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace xbox::replay {

enum class Mode { Off, Record, Replay };

struct Config {
    Mode mode = Mode::Off;
    std::filesystem::path file;
    // Both modes: at this presented frame, write the level locals and every entity (raw bytes) to dump_dir
    // (frame_<n>_level.bin, _clients.bin, _entities.bin), for tools/replay_diff.py. 0 = never.
    uint32_t dump_frame = 0;
    std::filesystem::path dump_dir;
    // Replay: exit when the file ends (exit code 0 = every frame matched, 3 = some differed).
    bool exit_at_end = true;
    // Replay: also write this run's own state records ('E' + 'F', the recording's format) to this file, so runs in
    // different builds can be compared with each other (tools/replay_compare.py). Empty = no.
    std::filesystem::path hashes_out;
    // Replay: another run's state records (a recording or a hashes_out file); the first frame whose state differs
    // from it is logged (differing entities) and written to reference_dump_dir like dump_frame. Empty = no.
    std::filesystem::path reference;
    std::filesystem::path reference_dump_dir;
    // Record: written at the start of the file, so a replay runs with the same settings ('O': the settings.ini
    // text the run used, 'A': its command line, one argument per line).
    std::string settings_text;
    std::string command_line;
};

// A recording's settings.ini text and command line (Config::settings_text / command_line); false if the file is no
// recording of this format. The loader uses the settings in place of the player's settings.ini when replaying.
bool read_recorded_settings(const std::filesystem::path& file, std::string& settings_text, std::string& command_line);

// False (logged) if the file cannot be opened or is not a recording.
bool start(const Config& config);

// The title's main thread: only its reads are recorded or replayed.
void bind_current_thread();

bool active();

// Value streams. Each returns what the title gets: the real value (recorded) or the recorded one.
int64_t clock(int64_t real);
uint32_t value(char tag, uint32_t real);
// A block of bytes (pad state, pointer state): recorded from `data`, or overwritten with the recorded block.
void bytes(char tag, void* data, size_t size);

// The calling thread is the title's main thread of an active recording or replay (its reads go to the file).
bool on_stream();
// The title's main thread (bound), whether or not a recording or replay still runs on it.
bool main_thread();

// For values that need more than "hand back the recorded one" (a wait that has to wait for the recorded outcome):
// replay only - the next recorded value of this tag in this frame, false if there is none (the caller then acts
// live). Record with value() as usual.
bool recorded(char tag, uint32_t& v);
bool replaying();

// Ends a recording (flushed, closed) or a replay (its report; exits if exit_at_end). Before a reboot.
void stop();

// Once per presented frame (main thread): the state hash.
void frame();

}  // namespace xbox::replay
