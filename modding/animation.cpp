// See animation.h.

#include "animation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <vector>

#include "csv.h"
#include "pack.h"

namespace fs = std::filesystem;

namespace modding {
namespace {

// The Xbox asset extensions the game's streamer registers (Stream_GetFileType /
// Stream_GetLoadHandler in game/code/stream.cpp). Everything else in the packs
// belongs to another tab (textures, sounds, effects, fonts, scripts).
const std::array<std::string, 7> kExtensions = {".xbmesh", ".xbcell", ".xbmorph", ".xmb",
                                                 ".xbanim", ".xbsanim", ".xbskel"};

const std::array<std::string, 14> kManifestColumns = {
    "category", "group", "kind", "extension", "source", "entry", "asset_path",
    "packed", "offset", "size", "hash", "output", "duplicate_of", "note"};

uint64_t hash_asset(const std::vector<uint8_t>& bytes) {
    uint64_t h = 0xCBF29CE484222325ull ^ (bytes.size() * 0x9E3779B97F4A7C15ull);
    for (uint8_t c : bytes) {
        h = (h ^ c) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}

bool alpha_numeric(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

std::string sanitize(std::string text) {
    for (char& c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) c = '_';
    }
    // No path components may be "." or ".." after sanitizing.
    if (text == "." || text == "..") text = "_";
    return text;
}

std::string stem_of(const std::string& asset_path, const std::string& entry) {
    std::string path = asset_path;
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string name;
    if (const size_t slash = path.find_last_of('/'); slash != std::string::npos)
        name = path.substr(slash + 1);
    else
        name = path;
    if (name.empty()) {
        name = entry;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (const size_t slash = name.find_last_of('/'); slash != std::string::npos) name = name.substr(slash + 1);
    }
    if (const size_t dot = name.find_last_of('.'); dot != std::string::npos) name.resize(dot);
    if (name.empty()) name = "asset";
    return sanitize(name);
}

// Lowercased "path/name" identity with separators turned into word boundaries.
std::string identity(const AnimationAsset& asset) {
    std::string text = lowercase(asset.asset_path + "/" + asset.entry);
    for (char& c : text)
        if (c == '\\' || c == '/' || c == ':') c = '_';
    return text;
}

// True when `word` occurs as a whole underscore/punctuation-delimited token.
bool has_word(const std::string& text, std::string_view word) {
    size_t at = text.find(word);
    while (at != std::string::npos) {
        const bool left = at == 0 || !alpha_numeric(text[at - 1]);
        const size_t end = at + word.size();
        const bool right = end == text.size() || !alpha_numeric(text[end]);
        if (left && right) return true;
        at = text.find(word, at + 1);
    }
    return false;
}

bool looks_like_hands(const std::string& text) {
    static constexpr std::array<std::string_view, 11> kTokens = {
        "viewmodel_hands", "viewhands", "view_hands", "player_hands", "hands_us", "hands_ge",
        "hands_", "_hands", "viewmodel_arm", "viewarm", "_arms"};
    for (const auto token : kTokens)
        if (text.find(token) != std::string::npos) return true;
    return false;
}

bool looks_like_weapon(const std::string& text) {
    static constexpr std::array<std::string_view, 9> kPhrases = {
        "viewmodel", "weapon", "viewgun", "gunmodel", "weapons_", "guns_", "view_weapon", "_gun", "firearm"};
    for (const auto phrase : kPhrases)
        if (text.find(phrase) != std::string::npos) return true;
    // Bare weapon names (CoD2: BRO model/anim names without a "weapon" prefix).
    static constexpr std::array<std::string_view, 38> kWeaponNames = {
        "m1garand", "garand", "thompson", "mp40", "kar98", "k98", "springfield", "colt45",
        "panzerfaust", "panzerschreck", "bazooka", "mg42", "mg34", "fg42", "stg44", "mp44",
        "ppsh", "luger", "m1a1", "carbine", "enfield", "sten", "bren", "dp28", "g43",
        "gewehr", "m1919", "browning", "grenade", "nade", "rifle", "pistol", "shotgun",
        "sniper", "satchel", "ammo", "bar", "m1"};
    for (const auto name : kWeaponNames)
        if (has_word(text, name)) return true;
    return false;
}

std::string first_words(std::string name, size_t count) {
    size_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        at = name.find('_', at);
        if (at == std::string::npos) return name;
        if (i + 1 == count) return name.substr(0, at);
        ++at;
    }
    return name;
}

std::string group_of(const AnimationAsset& asset) {
    std::string name = lowercase(stem_of(asset.asset_path, asset.entry));
    static constexpr std::array<std::string_view, 9> kPrefixes = {
        "viewmodel_", "viewmodel", "weapon_", "weapon", "view_", "anim_", "xmodel_", "w_", "v_"};
    for (const auto prefix : kPrefixes) {
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
            name = name.substr(prefix.size());
            break;
        }
    }
    if (asset.category == AnimationCategory::ViewHands) {
        const std::string group = first_words(name, 2);
        return sanitize(group.empty() ? "hands" : group);
    }
    if (asset.category == AnimationCategory::Weapon) {
        const std::string group = first_words(name, 1);
        return sanitize(group.empty() ? "weapon" : group);
    }
    return "shared";
}

AnimationKind kind_of(const std::string& extension) {
    if (extension == ".xbanim" || extension == ".xbsanim") return AnimationKind::Animation;
    if (extension == ".xbskel") return AnimationKind::Skeleton;
    if (extension == ".xmb") return AnimationKind::ModelDescriptor;
    if (extension == ".xbmesh" || extension == ".xbcell" || extension == ".xbmorph") return AnimationKind::Mesh;
    return AnimationKind::Other;
}

void classify(AnimationAsset& asset) {
    asset.kind = kind_of(asset.extension);
    const std::string text = identity(asset);
    if (looks_like_hands(text))
        asset.category = AnimationCategory::ViewHands;
    else if (looks_like_weapon(text))
        asset.category = AnimationCategory::Weapon;
    else if (asset.kind == AnimationKind::Animation || asset.kind == AnimationKind::Skeleton)
        asset.category = AnimationCategory::Shared;
    else
        asset.category = AnimationCategory::Other;
    asset.group = group_of(asset);
}

std::string output_for(const AnimationAsset& asset) {
    std::string folder;
    switch (asset.category) {
        case AnimationCategory::ViewHands: folder = "viewhands/"; break;
        case AnimationCategory::Weapon: folder = "weapons/" + sanitize(asset.group) + "/"; break;
        case AnimationCategory::Shared:
        case AnimationCategory::Other:
            switch (asset.kind) {
                case AnimationKind::Animation: folder = "animations/"; break;
                case AnimationKind::Skeleton: folder = "skeletons/"; break;
                case AnimationKind::Mesh:
                case AnimationKind::ModelDescriptor: folder = "models/"; break;
                default: folder = "other/"; break;
            }
            break;
    }
    return folder + stem_of(asset.asset_path, asset.entry) + asset.extension;
}

std::string short_hash(uint64_t hash) { return hex64(hash).substr(0, 16); }

std::string unique_output(const AnimationAsset& asset, std::map<std::string, uint64_t>& used) {
    const std::string base = output_for(asset);
    const size_t dot = base.find_last_of('.');
    const std::string prefix = dot == std::string::npos ? base : base.substr(0, dot);
    const std::string suffix = dot == std::string::npos ? "" : base.substr(dot);
    std::string candidate = base;
    const auto it = used.find(candidate);
    if (it == used.end() || it->second == asset.hash) return candidate;
    for (int n = 2; n < 10000; ++n) {
        candidate = prefix + "__" + short_hash(asset.hash) + "_" + std::to_string(n) + suffix;
        const auto other = used.find(candidate);
        if (other == used.end() || other->second == asset.hash) return candidate;
    }
    return prefix + "__" + short_hash(asset.hash) + suffix;
}

bool write_animation_manifest(const fs::path& file, const std::vector<AnimationAsset>& rows, std::string& error) {
    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        error = "cannot create " + file.generic_string();
        return false;
    }
    out << csv_line(std::vector<std::string>(kManifestColumns.begin(), kManifestColumns.end()));
    for (const AnimationAsset& row : rows) {
        out << csv_line({animation_category_name(row.category), row.group, animation_kind_name(row.kind),
                         row.extension, row.source, row.entry, row.asset_path, row.packed ? "1" : "0",
                         std::to_string(row.offset), std::to_string(row.size), hex64(row.hash), row.output,
                         row.duplicate_of, row.note});
    }
    out.flush();
    if (!out) {
        error = "cannot write " + file.generic_string();
        return false;
    }
    return true;
}

