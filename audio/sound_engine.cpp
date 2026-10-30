// The sound device on OpenAL Soft. See sound_engine.h.
//
// - Distance attenuation: from the voice's distance curve (points spread evenly between the
//   minimum and maximum distance) or else its rolloff factor; OpenAL only pans/spatializes.
// - Coordinates: the game's space is left-handed (+z forward), OpenAL's right-handed, so z is
//   negated on the way out.

#include "sound_engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <memory>
#include <vector>

#include "dispatcher.h"
#include "kernel.h"
#include "log.h"
#include "test_mode.h"
#include "types.h"

namespace audio {

namespace {

using xbox::from_xbox_ptr;
using xbox::to_xbox_ptr;
using xbox::XbPtr;

constexpr uint32_t kMagicVoice = 0x4F565344;

struct VoiceImpl;

// What a voice id points at (title-addressable): a DirectSound object to original code.
struct VoiceHeader {
    XbPtr vtable;
    uint32_t magic;
    VoiceImpl* impl;
};

struct VoiceImpl {
    explicit VoiceImpl(const VoiceDesc& desc)
        : voice(desc.stream ? Voice::Kind::Stream : Voice::Kind::Buffer), desc(desc) {}

    VoiceHeader header{};
    int refs = 1;
    Voice voice;
    const VoiceDesc desc;
    Output::Source* source = nullptr;
    uint32_t callback = 0, context = 0;
    std::vector<uint8_t> own_data;

    float volume = 1.0f, headroom = 1.0f;
    uint32_t rate = 0;
    float pan = 0.0f;
    bool reverb_send = false;
    float occlusion = 0.0f;
    Category category = Category::Effects;
    Spatial current, pending;
    bool has_pending = false;
    std::vector<float> curve;

    bool synch_play = false, synch_looping = false, synch_from_start = false, synch_resume = false;
};

struct Callback {
    uint32_t routine;
    uint32_t args[3];
};

class Engine final : public SoundDevice {
public:
    bool open() override;
    VoiceId create_voice(const VoiceDesc& desc, const Format* format) override;
    uint32_t add_ref(VoiceId voice) override;
    uint32_t release(VoiceId voice) override;
    bool set_format(VoiceId voice, const Format& format) override;
    void set_buffer_data(VoiceId voice, const uint8_t* data, uint32_t size) override;
    void set_play_region(VoiceId voice, uint32_t start, uint32_t length) override;
    void set_loop_region(VoiceId voice, uint32_t start, uint32_t length) override;
    void set_position(VoiceId voice, uint32_t offset) override;
    void play(VoiceId voice, bool looping, bool from_start, Start start) override;
    bool submit(VoiceId voice, const Packet& packet) override;
    void end_stream(VoiceId voice) override;
    void set_packet_callback(VoiceId voice, uint32_t routine, uint32_t context) override;
    void stop(VoiceId voice) override;
    void pause(VoiceId voice) override;
    void resume(VoiceId voice, Start start) override;
    VoiceStatus status(VoiceId voice) override;
    void set_volume(VoiceId voice, float gain) override;
    void set_headroom(VoiceId voice, float gain) override;
    void set_rate(VoiceId voice, uint32_t rate) override;
    void set_pan(VoiceId voice, float pan) override;
    void set_reverb_send(VoiceId voice, bool send) override;
    void set_spatial(VoiceId voice, const Spatial& spatial, Apply apply) override;
    void set_spatial_mode(VoiceId voice, SpatialMode mode, Apply apply) override;
    void set_distance_curve(VoiceId voice, const float* points, uint32_t count) override;
    void set_listener(const Listener& listener, Apply apply) override;
    Listener listener(Apply apply) override;
    void commit() override;
    void start_synched() override;
    void set_headphones(bool enabled) override;
    void do_work() override;
    void set_category(VoiceId voice, Category category) override;
    void set_environment(const Environment& environment) override;
    bool enhanced() override;
    void set_occlusion(VoiceId voice, float amount) override;
    bool has(Feature feature) override;
    bool feature_on(Feature feature) const { return config.enhanced && config.features.has(feature); }  // locked
    void apply_enhanced();  // locked: after config.enhanced or features changed

