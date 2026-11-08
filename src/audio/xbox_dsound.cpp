// Xbox DirectSound replacement. See xbox_dsound.h.
//
// A translation onto our own sound device (sound_device.h): DirectSound's units and objects
// become the device's. Semantics follow the XDK and what CoD2: BRO relies on:
// - Volumes and headroom are in hundredths of a dB (attenuation); frequency in Hz
//   (0 = the format's rate).
// - 3D settings may be deferred (apply = DS3D_DEFERRED) until CommitDeferredSettings.
// - Play/Pause with the SYNCHPLAYBACK flag wait for IDirectSound_SynchPlayback.
// - Effects (DSP images, effect data, mix bin headroom) are accepted and ignored; a voice
//   routed to the I3DL2 mix bin has its reverb send on.
// - Buffer and stream objects are the device's voices (a voice id is the object's address).

#include "xbox_dsound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "compiler.h"
#include "dispatcher.h"
#include "filesystem.h"
#include "log.h"
#include "objects.h"
#include "test_mode.h"
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
constexpr uint32_t kMagicDevice = 0x444E5344, kMagicMedia = 0x4F4D4658;
constexpr uint32_t kMixBinI3dl2 = 10;  // DSMIXBIN_I3DL2

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

SoundDevice& dev() { return sound_device(); }

struct DeviceObject {
    GuestObject guest{};
    int refs = 0;
};
DeviceObject g_device;
std::mutex g_device_mutex;
std::mutex g_media_mutex;  // the file media objects

// DSEFFECTIMAGEDESC for titles that look at it: no effects.
uint32_t g_empty_effect_image[64] = {};

// --- Vtables -------------------------------------------------------------------------------

