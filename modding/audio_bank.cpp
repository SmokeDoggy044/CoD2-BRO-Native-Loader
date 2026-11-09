// See audio_bank.h.

#include "audio_bank.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>

#include "audio_codec.h"
#include "csv.h"
#include "pack.h"
#include "texture_hash.h"

namespace fs = std::filesystem;

namespace modding {
namespace {

uint32_t u32(const std::vector<uint8_t>& bytes, size_t at) {
    if (at + 4 > bytes.size()) return 0;
    return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 | uint32_t(bytes[at + 2]) << 16 |
           uint32_t(bytes[at + 3]) << 24;
}
uint16_t u16(const std::vector<uint8_t>& bytes, size_t at) {
    if (at + 2 > bytes.size()) return 0;
    return uint16_t(uint16_t(bytes[at]) | uint16_t(bytes[at + 1]) << 8);
}

uint16_t popcount8(uint8_t value) {
    uint16_t count = 0;
    while (value) { count += value & 1; value >>= 1; }
    return count;
}

std::string safe_name(std::string text) {
    for (char& c : text) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                         c == '>' || c == '|' || c < 0x20;
        if (bad) c = '_';
    }
    if (text.size() > 96) text.resize(96);
    return text;
}

std::string strip_extension(std::string path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    return path;
}

bool plausible_bank(const std::vector<uint8_t>& bytes, size_t at, size_t limit) {
    if (at + 0x90 > limit || at + 0x90 > bytes.size()) return false;
    if (bytes[at + 6] != '1' || bytes[at + 7] != '1') return false;
    for (size_t i = 0; i < 6; ++i) {
        const unsigned char c = bytes[at + i];
        if (c != 0 && (c < 0x20 || c > 0x7e)) return false;
    }
    const uint32_t first_part = u32(bytes, at + 0x10), raw_size = u32(bytes, at + 0x14);
    const uint32_t count = u32(bytes, at + 0x40), entries = u32(bytes, at + 0x44), names = u32(bytes, at + 0x64);
    if (first_part < 0x1000 || first_part > limit - at) return false;
    if (raw_size > limit - at - first_part + 0x1000) return false;
    if (!count || count > 20000 || entries < 0x90 || uint64_t(entries) + uint64_t(count) * 0x28 > first_part) return false;
    if (names && (names < 0x90 || names >= first_part)) return false;
    return true;
}

std::vector<size_t> find_banks(const std::vector<uint8_t>& bytes, size_t begin, size_t end) {
    std::vector<size_t> banks;
    if (begin >= end || end > bytes.size() || end <= 0x90) return banks;
    const size_t last = std::min(end, bytes.size()) - 0x90;
    for (size_t at = begin; at <= last && banks.size() < 128; at += 4) {
        if (plausible_bank(bytes, at, std::min(end, bytes.size()))) {
            // A bank's header is followed by its own data; skip past it to avoid
            // matching bytes inside the entry table as another bank.
            banks.push_back(at);
            at += u32(bytes, at + 0x10);
        }
    }
    return banks;
}

void scan_bank(const std::vector<uint8_t>& bytes, size_t bank, size_t limit, const std::string& source,
               const std::string& prefix, uint64_t pack_offset, std::set<std::string>& used,
               std::vector<SoundRow>& rows) {
    const uint32_t first_part = u32(bytes, bank + 0x10);
    const uint32_t count = u32(bytes, bank + 0x40), entries = u32(bytes, bank + 0x44);
    const uint32_t names = u32(bytes, bank + 0x64);
    const size_t second_part = bank + first_part;
    if (second_part > limit || bank + entries > limit) return;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = bank + entries + size_t(i) * 0x28;
        if (at + 0x28 > limit) break;
        const uint8_t* entry = bytes.data() + at;
        const uint8_t kind = entry[4], flags = entry[5];
        if (kind != kPcm8 && kind != kPcm16 && kind != kXboxAdpcm) continue;
        uint16_t channels = popcount8(entry[6]);
        if (!channels) channels = 1;
        if (channels > 6) continue;
        const uint32_t rate = u16(bytes, at + 0x20) ? u16(bytes, at + 0x20) : 44100;
        const uint32_t frames = u32(bytes, at + 0xc);
        const uint32_t data_rel = u32(bytes, at + 0x1c);
        const uint32_t name_rel = u32(bytes, at);
        std::string name;
        if (names && name_rel != 0xffffffffu) {
            const size_t at_name = bank + names + name_rel;
            if (at_name < limit) {
                for (size_t p = at_name; p < limit && p - at_name < 128 && bytes[p]; ++p) name += char(bytes[p]);
            }
        }
        name = safe_name(name);
        if (name.empty()) name = "sound_" + std::to_string(i);

        size_t wanted = frames_to_bytes(kind, channels, frames);
        const size_t unit = kind == kXboxAdpcm ? size_t(36) * channels
                            : kind == kPcm16    ? size_t(2) * channels
                                               : size_t(channels);
        // Streaming sounds keep their data in the first part of the file; loaded
        // ones are in the second part. Try both: some discs store them differently.
        const size_t candidates[2] = {(flags & 2) ? bank + data_rel : second_part + data_rel,
                                      (flags & 2) ? second_part + data_rel : bank + data_rel};
        size_t data_offset = 0, available = 0;
        for (size_t candidate : candidates) {
            if (candidate < bank + 0x100 || candidate >= limit) continue;
            const size_t room = limit - candidate;
            if (room < unit) continue;
            data_offset = candidate;
            available = room;
            break;
        }
        if (!data_offset || !frames) continue;
        if (!wanted || wanted > available) wanted = available;
        wanted -= wanted % unit;
        if (wanted < unit) continue;
        const uint64_t hash = render::hash_bytes(bytes.data() + data_offset, wanted);
        const std::string short_hash = hex64(hash).substr(0, 16);
        SoundRow row;
        row.hash = hex64(hash);
        row.name = name;
        row.source = source;
        row.data_offset = data_offset;
        row.data_size = static_cast<uint32_t>(wanted);
        row.pack_offset = pack_offset;
        row.bank_offset = bank;
        row.frames = frames;
        row.rate = rate;
        row.channels = channels;
        row.kind = kind;
        std::string file = prefix + "/" + name + "__" + short_hash + ".wav";
        for (int n = 2; used.count(file); ++n) file = prefix + "/" + name + "__" + short_hash + "_" + std::to_string(n) + ".wav";
        used.insert(file);
        row.file = file;
        rows.push_back(std::move(row));
    }
}

