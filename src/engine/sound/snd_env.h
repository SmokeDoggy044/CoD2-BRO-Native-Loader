#pragma once

// Enhanced sound: the reverb environment around the listener, found with the world's
// collision (our addition; the retail game plays no reverb). Not title code.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "reverb_tuning.h"

namespace audio {
class SoundDevice;
struct Listener;
struct Transmission;
}

namespace game {

// What reaches the listener of a sound (3.2): the occlusion amount as before (0 in view .. 1; the reverb send, the
// sound view) and the direct sound's loss per band (dB: below ~250 Hz, middle, above ~5 kHz; the middle at most 18, the highs up to 12
// more) - through
// each solid on the occlusion lines by material and thickness, or round the nearest edge, whichever is louder.
struct SndPropagation {
    float occlusion = 0;
    float loss_db[3] = {0, 0, 0};
    float detour = 1;  // the edge detour's cost (1: none)
};
// `partial_cover` (3.3): 32 lines over the sound's size instead of the 7.
SndPropagation SndEnv_Propagation(const float listener[3], const float source[3], bool partial_cover,
                                  std::string* describe = nullptr);
// The loss as the sound device's band gains (the middle as the gain, the outer bands relative, at most 1).
audio::Transmission SndEnv_ToTransmission(const float loss_db[3]);

// A 3D voice whose propagation the worker traces (slot and owner identify it when the result comes back).
struct SndVoiceRay {
    uint32_t slot = 0;
    int32_t owner = 0;
    float position[3] = {0, 0, 0};
    SndPropagation propagation;  // the result
};
// Once per sound update (SndHw_Update) on an enhanced device, game thread: hands the ray worker (3.1) this update's
// listener, the probe when due (`reverb`) and `voices` to trace, when it is idle; applies what it finished since:
// the environment (probes now and then, turned with the listener every time) and the voices' occlusion in `done`.
void SndEnv_Update(audio::SoundDevice& device, const audio::Listener& listener, bool reverb, bool partial_cover,
                   std::vector<SndVoiceRay> voices, std::vector<SndVoiceRay>& done);
// COD_BRO_SOUND_TIME=1: the game thread's time in the enhanced sound update (`ns`, measured by the caller) counted;
// a log line every 600 updates with the ray worker's job times.
void SndEnv_CountGameTime(uint64_t ns);
// How much of a sound at `source` is blocked on its way to the listener (0 clear .. 1 behind
// walls), for occlusion (enhanced sound): volumetric, the share of seven lines through the
// world's collision to points on a sphere around the source that is blocked. Glass blocks less.
float SndEnv_Occlusion(const float listener[3], const float source[3]);

// Diagnostics (a playtest mark): each line of SndEnv_Occlusion and what it hit, into `out`.
void SndEnv_DescribeOcclusion(const float listener[3], const float source[3], char* out, size_t size);

// Diagnostics (a playtest mark's <mark>_sound.txt): the environment in effect and the probe rays from the listener
// (distance, surface); per voice each occlusion line (clear, or the surface it hit and where), detour and result.
void SndEnv_DescribeForMark(const float listener[3], std::string& out);
void SndEnv_DescribeVoiceForMark(const float listener[3], const float source[3], std::string& out);
// Developers (the sound debug view): the lines SndEnv_Occlusion traces from the listener to `source`, split where
// they hit (listener -> hit Blocked or Glass, hit -> point Beyond), and the detour round the centre line's hit (two
// legs) when one is found. Game coordinates. Returns the count written (at most `max`; 16 is enough).
struct SoundPathLine {
    enum Kind : uint8_t { Clear, Blocked, Glass, Beyond, Detour } kind = Clear;
    float from[3], to[3];
};
size_t SndEnv_SoundPaths(const float listener[3], const float source[3], SoundPathLine* out, size_t max);
// The player's adjustments (settings.ini "reverb", the PC menu); any thread, applied from the
// next probe on.
void SndEnv_SetTuning(const audio::ReverbTuning& tuning);
// Before a batch of sound rays on this thread (each sound update, a mark): takes the acoustic scene of now
// (snd_scene.h; none while a level's is being built: rays then hit nothing).
void SndEnv_BeginRays();
// An in-process title restart (own engine host 5b): no environment heard yet.
void SndEnv_Reset();
audio::ReverbTuning SndEnv_Tuning();

}  // namespace game