[[noreturn]] void vtable_trap(const char* type, int slot) {
    if (!in_test()) LOG_ERROR("audio", "%s method #%d called through its vtable: not implemented", type, slot);
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

// A buffer or stream: the engine's voice id is the object's address.
VoiceId voice_of(XbPtr pointer) {
    const VoiceId voice = voice_from_object(pointer);
    if (!voice && !in_test()) LOG_WARN("audio", "%08x is not a DirectSound buffer or stream", pointer);
    return voice;
}

float db_gain(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : std::pow(10.0f, hundredths / 2000.0f); }

// Speakers the voice is routed to: its panning when it is not 3D, and the I3DL2 bin (10) is
// the reverb send.
void set_mix_bins(VoiceId voice, XbPtr mix_bins) {
    float pan = 0.0f;
    bool reverb = false;
    const auto* bins = from_xbox_ptr<const XbMixBins>(mix_bins);
    if (bins && bins->pairs) {
        const auto* pairs = from_xbox_ptr<const uint32_t>(bins->pairs);
        bool left = false, right = false;
        for (uint32_t i = 0; i < bins->count; ++i) {
            left |= pairs[2 * i] == 0 || pairs[2 * i] == 4;   // front/back left
            right |= pairs[2 * i] == 1 || pairs[2 * i] == 5;  // front/back right
            reverb |= pairs[2 * i] == kMixBinI3dl2;
        }
        if (left != right) pan = left ? -1.0f : 1.0f;
    }
    dev().set_pan(voice, pan);
    dev().set_reverb_send(voice, reverb);
}

std::optional<Format> wave_format(XbPtr pointer) {
    const std::optional<Format> format = parse_wave_format(from_xbox_ptr<const void>(pointer));
    if (!format && !in_test())
        LOG_WARN("audio", "unsupported wave format (tag %04x)", pointer ? *from_xbox_ptr<const uint16_t>(pointer) : 0u);
    return format;
}

const Vtable& voice_vtable();

void ensure_vtable() {
    static const bool done = [] {
        set_voice_vtable(to_xbox_ptr(voice_vtable().data()));
        return true;
    }();
    (void)done;
}

VoiceId create_voice(bool stream, uint32_t flags, XbPtr format_pointer, XbPtr mix_bins, uint32_t max_packets,
                     uint32_t buffer_bytes) {
    ensure_vtable();
    VoiceDesc desc;
    desc.stream = stream;
    desc.spatial = flags & kCaps3D;
    desc.mute_at_max_distance = flags & kCapsMuteAtMaxDistance;
    desc.max_packets = max_packets;
    desc.buffer_bytes = buffer_bytes;
    const std::optional<Format> format = format_pointer ? wave_format(format_pointer) : std::nullopt;
    const VoiceId voice = dev().create_voice(desc, format ? &*format : nullptr);
    if (voice) set_mix_bins(voice, mix_bins);
    return voice;
}

Spatial to_spatial(const XbDs3dBuffer& p) {
    Spatial s;
    std::copy(p.position, p.position + 3, s.position);
    std::copy(p.velocity, p.velocity + 3, s.velocity);
    s.min_distance = p.min_distance;
    s.max_distance = p.max_distance;
    s.rolloff_factor = p.rolloff_factor;
    s.doppler_factor = p.doppler_factor;
    s.mode = p.mode == kModeHeadRelative ? SpatialMode::HeadRelative
           : p.mode == kModeDisable      ? SpatialMode::Off
                                         : SpatialMode::World;
    return s;
}

Apply apply_of(uint32_t apply) { return apply == kDeferred ? Apply::Deferred : Apply::Now; }

template <typename Change>
uint32_t change_listener(uint32_t apply, Change change) {
    Listener listener = dev().listener(apply_of(apply));
    change(listener);
    dev().set_listener(listener, apply_of(apply));
    return kOk;
}

// --- DirectSound ---------------------------------------------------------------------------

uint32_t XB_STDCALL DirectSoundCreate(XbPtr device_guid, XbPtr* direct_sound, XbPtr outer) {
    (void)device_guid, (void)outer;
    ensure_vtable();
    if (!dev().open()) return kFail;
    Guard lock(g_device_mutex, TestLock::State);
    static const Vtable vtable = make_traps<DeviceTrap>(std::make_integer_sequence<int, kVtableSlots>{});
    g_device.guest = {to_xbox_ptr(vtable.data()), kMagicDevice, nullptr};
    ++g_device.refs;
    *direct_sound = to_xbox_ptr(&g_device.guest);
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_Release(XbPtr self) {
    (void)self;
    Guard lock(g_device_mutex, TestLock::State);
    return g_device.refs > 0 ? static_cast<uint32_t>(--g_device.refs) : 0;
}

uint32_t XB_STDCALL DirectSoundCreateBuffer(const XbDsBufferDesc* desc, XbPtr* buffer) {
    if (!desc || !dev().open()) return kInvalidArg;
    const VoiceId voice = create_voice(false, desc->flags, desc->format, desc->mix_bins, 0, desc->buffer_bytes);
    if (!voice) return kInvalidArg;
    *buffer = voice;
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_CreateSoundBuffer(XbPtr self, const XbDsBufferDesc* desc, XbPtr* buffer, XbPtr outer) {
    (void)self, (void)outer;
    return DirectSoundCreateBuffer(desc, buffer);
}

uint32_t XB_STDCALL DirectSoundCreateStream(const XbDsStreamDesc* desc, XbPtr* stream) {
    if (!desc || !dev().open()) return kInvalidArg;
    const VoiceId voice = create_voice(true, desc->flags, desc->format, desc->mix_bins,
                                       std::max<uint32_t>(desc->max_attached_packets, 1), 0);
    if (!voice) return kInvalidArg;
    if (desc->callback) dev().set_packet_callback(voice, desc->callback, desc->context);
    *stream = voice;
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SetPosition(XbPtr self, float x, float y, float z, uint32_t apply) {
    (void)self;
    return change_listener(apply, [&](Listener& l) { l.position[0] = x, l.position[1] = y, l.position[2] = z; });
}

uint32_t XB_STDCALL IDirectSound_SetVelocity(XbPtr self, float x, float y, float z, uint32_t apply) {
    (void)self;
    return change_listener(apply, [&](Listener& l) { l.velocity[0] = x, l.velocity[1] = y, l.velocity[2] = z; });
}

uint32_t XB_STDCALL IDirectSound_SetOrientation(XbPtr self, float front_x, float front_y, float front_z, float top_x,
                                                float top_y, float top_z, uint32_t apply) {
    (void)self;
    return change_listener(apply, [&](Listener& l) {
        l.front[0] = front_x, l.front[1] = front_y, l.front[2] = front_z;
        l.top[0] = top_x, l.top[1] = top_y, l.top[2] = top_z;
    });
}

uint32_t XB_STDCALL IDirectSound_SetDistanceFactor(XbPtr self, float meters_per_unit, uint32_t apply) {
    (void)self, (void)meters_per_unit, (void)apply;  // only affects doppler, which is off
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_CommitDeferredSettings(XbPtr self) {
    (void)self;
    dev().commit();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_SynchPlayback(XbPtr self) {
    (void)self;
    dev().start_synched();
    return kOk;
}

uint32_t XB_STDCALL IDirectSound_EnableHeadphones(XbPtr self, uint32_t enabled) {
    (void)self;
    dev().set_headphones(enabled != 0);
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

// Void in the SDK; EAX = 0 here, so that it is the same in every call (the comparison
// harness logs it).
uint32_t XB_STDCALL DirectSoundUseFullHRTF() { return kOk; }

// Runs stream callbacks.
uint32_t XB_STDCALL DirectSoundDoWork() {  // void in the SDK
    dev().do_work();
    return kOk;
}

// --- Formats ---------------------------------------------------------------------------------

// WORD arguments arrive in 4-byte stack slots.
uint32_t XB_STDCALL XAudioCreatePcmFormat(uint32_t channels, uint32_t samples_per_second, uint32_t bits,
                                          uint8_t* format) {  // void in the SDK
    const auto block_align = static_cast<uint16_t>((channels & 0xFFFF) * (bits & 0xFFFF) / 8);
    const uint16_t words[] = {1, static_cast<uint16_t>(channels)};
    std::memcpy(format, words, 4);
    const uint32_t rates[] = {samples_per_second, samples_per_second * block_align};
    std::memcpy(format + 4, rates, 8);
    const uint16_t tail[] = {block_align, static_cast<uint16_t>(bits), 0};
    std::memcpy(format + 12, tail, 6);
    return kOk;
}

uint32_t XB_STDCALL XAudioCreateAdpcmFormat(uint32_t channels, uint32_t samples_per_second,
                                            uint8_t* format) {  // void in the SDK
    const auto block_align = static_cast<uint16_t>(36 * (channels & 0xFFFF));
    const uint16_t words[] = {0x69, static_cast<uint16_t>(channels)};
    std::memcpy(format, words, 4);
    const uint32_t rates[] = {samples_per_second, samples_per_second * block_align / 64};
    std::memcpy(format + 4, rates, 8);
    const uint16_t tail[] = {block_align, 4, 2, 64};  // block align, bits, cbSize, samples per block
    std::memcpy(format + 12, tail, 8);
    return kOk;
}

// --- Buffers ---------------------------------------------------------------------------------

uint32_t XB_STDCALL IDirectSoundBuffer_SetFormat(XbPtr self, XbPtr format_pointer) {
    const VoiceId voice = voice_of(self);
    const std::optional<Format> format = wave_format(format_pointer);
    if (!voice || !format) return kInvalidArg;
    return dev().set_format(voice, *format) ? kOk : kInvalidArg;
}

// Runs `call` on the voice behind `self`.
template <typename Call>
uint32_t with_voice(XbPtr self, Call call) {
    const VoiceId voice = voice_of(self);
    if (!voice) return kInvalidArg;
    call(voice);
    return kOk;
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetBufferData(XbPtr self, XbPtr data, uint32_t size) {
    return with_voice(self, [&](VoiceId v) { dev().set_buffer_data(v, from_xbox_ptr<const uint8_t>(data), size); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetPlayRegion(XbPtr self, uint32_t start, uint32_t length) {
    return with_voice(self, [&](VoiceId v) { dev().set_play_region(v, start, length); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetLoopRegion(XbPtr self, uint32_t start, uint32_t length) {
    return with_voice(self, [&](VoiceId v) { dev().set_loop_region(v, start, length); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_SetCurrentPosition(XbPtr self, uint32_t offset) {
    return with_voice(self, [&](VoiceId v) { dev().set_position(v, offset); });
}

uint32_t play(XbPtr self, uint32_t flags) {
    return with_voice(self, [&](VoiceId v) {
        dev().play(v, flags & kPlayLooping, flags & kPlayFromStart, flags & kPlaySynch ? Start::WithSynch : Start::Now);
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
    return with_voice(self, [](VoiceId v) { dev().stop(v); });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Stop(XbPtr self) { return stop(self); }

uint32_t XB_STDCALL IDirectSoundBuffer_StopEx(XbPtr self, int64_t timestamp, uint32_t flags) {
    (void)timestamp, (void)flags;  // envelope release: stops at once
    return stop(self);
}

uint32_t pause(XbPtr self, uint32_t flags) {
    return with_voice(self, [&](VoiceId v) {
        if (flags == kPauseSynch)
            dev().resume(v, Start::WithSynch);
        else if (flags == kPauseResume)
            dev().resume(v, Start::Now);
        else
            dev().pause(v);  // PAUSE, or (streams) PAUSENOACTIVATE
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Pause(XbPtr self, uint32_t flags) { return pause(self, flags); }

uint32_t XB_STDCALL IDirectSoundBuffer_GetStatus(XbPtr self, uint32_t* status) {
    return with_voice(self, [&](VoiceId v) {
        const VoiceStatus s = dev().status(v);
        uint32_t bits = 0;
        if (s.playing && !s.paused) bits |= kBufferPlaying;
        if (s.playing && s.paused) bits |= kBufferPaused;
        if (s.looping) bits |= kBufferLooping;
        *status = bits;
    });
}

uint32_t XB_STDCALL IDirectSoundBuffer_Release(XbPtr self) {
    const VoiceId voice = voice_of(self);
    return voice ? dev().release(voice) : 0;
}

// --- Shared buffer/stream parameters --------------------------------------------------------

uint32_t XB_STDCALL Voice_SetVolume(XbPtr self, int32_t volume) {
    return with_voice(self, [&](VoiceId v) { dev().set_volume(v, db_gain(volume)); });
}

uint32_t XB_STDCALL Voice_SetHeadroom(XbPtr self, uint32_t headroom) {
    return with_voice(self, [&](VoiceId v) { dev().set_headroom(v, db_gain(-int32_t(headroom))); });
}

uint32_t XB_STDCALL Voice_SetFrequency(XbPtr self, uint32_t frequency) {
    return with_voice(self, [&](VoiceId v) { dev().set_rate(v, frequency); });
}

uint32_t XB_STDCALL Voice_SetEG(XbPtr self, XbPtr envelope) {
    (void)self, (void)envelope;  // CoD uses an envelope that is always at full level
    return kOk;
}

uint32_t XB_STDCALL Voice_SetMixBins(XbPtr self, XbPtr mix_bins) {
    return with_voice(self, [&](VoiceId v) { set_mix_bins(v, mix_bins); });
}

uint32_t XB_STDCALL Voice_SetMixBinVolumes(XbPtr self, XbPtr mix_bins) {
    (void)self, (void)mix_bins;  // per-speaker levels: only used by voice chat
    return kOk;
}

uint32_t XB_STDCALL Voice_SetAllParameters(XbPtr self, const XbDs3dBuffer* p, uint32_t apply) {
    return with_voice(self, [&](VoiceId v) { dev().set_spatial(v, to_spatial(*p), apply_of(apply)); });
}

uint32_t XB_STDCALL Voice_SetMode(XbPtr self, uint32_t mode, uint32_t apply) {
    const SpatialMode spatial = mode == kModeHeadRelative ? SpatialMode::HeadRelative
                              : mode == kModeDisable      ? SpatialMode::Off
                                                          : SpatialMode::World;
    return with_voice(self, [&](VoiceId v) { dev().set_spatial_mode(v, spatial, apply_of(apply)); });
}

uint32_t XB_STDCALL Voice_SetRolloffCurve(XbPtr self, const float* points, uint32_t count, uint32_t apply) {
    (void)apply;
    return with_voice(self, [&](VoiceId v) { dev().set_distance_curve(v, points, count); });
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
    const VoiceId voice = voice_of(self);
    return voice ? dev().add_ref(voice) : 0;
}

uint32_t XB_STDCALL Stream_Release(XbPtr self) { return IDirectSoundBuffer_Release(self); }

uint32_t XB_STDCALL Stream_GetInfo(XbPtr self, XbMediaInfo* info) {
    (void)self;
    *info = {0x1 /* XMO_STREAMF_FIXED_SAMPLE_SIZE */, 0, 0, 0};
    return kOk;
}

uint32_t XB_STDCALL Stream_GetStatus(XbPtr self, uint32_t* status) {
    return with_voice(self, [&](VoiceId v) {
        const VoiceStatus s = dev().status(v);
        uint32_t bits = 0;
        if (s.accepts_packets) bits |= kStreamReady;
        if (s.playing && !s.paused) bits |= kStreamPlaying;
        if (s.paused) bits |= kStreamPaused;
        if (s.starved) bits |= kStreamStarved;
        *status = bits;
    });
}

uint32_t XB_STDCALL Stream_Process(XbPtr self, const XbMediaPacket* input, const XbMediaPacket* output) {
    (void)output;
    const VoiceId voice = voice_of(self);
    if (!voice || !input) return kInvalidArg;
    Packet packet;
    packet.data = from_xbox_ptr<const uint8_t>(input->buffer);
    packet.size = input->max_size;
    packet.status = from_xbox_ptr<uint32_t>(input->status);
    packet.completed = from_xbox_ptr<uint32_t>(input->completed_size);
    packet.completion = input->completion;
    return dev().submit(voice, packet) ? kOk : kFail;
}

uint32_t XB_STDCALL Stream_Discontinuity(XbPtr self) {
    return with_voice(self, [](VoiceId v) { dev().end_stream(v); });
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
    Guard lock(g_media_mutex, TestLock::State);
    MediaImpl* m = media_of(self);
    return m ? static_cast<uint32_t>(++m->refs) : 0;
}

uint32_t XB_STDCALL Media_Release(XbPtr self) {
    Guard lock(g_media_mutex, TestLock::State);
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
    Guard lock(g_media_mutex, TestLock::State);
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
    Guard lock(g_media_mutex, TestLock::State);
    MediaImpl* m = media_of(self);
    if (!m) return kInvalidArg;
    const int64_t base = origin == 1 ? int64_t(m->position) : origin == 2 ? int64_t(m->file->size().value_or(0)) : 0;
    m->position = static_cast<uint64_t>(std::max<int64_t>(0, base + offset));
    if (absolute) *absolute = static_cast<uint32_t>(m->position);
    return kOk;
}

uint32_t XB_STDCALL Media_GetLength(XbPtr self, uint32_t* length) {
    Guard lock(g_media_mutex, TestLock::State);
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
        if (!in_test())
            LOG_WARN("audio", "XFileCreateMediaObjectAsync: handle %08x is not an open file", file);
        return kInvalidArg;
    }
    Guard lock(g_media_mutex, TestLock::State);
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

// Methods titles call through the vtables (not by SDK name): the comparison harness runs
// them in its test processes like the functions above.
const xbox::KernelImpl kMethods[] = {
    DS(Stream_AddRef),     DS(Stream_Release),       DS(Stream_GetInfo), DS(Stream_GetStatus),
    DS(Stream_Process),    DS(Stream_Discontinuity), DS(Stream_Flush),   DS(Media_AddRef),
    DS(Media_Release),     DS(Media_GetInfo),        DS(Media_GetStatus), DS(Media_Process),
    DS(Media_Nothing),     DS(Media_Seek),           DS(Media_GetLength),
};

}  // namespace

TestMode g_test_mode;

void set_test_mode(const volatile uint8_t* in_test, void (*lock_held)(TestLock which)) {
    g_test_mode = {in_test, lock_held};
}

std::span<const xbox::KernelImpl> dsound_methods() { return kMethods; }

std::span<const xbox::KernelImpl> dsound_functions() {
    static const bool registered = [] {
        add_fork_lock(g_device_mutex);
        add_fork_lock(g_media_mutex);
        ensure_vtable();
        return true;
    }();
    (void)registered;
    return kFunctions;
}

}  // namespace audio