AnimationKind parse_kind(const std::string& text) {
    if (text == "Animation") return AnimationKind::Animation;
    if (text == "Skeleton") return AnimationKind::Skeleton;
    if (text == "ModelDescriptor") return AnimationKind::ModelDescriptor;
    if (text == "Mesh") return AnimationKind::Mesh;
    return AnimationKind::Other;
}

AnimationCategory parse_category(const std::string& text) {
    if (text == "ViewHands") return AnimationCategory::ViewHands;
    if (text == "Weapon") return AnimationCategory::Weapon;
    if (text == "Shared") return AnimationCategory::Shared;
    return AnimationCategory::Other;
}

uint64_t parse_decimal(const std::string& text) {
    return static_cast<uint64_t>(std::strtoull(text.c_str(), nullptr, 10));
}

void write_readme(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (fs::exists(path, ec)) return;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

}  // namespace

bool is_animation_extension(const std::string& lowercase_extension) {
    return std::find(kExtensions.begin(), kExtensions.end(), lowercase_extension) != kExtensions.end();
}

std::string animation_kind_name(AnimationKind kind) {
    switch (kind) {
        case AnimationKind::Mesh: return "Mesh";
        case AnimationKind::ModelDescriptor: return "ModelDescriptor";
        case AnimationKind::Animation: return "Animation";
        case AnimationKind::Skeleton: return "Skeleton";
        default: return "Other";
    }
}

