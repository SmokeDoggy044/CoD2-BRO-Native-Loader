#pragma once

// Xbox DirectSound (DSOUND library) replacement: the functions titles call, with the
// same signatures, translated onto our own sound device (sound_engine.h).
//
// The title's DirectSound, buffer, stream and media objects are ours: small structs in
// title-addressable memory whose first field is a vtable, because titles call some
// methods (XMediaObject's Process, GetStatus, ...) through it.

#include <span>

#include "kernel.h"
#include "sound_engine.h"

namespace audio {

// The replacements, by SDK function name (as XbSymbolDatabase/Ghidra name them,
// without the library prefix). The loader redirects each function it finds.
std::span<const xbox::KernelImpl> dsound_functions();

// The buffer, stream and media object methods titles call through vtables, by our own
// names (Stream_GetStatus, Media_Process, ...).
std::span<const xbox::KernelImpl> dsound_methods();

}  // namespace audio