void add_wav(const std::vector<uint8_t>& bytes, size_t offset, size_t size, const std::string& source,
             const std::string& stem, uint64_t pack_offset, std::set<std::string>& used,
             std::vector<SoundRow>& rows) {
    if (offset + size > bytes.size() || size < 44) return;
    const uint64_t hash = render::hash_bytes(bytes.data() + offset, size);
    SoundRow row;
    row.hash = hex64(hash);
    row.name = fs::path(stem).filename().generic_string();
    row.source = source;
    row.data_offset = offset;
    row.data_size = static_cast<uint32_t>(size);
    row.pack_offset = pack_offset;
    row.kind = 0;  // already a WAV file: replacement is a straight copy
    row.rate = 44100;
    std::string file = strip_extension(stem) + "__" + hex64(hash).substr(0, 16) + ".wav";
    for (int n = 2; used.count(file); ++n)
        file = strip_extension(stem) + "__" + hex64(hash).substr(0, 16) + "_" + std::to_string(n) + ".wav";
    used.insert(file);
    row.file = file;
    rows.push_back(std::move(row));
}

uint64_t number(const std::string& text) { return std::strtoull(text.c_str(), nullptr, 10); }
uint32_t number32(const std::string& text) { return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10)); }
uint16_t number16(const std::string& text) { return static_cast<uint16_t>(std::strtoul(text.c_str(), nullptr, 10)); }
uint8_t number8(const std::string& text) { return static_cast<uint8_t>(std::strtoul(text.c_str(), nullptr, 10)); }

}  // namespace

