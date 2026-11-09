#pragma once

// Finding the game's sounds. The campaign keeps them in wave banks (.wbk, loose on
// the disc or packed inside .cod): a 0x1000-byte header, a table of 0x28-byte
// entries, then the sample data (PCM 8/16 or Xbox ADPCM). See game/code/snd.cpp
// (SndBank_Relocate / SndVoice_SetupBuffer) for the fields this reads.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace modding {

struct SoundRow {
    std::string hash;     // FNV-1a of the original sample bytes (render::hash_bytes)
    std::string file;     // relative WAV path under mods/audio
    std::string name;     // sound alias from the bank's name table
    std::string source;   // .wbk or .cod file, relative to the assets folder
    uint64_t data_offset = 0;  // absolute byte offset in `source`
    uint32_t data_size = 0;    // bytes of sample data
    uint64_t pack_offset = 0;  // .cod entry offset (0 for a loose file)
    uint64_t bank_offset = 0;  // bank header offset in the file
    uint32_t frames = 0;
    uint32_t rate = 44100;
    uint16_t channels = 1;
    uint8_t kind = 0;  // SoundKind, or 0 for an already-complete WAV file
};

struct AudioScan {
    std::vector<SoundRow> rows;
    size_t banks = 0;
    size_t sources = 0;
    std::string error;
};

// Scans every .wbk / .wav in `assets` and every .cod pack. Progress is called with
// (sources done, sources total) so the Modding window can draw a bar.
AudioScan scan_audio(const std::filesystem::path& assets,
                     const std::function<void(size_t, size_t)>& progress = {});

bool write_audio_manifest(const std::filesystem::path& path, const std::vector<SoundRow>& rows, std::string& error);
bool read_audio_manifest(const std::filesystem::path& path, std::vector<SoundRow>& rows, std::string& error);

}  // namespace modding
