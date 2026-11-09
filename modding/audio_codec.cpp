// See audio_codec.h. Xbox ADPCM is IMA ADPCM in the WAV layout: a 4-byte header per
// channel (predictor + step index), then 4-byte groups alternating between
// channels, 8 nibbles each. A block holds 65 samples per channel and the game
// plays 64 of them (audio/format.cpp has the original decoder).

#include "audio_codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace modding {
namespace {

constexpr int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct AdpcmState { int predictor = 0, index = 0; };

int16_t step(AdpcmState& state, uint8_t nibble) {
    const int delta = kStepTable[std::clamp(state.index, 0, 88)];
    int difference = delta >> 3;
    if (nibble & 1) difference += delta >> 2;
    if (nibble & 2) difference += delta >> 1;
    if (nibble & 4) difference += delta;
    state.predictor += (nibble & 8) ? -difference : difference;
    state.predictor = std::clamp(state.predictor, -32768, 32767);
    state.index = std::clamp(state.index + kIndexTable[nibble & 15], 0, 88);
    return static_cast<int16_t>(state.predictor);
}

uint16_t u16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
int16_t i16(const uint8_t* p) { return static_cast<int16_t>(u16(p)); }
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) {
    for (uint32_t i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

uint8_t best_nibble(const AdpcmState& state, int16_t wanted) {
    uint8_t best = 0;
    int best_error = 1 << 30;
    for (uint8_t nibble = 0; nibble < 16; ++nibble) {
        AdpcmState trial = state;
        const int predicted = step(trial, nibble);
        const int error = std::abs(predicted - wanted);
        if (error < best_error) {
            best_error = error;
            best = nibble;
        }
    }
    return best;
}

int16_t at(const Pcm& pcm, size_t frame, uint16_t channel) {
    if (frame >= pcm.frames()) return 0;
    return pcm.samples[frame * pcm.channels + channel];
}

// Linear resampling + channel conversion, into `frames` frames of `channels` audio.
Pcm fit(const Pcm& input, uint16_t channels, uint32_t rate, size_t frames) {
    Pcm out;
    out.channels = channels;
    out.rate = rate;
    out.samples.assign(frames * channels, 0);
    if (!frames || !input.frames() || !input.channels) return out;
    for (size_t frame = 0; frame < frames; ++frame) {
        const double source = rate && input.rate ? double(frame) * input.rate / rate : double(frame);
        const size_t a = static_cast<size_t>(source);
        const double fraction = source - a;
        for (uint16_t c = 0; c < channels; ++c) {
            const uint16_t from = std::min<uint16_t>(c, static_cast<uint16_t>(input.channels - 1));
            const int32_t left = at(input, a, from);
            const int32_t right = at(input, std::min(a + 1, input.frames() - 1), from);
            out.samples[frame * channels + c] = static_cast<int16_t>(std::clamp<int32_t>(
                static_cast<int32_t>(left + (right - left) * fraction), -32768, 32767));
        }
    }
    return out;
}

}  // namespace

size_t frames_to_bytes(uint8_t kind, uint16_t channels, uint32_t frames) {
    if (!channels) channels = 1;
    switch (kind) {
        case kPcm8: return size_t(frames) * channels;
        case kPcm16: return size_t(frames) * channels * 2;
        case kXboxAdpcm: return size_t(frames / 64) * 36 * channels;
        default: return 0;
    }
}

uint32_t bytes_to_frames(uint8_t kind, uint16_t channels, size_t bytes) {
    if (!channels) channels = 1;
    switch (kind) {
        case kPcm8: return static_cast<uint32_t>(bytes / channels);
        case kPcm16: return static_cast<uint32_t>(bytes / (size_t(channels) * 2));
        case kXboxAdpcm: return static_cast<uint32_t>(bytes / (size_t(36) * channels) * 64);
        default: return 0;
    }
}

bool decode_samples(const uint8_t* bytes, size_t size, uint8_t kind, uint16_t channels, uint32_t rate,
                    uint32_t frames, Pcm& pcm, std::string& error) {
    if (!bytes || !size || !channels || channels > 6 || !rate) { error = "bad sound format"; return false; }
    const uint32_t available = bytes_to_frames(kind, channels, size);
    if (!available) { error = "empty sound"; return false; }
    if (!frames || frames > available) frames = available;
    pcm = {};
    pcm.channels = channels;
    pcm.rate = rate;
    pcm.samples.assign(size_t(frames) * channels, 0);
    if (kind == kPcm8) {
        for (uint32_t frame = 0; frame < frames; ++frame)
            for (uint16_t c = 0; c < channels; ++c)
                pcm.samples[size_t(frame) * channels + c] = static_cast<int16_t>((int(bytes[size_t(frame) * channels + c]) - 128) << 8);
        return true;
    }
    if (kind == kPcm16) {
        for (uint32_t frame = 0; frame < frames; ++frame)
            for (uint16_t c = 0; c < channels; ++c)
                pcm.samples[size_t(frame) * channels + c] = i16(bytes + (size_t(frame) * channels + c) * 2);
        return true;
    }
    if (kind != kXboxAdpcm) { error = "unknown sound kind"; return false; }
    const size_t block_size = size_t(36) * channels;
    const size_t blocks = size / block_size;
    std::vector<int16_t> decoded(size_t(64) * channels);
    for (size_t block = 0; block < blocks; ++block) {
        const uint8_t* data = bytes + block * block_size;
        AdpcmState state[6];
        for (uint16_t c = 0; c < channels; ++c) {
            state[c].predictor = i16(data + 4 * c);
            state[c].index = std::clamp<int>(data[4 * c + 2], 0, 88);
            decoded[c] = static_cast<int16_t>(state[c].predictor);
        }
        const uint8_t* nibbles = data + 4 * channels;
        for (uint32_t group = 0; group < 8; ++group) {
            for (uint16_t c = 0; c < channels; ++c) {
                const uint8_t* group_bytes = nibbles + (size_t(group) * channels + c) * 4;
                for (uint32_t i = 0; i < 8; ++i) {
                    const uint32_t sample = 1 + group * 8 + i;
                    const uint8_t nibble = (group_bytes[i / 2] >> ((i & 1) * 4)) & 15;
                    const int16_t value = step(state[c], nibble);
                    if (sample < 64) decoded[size_t(sample) * channels + c] = value;
                }
            }
        }
        for (uint32_t frame = 0; frame < 64; ++frame) {
            const size_t target = size_t(block) * 64 + frame;
            if (target >= frames) break;
            for (uint16_t c = 0; c < channels; ++c)
                pcm.samples[target * channels + c] = decoded[size_t(frame) * channels + c];
        }
    }
    return true;
}

std::vector<uint8_t> encode_samples(const Pcm& input, uint8_t kind, uint16_t channels, uint32_t rate,
                                    size_t target_bytes, std::string& error) {
    std::vector<uint8_t> out;
    if (!channels || channels > 6 || !rate || !target_bytes) { error = "bad replacement format"; return out; }
    const uint32_t frames = bytes_to_frames(kind, channels, target_bytes);
    if (!frames) { error = "replacement is too short"; return out; }
    const Pcm audio = fit(input, channels, rate, frames);
    out.assign(target_bytes, 0);
    if (kind == kPcm8) {
        for (uint32_t frame = 0; frame < frames; ++frame)
            for (uint16_t c = 0; c < channels; ++c) {
                const size_t at_byte = size_t(frame) * channels + c;
                if (at_byte < out.size()) out[at_byte] = static_cast<uint8_t>((audio.samples[at_byte] >> 8) + 128);
            }
        return out;
    }
    if (kind == kPcm16) {
        for (uint32_t frame = 0; frame < frames; ++frame)
            for (uint16_t c = 0; c < channels; ++c) {
                const size_t at_byte = (size_t(frame) * channels + c) * 2;
                if (at_byte + 2 <= out.size()) put16(out.data() + at_byte, static_cast<uint16_t>(audio.samples[at_byte / 2]));
            }
        return out;
    }
    if (kind != kXboxAdpcm) { error = "unknown sound kind"; return {}; }
    const size_t block_size = size_t(36) * channels;
    const size_t blocks = target_bytes / block_size;
    for (size_t block = 0; block < blocks; ++block) {
        uint8_t* data = out.data() + block * block_size;
        AdpcmState state[6];
        for (uint16_t c = 0; c < channels; ++c) {
            state[c].predictor = at(audio, block * 64, c);
            put16(data + 4 * c, static_cast<uint16_t>(state[c].predictor));
            data[4 * c + 2] = static_cast<uint8_t>(state[c].index);
            data[4 * c + 3] = 0;
        }
        uint8_t* nibbles = data + 4 * channels;
        for (uint16_t c = 0; c < channels; ++c) {
            AdpcmState channel_state = state[c];
            for (uint32_t group = 0; group < 8; ++group) {
                uint8_t* group_bytes = nibbles + (size_t(group) * channels + c) * 4;
                std::memset(group_bytes, 0, 4);
                for (uint32_t i = 0; i < 8; ++i) {
                    const size_t frame = block * 64 + 1 + group * 8 + i;
                    const uint8_t nibble = best_nibble(channel_state, at(audio, frame, c));
                    (void)step(channel_state, nibble);
                    group_bytes[i / 2] |= static_cast<uint8_t>(nibble << ((i & 1) * 4));
                }
            }
        }
    }
    return out;
}

bool read_wav(const std::string& path, Pcm& pcm, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open WAV"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
        error = "not a RIFF WAVE file";
        return false;
    }
    uint16_t tag = 1, channels = 1, bits = 16, block_align = 2;
    uint32_t rate = 44100;
    const uint8_t* data = nullptr;
    size_t data_size = 0;
    bool have_fmt = false;
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const uint32_t size = u32(bytes.data() + at + 4);
        const uint8_t* chunk = bytes.data() + at + 8;
        if (size > bytes.size() - (at + 8)) break;
        if (!std::memcmp(bytes.data() + at, "fmt ", 4) && size >= 16) {
            tag = u16(chunk);
            channels = u16(chunk + 2);
            rate = u32(chunk + 4);
            block_align = u16(chunk + 12);
            bits = u16(chunk + 14);
            if (tag == 0xFFFE && size >= 40) tag = u16(chunk + 24);  // SubFormat GUID's first word
            have_fmt = true;
        } else if (!std::memcmp(bytes.data() + at, "data", 4)) {
            data = chunk;
            data_size = size;
            break;
        }
        at += 8 + size + (size & 1);
    }
    if (!have_fmt || !data || !channels || channels > 6 || !rate) { error = "incomplete WAV header"; return false; }
    if (tag == 0x69) {  // WAVE_FORMAT_XBOX_ADPCM
        return decode_samples(data, data_size, kXboxAdpcm, channels, rate, 0, pcm, error);
    }
    if (tag != 1 && tag != 3) { error = "unsupported WAV codec"; return false; }
    const uint32_t bytes_per_sample = std::max<uint32_t>(1, (bits + 7) / 8);
    if (block_align && block_align != channels * bytes_per_sample) {
        // Some writers pad the block align; the sample layout stays interleaved.
        if (block_align < channels * bytes_per_sample) { error = "bad WAV block align"; return false; }
    }
    const size_t frames = data_size / (size_t(channels) * bytes_per_sample);
    pcm = {};
    pcm.channels = channels;
    pcm.rate = rate;
    pcm.samples.assign(frames * channels, 0);
    for (size_t frame = 0; frame < frames; ++frame) {
        for (uint16_t c = 0; c < channels; ++c) {
            const uint8_t* sample = data + (frame * channels + c) * bytes_per_sample;
            int32_t value = 0;
            if (tag == 3 && bits == 32) {
                float f = 0;
                std::memcpy(&f, sample, 4);
                value = static_cast<int32_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
            } else if (bits == 8) {
                value = (int32_t(sample[0]) - 128) << 8;
            } else if (bits == 16) {
                value = i16(sample);
            } else if (bits == 24) {
                value = int32_t(sample[0]) | int32_t(sample[1]) << 8 | int32_t(sample[2]) << 16;
                if (value & 0x800000) value |= ~0xFFFFFF;
                value >>= 8;
            } else if (bits == 32) {
                uint32_t raw = u32(sample);
                value = static_cast<int32_t>(raw) >> 16;
            } else {
                error = "unsupported WAV bit depth";
                return false;
            }
            pcm.samples[frame * channels + c] = static_cast<int16_t>(std::clamp<int32_t>(value, -32768, 32767));
        }
    }
    return true;
}

