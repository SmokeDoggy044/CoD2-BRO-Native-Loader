#include "format.h"

#include <algorithm>
#include <cstring>

namespace audio {

namespace {

constexpr uint16_t kTagPcm = 0x0001;
constexpr uint16_t kTagXboxAdpcm = 0x0069;
constexpr uint16_t kTagExtensible = 0xFFFE;

constexpr int kAdpcmBytesPerChannel = 36;  // 4-byte header + 32 bytes of nibbles

constexpr int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t read32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct AdpcmChannel {
    int predictor;
    int index;

    int16_t step(int nibble) {
        const int step = kStepTable[index];
        int diff = step >> 3;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 4) diff += step;
        predictor += (nibble & 8) ? -diff : diff;
        predictor = std::clamp(predictor, -32768, 32767);
        index = std::clamp(index + kIndexTable[nibble], 0, 88);
        return static_cast<int16_t>(predictor);
    }
};

}  // namespace

std::optional<Format> parse_wave_format(const void* waveformatex) {
    const auto* p = static_cast<const uint8_t*>(waveformatex);
    if (!p) return std::nullopt;
    uint16_t tag = read16(p);
    Format format;
    format.channels = read16(p + 2);
    format.rate = read32(p + 4);
    format.block_align = read16(p + 12);
    format.bits = read16(p + 14);
    if (tag == kTagExtensible && read16(p + 16) >= 22) tag = read16(p + 24);  // SubFormat GUID's first word
    if (format.channels == 0 || format.channels > 6 || format.rate == 0) return std::nullopt;
    if (tag == kTagPcm) {
        if (format.bits != 8 && format.bits != 16) return std::nullopt;
        format.codec = Codec::Pcm;
        format.block_align = static_cast<uint16_t>(format.channels * format.bits / 8);
        return format;
    }
    if (tag == kTagXboxAdpcm) {
        format.codec = Codec::XboxAdpcm;
        format.bits = 4;
        format.block_align = static_cast<uint16_t>(kAdpcmBytesPerChannel * format.channels);
        return format;
    }
    return std::nullopt;
}

void decode_block(const Format& format, const uint8_t* block, int16_t* frames) {
    const int channels = format.channels;
    if (format.codec == Codec::Pcm) {
        if (format.bits == 16) {
            std::memcpy(frames, block, format.block_align);
        } else {
            for (int c = 0; c < channels; ++c) frames[c] = static_cast<int16_t>((block[c] - 128) << 8);
        }
        return;
    }
    // Xbox ADPCM is IMA ADPCM in the WAV layout: a 4-byte header per channel
    // (first sample, step index), then 4-byte groups (8 nibbles, low first)
    // alternating between channels. A block holds 65 samples per channel; the
    // APU plays 64 (block sizes and positions are counted that way), so the last
    // nibble only carries the predictor into the next block's header.
    AdpcmChannel state[6];
    for (int c = 0; c < channels; ++c) {
        state[c].predictor = static_cast<int16_t>(read16(block + 4 * c));
        state[c].index = std::clamp<int>(block[4 * c + 2], 0, 88);
        frames[c] = static_cast<int16_t>(state[c].predictor);
    }
    const uint8_t* data = block + 4 * channels;
    for (int group = 0; group < 8; ++group) {
        for (int c = 0; c < channels; ++c) {
            const uint8_t* bytes = data + (group * channels + c) * 4;
            for (int i = 0; i < 8; ++i) {
                const int sample = 1 + group * 8 + i;  // frame index; 64 is dropped
                const int nibble = (bytes[i / 2] >> ((i & 1) * 4)) & 0xF;
                const int16_t value = state[c].step(nibble);
                if (sample < 64) frames[sample * channels + c] = value;
            }
        }
    }
}

}  // namespace audio