    std::mutex mutex;  // everything below except `callbacks`; never taken by the mixer
    OutputConfig config;
    CategoryVolumes volumes;
    std::unique_ptr<Output> output;
    XbPtr vtable = 0;
    std::vector<VoiceImpl*> voices;  // never freed: the mixer may still hold a released one
    std::vector<std::mutex*> fork_locks;
    Listener current_listener, pending_listener;
    bool listener_pending = false;

    std::mutex callbacks_mutex;
    std::deque<Callback> callbacks;

private:
    // The output, or none in a test process: there voices only keep their state.
    Output* out() { return in_test() ? nullptr : output.get(); }
    VoiceImpl* find(VoiceId id);
    template <typename Change>
    void change(VoiceId id, Change change);
    template <typename Change>
    void change_spatial(VoiceId id, Apply apply, Change change);
    bool is_spatial(const VoiceImpl& v);
    float distance_gain(const VoiceImpl& v);
    void update(VoiceImpl& v);
    friend void audio::set_enhanced(bool enhanced);
    friend void audio::set_enhanced_features(const EnhancedFeatures& features);
    friend void audio::log_voices(void (*)(const float*, const float*, char*, size_t));
    friend void audio::set_category_volumes(const CategoryVolumes& volumes);
    void update_listener();
    void sync(VoiceImpl& v);
    void packet_done(VoiceImpl& v, const Packet& packet, uint32_t status);
};

Engine& g = *new Engine;  // used by the mixer thread until exit: never destroyed

void to_al(const float in[3], float out[3]) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = -in[2];
}

VoiceImpl* Engine::find(VoiceId id) {
    const auto* header = from_xbox_ptr<const VoiceHeader>(id);
    if (header && header->magic == kMagicVoice) return header->impl;
    if (!in_test()) LOG_WARN("audio", "%08x is not a voice", id);
    return nullptr;
}

template <typename Change>
void Engine::change(VoiceId id, Change change) {
    Guard lock(mutex, TestLock::State);
    if (VoiceImpl* v = find(id)) change(*v);
}

template <typename Change>
void Engine::change_spatial(VoiceId id, Apply apply, Change change) {
    Guard lock(mutex, TestLock::State);
    VoiceImpl* v = find(id);
    if (!v) return;
    if (apply == Apply::Deferred) {
        if (!v->has_pending) v->pending = v->current;
        change(v->pending);
        v->has_pending = true;
    } else {
        change(v->current);
        update(*v);
    }
}

bool Engine::is_spatial(const VoiceImpl& v) {
    return v.desc.spatial && v.voice.format().channels == 1 && v.current.mode != SpatialMode::Off;
}

float Engine::distance_gain(const VoiceImpl& v) {
    if (!is_spatial(v)) return 1.0f;
    const Spatial& p = v.current;
    float d[3];
    for (int i = 0; i < 3; ++i)
        d[i] = p.position[i] - (p.mode == SpatialMode::HeadRelative ? 0.0f : current_listener.position[i]);
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float min = std::max(p.min_distance, 1.0e-4f);
    const float max = std::max(p.max_distance, min);
    if (v.desc.mute_at_max_distance && distance >= max) return 0.0f;
    if (!v.curve.empty()) {
        if (distance <= min || v.curve.size() == 1) return v.curve.front();
        if (distance >= max) return v.curve.back();
        const float t = (distance - min) / (max - min) * float(v.curve.size() - 1);
        const auto i = static_cast<size_t>(t);
        return v.curve[i] + (v.curve[i + 1] - v.curve[i]) * (t - float(i));
    }
    const float clamped = std::min(distance, max);
    return clamped <= min ? 1.0f : min / (min + p.rolloff_factor * (clamped - min));
}

void Engine::update(VoiceImpl& v) {
    Output* const o = out();
    if (!v.source || !o) return;
    const Format format = v.voice.format();
    Output::SourceParams params;
    params.gain = v.volume * v.headroom * distance_gain(v) * volumes.gain(v.category);
    if (feature_on(Feature::CloseBoost) && (v.category == Category::Gunfire || v.category == Category::OwnGunfire ||
                                            v.category == Category::Bullets)) {
        float distance = 0.0f;  // not placed: the player's own
        if (is_spatial(v)) {
            const float* p = v.current.position;
            const float* l = current_listener.position;
            distance = std::sqrt((p[0] - l[0]) * (p[0] - l[0]) + (p[1] - l[1]) * (p[1] - l[1]) +
                                 (p[2] - l[2]) * (p[2] - l[2]));
        }
        params.gain *= volumes.close_gain(v.category, distance);
    }
    params.pitch = v.rate && format.rate ? std::clamp(float(v.rate) / float(format.rate), 1.0f / 64, 16.0f) : 1.0f;
    if (is_spatial(v)) {
        params.placement =
            v.current.mode == SpatialMode::HeadRelative ? Output::Placement::Relative : Output::Placement::World;
        to_al(v.current.position, params.position);
    } else if (format.channels == 1) {
        params.placement = Output::Placement::Relative;
        params.position[0] = v.pan;
    } else {
        params.placement = Output::Placement::Direct;
    }
    // Enhanced: effects and dialogue are heard in the room (the game asks for no reverb send).
    params.reverb_send = v.reverb_send || (feature_on(Feature::Reverb) && (v.category == Category::Effects ||
                                                                            v.category == Category::Gunfire ||
                                                                            v.category == Category::OwnGunfire ||
                                                                            v.category == Category::Bullets ||
                                                                            v.category == Category::Dialogue));
    // Enhanced: a blocked world voice through a low-pass (full: -3 dB, a further -6 dB at 5 kHz:
    // what comes through walls, gently as Steam Audio's transmission),
    // and heard a little more through the room: its reverb send up to +3 dB (sound reaching the
    // listener by reflections; +6 dB made blocked gunfire in the open a wash, playtest 10-30).
    if (feature_on(Feature::Occlusion) && v.occlusion > 0.0f && is_spatial(v)) {
        params.direct_gain = 1.0f - 0.3f * v.occlusion;
        params.direct_gain_hf = 1.0f - 0.5f * v.occlusion;
        if (params.reverb_send) params.send_gain = 1.0f + 0.4f * v.occlusion;
    }
    // A position or level that is not a number (seen once, 10-30: an entity gone NaN just before a
    // crash in the animation code) would be refused by OpenAL on every update: the voice is muted
    // at the origin instead, and it is logged once.
    if (!std::isfinite(params.gain) || !std::isfinite(params.position[0]) || !std::isfinite(params.position[1]) ||
        !std::isfinite(params.position[2])) {
        static bool logged = false;
        if (!logged) LOG_WARN("audio", "a voice's position or level is not a number: muted");
        logged = true;
        params.gain = 0.0f;
        params.position[0] = params.position[1] = params.position[2] = 0.0f;
    }
    o->update(v.source, params);
}

void Engine::update_listener() {
    Output* const o = out();
    if (!o) return;
    float position[3], velocity[3], at[3], up[3];
    to_al(current_listener.position, position);
    to_al(current_listener.velocity, velocity);
    to_al(current_listener.front, at);
    to_al(current_listener.top, up);
    o->set_listener(position, velocity, at, up);
}

// Starts, pauses or stops the voice's OpenAL source as its state now says.
void Engine::sync(VoiceImpl& v) {
    if (Output* o = out(); o && v.source) o->sync(v.source);
}

// Called with the voice locked, possibly on the mixer thread.
void Engine::packet_done(VoiceImpl& v, const Packet& packet, uint32_t status) {
    if (v.callback) {
        Guard lock(callbacks_mutex, TestLock::Callbacks);
        callbacks.push_back({v.callback, {v.context, packet.completion, status}});
    } else if (packet.completion) {
        xbox::set_event_handle(packet.completion);
    }
}

bool Engine::open() {
    Guard lock(mutex, TestLock::State);
    if (in_test()) return true;  // voices without output
    if (!output) output = Output::open(config);
    return output != nullptr;
}

VoiceId Engine::create_voice(const VoiceDesc& desc, const Format* format) {
    Guard lock(mutex, TestLock::State);
    if (!in_test() && !output) return 0;
    auto* v = new VoiceImpl(desc);
    v->header = {vtable, kMagicVoice, v};
    v->voice.on_packet_done = [v](const Packet& packet, uint32_t status) { g.packet_done(*v, packet, status); };
    if (desc.buffer_bytes) {
        v->own_data.assign(desc.buffer_bytes, 0);
        v->voice.set_buffer_data(v->own_data.data(), desc.buffer_bytes);
    }
    Output* const o = out();
    v->source = o ? o->add_source(v->voice) : nullptr;
    if (format) {
        v->voice.set_format(*format);
        if (v->source) o->reformat(v->source);
    }
    update(*v);
    voices.push_back(v);
    return to_xbox_ptr(&v->header);
}

uint32_t Engine::add_ref(VoiceId id) {
    Guard lock(mutex, TestLock::State);
    VoiceImpl* v = find(id);
    return v ? static_cast<uint32_t>(++v->refs) : 0;
}

uint32_t Engine::release(VoiceId id) {
    Guard lock(mutex, TestLock::State);
    VoiceImpl* v = find(id);
    if (!v) return 0;
    if (--v->refs == 0) {
        v->synch_play = v->synch_resume = false;
        v->voice.stop();
        sync(*v);
    }
    return static_cast<uint32_t>(std::max(v->refs, 0));
}

bool Engine::set_format(VoiceId id, const Format& format) {
    Guard lock(mutex, TestLock::State);
    VoiceImpl* v = find(id);
    if (!v) return false;
    v->voice.stop();
    v->voice.set_format(format);
    if (Output* o = out(); o && v->source) o->reformat(v->source);
    update(*v);
    return true;
}

void Engine::set_buffer_data(VoiceId id, const uint8_t* data, uint32_t size) {
    change(id, [&](VoiceImpl& v) {
        if (data)
            v.voice.set_buffer_data(data, size);
        else
            v.voice.set_buffer_data(v.own_data.data(), static_cast<uint32_t>(v.own_data.size()));
    });
}

void Engine::set_play_region(VoiceId id, uint32_t start, uint32_t length) {
    change(id, [&](VoiceImpl& v) { v.voice.set_play_region(start, length); });
}

void Engine::set_loop_region(VoiceId id, uint32_t start, uint32_t length) {
    change(id, [&](VoiceImpl& v) { v.voice.set_loop_region(start, length); });
}

void Engine::set_position(VoiceId id, uint32_t offset) {
    change(id, [&](VoiceImpl& v) { v.voice.set_position(offset); });
}

void Engine::play(VoiceId id, bool looping, bool from_start, Start start) {
    change(id, [&](VoiceImpl& v) {
        if (start == Start::WithSynch) {
            v.synch_play = true;
            v.synch_looping = looping;
            v.synch_from_start = from_start;
            return;
        }
        v.voice.play(looping, from_start);
        sync(v);
    });
}

bool Engine::submit(VoiceId id, const Packet& packet) {
    Guard lock(mutex, TestLock::State);
    VoiceImpl* v = find(id);
    if (!v || !v->voice.submit(packet, std::max<uint32_t>(v->desc.max_packets, 1))) return false;
    sync(*v);  // streams start with their first packet
    return true;
}

void Engine::end_stream(VoiceId id) {
    change(id, [](VoiceImpl& v) { v.voice.discontinuity(); });
}

void Engine::set_packet_callback(VoiceId id, uint32_t routine, uint32_t context) {
    change(id, [&](VoiceImpl& v) {
        v.callback = routine;
        v.context = context;
    });
}

void Engine::stop(VoiceId id) {
    change(id, [&](VoiceImpl& v) {
        v.synch_play = v.synch_resume = false;
        v.voice.stop();
        sync(v);
    });
}

void Engine::pause(VoiceId id) {
    change(id, [&](VoiceImpl& v) {
        v.voice.set_paused(true);
        sync(v);
    });
}

void Engine::resume(VoiceId id, Start start) {
    change(id, [&](VoiceImpl& v) {
        if (start == Start::WithSynch) {
            v.synch_resume = true;
            return;
        }
        v.voice.set_paused(false);
        sync(v);
    });
}

VoiceStatus Engine::status(VoiceId id) {
    VoiceStatus s;
    change(id, [&](VoiceImpl& v) {
        s.playing = v.voice.playing() || v.synch_play;
        s.paused = v.voice.paused();
        s.looping = s.playing && v.voice.looping();
        s.starved = v.voice.starved();
        s.accepts_packets = v.voice.queued_packets() < std::max<uint32_t>(v.desc.max_packets, 1);
    });
    return s;
}

void Engine::set_volume(VoiceId id, float gain) {
    change(id, [&](VoiceImpl& v) {
        v.volume = gain;
        update(v);
    });
}

void Engine::set_headroom(VoiceId id, float gain) {
    change(id, [&](VoiceImpl& v) {
        v.headroom = gain;
        update(v);
    });
}

void Engine::set_rate(VoiceId id, uint32_t rate) {
    change(id, [&](VoiceImpl& v) {
        v.rate = rate;
        update(v);
    });
}

void Engine::set_pan(VoiceId id, float pan) {
    change(id, [&](VoiceImpl& v) {
        v.pan = pan;
        update(v);
    });
}

void Engine::set_reverb_send(VoiceId id, bool send) {
    change(id, [&](VoiceImpl& v) { v.reverb_send = send; });
}

void Engine::set_spatial(VoiceId id, const Spatial& spatial, Apply apply) {
    change_spatial(id, apply, [&](Spatial& s) { s = spatial; });
}

void Engine::set_spatial_mode(VoiceId id, SpatialMode mode, Apply apply) {
    change_spatial(id, apply, [&](Spatial& s) { s.mode = mode; });
}

void Engine::set_distance_curve(VoiceId id, const float* points, uint32_t count) {
    change(id, [&](VoiceImpl& v) {
        v.curve.assign(points, points + (points ? count : 0));
        update(v);
    });
}

void Engine::set_listener(const Listener& listener, Apply apply) {
    Guard lock(mutex, TestLock::State);
    if (apply == Apply::Deferred) {
        pending_listener = listener;
        listener_pending = true;
        return;
    }
    current_listener = listener;
    update_listener();
    for (VoiceImpl* v : voices) update(*v);
}

Listener Engine::listener(Apply apply) {
    Guard lock(mutex, TestLock::State);
    return apply == Apply::Deferred && listener_pending ? pending_listener : current_listener;
}

void Engine::commit() {
    Guard lock(mutex, TestLock::State);
    if (!output) return;
    Output* const o = out();
    if (o) o->begin_batch();
    if (listener_pending) {
        current_listener = pending_listener;
        listener_pending = false;
        update_listener();
    }
    // The listener may have moved, so every voice's distance gain is refreshed.
    for (VoiceImpl* v : voices) {
        if (v->has_pending) {
            v->current = v->pending;
            v->has_pending = false;
        }
        update(*v);
    }
    if (o) o->end_batch();
}

void Engine::start_synched() {
    Guard lock(mutex, TestLock::State);
    if (!output) return;
    Output* const o = out();
    if (o) o->begin_batch();
    for (VoiceImpl* v : voices) {
        if (!v->synch_play && !v->synch_resume) continue;
        if (v->synch_play) v->voice.play(v->synch_looping, v->synch_from_start);
        if (v->synch_resume) v->voice.set_paused(false);
        v->synch_play = v->synch_resume = false;
        sync(*v);
    }
    if (o) o->end_batch();
}

void Engine::set_headphones(bool enabled) {
    Guard lock(mutex, TestLock::State);
    if (Output* o = out()) o->set_hrtf(enabled);
}

// The APU interrupt would have run these.
void Engine::do_work() {
    std::deque<Callback> due;
    {
        Guard lock(callbacks_mutex, TestLock::Callbacks);
        due.swap(callbacks);
    }
    const auto call_guest = xbox::kernel_config().call_guest;
    for (const Callback& callback : due)
        if (call_guest) call_guest(callback.routine, callback.args, 3);
}

void Engine::set_category(VoiceId id, Category category) {
    change(id, [&](VoiceImpl& v) {
        if (v.category == category) return;
        v.category = category;
        update(v);
    });
}

void Engine::set_environment(const Environment& environment) {
    Guard lock(mutex, TestLock::State);
    if (Output* o = out(); o && feature_on(Feature::Reverb)) o->set_environment(environment);
}

bool Engine::enhanced() {
    Guard lock(mutex, TestLock::State);
    return config.enhanced;
}

bool Engine::has(Feature feature) {
    Guard lock(mutex, TestLock::State);
    return feature_on(feature);
}

void Engine::apply_enhanced() {
    if (Output* o = output.get()) {
        o->begin_batch();
        o->set_enhanced(feature_on(Feature::Resampler), feature_on(Feature::Reverb));
        for (VoiceImpl* v : voices) update(*v);
        o->end_batch();
    }
}

void Engine::set_occlusion(VoiceId id, float amount) {
    change(id, [&](VoiceImpl& v) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        if (v.occlusion == amount) return;
        v.occlusion = amount;
        update(v);
    });
}

}  // namespace

