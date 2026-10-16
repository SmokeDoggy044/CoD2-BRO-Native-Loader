// Xbox DirectSound replacement. See dsound.h.
//
// Semantics follow the XDK and what CoD2: BRO relies on:
// - Volumes and headroom are in hundredths of a dB (attenuation); frequency in Hz
//   (0 = the format's rate).
// - 3D settings may be deferred (apply = DS3D_DEFERRED) until
//   CommitDeferredSettings. Distance attenuation is computed here from the voice's
//   min/max distance and rolloff factor, or its rolloff curve (points spaced evenly
//   between min and max distance); OpenAL only pans/spatializes.
// - Play/Pause with the SYNCHPLAYBACK flag wait for IDirectSound_SynchPlayback.
// - Effects (DSP images, effect data, mix bin headroom) are accepted and ignored: the
//   title's own sounds never route to the effects, only the voice-chat code uses them.
// - Coordinates: DirectSound is left-handed (+z forward), OpenAL right-handed, so z is
//   negated on the way out.

#include "xbox_dsound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "compiler.h"
#include "dispatcher.h"
#include "filesystem.h"
#include "log.h"
#include "objects.h"
#include "types.h"

namespace audio {

namespace {

using xbox::from_xbox_ptr;
using xbox::Handle;
using xbox::to_xbox_ptr;
using xbox::XbPtr;

constexpr uint32_t kOk = 0;
constexpr uint32_t kFail = 0x80004005;        // E_FAIL
constexpr uint32_t kInvalidArg = 0x80070057;  // E_INVALIDARG

// Flags and modes.
constexpr uint32_t kCaps3D = 0x10;                  // DSBCAPS_CTRL3D / DSSTREAMCAPS_CTRL3D
constexpr uint32_t kCapsMuteAtMaxDistance = 0x20000;
constexpr uint32_t kPlayLooping = 0x1, kPlayFromStart = 0x2, kPlaySynch = 0x4;
constexpr uint32_t kPauseResume = 0, kPausePause = 1, kPauseSynch = 2;
constexpr uint32_t kDeferred = 1;  // DS3D_DEFERRED
constexpr uint32_t kModeNormal = 0, kModeHeadRelative = 1, kModeDisable = 2;
constexpr uint32_t kBufferPlaying = 0x1, kBufferPaused = 0x2, kBufferLooping = 0x4;
constexpr uint32_t kStreamReady = 0x1, kStreamPlaying = 0x10000, kStreamPaused = 0x20000, kStreamStarved = 0x40000;
constexpr uint32_t kMediaAcceptOutput = 0x2;  // XMO_STATUSF_ACCEPT_OUTPUT_DATA
constexpr uint32_t kMagicDevice = 0x444E5344, kMagicVoice = 0x4F565344, kMagicMedia = 0x4F4D4658;

// --- Structures in title memory --------------------------------------------------------

struct XbDsBufferDesc {  // DSBUFFERDESC
    uint32_t size;
    uint32_t flags;
    uint32_t buffer_bytes;
    XbPtr format;    // WAVEFORMATEX*
    XbPtr mix_bins;  // DSMIXBINS*
    uint32_t input_mix_bin;
};

struct XbDsStreamDesc {  // DSSTREAMDESC
    uint32_t flags;
    uint32_t max_attached_packets;
    XbPtr format;
    XbPtr callback;  // VOID CALLBACK (*)(stream context, packet context, status)
    XbPtr context;
    XbPtr mix_bins;
};

struct XbMixBins {  // DSMIXBINS
    uint32_t count;
    XbPtr pairs;  // DSMIXBINVOLUMEPAIR[count]: {DWORD mix bin, LONG volume}
};

struct XbDs3dBuffer {  // DS3DBUFFER
    uint32_t size;
    float position[3];
    float velocity[3];
    uint32_t inside_cone_angle;
    uint32_t outside_cone_angle;
    float cone_orientation[3];
    int32_t cone_outside_volume;
    float min_distance;
    float max_distance;
    uint32_t mode;
    float distance_factor;
    float rolloff_factor;
    float doppler_factor;
};
static_assert(sizeof(XbDs3dBuffer) == 0x4C);

struct XbMediaPacket {  // XMEDIAPACKET
    XbPtr buffer;
    uint32_t max_size;
    XbPtr completed_size;  // DWORD*
    XbPtr status;          // DWORD*
    uint32_t completion;   // event handle (media objects) or context (stream callbacks)
    XbPtr timestamp;
};

struct XbMediaInfo {  // XMEDIAINFO
    uint32_t flags;
    uint32_t input_size;
    uint32_t output_size;
    uint32_t max_lookahead;
};

// What the title's object pointers point at.
struct GuestObject {
    XbPtr vtable;
    uint32_t magic;
    void* impl;
};

// --- State -----------------------------------------------------------------------------

struct Ds3d {
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float min_distance = 1.0f;
    float max_distance = 1.0e9f;
    uint32_t mode = kModeNormal;
    float rolloff_factor = 1.0f;
};

struct VoiceImpl {
    explicit VoiceImpl(bool is_stream) : voice(is_stream ? Voice::Kind::Stream : Voice::Kind::Buffer), stream(is_stream) {}