AudioScan scan_audio(const fs::path& assets, const std::function<void(size_t, size_t)>& progress) {
    AudioScan scan;
    std::set<std::string> used;
    const auto loose = find_files(assets, {".wbk", ".wav"});
    const std::vector<Pack> packs = find_packs(assets);
    const size_t total = loose.size() + packs.size();
    size_t done = 0;
    auto finish_source = [&] { if (progress) progress(++done, total); };

    for (const fs::path& file : loose) {
        std::vector<uint8_t> bytes;
        std::string error;
        if (!read_bytes(file, bytes, error)) { finish_source(); continue; }
        std::error_code ec;
        const std::string relative = file.lexically_relative(assets).generic_string();
        const std::string stem = strip_extension(relative);
        if (ends_with_ci(file.extension().generic_string(), ".wav")) {
            if (bytes.size() >= 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WAVE", 4))
                add_wav(bytes, 0, bytes.size(), relative, stem, 0, used, scan.rows);
        } else {
            for (const size_t bank : find_banks(bytes, 0, bytes.size())) {
                ++scan.banks;
                scan_bank(bytes, bank, bytes.size(), relative, stem, 0, used, scan.rows);
            }
        }
        ++scan.sources;
        finish_source();
    }

    for (const Pack& pack : packs) {
        std::vector<uint8_t> bytes;
        std::string error;
        if (!read_bytes(pack.file, bytes, error)) { finish_source(); continue; }
        const std::string pack_stem = strip_extension(pack.relative);
        for (const PackEntry& entry : pack.entries) {
            if (entry.offset + entry.size > bytes.size() || entry.size < 0x100) continue;
            const std::string entry_stem = pack_stem + "/" + strip_extension(safe_name(entry.name));
            const bool wav = bytes.size() >= entry.offset + 12 && !std::memcmp(bytes.data() + entry.offset, "RIFF", 4) &&
                             !std::memcmp(bytes.data() + entry.offset + 8, "WAVE", 4);
            if (wav || ends_with_ci(entry.name, ".wav")) {
                add_wav(bytes, entry.offset, entry.size, pack.relative, entry_stem, entry.offset, used, scan.rows);
                continue;
            }
            const size_t end = std::min<size_t>(bytes.size(), entry.offset + entry.size);
            const bool looks_bank = ends_with_ci(entry.name, ".wbk") ||
                                    plausible_bank(bytes, entry.offset, end);
            if (!looks_bank) continue;
            for (const size_t bank : find_banks(bytes, entry.offset, end)) {
                ++scan.banks;
                scan_bank(bytes, bank, end, pack.relative, entry_stem, entry.offset, used, scan.rows);
            }
        }
        ++scan.sources;
        finish_source();
    }
    return scan;
}

bool write_audio_manifest(const fs::path& path, const std::vector<SoundRow>& rows, std::string& error) {
    std::string text = csv_line({"hash", "file", "name", "kind", "channels", "rate", "frames", "source",
                                 "data_offset", "data_size", "pack_offset", "bank_offset"});
    for (const SoundRow& row : rows)
        text += csv_line({row.hash, row.file, row.name, std::to_string(row.kind), std::to_string(row.channels),
                          std::to_string(row.rate), std::to_string(row.frames), row.source,
                          std::to_string(row.data_offset), std::to_string(row.data_size),
                          std::to_string(row.pack_offset), std::to_string(row.bank_offset)});
    std::vector<uint8_t> bytes(text.begin(), text.end());
    return write_bytes(path, bytes, error);
}

bool read_audio_manifest(const fs::path& path, std::vector<SoundRow>& rows, std::string& error) {
    std::ifstream in(path);
    if (!in) { error = "no audio.csv - extract first"; return false; }
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) { header = false; continue; }
        const std::vector<std::string> f = csv_split(line);
        if (f.size() < 12) continue;
        SoundRow row;
        row.hash = f[0];
        row.file = f[1];
        row.name = f[2];
        row.kind = number8(f[3]);
        row.channels = number16(f[4]);
        row.rate = number32(f[5]);
        row.frames = number32(f[6]);
        row.source = f[7];
        row.data_offset = number(f[8]);
        row.data_size = number32(f[9]);
        row.pack_offset = number(f[10]);
        row.bank_offset = number(f[11]);
        rows.push_back(std::move(row));
    }
    return true;
}

}  // namespace modding
