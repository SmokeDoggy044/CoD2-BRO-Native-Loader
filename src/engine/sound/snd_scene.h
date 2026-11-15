#pragma once

// The level's acoustic scene (engine improvements phase 3.1): the collision the game loaded (world trees 0x0095f4fc:
// triangle meshes and brushes, cm.h) copied once per level on the game thread, turned into triangles and a BVH
// (acoustic_scene.h) on a worker thread. Every sound ray (snd_env.cpp) queries it instead of the game's CM_BoxTrace,
// so the sound path neither costs the game thread traces nor touches the collision state. Our addition, not title
// code.

#include <cstddef>
#include <memory>

#include "acoustic_scene.h"
#include "compiler.h"

namespace game {

// What the sound rays trace against: solid and glass (foliage and water are copied too, for later passes).
constexpr uint32_t kSceneSolid = 1, kSceneGlass = 0x10, kSceneFoliage = 0x2, kSceneWater = 0x20;

// The scene of the level loaded now, null while none is built (a level loading, or the build still running).
// Any thread; the scene stays valid for as long as the caller holds it.
std::shared_ptr<const AcousticScene> SndScene_Current();

// The game thread, once per sound update: notices a new or changed level (its trees) and starts a build, or drops
// the scene when the world is gone.
void SndScene_Update();

// Sound log lines of our additions (the game library has no logger of its own): the loader sets where they go.
void SndScene_SetLog(void (*log)(const char* line));
void SndScene_Log(const char* format, ...) PLATFORM_PRINTF(1, 2);

// An in-process title restart (own engine host 5b): no scene; a build still running is waited for.
void SndScene_Reset();

}  // namespace game
