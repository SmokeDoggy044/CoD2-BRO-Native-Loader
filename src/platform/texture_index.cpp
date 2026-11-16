#include "texture_index.h"

#include <charconv>
#include <cstdio>
#include <cstring>

#include "sha256.h"

namespace platform {

namespace {

bool parse_u64(std::string_view s, uint64_t& out, int base = 10) {
    if (s.empty()) return false;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
    return ec == std::errc() && end == s.data() + s.size();
}

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool is_sha256(std::string_view s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

}  // namespace

uint64_t TextureIndex::Pack::stored_bytes() const {
    uint64_t total = 0;
    for (const File& f : files) total += f.stored;
    return total;
}

std::string TextureIndex::Pack::ini_value(std::string_view key) const {
    for (const std::string& line : ini) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos && std::string_view(line).substr(0, eq) == key) return line.substr(eq + 1);
    }
    return {};
}

const TextureIndex::Pack* TextureIndex::find(std::string_view pack_id) const {
    for (const Pack& p : packs)
        if (p.id == pack_id) return &p;
    return nullptr;
}

std::optional<TextureIndex> parse_texture_index(std::string_view text, std::string* error) {
    TextureIndex index;
    bool header = false;
    size_t line_number = 0;
    auto fail = [&](const char* what) -> std::optional<TextureIndex> {
        if (error) *error = "line " + std::to_string(line_number) + ": " + what;
        return std::nullopt;
    };
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        const size_t space = line.find(' ');
        const std::string_view word = line.substr(0, space);
        const std::string_view rest = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
        if (!header) {
            if (word != "cod_bro_textures") return fail("not a texture pack index");
            if (rest != "1") return fail("an index format this build does not know (update the launcher)");
            header = true;
        } else if (word == "release") {
            index.release = std::string(rest);
        } else if (word == "part") {
            const auto w = words(rest);
            TextureIndex::Part part;
            if (w.size() != 3 || !parse_u64(w[1], part.size) || !is_sha256(w[2])) return fail("bad part line");
            part.name = std::string(w[0]);
            if (part.name.find_first_of("/\\:") != std::string::npos || part.name == ".." || part.name == ".")
                return fail("bad part name");
            part.sha256 = std::string(w[2]);
            index.parts.push_back(std::move(part));
        } else if (word == "pack") {
            if (rest.empty() || rest.find_first_of("/\\:. ") != std::string_view::npos) return fail("bad pack id");
            index.packs.push_back({});
            index.packs.back().id = std::string(rest);
        } else if (word == "ini") {
            if (index.packs.empty()) return fail("ini line before a pack");
            index.packs.back().ini.emplace_back(rest);
        } else if (word == "file") {
            if (index.packs.empty()) return fail("file line before a pack");
            const auto w = words(rest);
            TextureIndex::File f;
            uint64_t part = 0;
            if (w.size() != 7 || w[0].size() != 16 || !parse_u64(w[0], f.id, 16) || w[1].size() != 1 ||
                std::strchr("tns", w[1][0]) == nullptr || !parse_u64(w[2], part) || !parse_u64(w[3], f.offset) ||
                !parse_u64(w[4], f.stored) || !parse_u64(w[5], f.size) || !is_sha256(w[6]))
                return fail("bad file line");
            if (part < 1 || part > index.parts.size()) return fail("file in a part the index does not list");
            const TextureIndex::Part& p = index.parts[part - 1];
            if (f.offset > p.size || f.stored > p.size - f.offset) return fail("file past its part's end");
            f.kind = w[1][0];
            f.part = static_cast<uint32_t>(part);
            f.sha256 = std::string(w[6]);
            index.packs.back().files.push_back(std::move(f));
        }
        // Other words: from a later minor version, ignored.
    }
    if (!header) return fail("empty index");
    return index;
}

std::string format_texture_index(const TextureIndex& index) {
    std::string out = "cod_bro_textures 1\nrelease " + index.release + "\n";
    for (const auto& p : index.parts)
        out += "part " + p.name + " " + std::to_string(p.size) + " " + p.sha256 + "\n";
    char id[17];
    for (const auto& pack : index.packs) {
        out += "pack " + pack.id + "\n";
        for (const auto& line : pack.ini) out += "ini " + line + "\n";
        for (const auto& f : pack.files) {
            std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(f.id));
            out += "file " + std::string(id) + " " + f.kind + " " + std::to_string(f.part) + " " +
                   std::to_string(f.offset) + " " + std::to_string(f.stored) + " " + std::to_string(f.size) + " " +
                   f.sha256 + "\n";
        }
    }
    return out;
}

std::array<uint8_t, 32> texture_key(const void* original, size_t size) {
    static constexpr char kPrefix[] = "cod_bro texture key v1";
    Sha256 h;
    h.update(kPrefix, sizeof(kPrefix) - 1);
    h.update(original, size);
    return h.finish();
}

std::array<uint8_t, 12> texture_nonce(uint64_t id, char kind) {
    std::array<uint8_t, 12> nonce{};
    for (int i = 0; i < 8; ++i) nonce[i] = static_cast<uint8_t>(id >> (i * 8));
    nonce[8] = kind == 'n' ? 1 : kind == 's' ? 2 : 0;
    return nonce;
}

}  // namespace platform