SoundDevice& sound_device() { return g; }

std::span<const xbox::KernelImpl> device_methods() {
    // SoundDevice's virtual functions in declaration order (sound_device.h): its vtable's slots.
    static const char* const kNames[] = {
        "SoundDevice::open", "SoundDevice::create_voice", "SoundDevice::add_ref", "SoundDevice::release",
        "SoundDevice::set_format", "SoundDevice::set_buffer_data", "SoundDevice::set_play_region",
        "SoundDevice::set_loop_region", "SoundDevice::set_position", "SoundDevice::play", "SoundDevice::submit",
        "SoundDevice::end_stream", "SoundDevice::set_packet_callback", "SoundDevice::stop", "SoundDevice::pause",
        "SoundDevice::resume", "SoundDevice::status", "SoundDevice::set_volume", "SoundDevice::set_headroom",
        "SoundDevice::set_rate", "SoundDevice::set_pan", "SoundDevice::set_reverb_send", "SoundDevice::set_spatial",
        "SoundDevice::set_spatial_mode", "SoundDevice::set_distance_curve", "SoundDevice::set_listener",
        "SoundDevice::listener", "SoundDevice::commit", "SoundDevice::start_synched", "SoundDevice::set_headphones",
        "SoundDevice::do_work", "SoundDevice::set_category", "SoundDevice::set_environment",
        "SoundDevice::enhanced", "SoundDevice::set_occlusion", "SoundDevice::has",
    };
    static const std::vector<xbox::KernelImpl> methods = [] {
        void* const* vtable = *reinterpret_cast<void* const* const*>(&g);
        std::vector<xbox::KernelImpl> list;
        for (size_t i = 0; i < std::size(kNames); ++i) list.push_back({kNames[i], vtable[i]});
        return list;
    }();
    return methods;
}

