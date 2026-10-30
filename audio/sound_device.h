#pragma once

// Our own sound interface: what the game's sound system needs from the sound hardware, in
// its own terms (voices, placement in the world, the listener), with nothing of DirectSound.
// The decompiled sound code (game/code/snd.cpp) calls it; the DirectSound replacement
// (xbox_dsound.cpp) translates the original code's calls onto it. One back end so far:
// OpenAL Soft (sound_engine.h).
//
// This header is only declarations: the game library (game/code) includes it without
// linking the audio library; the loader hands it the device (game::set_sound_device).
//
// Conventions:
// - Positions and directions are the game's (left-handed, +z forward, like DirectSound).
// - Gains are linear (1 = full level). Rates are in Hz; 0 = the format's own rate.
// - Spatial and listener changes made with Apply::Deferred take effect together at commit().
// - Voices started or resumed with Start::WithSynch begin together at start_synched().
// - VoiceId is 0 for none. In the loader it is also the address of a DirectSound object, so
//   original code given a voice by decompiled code (and the other way round) works on it.

#include <cstdint>

#include "format.h"
#include "packet.h"

namespace audio {

using VoiceId = uint32_t;

struct VoiceDesc {
    bool stream = false;
    bool spatial = false;               // positioned in the world (mono voices)
    bool mute_at_max_distance = false;  // silent beyond the maximum distance
    uint8_t reserved = 0;               // no padding: the comparison harness logs it by content
    uint32_t max_packets = 0;           // streams: packets that may be queued (at least 1)
    uint32_t buffer_bytes = 0;          // buffers: zeroed memory of the voice's own (0: none)
};
static_assert(sizeof(VoiceDesc) == 12, "VoiceDesc must have no padding");

enum class Start : uint8_t { Now, WithSynch };
enum class Apply : uint8_t { Now, Deferred };

struct VoiceStatus {
    bool playing = false;  // started and not finished (paused voices still count)
    bool paused = false;
    bool looping = false;
    bool starved = false;          // streams: playing with no packet
    bool accepts_packets = false;  // streams: fewer than max_packets queued
};

enum class SpatialMode : uint8_t { World, HeadRelative, Off };

// What a voice plays (the game's sound groups SFX, VOICE, MUSIC, GUI). Ambience: the level
// background beds, which the game plays as MUSIC (snd.cpp, Music_Play: by the alias name).
enum class Category : uint8_t { Effects, Dialogue, Music, Interface, Ambience };

struct Spatial {
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float min_distance = 1.0f;  // full level up to here
    float max_distance = 1.0e9f;
    float rolloff_factor = 1.0f;  // without a distance curve
    float doppler_factor = 1.0f;
    SpatialMode mode = SpatialMode::World;
};

// A reverb environment in I3DL2 terms (DSI3DL2LISTENER's fields), then where the reflections
// come from: levels in millibels (-10000 = silent), times in seconds, diffusion/density in
// percent. Float throughout, so environments blend.
struct Environment {
    float room = -10000.0f;  // the reverb's overall level; -10000: no reverb
    float room_hf = 0.0f;    // its level at hf_reference
    float room_rolloff = 0.0f;
    float decay_time = 1.49f;
    float decay_hf_ratio = 0.83f;
    float reflections = -2602.0f;
    float reflections_delay = 0.007f;
    float reverb = 200.0f;
    float reverb_delay = 0.011f;
    float diffusion = 100.0f;
    float density = 100.0f;
    float hf_reference = 5000.0f;
    // The direction the early reflections / the late reverb come from, relative to the listener
    // (x right, y up, z ahead); the length is how focused (0: all around, 1: one direction).
    float reflections_pan[3] = {0, 0, 0};
    float reverb_pan[3] = {0, 0, 0};
};

struct Listener {
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float front[3] = {0, 0, 1};
    float top[3] = {0, 1, 0};
};

class SoundDevice {
public:
    // The output device (once; later calls report whether it is open).
    virtual bool open() = 0;

