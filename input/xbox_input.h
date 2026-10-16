#pragma once

// Xbox input (XPP library: XInputOpen & co, device enumeration) replacement.
//
// Gamepads, keyboard and mouse come from SDL3 (devices.h); port 0 is always
// connected. No memory units, no voice communicator.

#include <span>

#include "kernel.h"
#include "types.h"

namespace input {

struct InputConfig {
    // The title's XPP device type tables (XDEVICE_TYPE_GAMEPAD is &g_DeviceType_Gamepad).
    xbox::XbPtr gamepad_type = 0;
};

void configure(const InputConfig& config);

// The replacements, by SDK function name. The loader redirects each function it finds.
std::span<const xbox::KernelImpl> xpp_functions();

}  // namespace input