void configure(const OutputConfig& config) {
    Guard lock(g.mutex, TestLock::State);
    g.config = config;
}

void set_voice_vtable(uint32_t vtable) {
    Guard lock(g.mutex, TestLock::State);
    g.vtable = vtable;
    for (VoiceImpl* v : g.voices) v->header.vtable = vtable;
}

VoiceId voice_from_object(uint32_t object) {
    const auto* header = from_xbox_ptr<const VoiceHeader>(object);
    return header && header->magic == kMagicVoice ? object : 0;
}

void add_fork_lock(std::mutex& mutex) {
    Guard lock(g.mutex, TestLock::State);
    g.fork_locks.push_back(&mutex);
}

// The lock order everywhere: the other parts' locks, the state, then a voice, then the
// callbacks (render: voice, callbacks).
void hold_for_fork() {
    for (std::mutex* m : g.fork_locks) m->lock();
    g.mutex.lock();
    for (const VoiceImpl* v : g.voices) v->voice.hold();
    g.callbacks_mutex.lock();
}

void release_after_fork() {
    g.callbacks_mutex.unlock();
    for (auto v = g.voices.rbegin(); v != g.voices.rend(); ++v) (*v)->voice.release();
    g.mutex.unlock();
    for (auto m = g.fork_locks.rbegin(); m != g.fork_locks.rend(); ++m) (*m)->unlock();
}