    // --- Voices ---
    // 0 when there is no output. `format` may be null (set_format later).
    virtual VoiceId create_voice(const VoiceDesc& desc, const Format* format) = 0;
    virtual uint32_t add_ref(VoiceId voice) = 0;
    virtual uint32_t release(VoiceId voice) = 0;  // at 0 the voice stops
    virtual bool set_format(VoiceId voice, const Format& format) = 0;  // stops the voice

    // Buffers (positions in bytes): they play from memory the caller keeps, read as it plays.
    virtual void set_buffer_data(VoiceId voice, const uint8_t* data, uint32_t size) = 0;  // null: the voice's own
    virtual void set_play_region(VoiceId voice, uint32_t start, uint32_t length) = 0;     // 0: to the end
    virtual void set_loop_region(VoiceId voice, uint32_t start, uint32_t length) = 0;     // in the play region
    virtual void set_position(VoiceId voice, uint32_t offset) = 0;
    virtual void play(VoiceId voice, bool looping, bool from_start, Start start) = 0;

    // Streams: packets play in order; end_stream() lets the stream finish after the queued ones.
    virtual bool submit(VoiceId voice, const Packet& packet) = 0;  // false: max_packets queued
    virtual void end_stream(VoiceId voice) = 0;
    // A title routine called (routine(context, packet completion, status), from do_work()) when a
    // packet is done; without one, a packet's completion is an event handle that gets set.
    virtual void set_packet_callback(VoiceId voice, uint32_t routine, uint32_t context) = 0;

    // Both.
    virtual void stop(VoiceId voice) = 0;  // streams: queued packets are flushed
    virtual void pause(VoiceId voice) = 0;
    virtual void resume(VoiceId voice, Start start) = 0;
    virtual VoiceStatus status(VoiceId voice) = 0;
    virtual void set_volume(VoiceId voice, float gain) = 0;
    virtual void set_headroom(VoiceId voice, float gain) = 0;  // a second, fixed level
    virtual void set_rate(VoiceId voice, uint32_t rate) = 0;
    virtual void set_pan(VoiceId voice, float pan) = 0;  // non-spatial mono voices: -1 left .. 1 right
    // Sent to the environment's reverb as well (the game's request; it never asks in practice:
    // enhanced devices send world voices themselves, see set_environment).
    virtual void set_reverb_send(VoiceId voice, bool send) = 0;
    virtual void set_spatial(VoiceId voice, const Spatial& spatial, Apply apply) = 0;
    virtual void set_spatial_mode(VoiceId voice, SpatialMode mode, Apply apply) = 0;
    // Level by distance: `count` points spread evenly from the minimum to the maximum distance
    // (replaces the rolloff factor); count 0 removes it.
    virtual void set_distance_curve(VoiceId voice, const float* points, uint32_t count) = 0;

    // --- The listener and timing ---
    virtual void set_listener(const Listener& listener, Apply apply) = 0;
    virtual Listener listener(Apply apply) = 0;  // Deferred: what commit() would apply
    virtual void commit() = 0;
    virtual void start_synched() = 0;
    virtual void set_headphones(bool enabled) = 0;
    virtual void do_work() = 0;  // runs packet callbacks

    // --- Our additions ---
    // What the voice plays from now on (default Effects).
    virtual void set_category(VoiceId voice, Category category) = 0;
    // Enhanced only (the retail game never sets an environment): the reverb around the
    // listener; effects and dialogue are sent to it. Ignored unless the device is enhanced
    // (OutputConfig::enhanced).
    virtual void set_environment(const Environment& environment) = 0;
    virtual bool enhanced() = 0;
    // Enhanced only: how much of a world voice's direct sound is blocked on its way to the
    // listener (0 clear .. 1 behind a wall): quieter and duller; its reverb send stays.
    virtual void set_occlusion(VoiceId voice, float amount) = 0;

protected:
    ~SoundDevice() = default;
};

}  // namespace audio
