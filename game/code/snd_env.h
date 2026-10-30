#pragma once

// Enhanced sound: the reverb environment around the listener, found with the world's
// collision (our addition; the retail game plays no reverb). Not title code.

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

// The player's adjustments (settings.ini "reverb", the PC menu); any thread, applied from the
// next probe on.
void SndEnv_SetTuning(const audio::ReverbTuning& tuning);
audio::ReverbTuning SndEnv_Tuning();

}  // namespace game
