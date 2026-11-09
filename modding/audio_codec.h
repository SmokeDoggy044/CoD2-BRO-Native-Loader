#pragma once

// WAV I/O and the Xbox sound formats. Extraction writes ordinary 16-bit PCM WAV
// files; replacement converts those back to the format the game's sound bank used
// (8/16-bit PCM or Xbox ADPCM, tag 0x69) at the same byte length, so no offsets in
// the bank have to change.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace modding {

struct Pcm {
    uint16_t channels = 1;
    uint32_t rate = 44100;
    std::vector<int16_t> samples;  // interleaved frames
    size_t frames() const { return channels ? samples.size() / channels : 0; }
};

// Sound kinds in a wave bank's entries (game/code/snd.cpp: SndVoice_SetupBuffer).
enum SoundKind : uint8_t { kPcm8 = 1, kPcm16 = 2, kXboxAdpcm = 5 };

bool read_wav(const std::string& path, Pcm& pcm, std::string& error);
bool write_wav(const std::string& path, const Pcm& pcm, std::string& error);

// Decodes `bytes` of a sound's sample data into PCM. `frames` is the bank entry's
// sample count (used to trim the final partial ADPCM block).
bool decode_samples(const uint8_t* bytes, size_t size, uint8_t kind, uint16_t channels, uint32_t rate,
                    uint32_t frames, Pcm& pcm, std::string& error);

// Resamples/converts PCM and encodes it to exactly `target_bytes` bytes in `kind`.
// A short replacement is padded with silence, a long one is trimmed.
std::vector<uint8_t> encode_samples(const Pcm& input, uint8_t kind, uint16_t channels, uint32_t rate,
                                    size_t target_bytes, std::string& error);

// Byte length of `frames` frames in a format.
size_t frames_to_bytes(uint8_t kind, uint16_t channels, uint32_t frames);
uint32_t bytes_to_frames(uint8_t kind, uint16_t channels, size_t bytes);

}  // namespace modding
