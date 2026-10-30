#include "output.h"

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utility>
#include <cstring>
#include <vector>

#include "log.h"

namespace audio {

class Output::Source {
public:
    explicit Source(Voice& voice) : voice(voice) {}
    Voice& voice;
    ALuint source = 0;
    ALuint buffer = 0;
    uint32_t channels = 0;  // of the current callback buffer
    ALuint filter = 0;      // the direct sound's low-pass (EFX), made when first wanted
    bool filtered = false;  // attached
};

namespace {

ALenum al_format(uint32_t channels) {
    switch (channels) {
        case 1: return AL_FORMAT_MONO16;
        case 2: return AL_FORMAT_STEREO16;
        case 4: return AL_FORMAT_QUAD16;
        case 6: return AL_FORMAT_51CHN16;
        default: return 0;
    }
}

// Runs on OpenAL's mixer thread.
ALsizei AL_APIENTRY render_callback(void* user, void* data, ALsizei bytes) noexcept {
    auto& source = *static_cast<Output::Source*>(user);
    const size_t frame_bytes = size_t{source.channels} * sizeof(int16_t);
    const size_t produced = source.voice.render(static_cast<int16_t*>(data), static_cast<size_t>(bytes) / frame_bytes);
    return static_cast<ALsizei>(produced * frame_bytes);  // short: the source stops after this
}

using Speakers = OutputConfig::Speakers;

// The context's attributes for a speaker layout and, with Speakers::Auto, the game's
// headphone choice (-1: none yet, so OpenAL decides). `extra` comes first (loopback format).
std::vector<ALCint> context_attributes(Speakers speakers, int hrtf, std::vector<ALCint> extra = {}) {
    std::vector<ALCint> a = std::move(extra);
    ALCint mode = 0;
    switch (speakers) {
        case Speakers::Auto: break;
        case Speakers::Stereo: mode = ALC_STEREO_BASIC_SOFT; break;
        case Speakers::Headphones: mode = ALC_STEREO_HRTF_SOFT; break;
        case Speakers::Quad: mode = ALC_QUAD_SOFT; break;
        case Speakers::Surround51: mode = ALC_SURROUND_5_1_SOFT; break;
        case Speakers::Surround71: mode = ALC_SURROUND_7_1_SOFT; break;
    }
    if (mode) {
        a.insert(a.end(), {ALC_OUTPUT_MODE_SOFT, mode, ALC_HRTF_SOFT, speakers == Speakers::Headphones});
    } else if (hrtf >= 0) {
        a.insert(a.end(), {ALC_HRTF_SOFT, hrtf ? ALC_TRUE : ALC_FALSE});
    }
    // Room for enhanced sound's voices (OpenAL Soft's default is 256 sources in all; the game has up to 246 slots
    // + the movie + voice chat).
    a.insert(a.end(), {ALC_MONO_SOURCES, 480, ALC_STEREO_SOURCES, 32});
    a.push_back(0);
    return a;
}

const char* mode_name(ALCint mode) {
    switch (mode) {
        case ALC_MONO_SOFT: return "mono";
        case ALC_STEREO_SOFT: return "stereo";
        case ALC_STEREO_BASIC_SOFT: return "stereo";
        case ALC_STEREO_UHJ_SOFT: return "stereo (UHJ)";
        case ALC_STEREO_HRTF_SOFT: return "headphones (HRTF)";
        case ALC_QUAD_SOFT: return "quad";
        case ALC_SURROUND_5_1_SOFT: return "5.1";
        case ALC_SURROUND_6_1_SOFT: return "6.1";
        case ALC_SURROUND_7_1_SOFT: return "7.1";
        default: return "other";
    }
}

// Enhanced: OpenAL Soft's best resampler (its default is a cubic spline).
int best_resampler() {
    if (!alIsExtensionPresent("AL_SOFT_source_resampler")) return -1;
    const ALint count = alGetInteger(AL_NUM_RESAMPLERS_SOFT);
    for (ALint i = 0; i < count; ++i)
        if (const ALchar* name = alGetStringiSOFT(AL_RESAMPLER_NAME_SOFT, i); name && !std::strcmp(name, "23rd order Sinc"))
            return i;
    return count > 0 ? count - 1 : -1;  // the list goes from worst to best
}

// An OpenAL error raised since the last check, logged with `what` (each call site and error
// at most 10 times). True if it was logged (callers then add details).
bool al_failed(const char* what) {
    const ALenum error = alGetError();
    if (error == AL_NO_ERROR) return false;
    static std::mutex mutex;
    static std::vector<std::pair<const char*, ALenum>> seen;
    std::lock_guard lock(mutex);
    if (std::count(seen.begin(), seen.end(), std::make_pair(what, error)) < 10) {
        seen.emplace_back(what, error);
        LOG_WARN("audio", "OpenAL error %04x: %s", unsigned(error), what);
        return true;
    }
    return false;
}

// Millibels to a linear gain.
float gain_of(float millibels) { return std::pow(10.0f, millibels / 2000.0f); }

// EFX's reverb from an I3DL2 environment (the mapping of OpenAL Soft's efx-presets.h, which
// are the I3DL2/EAX presets), each value clamped to EFX's range.
void set_reverb_effect(ALuint effect, bool eax, const Environment& e) {
    const float density = std::clamp(e.density / 100.0f, 0.0f, 1.0f);
    const float diffusion = std::clamp(e.diffusion / 100.0f, 0.0f, 1.0f);
    const float gain = std::clamp(gain_of(e.room), 0.0f, 1.0f);
    const float gain_hf = std::clamp(gain_of(e.room_hf), 0.0f, 1.0f);
    const float decay = std::clamp(e.decay_time, 0.1f, 20.0f);
    const float decay_hf = std::clamp(e.decay_hf_ratio, 0.1f, 2.0f);
    const float reflections = std::clamp(gain_of(e.reflections), 0.0f, 3.16f);
    const float reflections_delay = std::clamp(e.reflections_delay, 0.0f, 0.3f);
    const float late = std::clamp(gain_of(e.reverb), 0.0f, 10.0f);
    const float late_delay = std::clamp(e.reverb_delay, 0.0f, 0.1f);
    const float rolloff = std::clamp(e.room_rolloff, 0.0f, 10.0f);
    if (eax) {
        alEffectf(effect, AL_EAXREVERB_DENSITY, density);
        alEffectf(effect, AL_EAXREVERB_DIFFUSION, diffusion);
        alEffectf(effect, AL_EAXREVERB_GAIN, gain);
        alEffectf(effect, AL_EAXREVERB_GAINHF, gain_hf);
        alEffectf(effect, AL_EAXREVERB_DECAY_TIME, decay);
        alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, decay_hf);
        alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, reflections);
        alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, reflections_delay);
        alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, late);
        alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, late_delay);
        alEffectf(effect, AL_EAXREVERB_HFREFERENCE, std::clamp(e.hf_reference, 1000.0f, 20000.0f));
        alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, rolloff);
        alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, AL_TRUE);
        // OpenAL Soft (1.24) folds a fully focused pan back to the middle in stereo: at most 0.6.
        auto pan = [](const float in[3], float out[3]) {
            const float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
            const float scale = length > 0.6f ? 0.6f / length : 1.0f;
            for (int i = 0; i < 3; ++i) out[i] = in[i] * scale;
        };
        float reflections_pan[3], reverb_pan[3];
        pan(e.reflections_pan, reflections_pan);
        pan(e.reverb_pan, reverb_pan);
        alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, reflections_pan);  // x right, y up, z ahead
        alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb_pan);
    } else {
        alEffectf(effect, AL_REVERB_DENSITY, density);
        alEffectf(effect, AL_REVERB_DIFFUSION, diffusion);
        alEffectf(effect, AL_REVERB_GAIN, gain);
        alEffectf(effect, AL_REVERB_GAINHF, gain_hf);
        alEffectf(effect, AL_REVERB_DECAY_TIME, decay);
        alEffectf(effect, AL_REVERB_DECAY_HFRATIO, decay_hf);
        alEffectf(effect, AL_REVERB_REFLECTIONS_GAIN, reflections);
        alEffectf(effect, AL_REVERB_REFLECTIONS_DELAY, reflections_delay);
        alEffectf(effect, AL_REVERB_LATE_REVERB_GAIN, late);
        alEffectf(effect, AL_REVERB_LATE_REVERB_DELAY, late_delay);
        alEffectf(effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, rolloff);
        alEffecti(effect, AL_REVERB_DECAY_HFLIMIT, AL_TRUE);
    }
}

}  // namespace

