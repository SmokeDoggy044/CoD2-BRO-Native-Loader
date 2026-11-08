#pragma once

// The output devices OpenAL Soft can open, by the names OutputConfig::device_name takes
// (the launcher's choice). Only OpenAL: the launcher links this without the sound device.

#include <string>
#include <vector>

namespace audio {

std::vector<std::string> output_devices();

}  // namespace audio
