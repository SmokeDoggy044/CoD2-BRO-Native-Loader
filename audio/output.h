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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    std::string device_name;          // Default: this output device (empty: the system's)
    Speakers speakers = Speakers::Auto;
    // Our improvements (resampling here; the sound device's own); false: as on the Xbox.
    bool enhanced = false;
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
    };
    class Source;

    static std::unique_ptr<Output> open(const OutputConfig& config);
    ~Output();

    // Sources are created for a voice and follow its format (call reformat() after
    // changing it, with the voice stopped).
    Source* add_source(Voice& voice);
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
    // Our improvements on or off while playing (OutputConfig::enhanced at first): the resampler
    // of every source; off silences the reverb.
    void set_enhanced(bool enhanced);
    // The reverb sources with reverb_send feed (EFX; ignored while not enhanced). Logs changes
    // that can be heard (at most once a second).
    void set_environment(const Environment& environment);
    // Stops all output until resumed (the voices do not advance).
    void set_paused(bool paused);

    bool is_loopback() const { return loopback_; }
    // Loopback only: mixes `frames` stereo 16-bit frames.
    void render(int16_t* out, size_t frames);

private:
    Output() = default;
    void drive_loopback();

    bool reset_device();

    ALCdevice* device_ = nullptr;
    OutputConfig::Speakers speakers_ = OutputConfig::Speakers::Auto;
    int hrtf_ = -1;          // the game's headphone choice (Speakers::Auto), -1: none yet
    bool enhanced_ = false;
    int best_resampler_ = -1;     // enhanced sources' (AL_SOFT_source_resampler), -1: none found
    int default_resampler_ = -1;  // the others'
    unsigned reverb_slot_ = 0, reverb_effect_ = 0;  // EFX, 0: none
    Environment logged_;          // the last environment logged
    int64_t logged_at_ = 0;       // steady clock, milliseconds
    bool eax_reverb_ = false;  // AL_EFFECT_EAXREVERB, else the standard reverb
    ALCcontext* context_ = nullptr;
    bool loopback_ = false;
    uint32_t rate_ = 48000;
    std::vector<std::unique_ptr<Source>> sources_;
    std::thread driver_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace audio
