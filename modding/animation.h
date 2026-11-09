#pragma once

// The Modding window's "Animations" tab. The game's .cod (KAPF) packs and loose
// assets carry the Xbox model/animation family:
//
//   .xbmesh  NGL mesh files (XBXM header)        -> hands, weapon and world models
//   .xbcell  cell meshes                         -> models
//   .xbmorph morph targets                      -> models / animation data
//   .xmb     XModel descriptors                 -> model definitions
//   .xbanim  NAL animation clips                -> every animation
//   .xbsanim skeletal animation data            -> animations
//   .xbskel  NAL skeletons                      -> bone hierarchies
//
// Extract writes every one of those, unchanged, into mods/animations/ grouped as
// viewhands/, weapons/<weapon>/, animations/, models/ and skeletons/. It also
// writes animations.csv, which records the pack, entry, byte offset, exact size
// and content hash of each asset. Replace patches edited files of the SAME byte
// length back into a copy of the source .cod (or loose file) under
// mods/overrides/ - the same read-only override layer Audio -> Replace uses.
// Original game files are never touched.
//
// These are the game's original Xbox formats, not glTF/FBX: the loader exports
// them byte-for-byte so tools that understand the CoD2 Xbox formats can edit
// them without losing data. No game files are needed to build or test the scan.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace modding {

enum class AnimationKind { Mesh, ModelDescriptor, Animation, Skeleton, Other };
enum class AnimationCategory { ViewHands, Weapon, Shared, Other };

struct AnimationAsset {
    std::string source;       // path relative to the assets folder (pack or loose file)
    std::string entry;        // name inside the pack, or the loose file's name
    std::string asset_path;   // the pack's original E:\asset_temp\... path
    std::string extension;    // lowercase, with the dot
    uint64_t offset = 0;      // absolute byte offset in source (0 for loose files)
    uint32_t size = 0;        // exact byte length in the game file
    bool packed = false;      // true when the asset lives inside a .cod KAPF pack
    uint64_t hash = 0;        // FNV-1a of the asset bytes (filled by Extract)
    AnimationKind kind = AnimationKind::Other;
    AnimationCategory category = AnimationCategory::Other;
    std::string group;        // "hands_us", "m1garand", "shared", ...
    std::string output;       // relative path under mods/animations/ (empty for duplicates)
    std::string duplicate_of; // output path of the first asset with this content
    std::string note;         // read/parse warning, or why Replace skipped it
};

// Called with (done, total, current) while a job is running; may be empty.
using AnimationProgress = std::function<void(size_t done, size_t total, const std::string& current)>;

// Enumerates model/animation assets in .cod packs and loose files. Pure
// directory/header scan: does not read any asset payload.
std::vector<AnimationAsset> scan_animation_assets(const std::filesystem::path& assets,
                                                  const AnimationProgress& progress = {});

bool is_animation_extension(const std::string& lowercase_extension);
std::string animation_kind_name(AnimationKind kind);
std::string animation_category_name(AnimationCategory category);
std::string animation_readme();

struct AnimationJobResult {
    size_t assets = 0;      // rows visited
    size_t written = 0;     // distinct assets written / patches applied
    size_t duplicates = 0;  // occurrences that shared content with an earlier one
    size_t failed = 0;      // unreadable or unusable files
    size_t unchanged = 0;   // Replace: exported file still equals the original
    bool ok = false;
    std::string message;
};

// Writes every distinct asset, animations.csv and README.txt into `dir`.
AnimationJobResult export_animation_assets(const std::filesystem::path& assets,
                                           const std::filesystem::path& dir,
                                           const std::vector<AnimationAsset>& scanned,
                                           const AnimationProgress& progress = {});

// Reads `animations.csv` back (header names, not positions).
bool read_animation_manifest(const std::filesystem::path& file, std::vector<AnimationAsset>& rows,
                             std::string& error);

// Patches edited, same-length assets into copies of their source game files
// under `overrides`. Returns counts and a human-readable message.
AnimationJobResult replace_animation_assets(const std::filesystem::path& assets,
                                            const std::filesystem::path& dir,
                                            const std::filesystem::path& overrides,
                                            const AnimationProgress& progress = {});

}  // namespace modding
