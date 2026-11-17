#pragma once

// Unattended menu walks (--menu-walk SCRIPT DIR, engine improvements phase 4 tools): a script of steps that wait for
// menu states, press pad buttons, move and click a scripted mouse pointer, and take pictures with the menu state
// beside them (DIR/<name>.bmp + DIR/<name>.txt, every state also in DIR/walk.txt). tools/menu_walk.py writes the
// scripts for the menu map and runs them.
//
// Script: steps separated by lines or ';' ('#' starts a comment). SCRIPT is the steps themselves or @FILE.
//   wait S              seconds
//   frames N            presented frames
//   screen [M:]N        until manager M (front, game, dialog; default front) shows screen N, not locked (30 s at most);
//                       `screen N a`: taps A (any button) every second meanwhile
//   idle                until no manager's screen is locked for 10 frames
//   a b x y black white lt rt start back up down left right ls rs   tap that pad button (then 12 frames)
//   hold BUTTON S       hold it S seconds
//   item N              the pointer onto item N of the topmost screen (hover selects it; waits until shown)
//   type TEXT           types TEXT into a text field that takes the keyboard (the profile name), one character every
//                       6 frames; the words enter and backspace stand for those keys
//   capture CODE        a waiting key capture (rebinding) gets input CODE (SDL scancode; bindings.h codes)
//   window X Y          the pointer to window pixel (X, Y), converted as the real mouse is
//   mouse X Y           the pointer to (X, Y) in the 640x480 menu screen
//   click [X Y]         left click (at X Y: moves there first);  rclick: right click
//   wheel N             wheel steps (up positive)
//   shot NAME           picture + state (after `idle`)
//   state NAME          state only
//   quit                end the program (exit code 0; a timeout ends it with 4)

#include <string>

namespace loader {

// False (and why on stderr) when the script does not parse or DIR cannot be made.
bool load_menu_walk(const std::string& script, const std::string& dir);
// Per presented frame (render::D3DConfig::frame_presented), and the picture path for that frame ("" none).
void menu_walk_frame();
std::string menu_walk_screenshot();

}  // namespace loader
