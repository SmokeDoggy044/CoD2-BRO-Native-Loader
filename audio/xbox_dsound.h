#pragma once

// Xbox DirectSound (DSOUND library) replacement: the functions titles call, with the
// same signatures, on top of Voice (playback rules) and Output (OpenAL Soft).
//
// The title's DirectSound, buffer, stream and media objects are ours: small structs in
// title-addressable memory whose first field is a vtable, because titles call some
// methods (XMediaObject's Process, GetStatus, ...) through it.

#include <span>

#include "kernel.h"
#include "output.h"

namespace audio {

// Call before the title creates DirectSound (optional; default: the system's output).
void configure(const OutputConfig& config);

// The replacements, by SDK function name (as XbSymbolDatabase/Ghidra name them,
// without the library prefix). The loader redirects each function it finds.
std::span<const xbox::KernelImpl> dsound_functions();

// Pauses or resumes all sound output (diagnostics: while the title is frozen).
void set_paused(bool paused);

// The output, once DirectSound has been created (tests render through it).
Output* output();

}  // namespace audio
