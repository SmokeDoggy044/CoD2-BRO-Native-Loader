#pragma once

#include <SDL3/SDL_mouse.h>

namespace render {

// Our mouse pointer for the game's menus (the Game Cursor setting): an arrow in the menus' paper colour with a dark
// outline and a soft shadow, drawn here (the Xbox game has no pointer art). `height` is the pointer's height in
// pixels; null if SDL cannot make it (the system pointer stays).
SDL_Cursor* make_game_cursor(int height);

}  // namespace render