bool write_wav(const std::string& path, const Pcm& pcm, std::string& error) {
    if (!pcm.channels || !pcm.rate || pcm.samples.empty()) { error = "empty PCM"; return false; }
    const size_t data_size = pcm.samples.size() * 2;
    std::vector<uint8_t> bytes(44 + data_size);
    std::memcpy(bytes.data(), "RIFF", 4);
    put32(bytes.data() + 4, static_cast<uint32_t>(36 + data_size));
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
    put32(bytes.data() + 16, 16);
    put16(bytes.data() + 20, 1);  // PCM
    put16(bytes.data() + 22, pcm.channels);
    put32(bytes.data() + 24, pcm.rate);
    put32(bytes.data() + 28, pcm.rate * pcm.channels * 2);
    put16(bytes.data() + 32, static_cast<uint16_t>(pcm.channels * 2));
    put16(bytes.data() + 34, 16);
    std::memcpy(bytes.data() + 36, "data", 4);
    put32(bytes.data() + 40, static_cast<uint32_t>(data_size));
    for (size_t i = 0; i < pcm.samples.size(); ++i) put16(bytes.data() + 44 + i * 2, static_cast<uint16_t>(pcm.samples[i]));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot create WAV"; return false; }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) { error = "cannot write WAV"; return false; }
    return true;
}

}  // namespace modding
