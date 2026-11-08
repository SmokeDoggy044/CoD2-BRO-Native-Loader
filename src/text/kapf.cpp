// See kapf.h.

#include "kapf.h"

#include <cstring>

#include "file.h"
#include "log.h"

namespace text {

namespace {

uint32_t u32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    uint32_t v;
    std::memcpy(&v, data.data() + offset, 4);
    return v;
}

uint16_t u16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    uint16_t v;
    std::memcpy(&v, data.data() + offset, 2);
    return v;
}

}  // namespace

KapfPack::~KapfPack() = default;

std::unique_ptr<KapfPack> KapfPack::open(const std::filesystem::path& path) {
    std::error_code error;
    auto file = platform::File::open(path, platform::File::Access::Read, false, error);
    if (!file) return nullptr;
    // The tables come before the first chunk (0x8000 in every pack of the game).
    const uint32_t kHeaderSize = 0x8000;
    std::vector<uint8_t> header(kHeaderSize);
    uint32_t done = 0;
    if (!file->read_at(0, header.data(), kHeaderSize, done, error) || done < 0x40 ||
        std::memcmp(header.data(), "KAPF", 4) != 0) {
        LOG_WARN("text", "%s is not a game pack", path.string().c_str());
        return nullptr;
    }
    header.resize(done);
    auto string_at = [&](uint32_t offset) {
        std::string s;
        while (offset < header.size() && header[offset]) s += static_cast<char>(header[offset++]);
        return s;
    };

    std::unique_ptr<KapfPack> pack(new KapfPack);
    const uint32_t sections = u32(header, 0x0c);
    for (uint32_t i = 0; i < sections && i < 16; ++i) {
        const size_t record = 0x28 + i * 24;
        const uint16_t count = u16(header, record + 2);
        const uint32_t chunks = u32(header, record + 4), directory = u32(header, record + 8);
        for (uint32_t e = 0; e < count; ++e) {
            const size_t at = directory + e * 20;
            if (at + 20 > header.size()) break;
            const uint32_t chunk = u32(header, at + 12);
            const size_t chunk_record = chunks + static_cast<size_t>(chunk) * 28;
            if (chunk_record + 4 > header.size()) continue;
            pack->entries_.push_back({string_at(u32(header, at)),
                                      static_cast<uint64_t>(u32(header, chunk_record)) + u32(header, at + 16),
                                      u32(header, at + 8)});
        }
    }
    pack->file_ = std::move(file);
    return pack;
}

const KapfPack::Entry* KapfPack::find(const std::string& name) const {
    for (const Entry& entry : entries_)
        if (entry.name == name) return &entry;
    return nullptr;
}

std::optional<std::vector<uint8_t>> KapfPack::read(const Entry& entry) const {
    std::vector<uint8_t> data(entry.size);
    uint32_t done = 0;
    std::error_code error;
    if (!file_->read_at(entry.offset, data.data(), entry.size, done, error) || done != entry.size) return std::nullopt;
    return data;
}

}  // namespace text