std::string animation_category_name(AnimationCategory category) {
    switch (category) {
        case AnimationCategory::ViewHands: return "ViewHands";
        case AnimationCategory::Weapon: return "Weapon";
        case AnimationCategory::Shared: return "Shared";
        default: return "Other";
    }
}

std::string animation_readme() {
    return "Animations modding - CoD2: BRO native loader\n"
           "============================================\n\n"
           "F4 -> Modding -> Animations -> Extract writes every player view-hands model,\n"
           "every weapon model and every animation the game ships, straight out of the\n"
           ".cod (KAPF) packs and loose assets. Nothing is decoded or re-encoded: these\n"
           "are the game's original Xbox files, byte for byte, so a CoD2 Xbox model/anim\n"
           "tool can edit them without losing data.\n\n"
           "Folders\n"
           "  viewhands/            first-person hands (arms) models and their skeletons\n"
           "  weapons/<weapon>/     one folder per weapon: its view/world models and\n"
           "                        every animation whose name belongs to that weapon\n"
           "  animations/           every other .xbanim / .xbsanim clip\n"
           "  models/               every other .xbmesh / .xbcell / .xbmorph / .xmb\n"
           "  skeletons/            every other .xbskel bone hierarchy\n\n"
           "Formats: .xbmesh/.xbcell/.xbmorph = NGL mesh (XBXM header), .xmb = XModel\n"
           "descriptor, .xbanim/.xbsanim = NAL animation clip, .xbskel = NAL skeleton.\n"
           "This tab exports, it does not convert to glTF/FBX/OBJ.\n\n"
           "animations.csv lists every occurrence: category, group, kind, source pack,\n"
           "original asset path, byte offset, exact size, content hash and the exported\n"
           "file. Duplicate content (the sp/ and mp/ packs share many assets) is written\n"
           "once; the duplicate_of column points at the file that was kept.\n\n"
           "To use an edit: press Replace. It compares each extracted file with the bytes\n"
           "in the game pack and, for files that changed AND kept the SAME byte length,\n"
           "writes a patched copy of the source .cod / loose file into\n"
           "mods/overrides/<same path>. The game's own files are never modified.\n"
           "Restart the game (or reload the level) to see the change; delete the file\n"
           "from mods/overrides/ to restore the original.\n\n"
           "Replacements must be exactly the original length: a KAPF pack stores its\n"
           "entry sizes in the pack directory, so a different length would need a new\n"
           "pack layout. Changed-size files are reported as skipped, never truncated.\n";
}

