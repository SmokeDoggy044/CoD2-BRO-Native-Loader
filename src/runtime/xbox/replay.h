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
#include <vector>

namespace xbox::replay {

enum class Mode { Off, Record, Replay };

struct Config {
    Mode mode = Mode::Off;
    std::filesystem::path file;
    // Both modes: at this presented frame, write the level locals and every entity (raw bytes) to dump_dir
    // (frame_<n>_level.bin, _clients.bin, _entities.bin), for tools/replay_diff.py. 0 = never.
    uint32_t dump_frame = 0;
    uint32_t dump_last = 0;  // --replay-dump N-M: every frame from dump_frame to this one
    uint32_t dump_step = 1;  // --replay-dump N-M:S: every S-th frame of that range
    std::filesystem::path dump_dir;
    // Both modes: at these presented frames (sorted), save the picture as shot_dir/shot_<n>.bmp - fixed capture
    // points: the same frames of one recording, compared between builds or back ends. Empty = none.
    std::vector<uint32_t> shot_frames;
    std::filesystem::path shot_dir;
    // Replay: exit when the file ends (exit code 0 = every frame matched, 3 = some differed, 4 = played, state not
    // compared: a recording from before version 6).
    bool exit_at_end = true;
    // Replay: also write this run's own state records ('E' + 'F', the recording's format) to this file, so runs in
    // different builds can be compared with each other (tools/replay_compare.py). Empty = no.
    std::filesystem::path hashes_out;
    // Replay: also write a new recording (this format) of the run: every value the replay hands the title, with this
    // build's state records and memory layout, and the old file's settings and command line - an old recording
    // brought to the current format (--rerecord). Empty = no.
    std::filesystem::path rerecord;
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
// `site` (format 7): what the value belongs to when one tag has several readers - a voice, a wait, a call site; a
// number the same in every run (never a host address). Recorded with the value; a replay that reaches the tag with
// another site in that frame hands over the one with this site if the frame has it, and counts a fault either way
// (reads in another order). 0 = one reader, no check.
int64_t clock(int64_t real);
uint32_t value(char tag, uint32_t real, uint32_t site = 0);
// A block of bytes (pad state, pointer state): recorded from `data`, or overwritten with the recorded block.
void bytes(char tag, void* data, size_t size);

// The calling thread is the title's main thread of an active recording or replay (its reads go to the file).
bool on_stream();
// The title's main thread (bound), whether or not a recording or replay still runs on it.
bool main_thread();

// For values that need more than "hand back the recorded one" (a wait that has to wait for the recorded outcome):
// replay only - the next recorded value of this tag in this frame, false if there is none (the caller then acts
// live). Record with value() as usual.
bool recorded(char tag, uint32_t& v, uint32_t site = 0);
// As recorded(), but not copied into a re-recording: the caller writes what it really used with rerecord_value()
// (a held sound packet the run never produced must not reach the new file).
bool take_recorded(char tag, uint32_t& v, uint32_t site = 0);
void rerecord_value(char tag, uint32_t v, uint32_t site = 0);
bool replaying();

// Replay faults: the run could not hand the title what the recording says it saw. The frame hashes may still match
// (the run went on live and happened to stay in step), so a replay with faults does not pass (exit code 5).
enum class Fault {
    MissedPacket,  // a recorded sound packet completion the run never produced
    LiveWait,      // a recorded wait outcome the run's objects never reached (it waited live)
    Order,         // a keyed value read with another site than recorded (or none recorded for it)
    Thread,        // a title thread started that the recording did not start there (or the other way round)
};
void fault(Fault kind);
// A title thread started by the main thread (its routine): recorded, and checked in a replay (Fault::Thread).
void thread_started(uint32_t routine);
// The game saved a checkpoint (its name): logged with its frame in every run; recorded; a replay compares name, order
// and frame with the recording's (scanned before it starts) and lists them all at its end.
void checkpoint(const char* name);
// The input format of the file being replayed (a recording: the current one; a re-recording: its original's, kept
// in its V record). 5 added native key events.
uint32_t version();

// Ends a recording (flushed, closed) or a replay (its report; exits if exit_at_end). Before a reboot.
void stop();

// Once per presented frame (main thread): the state hash.
void frame();

// The current frame number (for log lines).
uint32_t frame_number();

// After frame(): where to save this frame's picture (Config::shot_frames), empty if not wanted.
std::string screenshot_path();

// Frames from the current one to the next Config::shot_frames entry (0 = this one), UINT32_MAX when none is left.
uint32_t frames_to_next_shot();

}  // namespace xbox::replay
