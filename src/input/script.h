#pragma once

// Scripted button presses on port 0, for running the game unattended (debugging):
// "start@20,a@25.5" taps start 20 s and A 25.5 s after the script is set;
// "back@80+4" holds back for 4 s from 80 s.
// Button names: a b x y black white lt rt start back up down left right ls rs.

#include <string_view>

#include "devices.h"

namespace input {

// Replaces the script. False (logged) if it does not parse; the old one stays.
bool set_script(std::string_view script);

// Adds the presses due now to `state`.
void apply_script(PadState& state);

}  // namespace input
