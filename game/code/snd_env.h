#pragma once

// Enhanced sound: the reverb environment around the listener, found with the world's
// collision (our addition; the retail game plays no reverb). Not title code.

#include <cstddef>

#include "reverb_tuning.h"

namespace audio {
class SoundDevice;
struct Listener;
}

namespace game {

// Once per sound update (SndHw_Update) on an enhanced device, with the listener (world origin,
// orientation): probes now and then, and hands the device the environment, its reflections
// turned with the listener every time.
void SndEnv_Update(audio::SoundDevice& device, const audio::Listener& listener);

// How much of a sound at `source` is blocked on its way to the listener (0 clear .. 1 behind
// walls), for occlusion (enhanced sound): volumetric, the share of seven lines through the
// world's collision to points on a sphere around the source that is blocked. Glass blocks less.
float SndEnv_Occlusion(const float listener[3], const float source[3]);

// Diagnostics (a playtest mark): each line of SndEnv_Occlusion and what it hit, into `out`.
void SndEnv_DescribeOcclusion(const float listener[3], const float source[3], char* out, size_t size);

// The player's adjustments (settings.ini "reverb", the PC menu); any thread, applied from the
// next probe on.
void SndEnv_SetTuning(const audio::ReverbTuning& tuning);
audio::ReverbTuning SndEnv_Tuning();

}  // namespace game
