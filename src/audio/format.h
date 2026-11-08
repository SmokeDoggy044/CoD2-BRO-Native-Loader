#pragma once

// Sample formats the Xbox APU plays: PCM (8/16-bit) and Xbox ADPCM, with decoding to
// 16-bit PCM for the mixer.

#include <cstdint>
#include <optional>

namespace audio {

// 16 bits, so Format has no padding: voices copy formats built on the stack, and a padding
// byte would carry whatever the stack held there (comparison tests read host memory).
enum class Codec : uint16_t { Pcm, XboxAdpcm };

struct Format {
    Codec codec = Codec::Pcm;
    uint16_t channels = 1;
    uint32_t rate = 44100;
    uint16_t bits = 16;        // PCM: 8 or 16
    uint16_t block_align = 2;  // bytes per PCM frame, or per ADPCM block (36 * channels)

    // Decoded frames per block: 1 for PCM, 64 for Xbox ADPCM.
    uint32_t frames_per_block() const { return codec == Codec::XboxAdpcm ? 64 : 1; }
    uint32_t bytes_to_frames(uint32_t bytes) const { return bytes / block_align * frames_per_block(); }
    uint32_t frames_to_bytes(uint32_t frames) const { return frames / frames_per_block() * block_align; }
    bool operator==(const Format&) const = default;
};
static_assert(sizeof(Format) == 12, "Format must have no padding");

// Reads an Xbox WAVEFORMATEX (or XBOXADPCMWAVEFORMAT) from memory. Null if the
// format is not one the APU plays.
std::optional<Format> parse_wave_format(const void* waveformatex);

// Decodes one block (block_align bytes) into frames_per_block() interleaved frames.
void decode_block(const Format& format, const uint8_t* block, int16_t* frames);

}  // namespace audio