void set_enhanced(bool enhanced) {
    Guard lock(g.mutex, TestLock::State);
    if (g.config.enhanced == enhanced) return;
    g.config.enhanced = enhanced;
    LOG_INFO("audio", "%s sound", enhanced ? "enhanced" : "original");
    g.apply_enhanced();
}

void set_enhanced_features(const EnhancedFeatures& features) {
    Guard lock(g.mutex, TestLock::State);
    if (g.config.features == features) return;
    g.config.features = features;
    g.apply_enhanced();
}

EnhancedFeatures enhanced_features() {
    Guard lock(g.mutex, TestLock::State);
    return g.config.features;
}

bool enhanced() {
    Guard lock(g.mutex, TestLock::State);
    return g.config.enhanced;
}

void set_category_volumes(const CategoryVolumes& volumes) {
    Guard lock(g.mutex, TestLock::State);
    if (g.volumes == volumes) return;
    g.volumes = volumes;
    if (Output* o = g.output.get()) {
        o->begin_batch();
        for (VoiceImpl* v : g.voices) g.update(*v);
        o->end_batch();
    }
}

CategoryVolumes category_volumes() {
    Guard lock(g.mutex, TestLock::State);
    return g.volumes;
}

void set_paused(bool paused) {
    if (g.output) g.output->set_paused(paused);
}

