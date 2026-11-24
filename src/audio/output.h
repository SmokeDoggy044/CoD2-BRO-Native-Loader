#pragma once

// Audio output through OpenAL Soft. Each voice is an OpenAL source whose samples come
// from Voice::render through a callback buffer (AL_SOFT_callback_buffer), so the Xbox
// playback rules stay in Voice and OpenAL does resampling, 3D panning and HRTF.
//
// Without a usable output device (or when asked), a loopback device is used instead:
// it renders into nothing at real-time pace, so voices still advance and stream packets
// still complete. Tests drive the loopback device themselves (render()).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "enhanced_features.h"
#include "sound_device.h"
#include "voice.h"

typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

namespace audio {

struct OutputConfig {
    enum class Device { Default, Loopback, ManualLoopback };
    // Auto: what the system's output has, headphones (HRTF) when the game asks for them.
    // Otherwise this layout, whatever the game asks for.
    enum class Speakers { Auto, Stereo, Headphones, Quad, Surround51, Surround71 };
    Device device = Device::Default;  // ManualLoopback: nothing renders unless render() is called
    uint32_t rate = 48000;            // loopback rate
    double speed = 1;                 // Loopback: rendered this many times faster than real time (--replay-speed)
    std::string capture_path;         // Loopback: everything rendered also into this WAV file (--audio-capture)
    std::string device_name;          // Default: this output device (empty: the system's)
    Speakers speakers = Speakers::Auto;
    // Our improvements (resampling here; the sound device's own); false: as on the Xbox. Each
    // of them also on its own (features).
    bool enhanced = false;
    EnhancedFeatures features;
};

class Output {
public:
    enum class Placement { Direct, Relative, World };
    struct SourceParams {
        float gain = 1.0f;
        float pitch = 1.0f;
        Placement placement = Placement::Direct;  // Direct: channels straight to speakers
        float position[3] = {0, 0, 0};            // OpenAL coordinates (right-handed)
        bool reverb_send = false;                 // to set_environment's reverb
        float direct_gain = 1.0f, direct_gain_hf = 1.0f;  // a low-pass on the direct sound (EFX); 1, 1: none
        float direct_gain_lf = 1.0f;                      // below 1: a band-pass (low shelf too)
        float send_gain = 1.0f;                           // the reverb send's level (1 to 4)
        float echo_send = 0.0f;                           // the echo's send level (set_echo), 0: none
    };
    class Source;

    static std::unique_ptr<Output> open(const OutputConfig& config);
    ~Output();

    // Sources are created for a voice and follow its format (call reformat() after
    // changing it, with the voice stopped).
    Source* add_source(Voice& voice);
    // The source's OpenAL objects deleted (stopped and its callback buffer detached first) and the source gone. The
    // voice it played must outlive this call; nothing may use `source` afterwards.
    void remove_source(Source* source);
    void reformat(Source* source);
    void update(Source* source, const SourceParams& params);
    // Starts, pauses or stops the OpenAL source to match the voice's state.
    void sync(Source* source);
    void stop(Source* source);

    void set_listener(const float position[3], const float velocity[3], const float at[3], const float up[3]);
    // Between begin_batch() and end_batch() changes apply together.
    void begin_batch();
    void end_batch();
    // The game's headphone choice (Speakers::Auto only; otherwise the layout decides).
    bool set_hrtf(bool enabled);
    // Our improvements on or off while playing (OutputConfig::enhanced and features at first):
    // the best resampler on every source, the reverb (off silences it).
    void set_enhanced(bool resampler, bool reverb);
    // The reverb sources with reverb_send feed (EFX; ignored while not enhanced). Logs changes
    // that can be heard (at most once a second).
    void set_environment(const Environment& environment);
    // The slap-back echo (EFX echo on a second effect slot; enhanced only).
    void set_echo(const Echo& echo);
    // Stops all output until resumed (the voices do not advance).
    void set_paused(bool paused);
    // A fast replay's silent output (loopback) mixes at this speed: 1 when the player takes over.
    void set_speed(double speed) { speed_ = speed; }
    bool paused() const { return paused_; }

    bool is_loopback() const { return loopback_; }
    // Loopback only: mixes `frames` stereo 16-bit frames.
    void render(int16_t* out, size_t frames);
    // Loopback: stereo frames rendered so far (the capture's position; tools/audio_compare.py aligns by it).
    uint64_t rendered_frames() const { return rendered_.load(std::memory_order_relaxed); }
    // The level the source's voice handed OpenAL since the last call (before gains): RMS of full scale (0..1), -1
    // when it rendered nothing. For the capture's voice table.
    static float take_level(Source* source);

private:
    Output() = default;
    void drive_loopback();
    void mix(int16_t* out, size_t frames);  // loopback: renders, counts, captures
    void finish_capture();
    void write_capture_header();

    bool reset_device();

    ALCdevice* device_ = nullptr;
    OutputConfig::Speakers speakers_ = OutputConfig::Speakers::Auto;
    int hrtf_ = -1;          // the game's headphone choice (Speakers::Auto), -1: none yet
    bool resampler_on_ = false, reverb_on_ = false;
    int best_resampler_ = -1;     // enhanced sources' (AL_SOFT_source_resampler), -1: none found
    int default_resampler_ = -1;  // the others'
    unsigned reverb_slot_ = 0, reverb_effect_ = 0;  // EFX, 0: none
    unsigned echo_slot_ = 0, echo_effect_ = 0;      // EFX echo (3.5), 0: none
    Echo echo_;
    Environment logged_;          // the last environment logged
    int64_t logged_at_ = 0;       // steady clock, milliseconds
    bool eax_reverb_ = false;  // AL_EFFECT_EAXREVERB, else the standard reverb
    ALCcontext* context_ = nullptr;
    bool loopback_ = false;
    uint32_t rate_ = 48000;
    std::atomic<double> speed_{1};
    std::vector<std::unique_ptr<Source>> sources_;
    std::thread driver_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> paused_{false};
    std::atomic<uint64_t> rendered_{0};
    std::FILE* capture_ = nullptr;
    uint64_t capture_bytes_ = 0;
};

}  // namespace audio
