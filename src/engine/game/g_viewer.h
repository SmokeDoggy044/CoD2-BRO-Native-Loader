#pragma once

// Developer mode's object viewer (phase 1.0d, docs/developers/own-engine-host.md): one model from the loaded
// mission shown in front of the player, as a level script's spawn("script_model") + setmodel would, for looking
// at it from every side (it turns; the game is paused meanwhile). Not title code: host tooling on top of it.
// Call on the title's thread with a level running.

#include <string>

namespace game {

// Shows `model` (a base name, as setmodel takes: the .xbmesh's name without the extension) `distance` units in
// front of the player, standing where the player stands. The same entity is reused for the next model. False
// (and why; on success where) when there is no player or the loaded zones have no such model.
bool viewer_show(const char* model, float distance, std::string& why);
// The shown model's yaw and pitch in degrees (its place stays).
void viewer_turn(float yaw, float pitch);
// Takes the model away.
void viewer_hide();

}  // namespace game