std::vector<AnimationAsset> scan_animation_assets(const fs::path& assets, const AnimationProgress& progress) {
    std::vector<AnimationAsset> out;
    const std::vector<Pack> packs = find_packs(assets);
    for (size_t i = 0; i < packs.size(); ++i) {
        const Pack& pack = packs[i];
        if (progress) progress(i, packs.size(), pack.relative);
        std::error_code ec;
        const uint64_t pack_size = static_cast<uint64_t>(fs::file_size(pack.file, ec));
        if (ec) continue;
        for (const PackEntry& entry : pack.entries) {
            const std::string extension = lowercase(fs::path(entry.name).extension().generic_string());
            if (!is_animation_extension(extension)) continue;
            if (entry.offset + entry.size > pack_size || !entry.size) continue;
            AnimationAsset asset;
            asset.source = pack.relative;
            asset.entry = entry.name;
            asset.asset_path = entry.asset_path.empty() ? entry.name : entry.asset_path;
            asset.extension = extension;
            asset.offset = entry.offset;
            asset.size = entry.size;
            asset.packed = true;
            classify(asset);
            out.push_back(std::move(asset));
        }
    }
    if (progress) progress(packs.size(), packs.size(), "loose files");
    const std::vector<std::string> extensions(kExtensions.begin(), kExtensions.end());
    for (const fs::path& file : find_files(assets, extensions)) {
        std::error_code ec;
        const uint64_t size = static_cast<uint64_t>(fs::file_size(file, ec));
        if (ec || !size || size > 0xFFFFFFFFull) continue;
        std::string relative = file.lexically_relative(assets).generic_string();
        if (relative.empty() || relative == ".") relative = file.filename().generic_string();
        AnimationAsset asset;
        asset.source = relative;
        asset.entry = file.filename().generic_string();
        asset.asset_path = "loose/" + relative;
        asset.extension = lowercase(file.extension().generic_string());
        asset.offset = 0;
        asset.size = static_cast<uint32_t>(size);
        asset.packed = false;
        classify(asset);
        out.push_back(std::move(asset));
    }
    return out;
}