    GuestObject guest{};
    int refs = 1;
    Voice voice;
    const bool stream;
    Output::Source* source = nullptr;
    uint32_t flags = 0;
    uint32_t max_packets = 0;
    XbPtr callback = 0, context = 0;
    std::vector<uint8_t> own_data;  // when the title gives a size but no data

    int32_t volume = 0;     // hundredths of a dB
    uint32_t headroom = 0;  // hundredths of a dB
    uint32_t frequency = 0;
    float pan = 0.0f;  // -1 left .. 1 right, for 2D mono voices
    Ds3d current, pending;
    bool has_pending = false;
    std::vector<float> curve;

    bool synch_play = false, synch_looping = false, synch_from_start = false, synch_resume = false;
};

struct Listener {
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float front[3] = {0, 0, 1};
    float top[3] = {0, 1, 0};
};

struct Callback {
    XbPtr routine;
    uint32_t args[3];
};

struct State {
    std::mutex mutex;  // everything below except `callbacks`; never taken by the mixer
    OutputConfig config;
    std::unique_ptr<Output> output;
    GuestObject device{};
    int device_refs = 0;
    std::vector<VoiceImpl*> voices;  // never freed: titles create a fixed pool
    Listener listener, pending_listener;
    bool listener_pending = false;

    std::mutex callbacks_mutex;
    std::deque<Callback> callbacks;  // stream callbacks, run by DirectSoundDoWork
};

State& g = *new State;  // used by the mixer thread until exit: never destroyed

// DSEFFECTIMAGEDESC for titles that look at it: no effects.
uint32_t g_empty_effect_image[64] = {};

// --- Vtables -------------------------------------------------------------------------------

[[noreturn]] void vtable_trap(const char* type, int slot) {
    LOG_ERROR("audio", "%s method #%d called through its vtable: not implemented", type, slot);
    std::abort();
}

constexpr int kVtableSlots = 48;
using Vtable = std::array<XbPtr, kVtableSlots>;

// Every slot starts as a trap that names the method, so an unimplemented method used
// by a title is found at once.
template <int Slot>
struct DeviceTrap {
    static void XB_STDCALL call() { vtable_trap("IDirectSound", Slot); }
};
template <int Slot>
struct VoiceTrap {
    static void XB_STDCALL call() { vtable_trap("IDirectSoundBuffer/Stream", Slot); }
};
template <int Slot>
struct MediaTrap {
    static void XB_STDCALL call() { vtable_trap("XFileMediaObject", Slot); }
};

template <template <int> class Trap, int... Slots>
Vtable make_traps(std::integer_sequence<int, Slots...>) {
    return {to_xbox_ptr(reinterpret_cast<void*>(&Trap<Slots>::call))...};
}

// --- Helpers -------------------------------------------------------------------------------

template <typename T>
T* object_impl(XbPtr pointer, uint32_t magic) {
    const auto* object = from_xbox_ptr<const GuestObject>(pointer);
    return object && object->magic == magic ? static_cast<T*>(object->impl) : nullptr;
}

VoiceImpl* voice_of(XbPtr pointer) {
    VoiceImpl* voice = object_impl<VoiceImpl>(pointer, kMagicVoice);
    if (!voice) LOG_WARN("audio", "%08x is not a DirectSound buffer or stream", pointer);
    return voice;
}

float db_gain(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : std::pow(10.0f, hundredths / 2000.0f); }

void to_al(const float in[3], float out[3]) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = -in[2];
}

bool spatial(const VoiceImpl& v) {
    return (v.flags & kCaps3D) && v.voice.format().channels == 1 && v.current.mode != kModeDisable;
}

float distance_gain(const VoiceImpl& v) {
    if (!spatial(v)) return 1.0f;
    const Ds3d& p = v.current;
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = p.position[i] - (p.mode == kModeHeadRelative ? 0.0f : g.listener.position[i]);
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float min = std::max(p.min_distance, 1.0e-4f);
    const float max = std::max(p.max_distance, min);
    if ((v.flags & kCapsMuteAtMaxDistance) && distance >= max) return 0.0f;
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

void update_voice(VoiceImpl& v) {
    if (!v.source) return;
    const Format format = v.voice.format();
    Output::SourceParams params;
    params.gain = db_gain(v.volume) * db_gain(-int32_t(v.headroom)) * distance_gain(v);
    params.pitch = v.frequency && format.rate ? std::clamp(float(v.frequency) / float(format.rate), 1.0f / 64, 16.0f) : 1.0f;
    if (spatial(v)) {
        params.placement = v.current.mode == kModeHeadRelative ? Output::Placement::Relative : Output::Placement::World;
        to_al(v.current.position, params.position);
    } else if (format.channels == 1) {
        params.placement = Output::Placement::Relative;
        params.position[0] = v.pan;
    } else {
        params.placement = Output::Placement::Direct;
    }
    g.output->update(v.source, params);
}

void update_listener() {
    float position[3], velocity[3], at[3], up[3];
    to_al(g.listener.position, position);
    to_al(g.listener.velocity, velocity);
    to_al(g.listener.front, at);
    to_al(g.listener.top, up);
    g.output->set_listener(position, velocity, at, up);
}

// Speakers the voice is routed to decide its panning when it is not 3D.
void set_mix_bins(VoiceImpl& v, XbPtr mix_bins) {
    v.pan = 0.0f;
    const auto* bins = from_xbox_ptr<const XbMixBins>(mix_bins);
    if (!bins || !bins->pairs) return;
    const auto* pairs = from_xbox_ptr<const uint32_t>(bins->pairs);
    bool left = false, right = false;
    for (uint32_t i = 0; i < bins->count; ++i) {
        left |= pairs[2 * i] == 0 || pairs[2 * i] == 4;   // front/back left
        right |= pairs[2 * i] == 1 || pairs[2 * i] == 5;  // front/back right
    }
    if (left != right) v.pan = left ? -1.0f : 1.0f;
}

bool set_format(VoiceImpl& v, XbPtr wave_format) {
    const std::optional<Format> format = parse_wave_format(from_xbox_ptr<const void>(wave_format));
    if (!format) {
        LOG_WARN("audio", "unsupported wave format (tag %04x)",
                 wave_format ? *from_xbox_ptr<const uint16_t>(wave_format) : 0u);
        return false;
    }
    v.voice.stop();
    v.voice.set_format(*format);
    if (v.source) g.output->reformat(v.source);
    return true;
}

// Called with the voice locked, possibly on the mixer thread.
void packet_done(VoiceImpl& v, const Packet& packet, uint32_t status) {
    if (v.callback) {
        std::lock_guard lock(g.callbacks_mutex);
        g.callbacks.push_back({v.callback, {v.context, packet.completion, status}});
    } else if (packet.completion) {
        xbox::set_event_handle(packet.completion);
    }
}

bool ensure_output() {
    if (!g.output) g.output = Output::open(g.config);
    return g.output != nullptr;
}

const Vtable& voice_vtable();

VoiceImpl* create_voice(bool stream, uint32_t flags, XbPtr wave_format, XbPtr mix_bins) {
    auto* v = new VoiceImpl(stream);
    v->guest = {to_xbox_ptr(voice_vtable().data()), kMagicVoice, v};
    v->flags = flags;
    v->voice.on_packet_done = [v](const Packet& packet, uint32_t status) { packet_done(*v, packet, status); };
    v->source = g.output->add_source(v->voice);
    if (wave_format) set_format(*v, wave_format);
    set_mix_bins(*v, mix_bins);
    update_voice(*v);
    g.voices.push_back(v);
    return v;
}

template <typename Change>
uint32_t change_3d(XbPtr self, uint32_t apply, Change change) {
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    if (!v) return kInvalidArg;
    if (apply == kDeferred) {
        if (!v->has_pending) v->pending = v->current;
        change(v->pending);
        v->has_pending = true;
    } else {
        change(v->current);
        update_voice(*v);
    }
    return kOk;
}

template <typename Change>
uint32_t change_voice(XbPtr self, Change change) {
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    if (!v) return kInvalidArg;
    change(*v);
    return kOk;
}

// --- DirectSound ---------------------------------------------------------------------------

uint32_t XB_STDCALL DirectSoundCreate(XbPtr device_guid, XbPtr* direct_sound, XbPtr outer) {
    (void)device_guid, (void)outer;
    std::lock_guard lock(g.mutex);
    if (!ensure_output()) return kFail;
    static const Vtable vtable = make_traps<DeviceTrap>(std::make_integer_sequence<int, kVtableSlots>{});
    g.device = {to_xbox_ptr(vtable.data()), kMagicDevice, nullptr};
    ++g.device_refs;
    *direct_sound = to_xbox_ptr(&g.device);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_Release(XbPtr self) {
    (void)self;
    std::lock_guard lock(g.mutex);
    return g.device_refs > 0 ? static_cast<uint32_t>(--g.device_refs) : 0;
}

uint32_t XB_STDCALL DirectSoundCreateBuffer(const XbDsBufferDesc* desc, XbPtr* buffer) {
    std::lock_guard lock(g.mutex);
    if (!desc || !ensure_output()) return kInvalidArg;
    VoiceImpl* v = create_voice(false, desc->flags, desc->format, desc->mix_bins);
    if (desc->buffer_bytes) {
        v->own_data.assign(desc->buffer_bytes, 0);
        v->voice.set_buffer_data(v->own_data.data(), desc->buffer_bytes);
    }
    *buffer = to_xbox_ptr(&v->guest);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_CreateSoundBuffer(XbPtr self, const XbDsBufferDesc* desc, XbPtr* buffer, XbPtr outer) {
    (void)self, (void)outer;
    return DirectSoundCreateBuffer(desc, buffer);
}

uint32_t XB_STDCALL DirectSoundCreateStream(const XbDsStreamDesc* desc, XbPtr* stream) {
    std::lock_guard lock(g.mutex);
    if (!desc || !ensure_output()) return kInvalidArg;
    VoiceImpl* v = create_voice(true, desc->flags, desc->format, desc->mix_bins);
    v->max_packets = std::max<uint32_t>(desc->max_attached_packets, 1);
    v->callback = desc->callback;
    v->context = desc->context;
    *stream = to_xbox_ptr(&v->guest);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetPosition(XbPtr self, float x, float y, float z, uint32_t apply) {
    (void)self;
    std::lock_guard lock(g.mutex);
    Listener& l = apply == kDeferred ? g.pending_listener : g.listener;
    if (apply == kDeferred && !g.listener_pending) l = g.listener;
    l.position[0] = x, l.position[1] = y, l.position[2] = z;
    g.listener_pending |= apply == kDeferred;
    if (apply != kDeferred) {
        update_listener();
        for (VoiceImpl* v : g.voices) update_voice(*v);
    }
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetVelocity(XbPtr self, float x, float y, float z, uint32_t apply) {
    (void)self;
    std::lock_guard lock(g.mutex);
    Listener& l = apply == kDeferred ? g.pending_listener : g.listener;
    if (apply == kDeferred && !g.listener_pending) l = g.listener;
    l.velocity[0] = x, l.velocity[1] = y, l.velocity[2] = z;
    g.listener_pending |= apply == kDeferred;
    if (apply != kDeferred) update_listener();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetOrientation(XbPtr self, float front_x, float front_y, float front_z, float top_x,
                                                float top_y, float top_z, uint32_t apply) {
    (void)self;
    std::lock_guard lock(g.mutex);
    Listener& l = apply == kDeferred ? g.pending_listener : g.listener;
    if (apply == kDeferred && !g.listener_pending) l = g.listener;
    l.front[0] = front_x, l.front[1] = front_y, l.front[2] = front_z;
    l.top[0] = top_x, l.top[1] = top_y, l.top[2] = top_z;
    g.listener_pending |= apply == kDeferred;
    if (apply != kDeferred) update_listener();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetDistanceFactor(XbPtr self, float meters_per_unit, uint32_t apply) {
    (void)self, (void)meters_per_unit, (void)apply;  // only affects doppler, which is off
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_CommitDeferredSettings(XbPtr self) {
    (void)self;
    std::lock_guard lock(g.mutex);
    if (!g.output) return kOk;
    g.output->begin_batch();
    if (g.listener_pending) {
        g.listener = g.pending_listener;
        g.listener_pending = false;
        update_listener();
    }
    // The listener may have moved, so every voice's distance gain is refreshed.
    for (VoiceImpl* v : g.voices) {
        if (v->has_pending) {
            v->current = v->pending;
            v->has_pending = false;
        }
        update_voice(*v);
    }
    g.output->end_batch();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SynchPlayback(XbPtr self) {
    (void)self;
    std::lock_guard lock(g.mutex);
    if (!g.output) return kOk;
    g.output->begin_batch();
    for (VoiceImpl* v : g.voices) {
        if (!v->synch_play && !v->synch_resume) continue;
        if (v->synch_play) v->voice.play(v->synch_looping, v->synch_from_start);
        if (v->synch_resume) v->voice.set_paused(false);
        v->synch_play = v->synch_resume = false;
        g.output->sync(v->source);
    }
    g.output->end_batch();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_EnableHeadphones(XbPtr self, uint32_t enabled) {
    (void)self;
    std::lock_guard lock(g.mutex);
    if (g.output) g.output->set_hrtf(enabled != 0);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_DownloadEffectsImage(XbPtr self, XbPtr image, uint32_t size, XbPtr image_location,
                                                      XbPtr* image_desc) {
    (void)self, (void)image, (void)size, (void)image_location;
    if (image_desc) *image_desc = to_xbox_ptr(g_empty_effect_image);
    return kOk;
}

uint32_t XB_STDCALL XAudioDownloadEffectsImage(XbPtr image_name, XbPtr image_location, uint32_t flags,
                                               XbPtr* image_desc) {
    (void)image_name, (void)image_location, (void)flags;
    if (image_desc) *image_desc = to_xbox_ptr(g_empty_effect_image);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetEffectData(XbPtr self, uint32_t effect, uint32_t offset, XbPtr data, uint32_t size,
                                               uint32_t apply) {
    (void)self, (void)effect, (void)offset, (void)data, (void)size, (void)apply;
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetMixBinHeadroom(XbPtr self, uint32_t mix_bin, uint32_t headroom) {
    (void)self, (void)mix_bin, (void)headroom;
    return kOk;
}

void XB_STDCALL DirectSoundUseFullHRTF() {}

// Runs stream callbacks; the APU interrupt would have run them.
void XB_STDCALL DirectSoundDoWork() {
    std::deque<Callback> callbacks;
    {
        std::lock_guard lock(g.callbacks_mutex);
        callbacks.swap(g.callbacks);
    }
    const auto call_guest = xbox::kernel_config().call_guest;
    for (const Callback& callback : callbacks)
        if (call_guest) call_guest(callback.routine, callback.args, 3);
}

// --- Formats ---------------------------------------------------------------------------------

// WORD arguments arrive in 4-byte stack slots.
void XB_STDCALL XAudioCreatePcmFormat(uint32_t channels, uint32_t samples_per_second, uint32_t bits, uint8_t* format) {
    const auto block_align = static_cast<uint16_t>((channels & 0xFFFF) * (bits & 0xFFFF) / 8);
    const uint16_t words[] = {1, static_cast<uint16_t>(channels)};
    std::memcpy(format, words, 4);
    const uint32_t rates[] = {samples_per_second, samples_per_second * block_align};
    std::memcpy(format + 4, rates, 8);
    const uint16_t tail[] = {block_align, static_cast<uint16_t>(bits), 0};
    std::memcpy(format + 12, tail, 6);
}

void XB_STDCALL XAudioCreateAdpcmFormat(uint32_t channels, uint32_t samples_per_second, uint8_t* format) {
    const auto block_align = static_cast<uint16_t>(36 * (channels & 0xFFFF));
    const uint16_t words[] = {0x69, static_cast<uint16_t>(channels)};
    std::memcpy(format, words, 4);
    const uint32_t rates[] = {samples_per_second, samples_per_second * block_align / 64};
    std::memcpy(format + 4, rates, 8);
    const uint16_t tail[] = {block_align, 4, 2, 64};  // block align, bits, cbSize, samples per block
    std::memcpy(format + 12, tail, 8);
}

// --- Buffers ---------------------------------------------------------------------------------

uint32_t XB_STDCALL IDirectSoundBuffer_SetFormat(XbPtr self, XbPtr format) {
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    if (!v) return kInvalidArg;
    if (!set_format(*v, format)) return kInvalidArg;
    update_voice(*v);
    return kOk;
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetBufferData(XbPtr self, XbPtr data, uint32_t size) {
    return change_voice(self, [&](VoiceImpl& v) {
        v.voice.set_buffer_data(data ? from_xbox_ptr<const uint8_t>(data) : v.own_data.data(),
                                data ? size : static_cast<uint32_t>(v.own_data.size()));
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetPlayRegion(XbPtr self, uint32_t start, uint32_t length) {
    return change_voice(self, [&](VoiceImpl& v) { v.voice.set_play_region(start, length); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetLoopRegion(XbPtr self, uint32_t start, uint32_t length) {
    return change_voice(self, [&](VoiceImpl& v) { v.voice.set_loop_region(start, length); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetCurrentPosition(XbPtr self, uint32_t offset) {
    return change_voice(self, [&](VoiceImpl& v) { v.voice.set_position(offset); });
}

uint32_t play(XbPtr self, uint32_t flags) {
    return change_voice(self, [&](VoiceImpl& v) {
        const bool looping = flags & kPlayLooping, from_start = flags & kPlayFromStart;
        if (flags & kPlaySynch) {
            v.synch_play = true;
            v.synch_looping = looping;
            v.synch_from_start = from_start;
            return;
        }
        v.voice.play(looping, from_start);
        g.output->sync(v.source);
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Play(XbPtr self, uint32_t reserved1, uint32_t reserved2, uint32_t flags) {
    (void)reserved1, (void)reserved2;
    return play(self, flags);
}

uint32_t XB_STDCALL IDirectSoundBuffer_PlayEx(XbPtr self, int64_t timestamp, uint32_t flags) {
    (void)timestamp;
    return play(self, flags);
}

uint32_t stop(XbPtr self) {
    return change_voice(self, [&](VoiceImpl& v) {
        v.synch_play = v.synch_resume = false;
        v.voice.stop();
        g.output->sync(v.source);
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Stop(XbPtr self) { return stop(self); }

uint32_t XB_STDCALL IDirectSoundBuffer_StopEx(XbPtr self, int64_t timestamp, uint32_t flags) {
    (void)timestamp, (void)flags;  // envelope release: stops at once
    return stop(self);
}

uint32_t pause(XbPtr self, uint32_t flags) {
    return change_voice(self, [&](VoiceImpl& v) {
        if (flags == kPauseSynch) {
            v.synch_resume = true;
            return;
        }
        v.voice.set_paused(flags != kPauseResume);  // PAUSE, or (streams) PAUSENOACTIVATE
        g.output->sync(v.source);
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Pause(XbPtr self, uint32_t flags) { return pause(self, flags); }

uint32_t XB_STDCALL IDirectSoundBuffer_GetStatus(XbPtr self, uint32_t* status) {
    return change_voice(self, [&](VoiceImpl& v) {
        uint32_t s = 0;
        const bool playing = v.voice.playing() || v.synch_play;
        if (playing && !v.voice.paused()) s |= kBufferPlaying;
        if (playing && v.voice.paused()) s |= kBufferPaused;
        if (playing && v.voice.looping()) s |= kBufferLooping;
        *status = s;
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Release(XbPtr self) {
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    if (!v) return 0;
    if (--v->refs == 0) {
        v->voice.stop();
        g.output->sync(v->source);
        // The voice object stays allocated: the mixer may still hold it.
    }
    return static_cast<uint32_t>(std::max(v->refs, 0));
}

// --- Shared buffer/stream parameters --------------------------------------------------------

uint32_t XB_STDCALL Voice_SetVolume(XbPtr self, int32_t volume) {
    return change_voice(self, [&](VoiceImpl& v) {
        v.volume = volume;
        update_voice(v);
    });
}

uint32_t XB_STDCALL Voice_SetHeadroom(XbPtr self, uint32_t headroom) {
    return change_voice(self, [&](VoiceImpl& v) {
        v.headroom = headroom;
        update_voice(v);
    });
}

uint32_t XB_STDCALL Voice_SetFrequency(XbPtr self, uint32_t frequency) {
    return change_voice(self, [&](VoiceImpl& v) {
        v.frequency = frequency;
        update_voice(v);
    });
}

uint32_t XB_STDCALL Voice_SetEG(XbPtr self, XbPtr envelope) {
    (void)self, (void)envelope;  // CoD uses an envelope that is always at full level
    return kOk;
}

uint32_t XB_STDCALL Voice_SetMixBins(XbPtr self, XbPtr mix_bins) {
    return change_voice(self, [&](VoiceImpl& v) {
        set_mix_bins(v, mix_bins);
        update_voice(v);
    });
}

uint32_t XB_STDCALL Voice_SetMixBinVolumes(XbPtr self, XbPtr mix_bins) {
    (void)self, (void)mix_bins;  // per-speaker levels: only used by voice chat
    return kOk;
}

uint32_t XB_STDCALL Voice_SetAllParameters(XbPtr self, const XbDs3dBuffer* p, uint32_t apply) {
    return change_3d(self, apply, [&](Ds3d& d) {
        std::copy(p->position, p->position + 3, d.position);
        std::copy(p->velocity, p->velocity + 3, d.velocity);
        d.min_distance = p->min_distance;
        d.max_distance = p->max_distance;
        d.mode = p->mode;
        d.rolloff_factor = p->rolloff_factor;
    });
}

uint32_t XB_STDCALL Voice_SetMode(XbPtr self, uint32_t mode, uint32_t apply) {
    return change_3d(self, apply, [&](Ds3d& d) { d.mode = mode; });
}

uint32_t XB_STDCALL Voice_SetRolloffCurve(XbPtr self, const float* points, uint32_t count, uint32_t apply) {
    (void)apply;
    return change_voice(self, [&](VoiceImpl& v) {
        v.curve.assign(points, points + (points ? count : 0));
        update_voice(v);
    });
}

// --- Streams ---------------------------------------------------------------------------------

uint32_t XB_STDCALL IDirectSoundStream_SetFormat(XbPtr self, XbPtr format) { return IDirectSoundBuffer_SetFormat(self, format); }
uint32_t XB_STDCALL IDirectSoundStream_Pause(XbPtr self, uint32_t flags) { return pause(self, flags); }

uint32_t XB_STDCALL IDirectSoundStream_FlushEx(XbPtr self, int64_t timestamp, uint32_t flags) {
    (void)timestamp, (void)flags;
    return stop(self);
}

// XMediaObject methods, called through the vtable.
uint32_t XB_STDCALL Stream_AddRef(XbPtr self) {
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    return v ? static_cast<uint32_t>(++v->refs) : 0;
}

uint32_t XB_STDCALL Stream_Release(XbPtr self) { return IDirectSoundBuffer_Release(self); }

uint32_t XB_STDCALL Stream_GetInfo(XbPtr self, XbMediaInfo* info) {
    return change_voice(self, [&](VoiceImpl& v) {
        *info = {0x1 /* XMO_STREAMF_FIXED_SAMPLE_SIZE */, v.voice.format().block_align, 0, 0};
    });
}

uint32_t XB_STDCALL Stream_GetStatus(XbPtr self, uint32_t* status) {
    return change_voice(self, [&](VoiceImpl& v) {
        uint32_t s = 0;
        if (v.voice.queued_packets() < v.max_packets) s |= kStreamReady;
        if (v.voice.playing() && !v.voice.paused()) s |= kStreamPlaying;
        if (v.voice.paused()) s |= kStreamPaused;
        if (v.voice.starved()) s |= kStreamStarved;
        *status = s;
    });
}

uint32_t XB_STDCALL Stream_Process(XbPtr self, const XbMediaPacket* input, const XbMediaPacket* output) {
    (void)output;
    std::lock_guard lock(g.mutex);
    VoiceImpl* v = voice_of(self);
    if (!v || !input) return kInvalidArg;
    Packet packet;
    packet.data = from_xbox_ptr<const uint8_t>(input->buffer);
    packet.size = input->max_size;
    packet.status = from_xbox_ptr<uint32_t>(input->status);
    packet.completed = from_xbox_ptr<uint32_t>(input->completed_size);
    packet.completion = input->completion;
    if (!v->voice.submit(packet, v->max_packets)) return kFail;
    g.output->sync(v->source);  // streams start with their first packet
    return kOk;
}

uint32_t XB_STDCALL Stream_Discontinuity(XbPtr self) {
    return change_voice(self, [&](VoiceImpl& v) { v.voice.discontinuity(); });
}

uint32_t XB_STDCALL Stream_Flush(XbPtr self) { return stop(self); }

const Vtable& voice_vtable() {
    static const Vtable vtable = [] {
        Vtable table = make_traps<VoiceTrap>(std::make_integer_sequence<int, kVtableSlots>{});
        table[0] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_AddRef));
        table[1] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_Release));
        table[2] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_GetInfo));
        table[3] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_GetStatus));
        table[4] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_Process));
        table[5] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_Discontinuity));
        table[6] = to_xbox_ptr(reinterpret_cast<void*>(&Stream_Flush));
        return table;
    }();
    return vtable;
}

// --- File media objects ----------------------------------------------------------------------
//
// XFileCreateMediaObjectAsync: reads a file into the title's packets. Reads complete
// at once (the status is set before Process returns).

struct MediaImpl {
    GuestObject guest{};
    int refs = 1;
    std::shared_ptr<platform::File> file;
    uint64_t position = 0;
};

MediaImpl* media_of(XbPtr pointer) { return object_impl<MediaImpl>(pointer, kMagicMedia); }

uint32_t XB_STDCALL Media_AddRef(XbPtr self) {
    std::lock_guard lock(g.mutex);
    MediaImpl* m = media_of(self);
    return m ? static_cast<uint32_t>(++m->refs) : 0;
}

uint32_t XB_STDCALL Media_Release(XbPtr self) {
    std::lock_guard lock(g.mutex);
    MediaImpl* m = media_of(self);
    if (!m) return 0;
    const int refs = --m->refs;
    if (refs == 0) {
        // The caller's file handle stays open: CoD2: BRO creates every stream from one
        // handle to its sound archive. The object keeps its own reference to the file.
        m->guest.magic = 0;
        delete m;
    }
    return static_cast<uint32_t>(std::max(refs, 0));
}

uint32_t XB_STDCALL Media_GetInfo(XbPtr self, XbMediaInfo* info) {
    (void)self;
    *info = {0, 0, 0, 0};
    return kOk;
}

uint32_t XB_STDCALL Media_GetStatus(XbPtr self, uint32_t* status) {
    (void)self;
    *status = kMediaAcceptOutput;
    return kOk;
}

uint32_t XB_STDCALL Media_Process(XbPtr self, const XbMediaPacket* input, const XbMediaPacket* output) {
    (void)input;
    std::lock_guard lock(g.mutex);
    MediaImpl* m = media_of(self);
    if (!m || !output) return kInvalidArg;
    uint32_t done = 0;
    std::error_code error;
    const bool ok = m->file->read_at(m->position, from_xbox_ptr<void>(output->buffer), output->max_size, done, error);
    m->position += done;
    if (output->completed_size) *from_xbox_ptr<uint32_t>(output->completed_size) = done;
    if (output->status) *from_xbox_ptr<uint32_t>(output->status) = ok ? kPacketSuccess : kFail;
    xbox::set_event_handle(output->completion);
    return ok ? kOk : kFail;
}

uint32_t XB_STDCALL Media_Nothing(XbPtr self) {  // Discontinuity, Flush, DoWork
    (void)self;
    return kOk;
}

uint32_t XB_STDCALL Media_Seek(XbPtr self, int32_t offset, uint32_t origin, uint32_t* absolute) {
    std::lock_guard lock(g.mutex);
    MediaImpl* m = media_of(self);
    if (!m) return kInvalidArg;
    const int64_t base = origin == 1 ? int64_t(m->position) : origin == 2 ? int64_t(m->file->size().value_or(0)) : 0;
    m->position = static_cast<uint64_t>(std::max<int64_t>(0, base + offset));
    if (absolute) *absolute = static_cast<uint32_t>(m->position);
    return kOk;
}

uint32_t XB_STDCALL Media_GetLength(XbPtr self, uint32_t* length) {
    std::lock_guard lock(g.mutex);
    MediaImpl* m = media_of(self);
    if (!m) return kInvalidArg;
    *length = static_cast<uint32_t>(m->file->size().value_or(0));
    return kOk;
}

const Vtable& media_vtable() {
    static const Vtable vtable = [] {
        Vtable table = make_traps<MediaTrap>(std::make_integer_sequence<int, kVtableSlots>{});
        const void* methods[] = {
            reinterpret_cast<void*>(&Media_AddRef),  reinterpret_cast<void*>(&Media_Release),
            reinterpret_cast<void*>(&Media_GetInfo), reinterpret_cast<void*>(&Media_GetStatus),
            reinterpret_cast<void*>(&Media_Process), reinterpret_cast<void*>(&Media_Nothing),  // Discontinuity
            reinterpret_cast<void*>(&Media_Nothing),                                             // Flush
            reinterpret_cast<void*>(&Media_Seek),    reinterpret_cast<void*>(&Media_GetLength),
            reinterpret_cast<void*>(&Media_Nothing),  // DoWork
        };
        for (size_t i = 0; i < std::size(methods); ++i) table[i] = to_xbox_ptr(methods[i]);
        return table;
    }();
    return vtable;
}

uint32_t XB_STDCALL XFileCreateMediaObjectAsync(Handle file, uint32_t max_packets, XbPtr* media_object) {
    (void)max_packets;
    std::shared_ptr<platform::File> host = xbox::host_file(file);
    if (!host) {
        LOG_WARN("audio", "XFileCreateMediaObjectAsync: handle %08x is not an open file", file);
        return kInvalidArg;
    }
    std::lock_guard lock(g.mutex);
    auto* m = new MediaImpl();
    m->guest = {to_xbox_ptr(media_vtable().data()), kMagicMedia, m};
    m->file = std::move(host);
    *media_object = to_xbox_ptr(&m->guest);
    return kOk;
}

// --- The table -------------------------------------------------------------------------------

#define DS(name) {#name, reinterpret_cast<void*>(&name)}
#define DS_AS(name, function) {#name, reinterpret_cast<void*>(&function)}

const xbox::KernelImpl kFunctions[] = {
    DS(DirectSoundCreate),
    DS(DirectSoundCreateBuffer),
    DS(DirectSoundCreateStream),
    DS(DirectSoundDoWork),
    DS(DirectSoundUseFullHRTF),
    DS(IDirectSound_CommitDeferredSettings),
    DS(IDirectSound_CreateSoundBuffer),
    DS(IDirectSound_DownloadEffectsImage),
    DS(IDirectSound_EnableHeadphones),
    DS(IDirectSound_Release),
    DS(IDirectSound_SetDistanceFactor),
    DS(IDirectSound_SetEffectData),
    DS(IDirectSound_SetMixBinHeadroom),
    DS(IDirectSound_SetOrientation),
    DS(IDirectSound_SetPosition),
    DS(IDirectSound_SetVelocity),
    DS(IDirectSound_SynchPlayback),
    DS(IDirectSoundBuffer_GetStatus),
    DS(IDirectSoundBuffer_Pause),
    DS(IDirectSoundBuffer_Play),
    DS(IDirectSoundBuffer_PlayEx),
    DS(IDirectSoundBuffer_Release),
    DS_AS(IDirectSoundBuffer_SetAllParameters, Voice_SetAllParameters),
    DS(IDirectSoundBuffer_SetBufferData),
    DS(IDirectSoundBuffer_SetCurrentPosition),
    DS_AS(IDirectSoundBuffer_SetEG, Voice_SetEG),
    DS(IDirectSoundBuffer_SetFormat),
    DS_AS(IDirectSoundBuffer_SetFrequency, Voice_SetFrequency),
    DS_AS(IDirectSoundBuffer_SetHeadroom, Voice_SetHeadroom),
    DS(IDirectSoundBuffer_SetLoopRegion),
    DS_AS(IDirectSoundBuffer_SetMixBins, Voice_SetMixBins),
    DS_AS(IDirectSoundBuffer_SetMode, Voice_SetMode),
    DS(IDirectSoundBuffer_SetPlayRegion),
    DS_AS(IDirectSoundBuffer_SetRolloffCurve, Voice_SetRolloffCurve),
    DS_AS(IDirectSoundBuffer_SetVolume, Voice_SetVolume),
    DS(IDirectSoundBuffer_Stop),
    DS(IDirectSoundBuffer_StopEx),
    DS(IDirectSoundStream_FlushEx),
    DS(IDirectSoundStream_Pause),
    DS_AS(IDirectSoundStream_SetAllParameters, Voice_SetAllParameters),
    DS_AS(IDirectSoundStream_SetEG, Voice_SetEG),
    DS(IDirectSoundStream_SetFormat),
    DS_AS(IDirectSoundStream_SetFrequency, Voice_SetFrequency),
    DS_AS(IDirectSoundStream_SetHeadroom, Voice_SetHeadroom),
    DS_AS(IDirectSoundStream_SetMixBinVolumes_8, Voice_SetMixBinVolumes),
    DS_AS(IDirectSoundStream_SetMixBins, Voice_SetMixBins),
    DS_AS(IDirectSoundStream_SetMode, Voice_SetMode),
    DS_AS(IDirectSoundStream_SetRolloffCurve, Voice_SetRolloffCurve),
    DS_AS(IDirectSoundStream_SetVolume, Voice_SetVolume),
    DS(XAudioCreateAdpcmFormat),
    DS(XAudioCreatePcmFormat),
    DS(XAudioDownloadEffectsImage),
    DS(XFileCreateMediaObjectAsync),
};

}  // namespace

void configure(const OutputConfig& config) {
    std::lock_guard lock(g.mutex);
    g.config = config;
}

std::span<const xbox::KernelImpl> dsound_functions() { return kFunctions; }

void set_paused(bool paused) {
    if (g.output) g.output->set_paused(paused);
}

Output* output() {
    std::lock_guard lock(g.mutex);
    return g.output.get();
}

}  // namespace audio