void log_voices(void (*describe)(const float listener[3], const float source[3], char* out, size_t size)) {
    Guard lock(g.mutex, TestLock::State);
    static const char* const kCategories[] = {"effects", "dialogue", "music", "interface", "ambience", "gunfire", "bullets", "own gunfire"};
    int count = 0;
    for (VoiceImpl* v : g.voices) {
        if (!v->refs || !v->voice.playing() || !g.is_spatial(*v)) continue;
        const float* p = v->current.position;
        const float* l = g.current_listener.position;
        const float d = std::sqrt((p[0] - l[0]) * (p[0] - l[0]) + (p[1] - l[1]) * (p[1] - l[1]) +
                                  (p[2] - l[2]) * (p[2] - l[2]));
        LOG_INFO("audio", "voice %08x %s: distance %.0f (min %.0f, max %.0f), distance gain %.2f, volume %.2f, "
                 "occlusion %.2f",
                 unsigned(reinterpret_cast<uintptr_t>(&v->header)),
                 kCategories[size_t(v->category)], double(d), double(v->current.min_distance),
                 double(v->current.max_distance), double(g.distance_gain(*v)), double(v->volume),
                 double(v->occlusion));
        if (describe && v->occlusion > 0.0f) {
            char text[2048];
            describe(l, p, text, sizeof(text));
            LOG_INFO("audio", "  %s", text);
        }
        ++count;
    }
    LOG_INFO("audio", "%d world voices playing (%s sound)", count, g.config.enhanced ? "enhanced" : "original");
}

Output* output() {
    Guard lock(g.mutex, TestLock::State);
    return g.output.get();
}

}  // namespace audio