AnimationJobResult export_animation_assets(const fs::path& assets, const fs::path& dir,
                                           const std::vector<AnimationAsset>& scanned,
                                           const AnimationProgress& progress) {
    AnimationJobResult result;
    std::vector<AnimationAsset> rows = scanned;
    result.assets = rows.size();
    if (rows.empty()) {
        result.message = "No models or animations found. Check that the game files are in the assets folder.";
        return result;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::map<uint64_t, std::string> by_hash;
    std::map<std::string, uint64_t> used;
    for (size_t i = 0; i < rows.size(); ++i) {
        AnimationAsset& row = rows[i];
        if (progress) progress(i, rows.size(), row.asset_path);
        std::vector<uint8_t> bytes;
        std::string error;
        const fs::path file = assets / fs::path(row.source);
        const bool ok = row.packed ? read_bytes(file, row.offset, row.size, bytes, error)
                                   : read_bytes(file, bytes, error);
        if (!ok || bytes.empty()) {
            row.note = error.empty() ? "cannot read asset" : error;
            ++result.failed;
            continue;
        }
        row.hash = hash_asset(bytes);
        const auto known = by_hash.find(row.hash);
        if (known != by_hash.end()) {
            row.duplicate_of = known->second;
            ++result.duplicates;
            continue;
        }
        row.output = unique_output(row, used);
        if (!write_bytes(dir / fs::path(row.output), bytes, error)) {
            row.note = error;
            row.output.clear();
            ++result.failed;
            continue;
        }
        used[row.output] = row.hash;
        by_hash[row.hash] = row.output;
        ++result.written;
    }
    std::string error;
    if (!write_animation_manifest(dir / "animations.csv", rows, error)) {
        ++result.failed;
        result.message = error;
    }
    write_readme(dir / "README.txt", animation_readme());
    result.ok = result.written > 0;
    if (result.message.empty())
        result.message = "Exported " + std::to_string(result.written) + " model/animation assets (" +
                         std::to_string(result.assets) + " occurrences, " + std::to_string(result.duplicates) +
                         " shared, " + std::to_string(result.failed) + " skipped) to " + dir.generic_string();
    return result;
}

bool read_animation_manifest(const fs::path& file, std::vector<AnimationAsset>& rows, std::string& error) {
    std::ifstream in(file);
    if (!in) {
        error = "cannot open " + file.generic_string() + ". Press Extract first.";
        return false;
    }
    std::string line;
    if (!std::getline(in, line)) {
        error = "empty manifest " + file.generic_string();
        return false;
    }
    const std::vector<std::string> header = csv_split(line);
    std::map<std::string, size_t> column;
    for (size_t i = 0; i < header.size(); ++i) column[header[i]] = i;
    for (const char* required : {"source", "entry", "asset_path", "offset", "size", "output", "extension"})
        if (!column.count(required)) {
            error = std::string("manifest is missing the ") + required + " column. Press Extract again.";
            return false;
        }
    auto field = [&](const std::vector<std::string>& fields, const char* name) -> std::string {
        const auto it = column.find(name);
        return it == column.end() || it->second >= fields.size() ? std::string() : fields[it->second];
    };
    rows.clear();
    while (std::getline(in, line)) {
        const std::vector<std::string> fields = csv_split(line);
        if (fields.size() < 2) continue;
        AnimationAsset row;
        row.category = parse_category(field(fields, "category"));
        row.group = field(fields, "group");
        row.kind = parse_kind(field(fields, "kind"));
        row.extension = field(fields, "extension");
        row.source = field(fields, "source");
        row.entry = field(fields, "entry");
        row.asset_path = field(fields, "asset_path");
        row.packed = field(fields, "packed") == "1";
        row.offset = parse_decimal(field(fields, "offset"));
        row.size = static_cast<uint32_t>(parse_decimal(field(fields, "size")));
        row.hash = parse_hex64(field(fields, "hash"));
        row.output = field(fields, "output");
        row.duplicate_of = field(fields, "duplicate_of");
        row.note = field(fields, "note");
        if (!row.source.empty() && !row.output.empty() && row.size) rows.push_back(std::move(row));
    }
    return true;
}

AnimationJobResult replace_animation_assets(const fs::path& assets, const fs::path& dir,
                                            const fs::path& overrides,
                                            const AnimationProgress& progress) {
    AnimationJobResult result;
    std::vector<AnimationAsset> rows;
    std::string error;
    if (!read_animation_manifest(dir / "animations.csv", rows, error) || rows.empty()) {
        result.message = error.empty() ? "No exported assets in animations.csv. Press Extract first." : error;
        return result;
    }
    std::map<std::string, std::vector<const AnimationAsset*>> by_source;
    for (const AnimationAsset& row : rows) by_source[row.source].push_back(&row);
    size_t total = 0;
    for (const auto& [source, group] : by_source) total += group.size();
    result.assets = total;
    size_t done = 0;
    for (const auto& [source, group] : by_source) {
        std::vector<uint8_t> patched;
        if (!read_bytes(assets / fs::path(source), patched, error)) {
            result.failed += group.size();
            done += group.size();
            continue;
        }
        size_t changed = 0;
        for (const AnimationAsset* row : group) {
            ++done;
            if (progress) progress(done, total, row->asset_path);
            const uint64_t end = row->offset + row->size;
            if (end > patched.size()) {
                ++result.failed;
                continue;
            }
            std::vector<uint8_t> edited;
            if (!read_bytes(dir / fs::path(row->output), edited, error)) {
                ++result.failed;
                continue;
            }
            if (edited.size() != row->size) {
                // A KAPF entry's size lives in the pack directory: never truncate
                // or grow it, that would require rebuilding the whole pack.
                ++result.failed;
                continue;
            }
            if (std::equal(edited.begin(), edited.end(), patched.begin() + static_cast<ptrdiff_t>(row->offset))) {
                ++result.unchanged;
                continue;
            }
            std::copy(edited.begin(), edited.end(), patched.begin() + static_cast<ptrdiff_t>(row->offset));
            ++changed;
        }
        if (!changed) continue;
        if (!write_bytes(overrides / fs::path(source), patched, error)) {
            result.failed += changed;
            continue;
        }
        result.written += changed;
    }
    result.ok = result.written > 0;
    if (!result.ok) {
        result.message = "No changed assets. Edit an exported file (keeping its exact byte length) and press "
                         "Replace again.";
    } else {
        result.message = "Patched " + std::to_string(result.written) + " asset(s) into " +
                         overrides.generic_string() + " (" + std::to_string(result.unchanged) +
                         " unchanged, " + std::to_string(result.failed) +
                         " skipped). Restart the game or reload the level to see them.";
    }
    return result;
}

}  // namespace modding
