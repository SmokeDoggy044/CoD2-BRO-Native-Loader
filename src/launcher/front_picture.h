#pragma once

// The launcher's front page picture, from the player's own game files: a concept art
// image the player has unlocked in the campaign (the game's Bonus Materials), picked at
// random each start, else the main menu's background. The game's logo goes on top.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace launcher {

struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, rows top down
    float aspect = 1.0f;        // the shape it is shown at (the game stretches some textures)
    std::string source;         // for the log
};

// A DDS file's top level as RGBA (DXT1/3/5 or A8R8G8B8 / X8R8G8B8); nullopt for anything else.
std::optional<Picture> decode_dds(const std::vector<uint8_t>& dds);

// The concept art chapters a profile has unlocked: a profile slot file ("Profiles",
// "Profiles1".."Profiles3": 0x3cc-byte records, the profile from +0x1c) unlocks the
// chapters below its highest chapter reached (+0x14), all 13 with its "everything
// unlocked" flag (+0x24). Empty slots (no name) count for nothing. The most of any slot
// under `udata` (the saves' E/UDATA folder).
int unlocked_concept_chapters(const std::filesystem::path& udata);

// The unlocked concept art (bonus/concept<1|2>/cp<n>_<i>f.dds for chapters 1..unlocked).
std::vector<std::filesystem::path> unlocked_concept_art(const std::filesystem::path& assets, int chapters);

// The front page picture: a random one of `concept_art` (its letterbox bars cut off), else the main menu's
// background (sp/frontend.cod); nullopt without game files.
std::optional<Picture> front_picture(const std::filesystem::path& assets,
                                     const std::vector<std::filesystem::path>& concept_art, std::mt19937& random);

// The game's logo (sp/frontend.cod logo.dds, with alpha); nullopt without game files.
std::optional<Picture> game_logo(const std::filesystem::path& assets);

}  // namespace launcher
