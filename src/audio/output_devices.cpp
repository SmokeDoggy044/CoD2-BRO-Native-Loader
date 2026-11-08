#include "output_devices.h"

#include <AL/alc.h>

#include <cstring>

namespace audio {

std::vector<std::string> output_devices() {
    std::vector<std::string> names;
    if (!alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) return names;
    // A list of names, each ending in a zero, the list in another.
    for (const ALCchar* name = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER); name && *name;
         name += std::strlen(name) + 1)
        names.emplace_back(name);
    return names;
}

}  // namespace audio
