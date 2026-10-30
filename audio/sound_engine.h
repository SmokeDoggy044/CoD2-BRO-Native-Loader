#pragma once

// The sound device on OpenAL Soft (sound_device.h): voices are Voice (playback rules) with an
// Output source each (OpenAL does resampling, panning and HRTF). Distance attenuation is
// computed here from each voice's distances and curve; OpenAL only places the sound.
//
// In a test process of the comparison harness (test_mode.h) there is no output: voices only
// keep their state.

#include <mutex>

#include "output.h"
#include "sound_device.h"

namespace audio {

SoundDevice& sound_device();

// Call before the device opens (optional; default: the system's output).
void configure(const OutputConfig& config);

// The title-visible header each voice id points at starts with this vtable (the DirectSound
// replacement's, so original code can call stream methods through it).
void set_voice_vtable(uint32_t vtable);
// The voice behind a DirectSound object pointer, or 0 if it is not one of ours.
VoiceId voice_from_object(uint32_t object);

// A lock of another sound part (the file media objects) that hold_for_fork takes first.
void add_fork_lock(std::mutex& mutex);

// Around the comparison harness's fork of the game (each test's copy): every sound lock is
// taken first (the mixer thread's current voice finishes), so that the copy's voices are
// consistent and unlocked; release in both processes after the fork.
void hold_for_fork();
void release_after_fork();

// Pauses or resumes all sound output (diagnostics: while the title is frozen).
void set_paused(bool paused);

// The output, once the device is open (tests render through it).
Output* output();

}  // namespace audio