std::unique_ptr<Output> Output::open(const OutputConfig& config) {
    std::unique_ptr<Output> output(new Output());
    output->rate_ = config.rate;
    output->speakers_ = config.speakers;
    if (config.device == OutputConfig::Device::Default && !config.device_name.empty()) {
        output->device_ = alcOpenDevice(config.device_name.c_str());
        if (!output->device_)
            LOG_WARN("audio", "no audio output device \"%s\"; the system's is used", config.device_name.c_str());
    }
    if (config.device == OutputConfig::Device::Default && !output->device_) output->device_ = alcOpenDevice(nullptr);
    if (output->device_) {
        output->context_ = alcCreateContext(output->device_, context_attributes(config.speakers, -1).data());
    } else {
        if (config.device == OutputConfig::Device::Default)
            LOG_WARN("audio", "no audio output device; sound plays into a silent loopback device");
        output->loopback_ = true;
        output->device_ = alcLoopbackOpenDeviceSOFT(nullptr);
        // Loopback renders stereo 16-bit frames for render(), whatever the layout setting.
        const std::vector<ALCint> attributes =
            context_attributes(Speakers::Auto, -1, {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT,
                                                    ALC_SHORT_SOFT, ALC_FREQUENCY, ALCint(config.rate)});
        output->speakers_ = Speakers::Auto;
        if (output->device_) output->context_ = alcCreateContext(output->device_, attributes.data());
    }
    if (!output->context_ || !alcMakeContextCurrent(output->context_)) {
        LOG_ERROR("audio", "cannot create an OpenAL context");
        return nullptr;
    }
    if (!alIsExtensionPresent("AL_SOFT_callback_buffer") || !alIsExtensionPresent("AL_SOFT_direct_channels")) {
        LOG_ERROR("audio", "OpenAL Soft 1.22 or newer is needed (AL_SOFT_callback_buffer)");
        return nullptr;
    }
    // Distance attenuation follows the title's rolloff settings and is computed by the
    // caller; OpenAL only pans. No doppler for now.
    alDistanceModel(AL_NONE);
    alDopplerFactor(0.0f);
    output->best_resampler_ = best_resampler();
    if (output->best_resampler_ >= 0) output->default_resampler_ = alGetInteger(AL_DEFAULT_RESAMPLER_SOFT);
    if (alcIsExtensionPresent(output->device_, "ALC_EXT_EFX")) {
        alGenEffects(1, &output->reverb_effect_);
        alEffecti(output->reverb_effect_, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        output->eax_reverb_ = alGetError() == AL_NO_ERROR;
        if (!output->eax_reverb_) alEffecti(output->reverb_effect_, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        alGenAuxiliaryEffectSlots(1, &output->reverb_slot_);
        set_reverb_effect(output->reverb_effect_, output->eax_reverb_, Environment{});  // silent at first
        alAuxiliaryEffectSloti(output->reverb_slot_, AL_EFFECTSLOT_EFFECT, ALint(output->reverb_effect_));
        if (alGetError() != AL_NO_ERROR) {
            LOG_WARN("audio", "cannot set up the reverb (EFX); no reverb");
            output->reverb_slot_ = 0;
        }
    }
    const char* name = alcGetString(output->device_, ALC_ALL_DEVICES_SPECIFIER);
    ALCint mode = 0;
    alcGetIntegerv(output->device_, ALC_OUTPUT_MODE_SOFT, 1, &mode);
    LOG_INFO("audio", "OpenAL %s on %s%s, %s", alGetString(AL_VERSION), output->loopback_ ? "loopback" : (name ? name : "?"),
             config.device == OutputConfig::Device::ManualLoopback ? " (manual)" : "", mode_name(mode));
    output->set_enhanced(config.enhanced);
    if (output->loopback_ && config.device != OutputConfig::Device::ManualLoopback)
        output->driver_ = std::thread([raw = output.get()] { raw->drive_loopback(); });
    return output;
}

Output::~Output() {
    stopping_ = true;
    if (driver_.joinable()) driver_.join();
    for (auto& source : sources_) {
        alDeleteSources(1, &source->source);
        alDeleteBuffers(1, &source->buffer);
        if (source->filter) alDeleteFilters(1, &source->filter);
    }
    if (reverb_slot_) alDeleteAuxiliaryEffectSlots(1, &reverb_slot_);
    if (reverb_effect_) alDeleteEffects(1, &reverb_effect_);
    alcMakeContextCurrent(nullptr);
    if (context_) alcDestroyContext(context_);
    if (device_) alcCloseDevice(device_);
}

void Output::drive_loopback() {
    using namespace std::chrono;
    constexpr size_t kChunk = 480;  // 10 ms at 48 kHz
    std::vector<int16_t> scratch(kChunk * 2);
    auto next = steady_clock::now();
    while (!stopping_) {
        if (!paused_) alcRenderSamplesSOFT(device_, scratch.data(), static_cast<ALCsizei>(kChunk));
        next += microseconds(kChunk * 1000000 / rate_);
        std::this_thread::sleep_until(next);
    }
}

void Output::set_paused(bool paused) {
    if (paused_.exchange(paused) == paused || loopback_) return;
    if (!alcIsExtensionPresent(device_, "ALC_SOFT_pause_device")) return;
    if (paused)
        alcDevicePauseSOFT(device_);
    else
        alcDeviceResumeSOFT(device_);
}

void Output::render(int16_t* out, size_t frames) {
    if (loopback_) alcRenderSamplesSOFT(device_, out, static_cast<ALCsizei>(frames));
}

Output::Source* Output::add_source(Voice& voice) {
    auto source = std::make_unique<Source>(voice);
    alGenSources(1, &source->source);
    alGenBuffers(1, &source->buffer);
    if (enhanced_ && best_resampler_ >= 0) alSourcei(source->source, AL_SOURCE_RESAMPLER_SOFT, best_resampler_);
    Source* raw = source.get();
    sources_.push_back(std::move(source));
    reformat(raw);
    return raw;
}

void Output::reformat(Source* source) {
    const Format format = source->voice.format();
    al_failed("left over before a voice's new format");
    alSourceStop(source->source);
    alSourcei(source->source, AL_BUFFER, 0);
    if (al_failed("detaching a voice's callback buffer")) {
        ALint state = 0;
        alGetSourcei(source->source, AL_SOURCE_STATE, &state);
        LOG_WARN("audio", "  (source %u state %04x)", source->source, unsigned(state));
    }
    source->channels = 0;
    const ALenum al = al_format(format.channels);
    if (!al || format.block_align == 0) {
        if (format.block_align) LOG_WARN("audio", "%u-channel voices are not supported", format.channels);
        return;
    }
    source->channels = format.channels;
    alBufferCallbackSOFT(source->buffer, al, static_cast<ALsizei>(format.rate), render_callback, source);
    if (al_failed("setting up a callback buffer"))
        LOG_WARN("audio", "  (%u channels, %u Hz, block %u)", format.channels, format.rate, format.block_align);
    alSourcei(source->source, AL_BUFFER, static_cast<ALint>(source->buffer));
    al_failed("attaching a voice's callback buffer");
}

void Output::update(Source* source, const SourceParams& params) {
    const ALuint s = source->source;
    // A louder reverb send: the whole source louder, the direct sound's filter down by as much
    // (EFX filters do not amplify).
    const float send = reverb_slot_ ? std::clamp(params.send_gain, 1.0f, 4.0f) : 1.0f;
    alSourcef(s, AL_MAX_GAIN, 4.0f);
    alSourcef(s, AL_GAIN, params.gain * send);
    alSourcef(s, AL_PITCH, params.pitch);
    alSourcei(s, AL_DIRECT_CHANNELS_SOFT, params.placement == Placement::Direct ? AL_TRUE : AL_FALSE);
    alSourcei(s, AL_SOURCE_RELATIVE, params.placement == Placement::World ? AL_FALSE : AL_TRUE);
    alSourcefv(s, AL_POSITION, params.position);
    if (reverb_slot_)
        alSource3i(s, AL_AUXILIARY_SEND_FILTER, params.reverb_send ? ALint(reverb_slot_) : AL_EFFECTSLOT_NULL, 0,
                   AL_FILTER_NULL);
    // The filter's values are copied when it is attached: attached again after each change.
    const float direct_gain = params.direct_gain / send;
    const bool filter = direct_gain < 1.0f || params.direct_gain_hf < 1.0f;
    if (reverb_slot_ && (filter || source->filtered)) {
        if (filter && !source->filter) {
            alGenFilters(1, &source->filter);
            alFilteri(source->filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        }
        if (filter) {
            alFilterf(source->filter, AL_LOWPASS_GAIN, std::clamp(direct_gain, 0.0f, 1.0f));
            alFilterf(source->filter, AL_LOWPASS_GAINHF, std::clamp(params.direct_gain_hf, 0.0f, 1.0f));
        }
        alSourcei(s, AL_DIRECT_FILTER, filter ? ALint(source->filter) : AL_FILTER_NULL);
        source->filtered = filter;
    }
    if (al_failed("updating a voice"))
        LOG_WARN("audio", "  (gain %g, pitch %g, placement %d, position %g %g %g)", double(params.gain),
                 double(params.pitch), int(params.placement), double(params.position[0]), double(params.position[1]),
                 double(params.position[2]));
}

void Output::set_enhanced(bool enhanced) {
    enhanced_ = enhanced;
    if (best_resampler_ >= 0)
        for (auto& source : sources_)
            alSourcei(source->source, AL_SOURCE_RESAMPLER_SOFT, enhanced ? best_resampler_ : default_resampler_);
    if (reverb_slot_ && !enhanced) {
        set_reverb_effect(reverb_effect_, eax_reverb_, Environment{});
        alAuxiliaryEffectSloti(reverb_slot_, AL_EFFECTSLOT_EFFECT, ALint(reverb_effect_));
    }
    LOG_INFO("audio", "%s sound: resampler %s, reverb %s", enhanced ? "enhanced" : "original",
             enhanced && best_resampler_ >= 0 ? alGetStringiSOFT(AL_RESAMPLER_NAME_SOFT, best_resampler_) : "default",
             !reverb_slot_ ? "unavailable" : enhanced ? (eax_reverb_ ? "EAX reverb" : "standard reverb") : "off");
}

void Output::set_environment(const Environment& environment) {
    if (!reverb_slot_ || !enhanced_) return;
    // Changes worth a log line: 3 dB in a level, a fifth of the decay time.
    const Environment& l = logged_;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - logged_at_ >= 1000 &&
        (std::abs(environment.room - l.room) >= 300 || std::abs(environment.reflections - l.reflections) >= 300 ||
         std::abs(environment.reverb - l.reverb) >= 300 ||
         std::abs(environment.decay_time - l.decay_time) >= 0.2f * std::max(l.decay_time, 0.1f))) {
        LOG_INFO("audio", "reverb: level %.0f mB, decay %.2f s, reflections %.0f mB at %.0f ms, late %.0f mB",
                 double(environment.room), double(environment.decay_time), double(environment.reflections),
                 double(environment.reflections_delay * 1000), double(environment.reverb));
        logged_ = environment;
        logged_at_ = now;
    }
    set_reverb_effect(reverb_effect_, eax_reverb_, environment);
    // The slot takes the effect's values when it is attached again.
    alAuxiliaryEffectSloti(reverb_slot_, AL_EFFECTSLOT_EFFECT, ALint(reverb_effect_));
}

void Output::sync(Source* source) {
    if (!source->channels) return;
    ALint state = 0;
    alGetSourcei(source->source, AL_SOURCE_STATE, &state);
    const bool playing = source->voice.playing();
    const bool paused = source->voice.paused();
    if (playing && !paused && state != AL_PLAYING)
        alSourcePlay(source->source);
    else if (playing && paused && state == AL_PLAYING)
        alSourcePause(source->source);
    else if (!playing && (state == AL_PLAYING || state == AL_PAUSED))
        alSourceStop(source->source);
}

void Output::stop(Source* source) { alSourceStop(source->source); }

void Output::set_listener(const float position[3], const float velocity[3], const float at[3], const float up[3]) {
    const float orientation[6] = {at[0], at[1], at[2], up[0], up[1], up[2]};
    alListenerfv(AL_POSITION, position);
    alListenerfv(AL_VELOCITY, velocity);
    alListenerfv(AL_ORIENTATION, orientation);
}

void Output::begin_batch() { alDeferUpdatesSOFT(); }
void Output::end_batch() { alProcessUpdatesSOFT(); }

bool Output::set_hrtf(bool enabled) {
    if (loopback_ || speakers_ != Speakers::Auto || hrtf_ == int(enabled)) return false;
    hrtf_ = enabled;
    return reset_device();
}

bool Output::reset_device() {
    const bool reset = alcResetDeviceSOFT(device_, context_attributes(speakers_, hrtf_).data()) == ALC_TRUE;
    ALCint mode = 0;
    alcGetIntegerv(device_, ALC_OUTPUT_MODE_SOFT, 1, &mode);
    LOG_INFO("audio", "output %s%s", mode_name(mode), reset ? "" : " (the device could not be reset)");
    return reset;
}

}  // namespace audio
