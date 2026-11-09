// See pack.h.

#include "pack.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace modding {
namespace {

uint32_t u32(const std::vector<uint8_t>& data, size_t at) {
    if (at + 4 > data.size()) return 0;
    return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
}
uint16_t u16(const std::vector<uint8_t>& data, size_t at) {
    if (at + 2 > data.size()) return 0;
    return uint16_t(uint16_t(data[at]) | uint16_t(data[at + 1]) << 8);
}

std::string string_at(const std::vector<uint8_t>& data, uint32_t offset) {
    std::string out;
    while (offset < data.size() && data[offset]) out += static_cast<char>(data[offset++]);
    return out;
}

}  // namespace

std::string lowercase(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool ends_with_ci(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = text[text.size() - suffix.size() + i], b = suffix[i];
        const char la = a >= 'A' && a <= 'Z' ? char(a - 'A' + 'a') : a;
        const char lb = b >= 'A' && b <= 'Z' ? char(b - 'A' + 'a') : b;
        if (la != lb) return false;
    }
    return true;
}

bool read_pack(const fs::path& path, const fs::path& assets, Pack& pack, std::string& error) {
    std::vector<uint8_t> header(0x8000);
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) { error = "cannot open pack"; return false; }
        in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        header.resize(static_cast<size_t>(in.gcount()));
    }
    if (header.size() < 0x40 || std::memcmp(header.data(), "KAPF", 4) != 0) {
        error = "not a game pack";
        return false;
    }
    pack = {};
    pack.file = path;
    std::error_code ec;
    pack.relative = path.lexically_relative(assets).generic_string();
    if (pack.relative.empty() || pack.relative == ".") pack.relative = path.filename().generic_string();
    const uint32_t sections = std::min<uint32_t>(u32(header, 0x0c), 16);
    for (uint32_t i = 0; i < sections; ++i) {
        const size_t record = 0x28 + i * 24;
        const uint16_t count = u16(header, record + 2);
        const uint32_t chunks = u32(header, record + 4), directory = u32(header, record + 8);
        if (!directory || !chunks) continue;
        for (uint32_t e = 0; e < count; ++e) {
            const size_t at = size_t(directory) + size_t(e) * 20;
            if (at + 20 > header.size()) break;
            const uint32_t chunk = u32(header, at + 12);
            const size_t chunk_record = size_t(chunks) + size_t(chunk) * 28;
            if (chunk_record + 4 > header.size()) continue;
            PackEntry entry;
            entry.name = string_at(header, u32(header, at));
            entry.asset_path = string_at(header, u32(header, at + 4));
            entry.size = u32(header, at + 8);
            entry.offset = uint64_t(u32(header, chunk_record)) + u32(header, at + 16);
            pack.entries.push_back(std::move(entry));
        }
    }
    return true;
}

bool read_bytes(const fs::path& file, uint64_t offset, uint32_t size, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { error = "cannot open file"; return false; }
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) { error = "cannot seek"; return false; }
    out.resize(size);
    if (size) in.read(reinterpret_cast<char*>(out.data()), size);
    if (in.gcount() != static_cast<std::streamsize>(size)) { error = "short read"; return false; }
    return true;
}

bool read_bytes(const fs::path& file, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) { error = "cannot open file"; return false; }
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > 512ull * 1024 * 1024) { error = "file too large"; return false; }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size) in.read(reinterpret_cast<char*>(out.data()), size);
    if (in.gcount() != size) { error = "short read"; return false; }
    return true;
}

bool write_bytes(const fs::path& file, const std::vector<uint8_t>& bytes, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot create file"; return false; }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) { error = "cannot write file"; return false; }
    return true;
}

std::string asset_stem(const std::string& asset_path, const std::string& fallback) {
    std::string text = asset_path;
    std::replace(text.begin(), text.end(), '\\', '/');
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('/', start);
        std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty() && part != "." && part != "..") {
            if (part.size() == 2 && part[1] == ':') {  // drive letter
                start = end == std::string::npos ? text.size() + 1 : end + 1;
                continue;
            }
            std::replace(part.begin(), part.end(), ':', '_');
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (parts.size() > 1 && lowercase(parts.front()) == "asset_temp") parts.erase(parts.begin());
    if (parts.empty()) parts.push_back(fallback.empty() ? "asset" : fallback);
    std::string last = parts.back();
    if (const size_t dot = last.find_last_of('.'); dot != std::string::npos) last.resize(dot);
    if (last.empty()) last = "asset";
    parts.back() = last;
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty()) out += '/';
        out += part;
    }
    return out;
}

std::vector<Pack> find_packs(const fs::path& root) {
    std::vector<Pack> packs;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec) || !ends_with_ci(it->path().extension().generic_string(), ".cod")) continue;
        Pack pack;
        std::string error;
        if (read_pack(it->path(), root, pack, error)) packs.push_back(std::move(pack));
    }
    return packs;
}

std::vector<fs::path> find_files(const fs::path& root, const std::vector<std::string>& extensions) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const std::string extension = lowercase(it->path().extension().generic_string());
        if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end()) files.push_back(it->path());
    }
    return files;
}

}  // namespace modding
