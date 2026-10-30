// DirectSound replacement (audio/): ADPCM decoding, the Voice playback rules, and the
// Xbox API end to end on an OpenAL loopback device whose output is measured.

#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "filesystem.h"
#include "format.h"
#include "objects.h"
#include "test_support.h"
#include "types.h"
#include "voice.h"
#include "xbox_dsound.h"

using namespace audio;
using xbox::to_xbox_ptr;
using xbox::XbPtr;

namespace {

// --- Helpers -------------------------------------------------------------------------------

// A minimal IMA ADPCM encoder producing Xbox-layout mono blocks (36 bytes, 64 samples
// played), to check the decoder against known input.
std::vector<uint8_t> encode_adpcm_mono(const std::vector<int16_t>& samples) {
    static const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
    static const int kStep[89] = {
        7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
        31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
        130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
        544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
        2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
        9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    std::vector<uint8_t> out;
    int predictor = 0, index = 0;
    for (size_t block = 0; block * 64 < samples.size(); ++block) {
        const int16_t first = samples[block * 64];
        predictor = first;
        out.push_back(uint8_t(first & 0xFF));
        out.push_back(uint8_t((first >> 8) & 0xFF));
        out.push_back(uint8_t(index));
        out.push_back(0);
        uint8_t byte = 0;
        for (int i = 1; i <= 64; ++i) {
            const size_t n = block * 64 + std::min(i, 63);  // the 65th sample repeats
            const int target = n < samples.size() ? samples[n] : 0;
            const int step = kStep[index];
            int diff = target - predictor, nibble = 0;
            if (diff < 0) nibble = 8, diff = -diff;
            int delta = step >> 3;
            if (diff >= step) nibble |= 4, diff -= step, delta += step;
            if (diff >= step >> 1) nibble |= 2, diff -= step >> 1, delta += step >> 1;
            if (diff >= step >> 2) nibble |= 1, delta += step >> 2;
            predictor = std::clamp(predictor + ((nibble & 8) ? -delta : delta), -32768, 32767);
            index = std::clamp(index + kIndex[nibble], 0, 88);
            if ((i - 1) % 2 == 0) byte = uint8_t(nibble);
            else out.push_back(uint8_t(byte | nibble << 4));
        }
    }
    return out;
}

Format pcm16(uint16_t channels, uint32_t rate = 44100) {
    return {Codec::Pcm, channels, rate, 16, static_cast<uint16_t>(2 * channels)};
}

std::vector<int16_t> render(Voice& voice, size_t frames, size_t* produced = nullptr) {
    std::vector<int16_t> out(frames * voice.format().channels, -1);
    const size_t n = voice.render(out.data(), frames);
    if (produced) *produced = n;
    out.resize(n * voice.format().channels);
    return out;
}

double rms(const std::vector<int16_t>& samples) {
    double sum = 0;
    for (int16_t s : samples) sum += double(s) * s;
    return samples.empty() ? 0 : std::sqrt(sum / double(samples.size()));
}

template <typename Fn>
Fn ds(const char* name) {
    for (const auto& f : dsound_functions())
        if (std::strcmp(f.name, name) == 0) return reinterpret_cast<Fn>(f.address);
    std::printf("missing %s\n", name);
    std::exit(2);
}

// Calls slot `slot` of a title-visible object's vtable.
template <typename Fn>
Fn method(XbPtr object, int slot) {
    const XbPtr vtable = *xbox::from_xbox_ptr<XbPtr>(object);
    return reinterpret_cast<Fn>(static_cast<uintptr_t>(xbox::from_xbox_ptr<XbPtr>(vtable)[slot]));
}

// Renders `frames` of the mix and returns its RMS level. OpenAL Soft fades gain
// changes and stops over a few milliseconds (no clicks), so a short stretch is mixed
// and dropped first.
double mix_level(size_t frames = 4800) {
    std::vector<int16_t> mix(480 * 2);
    output()->render(mix.data(), 480);
    mix.resize(frames * 2);
    output()->render(mix.data(), frames);
    return rms(mix);
}

void test_adpcm() {
    std::vector<int16_t> sine(64 * 8);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = int16_t(12000 * std::sin(i * 0.05));
    const std::vector<uint8_t> blocks = encode_adpcm_mono(sine);
    CHECK(blocks.size() == 36 * 8, "encoded %zu bytes", blocks.size());
    Format format{Codec::XboxAdpcm, 1, 44100, 4, 36};
    std::vector<int16_t> decoded(64);
    double error = 0;
    for (int b = 0; b < 8; ++b) {
        decode_block(format, blocks.data() + 36 * b, decoded.data());
        CHECK(decoded[0] == sine[64 * b], "block %d starts with its header sample", b);
        for (int i = 0; i < 64; ++i) error += std::abs(decoded[i] - sine[64 * b + i]);
    }
    CHECK(error / sine.size() < 300, "mean decode error %.1f", error / sine.size());

    // Parsing: XBOXADPCMWAVEFORMAT and PCM.
    const uint8_t adpcm_wfx[20] = {0x69, 0, 2, 0, 0x80, 0xBB, 0, 0, 0, 0, 0, 0, 72, 0, 4, 0, 2, 0, 64, 0};
    const auto parsed = parse_wave_format(adpcm_wfx);
    CHECK(parsed && parsed->codec == Codec::XboxAdpcm && parsed->channels == 2 && parsed->rate == 48000 &&
          parsed->block_align == 72 && parsed->bytes_to_frames(72 * 10) == 640);
    const uint8_t pcm8_wfx[18] = {1, 0, 1, 0, 0x22, 0x56, 0, 0, 0, 0, 0, 0, 1, 0, 8, 0, 0, 0};
    const auto pcm8 = parse_wave_format(pcm8_wfx);
    CHECK(pcm8 && pcm8->bits == 8 && pcm8->block_align == 1);
    const uint8_t bad[18] = {0x55, 0, 1, 0, 0x22, 0x56, 0, 0, 0, 0, 0, 0, 1, 0, 8, 0, 0, 0};
    CHECK(!parse_wave_format(bad), "MP3 is not an APU format");
}

void test_buffer_voice() {
    // A shared "sample pool": frame i holds the value i.
    std::vector<int16_t> pool(1000);
    for (size_t i = 0; i < pool.size(); ++i) pool[i] = int16_t(i);
    Voice voice(Voice::Kind::Buffer);
    voice.set_format(pcm16(1));
    voice.set_buffer_data(reinterpret_cast<const uint8_t*>(pool.data()), 2000);

    // Play region frames [100, 150): plays once, then stops.
    voice.set_play_region(200, 100);
    voice.play(false, true);
    size_t produced = 0;
    std::vector<int16_t> out = render(voice, 80, &produced);
    CHECK(produced == 50 && out.front() == 100 && out.back() == 149 && !voice.playing(), "one-shot: %zu", produced);

    // Looping over frames [110, 130) of that region after the first pass.
    voice.set_loop_region(20, 40);
    voice.play(true, true);
    out = render(voice, 80, &produced);
    CHECK(produced == 80 && voice.playing());
    CHECK(out[29] == 129 && out[30] == 110 && out[49] == 129 && out[50] == 110, "loop wraps: %d %d", out[29], out[30]);

    // Positions are relative to the play region; pausing renders silence.
    voice.set_position(2 * 15);
    CHECK(voice.position() == 30 && render(voice, 1)[0] == 115);
    voice.set_paused(true);
    CHECK(render(voice, 4) == std::vector<int16_t>(4, 0) && voice.playing());
    voice.set_paused(false);

    // The data is read when played: rewriting the pool changes what plays next.
    voice.set_position(0);
    pool[100] = 4242;
    CHECK(render(voice, 1)[0] == 4242);
    voice.stop();
    CHECK(render(voice, 4).empty() && !voice.playing());

    // PCM8 and ADPCM buffers.
    const uint8_t bytes8[4] = {128, 255, 0, 192};
    Voice voice8(Voice::Kind::Buffer);
    voice8.set_format({Codec::Pcm, 1, 22050, 8, 1});
    voice8.set_buffer_data(bytes8, 4);
    voice8.play(false, true);
    out = render(voice8, 4);
    CHECK(out.size() == 4 && out[0] == 0 && out[1] == 127 << 8 && out[2] == -128 << 8 && out[3] == 64 << 8);

    std::vector<int16_t> ramp(128);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = int16_t(i * 100);
    const std::vector<uint8_t> adpcm = encode_adpcm_mono(ramp);
    Voice voice4(Voice::Kind::Buffer);
    voice4.set_format({Codec::XboxAdpcm, 1, 44100, 4, 36});
    voice4.set_buffer_data(adpcm.data(), static_cast<uint32_t>(adpcm.size()));
    voice4.set_play_region(36, 36);  // the second block only
    voice4.play(false, true);
    out = render(voice4, 100, &produced);
    CHECK(produced == 64 && out[0] == 6400 && std::abs(out[63] - 12700) < 400, "adpcm region: %zu %d", produced, out[63]);
}

void test_stream_voice() {
    Voice voice(Voice::Kind::Stream);
    voice.set_format(pcm16(1));
    std::vector<int16_t> a(10, 1), b(6, 2);
    uint32_t status_a = 7, status_b = 7, done_a = 7, done_b = 7;
    std::vector<uint32_t> finished;
    voice.on_packet_done = [&](const Packet&, uint32_t status) { finished.push_back(status); };
    CHECK(voice.submit({reinterpret_cast<uint8_t*>(a.data()), 20, &status_a, &done_a, 0}, 2));
    CHECK(status_a == kPacketPending && done_a == 0 && voice.playing());
    CHECK(voice.submit({reinterpret_cast<uint8_t*>(b.data()), 12, &status_b, &done_b, 0}, 2));
    CHECK(!voice.submit({reinterpret_cast<uint8_t*>(b.data()), 12, nullptr, nullptr, 0}, 2), "max packets");

    std::vector<int16_t> out = render(voice, 12);
    CHECK(status_a == kPacketSuccess && done_a == 20 && status_b == kPacketPending, "first packet done");
    CHECK(out[9] == 1 && out[10] == 2 && finished.size() == 1);
    out = render(voice, 10);  // 4 more frames of b, then starved: silence
    CHECK(status_b == kPacketSuccess && done_b == 12 && out[3] == 2 && out[4] == 0 && voice.starved() && voice.playing());

    // Discontinuity: plays what is queued, then ends.
    std::vector<int16_t> c(4, 3);
    voice.submit({reinterpret_cast<uint8_t*>(c.data()), 8, nullptr, nullptr, 0}, 2);
    voice.discontinuity();
    size_t produced = 0;
    out = render(voice, 10, &produced);
    CHECK(produced == 4 && !voice.playing(), "ended after %zu", produced);

    // Flush: pending packets report flushed, with what was played.
    uint32_t status_d = 0, done_d = 9;
    std::vector<int16_t> d(8, 4);
    voice.submit({reinterpret_cast<uint8_t*>(d.data()), 16, &status_d, &done_d, 0}, 2);
    render(voice, 3);
    voice.flush();
    CHECK(status_d == kPacketFlushed && done_d == 6 && !voice.playing());

    // ADPCM blocks split across packets.
    std::vector<int16_t> ramp(128);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = int16_t(i * 50);
    const std::vector<uint8_t> adpcm = encode_adpcm_mono(ramp);
    Voice stream4(Voice::Kind::Stream);
    stream4.set_format({Codec::XboxAdpcm, 1, 44100, 4, 36});
    stream4.submit({adpcm.data(), 20, nullptr, nullptr, 0}, 4);
    stream4.submit({adpcm.data() + 20, 52, nullptr, nullptr, 0}, 4);
    out = render(stream4, 128);
    CHECK(out[0] == 0 && out[64] == 3200 && std::abs(out[127] - 6350) < 300 && !stream4.starved());
}

void test_api(const std::filesystem::path& root) {
    OutputConfig config;
    config.device = OutputConfig::Device::ManualLoopback;
    configure(config);
    XbPtr device = 0;
    CHECK(ds<uint32_t(XB_STDCALL*)(XbPtr, XbPtr*, XbPtr)>("DirectSoundCreate")(0, &device, 0) == 0 && device);
    CHECK(output() && output()->is_loopback());

    const auto create_buffer = ds<uint32_t(XB_STDCALL*)(const void*, XbPtr*)>("DirectSoundCreateBuffer");
    const auto set_data = ds<uint32_t(XB_STDCALL*)(XbPtr, const void*, uint32_t)>("IDirectSoundBuffer_SetBufferData");
    const auto play = ds<uint32_t(XB_STDCALL*)(XbPtr, uint32_t, uint32_t, uint32_t)>("IDirectSoundBuffer_Play");
    const auto stop = ds<uint32_t(XB_STDCALL*)(XbPtr)>("IDirectSoundBuffer_Stop");
    const auto get_status = ds<uint32_t(XB_STDCALL*)(XbPtr, uint32_t*)>("IDirectSoundBuffer_GetStatus");
    const auto set_volume = ds<uint32_t(XB_STDCALL*)(XbPtr, int32_t)>("IDirectSoundBuffer_SetVolume");
    const auto set_all = ds<uint32_t(XB_STDCALL*)(XbPtr, const void*, uint32_t)>("IDirectSoundBuffer_SetAllParameters");
    const auto set_curve = ds<uint32_t(XB_STDCALL*)(XbPtr, const float*, uint32_t, uint32_t)>("IDirectSoundBuffer_SetRolloffCurve");
    const auto commit = ds<uint32_t(XB_STDCALL*)(XbPtr)>("IDirectSound_CommitDeferredSettings");
    const auto synch = ds<uint32_t(XB_STDCALL*)(XbPtr)>("IDirectSound_SynchPlayback");
    const auto pcm_format = ds<void(XB_STDCALL*)(uint32_t, uint32_t, uint32_t, void*)>("XAudioCreatePcmFormat");

    // A looping 2D tone at full volume, then quieter, then stopped.
    uint8_t wfx[20] = {};
    pcm_format(1, 48000, 16, wfx);
    std::vector<int16_t> tone(4800);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = int16_t(8000 * std::sin(i * 0.1));
    const uint32_t desc_2d[6] = {24, 0, 0, to_xbox_ptr(wfx), 0, 0};
    XbPtr buffer = 0;
    CHECK(create_buffer(desc_2d, &buffer) == 0 && buffer);
    set_data(buffer, tone.data(), 9600);
    CHECK(mix_level() < 1, "silent before Play");
    play(buffer, 0, 0, 1 /* looping */);
    uint32_t status = 0;
    CHECK(get_status(buffer, &status) == 0 && status == 0x5, "playing+looping: %x", status);
    const double full = mix_level();
    CHECK(full > 2000, "playing: level %.0f", full);
    set_volume(buffer, -2000);  // -20 dB
    const double quiet = mix_level();
    CHECK(quiet > full * 0.05 && quiet < full * 0.2, "-20 dB: %.0f vs %.0f", quiet, full);
    stop(buffer);
    CHECK(get_status(buffer, &status) == 0 && status == 0 && mix_level() < 1, "stopped");

    // SYNCHPLAYBACK waits for IDirectSound_SynchPlayback, but already counts as playing.
    set_volume(buffer, 0);
    play(buffer, 0, 0, 1 | 4);
    CHECK(get_status(buffer, &status) == 0 && (status & 1) && mix_level() < 1, "waits for synch");
    synch(device);
    CHECK(mix_level() > 2000, "started by SynchPlayback");
    stop(buffer);

    // 3D with the game's linear rolloff curve: loud inside min distance, silent at max;
    // deferred settings apply on commit.
    const uint32_t desc_3d[6] = {24, 0x10 /* CTRL3D */, 0, to_xbox_ptr(wfx), 0, 0};
    XbPtr voice3d = 0;
    create_buffer(desc_3d, &voice3d);
    set_data(voice3d, tone.data(), 9600);
    const float curve[11] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f};
    set_curve(voice3d, curve, 11, 0);
    float params[19] = {};
    auto place = [&](float z) {
        std::memset(params, 0, sizeof(params));
        reinterpret_cast<uint32_t&>(params[0]) = 0x4C;
        params[3] = z;           // position z
        params[13] = 1.0f;       // min distance
        params[14] = 100.0f;     // max distance
        params[16] = 1.0f;       // distance factor
        params[17] = 1.0f;       // rolloff factor
        set_all(voice3d, params, 1 /* deferred */);
    };
    play(voice3d, 0, 0, 1);
    place(0.5f);
    commit(device);
    const double near = mix_level();
    place(50.5f);  // half way: curve value 0.5
    const double still_near = mix_level();
    commit(device);
    const double half = mix_level();
    place(150.0f);
    commit(device);
    const double far = mix_level();
    CHECK(near > 2000 && std::abs(still_near - near) < near * 0.1, "deferred until commit: %.0f %.0f", near, still_near);
    CHECK(half > near * 0.4 && half < near * 0.6, "half way: %.0f vs %.0f", half, near);
    CHECK(far < 1, "beyond max distance: %.0f", far);
    stop(voice3d);

    // A stream fed through its vtable (XMediaObject::Process): packets complete as
    // they are mixed, and an event in the packet is signaled.
    const auto create_stream = ds<uint32_t(XB_STDCALL*)(const void*, XbPtr*)>("DirectSoundCreateStream");
    const uint32_t stream_desc[6] = {0, 2, to_xbox_ptr(wfx), 0, 0, 0};
    XbPtr stream = 0;
    CHECK(create_stream(stream_desc, &stream) == 0 && stream);
    const auto process = method<uint32_t(XB_STDCALL*)(XbPtr, const void*, const void*)>(stream, 4);
    const auto stream_status = method<uint32_t(XB_STDCALL*)(XbPtr, uint32_t*)>(stream, 3);
    xbox::Handle event = 0;
    test::find_export<uint32_t(XB_STDCALL*)(xbox::Handle*, void*, uint32_t, uint32_t)>("NtCreateEvent")(&event, nullptr, 0, 0);
    uint32_t packet_status = 0, packet_done = 0;
    const uint32_t packet[6] = {to_xbox_ptr(tone.data()), 4800, to_xbox_ptr(&packet_done), to_xbox_ptr(&packet_status), event, 0};
    CHECK(process(stream, packet, nullptr) == 0 && packet_status == kPacketPending);
    CHECK(stream_status(stream, &status) == 0 && (status & 0x10000) && (status & 1), "playing, ready: %x", status);
    CHECK(mix_level(2400) > 2000, "stream plays");
    mix_level(2400);
    CHECK(packet_status == kPacketSuccess && packet_done == 4800, "packet complete: %x %u", packet_status, packet_done);
    int64_t zero = 0;
    CHECK(test::find_export<uint32_t(XB_STDCALL*)(xbox::Handle, uint32_t, int64_t*)>("NtWaitForSingleObject")(event, 0, &zero) == 0,
          "completion event set");
    CHECK(stream_status(stream, &status) == 0 && (status & 0x40000), "starved: %x", status);

    // File media object: reads a file into packets, seeks, reports its length.
    std::ofstream(root / "data" / "E" / "music.raw", std::ios::binary) << std::string(10000, 'x');
    CHECK(xbox::create_symbolic_link("\\??\\E:", "\\Device\\Harddisk0\\Partition1") == 0);
    std::string name = "\\??\\E:\\music.raw";
    xbox::XbAnsiString ansi{uint16_t(name.size()), uint16_t(name.size()), to_xbox_ptr(name.data())};
    xbox::XbObjectAttributes attributes{0, to_xbox_ptr(&ansi), 0x40};
    xbox::XbIoStatusBlock io{};
    xbox::Handle file = 0;
    CHECK(test::find_export<uint32_t(XB_STDCALL*)(xbox::Handle*, uint32_t, void*, void*, void*, uint32_t, uint32_t, uint32_t,
                                                  uint32_t)>("NtCreateFile")(&file, 0x80100000, &attributes, &io, nullptr,
                                                                             0, 1, 1, 0) == 0);
    XbPtr media = 0;
    CHECK(ds<uint32_t(XB_STDCALL*)(xbox::Handle, uint32_t, XbPtr*)>("XFileCreateMediaObjectAsync")(file, 2, &media) == 0);
    std::vector<char> chunk(4096);
    uint32_t media_status = 0;
    const uint32_t read_packet[6] = {to_xbox_ptr(chunk.data()), 4096, 0, to_xbox_ptr(&media_status), 0, 0};
    const auto media_process = method<uint32_t(XB_STDCALL*)(XbPtr, const void*, const void*)>(media, 4);
    CHECK(media_process(media, nullptr, read_packet) == 0 && media_status == 0 && chunk[4095] == 'x');
    uint32_t length = 0, where = 0;
    CHECK(method<uint32_t(XB_STDCALL*)(XbPtr, uint32_t*)>(media, 8)(media, &length) == 0 && length == 10000);
    CHECK(method<uint32_t(XB_STDCALL*)(XbPtr, int32_t, uint32_t, uint32_t*)>(media, 7)(media, -100, 2, &where) == 0 &&
          where == 9900);
    uint32_t got = 0;
    const uint32_t tail_packet[6] = {to_xbox_ptr(chunk.data()), 4096, to_xbox_ptr(&got), to_xbox_ptr(&media_status), 0, 0};
    CHECK(media_process(media, nullptr, tail_packet) == 0 && got == 100, "short read at the end: %u", got);
    CHECK(method<uint32_t(XB_STDCALL*)(XbPtr)>(media, 1)(media) == 0, "released");
    // Windows cannot delete an open file (the cleanup in main would fail).
    test::find_export<uint32_t(XB_STDCALL*)(xbox::Handle)>("NtClose")(file);
}

}  // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("cod_bro_audio_test_" + std::to_string(std::time(nullptr)));
    std::filesystem::create_directories(root / "disc");
    test::init_kernel(root / "disc", root / "data");
    test_adpcm();
    test_buffer_voice();
    test_stream_voice();
    test_api(root);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return test::finish("audio_test");
}
