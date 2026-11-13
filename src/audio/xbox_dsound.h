#pragma once

// Xbox DirectSound (DSOUND library) replacement: the functions titles call, with the
// same signatures, translated onto our own sound device (sound_engine.h).
//
// The title's DirectSound, buffer, stream and media objects are ours: small structs in
// title-addressable memory whose first field is a vtable, because titles call some
// methods (XMediaObject's Process, GetStatus, ...) through it.

#include <memory>
#include <span>

#include "kernel.h"
#include "sound_engine.h"

namespace platform { class File; }

namespace audio {

// The host file behind a title file handle (XFileCreateMediaObjectAsync): the loader sets the kernel's file table
// (which forwards our native handles), cod_bro our native files (game::files::host_file). Unset: no file.
using TitleFileLookup = std::shared_ptr<platform::File> (*)(uint32_t handle);
void set_title_file_lookup(TitleFileLookup lookup);

// The replacements, by SDK function name (as XbSymbolDatabase/Ghidra name them,
// without the library prefix). The loader redirects each function it finds.
std::span<const xbox::KernelImpl> dsound_functions();

// The buffer, stream and media object methods titles call through vtables, by our own
// names (Stream_GetStatus, Media_Process, ...).
std::span<const xbox::KernelImpl> dsound_methods();

}  // namespace audio
