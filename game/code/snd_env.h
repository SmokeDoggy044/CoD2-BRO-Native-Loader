#pragma once

// Enhanced sound: the reverb environment around the listener, found with the world's
// collision (our addition; the retail game plays no reverb). Not title code.

namespace audio {
class SoundDevice;
struct Listener;
}

namespace game {

// Once per sound update (SndHw_Update) on an enhanced device, with the listener (world origin,
// orientation): probes now and then, and hands the device the environment, its reflections
// turned with the listener every time.
void SndEnv_Update(audio::SoundDevice& device, const audio::Listener& listener);

}  // namespace game
